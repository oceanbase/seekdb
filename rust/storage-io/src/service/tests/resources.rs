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

fn retire_all(engine: &mut Engine) {
    let devices = engine.snapshot().devices;
    for device in devices {
        for (lane, usage) in device.backend_envelopes.into_iter().enumerate() {
            for _ in 0..usage.into_iter().sum::<usize>() {
                let (id, retired) = engine
                    .backend(device.id, lane)
                    .retire(deadline())
                    .unwrap()
                    .unwrap();
                engine.retired(id, retired);
            }
        }
    }
}

#[test]
fn write_borrow_is_copied_before_native_call_returns() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut source = vec![0x31; 8192];
    let mut waiter = service.write(spec(&file, source.len()), &source).unwrap();
    source.fill(0x79);
    drop(source);
    let completion = waiter.wait(deadline()).unwrap();
    assert!(matches!(completion.status, CompletionStatus::Complete));
    assert_eq!(completion.data().unwrap(), &[0x31; 8192]);
    let mut disk = vec![0; 8192];
    file_io::read_exact_at(&file, &mut disk, 0).unwrap();
    assert_eq!(disk, vec![0x31; 8192]);
    service.shutdown().unwrap();
}
#[test]
fn cancellation_during_admission_wakes_waiter_without_allocating_or_publishing() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut config = config();
    config.admission.max_bytes = config.admission.probe_bytes + 4096;
    service.reconfigure(Generation(0), config).unwrap();
    let held = prepare(&service, spec(&file, 4096), Direction::Read, &[]);
    let mut admission = service.begin(spec(&file, 4096).split().0).unwrap();
    let control = admission.take_control().unwrap();
    let ready = Arc::new(Barrier::new(2));
    let worker_ready = Arc::clone(&ready);
    let waiting = thread::spawn(move || {
        worker_ready.wait();
        admission.wait_capacity()
    });
    ready.wait();
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 4096);
    assert_eq!(control.request_cancel(), CancelOutcome::Cancelled);
    assert_eq!(
        waiting.join().unwrap().unwrap_err().kind(),
        io::ErrorKind::Interrupted
    );
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 4096);
    assert_eq!(service.snapshot().unwrap().admitted_requests, 2);
    drop(control);
    drop(held);
    assert_eq!(service.snapshot().unwrap().admitted_requests, 0);
    service.shutdown().unwrap();
}
#[test]
fn two_devices_share_bytes_through_retained_completion() {
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
    let mut config = config();
    config.admission.max_bytes = config.admission.probe_bytes + 8192;
    service.reconfigure(Generation(0), config).unwrap();
    let held = service
        .read(spec(&file, 8192))
        .unwrap()
        .wait(deadline())
        .unwrap();
    let mut other = spec(&file, 8192);
    other.device = OTHER;
    let (options, target) = other.split();
    let mut admission = service.begin(options).unwrap();
    let ready = Arc::new(Barrier::new(2));
    let worker_ready = Arc::clone(&ready);
    let waiting = thread::spawn(move || {
        worker_ready.wait();
        admission.wait_capacity().unwrap();
        admission.prepare(target, Direction::Read, &[]).unwrap();
        admission
    });
    ready.wait();
    let before = service.snapshot().unwrap();
    assert_eq!(before.reserved_bytes, 8192);
    assert_eq!(before.awaiting_retirement, 0);
    drop(held);
    let admission = waiting.join().unwrap();
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 8192);
    drop(admission);
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 0);
    service.shutdown().unwrap();
}
#[test]
fn live_shrink_reports_debt_and_growth_wakes_waiting_admission() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut config = config();
    config.admission.max_bytes = config.admission.probe_bytes + 12288;
    service.reconfigure(Generation(0), config.clone()).unwrap();
    let first = prepare(&service, spec(&file, 4096), Direction::Read, &[]);
    let second = prepare(&service, spec(&file, 4096), Direction::Read, &[]);
    let mut waiting = service.begin(spec(&file, 8192).split().0).unwrap();
    let started = Arc::new(Barrier::new(2));
    let worker_started = Arc::clone(&started);
    let waiter = thread::spawn(move || {
        worker_started.wait();
        waiting.wait_capacity().unwrap();
        waiting
    });
    started.wait();
    config.admission.max_bytes = config.admission.probe_bytes + 4096;
    let shrunk = service.reconfigure(Generation(1), config.clone()).unwrap();
    assert_eq!(shrunk.debt[0].bytes, 4096);
    let before = service.snapshot().unwrap();
    assert_eq!(before.reserved_bytes, 8192);
    assert_eq!(before.generation, Generation(2));
    let mut invalid = config.clone();
    invalid.devices.get_mut(&DEVICE).unwrap().probe_depth = 0;
    assert!(matches!(
        service.reconfigure(Generation(2), invalid),
        Err(ConfigError::Invalid(_))
    ));
    assert_eq!(service.snapshot().unwrap(), before);
    assert!(matches!(
        service.reconfigure(Generation(1), config.clone()),
        Err(ConfigError::Stale(Generation(2)))
    ));
    config.admission.max_bytes = config.admission.probe_bytes + 16384;
    service.reconfigure(Generation(2), config).unwrap();
    let waiting = waiter.join().unwrap();
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 16384);
    drop((first, second, waiting));
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 0);
    service.shutdown().unwrap();
}
#[test]
fn normal_and_probe_two_megabyte_requests_saturate_separate_depth_partitions() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine();
    let (_, normal) = enqueue(
        &mut engine,
        spec(&file, 2 * 1024 * 1024),
        Direction::Write,
        &vec![0x55; 2 * 1024 * 1024],
    );
    let mut probe = spec(&file, 2 * 1024 * 1024);
    probe.class = RequestClass::Probe;
    let (_, probe) = enqueue(&mut engine, probe, Direction::Read, &[]);
    dispatch(&mut engine);
    let snapshot = engine.snapshot();
    assert_eq!(snapshot.devices[0].physical_depth, [3, 1]);
    assert_eq!(snapshot.devices[0].backend_envelopes, vec![[1, 1]]);
    retire_all(&mut engine);
    let normal = normal.recv().unwrap();
    let probe = probe.recv().unwrap();
    assert_eq!(
        normal.depth_cost,
        DepthCost {
            raw: 4,
            effective: 3
        }
    );
    assert_eq!(
        probe.depth_cost,
        DepthCost {
            raw: 4,
            effective: 1
        }
    );
    assert_eq!(normal.physical_attempts, 1);
    assert_eq!(probe.physical_attempts, 1);
    assert_eq!(engine.snapshot().probe_attempts, 1);
    assert_eq!(engine.snapshot().devices[0].physical_depth, [0, 0]);
    assert_eq!(engine.snapshot().reserved_bytes, 4 * 1024 * 1024);
    let mut oversized = spec(&file, 2 * 1024 * 1024 + 4096);
    oversized.class = RequestClass::Probe;
    assert!(
        matches!(Admission::begin(Arc::clone(&engine.shared), oversized.split().0), Err(e) if e.kind() == io::ErrorKind::InvalidInput)
    );
}
#[test]
#[cfg(target_os = "linux")]
fn native_lane_progresses_while_worker_envelopes_are_retained() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(8192).unwrap();
    let mut engine = engine();
    engine
        .register(
            DeviceConfig {
                id: OTHER,
                worker: Backend::new(BackendKind::Worker { threads: 1 }, 2).unwrap(),
                native: Some(Backend::new(BackendKind::NativeAio, 2).unwrap()),
            },
            false,
        )
        .unwrap();
    let mut worker = spec(&file, 4096);
    worker.device = OTHER;
    worker.execution = Execution::WorkerRequired;
    let (_, worker_result) = enqueue(&mut engine, worker, Direction::Write, &[0x38; 4096]);
    dispatch(&mut engine);
    let mut worker = spec(&file, 4096);
    worker.device = OTHER;
    worker.execution = Execution::WorkerRequired;
    let (_, waiting_worker) = enqueue(&mut engine, worker, Direction::Read, &[]);
    let mut native = spec(&file, 4096);
    native.device = OTHER;
    let (_, native_result) = enqueue(&mut engine, native, Direction::Read, &[]);
    dispatch(&mut engine);
    let snapshot = engine.snapshot();
    let device = snapshot.devices.iter().find(|d| d.id == OTHER).unwrap();
    assert_eq!(device.backend_envelopes, vec![[1, 0], [1, 0]]);
    assert_eq!(device.physical_depth, [2, 0]);
    let (id, retired) = engine
        .backend(OTHER, 1)
        .retire(deadline())
        .unwrap()
        .unwrap();
    engine.retired(id, retired);
    assert!(matches!(
        native_result.recv().unwrap().status,
        CompletionStatus::Complete
    ));
    assert!(worker_result.try_recv().is_err());
    assert!(waiting_worker.try_recv().is_err());
    let (id, retired) = engine
        .backend(OTHER, 0)
        .retire(deadline())
        .unwrap()
        .unwrap();
    engine.retired(id, retired);
    dispatch(&mut engine);
    retire_all(&mut engine);
    assert!(matches!(
        waiting_worker.recv().unwrap().status,
        CompletionStatus::Complete
    ));
}
#[test]
fn every_backend_reserves_one_probe_envelope_independently_of_device_depth() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine();
    engine
        .register(
            DeviceConfig {
                id: OTHER,
                worker: Backend::new(BackendKind::Worker { threads: 1 }, 2).unwrap(),
                native: None,
            },
            false,
        )
        .unwrap();
    let mut normal = spec(&file, 4096);
    normal.device = OTHER;
    let (_, first) = enqueue(&mut engine, normal, Direction::Read, &[]);
    let mut normal = spec(&file, 4096);
    normal.device = OTHER;
    let (_, second) = enqueue(&mut engine, normal, Direction::Read, &[]);
    let mut probe = spec(&file, 4096);
    probe.device = OTHER;
    probe.class = RequestClass::Probe;
    let (_, detector) = enqueue(&mut engine, probe, Direction::Read, &[]);
    dispatch(&mut engine);
    let snapshot = engine.snapshot();
    let d = snapshot.devices.iter().find(|d| d.id == OTHER).unwrap();
    assert_eq!(d.backend_envelopes, vec![[1, 1]]);
    assert_eq!(snapshot.queued_requests, 1);
    retire_all(&mut engine);
    assert!(first.try_recv().is_ok());
    assert!(detector.try_recv().is_ok());
    assert!(second.try_recv().is_err());
    dispatch(&mut engine);
    retire_all(&mut engine);
    assert!(second.try_recv().is_ok());
}
#[test]
fn global_group_maximum_is_shared_across_two_devices_and_survives_refresh() {
    let directory = Directory::new();
    let file = directory.file();
    let mut config = config();
    config.groups.get_mut(&GROUP).unwrap().maximum_iops = Rate::PerSecond(2);
    config.groups.get_mut(&GROUP).unwrap().burst = 2;
    let mut engine = engine_with(config.clone());
    engine
        .register(
            DeviceConfig {
                id: OTHER,
                worker: backend(),
                native: None,
            },
            false,
        )
        .unwrap();
    let mut results = Vec::new();
    for device in [DEVICE, OTHER, DEVICE, OTHER] {
        let mut request = spec(&file, 4);
        request.device = device;
        results.push(enqueue(&mut engine, request, Direction::Read, &[]).1);
    }
    let now = Instant::now();
    engine.dispatch(now);
    assert_eq!(engine.snapshot().groups[0].physical_attempts, 2);
    retire_all(&mut engine);
    engine.reconfigure(Generation(0), config, now).unwrap();
    engine.dispatch(now);
    assert_eq!(engine.snapshot().groups[0].physical_attempts, 2);
    engine.dispatch(now + Duration::from_millis(499));
    assert_eq!(engine.snapshot().groups[0].physical_attempts, 2);
    engine.dispatch(now + Duration::from_millis(500));
    assert_eq!(engine.snapshot().groups[0].physical_attempts, 3);
    retire_all(&mut engine);
    engine.dispatch(now + Duration::from_secs(1));
    assert_eq!(engine.snapshot().groups[0].physical_attempts, 4);
    retire_all(&mut engine);
    for result in results {
        assert!(result.try_recv().is_ok());
    }
}
#[test]
#[cfg_attr(
    feature = "ownership-test-support",
    allow(
        clippy::result_large_err,
        reason = "the proof-only owner enlarges Operation returned by the dispatch double"
    )
)]
fn retry_reacquires_depth_and_debits_only_successful_physical_attempts() {
    let directory = Directory::new();
    let file = directory.file();
    file_io::write_all_at(&file, b"abcd", 0).unwrap();
    let mut config = config();
    config.groups.get_mut(&GROUP).unwrap().maximum_iops = Rate::PerSecond(1);
    let mut engine = engine_with(config);
    let policy = RequestPolicy {
        retry_alignment: NonZeroUsize::new(4),
        ..RequestPolicy::default()
    };
    let (_, result) = enqueue(
        &mut engine,
        read(&file, 8, 0..8, policy),
        Direction::Read,
        &[],
    );
    let now = Instant::now();
    engine.dispatch_with(now, |_, operation| {
        Err(file_io::SubmitFailure {
            operation,
            error: io::ErrorKind::WouldBlock.into(),
        })
    });
    assert_eq!(engine.snapshot().groups[0].physical_attempts, 0);
    assert_eq!(engine.snapshot().devices[0].physical_depth, [0, 0]);
    engine.dispatch(now);
    retire(&mut engine);
    assert_eq!(engine.snapshot().devices[0].physical_depth, [0, 0]);
    engine.dispatch(now);
    assert_eq!(engine.snapshot().awaiting_retirement, 0);
    assert!(result.try_recv().is_err());
    engine.dispatch(now + Duration::from_secs(1));
    assert_eq!(engine.snapshot().devices[0].physical_depth, [1, 0]);
    retire(&mut engine);
    let completion = result.recv().unwrap();
    assert_eq!(completion.physical_attempts, 2);
    assert_eq!(completion.data().unwrap(), b"abcd");
}
#[test]
fn zero_maximum_and_zero_excess_still_allow_exempt_and_probe_progress() {
    let directory = Directory::new();
    let file = directory.file();
    let mut config = config();
    let group = config.groups.get_mut(&GROUP).unwrap();
    group.maximum_iops = Rate::PerSecond(0);
    group.excess = ExcessShare::None;
    let mut engine = engine_with(config);
    let (_, normal) = enqueue(&mut engine, spec(&file, 4), Direction::Read, &[]);
    let mut exempt = spec(&file, 4);
    exempt.rate_exempt = true;
    let (_, exempt) = enqueue(&mut engine, exempt, Direction::Read, &[]);
    let mut probe = spec(&file, 4);
    probe.class = RequestClass::Probe;
    let (_, probe) = enqueue(&mut engine, probe, Direction::Read, &[]);
    dispatch(&mut engine);
    retire_all(&mut engine);
    assert!(normal.try_recv().is_err());
    assert!(exempt.try_recv().is_ok());
    assert!(probe.try_recv().is_ok());
    assert_eq!(engine.snapshot().groups[0].rate_exempt_attempts, 1);
    assert_eq!(engine.snapshot().probe_attempts, 1);
}
#[test]
fn weighted_excess_and_eligible_minimum_credit_use_a_monotonic_clock() {
    let other = GroupId(2);
    let minimum = GroupId(3);
    let mut config = config();
    config.groups.insert(
        other,
        GroupLimits {
            excess: ExcessShare::Weighted(std::num::NonZeroU32::new(3).unwrap()),
            ..GroupLimits::default()
        },
    );
    config.groups.insert(
        minimum,
        GroupLimits {
            minimum_iops: 2,
            maximum_iops: Rate::PerSecond(2),
            excess: ExcessShare::None,
            burst: 1,
        },
    );
    let now = Instant::now();
    let mut scheduler = scheduler::Scheduler::new(&config, now);
    let eligible = [GROUP, other].into();
    scheduler.tick(now, &eligible);
    let mut counts = BTreeMap::new();
    for _ in 0..400 {
        let id = scheduler.choose(&eligible).unwrap();
        *counts.entry(id).or_insert(0) += 1;
        scheduler.charge(id);
    }
    assert_eq!(counts[&GROUP], 100);
    assert_eq!(counts[&other], 300);
    let eligible = [minimum].into();
    scheduler.tick(now + Duration::from_secs(10), &eligible);
    assert_eq!(scheduler.choose(&eligible), None);
    scheduler.tick(now + Duration::from_millis(10499), &eligible);
    assert_eq!(scheduler.choose(&eligible), None);
    scheduler.tick(now + Duration::from_millis(10500), &eligible);
    assert_eq!(scheduler.choose(&eligible), Some(minimum));
    scheduler.charge(minimum);
    assert_eq!(scheduler.choose(&eligible), None);
}
#[test]
fn finite_wait_does_not_cancel_and_last_waiter_drop_does() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine();
    let mut admission = core_prepare(&mut engine, spec(&file, 4), Direction::Write, b"late");
    let control = admission.take_control().unwrap();
    let (destination, completed) = mpsc::channel();
    let id = admission.publish(destination).unwrap();
    drop(admission);
    engine.drain_gate();
    let mut waiter = Waiter {
        lifetime: RequestLifetime::default(),
        control: Some(control),
        completed,
        id,
    };
    assert!(waiter.wait_until(Instant::now()).unwrap().is_none());
    engine.controls(Instant::now());
    assert_eq!(engine.snapshot().queued_requests, 1);
    dispatch(&mut engine);
    retire(&mut engine);
    assert!(matches!(
        waiter.wait(deadline()).unwrap().status,
        CompletionStatus::Complete
    ));
    let mut admission = core_prepare(&mut engine, spec(&file, 4), Direction::Write, b"nope");
    let control = admission.take_control().unwrap();
    let (destination, completed) = mpsc::channel();
    let id = admission.publish(destination).unwrap();
    drop(admission);
    engine.drain_gate();
    drop(Waiter {
        lifetime: RequestLifetime::default(),
        control: Some(control),
        completed,
        id,
    });
    engine.controls(Instant::now());
    dispatch(&mut engine);
    let mut disk = [0; 4];
    file_io::read_exact_at(&file, &mut disk, 0).unwrap();
    assert_eq!(&disk, b"late");
    assert_eq!(engine.snapshot().queued_requests, 0);
}
#[test]
fn publish_and_stop_choose_exactly_one_delivery_contract() {
    for accepted in [false, true] {
        let directory = Directory::new();
        let file = directory.file();
        let mut engine = engine();
        let mut admission = core_prepare(&mut engine, spec(&file, 4), Direction::Write, b"late");
        let (destination, completed) = mpsc::channel();
        if accepted {
            admission.publish(destination).unwrap();
            engine.shared.gate.lock().unwrap().accepting = false;
        } else {
            engine.shared.gate.lock().unwrap().accepting = false;
            assert!(admission.publish(destination).is_err());
        }
        drop(admission);
        engine.drain_gate();
        engine.controls(Instant::now());
        dispatch(&mut engine);
        if accepted {
            assert!(matches!(
                completed.recv().unwrap().status,
                CompletionStatus::Shutdown
            ));
        }
        assert!(completed.try_recv().is_err());
        assert_eq!(file.metadata().unwrap().len(), 0);
        assert_eq!(engine.snapshot().reserved_bytes, 0);
    }
}
#[test]
fn removing_a_device_preserves_another_devices_progress() {
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
    let held = prepare(&service, spec(&file, 4), Direction::Read, &[]);
    service.remove_device(DEVICE).unwrap();
    assert!(service.read(spec(&file, 4)).is_err());
    let mut request = spec(&file, 4);
    request.device = OTHER;
    assert!(matches!(
        service
            .read(request)
            .unwrap()
            .wait(deadline())
            .unwrap()
            .status,
        CompletionStatus::ShortIo
    ));
    drop(held);
    service.shutdown().unwrap();
}
#[test]
fn control_and_completion_keep_a_ledger_after_actor_shutdown() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut admission = prepare(&service, spec(&file, 4), Direction::Read, &[]);
    let control = admission.take_control().unwrap();
    let (destination, completed) = mpsc::channel();
    admission.publish(destination).unwrap();
    drop(admission);
    let completion = completed.recv_timeout(Duration::from_secs(5)).unwrap();
    let ledger = Arc::clone(&service.shared.ledger);
    service.shutdown().unwrap();
    assert_eq!(control.request_cancel(), CancelOutcome::NotPending);
    drop(completion);
    assert_eq!(ledger.lock().unwrap().usage[0].records, 1);
    drop(control);
    assert_eq!(ledger.lock().unwrap().usage[0], ResourceUsage::default());
}

