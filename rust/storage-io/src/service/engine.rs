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

pub(super) struct Incoming {
    pub operation: Operation,
    pub cell: Arc<ControlCell>,
    pub request: Option<Box<dyn RequestCompletion>>,
}
pub(crate) struct RouteCounters {
    state: Mutex<RouteCounterState>,
    worker_capacity: usize,
    driver_has_work: Arc<AtomicBool>,
}
struct RouteCounterState {
    limits: DeviceLimits,
    device_total: usize,
    worker_total: usize,
    direct_active: usize,
}
impl RouteCounters {
    fn new(limits: DeviceLimits, worker_capacity: usize, driver_has_work: Arc<AtomicBool>) -> Self {
        Self {
            state: Mutex::new(RouteCounterState {
                limits,
                device_total: 0,
                worker_total: 0,
                direct_active: 0,
            }),
            worker_capacity,
            driver_has_work,
        }
    }
    pub(super) fn limits(&self) -> DeviceLimits {
        self.state.lock().unwrap().limits
    }
    pub(super) fn set_limits(&self, limits: DeviceLimits) {
        self.state.lock().unwrap().limits = limits;
    }
    pub(super) fn reserve(&self, cost: DepthCost, worker: bool) -> bool {
        let mut state = self.state.lock().unwrap();
        self.reserve_locked(&mut state, cost, worker)
    }
    fn reserve_locked(&self, state: &mut RouteCounterState, cost: DepthCost, worker: bool) -> bool {
        let device_total = state.device_total + cost.effective;
        if device_total > state.limits.depth {
            return false;
        }
        let worker_total = state.worker_total + usize::from(worker);
        if worker_total > self.worker_capacity {
            return false;
        }
        state.device_total = device_total;
        state.worker_total = worker_total;
        true
    }
    pub(crate) fn reserve_direct(&self, length: usize) -> Option<DepthCost> {
        let mut state = self.state.lock().unwrap();
        let cost = state.limits.cost(length);
        if self.reserve_locked(&mut state, cost, true) {
            state.direct_active += 1;
            Some(cost)
        } else {
            None
        }
    }
    fn release(&self, cost: DepthCost, worker: bool) {
        Self::release_locked(&mut self.state.lock().unwrap(), cost, worker);
    }
    fn release_locked(state: &mut RouteCounterState, cost: DepthCost, worker: bool) {
        if worker {
            state.worker_total -= 1;
        }
        state.device_total -= cost.effective;
    }
    pub(crate) fn release_active(&self, cost: DepthCost, reserved: bool) {
        let mut state = self.state.lock().unwrap();
        if reserved {
            Self::release_locked(&mut state, cost, true);
        }
        state.direct_active -= 1;
    }
    pub(super) fn direct_active(&self) -> usize {
        self.state.lock().unwrap().direct_active
    }
    fn snapshot(&self) -> (usize, usize) {
        let state = self.state.lock().unwrap();
        (state.device_total, state.worker_total)
    }
}
pub(super) struct WorkerRoute {
    backend: Option<Arc<Backend>>,
    pub(super) counters: Arc<RouteCounters>,
    pub(super) native_available: bool,
    wake: Arc<Wake>,
}
impl WorkerRoute {
    pub(super) fn backend(&self) -> &Backend {
        self.backend.as_deref().unwrap()
    }
}
impl Drop for WorkerRoute {
    fn drop(&mut self) {
        drop(self.backend.take());
        self.wake.notify();
    }
}
struct BackendState {
    backend: Arc<Backend>,
    used: usize,
    failure: Option<io::Error>,
}
struct DeviceState {
    route: Arc<WorkerRoute>,
    port: Arc<file_io::CompletionPort>,
    lanes: Vec<BackendState>,
    closed: bool,
}
struct InlineCompletion {
    driver: Weak<Mutex<Driver>>,
    device: DeviceId,
}
impl file_io::CompletionListener for InlineCompletion {
    fn accepts(&self, _: SubmissionId, operation: &Operation) -> bool {
        if operation.has_completion_context::<DirectRequest>() {
            return true;
        }
        self.driver.strong_count() != 0
    }
    fn complete(&self, submission: SubmissionId, retired: Retired) {
        let Retired {
            mut operation,
            result,
        } = retired;
        if let Some(mut direct) = operation.take_completion_context::<DirectRequest>() {
            direct.retired(operation, result);
            return;
        }
        let retired = Retired { operation, result };
        Driver::retire_inline(&self.driver, self.device, submission, retired);
    }
}

