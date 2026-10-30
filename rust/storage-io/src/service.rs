// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use crate::BlockRegion;
use file_io::{AlignedBuffer, Backend, Direction, Operation};
use std::collections::{BTreeMap, VecDeque};
use std::fs::File;
use std::io;
use std::num::NonZeroUsize;
use std::ops::Range;
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicU8, AtomicUsize, Ordering};
use std::sync::{mpsc, Arc, Condvar, Mutex, Weak};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

mod admission;
pub mod benchmark;
use benchmark::BenchmarkIngress;
pub use benchmark::{
    BeginBenchmark, BenchmarkError, BenchmarkLimits, BenchmarkMetadata, BenchmarkPreparation,
    BenchmarkReport, BenchmarkRun, BenchmarkShape, BenchmarkStatus, BenchmarkTarget,
    CheckedWorkloads, NativeFinalPolicy, OperationPolicy, SamplePolicy, WaitPolicy, Workload,
    WorkloadMeasurement,
};
mod engine;
pub(crate) mod health;
mod scheduler;
pub(crate) use admission::Admission;
use admission::{Charge, ControlCell, Ledger, Wake};
use engine::{Engine, Incoming, Management, Retirement};
use health::{Diagnosis, DiagnosisStage, HealthIngress, ProbePass};
pub use health::{
    FinalFacts, FinalObserver, HealthConfig, HealthSnapshot, ObservationCounters,
    ObservationOutcome, ObservationReceipt, RequestLifetime, HEALTH_ACTIVE_CAPACITY,
    HEALTH_QUEUE_CAPACITY,
};

macro_rules! identifier {
    ($name:ident) => {
        #[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
        pub struct $name(pub u64);
        impl $name {
            pub fn get(self) -> u64 {
                self.0
            }
        }
    };
}
identifier!(RequestId);
identifier!(DeviceId);
identifier!(GroupId);
identifier!(Generation);
static NEXT_ID: AtomicU64 = AtomicU64::new(1);

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub enum RequestClass {
    Normal,
    Probe,
}
impl RequestClass {
    fn index(self) -> usize {
        match self {
            Self::Normal => 0,
            Self::Probe => 1,
        }
    }
}
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub enum Execution {
    WorkerRequired,
    PreferNative,
}
#[derive(Clone, Copy, Debug)]
pub enum CompletionRequirement {
    RequestedWindow,
    FullBuffer,
}
#[derive(Clone, Copy, Debug)]
pub struct RequestPolicy {
    pub deadline: Option<Instant>,
    pub retry_alignment: Option<NonZeroUsize>,
    pub max_error_retries: usize,
    pub completion: CompletionRequirement,
}
impl Default for RequestPolicy {
    fn default() -> Self {
        Self {
            deadline: None,
            retry_alignment: None,
            max_error_retries: 0,
            completion: CompletionRequirement::RequestedWindow,
        }
    }
}

#[derive(Clone)]
pub enum Target {
    File {
        file: Arc<File>,
        offset: u64,
    },
    Block(BlockRegion),
    Named {
        file: Arc<crate::NamedOpenedFile>,
        offset: u64,
    },
}
pub struct RequestSpec {
    pub device: DeviceId,
    pub group: GroupId,
    pub class: RequestClass,
    pub execution: Execution,
    pub rate_exempt: bool,
    pub target: Target,
    pub length: usize,
    pub alignment: usize,
    pub window: Range<usize>,
    pub policy: RequestPolicy,
    pub tag: u64,
}
impl RequestSpec {
    pub fn new(device: DeviceId, group: GroupId, target: Target, length: usize) -> Self {
        Self {
            device,
            group,
            class: RequestClass::Normal,
            execution: Execution::PreferNative,
            target,
            length,
            rate_exempt: false,
            alignment: 4096,
            window: 0..length,
            policy: RequestPolicy::default(),
            tag: 0,
        }
    }
    fn split(self) -> (RequestOptions, Target) {
        (
            RequestOptions {
                device: self.device,
                group: self.group,
                class: self.class,
                execution: self.execution,
                rate_exempt: self.rate_exempt,
                length: self.length,
                alignment: self.alignment,
                window: self.window,
                policy: self.policy,
                tag: self.tag,
            },
            self.target,
        )
    }
}

