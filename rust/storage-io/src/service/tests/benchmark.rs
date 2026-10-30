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
use crate::benchmark::{MAX_TARGETS, MAX_WORKERS, MAX_WORKLOADS};
use crate::calibration::{ByteCount, CalibrationCandidate, Mode, Sample};

fn shape(targets: usize, workers: usize) -> BenchmarkShape {
    BenchmarkShape {
        target_count: NonZeroUsize::new(targets).unwrap(),
        workload_count: NonZeroUsize::new(2).unwrap(),
        concurrency: NonZeroUsize::new(workers).unwrap(),
    }
}
fn plan(duration: Duration) -> CheckedWorkloads {
    CheckedWorkloads::new(
        vec![
            Workload {
                mode: Mode::Write,
                size: NonZeroUsize::new(4096).unwrap(),
                duration,
            },
            Workload {
                mode: Mode::Read,
                size: NonZeroUsize::new(4096).unwrap(),
                duration,
            },
        ],
        OperationPolicy::production(),
        SamplePolicy::Elapsed,
    )
    .unwrap()
}
fn bench_target(file: &Arc<File>) -> BenchmarkTarget {
    BenchmarkTarget::new(target(file), DEVICE, Execution::PreferNative, 4096, 4096).unwrap()
}
fn begin(service: &Service, shape: BenchmarkShape) -> BenchmarkPreparation {
    match service.begin_benchmark(shape).unwrap() {
        BeginBenchmark::Preparing(preparation) => preparation,
        _ => panic!("unexpected busy benchmark"),
    }
}
fn actor_service(engine: &Engine) -> Service {
    Service {
        shared: Arc::clone(&engine.shared),
        join: None,
    }
}
fn prepare_child(engine: &mut Engine) {
    for _ in 0..4 {
        engine.drain_gate();
        engine.grant_admission();
        engine.benchmark_turn(Instant::now(), false);
    }
    engine.drain_gate();
}
fn calibrated(service: &Service) -> u64 {
    service
        .calibration()
        .replace(
            CalibrationCandidate::try_from_samples(vec![
                Sample {
                    mode: Mode::Read,
                    size: ByteCount::new(4096).unwrap(),
                    latency_us: 1.0,
                    iops: 10.0,
                },
                Sample {
                    mode: Mode::Write,
                    size: ByteCount::new(4096).unwrap(),
                    latency_us: 1.0,
                    iops: 10.0,
                },
            ])
            .unwrap(),
        )
        .unwrap()
}

#[test]
fn native_run_publishes_once_and_old_handles_cannot_reserve_inventory() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = service();
    let mut old = Vec::new();
    for generation in 1..=4 {
        let mut preparation = begin(&service, shape(1, 2));
        preparation.add_target(bench_target(&file)).unwrap();
        let run = preparation.commit(plan(Duration::from_millis(50))).unwrap();
        assert!(
            matches!(run.wait_retired(), BenchmarkStatus::Finished { result: Ok(value), completed: 2, .. } if value == generation)
        );
        assert_eq!(service.calibration().snapshot().generation(), generation);
        assert!(!service.snapshot().unwrap().benchmark.run_reserved);
        let report = run.report();
        assert_eq!(report.len(), 2);
        assert!(report.get(0).unwrap().successful_count > 0);
        assert!(report.get(1).unwrap().successful_latency_us > 0);
        old.push((run, report));
    }
    service.shutdown().unwrap();
    for (run, report) in old {
        assert_eq!(run.report(), report);
        assert!(run.status().is_terminal());
        run.request_stop();
    }
    let mut bytes = vec![0; 4096];
    file_io::read_exact_at(&file, &mut bytes, 0).unwrap();
    assert!(bytes
        .iter()
        .enumerate()
        .all(|(i, b)| *b == (i % 251 + 1) as u8));
}

#[test]
fn concurrent_begin_reserves_before_any_target_allocation() {
    let service = Arc::new(service());
    let barrier = Arc::new(Barrier::new(9));
    let allocations = Arc::new(AtomicUsize::new(0));
    let (send, receive) = mpsc::channel();
    let mut threads = Vec::new();
    for _ in 0..8 {
        let service = Arc::clone(&service);
        let barrier = Arc::clone(&barrier);
        let allocations = Arc::clone(&allocations);
        let send = send.clone();
        threads.push(thread::spawn(move || {
            barrier.wait();
            let begun = service.begin_benchmark(shape(1, 1)).unwrap();
            if matches!(begun, BeginBenchmark::Preparing(_)) {
                allocations.fetch_add(1, Ordering::Relaxed);
            }
            send.send(begun).ok().unwrap();
        }));
    }
    barrier.wait();
    for thread in threads {
        thread.join().unwrap();
    }
    assert_eq!(allocations.load(Ordering::Relaxed), 1);
    let mut owner = None;
    for _ in 0..8 {
        if let BeginBenchmark::Preparing(p) = receive.recv().unwrap() {
            owner = Some(p);
        }
    }
    let run = owner.unwrap().abort(BenchmarkError::External(-77));
    assert!(matches!(
        run.wait_retired(),
        BenchmarkStatus::Finished {
            result: Err(BenchmarkError::External(-77)),
            ..
        }
    ));
    Arc::try_unwrap(service).ok().unwrap().shutdown().unwrap();
}

#[test]
fn rejected_commit_keeps_targets_and_reservation_through_outside_lock_cleanup() {
    let directory = Directory::new();
    let file = directory.file();
    let service = Arc::new(service());
    let entered = Arc::new(Barrier::new(2));
    let release = Arc::new(Barrier::new(2));
    let mut preparation = begin(&service, shape(1, 1));
    let (in_drop, out_drop) = (Arc::clone(&entered), Arc::clone(&release));
    preparation
        .add_target(bench_target(&file).on_drop(move || {
            in_drop.wait();
            out_drop.wait();
        }))
        .unwrap();
    assert_eq!(
        preparation
            .commit(CheckedWorkloads::production().unwrap())
            .err(),
        Some(BenchmarkError::InvalidPlan)
    );
    let address = preparation.storage_address();
    assert_ne!(address, 0);
    let (cleared, after_clear) = mpsc::channel();
    let (finish, finishing) = mpsc::channel();
    let cleanup = thread::spawn(move || {
        preparation.discard_targets();
        cleared.send(()).unwrap();
        finishing.recv().unwrap();
        preparation
            .abort(BenchmarkError::External(-88))
            .wait_retired()
    });
    entered.wait();
    assert!(matches!(
        service.begin_benchmark(shape(1, 1)).unwrap(),
        BeginBenchmark::AlreadyRunning(_)
    ));
    assert!(service.snapshot().unwrap().benchmark.run_reserved);
    release.wait();
    after_clear.recv().unwrap();
    assert!(matches!(
        service.begin_benchmark(shape(1, 1)).unwrap(),
        BeginBenchmark::AlreadyRunning(_)
    ));
    finish.send(()).unwrap();
    assert!(matches!(
        cleanup.join().unwrap(),
        BenchmarkStatus::Finished {
            result: Err(BenchmarkError::External(-88)),
            ..
        }
    ));
    Arc::try_unwrap(service).ok().unwrap().shutdown().unwrap();
}