#[test]
fn full_management_queue_cannot_block_retirement_cancellation_or_stop() {
    let directory = Directory::new();
    let file = directory.file();
    let config = Arc::new(config());
    let (management, commands) = mpsc::sync_channel(32);
    let (retirement, retired) = mpsc::channel();
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
    let mut engine = Engine::new(Arc::clone(&shared));
    engine
        .register(
            DeviceConfig {
                id: DEVICE,
                worker: backend(),
                native: None,
            },
            true,
        )
        .unwrap();
    let mut admission = core_prepare(&mut engine, spec(&file, 4), Direction::Write, b"late");
    let control = admission.take_control().unwrap();
    let (destination, completed) = mpsc::channel();
    admission.publish(destination).unwrap();
    drop(admission);
    engine.drain_gate();
    dispatch(&mut engine);
    let mut request = spec(&file, 4);
    request.target = Target::File {
        file: Arc::clone(&file),
        offset: 4,
    };
    let (_, queued) = enqueue(&mut engine, request, Direction::Write, b"nope");
    let mut snapshots = Vec::new();
    for _ in 0..32 {
        let (reply, result) = mpsc::channel();
        shared
            .management
            .try_send(Management::Snapshot(reply))
            .unwrap();
        snapshots.push(result);
    }
    let (reply, _) = mpsc::channel();
    assert!(matches!(
        shared.management.try_send(Management::Snapshot(reply)),
        Err(mpsc::TrySendError::Full(_))
    ));
    assert_eq!(control.request_cancel(), CancelOutcome::Cancelled);
    shared.gate.lock().unwrap().accepting = false;
    let actor = thread::spawn(move || engine.run(commands, retired));
    let completion = completed.recv_timeout(Duration::from_secs(5)).unwrap();
    assert!(matches!(
        completion.status,
        CompletionStatus::Cancelled | CompletionStatus::Shutdown | CompletionStatus::Complete
    ));
    drop(completion);
    actor.join().unwrap().unwrap();
    drop(control);
    assert!(matches!(
        queued.recv().unwrap().status,
        CompletionStatus::Shutdown
    ));
    assert_eq!(file.metadata().unwrap().len(), 4);
    assert!(completed.try_recv().is_err());
    assert_eq!(
        shared.ledger.lock().unwrap().usage[0],
        ResourceUsage::default()
    );
    drop(snapshots);
}
#[test]
fn probe_reserves_records_bytes_and_depth_when_normal_is_full() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(2 * 1024 * 1024).unwrap();
    let mut config = config();
    config.admission.max_records = 2;
    config.admission.probe_records = 1;
    config.admission.max_bytes = 4 * 1024 * 1024;
    let mut engine = engine_with(config);
    let (_, normal) = enqueue(
        &mut engine,
        spec(&file, 2 * 1024 * 1024),
        Direction::Read,
        &[],
    );
    dispatch(&mut engine);
    assert!(
        matches!(Admission::begin(Arc::clone(&engine.shared), spec(&file, 4096).split().0), Err(e) if e.kind() == io::ErrorKind::WouldBlock)
    );
    let mut probe = spec(&file, 2 * 1024 * 1024);
    probe.class = RequestClass::Probe;
    let (_, detector) = enqueue(&mut engine, probe, Direction::Read, &[]);
    dispatch(&mut engine);
    let mut extra_probe = spec(&file, 4096);
    extra_probe.class = RequestClass::Probe;
    assert!(
        matches!(Admission::begin(Arc::clone(&engine.shared), extra_probe.split().0), Err(e) if e.kind() == io::ErrorKind::WouldBlock)
    );
    assert_eq!(engine.snapshot().devices[0].physical_depth, [3, 1]);
    assert_eq!(engine.snapshot().reserved_bytes, 4 * 1024 * 1024);
    retire_all(&mut engine);
    assert!(matches!(
        normal.recv().unwrap().status,
        CompletionStatus::Complete
    ));
    assert!(matches!(
        detector.recv().unwrap().status,
        CompletionStatus::Complete
    ));
}
#[test]
#[cfg(target_os = "linux")]
fn native_two_megabyte_probe_uses_its_reserved_depth_at_total_depth_four() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(2 * 1024 * 1024).unwrap();
    let mut engine = engine();
    engine
        .register(
            DeviceConfig {
                id: OTHER,
                worker: backend(),
                native: Some(Backend::new(BackendKind::NativeAio, 4).unwrap()),
            },
            false,
        )
        .unwrap();
    let mut normal = spec(&file, 2 * 1024 * 1024);
    normal.device = OTHER;
    let (_, normal) = enqueue(&mut engine, normal, Direction::Read, &[]);
    dispatch(&mut engine);
    let mut probe = spec(&file, 2 * 1024 * 1024);
    probe.device = OTHER;
    probe.class = RequestClass::Probe;
    let (_, detector) = enqueue(&mut engine, probe, Direction::Read, &[]);
    dispatch(&mut engine);
    let snapshot = engine.snapshot();
    let device = snapshot.devices.iter().find(|d| d.id == OTHER).unwrap();
    assert_eq!(device.physical_depth, [3, 1]);
    assert_eq!(device.backend_envelopes[1], [1, 1]);
    retire_all(&mut engine);
    assert!(matches!(
        normal.recv().unwrap().status,
        CompletionStatus::Complete
    ));
    assert!(matches!(
        detector.recv().unwrap().status,
        CompletionStatus::Complete
    ));
}
#[test]
fn increasing_probe_depth_reports_old_normal_debt_until_retirement() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine();
    let (_, result) = enqueue(
        &mut engine,
        spec(&file, 2 * 1024 * 1024),
        Direction::Read,
        &[],
    );
    dispatch(&mut engine);
    let mut config = config();
    config.devices.get_mut(&DEVICE).unwrap().probe_depth = 3;
    let receipt = engine
        .reconfigure(Generation(0), config.clone(), Instant::now())
        .unwrap();
    assert!(receipt.physical_debt);
    assert_eq!(engine.snapshot().devices[0].depth_debt, [2, 0]);
    retire_all(&mut engine);
    drop(result.recv().unwrap());
    assert_eq!(engine.snapshot().devices[0].depth_debt, [0, 0]);
    assert!(
        !engine
            .reconfigure(receipt.generation, config, Instant::now())
            .unwrap()
            .physical_debt
    );
}
#[test]
fn invalid_minimums_fail_without_changing_any_generation_or_limit() {
    let service = service();
    let before = service.snapshot().unwrap();
    let mut config = before.config.clone();
    config.groups.get_mut(&GROUP).unwrap().minimum_iops = 2;
    config.group_capacity_iops = Rate::PerSecond(1);
    assert!(matches!(
        service.reconfigure(before.generation, config),
        Err(ConfigError::Invalid(_))
    ));
    assert_eq!(service.snapshot().unwrap(), before);
    service.shutdown().unwrap();
}

