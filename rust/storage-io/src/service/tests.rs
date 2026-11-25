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

use super::engine::{Driver as Engine, Stage};
use super::*;
use crate::{BlockConfig, SpaceBudget, UnmarkedBlockFile};
use file_io::{BackendKind, Retired, SubmissionId};
use std::fs::{self, OpenOptions};
use std::path::PathBuf;
use std::sync::{Barrier, mpsc};
use std::thread;
mod direct;
mod ownership;
mod request_owner;

struct ChannelCompletion(mpsc::Sender<Completion>);
impl RequestCompletion for ChannelCompletion {
    fn complete(self: Box<Self>, completion: Completion) {
        let _ = self.0.send(completion);
    }
}
impl Admission {
    fn publish_channel(&mut self, sender: mpsc::Sender<Completion>) -> io::Result<RequestId> {
        self.register_completion(Box::new(ChannelCompletion(sender)))?;
        self.publish_driver()
    }
}
const DEVICE: DeviceId = DeviceId(1);
const OTHER: DeviceId = DeviceId(2);
struct Directory(PathBuf);
impl Directory {
    fn new() -> Self {
        static NEXT: AtomicU64 = AtomicU64::new(0);
        let path = std::env::temp_dir().join(format!(
            "storage-service-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir(&path).unwrap();
        Self(path)
    }
    fn file(&self) -> Arc<File> {
        Arc::new(
            OpenOptions::new()
                .create_new(true)
                .read(true)
                .write(true)
                .open(self.0.join("file"))
                .unwrap(),
        )
    }
}
impl Drop for Directory {
    fn drop(&mut self) {
        fs::remove_dir_all(&self.0).unwrap();
    }
}
fn deadline() -> Instant {
    Instant::now() + Duration::from_secs(5)
}
fn config() -> Config {
    Config {
        callback_threads: 2,
        statistics_lanes: 2,
        admission: AdmissionLimits {
            max_records: 32,
            max_bytes: 8 * 1024 * 1024,
        },
        devices: [DEVICE, OTHER]
            .into_iter()
            .map(|id| (id, DeviceLimits { depth: 3 }))
            .collect(),
    }
}
fn backend() -> Backend {
    Backend::new(BackendKind::Worker { threads: 2 }, 4).unwrap()
}
fn service() -> Service {
    let service = Service::start(config()).unwrap();
    service
        .register_device(DeviceConfig {
            id: DEVICE,
            worker: backend(),
            native: None,
        })
        .unwrap();
    service
}
fn target(file: &Arc<File>) -> Target {
    Target::File {
        file: Arc::clone(file),
        offset: 0,
    }
}
fn spec(file: &Arc<File>, length: usize) -> RequestSpec {
    RequestSpec::new(DEVICE, target(file), length)
}
fn read(file: &Arc<File>, size: usize, window: Range<usize>, policy: RequestPolicy) -> RequestSpec {
    let mut spec = spec(file, size);
    spec.window = window;
    spec.policy = policy;
    spec
}
fn prepare(service: &Service, spec: RequestSpec, direction: Direction, bytes: &[u8]) -> Admission {
    let (options, target) = spec.split();
    let mut admission = service.begin(options).unwrap();
    admission.wait_capacity().unwrap();
    admission.prepare(target, direction, bytes).unwrap();
    admission
}
fn receive_driven(service: &Service, receiver: &mpsc::Receiver<Completion>) -> Completion {
    let until = deadline();
    loop {
        Engine::progress_inline(&Arc::downgrade(&service.driver));
        if let Ok(result) = receiver.try_recv() {
            return result;
        }
        assert!(Instant::now() < until);
        thread::yield_now();
    }
}
fn shared_read(service: &Service, spec: RequestSpec) -> (RequestId, mpsc::Receiver<Completion>) {
    let mut admission = prepare(service, spec, Direction::Read, &[]);
    let (destination, completed) = mpsc::channel();
    let id = admission.publish_channel(destination).unwrap();
    (id, completed)
}
fn engine_with(config: Config) -> Engine {
    let config = Arc::new(config);
    let wake = Arc::new(Wake::default());
    let driver_has_work = Arc::new(AtomicBool::new(false));
    let metadata_ledger = Arc::new(Mutex::new(
        Ledger::new(config.admission, Arc::clone(&driver_has_work)),
    ));
    let shared = Arc::new(Shared {
        requests: Arc::new(Mutex::new(request::Registry::default())),
        drain_waiters: Arc::new(AtomicUsize::new(0)),
        statistics: Arc::new(
            crate::statistics::CompletionStatistics::new(
                config.statistics_lanes,
                statistics_now_us(),
            )
            .unwrap(),
        ),
        executor: delivery::Executor::new(),
        accepting: Arc::new(AtomicBool::new(true)),
        driver_has_work,
        ledger: metadata_ledger,
        wake,
        gate: Mutex::new(Gate {
            phase: ServicePhase::Active,
            devices: BTreeMap::new(),
            config,
            waiting: BTreeMap::new(),
        }),
        cancellations: Arc::new(Mutex::new(VecDeque::new())),
        worker_routes: Mutex::new(BTreeMap::new()),
        driver: Mutex::new(Weak::new()),
    });
    let mut engine = Engine::new(shared);
    engine
        .register(DeviceConfig {
            id: DEVICE,
            worker: backend(),
            native: None,
        })
        .unwrap();
    engine
}
fn engine() -> Engine {
    engine_with(config())
}
fn core_prepare(
    engine: &mut Engine,
    spec: RequestSpec,
    direction: Direction,
    bytes: &[u8],
) -> Admission {
    let (options, target) = spec.split();
    let mut admission = Admission::begin(Arc::clone(&engine.shared), options).unwrap();
    engine.accept_close();
    admission.wait_capacity().unwrap();
    admission.prepare(target, direction, bytes).unwrap();
    admission
}
fn enqueue(
    engine: &mut Engine,
    spec: RequestSpec,
    direction: Direction,
    bytes: &[u8],
) -> (RequestId, mpsc::Receiver<Completion>) {
    let mut admission = core_prepare(engine, spec, direction, bytes);
    let (destination, completed) = mpsc::channel();
    let id = admission.id();
    let admission::Preparation::Prepared(operation) =
        std::mem::replace(&mut admission.state, admission::Preparation::Published)
    else {
        unreachable!()
    };
    admission
        .cell
        .phase
        .store(admission::PUBLISHED, Ordering::Release);
    engine.accept(Incoming {
        operation,
        cell: Arc::clone(&admission.cell),
        request: Some(Box::new(ChannelCompletion(destination))),
    });
    drop(admission);
    (id, completed)
}
fn retired(engine: &Engine) -> (SubmissionId, Retired) {
    engine
        .backend(DEVICE, 0)
        .retire(deadline())
        .unwrap()
        .unwrap()
}
fn retire(engine: &mut Engine) {
    let (id, retired) = retired(engine);
    engine.retired(id, retired);
    engine.flush_test();
}
fn dispatch(engine: &mut Engine) {
    engine.dispatch(Instant::now());
    engine.flush_test();
}

#[test]
fn addressed_completions_cannot_be_stolen_by_shared_consumer() {
    let directory = Directory::new();
    let file = directory.file();
    file_io::write_all_at(&file, b"abcdef", 0).unwrap();
    let service = service();
    let mut spec = read(&file, 8, 2..6, RequestPolicy::default());
    spec.tag = 19;
    let (shared, receiver) = shared_read(&service, spec);
    let mut waiter = service
        .read(read(&file, 8, 0..3, RequestPolicy::default()))
        .unwrap();
    let completion = receive_driven(&service, &receiver);
    assert_eq!(completion.id, shared);
    assert_eq!(completion.tag, 19);
    assert!(matches!(completion.status, CompletionStatus::Complete));
    assert_eq!(completion.data().unwrap(), b"cdef");
    let result = waiter.wait(deadline()).unwrap();
    assert_eq!(result.id, waiter.id());
    assert_eq!(result.data().unwrap(), b"abc");
    assert!(receiver.try_recv().is_err());
    service.shutdown().unwrap();
    assert_eq!(completion.data().unwrap(), b"cdef");
}

#[test]
#[cfg(target_os = "linux")]
fn native_service_retires_block_lease_before_consumed_buffer_is_released() {
    let directory = Directory::new();
    let owner = UnmarkedBlockFile::open(
        &directory.0.join("blocks"),
        BlockConfig {
            block_size: 4096,
            initial_size: crate::FileSize::Bytes(8192),
            direct_io: true,
            punch_holes: true,
        },
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
    )
    .unwrap()
    .finish_marking()
    .unwrap();
    let block = owner.allocate().unwrap();
    let service = Service::start(config()).unwrap();
    service
        .register_device(DeviceConfig {
            id: DEVICE,
            worker: backend(),
            native: Some(Backend::new(BackendKind::NativeAio, 4).unwrap()),
        })
        .unwrap();
    let written = service
        .write(
            RequestSpec::new(
                DEVICE,
                Target::Block(owner.region(block, 0..4096).unwrap()),
                4096,
            ),
            &[0x35; 4096],
        )
        .unwrap()
        .wait(deadline())
        .unwrap();
    assert!(matches!(written.status, CompletionStatus::Complete));
    assert_eq!(written.transferred, 4096);
    drop(written);
    owner.sync().unwrap();
    let mut request = RequestSpec::new(
        DEVICE,
        Target::Block(owner.region(block, 0..4096).unwrap()),
        4096,
    );
    request.window = 12..20;
    let read = service.read(request).unwrap().wait(deadline()).unwrap();
    assert_eq!(read.data().unwrap(), &[0x35; 8]);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 4096);
    owner.release(block).unwrap();
    assert_eq!(owner.allocate().unwrap(), block);
    service.shutdown().unwrap();
    assert_eq!(read.data().unwrap(), &[0x35; 8]);
}
#[test]
fn completion_holds_admission_until_consumer_releases_bytes() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut config = config();
    config.admission.max_bytes = 8;
    service.reconfigure(config).unwrap();
    let completion = service
        .read(spec(&file, 8))
        .unwrap()
        .wait(deadline())
        .unwrap();
    let (options, target) = spec(&file, 8).split();
    let mut waiting = service.begin(options).unwrap();
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 8);
    assert_eq!(
        waiting.cell.phase.load(Ordering::Acquire),
        admission::WAITING
    );
    drop(completion);
    waiting.wait_capacity().unwrap();
    waiting.prepare(target, Direction::Read, &[]).unwrap();
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 8);
    drop(waiting);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 0);
    service.shutdown().unwrap();
}
#[test]
fn short_envelope_preserves_requested_bytes_and_callback_requirement() {
    let directory = Directory::new();
    let file = directory.file();
    file_io::write_all_at(&file, b"abcdef", 0).unwrap();
    let service = service();
    let policy = RequestPolicy {
        completion: CompletionRequirement::FullBuffer,
        ..RequestPolicy::default()
    };
    let completion = service
        .read(read(&file, 8, 1..4, policy))
        .unwrap()
        .wait(deadline())
        .unwrap();
    assert!(matches!(
        completion.status,
        CompletionStatus::IncompleteEnvelope
    ));
    assert_eq!(completion.transferred, 6);
    assert_eq!(completion.data().unwrap(), b"bcd");
    let completion = service
        .read(read(&file, 8, 4..8, RequestPolicy::default()))
        .unwrap()
        .wait(deadline())
        .unwrap();
    assert!(matches!(completion.status, CompletionStatus::ShortIo));
    assert_eq!(completion.data().unwrap(), b"ef");
    service.shutdown().unwrap();
}
#[test]
fn aligned_short_read_retries_only_the_remaining_suffix() {
    let directory = Directory::new();
    let file = directory.file();
    file_io::write_all_at(&file, b"abcd", 0).unwrap();
    let service = service();
    let policy = RequestPolicy {
        retry_alignment: NonZeroUsize::new(4),
        ..RequestPolicy::default()
    };
    let completion = service
        .read(read(&file, 8, 0..8, policy))
        .unwrap()
        .wait(deadline())
        .unwrap();
    assert!(matches!(completion.status, CompletionStatus::ShortIo));
    assert_eq!(completion.transferred, 4);
    assert_eq!(completion.data().unwrap(), b"abcd");
    assert_eq!(completion.physical_attempts, 2);
    service.shutdown().unwrap();
}
#[test]
fn timeout_before_dispatch_does_not_write_and_completes_once() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine();
    let at = Instant::now() + Duration::from_secs(1);
    let mut request = spec(&file, 4);
    request.policy.deadline = Some(at);
    let (id, results) = enqueue(&mut engine, request, Direction::Write, b"late");
    engine.dispatch(at);
    engine.flush_test();
    let completion = results.recv().unwrap();
    assert!(matches!(completion.status, CompletionStatus::TimedOut));
    assert!(completion.buffer().is_none());
    assert_eq!(
        cancel(&mut engine, id, CompletionStatus::Cancelled),
        CancelOutcome::NotPending
    );
    assert!(engine.active.is_empty());
    assert_eq!(file.metadata().unwrap().len(), 0);
    drop(completion);
    assert_eq!(engine.snapshot().reserved_bytes, 0);
}
fn block_owner(directory: &Directory) -> crate::BlockFile {
    UnmarkedBlockFile::open(
        &directory.0.join("blocks"),
        BlockConfig {
            block_size: 4096,
            initial_size: crate::FileSize::Bytes(8192),
            direct_io: false,
            punch_holes: false,
        },
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
    )
    .unwrap()
    .finish_marking()
    .unwrap()
}
#[test]
fn cancellation_keeps_region_and_budget_until_physical_retirement() {
    let directory = Directory::new();
    let owner = block_owner(&directory);
    let block = owner.allocate().unwrap();
    let mut engine = engine();
    let request = RequestSpec::new(
        DEVICE,
        Target::Block(owner.region(block, 0..4096).unwrap()),
        4096,
    );
    let (id, result) = enqueue(&mut engine, request, Direction::Write, &[0; 4096]);
    dispatch(&mut engine);
    assert_eq!(
        cancel(&mut engine, id, CompletionStatus::Cancelled),
        CancelOutcome::Cancelled
    );
    drop(result.recv().unwrap());
    owner.release(block).unwrap();
    assert!(matches!(
        owner.allocate(),
        Err(crate::BlockError::Exhausted)
    ));
    assert_eq!(engine.snapshot().reserved_bytes, 4096);
    assert_eq!(engine.snapshot().devices[0].physical_depth, 1);
    retire(&mut engine);
    assert!(engine.active.is_empty());
    assert_eq!(engine.snapshot().reserved_bytes, 0);
    assert_eq!(owner.allocate().unwrap(), block);
    assert!(result.try_recv().is_err());
}
#[test]
fn partial_prefix_survives_retryable_submission_error() {
    let directory = Directory::new();
    let file = directory.file();
    file_io::write_all_at(&file, b"abcdefgh", 0).unwrap();
    let mut engine = engine();
    let policy = RequestPolicy {
        retry_alignment: NonZeroUsize::new(4),
        max_error_retries: 1,
        ..RequestPolicy::default()
    };
    let (request_id, result) = enqueue(
        &mut engine,
        read(&file, 12, 0..12, policy),
        Direction::Read,
        &[],
    );
    dispatch(&mut engine);
    let (id, mut r) = retired(&engine);
    r.result = Ok(4);
    engine.retired(id, r);
    engine.flush_test();
    assert_eq!(engine.snapshot().devices[0].physical_depth, 0);
    dispatch(&mut engine);
    let (id, mut r) = retired(&engine);
    r.result = Err(io::ErrorKind::WouldBlock.into());
    engine.retired(id, r);
    engine.flush_test();
    assert_eq!(engine.active[&request_id].transferred, 4);
    let Stage::Queued(ref operation) = engine.active[&request_id].stage else {
        panic!("retry is queued")
    };
    assert_eq!(operation.offset(), 4);
    assert_eq!(operation.remaining_len(), 8);
    dispatch(&mut engine);
    retire(&mut engine);
    dispatch(&mut engine);
    retire(&mut engine);
    let completion = result.recv().unwrap();
    assert_eq!(completion.transferred, 8);
    assert_eq!(completion.data().unwrap(), b"abcdefgh");
    assert_eq!(completion.physical_attempts, 4);
    assert!(matches!(completion.status, CompletionStatus::ShortIo));
}
#[test]
#[cfg_attr(
    feature = "ownership-test-support",
    allow(
        clippy::result_large_err,
        reason = "the proof-only owner enlarges Operation returned by the dispatch double"
    )
)]
fn backend_rejection_after_short_read_keeps_the_completed_prefix() {
    let directory = Directory::new();
    let file = directory.file();
    file_io::write_all_at(&file, b"abcd", 0).unwrap();
    let mut engine = engine();
    let policy = RequestPolicy {
        retry_alignment: NonZeroUsize::new(4),
        ..RequestPolicy::default()
    };
    let (_, results) = enqueue(
        &mut engine,
        read(&file, 8, 0..8, policy),
        Direction::Read,
        &[],
    );
    dispatch(&mut engine);
    retire(&mut engine);
    engine.dispatch_with(Instant::now(), |_, operation| {
        Err(file_io::SubmitFailure {
            operation,
            error: io::ErrorKind::BrokenPipe.into(),
        })
    });
    engine.flush_test();
    let completion = results.recv().unwrap();
    assert_eq!(completion.transferred, 4);
    assert_eq!(completion.data().unwrap(), b"abcd");
    assert_eq!(completion.physical_attempts, 1);
}
#[test]
fn prepared_admission_reserves_before_build_and_releases_on_failure() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut config = config();
    config.admission.max_records = 1;
    service.reconfigure(config).unwrap();
    let request = prepare(&service, spec(&file, 8), Direction::Read, &[]);
    assert!(
        matches!(service.begin(spec(&file, 8).split().0), Err(e) if e.kind() == io::ErrorKind::WouldBlock)
    );
    assert_eq!(service.snapshot().unwrap().admitted_requests, 1);
    drop(request);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 0);
    let (options, target) = spec(&file, 8).split();
    let mut admission = service.begin(options).unwrap();
    admission.wait_capacity().unwrap();
    assert!(
        admission
            .prepare_with(target, Direction::Read, &[], |_, _| Err(
                io::ErrorKind::OutOfMemory.into()
            ))
            .is_err()
    );
    drop(admission);
    assert_eq!(service.snapshot().unwrap().admitted_requests, 0);
    let (options, target) = spec(&file, 8).split();
    let mut admission = service.begin(options).unwrap();
    admission.wait_capacity().unwrap();
    assert!(
        admission
            .prepare_with(target, Direction::Read, &[], |_, _| AlignedBuffer::zeroed(
                4, 8
            ))
            .is_err()
    );
    drop(admission);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 0);
    #[cfg(panic = "unwind")]
    {
        let (options, target) = spec(&file, 8).split();
        let mut admission = service.begin(options).unwrap();
        admission.wait_capacity().unwrap();
        assert!(
            std::panic::catch_unwind(std::panic::AssertUnwindSafe(move || {
                admission
                    .prepare_with(
                        target,
                        Direction::Read,
                        &[],
                        |_, _| -> io::Result<AlignedBuffer> { panic!("allocation failed") },
                    )
                    .unwrap();
            }))
            .is_err()
        );
        assert_eq!(service.snapshot().unwrap().reserved_bytes, 0);
    }
    service.shutdown().unwrap();
}
#[test]
fn preparation_rejects_malformed_input_before_building_buffer() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    for (direction, bytes) in [
        (Direction::Read, &b"x"[..]),
        (Direction::Write, &b"short"[..]),
    ] {
        let (options, target) = spec(&file, 8).split();
        let mut admission = service.begin(options).unwrap();
        admission.wait_capacity().unwrap();
        assert_eq!(
            admission
                .prepare_with(target, direction, bytes, |_, _| {
                    panic!("invalid input must be rejected before buffer construction")
                })
                .unwrap_err()
                .kind(),
            io::ErrorKind::InvalidInput
        );
        drop(admission);
        let snapshot = service.snapshot().unwrap();
        assert_eq!(snapshot.admitted_requests, 0);
        assert_eq!(snapshot.reserved_bytes, 0);
    }
    service.shutdown().unwrap();
}

