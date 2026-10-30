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
use std::fs::OpenOptions;
use std::path::PathBuf;
use std::sync::mpsc;
use std::time::Duration;

static FILE_ID: AtomicU64 = AtomicU64::new(1);

pub(super) struct TestFile {
    pub(super) path: PathBuf,
    pub(super) file: Arc<File>,
}
impl TestFile {
    pub(super) fn new() -> Self {
        let path = std::env::temp_dir().join(format!(
            "file-io-aio-{}-{}",
            std::process::id(),
            FILE_ID.fetch_add(1, Ordering::Relaxed)
        ));
        let file = Arc::new(
            OpenOptions::new()
                .create_new(true)
                .read(true)
                .write(true)
                .open(&path)
                .unwrap(),
        );
        Self { path, file }
    }
}
impl Drop for TestFile {
    fn drop(&mut self) {
        let _ = std::fs::remove_file(&self.path);
    }
}

pub(super) fn operation(
    file: &TestFile,
    direction: Direction,
    bytes: &[u8],
    offset: u64,
) -> Operation {
    let mut buffer = AlignedBuffer::zeroed(bytes.len(), 4096).unwrap();
    buffer.as_mut_slice().copy_from_slice(bytes);
    Operation::new(file.file.clone().into(), buffer, offset, direction).unwrap()
}
fn retired(backend: &Backend) -> (SubmissionId, Retired) {
    backend
        .retire(Instant::now() + Duration::from_secs(5))
        .unwrap()
        .expect("physical retirement before deadline")
}

#[test]
fn operation_advances_without_losing_prefix_or_alignment() {
    let file = TestFile::new();
    let mut op = operation(&file, Direction::Write, b"abcdefgh", 10);
    let ptr = op.buffer().as_slice().as_ptr();
    op.buffer_mut().as_mut_slice()[0] = b'A';
    let op = op.advance(3).unwrap();
    assert_eq!(op.direction(), Direction::Write);
    assert_eq!(op.offset(), 13);
    assert_eq!(op.remaining_len(), 5);
    assert_eq!(op.buffer().as_slice(), b"Abcdefgh");
    assert_eq!(op.buffer().as_slice().as_ptr(), ptr);
    assert!(op.advance(6).is_err());
    assert!(Operation::read(
        file.file.clone(),
        AlignedBuffer::zeroed(1, 8).unwrap(),
        i64::MAX as u64
    )
    .is_err());
}

fn roundtrip(kind: BackendKind) {
    let file = TestFile::new();
    let backend = Backend::new(kind, 2).unwrap();
    let write = backend
        .submit(operation(&file, Direction::Write, b"hello", 7))
        .unwrap();
    let (id, done) = retired(&backend);
    assert_eq!(id, write);
    assert_eq!(done.result.unwrap(), 5);
    assert_eq!(done.operation.buffer().as_slice(), b"hello");
    let read = backend
        .submit(operation(&file, Direction::Read, &[0; 8], 7))
        .unwrap();
    let (id, done) = retired(&backend);
    assert_eq!(id, read);
    assert_eq!(done.result.unwrap(), 5);
    assert_eq!(done.operation.buffer().as_slice(), b"hello\0\0\0");
    let op = done.operation.advance(5).unwrap();
    backend.submit(op).unwrap();
    let (_, done) = retired(&backend);
    assert_eq!(done.result.unwrap(), 0);
    assert_eq!(done.operation.buffer().as_slice(), b"hello\0\0\0");
    backend.shutdown().unwrap();
}

#[test]
fn worker_roundtrip_and_short_eof() {
    roundtrip(BackendKind::Worker { threads: 2 });
}

#[test]
fn worker_fault_suppresses_the_syscall_and_toggle_preserves_error() {
    struct RestoreDelay;
    impl Drop for RestoreDelay {
        fn drop(&mut self) {
            SKIP_WORKER_DELAY.store(false, Ordering::Relaxed);
        }
    }
    let _restore = RestoreDelay;
    let file = TestFile::new();
    let backend = Backend::new(BackendKind::Worker { threads: 2 }, 2).unwrap();
    let fault = WorkerFault::new(Duration::ZERO, true);
    backend
        .submit(operation(&file, Direction::Write, b"bad", 0).with_worker_fault(fault))
        .unwrap();
    let (_, failed) = retired(&backend);
    assert_eq!(failed.result.unwrap_err().raw_os_error(), None);
    assert_eq!(file.file.metadata().unwrap().len(), 0);
    toggle_worker_fault_delay();
    assert!(SKIP_WORKER_DELAY.load(Ordering::Relaxed));
    for fail in [false, true] {
        backend
            .submit(
                operation(&file, Direction::Write, b"ok", 0)
                    .with_worker_fault(WorkerFault::new(Duration::from_secs(60), fail)),
            )
            .unwrap();
    }
    let results = [retired(&backend).1.result, retired(&backend).1.result];
    assert_eq!(results.iter().filter(|result| result.is_ok()).count(), 1);
    assert_eq!(file.file.metadata().unwrap().len(), 2);
    backend.shutdown().unwrap();
    toggle_worker_fault_delay();
    assert!(!SKIP_WORKER_DELAY.load(Ordering::Relaxed));
}

