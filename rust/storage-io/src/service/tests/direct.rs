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

fn delayed(service: &Service, file: &Arc<File>, deadline: Option<Instant>) -> Request {
    delayed_with_duration(service, file, deadline, Duration::from_millis(30))
}

fn delayed_with_duration(
    service: &Service,
    file: &Arc<File>,
    deadline: Option<Instant>,
    delay: Duration,
) -> Request {
    let mut request = spec(file, 4096);
    request.policy.deadline = deadline;
    publish_delayed_write(service, request, delay)
}

fn publish_delayed_write(service: &Service, request: RequestSpec, delay: Duration) -> Request {
    let mut admission = prepare(service, request, Direction::Write, &[0x61; 4096]);
    if let admission::Preparation::Prepared(operation) =
        std::mem::replace(&mut admission.state, admission::Preparation::Reserved)
    {
        admission.state = admission::Preparation::Prepared(
            operation.with_worker_fault(file_io::WorkerFault::new(delay, false)),
        );
    } else {
        unreachable!()
    }
    let lifetime = admission.lifetime();
    service.publish_request(admission, lifetime, None).unwrap()
}
#[test]
fn publish_submits_without_a_progress_thread() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut waiter = service.write(spec(&file, 4096), &[0x71; 4096]).unwrap();
    let state = service.snapshot().unwrap();
    assert_eq!(state.awaiting_retirement, 1);
    assert_eq!(state.queued_requests, 0);
    let completion = waiter.wait(deadline()).unwrap();
    assert!(matches!(completion.status, CompletionStatus::Complete));
    assert_eq!(completion.data().unwrap(), &[0x71; 4096]);
    service.shutdown().unwrap();
}
#[test]
fn a_wait_slice_does_not_cancel_an_accepted_write() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut waiter = delayed(&service, &file, None);
    assert!(
        waiter
            .wait_until(Instant::now() + Duration::from_millis(1))
            .unwrap()
            .is_none()
    );
    let completion = waiter.wait(deadline()).unwrap();
    assert!(matches!(completion.status, CompletionStatus::Complete));
    assert_eq!(
        std::fs::read(directory.0.join("file")).unwrap(),
        [0x61; 4096]
    );
    service.shutdown().unwrap();
}
#[test]
fn deadline_expiry_does_not_release_running_buffer() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let until = Instant::now() + Duration::from_millis(100);
    let mut waiter =
        delayed_with_duration(&service, &file, Some(until), Duration::from_millis(500));
    assert!(matches!(
        waiter.wait_until(until).unwrap().unwrap().status,
        CompletionStatus::TimedOut
    ));
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 4096);
    let drained_by = deadline();
    while service.snapshot().unwrap().awaiting_retirement != 0 {
        service.driver.lock().unwrap().progress(None);
        assert!(
            Instant::now() < drained_by,
            "timed out request did not physically retire"
        );
        thread::sleep(Duration::from_millis(1));
    }
    let completion = waiter.wait(deadline()).unwrap();
    assert!(matches!(completion.status, CompletionStatus::TimedOut));
    drop(completion);
    drop(waiter);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 0);
    service.shutdown().unwrap();
}

#[test]
fn start_deadline_preserves_a_running_writes_result() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    service.start_progress_worker().unwrap();
    let cutoff = Instant::now() + Duration::from_millis(200);
    let mut request = spec(&file, 4096);
    request.policy.deadline = Some(cutoff);
    request.policy.deadline_before_start = true;
    let waiter = publish_delayed_write(&service, request, Duration::from_millis(600));

    assert!(waiter.wait_until(cutoff).unwrap().is_none());
    let completion = waiter.wait(deadline()).unwrap();
    assert!(matches!(completion.status, CompletionStatus::Complete));
    assert_eq!(fs::read(directory.0.join("file")).unwrap(), [0x61; 4096]);
    service.shutdown().unwrap();
}

