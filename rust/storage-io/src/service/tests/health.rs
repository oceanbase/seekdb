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
use crate::service::health::{owned_payload_bytes_for, CapturedTarget, Iteration, Trigger};

fn facts(error: bool, timeout: bool) -> FinalFacts {
    FinalFacts {
        io_error: error,
        raw_os_error: std::num::NonZeroI32::new(5),
        deadline_exceeded: timeout,
        finished_wall_us: 123,
    }
}
fn capture(
    engine: &mut Engine,
    spec: RequestSpec,
    hook: Option<Box<dyn FnOnce() + Send + Sync>>,
) -> FinalObserver {
    let (options, target) = spec.split();
    let admission = Admission::begin(Arc::clone(&engine.shared), options).unwrap();
    let mut lifetime = admission.lifetime();
    lifetime.capture(&admission, target, false);
    Arc::get_mut(lifetime.target.as_mut().unwrap())
        .unwrap()
        .drop_hook = hook;
    lifetime.enable_observer(&engine.shared.health);
    let observer = lifetime.take_final_observer().unwrap();
    drop((admission, lifetime));
    engine.accept_probes_and_close();
    observer
}
fn publish_probe(engine: &mut Engine) {
    engine.health_turn(Instant::now());
    engine.accept_probes_and_close();
    engine.health_turn(Instant::now());
    engine.accept_probes_and_close();
}
fn large_config() -> Config {
    let mut c = config();
    c.admission.max_records = 300;
    c
}

#[test]
fn two_final_facts_admit_error_first_and_count_every_refusal() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine_with(large_config());
    let ingress = Arc::clone(&engine.shared.health);
    for _ in 0..99 {
        let observer = capture(&mut engine, spec(&file, 4096), None);
        assert_eq!(
            observer.observe(facts(true, false)).error,
            ObservationOutcome::Accepted
        );
    }
    let observer = capture(&mut engine, spec(&file, 4096), None);
    assert_eq!(
        observer.observe(facts(true, true)),
        ObservationReceipt {
            error: ObservationOutcome::Accepted,
            timeout: ObservationOutcome::Full
        }
    );
    assert_eq!(
        observer.observe(facts(true, true)).error,
        ObservationOutcome::AlreadyObserved
    );
    let observer = capture(&mut engine, spec(&file, 4096), None);
    assert_eq!(
        observer.observe(facts(true, true)),
        ObservationReceipt {
            error: ObservationOutcome::Full,
            timeout: ObservationOutcome::Full
        }
    );
    let observer = capture(&mut engine, spec(&file, 4096), None);
    ingress.while_locked(|| {
        assert_eq!(
            observer.observe(facts(false, true)).timeout,
            ObservationOutcome::Busy
        )
    });
    ingress.close();
    let observer = capture_after_close(&mut engine, &file);
    assert_eq!(
        observer.observe(facts(true, false)).error,
        ObservationOutcome::Stopped
    );
    let snapshot = ingress.snapshot(false, Instant::now());
    assert_eq!(
        (snapshot.queued, snapshot.cleaning, snapshot.active),
        (100, 0, 0)
    );
    assert_eq!(
        snapshot.observations,
        ObservationCounters {
            accepted_error: 100,
            accepted_timeout: 0,
            full: 3,
            busy: 1,
            stopped: 1,
            already_observed: 1
        }
    );
    while ingress.discard_one() {}
    assert!(ingress.empty());
}
fn capture_after_close(engine: &mut Engine, file: &Arc<File>) -> FinalObserver {
    capture(engine, spec(file, 4096), None)
}

#[test]
fn detached_cleanup_cannot_lend_its_waiting_credit_to_a_publisher() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine_with(large_config());
    let entered = Arc::new(Barrier::new(2));
    let release = Arc::new(Barrier::new(2));
    let hook_entered = Arc::clone(&entered);
    let hook_release = Arc::clone(&release);
    let observer = capture(
        &mut engine,
        spec(&file, 4096),
        Some(Box::new(move || {
            hook_entered.wait();
            hook_release.wait();
        })),
    );
    assert_eq!(
        observer.observe(facts(true, false)).error,
        ObservationOutcome::Accepted
    );
    drop(observer);
    for _ in 1..100 {
        capture(&mut engine, spec(&file, 4096), None).observe(facts(true, false));
    }
    let ingress = Arc::clone(&engine.shared.health);
    let cleanup = Arc::clone(&ingress);
    let join = thread::spawn(move || cleanup.discard_one());
    entered.wait();
    let snapshot = ingress.snapshot(false, Instant::now());
    assert_eq!((snapshot.queued, snapshot.cleaning), (99, 1));
    assert_eq!(
        capture(&mut engine, spec(&file, 4096), None)
            .observe(facts(true, false))
            .error,
        ObservationOutcome::Full
    );
    release.wait();
    assert!(join.join().unwrap());
    assert_eq!(
        capture(&mut engine, spec(&file, 4096), None)
            .observe(facts(true, false))
            .error,
        ObservationOutcome::Accepted
    );
    assert_eq!(ingress.snapshot(false, Instant::now()).queued, 100);
    while ingress.discard_one() {}
}