#[cfg(feature = "ownership-test-support")]
#[test]
fn cancelled_commit_preserves_full_inventory_and_token() {
    use crate::service::benchmark::proof::{BenchmarkProof, ProofState, Scenario};
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut preparation = begin(&service, shape(2, 1));
    let state = ProofState::new(Scenario::RejectedCommitCleanup);
    preparation.attach_proof(Arc::clone(&state)).unwrap();
    let proof = BenchmarkProof { state, run: preparation.run() };
    preparation.add_target(bench_target(&file)).unwrap();
    preparation.add_target(bench_target(&file)).unwrap();
    preparation.proof_capture_inventory();
    preparation.run().request_stop();
    let error = preparation.commit(plan(Duration::from_millis(1))).err().unwrap();
    preparation.proof_rejected_inventory(error);
    let facts = proof.cleanup_facts();
    let reserved = service.snapshot().unwrap().benchmark.run_reserved;
    preparation.discard_targets();
    let terminal = preparation.abort(error).wait_retired();
    service.shutdown().unwrap();
    assert_eq!(error, BenchmarkError::Cancelled);
    assert!(facts.rejection_observed);
    assert!(facts.token_retained);
    assert!(facts.inventory_preserved);
    assert_eq!(facts.captured_targets, 2);
    assert_eq!(facts.returned_targets, 2);
    assert!(reserved);
    assert!(matches!(terminal, BenchmarkStatus::Finished { result: Err(BenchmarkError::Cancelled), .. }));
}

#[cfg(feature = "ownership-test-support")]
fn cleanup_infrastructure_ok(facts: crate::service::benchmark::proof::PreparationCleanupFacts) -> bool {
    if facts.native_timed_out || facts.upper_timed_out {
        eprintln!("PREPARATION CLEANUP INFRASTRUCTURE: destructor witness timed out");
        false
    } else {
        true
    }
}

#[cfg(feature = "ownership-test-support")]
fn expired_cleanup_witness_probe(swap: bool) {
    use crate::service::benchmark::proof::{BenchmarkProof, ProofState, Scenario};
    let directory = Directory::new();
    let owner = UnmarkedBlockFile::open(
        &directory.0.join("block"),
        BlockConfig {
            block_size: 4096,
            initial_size: 3 * 4096,
            direct_io: false,
            punch_holes: false,
        },
        SpaceBudget::default(),
        file_io::DirectoryDurability::Required,
        &mut file_io::DirectorySyncSummary::default(),
    ).unwrap().finish_marking().unwrap();
    let index = owner.allocate().unwrap();
    let region = owner.region(index, 0..4096).unwrap();
    let service = service();
    let mut preparation = begin(&service, shape(1, 1));
    let state = ProofState::new(Scenario::PrefixCleanup);
    assert!(state.lease.set(region.watch_final_lease().unwrap()).is_ok());
    preparation.attach_proof(Arc::clone(&state)).unwrap();
    let proof = BenchmarkProof { state, run: preparation.run() };
    preparation.add_target(BenchmarkTarget::new(
        Target::Block(region), DEVICE, Execution::WorkerRequired, 4096, 4096,
    ).unwrap()).unwrap();
    thread::scope(|scope| {
        let cleanup = scope.spawn(|| {
            if swap {
                drop(proof.proof_enter_upper_cleanup());
                preparation.discard_targets();
            } else {
                preparation.discard_targets();
                drop(proof.proof_enter_upper_cleanup());
            }
            preparation.abort(BenchmarkError::Cancelled).wait_retired()
        });
        let deadline = Instant::now() + Duration::from_secs(15);
        let mut order_reported = false;
        let observed = loop {
            let facts = proof.cleanup_facts();
            if swap && facts.cleanup_order_bad && !order_reported
                && cleanup_infrastructure_ok(facts)
            {
                eprintln!("PREPARATION CLEANUP INVARIANT: upper cleanup entered before native prefix retirement");
                order_reported = true;
            }
            if (!swap && facts.upper_entered) || (swap && facts.native_entered) {
                break facts;
            }
            if Instant::now() >= deadline {
                eprintln!("PREPARATION CLEANUP INFRASTRUCTURE: probe observation deadline");
                break facts;
            }
            thread::yield_now();
        };
        proof.state.release();
        let terminal = cleanup.join().unwrap();
        service.shutdown().unwrap();
        let final_facts = proof.cleanup_facts();
        let infrastructure_ok = cleanup_infrastructure_ok(final_facts);
        assert!(observed.native_timed_out || observed.upper_timed_out);
        assert_eq!(swap, observed.cleanup_order_bad);
        assert!(final_facts.native_finished && final_facts.upper_finished);
        assert!(matches!(terminal, BenchmarkStatus::Finished { result: Err(BenchmarkError::Cancelled), .. }));
        assert!(infrastructure_ok, "deliberate expired witness must fail as infrastructure");
    });
}

#[cfg(feature = "ownership-test-support")]
#[test]
#[ignore = "deliberately expires a real destructor witness and fails; run under supervisor"]
fn expired_native_cleanup_witness_is_infrastructure() {
    expired_cleanup_witness_probe(false);
}

#[cfg(feature = "ownership-test-support")]
#[test]
#[ignore = "deliberately expires a real destructor witness after ordering evidence; run under supervisor"]
fn expired_upper_cleanup_witness_overrides_ordering_evidence() {
    expired_cleanup_witness_probe(true);
}

