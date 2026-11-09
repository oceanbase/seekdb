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

//! Portable SIMD vector kernels.
//!
//! `simsimd` is the single implementation of the float32 L2 square kernel for
//! every platform and architecture; it picks AVX2/AVX-512/NEON/SVE at runtime
//! instead of carrying per-ISA hand-written kernels in the C++ tree. The
//! caller (`ob_vector_l2_distance.cpp`) owns the range contract: float32 lane
//! accumulation can overflow/underflow, so the C++ wrapper falls back to the
//! deterministic binary64 reference for those inputs.
//!
//! The crate must not use nightly-only `std::simd`; the workspace toolchain is
//! pinned to an exact stable version.

mod bridge;