#[test]
fn unchanged_refresh_preserves_earned_minimum_credit_without_a_new_burst() {
    let mut config = config();
    config.groups.get_mut(&GROUP).unwrap().minimum_iops = 1;
    config.groups.get_mut(&GROUP).unwrap().maximum_iops = Rate::PerSecond(1);
    config.groups.get_mut(&GROUP).unwrap().excess = ExcessShare::None;
    let now = Instant::now();
    let eligible = [GROUP].into();
    let mut scheduler = scheduler::Scheduler::new(&config, now);
    scheduler.tick(now, &eligible);
    scheduler.tick(now + Duration::from_secs(1), &eligible);
    scheduler.reconfigure(&config, now + Duration::from_secs(1));
    scheduler.tick(now + Duration::from_secs(1), &eligible);
    assert_eq!(scheduler.choose(&eligible), Some(GROUP));
    scheduler.charge(GROUP);
    scheduler.reconfigure(&config, now + Duration::from_secs(1));
    scheduler.tick(now + Duration::from_secs(1), &eligible);
    assert_eq!(scheduler.choose(&eligible), None);
}
#[test]
fn simultaneous_publish_and_stop_never_lose_an_accepted_destination() {
    for _ in 0..16 {
        let directory = Directory::new();
        let file = directory.file();
        let mut engine = engine();
        let mut admission = core_prepare(&mut engine, spec(&file, 4), Direction::Write, b"late");
        let shared = Arc::clone(&engine.shared);
        let ready = Arc::new(Barrier::new(2));
        let stopping = Arc::clone(&ready);
        let stop = thread::spawn(move || {
            stopping.wait();
            shared.gate.lock().unwrap().accepting = false;
            shared.wake.notify();
        });
        let (destination, completed) = mpsc::channel();
        ready.wait();
        let published = admission.publish(destination);
        drop(admission);
        stop.join().unwrap();
        engine.drain_gate();
        engine.controls(Instant::now());
        dispatch(&mut engine);
        if let Ok(id) = published {
            let completion = completed.recv().unwrap();
            assert_eq!(completion.id, id);
            assert!(matches!(completion.status, CompletionStatus::Shutdown));
        }
        assert!(completed.try_recv().is_err());
        assert_eq!(engine.snapshot().reserved_bytes, 0);
        assert_eq!(file.metadata().unwrap().len(), 0);
    }
}

