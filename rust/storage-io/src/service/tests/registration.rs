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

const MIB: usize = 1024 * 1024;

fn original() -> Config {
    let mut config = config();
    config.admission.max_bytes = 16 * MIB;
    config.devices.remove(&OTHER);
    config
}
fn expanded() -> Config {
    let mut config = original();
    config.admission.probe_bytes = 8 * MIB;
    config.devices.insert(
        OTHER,
        DeviceLimits {
            depth: 4,
            probe_depth: 1,
            max_probe_bytes: 8 * MIB,
        },
    );
    config
}
fn device(id: DeviceId) -> DeviceConfig {
    DeviceConfig {
        id,
        worker: backend(),
        native: None,
    }
}

#[test]
fn failed_registration_preserves_existing_and_waiting_normal_capacity() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine_with(original());
    let held = core_prepare(&mut engine, spec(&file, 4 * MIB), Direction::Read, &[]);
    let mut waiting =
        Admission::begin(Arc::clone(&engine.shared), spec(&file, 12 * MIB).split().0).unwrap();
    engine.drain_gate();
    engine.grant_admission();
    let before = engine.snapshot();
    assert_eq!(before.reserved_bytes, 4 * MIB);
    assert_eq!(before.admitted_requests, 2);
    engine.reader_start_failure = Some(0);
    assert!(matches!(
        engine.register_with_config(Generation(0), expanded(), device(OTHER), Instant::now(),),
        Err(ConfigError::Invalid(_))
    ));
    assert_eq!(engine.snapshot(), before);
    drop(held);
    engine.grant_admission();
    waiting.wait_capacity().unwrap();
    waiting
        .prepare(target(&file), Direction::Read, &[])
        .unwrap();
    assert_eq!(engine.snapshot().reserved_bytes, 12 * MIB);
    drop(waiting);
    let another = core_prepare(&mut engine, spec(&file, 12 * MIB), Direction::Read, &[]);
    assert_eq!(engine.snapshot().reserved_bytes, 12 * MIB);
    drop(another);
}

#[test]
fn registration_rejects_missing_duplicate_malformed_and_unsupported_routes() {
    let mut engine = engine_with(original());
    let before = engine.snapshot();
    let mut missing_existing = expanded();
    missing_existing.devices.remove(&DEVICE);
    let mut malformed = expanded();
    malformed.groups.clear();
    let mut narrow_worker = device(OTHER);
    narrow_worker.worker = Backend::new(BackendKind::Worker { threads: 1 }, 1).unwrap();
    let mut wrong_native = device(OTHER);
    wrong_native.native = Some(backend());
    for (config, device) in [
        (original(), device(OTHER)),
        (missing_existing, device(OTHER)),
        (expanded(), device(DEVICE)),
        (malformed, device(OTHER)),
        (expanded(), narrow_worker),
        (expanded(), wrong_native),
    ] {
        assert!(matches!(
            engine.register_with_config(Generation(0), config, device, Instant::now(),),
            Err(ConfigError::Invalid(_))
        ));
        assert_eq!(engine.snapshot(), before);
    }
    assert!(matches!(
        engine.register_with_config(Generation(1), expanded(), device(OTHER), Instant::now(),),
        Err(ConfigError::Stale(Generation(0)))
    ));
    assert_eq!(engine.snapshot(), before);
    engine.shared.gate.lock().unwrap().generation = Generation(u64::MAX);
    let before = engine.snapshot();
    assert!(matches!(
        engine.register_with_config(
            Generation(u64::MAX),
            expanded(),
            device(OTHER),
            Instant::now(),
        ),
        Err(ConfigError::Invalid(_))
    ));
    assert_eq!(engine.snapshot(), before);
}

