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

use std::num::NonZeroU64;
use std::sync::{Arc, RwLock};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Mode {
    Read,
    Write,
}
impl Mode {
    fn index(self) -> usize {
        match self {
            Self::Read => 0,
            Self::Write => 1,
        }
    }
}
#[derive(Clone, Copy, Debug, Eq, PartialEq, Ord, PartialOrd)]
pub struct ByteCount(NonZeroU64);
impl ByteCount {
    pub fn new(value: u64) -> Result<Self, CalibrationError> {
        if value > i64::MAX as u64 {
            return Err(CalibrationError::InvalidSize);
        }
        NonZeroU64::new(value)
            .map(Self)
            .ok_or(CalibrationError::InvalidSize)
    }
    pub fn get(self) -> u64 {
        self.0.get()
    }
}
#[derive(Clone, Copy, Debug)]
pub struct Sample {
    pub mode: Mode,
    pub size: ByteCount,
    pub latency_us: f64,
    pub iops: f64,
}
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CalibrationError {
    InvalidSize,
    InvalidSample,
    MissingMode,
    DuplicateSize,
    Arithmetic,
    GenerationOverflow,
}
#[derive(Debug)]
pub struct CalibrationCandidate {
    modes: [Vec<Sample>; 2],
    baseline: f64,
}
#[derive(Debug, Default)]
pub struct CalibrationSnapshot {
    generation: u64,
    data: Option<CalibrationCandidate>,
}
#[derive(Clone, Copy, Debug)]
pub struct IopsScale {
    pub value: f64,
    pub calibrated: bool,
}
pub struct Calibration {
    accepted: RwLock<Arc<CalibrationSnapshot>>,
}