#[derive(Clone, Debug)]
pub(crate) struct RequestOptions {
    pub device: DeviceId,
    pub group: GroupId,
    pub class: RequestClass,
    pub execution: Execution,
    pub rate_exempt: bool,
    pub length: usize,
    pub alignment: usize,
    pub window: Range<usize>,
    pub policy: RequestPolicy,
    pub tag: u64,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct AdmissionLimits {
    pub max_records: usize,
    pub max_bytes: usize,
    pub probe_records: usize,
    /// In Config, the Probe floor; effective_admission() raises it for device requirements.
    pub probe_bytes: usize,
}
impl AdmissionLimits {
    fn records(self, class: RequestClass) -> usize {
        match class {
            RequestClass::Normal => self.max_records - self.probe_records,
            RequestClass::Probe => self.probe_records,
        }
    }
    fn bytes(self, class: RequestClass) -> usize {
        match class {
            RequestClass::Normal => self.max_bytes - self.probe_bytes,
            RequestClass::Probe => self.probe_bytes,
        }
    }
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DeviceLimits {
    pub depth: usize,
    pub probe_depth: usize,
    pub max_probe_bytes: usize,
}
impl DeviceLimits {
    fn capacity(self, class: RequestClass) -> usize {
        match class {
            RequestClass::Normal => self.depth - self.probe_depth,
            RequestClass::Probe => self.probe_depth,
        }
    }
    pub fn cost(self, class: RequestClass, length: usize) -> DepthCost {
        let raw = length.div_ceil(512 * 1024).max(1);
        DepthCost {
            raw,
            effective: raw.min(self.capacity(class)),
        }
    }
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DepthCost {
    pub raw: usize,
    pub effective: usize,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Rate {
    Unlimited,
    PerSecond(u64),
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ExcessShare {
    None,
    Weighted(std::num::NonZeroU32),
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct GroupLimits {
    pub minimum_iops: u64,
    pub maximum_iops: Rate,
    pub excess: ExcessShare,
    pub burst: u64,
}
impl Default for GroupLimits {
    fn default() -> Self {
        Self {
            minimum_iops: 0,
            maximum_iops: Rate::Unlimited,
            excess: ExcessShare::Weighted(std::num::NonZeroU32::new(1).unwrap()),
            burst: 1,
        }
    }
}
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Config {
    pub benchmark: BenchmarkLimits,
    pub health: HealthConfig,
    pub admission: AdmissionLimits,
    pub devices: BTreeMap<DeviceId, DeviceLimits>,
    pub groups: BTreeMap<GroupId, GroupLimits>,
    pub default_group: GroupId,
    pub group_capacity_iops: Rate,
}
impl Config {
    pub fn effective_admission(&self) -> AdmissionLimits {
        let mut limits = self.admission;
        for device in self.devices.values() {
            limits.probe_bytes = limits.probe_bytes.max(device.max_probe_bytes);
        }
        limits
    }
    fn validate(&self) -> io::Result<()> {
        self.health.validate()?;
        self.benchmark.validate()?;
        let a = self.effective_admission();
        if a.probe_records == 0
            || a.probe_records >= a.max_records
            || self.admission.probe_bytes == 0
            || a.probe_bytes >= a.max_bytes
            || !self.groups.contains_key(&self.default_group)
        {
            return Err(invalid(
                "invalid server resource partitions or default group",
            ));
        }
        for d in self.devices.values() {
            if d.probe_depth == 0
                || d.probe_depth >= d.depth
                || d.max_probe_bytes == 0
                || d.max_probe_bytes > a.probe_bytes
            {
                return Err(invalid("invalid device partitions or Probe byte reserve"));
            }
        }
        let mut minimum = 0u64;
        for g in self.groups.values() {
            minimum = minimum
                .checked_add(g.minimum_iops)
                .ok_or_else(|| invalid("minimum rates overflow"))?;
            if g.burst == 0 || matches!(g.maximum_iops, Rate::PerSecond(n) if n < g.minimum_iops) {
                return Err(invalid("invalid group rate, weight or burst"));
            }
        }
        if matches!(self.group_capacity_iops, Rate::PerSecond(n) if minimum > n) {
            return Err(invalid("infeasible aggregate minimum rates"));
        }
        Ok(())
    }
}

pub struct DeviceConfig {
    pub id: DeviceId,
    pub worker: Backend,
    pub native: Option<Backend>,
}
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct ResourceUsage {
    pub records: usize,
    pub bytes: usize,
}
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DeviceSnapshot {
    pub id: DeviceId,
    pub physical_depth: [usize; 2],
    pub depth_debt: [usize; 2],
    pub backend_envelopes: Vec<[usize; 2]>,
    pub closed: bool,
}
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct GroupSnapshot {
    pub id: GroupId,
    pub physical_attempts: u64,
    pub rate_exempt_attempts: u64,
    pub rate_throttled: bool,
    pub device_blocked: bool,
}
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ServiceSnapshot {
    pub benchmark: BenchmarkMetadata,
    pub health: HealthSnapshot,
    pub generation: Generation,
    pub config: Config,
    pub admitted_requests: usize,
    pub reserved_bytes: usize,
    pub usage: [ResourceUsage; 2],
    pub debt: [ResourceUsage; 2],
    pub queued_requests: usize,
    pub awaiting_retirement: usize,
    pub probe_attempts: u64,
    pub devices: Vec<DeviceSnapshot>,
    pub groups: Vec<GroupSnapshot>,
}
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ConfigReceipt {
    pub generation: Generation,
    pub debt: [ResourceUsage; 2],
    pub physical_debt: bool,
}
#[derive(Debug)]
pub enum ConfigError {
    Invalid(io::Error),
    Stale(Generation),
    Unavailable(io::Error),
}

#[derive(Debug)]
pub enum CompletionStatus {
    Complete,
    ShortIo,
    IncompleteEnvelope,
    IoError(io::Error),
    Cancelled,
    TimedOut,
    Shutdown,
}
pub struct Completion {
    pub id: RequestId,
    pub tag: u64,
    pub status: CompletionStatus,
    pub transferred: usize,
    pub depth_cost: DepthCost,
    pub physical_attempts: u64,
    pub group: GroupId,
    operation: Option<Operation>,
    window: Range<usize>,
    _charge: Arc<Charge>,
}
impl Completion {
    pub fn buffer(&self) -> Option<&[u8]> {
        self.operation.as_ref().map(|o| o.buffer().as_slice())
    }
    pub fn data(&self) -> Option<&[u8]> {
        let buffer = self.buffer()?;
        let end = self.window.end.min(self.transferred);
        Some(&buffer[self.window.start.min(end)..end])
    }
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CancelOutcome {
    Cancelled,
    NotPending,
}
pub struct RequestControl {
    cell: Arc<ControlCell>,
    armed: bool,
}
impl RequestControl {
    pub fn id(&self) -> RequestId {
        self.cell.id
    }
    pub fn request_cancel(&self) -> CancelOutcome {
        self.cell.cancel()
    }
}
impl Drop for RequestControl {
    fn drop(&mut self) {
        if self.armed {
            self.cell.cancel();
        }
    }
}
pub struct Waiter {
    lifetime: RequestLifetime,
    control: Option<RequestControl>,
    completed: mpsc::Receiver<Completion>,
    id: RequestId,
}
impl Waiter {
    pub fn take_final_observer(&mut self) -> Option<FinalObserver> {
        self.lifetime.take_final_observer()
    }
    pub fn id(&self) -> RequestId {
        self.id
    }
    pub fn request_cancel(&self) -> CancelOutcome {
        self.control
            .as_ref()
            .map_or(CancelOutcome::NotPending, RequestControl::request_cancel)
    }
    pub fn wait_until(&mut self, deadline: Instant) -> io::Result<Option<Completion>> {
        let result = receive(&self.completed, deadline)?;
        if result.is_some() {
            if let Some(mut control) = self.control.take() {
                control.armed = false;
            }
        }
        Ok(result)
    }
    pub fn wait(&mut self, deadline: Instant) -> io::Result<Completion> {
        self.wait_until(deadline)?
            .ok_or_else(|| io::ErrorKind::TimedOut.into())
    }
}

enum Removal {
    Close,
    Transaction {
        expected: Generation,
        reply: mpsc::Sender<Result<Generation, ConfigError>>,
        close_requested: bool,
    },
}
struct Gate {
    accepting: bool,
    devices: BTreeMap<DeviceId, bool>,
    removals: BTreeMap<DeviceId, Removal>,
    config: Arc<Config>,
    generation: Generation,
    waiting: [BTreeMap<RequestId, Arc<ControlCell>>; 2],
    published: VecDeque<Incoming>,
}
struct Shared {
    benchmark: Arc<BenchmarkIngress>,
    health: Arc<HealthIngress>,
    calibration: Arc<crate::calibration::Calibration>,
    gate: Mutex<Gate>,
    ledger: Arc<Mutex<Ledger>>,
    wake: Arc<Wake>,
    management: mpsc::SyncSender<Management>,
    retirement: mpsc::Sender<Retirement>,
}
pub struct Service {
    shared: Arc<Shared>,
    join: Option<JoinHandle<io::Result<()>>>,
}
impl Service {
    pub fn calibration(&self) -> &Arc<crate::calibration::Calibration> {
        &self.shared.calibration
    }
    pub fn start(config: Config) -> io::Result<Self> {
        config.validate()?;
        let (management, commands) = mpsc::sync_channel(32);
        let (retirement, retired) = mpsc::channel();
        let config = Arc::new(config);
        let wake = Arc::new(Wake::default());
        let ledger = Arc::new(Mutex::new(Ledger {
            limits: config.effective_admission(),
            usage: [ResourceUsage::default(); 2],
            health_payload_bytes: 0,
            benchmark: BenchmarkMetadata::default(),
        }));
        let health = Arc::new(HealthIngress::new(
            Arc::clone(&wake),
            Arc::clone(&ledger),
            config.health,
        )?);
        let shared = Arc::new(Shared {
            benchmark: Arc::new(BenchmarkIngress::new(Arc::clone(&wake))),
            health,
            calibration: Arc::new(crate::calibration::Calibration::default()),
            ledger,
            wake,
            gate: Mutex::new(Gate {
                accepting: true,
                devices: BTreeMap::new(),
                removals: BTreeMap::new(),
                config,
                generation: Generation(0),
                waiting: std::array::from_fn(|_| BTreeMap::new()),
                published: VecDeque::new(),
            }),
            management,
            retirement,
        });
        let actor = Arc::clone(&shared);
        let join = thread::Builder::new()
            .name("storage-io".into())
            .spawn(move || Engine::new(actor).run(commands, retired))?;
        Ok(Self {
            shared,
            join: Some(join),
        })
    }
    pub fn register_device(&self, device: DeviceConfig) -> io::Result<()> {
        let (reply, result) = mpsc::channel();
        self.command(Management::Register(device, reply))?;
        result.recv().map_err(|_| io::ErrorKind::BrokenPipe)?
    }
    /// Publishes the device and its complete policy in one actor transition.
    /// Ordinary rejection leaves both unchanged. Actor termination after commit
    /// but before replying can produce an ambiguous transport error.
    pub fn register_device_with_config(
        &self,
        expected: Generation,
        config: Config,
        device: DeviceConfig,
    ) -> Result<ConfigReceipt, ConfigError> {
        config.validate().map_err(ConfigError::Invalid)?;
        let (reply, result) = mpsc::channel();
        self.command(Management::RegisterWithConfig(
            expected, config, device, reply,
        ))
        .map_err(ConfigError::Unavailable)?;
        result
            .recv()
            .map_err(|_| ConfigError::Unavailable(io::ErrorKind::BrokenPipe.into()))?
    }
    /// Closes admission immediately. The actor prunes policy asynchronously;
    /// a subsequent snapshot observes the committed removal.
    pub fn remove_device(&self, device: DeviceId) -> io::Result<()> {
        let mut gate = self.shared.gate.lock().unwrap();
        let accepting = gate
            .devices
            .get_mut(&device)
            .ok_or(io::ErrorKind::NotFound)?;
        *accepting = false;
        if !gate.accepting || !gate.config.devices.contains_key(&device) {
            return Ok(());
        }
        gate.removals
            .entry(device)
            .and_modify(|removal| {
                if let Removal::Transaction {
                    close_requested, ..
                } = removal
                {
                    *close_requested = true;
                }
            })
            .or_insert(Removal::Close);
        drop(gate);
        self.shared.wake.notify();
        Ok(())
    }
    /// Atomically closes admission and prunes policy without waiting for physical retirement.
    /// Repeating a committed removal does not advance the configuration generation.
    /// Actor loss after commit but before reply can produce an ambiguous transport error.
    pub fn remove_device_at(
        &self,
        expected: Generation,
        device: DeviceId,
    ) -> Result<Generation, ConfigError> {
        let (reply, result) = mpsc::channel();
        let mut gate = self.shared.gate.lock().unwrap();
        if !gate.accepting {
            return Err(ConfigError::Unavailable(io::ErrorKind::BrokenPipe.into()));
        }
        if expected != gate.generation {
            return Err(ConfigError::Stale(gate.generation));
        }
        if !gate.devices.contains_key(&device) {
            return Err(ConfigError::Invalid(io::ErrorKind::NotFound.into()));
        }
        if !gate.config.devices.contains_key(&device) {
            return Ok(gate.generation);
        }
        if gate.removals.contains_key(&device) {
            return Err(ConfigError::Unavailable(io::ErrorKind::WouldBlock.into()));
        }
        gate.removals.insert(
            device,
            Removal::Transaction {
                expected,
                reply,
                close_requested: false,
            },
        );
        drop(gate);
        self.shared.wake.notify();
        result
            .recv()
            .map_err(|_| ConfigError::Unavailable(io::ErrorKind::BrokenPipe.into()))?
    }
    pub fn read(&self, spec: RequestSpec) -> io::Result<Waiter> {
        self.submit(spec, Direction::Read, &[])
    }
    pub fn write(&self, spec: RequestSpec, bytes: &[u8]) -> io::Result<Waiter> {
        self.submit(spec, Direction::Write, bytes)
    }
    fn submit(&self, spec: RequestSpec, direction: Direction, bytes: &[u8]) -> io::Result<Waiter> {
        let (options, target) = spec.split();
        let mut admission = self.begin(options)?;
        let mut lifetime = admission.lifetime();
        let eligible =
            direction == Direction::Read && admission.cell.options.class == RequestClass::Normal;
        lifetime.capture(&admission, target, eligible);
        let control = admission.take_control()?;
        admission.wait_capacity()?;
        admission.prepare(
            lifetime.target.as_ref().unwrap().target.clone(),
            direction,
            bytes,
        )?;
        let (destination, completed) = mpsc::channel();
        let id = admission.publish(destination)?;
        Ok(Waiter {
            lifetime,
            control: Some(control),
            completed,
            id,
        })
    }
    pub(crate) fn begin(&self, options: RequestOptions) -> io::Result<Admission> {
        Admission::begin(Arc::clone(&self.shared), options)
    }
    fn command(&self, command: Management) -> io::Result<()> {
        let gate = self.shared.gate.lock().unwrap();
        if !gate.accepting {
            return Err(io::ErrorKind::BrokenPipe.into());
        }
        self.shared
            .management
            .try_send(command)
            .map_err(|e| match e {
                mpsc::TrySendError::Full(_) => io::ErrorKind::WouldBlock,
                mpsc::TrySendError::Disconnected(_) => io::ErrorKind::BrokenPipe,
            })?;
        self.shared.wake.notify();
        Ok(())
    }
    pub fn reconfigure(
        &self,
        expected: Generation,
        config: Config,
    ) -> Result<ConfigReceipt, ConfigError> {
        config.validate().map_err(ConfigError::Invalid)?;
        let (reply, result) = mpsc::channel();
        self.command(Management::Reconfigure(expected, config, reply))
            .map_err(ConfigError::Unavailable)?;
        result
            .recv()
            .map_err(|_| ConfigError::Unavailable(io::ErrorKind::BrokenPipe.into()))?
    }
    pub fn snapshot(&self) -> io::Result<ServiceSnapshot> {
        let (reply, result) = mpsc::channel();
        self.command(Management::Snapshot(reply))?;
        result.recv().map_err(|_| io::ErrorKind::BrokenPipe.into())
    }
    pub fn health_status(&self) -> HealthSnapshot {
        self.shared.health.snapshot(true, Instant::now())
    }
    pub fn stop(&self) {
        self.shared.gate.lock().unwrap().accepting = false;
        self.shared.health.close();
        self.shared.benchmark.close();
        self.shared.wake.notify();
    }
    pub fn shutdown(mut self) -> io::Result<()> {
        self.stop();
        self.join
            .take()
            .unwrap()
            .join()
            .unwrap_or_else(|_| Err(io::Error::other("storage actor panicked")))
    }
}
impl Drop for Service {
    fn drop(&mut self) {
        self.stop();
    }
}
fn invalid(message: &'static str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidInput, message)
}
fn receive<T>(receiver: &mpsc::Receiver<T>, deadline: Instant) -> io::Result<Option<T>> {
    match receiver.recv_timeout(deadline.saturating_duration_since(Instant::now())) {
        Ok(value) => Ok(Some(value)),
        Err(mpsc::RecvTimeoutError::Timeout) => Ok(None),
        Err(mpsc::RecvTimeoutError::Disconnected) => Err(io::ErrorKind::BrokenPipe.into()),
    }
}
#[cfg(test)]
mod tests;
