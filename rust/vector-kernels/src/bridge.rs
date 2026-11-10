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

        /// Internal f32-returning kernels (k-means, IVF re-rank and PQ table
        /// preparation) so the index machinery shares the same implementation
        /// as the SQL-visible path.
        fn l2_square_flt_f32(a: &[f32], b: &[f32]) -> f32;
        fn l2_norm_square_f32(a: &[f32]) -> f32;
        fn fvec_madd_f32(a: &[f32], bf: f32, b: &[f32], c: &mut [f32]);
        fn fvec_madd_in_place_f32(a: &[f32], bf: f32, b: &mut [f32]);
    }
}

fn l2_square_f32(a: &[f32], b: &[f32]) -> f64 {
    if a.len() != b.len() {
        return f64::NAN;
    }
    f32::sqeuclidean(a, b).unwrap_or(f64::NAN)
}

fn l2_square_flt_f32(a: &[f32], b: &[f32]) -> f32 {
    if a.len() != b.len() {
        return f32::NAN;
    }
    // simsimd's f32 kernels return the Distance (f64) accumulator even for
    // f32 inputs; the internal interface keeps the historical f32 result.
    f32::sqeuclidean(a, b).map(|square| square as f32).unwrap_or(f32::NAN)
}

fn l2_norm_square_f32(a: &[f32]) -> f32 {
    f32::dot(a, a).map(|norm| norm as f32).unwrap_or(f32::NAN)
}

fn fvec_madd_f32(a: &[f32], bf: f32, b: &[f32], c: &mut [f32]) {
    let n = c.len().min(a.len()).min(b.len());
    for i in 0..n {
        c[i] = a[i] + bf * b[i];
    }
}

fn fvec_madd_in_place_f32(a: &[f32], bf: f32, b: &mut [f32]) {
    for (a, b) in a.iter().zip(b.iter_mut()) {
        *b = *a + bf * *b;
    }
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
    fn internal_kernels_match_scalar_loops() {
        let a: Vec<f32> = (0..128).map(|i| (i as f32 * 0.01) - 0.64).collect();
        let b: Vec<f32> = (0..128).map(|i| 0.5 - (i as f32 * 0.007)).collect();
        let expected_square = {
            let mut sum = 0.0f64;
            for i in 0..a.len() {
                let diff = a[i] as f64 - b[i] as f64;
                sum += diff * diff;
            }
            sum
        };
        let square = l2_square_flt_f32(&a, &b);
        assert!((square as f64 - expected_square).abs() <= 1e-5 * expected_square);
        let norm = l2_norm_square_f32(&a);
        let expected_norm: f64 = a.iter().map(|&x| (x as f64) * (x as f64)).sum();
        assert!((norm as f64 - expected_norm).abs() <= 1e-5 * expected_norm);
        let mut c = vec![0.0f32; a.len()];
        fvec_madd_f32(&a, 2.0, &b, &mut c);
        for i in 0..a.len() {
            assert_eq!(c[i], a[i] + 2.0 * b[i]);
        }
    }

    #[test]
    fn length_mismatch_is_nan() {
        assert!(l2_square_f32(&[1.0, 2.0], &[1.0]).is_nan());
        assert!(l2_square_flt_f32(&[1.0, 2.0], &[1.0]).is_nan());
    }

    #[test]
    fn pq_table_update_preserves_input_and_updates_in_place() {
        let norms = [1.0, 4.0, 9.0, 16.0, 25.0];
        let mut table = [0.5, -2.0, 3.0, -4.0, 0.0];
        fvec_madd_in_place_f32(&norms, 2.0, &mut table);
        assert_eq!(table, [2.0, 0.0, 15.0, 8.0, 25.0]);
        assert_eq!(norms, [1.0, 4.0, 9.0, 16.0, 25.0]);
    }
}