#[cfg(target_os = "linux")]
#[test]
fn native_backend_rejects_worker_fault_without_taking_ownership() {
    let file = TestFile::new();
    let backend = Backend::new(BackendKind::NativeAio, 1).unwrap();
    let operation = operation(&file, Direction::Write, b"bad", 0)
        .with_worker_fault(WorkerFault::new(Duration::ZERO, true));
    let rejected = backend.submit(operation).unwrap_err();
    assert_eq!(rejected.error.kind(), io::ErrorKind::InvalidInput);
    assert_eq!(rejected.operation.buffer().as_slice(), b"bad");
    assert_eq!(file.file.metadata().unwrap().len(), 0);
    backend.shutdown().unwrap();
}

#[cfg(target_os = "linux")]
#[test]
fn native_roundtrip_and_short_eof() {
    roundtrip(BackendKind::NativeAio);
}

fn read_only_write_error(kind: BackendKind) {
    let file = TestFile::new();
    let read_only = Arc::new(File::open(&file.path).unwrap());
    let buffer = AlignedBuffer::zeroed(4096, 4096).unwrap();
    let pointer = buffer.as_slice().as_ptr();
    let backend = Backend::new(kind, 1).unwrap();
    backend
        .submit(Operation::write(read_only, buffer, 0).unwrap())
        .unwrap();
    let (_, done) = retired(&backend);
    assert!(done.result.is_err());
    #[cfg(unix)]
    assert_eq!(done.result.unwrap_err().raw_os_error(), Some(libc::EBADF));
    assert_eq!(done.operation.buffer().as_slice().as_ptr(), pointer);
    backend.shutdown().unwrap();
}

#[test]
fn worker_preserves_operation_on_syscall_error() {
    read_only_write_error(BackendKind::Worker { threads: 1 });
}

#[cfg(target_os = "linux")]
#[test]
fn native_preserves_operation_on_submission_error() {
    read_only_write_error(BackendKind::NativeAio);
}

#[test]
fn completion_queue_holds_admission_until_consumed() {
    let file = TestFile::new();
    let backend = Backend::new(BackendKind::Worker { threads: 1 }, 1).unwrap();
    backend
        .submit(operation(&file, Direction::Write, b"a", 0))
        .unwrap();
    {
        let mut state = backend.shared.lock();
        while state.completed.is_empty() {
            state = backend
                .shared
                .completion_available
                .wait_timeout(state, Duration::from_secs(5))
                .unwrap()
                .0;
            assert!(state.workers > 0);
        }
    }
    let rejected = backend
        .submit(operation(&file, Direction::Write, b"b", 1))
        .unwrap_err();
    assert_eq!(rejected.error.kind(), io::ErrorKind::WouldBlock);
    assert_eq!(rejected.operation.buffer().as_slice(), b"b");
    let (_, completed) = retired(&backend);
    backend.submit(rejected.operation).unwrap();
    assert_eq!(retired(&backend).1.result.unwrap(), 1);
    assert_eq!(completed.operation.buffer().as_slice(), b"a");
    backend.shutdown().unwrap();
}

pub(super) fn test_shared(depth: usize) -> Arc<Shared> {
    Arc::new(Shared {
        depth,
        state: Mutex::new(State {
            accepting: true,
            discard: false,
            workers: 0,
            failure: None,
            admitted: HashSet::with_capacity(depth),
            work: VecDeque::with_capacity(depth),
            cancel: HashSet::with_capacity(depth),
            completed: VecDeque::with_capacity(depth),
        }),
        work_available: Condvar::new(),
        completion_available: Condvar::new(),
        #[cfg(target_os = "linux")]
        wake: None,
    })
}

fn gated_backend(
    depth: usize,
    threads: usize,
    entered: mpsc::Sender<()>,
    gate: Arc<(Mutex<bool>, Condvar)>,
) -> Backend {
    let shared = test_shared(depth);
    let mut backend = Backend {
        shared,
        joins: Vec::new(),
        native: false,
    };
    for _ in 0..threads {
        let entered = entered.clone();
        let gate = gate.clone();
        backend
            .spawn(move |shared| {
                worker(shared, move |operation| {
                    entered.send(()).unwrap();
                    let (mutex, changed) = &*gate;
                    let mut open = mutex.lock().unwrap();
                    while !*open {
                        open = changed.wait(open).unwrap();
                    }
                    operation.execute()
                })
            })
            .unwrap();
    }
    backend
}