#[test]
fn active_credit_covers_final_target_destructor() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine();
    let entered = Arc::new(Barrier::new(2));
    let release = Arc::new(Barrier::new(2));
    let a = Arc::clone(&entered);
    let b = Arc::clone(&release);
    let observer = capture(
        &mut engine,
        spec(&file, 4096),
        Some(Box::new(move || {
            a.wait();
            b.wait();
        })),
    );
    observer.observe(facts(true, false));
    drop(observer);
    let ingress = Arc::clone(&engine.shared.health);
    let diagnosis = ingress.activate(Instant::now()).unwrap();
    capture(&mut engine, spec(&file, 4096), None).observe(facts(true, false));
    let cleanup = Arc::clone(&ingress);
    let join = thread::spawn(move || cleanup.finish(diagnosis));
    entered.wait();
    assert_eq!(ingress.snapshot(false, Instant::now()).active, 1);
    assert!(ingress.activate(Instant::now()).is_none());
    release.wait();
    join.join().unwrap();
    let next = ingress.activate(Instant::now()).unwrap();
    ingress.finish(next);
    assert!(ingress.empty());
}

#[test]
fn full_interval_uses_one_record_and_only_one_live_segment() {
    let directory = Directory::new();
    let file = directory.file();
    let length = 5 * 1024 * 1024;
    file.set_len(length as u64).unwrap();
    let mut engine = engine();
    capture(&mut engine, spec(&file, length), None).observe(facts(true, false));
    publish_probe(&mut engine);
    let mut ranges = Vec::new();
    for _ in 0..3 {
        assert_eq!(
            engine.snapshot().usage[1],
            ResourceUsage {
                records: 1,
                bytes: 2 * 1024 * 1024
            }
        );
        dispatch(&mut engine);
        let (id, retired) = super::retired(&engine);
        ranges.push((
            retired.operation.offset(),
            retired.operation.remaining_len(),
        ));
        engine.retired(id, retired);
    }
    engine.health_turn(Instant::now());
    assert_eq!(
        ranges,
        vec![
            (0, 2 * 1024 * 1024),
            (2 * 1024 * 1024, 2 * 1024 * 1024),
            (4 * 1024 * 1024, 1024 * 1024)
        ]
    );
    assert_eq!(engine.snapshot().usage[1], ResourceUsage::default());
    assert_eq!(engine.snapshot().health.active, 0);
    assert!(!engine.snapshot().health.warning);
}

#[test]
fn failed_last_segment_restarts_the_original_range() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(3 * 1024 * 1024).unwrap();
    let mut engine = engine();
    capture(&mut engine, spec(&file, 3 * 1024 * 1024), None).observe(facts(true, false));
    publish_probe(&mut engine);
    dispatch(&mut engine);
    retire(&mut engine);
    dispatch(&mut engine);
    let (id, mut retired) = super::retired(&engine);
    assert_eq!(retired.operation.offset(), 2 * 1024 * 1024);
    retired.result = Err(io::Error::from_raw_os_error(5));
    engine.retired(id, retired);
    assert_eq!(engine.snapshot().health.active, 1);
    publish_probe(&mut engine);
    dispatch(&mut engine);
    let (id, retired) = super::retired(&engine);
    assert_eq!(retired.operation.offset(), 0);
    engine.retired(id, retired);
    dispatch(&mut engine);
    retire(&mut engine);
    engine.health_turn(Instant::now());
    assert_eq!(engine.snapshot().health.active, 0);
}

