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

//! CXX bridge used by `src/share/ob_math_compat`.

#[cxx::bridge(namespace = "oceanbase::math")]
pub mod ffi {
    extern "Rust" {
        fn sin(x: f64) -> f64;
        fn cos(x: f64) -> f64;
        fn tan(x: f64) -> f64;
        fn asin(x: f64) -> f64;
        fn acos(x: f64) -> f64;
        fn atan(x: f64) -> f64;
        fn atan2(y: f64, x: f64) -> f64;
        fn atanh(x: f64) -> f64;
        fn asinh(x: f64) -> f64;
        fn acosh(x: f64) -> f64;
        fn sinh(x: f64) -> f64;
        fn cosh(x: f64) -> f64;
        fn tanh(x: f64) -> f64;
        fn exp(x: f64) -> f64;
        fn exp2(x: f64) -> f64;
        fn expm1(x: f64) -> f64;
        fn log(x: f64) -> f64;
        fn log2(x: f64) -> f64;
        fn log10(x: f64) -> f64;
        fn log1p(x: f64) -> f64;
        fn pow(x: f64, y: f64) -> f64;
        fn cbrt(x: f64) -> f64;
        fn hypot(x: f64, y: f64) -> f64;
    }
}

pub(crate) fn sin(x: f64) -> f64 {
    libm::sin(x)
}

pub(crate) fn cos(x: f64) -> f64 {
    libm::cos(x)
}

pub(crate) fn tan(x: f64) -> f64 {
    libm::tan(x)
}

pub(crate) fn asin(x: f64) -> f64 {
    libm::asin(x)
}

pub(crate) fn acos(x: f64) -> f64 {
    libm::acos(x)
}

pub(crate) fn atan(x: f64) -> f64 {
    libm::atan(x)
}

pub(crate) fn atan2(y: f64, x: f64) -> f64 {
    libm::atan2(y, x)
}

pub(crate) fn atanh(x: f64) -> f64 {
    libm::atanh(x)
}

pub(crate) fn asinh(x: f64) -> f64 {
    libm::asinh(x)
}

pub(crate) fn acosh(x: f64) -> f64 {
    libm::acosh(x)
}

pub(crate) fn sinh(x: f64) -> f64 {
    libm::sinh(x)
}

pub(crate) fn cosh(x: f64) -> f64 {
    libm::cosh(x)
}

pub(crate) fn tanh(x: f64) -> f64 {
    libm::tanh(x)
}

pub(crate) fn exp(x: f64) -> f64 {
    libm::exp(x)
}

pub(crate) fn exp2(x: f64) -> f64 {
    libm::exp2(x)
}

pub(crate) fn expm1(x: f64) -> f64 {
    libm::expm1(x)
}

pub(crate) fn log(x: f64) -> f64 {
    libm::log(x)
}

pub(crate) fn log2(x: f64) -> f64 {
    libm::log2(x)
}

pub(crate) fn log10(x: f64) -> f64 {
    libm::log10(x)
}

pub(crate) fn log1p(x: f64) -> f64 {
    libm::log1p(x)
}

pub(crate) fn pow(x: f64, y: f64) -> f64 {
    libm::pow(x, y)
}

pub(crate) fn cbrt(x: f64) -> f64 {
    libm::cbrt(x)
}

pub(crate) fn hypot(x: f64, y: f64) -> f64 {
    libm::hypot(x, y)
}
