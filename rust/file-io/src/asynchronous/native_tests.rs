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
use crate::asynchronous::tests::{operation, test_shared, TestFile};

#[test]
fn malformed_events_cannot_remove_a_live_slot() {
    let file = TestFile::new();
    let shared = test_shared(1);
    let mut context = Context::new(1).unwrap();
    let id = SubmissionId(123);
    context
        .submit(id, operation(&file, Direction::Write, b"abc", 0), &shared)
        .unwrap();
    assert_eq!(context.kernel.pending.len(), 1);
    let pointer = &context.kernel.pending[&id].control as *const IoControl as u64;
    let valid = IoEvent {
        data: id.0,
        object: pointer,
        result: 3,
        result2: 0,
    };
    for bad in [
        IoEvent {
            data: id.0 + 1,
            ..valid
        },
        IoEvent {
            object: pointer + 1,
            ..valid
        },
        IoEvent { result: 4, ..valid },
        IoEvent {
            result: -4096,
            ..valid
        },
        IoEvent {
            result2: 1,
            ..valid
        },
    ] {
        assert_eq!(
            context.finish(bad, &shared).unwrap_err().kind(),
            io::ErrorKind::InvalidData
        );
        assert_eq!(context.kernel.pending.len(), 1);
        assert!(shared.lock().completed.is_empty());
        assert_eq!(Arc::strong_count(&file.file), 2);
    }
    drop(context);
    assert_eq!(Arc::strong_count(&file.file), 1);
    assert_eq!(std::fs::read(&file.path).unwrap(), b"abc");
}

#[test]
fn context_unwind_destroys_aio_before_freeing_pending_operations() {
    let file = TestFile::new();
    let shared = test_shared(1);
    let mut context = Context::new(1).unwrap();
    context
        .submit(
            SubmissionId(456),
            operation(&file, Direction::Write, b"survives unwind", 0),
            &shared,
        )
        .unwrap();
    assert_eq!(context.kernel.pending.len(), 1);
    assert_eq!(Arc::strong_count(&file.file), 2);
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(move || {
        let _context = context;
        panic!("injected failure after kernel acceptance");
    }));
    assert!(result.is_err());
    assert_eq!(Arc::strong_count(&file.file), 1);
    assert_eq!(std::fs::read(&file.path).unwrap(), b"survives unwind");
}

#[test]
fn failed_kernel_cancel_retains_pending_until_actual_event() {
    let file = TestFile::new();
    let shared = test_shared(1);
    let mut context = Context::new(1).unwrap();
    let id = SubmissionId(789);
    context
        .submit(id, operation(&file, Direction::Write, b"abc", 0), &shared)
        .unwrap();
    context.cancel(id, &shared).unwrap();
    if !context.kernel.pending.is_empty() {
        assert_eq!(Arc::strong_count(&file.file), 2);
        while !context.kernel.pending.is_empty() {
            context.reap(&shared).unwrap();
            if !context.kernel.pending.is_empty() {
                context.wake.wait().unwrap();
            }
        }
    }
    let mut state = shared.lock();
    assert_eq!(state.completed.len(), 1);
    let (returned, retired) = state.completed.pop_front().unwrap();
    assert_eq!(returned, id);
    assert_eq!(retired.operation.buffer().as_slice(), b"abc");
}

#[test]
fn syscall_retry_repeats_only_interrupted_attempts() {
    let mut attempts = 0;
    let result = syscall_retry(|| {
        attempts += 1;
        if attempts == 1 {
            // SAFETY: errno is thread-local storage owned by libc on this thread.
            unsafe {
                *libc::__errno_location() = libc::EINTR;
            }
            -1
        } else {
            1
        }
    })
    .unwrap();
    assert_eq!(result, 1);
    assert_eq!(attempts, 2);
    let result = syscall_retry(|| {
        attempts += 1;
        // SAFETY: errno is thread-local storage owned by libc on this thread.
        unsafe {
            *libc::__errno_location() = libc::EAGAIN;
        }
        -1
    })
    .unwrap_err();
    assert_eq!(result.raw_os_error(), Some(libc::EAGAIN));
    assert_eq!(attempts, 3);
}

