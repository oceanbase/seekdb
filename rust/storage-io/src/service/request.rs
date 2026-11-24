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
use std::ops::Deref;
use std::sync::{Condvar, OnceLock};

pub(crate) trait Consumer: Send + Sync {
    fn process(&mut self, bytes: &[u8]) -> Result<i32, ()>;
    #[cfg(feature = "cxx")]
    fn foreign(&self) -> Option<&crate::bridge::ffi::CallbackAdapter> {
        None
    }
}
pub(crate) type CallbackOwner = Box<dyn Consumer>;

#[derive(Clone, Copy, Eq, PartialEq)]
enum Phase {
    Pending,
    Delivering(std::thread::ThreadId),
    Ready,
}
struct Progress {
    phase: Phase,
    control: Option<RequestControl>,
    lifetime: Option<RequestLifetime>,
    callback: Option<CallbackOwner>,
}
pub struct PublishedCompletion {
    pub id: RequestId,
    pub tag: u64,
    pub status: CompletionStatus,
    pub transferred: usize,
    pub depth_cost: DepthCost,
    pub physical_attempts: u64,
    buffer: Option<AlignedBuffer>,
    window: Range<usize>,
    _cell: Option<Arc<ControlCell>>,
}
impl PublishedCompletion {
    fn freeze(completion: Completion) -> Self {
        let Completion {
            id,
            tag,
            status,
            transferred,
            depth_cost,
            physical_attempts,
            operation,
            window,
            _cell,
            ..
        } = completion;
        Self {
            id,
            tag,
            status,
            transferred,
            depth_cost,
            physical_attempts,
            buffer: operation.map(Operation::into_buffer),
            window,
            _cell,
        }
    }
    pub fn data_size(&self) -> usize {
        if matches!(
            self.status,
            CompletionStatus::Cancelled | CompletionStatus::TimedOut | CompletionStatus::Shutdown
        ) {
            return 0;
        }
        self.window
            .end
            .min(self.transferred)
            .saturating_sub(self.window.start)
    }
    pub fn buffer(&self) -> Option<&[u8]> {
        self.buffer.as_ref().map(AlignedBuffer::as_slice)
    }
    pub fn data(&self) -> Option<&[u8]> {
        let buffer = self.buffer()?;
        let end = self.window.end.min(self.transferred);
        Some(&buffer[self.window.start.min(end)..end])
    }
}
struct Outcome {
    completion: PublishedCompletion,
    callback: Option<CallbackOwner>,
    callback_error: i32,
    elapsed: Duration,
}
struct Registration {
    state: Weak<RequestState>,
    deadline: Option<Instant>,
}
#[derive(Default)]
pub(super) struct Registry {
    entries: HashMap<RequestId, Registration>,
    wake_at: Option<Instant>,
}
impl Registry {
    fn insert(&mut self, state: &Arc<RequestState>) -> bool {
        let deadline = state.options.policy.deadline;
        self.entries.insert(
            state.id,
            Registration {
                state: Arc::downgrade(state),
                deadline,
            },
        );
        let earlier =
            deadline.is_some_and(|deadline| self.wake_at.is_none_or(|wake| deadline < wake));
        if earlier {
            self.wake_at = deadline;
        }
        earlier
    }
    fn remove(&mut self, id: RequestId) {
        self.entries.remove(&id);
    }
    pub(super) fn states(&self, device: Option<DeviceId>) -> Vec<Arc<RequestState>> {
        self.entries
            .values()
            .filter_map(|entry| entry.state.upgrade())
            .filter(|state| device.is_none_or(|device| state.device() == device))
            .collect()
    }
    #[cfg(any(feature = "cxx", test))]
    pub(super) fn has_device(&self, device: DeviceId) -> bool {
        self.entries
            .values()
            .filter_map(|entry| entry.state.upgrade())
            .any(|state| state.device() == device)
    }
    pub(super) fn deadlines(&mut self, now: Instant) -> (Vec<Arc<RequestState>>, Option<Duration>) {
        if self.wake_at.is_none_or(|wake| wake > now) {
            return (
                Vec::new(),
                self.wake_at.map(|wake| wake.saturating_duration_since(now)),
            );
        }
        let mut expired = Vec::new();
        for entry in self.entries.values_mut() {
            if entry.deadline.is_some_and(|deadline| deadline <= now) {
                entry.deadline = None;
                if let Some(state) = entry.state.upgrade() {
                    expired.push(state);
                }
            }
        }
        self.wake_at = self
            .entries
            .values()
            .filter_map(|entry| entry.deadline)
            .min();
        (
            expired,
            self.wake_at.map(|wake| wake.saturating_duration_since(now)),
        )
    }
}
struct ResultOwner {
    outcome: OnceLock<Outcome>,
}

