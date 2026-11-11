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

//! Golden IEEE-754 bit patterns for the pinned pure-Rust `libm` backend.
//!
//! These tests are the cross-platform contract: every target accepts exactly
//! the same bits for the same input, independent of the host libm.  Run with
//! `cargo test -p math` on Linux, macOS and Windows.

use crate::bridge;

fn bits(x: f64) -> u64 {
    x.to_bits()
}

#[test]
fn trig_and_inverse_trig() {
    assert_eq!(bits(bridge::sin(186.0)), 0xbfe343c7b466b0d9);
    assert_eq!(bits(bridge::sin(1.0)), 0x3feaed548f090cee);
    assert_eq!(bits(bridge::sin(-0.5)), 0xbfdeaee8744b05f0);
    assert_eq!(bits(bridge::cos(186.0)), 0xbfe98d21d0f53529);
    assert_eq!(bits(bridge::cos(200.0)), 0x3fdf2e1536cad6e8);
    assert_eq!(bits(bridge::cos(1.0)), 0x3fe14a280fb5068c);
    assert_eq!(bits(bridge::tan(1.0)), 0x3ff8eb245cbee3a6);
    assert_eq!(bits(bridge::tan(0.25)), 0x3fd05785a43c4c56);
    assert_eq!(bits(bridge::asin(0.5)), 0x3fe0c152382d7366);
    assert_eq!(bits(bridge::acos(-1.0)), 0x400921fb54442d18);
    assert_eq!(bits(bridge::atan(1.0)), 0x3fe921fb54442d18);
    assert_eq!(bits(bridge::atan2(1.0, 2.0)), 0x3fddac670561bb4f);
    assert_eq!(bits(bridge::atanh(0.5)), 0x3fe193ea7aad030a);
}

#[test]
fn exp_and_log() {
    assert_eq!(bits(bridge::exp(1.0)), 0x4005bf0a8b14576a);
    assert_eq!(bits(bridge::exp(-1.0)), 0x3fd78b56362cef38);
    assert_eq!(bits(bridge::log(2.0)), 0x3fe62e42fefa39ef);
    assert_eq!(bits(bridge::log2(10.0)), 0x400a934f0979a371);
    assert_eq!(bits(bridge::log10(2.0)), 0x3fd34413509f79ff);
    assert_eq!(bits(bridge::pow(2.0, 0.5)), 0x3ff6a09e667f3bcd);
    assert_eq!(bits(bridge::pow(10.0, -3.0)), 0x3f50624dd2f1a9fc);
    assert_eq!(bits(bridge::cbrt(2.0)), 0x3ff428a2f98d728b);
    assert_eq!(bits(bridge::hypot(3.0, 4.0)), 0x4014000000000000);
}
