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
use std::mem::size_of;
use std::num::NonZeroI32;
use std::time::{SystemTime, UNIX_EPOCH};

pub const HEALTH_QUEUE_CAPACITY: usize = 100;
pub const HEALTH_ACTIVE_CAPACITY: usize = 1;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct HealthConfig {
    pub warning_tolerance: Duration,
    pub blacklist_interval: Duration,
    pub wait_cap: Duration,
}
impl Default for HealthConfig {
    fn default() -> Self {
        Self {
            warning_tolerance: Duration::from_secs(5),
            blacklist_interval: Duration::from_secs(60),
            wait_cap: Duration::from_secs(120),
        }
    }
}
impl HealthConfig {
    pub(super) fn validate(self) -> io::Result<()> {
        if !(Duration::from_secs(1)..=Duration::from_secs(300)).contains(&self.warning_tolerance)
            || self.wait_cap.is_zero()
            || self.wait_cap.as_micros() > i64::MAX as u128
            || self.blacklist_interval.is_zero()
            || self.blacklist_interval.as_micros() > i64::MAX as u128
        {
            return Err(invalid("invalid health timing configuration"));
        }
        Ok(())
    }
}
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct ObservationCounters {
    pub accepted_error: u64,
    pub accepted_timeout: u64,
    pub full: u64,
    pub busy: u64,
    pub stopped: u64,
    pub already_observed: u64,
}
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum ObservationOutcome {
    #[default]
    NotOffered,
    Accepted,
    Full,
    Busy,
    Stopped,
    AlreadyObserved,
}
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct ObservationReceipt {
    pub error: ObservationOutcome,
    pub timeout: ObservationOutcome,
}
#[derive(Clone, Copy, Debug)]
pub struct FinalFacts {
    pub io_error: bool,
    pub raw_os_error: Option<NonZeroI32>,
    pub deadline_exceeded: bool,
    pub finished_wall_us: i64,
}
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct HealthSnapshot {
    pub warning: bool,
    pub warning_wall_us: i64,
    pub queued: usize,
    pub cleaning: usize,
    pub active: usize,
    pub waiting_capacity: usize,
    pub active_capacity: usize,
    /// Reserved typed payload bytes, including fixed backing capacity and retained-target
    /// payload allowances. Shared allocations can have overlapping allowances. Excludes
    /// opaque allocation headers, allocator overhead, and separately accounted I/O buffers.
    /// This is neither live heap usage nor an exact allocator-request total.
    pub owned_payload_bytes: usize,
    pub observations: ObservationCounters,
    pub successful_passes: u64,
    pub failed_passes: u64,
}
pub(crate) struct CapturedTarget {
    pub(super) device_open: Arc<AtomicBool>,
    pub(crate) target: Target,
    #[cfg(test)]
    pub(super) drop_hook: Option<Box<dyn FnOnce() + Send + Sync>>,
    pub(super) options: RequestOptions,
}
#[cfg(test)]
impl Drop for CapturedTarget {
    fn drop(&mut self) {
        if let Some(hook) = self.drop_hook.take() {
            hook();
        }
    }
}
impl CapturedTarget {
    pub(super) fn segment(&self, start: usize, length: usize) -> io::Result<Target> {
        let end = start
            .checked_add(length)
            .ok_or_else(|| invalid("segment overflow"))?;
        if end > self.options.length {
            return Err(invalid("segment exceeds captured interval"));
        }
        match &self.target {
            Target::File { file, offset } => Ok(Target::File {
                file: Arc::clone(file),
                offset: offset
                    .checked_add(start as u64)
                    .ok_or_else(|| invalid("offset overflow"))?,
            }),
            Target::Named { file, offset } => {
                let offset = offset
                    .checked_add(start as u64)
                    .ok_or_else(|| invalid("offset overflow"))?;
                file.check_range(offset, length as u64)?;
                Ok(Target::Named {
                    file: Arc::clone(file),
                    offset,
                })
            }
            Target::Block(region) => region
                .subregion(start as u64..end as u64)
                .map(Target::Block)
                .map_err(|e| io::Error::new(io::ErrorKind::InvalidInput, e)),
        }
    }
}
/// Keeps admission and the original target alive independently of delivery or buffer recycling.
/// CXX callers destroy this owner after leaving their result-condition guard.
#[derive(Default)]
pub struct RequestLifetime {
    pub(crate) target: Option<Arc<CapturedTarget>>,
    observer: Option<FinalObserver>,
    charge: Option<Arc<Charge>>,
}
impl RequestLifetime {
    #[cfg(any(test, feature = "ownership-test-support"))]
    pub fn watch_final_charge(&self) -> io::Result<crate::DropWitness> {
        self.charge
            .as_ref()
            .ok_or_else(|| invalid("request has no charge"))?
            .drop_witness
            .watch()
    }

