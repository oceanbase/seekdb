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
use std::sync::{mpsc, Arc, Mutex, Weak};

use std::time::{Duration, Instant};

mod admission;
mod engine;
pub(crate) mod health;
pub(crate) use admission::Admission;
use admission::{Charge, ControlCell, Ledger, Wake};
pub(crate) use engine::Destination;
use engine::{Driver, Incoming};
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
    pub class: RequestClass,
    pub execution: Execution,
    pub target: Target,
    pub length: usize,
    pub alignment: usize,
    pub window: Range<usize>,
    pub policy: RequestPolicy,
    pub tag: u64,
}
impl RequestSpec {
    pub fn new(device: DeviceId, target: Target, length: usize) -> Self {
        Self {
            device,
            class: RequestClass::Normal,
            execution: Execution::PreferNative,
            target,
            length,
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
                class: self.class,
                execution: self.execution,
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
    pub class: RequestClass,
    pub execution: Execution,
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
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Config {
    pub health: HealthConfig,
    pub admission: AdmissionLimits,
    pub devices: BTreeMap<DeviceId, DeviceLimits>,
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
        let a = self.effective_admission();
        if a.probe_records == 0
            || a.probe_records >= a.max_records
            || self.admission.probe_bytes == 0
            || a.probe_bytes >= a.max_bytes
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
pub struct ServiceSnapshot {
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
    shared: Arc<Shared>,
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
        let result = loop {
            if self
                .control
                .as_ref()
                .is_some_and(|control| control.cell.cancelled.load(Ordering::Acquire))
            {
                if let Some(driver) = self.shared.driver.lock().unwrap().upgrade() {
                    driver
                        .lock()
                        .unwrap()
                        .progress(self.control.as_ref().map(|c| c.cell.options.device));
                }
                drop(self.completed.try_recv());
                return Err(io::ErrorKind::Interrupted.into());
            }
            match self.completed.try_recv() {
                Ok(result) => break Some(result),
                Err(mpsc::TryRecvError::Disconnected) => {
                    return Err(io::ErrorKind::BrokenPipe.into())
                }
                Err(mpsc::TryRecvError::Empty) => (),
            }
            let epoch = self.shared.wake.epoch();
            let driver = self
                .shared
                .driver
                .lock()
                .unwrap()
                .upgrade()
                .ok_or(io::ErrorKind::BrokenPipe)?;
            let mut driver = driver.lock().unwrap();
            let busy = driver.progress(self.control.as_ref().map(|c| c.cell.options.device));
            let health_delay = driver.health_delay(Instant::now());
            drop(driver);
            if self
                .control
                .as_ref()
                .is_some_and(|control| control.cell.cancelled.load(Ordering::Acquire))
            {
                drop(self.completed.try_recv());
                return Err(io::ErrorKind::Interrupted.into());
            }
            match self.completed.try_recv() {
                Ok(result) => break Some(result),
                Err(mpsc::TryRecvError::Disconnected) => {
                    return Err(io::ErrorKind::BrokenPipe.into())
                }
                Err(mpsc::TryRecvError::Empty) => (),
            }
            if let Some(control) = &self.control {
                if control.cell.cancelled.load(Ordering::Acquire) {
                    return Err(io::ErrorKind::Interrupted.into());
                }
                if control
                    .cell
                    .options
                    .policy
                    .deadline
                    .is_some_and(|until| until <= Instant::now())
                {
                    return Err(io::ErrorKind::TimedOut.into());
                }
            }
            let now = Instant::now();
            if now >= deadline {
                break None;
            }
            let until = self
                .control
                .as_ref()
                .and_then(|c| c.cell.options.policy.deadline)
                .map_or(deadline, |until| until.min(deadline));
            let remaining = until.saturating_duration_since(now);
            let delay = health_delay.map_or(remaining, |delay| delay.min(remaining));
            if !busy {
                self.shared.wake.wait(epoch, Some(delay));
            }
        };
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

struct Gate {
    accepting: bool,
    devices: BTreeMap<DeviceId, Arc<AtomicBool>>,
    config: Arc<Config>,
    generation: Generation,
    waiting: [BTreeMap<RequestId, Arc<ControlCell>>; 2],
    pending_probes: VecDeque<Incoming>,
}
struct Shared {
    health: Arc<HealthIngress>,
    gate: Mutex<Gate>,
    ledger: Arc<Mutex<Ledger>>,
    wake: Arc<Wake>,
    cancellations: Arc<Mutex<VecDeque<admission::Cancellation>>>,
    driver: Mutex<Weak<Mutex<Driver>>>,
}
pub struct Service {
    shared: Arc<Shared>,
    driver: Arc<Mutex<Driver>>,
}
impl Service {
    pub fn start(config: Config) -> io::Result<Self> {
        config.validate()?;
        let config = Arc::new(config);
        let wake = Arc::new(Wake::default());
        let ledger = Arc::new(Mutex::new(Ledger {
            limits: config.effective_admission(),
            usage: [ResourceUsage::default(); 2],
            health_payload_bytes: 0,
        }));
        let health = Arc::new(HealthIngress::new(
            Arc::clone(&wake),
            Arc::clone(&ledger),
            config.health,
        )?);
        let mut cancellations = VecDeque::new();
        cancellations
            .try_reserve(config.admission.max_records)
            .map_err(io::Error::other)?;
        let shared = Arc::new(Shared {
            cancellations: Arc::new(Mutex::new(cancellations)),
            health,
            ledger,
            wake,
            gate: Mutex::new(Gate {
                accepting: true,
                devices: BTreeMap::new(),
                config,
                generation: Generation(0),
                waiting: std::array::from_fn(|_| BTreeMap::new()),
                pending_probes: VecDeque::new(),
            }),
            driver: Mutex::new(Weak::new()),
        });
        let driver = Arc::new(Mutex::new(Driver::new(Arc::clone(&shared))));
        *shared.driver.lock().unwrap() = Arc::downgrade(&driver);
        Ok(Self { shared, driver })
    }
    pub fn register_device(&self, device: DeviceConfig) -> io::Result<()> {
        self.driver.lock().unwrap().register(device)
    }
    pub(crate) fn register_device_with_config(
        &self,
        expected: Generation,
        config: Config,
        device: DeviceConfig,
    ) -> Result<ConfigReceipt, ConfigError> {
        self.driver
            .lock()
            .unwrap()
            .register_with_config(expected, config, device, Instant::now())
    }
    pub fn remove_device(&self, device: DeviceId) -> io::Result<()> {
        let generation = self.shared.gate.lock().unwrap().generation;
        self.remove_device_at(generation, device)
            .map(|_| ())
            .map_err(|e| match e {
                ConfigError::Invalid(e) | ConfigError::Unavailable(e) => e,
                ConfigError::Stale(_) => io::ErrorKind::WouldBlock.into(),
            })
    }
    pub(crate) fn remove_device_at(
        &self,
        expected: Generation,
        device: DeviceId,
    ) -> Result<Generation, ConfigError> {
        self.driver
            .lock()
            .unwrap()
            .remove_config(expected, device, Instant::now())
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
        let id = admission.publish(engine::Destination::Caller(destination))?;
        Ok(Waiter {
            lifetime,
            control: Some(control),
            completed,
            id,
            shared: Arc::clone(&self.shared),
        })
    }
    pub(crate) fn begin(&self, options: RequestOptions) -> io::Result<Admission> {
        match Admission::begin(Arc::clone(&self.shared), options.clone()) {
            Err(error) if error.kind() == io::ErrorKind::WouldBlock => {
                self.driver.lock().unwrap().progress(None);
                Admission::begin(Arc::clone(&self.shared), options)
            }
            result => result,
        }
    }
    pub(crate) fn reconfigure(
        &self,
        expected: Generation,
        config: Config,
    ) -> Result<ConfigReceipt, ConfigError> {
        self.driver
            .lock()
            .unwrap()
            .reconfigure(expected, config, Instant::now())
    }
    pub fn snapshot(&self) -> io::Result<ServiceSnapshot> {
        Ok(self.driver.lock().unwrap().snapshot())
    }
    pub(crate) fn device_drained(&self, device: DeviceId) -> bool {
        self.driver.lock().unwrap().device_drained(device)
    }
    pub(crate) fn poll_completion(
        &self,
        device: DeviceId,
        until: Instant,
    ) -> io::Result<Completion> {
        loop {
            let epoch = self.shared.wake.epoch();
            let mut driver = self.driver.lock().unwrap();
            let busy = driver.progress(Some(device));
            if let Some(completion) = driver.take_completion(device) {
                return Ok(completion);
            }
            let now = Instant::now();
            if now >= until {
                return Err(io::ErrorKind::WouldBlock.into());
            }
            let delay = driver
                .health_delay(now)
                .unwrap_or(until - now)
                .min(until - now);
            drop(driver);
            if !busy {
                self.shared.wake.wait(epoch, Some(delay));
            }
        }
    }
    pub fn health_status(&self) -> HealthSnapshot {
        self.shared.health.snapshot(true, Instant::now())
    }
    pub fn stop(&self) {
        self.shared.gate.lock().unwrap().accepting = false;
        self.shared.health.close();
        self.shared.wake.notify();
    }
    pub fn shutdown(self) -> io::Result<()> {
        self.stop();
        loop {
            let epoch = self.shared.wake.epoch();
            let mut driver = self.driver.lock().unwrap();
            let busy = driver.progress(None);
            if driver.quiescent() {
                return driver.shutdown_backends();
            }
            let delay = driver.health_delay(Instant::now());
            drop(driver);
            if !busy {
                self.shared.wake.wait(epoch, delay);
            }
        }
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
#[cfg(test)]
mod tests;