#[test]
fn combined_registration_publishes_one_generation_and_usable_device() {
    let directory = Directory::new();
    let file = directory.file();
    let service = Service::start(original()).unwrap();
    service.register_device(device(DEVICE)).unwrap();
    assert_eq!(service.snapshot().unwrap().generation, Generation(0));
    let receipt = service
        .register_device_with_config(Generation(0), expanded(), device(OTHER))
        .unwrap();
    assert_eq!(receipt.generation, Generation(1));
    let snapshot = service.snapshot().unwrap();
    assert_eq!(snapshot.config, expanded());
    assert_eq!(snapshot.devices.len(), 2);
    let mut request = spec(&file, 4);
    request.device = OTHER;
    let completion = service
        .write(request, b"live")
        .unwrap()
        .wait(deadline())
        .unwrap();
    assert!(matches!(completion.status, CompletionStatus::Complete));
    assert_eq!(completion.data().unwrap(), b"live");
    drop(completion);
    service.shutdown().unwrap();
}

#[test]
#[cfg(target_os = "linux")]
fn later_reader_start_failure_joins_inactive_readers_and_allows_same_id_retry() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine_with(original());
    let (retirement, retired) = mpsc::channel();
    Arc::get_mut(&mut engine.shared).unwrap().retirement = retirement;
    let worker = backend();
    worker
        .submit(
            Operation::write(
                Arc::clone(&file),
                file_io::AlignedBuffer::zeroed(4096, 4096).unwrap(),
                0,
            )
            .unwrap(),
        )
        .unwrap();
    let until = deadline();
    while file.metadata().unwrap().len() != 4096 {
        assert!(Instant::now() < until);
        thread::yield_now();
    }
    let before = engine.snapshot();
    engine.reader_start_failure = Some(1);
    assert!(matches!(
        engine.register_with_config(
            Generation(0),
            expanded(),
            DeviceConfig {
                id: OTHER,
                worker,
                native: Some(Backend::new(BackendKind::NativeAio, 4).unwrap()),
            },
            Instant::now(),
        ),
        Err(ConfigError::Invalid(_))
    ));
    assert_eq!(engine.snapshot(), before);
    assert!(matches!(retired.try_recv(), Err(mpsc::TryRecvError::Empty)));
    let until = deadline();
    while Arc::strong_count(&file) != 1 {
        assert!(Instant::now() < until);
        thread::yield_now();
    }
    engine.reader_start_failure = None;
    engine
        .register_with_config(Generation(0), expanded(), device(OTHER), Instant::now())
        .unwrap();
    let mut request = spec(&file, 4);
    request.device = OTHER;
    let (_, completed) = enqueue(&mut engine, request, Direction::Write, b"live");
    dispatch(&mut engine);
    let event = retired.recv_timeout(Duration::from_secs(5)).unwrap();
    let Retirement::Complete(id, lane, submission, envelope) = event else {
        panic!("unexpected retirement failure");
    };
    assert_eq!((id, lane), (OTHER, 0));
    engine.retired(submission, envelope);
    assert!(matches!(
        completed.recv().unwrap().status,
        CompletionStatus::Complete
    ));
    assert!(matches!(retired.try_recv(), Err(mpsc::TryRecvError::Empty)));
}