#[test]
fn start_deadline_expires_a_queued_write_without_touching_the_file() {
    // With one backend slot the request waits in the driver. With two it can wait in Tokio.
    for depth in [1, 2] {
        let directory = Directory::new();
        let file = directory.file();
        let service = Service::start(config()).unwrap();
        service
            .register_device(DeviceConfig {
                id: DEVICE,
                worker: Backend::new(BackendKind::Worker { threads: 1 }, depth).unwrap(),
                native: None,
            })
            .unwrap();
        service.start_progress_worker().unwrap();
        let running = delayed_with_duration(&service, &file, None, Duration::from_millis(600));
        let cutoff = Instant::now() + Duration::from_millis(100);
        let mut request = spec(&file, 4096);
        request.policy.deadline = Some(cutoff);
        request.policy.deadline_before_start = true;
        let queued = service.write(request, &[0x71; 4096]).unwrap();

        let completion = queued.wait(cutoff + Duration::from_millis(200)).unwrap();
        assert!(matches!(completion.status, CompletionStatus::TimedOut));
        let completion = running.wait(deadline()).unwrap();
        assert!(matches!(completion.status, CompletionStatus::Complete));
        service.shutdown().unwrap();
        assert_eq!(fs::read(directory.0.join("file")).unwrap(), [0x61; 4096]);
    }
}
#[test]
fn dropping_the_last_waiter_cannot_free_an_operation_early() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine();
    let mut admission = core_prepare(
        &mut engine,
        spec(&file, 4096),
        Direction::Write,
        &[0x61; 4096],
    );
    let lifetime = admission.lifetime();
    let waiter = Request::accepted(&mut admission, lifetime, None, Instant::now()).unwrap();
    let publisher = waiter.publisher(Arc::clone(&engine.shared.executor));
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
        request: Some(publisher),
    });
    drop(admission);
    dispatch(&mut engine);
    let (submission, pending_retirement) = retired(&engine);
    drop(waiter);
    engine.progress(None);
    let snapshot = engine.snapshot();
    assert_eq!(snapshot.admitted_requests, 1);
    assert_eq!(snapshot.reserved_bytes, 4096);
    assert_eq!(snapshot.awaiting_retirement, 1);
    engine.retired(submission, pending_retirement);
    engine.flush_test();
    assert_eq!(engine.snapshot().admitted_requests, 0);
    assert_eq!(engine.snapshot().reserved_bytes, 0);
}
#[test]
fn close_rejects_new_work_and_allows_accepted_work_to_retire() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut waiter = delayed(&service, &file, None);
    service.remove_device(DEVICE).unwrap();
    assert!(service.read(spec(&file, 4096)).is_err());
    let completion = waiter.wait(deadline()).unwrap();
    assert!(matches!(completion.status, CompletionStatus::Shutdown));
    drop((completion, waiter));
    service.shutdown().unwrap();
}
#[test]
fn two_devices_keep_independent_completion_addresses() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    service
        .register_device(DeviceConfig {
            id: OTHER,
            worker: backend(),
            native: None,
        })
        .unwrap();
    let mut other = spec(&file, 4096);
    other.device = OTHER;
    other.tag = 42;
    let mut first = service.write(spec(&file, 4096), &[0x71; 4096]).unwrap();
    let mut second = service.read(other).unwrap();
    let b = second.wait(deadline()).unwrap();
    let a = first.wait(deadline()).unwrap();
    assert_eq!(b.tag, 42);
    assert_ne!(a.id, b.id);
    service.shutdown().unwrap();
}
#[test]
fn retained_completion_keeps_memory_bounded_after_physical_depth_returns() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let completion = service
        .write(spec(&file, 4096), &[0x71; 4096])
        .unwrap()
        .wait(deadline())
        .unwrap();
    let state = service.snapshot().unwrap();
    assert_eq!(state.reserved_bytes, 4096);
    assert_eq!(state.awaiting_retirement, 0);
    drop(completion);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 0);
    service.shutdown().unwrap();
}
#[test]
fn shrinking_memory_preserves_owned_data_until_release() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let completion = service
        .write(spec(&file, 8192), &[0x71; 8192])
        .unwrap()
        .wait(deadline())
        .unwrap();
    let mut replacement = config();
    replacement.admission.max_bytes = 4096;
    service.reconfigure(replacement).unwrap();
    assert_eq!(service.snapshot().unwrap().debt.bytes, 4096);
    assert_eq!(completion.data().unwrap(), &[0x71; 8192]);
    drop(completion);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 0);
    service.shutdown().unwrap();
}
#[test]
fn capacity_waiter_cancellation_needs_no_actor() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut replacement = config();
    replacement.admission.max_bytes = 4096;
    service.reconfigure(replacement).unwrap();
    let held = prepare(&service, spec(&file, 4096), Direction::Read, &[]);
    let mut waiting = service.begin(spec(&file, 4096).split().0).unwrap();
    let control = waiting.take_control().unwrap();
    let worker = thread::spawn(move || waiting.wait_capacity());
    control.request_cancel();
    assert_eq!(
        worker.join().unwrap().unwrap_err().kind(),
        io::ErrorKind::Interrupted
    );
    drop((control, held));
    service.shutdown().unwrap();
}
#[test]
fn stop_and_publish_choose_one_acceptance_contract() {
    for stop_first in [false, true] {
        let directory = Directory::new();
        let file = directory.file();
        let service = service();
        let mut prepared = prepare(&service, spec(&file, 4096), Direction::Write, &[7; 4096]);
        let (sender, receiver) = mpsc::channel();
        if stop_first {
            service.stop();
        }
        let result = prepared.publish_channel(sender);
        if stop_first {
            assert!(result.is_err());
            assert!(receiver.try_recv().is_err());
        } else {
            assert!(result.is_ok());
            service.stop();
        }
        drop(prepared);
        service.shutdown().unwrap();
        if !stop_first {
            assert!(receiver.try_recv().is_ok());
            assert!(receiver.try_recv().is_err());
        }
    }
}
#[test]
fn invalid_registration_does_not_change_memory_or_devices() {
    let service = service();
    let before = service.snapshot().unwrap();
    assert!(
        service
            .register_device(DeviceConfig {
                id: DEVICE,
                worker: backend(),
                native: None
            })
            .is_err()
    );
    assert_eq!(service.snapshot().unwrap(), before);
    service.shutdown().unwrap();
}