    #[cfg(any(test, feature = "ownership-test-support"))]
    pub fn watch_final_block_lease(&self) -> io::Result<crate::DropWitness> {
        let target = &self
            .target
            .as_ref()
            .ok_or_else(|| invalid("request has no target"))?
            .target;
        match target {
            Target::Block(region) => region.watch_final_lease(),
            Target::File { .. } | Target::Named { .. } => {
                Err(invalid("request target is not a block lease"))
            }
        }
    }

    pub(super) fn reserved(charge: Arc<Charge>) -> Self {
        Self {
            charge: Some(charge),
            ..Self::default()
        }
    }
    #[cfg(feature = "cxx")]
    pub(crate) fn is_empty(&self) -> bool {
        self.charge.is_none()
    }
    pub(crate) fn capture(&mut self, admission: &Admission, target: Target, eligible: bool) {
        let target = Arc::new(CapturedTarget {
            device_open: Arc::clone(&admission.cell.device_open),
            target,
            options: admission.cell.options.clone(),
            #[cfg(test)]
            drop_hook: None,
        });
        self.target = Some(target);
        if eligible {
            self.enable_observer(&admission.shared.health);
        }
    }
    pub(super) fn enable_observer(&mut self, ingress: &Arc<HealthIngress>) {
        self.observer = Some(FinalObserver {
            ingress: Arc::clone(ingress),
            target: Arc::clone(self.target.as_ref().unwrap()),
            _charge: Arc::clone(self.charge.as_ref().unwrap()),
            observed: AtomicBool::new(false),
        });
    }
    #[cfg(feature = "cxx")]
    pub(crate) fn belongs_to(&self, admission: &Admission) -> bool {
        self.charge
            .as_ref()
            .is_some_and(|charge| Arc::ptr_eq(charge, &admission.cell.charge))
    }
    pub fn observe(&self, facts: FinalFacts) -> ObservationReceipt {
        self.observer
            .as_ref()
            .map_or(ObservationReceipt::default(), |o| o.observe(facts))
    }
    pub fn take_final_observer(&mut self) -> Option<FinalObserver> {
        self.observer.take()
    }
}
/// One request's final consumer observation. Keep this owner through callback completion;
/// observing borrows it and never releases its captured target or existing request charge.
/// An observer can outlive Service shutdown without retaining an actor.
pub struct FinalObserver {
    ingress: Arc<HealthIngress>,
    target: Arc<CapturedTarget>,
    _charge: Arc<Charge>,
    observed: AtomicBool,
}
impl FinalObserver {
    pub fn observe(&self, facts: FinalFacts) -> ObservationReceipt {
        use ObservationOutcome::*;
        let receipt = |outcome| ObservationReceipt {
            error: if facts.io_error { outcome } else { NotOffered },
            timeout: if facts.deadline_exceeded {
                outcome
            } else {
                NotOffered
            },
        };
        if self.observed.swap(true, Ordering::AcqRel) {
            self.ingress
                .already_observed
                .fetch_add(1, Ordering::Relaxed);
            return receipt(AlreadyObserved);
        }
        if !facts.io_error && !facts.deadline_exceeded {
            return receipt(NotOffered);
        }
        let Ok(mut inbox) = self.ingress.inbox.try_lock() else {
            self.ingress.busy.fetch_add(1, Ordering::Relaxed);
            return receipt(Busy);
        };
        if !inbox.accepting || !self.target.device_open.load(Ordering::Acquire) {
            inbox.counters.stopped += 1;
            return receipt(Stopped);
        }
        let mut result = ObservationReceipt::default();
        for (offered, timeout, destination) in [
            (facts.io_error, false, &mut result.error),
            (facts.deadline_exceeded, true, &mut result.timeout),
        ] {
            if !offered {
                continue;
            }
            if let Some(index) = inbox
                .slots
                .iter()
                .position(|s| matches!(s, HealthSlot::Free))
            {
                let seed_ms = if timeout {
                    inbox.config.warning_tolerance.as_micros() as i64
                } else {
                    5000
                };
                inbox.slots[index] = HealthSlot::Queued(Trigger {
                    target: Arc::clone(&self.target),
                    seed_ms,
                });
                let tail = (inbox.head + inbox.len) % HEALTH_QUEUE_CAPACITY;
                inbox.order[tail] = index;
                inbox.len += 1;
                if timeout {
                    inbox.counters.accepted_timeout += 1;
                } else {
                    inbox.counters.accepted_error += 1;
                }
                *destination = Accepted;
            } else {
                inbox.counters.full += 1;
                *destination = Full;
            }
        }
        if result.error == Accepted || result.timeout == Accepted {
            drop(inbox);
            self.ingress.pending.store(true, Ordering::Release);
            self.ingress.wake.notify();
        }
        result
    }
}
pub(super) struct Trigger {
    pub target: Arc<CapturedTarget>,
    pub seed_ms: i64,
}
enum HealthSlot {
    Free,
    Queued(Trigger),
    Cleaning,
}
struct Inbox {
    accepting: bool,
    slots: [HealthSlot; HEALTH_QUEUE_CAPACITY],
    order: [usize; HEALTH_QUEUE_CAPACITY],
    head: usize,
    len: usize,
    active: bool,
    config: HealthConfig,
    warning: Option<(Instant, i64)>,
    counters: ObservationCounters,
    passes: [u64; 2],
}
struct MetadataReservation {
    ledger: Arc<Mutex<Ledger>>,
    bytes: usize,
}
impl Drop for MetadataReservation {
    fn drop(&mut self) {
        self.ledger.lock().unwrap().health_payload_bytes -= self.bytes;
    }
}
pub(super) struct HealthIngress {
    pending: AtomicBool,
    pub wake: Arc<Wake>,
    inbox: Mutex<Inbox>,
    busy: AtomicU64,
    already_observed: AtomicU64,
    reservation: MetadataReservation,
}
pub(super) fn owned_payload_bytes_for(positions: usize) -> io::Result<usize> {
    let target = size_of::<CapturedTarget>()
        .checked_add(crate::block_file::region_lease_payload_size().max(size_of::<File>()))
        .ok_or_else(|| invalid("metadata payload overflow"))?;
    size_of::<HealthIngress>()
        .checked_add(size_of::<Arc<HealthIngress>>())
        .and_then(|n| n.checked_add(size_of::<Ledger>()))
        .and_then(|n| n.checked_add(size_of::<Option<Diagnosis>>()))
        .and_then(|n| n.checked_add(size_of::<engine::WorkOwner>()))
        .and_then(|n| n.checked_add(size_of::<(usize, Trigger)>()))
        .and_then(|n| n.checked_add(size_of::<Wake>()))
        .and_then(|n| n.checked_add(positions.checked_mul(target)?))
        .ok_or_else(|| invalid("metadata payload overflow"))
}

