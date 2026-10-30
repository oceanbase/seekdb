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

fn delayed(service: &Service, file: &Arc<File>, deadline: Option<Instant>) -> Waiter {
    let mut request = spec(file, 4096);
    request.policy.deadline = deadline;
    let mut admission = prepare(service, request, Direction::Write, &[0x61; 4096]);
    if let admission::Preparation::Prepared(operation) =
        std::mem::replace(&mut admission.state, admission::Preparation::Reserved)
    {
        admission.state = admission::Preparation::Prepared(
            operation
                .with_worker_fault(file_io::WorkerFault::new(Duration::from_millis(30), false)),
        );
    } else {
        unreachable!()
    }
    let lifetime = admission.lifetime();
    let control = admission.take_control().unwrap();
    let (destination, completed) = mpsc::channel();
    let id = admission
        .publish(engine::Destination::Caller(destination))
        .unwrap();
    Waiter {
        lifetime,
        control: Some(control),
        completed,
        id,
        shared: Arc::clone(&service.shared),
    }
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
    assert!(waiter
        .wait_until(Instant::now() + Duration::from_millis(1))
        .unwrap()
        .is_none());
    let completion = waiter.wait(deadline()).unwrap();
    assert!(matches!(completion.status, CompletionStatus::Complete));
    assert_eq!(
        std::fs::read(directory.0.join("file")).unwrap(),
        [0x61; 4096]
    );
    service.shutdown().unwrap();
}
#[test]
fn cancellation_returns_before_physical_retirement_and_retains_charge() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut waiter = delayed(&service, &file, None);
    assert_eq!(waiter.request_cancel(), CancelOutcome::Cancelled);
    assert!(matches!(waiter.wait(deadline()), Err(e) if e.kind()==io::ErrorKind::Interrupted));
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 4096);
    drop(waiter);
    service.shutdown().unwrap();
}
#[test]
fn deadline_expiry_does_not_release_running_buffer() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let until = Instant::now() + Duration::from_millis(2);
    let mut waiter = delayed(&service, &file, Some(until));
    assert!(matches!(waiter.wait_until(until),Err(e) if e.kind()==io::ErrorKind::TimedOut));
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 4096);
    thread::sleep(Duration::from_millis(40));
    let completion = waiter.wait(deadline()).unwrap();
    assert!(matches!(completion.status, CompletionStatus::TimedOut));
    drop(completion);
    drop(waiter);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 0);
    service.shutdown().unwrap();
}
#[test]
fn dropping_the_last_waiter_cannot_free_an_operation_early() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let waiter = delayed(&service, &file, None);
    drop(waiter);
    assert_eq!(service.snapshot().unwrap().admitted_requests, 1);
    service.shutdown().unwrap();
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
    replacement.admission.max_bytes = replacement.admission.probe_bytes + 4096;
    let receipt = service.reconfigure(Generation(0), replacement).unwrap();
    assert_eq!(receipt.debt[0].bytes, 4096);
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
    replacement.admission.max_bytes = replacement.admission.probe_bytes + 4096;
    service.reconfigure(Generation(0), replacement).unwrap();
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
        let result = prepared.publish(engine::Destination::Caller(sender));
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
    assert!(service
        .register_device(DeviceConfig {
            id: DEVICE,
            worker: backend(),
            native: None
        })
        .is_err());
    assert_eq!(service.snapshot().unwrap(), before);
    service.shutdown().unwrap();
}

#[test]
fn queued_cancel_returns_capacity_while_another_request_cannot_retire() {
    let directory = Directory::new();
    let file = directory.file();
    let mut cfg = config();
    cfg.admission.max_bytes = cfg.admission.probe_bytes + 8192;
    cfg.devices.get_mut(&DEVICE).unwrap().depth = 2;
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
    admission
        .publish(engine::Destination::Caller(sender))
        .unwrap();
    drop(admission);
    let mut queued = service.read(spec(&file, 4096)).unwrap();
    assert_eq!(service.snapshot().unwrap().queued_requests, 1);
    queued.request_cancel();
    assert!(
        matches!(queued.wait(deadline()),Err(error) if error.kind()==io::ErrorKind::Interrupted)
    );
    drop(queued);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 4096);
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
    cfg.admission.max_bytes = cfg.admission.probe_bytes + 4096;
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
    limits.admission.max_bytes = limits.admission.probe_bytes + 8192;
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
fn delayed_cancellation_entries_keep_their_record_budget_until_consumed() {
    let directory = Directory::new();
    let file = directory.file();
    let mut limits = config();
    limits.admission.max_records = 4;
    limits.admission.probe_records = 1;
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
            owner: engine::WorkOwner::Ordinary,
            operation,
            cell: Arc::clone(&admission.cell),
            destination: engine::Destination::Caller(destination),
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
    assert_eq!(driver.shared.ledger.lock().unwrap().usage[0].records, 3);
    driver.progress(None);
    drop(receivers);
    assert_eq!(driver.shared.ledger.lock().unwrap().usage[0].records, 0);
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
    assert_eq!(driver.active[&id].transferred, 4096);
    dispatch(&mut driver);
    let (submission, retry) = retired(&driver);
    match status {
        CompletionStatus::TimedOut => {
            thread::sleep(until.saturating_duration_since(Instant::now()));
        }
        CompletionStatus::Shutdown => {
            driver
                .remove_config(Generation(0), DEVICE, Instant::now())
                .unwrap();
        }
        CompletionStatus::Cancelled => {
            assert_eq!(driver.active[&id].cell.cancel(), CancelOutcome::Cancelled);
        }
        _ => unreachable!(),
    }
    driver.retired(submission, retry);
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
    assert_eq!(driver.snapshot().reserved_bytes, 8192);
    drop(completion);
    driver.progress(None);
    assert_eq!(driver.snapshot().reserved_bytes, 0);
}

