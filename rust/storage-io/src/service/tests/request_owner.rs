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
struct Callback<F>(F);
impl<F: FnMut(&[u8]) -> Result<i32, ()> + Send + Sync> Consumer for Callback<F> {
    fn process(&mut self, bytes: &[u8]) -> Result<i32, ()> {
        (self.0)(bytes)
    }
}
fn callback<F: FnMut(&[u8]) -> Result<i32, ()> + Send + Sync + 'static>(f: F) -> CallbackOwner {
    Box::new(Callback(f))
}
fn callback_read(service: &Service, file: &Arc<File>, callback: CallbackOwner) -> Request {
    let (mut options, target) = spec(file, 4096).split();
    options.statistics_lane = Some(0);
    service
        .submit_request(options, target, Direction::Read, &[], Some(callback), None)
        .unwrap()
}
fn delayed_write(service: &Service, file: &Arc<File>, duration: Duration) -> Request {
    let (mut options, target) = spec(file, 4096).split();
    options.execution = Execution::WorkerRequired;
    service
        .submit_request(
            options,
            target,
            Direction::Write,
            &[0x51; 4096],
            None,
            Some(file_io::WorkerFault::new(duration, false)),
        )
        .unwrap()
}
fn callback_service(threads: usize) -> Service {
    let mut config = config();
    config.callback_threads = threads;
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
fn physically_idle(service: &Service) {
    let until = deadline();
    while service.snapshot().unwrap().awaiting_retirement != 0 {
        assert!(Instant::now() < until);
        thread::yield_now();
    }
}
#[test]
fn callback_receives_requested_window_and_releases_raw_input() {
    let directory = Directory::new();
    let file = directory.file();
    file_io::write_all_at(&file, b"01234567", 0).unwrap();
    let service = service();
    let mut spec = spec(&file, 8);
    spec.window = 2..6;
    let (options, target) = spec.split();
    let request = service
        .submit_request(
            options,
            target,
            Direction::Read,
            &[],
            Some(callback(|bytes| {
                assert_eq!(bytes, b"2345");
                Ok(0)
            })),
            None,
        )
        .unwrap();
    let result = request.wait(deadline()).unwrap();
    assert!(matches!(result.status, CompletionStatus::Complete));
    assert_eq!(request.callback_error(), 0);
    assert_eq!(request.data_size(), 4);
    assert!(request.has_callback_output());
    assert!(request.data().is_empty());
    service.shutdown().unwrap();
}
#[test]
fn queued_callback_cancellation_skips_business_processing() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = callback_service(1);
    let (entered, started) = mpsc::channel();
    let (release, released) = mpsc::channel();
    let released = Mutex::new(released);
    let first = callback_read(
        &service,
        &file,
        callback(move |_| {
            entered.send(()).unwrap();
            released
                .lock()
                .unwrap()
                .recv_timeout(Duration::from_secs(3))
                .unwrap();
            Ok(0)
        }),
    );
    started.recv_timeout(Duration::from_secs(3)).unwrap();
    let calls = Arc::new(AtomicUsize::new(0));
    let invoked = Arc::clone(&calls);
    let second = callback_read(
        &service,
        &file,
        callback(move |_| {
            invoked.fetch_add(1, Ordering::Relaxed);
            Ok(0)
        }),
    );
    physically_idle(&service);
    assert_eq!(second.request_cancel(), CancelOutcome::Cancelled);
    assert!(matches!(
        second.wait(deadline()).unwrap().status,
        CompletionStatus::Cancelled
    ));
    release.send(()).unwrap();
    first.wait(deadline()).unwrap();
    service.shutdown().unwrap();
    assert_eq!(calls.load(Ordering::Relaxed), 0);
}
#[test]
fn committed_callback_rejects_cancel_and_wait_drains_borrowed_context() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = callback_service(1);
    let (entered, started) = mpsc::channel();
    let (release, released) = mpsc::channel();
    let released = Mutex::new(released);
    let request = callback_read(
        &service,
        &file,
        callback(move |_| {
            entered.send(()).unwrap();
            released
                .lock()
                .unwrap()
                .recv_timeout(Duration::from_secs(3))
                .unwrap();
            Ok(0)
        }),
    );
    started.recv_timeout(Duration::from_secs(3)).unwrap();
    assert_eq!(request.request_cancel(), CancelOutcome::NotPending);
    let copy = request.clone();
    let (done, returned) = mpsc::channel();
    let waiting = thread::spawn(move || {
        let result = copy.wait(Instant::now()).unwrap();
        assert!(matches!(result.status, CompletionStatus::Complete));
        done.send(()).unwrap();
    });
    assert!(returned.recv_timeout(Duration::from_millis(20)).is_err());
    release.send(()).unwrap();
    returned.recv_timeout(Duration::from_secs(3)).unwrap();
    waiting.join().unwrap();
    service.shutdown().unwrap();
}
#[test]
fn callback_failure_is_terminal_and_does_not_kill_delivery_worker() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = callback_service(1);
    let failed = callback_read(&service, &file, callback(|_| Err(())));
    failed.wait(deadline()).unwrap();
    assert_ne!(failed.callback_error(), 0);
    let succeeded = callback_read(&service, &file, callback(|_| Ok(0)));
    succeeded.wait(deadline()).unwrap();
    assert_eq!(succeeded.callback_error(), 0);
    service.shutdown().unwrap();
}
#[test]
fn dropping_one_handle_keeps_the_other_live() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let request = delayed_write(&service, &file, Duration::from_millis(40));
    let copy = request.clone();
    drop(request);
    let result = copy.wait(deadline()).unwrap();
    assert!(matches!(result.status, CompletionStatus::Complete));
    let mut bytes = [0; 4096];
    file_io::read_exact_at(&file, &mut bytes, 0).unwrap();
    assert_eq!(bytes, [0x51; 4096]);
    service.shutdown().unwrap();
}
#[test]
fn last_external_handle_cancels_without_releasing_running_buffer() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = Service::start(config()).unwrap();
    service
        .register_device(DeviceConfig {
            id: DEVICE,
            worker: Backend::new(BackendKind::Worker { threads: 1 }, 4).unwrap(),
            native: None,
        })
        .unwrap();
    let first = delayed_write(&service, &file, Duration::from_millis(200));
    let second = service.write(spec(&file, 4096), &[0x62; 4096]).unwrap();
    assert_eq!(service.snapshot().unwrap().awaiting_retirement, 2);
    drop(second);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 8192);
    first.wait(deadline()).unwrap();
    physically_idle(&service);
    let mut bytes = [0; 4096];
    file_io::read_exact_at(&file, &mut bytes, 0).unwrap();
    assert!(bytes.iter().all(|byte| *byte == 0x51));
    service.shutdown().unwrap();
}
#[test]
fn retained_result_outlives_service_without_retaining_its_driver() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let driver = Arc::downgrade(&service.driver);
    let request = service.write(spec(&file, 4096), &[0x73; 4096]).unwrap();
    let copy = request.clone();
    request.wait(deadline()).unwrap();
    drop(request);
    service.shutdown().unwrap();
    drop(service);
    assert!(driver.upgrade().is_none());
    assert_eq!(copy.data(), &[0x73; 4096]);
}
#[test]
fn callback_can_stop_and_release_final_service_owner() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = Arc::new(callback_service(1));
    let driver = Arc::downgrade(&service.driver);
    let (entered, started) = mpsc::channel();
    let (release, released) = mpsc::channel();
    let released = Mutex::new(released);
    let owner = Mutex::new(Some(Arc::clone(&service)));
    let request = callback_read(
        &service,
        &file,
        callback(move |_| {
            entered.send(()).unwrap();
            released
                .lock()
                .unwrap()
                .recv_timeout(Duration::from_secs(3))
                .unwrap();
            let service = owner.lock().unwrap().take().unwrap();
            assert_eq!(
                service.wait_device_drained(DEVICE).unwrap_err().kind(),
                io::ErrorKind::WouldBlock
            );
            service.stop();
            drop(service);
            Ok(0)
        }),
    );
    started.recv_timeout(Duration::from_secs(3)).unwrap();
    drop(service);
    release.send(()).unwrap();
    request.wait(deadline()).unwrap();
    let until = deadline();
    while driver.upgrade().is_some() {
        assert!(Instant::now() < until);
        thread::yield_now();
    }
}
#[test]
fn completion_statistics_are_recorded_once_without_waiting() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let request = service.write(spec(&file, 4096), &[0x75; 4096]).unwrap();
    let until = deadline();
    while !request.is_finished() {
        assert!(Instant::now() < until);
        thread::yield_now();
    }
    let snapshot = service
        .statistics()
        .success_snapshot(statistics_now_us() + 1)
        .unwrap();
    assert_eq!(snapshot[1].completed, 1);
    assert_eq!(snapshot[1].bytes, 4096);
    request.wait(deadline()).unwrap();
    request.wait(deadline()).unwrap();
    let snapshot = service
        .statistics()
        .success_snapshot(statistics_now_us() + 1)
        .unwrap();
    assert_eq!(snapshot[1].completed, 0);
    service.shutdown().unwrap();
}