#[test]
fn immediate_admission_reserves_without_actor_progress_and_keeps_final_charge() {
    let directory = Directory::new();
    let file = directory.file();
    let engine = engine();
    let mut admission =
        Admission::begin(Arc::clone(&engine.shared), spec(&file, 8).split().0).unwrap();
    let lifetime = admission.lifetime();
    assert!(
        admission.ready().unwrap(),
        "free capacity must not need actor progress"
    );
    assert_eq!(engine.snapshot().reserved_bytes, 8);
    drop(admission);
    assert_eq!(engine.snapshot().reserved_bytes, 8);
    assert_eq!(engine.snapshot().admitted_requests, 1);
    drop(lifetime);
    assert_eq!(engine.snapshot().reserved_bytes, 0);
    assert_eq!(engine.snapshot().admitted_requests, 0);
}

#[test]
fn smaller_admission_cannot_bypass_an_older_blocked_head() {
    let directory = Directory::new();
    let file = directory.file();
    let mut config = config();
    config.admission.max_bytes = config.admission.probe_bytes + 8;
    let mut engine = engine_with(config);
    let held = core_prepare(&mut engine, spec(&file, 4), Direction::Read, &[]);
    let mut head = Admission::begin(Arc::clone(&engine.shared), spec(&file, 8).split().0).unwrap();
    engine.drain_gate();
    engine.grant_admission();
    assert!(!head.ready().unwrap());
    let mut smaller =
        Admission::begin(Arc::clone(&engine.shared), spec(&file, 4).split().0).unwrap();
    assert!(!smaller.ready().unwrap());
    engine.drain_gate();
    engine.grant_admission();
    assert!(!smaller.ready().unwrap());
    let mut request = spec(&file, 4);
    request.class = RequestClass::Probe;
    let mut probe = Admission::begin(Arc::clone(&engine.shared), request.split().0).unwrap();
    assert!(!probe.ready().unwrap());
    engine.grant_admission();
    assert!(probe.ready().unwrap());
    assert!(!head.ready().unwrap());
    assert!(!smaller.ready().unwrap());
    drop(held);
    engine.grant_admission();
    assert!(head.ready().unwrap());
    assert!(!smaller.ready().unwrap());
    drop(head);
    engine.grant_admission();
    assert!(smaller.ready().unwrap());
}