#[test]
fn queued_cancel_returns_capacity_while_another_request_cannot_retire() {
    let directory = Directory::new();
    let file = directory.file();
    let mut cfg = config();
    cfg.admission.max_bytes = 8192;
    cfg.devices.get_mut(&DEVICE).unwrap().depth = 1;
    let service = Service::start(cfg).unwrap();
    service
        .register_device(DeviceConfig {
            id: DEVICE,
            worker: backend(),
            native: None,
        })
        .unwrap();
    service
        .register_device(DeviceConfig {
            id: OTHER,
            worker: backend(),
            native: None,
        })
        .unwrap();
    let mut admission = prepare(&service, spec(&file, 4096), Direction::Write, &[7; 4096]);
    let admission::Preparation::Prepared(operation) =
        std::mem::replace(&mut admission.state, admission::Preparation::Reserved)
    else {
        unreachable!()
    };
    admission.state = admission::Preparation::Prepared(
        operation.with_worker_fault(file_io::WorkerFault::new(Duration::from_millis(300), false)),
    );
    let (sender, _result) = mpsc::channel();
    admission.publish_channel(sender).unwrap();
    drop(admission);
    let mut queued = service.read(spec(&file, 4096)).unwrap();
    assert_eq!(service.snapshot().unwrap().queued_requests, 1);
    queued.request_cancel();
    assert!(matches!(
        queued.wait(deadline()).unwrap().status,
        CompletionStatus::Cancelled
    ));
    drop(queued);
    let released_by = Instant::now() + Duration::from_millis(150);
    while service.snapshot().unwrap().reserved_bytes != 4096 {
        assert!(
            Instant::now() < released_by,
            "queued cancellation waited for unrelated physical retirement"
        );
        thread::yield_now();
    }
    assert_eq!(service.snapshot().unwrap().awaiting_retirement, 1);
    let mut other = spec(&file, 4096);
    other.device = OTHER;
    let result = service.read(other).unwrap().wait(deadline()).unwrap();
    assert!(matches!(
        result.status,
        CompletionStatus::ShortIo | CompletionStatus::Complete
    ));
    assert_eq!(service.snapshot().unwrap().awaiting_retirement, 1);
    drop(result);
    service.shutdown().unwrap();
}

#[test]
fn completed_native_result_survives_service_shutdown_before_wait() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut waiter = service.write(spec(&file, 4096), &[0x73; 4096]).unwrap();
    service.shutdown().unwrap();
    let result = waiter.wait(deadline()).unwrap();
    assert!(matches!(
        result.status,
        CompletionStatus::Shutdown | CompletionStatus::Complete
    ));
}

#[test]
fn dropped_native_waiter_cannot_deadlock_the_next_capacity_wait() {
    let directory = Directory::new();
    let file = directory.file();
    let mut cfg = config();
    cfg.admission.max_bytes = 4096;
    let service = Service::start(cfg).unwrap();
    service
        .register_device(DeviceConfig {
            id: DEVICE,
            worker: backend(),
            native: None,
        })
        .unwrap();
    drop(service.write(spec(&file, 4096), &[7; 4096]).unwrap());
    let next = service
        .write(spec(&file, 4096), &[8; 4096])
        .unwrap()
        .wait(deadline())
        .unwrap();
    assert!(matches!(next.status, CompletionStatus::Complete));
    service.shutdown().unwrap();
}