#[test]
fn deadline_completes_without_a_waiter_while_physical_input_remains_owned() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = service();
    let (mut options, target) = spec(&file, 4096).split();
    options.execution = Execution::WorkerRequired;
    options.policy.deadline = Some(Instant::now() + Duration::from_millis(20));
    let request = service
        .submit_request(
            options,
            target,
            Direction::Write,
            &[0x61; 4096],
            None,
            Some(file_io::WorkerFault::new(Duration::from_millis(500), false)),
        )
        .unwrap();
    let until = Instant::now() + Duration::from_millis(300);
    while !request.is_finished() {
        assert!(
            Instant::now() < until,
            "logical deadline needed an observing waiter"
        );
        thread::yield_now();
    }
    assert!(matches!(
        request.completed().unwrap().status,
        CompletionStatus::TimedOut
    ));
    assert_eq!(service.snapshot().unwrap().awaiting_retirement, 1);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 4096);
    service.shutdown().unwrap();
    assert_eq!(request.data_size(), 0);
}
#[test]
fn callback_commit_checks_deadline_even_when_timer_worker_is_stopped() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = callback_service(1);
    let (entered, started) = mpsc::channel();
    let (release, released) = mpsc::channel();
    let released = Mutex::new(released);
    let first = callback_read(
        &service,
        &file,
        callback(move |_| {
            entered.send(()).unwrap();
            released
                .lock()
                .unwrap()
                .recv_timeout(Duration::from_secs(3))
                .unwrap();
            Ok(0)
        }),
    );
    started.recv_timeout(Duration::from_secs(3)).unwrap();
    let calls = Arc::new(AtomicUsize::new(0));
    let invoked = Arc::clone(&calls);
    let (mut options, target) = spec(&file, 4096).split();
    let expires = Instant::now() + Duration::from_millis(100);
    options.policy.deadline = Some(expires);
    let second = service
        .submit_request(
            options,
            target,
            Direction::Read,
            &[],
            Some(callback(move |_| {
                invoked.fetch_add(1, Ordering::Relaxed);
                Ok(0)
            })),
            None,
        )
        .unwrap();
    physically_idle(&service);
    service.stop_progress_worker().unwrap();
    thread::sleep(expires.saturating_duration_since(Instant::now()) + Duration::from_millis(5));
    release.send(()).unwrap();
    first.wait(deadline()).unwrap();
    assert!(matches!(
        second.wait(deadline()).unwrap().status,
        CompletionStatus::TimedOut
    ));
    assert_eq!(calls.load(Ordering::Relaxed), 0);
    service.shutdown().unwrap();
}
#[test]
fn changing_callback_limit_is_rejected_without_disrupting_committed_work() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = callback_service(2);
    let barrier = Arc::new(Barrier::new(3));
    let (entered, started) = mpsc::channel();
    let mut requests = Vec::new();
    for _ in 0..2 {
        let barrier = Arc::clone(&barrier);
        let entered = entered.clone();
        requests.push(callback_read(
            &service,
            &file,
            callback(move |_| {
                entered.send(()).unwrap();
                barrier.wait();
                Ok(0)
            }),
        ));
    }
    for _ in 0..2 {
        started.recv_timeout(Duration::from_secs(3)).unwrap();
    }
    let snapshot = service.snapshot().unwrap();
    let mut config = snapshot.config;
    config.callback_threads = 1;
    assert!(matches!(
        service.reconfigure(config),
        Err(ConfigError::Invalid(error)) if error.kind() == io::ErrorKind::Unsupported
    ));
    assert_eq!(service.snapshot().unwrap().config.callback_threads, 2);
    let mut config = service.snapshot().unwrap().config;
    config.admission.max_bytes += 4096;
    service.reconfigure(config).unwrap();
    barrier.wait();
    for request in requests {
        request.wait(deadline()).unwrap();
    }
    let later = callback_read(&service, &file, callback(|_| Ok(0)));
    later.wait(deadline()).unwrap();
    service.shutdown().unwrap();
}