#[test]
fn prepared_write_owns_source_and_retains_charge_through_completion() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let expected: Vec<u8> = (0..8193).map(|i| (i % 251) as u8).collect();
    let mut source = expected.clone();
    let mut admission = prepare(
        &service,
        spec(&file, source.len()),
        Direction::Write,
        &source,
    );
    source.fill(73);
    drop(source);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, expected.len());
    let (destination, completed) = mpsc::channel();
    admission.publish_channel(destination).unwrap();
    drop(admission);
    let completion = receive_driven(&service, &completed);
    assert!(matches!(completion.status, CompletionStatus::Complete));
    assert_eq!(completion.transferred, expected.len());
    let mut actual = vec![0; expected.len()];
    file_io::read_exact_at(&file, &mut actual, 0).unwrap();
    assert_eq!(actual, expected);
    let snapshot = service.snapshot().unwrap();
    assert_eq!(snapshot.admitted_requests, 1);
    assert_eq!(snapshot.reserved_bytes, expected.len());
    drop(completion);
    let snapshot = service.snapshot().unwrap();
    assert_eq!(snapshot.admitted_requests, 0);
    assert_eq!(snapshot.reserved_bytes, 0);
    service.shutdown().unwrap();
}

#[test]
fn prepared_request_retains_charge_across_submission_and_shrink() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut admission = prepare(&service, spec(&file, 4096), Direction::Read, &[]);
    let mut config = config();
    config.admission.max_bytes = 1;
    service.reconfigure(config).unwrap();
    assert_eq!(service.snapshot().unwrap().debt.bytes, 4095);
    let (destination, completed) = mpsc::channel();
    admission.publish_channel(destination).unwrap();
    drop(admission);
    let completion = receive_driven(&service, &completed);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 4096);
    drop(completion);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 0);
    service.shutdown().unwrap();
}
#[test]
fn stopped_service_preserves_prepared_reservation_on_rejection() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut admission = prepare(&service, spec(&file, 8), Direction::Read, &[]);
    let ledger = Arc::clone(&service.shared.ledger);
    let (destination, completed) = mpsc::channel();
    service.stop();
    assert_eq!(
        admission.publish_channel(destination).unwrap_err().kind(),
        io::ErrorKind::BrokenPipe
    );
    service.shutdown().unwrap();
    assert_eq!(ledger.lock().unwrap().usage.bytes, 8);
    assert!(completed.try_recv().is_err());
    drop(admission);
    assert_eq!(ledger.lock().unwrap().usage.bytes, 0);
}