#[test]
fn timeout_delivery_keeps_probe_target_record_and_credit_until_retirement() {
    let directory = Directory::new();
    let path = directory.0.join("blocks");
    let owner = UnmarkedBlockFile::open(
        &path,
        BlockConfig {
            block_size: 4 * 1024 * 1024,
            initial_size: 8 * 1024 * 1024,
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
    let mut engine = engine();
    let request = RequestSpec::new(
        DEVICE,
        Target::Block(owner.region(block, 0..4 * 1024 * 1024).unwrap()),
        4 * 1024 * 1024,
    );
    capture(&mut engine, request, None).observe(facts(true, false));
    owner.release(block).unwrap();
    publish_probe(&mut engine);
    dispatch(&mut engine);
    let id = *engine.active.keys().next().unwrap();
    engine.cancel(id, CompletionStatus::TimedOut);
    engine.health_turn(Instant::now());
    assert_eq!(engine.snapshot().usage[1].records, 1);
    assert_eq!(engine.snapshot().health.active, 1);
    assert_eq!(owner.snapshot().pending_release_blocks, 1);
    let ingress = Arc::clone(&engine.shared.health);
    ingress.close();
    engine.shared.gate.lock().unwrap().accepting = false;
    engine.accept_probes_and_close();

    engine.health_turn(Instant::now());
    retire(&mut engine);
    engine.health_turn(Instant::now());
    assert_eq!(engine.snapshot().usage[1], ResourceUsage::default());
    assert_eq!(engine.snapshot().health.active, 0);
    owner.reclaim_released();
    assert_eq!(owner.snapshot().pending_release_blocks, 0);
    assert_eq!(owner.allocate().unwrap(), block);
}

#[test]
fn probe_keeps_original_file_identity_after_path_replacement() {
    let directory = Directory::new();
    let file = directory.file();
    file_io::write_all_at(&file, &[0x71; 4096], 0).unwrap();
    let mut engine = engine();
    capture(&mut engine, spec(&file, 4096), None).observe(facts(true, false));
    fs::rename(directory.0.join("file"), directory.0.join("original")).unwrap();
    let replacement = directory.file();
    file_io::write_all_at(&replacement, &[0x32; 4096], 0).unwrap();
    drop(file);
    publish_probe(&mut engine);
    dispatch(&mut engine);
    let (id, retired) = super::retired(&engine);
    assert_eq!(retired.operation.buffer().as_slice(), &[0x71; 4096]);
    engine.retired(id, retired);
    engine.health_turn(Instant::now());
    assert_eq!(engine.snapshot().health.active, 0);
}

#[test]
fn probe_progresses_while_normal_buffers_and_device_depth_are_saturated() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(3 * 1024 * 1024).unwrap();
    let mut engine = engine();
    let (normal, completed) = enqueue(
        &mut engine,
        spec(&file, 6 * 1024 * 1024),
        Direction::Read,
        &[],
    );
    dispatch(&mut engine);
    capture(&mut engine, spec(&file, 3 * 1024 * 1024), None).observe(facts(true, false));
    publish_probe(&mut engine);
    dispatch(&mut engine);
    assert_eq!(engine.snapshot().devices[0].physical_depth, [3, 1]);
    while !engine.active.is_empty() {
        retire(&mut engine);
        dispatch(&mut engine);
        engine.health_turn(Instant::now());
    }
    let normal_result = completed.recv().unwrap();
    assert_eq!(normal_result.id, normal);
    assert_eq!(engine.snapshot().probe_attempts, 2);
    assert_eq!(engine.snapshot().health.active, 0);
}

#[test]
fn aggregate_native_retry_budget_and_deadline_do_not_restart_at_segments() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(3 * 1024 * 1024).unwrap();
    let mut engine = engine();
    let mut request = spec(&file, 3 * 1024 * 1024);
    request.policy.max_error_retries = 1;
    capture(&mut engine, request, None).observe(facts(true, false));
    publish_probe(&mut engine);
    dispatch(&mut engine);
    let (id, mut r) = super::retired(&engine);
    r.result = Err(io::Error::from(io::ErrorKind::Interrupted));
    engine.retired(id, r);
    dispatch(&mut engine);
    retire(&mut engine);
    dispatch(&mut engine);
    let (id, mut r) = super::retired(&engine);
    r.result = Err(io::Error::from(io::ErrorKind::Interrupted));
    engine.retired(id, r);
    assert_eq!(engine.snapshot().probe_attempts, 3);
    assert_eq!(engine.snapshot().usage[1], ResourceUsage::default());
    engine.health_turn(Instant::now());
    capture(&mut engine, spec(&file, 3 * 1024 * 1024), None).observe(facts(true, false));
    publish_probe(&mut engine);
    dispatch(&mut engine);
    retire(&mut engine);
    engine.dispatch(Instant::now() + Duration::from_secs(20));
    assert_eq!(engine.snapshot().usage[1], ResourceUsage::default());
    assert_eq!(engine.snapshot().awaiting_retirement, 0);
}

#[test]
fn requested_suffix_eof_succeeds_but_requested_range_eof_aborts() {
    for (file_length, success) in [(5000, true), (4999, false)] {
        let directory = Directory::new();
        let file = directory.file();
        file.set_len(file_length).unwrap();
        let mut cfg = config();
        cfg.devices.get_mut(&DEVICE).unwrap().max_probe_bytes = 4096;
        let mut engine = engine_with(cfg);
        let mut request = spec(&file, 8192);
        request.window = 100..5000;
        capture(&mut engine, request, None).observe(facts(true, false));
        publish_probe(&mut engine);
        dispatch(&mut engine);
        retire(&mut engine);
        dispatch(&mut engine);
        let (id, r) = super::retired(&engine);
        assert_eq!(r.result.as_ref().unwrap(), &(file_length as usize - 4096));
        assert_eq!(r.result.as_ref().unwrap() >= &904, success);
        engine.retired(id, r);
        engine.health_turn(Instant::now());
        assert_eq!(engine.snapshot().health.active, 0);
        assert_eq!(engine.snapshot().probe_attempts, 2);
        assert_eq!(
            engine.snapshot().health.successful_passes,
            u64::from(success)
        );
        assert_eq!(engine.snapshot().health.failed_passes, u64::from(!success));
    }
}