#[test]
fn immediate_admission_still_checks_cancellation_deadline_and_device_close() {
    let directory = Directory::new();
    let file = directory.file();
    let engine = engine();
    let mut cancelled =
        Admission::begin(Arc::clone(&engine.shared), spec(&file, 4).split().0).unwrap();
    let control = cancelled.take_control().unwrap();
    control.request_cancel();
    assert_eq!(
        cancelled.wait_capacity().unwrap_err().kind(),
        io::ErrorKind::Interrupted
    );
    let mut request = spec(&file, 4);
    request.policy.deadline = Some(Instant::now());
    let mut expired = Admission::begin(Arc::clone(&engine.shared), request.split().0).unwrap();
    assert_eq!(
        expired.wait_capacity().unwrap_err().kind(),
        io::ErrorKind::TimedOut
    );
    let mut closed =
        Admission::begin(Arc::clone(&engine.shared), spec(&file, 4).split().0).unwrap();
    engine
        .shared
        .gate
        .lock()
        .unwrap()
        .devices
        .insert(DEVICE, false);
    assert_eq!(
        closed.wait_capacity().unwrap_err().kind(),
        io::ErrorKind::BrokenPipe
    );
}

#[test]
fn dropped_waiting_admissions_cannot_grow_metadata_beyond_the_record_partition() {
    let directory = Directory::new();
    let file = directory.file();
    let count = config().admission.max_records - config().admission.probe_records;
    let mut config = config();
    config.admission.max_bytes = config.admission.probe_bytes + 4;
    let mut engine = engine_with(config);
    let held = core_prepare(&mut engine, spec(&file, 4), Direction::Read, &[]);
    for _ in 0..count - 1 {
        drop(Admission::begin(Arc::clone(&engine.shared), spec(&file, 4).split().0).unwrap());
    }
    assert!(
        matches!(Admission::begin(Arc::clone(&engine.shared), spec(&file, 4).split().0), Err(e) if e.kind() == io::ErrorKind::WouldBlock)
    );
    assert_eq!(engine.snapshot().admitted_requests, count);
    engine.controls(Instant::now());
    assert_eq!(engine.snapshot().admitted_requests, 1);
    let head = Admission::begin(Arc::clone(&engine.shared), spec(&file, 4).split().0).unwrap();
    engine.drain_gate();
    engine.grant_admission();
    for _ in 0..count - 2 {
        drop(Admission::begin(Arc::clone(&engine.shared), spec(&file, 4).split().0).unwrap());
    }
    engine.drain_gate();
    engine.controls(Instant::now());
    assert_eq!(engine.snapshot().admitted_requests, 2);
    drop((head, held));
    engine.controls(Instant::now());
    assert_eq!(engine.snapshot().admitted_requests, 0);
}

