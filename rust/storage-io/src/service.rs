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
use std::collections::{BTreeMap, HashMap, VecDeque};
use std::fs::File;
use std::io;
use std::num::NonZeroUsize;
use std::ops::Range;
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicU8, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex, Weak};

use std::time::{Duration, Instant};

mod admission;
mod delivery;
mod engine;
mod request;
pub(crate) use request::{CallbackOwner, Consumer};
pub use request::{PublishedCompletion, Request, RequestResult};
pub(crate) mod lifetime;
pub(crate) use admission::Admission;
use admission::{ControlCell, Ledger, Wake};
use engine::{Driver, Incoming};
pub(crate) use lifetime::RequestLifetime;

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
static NEXT_ID: AtomicU64 = AtomicU64::new(1);

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
    pub execution: Execution,
    pub target: Target,
    pub length: usize,
    pub alignment: usize,
    pub window: Range<usize>,
    pub policy: RequestPolicy,
    pub tag: u64,
    pub statistics_lane: Option<usize>,
}
impl RequestSpec {
    pub fn new(device: DeviceId, target: Target, length: usize) -> Self {
        Self {
            device,
            execution: Execution::PreferNative,
            target,
            length,
            alignment: 4096,
            window: 0..length,
            policy: RequestPolicy::default(),
            tag: 0,
            statistics_lane: None,
        }
    }
    fn split(self) -> (RequestOptions, Target) {
        (
            RequestOptions {
                device: self.device,
                execution: self.execution,
                length: self.length,
                alignment: self.alignment,
                window: self.window,
                policy: self.policy,
                tag: self.tag,
                statistics_lane: self.statistics_lane,
            },
            self.target,
        )
    }
}