#[test]
fn partial_read_late_timeout_does_not_expose_the_prefix() {
    assert_partial_read_is_hidden_after_terminal(CompletionStatus::TimedOut);
}

#[test]
fn partial_read_late_close_does_not_expose_the_prefix() {
    assert_partial_read_is_hidden_after_terminal(CompletionStatus::Shutdown);
}

#[test]
fn partial_read_late_cancel_does_not_expose_the_prefix() {
    assert_partial_read_is_hidden_after_terminal(CompletionStatus::Cancelled);
}

fn enqueue_device(driver: &mut Engine, file: &Arc<File>, tag: u64) -> RequestId {
    let mut request = spec(file, 4096);
    request.tag = tag;
    let mut admission = core_prepare(driver, request, Direction::Read, &[]);
    let admission::Preparation::Prepared(operation) =
        std::mem::replace(&mut admission.state, admission::Preparation::Published)
    else {
        unreachable!()
    };
    admission
        .cell
        .phase
        .store(admission::PUBLISHED, Ordering::Release);
    let id = admission.id();
    driver.accept(Incoming {
        operation,
        cell: Arc::clone(&admission.cell),
        destination: engine::Destination::Device(DEVICE),
        owner: engine::WorkOwner::Ordinary,
    });
    id
}

#[cfg_attr(
    feature = "ownership-test-support",
    allow(
        clippy::result_large_err,
        reason = "the proof-only owner enlarges Operation returned by the dispatch double"
    )
)]
fn enqueue_device_error(driver: &mut Engine, file: &Arc<File>, tag: u64) -> RequestId {
    let id = enqueue_device(driver, file, tag);
    driver.dispatch_with(Instant::now(), |_, operation| {
        Err(file_io::SubmitFailure {
            operation,
            error: io::Error::other("submission failed"),
        })
    });
    id
}

