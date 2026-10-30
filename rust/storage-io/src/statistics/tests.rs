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
use std::sync::{Arc, Barrier};

fn event(outcome: CompletionOutcome, bytes: u64, delay: u64) -> CompletionEvent {
    CompletionEvent {
        outcome,
        bytes,
        delays_us: [delay, delay + 1, delay + 2, delay + 3, delay + 4],
    }
}

#[test]
fn successful_windows_preserve_cached_preview_and_advance_contract() {
    let stats = CompletionStatistics::new(2, 1_000_000).unwrap();
    stats
        .record(0, event(CompletionOutcome::Success, 100, 10))
        .unwrap();
    stats
        .record(0, event(CompletionOutcome::Success, 300, 20))
        .unwrap();
    stats
        .record(1, event(CompletionOutcome::Success, 500, 30))
        .unwrap();
    assert_eq!(
        stats
            .success_snapshot(2_000_000, SuccessSample::Cached)
            .unwrap(),
        vec![CompletionRates::default(); 2]
    );
    let preview = stats
        .success_snapshot(2_000_000, SuccessSample::Preview)
        .unwrap();
    assert_eq!(preview[0].completed, 2);
    assert_eq!(preview[0].bytes, 400);
    assert_eq!(preview[0].average_bytes, 200.0);
    assert_eq!(preview[0].iops, 2.0);
    assert_eq!(preview[0].bytes_per_second, 400);
    assert_eq!(preview[0].average_delays_us, [15, 16, 17, 18, 19]);
    assert_eq!(preview[1].average_bytes, 500.0);
    assert_eq!(
        stats
            .success_snapshot(2_000_000, SuccessSample::Advance)
            .unwrap(),
        preview
    );
    stats
        .record(0, event(CompletionOutcome::Success, 800, 40))
        .unwrap();
    assert_eq!(
        stats
            .success_snapshot(3_000_000, SuccessSample::Cached)
            .unwrap(),
        preview
    );
    let next = stats
        .success_snapshot(4_000_000, SuccessSample::Advance)
        .unwrap();
    assert_eq!(next[0].completed, 1);
    assert_eq!(next[0].iops, 0.5);
    assert_eq!(next[0].average_bytes, 800.0);
    assert_eq!(next[1], CompletionRates::default());
    assert_eq!(
        stats
            .success_snapshot(5_000_000, SuccessSample::Advance)
            .unwrap(),
        vec![CompletionRates::default(); 2]
    );
}

#[test]
fn failure_windows_are_independent_and_only_reset_when_sampled() {
    let stats = CompletionStatistics::new(2, 1_000_000).unwrap();
    stats
        .record(0, event(CompletionOutcome::Failure, 600, 60))
        .unwrap();
    stats
        .record(1, event(CompletionOutcome::Failure, 800, 80))
        .unwrap();
    stats
        .record(0, event(CompletionOutcome::Success, 100, 10))
        .unwrap();
    let success = stats
        .success_snapshot(2_000_000, SuccessSample::Advance)
        .unwrap();
    assert_eq!(success[0].completed, 1);
    let first = stats.sample_failure(0, 2_000_000).unwrap();
    assert_eq!(first.completed, 1);
    assert_eq!(first.average_bytes, 600.0);
    assert_eq!(first.average_delays_us, [60, 61, 62, 63, 64]);
    assert_eq!(
        stats.sample_failure(0, 3_000_000).unwrap(),
        CompletionRates::default()
    );
    let skipped = stats.sample_failure(1, 5_000_000).unwrap();
    assert_eq!(skipped.completed, 1);
    assert_eq!(skipped.iops, 0.25);
    assert_eq!(skipped.bytes_per_second, 200);
    assert_eq!(
        stats
            .success_snapshot(6_000_000, SuccessSample::Cached)
            .unwrap(),
        success
    );
}

#[test]
fn equal_and_backwards_clocks_consume_success_but_retain_failures() {
    let stats = CompletionStatistics::new(1, 10).unwrap();
    for outcome in [CompletionOutcome::Success, CompletionOutcome::Failure] {
        stats.record(0, event(outcome, 100, 10)).unwrap();
    }
    assert_eq!(
        stats.sample_failure(0, 10).unwrap(),
        CompletionRates::default()
    );
    assert_eq!(
        stats.sample_failure(0, 9).unwrap(),
        CompletionRates::default()
    );
    let success = stats.success_snapshot(9, SuccessSample::Advance).unwrap()[0];
    assert_eq!(success.completed, 1);
    assert_eq!(success.average_bytes, 100.0);
    assert_eq!(success.iops, 0.0);
    assert_eq!(
        stats.success_snapshot(11, SuccessSample::Advance).unwrap()[0].completed,
        0
    );
    assert_eq!(stats.sample_failure(0, 11).unwrap().completed, 1);
    assert_eq!(stats.sample_failure(0, 12).unwrap().completed, 0);
}

