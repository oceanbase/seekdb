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

use super::{ffi, ServiceHandle};
use crate::calibration::{
    checked_i64, ByteCount, CalibrationCandidate, CalibrationSnapshot, Mode, Sample,
};
use std::sync::Arc;

pub struct CalibrationView(Arc<CalibrationSnapshot>);
pub fn empty_calibration_view() -> Box<CalibrationView> {
    Box::new(CalibrationView(Arc::new(CalibrationSnapshot::default())))
}
fn mode(raw: i32) -> Option<Mode> {
    match raw {
        0 => Some(Mode::Read),
        1 => Some(Mode::Write),
        _ => None,
    }
}
impl ServiceHandle {
    pub fn calibration_snapshot(&self) -> Box<CalibrationView> {
        self.0
            .as_ref()
            .map_or_else(empty_calibration_view, |service| {
                Box::new(CalibrationView(service.calibration().snapshot()))
            })
    }
    pub fn replace_calibration(&self, samples: &[ffi::CalibrationSample]) -> i32 {
        let Some(service) = &self.0 else {
            return -4013;
        };
        let samples: Option<Vec<_>> = samples
            .iter()
            .map(|sample| {
                Some(Sample {
                    mode: mode(sample.mode)?,
                    size: ByteCount::new(u64::try_from(sample.size).ok()?).ok()?,
                    iops: sample.iops,
                    latency_us: sample.latency_us,
                })
            })
            .collect();
        let Some(samples) = samples else {
            return -4002;
        };
        match CalibrationCandidate::try_from_samples(samples)
            .and_then(|candidate| service.calibration().replace(candidate))
        {
            Ok(_) => 0,
            Err(_) => -4002,
        }
    }
    pub fn reset_calibration(&self) -> i32 {
        let Some(service) = &self.0 else {
            return -4013;
        };
        match service.calibration().reset() {
            Ok(_) => 0,
            Err(_) => -4002,
        }
    }
}
pub fn calibration_integer(value: f64) -> ffi::CalibrationMetric {
    match checked_i64(value) {
        Ok(value) => ffi::CalibrationMetric { error: 0, value },
        Err(_) => ffi::CalibrationMetric {
            error: -4002,
            value: 0,
        },
    }
}
pub fn calibration_mbps(iops: i64, size: i64) -> ffi::CalibrationMetric {
    match iops.checked_mul(size) {
        Some(bytes) => ffi::CalibrationMetric {
            error: 0,
            value: bytes / 1024 / 1024,
        },
        None => ffi::CalibrationMetric {
            error: -4002,
            value: 0,
        },
    }
}

