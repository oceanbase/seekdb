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

//! Completion reporting windows independent of resource admission and logical group names.

use std::sync::Mutex;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CompletionOutcome {
    Success,
    Failure,
}

#[derive(Clone, Copy, Debug)]
pub struct CompletionEvent {
    pub outcome: CompletionOutcome,
    pub bytes: u64,
    /// Preparation, scheduling, submission, device and total durations in microseconds.
    pub delays_us: [u64; 5],
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct CompletionRates {
    pub completed: u128,
    pub bytes: u128,
    pub average_bytes: f64,
    pub iops: f64,
    pub bytes_per_second: i64,
    pub average_delays_us: [u64; 5],
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SuccessSample {
    Cached,
    Advance,
    Preview,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StatisticsError {
    InvalidLane,
    Capacity,
}

#[derive(Clone, Copy, Debug, Default)]
struct Totals {
    count: u128,
    bytes: u128,
    delays: [u128; 5],
}
impl Totals {
    fn record(&mut self, event: CompletionEvent) {
        self.count = self.count.saturating_add(1);
        self.bytes = self.bytes.saturating_add(event.bytes as u128);
        for (total, delay) in self.delays.iter_mut().zip(event.delays_us) {
            *total = total.saturating_add(delay as u128);
        }
    }

    fn rates(self, elapsed_us: Option<u64>) -> CompletionRates {
        let mut rates = CompletionRates {
            completed: self.count,
            bytes: self.bytes,
            ..CompletionRates::default()
        };
        if self.count > 0 {
            rates.average_bytes = self.bytes as f64 / self.count as f64;
            for (average, total) in rates.average_delays_us.iter_mut().zip(self.delays) {
                *average = (total / self.count).min(u64::MAX as u128) as u64;
            }
        }
        if let Some(elapsed) = elapsed_us {
            rates.iops = self.count as f64 * 1_000_000.0 / elapsed as f64;
            rates.bytes_per_second = (self.bytes.saturating_mul(1_000_000) / elapsed as u128)
                .min(i64::MAX as u128) as i64;
        }
        rates
    }
}

#[derive(Debug)]
struct Lane {
    success: Totals,
    failed: Totals,
    success_since_us: i64,
    failed_since_us: i64,
    cached_success: CompletionRates,
}

/// Owns historical reporting only. Lane identities and their lifetime belong to the caller.
#[derive(Debug)]
pub struct CompletionStatistics {
    lanes: Mutex<Vec<Lane>>,
}
impl CompletionStatistics {
    pub fn new(lanes: usize, now_us: i64) -> Result<Self, StatisticsError> {
        if lanes == 0 {
            return Err(StatisticsError::InvalidLane);
        }
        let mut values = Vec::new();
        values
            .try_reserve_exact(lanes)
            .map_err(|_| StatisticsError::Capacity)?;
        values.resize_with(lanes, || Lane {
            success: Totals::default(),
            failed: Totals::default(),
            success_since_us: now_us,
            failed_since_us: now_us,
            cached_success: CompletionRates::default(),
        });
        Ok(Self {
            lanes: Mutex::new(values),
        })
    }

    pub fn lane_count(&self) -> usize {
        self.lanes
            .lock()
            .unwrap_or_else(|poison| poison.into_inner())
            .len()
    }

    pub fn record(&self, lane: usize, event: CompletionEvent) -> Result<(), StatisticsError> {
        let mut lanes = self
            .lanes
            .lock()
            .unwrap_or_else(|poison| poison.into_inner());
        let lane = lanes.get_mut(lane).ok_or(StatisticsError::InvalidLane)?;
        match event.outcome {
            CompletionOutcome::Success => lane.success.record(event),
            CompletionOutcome::Failure => lane.failed.record(event),
        }
        Ok(())
    }

    pub fn success_snapshot(
        &self,
        now_us: i64,
        sample: SuccessSample,
    ) -> Result<Vec<CompletionRates>, StatisticsError> {
        let mut lanes = self
            .lanes
            .lock()
            .unwrap_or_else(|poison| poison.into_inner());
        let mut snapshot = Vec::new();
        snapshot
            .try_reserve_exact(lanes.len())
            .map_err(|_| StatisticsError::Capacity)?;
        for lane in lanes.iter_mut() {
            let rates = if sample == SuccessSample::Cached {
                lane.cached_success
            } else {
                let elapsed =
                    elapsed(lane.success_since_us, now_us).filter(|_| lane.success_since_us > 0);
                lane.success.rates(elapsed)
            };
            if sample == SuccessSample::Advance {
                lane.success = Totals::default();
                lane.success_since_us = now_us;
                lane.cached_success = rates;
            }
            snapshot.push(rates);
        }
        Ok(snapshot)
    }

    pub fn sample_failure(
        &self,
        lane: usize,
        now_us: i64,
    ) -> Result<CompletionRates, StatisticsError> {
        let mut lanes = self
            .lanes
            .lock()
            .unwrap_or_else(|poison| poison.into_inner());
        let lane = lanes.get_mut(lane).ok_or(StatisticsError::InvalidLane)?;
        let Some(elapsed) =
            elapsed(lane.failed_since_us, now_us).filter(|_| lane.failed_since_us != 0)
        else {
            return Ok(CompletionRates::default());
        };
        let rates = lane.failed.rates(Some(elapsed));
        lane.failed = Totals::default();
        lane.failed_since_us = now_us;
        Ok(rates)
    }
}

fn elapsed(previous: i64, now: i64) -> Option<u64> {
    (now > previous).then(|| (now as i128 - previous as i128) as u64)
}

#[cfg(test)]
mod tests;
