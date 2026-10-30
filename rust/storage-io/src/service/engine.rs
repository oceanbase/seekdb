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

use super::admission::{FINISHED, WAITING};
use super::scheduler::Scheduler;
use super::*;
use file_io::{Retired, SubmissionId};
use std::collections::{BTreeSet, HashMap};
const QUANTUM: usize = 64;

pub(super) enum Destination {
    Caller(mpsc::Sender<Completion>),
    Diagnosis,
}
pub(super) enum WorkOwner {
    Ordinary,
    Probe(ProbePass),
}
pub(super) struct Incoming {
    pub owner: WorkOwner,
    pub operation: Operation,
    pub cell: Arc<ControlCell>,
    pub destination: Destination,
}
pub(super) enum Management {
    Register(DeviceConfig, mpsc::Sender<io::Result<()>>),
    RegisterWithConfig(
        Generation,
        Config,
        DeviceConfig,
        mpsc::Sender<Result<ConfigReceipt, ConfigError>>,
    ),
    Reconfigure(
        Generation,
        Config,
        mpsc::Sender<Result<ConfigReceipt, ConfigError>>,
    ),
    Snapshot(mpsc::Sender<ServiceSnapshot>),
}
pub(super) enum Retirement {
    Complete(DeviceId, usize, SubmissionId, Retired),
    Failed(DeviceId, usize, io::Error),
}
struct ActorLifetime(Arc<Shared>);
impl Drop for ActorLifetime {
    fn drop(&mut self) {
        let mut gate = self
            .0
            .gate
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        gate.accepting = false;
        gate.removals.clear();
        let waiting = std::mem::take(&mut gate.waiting);
        drop(gate);
        drop(waiting);
        self.0.health.close();
        self.0.wake.notify();
    }
}
struct RetirementReader {
    device: DeviceId,
    stop: Arc<AtomicBool>,
    join: Option<JoinHandle<()>>,
    activate: Option<mpsc::Sender<()>>,
}
impl RetirementReader {
    fn start(
        shared: &Shared,
        id: DeviceId,
        lane: usize,
        backend: Arc<Backend>,
    ) -> io::Result<Self> {
        let (activate, activated) = mpsc::channel();
        let stop = Arc::new(AtomicBool::new(false));
        let stopping = Arc::clone(&stop);
        let retirement = shared.retirement.clone();
        let wake = Arc::clone(&shared.wake);
        let join = thread::Builder::new()
            .name("storage-retire".into())
            .spawn(move || {
                if activated.recv().is_err() {
                    return;
                }
                while !stopping.load(Ordering::Acquire) {
                    let event = match backend.retire(Instant::now() + Duration::from_millis(25)) {
                        Ok(Some((submission, retired))) => {
                            Retirement::Complete(id, lane, submission, retired)
                        }
                        Ok(None) => continue,
                        Err(error) => {
                            let _ = retirement.send(Retirement::Failed(id, lane, error));
                            wake.notify();
                            return;
                        }
                    };
                    if retirement.send(event).is_err() {
                        return;
                    }
                    wake.notify();
                }
            })?;
        Ok(Self {
            device: id,
            stop,
            join: Some(join),
            activate: Some(activate),
        })
    }
    fn activate(&mut self) {
        let _ = self.activate.take().unwrap().send(());
    }
    fn stop_and_detach(mut self) {
        drop(self.activate.take());
        self.stop.store(true, Ordering::Release);
        drop(self.join.take());
    }
}
impl Drop for RetirementReader {
    fn drop(&mut self) {
        drop(self.activate.take());
        self.stop.store(true, Ordering::Release);
        if let Some(join) = self.join.take() {
            let _ = join.join();
        }
    }
}
struct BackendState {
    backend: Arc<Backend>,
    used: [usize; 2],
    failure: Option<io::Error>,
}
struct DeviceState {
    limits: DeviceLimits,
    lanes: Vec<BackendState>,
    used: [usize; 2],
    closed: bool,
}
struct PreparedDevice {
    id: DeviceId,
    state: DeviceState,
    readers: Vec<RetirementReader>,
}
#[derive(Clone, Copy)]
struct Permit {
    device: DeviceId,
    lane: usize,
    class: RequestClass,
    cost: DepthCost,
}
pub(super) enum Stage {
    Queued(Operation),
    Submitted(SubmissionId),
}
#[derive(Clone, Copy, Eq, Ord, PartialEq, PartialOrd)]
struct LaneKey {
    device: DeviceId,
    group: GroupId,
    class: RequestClass,
    execution: Execution,
    rate_exempt: bool,
    retry_route: Option<usize>,
}
pub(super) struct Active {
    pub stage: Stage,
    pub transferred: usize,
    error_retries: usize,
    destination: Option<Destination>,
    pub(super) owner: WorkOwner,
    window: Range<usize>,
    pub(super) cell: Arc<ControlCell>,
    group: GroupId,
    route: Option<usize>,
    last_cost: DepthCost,
    attempts: u64,
}
pub(super) struct Engine {
    pub shared: Arc<Shared>,
    devices: BTreeMap<DeviceId, DeviceState>,
    readers: Vec<RetirementReader>,
    pub active: BTreeMap<RequestId, Active>,
    submitted: HashMap<SubmissionId, (RequestId, Permit)>,
    queued: VecDeque<RequestId>,
    deadlines: BTreeSet<(Instant, RequestId)>,
    pub(super) config: Arc<Config>,
    scheduler: Scheduler,
    stopping: bool,
    prefer_probe: bool,
    control_cursor: Option<RequestId>,
    scan_epoch: u64,
    admission_cursors: [Option<RequestId>; 2],
    admission_scan_epochs: [u64; 2],
    probe_attempts: u64,
    health: Option<Diagnosis>,
    #[cfg(test)]
    pub(super) reader_start_failure: Option<usize>,
    #[cfg(test)]
    pub(super) registration_precommit: Option<Box<dyn FnOnce() + Send>>,
}
impl Engine {
    pub fn new(shared: Arc<Shared>) -> Self {
        let config = Arc::clone(&shared.gate.lock().unwrap().config);
        let scheduler = Scheduler::new(&config, Instant::now());
        Self {
            shared,
            devices: BTreeMap::new(),
            readers: Vec::new(),
            active: BTreeMap::new(),
            submitted: HashMap::new(),
            queued: VecDeque::new(),
            deadlines: BTreeSet::new(),
            config,
            scheduler,
            stopping: false,
            prefer_probe: true,
            control_cursor: None,
            scan_epoch: 0,
            admission_cursors: [None; 2],
            admission_scan_epochs: [0; 2],
            probe_attempts: 0,
            health: None,
            #[cfg(test)]
            reader_start_failure: None,
            #[cfg(test)]
            registration_precommit: None,
        }
    }
    pub fn run(
        mut self,
        commands: mpsc::Receiver<Management>,
        retired: mpsc::Receiver<Retirement>,
    ) -> io::Result<()> {
        let _lifetime = ActorLifetime(Arc::clone(&self.shared));
        loop {
            let epoch = self.shared.wake.epoch();
            let mut busy = false;
            for n in 0..QUANTUM {
                let Ok(event) = retired.try_recv() else {
                    break;
                };
                self.retirement(event);
                busy |= n + 1 == QUANTUM;
            }
            busy |= self.drain_gate();
            busy |= self.controls(Instant::now());
            busy |= self.grant_admission();
            for n in 0..8 {
                let Ok(command) = commands.try_recv() else {
                    break;
                };
                self.manage(command);
                busy |= n == 7;
            }
            busy |= self.health_turn(Instant::now());
            busy |= self.dispatch(Instant::now());
            self.reap_devices();
            if self.stopping
                && !busy
                && self.active.is_empty()
                && self.health.is_none()
                && self.shared.health.closed_and_empty()
            {
                let gate = self.shared.gate.lock().unwrap();
                if gate.published.is_empty()
                    && gate.waiting.iter().all(BTreeMap::is_empty)
                    && gate.removals.is_empty()
                {
                    break;
                }
            }
            if busy {
                continue;
            }
            let deadline = self
                .deadlines
                .first()
                .map(|(d, _)| d.saturating_duration_since(Instant::now()));
            let delay = match (deadline, self.scheduler.delay()) {
                (Some(a), Some(b)) => Some(a.min(b)),
                (a, b) => a.or(b),
            };
            let delay = match (delay, self.health_delay(Instant::now())) {
                (Some(a), Some(b)) => Some(a.min(b)),
                (a, b) => a.or(b),
            };
            self.shared.wake.wait(epoch, delay);
        }
        drop(self.readers);
        let mut result = Ok(());
        for device in self.devices.into_values() {
            for lane in device.lanes {
                let stopped = Arc::try_unwrap(lane.backend)
                    .map_err(|_| io::Error::other("retirement reader retained backend"))?
                    .shutdown();
                if result.is_ok() {
                    result = stopped;
                }
            }
        }
        result
    }
    fn health_delay(&self, now: Instant) -> Option<Duration> {
        let health = self.health.as_ref()?;
        match health.stage {
            DiagnosisStage::Backoff { until, .. } => Some(until.saturating_duration_since(now)),
            DiagnosisStage::Running { ref iteration, .. } => iteration
                .wait_until
                .map(|until| until.saturating_duration_since(now)),
            DiagnosisStage::Preparing { ref admission, .. } => admission
                .cell
                .options
                .policy
                .deadline
                .map(|until| until.saturating_duration_since(now)),
            DiagnosisStage::Draining { .. } | DiagnosisStage::Waiting => Some(
                (health.began + self.config.health.warning_tolerance)
                    .saturating_duration_since(now),
            ),
            DiagnosisStage::Ready | DiagnosisStage::Done { pending: None } => Some(Duration::ZERO),
            _ => None,
        }
    }
    pub(super) fn health_turn(&mut self, now: Instant) -> bool {
        let mut cleaned = false;
        if self.stopping || self.shared.health.warning() {
            cleaned = self.shared.health.discard_one();
            if let Some(health) = &mut self.health {
                match health.stage {
                    DiagnosisStage::Running { .. } => (),
                    DiagnosisStage::Backoff { pending, .. } | DiagnosisStage::Done { pending } => {
                        health.stage = DiagnosisStage::Done { pending }
                    }
                    DiagnosisStage::Draining { id } => {
                        health.stage = DiagnosisStage::Done { pending: Some(id) }
                    }
                    _ => health.stage = DiagnosisStage::Done { pending: None },
                }
            }
            if self.health.is_none() {
                return cleaned;
            }
        }
        if self.health.is_none() {
            self.health = self.shared.health.activate(now);
        }
        let Some(mut health) = self.health.take() else {
            return false;
        };
        let stage = std::mem::replace(&mut health.stage, DiagnosisStage::Ready);
        let mut busy = cleaned;
        match stage {
            DiagnosisStage::Ready => {
                if let Some((iteration, timeout)) = health.iteration(self.config.health, now) {
                    let result = self.prepare_probe(&health.target, timeout, now);
                    match result {
                        Ok((admission, pass)) => {
                            health.stage = DiagnosisStage::Preparing {
                                admission,
                                iteration,
                                pass,
                            }
                        }
                        Err(error) => {
                            let id = RequestId(0);
                            health.stage = DiagnosisStage::Running { id, iteration };
                            health.outcome(
                                id,
                                &CompletionStatus::IoError(error),
                                true,
                                &self.shared.health,
                                now,
                            );
                        }
                    }
                } else if now >= health.began + self.config.health.warning_tolerance {
                    self.shared.health.warn(now);
                    health.stage = DiagnosisStage::Done { pending: None };
                } else {
                    health.stage = DiagnosisStage::Waiting;
                }
                busy = true;
            }
            DiagnosisStage::Preparing {
                mut admission,
                mut iteration,
                pass,
            } => match admission.ready() {
                Ok(false) => {
                    health.stage = DiagnosisStage::Preparing {
                        admission,
                        iteration,
                        pass,
                    }
                }
                ready => {
                    let id = admission.id();
                    let result = ready.and_then(|_| pass.operation()).and_then(|operation| {
                        iteration.wait_until = Some(
                            Instant::now()
                                + self
                                    .config
                                    .health
                                    .wait_cap
                                    .min(Duration::from_millis(health.timeout_ms as u64)),
                        );
                        admission.publish_probe(operation, pass)
                    });
                    health.stage = DiagnosisStage::Running { id, iteration };
                    if let Err(error) = result {
                        let status = if error.kind() == io::ErrorKind::TimedOut {
                            CompletionStatus::TimedOut
                        } else {
                            CompletionStatus::IoError(error)
                        };
                        health.outcome(id, &status, true, &self.shared.health, now);
                    }
                    busy = true;
                }
            },
            DiagnosisStage::Backoff {
                until,
                warning_at,
                pending,
            } => {
                if now < until {
                    health.stage = DiagnosisStage::Backoff {
                        until,
                        warning_at,
                        pending,
                    };
                } else if now >= warning_at {
                    self.shared.health.warn(now);
                    health.stage = DiagnosisStage::Done { pending };
                } else {
                    health.stage =
                        pending.map_or(DiagnosisStage::Ready, |id| DiagnosisStage::Draining { id });
                    busy = true;
                }
            }
            DiagnosisStage::Waiting => {
                if now >= health.began + self.config.health.warning_tolerance {
                    self.shared.health.warn(now);
                    health.stage = DiagnosisStage::Done { pending: None };
                } else {
                    health.stage = DiagnosisStage::Waiting;
                }
            }
            DiagnosisStage::Draining { id } => {
                if now >= health.began + self.config.health.warning_tolerance {
                    self.shared.health.warn(now);
                    health.stage = DiagnosisStage::Done { pending: Some(id) };
                } else {
                    health.stage = DiagnosisStage::Draining { id };
                }
            }
            DiagnosisStage::Running { id, iteration } => {
                let expired = iteration.wait_until.is_some_and(|until| now >= until);
                health.stage = DiagnosisStage::Running { id, iteration };
                if expired {
                    self.health = Some(health);
                    self.cancel(id, CompletionStatus::TimedOut);
                    return true;
                }
            }
            stage => health.stage = stage,
        }
        if matches!(health.stage, DiagnosisStage::Done { pending: None }) {
            self.shared.health.finish(health);
            busy = true;
        } else {
            self.health = Some(health);
        }
        busy
    }
    fn prepare_probe(
        &self,
        target: &Arc<health::CapturedTarget>,
        timeout: Duration,
        now: Instant,
    ) -> io::Result<(Admission, ProbePass)> {
        let limits = self
            .config
            .devices
            .get(&target.options.device)
            .ok_or(io::ErrorKind::BrokenPipe)?;
        let alignment = target.options.alignment;
        let chunk = (limits.max_probe_bytes / alignment) * alignment;
        if chunk == 0 {
            return Err(invalid("Probe reserve cannot fit target alignment"));
        }
        let pass = ProbePass {
            target: Arc::clone(target),
            offset: 0,
            length: chunk.min(target.options.length),
            chunk,
        };
        let mut options = target.options.clone();
        options.class = RequestClass::Probe;
        options.rate_exempt = true;
        options.length = pass.length;
        options.window = pass.window();
        options.policy.deadline = Some(now + timeout);
        options.policy.completion = CompletionRequirement::RequestedWindow;
        let admission = Admission::begin(Arc::clone(&self.shared), options)?;
        Ok((admission, pass))
    }
    pub(super) fn drain_gate(&mut self) -> bool {
        self.drain_removals();
        let (published, stopping, busy) = {
            let mut gate = self.shared.gate.lock().unwrap();
            for (id, device) in &mut self.devices {
                device.closed |= gate.devices.get(id) == Some(&false);
            }
            let count = QUANTUM.min(gate.published.len());
            let published: Vec<_> = gate.published.drain(..count).collect();
            (published, !gate.accepting, !gate.published.is_empty())
        };
        if stopping && !self.stopping {
            self.stopping = true;
            self.shared.health.close();
        }
        for incoming in published {
            self.accept(incoming);
        }
        busy
    }
    pub fn accept(&mut self, incoming: Incoming) {
        let Incoming {
            operation,
            cell,
            destination,
            owner,
        } = incoming;
        let id = cell.id;
        let options = &cell.options;
        let group = if self.config.groups.contains_key(&options.group) {
            options.group
        } else {
            self.config.default_group
        };
        let cost = self.devices[&options.device]
            .limits
            .cost(options.class, options.length);
        let active = Active {
            stage: Stage::Queued(operation.keep_alive(Arc::clone(&cell.charge))),
            transferred: 0,
            error_retries: 0,
            destination: Some(destination),
            owner,
            window: options.window.clone(),
            group,
            route: None,
            last_cost: cost,
            attempts: 0,
            cell,
        };
        if self.stopping {
            self.finish(active, None, CompletionStatus::Shutdown);
            return;
        }
        let Some(device) = self.devices.get(&active.cell.options.device) else {
            self.finish(active, None, CompletionStatus::Shutdown);
            return;
        };
        if device.closed {
            let failure = device.lanes.iter().find_map(|lane| lane.failure.as_ref());
            let status = failure
                .map(|error| CompletionStatus::IoError(copy_error(error)))
                .unwrap_or(CompletionStatus::Shutdown);
            self.finish(active, None, status);
            return;
        }
        if let Some(deadline) = active.cell.options.policy.deadline {
            self.deadlines.insert((deadline, id));
        }
        self.active.insert(id, active);
        self.queued.push_back(id);
    }
    pub(super) fn grant_admission(&mut self) -> bool {
        let mut busy = false;
        for class in [RequestClass::Probe, RequestClass::Normal] {
            for n in 0..QUANTUM {
                busy |= n + 1 == QUANTUM;
                let mut gate = self.shared.gate.lock().unwrap();
                let Some((_, cell)) = gate.waiting[class.index()].first_key_value() else {
                    break;
                };
                let cell = Arc::clone(cell);
                if !gate.accepting
                    || cell.phase.load(Ordering::Acquire) != WAITING
                    || cell.check().is_err()
                    || gate.devices.get(&cell.options.device) != Some(&true)
                    || self
                        .devices
                        .get(&cell.options.device)
                        .is_none_or(|d| d.closed)
                {
                    let removed = gate.waiting[class.index()].remove(&cell.id);
                    drop(gate);
                    drop(removed);
                    continue;
                }
                let mut ledger = self.shared.ledger.lock().unwrap();
                if !ledger.reserve_bytes(&cell) && cell.phase.load(Ordering::Acquire) == WAITING {
                    break;
                }
                drop(ledger);
                let removed = gate.waiting[class.index()].remove(&cell.id);
                drop(gate);
                drop(removed);
                self.shared.wake.notify();
            }
        }
        busy
    }
    pub(super) fn controls(&mut self, now: Instant) -> bool {
        if self.control_cursor.is_none() {
            self.scan_epoch = self.shared.wake.epoch();
        }
        let ids: Vec<_> = self
            .active
            .range((
                self.control_cursor
                    .map_or(std::ops::Bound::Unbounded, std::ops::Bound::Excluded),
                std::ops::Bound::Unbounded,
            ))
            .take(QUANTUM + 1)
            .map(|(&id, _)| id)
            .collect();
        let more = ids.len() > QUANTUM;
        for id in ids.into_iter().take(QUANTUM) {
            self.control_cursor = Some(id);
            let active = &self.active[&id];
            let options = &active.cell.options;
            let status =
                if self.stopping || self.devices.get(&options.device).is_none_or(|d| d.closed) {
                    Some(CompletionStatus::Shutdown)
                } else if active.cell.cancelled.load(Ordering::Acquire) {
                    Some(CompletionStatus::Cancelled)
                } else if options
                    .policy
                    .deadline
                    .is_some_and(|deadline| deadline <= now)
                {
                    Some(CompletionStatus::TimedOut)
                } else {
                    None
                };
            if let Some(status) = status {
                self.cancel(id, status);
            }
        }
        if !more {
            self.control_cursor = None;
        }
        more | self.prune_admissions() | (self.scan_epoch != self.shared.wake.epoch())
    }
    fn prune_admissions(&mut self) -> bool {
        let mut busy = false;
        for class in 0..2 {
            if self.admission_cursors[class].is_none() {
                self.admission_scan_epochs[class] = self.shared.wake.epoch();
            }
            let mut gate = self.shared.gate.lock().unwrap();
            let ids: Vec<_> = gate.waiting[class]
                .range((
                    self.admission_cursors[class]
                        .map_or(std::ops::Bound::Unbounded, std::ops::Bound::Excluded),
                    std::ops::Bound::Unbounded,
                ))
                .take(QUANTUM + 1)
                .map(|(&id, _)| id)
                .collect();
            let more = ids.len() > QUANTUM;
            let mut removed = Vec::new();
            for id in ids.into_iter().take(QUANTUM) {
                self.admission_cursors[class] = Some(id);
                let cell = &gate.waiting[class][&id];
                if !gate.accepting
                    || cell.phase.load(Ordering::Acquire) != WAITING
                    || cell.check().is_err()
                    || gate.devices.get(&cell.options.device) != Some(&true)
                    || self
                        .devices
                        .get(&cell.options.device)
                        .is_none_or(|d| d.closed)
                {
                    removed.push(gate.waiting[class].remove(&id));
                }
            }
            drop(gate);
            drop(removed);
            if !more {
                self.admission_cursors[class] = None;
            }
            busy |= more || self.admission_scan_epochs[class] != self.shared.wake.epoch();
        }
        busy
    }
    fn route(&self, active: &Active, operation: &Operation) -> Option<Permit> {
        let options = &active.cell.options;
        let device = &self.devices[&options.device];
        if device.closed {
            return None;
        }
        let limits = device.limits;
        let cost = limits.cost(options.class, operation.remaining_len());
        let class = options.class.index();
        if cost.effective
            > limits
                .capacity(options.class)
                .saturating_sub(device.used[class])
            || cost.effective > limits.depth.saturating_sub(device.used[0] + device.used[1])
        {
            return None;
        }
        let routes: &[usize] = match (active.route.as_ref(), options.execution, device.lanes.len())
        {
            (Some(route), _, _) => std::slice::from_ref(route),
            (_, Execution::PreferNative, 2) => &[1, 0],
            _ => &[0],
        };
        for &index in routes {
            let lane = &device.lanes[index];
            let capacity = lane.backend.capacity();
            if lane.failure.is_none()
                && lane.used[0] + lane.used[1] < capacity
                && (class == 1 || lane.used[0] < capacity - 1)
            {
                return Some(Permit {
                    device: options.device,
                    lane: index,
                    class: options.class,
                    cost,
                });
            }
        }
        None
    }
    fn candidates(
        &self,
        refused: &BTreeSet<(DeviceId, usize)>,
    ) -> (Vec<(RequestId, Permit)>, BTreeSet<GroupId>) {
        let mut candidates = Vec::new();
        let mut groups = BTreeSet::new();
        let mut heads = BTreeSet::new();
        for &id in &self.queued {
            let active = &self.active[&id];
            let options = &active.cell.options;
            let key = LaneKey {
                device: options.device,
                group: active.group,
                class: options.class,
                execution: options.execution,
                rate_exempt: options.rate_exempt,
                retry_route: active.route,
            };
            if !heads.insert(key) {
                continue;
            }
            if let Stage::Queued(operation) = &active.stage {
                if let Some(permit) = self.route(active, operation) {
                    if refused.contains(&(permit.device, permit.lane)) {
                        continue;
                    }
                    candidates.push((id, permit));
                    if options.class == RequestClass::Normal && !options.rate_exempt {
                        groups.insert(active.group);
                    }
                }
            }
        }
        (candidates, groups)
    }
    pub fn dispatch(&mut self, now: Instant) -> bool {
        self.dispatch_with(now, Backend::submit)
    }
    pub fn dispatch_with(
        &mut self,
        now: Instant,
        mut submit: impl FnMut(&Backend, Operation) -> Result<SubmissionId, file_io::SubmitFailure>,
    ) -> bool {
        if self.stopping {
            return false;
        }
        let mut refused = BTreeSet::new();
        for n in 0..QUANTUM {
            let (candidates, eligible) = self.candidates(&refused);
            self.scheduler.tick(now, &eligible);
            let normal_group = self.scheduler.choose(&eligible);
            let probe = candidates
                .iter()
                .find(|(_, p)| p.class == RequestClass::Probe)
                .copied();
            let normal = candidates
                .iter()
                .find(|(id, p)| {
                    p.class == RequestClass::Normal
                        && (self.active[id].cell.options.rate_exempt
                            || Some(self.active[id].group) == normal_group)
                })
                .copied();
            let selected = if self.prefer_probe {
                probe.or(normal)
            } else {
                normal.or(probe)
            };
            let Some((id, permit)) = selected else {
                return false;
            };
            self.prefer_probe = permit.class != RequestClass::Probe;
            let mut active = self.active.remove(&id).unwrap();
            self.queued.retain(|queued| *queued != id);
            if active.cell.cancelled.load(Ordering::Acquire)
                || active
                    .cell
                    .options
                    .policy
                    .deadline
                    .is_some_and(|d| d <= now)
            {
                self.remove_deadline(&active);
                let status = if active.cell.cancelled.load(Ordering::Acquire) {
                    CompletionStatus::Cancelled
                } else {
                    CompletionStatus::TimedOut
                };
                self.finish(active, None, status);
                continue;
            }
            let Stage::Queued(operation) = active.stage else {
                unreachable!()
            };
            match submit(
                &self.devices[&permit.device].lanes[permit.lane].backend,
                operation,
            ) {
                Ok(submission) => {
                    let device = self.devices.get_mut(&permit.device).unwrap();
                    device.used[permit.class.index()] += permit.cost.effective;
                    device.lanes[permit.lane].used[permit.class.index()] += 1;
                    if permit.class == RequestClass::Normal {
                        if active.cell.options.rate_exempt {
                            self.scheduler.exempt_attempt(active.group);
                        } else {
                            self.scheduler.charge(active.group);
                        }
                    } else {
                        self.probe_attempts = self.probe_attempts.saturating_add(1);
                    }
                    active.attempts = active.attempts.saturating_add(1);
                    active.last_cost = permit.cost;
                    active.route = Some(permit.lane);
                    active.stage = Stage::Submitted(submission);
                    self.submitted.insert(submission, (id, permit));
                    self.active.insert(id, active);
                }
                Err(failure) => {
                    active.stage = Stage::Queued(failure.operation);
                    if failure.error.kind() == io::ErrorKind::WouldBlock {
                        refused.insert((permit.device, permit.lane));
                        self.active.insert(id, active);
                        self.queued.push_front(id);
                    } else {
                        self.remove_deadline(&active);
                        self.finish(active, None, CompletionStatus::IoError(failure.error));
                    }
                }
            }
            if n + 1 == QUANTUM {
                return true;
            }
        }
        false
    }
    fn release(&mut self, permit: Permit) {
        let device = self.devices.get_mut(&permit.device).unwrap();
        device.used[permit.class.index()] -= permit.cost.effective;
        device.lanes[permit.lane].used[permit.class.index()] -= 1;
    }
    pub fn retired(&mut self, submission: SubmissionId, retired: Retired) {
        let Some((id, permit)) = self.submitted.remove(&submission) else {
            return;
        };
        self.release(permit);
        let mut active = self.active.remove(&id).unwrap();
        if active.destination.is_none() {
            drop(retired);
            self.finish(active, None, CompletionStatus::Shutdown);
            return;
        }
        let Retired { operation, result } = retired;
        let remaining = operation.remaining_len();
        let policy = active.cell.options.policy;
        let status = match result {
            Ok(count) if count <= remaining => {
                active.transferred += count;
                if count != 0
                    && count < remaining
                    && policy
                        .retry_alignment
                        .is_some_and(|a| count.is_multiple_of(a.get()))
                {
                    active.stage =
                        Stage::Queued(operation.advance(count).expect("validated physical prefix"));
                    self.active.insert(id, active);
                    self.queued.push_back(id);
                    return;
                }
                if count == 0 && remaining != 0 && operation.direction() == Direction::Write {
                    CompletionStatus::IoError(io::ErrorKind::WriteZero.into())
                } else if active.transferred < active.window.end {
                    CompletionStatus::ShortIo
                } else if matches!(policy.completion, CompletionRequirement::FullBuffer)
                    && active.transferred < operation.buffer().as_slice().len()
                {
                    CompletionStatus::IncompleteEnvelope
                } else {
                    CompletionStatus::Complete
                }
            }
            Ok(_) => CompletionStatus::IoError(io::Error::new(
                io::ErrorKind::InvalidData,
                "invalid physical completion count",
            )),
            Err(error) => {
                if matches!(
                    error.kind(),
                    io::ErrorKind::WouldBlock | io::ErrorKind::Interrupted
                ) && active.error_retries < policy.max_error_retries
                {
                    active.error_retries += 1;
                    active.stage = Stage::Queued(operation);
                    self.active.insert(id, active);
                    self.queued.push_back(id);
                    return;
                }
                CompletionStatus::IoError(error)
            }
        };
        if matches!(status, CompletionStatus::Complete) {
            if let WorkOwner::Probe(pass) = &mut active.owner {
                if pass.advance() {
                    drop(operation);
                    active.window = pass.window();
                    active.transferred = 0;
                    match pass.operation() {
                        Ok(operation) => {
                            active.stage = Stage::Queued(
                                operation.keep_alive(Arc::clone(&active.cell.charge)),
                            );
                            self.active.insert(id, active);
                            self.queued.push_back(id);
                            return;
                        }
                        Err(error) => {
                            self.remove_deadline(&active);
                            self.finish(active, None, CompletionStatus::IoError(error));
                            return;
                        }
                    }
                }
            }
        }
        self.remove_deadline(&active);
        self.finish(active, Some(operation), status);
    }
    fn finish(&mut self, active: Active, operation: Option<Operation>, status: CompletionStatus) {
        let Active {
            stage,
            destination,
            cell,
            transferred,
            last_cost,
            attempts,
            group,
            owner,
            window,
            ..
        } = active;
        let mut operation = operation.or(match stage {
            Stage::Queued(o) => Some(o),
            Stage::Submitted(_) => None,
        });
        if let Some(operation) = &mut operation {
            operation.release_keep_alive();
        }
        cell.phase.store(FINISHED, Ordering::Release);
        match destination {
            Some(Destination::Caller(destination)) => {
                let _ = destination.send(Completion {
                    id: cell.id,
                    tag: cell.options.tag,
                    status,
                    transferred,
                    depth_cost: last_cost,
                    physical_attempts: attempts,
                    group,
                    operation,
                    window,
                    _charge: Arc::clone(&cell.charge),
                });
            }
            Some(Destination::Diagnosis) => {
                self.shared.health.record_outcome(&status);
                if let Some(health) = &mut self.health {
                    health.outcome(cell.id, &status, true, &self.shared.health, Instant::now());
                }
                drop(operation);
            }
            None => drop(operation),
        }
        let probe = matches!(owner, WorkOwner::Probe(_));
        let id = cell.id;
        drop(owner);
        drop(cell);
        if probe {
            if let Some(health) = &mut self.health {
                health.retired(id);
            }
        }
    }
    fn deliver(
        &mut self,
        active: &mut Active,
        operation: Option<Operation>,
        status: CompletionStatus,
        retired: bool,
    ) {
        active.cell.phase.store(FINISHED, Ordering::Release);
        match active.destination.take() {
            Some(Destination::Caller(destination)) => {
                let options = &active.cell.options;
                let _ = destination.send(Completion {
                    id: active.cell.id,
                    tag: options.tag,
                    status,
                    transferred: active.transferred,
                    depth_cost: active.last_cost,
                    physical_attempts: active.attempts,
                    group: active.group,
                    operation,
                    window: active.window.clone(),
                    _charge: Arc::clone(&active.cell.charge),
                });
            }
            Some(Destination::Diagnosis) => {
                self.shared.health.record_outcome(&status);
                if let Some(health) = &mut self.health {
                    health.outcome(
                        active.cell.id,
                        &status,
                        retired,
                        &self.shared.health,
                        Instant::now(),
                    );
                }
                drop(operation);
            }
            None => drop(operation),
        }
    }
    fn remove_deadline(&mut self, active: &Active) {
        if let Some(deadline) = active.cell.options.policy.deadline {
            self.deadlines.remove(&(deadline, active.cell.id));
        }
    }
    pub fn cancel(&mut self, id: RequestId, status: CompletionStatus) -> CancelOutcome {
        let Some(mut active) = self.active.remove(&id) else {
            return CancelOutcome::NotPending;
        };
        if active.destination.is_none() {
            self.active.insert(id, active);
            return CancelOutcome::NotPending;
        }
        self.remove_deadline(&active);
        self.deliver(&mut active, None, status, false);
        match active.stage {
            Stage::Queued(_) => {
                self.queued.retain(|queued| *queued != id);
                self.finish(active, None, CompletionStatus::Cancelled);
            }
            Stage::Submitted(submission) => {
                let (_, permit) = self.submitted[&submission];
                let _ = self.devices[&permit.device].lanes[permit.lane]
                    .backend
                    .request_cancel(submission);
                self.active.insert(id, active);
            }
        }
        CancelOutcome::Cancelled
    }
    fn retirement(&mut self, event: Retirement) {
        match event {
            Retirement::Complete(device, lane, submission, retired) => {
                if self
                    .submitted
                    .get(&submission)
                    .is_some_and(|(_, p)| p.device == device && p.lane == lane)
                {
                    self.retired(submission, retired);
                }
            }
            Retirement::Failed(device, lane, error) => self.backend_failed(device, lane, error),
        }
    }
    pub(super) fn backend_failed(&mut self, device: DeviceId, lane: usize, error: io::Error) {
        if !self.devices.contains_key(&device)
            && self.shared.gate.lock().unwrap().devices.get(&device) == Some(&false)
        {
            return;
        }
        let affected: Vec<_> = self
            .submitted
            .iter()
            .filter(|(_, (_, p))| p.device == device && p.lane == lane)
            .map(|(&id, _)| id)
            .collect();
        for submission in affected {
            let (id, permit) = self.submitted.remove(&submission).unwrap();
            self.release(permit);
            let active = self.active.remove(&id).unwrap();
            self.remove_deadline(&active);
            self.finish(active, None, CompletionStatus::IoError(copy_error(&error)));
        }
        let state = self.devices.get_mut(&device).unwrap();
        state.lanes[lane].failure = Some(copy_error(&error));
        let closed = state.lanes.iter().all(|l| l.failure.is_some());
        state.closed |= closed;
        if closed {
            self.shared
                .gate
                .lock()
                .unwrap()
                .devices
                .insert(device, false);
        }
        let affected: Vec<_> = self
            .queued
            .iter()
            .copied()
            .filter(|id| {
                let a = &self.active[id];
                a.cell.options.device == device
                    && (closed
                        || a.route == Some(lane)
                        || (lane == 0 && a.cell.options.execution == Execution::WorkerRequired))
            })
            .collect();
        for id in affected {
            let active = self.active.remove(&id).unwrap();
            self.queued.retain(|queued| *queued != id);
            self.remove_deadline(&active);
            self.finish(active, None, CompletionStatus::IoError(copy_error(&error)));
        }
        self.shared.wake.notify();
    }
    fn prepare_device(
        &self,
        device: DeviceConfig,
        config: &Config,
        readers: bool,
    ) -> io::Result<PreparedDevice> {
        if !config.devices.contains_key(&device.id)
            || self
                .shared
                .gate
                .lock()
                .unwrap()
                .devices
                .contains_key(&device.id)
        {
            return Err(invalid("device must have policy and a fresh identity"));
        }
        if device.worker.is_native()
            || device.worker.capacity() < 2
            || device
                .native
                .as_ref()
                .is_some_and(|b| !b.is_native() || b.capacity() < 2)
        {
            return Err(invalid(
                "backend route must have its own Probe envelope and a Normal envelope",
            ));
        }
        let mut lanes = vec![BackendState {
            backend: Arc::new(device.worker),
            used: [0; 2],
            failure: None,
        }];
        if let Some(native) = device.native {
            lanes.push(BackendState {
                backend: Arc::new(native),
                used: [0; 2],
                failure: None,
            });
        }
        let mut started = Vec::new();
        if readers {
            for (index, lane) in lanes.iter().enumerate() {
                #[cfg(test)]
                if self.reader_start_failure == Some(index) {
                    return Err(io::Error::other("injected retirement reader start failure"));
                }
                started.push(RetirementReader::start(
                    &self.shared,
                    device.id,
                    index,
                    Arc::clone(&lane.backend),
                )?);
            }
        }
        Ok(PreparedDevice {
            id: device.id,
            state: DeviceState {
                limits: config.devices[&device.id],
                lanes,
                used: [0; 2],
                closed: false,
            },
            readers: started,
        })
    }
    pub(super) fn register(&mut self, device: DeviceConfig, readers: bool) -> io::Result<()> {
        let mut prepared = self.prepare_device(device, &self.config, readers)?;
        let mut gate = self.shared.gate.lock().unwrap();
        if !gate.accepting {
            return Err(io::ErrorKind::BrokenPipe.into());
        }
        self.devices.insert(prepared.id, prepared.state);
        gate.devices.insert(prepared.id, true);
        for reader in &mut prepared.readers {
            reader.activate();
        }
        self.readers.extend(prepared.readers);
        Ok(())
    }
    pub(super) fn register_with_config(
        &mut self,
        expected: Generation,
        config: Config,
        device: DeviceConfig,
        now: Instant,
    ) -> Result<ConfigReceipt, ConfigError> {
        self.validate_config(expected, &config)?;
        let prepared = self
            .prepare_device(device, &config, true)
            .map_err(ConfigError::Invalid)?;
        #[cfg(test)]
        if let Some(hook) = self.registration_precommit.take() {
            hook();
        }
        self.commit_config(expected, config, Some(prepared), None, now)
    }
    fn drain_removals(&mut self) {
        let pending = std::mem::take(&mut self.shared.gate.lock().unwrap().removals);
        for (id, removal) in pending {
            let close = match removal {
                Removal::Close => true,
                Removal::Transaction {
                    expected,
                    reply,
                    close_requested,
                } => {
                    let result = self.remove_config(expected, id, Instant::now());
                    let close = close_requested && result.is_err();
                    let _ = reply.send(result);
                    close
                }
            };
            if close {
                let generation = self.shared.gate.lock().unwrap().generation;
                let _ = self.remove_config(generation, id, Instant::now());
            }
        }
    }
    pub(super) fn remove_config(
        &mut self,
        expected: Generation,
        device: DeviceId,
        now: Instant,
    ) -> Result<Generation, ConfigError> {
        {
            let gate = self.shared.gate.lock().unwrap();
            if !gate.accepting {
                return Err(ConfigError::Unavailable(io::ErrorKind::BrokenPipe.into()));
            }
            if expected != gate.generation {
                return Err(ConfigError::Stale(gate.generation));
            }
            if !gate.devices.contains_key(&device) {
                return Err(ConfigError::Invalid(io::ErrorKind::NotFound.into()));
            }
            if !self.config.devices.contains_key(&device) {
                return Ok(gate.generation);
            }
        }
        let mut config = (*self.config).clone();
        config.devices.remove(&device);
        config.validate().map_err(ConfigError::Invalid)?;
        self.commit_config(expected, config, None, Some(device), now)
            .map(|receipt| receipt.generation)
    }
    fn reap_devices(&mut self) {
        let gate = self.shared.gate.lock().unwrap();
        let removable: BTreeSet<_> = self
            .devices
            .iter()
            .filter(|(id, device)| {
                device.closed
                    && device.used == [0, 0]
                    && !self.active.values().any(|a| a.cell.options.device == **id)
                    && !gate.published.iter().any(|a| a.cell.options.device == **id)
            })
            .map(|(&id, _)| id)
            .collect();
        drop(gate);
        if removable.is_empty() {
            return;
        }
        for reader in self
            .readers
            .extract_if(.., |reader| removable.contains(&reader.device))
        {
            reader.stop_and_detach();
        }
        for id in removable {
            self.devices.remove(&id);
        }
    }
    pub(super) fn reconfigure(
        &mut self,
        expected: Generation,
        config: Config,
        now: Instant,
    ) -> Result<ConfigReceipt, ConfigError> {
        self.validate_config(expected, &config)?;
        self.commit_config(expected, config, None, None, now)
    }
    fn validate_config(&self, expected: Generation, config: &Config) -> Result<(), ConfigError> {
        config.validate().map_err(ConfigError::Invalid)?;
        let gate = self.shared.gate.lock().unwrap();
        if !gate.accepting {
            return Err(ConfigError::Unavailable(io::ErrorKind::BrokenPipe.into()));
        }
        if expected != gate.generation {
            return Err(ConfigError::Stale(gate.generation));
        }
        if gate
            .devices
            .keys()
            .any(|id| gate.config.devices.contains_key(id) != config.devices.contains_key(id))
        {
            return Err(ConfigError::Invalid(invalid(
                "device policy removal requires closing its registration",
            )));
        }
        drop(gate);
        expected
            .0
            .checked_add(1)
            .ok_or_else(|| ConfigError::Invalid(invalid("config generations exhausted")))?;
        Ok(())
    }
    fn commit_config(
        &mut self,
        expected: Generation,
        config: Config,
        device: Option<PreparedDevice>,
        closing: Option<DeviceId>,
        now: Instant,
    ) -> Result<ConfigReceipt, ConfigError> {
        let config = Arc::new(config);
        let mut gate = self.shared.gate.lock().unwrap();
        if !gate.accepting {
            return Err(ConfigError::Unavailable(io::ErrorKind::BrokenPipe.into()));
        }
        if expected != gate.generation {
            return Err(ConfigError::Stale(gate.generation));
        }
        let generation = Generation(
            expected
                .0
                .checked_add(1)
                .ok_or_else(|| ConfigError::Invalid(invalid("config generations exhausted")))?,
        );
        let mut ledger = self.shared.ledger.lock().unwrap();
        ledger.limits = config.effective_admission();
        gate.config = Arc::clone(&config);
        gate.generation = generation;
        self.config = config;
        self.shared.health.configure(self.config.health);
        self.scheduler.reconfigure(&self.config, now);
        for (id, device) in &mut self.devices {
            if let Some(limits) = self.config.devices.get(id) {
                device.limits = *limits;
            }
        }
        for active in self.active.values_mut() {
            if !self.config.groups.contains_key(&active.group) {
                active.group = self.config.default_group;
            }
        }
        // Collection allocation and
        // scheduler updates can only fail through process abort or panic.
        if let Some(mut prepared) = device {
            self.devices.insert(prepared.id, prepared.state);
            gate.devices.insert(prepared.id, true);
            for reader in &mut prepared.readers {
                reader.activate();
            }
            self.readers.extend(prepared.readers);
        }
        if let Some(id) = closing {
            gate.devices.insert(id, false);
            if let Some(device) = self.devices.get_mut(&id) {
                device.closed = true;
            }
        }
        let debt = ledger.debt();
        let physical_debt = self.devices.values().any(|device| {
            let limits = device.limits;
            device.used[0] > limits.capacity(RequestClass::Normal)
                || device.used[1] > limits.capacity(RequestClass::Probe)
                || device.used[0] + device.used[1] > limits.depth
        });
        drop(ledger);
        drop(gate);
        self.shared.wake.notify();
        Ok(ConfigReceipt {
            generation,
            debt,
            physical_debt,
        })
    }
    pub(super) fn manage(&mut self, command: Management) {
        self.drain_removals();
        match command {
            Management::Register(device, reply) => {
                let _ = reply.send(self.register(device, true));
            }
            Management::RegisterWithConfig(expected, config, device, reply) => {
                let _ =
                    reply.send(self.register_with_config(expected, config, device, Instant::now()));
            }
            Management::Reconfigure(expected, config, reply) => {
                let _ = reply.send(self.reconfigure(expected, config, Instant::now()));
            }
            Management::Snapshot(reply) => {
                let _ = reply.send(self.snapshot());
            }
        }
    }
    #[cfg(test)]
    pub fn backend(&self, device: DeviceId, lane: usize) -> Arc<Backend> {
        Arc::clone(&self.devices[&device].lanes[lane].backend)
    }
    pub fn snapshot(&self) -> ServiceSnapshot {
        let gate = self.shared.gate.lock().unwrap();
        let ledger = self.shared.ledger.lock().unwrap();
        let backlogged = self.queued.iter().map(|id| self.active[id].group).collect();
        ServiceSnapshot {
            health: self.shared.health.snapshot(false, Instant::now()),
            generation: gate.generation,
            config: (*gate.config).clone(),
            admitted_requests: ledger.usage.iter().map(|u| u.records).sum(),
            reserved_bytes: ledger.usage.iter().map(|u| u.bytes).sum(),
            usage: ledger.usage,
            debt: ledger.debt(),
            queued_requests: self.queued.len(),
            awaiting_retirement: self.submitted.len(),
            probe_attempts: self.probe_attempts,
            devices: self
                .devices
                .iter()
                .map(|(&id, state)| DeviceSnapshot {
                    id,
                    physical_depth: state.used,
                    depth_debt: [
                        state.used[0].saturating_sub(state.limits.capacity(RequestClass::Normal)),
                        state.used[1].saturating_sub(state.limits.probe_depth),
                    ],
                    backend_envelopes: state.lanes.iter().map(|l| l.used).collect(),
                    closed: state.closed,
                })
                .collect(),
            groups: self.scheduler.snapshots(&backlogged),
        }
    }
}
fn copy_error(error: &io::Error) -> io::Error {
    error
        .raw_os_error()
        .map(io::Error::from_raw_os_error)
        .unwrap_or_else(|| io::Error::new(error.kind(), error.to_string()))
}