pub(crate) struct RequestState {
    progress: Mutex<Progress>,
    changed: Condvar,
    result: Weak<ResultOwner>,
    options: RequestOptions,
    id: RequestId,
    began: Instant,
    handles: AtomicUsize,
    submission_error: i32,
    statistics: Option<Arc<crate::statistics::CompletionStatistics>>,
    registry: Weak<Mutex<Registry>>,
    service_wake: Option<Arc<Wake>>,
    drain_waiters: Option<Arc<AtomicUsize>>,
}
/// Owns the consumer-visible lifecycle. Backend work retains the same state until retirement.
pub struct Request {
    state: Arc<RequestState>,
    result: Arc<ResultOwner>,
}
pub struct RequestResult {
    result: Arc<ResultOwner>,
}
impl Deref for RequestResult {
    type Target = PublishedCompletion;
    fn deref(&self) -> &PublishedCompletion {
        &self.result.outcome.get().unwrap().completion
    }
}
impl Clone for Request {
    fn clone(&self) -> Self {
        self.state.handles.fetch_add(1, Ordering::Relaxed);
        Self {
            state: Arc::clone(&self.state),
            result: Arc::clone(&self.result),
        }
    }
}
impl Drop for Request {
    fn drop(&mut self) {
        if self.state.handles.fetch_sub(1, Ordering::AcqRel) == 1 {
            self.state.cancel(CompletionStatus::Cancelled);
            self.state.drain_delivery();
        }
    }
}
impl Request {
    pub(super) fn accepted(
        admission: &mut Admission,
        lifetime: RequestLifetime,
        callback: Option<CallbackOwner>,
        began: Instant,
    ) -> io::Result<Self> {
        let control = admission.take_control()?;
        let shared = &admission.shared;
        let result = Arc::new(ResultOwner {
            outcome: OnceLock::new(),
        });
        let request = Self {
            state: Arc::new(RequestState {
                progress: Mutex::new(Progress {
                    phase: Phase::Pending,
                    lifetime: Some(lifetime),
                    control: Some(control),
                    callback,
                }),
                changed: Condvar::new(),
                result: Arc::downgrade(&result),
                options: admission.cell.options.clone(),
                id: admission.id(),
                began,
                handles: AtomicUsize::new(1),
                submission_error: 0,
                statistics: Some(Arc::clone(&shared.statistics)),
                registry: Arc::downgrade(&shared.requests),
                service_wake: Some(Arc::clone(&shared.wake)),
                drain_waiters: Some(Arc::clone(&shared.drain_waiters)),
            }),
            result,
        };
        let earlier = shared.requests.lock().unwrap().insert(&request.state);
        if earlier {
            shared.wake.notify();
        }
        Ok(request)
    }
    pub(super) fn publisher(
        &self,
        executor: Arc<super::delivery::Executor>,
    ) -> Box<dyn RequestCompletion> {
        Box::new(Publisher {
            state: Arc::clone(&self.state),
            executor,
        })
    }
    pub fn submission_error(&self) -> i32 {
        self.state.submission_error
    }
    #[cfg(feature = "cxx")]
    pub(crate) fn rejected(error: io::Error, callback: Option<CallbackOwner>) -> Self {
        let number = file_io::bridge::error_number(io::Error::from(error.kind()));
        let result = Arc::new(ResultOwner {
            outcome: OnceLock::new(),
        });
        let state = Arc::new(RequestState {
            progress: Mutex::new(Progress {
                phase: Phase::Ready,
                lifetime: None,
                control: None,
                callback: None,
            }),
            changed: Condvar::new(),
            result: Arc::downgrade(&result),
            options: RequestOptions {
                device: DeviceId(0),
                execution: Execution::WorkerRequired,
                length: 0,
                alignment: 1,
                window: 0..0,
                policy: RequestPolicy::default(),
                tag: 0,
                statistics_lane: None,
            },
            id: RequestId(0),
            began: Instant::now(),
            handles: AtomicUsize::new(1),
            submission_error: error.raw_os_error().unwrap_or(number),
            statistics: None,
            registry: Weak::new(),
            service_wake: None,
            drain_waiters: None,
        });
        result
            .outcome
            .set(Outcome {
                completion: PublishedCompletion::freeze(
                    state.empty_completion(CompletionStatus::IoError(error)),
                ),
                callback,
                callback_error: 0,
                elapsed: Duration::ZERO,
            })
            .ok()
            .unwrap();
        Self { state, result }
    }
    pub fn id(&self) -> RequestId {
        self.state.id
    }
    pub fn clone_handle(&self) -> Box<Self> {
        Box::new(self.clone())
    }
    pub fn request_cancel(&self) -> CancelOutcome {
        self.state.cancel(CompletionStatus::Cancelled)
    }
    pub fn is_finished(&self) -> bool {
        self.result.outcome.get().is_some()
    }
    pub fn elapsed_us(&self) -> i64 {
        self.result
            .outcome
            .get()
            .map_or_else(|| self.state.began.elapsed(), |o| o.elapsed)
            .as_micros()
            .min(i64::MAX as u128) as i64
    }
    pub fn data_size(&self) -> usize {
        self.result
            .outcome
            .get()
            .map_or(0, |o| o.completion.data_size())
    }
    pub fn has_callback_output(&self) -> bool {
        self.result.outcome.get().is_some_and(|o| {
            o.callback.is_some()
                && o.callback_error == 0
                && matches!(o.completion.status, CompletionStatus::Complete)
        })
    }
    pub fn data(&self) -> &[u8] {
        self.result
            .outcome
            .get()
            .and_then(|o| o.completion.data())
            .unwrap_or(&[])
    }
    #[cfg(any(feature = "cxx", test))]
    pub(crate) fn completed(&self) -> Option<&PublishedCompletion> {
        self.result.outcome.get().map(|o| &o.completion)
    }
    #[cfg(any(feature = "cxx", test))]
    pub(crate) fn callback_error(&self) -> i32 {
        self.result.outcome.get().map_or(0, |o| o.callback_error)
    }
    #[cfg(feature = "cxx")]
    pub fn callback_output(&self) -> io::Result<&crate::bridge::ffi::CallbackAdapter> {
        self.result
            .outcome
            .get()
            .filter(|o| {
                o.callback_error == 0 && matches!(o.completion.status, CompletionStatus::Complete)
            })
            .and_then(|o| o.callback.as_ref())
            .and_then(|c| c.foreign())
            .ok_or_else(|| io::ErrorKind::NotFound.into())
    }
    /// An elapsed wait window leaves pending I/O running. Once a callback starts,
    /// this call drains it before returning so its borrowed caller context stays valid.
    pub fn wait_until(&self, until: Instant) -> io::Result<Option<RequestResult>> {
        if self.state.wait(Some(until)) {
            Ok(Some(RequestResult {
                result: Arc::clone(&self.result),
            }))
        } else {
            Ok(None)
        }
    }
    pub fn wait(&self, until: Instant) -> io::Result<RequestResult> {
        self.wait_until(until)?
            .ok_or_else(|| io::ErrorKind::WouldBlock.into())
    }
    #[cfg(feature = "cxx")]
    pub fn wait_micros(&self, timeout_us: u64) -> i32 {
        let until = if timeout_us == u64::MAX {
            None
        } else {
            Instant::now().checked_add(Duration::from_micros(timeout_us))
        };
        if self.state.wait(until) {
            0
        } else {
            file_io::bridge::error_number(io::ErrorKind::WouldBlock.into())
        }
    }
    #[cfg(any(test, feature = "ownership-test-support"))]
    pub fn watch_final_charge(&self) -> io::Result<crate::DropWitness> {
        let progress = self.state.progress.lock().unwrap();
        progress
            .control
            .as_ref()
            .ok_or_else(|| io::Error::from(io::ErrorKind::NotFound))?
            .cell
            .charge
            .drop_witness
            .watch()
    }
}
impl RequestState {
    pub(super) fn device(&self) -> DeviceId {
        self.options.device
    }
    pub(super) fn stop(&self) {
        self.cancel(CompletionStatus::Shutdown);
    }
    pub(super) fn expire(&self) {
        self.cancel(CompletionStatus::TimedOut);
    }
    fn published(&self) {
        if let Some(registry) = self.registry.upgrade() {
            registry.lock().unwrap().remove(self.id);
        }
        self.changed.notify_all();
        if self
            .drain_waiters
            .as_ref()
            .is_some_and(|waiters| waiters.load(Ordering::Acquire) != 0)
        {
            if let Some(wake) = &self.service_wake {
                wake.notify();
            }
        }
    }
    fn empty_completion(&self, status: CompletionStatus) -> Completion {
        Completion {
            id: self.id,
            tag: self.options.tag,
            status,
            transferred: 0,
            depth_cost: DepthCost {
                raw: 0,
                effective: 0,
            },
            physical_attempts: 0,
            operation: None,
            window: self.options.window.clone(),
            _cell: None,
        }
    }
    fn record_statistics(&self, outcome: &Outcome) {
        use crate::statistics::{CompletionEvent, CompletionOutcome};
        if let (Some(statistics), Some(lane)) = (&self.statistics, self.options.statistics_lane) {
            let success = matches!(outcome.completion.status, CompletionStatus::Complete)
                && outcome.callback_error == 0;
            let _ = statistics.record(
                lane,
                CompletionEvent {
                    outcome: if success {
                        CompletionOutcome::Success
                    } else {
                        CompletionOutcome::Failure
                    },
                    bytes: outcome.completion.data_size() as u64,
                    total_us: outcome.elapsed.as_micros().min(u64::MAX as u128) as u64,
                },
            );
        }
    }
    fn cancel(&self, status: CompletionStatus) -> CancelOutcome {
        let mut progress = self.progress.lock().unwrap();
        if progress.phase != Phase::Pending {
            return CancelOutcome::NotPending;
        }
        progress.phase = Phase::Delivering(std::thread::current().id());
        let callback = progress.callback.take();
        let lifetime = progress.lifetime.take();
        let cell = progress
            .control
            .as_ref()
            .map(|control| Arc::clone(&control.cell));
        drop(progress);
        if let Some(cell) = &cell {
            cell.cancel();
        }
        drop((callback, lifetime));
        let mut completion = self.empty_completion(status);
        completion._cell = cell;
        let outcome = Outcome {
            completion: PublishedCompletion::freeze(completion),
            callback: None,
            callback_error: 0,
            elapsed: self.began.elapsed(),
        };
        self.record_statistics(&outcome);
        if let Some(result) = self.result.upgrade() {
            result.outcome.set(outcome).ok().unwrap();
        } else {
            drop(outcome);
        }
        let mut progress = self.progress.lock().unwrap();
        progress.phase = Phase::Ready;
        drop(progress);
        self.published();
        CancelOutcome::Cancelled
    }
    fn drain_delivery(&self) {
        let mut progress = self.progress.lock().unwrap();
        while let Phase::Delivering(owner) = progress.phase {
            if owner == std::thread::current().id() {
                return;
            }
            progress = self.changed.wait(progress).unwrap();
        }
    }
    fn wait(&self, until: Option<Instant>) -> bool {
        let mut progress = self.progress.lock().unwrap();
        loop {
            match progress.phase {
                Phase::Ready => return true,
                // Some database consumers lend stack context to a callback. Once committed,
                // waiting must drain that callback before permitting its caller to unwind.
                Phase::Delivering(owner) => {
                    if owner == std::thread::current().id() {
                        return false;
                    }
                    progress = self.changed.wait(progress).unwrap();
                }
                Phase::Pending => {
                    let now = Instant::now();
                    if self.options.policy.deadline.is_some_and(|d| d <= now) {
                        drop(progress);
                        self.cancel(CompletionStatus::TimedOut);
                        progress = self.progress.lock().unwrap();
                        continue;
                    }
                    if until.is_some_and(|d| d <= now) {
                        return false;
                    }
                    let end = match (until, self.options.policy.deadline) {
                        (Some(a), Some(b)) => Some(a.min(b)),
                        (a, b) => a.or(b),
                    };
                    progress = match end {
                        Some(end) => {
                            self.changed
                                .wait_timeout(progress, end.saturating_duration_since(now))
                                .unwrap()
                                .0
                        }
                        None => self.changed.wait(progress).unwrap(),
                    };
                }
            }
        }
    }
    fn publish(
        &self,
        completion: Completion,
        callback_error: i32,
        callback: Option<CallbackOwner>,
        delivering: bool,
    ) {
        let mut completion = PublishedCompletion::freeze(completion);
        if callback.is_some() {
            completion.buffer = None;
            if let Some(cell) = &completion._cell {
                cell.charge.release_bytes();
            }
        }
        let mut progress = self.progress.lock().unwrap();
        let expected = if delivering {
            Phase::Delivering(std::thread::current().id())
        } else {
            Phase::Pending
        };
        if progress.phase != expected {
            let cell = completion
                .buffer
                .as_ref()
                .and(completion._cell.as_ref())
                .cloned();
            drop(progress);
            drop(completion);
            if let Some(cell) = cell {
                cell.charge.release_bytes();
            }
            return;
        }
        progress.phase = Phase::Delivering(std::thread::current().id());
        let lifetime = progress.lifetime.take();
        let callback = callback.or_else(|| progress.callback.take());
        drop(progress);
        drop(lifetime);
        let outcome = Outcome {
            completion,
            callback,
            callback_error,
            elapsed: self.began.elapsed(),
        };
        self.record_statistics(&outcome);
        if let Some(result) = self.result.upgrade() {
            result.outcome.set(outcome).ok().unwrap();
        } else {
            drop(outcome);
        }
        let mut progress = self.progress.lock().unwrap();
        progress.phase = Phase::Ready;
        drop(progress);
        self.published();
    }
}
pub(super) struct Publisher {
    pub(super) state: Arc<RequestState>,
    pub(super) executor: Arc<super::delivery::Executor>,
}
impl RequestCompletion for Publisher {
    fn complete(self: Box<Self>, completion: Completion) {
        let callback = self.state.progress.lock().unwrap().callback.is_some();
        if callback && matches!(completion.status, CompletionStatus::Complete) {
            self.executor.enqueue(Arc::clone(&self.state), completion);
        } else {
            self.state.publish(completion, 0, None, false);
        }
    }
}
pub(super) fn discard(state: Arc<RequestState>, completion: Completion) {
    state.cancel(CompletionStatus::Shutdown);
    let cell = completion._cell.clone();
    drop(completion);
    if let Some(cell) = cell {
        cell.charge.release_bytes();
    }
}
pub(super) fn deliver(state: Arc<RequestState>, completion: Completion) {
    let mut progress = state.progress.lock().unwrap();
    if progress.phase != Phase::Pending {
        let cell = completion._cell.clone();
        drop(progress);
        drop(completion);
        if let Some(cell) = cell {
            cell.charge.release_bytes();
        }
        return;
    }
    if state
        .options
        .policy
        .deadline
        .is_some_and(|until| until <= Instant::now())
    {
        drop(progress);
        state.cancel(CompletionStatus::TimedOut);
        let cell = completion._cell.clone();
        drop(completion);
        if let Some(cell) = cell {
            cell.charge.release_bytes();
        }
        return;
    }
    let Some(mut callback) = progress.callback.take() else {
        drop(progress);
        state.publish(completion, 0, None, false);
        return;
    };
    progress.phase = Phase::Delivering(std::thread::current().id());
    drop(progress);
    let error = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        callback.process(completion.data().unwrap_or(&[]))
    }))
    .ok()
    .and_then(Result::ok)
    .unwrap_or(-4000);
    state.publish(completion, error, Some(callback), true);
}