#[derive(Clone, Debug)]
pub(crate) struct RequestOptions {
    pub device: DeviceId,
    pub execution: Execution,
    pub length: usize,
    pub alignment: usize,
    pub window: Range<usize>,
    pub policy: RequestPolicy,
    pub tag: u64,
    pub statistics_lane: Option<usize>,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct AdmissionLimits {
    pub max_records: usize,
    pub max_bytes: usize,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DeviceLimits {
    pub depth: usize,
}
impl DeviceLimits {
    pub fn cost(self, length: usize) -> DepthCost {
        let raw = length.div_ceil(512 * 1024).max(1);
        DepthCost {
            raw,
            effective: raw.min(self.depth),
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
    pub callback_threads: usize,
    pub statistics_lanes: usize,
    pub admission: AdmissionLimits,
    pub devices: BTreeMap<DeviceId, DeviceLimits>,
}
impl Config {
    fn validate(&self) -> io::Result<()> {
        if self.statistics_lanes == 0
            || self.callback_threads == 0
            || self.admission.max_records == 0
            || self.admission.max_bytes == 0
        {
            return Err(invalid("request and byte limits must be nonzero"));
        }
        if self.devices.values().any(|device| device.depth == 0) {
            return Err(invalid("device depth must be nonzero"));
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
    pub physical_depth: usize,
    pub depth_debt: usize,
    pub backend_envelopes: Vec<usize>,
    pub closed: bool,
}
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ServiceSnapshot {
    pub config: Config,
    pub admitted_requests: usize,
    pub reserved_bytes: usize,
    pub usage: ResourceUsage,
    pub debt: ResourceUsage,
    pub queued_requests: usize,
    pub awaiting_retirement: usize,
    pub devices: Vec<DeviceSnapshot>,
}
#[derive(Debug)]
pub enum ConfigError {
    Invalid(io::Error),
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
pub(crate) struct Completion {
    pub id: RequestId,
    pub tag: u64,
    pub status: CompletionStatus,
    pub transferred: usize,
    pub depth_cost: DepthCost,
    pub physical_attempts: u64,
    operation: Option<Operation>,
    window: Range<usize>,
    _cell: Option<Arc<ControlCell>>,
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

pub(crate) trait RequestCompletion: Send + Sync + 'static {
    fn complete(self: Box<Self>, completion: Completion);
    fn cancel(self: Box<Self>);
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CancelOutcome {
    Cancelled,
    NotPending,
}
pub(crate) struct RequestControl {
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
#[derive(Clone, Copy, Eq, PartialEq)]
enum ServicePhase {
    Prepared,
    Active,
    Stopped,
}

struct Gate {
    phase: ServicePhase,
    devices: BTreeMap<DeviceId, Arc<AtomicBool>>,
    config: Arc<Config>,
    waiting: BTreeMap<RequestId, Arc<ControlCell>>,
}

struct Shared {
    requests: Arc<Mutex<request::Registry>>,
    drain_waiters: Arc<AtomicUsize>,
    statistics: Arc<crate::statistics::CompletionStatistics>,
    executor: Arc<delivery::Executor>,
    accepting: Arc<AtomicBool>,
    // Driver writes this under its mutex before checking capacity.
    driver_has_work: Arc<AtomicBool>,
    gate: Mutex<Gate>,
    ledger: Arc<Mutex<Ledger>>,
    wake: Arc<Wake>,
    cancellations: Arc<Mutex<VecDeque<admission::Cancellation>>>,
    worker_routes: Mutex<BTreeMap<DeviceId, Arc<engine::WorkerRoute>>>,
    driver: Mutex<Weak<Mutex<Driver>>>,
}
enum ShutdownPhase {
    Open,
    Draining(std::thread::ThreadId),
    Done,
}

pub struct Service {
    shared: Arc<Shared>,
    driver: Arc<Mutex<Driver>>,
    progress_worker: Mutex<Option<ProgressWorker>>,
    shutdown_phase: Mutex<ShutdownPhase>,
    shutdown_changed: std::sync::Condvar,
    next_device_id: AtomicU64,
}
struct ProgressWorker {
    stopping: Arc<AtomicBool>,
    thread: std::thread::JoinHandle<()>,
}
impl Service {
    pub fn start(config: Config) -> io::Result<Self> {
        Self::start_in_phase(config, ServicePhase::Active)
    }
    pub(crate) fn prepare(config: Config) -> io::Result<Self> {
        Self::start_in_phase(config, ServicePhase::Prepared)
    }
    fn start_in_phase(config: Config, phase: ServicePhase) -> io::Result<Self> {
        config.validate()?;
        let config = Arc::new(config);
        let wake = Arc::new(Wake::default());
        let driver_has_work = Arc::new(AtomicBool::new(false));
        let ledger = Arc::new(Mutex::new(Ledger::new(
            config.admission,
            Arc::clone(&driver_has_work),
        )));
        let mut cancellations = VecDeque::new();
        cancellations
            .try_reserve(config.admission.max_records)
            .map_err(io::Error::other)?;
        let statistics = Arc::new(
            crate::statistics::CompletionStatistics::new(
                config.statistics_lanes,
                statistics_now_us(),
            )
            .map_err(|_| io::Error::from(io::ErrorKind::OutOfMemory))?,
        );
        let shared = Arc::new(Shared {
            requests: Arc::new(Mutex::new(request::Registry::default())),
            drain_waiters: Arc::new(AtomicUsize::new(0)),
            statistics,
            executor: delivery::Executor::new(),
            cancellations: Arc::new(Mutex::new(cancellations)),
            worker_routes: Mutex::new(BTreeMap::new()),
            accepting: Arc::new(AtomicBool::new(true)),
            driver_has_work,
            ledger,
            wake,
            gate: Mutex::new(Gate {
                phase,
                devices: BTreeMap::new(),
                config,
                waiting: BTreeMap::new(),
            }),
            driver: Mutex::new(Weak::new()),
        });
        let driver = Arc::new(Mutex::new(Driver::new(Arc::clone(&shared))));
        *shared.driver.lock().unwrap() = Arc::downgrade(&driver);
        Ok(Self {
            shared,
            driver,
            progress_worker: Mutex::new(None),
            shutdown_phase: Mutex::new(ShutdownPhase::Open),
            shutdown_changed: std::sync::Condvar::new(),
            next_device_id: AtomicU64::new(1),
        })
    }
    pub(crate) fn activate(&self) -> io::Result<()> {
        let mut gate = self.shared.gate.lock().unwrap();
        match gate.phase {
            ServicePhase::Prepared | ServicePhase::Active => {
                gate.phase = ServicePhase::Active;
                Ok(())
            }
            ServicePhase::Stopped => Err(io::ErrorKind::BrokenPipe.into()),
        }
    }
    pub(crate) fn allocate_device_id(&self) -> io::Result<DeviceId> {
        self.next_device_id
            .fetch_update(Ordering::Relaxed, Ordering::Relaxed, |id| id.checked_add(1))
            .map(DeviceId)
            .map_err(|_| io::Error::other("device identifiers exhausted"))
    }
    fn start_progress_worker(&self) -> io::Result<()> {
        let mut worker = self.progress_worker.lock().unwrap();
        if !self.shared.accepting.load(Ordering::Acquire) {
            return Err(io::ErrorKind::BrokenPipe.into());
        }
        if worker.is_some() {
            return Ok(());
        }
        let stopping = Arc::new(AtomicBool::new(false));
        let stop = Arc::clone(&stopping);
        let wake = Arc::clone(&self.shared.wake);
        let driver = Arc::downgrade(&self.driver);
        let shared = Arc::downgrade(&self.shared);
        let thread = std::thread::Builder::new()
            .name("io-progress".into())
            .spawn(move || {
                while !stop.load(Ordering::Acquire) {
                    let epoch = wake.epoch();
                    let Some((busy, _)) = Driver::progress_inline(&driver) else {
                        break;
                    };
                    let Some(shared) = shared.upgrade() else {
                        break;
                    };
                    let (expired, delay) =
                        shared.requests.lock().unwrap().deadlines(Instant::now());
                    drop(shared);
                    for request in expired {
                        request.expire();
                    }
                    if !busy && !stop.load(Ordering::Acquire) {
                        wake.wait(epoch, delay);
                    }
                }
            })?;
        *worker = Some(ProgressWorker { stopping, thread });
        Ok(())
    }
    fn stop_progress_worker(&self) -> io::Result<()> {
        let worker = self.progress_worker.lock().unwrap().take();
        if let Some(worker) = worker {
            worker.stopping.store(true, Ordering::Release);
            self.shared.wake.notify();
            if worker.thread.thread().id() != std::thread::current().id() {
                worker
                    .thread
                    .join()
                    .map_err(|_| io::Error::other("I/O progress worker panicked"))?;
            }
        }
        Ok(())
    }
    pub fn register_device(&self, device: DeviceConfig) -> io::Result<()> {
        self.driver.lock().unwrap().register(device)
    }

    pub(crate) fn register_device_with_limits(
        &self,
        device: DeviceConfig,
        limits: DeviceLimits,
    ) -> Result<(), ConfigError> {
        self.next_device_id
            .fetch_max(device.id.0.saturating_add(1), Ordering::Relaxed);
        let mut driver = self.driver.lock().unwrap();
        let mut config = {
            let gate = self.shared.gate.lock().unwrap();
            (*gate.config).clone()
        };
        if config.devices.insert(device.id, limits).is_some() {
            return Err(ConfigError::Invalid(io::ErrorKind::AlreadyExists.into()));
        }
        driver.register_with_config(config, device, Instant::now())
    }

    pub(crate) fn configure_resources(
        &self,
        max_bytes: usize,
        callback_threads: usize,
    ) -> Result<(), ConfigError> {
        let mut driver = self.driver.lock().unwrap();
        let mut config = {
            let gate = self.shared.gate.lock().unwrap();
            (*gate.config).clone()
        };
        config.admission.max_bytes = max_bytes;
        config.callback_threads = callback_threads;
        driver.reconfigure(config, Instant::now())
    }

    pub fn remove_device(&self, device: DeviceId) -> io::Result<()> {
        {
            let mut driver = self.driver.lock().unwrap();
            driver
                .remove_config(device, Instant::now())
                .or_else(|error| match error {
                    ConfigError::Unavailable(error) if error.kind() == io::ErrorKind::BrokenPipe => {
                        Ok(())
                    }
                    error => Err(error),
                })
                .map_err(|error| match error {
                    ConfigError::Invalid(error) | ConfigError::Unavailable(error) => error,
                })?;
        }
        self.cancel_requests(Some(device));
        Ok(())
    }
    pub fn read(&self, spec: RequestSpec) -> io::Result<Request> {
        self.submit(spec, Direction::Read, &[])
    }
    pub fn write(&self, spec: RequestSpec, bytes: &[u8]) -> io::Result<Request> {
        self.submit(spec, Direction::Write, bytes)
    }
    fn submit(&self, spec: RequestSpec, direction: Direction, bytes: &[u8]) -> io::Result<Request> {
        let (mut options, target) = spec.split();
        if options.statistics_lane.is_none() {
            options.statistics_lane = Some(if direction == Direction::Read { 0 } else { 1 });
        }
        self.submit_request(options, target, direction, bytes, None, None)
    }
    pub(crate) fn submit_request(
        &self,
        options: RequestOptions,
        target: Target,
        direction: Direction,
        bytes: &[u8],
        callback: Option<CallbackOwner>,
        fault: Option<file_io::WorkerFault>,
    ) -> io::Result<Request> {
        let began = Instant::now();
        if options
            .statistics_lane
            .is_some_and(|lane| lane >= self.shared.statistics.lane_count())
        {
            return Err(invalid("unknown statistics lane"));
        }
        self.start_progress_worker()?;
        if callback.is_some() {
            let config = Arc::clone(&self.shared.gate.lock().unwrap().config);
            self.shared.executor.prepare(config.callback_threads, true)?;
        }
        let mut admission = self.begin(options)?;
        let mut lifetime = admission.lifetime();
        lifetime.capture(&admission, target);
        admission.wait_capacity()?;
        admission.prepare(
            lifetime.target.as_ref().unwrap().target.clone(),
            direction,
            bytes,
        )?;
        if let Some(fault) = fault {
            admission.set_worker_fault(fault)?;
        }
        self.publish_request_at(admission, lifetime, callback, began)
    }
    pub(super) fn publish_request(
        &self,
        admission: Admission,
        lifetime: RequestLifetime,
        callback: Option<CallbackOwner>,
    ) -> io::Result<Request> {
        self.start_progress_worker()?;
        self.publish_request_at(admission, lifetime, callback, Instant::now())
    }
    fn publish_request_at(
        &self,
        mut admission: Admission,
        lifetime: RequestLifetime,
        callback: Option<CallbackOwner>,
        began: Instant,
    ) -> io::Result<Request> {
        let request = Request::accepted(&mut admission, lifetime, callback, began)?;
        admission.register_completion(request.publisher(Arc::clone(&self.shared.executor)))?;
        admission.publish()?;
        Ok(request)
    }
    pub(crate) fn begin(&self, options: RequestOptions) -> io::Result<Admission> {
        match Admission::begin(Arc::clone(&self.shared), options.clone()) {
            Err(error) if error.kind() == io::ErrorKind::WouldBlock => {
                Driver::progress_inline(&Arc::downgrade(&self.driver));
                Admission::begin(Arc::clone(&self.shared), options)
            }
            result => result,
        }
    }
    #[cfg(test)]
    pub(crate) fn reconfigure(
        &self,
        config: Config,
    ) -> Result<(), ConfigError> {
        self.driver
            .lock()
            .unwrap()
            .reconfigure(config, Instant::now())
    }
    pub(crate) fn statistics(&self) -> &crate::statistics::CompletionStatistics {
        &self.shared.statistics
    }
    pub(crate) fn statistics_owner(&self) -> Arc<crate::statistics::CompletionStatistics> {
        Arc::clone(&self.shared.statistics)
    }
    pub fn snapshot(&self) -> io::Result<ServiceSnapshot> {
        Ok(self.driver.lock().unwrap().snapshot())
    }
    pub(crate) fn device_drained(&self, device: DeviceId) -> bool {
        self.driver.lock().unwrap().device_drained(device)
            && !self.shared.requests.lock().unwrap().has_device(device)
    }

    fn cancel_requests(&self, device: Option<DeviceId>) {
        let requests: Vec<_> = self.shared.requests.lock().unwrap().states(device);
        for request in requests {
            request.stop();
        }
    }
    pub(crate) fn wait_device_drained(&self, device: DeviceId) -> io::Result<()> {
        struct DrainWait<'a>(&'a AtomicUsize);
        impl Drop for DrainWait<'_> {
            fn drop(&mut self) {
                self.0.fetch_sub(1, Ordering::AcqRel);
            }
        }
        self.shared.drain_waiters.fetch_add(1, Ordering::AcqRel);
        let _waiter = DrainWait(&self.shared.drain_waiters);
        if self.shared.executor.is_current_worker() {
            return Err(io::Error::new(
                io::ErrorKind::WouldBlock,
                "callback cannot drain its own service",
            ));
        }
        loop {
            let epoch = self.shared.wake.epoch();
            if self.device_drained(device) {
                return Ok(());
            }
            Driver::progress_inline(&Arc::downgrade(&self.driver));
            if self.device_drained(device) {
                return Ok(());
            }
            self.shared.wake.wait(epoch, None);
        }
    }
    pub fn stop(&self) {
        let mut gate = self.shared.gate.lock().unwrap();
        gate.phase = ServicePhase::Stopped;
        self.shared.accepting.store(false, Ordering::Release);
        drop(gate);
        self.cancel_requests(None);
        self.shared.wake.notify();
    }
    pub fn shutdown(&self) -> io::Result<()> {
        if self.shared.executor.is_current_worker() {
            return Err(io::Error::new(
                io::ErrorKind::WouldBlock,
                "callback cannot drain its own service",
            ));
        }
        let current = std::thread::current().id();
        let mut phase = self.shutdown_phase.lock().unwrap();
        loop {
            match *phase {
                ShutdownPhase::Done => return Ok(()),
                ShutdownPhase::Open => {
                    *phase = ShutdownPhase::Draining(current);
                    break;
                }
                ShutdownPhase::Draining(owner) if owner == current => {
                    return Err(io::Error::new(
                        io::ErrorKind::WouldBlock,
                        "shutdown cannot wait for itself",
                    ));
                }
                ShutdownPhase::Draining(_) => phase = self.shutdown_changed.wait(phase).unwrap(),
            }
        }
        drop(phase);
        let result = self.drain();
        *self.shutdown_phase.lock().unwrap() = if result.is_ok() {
            ShutdownPhase::Done
        } else {
            ShutdownPhase::Open
        };
        self.shutdown_changed.notify_all();
        result
    }
    fn drain(&self) -> io::Result<()> {
        self.stop();
        self.stop_progress_worker()?;
        let backends = loop {
            let epoch = self.shared.wake.epoch();
            let (busy, delay) = Driver::progress_inline(&Arc::downgrade(&self.driver))
                .expect("service owns its driver");
            let mut driver = self.driver.lock().unwrap();
            if driver.quiescent() {
                break driver.take_backends();
            }
            drop(driver);
            if !busy {
                self.shared.wake.wait(epoch, delay);
            }
        };
        let mut pending = backends;
        let mut result = Ok(());
        while !pending.is_empty() {
            let epoch = self.shared.wake.epoch();
            let mut retained = Vec::new();
            for backend in pending {
                match Arc::try_unwrap(backend) {
                    Ok(backend) => {
                        if let Err(error) = backend.shutdown() {
                            if result.is_ok() {
                                result = Err(error);
                            }
                        }
                    }
                    Err(backend) => retained.push(backend),
                }
            }
            pending = retained;
            if !pending.is_empty() {
                self.shared.wake.wait(epoch, None);
            }
        }
        self.shared.executor.shutdown()?;
        result
    }
}
impl Drop for Service {
    fn drop(&mut self) {
        self.stop();
        let _ = self.stop_progress_worker();
    }
}
fn invalid(message: &'static str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidInput, message)
}
#[cfg(test)]
mod tests;

fn statistics_now_us() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_micros()
        .min(i64::MAX as u128) as i64
}
