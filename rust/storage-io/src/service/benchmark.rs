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

use super::*;
use crate::calibration::{ByteCount, CalibrationCandidate, Mode, Sample};
use std::mem::size_of;
use std::time::{SystemTime, UNIX_EPOCH};

pub const MAX_TARGETS: usize = 10240;
pub const MAX_WORKLOADS: usize = 20;
pub const MAX_WORKERS: usize = 16;
pub const PRODUCTION_REGION_BYTES: usize = 2 * 1024 * 1024;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BenchmarkLimits {
    pub targets: usize,
    pub workloads: usize,
    pub workers: usize,
}
impl Default for BenchmarkLimits {
    fn default() -> Self {
        Self {
            targets: MAX_TARGETS,
            workloads: MAX_WORKLOADS,
            workers: MAX_WORKERS,
        }
    }
}
impl BenchmarkLimits {
    pub(super) fn validate(self) -> io::Result<()> {
        if self.targets > MAX_TARGETS
            || self.workloads > MAX_WORKLOADS
            || self.workers > MAX_WORKERS
        {
            return Err(invalid("benchmark count limits exceed fixed capacities"));
        }
        Ok(())
    }
    fn contains(self, shape: BenchmarkShape) -> bool {
        shape.target_count.get() <= self.targets
            && shape.workload_count.get() <= self.workloads
            && shape.concurrency.get() <= self.workers
    }
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BenchmarkShape {
    pub target_count: NonZeroUsize,
    pub workload_count: NonZeroUsize,
    pub concurrency: NonZeroUsize,
}
impl BenchmarkShape {
    pub fn production() -> Self {
        Self {
            target_count: NonZeroUsize::new(MAX_TARGETS).unwrap(),
            workload_count: NonZeroUsize::new(MAX_WORKLOADS).unwrap(),
            concurrency: NonZeroUsize::new(MAX_WORKERS).unwrap(),
        }
    }
}
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct BenchmarkMetadata {
    pub targets: usize,
    pub workloads: usize,
    pub workers: usize,
    pub run_reserved: bool,
    /// Reserved typed payload including actual vector capacity and retained-target allowances.
    /// Shared payload allowances may overlap. Excludes opaque allocation headers, allocator
    /// overhead, transitively retained owners and separately charged I/O buffers. Not heap or RSS.
    pub owned_payload_bytes: usize,
    pub target_debt: usize,
    pub workload_debt: usize,
    pub worker_debt: usize,
}
impl BenchmarkMetadata {
    pub(super) fn with_limits(mut self, limits: BenchmarkLimits) -> Self {
        self.target_debt = self.targets.saturating_sub(limits.targets);
        self.workload_debt = self.workloads.saturating_sub(limits.workloads);
        self.worker_debt = self.workers.saturating_sub(limits.workers);
        self
    }
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BenchmarkError {
    InvalidPlan,
    Memory,
    Cancelled,
    Unavailable,
    NoSuccess,
    Arithmetic,
    External(i32),
    Io {
        kind: io::ErrorKind,
        os_error: Option<i32>,
    },
}
impl From<io::Error> for BenchmarkError {
    fn from(error: io::Error) -> Self {
        match error.kind() {
            io::ErrorKind::InvalidInput => Self::InvalidPlan,
            io::ErrorKind::OutOfMemory => Self::Memory,
            io::ErrorKind::Interrupted => Self::Cancelled,
            io::ErrorKind::BrokenPipe | io::ErrorKind::NotConnected => Self::Unavailable,
            kind => Self::Io {
                kind,
                os_error: error.raw_os_error(),
            },
        }
    }
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BenchmarkStatus {
    NotStarted,
    Preparing {
        started_wall_us: i64,
    },
    Running {
        started_wall_us: i64,
        workload: usize,
        completed: usize,
        elapsed: Duration,
    },
    Draining {
        started_wall_us: i64,
        completed: usize,
        elapsed: Duration,
    },
    Finished {
        started_wall_us: i64,
        finished_wall_us: i64,
        completed: usize,
        elapsed: Duration,
        result: Result<u64, BenchmarkError>,
    },
}
impl BenchmarkStatus {
    pub fn is_terminal(self) -> bool {
        matches!(self, Self::Finished { .. })
    }
}
/// One started workload, sealed only after all of its children physically retire.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct WorkloadMeasurement {
    pub mode: Mode,
    pub size: NonZeroUsize,
    pub elapsed: Duration,
    pub successful_count: u64,
    pub successful_latency_us: u64,
}
/// A bounded snapshot of drained phases. Unstarted and still-draining phases are absent.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BenchmarkReport {
    measurements: [Option<WorkloadMeasurement>; MAX_WORKLOADS],
    len: usize,
}
impl BenchmarkReport {
    pub fn len(&self) -> usize {
        self.len
    }
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
    pub fn get(&self, index: usize) -> Option<WorkloadMeasurement> {
        self.measurements.get(index).copied().flatten()
    }
}
impl Default for BenchmarkReport {
    fn default() -> Self {
        Self {
            measurements: [None; MAX_WORKLOADS],
            len: 0,
        }
    }
}
struct RunSnapshot {
    status: BenchmarkStatus,
    report: BenchmarkReport,
}
#[cfg(feature = "ownership-test-support")]
pub(crate) mod proof;

struct RunCell {
    #[cfg(feature = "ownership-test-support")]
    proof: std::sync::OnceLock<Arc<proof::ProofState>>,
    id: u64,
    started_wall_us: i64,
    snapshot: Mutex<RunSnapshot>,
    changed: Condvar,
    stop_requested: AtomicBool,
    wake: Arc<Wake>,
}
impl RunCell {
    fn set(&self, status: BenchmarkStatus) {
        self.snapshot.lock().unwrap().status = status;
        self.changed.notify_all();
    }
}
#[derive(Clone)]
pub struct BenchmarkRun {
    cell: Arc<RunCell>,
}
impl BenchmarkRun {
    pub fn status(&self) -> BenchmarkStatus {
        self.cell.snapshot.lock().unwrap().status
    }
    pub fn report(&self) -> BenchmarkReport {
        self.cell.snapshot.lock().unwrap().report
    }
    pub fn request_stop(&self) {
        let snapshot = self.cell.snapshot.lock().unwrap();
        if !snapshot.status.is_terminal() {
            self.cell.stop_requested.store(true, Ordering::Release);
        }
        drop(snapshot);
        self.cell.wake.notify();
    }
    pub fn wait_retired(&self) -> BenchmarkStatus {
        let mut snapshot = self.cell.snapshot.lock().unwrap();
        while !snapshot.status.is_terminal() {
            #[cfg(feature = "ownership-test-support")]
            if let Some(proof) = self.cell.proof.get() {
                proof.facts.lock().unwrap().wait_entered = true;
            }
            snapshot = self.cell.changed.wait(snapshot).unwrap();
        }
        snapshot.status
    }
}
pub enum BeginBenchmark {
    Preparing(BenchmarkPreparation),
    AlreadyRunning(BenchmarkRun),
}
#[derive(Clone, Copy, Debug)]
pub enum WaitPolicy {
    LiveGeneralCapAtPublication,
    NoAdditionalCap,
}
#[derive(Clone, Copy, Debug)]
pub enum NativeFinalPolicy {
    None,
    ObserveEligibleRead,
}
#[derive(Clone, Copy, Debug)]
pub struct OperationPolicy {
    pub timeout: Duration,
    pub wait: WaitPolicy,
    pub completion: CompletionRequirement,
    pub health: NativeFinalPolicy,
}
impl OperationPolicy {
    pub fn production() -> Self {
        Self {
            timeout: Duration::from_secs(300),
            wait: WaitPolicy::LiveGeneralCapAtPublication,
            completion: CompletionRequirement::RequestedWindow,
            health: NativeFinalPolicy::ObserveEligibleRead,
        }
    }
}
#[derive(Clone, Copy, Debug)]
pub enum SamplePolicy {
    NominalIntegerSeconds(std::num::NonZeroU64),
    Elapsed,
}
#[derive(Clone, Copy, Debug)]
pub struct Workload {
    pub mode: Mode,
    pub size: NonZeroUsize,
    pub duration: Duration,
}
pub struct CheckedWorkloads {
    workloads: Vec<Workload>,
    policy: OperationPolicy,
    samples: SamplePolicy,
    production: bool,
}
impl CheckedWorkloads {
    pub fn new(
        workloads: Vec<Workload>,
        policy: OperationPolicy,
        samples: SamplePolicy,
    ) -> Result<Self, BenchmarkError> {
        if workloads.is_empty()
            || workloads.len() > MAX_WORKLOADS
            || policy.timeout.is_zero()
            || Instant::now().checked_add(policy.timeout).is_none()
            || workloads.iter().any(|w| {
                w.duration.is_zero()
                    || Instant::now().checked_add(w.duration).is_none()
                    || w.size.get() > i64::MAX as usize
            })
        {
            return Err(BenchmarkError::InvalidPlan);
        }
        Ok(Self {
            workloads,
            policy,
            samples,
            production: false,
        })
    }
    pub fn production() -> Result<Self, BenchmarkError> {
        let mut workloads = reserved_vec(MAX_WORKLOADS)?;
        for mode in [Mode::Write, Mode::Read] {
            for shift in 0..10 {
                workloads.push(Workload {
                    mode,
                    size: NonZeroUsize::new(4096 << shift).unwrap(),
                    duration: Duration::from_secs(5),
                });
            }
        }
        let mut plan = Self::new(
            workloads,
            OperationPolicy::production(),
            SamplePolicy::NominalIntegerSeconds(std::num::NonZeroU64::new(5).unwrap()),
        )?;
        plan.production = true;
        Ok(plan)
    }
    pub fn workloads(&self) -> &[Workload] {
        &self.workloads
    }
    pub fn operation_policy(&self) -> OperationPolicy {
        self.policy
    }
}
/// A single owned capability. It cannot retain a benchmark plan or inventory.
pub struct BenchmarkTarget {
    target: Target,
    device: DeviceId,
    execution: Execution,
    length: usize,
    alignment: usize,
    #[cfg(test)]
    drop_hook: Option<Box<dyn FnOnce() + Send + Sync>>,
}
#[cfg(test)]
impl Drop for BenchmarkTarget {
    fn drop(&mut self) {
        if let Some(hook) = self.drop_hook.take() {
            hook();
        }
    }
}
impl BenchmarkTarget {
    pub fn new(
        target: Target,
        device: DeviceId,
        execution: Execution,
        length: usize,
        alignment: usize,
    ) -> Result<Self, BenchmarkError> {
        let (offset, available) = match &target {
            Target::File { offset, .. } => (*offset, u64::MAX - *offset),
            Target::Named { file, offset } => (
                *offset,
                file.extent()
                    .checked_sub(*offset)
                    .ok_or(BenchmarkError::InvalidPlan)?,
            ),
            Target::Block(region) => {
                let (_, offset, len) = region.file_region();
                (offset, len)
            }
        };
        if length == 0
            || !alignment.is_power_of_two()
            || length as u64 > available
            || !length.is_multiple_of(alignment)
            || !offset.is_multiple_of(alignment as u64)
            || std::alloc::Layout::from_size_align(length, alignment).is_err()
        {
            return Err(BenchmarkError::InvalidPlan);
        }
        Ok(Self {
            target,
            device,
            execution,
            length,
            alignment,
            #[cfg(test)]
            drop_hook: None,
        })
    }
    fn interval(&self, start: usize, length: usize) -> io::Result<Target> {
        match &self.target {
            Target::File { file, offset } => Ok(Target::File {
                file: Arc::clone(file),
                offset: offset + start as u64,
            }),
            Target::Named { file, offset } => {
                let offset = offset
                    .checked_add(start as u64)
                    .ok_or_else(|| io::Error::from(io::ErrorKind::InvalidInput))?;
                file.check_range(offset, length as u64)?;
                Ok(Target::Named {
                    file: Arc::clone(file),
                    offset,
                })
            }
            Target::Block(region) => region
                .subregion(start as u64..(start + length) as u64)
                .map(Target::Block)
                .map_err(|e| io::Error::new(io::ErrorKind::InvalidInput, e)),
        }
    }
}
struct MetadataReservation {
    ledger: Arc<Mutex<Ledger>>,
}
impl Drop for MetadataReservation {
    fn drop(&mut self) {
        self.ledger.lock().unwrap().benchmark = BenchmarkMetadata::default();
    }
}
enum Publication {
    Idle,
    Preparing,
    Committed(BenchmarkStorage),
    Working,
    Aborted(BenchmarkError),
}
struct Inbox {
    accepting: bool,
    next_id: u64,
    current: Option<Arc<RunCell>>,
    publication: Publication,
}
pub(super) struct BenchmarkIngress {
    inbox: Mutex<Inbox>,
    wake: Arc<Wake>,
}
impl BenchmarkIngress {
    pub(super) fn new(wake: Arc<Wake>) -> Self {
        Self {
            inbox: Mutex::new(Inbox {
                accepting: true,
                next_id: 1,
                current: None,
                publication: Publication::Idle,
            }),
            wake,
        }
    }
    pub(super) fn close(&self) {
        let mut inbox = self.inbox.lock().unwrap();
        inbox.accepting = false;
        if let Some(cell) = &inbox.current {
            cell.stop_requested.store(true, Ordering::Release);
        }
        drop(inbox);
        self.wake.notify();
    }
    pub(super) fn idle(&self) -> bool {
        matches!(self.inbox.lock().unwrap().publication, Publication::Idle)
    }
    fn finish(
        &self,
        cell: &Arc<RunCell>,
        completed: usize,
        elapsed: Duration,
        result: Result<u64, BenchmarkError>,
    ) {
        let mut inbox = self.inbox.lock().unwrap();
        assert!(inbox.current.as_ref().is_some_and(|c| c.id == cell.id));
        cell.set(BenchmarkStatus::Finished {
            started_wall_us: cell.started_wall_us,
            finished_wall_us: wall_us(),
            completed,
            elapsed,
            result,
        });
        inbox.publication = Publication::Idle;
    }
}
pub struct BenchmarkPreparation {
    cell: Arc<RunCell>,
    endpoint: Arc<BenchmarkIngress>,
    shape: BenchmarkShape,
    targets: Vec<BenchmarkTarget>,
    reservation: Option<MetadataReservation>,
}
impl Service {
    pub fn begin_benchmark(&self, shape: BenchmarkShape) -> Result<BeginBenchmark, BenchmarkError> {
        let gate = self.shared.gate.lock().unwrap();
        let mut inbox = self.shared.benchmark.inbox.lock().unwrap();
        if !gate.accepting || !inbox.accepting {
            return Err(BenchmarkError::Unavailable);
        }
        if !matches!(inbox.publication, Publication::Idle) {
            return Ok(BeginBenchmark::AlreadyRunning(BenchmarkRun {
                cell: Arc::clone(inbox.current.as_ref().unwrap()),
            }));
        }
        if !gate.config.benchmark.contains(shape) {
            return Err(BenchmarkError::InvalidPlan);
        }
        let next = inbox
            .next_id
            .checked_add(1)
            .ok_or(BenchmarkError::Arithmetic)?;
        let started_wall_us = wall_us();
        let cell = Arc::new(RunCell {
            #[cfg(feature = "ownership-test-support")]
            proof: std::sync::OnceLock::new(),
            id: inbox.next_id,
            started_wall_us,
            snapshot: Mutex::new(RunSnapshot {
                status: BenchmarkStatus::Preparing { started_wall_us },
                report: BenchmarkReport::default(),
            }),
            changed: Condvar::new(),
            stop_requested: AtomicBool::new(false),
            wake: Arc::clone(&self.shared.wake),
        });
        inbox.next_id = next;
        let old = inbox.current.replace(Arc::clone(&cell));
        inbox.publication = Publication::Preparing;
        self.shared.ledger.lock().unwrap().benchmark = BenchmarkMetadata {
            targets: shape.target_count.get(),
            workloads: shape.workload_count.get(),
            workers: shape.concurrency.get(),
            run_reserved: true,
            ..BenchmarkMetadata::default()
        };
        drop(inbox);
        drop(gate);
        drop(old);
        let mut preparation = BenchmarkPreparation {
            cell,
            endpoint: Arc::clone(&self.shared.benchmark),
            shape,
            targets: Vec::new(),
            reservation: Some(MetadataReservation {
                ledger: Arc::clone(&self.shared.ledger),
            }),
        };
        let allocation = reserved_vec(shape.target_count.get()).and_then(|targets| {
            let bytes = owned_payload_bytes_for(
                targets.capacity(),
                shape.workload_count.get(),
                shape.workload_count.get(),
                shape.concurrency.get(),
            )?;
            preparation.targets = targets;
            preparation
                .reservation
                .as_ref()
                .unwrap()
                .ledger
                .lock()
                .unwrap()
                .benchmark
                .owned_payload_bytes = bytes;
            Ok(())
        });
        if let Err(error) = allocation {
            preparation.abort_inner(error);
            return Err(error);
        }
        Ok(BeginBenchmark::Preparing(preparation))
    }
    pub fn stop_benchmarks(&self) {
        self.shared.benchmark.close();
    }
    pub fn benchmark_status(&self) -> BenchmarkStatus {
        let cell = self.shared.benchmark.inbox.lock().unwrap().current.clone();
        cell.map_or(BenchmarkStatus::NotStarted, |cell| {
            cell.snapshot.lock().unwrap().status
        })
    }
}
impl BenchmarkPreparation {
    pub fn run(&self) -> BenchmarkRun {
        BenchmarkRun {
            cell: Arc::clone(&self.cell),
        }
    }
    pub fn remaining_targets(&self) -> usize {
        self.shape.target_count.get() - self.targets.len()
    }
    pub fn add_target(&mut self, target: BenchmarkTarget) -> Result<(), BenchmarkError> {
        self.check_add()?;
        self.targets.push(target);
        Ok(())
    }
    pub fn check_add(&self) -> Result<(), BenchmarkError> {
        if self.reservation.is_none() || self.remaining_targets() == 0 {
            return Err(BenchmarkError::InvalidPlan);
        }
        if self.cell.stop_requested.load(Ordering::Acquire) {
            return Err(BenchmarkError::Cancelled);
        }
        Ok(())
    }
    pub fn commit(&mut self, plan: CheckedWorkloads) -> Result<BenchmarkRun, BenchmarkError> {
        if self.reservation.is_none()
            || self.targets.is_empty()
            || plan.workloads.len() > self.shape.workload_count.get()
            || (plan.production && self.shape.concurrency.get() != MAX_WORKERS)
            || self.targets.iter().any(|target| {
                (plan.production
                    && (target.length != PRODUCTION_REGION_BYTES || 4096 % target.alignment != 0))
                    || plan.workloads.iter().any(|w| {
                        w.size.get() > target.length
                            || (w.mode == Mode::Write
                                && !w.size.get().is_multiple_of(target.alignment))
                    })
            })
        {
            return Err(BenchmarkError::InvalidPlan);
        }
        let measurements = reserved_vec(self.shape.workload_count.get())?;
        let mut workers = reserved_vec(self.shape.concurrency.get())?;
        workers.resize_with(self.shape.concurrency.get(), || WorkerState::Ready);
        let bytes = owned_payload_bytes_for(
            self.targets.capacity(),
            plan.workloads.capacity(),
            measurements.capacity(),
            workers.capacity(),
        )?;
        let mut inbox = self.endpoint.inbox.lock().unwrap();
        if !inbox.accepting || self.cell.stop_requested.load(Ordering::Acquire) {
            return Err(BenchmarkError::Cancelled);
        }
        if !matches!(inbox.publication, Publication::Preparing)
            || inbox.current.as_ref().is_none_or(|c| c.id != self.cell.id)
        {
            return Err(BenchmarkError::InvalidPlan);
        }
        self.reservation
            .as_ref()
            .unwrap()
            .ledger
            .lock()
            .unwrap()
            .benchmark
            .owned_payload_bytes = bytes;
        let storage = BenchmarkStorage {
            targets: std::mem::take(&mut self.targets),
            plan,
            measurements,
            workers,
            _reservation: self.reservation.take().unwrap(),
        };
        inbox.publication = Publication::Committed(storage);
        drop(inbox);
        self.endpoint.wake.notify();
        Ok(self.run())
    }
    pub fn discard_targets(&mut self) {
        self.targets.clear();
    }
    pub fn abort(mut self, error: BenchmarkError) -> BenchmarkRun {
        self.abort_inner(error);
        self.run()
    }
    fn abort_inner(&mut self, error: BenchmarkError) {
        if self.reservation.is_none() {
            return;
        }
        self.discard_targets();
        drop(std::mem::take(&mut self.targets));
        drop(self.reservation.take());
        let mut inbox = self.endpoint.inbox.lock().unwrap();
        if matches!(inbox.publication, Publication::Preparing)
            && inbox.current.as_ref().is_some_and(|c| c.id == self.cell.id)
        {
            inbox.publication = Publication::Aborted(error);
        }
        drop(inbox);
        self.endpoint.wake.notify();
    }
}
impl Drop for BenchmarkPreparation {
    fn drop(&mut self) {
        self.abort_inner(BenchmarkError::Cancelled);
    }
}
fn reserved_vec<T>(count: usize) -> Result<Vec<T>, BenchmarkError> {
    let mut vector = Vec::new();
    vector
        .try_reserve_exact(count)
        .map_err(|_| BenchmarkError::Memory)?;
    Ok(vector)
}
fn wall_us() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_micros()
        .min(i64::MAX as u128) as i64
}
pub(super) fn owned_payload_bytes_for(
    targets: usize,
    workloads: usize,
    measurements: usize,
    workers: usize,
) -> Result<usize, BenchmarkError> {
    let fixed = size_of::<BenchmarkIngress>()
        + 2 * size_of::<RunCell>()
        + size_of::<BenchmarkPreparation>()
        + size_of::<BenchmarkJob>()
        + size_of::<Wake>()
        + size_of::<MetadataReservation>();
    let target = size_of::<BenchmarkTarget>()
        + crate::block_file::region_lease_payload_size().max(size_of::<File>());
    [
        (targets, target),
        (workloads, size_of::<Workload>()),
        (measurements, size_of::<Sample>()),
        (workers, size_of::<WorkerState>()),
    ]
    .into_iter()
    .try_fold(fixed, |sum, (count, size)| {
        count
            .checked_mul(size)
            .and_then(|bytes| sum.checked_add(bytes))
            .ok_or(BenchmarkError::Arithmetic)
    })
}
struct BenchmarkStorage {
    targets: Vec<BenchmarkTarget>,
    plan: CheckedWorkloads,
    measurements: Vec<Sample>,
    workers: Vec<WorkerState>,
    _reservation: MetadataReservation,
}
#[derive(Clone, Copy, Eq, PartialEq)]
enum FinalState {
    Pending,
    Finished,
    AbandonedByWait,
}
pub(super) struct BenchChild {
    #[cfg(feature = "ownership-test-support")]
    pub(super) proof: Option<Arc<proof::ProofState>>,
    run: u64,
    worker: usize,
    issued_at: Instant,
    original_deadline: Instant,
    lifetime: RequestLifetime,
    final_state: FinalState,
}
struct PendingChild {
    child: BenchChild,
    options: RequestOptions,
    target: Target,
    direction: Direction,
    admission: Option<Admission>,
}
#[expect(
    clippy::large_enum_variant,
    reason = "Reserved worker backing avoids allocating every pending operation"
)]
enum WorkerState {
    Ready,
    Preparing(PendingChild),
    Active {
        control: RequestControl,
        wait_until: Option<Instant>,
    },
    Draining {
        control: RequestControl,
        reason: DrainReason,
    },
    Stopped,
}
#[derive(Clone, Copy)]
enum DrainReason {
    Stop,
    WaitExpired,
}
pub(super) struct BenchmarkJob {
    storage: BenchmarkStorage,
    cell: Arc<RunCell>,
    phase: usize,
    phase_started: Instant,
    phase_end: Instant,
    began: Instant,
    successes: u64,
    latency_us: u64,
    failed: Option<BenchmarkError>,
    random: u64,
}
impl BenchmarkJob {
    fn random(&mut self, upper: usize) -> usize {
        self.random ^= self.random << 13;
        self.random ^= self.random >> 7;
        self.random ^= self.random << 17;
        (self.random % upper as u64) as usize
    }
    fn issue(
        &mut self,
        worker: usize,
        now: Instant,
        native: impl Fn(DeviceId) -> bool,
        group: GroupId,
    ) -> io::Result<PendingChild> {
        let workload = self.storage.plan.workloads[self.phase];
        let index = self.random(self.storage.targets.len());
        let limit = self.storage.targets[index].length - workload.size.get() + 1;
        let relative = self.random(limit);
        let target = &self.storage.targets[index];
        let write_alignment = if self.storage.plan.production {
            4096
        } else {
            target.alignment
        };
        let logical = if workload.mode == Mode::Write {
            relative / write_alignment * write_alignment
        } else {
            relative
        };
        let start = logical / target.alignment * target.alignment;
        let end = (logical + workload.size.get()).div_ceil(target.alignment) * target.alignment;
        let native = native(target.device) && target.execution == Execution::PreferNative;
        let original_deadline = now
            .checked_add(self.storage.plan.policy.timeout)
            .ok_or_else(|| invalid("benchmark deadline overflow"))?;
        let options = RequestOptions {
            device: target.device,
            group,
            class: RequestClass::Normal,
            execution: target.execution,
            rate_exempt: true,
            length: end - start,
            alignment: target.alignment,
            window: logical - start..logical - start + workload.size.get(),
            policy: RequestPolicy {
                deadline: Some(original_deadline),
                retry_alignment: native.then(|| NonZeroUsize::new(target.alignment).unwrap()),
                max_error_retries: if native { 10 } else { 0 },
                completion: self.storage.plan.policy.completion,
            },
            tag: 0,
        };
        Ok(PendingChild {
            child: BenchChild {
                #[cfg(feature = "ownership-test-support")]
                proof: self.cell.proof.get().cloned(),
                run: self.cell.id,
                worker,
                issued_at: now,
                original_deadline,
                lifetime: RequestLifetime::default(),
                final_state: FinalState::Pending,
            },
            options,
            target: target.interval(start, end - start)?,
            direction: if workload.mode == Mode::Read {
                Direction::Read
            } else {
                Direction::Write
            },
            admission: None,
        })
    }
    fn outcome(&mut self, child: &mut BenchChild, status: &CompletionStatus, now: Instant) {
        if child.run != self.cell.id || child.final_state != FinalState::Pending {
            return;
        }
        if matches!(
            self.storage.workers[child.worker],
            WorkerState::Draining {
                reason: DrainReason::WaitExpired,
                ..
            }
        ) {
            child.final_state = FinalState::AbandonedByWait;
            return;
        }
        child.final_state = FinalState::Finished;
        child.lifetime.observe(FinalFacts {
            io_error: matches!(status, CompletionStatus::IoError(_)),
            raw_os_error: match status {
                CompletionStatus::IoError(error) => {
                    error.raw_os_error().and_then(std::num::NonZeroI32::new)
                }
                _ => None,
            },
            deadline_exceeded: now > child.original_deadline,
            finished_wall_us: wall_us(),
        });
        if matches!(status, CompletionStatus::Complete) {
            let latency = u64::try_from(now.saturating_duration_since(child.issued_at).as_micros());
            match (
                self.successes.checked_add(1),
                latency.ok().and_then(|n| self.latency_us.checked_add(n)),
            ) {
                (Some(count), Some(latency)) => {
                    self.successes = count;
                    self.latency_us = latency;
                }
                _ => self.failed = Some(BenchmarkError::Arithmetic),
            }
        }
    }
    fn sample(&self, now: Instant) -> Result<Sample, BenchmarkError> {
        if self.successes == 0 {
            return Err(BenchmarkError::NoSuccess);
        }
        let workload = self.storage.plan.workloads[self.phase];
        let (iops, latency_us) = sample_values(
            self.successes,
            self.latency_us,
            self.storage.plan.samples,
            now.duration_since(self.phase_started),
        )?;
        Ok(Sample {
            mode: workload.mode,
            size: ByteCount::new(workload.size.get() as u64)
                .map_err(|_| BenchmarkError::InvalidPlan)?,
            iops,
            latency_us,
        })
    }
}
pub(super) fn sample_values(
    successes: u64,
    latency_us: u64,
    policy: SamplePolicy,
    elapsed: Duration,
) -> Result<(f64, f64), BenchmarkError> {
    if successes == 0 {
        return Err(BenchmarkError::NoSuccess);
    }
    let values = match policy {
        SamplePolicy::NominalIntegerSeconds(seconds) => (
            (successes / seconds.get()) as f64,
            (latency_us / successes) as f64,
        ),
        SamplePolicy::Elapsed => (
            successes as f64 / elapsed.as_secs_f64(),
            latency_us as f64 / successes as f64,
        ),
    };
    if !values.0.is_finite()
        || !values.1.is_finite()
        || values.0 <= f64::EPSILON
        || values.1 <= f64::EPSILON
    {
        Err(BenchmarkError::Arithmetic)
    } else {
        Ok(values)
    }
}
impl BenchChild {
    pub(super) fn identity(&self) -> (u64, usize) {
        (self.run, self.worker)
    }
}
impl Engine {
    pub(super) fn benchmark_outcome(
        &mut self,
        child: &mut BenchChild,
        status: &CompletionStatus,
        now: Instant,
    ) {
        if let Some(job) = &mut self.benchmark {
            job.outcome(child, status, now);
        }
    }
    pub(super) fn benchmark_retired(&mut self, run: u64, worker: usize) {
        if let Some(job) = &mut self.benchmark {
            if job.cell.id == run {
                match &job.storage.workers[worker] {
                    WorkerState::Active { control, .. } | WorkerState::Draining { control, .. } => {
                        debug_assert_eq!(
                            control.cell.phase.load(Ordering::Acquire),
                            admission::FINISHED
                        );
                        job.storage.workers[worker] = WorkerState::Ready;
                    }
                    _ => unreachable!("benchmark child retired without a worker"),
                }
            }
        }
    }
    pub(super) fn benchmark_delay(&self, now: Instant) -> Option<Duration> {
        let job = self.benchmark.as_ref()?;
        let mut next = (job.phase_end > now).then_some(job.phase_end);
        for worker in &job.storage.workers {
            let deadline = match worker {
                WorkerState::Ready => Some(now),
                WorkerState::Preparing(pending) => Some(pending.child.original_deadline),
                WorkerState::Active { wait_until, .. } => *wait_until,
                _ => None,
            };
            if let Some(deadline) = deadline {
                next = Some(next.map_or(deadline, |old| old.min(deadline)));
            }
        }
        next.map(|deadline| deadline.saturating_duration_since(now))
    }
    pub(super) fn benchmark_turn(&mut self, now: Instant, stopping: bool) -> bool {
        if self.benchmark.is_none() {
            let mut inbox = self.shared.benchmark.inbox.lock().unwrap();
            match std::mem::replace(&mut inbox.publication, Publication::Working) {
                Publication::Committed(storage) => {
                    let cell = Arc::clone(inbox.current.as_ref().unwrap());
                    let phase_end = now.checked_add(storage.plan.workloads[0].duration);
                    self.benchmark = Some(BenchmarkJob {
                        storage,
                        random: cell.id | 1,
                        cell,
                        phase: 0,
                        phase_started: now,
                        phase_end: phase_end.unwrap_or(now),
                        began: now,
                        successes: 0,
                        latency_us: 0,
                        failed: phase_end.is_none().then_some(BenchmarkError::Arithmetic),
                    });
                }
                Publication::Aborted(error) => {
                    let cell = Arc::clone(inbox.current.as_ref().unwrap());
                    drop(inbox);
                    self.shared
                        .benchmark
                        .finish(&cell, 0, Duration::ZERO, Err(error));
                    return true;
                }
                state => {
                    inbox.publication = state;
                    return false;
                }
            }
        }
        let mut job = self.benchmark.take().unwrap();
        let stopped = stopping || job.cell.stop_requested.load(Ordering::Acquire);
        if stopped && job.failed.is_none() {
            job.failed = Some(BenchmarkError::Cancelled);
        }
        let expired = now >= job.phase_end;
        let mut busy = false;
        let mut cancel = [None; MAX_WORKERS];
        for (worker, cancellation) in cancel
            .iter_mut()
            .enumerate()
            .take(job.storage.workers.len())
        {
            let state = std::mem::replace(&mut job.storage.workers[worker], WorkerState::Stopped);
            #[cfg(feature = "ownership-test-support")]
            let active_id = match &state {
                WorkerState::Active { control, .. } => Some(control.id()),
                _ => None,
            };
            let state = match state {
                WorkerState::Ready if !expired && job.failed.is_none() => {
                    busy = true;
                    match job.issue(
                        worker,
                        now,
                        |id| self.native_enabled(id),
                        self.config.default_group,
                    ) {
                        Ok(pending) => WorkerState::Preparing(pending),
                        Err(error) => {
                            job.failed = Some(error.into());
                            WorkerState::Stopped
                        }
                    }
                }
                WorkerState::Ready => WorkerState::Stopped,
                WorkerState::Preparing(pending) if job.failed.is_some() => {
                    self.discard_unissued(pending);
                    busy = true;
                    WorkerState::Stopped
                }
                WorkerState::Preparing(mut pending) => {
                    if now >= pending.child.original_deadline {
                        self.discard_unissued(pending);
                        busy = true;
                        WorkerState::Ready
                    } else {
                        let result = self.prepare_benchmark_child(
                            &mut pending,
                            job.storage.plan.policy,
                            AlignedBuffer::zeroed,
                        );
                        busy |= !matches!(result, Ok(false));
                        self.finish_benchmark_preparation(pending, result, job.storage.plan.policy)
                    }
                }
                WorkerState::Active { control, .. } if job.failed.is_some() => {
                    control.request_cancel();
                    *cancellation = Some((control.id(), DrainReason::Stop));
                    WorkerState::Draining {
                        control,
                        reason: DrainReason::Stop,
                    }
                }
                WorkerState::Active {
                    control,
                    wait_until,
                } if wait_until.is_some_and(|deadline| now >= deadline) => {
                    control.request_cancel();
                    *cancellation = Some((control.id(), DrainReason::WaitExpired));
                    WorkerState::Draining {
                        control,
                        reason: DrainReason::WaitExpired,
                    }
                }
                state => state,
            };
            job.storage.workers[worker] = state;
            #[cfg(feature = "ownership-test-support")]
            if let (Some(proof), Some(request)) = (job.cell.proof.get(), active_id) {
                let mut facts = proof.facts.lock().unwrap();
                if stopped && facts.selected == Some((worker, request.0)) && facts.actor.is_none() {
                    let resulting = match &job.storage.workers[worker] {
                        WorkerState::Draining { .. } => 1,
                        WorkerState::Stopped => 2,
                        _ => 3,
                    };
                    facts.actor = Some((
                        worker,
                        request.0,
                        resulting,
                        stopped,
                        proof.hold.facts().executed,
                    ));
                }
            }
        }
        let elapsed = now.saturating_duration_since(job.began);
        if expired || job.failed.is_some() {
            job.cell.set(BenchmarkStatus::Draining {
                started_wall_us: job.cell.started_wall_us,
                completed: job.storage.measurements.len(),
                elapsed,
            });
        } else {
            job.cell.set(BenchmarkStatus::Running {
                started_wall_us: job.cell.started_wall_us,
                workload: job.phase,
                completed: job.storage.measurements.len(),
                elapsed,
            });
        }
        if job
            .storage
            .workers
            .iter()
            .all(|state| matches!(state, WorkerState::Stopped))
        {
            let workload = job.storage.plan.workloads[job.phase];
            {
                let mut snapshot = job.cell.snapshot.lock().unwrap();
                let report = &mut snapshot.report;
                report.measurements[report.len] = Some(WorkloadMeasurement {
                    mode: workload.mode,
                    size: workload.size,
                    elapsed: now.saturating_duration_since(job.phase_started),
                    successful_count: job.successes,
                    successful_latency_us: job.latency_us,
                });
                report.len += 1;
            }
            if job.failed.is_none() {
                match job.sample(now) {
                    Ok(sample) => job.storage.measurements.push(sample),
                    Err(error) => job.failed = Some(error),
                }
            }
            if job.failed.is_none() && job.phase + 1 < job.storage.plan.workloads.len() {
                job.phase += 1;
                job.phase_started = now;
                match now.checked_add(job.storage.plan.workloads[job.phase].duration) {
                    Some(end) => job.phase_end = end,
                    None => {
                        job.phase_end = now;
                        job.failed = Some(BenchmarkError::Arithmetic);
                    }
                }
                job.successes = 0;
                job.latency_us = 0;
                job.storage
                    .workers
                    .iter_mut()
                    .for_each(|worker| *worker = WorkerState::Ready);
                busy = true;
            } else {
                let completed = job.storage.measurements.len();
                let cell = Arc::clone(&job.cell);
                let candidate = if let Some(error) = job.failed {
                    Err(error)
                } else {
                    CalibrationCandidate::try_from_samples(std::mem::take(
                        &mut job.storage.measurements,
                    ))
                    .map_err(|_| BenchmarkError::Arithmetic)
                };
                let began = job.began;
                drop(job);
                let mut inbox = self.shared.benchmark.inbox.lock().unwrap();
                let mut snapshot = cell.snapshot.lock().unwrap();
                let result = if cell.stop_requested.load(Ordering::Acquire) || !inbox.accepting {
                    Err(BenchmarkError::Cancelled)
                } else {
                    candidate.and_then(|candidate| {
                        self.shared
                            .calibration
                            .replace(candidate)
                            .map_err(|_| BenchmarkError::Arithmetic)
                    })
                };
                snapshot.status = BenchmarkStatus::Finished {
                    started_wall_us: cell.started_wall_us,
                    finished_wall_us: wall_us(),
                    completed,
                    elapsed: Instant::now().saturating_duration_since(began),
                    result,
                };
                cell.changed.notify_all();
                inbox.publication = Publication::Idle;
                return true;
            }
        }
        self.benchmark = Some(job);
        for (id, reason) in cancel.into_iter().flatten() {
            self.cancel(
                id,
                match reason {
                    DrainReason::Stop => CompletionStatus::Cancelled,
                    DrainReason::WaitExpired => CompletionStatus::TimedOut,
                },
            );
            busy = true;
        }
        busy
    }
    fn discard_unissued(&mut self, pending: PendingChild) {
        if let Some(admission) = &pending.admission {
            self.discard_benchmark_admission(admission.id());
        }
        drop(pending);
    }
    fn finish_benchmark_preparation(
        &mut self,
        mut pending: PendingChild,
        result: io::Result<bool>,
        policy: OperationPolicy,
    ) -> WorkerState {
        match result {
            Ok(false) => WorkerState::Preparing(pending),
            Err(_) => {
                self.discard_unissued(pending);
                WorkerState::Ready
            }
            Ok(true) => {
                let published = Instant::now();
                let wait_until = match policy.wait {
                    WaitPolicy::NoAdditionalCap => None,
                    WaitPolicy::LiveGeneralCapAtPublication => {
                        let remaining_ms = pending
                            .child
                            .original_deadline
                            .saturating_duration_since(published)
                            .as_millis();
                        let remaining =
                            Duration::from_millis(remaining_ms.min(u64::MAX as u128) as u64);
                        Some(published + self.config.health.wait_cap.min(remaining))
                    }
                };
                let mut admission = pending.admission.take().unwrap();
                let control = admission
                    .take_control()
                    .expect("unpublished benchmark control");
                match admission.publish_benchmark(pending.child) {
                    Ok(_) => WorkerState::Active {
                        control,
                        wait_until,
                    },
                    Err(_) => {
                        self.discard_benchmark_admission(admission.id());
                        WorkerState::Ready
                    }
                }
            }
        }
    }
    fn prepare_benchmark_child(
        &self,
        pending: &mut PendingChild,
        policy: OperationPolicy,
        allocate: impl FnOnce(usize, usize) -> io::Result<AlignedBuffer>,
    ) -> io::Result<bool> {
        if pending.admission.is_none() {
            match Admission::begin(Arc::clone(&self.shared), pending.options.clone()) {
                Ok(admission) => {
                    pending.child.lifetime = admission.lifetime();
                    pending.child.lifetime.capture(
                        &admission,
                        pending.target.clone(),
                        pending.direction == Direction::Read
                            && matches!(policy.health, NativeFinalPolicy::ObserveEligibleRead),
                    );
                    pending.admission = Some(admission);
                }
                Err(error) if error.kind() == io::ErrorKind::WouldBlock => return Ok(false),
                Err(error) => return Err(error),
            }
        }
        let admission = pending.admission.as_mut().unwrap();
        if !admission.ready()? {
            return Ok(false);
        }
        admission.prepare_benchmark_with(pending.target.clone(), pending.direction, allocate)?;
        Ok(true)
    }
}
#[cfg(test)]
impl BenchmarkTarget {
    pub(super) fn on_drop(mut self, hook: impl FnOnce() + Send + Sync + 'static) -> Self {
        self.drop_hook = Some(Box::new(hook));
        self
    }
}
#[cfg(test)]
impl BenchmarkPreparation {
    pub(super) fn storage_address(&self) -> usize {
        self.targets.as_ptr() as usize
    }
}
#[cfg(test)]
impl Engine {
    pub(super) fn benchmark_storage_address(&self) -> usize {
        self.benchmark.as_ref().unwrap().storage.targets.as_ptr() as usize
    }
    pub(super) fn benchmark_child_policy(
        &self,
    ) -> Option<(Instant, RequestPolicy, Option<Instant>)> {
        let job = self.benchmark.as_ref()?;
        for worker in &job.storage.workers {
            if let WorkerState::Active {
                control,
                wait_until,
            } = worker
            {
                let active = self.active.get(&control.id())?;
                if let engine::WorkOwner::Benchmark(child) = &active.owner {
                    return Some((child.issued_at, active.cell.options.policy, *wait_until));
                }
            }
        }
        None
    }
}

#[cfg(test)]
impl Engine {
    pub(super) fn benchmark_prepare_with(
        &mut self,
        allocate: impl FnOnce(usize, usize) -> io::Result<AlignedBuffer>,
        before_publication: impl FnOnce(&Self),
    ) {
        let mut job = self.benchmark.take().unwrap();
        let WorkerState::Preparing(mut pending) =
            std::mem::replace(&mut job.storage.workers[0], WorkerState::Stopped)
        else {
            panic!("child not preparing")
        };
        let policy = job.storage.plan.policy;
        let result = self.prepare_benchmark_child(&mut pending, policy, allocate);
        before_publication(self);
        job.storage.workers[0] = self.finish_benchmark_preparation(pending, result, policy);
        self.benchmark = Some(job);
    }
}

#[cfg(test)]
impl Engine {
    pub(super) fn benchmark_success_after(&mut self, latency: Duration) {
        let id = *self.active.keys().next().unwrap();
        let mut active = self.active.remove(&id).unwrap();
        let engine::WorkOwner::Benchmark(child) = &mut active.owner else {
            panic!("not benchmark")
        };
        self.benchmark_outcome(
            child,
            &CompletionStatus::Complete,
            child.issued_at + latency,
        );
        self.active.insert(id, active);
    }
}