fn diagnosis(file: &Arc<File>, now: Instant) -> Diagnosis {
    let (options, target) = spec(file, 4096).split();
    let target = Arc::new(CapturedTarget {
        device_open: Arc::new(AtomicBool::new(true)),
        target,
        options,
        drop_hook: None,
    });
    Diagnosis::new(
        Trigger {
            target,
            seed_ms: 5000,
        },
        HealthConfig::default(),
        now,
    )
}
#[test]
fn warning_boundary_is_captured_while_running_and_live_while_draining() {
    let directory = Directory::new();
    let file = directory.file();
    let engine = engine();
    let ingress = &engine.shared.health;
    let began = Instant::now();
    let mut d = diagnosis(&file, began);
    let id = RequestId(999);
    d.stage = DiagnosisStage::Running {
        id,
        iteration: Iteration {
            warning_at: began + Duration::from_secs(10),
            wait_until: None,
        },
    };
    let mut cfg = HealthConfig::default();
    ingress.configure(cfg);
    d.outcome(
        id,
        &CompletionStatus::TimedOut,
        false,
        ingress,
        began + Duration::from_secs(7),
    );
    assert!(matches!(d.stage, DiagnosisStage::Draining { .. }));
    assert!(!ingress.warning());
    assert!(began + cfg.warning_tolerance < began + Duration::from_secs(7));
    d.retired(id);
    assert!(matches!(d.stage, DiagnosisStage::Ready));
    assert!(d.iteration(cfg, began + Duration::from_secs(7)).is_none());
    d.stage = DiagnosisStage::Running {
        id,
        iteration: Iteration {
            warning_at: began + Duration::from_secs(5),
            wait_until: None,
        },
    };
    cfg.warning_tolerance = Duration::from_secs(20);
    ingress.configure(cfg);
    d.outcome(
        id,
        &CompletionStatus::TimedOut,
        true,
        ingress,
        began + Duration::from_secs(7),
    );
    assert!(ingress.warning());
    let blacklist = cfg.blacklist_interval;
    assert!(
        ingress
            .snapshot(true, began + Duration::from_secs(7) + blacklist)
            .warning
    );
    assert!(
        !ingress
            .snapshot(
                true,
                began + Duration::from_secs(7) + blacklist + Duration::from_nanos(1)
            )
            .warning
    );
}
#[test]
fn late_success_zero_errno_ten_errno_and_eagain_have_distinct_outcomes() {
    let directory = Directory::new();
    let file = directory.file();
    let engine = engine();
    let ingress = &engine.shared.health;
    let now = Instant::now();
    let id = RequestId(999);
    for status in [
        CompletionStatus::Complete,
        CompletionStatus::IoError(io::Error::other("zero errno")),
    ] {
        let mut d = diagnosis(&file, now);
        d.stage = DiagnosisStage::Running {
            id,
            iteration: Iteration {
                warning_at: now,
                wait_until: None,
            },
        };
        d.outcome(id, &status, true, ingress, now + Duration::from_secs(1));
        assert!(matches!(d.stage, DiagnosisStage::Done { pending: None }));
        assert!(!ingress.warning());
    }
    let mut d = diagnosis(&file, now);
    d.stage = DiagnosisStage::Running {
        id,
        iteration: Iteration {
            warning_at: now,
            wait_until: None,
        },
    };
    d.outcome(
        id,
        &CompletionStatus::IoError(io::ErrorKind::WouldBlock.into()),
        true,
        ingress,
        now,
    );
    assert!(
        matches!(d.stage, DiagnosisStage::Backoff { until, .. } if until == now + Duration::from_millis(100))
    );
    assert!(!ingress.warning());
    assert_eq!(d.errors, 0);
    for index in 0..10 {
        d.stage = DiagnosisStage::Running {
            id,
            iteration: Iteration {
                warning_at: now + Duration::from_secs(100),
                wait_until: None,
            },
        };
        d.outcome(
            id,
            &CompletionStatus::IoError(io::Error::from_raw_os_error(5)),
            true,
            ingress,
            now,
        );
        assert_eq!(ingress.warning(), index == 9);
    }
    assert_eq!(d.errors, 10);
}