fn limited_buffer_service() -> Service {
    let mut config = config();
    config.admission.max_bytes = 8192;

    let service = Service::start(config).unwrap();
    service
        .register_device(DeviceConfig {
            id: DEVICE,
            worker: backend(),
            native: None,
        })
        .unwrap();
    service
}

#[test]
fn released_buffer_charge_allows_a_full_partition_request() {
    let directory = Directory::new();
    let file = directory.file();
    let service = limited_buffer_service();
    drop(prepare(&service, spec(&file, 4096), Direction::Read, &[]));
    let admission = prepare(&service, spec(&file, 8192), Direction::Read, &[]);
    let snapshot = service.snapshot().unwrap();
    assert_eq!(snapshot.reserved_bytes, 8192);
    drop(admission);
    service.shutdown().unwrap();
}

#[test]
fn retained_buffer_lifetime_accounts_for_reconfiguration_debt() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = limited_buffer_service();
    let mut waiter = service.read(spec(&file, 4096)).unwrap();
    let lifetime = waiter.wait(deadline()).unwrap();
    drop(waiter);
    let snapshot = service.snapshot().unwrap();
    assert_eq!(snapshot.reserved_bytes, 4096);
    let mut config = snapshot.config;
    config.admission.max_bytes = 2048;
    service.reconfigure(config).unwrap();
    let snapshot = service.snapshot().unwrap();
    assert_eq!(snapshot.debt.bytes, 2048);
    drop(lifetime);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 0);
    service.shutdown().unwrap();
}