#[test]
fn minimum_only_groups_rotate_when_one_normal_slot_retires_each_turn() {
    let directory = Directory::new();
    let file = directory.file();
    let other = GroupId(2);
    let mut config = config();
    let policy = GroupLimits {
        minimum_iops: 1,
        maximum_iops: Rate::PerSecond(1),
        excess: ExcessShare::None,
        burst: 1,
    };
    config.groups.insert(GROUP, policy);
    config.groups.insert(other, policy);
    config.devices.get_mut(&DEVICE).unwrap().depth = 2;
    let mut engine = engine_with(config);
    let mut results = Vec::new();
    for group in [GROUP, GROUP, other, other] {
        let mut request = spec(&file, 4);
        request.group = group;
        results.push(enqueue(&mut engine, request, Direction::Read, &[]).1);
    }
    let now = Instant::now();
    engine.dispatch(now);
    for turn in 1..=4 {
        let clock = now + Duration::from_secs(turn);
        engine.dispatch(clock);
        assert_eq!(engine.snapshot().awaiting_retirement, 1);
        let groups = engine.snapshot().groups;
        assert_eq!(groups[0].physical_attempts, turn.div_ceil(2));
        assert_eq!(groups[1].physical_attempts, turn / 2);
        retire_all(&mut engine);
        engine.dispatch(clock);
    }
    for result in results {
        assert!(result.try_recv().is_ok());
    }
}