#[cfg(feature = "ownership-test-support")]
#[test]
fn stop_during_preparing_waits_for_owner_acknowledgement() {
    use crate::service::benchmark::proof::{BenchmarkProof, CleanupStage, ProofState, Scenario};
    let service = service();
    let preparation = begin(&service, shape(1, 1));
    let state = ProofState::new(Scenario::PrefixCleanup);
    preparation.attach_proof(Arc::clone(&state)).unwrap();
    let proof = Arc::new(BenchmarkProof { state, run: preparation.run() });
    let (done, completed) = mpsc::channel();
    let shutdown = thread::spawn(move || {
        let result = service.shutdown();
        done.send(result).unwrap();
    });
    let wait = |stage| {
        let deadline = Instant::now() + Duration::from_secs(4);
        loop {
            let facts = proof.cleanup_facts();
            if !cleanup_infrastructure_ok(facts) { return (false, None); }
            if let Ok(result) = completed.try_recv() {
                eprintln!("PREPARATION CLEANUP INVARIANT: shutdown returned before preparation acknowledgement");
                return (false, Some(result));
            }
            if facts.shutdown_exit_decided {
                eprintln!("PREPARATION CLEANUP INVARIANT: shutdown decided to exit with Preparing cleanup outstanding");
                return (false, None);
            }
            if facts.shutdown_decision_seen && facts.shutdown_stage == stage
                && (stage != CleanupStage::UpperReleasing || facts.upper_entered)
            {
                return (!facts.terminal && facts.shutdown_run == proof.run.proof_run_id(), None);
            }
            if Instant::now() >= deadline { return (false, None); }
            thread::yield_now();
        }
    };
    let (native_waiting, early_native) = wait(CleanupStage::NativeReleasing);
    let upper_proof = Arc::clone(&proof);
    let cleanup = thread::spawn(move || {
        drop(upper_proof.proof_enter_upper_cleanup());
        preparation.abort(BenchmarkError::Cancelled).wait_retired()
    });
    let (upper_waiting, early_upper) = wait(CleanupStage::UpperReleasing);
    proof.proof_release_upper_cleanup();
    let terminal = cleanup.join().unwrap();
    let shutdown_result = early_native.or(early_upper).unwrap_or_else(||
        completed.recv_timeout(Duration::from_secs(5)).expect("shutdown acknowledgement watchdog"));
    shutdown.join().unwrap();
    assert!(cleanup_infrastructure_ok(proof.cleanup_facts()));
    assert!(native_waiting, "shutdown returned before preparation acknowledgement");
    assert!(upper_waiting, "shutdown returned before preparation acknowledgement");
    assert!(proof.cleanup_facts().upper_finished);
    assert!(!proof.cleanup_facts().upper_timed_out);
    assert!(matches!(terminal, BenchmarkStatus::Finished { result: Err(BenchmarkError::Cancelled), .. }));
    shutdown_result.unwrap();
}

#[test]
fn every_partial_target_index_releases_once_without_reopening_run_early() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    for failed_index in 0..=12 {
        let drops = Arc::new(AtomicUsize::new(0));
        let mut preparation = begin(&service, shape(12, 1));
        for _ in 0..failed_index {
            let drops = Arc::clone(&drops);
            preparation
                .add_target(bench_target(&file).on_drop(move || {
                    drops.fetch_add(1, Ordering::Relaxed);
                }))
                .unwrap();
        }
        preparation.discard_targets();
        assert_eq!(drops.load(Ordering::Relaxed), failed_index);
        assert!(matches!(
            service.begin_benchmark(shape(1, 1)).unwrap(),
            BeginBenchmark::AlreadyRunning(_)
        ));
        preparation.abort(BenchmarkError::Memory).wait_retired();
        assert_eq!(drops.load(Ordering::Relaxed), failed_index);
    }
    service.shutdown().unwrap();
}

#[test]
fn commit_moves_backing_and_reports_metadata_debt_after_shrink() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine();
    let service = actor_service(&engine);
    let mut preparation = begin(&service, shape(2, 2));
    preparation.add_target(bench_target(&file)).unwrap();
    let address = preparation.storage_address();
    let run = preparation.commit(plan(Duration::from_secs(5))).unwrap();
    engine.benchmark_turn(Instant::now(), false);
    assert_eq!(address, engine.benchmark_storage_address());
    let mut config = (*engine.shared.gate.lock().unwrap().config).clone();
    config.benchmark = BenchmarkLimits {
        targets: 0,
        workloads: 0,
        workers: 0,
    };
    let (reply, received) = mpsc::channel();
    engine.manage(engine::Management::Reconfigure(
        Generation(0),
        config,
        reply,
    ));
    received.recv().unwrap().unwrap();
    let metadata = engine.snapshot().benchmark;
    assert_eq!(
        (
            metadata.target_debt,
            metadata.workload_debt,
            metadata.worker_debt
        ),
        (2, 2, 2)
    );
    assert!(metadata.owned_payload_bytes > 0);
    run.request_stop();
    engine.benchmark_turn(Instant::now(), false);
    assert!(run.status().is_terminal());
    assert!(!engine.snapshot().benchmark.run_reserved);
    assert_eq!(
        service.begin_benchmark(BenchmarkShape::production()).err(),
        Some(BenchmarkError::InvalidPlan)
    );
}

#[test]
fn late_child_blocks_successor_phase_and_terminal_fence() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let mut engine = engine();
    let service = actor_service(&engine);
    let previous = calibrated(&service);
    let mut preparation = begin(&service, shape(1, 1));
    preparation.add_target(bench_target(&file)).unwrap();
    let run = preparation.commit(plan(Duration::from_millis(1))).unwrap();
    prepare_child(&mut engine);
    dispatch(&mut engine);
    let physical = retired(&engine);
    run.request_stop();
    engine.benchmark_turn(Instant::now(), false);
    assert!(!run.status().is_terminal());
    assert_eq!(engine.snapshot().awaiting_retirement, 1);
    for _ in 0..8 {
        engine.benchmark_turn(Instant::now() + Duration::from_secs(1), false);
    }
    assert_eq!(engine.snapshot().awaiting_retirement, 1);
    assert_eq!(engine.snapshot().queued_requests, 0);
    assert_eq!(service.calibration().snapshot().generation(), previous);
    engine.retired(physical.0, physical.1);
    engine.benchmark_turn(Instant::now(), false);
    assert!(matches!(
        run.status(),
        BenchmarkStatus::Finished {
            result: Err(BenchmarkError::Cancelled),
            ..
        }
    ));
    assert_eq!(engine.snapshot().usage[0], ResourceUsage::default());
}