#[test]
fn stop_before_commit_rejects_staged_registration_without_publication() {
    let directory = Directory::new();
    let file = directory.file();
    let worker_backend = backend();
    worker_backend
        .submit(
            Operation::write(
                Arc::clone(&file),
                file_io::AlignedBuffer::zeroed(4096, 4096).unwrap(),
                0,
            )
            .unwrap(),
        )
        .unwrap();
    let until = deadline();
    while file.metadata().unwrap().len() != 4096 {
        assert!(Instant::now() < until);
        thread::yield_now();
    }
    let mut engine = engine_with(original());
    let (retirement, retired) = mpsc::channel();
    Arc::get_mut(&mut engine.shared).unwrap().retirement = retirement;
    let before = engine.snapshot();
    let shared = Arc::clone(&engine.shared);
    let ready = Arc::new(Barrier::new(2));
    let released = Arc::new(Barrier::new(2));
    let worker_ready = Arc::clone(&ready);
    let worker_released = Arc::clone(&released);
    engine.registration_precommit = Some(Box::new(move || {
        worker_ready.wait();
        worker_released.wait();
    }));
    let worker = thread::spawn(move || {
        let result = engine.register_with_config(
            Generation(0),
            expanded(),
            DeviceConfig {
                id: OTHER,
                worker: worker_backend,
                native: None,
            },
            Instant::now(),
        );
        (engine, result)
    });
    ready.wait();
    assert!(matches!(
        retired.recv_timeout(Duration::from_millis(50)),
        Err(mpsc::RecvTimeoutError::Timeout)
    ));
    let service = Service { shared, join: None };
    service.stop();
    released.wait();
    let (engine, result) = worker.join().unwrap();
    assert!(matches!(result, Err(ConfigError::Unavailable(_))));
    assert_eq!(engine.snapshot(), before);
    assert!(matches!(retired.try_recv(), Err(mpsc::TryRecvError::Empty)));
    let until = deadline();
    while Arc::strong_count(&file) != 1 {
        assert!(Instant::now() < until);
        thread::yield_now();
    }
    assert_eq!(
        engine.shared.ledger.lock().unwrap().limits,
        original().admission
    );
}

#[test]
fn stop_after_commit_preserves_successful_generation_and_topology() {
    let mut engine = engine_with(original());
    let receipt = engine
        .register_with_config(Generation(0), expanded(), device(OTHER), Instant::now())
        .unwrap();
    let service = Service {
        shared: Arc::clone(&engine.shared),
        join: None,
    };
    service.stop();
    assert_eq!(receipt.generation, Generation(1));
    let snapshot = engine.snapshot();
    assert_eq!(snapshot.generation, Generation(1));
    assert_eq!(snapshot.config, expanded());
    assert_eq!(snapshot.devices.len(), 2);
}

#[test]
fn full_management_queue_rejects_registration_without_state_changes() {
    let mut engine = engine_with(original());
    let (management, commands) = mpsc::sync_channel(1);
    Arc::get_mut(&mut engine.shared).unwrap().management = management;
    let service = Service {
        shared: Arc::clone(&engine.shared),
        join: None,
    };
    let (reply, _result) = mpsc::channel();
    service.command(Management::Snapshot(reply)).unwrap();
    let before = engine.snapshot();
    assert!(matches!(service.register_device_with_config(
        Generation(0), expanded(), device(OTHER),
    ), Err(ConfigError::Unavailable(error)) if error.kind() == io::ErrorKind::WouldBlock));
    assert_eq!(engine.snapshot(), before);
    assert_eq!(
        engine.shared.ledger.lock().unwrap().limits,
        original().admission
    );
    drop(commands);
}

#[test]
fn shutdown_rejects_queued_transaction_without_commit() {
    let mut engine = engine_with(original());
    let (management, commands) = mpsc::sync_channel(32);
    let (retirement, retired) = mpsc::channel();
    Arc::get_mut(&mut engine.shared).unwrap().management = management;
    Arc::get_mut(&mut engine.shared).unwrap().retirement = retirement;
    let service = Service {
        shared: Arc::clone(&engine.shared),
        join: None,
    };
    for _ in 0..8 {
        let (reply, _) = mpsc::channel();
        service.command(Management::Snapshot(reply)).unwrap();
    }
    let (reply, result) = mpsc::channel();
    service
        .command(Management::RegisterWithConfig(
            Generation(0),
            expanded(),
            device(OTHER),
            reply,
        ))
        .unwrap();
    service.stop();
    engine.run(commands, retired).unwrap();
    assert!(
        matches!(result.recv_timeout(Duration::from_secs(5)).unwrap(), Err(ConfigError::Unavailable(error)) if error.kind() == io::ErrorKind::BrokenPipe)
    );
    let gate = service.shared.gate.lock().unwrap();
    assert_eq!(gate.generation, Generation(0));
    assert_eq!(*gate.config, original());
    assert!(!gate.devices.contains_key(&OTHER));
    assert_eq!(
        service.shared.ledger.lock().unwrap().limits,
        original().admission
    );
}