#[test]
fn reserving_fifo_head_wakes_the_next_waiter_when_bytes_remain() {
    let directory = Directory::new();
    let file = directory.file();
    let mut limits = config();
    limits.admission.max_bytes = 8192;
    let service = Service::start(limits).unwrap();
    service
        .register_device(DeviceConfig {
            id: DEVICE,
            worker: backend(),
            native: None,
        })
        .unwrap();
    let (options, _) = spec(&file, 8192).split();
    let mut occupied = service.begin(options).unwrap();
    occupied.wait_capacity().unwrap();
    let (options, _) = spec(&file, 4096).split();
    let mut older = service.begin(options).unwrap();
    let mut request = spec(&file, 4096);
    request.policy.deadline = Some(Instant::now() + Duration::from_secs(1));
    let (options, _) = request.split();
    let mut later = service.begin(options).unwrap();
    drop(occupied);

    // The later waiter handles the capacity-release wake before the FIFO head.
    // Freeze its next wait at that observed epoch so the head runs second.
    let observed = service.shared.wake.epoch();
    assert!(!later.ready().unwrap());
    let wake = Arc::clone(&service.shared.wake);
    let (finished, result) = mpsc::channel();
    let waiter = thread::spawn(move || {
        wake.wait(observed, Some(Duration::from_secs(1)));
        let reserved = later.wait_capacity();
        finished.send((reserved, later)).unwrap();
    });
    older.wait_capacity().unwrap();
    let (reserved, later) = result.recv_timeout(Duration::from_secs(2)).unwrap();
    assert!(
        reserved.is_ok(),
        "later waiter timed out with 4096 bytes free"
    );
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 8192);
    drop((later, older));
    waiter.join().unwrap();
    service.shutdown().unwrap();
}

#[test]
fn retained_lifetime_release_wakes_byte_waiter_after_driver_becomes_idle() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let mut cfg = config();
    cfg.admission.max_bytes = 4096;
    let service = Service::start(cfg).unwrap();
    service
        .register_device(DeviceConfig {
            id: DEVICE,
            worker: backend(),
            native: None,
        })
        .unwrap();
    let mut first = service.read(spec(&file, 4096)).unwrap();
    let lifetime = first.wait(deadline()).unwrap();
    drop(first);
    let (options, _) = spec(&file, 4096).split();
    let mut later = service.begin(options).unwrap();
    assert!(!later.ready().unwrap());
    let wake = Arc::clone(&service.shared.wake);
    let observed = wake.epoch();
    let (ready, started) = mpsc::channel();
    let (done, result) = mpsc::channel();
    let waiting = thread::spawn(move || {
        ready.send(()).unwrap();
        wake.wait(observed, Some(Duration::from_secs(5)));
        let reserved = later.wait_capacity();
        done.send((reserved, later)).unwrap();
    });
    started.recv_timeout(Duration::from_secs(2)).unwrap();
    drop(lifetime);
    let (reserved, later) = result.recv_timeout(Duration::from_secs(2)).unwrap();
    assert!(reserved.is_ok());
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 4096);
    drop(later);
    waiting.join().unwrap();
    service.shutdown().unwrap();
}