#[test]
fn fallible_preparation_retains_last_target_and_charge_until_guard_is_released() {
    let directory = Directory::new();
    let file = directory.file();
    for failure in 0..5 {
        let mut engine = engine();
        let (mut options, mut target) = spec(&file, 4096).split();
        if failure == 4 {
            let owner = UnmarkedBlockFile::open(
                &directory.0.join("geometry"),
                BlockConfig {
                    block_size: 4096,
                    initial_size: crate::FileSize::Bytes(8192),
                    direct_io: false,
                    punch_holes: false,
                },
                SpaceBudget::default(),
                file_io::DirectoryDurability::Required,
                &mut file_io::DirectorySyncSummary::default(),
            )
            .unwrap()
            .finish_marking()
            .unwrap();
            let block = owner.allocate().unwrap();
            target = Target::Block(owner.region(block, 0..4096).unwrap());
            options.length = 8192;
            options.window = 0..8192;
        }
        let expected_bytes = options.length;
        if failure == 3 {
            options.execution = Execution::PreferNative;
        }
        let mut admission = Admission::begin(Arc::clone(&engine.shared), options).unwrap();
        let mut lifetime = admission.lifetime();
        lifetime.capture(&admission, target);
        let guard = Arc::new(Mutex::new(()));
        let drop_guard = Arc::clone(&guard);
        let dropped = Arc::new(AtomicBool::new(false));
        let drop_flag = Arc::clone(&dropped);
        Arc::get_mut(lifetime.target.as_mut().unwrap())
            .unwrap()
            .drop_hook = Some(Box::new(move || {
            assert!(drop_guard.try_lock().is_ok());
            drop_flag.store(true, Ordering::Release);
        }));
        engine.accept_close();
        admission.wait_capacity().unwrap();
        let control = admission.take_control().unwrap();
        {
            let _guard = guard.lock().unwrap();
            if failure == 1 {
                control.request_cancel();
            }
            let target = lifetime.target.as_ref().unwrap().target.clone();
            let result = if failure == 0 {
                admission.prepare_with(target, Direction::Read, &[], |_, _| {
                    Err(io::ErrorKind::OutOfMemory.into())
                })
            } else if failure == 2 {
                admission.prepare(target, Direction::Write, &[])
            } else {
                admission.prepare(target, Direction::Read, &[])
            };
            if failure != 3 {
                assert!(result.is_err());
            }
            #[cfg(feature = "cxx")]
            if failure == 3 {
                assert!(
                    admission
                        .set_worker_fault(file_io::WorkerFault::new(Duration::ZERO, false))
                        .is_err()
                );
            }
            drop(admission);
            drop(control);
            assert!(!dropped.load(Ordering::Acquire));
            assert_eq!(
                engine.shared.ledger.lock().unwrap().usage,
                ResourceUsage {
                    records: 1,
                    bytes: expected_bytes
                }
            );
        }
        drop(lifetime);
        assert!(dropped.load(Ordering::Acquire));
        assert_eq!(
            engine.shared.ledger.lock().unwrap().usage,
            ResourceUsage::default()
        );
    }
}