#[test]
fn natural_expiry_counts_current_child_and_drains_before_next_phase() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let mut engine = engine();
    let service = actor_service(&engine);
    let mut preparation = begin(&service, shape(1, 1));
    preparation.add_target(bench_target(&file)).unwrap();
    let run = preparation.commit(plan(Duration::from_millis(20))).unwrap();
    prepare_child(&mut engine);
    dispatch(&mut engine);
    let physical = retired(&engine);
    let expired = Instant::now() + Duration::from_secs(1);
    engine.benchmark_turn(expired, false);
    assert!(matches!(
        run.status(),
        BenchmarkStatus::Draining { completed: 0, .. }
    ));
    assert_eq!(engine.snapshot().awaiting_retirement, 1);
    engine.retired(physical.0, physical.1);
    engine.benchmark_turn(expired, false);
    prepare_child(&mut engine);
    assert!(matches!(
        run.status(),
        BenchmarkStatus::Running {
            workload: 1,
            completed: 1,
            ..
        }
    ));
    run.request_stop();
    engine.benchmark_turn(Instant::now(), false);
    engine.drain_gate();
    engine.controls(Instant::now());
    engine.benchmark_turn(Instant::now(), false);
    assert!(run.status().is_terminal());
}

#[test]
fn production_plan_and_operation_deadline_are_independent_of_wait_cap() {
    let plan = CheckedWorkloads::production().unwrap();
    assert_eq!(plan.workloads().len(), MAX_WORKLOADS);
    assert_eq!(MAX_WORKERS, 16);
    assert_eq!(MAX_TARGETS, 10240);
    for (index, workload) in plan.workloads().iter().enumerate() {
        assert_eq!(
            workload.mode,
            if index < 10 { Mode::Write } else { Mode::Read }
        );
        assert_eq!(workload.size.get(), 4096 << (index % 10));
        assert_eq!(workload.duration, Duration::from_secs(5));
    }
    let directory = Directory::new();
    let file = directory.file();
    let mut config = config();
    config.health.wait_cap = Duration::from_millis(37);
    let mut engine = engine_with(config);
    let service = actor_service(&engine);
    let mut preparation = begin(&service, shape(1, 1));
    preparation.add_target(bench_target(&file)).unwrap();
    let run = preparation
        .commit(self::plan(Duration::from_secs(1)))
        .unwrap();
    prepare_child(&mut engine);
    let (issued, policy, wait) = engine.benchmark_child_policy().unwrap();
    assert_eq!(
        policy.deadline.unwrap().duration_since(issued),
        Duration::from_secs(300)
    );
    assert_eq!(policy.retry_alignment, None);
    assert_eq!(policy.max_error_retries, 0);
    let captured = wait.unwrap();
    let mut config = (*engine.shared.gate.lock().unwrap().config).clone();
    config.health.wait_cap = Duration::from_secs(99);
    let (reply, received) = mpsc::channel();
    engine.manage(engine::Management::Reconfigure(
        Generation(0),
        config,
        reply,
    ));
    received.recv().unwrap().unwrap();
    assert_eq!(engine.benchmark_child_policy().unwrap().2, Some(captured));
    run.request_stop();
    engine.benchmark_turn(Instant::now(), false);
    engine.benchmark_turn(Instant::now(), false);
    assert!(run.status().is_terminal());
}

#[test]
fn phase_failure_and_explicit_stop_keep_previous_calibration() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let previous = calibrated(&service);
    let old = service.calibration().snapshot();
    let mut preparation = begin(&service, shape(1, 1));
    preparation.add_target(bench_target(&file)).unwrap();
    let reads = CheckedWorkloads::new(
        vec![Workload {
            mode: Mode::Read,
            size: NonZeroUsize::new(4096).unwrap(),
            duration: Duration::from_millis(10),
        }],
        OperationPolicy::production(),
        SamplePolicy::Elapsed,
    )
    .unwrap();
    let run = preparation.commit(reads).unwrap();
    assert!(matches!(
        run.wait_retired(),
        BenchmarkStatus::Finished {
            result: Err(BenchmarkError::NoSuccess),
            ..
        }
    ));
    assert_eq!(service.calibration().snapshot().generation(), previous);
    let preparation = begin(&service, shape(1, 1));
    preparation.run().request_stop();
    let run = preparation.abort(BenchmarkError::Cancelled);
    run.wait_retired();
    assert!(run.report().is_empty());
    assert_eq!(service.calibration().snapshot().generation(), previous);
    assert_eq!(old.samples(Mode::Read)[0].iops, 10.0);
    service.shutdown().unwrap();
}

#[test]
fn invalid_production_geometry_fails_before_io() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    for size in [4096, 4 * 1024 * 1024] {
        let mut preparation = begin(&service, BenchmarkShape::production());
        preparation
            .add_target(
                BenchmarkTarget::new(target(&file), DEVICE, Execution::PreferNative, size, 4096)
                    .unwrap(),
            )
            .unwrap();
        assert_eq!(
            preparation
                .commit(CheckedWorkloads::production().unwrap())
                .err(),
            Some(BenchmarkError::InvalidPlan)
        );
        assert_eq!(file.metadata().unwrap().len(), 0);
        preparation
            .abort(BenchmarkError::InvalidPlan)
            .wait_retired();
    }
    assert!(
        BenchmarkTarget::new(target(&file), DEVICE, Execution::PreferNative, 4096, 8192).is_err()
    );
    service.shutdown().unwrap();
}

fn read_plan(duration: Duration) -> CheckedWorkloads {
    CheckedWorkloads::new(
        vec![Workload {
            mode: Mode::Read,
            size: NonZeroUsize::new(4096).unwrap(),
            duration,
        }],
        OperationPolicy::production(),
        SamplePolicy::Elapsed,
    )
    .unwrap()
}