#[test]
fn inactive_excess_groups_do_not_give_newcomers_historical_service() {
    for inactive in [
        GroupLimits {
            minimum_iops: 1,
            maximum_iops: Rate::PerSecond(1),
            excess: ExcessShare::None,
            burst: 1,
        },
        GroupLimits {
            maximum_iops: Rate::PerSecond(0),
            ..GroupLimits::default()
        },
        GroupLimits {
            maximum_iops: Rate::PerSecond(1),
            ..GroupLimits::default()
        },
    ] {
        let directory = Directory::new();
        let file = directory.file();
        file_io::write_all_at(&file, b"data", 0).unwrap();
        let newcomer = GroupId(2);
        let blocked = GroupId(3);
        let mut config = config();
        config.groups.insert(newcomer, GroupLimits::default());
        config.groups.insert(blocked, inactive);
        config.devices.get_mut(&DEVICE).unwrap().depth = 2;
        let mut engine = engine_with(config);
        engine
            .register(
                DeviceConfig {
                    id: OTHER,
                    worker: backend(),
                    native: None,
                },
                false,
            )
            .unwrap();
        let mut blocked_results = Vec::new();
        for _ in 0..2 {
            let mut request = spec(&file, 4);
            request.device = OTHER;
            request.group = blocked;
            blocked_results.push(enqueue(&mut engine, request, Direction::Read, &[]).1);
        }
        let now = Instant::now();
        for _ in 0..100 {
            let (_, result) = enqueue(&mut engine, spec(&file, 4), Direction::Read, &[]);
            engine.dispatch(now);
            retire_all(&mut engine);
            assert_eq!(result.try_recv().unwrap().data().unwrap(), b"data");
        }
        let (_, old_result) = enqueue(&mut engine, spec(&file, 4), Direction::Read, &[]);
        let mut newcomer_results = Vec::new();
        for _ in 0..20 {
            let mut request = spec(&file, 4);
            request.group = newcomer;
            newcomer_results.push(enqueue(&mut engine, request, Direction::Read, &[]).1);
        }
        for _ in 0..2 {
            engine.dispatch(now);
            retire_all(&mut engine);
        }
        assert_eq!(old_result.try_recv().unwrap().data().unwrap(), b"data");
        assert_eq!(
            newcomer_results
                .iter()
                .filter_map(|r| r.try_recv().ok())
                .count(),
            1
        );
    }
}

#[test]
fn returning_excess_groups_resume_at_current_service_time() {
    for via_config in [false, true] {
        let returning = GroupId(0);
        let mut config = config();
        config.groups.insert(
            returning,
            GroupLimits {
                maximum_iops: Rate::PerSecond(100),
                burst: 10,
                ..GroupLimits::default()
            },
        );
        let now = Instant::now();
        let eligible = [GROUP, returning].into();
        let mut scheduler = scheduler::Scheduler::new(&config, now);
        scheduler.tick(now, &eligible);
        for _ in 0..20 {
            let group = scheduler.choose(&eligible).unwrap();
            scheduler.charge(group);
        }
        assert!(!scheduler.allowed(returning));
        for _ in 0..100 {
            assert_eq!(scheduler.choose(&eligible), Some(GROUP));
            scheduler.charge(GROUP);
        }
        let next = if via_config {
            config.groups.get_mut(&returning).unwrap().maximum_iops = Rate::Unlimited;
            scheduler.reconfigure(&config, now);
            now
        } else {
            now + Duration::from_secs(1)
        };
        scheduler.tick(next, &eligible);
        let mut selected = Vec::new();
        for _ in 0..2 {
            let group = scheduler.choose(&eligible).unwrap();
            selected.push(group);
            scheduler.charge(group);
        }
        selected.sort();
        assert_eq!(selected, vec![returning, GROUP]);
    }
}

#[test]
fn enabling_a_zero_rate_group_preserves_its_empty_bucket() {
    let mut config = config();
    config.groups.get_mut(&GROUP).unwrap().maximum_iops = Rate::PerSecond(0);
    let now = Instant::now();
    let eligible = [GROUP].into();
    let mut scheduler = scheduler::Scheduler::new(&config, now);
    scheduler.tick(now, &eligible);
    assert_eq!(scheduler.choose(&eligible), None);
    config.groups.get_mut(&GROUP).unwrap().maximum_iops = Rate::PerSecond(1);
    scheduler.reconfigure(&config, now);
    scheduler.tick(now, &eligible);
    assert_eq!(scheduler.choose(&eligible), None);
    scheduler.tick(now + Duration::from_secs(1), &eligible);
    assert_eq!(scheduler.choose(&eligible), Some(GROUP));
    scheduler.charge(GROUP);
    assert_eq!(scheduler.choose(&eligible), None);
}