fn finite(value: f64) -> Result<f64, CalibrationError> {
    if value.is_finite() {
        Ok(value)
    } else {
        Err(CalibrationError::Arithmetic)
    }
}
/// The upper bound is exclusive because i64::MAX rounds to 2^63 as f64.
pub fn checked_i64(value: f64) -> Result<i64, CalibrationError> {
    if value.is_finite() && (-9223372036854775808.0..9223372036854775808.0).contains(&value) {
        Ok(value as i64)
    } else {
        Err(CalibrationError::Arithmetic)
    }
}
fn interpolate(
    samples: &[Sample],
    size: ByteCount,
    latency: bool,
) -> Result<f64, CalibrationError> {
    let query = size.get();
    let index = samples.partition_point(|sample| sample.size < size);
    let value = if index == samples.len() {
        let tail = samples.last().ok_or(CalibrationError::MissingMode)?;
        if latency {
            query as f64 / tail.size.get() as f64 * tail.latency_us
        } else {
            tail.iops * tail.size.get() as f64 / query as f64
        }
    } else {
        let found = &samples[index];
        if index == 0 || found.size == size {
            if latency {
                found.latency_us
            } else {
                found.iops
            }
        } else {
            let previous = &samples[index - 1];
            let width = (found.size.get() - previous.size.get()) as f64;
            let offset = (query - previous.size.get()) as f64;
            if latency {
                let slope = (found.latency_us - previous.latency_us) / width;
                previous.latency_us + slope * offset
            } else {
                let delta = checked_i64(found.iops - previous.iops)?;
                previous.iops + delta as f64 * (offset / width)
            }
        }
    };
    finite(value)
}
impl CalibrationCandidate {
    pub fn try_from_samples(samples: Vec<Sample>) -> Result<Self, CalibrationError> {
        let mut modes: [Vec<Sample>; 2] = [Vec::new(), Vec::new()];
        for sample in samples {
            if !sample.iops.is_finite()
                || !sample.latency_us.is_finite()
                || sample.iops <= f64::EPSILON
                || sample.latency_us <= f64::EPSILON
            {
                return Err(CalibrationError::InvalidSample);
            }
            modes[sample.mode.index()].push(sample);
        }
        for samples in &mut modes {
            if samples.is_empty() {
                return Err(CalibrationError::MissingMode);
            }
            samples.sort_unstable_by_key(|sample| sample.size);
            for pair in samples.windows(2) {
                if pair[0].size == pair[1].size {
                    return Err(CalibrationError::DuplicateSize);
                }
                checked_i64(pair[1].iops - pair[0].iops)?;
            }
        }
        let baseline = interpolate(&modes[0], ByteCount::new(16384)?, false)?;
        if baseline <= f64::EPSILON {
            return Err(CalibrationError::Arithmetic);
        }
        Ok(Self { modes, baseline })
    }
}
impl Default for Calibration {
    fn default() -> Self {
        Self {
            accepted: RwLock::new(Arc::new(CalibrationSnapshot::default())),
        }
    }
}
impl Calibration {
    pub fn snapshot(&self) -> Arc<CalibrationSnapshot> {
        Arc::clone(&self.accepted.read().unwrap_or_else(|p| p.into_inner()))
    }
    pub fn replace(&self, candidate: CalibrationCandidate) -> Result<u64, CalibrationError> {
        self.publish(Some(candidate))
    }
    pub fn reset(&self) -> Result<u64, CalibrationError> {
        self.publish(None)
    }
    fn publish(&self, data: Option<CalibrationCandidate>) -> Result<u64, CalibrationError> {
        let mut next = Arc::new(CalibrationSnapshot {
            generation: 0,
            data,
        });
        let mut accepted = self.accepted.write().unwrap_or_else(|p| p.into_inner());
        let Some(generation) = accepted.generation.checked_add(1) else {
            drop(accepted);
            return Err(CalibrationError::GenerationOverflow);
        };
        Arc::get_mut(&mut next).unwrap().generation = generation;
        let old = std::mem::replace(&mut *accepted, next);
        drop(accepted);
        drop(old);
        Ok(generation)
    }
}
impl CalibrationSnapshot {
    pub fn generation(&self) -> u64 {
        self.generation
    }
    pub fn is_calibrated(&self) -> bool {
        self.data.is_some()
    }
    pub fn samples(&self, mode: Mode) -> &[Sample] {
        self.data
            .as_ref()
            .map_or(&[], |data| &data.modes[mode.index()])
    }
    pub fn iops(&self, mode: Mode, size: ByteCount) -> Result<Option<f64>, CalibrationError> {
        self.data
            .as_ref()
            .map(|data| interpolate(&data.modes[mode.index()], size, false))
            .transpose()
    }
    pub fn latency_us(&self, mode: Mode, size: ByteCount) -> Result<Option<f64>, CalibrationError> {
        self.data
            .as_ref()
            .map(|data| interpolate(&data.modes[mode.index()], size, true))
            .transpose()
    }
    pub fn scale(&self, mode: Mode, size: ByteCount) -> Result<IopsScale, CalibrationError> {
        match &self.data {
            Some(data) => Ok(IopsScale {
                value: finite(
                    interpolate(&data.modes[mode.index()], size, false)? / data.baseline,
                )?,
                calibrated: true,
            }),
            None => Ok(IopsScale {
                value: 16384.0 / size.get() as f64,
                calibrated: false,
            }),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn bytes(value: u64) -> ByteCount {
        ByteCount::new(value).unwrap()
    }
    fn samples() -> Vec<Sample> {
        vec![
            Sample {
                mode: Mode::Write,
                size: bytes(32768),
                iops: 50.9,
                latency_us: 17.3,
            },
            Sample {
                mode: Mode::Read,
                size: bytes(32768),
                iops: 100.8,
                latency_us: 7.3,
            },
            Sample {
                mode: Mode::Read,
                size: bytes(4096),
                iops: 200.2,
                latency_us: 1.1,
            },
            Sample {
                mode: Mode::Write,
                size: bytes(4096),
                iops: 150.3,
                latency_us: 2.7,
            },
        ]
    }
    #[test]
    fn numerical_contract_and_snapshot_lifetime() {
        let store = Calibration::default();
        let empty = store.snapshot();
        assert!(!empty.scale(Mode::Read, bytes(8192)).unwrap().calibrated);
        assert_eq!(empty.scale(Mode::Read, bytes(8192)).unwrap().value, 2.0);
        assert_eq!(
            store.replace(CalibrationCandidate::try_from_samples(samples()).unwrap()),
            Ok(1)
        );
        let old = store.snapshot();
        assert_eq!(old.samples(Mode::Write)[0].size, bytes(4096));
        assert_eq!(old.iops(Mode::Read, bytes(1)).unwrap(), Some(200.2));
        assert_eq!(old.iops(Mode::Read, bytes(32768)).unwrap(), Some(100.8));
        let expected = 200.2 + (-99i64) as f64 * (12288.0 / 28672.0);
        assert_eq!(old.iops(Mode::Read, bytes(16384)).unwrap(), Some(expected));
        assert_eq!(old.scale(Mode::Read, bytes(16384)).unwrap().value, 1.0);
        assert_eq!(
            old.latency_us(Mode::Read, bytes(16384)).unwrap(),
            Some(1.1 + ((7.3 - 1.1) / 28672.0) * 12288.0)
        );
        assert_eq!(
            old.iops(Mode::Write, bytes(65536)).unwrap(),
            Some(50.9 * 32768.0 / 65536.0)
        );
        assert_eq!(
            old.latency_us(Mode::Write, bytes(65536)).unwrap(),
            Some(65536.0 / 32768.0 * 17.3)
        );
        assert_eq!(store.reset(), Ok(2));
        drop(store);
        assert_eq!(old.generation(), 1);
        assert_eq!(old.samples(Mode::Write).len(), 2);
        assert!(!empty.is_calibrated());
    }
    #[test]
    fn invalid_replacement_preserves_all_accepted_data() {
        let store = Calibration::default();
        store
            .replace(CalibrationCandidate::try_from_samples(samples()).unwrap())
            .unwrap();
        let old = store.snapshot();
        for value in [
            f64::NAN,
            f64::INFINITY,
            f64::NEG_INFINITY,
            0.0,
            -1.0,
            f64::EPSILON,
        ] {
            for latency in [false, true] {
                let mut invalid = samples();
                if latency {
                    invalid[0].latency_us = value;
                } else {
                    invalid[0].iops = value;
                }
                assert!(CalibrationCandidate::try_from_samples(invalid)
                    .and_then(|c| store.replace(c))
                    .is_err());
                assert!(Arc::ptr_eq(&old, &store.snapshot()));
            }
        }
        let mut duplicate = samples();
        duplicate.push(duplicate[0]);
        assert_eq!(
            CalibrationCandidate::try_from_samples(duplicate).unwrap_err(),
            CalibrationError::DuplicateSize
        );
        let mut overflow = samples();
        overflow[0].iops = 9223372036854775808.0;
        assert!(CalibrationCandidate::try_from_samples(overflow).is_err());
        assert!(CalibrationCandidate::try_from_samples(Vec::new()).is_err());
        assert!(ByteCount::new(0).is_err());
        assert!(ByteCount::new(i64::MAX as u64 + 1).is_err());
        assert!(ByteCount::new(i64::MAX as u64).is_ok());
        assert!(checked_i64(9223372036854775808.0).is_err());
        assert_eq!(checked_i64(-9223372036854775808.0), Ok(i64::MIN));
        assert!(checked_i64(f64::NAN).is_err());
    }
    #[test]
    fn derived_baseline_and_query_overflow_are_errors() {
        let mut input = samples();
        input.retain(|sample| sample.size == bytes(4096));
        input[0].iops = f64::MAX;
        assert!(CalibrationCandidate::try_from_samples(input).is_err());
        let mut input = samples();
        input[0].latency_us = f64::MAX;
        let store = Calibration::default();
        store
            .replace(CalibrationCandidate::try_from_samples(input).unwrap())
            .unwrap();
        assert!(store
            .snapshot()
            .latency_us(Mode::Write, bytes(i64::MAX as u64))
            .is_err());
    }
    #[test]
    fn generation_overflow_preserves_snapshot() {
        let old = Arc::new(CalibrationSnapshot {
            generation: u64::MAX,
            data: None,
        });
        let store = Calibration {
            accepted: RwLock::new(old.clone()),
        };
        assert_eq!(store.reset(), Err(CalibrationError::GenerationOverflow));
        assert!(Arc::ptr_eq(&old, &store.snapshot()));
    }
}
