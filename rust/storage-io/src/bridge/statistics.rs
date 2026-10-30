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
use crate::statistics::{
    CompletionEvent, CompletionOutcome, CompletionRates, CompletionStatistics, StatisticsError,
    SuccessSample,
};

pub struct StatisticsHandle(Option<CompletionStatistics>);
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
        prepare_us: value.average_delays_us[0].min(i64::MAX as u64) as i64,
        schedule_us: value.average_delays_us[1].min(i64::MAX as u64) as i64,
        submit_us: value.average_delays_us[2].min(i64::MAX as u64) as i64,
        device_us: value.average_delays_us[3].min(i64::MAX as u64) as i64,
        total_us: value.average_delays_us[4].min(i64::MAX as u64) as i64,
    }
}
impl StatisticsHandle {
    pub fn initialize_statistics(&mut self, lanes: usize, now_us: i64) -> i32 {
        if let Some(owner) = &self.0 {
            return if owner.lane_count() == lanes {
                0
            } else {
                -4005
            };
        }
        match CompletionStatistics::new(lanes, now_us) {
            Ok(owner) => {
                self.0 = Some(owner);
                0
            }
            Err(err) => error(err),
        }
    }
    pub fn record_completion(&self, event: ffi::CompletionEventDto) -> i32 {
        let Some(owner) = &self.0 else {
            return -4006;
        };
        owner
            .record(
                event.lane,
                CompletionEvent {
                    outcome: if event.success {
                        CompletionOutcome::Success
                    } else {
                        CompletionOutcome::Failure
                    },
                    bytes: event.bytes,
                    delays_us: [
                        event.prepare_us,
                        event.schedule_us,
                        event.submit_us,
                        event.device_us,
                        event.total_us,
                    ],
                },
            )
            .map_or_else(error, |()| 0)
    }
    pub fn statistics_snapshot(
        &self,
        now_us: i64,
        sample: ffi::StatisticsSampleMode,
        output: &mut StatisticsSnapshot,
    ) -> i32 {
        let Some(owner) = &self.0 else {
            return -4006;
        };
        let sample = match sample {
            ffi::StatisticsSampleMode::Cached => SuccessSample::Cached,
            ffi::StatisticsSampleMode::Advance => SuccessSample::Advance,
            ffi::StatisticsSampleMode::Preview => SuccessSample::Preview,
            _ => return -4002,
        };
        match owner.success_snapshot(now_us, sample) {
            Ok(values) => {
                output.0 = values;
                0
            }
            Err(err) => error(err),
        }
    }
    pub fn sample_failed_statistics(&self, lane: usize, now_us: i64) -> ffi::CompletionRatesDto {
        let Some(owner) = &self.0 else {
            let mut result = rates(CompletionRates::default());
            result.error = -4006;
            return result;
        };
        let result = owner.sample_failure(lane, now_us);
        match result {
            Ok(value) => rates(value),
            Err(err) => {
                let mut result = rates(CompletionRates::default());
                result.error = error(err);
                result
            }
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
    fn initialization_retries_preserve_existing_completion_windows() {
        let mut handle = new_statistics_handle();
        assert_eq!(handle.initialize_statistics(usize::MAX, 1), -4013);
        assert_eq!(handle.initialize_statistics(1, 1), 0);
        assert_eq!(
            handle.record_completion(ffi::CompletionEventDto {
                lane: 0,
                success: true,
                bytes: 512,
                prepare_us: 1,
                schedule_us: 2,
                submit_us: 3,
                device_us: 4,
                total_us: 10,
            }),
            0
        );
        assert_eq!(handle.initialize_statistics(1, 100), 0);
        assert_eq!(handle.initialize_statistics(2, 100), -4005);
        let mut snapshot = new_statistics_snapshot();
        assert_eq!(
            handle.statistics_snapshot(
                1_000_001,
                ffi::StatisticsSampleMode::Advance,
                &mut snapshot
            ),
            0
        );
        assert_eq!(snapshot.statistics_at(0).iops, 1.0);
        assert_eq!(snapshot.statistics_at(0).average_bytes, 512.0);
    }
}