#[test]
fn delayed_cancellation_entries_keep_their_record_budget_until_consumed() {
    let directory = Directory::new();
    let file = directory.file();
    let mut limits = config();
    limits.admission.max_records = 3;

    let mut driver = engine_with(limits);
    let queue_capacity = driver.shared.cancellations.lock().unwrap().capacity();
    for _ in 0..3 {
        let (id, completed) = enqueue(&mut driver, spec(&file, 4096), Direction::Read, &[]);
        dispatch(&mut driver);
        let (submission, physical) = retired(&driver);
        let cell = Arc::clone(&driver.active[&id].cell);
        assert_eq!(cell.phase.load(Ordering::Acquire), admission::PUBLISHED);
        assert!(!cell.cancelled.swap(true, Ordering::AcqRel));
        // Cancellation has observed PUBLISHED, then pauses before enqueueing.
        driver.retired(submission, physical);
        driver.flush_test();
        drop(completed.recv_timeout(Duration::from_secs(1)).unwrap());
        cell.enqueue_cancellation();
        drop(cell);
    }
    let mut new_requests = 0;
    let mut receivers = Vec::new();
    for _ in 0..3 {
        let (options, target) = spec(&file, 4096).split();
        let mut admission = match Admission::begin(Arc::clone(&driver.shared), options) {
            Ok(admission) => admission,
            Err(error) => {
                assert_eq!(error.kind(), io::ErrorKind::WouldBlock);
                break;
            }
        };
        admission.wait_capacity().unwrap();
        admission.prepare(target, Direction::Read, &[]).unwrap();
        let admission::Preparation::Prepared(operation) =
            std::mem::replace(&mut admission.state, admission::Preparation::Published)
        else {
            unreachable!()
        };
        admission
            .cell
            .phase
            .store(admission::PUBLISHED, Ordering::Release);
        let (destination, completed) = mpsc::channel();
        driver.accept(Incoming {
            operation,
            cell: Arc::clone(&admission.cell),
            request: Some(Box::new(ChannelCompletion(destination))),
        });
        assert_eq!(admission.cell.cancel(), CancelOutcome::Cancelled);
        receivers.push(completed);
        new_requests += 1;
    }
    assert_eq!(
        driver.shared.cancellations.lock().unwrap().capacity(),
        queue_capacity,
        "delayed cancellation entries allowed post-accept queue growth"
    );
    assert_eq!(
        new_requests, 0,
        "queued cancellations must retain their records"
    );
    assert_eq!(driver.shared.ledger.lock().unwrap().usage.records, 3);
    driver.progress(None);
    driver.flush_test();
    drop(receivers);
    assert_eq!(driver.shared.ledger.lock().unwrap().usage.records, 0);
    let (options, _) = spec(&file, 4096).split();
    let admission = Admission::begin(Arc::clone(&driver.shared), options).unwrap();
    drop(admission);
}

fn assert_partial_read_is_hidden_after_terminal(status: CompletionStatus) {
    let directory = Directory::new();
    let file = directory.file();
    file_io::write_all_at(&file, &[0x61; 8192], 0).unwrap();
    let mut driver = engine();
    let until = Instant::now() + Duration::from_millis(200);
    let policy = RequestPolicy {
        deadline: matches!(status, CompletionStatus::TimedOut).then_some(until),
        retry_alignment: NonZeroUsize::new(4096),
        ..RequestPolicy::default()
    };
    let (id, completed) = enqueue(
        &mut driver,
        read(&file, 8192, 0..8192, policy),
        Direction::Read,
        &[],
    );
    dispatch(&mut driver);
    let (submission, mut first) = retired(&driver);
    first.result = Ok(4096);
    driver.retired(submission, first);
    driver.flush_test();
    assert_eq!(driver.active[&id].transferred, 4096);
    dispatch(&mut driver);
    let (submission, retry) = retired(&driver);
    match status {
        CompletionStatus::TimedOut => {
            thread::sleep(until.saturating_duration_since(Instant::now()));
        }
        CompletionStatus::Shutdown => {
            driver
                .remove_config(DEVICE, Instant::now())
                .unwrap();
        }
        CompletionStatus::Cancelled => {
            assert_eq!(driver.active[&id].cell.cancel(), CancelOutcome::Cancelled);
        }
        _ => unreachable!(),
    }
    driver.retired(submission, retry);
    driver.flush_test();
    let completion = completed.recv_timeout(Duration::from_secs(1)).unwrap();
    assert_eq!(
        std::mem::discriminant(&completion.status),
        std::mem::discriminant(&status)
    );
    assert_eq!(completion.transferred, 4096);
    assert!(
        completion.buffer().is_none(),
        "terminal result exposed its buffer"
    );
    assert!(
        completion.data().is_none(),
        "terminal result exposed its read prefix"
    );
    assert_eq!(driver.snapshot().reserved_bytes, 0);
    assert_eq!(driver.snapshot().admitted_requests, 1);
    drop(completion);
    driver.progress(None);
    driver.flush_test();
    assert_eq!(driver.snapshot().reserved_bytes, 0);
}

#[test]
fn partial_read_does_not_expose_prefix_after_terminal_result() {
    for status in [
        CompletionStatus::TimedOut,
        CompletionStatus::Shutdown,
        CompletionStatus::Cancelled,
    ] {
        assert_partial_read_is_hidden_after_terminal(status);
    }
}