#[test]
fn last_external_drop_drains_an_already_committed_callback() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = callback_service(1);
    let (entered, started) = mpsc::channel();
    let (release, released) = mpsc::channel();
    let released = Mutex::new(released);
    let request = callback_read(
        &service,
        &file,
        callback(move |_| {
            entered.send(()).unwrap();
            released
                .lock()
                .unwrap()
                .recv_timeout(Duration::from_secs(3))
                .unwrap();
            Ok(0)
        }),
    );
    started.recv_timeout(Duration::from_secs(3)).unwrap();
    let (done, returned) = mpsc::channel();
    let dropping = thread::spawn(move || {
        drop(request);
        done.send(()).unwrap();
    });
    assert!(returned.recv_timeout(Duration::from_millis(20)).is_err());
    release.send(()).unwrap();
    returned.recv_timeout(Duration::from_secs(3)).unwrap();
    dropping.join().unwrap();
    service.shutdown().unwrap();
}
#[test]
fn callback_self_wait_is_refused_and_self_drop_does_not_deadlock() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = callback_service(1);
    let owner: Arc<Mutex<Option<Request>>> = Arc::new(Mutex::new(None));
    let own = Arc::clone(&owner);
    let (entered, started) = mpsc::channel();
    let (release, released) = mpsc::channel();
    let released = Mutex::new(released);
    let (done, returned) = mpsc::channel();
    let request = callback_read(
        &service,
        &file,
        callback(move |_| {
            entered.send(()).unwrap();
            released
                .lock()
                .unwrap()
                .recv_timeout(Duration::from_secs(3))
                .unwrap();
            let request = own.lock().unwrap().take().unwrap();
            assert!(
                matches!(request.wait(Instant::now()),Err(error) if error.kind()==io::ErrorKind::WouldBlock)
            );
            drop(request);
            done.send(()).unwrap();
            Ok(0)
        }),
    );
    *owner.lock().unwrap() = Some(request.clone());
    started.recv_timeout(Duration::from_secs(3)).unwrap();
    drop(request);
    release.send(()).unwrap();
    returned.recv_timeout(Duration::from_secs(3)).unwrap();
    service.shutdown().unwrap();
}