#[test]
fn wait_abandonment_does_not_observe_a_late_physical_error() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let mut config = config();
    config.health.wait_cap = Duration::from_millis(1);
    let mut engine = engine_with(config);
    let service = actor_service(&engine);
    let mut preparation = begin(&service, shape(1, 1));
    preparation.add_target(bench_target(&file)).unwrap();
    let run = preparation
        .commit(read_plan(Duration::from_millis(20)))
        .unwrap();
    prepare_child(&mut engine);
    dispatch(&mut engine);
    let (id, mut physical) = retired(&engine);
    engine.benchmark_turn(Instant::now() + Duration::from_secs(1), false);
    assert!(!run.status().is_terminal());
    assert_eq!(engine.snapshot().awaiting_retirement, 1);
    physical.result = Err(io::Error::from_raw_os_error(5));
    engine.retired(id, physical);
    engine.benchmark_turn(Instant::now() + Duration::from_secs(1), false);
    assert_eq!(
        engine.snapshot().health.observations,
        ObservationCounters::default()
    );
    assert!(matches!(
        run.status(),
        BenchmarkStatus::Finished {
            result: Err(BenchmarkError::NoSuccess),
            ..
        }
    ));
}

#[test]
fn final_read_error_escapes_only_its_target_and_not_the_inventory() {
    let directory = Directory::new();
    let first = directory.file();
    first.set_len(4096).unwrap();
    let second = Arc::new(
        OpenOptions::new()
            .read(true)
            .write(true)
            .create_new(true)
            .open(directory.0.join("second"))
            .unwrap(),
    );
    second.set_len(4096).unwrap();
    let weak = [Arc::downgrade(&first), Arc::downgrade(&second)];
    let drops = Arc::new(AtomicUsize::new(0));
    let mut engine = engine();
    let service = actor_service(&engine);
    let mut preparation = begin(&service, shape(2, 1));
    for file in [&first, &second] {
        let drops = Arc::clone(&drops);
        preparation
            .add_target(bench_target(file).on_drop(move || {
                drops.fetch_add(1, Ordering::Relaxed);
            }))
            .unwrap();
    }
    let run = preparation
        .commit(read_plan(Duration::from_millis(1)))
        .unwrap();
    drop((first, second));
    prepare_child(&mut engine);
    dispatch(&mut engine);
    let (id, mut physical) = retired(&engine);
    physical.result = Err(io::Error::from_raw_os_error(5));
    engine.retired(id, physical);
    engine.benchmark_turn(Instant::now() + Duration::from_secs(1), false);
    assert!(run.status().is_terminal());
    assert_eq!(drops.load(Ordering::Relaxed), 2);
    assert_eq!(
        weak.iter().filter(|file| file.strong_count() != 0).count(),
        1
    );
    assert_eq!(engine.snapshot().health.observations.accepted_error, 1);
    assert_eq!(engine.snapshot().health.queued, 1);
    assert_eq!(engine.snapshot().usage[0], ResourceUsage::default());
    let next = begin(&service, shape(1, 1));
    let next = next.abort(BenchmarkError::Cancelled);
    engine.benchmark_turn(Instant::now(), false);
    assert!(next.status().is_terminal());
    assert!(engine.shared.health.discard_one());
    assert!(weak.iter().all(|file| file.strong_count() == 0));
}

#[test]
fn final_inventory_cleanup_blocks_terminal_and_preserves_cancelled_calibration() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let service = Arc::new(service());
    let previous = calibrated(&service);
    let entered = Arc::new(Barrier::new(2));
    let release = Arc::new(Barrier::new(2));
    let (a, b) = (Arc::clone(&entered), Arc::clone(&release));
    let mut preparation = begin(&service, shape(1, 1));
    preparation
        .add_target(bench_target(&file).on_drop(move || {
            a.wait();
            b.wait();
        }))
        .unwrap();
    let run = preparation.commit(plan(Duration::from_millis(50))).unwrap();
    entered.wait();
    assert!(!run.status().is_terminal());
    assert!(matches!(
        service.begin_benchmark(shape(1, 1)).unwrap(),
        BeginBenchmark::AlreadyRunning(_)
    ));
    assert_eq!(service.shared.ledger.lock().unwrap().benchmark.targets, 1);
    run.request_stop();
    release.wait();
    assert!(matches!(
        run.wait_retired(),
        BenchmarkStatus::Finished {
            result: Err(BenchmarkError::Cancelled),
            ..
        }
    ));
    assert_eq!(service.calibration().snapshot().generation(), previous);
    Arc::try_unwrap(service).ok().unwrap().shutdown().unwrap();
}

#[test]
fn stop_during_internal_admission_retires_its_record_before_fence() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let mut config = config();
    config.admission.max_bytes = config.admission.probe_bytes + 4096;
    let mut engine = engine_with(config);
    let held = core_prepare(&mut engine, spec(&file, 4096), Direction::Read, &[]);
    let service = actor_service(&engine);
    let mut preparation = begin(&service, shape(1, 1));
    preparation.add_target(bench_target(&file)).unwrap();
    let run = preparation.commit(plan(Duration::from_secs(1))).unwrap();
    engine.benchmark_turn(Instant::now(), false);
    engine.benchmark_turn(Instant::now(), false);
    assert_eq!(engine.snapshot().usage[0].records, 2);
    run.request_stop();
    engine.benchmark_turn(Instant::now(), false);
    assert!(run.status().is_terminal());
    assert_eq!(engine.snapshot().usage[0].records, 1);
    drop(held);
    assert_eq!(engine.snapshot().usage[0], ResourceUsage::default());
}

#[test]
fn zero_progress_write_fails_without_observation_or_partial_publication() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine();
    let service = actor_service(&engine);
    let previous = calibrated(&service);
    let mut preparation = begin(&service, shape(1, 1));
    preparation.add_target(bench_target(&file)).unwrap();
    let run = preparation.commit(plan(Duration::from_millis(1))).unwrap();
    prepare_child(&mut engine);
    dispatch(&mut engine);
    let (id, mut physical) = retired(&engine);
    physical.result = Ok(0);
    engine.retired(id, physical);
    engine.benchmark_turn(Instant::now() + Duration::from_secs(1), false);
    assert!(matches!(
        run.status(),
        BenchmarkStatus::Finished {
            result: Err(BenchmarkError::NoSuccess),
            completed: 0,
            ..
        }
    ));
    assert_eq!(service.calibration().snapshot().generation(), previous);
    assert_eq!(
        engine.snapshot().health.observations,
        ObservationCounters::default()
    );
}