#[test]
fn teardown_failure_keeps_retirement_source_alive_until_kernel_ownership_ends() {
    use crate::asynchronous::Backend;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::mpsc;
    use std::time::{Duration, Instant};

    let file = TestFile::new();
    let shared = test_shared(1);
    let mut context = Context::new(1).unwrap();
    let id = SubmissionId(901);
    context
        .submit(id, operation(&file, Direction::Write, b"lease", 0), &shared)
        .unwrap();
    let allow_destroy = Arc::new(AtomicBool::new(false));
    let gate = allow_destroy.clone();
    let (attempted, attempt) = mpsc::channel();
    context.kernel.before_destroy = Some(Box::new(move || {
        if gate.load(Ordering::Acquire) {
            return Ok(());
        }
        attempted.send(()).unwrap();
        Err(io::Error::from_raw_os_error(libc::EIO))
    }));
    let mut backend = Backend {
        shared,
        joins: Vec::new(),
        native: true,
    };
    backend
        .spawn(move |_| {
            let _context = context;
            panic!("injected panic with failed teardown");
        })
        .unwrap();
    attempt.recv_timeout(Duration::from_secs(5)).unwrap();
    assert_eq!(backend.shared.lock().workers, 1);
    assert_eq!(Arc::strong_count(&file.file), 2);
    assert!(backend.retire(Instant::now()).unwrap().is_none());
    allow_destroy.store(true, Ordering::Release);
    backend.joins[0].thread().unpark();
    assert!(backend
        .retire(Instant::now() + Duration::from_secs(5))
        .is_err());
    assert_eq!(Arc::strong_count(&file.file), 1);
    assert_eq!(std::fs::read(&file.path).unwrap(), b"lease");
    assert!(backend.shutdown().is_err());
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum Call {
    Setup,
    Submit,
    Events,
    Destroy,
}

#[derive(Clone, Copy, Debug)]
pub(crate) enum Fault {
    Error(i32),
    Count(libc::c_long),
}

#[derive(Clone, Copy)]
enum EventFault {
    Result(i64),
    Object,
    Identity,
    Result2,
}

#[derive(Default)]
struct Injection {
    calls: std::collections::VecDeque<(Call, Fault)>,
    event: Option<EventFault>,
    corrupt_read: bool,
}

thread_local! {
    static INJECTION: std::cell::RefCell<Injection> = std::cell::RefCell::default();
}

pub(crate) fn with_faults<T>(faults: &[(Call, Fault)], run: impl FnOnce() -> T) -> T {
    struct Reset;
    impl Drop for Reset {
        fn drop(&mut self) {
            INJECTION.with(|injection| *injection.borrow_mut() = Injection::default());
        }
    }
    INJECTION.with(|injection| injection.borrow_mut().calls.extend(faults));
    let _reset = Reset;
    let result = run();
    INJECTION.with(|injection| assert!(injection.borrow().calls.is_empty()));
    result
}

pub(super) fn inject(call: Call) -> Option<libc::c_long> {
    INJECTION.with(|injection| {
        let mut injection = injection.borrow_mut();
        if injection
            .calls
            .front()
            .is_none_or(|(next, _)| *next != call)
        {
            return None;
        }
        Some(match injection.calls.pop_front().unwrap().1 {
            Fault::Count(count) => count,
            Fault::Error(error) => {
                // SAFETY: errno is libc's thread-local storage on this Linux thread.
                unsafe {
                    *libc::__errno_location() = error;
                }
                -1
            }
        })
    })
}

pub(super) fn alter_events(events: &mut [IoEvent]) {
    let Some(event) = events.first_mut() else {
        return;
    };
    INJECTION.with(|injection| {
        if let Some(fault) = injection.borrow_mut().event.take() {
            match fault {
                EventFault::Result(result) => event.result = result,
                EventFault::Object => event.object += 1,
                EventFault::Identity => event.data += 1,
                EventFault::Result2 => event.result2 = 1,
            }
        }
    });
}

pub(super) fn alter_read(buffer: &mut AlignedBuffer) {
    INJECTION.with(|injection| {
        if std::mem::take(&mut injection.borrow_mut().corrupt_read) {
            buffer.as_mut_slice()[0] ^= 1;
        }
    });
}

fn native_probe(file: &TestFile) -> io::Result<bool> {
    probe(
        file.file.clone(),
        AlignedBuffer::zeroed(4096, 4096).unwrap(),
    )
}

#[test]
fn probe_setup_fallback_is_limited_to_enosys() {
    let file = TestFile::new();
    for error in [
        libc::ENOSYS,
        libc::EINVAL,
        libc::EOPNOTSUPP,
        libc::EAGAIN,
        libc::EPERM,
        libc::EACCES,
        libc::ENOSPC,
        libc::EIO,
    ] {
        let result = with_faults(&[(Call::Setup, Fault::Error(error))], || {
            native_probe(&file)
        });
        if error == libc::ENOSYS {
            assert!(!result.unwrap());
        } else {
            assert_eq!(result.unwrap_err().raw_os_error(), Some(error));
        }
        assert_eq!(Arc::strong_count(&file.file), 1);
    }
}

#[test]
fn probe_submission_and_retrieval_preserve_stage_errors() {
    let file = TestFile::new();
    for call in [Call::Submit, Call::Events] {
        for error in [
            libc::EINVAL,
            libc::EOPNOTSUPP,
            libc::ENOSYS,
            libc::EAGAIN,
            libc::EPERM,
            libc::EACCES,
            libc::ENOSPC,
            libc::EIO,
        ] {
            let result = with_faults(&[(call, Fault::Error(error))], || native_probe(&file));
            if [libc::EINVAL, libc::EOPNOTSUPP, libc::ENOSYS].contains(&error) {
                assert!(!result.unwrap());
            } else {
                assert_eq!(result.unwrap_err().raw_os_error(), Some(error));
            }
            assert_eq!(Arc::strong_count(&file.file), 1);
        }
    }
}

#[test]
fn probe_checks_submission_event_and_byte_counts() {
    let file = TestFile::new();
    for (call, count, expected) in [
        (Call::Submit, 0, libc::EAGAIN),
        (Call::Submit, 2, libc::EIO),
        (Call::Events, 0, libc::EIO),
        (Call::Events, 2, libc::EIO),
    ] {
        let error =
            with_faults(&[(call, Fault::Count(count))], || native_probe(&file)).unwrap_err();
        assert_eq!(error.raw_os_error(), Some(expected));
        assert_eq!(Arc::strong_count(&file.file), 1);
    }
    for fault in [
        EventFault::Result(0),
        EventFault::Result(4095),
        EventFault::Result(4097),
        EventFault::Result(-4096),
        EventFault::Object,
        EventFault::Identity,
        EventFault::Result2,
    ] {
        INJECTION.with(|injection| injection.borrow_mut().event = Some(fault));
        assert_eq!(
            native_probe(&file).unwrap_err().raw_os_error(),
            Some(libc::EIO)
        );
        assert_eq!(Arc::strong_count(&file.file), 1);
    }
    INJECTION.with(|injection| injection.borrow_mut().corrupt_read = true);
    assert_eq!(
        native_probe(&file).unwrap_err().raw_os_error(),
        Some(libc::EIO)
    );
}

#[test]
fn probe_valid_negative_completion_can_fallback() {
    let file = TestFile::new();
    for error in [
        libc::EINVAL,
        libc::EOPNOTSUPP,
        libc::ENOSYS,
        libc::EAGAIN,
        libc::EACCES,
        libc::ENOSPC,
        libc::EIO,
    ] {
        INJECTION.with(|injection| {
            injection.borrow_mut().event = Some(EventFault::Result(-(error as i64)))
        });
        let result = native_probe(&file);
        if [libc::EINVAL, libc::EOPNOTSUPP, libc::ENOSYS].contains(&error) {
            assert!(!result.unwrap());
        } else {
            assert_eq!(result.unwrap_err().raw_os_error(), Some(error));
        }
    }
}

#[test]
fn probe_interrupted_submission_and_retrieval_complete_actual_round_trip() {
    let file = TestFile::new();
    assert!(with_faults(
        &[
            (Call::Submit, Fault::Error(libc::EINTR)),
            (Call::Events, Fault::Error(libc::EINTR)),
            (Call::Submit, Fault::Error(libc::EINTR)),
            (Call::Events, Fault::Error(libc::EINTR)),
        ],
        || native_probe(&file)
    )
    .unwrap());
    assert_eq!(std::fs::read(&file.path).unwrap(), vec![0x36; 4096]);
}

#[test]
fn probe_destroy_error_overrides_success_and_fallback_but_not_fatal_transfer() {
    let file = TestFile::new();
    for transfer in [None, Some(libc::EOPNOTSUPP), Some(libc::ENOSPC)] {
        let mut faults = Vec::new();
        if let Some(error) = transfer {
            faults.push((Call::Events, Fault::Error(error)));
        }
        faults.push((Call::Destroy, Fault::Error(libc::EIO)));
        let error = with_faults(&faults, || native_probe(&file)).unwrap_err();
        assert_eq!(
            error.raw_os_error(),
            Some(if transfer == Some(libc::ENOSPC) {
                libc::ENOSPC
            } else {
                libc::EIO
            })
        );
        assert_eq!(Arc::strong_count(&file.file), 1);
    }
}

#[test]
fn probe_waits_for_failed_destruction_before_returning_accepted_resources() {
    use std::sync::mpsc;
    use std::time::Duration;
    let file = TestFile::new();
    let owned = file.file.clone();
    let (attempted, attempts) = mpsc::channel();
    let (allow_destroy, allowed) = mpsc::channel();
    let (returned, returns) = mpsc::channel();
    let thread = std::thread::spawn(move || {
        let mut kernel = KernelContext::new(1).unwrap();
        kernel.setup(1).unwrap();
        let mut first = true;
        kernel.before_destroy = Some(Box::new(move || {
            if std::mem::take(&mut first) {
                attempted.send(()).unwrap();
                Err(io::Error::from_raw_os_error(libc::EIO))
            } else {
                allowed.recv().unwrap();
                Ok(())
            }
        }));
        let result = with_faults(&[(Call::Events, Fault::Error(libc::EOPNOTSUPP))], || {
            probe_with_context(kernel, owned, AlignedBuffer::zeroed(4096, 4096).unwrap())
        });
        returned.send(result).unwrap();
    });
    attempts.recv_timeout(Duration::from_secs(5)).unwrap();
    assert!(returns.try_recv().is_err());
    assert_eq!(Arc::strong_count(&file.file), 2);
    allow_destroy.send(()).unwrap();
    let error = returns
        .recv_timeout(Duration::from_secs(5))
        .unwrap()
        .unwrap_err();
    assert_eq!(error.raw_os_error(), Some(libc::EIO));
    thread.join().unwrap();
    assert_eq!(Arc::strong_count(&file.file), 1);
    assert_eq!(std::fs::read(&file.path).unwrap(), vec![0x36; 4096]);
}