#[test]
fn workers_execute_concurrently_and_bound_inflight_operations() {
    let file = TestFile::new();
    let (send, receive) = mpsc::channel();
    let gate = Arc::new((Mutex::new(false), Condvar::new()));
    let backend = gated_backend(2, 2, send, gate.clone());
    for offset in 0..2 {
        backend
            .submit(operation(&file, Direction::Write, b"x", offset))
            .unwrap();
    }
    for _ in 0..2 {
        receive.recv_timeout(Duration::from_secs(5)).unwrap();
    }
    assert_eq!(
        backend
            .submit(operation(&file, Direction::Read, &[0], 0))
            .unwrap_err()
            .error
            .kind(),
        io::ErrorKind::WouldBlock
    );
    assert!(backend.retire(Instant::now()).unwrap().is_none());
    *gate.0.lock().unwrap() = true;
    gate.1.notify_all();
    for _ in 0..2 {
        assert_eq!(retired(&backend).1.result.unwrap(), 1);
    }
    backend.shutdown().unwrap();
}

#[test]
fn worker_and_retirement_waiters_receive_notifications_and_stop() {
    let file = TestFile::new();
    let (entered_tx, entered_rx) = mpsc::channel();
    let execute_gate = Arc::new((Mutex::new(false), Condvar::new()));
    let backend = Arc::new(gated_backend(2, 2, entered_tx, execute_gate.clone()));
    let (started_tx, started_rx) = mpsc::channel();
    let (result_tx, result_rx) = mpsc::channel();
    let mut consumers = Vec::new();
    for _ in 0..3 {
        let backend = Arc::clone(&backend);
        let started = started_tx.clone();
        let result = result_tx.clone();
        consumers.push(thread::spawn(move || {
            started.send(()).unwrap();
            let retired = backend.retire(Instant::now() + Duration::from_secs(10));
            result
                .send(retired.map(|value| value.map(|(id, retired)| (id, retired.result))))
                .unwrap();
        }));
    }
    for _ in 0..3 {
        started_rx.recv_timeout(Duration::from_secs(2)).unwrap();
    }
    // Give the consumer threads an opportunity to enter retire before work completes.
    thread::sleep(Duration::from_millis(100));
    let first = backend
        .submit(operation(&file, Direction::Write, b"x", 0))
        .unwrap();
    let second = backend
        .submit(operation(&file, Direction::Write, b"y", 1))
        .unwrap();
    let workers_started = (0..2).all(|_| entered_rx.recv_timeout(Duration::from_secs(2)).is_ok());
    *execute_gate.0.lock().unwrap() = true;
    execute_gate.1.notify_all();
    let first_result = result_rx.recv_timeout(Duration::from_secs(2));
    let second_result = result_rx.recv_timeout(Duration::from_secs(2));
    backend.stop();
    let stopped = result_rx.recv_timeout(Duration::from_secs(2));
    for consumer in consumers {
        consumer.join().unwrap();
    }
    let remaining = result_rx.try_iter().collect::<Vec<_>>();
    Arc::try_unwrap(backend).ok().unwrap().shutdown().unwrap();
    assert!(workers_started, "both workers should receive work promptly");
    let completions = [first_result, second_result].map(|value| {
        let (id, result) = value
            .expect("completion notification watchdog")
            .unwrap()
            .unwrap();
        assert_eq!(result.unwrap(), 1);
        id
    });
    assert!(completions.contains(&first));
    assert!(completions.contains(&second));
    assert!(matches!(stopped.expect("stop notification watchdog"), Ok(None)));
    assert!(remaining.is_empty());
}

#[test]
fn dropped_backend_keeps_pending_file_and_buffer_until_worker_finishes() {
    let file = TestFile::new();
    let weak_file = Arc::downgrade(&file.file);
    let path = file.path.clone();
    let reader = File::open(&path).unwrap();
    let (send, receive) = mpsc::channel();
    let gate = Arc::new((Mutex::new(false), Condvar::new()));
    let backend = gated_backend(1, 1, send, gate.clone());
    let shared = backend.shared.clone();
    backend
        .submit(operation(&file, Direction::Write, b"owned after drop", 0))
        .unwrap();
    receive.recv_timeout(Duration::from_secs(5)).unwrap();
    drop(backend);
    assert_eq!(Arc::strong_count(&file.file), 2);
    drop(file);
    assert!(weak_file.upgrade().is_some());
    assert_eq!(reader.metadata().unwrap().len(), 0);
    *gate.0.lock().unwrap() = true;
    gate.1.notify_all();
    let mut state = shared.lock();
    let deadline = Instant::now() + Duration::from_secs(5);
    while state.workers > 0 {
        let wait = deadline
            .checked_duration_since(Instant::now())
            .expect("worker retired after backend drop");
        state = shared.completion_available.wait_timeout(state, wait).unwrap().0;
    }
    drop(state);
    assert!(weak_file.upgrade().is_none());
    let mut bytes = [0; 16];
    crate::read_exact_at(&reader, &mut bytes, 0).unwrap();
    assert_eq!(&bytes, b"owned after drop");
    drop(reader);
    let _ = std::fs::remove_file(path);
}