#[test]
fn native_observer_can_outlive_service_and_keeps_existing_request_record() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = service();
    let ledger = Arc::clone(&service.shared.ledger);
    let mut waiter = service.read(spec(&file, 4096)).unwrap();
    let observer = waiter.take_final_observer().unwrap();
    assert!(waiter.take_final_observer().is_none());
    let completion = waiter.wait(deadline()).unwrap();
    drop(waiter);
    assert_eq!(ledger.lock().unwrap().usage[0].bytes, 4096);
    drop(completion);
    assert_eq!(ledger.lock().unwrap().usage[0].records, 1);
    service.shutdown().unwrap();
    assert_eq!(
        observer.observe(facts(true, true)).error,
        ObservationOutcome::Stopped
    );
    assert!(ledger.lock().unwrap().health_payload_bytes > 0);
    drop(observer);
    assert_eq!(ledger.lock().unwrap().usage[0], ResourceUsage::default());
    assert_eq!(ledger.lock().unwrap().health_payload_bytes, 0);
}
#[test]
fn publication_racing_close_is_either_accepted_and_drained_or_refused() {
    for _ in 0..20 {
        let directory = Directory::new();
        let file = directory.file();
        let mut engine = engine();
        let observer = capture(&mut engine, spec(&file, 4096), None);
        let ingress = Arc::clone(&engine.shared.health);
        let barrier = Arc::new(Barrier::new(2));
        let start = Arc::clone(&barrier);
        let publisher = thread::spawn(move || {
            start.wait();
            observer.observe(facts(true, true))
        });
        barrier.wait();
        ingress.close();
        let receipt = publisher.join().unwrap();
        for outcome in [receipt.error, receipt.timeout] {
            assert!(matches!(
                outcome,
                ObservationOutcome::Accepted
                    | ObservationOutcome::Stopped
                    | ObservationOutcome::Busy
            ));
        }
        while ingress.discard_one() {}
        assert!(ingress.empty());
    }
}
#[test]
fn payload_metric_uses_checked_type_sizes_and_fixed_count_budget() {
    assert!(owned_payload_bytes_for(usize::MAX).is_err());
    let engine = engine();
    let snapshot = engine.snapshot().health;
    assert_eq!(
        snapshot.owned_payload_bytes,
        owned_payload_bytes_for(101).unwrap()
    );
    assert_eq!(
        (snapshot.waiting_capacity, snapshot.active_capacity),
        (100, 1)
    );
    assert_eq!(
        engine.shared.ledger.lock().unwrap().health_payload_bytes,
        snapshot.owned_payload_bytes
    );
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
        lifetime.capture(&admission, target, false);
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
        engine.accept_probes_and_close();
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
                assert!(admission
                    .set_worker_fault(file_io::WorkerFault::new(Duration::ZERO, false))
                    .is_err());
            }
            drop(admission);
            drop(control);
            assert!(!dropped.load(Ordering::Acquire));
            assert_eq!(
                engine.shared.ledger.lock().unwrap().usage[0],
                ResourceUsage {
                    records: 1,
                    bytes: expected_bytes
                }
            );
        }
        drop(lifetime);
        assert!(dropped.load(Ordering::Acquire));
        assert_eq!(
            engine.shared.ledger.lock().unwrap().usage[0],
            ResourceUsage::default()
        );
    }
}

#[test]
fn timeout_seed_keeps_the_legacy_microseconds_as_milliseconds_quirk() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine();
    let cfg = HealthConfig {
        warning_tolerance: Duration::from_secs(30),
        ..HealthConfig::default()
    };
    engine.shared.health.configure(cfg);
    capture(&mut engine, spec(&file, 4096), None).observe(facts(false, true));
    let now = Instant::now();
    let mut diagnosis = engine.shared.health.activate(now).unwrap();
    assert_eq!(diagnosis.timeout_ms, 30_000_000);
    let (_, timeout) = diagnosis.iteration(cfg, now).unwrap();
    assert_eq!(timeout, Duration::from_secs(30));
    engine.shared.health.finish(diagnosis);
}

#[test]
fn shutdown_drains_accepted_observations_and_does_not_wait_for_the_observer() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = service();
    let mut waiter = service.read(spec(&file, 4096)).unwrap();
    let observer = waiter.take_final_observer().unwrap();
    let completion = waiter.wait(deadline()).unwrap();
    drop((waiter, completion));
    let receipt = observer.observe(facts(true, true));
    assert!(matches!(
        receipt.error,
        ObservationOutcome::Accepted | ObservationOutcome::Busy
    ));
    service.shutdown().unwrap();
    assert_eq!(
        observer.observe(facts(true, true)).error,
        ObservationOutcome::AlreadyObserved
    );
}

#[test]
fn an_empty_accepting_ingress_does_not_satisfy_shutdown() {
    let engine = engine();
    assert!(engine.shared.health.empty());
    assert!(!engine.shared.health.closed_and_empty());
    engine.shared.health.close();
    assert!(engine.shared.health.closed_and_empty());
}

