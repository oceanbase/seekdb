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

fn event(outcome: CompletionOutcome, bytes: u64, total_us: u64) -> CompletionEvent {
    CompletionEvent {
        outcome,
        bytes,
        total_us,
    }
}

#[test]
fn successful_windows_report_rates_then_advance() {
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
    let first = stats.success_snapshot(2_000_000).unwrap();
    assert_eq!(first[0].completed, 2);
    assert_eq!(first[0].bytes, 400);
    assert_eq!(first[0].average_bytes, 200.0);
    assert_eq!(first[0].iops, 2.0);
    assert_eq!(first[0].bytes_per_second, 400);
    assert_eq!(first[0].average_total_us, 15);
    assert_eq!(first[1].average_bytes, 500.0);
    stats
        .record(0, event(CompletionOutcome::Success, 800, 40))
        .unwrap();
    let next = stats.success_snapshot(4_000_000).unwrap();
    assert_eq!(next[0].completed, 1);
    assert_eq!(next[0].iops, 0.5);
    assert_eq!(next[0].average_bytes, 800.0);
    assert_eq!(next[1], CompletionRates::default());
    assert_eq!(
        stats.success_snapshot(5_000_000).unwrap(),
        vec![CompletionRates::default(); 2]
    );
}

#[test]
fn failures_do_not_inflate_successful_completion_rates() {
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
    let success = stats.success_snapshot(2_000_000).unwrap();
    assert_eq!(success[0].completed, 1);
    assert_eq!(success[0].bytes, 100);
    assert_eq!(success[0].average_total_us, 10);
    assert_eq!(success[1], CompletionRates::default());
}

#[test]
fn equal_and_backwards_clocks_advance_without_dividing_by_zero() {
    let stats = CompletionStatistics::new(1, 10).unwrap();
    for now in [10, 9] {
        stats
            .record(0, event(CompletionOutcome::Success, 100, 10))
            .unwrap();
        let success = stats.success_snapshot(now).unwrap()[0];
        assert_eq!(success.completed, 1);
        assert_eq!(success.average_bytes, 100.0);
        assert_eq!(success.iops, 0.0);
    }
    assert_eq!(stats.success_snapshot(11).unwrap()[0].completed, 0);
}

#[test]
fn zero_origin_and_invalid_lane_preserve_existing_window_rules() {
    let stats = CompletionStatistics::new(1, 0).unwrap();
    stats
        .record(0, event(CompletionOutcome::Success, 100, 10))
        .unwrap();
    assert_eq!(stats.success_snapshot(100).unwrap()[0].iops, 0.0);
    assert_eq!(
        stats.record(1, event(CompletionOutcome::Success, 100, 10)),
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
    assert_eq!(elapsed(i64::MIN, i64::MAX), Some(u64::MAX));
    let stats = CompletionStatistics::new(1, 1).unwrap();
    for _ in 0..2 {
        stats
            .record(0, event(CompletionOutcome::Success, u64::MAX, u64::MAX))
            .unwrap();
    }
    let rates = stats.success_snapshot(i64::MAX).unwrap()[0];
    assert_eq!(rates.bytes, u64::MAX as u128 * 2);
    assert_eq!(rates.average_total_us, u64::MAX);
    assert_eq!(rates.bytes_per_second, 4_000_000);
    let narrow = CompletionStatistics::new(1, 1).unwrap();
    narrow
        .record(0, event(CompletionOutcome::Success, u64::MAX, 0))
        .unwrap();
    assert_eq!(
        narrow.success_snapshot(2).unwrap()[0].bytes_per_second,
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
    let (mut successes, mut success_bytes) = (0, 0);
    for now in 2..1_002 {
        let success = stats.success_snapshot(now).unwrap()[0];
        successes += success.completed;
        success_bytes += success.bytes;
    }
    for worker in workers {
        worker.join().unwrap();
    }
    let success = stats.success_snapshot(1_002).unwrap()[0];
    assert_eq!(successes + success.completed, 4_000);
    assert_eq!(success_bytes + success.bytes, 4_000 * 512);
}