#[test]
fn worker_cancellation_is_explicitly_unsupported() {
    let file = TestFile::new();
    let backend = Backend::new(BackendKind::Worker { threads: 1 }, 1).unwrap();
    let id = backend
        .submit(operation(&file, Direction::Read, &[0], 0))
        .unwrap();
    assert_eq!(
        backend.request_cancel(id).unwrap_err().kind(),
        io::ErrorKind::Unsupported
    );
    assert_eq!(retired(&backend).1.result.unwrap(), 0);
    backend.shutdown().unwrap();
}

#[cfg(target_os = "linux")]
#[test]
fn stale_or_foreign_native_cancel_never_targets_a_new_operation() {
    let file = TestFile::new();
    let first = Backend::new(BackendKind::NativeAio, 1).unwrap();
    let stale = first
        .submit(operation(&file, Direction::Write, b"a", 0))
        .unwrap();
    first.request_cancel(stale).unwrap();
    let _ = retired(&first);
    assert_eq!(
        first.request_cancel(stale).unwrap_err().kind(),
        io::ErrorKind::NotFound
    );
    first.shutdown().unwrap();
    let second = Backend::new(BackendKind::NativeAio, 1).unwrap();
    let live = second
        .submit(operation(&file, Direction::Write, b"b", 1))
        .unwrap();
    assert_ne!(stale, live);
    assert_eq!(
        second.request_cancel(stale).unwrap_err().kind(),
        io::ErrorKind::NotFound
    );
    assert_eq!(retired(&second).1.result.unwrap(), 1);
    second.shutdown().unwrap();
}

#[test]
fn shutdown_drains_work_without_requiring_completion_consumption() {
    let file = TestFile::new();
    let backend = Backend::new(BackendKind::Worker { threads: 2 }, 8).unwrap();
    for offset in 0..8 {
        backend
            .submit(operation(&file, Direction::Write, b"x", offset))
            .unwrap();
    }
    backend.shutdown().unwrap();
    assert_eq!(std::fs::read(&file.path).unwrap(), b"xxxxxxxx");
}

#[cfg(target_os = "linux")]
#[test]
fn dropped_native_backend_drains_all_accepted_writes() {
    let file = TestFile::new();
    let backend = Backend::new(BackendKind::NativeAio, 32).unwrap();
    let shared = backend.shared.clone();
    for offset in 0..32 {
        backend
            .submit(operation(&file, Direction::Write, b"x", offset))
            .unwrap();
    }
    drop(backend);
    let deadline = Instant::now() + Duration::from_secs(5);
    let mut state = shared.lock();
    while state.workers != 0 {
        let wait = deadline
            .checked_duration_since(Instant::now())
            .expect("native backend retired after drop");
        state = shared.completion_available.wait_timeout(state, wait).unwrap().0;
    }
    assert!(state.failure.is_none(), "{:?}", state.failure);
    drop(state);
    assert_eq!(Arc::strong_count(&file.file), 1);
    assert_eq!(std::fs::read(&file.path).unwrap(), vec![b'x'; 32]);
}

#[cfg(target_os = "linux")]
#[test]
fn native_direct_io_roundtrip_owns_aligned_buffers() {
    let file = TestFile::new();
    crate::enable_direct_io(&file.file).unwrap();
    let backend = Backend::new(BackendKind::NativeAio, 4).unwrap();
    for page in 0..4 {
        backend
            .submit(operation(
                &file,
                Direction::Write,
                &vec![page as u8 + 1; 4096],
                page * 4096,
            ))
            .unwrap();
    }
    for _ in 0..4 {
        assert_eq!(retired(&backend).1.result.unwrap(), 4096);
    }
    for page in 0..4 {
        backend
            .submit(operation(&file, Direction::Read, &[0; 4096], page * 4096))
            .unwrap();
    }
    for _ in 0..4 {
        let (_, done) = retired(&backend);
        assert_eq!(done.result.unwrap(), 4096);
        let expected = (done.operation.offset() / 4096) as u8 + 1;
        assert!(done
            .operation
            .buffer()
            .as_slice()
            .iter()
            .all(|byte| *byte == expected));
    }
    backend.shutdown().unwrap();
}

#[cfg(not(target_os = "linux"))]
#[test]
fn native_aio_is_explicitly_unsupported_off_linux() {
    assert!(
        matches!(Backend::new(BackendKind::NativeAio, 1), Err(error) if error.kind() == io::ErrorKind::Unsupported)
    );
}