#[test]
fn a_late_terminal_event_from_a_removed_device_preserves_other_device_reads() {
    let directory = Directory::new();
    let file = directory.file();
    file_io::write_all_at(&file, b"live", 0).unwrap();
    let service = service();
    service
        .register_device(DeviceConfig {
            id: OTHER,
            worker: backend(),
            native: None,
        })
        .unwrap();
    let ready = Arc::new(Barrier::new(2));
    let release = Arc::new(Barrier::new(2));
    let reader_ready = Arc::clone(&ready);
    let reader_release = Arc::clone(&release);
    let shared = Arc::clone(&service.shared);
    let late_reader = thread::spawn(move || {
        reader_ready.wait();
        reader_release.wait();
        shared
            .retirement
            .send(Retirement::Failed(
                DEVICE,
                0,
                io::ErrorKind::PermissionDenied.into(),
            ))
            .unwrap();
        shared.wake.notify();
    });
    ready.wait();
    service.remove_device(DEVICE).unwrap();
    let until = deadline();
    while service
        .snapshot()
        .unwrap()
        .devices
        .iter()
        .any(|d| d.id == DEVICE)
    {
        assert!(Instant::now() < until);
        thread::yield_now();
    }
    release.wait();
    late_reader.join().unwrap();
    let mut request = spec(&file, 4);
    request.device = OTHER;
    request.policy.deadline = Some(deadline());
    let completion = service.read(request).unwrap().wait(deadline()).unwrap();
    assert_eq!(completion.data().unwrap(), b"live");
    service.remove_device(DEVICE).unwrap();
    assert_eq!(
        service
            .remove_device(DeviceId(u64::MAX))
            .unwrap_err()
            .kind(),
        io::ErrorKind::NotFound
    );
    service.shutdown().unwrap();
}

#[test]
fn full_management_queue_cannot_prevent_device_close_or_release_physical_ownership() {
    let directory = Directory::new();
    let file = directory.file();
    file_io::write_all_at(&file, b"live", 0).unwrap();
    let mut config = config();
    config.admission.max_bytes = config.admission.probe_bytes + 8;
    let mut engine = engine_with(config);
    let (management, commands) = mpsc::sync_channel(32);
    let (retirement, retired) = mpsc::channel();
    let shared = Arc::get_mut(&mut engine.shared).unwrap();
    shared.management = management;
    shared.retirement = retirement;
    engine
        .register(
            DeviceConfig {
                id: OTHER,
                worker: backend(),
                native: None,
            },
            true,
        )
        .unwrap();
    let (_, closing_result) = enqueue(&mut engine, spec(&file, 4), Direction::Read, &[]);
    let mut request = spec(&file, 4);
    request.device = OTHER;
    let (_, other_result) = enqueue(&mut engine, request, Direction::Read, &[]);
    dispatch(&mut engine);
    let (submission, physical) = engine
        .backend(DEVICE, 0)
        .retire(deadline())
        .unwrap()
        .unwrap();
    let service = Service {
        shared: Arc::clone(&engine.shared),
        join: None,
    };
    let mut waiting = service.begin(spec(&file, 4).split().0).unwrap();
    let ready = Arc::new(Barrier::new(2));
    let waiter_ready = Arc::clone(&ready);
    let waiter = thread::spawn(move || {
        waiter_ready.wait();
        waiting.wait_capacity()
    });
    ready.wait();
    let mut snapshots = Vec::new();
    for _ in 0..32 {
        let (reply, result) = mpsc::channel();
        service
            .shared
            .management
            .try_send(Management::Snapshot(reply))
            .unwrap();
        snapshots.push(result);
    }
    assert!(matches!(service.snapshot(), Err(e) if e.kind() == io::ErrorKind::WouldBlock));
    service.remove_device(DEVICE).unwrap();
    service.remove_device(DEVICE).unwrap();
    assert!(
        matches!(service.begin(spec(&file, 4).split().0), Err(e) if e.kind() == io::ErrorKind::BrokenPipe)
    );
    assert_eq!(
        waiter.join().unwrap().unwrap_err().kind(),
        io::ErrorKind::BrokenPipe
    );
    let actor = thread::spawn(move || engine.run(commands, retired));
    let closing = closing_result.recv_timeout(Duration::from_secs(5)).unwrap();
    assert!(matches!(closing.status, CompletionStatus::Shutdown));
    let other = other_result.recv_timeout(Duration::from_secs(5)).unwrap();
    assert_eq!(other.data().unwrap(), b"live");
    drop((closing, other));
    snapshots
        .pop()
        .unwrap()
        .recv_timeout(Duration::from_secs(5))
        .unwrap();
    let last = service.snapshot().unwrap();
    assert_eq!(last.awaiting_retirement, 1);
    assert_eq!(last.reserved_bytes, 4);
    assert_eq!(
        last.devices
            .iter()
            .find(|d| d.id == DEVICE)
            .unwrap()
            .physical_depth,
        [1, 0]
    );
    service
        .shared
        .retirement
        .send(Retirement::Complete(DEVICE, 0, submission, physical))
        .unwrap();
    service.shared.wake.notify();
    let until = deadline();
    while service
        .snapshot()
        .unwrap()
        .devices
        .iter()
        .any(|d| d.id == DEVICE)
    {
        assert!(Instant::now() < until);
        thread::yield_now();
    }
    assert_eq!(service.snapshot().unwrap().reserved_bytes, 0);
    service.stop();
    actor.join().unwrap().unwrap();
}