#[test]
fn running_ignores_tolerance_shrink_then_draining_uses_the_live_boundary() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let mut cfg = config();
    cfg.health.warning_tolerance = Duration::from_secs(10);
    let mut engine = engine_with(cfg.clone());
    capture(&mut engine, spec(&file, 4096), None).observe(facts(true, false));
    let began = Instant::now();
    publish_probe(&mut engine);
    dispatch(&mut engine);
    let pending = *engine.active.keys().next().unwrap();
    let retired = super::retired(&engine);
    cfg.health.warning_tolerance = Duration::from_secs(1);
    engine
        .reconfigure(Generation(0), cfg.clone(), Instant::now())
        .unwrap();
    engine.health_turn(began + Duration::from_secs(7));
    assert!(!engine.snapshot().health.warning);
    engine.cancel(pending, CompletionStatus::TimedOut);
    cfg.health.warning_tolerance = Duration::from_secs(20);
    engine
        .reconfigure(Generation(1), cfg.clone(), Instant::now())
        .unwrap();
    engine.health_turn(began + Duration::from_secs(7));
    assert!(!engine.snapshot().health.warning);
    cfg.health.warning_tolerance = Duration::from_secs(1);
    engine
        .reconfigure(Generation(2), cfg, Instant::now())
        .unwrap();
    engine.health_turn(began + Duration::from_secs(7));
    assert!(engine.snapshot().health.warning);
    assert_eq!(engine.snapshot().health.active, 1);
    assert_eq!(engine.snapshot().awaiting_retirement, 1);
    engine.retired(retired.0, retired.1);
    engine.health_turn(Instant::now());
    assert_eq!(engine.snapshot().health.active, 0);
}

#[test]
fn wait_cap_is_captured_at_publication_and_survives_reconfiguration() {
    for shrink in [false, true] {
        let directory = Directory::new();
        let file = directory.file();
        file.set_len(3 * 1024 * 1024).unwrap();
        let mut cfg = config();
        cfg.health.warning_tolerance = Duration::from_secs(30);
        cfg.health.wait_cap = Duration::from_secs(if shrink { 30 } else { 1 });
        let mut engine = engine_with(cfg.clone());
        capture(&mut engine, spec(&file, 3 * 1024 * 1024), None).observe(facts(true, false));
        let began = Instant::now();
        publish_probe(&mut engine);
        dispatch(&mut engine);
        retire(&mut engine);
        dispatch(&mut engine);
        cfg.health.wait_cap = Duration::from_secs(if shrink { 1 } else { 30 });
        engine
            .reconfigure(Generation(0), cfg, Instant::now())
            .unwrap();
        engine.health_turn(began + Duration::from_secs(2));
        assert_eq!(engine.snapshot().health.failed_passes, u64::from(!shrink));
        assert_eq!(engine.snapshot().awaiting_retirement, 1);
        engine.shared.gate.lock().unwrap().accepting = false;
        engine.shared.health.close();
        engine.accept_probes_and_close();

        engine.health_turn(Instant::now());
        retire(&mut engine);
        engine.health_turn(Instant::now());
        assert_eq!(engine.snapshot().health.active, 0);
    }
}

#[cfg(target_os = "linux")]
#[test]
fn full_probe_progresses_on_native_route_under_normal_saturation() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(6 * 1024 * 1024).unwrap();
    let mut engine = engine();
    engine
        .register(DeviceConfig {
            id: OTHER,
            worker: backend(),
            native: Some(Backend::new(BackendKind::NativeAio, 4).unwrap()),
        })
        .unwrap();
    let mut normal = spec(&file, 6 * 1024 * 1024);
    normal.device = OTHER;
    let (_, results) = enqueue(&mut engine, normal, Direction::Read, &[]);
    dispatch(&mut engine);
    let mut request = spec(&file, 3 * 1024 * 1024);
    request.device = OTHER;
    capture(&mut engine, request, None).observe(facts(true, false));
    publish_probe(&mut engine);
    dispatch(&mut engine);
    assert_eq!(engine.snapshot().devices[1].physical_depth, [3, 1]);
    assert_eq!(engine.snapshot().devices[1].backend_envelopes[0], [0, 0]);
    let mut ranges = Vec::new();
    while !engine.active.is_empty() {
        let (id, retired) = engine
            .backend(OTHER, 1)
            .retire(deadline())
            .unwrap()
            .unwrap();
        if retired.operation.buffer().as_slice().len() <= 2 * 1024 * 1024 {
            ranges.push((
                retired.operation.offset(),
                retired.operation.remaining_len(),
            ));
        }
        engine.retired(id, retired);
        dispatch(&mut engine);
        engine.health_turn(Instant::now());
    }
    assert!(matches!(
        results.recv().unwrap().status,
        CompletionStatus::Complete
    ));
    assert_eq!(
        ranges,
        vec![(0, 2 * 1024 * 1024), (2 * 1024 * 1024, 1024 * 1024)]
    );
    assert_eq!(engine.snapshot().health.successful_passes, 1);
    assert_eq!(engine.snapshot().health.active, 0);
}