#[test]
fn cancelled_retained_result_keeps_record_limit_after_buffer_retirement() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let mut config = config();
    config.admission.max_records = 1;
    let service = Service::start(config).unwrap();
    service
        .register_device(DeviceConfig {
            id: DEVICE,
            worker: backend(),
            native: None,
        })
        .unwrap();
    let request = delayed_write(&service, &file, Duration::from_millis(80));
    request.request_cancel();
    let result = request.wait(deadline()).unwrap();
    drop(request);
    physically_idle(&service);
    let snapshot = service.snapshot().unwrap();
    assert_eq!(snapshot.admitted_requests, 1);
    assert_eq!(snapshot.reserved_bytes, 0);
    assert!(
        matches!(service.read(spec(&file,4096)),Err(error) if error.kind()==io::ErrorKind::WouldBlock)
    );
    drop(result);
    service
        .read(spec(&file, 4096))
        .unwrap()
        .wait(deadline())
        .unwrap();
    service.shutdown().unwrap();
}

#[test]
fn shared_shutdown_is_idempotent_and_statistics_do_not_own_the_service() {
    let directory = Directory::new();
    let file = directory.file();
    let service = Arc::new(service());
    let device_owner = Arc::clone(&service);
    let other = Arc::clone(&service);
    let counters = service.statistics_owner();
    let weak = Arc::downgrade(&service);
    let request = delayed_write(&service, &file, Duration::from_millis(80));
    let stopping = thread::spawn(move || other.shutdown());
    service.shutdown().unwrap();
    stopping.join().unwrap().unwrap();
    device_owner.shutdown().unwrap();
    assert!(
        matches!(service.write(spec(&file,4096),&[0;4096]),Err(error) if error.kind()==io::ErrorKind::BrokenPipe)
    );
    assert!(request.is_finished());
    drop(request);
    drop(device_owner);
    drop(service);
    assert!(weak.upgrade().is_none());
    assert_eq!(counters.lane_count(), 2);
}

#[test]
fn shutdown_waits_for_submitting_route_owner_and_running_callback() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = Arc::new(callback_service(1));
    let route = service.shared.worker_routes.lock().unwrap()[&DEVICE].clone();
    let (entered, started) = mpsc::channel();
    let (release, released) = mpsc::channel();
    let released = Mutex::new(released);
    let request = callback_read(
        &service,
        &file,
        callback(move |_| {
            entered.send(()).unwrap();
            released
                .lock()
                .unwrap()
                .recv_timeout(Duration::from_secs(5))
                .unwrap();
            Ok(0)
        }),
    );
    started.recv_timeout(Duration::from_secs(5)).unwrap();
    let other = Arc::clone(&service);
    let (done, returned) = mpsc::channel();
    let shutdown = thread::spawn(move || {
        done.send(other.shutdown()).unwrap();
    });
    let until = deadline();
    while service.activate().is_ok() {
        assert!(Instant::now() < until);
        thread::yield_now();
    }
    assert!(returned.recv_timeout(Duration::from_millis(20)).is_err());
    drop(route);
    assert!(returned.recv_timeout(Duration::from_millis(20)).is_err());
    release.send(()).unwrap();
    returned
        .recv_timeout(Duration::from_secs(5))
        .unwrap()
        .unwrap();
    shutdown.join().unwrap();
    assert!(request.is_finished());
    service.shutdown().unwrap();
}