#[test]
fn full_management_queue_does_not_gate_preparation_abort_or_stop() {
    let mut engine = engine();
    let (management, _commands) = mpsc::sync_channel(32);
    Arc::get_mut(&mut engine.shared).unwrap().management = management;
    let service = actor_service(&engine);
    let preparation = begin(&service, shape(1, 1));
    let mut replies = Vec::new();
    for _ in 0..32 {
        let (reply, result) = mpsc::channel();
        service
            .shared
            .management
            .try_send(engine::Management::Snapshot(reply))
            .ok()
            .unwrap();
        replies.push(result);
    }
    assert_eq!(
        service.snapshot().unwrap_err().kind(),
        io::ErrorKind::WouldBlock
    );
    let run = preparation.abort(BenchmarkError::Cancelled);
    engine.benchmark_turn(Instant::now(), false);
    assert!(run.status().is_terminal());
    let preparation = begin(&service, shape(1, 1));
    let run = preparation.run();
    run.request_stop();
    let run = preparation.abort(BenchmarkError::Cancelled);
    engine.benchmark_turn(Instant::now(), false);
    assert!(run.status().is_terminal());
}

#[cfg(target_os = "linux")]
#[test]
fn native_capability_captures_ten_retries_and_request_alignment() {
    let directory = Directory::new();
    let file = directory.file();
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
    let service = actor_service(&engine);
    for execution in [Execution::PreferNative, Execution::WorkerRequired] {
        let mut preparation = begin(&service, shape(1, 1));
        preparation
            .add_target(BenchmarkTarget::new(target(&file), OTHER, execution, 4096, 4096).unwrap())
            .unwrap();
        let run = preparation.commit(plan(Duration::from_secs(1))).unwrap();
        prepare_child(&mut engine);
        let (_, policy, _) = engine.benchmark_child_policy().unwrap();
        assert_eq!(
            policy.max_error_retries,
            if execution == Execution::PreferNative {
                10
            } else {
                0
            }
        );
        assert_eq!(
            policy.retry_alignment,
            if execution == Execution::PreferNative {
                NonZeroUsize::new(4096)
            } else {
                None
            }
        );
        run.request_stop();
        engine.benchmark_turn(Instant::now(), false);
        engine.benchmark_turn(Instant::now(), false);
        assert!(run.status().is_terminal());
    }
}

#[test]
fn production_samples_keep_integer_division_and_payload_arithmetic_is_checked() {
    let policy = SamplePolicy::NominalIntegerSeconds(std::num::NonZeroU64::new(5).unwrap());
    assert_eq!(
        crate::benchmark::sample_values(11, 37, policy, Duration::from_secs(700)),
        Ok((2.0, 3.0))
    );
    assert_eq!(
        crate::benchmark::sample_values(0, 0, policy, Duration::from_secs(5)),
        Err(BenchmarkError::NoSuccess)
    );
    assert_eq!(
        crate::benchmark::sample_values(4, 80, policy, Duration::from_secs(5)),
        Err(BenchmarkError::Arithmetic)
    );
    assert_eq!(
        crate::benchmark::owned_payload_bytes_for(usize::MAX, 20, 20, 16),
        Err(BenchmarkError::Arithmetic)
    );
}

#[test]
fn block_cannot_be_reused_before_child_and_inventory_physically_retire() {
    let directory = Directory::new();
    let owner = UnmarkedBlockFile::open(
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
    .unwrap();
    let block = owner.allocate().unwrap();
    let region = owner.region(block, 0..4096).unwrap();
    let mut engine = engine();
    let service = actor_service(&engine);
    let mut preparation = begin(&service, shape(1, 1));
    preparation
        .add_target(
            BenchmarkTarget::new(
                Target::Block(region),
                DEVICE,
                Execution::PreferNative,
                4096,
                4096,
            )
            .unwrap(),
        )
        .unwrap();
    let run = preparation.commit(plan(Duration::from_secs(1))).unwrap();
    prepare_child(&mut engine);
    dispatch(&mut engine);
    let physical = retired(&engine);
    assert!(matches!(
        owner.release(block).unwrap(),
        crate::ReleaseOutcome::Pending
    ));
    assert!(matches!(
        owner.allocate(),
        Err(crate::BlockError::Exhausted)
    ));
    run.request_stop();
    engine.benchmark_turn(Instant::now(), false);
    assert!(!run.status().is_terminal());
    assert!(matches!(
        owner.allocate(),
        Err(crate::BlockError::Exhausted)
    ));
    engine.retired(physical.0, physical.1);
    engine.benchmark_turn(Instant::now(), false);
    assert!(run.status().is_terminal());
    assert_eq!(owner.allocate().unwrap(), block);
    assert!(matches!(
        owner.allocate(),
        Err(crate::BlockError::Exhausted)
    ));
}

#[test]
fn closed_benchmark_ingress_rejects_new_runs_while_normal_io_remains_available() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let preparation = begin(&service, shape(1, 1));
    let run = preparation.run();
    service.stop_benchmarks();
    assert_eq!(
        service.begin_benchmark(shape(1, 1)).err(),
        Some(BenchmarkError::Unavailable)
    );
    assert!(!run.status().is_terminal());
    preparation.abort(BenchmarkError::Cancelled).wait_retired();
    let completion = service
        .write(spec(&file, 4), b"live")
        .unwrap()
        .wait(deadline())
        .unwrap();
    assert!(matches!(completion.status, CompletionStatus::Complete));
    drop(completion);
    service.shutdown().unwrap();
}

#[test]
fn production_preparation_supports_all_positions_and_refuses_target_10241() {
    let directory = Directory::new();
    let file = directory.file();
    let service = service();
    let mut preparation = begin(&service, BenchmarkShape::production());
    let drops = Arc::new(AtomicUsize::new(0));
    for _ in 0..MAX_TARGETS {
        let drops = Arc::clone(&drops);
        preparation
            .add_target(
                BenchmarkTarget::new(
                    target(&file),
                    DEVICE,
                    Execution::PreferNative,
                    2 * 1024 * 1024,
                    4096,
                )
                .unwrap()
                .on_drop(move || {
                    drops.fetch_add(1, Ordering::Relaxed);
                }),
            )
            .unwrap();
    }
    assert_eq!(preparation.check_add(), Err(BenchmarkError::InvalidPlan));
    let metadata = service.snapshot().unwrap().benchmark;
    assert_eq!(
        (metadata.targets, metadata.workloads, metadata.workers),
        (10240, 20, 16)
    );
    assert_eq!(drops.load(Ordering::Relaxed), 0);
    preparation.abort(BenchmarkError::Cancelled).wait_retired();
    assert_eq!(drops.load(Ordering::Relaxed), 10240);
    service.shutdown().unwrap();
}