#[test]
fn replenished_ready_results_do_not_starve_retirement_cancel_or_health() {
    let directory = Directory::new();
    let file = directory.file();
    file_io::write_all_at(&file, &[0x61; 4096], 0).unwrap();
    let mut cfg = config();
    cfg.admission.max_records = 300;
    let mut driver = engine_with(cfg);
    let mut expected = VecDeque::new();
    for tag in 0..130 {
        expected.push_back(enqueue_device_error(&mut driver, &file, tag));
    }
    let first = driver.poll_device_completion(DEVICE).0.unwrap();
    assert_eq!(first.id, expected.pop_front().unwrap());

    let (physical, physical_result) =
        enqueue(&mut driver, spec(&file, 4096), Direction::Read, &[]);
    let wake = Arc::clone(&driver.shared.wake);
    let observed = wake.epoch();
    dispatch(&mut driver);
    let until = deadline();
    while wake.epoch() == observed {
        let wait = until
            .checked_duration_since(Instant::now())
            .expect("physical completion did not arrive");
        wake.wait(observed, Some(wait));
    }
    assert_eq!(driver.snapshot().awaiting_retirement, 1);

    let (cancelled, cancelled_result) =
        enqueue(&mut driver, spec(&file, 4096), Direction::Read, &[]);
    assert_eq!(
        driver.active[&cancelled].cell.cancel(),
        CancelOutcome::Cancelled
    );
    let (options, target) = spec(&file, 4096).split();
    let admission = Admission::begin(Arc::clone(&driver.shared), options).unwrap();
    let mut lifetime = admission.lifetime();
    lifetime.capture(&admission, target, false);
    lifetime.enable_observer(&driver.shared.health);
    let observer = lifetime.take_final_observer().unwrap();
    drop((admission, lifetime));
    assert_eq!(
        observer
            .observe(FinalFacts {
                io_error: true,
                raw_os_error: std::num::NonZeroI32::new(5),
                deadline_exceeded: false,
                finished_wall_us: 123,
            })
            .error,
        ObservationOutcome::Accepted
    );

    for tag in 130..194 {
        let completion = driver.poll_device_completion(DEVICE).0.unwrap();
        assert_eq!(completion.id, expected.pop_front().unwrap());
        assert!(matches!(completion.status, CompletionStatus::IoError(_)));
        drop(completion);
        expected.push_back(enqueue_device_error(&mut driver, &file, tag));
    }
    let result = physical_result
        .try_recv()
        .expect("physical result was starved");
    assert_eq!(result.id, physical);
    assert_eq!(result.data().unwrap(), &[0x61; 4096]);
    assert_eq!(driver.snapshot().devices[0].physical_depth[0], 0);
    let cancelled = cancelled_result.try_recv().unwrap();
    assert!(matches!(cancelled.status, CompletionStatus::Cancelled));
    assert!(cancelled.buffer().is_none());
    let reserved = driver.snapshot().reserved_bytes;
    drop(cancelled);
    assert_eq!(driver.snapshot().reserved_bytes, reserved - 4096);
    let health = driver.snapshot().health;
    assert_eq!(health.queued, 0);
    assert!(health.active != 0 || health.successful_passes != 0 || health.failed_passes != 0);

    driver
        .remove_config(Generation(0), DEVICE, Instant::now())
        .unwrap();
    while let Some(id) = expected.pop_front() {
        let completion = driver.poll_device_completion(DEVICE).0.unwrap();
        assert_eq!(completion.id, id);
    }
    let until = deadline();
    while !driver.device_drained(DEVICE) {
        assert!(driver.poll_device_completion(DEVICE).0.is_none());
        assert!(Instant::now() < until);
        thread::yield_now();
    }
    assert!(driver.poll_device_completion(DEVICE).0.is_none());
    drop((result, first, observer));
    driver.progress(None);
    assert_eq!(driver.snapshot().reserved_bytes, 0);
}

#[test]
fn completion_turn_retains_each_buffer_after_device_reaping() {
    let directory = Directory::new();
    let file = directory.file();
    file_io::write_all_at(&file, &[0x72; 4096], 0).unwrap();
    let mut driver = engine();
    let mut expected = VecDeque::new();
    for tag in 0..4 {
        expected.push_back(enqueue_device(&mut driver, &file, tag));
        dispatch(&mut driver);
        retire(&mut driver);
    }
    let retained = driver.poll_device_completion(DEVICE).0.unwrap();
    assert_eq!(retained.id, expected.pop_front().unwrap());
    driver
        .remove_config(Generation(0), DEVICE, Instant::now())
        .unwrap();
    driver.progress(None);
    assert!(driver.snapshot().devices.is_empty());
    while let Some(id) = expected.pop_front() {
        let completion = driver.poll_device_completion(DEVICE).0.unwrap();
        assert_eq!(completion.id, id);
        assert!(matches!(completion.status, CompletionStatus::Complete));
        assert_eq!(completion.data().unwrap(), &[0x72; 4096]);
    }
    assert!(driver.poll_device_completion(DEVICE).0.is_none());
    assert_eq!(retained.data().unwrap(), &[0x72; 4096]);
    assert_eq!(driver.snapshot().reserved_bytes, 4096);
    drop(retained);
    assert_eq!(driver.snapshot().reserved_bytes, 0);
}

#[test]
fn empty_completion_poll_can_wait_for_a_later_publication() {
    let directory = Directory::new();
    let file = directory.file();
    let service = Arc::new(service());
    assert!(matches!(
        service.poll_completion(DEVICE, Instant::now()),
        Err(error) if error.kind() == io::ErrorKind::WouldBlock
    ));
    let ready = Arc::new(Barrier::new(2));
    let consumer_service = Arc::clone(&service);
    let consumer_ready = Arc::clone(&ready);
    let consumer = thread::spawn(move || {
        consumer_ready.wait();
        consumer_service.poll_completion(DEVICE, deadline()).unwrap()
    });
    ready.wait();
    let mut admission = prepare(&service, spec(&file, 4096), Direction::Write, &[0x39; 4096]);
    let id = admission
        .publish(engine::Destination::Device(DEVICE))
        .unwrap();
    let result = consumer.join().unwrap();
    assert_eq!(result.id, id);
    assert!(matches!(result.status, CompletionStatus::Complete));
    assert_eq!(result.data().unwrap(), &[0x39; 4096]);
    drop((result, admission));
    Arc::try_unwrap(service).ok().unwrap().shutdown().unwrap();
}