#[test]
fn shutdown_waits_for_a_submitting_route_after_its_closed_device_is_reaped() {
    let service = Arc::new(service());
    let route = service.shared.worker_routes.lock().unwrap()[&DEVICE].clone();
    service.remove_device(DEVICE).unwrap();
    Driver::progress_inline(&Arc::downgrade(&service.driver));
    assert!(service.snapshot().unwrap().devices.is_empty());
    let other = Arc::clone(&service);
    let (done, returned) = mpsc::channel();
    let shutdown = thread::spawn(move || {
        done.send(other.shutdown()).unwrap();
    });
    let until = deadline();
    while service.activate().is_ok() {
        assert!(Instant::now() < until);
        thread::yield_now();
    }
    assert!(returned.recv_timeout(Duration::from_millis(20)).is_err());
    drop(route);
    returned
        .recv_timeout(Duration::from_secs(5))
        .unwrap()
        .unwrap();
    shutdown.join().unwrap();
    service.shutdown().unwrap();
}

#[test]
fn reaped_backend_is_released_by_normal_progress_after_its_submitter_leaves() {
    let service = service();
    let route = service.shared.worker_routes.lock().unwrap()[&DEVICE].clone();
    let backend = Arc::downgrade(&service.driver.lock().unwrap().backend(DEVICE, 0));
    service.remove_device(DEVICE).unwrap();
    Driver::progress_inline(&Arc::downgrade(&service.driver));
    assert!(service.snapshot().unwrap().devices.is_empty());
    assert!(backend.upgrade().is_some());
    drop(route);
    Driver::progress_inline(&Arc::downgrade(&service.driver));
    assert!(backend.upgrade().is_none());
    service.shutdown().unwrap();
}

#[test]
fn device_drain_and_shutdown_wait_for_direct_completion_publication() {
    struct PausedCompletion {
        service: Weak<Service>,
        entered: mpsc::Sender<()>,
        resume: Mutex<mpsc::Receiver<()>>,
    }
    impl RequestCompletion for PausedCompletion {
        fn complete(self: Box<Self>, completion: Completion) {
            assert!(matches!(completion.status, CompletionStatus::Complete));
            assert!(self.service.upgrade().unwrap().snapshot().is_ok());
            self.entered.send(()).unwrap();
            self.resume
                .lock()
                .unwrap()
                .recv_timeout(Duration::from_secs(5))
                .unwrap();
        }
        fn cancel(self: Box<Self>) {}
    }
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = Arc::new(service());
    let (entered, started) = mpsc::channel();
    let (resume, released) = mpsc::channel();
    let mut admission = prepare(&service, spec(&file, 4096), Direction::Read, &[]);
    admission
        .register_completion(Box::new(PausedCompletion {
            service: Arc::downgrade(&service),
            entered,
            resume: Mutex::new(released),
        }))
        .unwrap();
    admission.publish().unwrap();
    drop(admission);
    started.recv_timeout(Duration::from_secs(5)).unwrap();
    service.remove_device(DEVICE).unwrap();
    Driver::progress_inline(&Arc::downgrade(&service.driver));
    let drained_before_publication = service.device_drained(DEVICE);
    let other = Arc::clone(&service);
    let (done, returned) = mpsc::channel();
    let shutdown = thread::spawn(move || {
        done.send(other.shutdown()).unwrap();
    });
    let until = deadline();
    while service.activate().is_ok() {
        assert!(Instant::now() < until);
        thread::yield_now();
    }
    let returned_early = returned.recv_timeout(Duration::from_millis(20));
    resume.send(()).unwrap();
    if returned_early.is_err() {
        returned
            .recv_timeout(Duration::from_secs(5))
            .unwrap()
            .unwrap();
    }
    shutdown.join().unwrap();
    assert!(!drained_before_publication);
    assert!(returned_early.is_err());
    assert!(service.device_drained(DEVICE));
}