fn cancel(engine: &mut Engine, id: RequestId, status: CompletionStatus) -> CancelOutcome {
    let outcome = engine.cancel(id, status);
    engine.flush_test();
    outcome
}

#[test]
fn prepared_service_registers_devices_but_admits_requests_only_after_activation() {
    let directory = Directory::new();
    let file = directory.file();
    let service = Service::prepare(config()).unwrap();
    service
        .register_device(DeviceConfig {
            id: DEVICE,
            worker: backend(),
            native: None,
        })
        .unwrap();
    assert!(
        matches!(service.write(spec(&file, 4096), &[0x42; 4096]), Err(error) if error.kind() == io::ErrorKind::BrokenPipe)
    );
    service.activate().unwrap();
    service.activate().unwrap();
    let request = service.write(spec(&file, 4096), &[0x42; 4096]).unwrap();
    assert!(matches!(
        request.wait(deadline()).unwrap().status,
        CompletionStatus::Complete
    ));
    service.stop();
    assert_eq!(
        service.activate().unwrap_err().kind(),
        io::ErrorKind::BrokenPipe
    );
    assert!(service.read(spec(&file, 4096)).is_err());
    service.shutdown().unwrap();
    service.shutdown().unwrap();
}

#[test]
fn activation_racing_stop_never_reopens_admission() {
    let directory = Directory::new();
    let file = directory.file();
    for _ in 0..32 {
        let service = Arc::new(Service::prepare(config()).unwrap());
        service
            .register_device(DeviceConfig {
                id: DEVICE,
                worker: backend(),
                native: None,
            })
            .unwrap();
        let ready = Arc::new(Barrier::new(2));
        let other = Arc::clone(&service);
        let gate = Arc::clone(&ready);
        let activate = thread::spawn(move || {
            gate.wait();
            other.activate()
        });
        ready.wait();
        service.stop();
        let _ = activate.join().unwrap();
        assert_eq!(
            service.activate().unwrap_err().kind(),
            io::ErrorKind::BrokenPipe
        );
        assert!(service.read(spec(&file, 4096)).is_err());
        service.shutdown().unwrap();
    }
}

#[test]
fn closed_endpoints_are_reaped_without_reviving_old_requests() {
    let directory = Directory::new();
    let file = directory.file();
    let mut config = config();
    config.devices.clear();
    let service = Service::start(config).unwrap();
    for _ in 0..16 {
        let id = service.allocate_device_id().unwrap();
        service.register_device_with_limits(
            DeviceConfig { id, worker: backend(), native: None },
            DeviceLimits { depth: 3 },
        ).unwrap();
        let old_open = service.shared.gate.lock().unwrap().devices[&id].clone();
        service.remove_device(id).unwrap();
        service.wait_device_drained(id).unwrap();
        service.driver.lock().unwrap().progress(None);
        assert!(!old_open.load(Ordering::Acquire));
        assert!(service.shared.gate.lock().unwrap().devices.is_empty());
        assert!(service.shared.worker_routes.lock().unwrap().is_empty());
        service.remove_device(id).unwrap();
        assert!(service.read(RequestSpec::new(id, target(&file), 4096)).is_err());
    }
    service.shutdown().unwrap();
}