impl CalibrationView {
    pub fn calibrated(&self) -> bool {
        self.0.is_calibrated()
    }
    pub fn calibration_generation(&self) -> u64 {
        self.0.generation()
    }
    pub fn calibration_sample(
        &self,
        raw: i32,
        index: usize,
        output: &mut ffi::CalibrationSample,
    ) -> bool {
        let Some(sample) = mode(raw).and_then(|mode| self.0.samples(mode).get(index)) else {
            return false;
        };
        *output = ffi::CalibrationSample {
            mode: raw,
            size: sample.size.get() as i64,
            iops: sample.iops,
            latency_us: sample.latency_us,
        };
        true
    }
    pub fn calibration_scale(
        &self,
        raw: i32,
        size: i64,
        initialized: bool,
    ) -> ffi::CalibrationScale {
        let mut output = ffi::CalibrationScale {
            error: 0,
            value: 16384.0 / size as f64,
            calibrated: false,
        };
        if initialized {
            if let Some(mode) = mode(raw) {
                if size <= 0 {
                    output.value = 1.0;
                } else {
                    match self.0.scale(mode, ByteCount::new(size as u64).unwrap()) {
                        Ok(scale) => {
                            output.value = scale.value;
                            output.calibrated = scale.calibrated;
                        }
                        Err(_) => {
                            output.error = -4002;
                            output.value = 0.0;
                        }
                    }
                }
            }
        }
        output
    }
    pub fn normalized_iops(
        &self,
        raw: i32,
        size: i64,
        iops: f64,
        initialized: bool,
    ) -> ffi::CalibrationMetric {
        if iops < f64::EPSILON {
            return calibration_integer(0.0);
        }
        let scale = self.calibration_scale(raw, size, initialized);
        if scale.error != 0 {
            return ffi::CalibrationMetric {
                error: scale.error,
                value: 0,
            };
        }
        calibration_integer(if raw == 2 || scale.value < f64::EPSILON {
            size as f64 * iops / 16384.0
        } else {
            iops / scale.value
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn sample_lookup_retains_snapshot_and_preserves_output_on_missing_rows() {
        let store = crate::calibration::Calibration::default();
        let samples = [(Mode::Read, 8192), (Mode::Write, 4096), (Mode::Read, 4096)]
            .into_iter()
            .map(|(mode, size)| Sample {
                mode,
                size: ByteCount::new(size).unwrap(),
                iops: 100.0,
                latency_us: 1.0,
            })
            .collect();
        store
            .replace(CalibrationCandidate::try_from_samples(samples).unwrap())
            .unwrap();
        let view = CalibrationView(store.snapshot());
        store.reset().unwrap();
        drop(store);
        let mut sample = ffi::CalibrationSample {
            mode: -1,
            size: -1,
            iops: 0.0,
            latency_us: 0.0,
        };
        for (index, size) in [4096, 8192].into_iter().enumerate() {
            assert!(view.calibration_sample(0, index, &mut sample));
            assert_eq!(sample.mode, 0);
            assert_eq!(sample.size, size);
        }
        assert!(!view.calibration_sample(0, 2, &mut sample));
        assert!(!view.calibration_sample(-1, 0, &mut sample));
        assert!(!view.calibration_sample(1, usize::MAX, &mut sample));
        assert_eq!(sample.size, 8192);
        assert!(view.calibration_sample(1, 0, &mut sample));
        assert_eq!(sample.mode, 1);
        assert_eq!(sample.size, 4096);
    }
    #[test]
    fn legacy_mode_size_boundary_and_integer_failures() {
        let view = empty_calibration_view();
        for raw in [-1, 2, i32::MAX] {
            let scale = view.calibration_scale(raw, 8192, true);
            assert_eq!(scale.value, 2.0);
            assert!(!scale.calibrated);
            let mut sample = ffi::CalibrationSample {
                mode: 1,
                size: 4096,
                iops: 1.0,
                latency_us: 2.0,
            };
            assert!(!view.calibration_sample(raw, 0, &mut sample));
            assert_eq!(sample.size, 4096);
        }
        assert_eq!(view.calibration_scale(0, -1, true).value, 1.0);
        assert_eq!(view.calibration_scale(1, 0, true).value, 1.0);
        assert!(view.calibration_scale(0, 0, false).value.is_infinite());
        assert_eq!(view.calibration_scale(-1, -8192, true).value, -2.0);
        assert_eq!(view.normalized_iops(2, 16384, 100.5, true).value, 100);
        assert_ne!(view.normalized_iops(2, i64::MAX, f64::MAX, true).error, 0);
        assert_ne!(view.normalized_iops(0, 4096, f64::NAN, true).error, 0);
        assert_ne!(calibration_integer(f64::INFINITY).error, 0);
        assert_eq!(calibration_integer(-2.5).value, -2);
        assert_eq!(calibration_mbps(9007199254740991, 1).value, 8589934591);
        assert_ne!(calibration_mbps(i64::MAX, 2).error, 0);
    }
    #[test]
    fn empty_service_cannot_publish_or_reset() {
        let service = super::super::new_service_handle();
        assert_ne!(service.replace_calibration(&[]), 0);
        assert_ne!(service.reset_calibration(), 0);
        assert!(!service.calibration_snapshot().calibrated());
    }
}