pub(super) struct DirectRequest {
    shared: Weak<Shared>,
    accepting: Arc<AtomicBool>,
    counters: Arc<RouteCounters>,
    cell: Arc<ControlCell>,
    pub(super) request: Option<Box<dyn RequestCompletion>>,
    cost: DepthCost,
    transferred: usize,
    error_retries: usize,
    attempts: u64,
    reserved: bool,
    active: bool,
}

impl DirectRequest {
    pub(super) fn new(
        shared: &Arc<Shared>,
        route: &WorkerRoute,
        cell: Arc<ControlCell>,
        cost: DepthCost,
        request: Option<Box<dyn RequestCompletion>>,
    ) -> Self {
        Self {
            shared: Arc::downgrade(shared),
            accepting: Arc::clone(&shared.accepting),
            counters: Arc::clone(&route.counters),
            cell,
            request,
            cost,
            transferred: 0,
            error_retries: 0,
            attempts: 1,
            reserved: true,
            active: true,
        }
    }

    fn release_reservation(&mut self) {
        if self.reserved {
            self.counters.release(self.cost, true);
            self.reserved = false;
        }
    }

    pub(super) fn release_active(&mut self) {
        let reserved = self.reserved;
        self.reserved = false;
        if self.active {
            self.active = false;
            self.counters.release_active(self.cost, reserved);
            if self.counters.driver_has_work.load(Ordering::Acquire)
                || !self.accepting.load(Ordering::Acquire)
                || !self.cell.device_open.load(Ordering::Acquire)
            {
                if let Some(shared) = self.shared.upgrade() {
                    shared.wake.notify();
                }
            }
        }
    }

    fn terminal_status(&self) -> Option<CompletionStatus> {
        if !self.cell.device_open.load(Ordering::Acquire) || !self.accepting.load(Ordering::Acquire)
        {
            return Some(CompletionStatus::Shutdown);
        }
        cancelled_status(&self.cell)
    }

    fn retired(&mut self, operation: Operation, result: io::Result<usize>) {
        self.release_reservation();
        if let Some(status) = self.terminal_status() {
            self.finish(Some(operation), status);
            return;
        }
        let policy = self.cell.options.policy;
        match physical_completion(
            operation,
            result,
            &mut self.transferred,
            &mut self.error_retries,
            &policy,
            self.cell.options.window.end,
        ) {
            PhysicalCompletion::Retry(operation) => self.retry(operation),
            PhysicalCompletion::Finished(operation, status) => self.finish(Some(operation), status),
        }
    }

    fn retry(&mut self, operation: Operation) {
        let Some(shared) = self.shared.upgrade() else {
            self.finish(Some(operation), CompletionStatus::Shutdown);
            return;
        };
        let Some(driver) = shared.driver.lock().unwrap().upgrade() else {
            self.finish(Some(operation), CompletionStatus::Shutdown);
            return;
        };
        let active = Active {
            stage: Stage::Queued(operation.keep_alive(Arc::clone(&self.cell))),
            transferred: self.transferred,
            error_retries: self.error_retries,
            request: self.request.take(),
            cell: Arc::clone(&self.cell),
            route: Some(0),
            last_cost: self.cost,
            attempts: self.attempts,
        };
        driver.lock().unwrap().accept_active(active);
        shared.wake.notify();
        self.release_active();
    }

    pub(super) fn submit_failed(&mut self, operation: Operation, error: io::Error) {
        self.attempts = 0;
        self.finish(Some(operation), CompletionStatus::IoError(error));
    }

    fn finish(&mut self, operation: Option<Operation>, status: CompletionStatus) {
        let operation = retire(&self.cell, operation, &status);
        self.release_reservation();
        if let Some(request) = self.request.take() {
            request.complete(completion(
                &self.cell,
                operation,
                status,
                self.transferred,
                self.cost,
                self.attempts,
            ));
        }
        self.release_active();
    }
}

