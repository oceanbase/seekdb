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

use super::engine::Stage;
use super::*;
use crate::{BlockConfig, SpaceBudget, UnmarkedBlockFile};
use file_io::{BackendKind, Retired, SubmissionId};
use std::fs::{self, OpenOptions};
use std::path::PathBuf;
use std::sync::Barrier;
mod health;
mod ownership;
mod registration;
mod resources;

const DEVICE: DeviceId = DeviceId(1);
const OTHER: DeviceId = DeviceId(2);
const GROUP: GroupId = GroupId(1);
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
        health: crate::HealthConfig::default(),
        admission: AdmissionLimits {
            max_records: 32,
            max_bytes: 8 * 1024 * 1024,
            probe_records: 4,
            probe_bytes: 2 * 1024 * 1024,
        },
        devices: [DEVICE, OTHER]
            .into_iter()
            .map(|id| {
                (
                    id,
                    DeviceLimits {
                        depth: 4,
                        probe_depth: 1,
                        max_probe_bytes: 2 * 1024 * 1024,
                    },
                )
            })
            .collect(),
        groups: [(GROUP, GroupLimits::default())].into(),
        default_group: GROUP,
        group_capacity_iops: Rate::Unlimited,
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
    RequestSpec::new(DEVICE, GROUP, target(file), length)
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
fn shared_read(service: &Service, spec: RequestSpec) -> (RequestId, mpsc::Receiver<Completion>) {
    let mut admission = prepare(service, spec, Direction::Read, &[]);
    let (destination, completed) = mpsc::channel();
    let id = admission.publish(destination).unwrap();
    (id, completed)
}
fn engine_with(config: Config) -> Engine {
    let (management, _) = mpsc::sync_channel(32);
    let (retirement, _) = mpsc::channel();
    let config = Arc::new(config);
    let wake = Arc::new(Wake::default());
    let metadata_ledger = Arc::new(Mutex::new(Ledger {
        limits: config.effective_admission(),
        usage: [ResourceUsage::default(); 2],
        health_payload_bytes: 0,
    }));
    let shared = Arc::new(Shared {
        health: Arc::new(
            HealthIngress::new(
                Arc::clone(&wake),
                Arc::clone(&metadata_ledger),
                config.health,
            )
            .unwrap(),
        ),
        calibration: Arc::new(crate::calibration::Calibration::default()),
        ledger: metadata_ledger,
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
    let mut engine = Engine::new(shared);
    engine
        .register(
            DeviceConfig {
                id: DEVICE,
                worker: backend(),
                native: None,
            },
            false,
        )
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
    engine.drain_gate();
    engine.grant_admission();
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
    let id = admission.publish(destination).unwrap();
    drop(admission);
    engine.drain_gate();
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
}
fn dispatch(engine: &mut Engine) {
    engine.dispatch(Instant::now());
}

#[test]
fn timeout_retains_physical_ownership_until_retirement() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine();
    let at = Instant::now() + Duration::from_secs(1);
    let mut request = spec(&file, 4);
    request.policy.deadline = Some(at);
    let (id, results) = enqueue(&mut engine, request, Direction::Write, b"late");
    dispatch(&mut engine);
    engine.controls(at);
    let completion = results.recv().unwrap();
    assert_eq!(completion.id, id);
    assert!(matches!(completion.status, CompletionStatus::TimedOut));
    assert!(completion.buffer().is_none());
    drop(completion);
    assert_eq!(engine.snapshot().awaiting_retirement, 1);
    assert_eq!(engine.snapshot().reserved_bytes, 4);
    retire(&mut engine);
    assert_eq!(engine.snapshot().reserved_bytes, 0);
    let mut bytes = [0; 4];
    file_io::read_exact_at(&file, &mut bytes, 0).unwrap();
    assert_eq!(&bytes, b"late");
    assert!(results.try_recv().is_err());
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
    let completion = receiver.recv_timeout(Duration::from_secs(5)).unwrap();
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
            initial_size: 8192,
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
                GROUP,
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
        GROUP,
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
    config.admission.max_bytes = config.admission.probe_bytes + 8;
    service.reconfigure(Generation(0), config).unwrap();
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
    engine.controls(at);
    engine.dispatch(at);
    let completion = results.recv().unwrap();
    assert!(matches!(completion.status, CompletionStatus::TimedOut));
    assert!(completion.buffer().is_none());
    assert_eq!(
        engine.cancel(id, CompletionStatus::Cancelled),
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
            initial_size: 8192,
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
        GROUP,
        Target::Block(owner.region(block, 0..4096).unwrap()),
        4096,
    );
    let (id, result) = enqueue(&mut engine, request, Direction::Write, &[0; 4096]);
    dispatch(&mut engine);
    assert_eq!(
        engine.cancel(id, CompletionStatus::Cancelled),
        CancelOutcome::Cancelled
    );
    drop(result.recv().unwrap());
    owner.release(block).unwrap();
    assert!(matches!(
        owner.allocate(),
        Err(crate::BlockError::Exhausted)
    ));
    assert_eq!(engine.snapshot().reserved_bytes, 4096);
    assert_eq!(engine.snapshot().devices[0].physical_depth, [1, 0]);
    retire(&mut engine);
    assert!(engine.active.is_empty());
    assert_eq!(engine.snapshot().reserved_bytes, 0);
    assert_eq!(owner.allocate().unwrap(), block);
    assert!(result.try_recv().is_err());
}
#[test]
fn lost_actor_does_not_drop_the_physical_region_or_memory_charge() {
    let directory = Directory::new();
    let owner = block_owner(&directory);
    let block = owner.allocate().unwrap();
    let mut engine = engine();
    let backend = engine.backend(DEVICE, 0);
    let ledger = Arc::clone(&engine.shared.ledger);
    let request = RequestSpec::new(
        DEVICE,
        GROUP,
        Target::Block(owner.region(block, 0..4096).unwrap()),
        4096,
    );
    let (_, _result) = enqueue(&mut engine, request, Direction::Write, &[0; 4096]);
    dispatch(&mut engine);
    drop(engine);
    owner.release(block).unwrap();
    assert!(matches!(
        owner.allocate(),
        Err(crate::BlockError::Exhausted)
    ));
    assert_eq!(ledger.lock().unwrap().usage[0].bytes, 4096);
    drop(backend.retire(deadline()).unwrap().unwrap());
    assert_eq!(ledger.lock().unwrap().usage[0].bytes, 0);
    assert_eq!(owner.allocate().unwrap(), block);
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
    assert_eq!(engine.snapshot().devices[0].physical_depth, [0, 0]);
    dispatch(&mut engine);
    let (id, mut r) = retired(&engine);
    r.result = Err(io::ErrorKind::WouldBlock.into());
    engine.retired(id, r);
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
fn dropping_public_service_still_delivers_logical_shutdown() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut waiter = service.read(spec(&file, 8)).unwrap();
    drop(service);
    let completion = waiter.wait(deadline()).unwrap();
    assert!(matches!(
        completion.status,
        CompletionStatus::Shutdown | CompletionStatus::ShortIo
    ));
}
#[test]
fn terminal_backend_failure_completes_submissions_already_in_the_mailbox() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine();
    let mut admission = core_prepare(&mut engine, spec(&file, 8), Direction::Read, &[]);
    let (destination, completed) = mpsc::channel();
    let id = admission.publish(destination).unwrap();
    drop(admission);
    engine.backend_failed(DEVICE, 0, io::ErrorKind::PermissionDenied.into());
    engine.drain_gate();
    let completion = completed.recv().unwrap();
    assert_eq!(completion.id, id);
    assert!(
        matches!(completion.status, CompletionStatus::IoError(ref e) if e.kind() == io::ErrorKind::PermissionDenied)
    );
    drop(completion);
    assert_eq!(engine.snapshot().reserved_bytes, 0);
    assert!(completed.try_recv().is_err());
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
    config.admission.max_records = config.admission.probe_records + 1;
    service.reconfigure(Generation(0), config).unwrap();
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
    assert!(admission
        .prepare_with(target, Direction::Read, &[], |_, _| Err(
            io::ErrorKind::OutOfMemory.into()
        ))
        .is_err());
    drop(admission);
    assert_eq!(service.snapshot().unwrap().admitted_requests, 0);
    let (options, target) = spec(&file, 8).split();
    let mut admission = service.begin(options).unwrap();
    admission.wait_capacity().unwrap();
    assert!(admission
        .prepare_with(target, Direction::Read, &[], |_, _| AlignedBuffer::zeroed(
            4, 8
        ))
        .is_err());
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
fn prepared_request_retains_charge_across_submission_and_shrink() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut admission = prepare(&service, spec(&file, 4096), Direction::Read, &[]);
    let mut config = config();
    config.admission.max_bytes = config.admission.probe_bytes + 1;
    let receipt = service.reconfigure(Generation(0), config).unwrap();
    assert_eq!(receipt.debt[0].bytes, 4095);
    let (destination, completed) = mpsc::channel();
    admission.publish(destination).unwrap();
    drop(admission);
    let completion = completed.recv_timeout(Duration::from_secs(5)).unwrap();
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
        admission.publish(destination).unwrap_err().kind(),
        io::ErrorKind::BrokenPipe
    );
    service.shutdown().unwrap();
    assert_eq!(ledger.lock().unwrap().usage[0].bytes, 8);
    assert!(completed.try_recv().is_err());
    drop(admission);
    assert_eq!(ledger.lock().unwrap().usage[0].bytes, 0);
}