impl HealthIngress {
    #[cfg(test)]
    pub(super) fn while_locked(&self, callback: impl FnOnce()) {
        let _guard = self.inbox.lock().unwrap();
        callback();
    }
    pub fn new(
        wake: Arc<Wake>,
        ledger: Arc<Mutex<Ledger>>,
        config: HealthConfig,
    ) -> io::Result<Self> {
        let bytes = owned_payload_bytes_for(HEALTH_QUEUE_CAPACITY + HEALTH_ACTIVE_CAPACITY)?;
        ledger.lock().unwrap().health_payload_bytes += bytes;
        Ok(Self {
            pending: AtomicBool::new(false),
            wake,
            inbox: Mutex::new(Inbox {
                accepting: true,
                slots: std::array::from_fn(|_| HealthSlot::Free),
                order: [0; HEALTH_QUEUE_CAPACITY],
                head: 0,
                len: 0,
                active: false,
                config,
                warning: None,
                counters: ObservationCounters::default(),
                passes: [0; 2],
            }),
            busy: AtomicU64::new(0),
            already_observed: AtomicU64::new(0),
            reservation: MetadataReservation { ledger, bytes },
        })
    }
    pub fn has_work(&self) -> bool {
        self.pending.load(Ordering::Acquire)
    }
    pub fn discard_device(&self, device: DeviceId) {
        let mut removed = Vec::with_capacity(HEALTH_QUEUE_CAPACITY);
        {
            let mut inbox = self.inbox.lock().unwrap();
            let mut order = [0; HEALTH_QUEUE_CAPACITY];
            let mut count = 0;
            for offset in 0..inbox.len {
                let index = inbox.order[(inbox.head + offset) % HEALTH_QUEUE_CAPACITY];
                if matches!(&inbox.slots[index], HealthSlot::Queued(trigger) if trigger.target.options.device == device)
                {
                    let HealthSlot::Queued(trigger) =
                        std::mem::replace(&mut inbox.slots[index], HealthSlot::Cleaning)
                    else {
                        unreachable!()
                    };
                    removed.push((index, trigger));
                } else {
                    order[count] = index;
                    count += 1;
                }
            }
            inbox.order = order;
            inbox.head = 0;
            inbox.len = count;
            if count == 0 {
                self.pending.store(false, Ordering::Release);
            }
        }
        for (index, trigger) in removed {
            drop(trigger);
            self.inbox.lock().unwrap().slots[index] = HealthSlot::Free;
        }
    }
    pub fn record_outcome(&self, status: &CompletionStatus) {
        let index = usize::from(!matches!(status, CompletionStatus::Complete));
        let mut inbox = self.inbox.lock().unwrap();
        inbox.passes[index] = inbox.passes[index].saturating_add(1);
    }
    pub fn close(&self) {
        self.inbox.lock().unwrap().accepting = false;
        self.wake.notify();
    }
    pub fn configure(&self, config: HealthConfig) {
        self.inbox.lock().unwrap().config = config;
    }
    pub fn snapshot(&self, clear_expired: bool, now: Instant) -> HealthSnapshot {
        let mut inbox = self.inbox.lock().unwrap();
        if clear_expired
            && inbox.warning.is_some_and(|(at, _)| {
                now.saturating_duration_since(at) > inbox.config.blacklist_interval
            })
        {
            inbox.warning = None;
        }
        let mut counters = inbox.counters;
        counters.busy = self.busy.load(Ordering::Relaxed);
        counters.already_observed = self.already_observed.load(Ordering::Relaxed);
        HealthSnapshot {
            warning: inbox.warning.is_some(),
            warning_wall_us: inbox.warning.map_or(0, |(_, w)| w),
            queued: inbox.len,
            cleaning: inbox
                .slots
                .iter()
                .filter(|s| matches!(s, HealthSlot::Cleaning))
                .count(),
            active: usize::from(inbox.active),
            waiting_capacity: HEALTH_QUEUE_CAPACITY,
            active_capacity: HEALTH_ACTIVE_CAPACITY,
            owned_payload_bytes: self.reservation.bytes,
            observations: counters,
            successful_passes: inbox.passes[0],
            failed_passes: inbox.passes[1],
        }
    }
    pub fn warning(&self) -> bool {
        self.inbox.lock().unwrap().warning.is_some()
    }
    pub fn warn(&self, now: Instant) {
        let wall = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_micros()
            .min(i64::MAX as u128) as i64;
        self.inbox.lock().unwrap().warning = Some((now, wall));
    }
    pub fn activate(&self, now: Instant) -> Option<Diagnosis> {
        let mut inbox = self.inbox.lock().unwrap();
        if inbox.len == 0 {
            self.pending.store(false, Ordering::Release);
        }
        if inbox.active || inbox.len == 0 || !inbox.accepting || inbox.warning.is_some() {
            return None;
        }
        inbox.active = true;
        let index = inbox.order[inbox.head];
        inbox.head = (inbox.head + 1) % HEALTH_QUEUE_CAPACITY;
        inbox.len -= 1;
        let HealthSlot::Queued(trigger) =
            std::mem::replace(&mut inbox.slots[index], HealthSlot::Free)
        else {
            unreachable!()
        };
        Some(Diagnosis::new(trigger, inbox.config, now))
    }
    pub fn discard_one(&self) -> bool {
        let cleanup = {
            let mut inbox = self.inbox.lock().unwrap();
            if inbox.len == 0 {
                return false;
            }
            let index = inbox.order[inbox.head];
            inbox.head = (inbox.head + 1) % HEALTH_QUEUE_CAPACITY;
            inbox.len -= 1;
            let HealthSlot::Queued(trigger) =
                std::mem::replace(&mut inbox.slots[index], HealthSlot::Cleaning)
            else {
                unreachable!()
            };
            (index, trigger)
        };
        let (index, trigger) = cleanup;
        drop(trigger);
        self.inbox.lock().unwrap().slots[index] = HealthSlot::Free;
        true
    }
    pub fn finish(&self, diagnosis: Diagnosis) {
        drop(diagnosis);
        self.inbox.lock().unwrap().active = false;
    }
    pub fn closed_and_empty(&self) -> bool {
        let inbox = self.inbox.lock().unwrap();
        !inbox.accepting
            && !inbox.active
            && inbox.len == 0
            && inbox.slots.iter().all(|s| matches!(s, HealthSlot::Free))
    }
    #[cfg(test)]
    pub fn empty(&self) -> bool {
        let inbox = self.inbox.lock().unwrap();
        !inbox.active && inbox.len == 0 && inbox.slots.iter().all(|s| matches!(s, HealthSlot::Free))
    }
}
pub(super) struct Iteration {
    pub warning_at: Instant,
    pub wait_until: Option<Instant>,
}
pub(super) enum DiagnosisStage {
    Ready,
    Waiting,
    Preparing {
        admission: Admission,
        iteration: Iteration,
        pass: ProbePass,
    },
    Running {
        id: RequestId,
        iteration: Iteration,
    },
    Backoff {
        until: Instant,
        warning_at: Instant,
        pending: Option<RequestId>,
    },
    Draining {
        id: RequestId,
    },
    Done {
        pending: Option<RequestId>,
    },
}
pub(super) struct Diagnosis {
    pub target: Arc<CapturedTarget>,
    pub began: Instant,
    pub timeout_ms: i64,
    pub min_ms: i64,
    pub max_ms: i64,
    pub errors: usize,
    pub stage: DiagnosisStage,
}
impl Diagnosis {
    pub(super) fn new(trigger: Trigger, config: HealthConfig, now: Instant) -> Self {
        let bound = (config.blacklist_interval.as_millis() as i64) - 1000;
        Self {
            target: trigger.target,
            began: now,
            timeout_ms: trigger.seed_ms,
            min_ms: 10_000.min(bound),
            max_ms: 180_000.min(bound),
            errors: 0,
            stage: DiagnosisStage::Ready,
        }
    }
    pub fn iteration(
        &mut self,
        config: HealthConfig,
        now: Instant,
    ) -> Option<(Iteration, Duration)> {
        let warning_at = self.began + config.warning_tolerance;
        let remaining = if now <= warning_at {
            warning_at.duration_since(now).as_micros() as i128
        } else {
            -(now.duration_since(warning_at).as_micros() as i128)
        } / 1000;
        let timeout = remaining.min(
            i128::from(self.max_ms)
                .min((i128::from(self.timeout_ms) * 2).max(i128::from(self.min_ms))),
        );
        self.timeout_ms = i64::try_from(timeout).unwrap_or(i64::MIN);
        (self.timeout_ms > 0).then(|| {
            (
                Iteration {
                    warning_at,
                    wait_until: None,
                },
                Duration::from_millis(self.timeout_ms as u64),
            )
        })
    }
    pub fn outcome(
        &mut self,
        id: RequestId,
        status: &CompletionStatus,
        retired: bool,
        ingress: &HealthIngress,
        now: Instant,
    ) {
        let DiagnosisStage::Running {
            id: current,
            iteration,
        } = &self.stage
        else {
            return;
        };
        if *current != id {
            return;
        }
        let warning_at = iteration.warning_at;
        let pending = (!retired).then_some(id);
        let errno = match status {
            CompletionStatus::IoError(e) => e.raw_os_error().filter(|n| *n != 0),
            _ => None,
        };
        let again =
            matches!(status, CompletionStatus::IoError(e) if e.kind() == io::ErrorKind::WouldBlock);
        let retry = matches!(status, CompletionStatus::TimedOut) || again || errno.is_some();
        if !retry {
            self.stage = DiagnosisStage::Done { pending };
            return;
        }
        if again {
            self.stage = DiagnosisStage::Backoff {
                until: now + Duration::from_millis(100),
                warning_at,
                pending,
            };
            return;
        }
        if errno.is_some() {
            self.errors += 1;
        }
        if now >= warning_at || (errno.is_some() && self.errors >= 10) {
            ingress.warn(now);
            self.stage = DiagnosisStage::Done { pending };
        } else {
            self.stage =
                pending.map_or(DiagnosisStage::Ready, |id| DiagnosisStage::Draining { id });
        }
    }
    pub fn retired(&mut self, id: RequestId) {
        match &mut self.stage {
            DiagnosisStage::Draining { id: current } if *current == id => {
                self.stage = DiagnosisStage::Ready
            }
            DiagnosisStage::Done { pending } | DiagnosisStage::Backoff { pending, .. }
                if *pending == Some(id) =>
            {
                *pending = None
            }
            _ => (),
        }
    }
}
pub(super) struct ProbePass {
    pub target: Arc<CapturedTarget>,
    pub offset: usize,
    pub length: usize,
    pub chunk: usize,
}
impl ProbePass {
    pub fn window(&self) -> Range<usize> {
        let w = &self.target.options.window;
        w.start.saturating_sub(self.offset).min(self.length)
            ..w.end.saturating_sub(self.offset).min(self.length)
    }
    pub fn operation(&self) -> io::Result<Operation> {
        let buffer = AlignedBuffer::zeroed(self.length, self.target.options.alignment)?;
        match self.target.segment(self.offset, self.length)? {
            Target::File { file, offset } => Operation::read(file, buffer, offset),
            Target::Named { file, offset } => {
                file.operation(buffer, offset, file_io::Direction::Read)
            }
            Target::Block(region) => {
                let (file, offset, _) = region.file_region();
                Ok(Operation::read(file, buffer, offset)?.keep_alive(region))
            }
        }
    }
    pub fn advance(&mut self) -> bool {
        self.offset += self.length;
        self.length = self.chunk.min(self.target.options.length - self.offset);
        self.length != 0
    }
}