impl Drop for DirectRequest {
    fn drop(&mut self) {
        self.release_active();
    }
}

fn retire(
    cell: &ControlCell,
    mut operation: Option<Operation>,
    status: &CompletionStatus,
) -> Option<Operation> {
    if let Some(operation) = &mut operation {
        operation.buffer_mut().zero_uninitialized();
        operation.release_keep_alive();
    }
    let discard_buffer = matches!(
        status,
        CompletionStatus::Cancelled | CompletionStatus::TimedOut | CompletionStatus::Shutdown
    );
    if discard_buffer {
        drop(operation.take());
    }
    cell.phase.store(FINISHED, Ordering::Release);
    if discard_buffer {
        cell.charge.release_bytes();
    }
    operation
}

fn cancelled_status(cell: &ControlCell) -> Option<CompletionStatus> {
    cell.check().err().map(|error| {
        if error.kind() == io::ErrorKind::TimedOut {
            CompletionStatus::TimedOut
        } else {
            CompletionStatus::Cancelled
        }
    })
}

fn completion(
    cell: &Arc<ControlCell>,
    operation: Option<Operation>,
    status: CompletionStatus,
    transferred: usize,
    depth_cost: DepthCost,
    physical_attempts: u64,
) -> Completion {
    Completion {
        id: cell.id,
        tag: cell.options.tag,
        status,
        transferred,
        depth_cost,
        physical_attempts,
        operation,
        window: cell.options.window.clone(),
        _cell: Some(Arc::clone(cell)),
    }
}

enum PhysicalCompletion {
    Retry(Operation),
    Finished(Operation, CompletionStatus),
}

fn physical_completion(
    operation: Operation,
    result: io::Result<usize>,
    transferred: &mut usize,
    error_retries: &mut usize,
    policy: &RequestPolicy,
    window_end: usize,
) -> PhysicalCompletion {
    let remaining = operation.remaining_len();
    let status = match result {
        Ok(count) if count <= remaining => {
            *transferred += count;
            if count != 0
                && count < remaining
                && policy
                    .retry_alignment
                    .is_some_and(|alignment| count.is_multiple_of(alignment.get()))
            {
                return PhysicalCompletion::Retry(
                    operation.advance(count).expect("validated physical prefix"),
                );
            }
            if count == 0 && remaining != 0 && operation.direction() == Direction::Write {
                CompletionStatus::IoError(io::ErrorKind::WriteZero.into())
            } else if *transferred < window_end {
                CompletionStatus::ShortIo
            } else if matches!(policy.completion, CompletionRequirement::FullBuffer)
                && *transferred < operation.buffer().len()
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
            ) && *error_retries < policy.max_error_retries
            {
                *error_retries += 1;
                return PhysicalCompletion::Retry(operation);
            }
            CompletionStatus::IoError(error)
        }
    };
    PhysicalCompletion::Finished(operation, status)
}

