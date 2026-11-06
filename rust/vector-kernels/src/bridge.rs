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

//! CXX bridge used by `src/storage/vector_type/ob_vector_l2_distance.cpp`.

use simsimd::SpatialSimilarity;

#[cxx::bridge(namespace = "oceanbase::common")]
pub mod ffi {
    extern "Rust" {
        /// sum((a[i] - b[i])^2) for equal-length f32 slices.
        ///
        /// Returns NaN when the lengths differ. The result is the simsimd
        /// float32 kernel value; the C++ caller detects the float32 range
        /// defect (inf / zero / subnormal) and falls back to the binary64
        /// reference for those inputs.
        fn l2_square_f32(a: &[f32], b: &[f32]) -> f64;
    }
}

fn l2_square_f32(a: &[f32], b: &[f32]) -> f64 {
    if a.len() != b.len() {
        return f64::NAN;
    }
    f32::sqeuclidean(a, b)
        .map(|square| square as f64)
        .unwrap_or(f64::NAN)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn reference(a: &[f32], b: &[f32]) -> f64 {
        let mut square = 0.0f64;
        for i in 0..a.len() {
            let diff = a[i] as f64 - b[i] as f64;
            square += diff * diff;
        }
        square
    }

    #[test]
    fn matches_binary64_reference_for_normal_inputs() {
        // 1) deterministic pseudo-random vectors in the usual vector-search
        //    magnitude range; simsimd is an f32 accumulation, so allow a
        //    relative error of a few float32 ulp.
        let mut state = 0x9e3779b97f4a7c15u64;
        for dim in [3usize, 5, 16, 128, 768] {
            for _ in 0..32 {
                let mut a = Vec::with_capacity(dim);
                let mut b = Vec::with_capacity(dim);
                for _ in 0..dim {
                    state = state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
                    let x = ((state >> 40) as f32 / (1u64 << 24) as f32) - 0.5;
                    state = state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
                    let y = ((state >> 40) as f32 / (1u64 << 24) as f32) - 0.5;
                    a.push(x);
                    b.push(y);
                }
                let expected = reference(&a, &b);
                let actual = l2_square_f32(&a, &b);
                let tolerance = 1e-5 * expected.abs().max(1e-30);
                assert!(
                    (actual - expected).abs() <= tolerance,
                    "dim={dim} actual={actual} expected={expected}"
                );
            }
        }
    }

    #[test]
    fn length_mismatch_is_nan() {
        assert!(l2_square_f32(&[1.0, 2.0], &[1.0]).is_nan());
    }
}