#[test]
fn native_config_and_registration_compete_for_one_generation() {
    let service = Service::start(original()).unwrap();
    service.register_device(device(DEVICE)).unwrap();
    let ready = Barrier::new(3);
    let (registration, replacement) = thread::scope(|scope| {
        let registration = scope.spawn(|| {
            ready.wait();
            service.register_device_with_config(Generation(0), expanded(), device(OTHER))
        });
        let replacement = scope.spawn(|| {
            ready.wait();
            service.reconfigure(Generation(0), original())
        });
        ready.wait();
        (registration.join().unwrap(), replacement.join().unwrap())
    });
    assert_ne!(registration.is_ok(), replacement.is_ok());
    let registered = registration.is_ok();
    let loser = if registered {
        replacement
    } else {
        registration
    };
    assert!(matches!(loser, Err(ConfigError::Stale(Generation(1)))));
    let snapshot = service.snapshot().unwrap();
    assert_eq!(snapshot.generation, Generation(1));
    assert_eq!(snapshot.devices.len(), if registered { 2 } else { 1 });
    assert_eq!(
        snapshot.config,
        if registered { expanded() } else { original() }
    );
    service.shutdown().unwrap();
}

#[test]
fn removal_prunes_accepted_policy() {
    let service = Service::start(original()).unwrap();
    service.register_device(device(DEVICE)).unwrap();
    service
        .register_device_with_config(Generation(0), expanded(), device(OTHER))
        .unwrap();
    service.remove_device(OTHER).unwrap();
    assert!(!service
        .snapshot()
        .unwrap()
        .config
        .devices
        .contains_key(&OTHER));
    service.shutdown().unwrap();
}

#[test]
fn removing_large_device_restores_normal_capacity_and_preserves_configured_floor() {
    let directory = Directory::new();
    let file = directory.file();
    for floor in [2 * MIB, 4 * MIB] {
        let mut initial = original();
        initial.admission.probe_bytes = floor;
        let service = Service::start(initial.clone()).unwrap();
        service.register_device(device(DEVICE)).unwrap();
        for id in 2..6 {
            let before = service.snapshot().unwrap();
            let mut replacement = before.config.clone();
            replacement.devices.insert(
                DeviceId(id),
                DeviceLimits {
                    depth: 4,
                    probe_depth: 1,
                    max_probe_bytes: 8 * MIB,
                },
            );
            let added = service
                .register_device_with_config(before.generation, replacement, device(DeviceId(id)))
                .unwrap();
            assert!(
                matches!(service.begin(spec(&file, 12 * MIB).split().0), Err(e) if e.kind() == io::ErrorKind::InvalidInput)
            );
            let removed = service
                .remove_device_at(added.generation, DeviceId(id))
                .unwrap();
            service.remove_device(DeviceId(id)).unwrap();
            assert_eq!(
                service.remove_device_at(removed, DeviceId(id)).unwrap(),
                removed
            );
            let after = service.snapshot().unwrap();
            assert_eq!(after.generation, removed);
            assert_eq!(after.config, initial);
            assert_eq!(after.config.effective_admission().probe_bytes, floor);
            let prepared = prepare(&service, spec(&file, 12 * MIB), Direction::Read, &[]);
            assert_eq!(service.snapshot().unwrap().reserved_bytes, 12 * MIB);
            drop(prepared);
        }
        service.shutdown().unwrap();
    }
}