fn publish_backlog(
    engine: &mut Engine,
    file: &Arc<File>,
    count: usize,
) -> Vec<mpsc::Receiver<Completion>> {
    let mut admissions = Vec::new();
    for _ in 0..count {
        let (options, _) = spec(file, 4).split();
        admissions.push(Admission::begin(Arc::clone(&engine.shared), options).unwrap());
    }
    while engine.snapshot().usage[0].bytes < count * 4 + 4096 {
        engine.drain_gate();
        engine.grant_admission();
    }
    admissions
        .into_iter()
        .map(|mut admission| {
            admission.wait_capacity().unwrap();
            admission
                .prepare(target(file), Direction::Read, &[])
                .unwrap();
            let (sender, receiver) = mpsc::channel();
            admission.publish(sender).unwrap();
            receiver
        })
        .collect()
}

#[test]
#[cfg_attr(
    feature = "ownership-test-support",
    allow(
        clippy::result_large_err,
        reason = "the proof-only owner enlarges Operation returned by the dispatch double"
    )
)]
fn published_backlog_preserves_stop_and_wait_abandonment_through_acceptance() {
    for abandoned in [false, true] {
        for failed_device in [false, true] {
            let directory = Directory::new();
            let file = directory.file();
            file.set_len(4096).unwrap();
            let mut config = config();
            config.admission.max_records = 256;
            config.health.wait_cap = Duration::from_secs(1);
            let mut engine = engine_with(config);
            let service = actor_service(&engine);
            let mut preparation = begin(&service, shape(1, 1));
            preparation.add_target(bench_target(&file)).unwrap();
            let run = preparation
                .commit(read_plan(Duration::from_secs(5)))
                .unwrap();
            engine.benchmark_turn(Instant::now(), false);
            engine.benchmark_turn(Instant::now(), false);
            let ordinary = publish_backlog(&mut engine, &file, 128);
            engine.benchmark_turn(Instant::now(), false);
            let child = engine
                .shared
                .gate
                .lock()
                .unwrap()
                .published
                .back()
                .unwrap()
                .cell
                .id;
            engine.drain_gate();
            assert!(engine
                .shared
                .gate
                .lock()
                .unwrap()
                .published
                .iter()
                .any(|i| i.cell.id == child));
            if !abandoned {
                run.request_stop();
            }
            let after_wait = Instant::now() + Duration::from_secs(6);
            engine.benchmark_turn(after_wait, false);
            assert!(!run.status().is_terminal());
            if failed_device {
                engine.backend_failed(DEVICE, 0, io::Error::from_raw_os_error(5));
            }
            engine.drain_gate();
            engine.drain_gate();
            let ordinary_ids: Vec<_> = engine
                .active
                .keys()
                .copied()
                .filter(|id| *id != child)
                .collect();
            for id in ordinary_ids {
                engine.cancel(id, CompletionStatus::Cancelled);
            }
            engine.dispatch_with(Instant::now(), |_, operation| {
                assert_eq!(
                    operation.buffer().as_slice().len(),
                    4,
                    "benchmark child submitted"
                );
                Err(file_io::SubmitFailure {
                    error: io::ErrorKind::WouldBlock.into(),
                    operation,
                })
            });
            engine.benchmark_turn(after_wait, false);
            assert!(run.status().is_terminal());
            if abandoned {
                assert_eq!(
                    engine.snapshot().health.observations,
                    ObservationCounters::default()
                );
            }
            let ids: Vec<_> = engine.active.keys().copied().collect();
            for id in ids {
                engine.cancel(id, CompletionStatus::Cancelled);
            }
            drop(ordinary);
            assert_eq!(engine.snapshot().usage[0], ResourceUsage::default());
            assert_eq!(run.report().len(), 1);
            assert_eq!(run.report().get(0).unwrap().successful_count, 0);
        }
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
fn accepted_queued_stop_does_not_need_a_controls_scan() {
    let directory = Directory::new();
    let file = directory.file();
    let mut engine = engine();
    let service = actor_service(&engine);
    let mut preparation = begin(&service, shape(1, 1));
    preparation.add_target(bench_target(&file)).unwrap();
    let run = preparation
        .commit(read_plan(Duration::from_secs(5)))
        .unwrap();
    prepare_child(&mut engine);
    assert_eq!(engine.active.len(), 1);
    run.request_stop();
    engine.benchmark_turn(Instant::now(), false);
    engine.dispatch_with(Instant::now(), |_, _| panic!("stopped child submitted"));
    engine.benchmark_turn(Instant::now(), false);
    assert!(run.status().is_terminal());
    assert_eq!(engine.snapshot().usage[0], ResourceUsage::default());
}

#[test]
#[cfg_attr(
    feature = "ownership-test-support",
    allow(
        clippy::result_large_err,
        reason = "the proof-only owner enlarges Operation returned by the dispatch double"
    )
)]
fn unissued_allocation_and_publication_failures_do_not_observe_reads() {
    for reject_publication in [false, true] {
        let directory = Directory::new();
        let file = directory.file();
        let mut engine = engine();
        let service = actor_service(&engine);
        let previous = calibrated(&service);
        let mut preparation = begin(&service, shape(1, 1));
        preparation.add_target(bench_target(&file)).unwrap();
        let run = preparation
            .commit(read_plan(Duration::from_secs(1)))
            .unwrap();
        engine.benchmark_turn(Instant::now(), false);
        let before = engine.snapshot().health;
        let mut allocated = false;
        engine.benchmark_prepare_with(
            |length, alignment| {
                allocated = true;
                if reject_publication {
                    AlignedBuffer::zeroed(length, alignment)
                } else {
                    Err(io::ErrorKind::OutOfMemory.into())
                }
            },
            |engine| {
                if reject_publication {
                    engine.shared.gate.lock().unwrap().accepting = false;
                }
            },
        );
        assert!(allocated);
        assert!(engine.shared.gate.lock().unwrap().published.is_empty());
        assert_eq!(engine.snapshot().usage[0], ResourceUsage::default());
        assert_eq!(engine.snapshot().health.observations, before.observations);
        assert_eq!(engine.snapshot().health.queued, before.queued);
        engine.dispatch_with(Instant::now(), |_, _| panic!("unissued child submitted"));
        engine.benchmark_turn(Instant::now() + Duration::from_secs(2), false);
        assert!(matches!(
            run.status(),
            BenchmarkStatus::Finished {
                result: Err(BenchmarkError::NoSuccess),
                ..
            }
        ));
        assert_eq!(service.calibration().snapshot().generation(), previous);
        assert_eq!(run.report().get(0).unwrap().successful_count, 0);
    }
}