struct PreparedDevice {
    id: DeviceId,
    state: DeviceState,
}
#[derive(Clone, Copy)]
struct Permit {
    device: DeviceId,
    lane: usize,
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
    request: Option<Box<dyn RequestCompletion>>,
    pub(super) cell: Arc<ControlCell>,
    route: Option<usize>,
    last_cost: DepthCost,
    attempts: u64,
}
pub(super) struct Driver {
    pub shared: Arc<Shared>,
    devices: BTreeMap<DeviceId, DeviceState>,
    retiring_backends: Vec<Arc<Backend>>,
    pub active: HashMap<RequestId, Active>,
    deliveries: VecDeque<(Box<dyn RequestCompletion>, Completion)>,
    submitted: HashMap<SubmissionId, (RequestId, Permit)>,
    queued: VecDeque<RequestId>,
    pub(super) config: Arc<Config>,
    stopping: bool,
}
impl Driver {
    pub fn new(shared: Arc<Shared>) -> Self {
        let config = Arc::clone(&shared.gate.lock().unwrap().config);
        Self {
            shared,
            devices: BTreeMap::new(),
            retiring_backends: Vec::new(),
            active: HashMap::new(),
            deliveries: VecDeque::new(),
            submitted: HashMap::new(),
            queued: VecDeque::new(),
            config,
            stopping: false,
        }
    }
    fn retire_inline(
        driver: &Weak<Mutex<Driver>>,
        _device: DeviceId,
        submission: SubmissionId,
        retired: Retired,
    ) {
        let Some(driver) = driver.upgrade() else {
            return;
        };
        {
            let mut driver = driver.lock().unwrap();
            driver.retired(submission, retired);
            if !driver.queued.is_empty() {
                driver.dispatch(Instant::now());
            }
            driver.reap_devices();
        }
        let wake = {
            let driver = driver.lock().unwrap();
            (!driver.shared.accepting.load(Ordering::Acquire)
                || driver.shared.drain_waiters.load(Ordering::Acquire) != 0)
                .then(|| Arc::clone(&driver.shared.wake))
        };
        Self::flush(&driver);
        if let Some(wake) = wake {
            wake.notify();
        }
    }
    pub(super) fn flush(driver: &Arc<Mutex<Driver>>) {
        loop {
            let delivery = driver.lock().unwrap().deliveries.pop_front();
            let Some((request, completion)) = delivery else {
                return;
            };
            request.complete(completion);
        }
    }
    #[cfg(test)]
    pub(super) fn flush_test(&mut self) {
        while let Some((request, completion)) = self.deliveries.pop_front() {
            request.complete(completion);
        }
    }
    pub(super) fn progress_inline(
        driver: &Weak<Mutex<Driver>>,
    ) -> Option<(bool, Option<Duration>)> {
        let driver = driver.upgrade()?;
        let busy = driver.lock().unwrap().progress(None);
        Self::flush(&driver);
        Some((busy, None))
    }
    pub(super) fn progress(&mut self, selected: Option<DeviceId>) -> bool {
        self.shared.driver_has_work.store(true, Ordering::Release);
        let mut busy = self.accept_close();
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
        if !self.queued.is_empty() {
            busy |= self.dispatch(Instant::now());
        }
        self.reap_devices();
        self.shared.driver_has_work.store(
            !self.active.is_empty() || !self.submitted.is_empty() || !self.queued.is_empty(),
            Ordering::Release,
        );
        busy
    }
    pub(super) fn take_backends(&mut self) -> Vec<Arc<Backend>> {
        let mut backends = std::mem::take(&mut self.retiring_backends);
        backends.extend(
            std::mem::take(&mut self.devices)
                .into_values()
                .flat_map(|device| device.lanes.into_iter().map(|lane| lane.backend)),
        );
        self.shared.worker_routes.lock().unwrap().clear();
        backends
    }
    pub(super) fn quiescent(&self) -> bool {
        self.active.is_empty()
            && self.deliveries.is_empty()
            && self.shared.cancellations.lock().unwrap().is_empty()
            && self
                .shared
                .worker_routes
                .lock()
                .unwrap()
                .values()
                .all(|route| route.counters.direct_active() == 0)
    }

