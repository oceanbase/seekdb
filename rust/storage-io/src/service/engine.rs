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

use super::admission::FINISHED;
use super::*;
use file_io::{Retired, SubmissionId};
use std::collections::{BTreeSet, HashMap};
const QUANTUM: usize = 64;

pub(crate) enum Destination {
    Caller(mpsc::Sender<Completion>),
    Device(DeviceId),
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
struct BackendState {
    backend: Arc<Backend>,
    used: [usize; 2],
    failure: Option<io::Error>,
}
struct DeviceState {
    limits: DeviceLimits,
    port: Arc<file_io::CompletionPort>,
    lanes: Vec<BackendState>,
    used: [usize; 2],
    closed: bool,
}
#[derive(Default)]
struct CompletedQueue {
    entries: VecDeque<Completion>,
    deliverable: usize,
}
struct PreparedDevice {
    id: DeviceId,
    state: DeviceState,
    completed: CompletedQueue,
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
pub(super) struct Active {
    pub stage: Stage,
    pub transferred: usize,
    error_retries: usize,
    destination: Option<Destination>,
    pub(super) owner: WorkOwner,
    window: Range<usize>,
    pub(super) cell: Arc<ControlCell>,
    route: Option<usize>,
    last_cost: DepthCost,
    attempts: u64,
}
pub(super) struct Driver {
    pub shared: Arc<Shared>,
    devices: BTreeMap<DeviceId, DeviceState>,
    pub active: HashMap<RequestId, Active>,
    completed: BTreeMap<DeviceId, CompletedQueue>,
    submitted: HashMap<SubmissionId, (RequestId, Permit)>,
    queued: VecDeque<RequestId>,
    pub(super) config: Arc<Config>,
    stopping: bool,
    probe_attempts: u64,
    health: Option<Diagnosis>,
}
impl Driver {
    pub fn new(shared: Arc<Shared>) -> Self {
        let config = Arc::clone(&shared.gate.lock().unwrap().config);
        Self {
            shared,
            devices: BTreeMap::new(),
            active: HashMap::new(),
            completed: BTreeMap::new(),
            submitted: HashMap::new(),
            queued: VecDeque::new(),
            config,
            stopping: false,
            probe_attempts: 0,
            health: None,
        }
    }
    pub(super) fn poll_device_completion(&mut self, device: DeviceId) -> (Option<Completion>, bool) {
        let mut busy = false;
        if self
            .completed
            .get(&device)
            .is_none_or(|queue| queue.deliverable == 0)
        {
            busy = self.progress(Some(device));
            if let Some(queue) = self.completed.get_mut(&device) {
                queue.deliverable = queue.entries.len().min(QUANTUM);
            }
        }
        let completion = self.completed.get_mut(&device).and_then(|queue| {
            if queue.deliverable == 0 {
                None
            } else {
                queue.deliverable -= 1;
                Some(queue.entries.pop_front().unwrap())
            }
        });
        if !self.devices.contains_key(&device)
            && self
                .completed
                .get(&device)
                .is_some_and(|queue| queue.entries.is_empty())
        {
            self.completed.remove(&device);
        }
        (completion, busy)
    }
    fn deliver_completion(&mut self, destination: Destination, completion: Completion) {
        match destination {
            Destination::Caller(sender) => {
                let _ = sender.send(completion);
            }
            Destination::Device(device) => self
                .completed
                .entry(device)
                .or_default()
                .entries
                .push_back(completion),
            Destination::Diagnosis => unreachable!(),
        }
        self.shared.wake.notify();
    }
    pub(super) fn progress(&mut self, selected: Option<DeviceId>) -> bool {
        let mut busy = self.accept_probes_and_close();
        for _ in 0..QUANTUM {
            let cancellation = self.shared.cancellations.lock().unwrap().pop_front();
            let Some(cancellation) = cancellation else {
                break;
            };
            let id = cancellation.id;
            if self
                .active
                .get(&id)
                .is_some_and(|active| matches!(active.stage, Stage::Queued(_)))
            {
                self.cancel(id, CompletionStatus::Cancelled);
            }
            busy = true;
        }
        let one = [selected.unwrap_or(DeviceId(0))];
        let devices = match selected {
            Some(_) => std::borrow::Cow::Borrowed(&one[..]),
            None => std::borrow::Cow::Owned(self.devices.keys().copied().collect::<Vec<_>>()),
        };
        for &device in devices.iter() {
            let Some(state) = self.devices.get(&device) else {
                continue;
            };
            let port = Arc::clone(&state.port);
            for _ in 0..QUANTUM {
                let Some((id, retired)) = port.try_retire() else {
                    break;
                };
                self.retired(id, retired);
                busy = true;
            }
            for lane in 0..self.devices[&device].lanes.len() {
                let state = &self.devices[&device].lanes[lane];
                if state.failure.is_none() {
                    if let Some(error) = state.backend.terminal_error() {
                        self.backend_failed(device, lane, error);
                    }
                }
            }
        }
        if self.health.is_some() || self.shared.health.has_work() {
            busy |= self.health_turn(Instant::now());
        }
        busy |= self.dispatch(Instant::now());
        self.reap_devices();
        busy
    }
    pub(super) fn shutdown_backends(&mut self) -> io::Result<()> {
        let mut result = Ok(());
        for device in std::mem::take(&mut self.devices).into_values() {
            for lane in device.lanes {
                let stopped = Arc::try_unwrap(lane.backend)
                    .map_err(|_| io::Error::other("backend still has a live owner"))?
                    .shutdown();
                if result.is_ok() {
                    result = stopped;
                }
            }
        }
        result
    }
    pub(super) fn quiescent(&self) -> bool {
        self.active.is_empty()
            && self.shared.cancellations.lock().unwrap().is_empty()
            && self.health.is_none()
            && self.shared.health.closed_and_empty()
            && self.shared.gate.lock().unwrap().pending_probes.is_empty()
    }
    pub(super) fn health_delay(&self, now: Instant) -> Option<Duration> {
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
        options.length = pass.length;
        options.window = pass.window();
        options.policy.deadline = Some(now + timeout);
        options.policy.completion = CompletionRequirement::RequestedWindow;
        let admission = Admission::begin(Arc::clone(&self.shared), options)?;
        Ok((admission, pass))
    }
    pub(super) fn accept_probes_and_close(&mut self) -> bool {
        let (published, stopping, busy) = {
            let mut gate = self.shared.gate.lock().unwrap();
            for (id, device) in &mut self.devices {
                device.closed |= gate
                    .devices
                    .get(id)
                    .is_some_and(|open| !open.load(Ordering::Acquire));
            }
            let count = QUANTUM.min(gate.pending_probes.len());
            let published: Vec<_> = gate.pending_probes.drain(..count).collect();
            (published, !gate.accepting, !gate.pending_probes.is_empty())
        };
        if stopping && !self.stopping {
            self.stopping = true;
            self.shared.health.close();
        }
        let closed: Vec<_> = self
            .queued
            .iter()
            .copied()
            .filter(|id| {
                let active = &self.active[id];
                self.stopping
                    || self
                        .devices
                        .get(&active.cell.options.device)
                        .is_none_or(|device| device.closed)
            })
            .collect();
        for id in closed {
            self.queued.retain(|queued| *queued != id);
            if let Some(active) = self.active.remove(&id) {
                self.finish(active, None, CompletionStatus::Shutdown);
            }
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
        let cost = self.devices[&options.device]
            .limits
            .cost(options.class, options.length);
        let active = Active {
            stage: Stage::Queued(operation.keep_alive(Arc::clone(&cell))),
            transferred: 0,
            error_retries: 0,
            destination: Some(destination),
            owner,
            window: options.window.clone(),
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
        self.active.insert(id, active);
        self.queued.push_back(id);
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
            let selected = self.queued.iter().find_map(|&id| {
                let active = &self.active[&id];
                let Stage::Queued(operation) = &active.stage else {
                    return None;
                };
                if active.cell.cancelled.load(Ordering::Acquire)
                    || !active.cell.device_open.load(Ordering::Acquire)
                    || active
                        .cell
                        .options
                        .policy
                        .deadline
                        .is_some_and(|until| until <= now)
                {
                    return Some((id, None));
                }
                let permit = self.route(active, operation)?;
                (!refused.contains(&(permit.device, permit.lane))).then_some((id, Some(permit)))
            });
            let Some((id, permit)) = selected else {
                return false;
            };
            let mut active = self.active.remove(&id).unwrap();
            self.queued.retain(|queued| *queued != id);
            if active.cell.cancelled.load(Ordering::Acquire)
                || !active.cell.device_open.load(Ordering::Acquire)
                || active
                    .cell
                    .options
                    .policy
                    .deadline
                    .is_some_and(|d| d <= now)
            {
                let status = if !active.cell.device_open.load(Ordering::Acquire) {
                    CompletionStatus::Shutdown
                } else if active.cell.cancelled.load(Ordering::Acquire) {
                    CompletionStatus::Cancelled
                } else {
                    CompletionStatus::TimedOut
                };
                self.finish(active, None, status);
                continue;
            }
            let permit = permit.expect("nonterminal dispatch candidate has a permit");
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
                    if permit.class == RequestClass::Probe {
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
        if self.stopping
            || self
                .devices
                .get(&active.cell.options.device)
                .is_none_or(|d| d.closed)
        {
            self.finish(active, Some(retired.operation), CompletionStatus::Shutdown);
            return;
        }
        if let Err(error) = active.cell.check() {
            let status = if error.kind() == io::ErrorKind::TimedOut {
                CompletionStatus::TimedOut
            } else {
                CompletionStatus::Cancelled
            };
            self.finish(active, Some(retired.operation), status);
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
                            active.stage =
                                Stage::Queued(operation.keep_alive(Arc::clone(&active.cell)));
                            self.active.insert(id, active);
                            self.queued.push_back(id);
                            return;
                        }
                        Err(error) => {
                            self.finish(active, None, CompletionStatus::IoError(error));
                            return;
                        }
                    }
                }
            }
        }
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
        if matches!(
            status,
            CompletionStatus::Cancelled | CompletionStatus::TimedOut | CompletionStatus::Shutdown
        ) {
            operation = None;
        }
        cell.phase.store(FINISHED, Ordering::Release);
        match destination {
            Some(destination @ (Destination::Caller(_) | Destination::Device(_))) => {
                self.deliver_completion(
                    destination,
                    Completion {
                        id: cell.id,
                        tag: cell.options.tag,
                        status,
                        transferred,
                        depth_cost: last_cost,
                        physical_attempts: attempts,
                        operation,
                        window,
                        _cell: Arc::clone(&cell),
                    },
                );
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
            Some(destination @ (Destination::Caller(_) | Destination::Device(_))) => {
                let options = &active.cell.options;
                self.deliver_completion(
                    destination,
                    Completion {
                        id: active.cell.id,
                        tag: options.tag,
                        status,
                        transferred: active.transferred,
                        depth_cost: active.last_cost,
                        physical_attempts: active.attempts,
                        operation,
                        window: active.window.clone(),
                        _cell: Arc::clone(&active.cell),
                    },
                );
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
    pub fn cancel(&mut self, id: RequestId, status: CompletionStatus) -> CancelOutcome {
        let Some(mut active) = self.active.remove(&id) else {
            return CancelOutcome::NotPending;
        };
        if active.destination.is_none() {
            self.active.insert(id, active);
            return CancelOutcome::NotPending;
        }
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
    pub(super) fn backend_failed(&mut self, device: DeviceId, lane: usize, error: io::Error) {
        if !self.devices.contains_key(&device)
            && self
                .shared
                .gate
                .lock()
                .unwrap()
                .devices
                .get(&device)
                .is_some_and(|open| !open.load(Ordering::Acquire))
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
                .get(&device)
                .unwrap()
                .store(false, Ordering::Release);
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
            self.finish(active, None, CompletionStatus::IoError(copy_error(&error)));
        }
        self.shared.wake.notify();
    }
    fn prepare_capacity(&mut self, config: &Config) -> io::Result<()> {
        let records = config.admission.max_records;
        {
            let mut cancellations = self.shared.cancellations.lock().unwrap();
            let additional = records.saturating_sub(cancellations.len());
            cancellations
                .try_reserve(additional)
                .map_err(io::Error::other)?;
        }
        self.active
            .try_reserve(records.saturating_sub(self.active.len()))
            .map_err(io::Error::other)?;
        self.submitted
            .try_reserve(records.saturating_sub(self.submitted.len()))
            .map_err(io::Error::other)?;
        self.queued
            .try_reserve(records.saturating_sub(self.queued.len()))
            .map_err(io::Error::other)?;
        for completed in self.completed.values_mut() {
            completed
                .entries
                .try_reserve(records.saturating_sub(completed.entries.len()))
                .map_err(io::Error::other)?;
        }
        Ok(())
    }
    pub(super) fn device_drained(&self, device: DeviceId) -> bool {
        !self
            .active
            .values()
            .any(|active| active.cell.options.device == device)
            && !self
                .shared
                .gate
                .lock()
                .unwrap()
                .pending_probes
                .iter()
                .any(|incoming| incoming.cell.options.device == device)
            && self
                .health
                .as_ref()
                .is_none_or(|health| health.target.options.device != device)
    }
    fn close_diagnosis(&mut self, device: DeviceId) {
        if self
            .health
            .as_ref()
            .is_none_or(|health| health.target.options.device != device)
        {
            return;
        }
        let mut health = self.health.take().unwrap();
        let pending = match &health.stage {
            DiagnosisStage::Running { id, .. } | DiagnosisStage::Draining { id } => Some(*id),
            DiagnosisStage::Backoff { pending, .. } | DiagnosisStage::Done { pending } => *pending,
            _ => None,
        };
        let submitted = pending.filter(|id| {
            self.active
                .get(id)
                .is_some_and(|active| matches!(active.stage, Stage::Submitted(_)))
        });
        if let Some(id) = pending {
            self.cancel(id, CompletionStatus::Shutdown);
        }
        health.stage = DiagnosisStage::Done { pending: submitted };
        if submitted.is_some() {
            self.health = Some(health);
        } else {
            self.shared.health.finish(health);
        }
    }
    fn prepare_device(&self, device: DeviceConfig, config: &Config) -> io::Result<PreparedDevice> {
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
        let port = Arc::new(file_io::CompletionPort::new(Arc::clone(&self.shared.wake)));
        for lane in &lanes {
            lane.backend.bind_completion_port(Arc::clone(&port))?;
        }
        let mut completed = CompletedQueue::default();
        completed
            .entries
            .try_reserve(config.admission.max_records)
            .map_err(io::Error::other)?;
        Ok(PreparedDevice {
            id: device.id,
            completed,
            state: DeviceState {
                limits: config.devices[&device.id],
                port,
                lanes,
                used: [0; 2],
                closed: false,
            },
        })
    }
    pub(super) fn register(&mut self, device: DeviceConfig) -> io::Result<()> {
        let prepared = self.prepare_device(device, &self.config)?;
        self.prepare_capacity(&self.config.clone())?;
        let mut gate = self.shared.gate.lock().unwrap();
        if !gate.accepting {
            return Err(io::ErrorKind::BrokenPipe.into());
        }
        self.completed.insert(prepared.id, prepared.completed);
        self.devices.insert(prepared.id, prepared.state);
        gate.devices
            .insert(prepared.id, Arc::new(AtomicBool::new(true)));
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
            .prepare_device(device, &config)
            .map_err(ConfigError::Invalid)?;
        self.prepare_capacity(&config)
            .map_err(ConfigError::Invalid)?;
        self.commit_config(expected, config, Some(prepared), None, now)
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
        let receipt = self.commit_config(expected, config, None, Some(device), now)?;
        self.shared.health.discard_device(device);
        self.close_diagnosis(device);
        Ok(receipt.generation)
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
                    && !gate
                        .pending_probes
                        .iter()
                        .any(|a| a.cell.options.device == **id)
            })
            .map(|(&id, _)| id)
            .collect();
        drop(gate);
        if removable.is_empty() {
            return;
        }
        for id in removable {
            self.devices.remove(&id);
            if self
                .completed
                .get(&id)
                .is_some_and(|queue| queue.entries.is_empty())
            {
                self.completed.remove(&id);
            }
        }
    }
    pub(super) fn reconfigure(
        &mut self,
        expected: Generation,
        config: Config,
        now: Instant,
    ) -> Result<ConfigReceipt, ConfigError> {
        self.validate_config(expected, &config)?;
        self.prepare_capacity(&config)
            .map_err(ConfigError::Invalid)?;
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
        _now: Instant,
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
        for (id, device) in &mut self.devices {
            if let Some(limits) = self.config.devices.get(id) {
                device.limits = *limits;
            }
        }
        if let Some(prepared) = device {
            self.completed.insert(prepared.id, prepared.completed);
            self.devices.insert(prepared.id, prepared.state);
            gate.devices
                .insert(prepared.id, Arc::new(AtomicBool::new(true)));
        }
        if let Some(id) = closing {
            if let Some(open) = gate.devices.get(&id) {
                open.store(false, Ordering::Release);
            }
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
    #[cfg(test)]
    pub fn backend(&self, device: DeviceId, lane: usize) -> Arc<Backend> {
        Arc::clone(&self.devices[&device].lanes[lane].backend)
    }
    pub fn snapshot(&self) -> ServiceSnapshot {
        let gate = self.shared.gate.lock().unwrap();
        let ledger = self.shared.ledger.lock().unwrap();
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
        }
    }
}
fn copy_error(error: &io::Error) -> io::Error {
    error
        .raw_os_error()
        .map(io::Error::from_raw_os_error)
        .unwrap_or_else(|| io::Error::new(error.kind(), error.to_string()))
}