#[cfg(test)]
mod tests {
    use super::*;
    struct CountDrop(Arc<AtomicUsize>);
    impl Consumer for CountDrop {
        fn process(&mut self, _: &[u8]) -> Result<i32, ()> {
            Ok(0)
        }
    }
    impl Drop for CountDrop {
        fn drop(&mut self) {
            self.0.fetch_add(1, Ordering::Relaxed);
        }
    }
    fn fixture() -> (Service, Arc<std::fs::File>, std::path::PathBuf) {
        let path = std::env::temp_dir().join(format!(
            "request-owner-{}-{}",
            std::process::id(),
            NEXT_ID.fetch_add(1, Ordering::Relaxed)
        ));
        let file = Arc::new(
            std::fs::OpenOptions::new()
                .read(true)
                .write(true)
                .create_new(true)
                .open(&path)
                .unwrap(),
        );
        file.set_len(4096).unwrap();
        let service = Service::start(Config {
            callback_threads: 1,
            statistics_lanes: 2,
            admission: AdmissionLimits {
                max_records: 4,
                max_bytes: 16384,
            },
            devices: [(DeviceId(1), DeviceLimits { depth: 2 })]
                .into_iter()
                .collect(),
        })
        .unwrap();
        service
            .register_device(DeviceConfig {
                id: DeviceId(1),
                worker: Backend::new(file_io::BackendKind::Worker { threads: 1 }, 2).unwrap(),
                native: None,
            })
            .unwrap();
        (service, file, path)
    }
    #[test]
    fn final_handle_releases_callback_even_while_control_state_is_retained() {
        let (service, file, path) = fixture();
        let (options, target) = RequestSpec::new(
            DeviceId(1),
            Target::File {
                file: Arc::clone(&file),
                offset: 0,
            },
            4096,
        )
        .split();
        let drops = Arc::new(AtomicUsize::new(0));
        let request = service
            .submit_request(
                options,
                target,
                Direction::Read,
                &[],
                Some(Box::new(CountDrop(Arc::clone(&drops)))),
                None,
            )
            .unwrap();
        request
            .wait(Instant::now() + Duration::from_secs(3))
            .unwrap();
        let retained_control = Arc::clone(&request.state);
        drop(request);
        assert_eq!(
            drops.load(Ordering::Relaxed),
            1,
            "a physical/task control reference postponed callback allocator release"
        );
        drop(retained_control);
        service.shutdown().unwrap();
        drop(file);
        std::fs::remove_file(path).unwrap();
    }
    // Hold a physically retired completion immediately before its Publisher can enqueue it.
    // Admission and the native Request use the production path; only transport scheduling is
    // controlled here, so shutdown need not depend on a real backend's thread scheduling.
    fn staged_publisher(
        service: &Service,
        file: &Arc<std::fs::File>,
        callback_drops: Arc<AtomicUsize>,
        payload_drops: Arc<AtomicUsize>,
    ) -> (Request, Box<dyn RequestCompletion>, Completion) {
        let (options, target) = RequestSpec::new(
            DeviceId(1),
            Target::File {
                file: Arc::clone(file),
                offset: 0,
            },
            4096,
        )
        .split();
        let mut admission = service.begin(options).unwrap();
        admission.wait_capacity().unwrap();
        let mut lifetime = admission.lifetime();
        lifetime.capture(&admission, target);
        let request = Request::accepted(
            &mut admission,
            lifetime,
            Some(Box::new(CountDrop(callback_drops))),
            Instant::now(),
        )
        .unwrap();
        let publisher = request.publisher(Arc::clone(&service.shared.executor));
        let mut completion = request.state.empty_completion(CompletionStatus::Complete);
        completion.transferred = 4096;
        completion._cell = Some(Arc::clone(&admission.cell));
        completion.operation = Some(
            Operation::read(
                Arc::clone(file),
                AlignedBuffer::zeroed(4096, 4096).unwrap(),
                0,
            )
            .unwrap()
            .keep_alive(CountDrop(payload_drops)),
        );
        (request, publisher, completion)
    }
    fn assert_retired_resources(
        service: Service,
        request: Request,
        callback_drops: Arc<AtomicUsize>,
        payload_drops: Arc<AtomicUsize>,
    ) {
        assert_eq!(callback_drops.load(Ordering::Relaxed), 1);
        assert_eq!(payload_drops.load(Ordering::Relaxed), 1);
        assert_eq!(service.snapshot().unwrap().usage.bytes, 0);
        let state = Arc::downgrade(&request.state);
        let result = Arc::downgrade(&request.result);
        let executor = Arc::downgrade(&service.shared.executor);
        drop(request);
        assert!(
            state.upgrade().is_none(),
            "retired publisher retained request control"
        );
        assert!(
            result.upgrade().is_none(),
            "retired publisher retained result payload"
        );
        assert_eq!(service.snapshot().unwrap().usage.records, 0);
        drop(service);
        assert!(
            executor.upgrade().is_none(),
            "closed queue retained its executor"
        );
    }
    #[test]
    fn cancellation_retains_payload_until_late_completion_is_discarded() {
        let (service, file, path) = fixture();
        let callbacks = Arc::new(AtomicUsize::new(0));
        let payloads = Arc::new(AtomicUsize::new(0));
        let (request, publisher, completion) = staged_publisher(
            &service,
            &file,
            Arc::clone(&callbacks),
            Arc::clone(&payloads),
        );
        assert_eq!(request.request_cancel(), CancelOutcome::Cancelled);
        let result = request
            .wait(Instant::now() + Duration::from_secs(1))
            .unwrap();
        assert!(matches!(result.status, CompletionStatus::Cancelled));
        assert_eq!(callbacks.load(Ordering::Relaxed), 1);
        assert_eq!(payloads.load(Ordering::Relaxed), 0);
        assert_eq!(service.snapshot().unwrap().usage.bytes, 4096);

        publisher.complete(completion);

        assert!(matches!(result.status, CompletionStatus::Cancelled));
        assert_eq!(payloads.load(Ordering::Relaxed), 1);
        assert_eq!(service.snapshot().unwrap().usage.bytes, 0);
        drop(request);
        assert_eq!(service.snapshot().unwrap().usage.records, 1);
        drop(result);
        assert_eq!(service.snapshot().unwrap().usage.records, 0);
        service.shutdown().unwrap();
        drop(file);
        std::fs::remove_file(path).unwrap();
    }
    #[test]
    fn late_publisher_after_shutdown_releases_all_resources() {
        let (service, file, path) = fixture();
        let callbacks = Arc::new(AtomicUsize::new(0));
        let payloads = Arc::new(AtomicUsize::new(0));
        let (request, publisher, completion) = staged_publisher(
            &service,
            &file,
            Arc::clone(&callbacks),
            Arc::clone(&payloads),
        );
        let (sampled, sampled_rx) = std::sync::mpsc::channel();
        let (resume, resume_rx) = std::sync::mpsc::channel();
        *service.shared.executor.before_enqueue.lock().unwrap() = Some(Box::new(move || {
            sampled.send(()).unwrap();
            resume_rx.recv().unwrap();
        }));
        let publishing = std::thread::spawn(move || publisher.complete(completion));
        sampled_rx.recv_timeout(Duration::from_secs(3)).unwrap();
        service.shutdown().unwrap();
        assert_eq!(payloads.load(Ordering::Relaxed), 0);
        assert!(service.shared.executor.prepare(1, true).is_err());
        assert!(service.shared.executor.prepare(1, false).is_err());
        resume.send(()).unwrap();
        publishing.join().unwrap();
        assert_retired_resources(service, request, callbacks, payloads);
        drop(file);
        std::fs::remove_file(path).unwrap();
    }
    #[test]
    fn enqueue_cannot_enter_between_drain_and_close() {
        let (service, file, path) = fixture();
        let callbacks = Arc::new(AtomicUsize::new(0));
        let payloads = Arc::new(AtomicUsize::new(0));
        let (request, publisher, completion) = staged_publisher(
            &service,
            &file,
            Arc::clone(&callbacks),
            Arc::clone(&payloads),
        );
        let (drained, drained_rx) = std::sync::mpsc::channel();
        let (close, close_rx) = std::sync::mpsc::channel();
        *service.shared.executor.before_close.lock().unwrap() = Some(Box::new(move || {
            drained.send(()).unwrap();
            close_rx.recv().unwrap();
        }));
        let executor = Arc::clone(&service.shared.executor);
        let shutting_down = std::thread::spawn(move || executor.shutdown().unwrap());
        drained_rx.recv_timeout(Duration::from_secs(3)).unwrap();
        let (attempted, attempted_rx) = std::sync::mpsc::channel();
        *service.shared.executor.before_enqueue.lock().unwrap() = Some(Box::new(move || {
            attempted.send(()).unwrap();
        }));
        let publishing = std::thread::spawn(move || publisher.complete(completion));
        attempted_rx.recv_timeout(Duration::from_secs(3)).unwrap();
        assert_eq!(payloads.load(Ordering::Relaxed), 0);
        close.send(()).unwrap();
        shutting_down.join().unwrap();
        publishing.join().unwrap();
        assert_eq!(payloads.load(Ordering::Relaxed), 1);
        service.shutdown().unwrap();
        assert_retired_resources(service, request, callbacks, payloads);
        drop(file);
        std::fs::remove_file(path).unwrap();
    }
}