#[test]
fn removal_keeps_submitted_probe_debt_and_private_policy_until_retirement() {
    let directory = Directory::new();
    let file = directory.file();
    let mut policy = config();
    policy.admission.max_bytes = 16 * MIB;
    policy.devices.get_mut(&DEVICE).unwrap().max_probe_bytes = 8 * MIB;
    let mut engine = engine_with(policy);
    engine.register(device(OTHER), false).unwrap();
    let mut probe = spec(&file, 8 * MIB);
    probe.class = RequestClass::Probe;
    let (_, results) = enqueue(&mut engine, probe, Direction::Read, &[]);
    dispatch(&mut engine);
    let physical = retired(&engine);
    engine
        .remove_config(Generation(0), DEVICE, Instant::now())
        .unwrap();
    engine.controls(Instant::now());
    drop(results.recv().unwrap());
    let snapshot = engine.snapshot();
    assert_eq!(snapshot.reserved_bytes, 8 * MIB);
    assert_eq!(snapshot.debt[1].bytes, 6 * MIB);
    assert_eq!(snapshot.awaiting_retirement, 1);
    assert_eq!(snapshot.devices[0].physical_depth, [0, 1]);
    assert!(!snapshot.config.devices.contains_key(&DEVICE));
    let mut normal = spec(&file, 12 * MIB);
    normal.device = OTHER;
    let mut waiting = Admission::begin(Arc::clone(&engine.shared), normal.split().0).unwrap();
    engine.drain_gate();
    engine.grant_admission();
    assert_eq!(engine.snapshot().reserved_bytes, 8 * MIB);
    engine
        .reconfigure(Generation(1), engine.snapshot().config, Instant::now())
        .unwrap();
    engine.retired(physical.0, physical.1);
    engine.grant_admission();
    waiting.wait_capacity().unwrap();
    waiting
        .prepare(target(&file), Direction::Read, &[])
        .unwrap();
    assert_eq!(engine.snapshot().reserved_bytes, 12 * MIB);
    assert_eq!(engine.snapshot().debt[1].bytes, 0);
}

#[test]
fn removal_rejects_begun_and_prepared_work_and_completes_unconsumed_publication() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine_with(original());
    let mut prepared = core_prepare(&mut engine, spec(&file, 4), Direction::Read, &[]);
    let mut published = core_prepare(&mut engine, spec(&file, 4), Direction::Read, &[]);
    let (destination, completed) = mpsc::channel();
    published.publish(destination).unwrap();
    let mut begun = Admission::begin(Arc::clone(&engine.shared), spec(&file, 4).split().0).unwrap();
    engine
        .remove_config(Generation(0), DEVICE, Instant::now())
        .unwrap();
    let (destination, _) = mpsc::channel();
    assert!(
        matches!(prepared.publish(destination), Err(e) if e.kind() == io::ErrorKind::BrokenPipe)
    );
    assert!(matches!(begun.wait_capacity(), Err(e) if e.kind() == io::ErrorKind::BrokenPipe));
    engine.drain_gate();
    assert!(matches!(
        completed.recv().unwrap().status,
        CompletionStatus::Shutdown
    ));
    assert!(!engine.snapshot().config.devices.contains_key(&DEVICE));
}

#[test]
fn closure_after_drain_before_snapshot_commits_one_consistent_generation() {
    let mut engine = engine_with(original());
    engine.drain_gate();
    let service = Service {
        shared: Arc::clone(&engine.shared),
        join: None,
    };
    service.remove_device(DEVICE).unwrap();
    let before = engine.snapshot();
    assert_eq!(before.generation, Generation(0));
    assert!(before.config.devices.contains_key(&DEVICE));
    let (reply, snapshot) = mpsc::channel();
    engine.manage(Management::Snapshot(reply));
    let after = snapshot.recv().unwrap();
    assert_eq!(after.generation, Generation(1));
    assert!(!after.config.devices.contains_key(&DEVICE));
    assert_eq!(
        engine.shared.ledger.lock().unwrap().limits,
        after.config.effective_admission()
    );
    service.remove_device(DEVICE).unwrap();
    engine.drain_gate();
    assert_eq!(engine.snapshot().generation, Generation(1));
}