#[test]
fn successful_native_reads_observe_once_and_ineligible_requests_have_no_observer() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = service();
    let mut write = service.write(spec(&file, 4096), &[1; 4096]).unwrap();
    assert!(write.take_final_observer().is_none());
    drop(write.wait(deadline()).unwrap());
    drop(write);
    let mut request = spec(&file, 4096);
    request.class = RequestClass::Probe;
    let mut probe = service.read(request).unwrap();
    assert!(probe.take_final_observer().is_none());
    drop(probe.wait(deadline()).unwrap());
    drop(probe);
    let mut read = service.read(spec(&file, 4096)).unwrap();
    let observer = read.take_final_observer().unwrap();
    drop(read.wait(deadline()).unwrap());
    drop(read);
    assert_eq!(
        observer.observe(facts(false, false)),
        ObservationReceipt::default()
    );
    assert_eq!(
        observer.observe(facts(true, true)).error,
        ObservationOutcome::AlreadyObserved
    );
    assert_eq!(service.health_status().observations.accepted_error, 0);
    assert_eq!(service.health_status().observations.accepted_timeout, 0);
    service.shutdown().unwrap();
}

#[test]
fn rejected_health_configuration_keeps_the_accepted_generation_and_limits() {
    let mut engine = engine();
    let before = engine.snapshot();
    for (tolerance, blacklist, wait_cap) in [
        (0, 60_000_000, 120_000_000),
        (300_000_001, 60_000_000, 120_000_000),
        (5_000_000, 0, 120_000_000),
        (5_000_000, 60_000_000, 0),
    ] {
        let mut cfg = config();
        cfg.health = HealthConfig {
            warning_tolerance: Duration::from_micros(tolerance),
            blacklist_interval: Duration::from_micros(blacklist),
            wait_cap: Duration::from_micros(wait_cap),
        };
        assert!(matches!(
            engine.reconfigure(Generation(0), cfg, Instant::now()),
            Err(ConfigError::Invalid(_))
        ));
        assert_eq!(engine.snapshot(), before);
    }
}

#[test]
fn large_block_probe_covers_every_subregion_after_original_owner_closes() {
    let directory = Directory::new();
    let path = directory.0.join("blocks");
    let block_config = BlockConfig {
        block_size: 4 * 1024 * 1024,
        initial_size: 8 * 1024 * 1024,
        direct_io: false,
        punch_holes: false,
    };
    let owner = UnmarkedBlockFile::open(
        &path,
        block_config,
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
    )
    .unwrap()
    .finish_marking()
    .unwrap();
    let block = owner.allocate().unwrap();
    let region = owner.region(block, 0..4 * 1024 * 1024).unwrap();
    let (_, start, _) = region.file_region();
    let mut engine = engine();
    capture(
        &mut engine,
        RequestSpec::new(DEVICE, Target::Block(region), 4 * 1024 * 1024),
        None,
    )
    .observe(facts(true, false));
    owner.release(block).unwrap();
    drop(owner);
    assert!(UnmarkedBlockFile::open(
        &path,
        block_config,
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default()
    )
    .is_err());
    publish_probe(&mut engine);
    for relative in [0, 2 * 1024 * 1024] {
        dispatch(&mut engine);
        let (id, retired) = super::retired(&engine);
        assert_eq!(retired.operation.offset(), start + relative);
        assert_eq!(retired.operation.remaining_len(), 2 * 1024 * 1024);
        assert!(UnmarkedBlockFile::open(
            &path,
            block_config,
            SpaceBudget::default(),
            file_io::DirectoryDurability::Required,
            &mut file_io::DirectorySyncSummary::default()
        )
        .is_err());
        engine.retired(id, retired);
    }
    engine.health_turn(Instant::now());
    assert_eq!(engine.snapshot().health.successful_passes, 1);
    drop(
        UnmarkedBlockFile::open(
            &path,
            block_config,
            SpaceBudget::default(),
            file_io::DirectoryDurability::Required,
            &mut file_io::DirectorySyncSummary::default(),
        )
        .unwrap(),
    );
}

#[test]
fn pending_probe_admission_deadline_wakes_without_an_external_event() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(2 * 1024 * 1024).unwrap();
    let mut cfg = config();
    cfg.health.warning_tolerance = Duration::from_secs(1);
    let service = Service::start(cfg).unwrap();
    service
        .register_device(DeviceConfig {
            id: DEVICE,
            worker: backend(),
            native: None,
        })
        .unwrap();
    let mut occupied = spec(&file, 2 * 1024 * 1024);
    occupied.class = RequestClass::Probe;
    let mut probe = service.read(occupied).unwrap();
    let retained = probe.wait(deadline()).unwrap();
    let publication_limit = deadline();
    let observer = loop {
        let mut read = service.read(spec(&file, 4096)).unwrap();
        let observer = read.take_final_observer().unwrap();
        drop(read.wait(deadline()).unwrap());
        drop(read);
        match observer.observe(facts(true, false)).error {
            ObservationOutcome::Accepted => break observer,
            ObservationOutcome::Busy if Instant::now() < publication_limit => (),
            other => panic!("publication did not progress: {other:?}"),
        }
    };
    assert!(
        matches!(service.poll_completion(DEVICE, Instant::now()+Duration::from_secs(2)), Err(e) if e.kind()==io::ErrorKind::WouldBlock)
    );
    assert!(service.health_status().warning);
    drop((probe, retained));
    service.shutdown().unwrap();
    drop(observer);
}