    pub(super) fn accept_close(&mut self) -> bool {
        let stopping = {
            let gate = self.shared.gate.lock().unwrap();
            for (id, device) in &mut self.devices {
                device.closed |= gate
                    .devices
                    .get(id)
                    .is_some_and(|open| !open.load(Ordering::Acquire));
            }
            gate.phase == ServicePhase::Stopped
        };
        if stopping && !self.stopping {
            self.stopping = true;
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
        false
    }
    pub fn accept(&mut self, incoming: Incoming) {
        let Incoming {
            operation,
            cell,
            request,
        } = incoming;
        let options = &cell.options;
        let cost = self.devices[&options.device]
            .route
            .counters
            .limits()
            .cost(options.length);
        let active = Active {
            stage: Stage::Queued(operation.keep_alive(Arc::clone(&cell))),
            transferred: 0,
            error_retries: 0,
            request,
            route: None,
            last_cost: cost,
            attempts: 0,
            cell,
        };
        self.accept_active(active);
    }
    fn accept_active(&mut self, active: Active) {
        self.shared.driver_has_work.store(true, Ordering::Release);
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
        let id = active.cell.id;
        self.active.insert(id, active);
        self.queued.push_back(id);
    }
    fn route(&self, active: &Active, operation: &Operation) -> Option<Permit> {
        let options = &active.cell.options;
        let device = &self.devices[&options.device];
        if device.closed {
            return None;
        }
        let limits = device.route.counters.limits();
        let (device_used, worker_used) = device.route.counters.snapshot();
        let cost = limits.cost(operation.remaining_len());
        if cost.effective > limits.depth.saturating_sub(device_used) {
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
            let lane_used = if index == 0 { worker_used } else { lane.used };
            if lane.failure.is_none() && lane_used < capacity {
                return Some(Permit {
                    device: options.device,
                    lane: index,
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
            if !self.devices[&permit.device]
                .route
                .counters
                .reserve(permit.cost, permit.lane == 0)
            {
                refused.insert((permit.device, permit.lane));
                self.active.insert(id, active);
                self.queued.push_front(id);
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
                    if permit.lane != 0 {
                        device.lanes[permit.lane].used += 1;
                    }
                    active.attempts = active.attempts.saturating_add(1);
                    active.last_cost = permit.cost;
                    active.route = Some(permit.lane);
                    active.stage = Stage::Submitted(submission);
                    self.submitted.insert(submission, (id, permit));
                    self.active.insert(id, active);
                }
                Err(failure) => {
                    self.devices[&permit.device]
                        .route
                        .counters
                        .release(permit.cost, permit.lane == 0);
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
        device.route.counters.release(permit.cost, permit.lane == 0);
        if permit.lane != 0 {
            device.lanes[permit.lane].used -= 1;
        }
    }
    pub fn retired(&mut self, submission: SubmissionId, retired: Retired) {
        let Some((id, permit)) = self.submitted.remove(&submission) else {
            return;
        };
        self.release(permit);
        let mut active = self.active.remove(&id).unwrap();
        if active.request.is_none() {
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
        if let Some(status) = cancelled_status(&active.cell) {
            self.finish(active, Some(retired.operation), status);
            return;
        }
        let Retired { operation, result } = retired;
        let policy = active.cell.options.policy;
        let (operation, status) = match physical_completion(
            operation,
            result,
            &mut active.transferred,
            &mut active.error_retries,
            &policy,
            active.cell.options.window.end,
        ) {
            PhysicalCompletion::Retry(operation) => {
                active.stage = Stage::Queued(operation);
                self.active.insert(id, active);
                self.queued.push_back(id);
                return;
            }
            PhysicalCompletion::Finished(operation, status) => (operation, status),
        };
        self.finish(active, Some(operation), status);
    }
    fn finish(&mut self, active: Active, operation: Option<Operation>, status: CompletionStatus) {
        let Active {
            stage,
            request,
            cell,
            transferred,
            last_cost,
            attempts,
            ..
        } = active;
        let operation = operation.or(match stage {
            Stage::Queued(operation) => Some(operation),
            Stage::Submitted(_) => None,
        });
        let operation = retire(&cell, operation, &status);
        if let Some(request) = request {
            self.deliveries.push_back((
                request,
                completion(&cell, operation, status, transferred, last_cost, attempts),
            ));
        }
    }
    pub fn cancel(&mut self, id: RequestId, status: CompletionStatus) -> CancelOutcome {
        let Some(mut active) = self.active.remove(&id) else {
            return CancelOutcome::NotPending;
        };
        if active.request.is_none() {
            self.active.insert(id, active);
            return CancelOutcome::NotPending;
        }
        // A submitted request can publish cancellation before the backend retires it.
        active.cell.phase.store(FINISHED, Ordering::Release);
        if let Some(request) = active.request.take() {
            self.deliveries.push_back((
                request,
                completion(
                    &active.cell,
                    None,
                    status,
                    active.transferred,
                    active.last_cost,
                    active.attempts,
                ),
            ));
        }
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
        self.deliveries
            .try_reserve(records.saturating_sub(self.deliveries.len()))
            .map_err(io::Error::other)?;
        self.shared
            .executor
            .prepare(config.callback_threads, false)?;
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
        Ok(())
    }
    #[cfg(any(feature = "cxx", test))]
    pub(super) fn device_drained(&self, device: DeviceId) -> bool {
        !self
            .active
            .values()
            .any(|active| active.cell.options.device == device)
            && self
                .devices
                .get(&device)
                .is_none_or(|state| state.route.counters.direct_active() == 0)
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
            || device.worker.capacity() == 0
            || device
                .native
                .as_ref()
                .is_some_and(|b| !b.is_native() || b.capacity() == 0)
        {
            return Err(invalid("backend route must have nonzero capacity"));
        }
        let native_available = device.native.is_some();
        let worker = Arc::new(device.worker);
        let route = Arc::new(WorkerRoute {
            counters: Arc::new(RouteCounters::new(
                config.devices[&device.id],
                worker.capacity(),
                Arc::clone(&self.shared.driver_has_work),
            )),
            backend: Some(Arc::clone(&worker)),
            native_available,
            wake: Arc::clone(&self.shared.wake),
        });
        let mut lanes = vec![BackendState {
            backend: worker,
            used: 0,
            failure: None,
        }];
        if let Some(native) = device.native {
            lanes.push(BackendState {
                backend: Arc::new(native),
                used: 0,
                failure: None,
            });
        }
        let port = Arc::new(file_io::CompletionPort::new(Arc::clone(&self.shared.wake)));
        for lane in &lanes {
            lane.backend.bind_completion_port(Arc::clone(&port))?;
        }
        let driver = self.shared.driver.lock().unwrap().clone();
        if driver.strong_count() != 0 {
            for lane in &lanes {
                lane.backend
                    .bind_completion_listener(Arc::new(InlineCompletion {
                        driver: driver.clone(),
                        device: device.id,
                    }))?;
            }
        }
        Ok(PreparedDevice {
            id: device.id,
            state: DeviceState {
                route,
                port,
                lanes,
                closed: false,
            },
        })
    }
    pub(super) fn register(&mut self, device: DeviceConfig) -> io::Result<()> {
        let prepared = self.prepare_device(device, &self.config)?;
        self.prepare_capacity(&self.config.clone())?;
        let mut gate = self.shared.gate.lock().unwrap();
        if gate.phase == ServicePhase::Stopped {
            return Err(io::ErrorKind::BrokenPipe.into());
        }
        self.shared
            .worker_routes
            .lock()
            .unwrap()
            .insert(prepared.id, prepared.state.route.clone());
        self.devices.insert(prepared.id, prepared.state);
        gate.devices
            .insert(prepared.id, Arc::new(AtomicBool::new(true)));
        Ok(())
    }
    #[cfg(any(feature = "cxx", test))]
    pub(super) fn register_with_config(
        &mut self,
        config: Config,
        device: DeviceConfig,
        now: Instant,
    ) -> Result<(), ConfigError> {
        self.validate_config(&config)?;
        let prepared = self
            .prepare_device(device, &config)
            .map_err(ConfigError::Invalid)?;
        self.prepare_capacity(&config)
            .map_err(ConfigError::Invalid)?;
        self.commit_config(config, Some(prepared), None, now)
    }
    pub(super) fn remove_config(
        &mut self,
        device: DeviceId,
        now: Instant,
    ) -> Result<(), ConfigError> {
        {
            let gate = self.shared.gate.lock().unwrap();
            if gate.phase == ServicePhase::Stopped {
                return Err(ConfigError::Unavailable(io::ErrorKind::BrokenPipe.into()));
            }
            if !self.config.devices.contains_key(&device) {
                return Ok(());
            }
            if !gate.devices.contains_key(&device) {
                return Err(ConfigError::Invalid(io::ErrorKind::NotFound.into()));
            }
        }
        let mut config = (*self.config).clone();
        config.devices.remove(&device);
        config.validate().map_err(ConfigError::Invalid)?;
        self.commit_config(config, None, Some(device), now)
    }
    fn reap_devices(&mut self) {
        let stopping = !self.shared.accepting.load(Ordering::Acquire);
        if !stopping {
            self.retiring_backends
                .retain(|backend| Arc::strong_count(backend) > 1);
        }
        let removable: Vec<_> = self
            .devices
            .iter()
            .filter(|(id, device)| {
                let (used, _) = device.route.counters.snapshot();
                device.closed
                    && used == 0
                    && device.route.counters.direct_active() == 0
                    && !self
                        .active
                        .values()
                        .any(|active| active.cell.options.device == **id)
            })
            .map(|(&id, _)| id)
            .collect();
        for id in removable {
            let device = self.devices.remove(&id).unwrap();
            self.shared.worker_routes.lock().unwrap().remove(&id);
            self.shared.gate.lock().unwrap().devices.remove(&id);
            drop(device.route);
            self.retiring_backends.extend(
                device
                    .lanes
                    .into_iter()
                    .map(|lane| lane.backend)
                    .filter(|backend| stopping || Arc::strong_count(backend) > 1),
            );
        }
    }
    #[cfg(any(feature = "cxx", test))]
    pub(super) fn reconfigure(&mut self, config: Config, now: Instant) -> Result<(), ConfigError> {
        self.validate_config(&config)?;
        self.prepare_capacity(&config)
            .map_err(ConfigError::Invalid)?;
        self.commit_config(config, None, None, now)
    }
    #[cfg(any(feature = "cxx", test))]
    fn validate_config(&self, config: &Config) -> Result<(), ConfigError> {
        if config.statistics_lanes != self.shared.statistics.lane_count() {
            return Err(ConfigError::Invalid(invalid(
                "statistics lane identities cannot change",
            )));
        }
        config.validate().map_err(ConfigError::Invalid)?;
        let gate = self.shared.gate.lock().unwrap();
        if gate.phase == ServicePhase::Stopped {
            return Err(ConfigError::Unavailable(io::ErrorKind::BrokenPipe.into()));
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
        Ok(())
    }
    fn commit_config(
        &mut self,
        config: Config,
        device: Option<PreparedDevice>,
        closing: Option<DeviceId>,
        _now: Instant,
    ) -> Result<(), ConfigError> {
        let config = Arc::new(config);
        let mut gate = self.shared.gate.lock().unwrap();
        if gate.phase == ServicePhase::Stopped {
            return Err(ConfigError::Unavailable(io::ErrorKind::BrokenPipe.into()));
        }
        let mut ledger = self.shared.ledger.lock().unwrap();
        ledger.set_limits(config.admission);
        gate.config = Arc::clone(&config);
        self.config = config;
        for (id, device) in &mut self.devices {
            if let Some(limits) = self.config.devices.get(id) {
                device.route.counters.set_limits(*limits);
            }
        }
        if let Some(prepared) = device {
            self.shared
                .worker_routes
                .lock()
                .unwrap()
                .insert(prepared.id, prepared.state.route.clone());
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
        drop(ledger);
        drop(gate);
        self.shared.wake.notify();
        Ok(())
    }
    #[cfg(test)]
    pub fn backend(&self, device: DeviceId, lane: usize) -> Arc<Backend> {
        Arc::clone(&self.devices[&device].lanes[lane].backend)
    }
    pub fn snapshot(&self) -> ServiceSnapshot {
        let gate = self.shared.gate.lock().unwrap();
        let ledger = self.shared.ledger.lock().unwrap();
        ServiceSnapshot {
            config: (*gate.config).clone(),
            admitted_requests: ledger.usage.records,
            reserved_bytes: ledger.usage.bytes,
            usage: ledger.usage,
            debt: ledger.debt(),
            queued_requests: self.queued.len(),
            awaiting_retirement: self.submitted.len()
                + self
                    .devices
                    .values()
                    .map(|device| device.route.counters.direct_active())
                    .sum::<usize>(),
            devices: self
                .devices
                .iter()
                .map(|(&id, state)| {
                    let (used, worker_used) = state.route.counters.snapshot();
                    let limits = state.route.counters.limits();
                    DeviceSnapshot {
                        id,
                        physical_depth: used,
                        depth_debt: used.saturating_sub(limits.depth),
                        backend_envelopes: std::iter::once(worker_used)
                            .chain(state.lanes.iter().skip(1).map(|lane| lane.used))
                            .collect(),
                        closed: state.closed,
                    }
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