#[test]
fn explicit_removal_bypasses_full_management_queue_and_preserves_first_reply() {
    let mut engine = engine_with(original());
    let (management, commands) = mpsc::sync_channel(1);
    Arc::get_mut(&mut engine.shared).unwrap().management = management;
    let service = Service {
        shared: Arc::clone(&engine.shared),
        join: None,
    };
    let (reply, _) = mpsc::channel();
    service.command(Management::Snapshot(reply)).unwrap();
    let (reply, result) = mpsc::channel();
    service.shared.gate.lock().unwrap().removals.insert(
        DEVICE,
        Removal::Transaction {
            expected: Generation(0),
            reply,
            close_requested: false,
        },
    );
    assert!(
        matches!(service.remove_device_at(Generation(0), DEVICE), Err(ConfigError::Unavailable(e)) if e.kind() == io::ErrorKind::WouldBlock)
    );
    assert!(service.shared.gate.lock().unwrap().devices[&DEVICE]);
    engine.drain_gate();
    assert_eq!(result.recv().unwrap().unwrap(), Generation(1));
    assert!(!engine.snapshot().config.devices.contains_key(&DEVICE));
    assert!(commands.try_recv().is_ok());
}

#[test]
fn stop_resolves_pending_removal_without_config_commit() {
    let mut engine = engine_with(original());
    let service = Service {
        shared: Arc::clone(&engine.shared),
        join: None,
    };
    let (reply, result) = mpsc::channel();
    service.shared.gate.lock().unwrap().removals.insert(
        DEVICE,
        Removal::Transaction {
            expected: Generation(0),
            reply,
            close_requested: false,
        },
    );
    service.stop();
    engine.drain_gate();
    assert!(matches!(
        result.recv().unwrap(),
        Err(ConfigError::Unavailable(_))
    ));
    assert_eq!(engine.snapshot().generation, Generation(0));
    engine.drain_gate();
    assert!(engine.shared.gate.lock().unwrap().removals.is_empty());
}

#[test]
fn native_config_and_removal_compete_for_one_generation() {
    let service = Service::start(original()).unwrap();
    service.register_device(device(DEVICE)).unwrap();
    let ready = Barrier::new(3);
    let (removal, replacement) = thread::scope(|scope| {
        let removal = scope.spawn(|| {
            ready.wait();
            service.remove_device_at(Generation(0), DEVICE)
        });
        let replacement = scope.spawn(|| {
            ready.wait();
            service.reconfigure(Generation(0), original())
        });
        ready.wait();
        (removal.join().unwrap(), replacement.join().unwrap())
    });
    assert_ne!(removal.is_ok(), replacement.is_ok());
    let snapshot = service.snapshot().unwrap();
    assert_eq!(snapshot.generation, Generation(1));
    assert_eq!(
        snapshot.config.devices.contains_key(&DEVICE),
        replacement.is_ok()
    );
    let error = if let Err(error) = removal {
        error
    } else {
        replacement.unwrap_err()
    };
    assert!(matches!(error, ConfigError::Stale(Generation(1))));
    service.shutdown().unwrap();
}

#[test]
#[cfg(feature = "cxx")]
fn removal_restores_capacity_only_as_delayed_physical_probe_releases_its_charge() {
    let directory = Directory::new();
    let file = directory.file();
    let mut policy = config();
    policy.admission.max_bytes = 16 * MIB;
    policy.devices.get_mut(&DEVICE).unwrap().max_probe_bytes = 8 * MIB;
    let mut engine = engine_with(policy);
    engine.register(device(OTHER), false).unwrap();
    let mut probe = spec(&file, 8 * MIB);
    probe.class = RequestClass::Probe;
    probe.execution = Execution::WorkerRequired;
    let mut admission = core_prepare(&mut engine, probe, Direction::Read, &[]);
    admission
        .set_worker_fault(file_io::WorkerFault::new(Duration::from_millis(250), false))
        .unwrap();
    let (destination, completed) = mpsc::channel();
    admission.publish(destination).unwrap();
    drop(admission);
    engine.drain_gate();
    dispatch(&mut engine);
    engine
        .remove_config(Generation(0), DEVICE, Instant::now())
        .unwrap();
    engine.controls(Instant::now());
    assert!(matches!(
        completed.recv().unwrap().status,
        CompletionStatus::Shutdown
    ));
    assert!(engine
        .backend(DEVICE, 0)
        .retire(Instant::now() + Duration::from_millis(10))
        .unwrap()
        .is_none());
    let mut normal = spec(&file, 12 * MIB);
    normal.device = OTHER;
    let mut waiting = Admission::begin(Arc::clone(&engine.shared), normal.split().0).unwrap();
    engine.drain_gate();
    engine.grant_admission();
    assert_eq!(engine.snapshot().reserved_bytes, 8 * MIB);
    assert_eq!(engine.snapshot().debt[1].bytes, 6 * MIB);
    retire(&mut engine);
    engine.grant_admission();
    waiting.wait_capacity().unwrap();
    waiting
        .prepare(target(&file), Direction::Read, &[])
        .unwrap();
    assert_eq!(engine.snapshot().reserved_bytes, 12 * MIB);
}