#[test]
fn zero_origin_and_invalid_lane_preserve_existing_window_rules() {
    let stats = CompletionStatistics::new(1, 0).unwrap();
    stats
        .record(0, event(CompletionOutcome::Success, 100, 10))
        .unwrap();
    stats
        .record(0, event(CompletionOutcome::Failure, 100, 10))
        .unwrap();
    assert_eq!(
        stats.success_snapshot(100, SuccessSample::Advance).unwrap()[0].iops,
        0.0
    );
    assert_eq!(
        stats.sample_failure(0, 100).unwrap(),
        CompletionRates::default()
    );
    assert_eq!(
        stats.record(1, event(CompletionOutcome::Success, 100, 10)),
        Err(StatisticsError::InvalidLane)
    );
    assert_eq!(
        stats.sample_failure(1, 100),
        Err(StatisticsError::InvalidLane)
    );
    assert!(matches!(
        CompletionStatistics::new(0, 1),
        Err(StatisticsError::InvalidLane)
    ));
    assert!(matches!(
        CompletionStatistics::new(usize::MAX, 1),
        Err(StatisticsError::Capacity)
    ));
}

#[test]
fn wide_totals_and_large_durations_do_not_wrap_or_narrow() {
    let stats = CompletionStatistics::new(1, i64::MIN).unwrap();
    for _ in 0..2 {
        stats
            .record(
                0,
                CompletionEvent {
                    outcome: CompletionOutcome::Failure,
                    bytes: u64::MAX,
                    delays_us: [u64::MAX; 5],
                },
            )
            .unwrap();
    }
    let rates = stats.sample_failure(0, i64::MAX).unwrap();
    assert_eq!(rates.bytes, u64::MAX as u128 * 2);
    assert_eq!(rates.average_delays_us, [u64::MAX; 5]);
    assert_eq!(rates.bytes_per_second, 2_000_000);
    stats
        .record(0, event(CompletionOutcome::Failure, u64::MAX, 0))
        .unwrap();
    let narrow = CompletionStatistics::new(1, 1).unwrap();
    narrow
        .record(0, event(CompletionOutcome::Failure, u64::MAX, 0))
        .unwrap();
    assert_eq!(
        narrow.sample_failure(0, 2).unwrap().bytes_per_second,
        i64::MAX
    );
}

#[test]
fn concurrent_completion_and_sampling_do_not_lose_or_double_count_events() {
    let stats = Arc::new(CompletionStatistics::new(1, 1).unwrap());
    let barrier = Arc::new(Barrier::new(5));
    let mut workers = Vec::new();
    for worker in 0..4 {
        let stats = stats.clone();
        let barrier = barrier.clone();
        workers.push(std::thread::spawn(move || {
            barrier.wait();
            let outcome = if worker % 2 == 0 {
                CompletionOutcome::Success
            } else {
                CompletionOutcome::Failure
            };
            for _ in 0..2_000 {
                stats.record(0, event(outcome, 512, 10)).unwrap();
            }
        }));
    }
    barrier.wait();
    let (mut successes, mut failures, mut success_bytes, mut failed_bytes) = (0, 0, 0, 0);
    for now in 2..1_002 {
        let success = stats.success_snapshot(now, SuccessSample::Advance).unwrap()[0];
        let failure = stats.sample_failure(0, now).unwrap();
        successes += success.completed;
        failures += failure.completed;
        success_bytes += success.bytes;
        failed_bytes += failure.bytes;
    }
    for worker in workers {
        worker.join().unwrap();
    }
    let success = stats
        .success_snapshot(1_002, SuccessSample::Advance)
        .unwrap()[0];
    let failure = stats.sample_failure(0, 1_002).unwrap();
    assert_eq!(successes + success.completed, 4_000);
    assert_eq!(failures + failure.completed, 4_000);
    assert_eq!(success_bytes + success.bytes, 4_000 * 512);
    assert_eq!(failed_bytes + failure.bytes, 4_000 * 512);
}