#[test]
#[cfg_attr(
    feature = "ownership-test-support",
    allow(
        clippy::result_large_err,
        reason = "the proof-only owner enlarges Operation returned by the dispatch double"
    )
)]
fn every_probe_release_route_retires_the_work_owner_once() {
    for route in 0..3 {
        let directory = Directory::new();
        let file = directory.file();
        file.set_len(4096).unwrap();
        let mut engine = engine();
        let drops = Arc::new(AtomicUsize::new(0));
        let target_drops = Arc::clone(&drops);
        capture(
            &mut engine,
            spec(&file, 4096),
            Some(Box::new(move || {
                target_drops.fetch_add(1, Ordering::Relaxed);
            })),
        )
        .observe(facts(true, false));
        publish_probe(&mut engine);
        let id = *engine.active.keys().next().unwrap();
        match route {
            0 => {
                engine.cancel(id, CompletionStatus::Cancelled);
            }
            1 => {
                engine.dispatch_with(Instant::now(), |_, operation| {
                    Err(file_io::SubmitFailure {
                        error: io::Error::other("dispatch failed"),
                        operation,
                    })
                });
            }
            _ => {
                dispatch(&mut engine);
                let (_, physical) = super::retired(&engine);
                engine.cancel(id, CompletionStatus::TimedOut);
                assert_eq!(engine.snapshot().health.active, 1);
                drop(physical);
                engine.backend_failed(DEVICE, 0, io::Error::other("retirement reader failed"));
            }
        }
        engine.health_turn(Instant::now());
        assert_eq!(engine.snapshot().health.active, 0);
        assert_eq!(engine.snapshot().usage[1], ResourceUsage::default());
        assert_eq!(engine.snapshot().awaiting_retirement, 0);
        assert_eq!(drops.load(Ordering::Relaxed), 1);
    }
}

#[test]
fn invalid_probe_geometry_aborts_without_a_physical_event_or_lost_owner() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine();
    let request = RequestSpec::new(
        DEVICE,
        Target::File {
            file,
            offset: u64::MAX,
        },
        4096,
    );
    let drops = Arc::new(AtomicUsize::new(0));
    let released = Arc::clone(&drops);
    capture(
        &mut engine,
        request,
        Some(Box::new(move || {
            released.fetch_add(1, Ordering::Relaxed);
        })),
    )
    .observe(facts(true, false));
    publish_probe(&mut engine);
    assert_eq!(engine.snapshot().health.active, 0);
    assert_eq!(engine.snapshot().probe_attempts, 0);
    assert_eq!(engine.snapshot().usage[1], ResourceUsage::default());
    assert_eq!(drops.load(Ordering::Relaxed), 1);
}

#[test]
fn closing_device_keeps_internal_probe_progress_until_its_retirement() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let mut driver = engine();
    let observer = capture(&mut driver, spec(&file, 4096), None);
    let late = capture(&mut driver, spec(&file, 4096), None);
    assert_eq!(
        observer.observe(facts(true, false)).error,
        ObservationOutcome::Accepted
    );
    publish_probe(&mut driver);
    dispatch(&mut driver);
    assert_eq!(driver.snapshot().awaiting_retirement, 1);
    driver
        .remove_config(Generation(0), DEVICE, Instant::now())
        .unwrap();
    assert!(!driver.device_drained(DEVICE));
    assert_eq!(
        late.observe(facts(true, false)).error,
        ObservationOutcome::Stopped
    );
    retire(&mut driver);
    driver.health_turn(Instant::now());
    assert!(driver.device_drained(DEVICE));
    assert_eq!(driver.snapshot().health.active, 0);
    assert_eq!(driver.snapshot().usage[1], ResourceUsage::default());
}

#[test]
fn closing_one_device_does_not_wait_for_another_devices_probe() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let mut driver = engine();
    driver
        .register(DeviceConfig {
            id: OTHER,
            worker: backend(),
            native: None,
        })
        .unwrap();
    let mut request = spec(&file, 4096);
    request.device = OTHER;
    let observer = capture(&mut driver, request, None);
    observer.observe(facts(true, false));
    publish_probe(&mut driver);
    dispatch(&mut driver);
    driver
        .remove_config(Generation(0), DEVICE, Instant::now())
        .unwrap();
    assert!(driver.device_drained(DEVICE));
    assert!(!driver.device_drained(OTHER));
    let (id, event) = driver
        .backend(OTHER, 0)
        .retire(deadline())
        .unwrap()
        .unwrap();
    driver.retired(id, event);
    driver.health_turn(Instant::now());
    assert_eq!(driver.snapshot().health.active, 0);
}