#[test]
fn backend_failure_does_not_publish_an_unrequested_config_removal() {
    let mut engine = engine_with(original());
    engine.backend_failed(DEVICE, 0, io::ErrorKind::PermissionDenied.into());
    engine.drain_gate();
    let failed = engine.snapshot();
    assert_eq!(failed.generation, Generation(0));
    assert!(failed.config.devices.contains_key(&DEVICE));
    assert!(failed.devices[0].closed);
    let mut without_policy = failed.config.clone();
    without_policy.devices.remove(&DEVICE);
    assert!(matches!(
        engine.reconfigure(Generation(0), without_policy, Instant::now()),
        Err(ConfigError::Invalid(_))
    ));
    assert_eq!(
        engine
            .remove_config(Generation(0), DEVICE, Instant::now())
            .unwrap(),
        Generation(1)
    );
    assert!(!engine.snapshot().config.devices.contains_key(&DEVICE));
}

#[test]
fn native_close_coalesces_with_stale_transaction_without_losing_its_reply() {
    let mut engine = engine_with(original());
    let service = Service {
        shared: Arc::clone(&engine.shared),
        join: None,
    };
    let (reply, result) = mpsc::channel();
    service.shared.gate.lock().unwrap().removals.insert(
        DEVICE,
        Removal::Transaction {
            expected: Generation(0),
            reply,
            close_requested: false,
        },
    );
    engine
        .reconfigure(Generation(0), original(), Instant::now())
        .unwrap();
    service.remove_device(DEVICE).unwrap();
    engine.drain_gate();
    assert!(matches!(
        result.recv().unwrap(),
        Err(ConfigError::Stale(Generation(1)))
    ));
    assert_eq!(engine.snapshot().generation, Generation(2));
    assert!(!engine.snapshot().config.devices.contains_key(&DEVICE));
}

#[test]
fn actor_unwind_disconnects_a_pending_removal_reply_and_closes_admission() {
    let mut engine = engine_with(original());
    let (management, commands) = mpsc::sync_channel(32);
    let (_retirement, retired) = mpsc::channel();
    Arc::get_mut(&mut engine.shared).unwrap().management = management;
    let shared = Arc::clone(&engine.shared);
    let exit_shared = Arc::clone(&shared);
    let (reply, result) = mpsc::channel();
    engine.registration_precommit = Some(Box::new(move || {
        exit_shared.gate.lock().unwrap().removals.insert(
            DEVICE,
            Removal::Transaction {
                expected: Generation(0),
                reply,
                close_requested: false,
            },
        );
        panic!("injected actor termination");
    }));
    let (reply, _registration) = mpsc::channel();
    shared
        .management
        .try_send(Management::RegisterWithConfig(
            Generation(0),
            expanded(),
            device(OTHER),
            reply,
        ))
        .unwrap();
    assert!(thread::spawn(move || engine.run(commands, retired))
        .join()
        .is_err());
    assert!(matches!(
        result.recv_timeout(Duration::from_secs(1)),
        Err(mpsc::RecvTimeoutError::Disconnected)
    ));
    assert!(!shared.gate.lock().unwrap().accepting);
}