#[test]
fn sealed_reports_keep_raw_counts_latency_and_each_phases_physical_drain() {
    let directory = Directory::new();
    let file = directory.file();
    file.set_len(4096).unwrap();
    let mut engine = engine();
    let service = actor_service(&engine);
    let mut preparation = begin(&service, shape(1, 1));
    preparation.add_target(bench_target(&file)).unwrap();
    let mut workloads = plan(Duration::from_secs(5));
    workloads = CheckedWorkloads::new(
        workloads.workloads().to_vec(),
        OperationPolicy {
            wait: WaitPolicy::NoAdditionalCap,
            ..OperationPolicy::production()
        },
        SamplePolicy::NominalIntegerSeconds(std::num::NonZeroU64::new(5).unwrap()),
    )
    .unwrap();
    let run = preparation.commit(workloads).unwrap();
    let began = Instant::now();
    engine.benchmark_turn(began, false);
    for phase in 0..2 {
        let count = if phase == 0 { 11 } else { 14 };
        for child in 0..count {
            prepare_child(&mut engine);
            dispatch(&mut engine);
            let physical = retired(&engine);
            engine.benchmark_success_after(Duration::from_micros(if phase == 0 { 7 } else { 9 }));
            if child + 1 == count {
                engine.benchmark_turn(began + Duration::from_secs(5 + 9 * phase), false);
                assert_eq!(run.report().len(), phase as usize);
                assert!(!run.status().is_terminal());
                engine.retired(physical.0, physical.1);
                engine.benchmark_turn(began + Duration::from_secs(9 + 5 * phase), false);
            } else {
                engine.retired(physical.0, physical.1);
            }
        }
    }
    assert!(matches!(
        run.status(),
        BenchmarkStatus::Finished { result: Ok(_), .. }
    ));
    let report = run.report();
    assert_eq!(report.len(), 2);
    assert_eq!(report.get(2), None);
    for index in 0..2 {
        let measurement = report.get(index).unwrap();
        assert_eq!(
            measurement.successful_count,
            if index == 0 { 11 } else { 14 }
        );
        assert_eq!(
            measurement.successful_latency_us,
            if index == 0 { 77 } else { 126 }
        );
        assert_eq!(
            measurement.elapsed,
            Duration::from_secs(if index == 0 { 9 } else { 5 })
        );
        let mode = if index == 0 { Mode::Write } else { Mode::Read };
        assert_eq!(measurement.mode, mode);
        let snapshot = service.calibration().snapshot();
        assert_eq!(snapshot.samples(mode)[0].iops, 2.0);
        assert_eq!(
            snapshot.samples(mode)[0].latency_us,
            if index == 0 { 7.0 } else { 9.0 }
        );
    }
    assert_eq!(engine.snapshot().usage[0], ResourceUsage::default());
    assert!(!engine.snapshot().benchmark.run_reserved);
}

#[cfg(feature = "ownership-test-support")]
#[test]
fn actual_worker_and_final_lease_fence_stop_publication() {
    use crate::service::benchmark::proof::{BenchmarkProof, ProofState, Scenario};
    fn until(predicate: impl Fn() -> bool) {
        let deadline = Instant::now() + Duration::from_secs(3);
        while !predicate() {
            assert!(
                Instant::now() < deadline,
                "ownership proof observation deadline"
            );
            thread::yield_now();
        }
    }
    let directory = Directory::new();
    let owner = UnmarkedBlockFile::open(
        &directory.0.join("block"),
        BlockConfig {
            block_size: 4096,
            initial_size: 3 * 4096,
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
    let index = owner.allocate().unwrap();
    let region = owner.region(index, 0..4096).unwrap();
    let service = service();
    let mut preparation = begin(&service, shape(1, 1));
    let state = ProofState::new(Scenario::Stop);
    assert!(state.lease.set(region.watch_final_lease().unwrap()).is_ok());
    preparation.attach_proof(state.clone()).unwrap();
    assert!(preparation.attach_proof(state.clone()).is_err());
    let proof = BenchmarkProof {
        state: state.clone(),
        run: preparation.run(),
    };
    preparation
        .add_target(
            BenchmarkTarget::new(
                Target::Block(region),
                DEVICE,
                Execution::WorkerRequired,
                4096,
                4096,
            )
            .unwrap(),
        )
        .unwrap();
    let run = preparation.commit(plan(Duration::from_secs(30))).unwrap();
    until(|| state.hold.facts().entered);
    let waiter = run.clone();
    run.request_stop();
    let (sent, received) = mpsc::channel();
    struct JoinedWait {
        state: Arc<ProofState>,
        thread: Option<thread::JoinHandle<()>>,
    }
    impl Drop for JoinedWait {
        fn drop(&mut self) {
            self.state.release();
            self.thread.take().unwrap().join().unwrap();
        }
    }
    let joined = JoinedWait {
        state: state.clone(),
        thread: Some(thread::spawn(move || {
            let _ = sent.send(waiter.wait_retired());
        })),
    };
    until(|| state.facts.lock().unwrap().wait_entered || received.try_recv().is_ok());
    until(|| state.facts.lock().unwrap().actor.is_some());
    let facts = *state.facts.lock().unwrap();
    assert_eq!(
        facts.actor.unwrap().2,
        1,
        "selected child must be Draining while worker held"
    );
    assert!(!facts.actor.unwrap().4);
    assert!(!run.status().is_terminal());
    assert!(!state.hold.facts().executed);
    assert!(received.try_recv().is_err());
    state.hold.release();
    let lease = state.lease.get().unwrap();
    until(|| lease.entered());
    assert!(!lease.finished());
    assert!(
        !run.status().is_terminal(),
        "terminal published before final inventory destruction"
    );
    lease.release();
    assert!(matches!(
        received.recv_timeout(Duration::from_secs(3)).unwrap(),
        BenchmarkStatus::Finished {
            result: Err(BenchmarkError::Cancelled),
            ..
        }
    ));
    drop(joined);
    assert!(!state.hold.facts().timed_out);
    assert!(!lease.timed_out());
    assert_eq!(service.snapshot().unwrap().admitted_requests, 0);
    drop(proof);
    owner.release(index).unwrap();
}
