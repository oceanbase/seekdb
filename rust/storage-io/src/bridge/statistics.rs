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

use super::ffi;
use crate::statistics::{CompletionRates, CompletionStatistics, StatisticsError};
use std::sync::Arc;

pub fn metric_integer(value: f64) -> ffi::Metric {
    if value.is_finite() && (-9223372036854775808.0..9223372036854775808.0).contains(&value) {
        ffi::Metric {
            error: 0,
            value: value as i64,
        }
    } else {
        ffi::Metric {
            error: -4002,
            value: 0,
        }
    }
}
pub fn metric_mbps(iops: i64, size: i64) -> ffi::Metric {
    match iops.checked_mul(size) {
        Some(bytes) => ffi::Metric {
            error: 0,
            value: bytes / 1024 / 1024,
        },
        None => ffi::Metric {
            error: -4002,
            value: 0,
        },
    }
}

pub struct StatisticsHandle(pub(super) Option<Arc<CompletionStatistics>>);
pub struct StatisticsSnapshot(Vec<CompletionRates>);

pub fn new_statistics_handle() -> Box<StatisticsHandle> {
    Box::new(StatisticsHandle(None))
}
pub fn new_statistics_snapshot() -> Box<StatisticsSnapshot> {
    Box::new(StatisticsSnapshot(Vec::new()))
}
fn error(error: StatisticsError) -> i32 {
    match error {
        StatisticsError::InvalidLane => -4002,
        StatisticsError::Capacity => -4013,
    }
}
fn rates(value: CompletionRates) -> ffi::CompletionRatesDto {
    ffi::CompletionRatesDto {
        error: 0,
        average_bytes: value.average_bytes,
        iops: value.iops,
        bytes_per_second: value.bytes_per_second,
        total_us: value.average_total_us.min(i64::MAX as u64) as i64,
    }
}
impl StatisticsHandle {
    pub fn statistics_snapshot(&self, now_us: i64, output: &mut StatisticsSnapshot) -> i32 {
        let Some(owner) = &self.0 else {
            return -4006;
        };
        match owner.success_snapshot(now_us) {
            Ok(values) => {
                output.0 = values;
                0
            }
            Err(err) => error(err),
        }
    }
}
impl StatisticsSnapshot {
    pub fn statistics_at(&self, lane: usize) -> ffi::CompletionRatesDto {
        match self.0.get(lane) {
            Some(value) => rates(*value),
            None => {
                let mut result = rates(CompletionRates::default());
                result.error = -4002;
                result
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn metrics_reject_unrepresentable_values() {
        assert_eq!(super::metric_integer(-2.5).value, -2);
        assert_eq!(super::metric_integer(i64::MIN as f64).value, i64::MIN);
        for value in [f64::NAN, f64::INFINITY, i64::MAX as f64] {
            assert_ne!(super::metric_integer(value).error, 0);
        }
        assert_eq!(super::metric_mbps(256, 4096).value, 1);
        assert_ne!(super::metric_mbps(i64::MAX, 2).error, 0);
    }

    #[test]
    fn snapshots_read_native_completion_windows() {
        let owner = Arc::new(CompletionStatistics::new(1, 1).unwrap());
        owner
            .record(
                0,
                crate::statistics::CompletionEvent {
                    outcome: crate::statistics::CompletionOutcome::Success,
                    bytes: 512,
                    total_us: 10,
                },
            )
            .unwrap();
        let handle = StatisticsHandle(Some(owner));
        let mut snapshot = new_statistics_snapshot();
        assert_eq!(handle.statistics_snapshot(1_000_001, &mut snapshot), 0);
        let rates = snapshot.statistics_at(0);
        assert_eq!(rates.iops, 1.0);
        assert_eq!(rates.average_bytes, 512.0);
        assert_eq!(rates.total_us, 10);
        assert_eq!(snapshot.statistics_at(1).error, -4002);
        assert_eq!(
            new_statistics_handle().statistics_snapshot(1, &mut snapshot),
            -4006
        );
    }
}
