/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

use super::*;

fn f64_ok(input: &str, value: f64, consumed: usize) {
    let parsed = parse_f64(input.as_bytes()).unwrap();
    assert_eq!(parsed.value.to_bits(), value.to_bits(), "{input:?}");
    assert_eq!(parsed.consumed, consumed, "{input:?}");
}

fn f32_ok(input: &str, value: f32, consumed: usize) {
    let parsed = parse_f32(input.as_bytes()).unwrap();
    assert_eq!(parsed.value.to_bits(), value.to_bits(), "{input:?}");
    assert_eq!(parsed.consumed, consumed, "{input:?}");
}

#[test]
fn float_prefix_and_rounding() {
    f64_ok(".5", 0.5, 2);
    f64_ok("1.", 1.0, 2);
    f64_ok("1.e2", 100.0, 4);
    f64_ok("1e2x", 100.0, 3);
    f64_ok("0x10", 0.0, 1);
    f64_ok("-0", -0.0, 2);
    f64_ok("1.7976931348623157e308", f64::MAX, 22);
    f64_ok("2.2250738585072014e-308", f64::MIN_POSITIVE, 23);
    f64_ok("3.141592653589793238462643383279", std::f64::consts::PI, 32);
    f32_ok("3.4028235e38", f32::MAX, 12);
    f32_ok("1.17549435e-38", f32::MIN_POSITIVE, 14);
}

#[test]
fn malformed_exponents_stop_before_exponent() {
    for (input, consumed) in [("1e", 1), ("1e+", 1), ("1e-", 1), ("1e+x", 1), ("1.e", 2)] {
        f64_ok(input, 1.0, consumed);
    }
}

#[test]
fn invalid_float_inputs() {
    for input in ["", " ", "+", "-", ".", "+.5", "+1", "--1", "x1"] {
        assert_eq!(
            parse_f64(input.as_bytes()).unwrap_err(),
            invalid(),
            "{input:?}"
        );
    }
}

#[test]
fn float_special_values() {
    f64_ok("inf", f64::INFINITY, 3);
    f64_ok("-Infinity", f64::NEG_INFINITY, 9);
    f64_ok("infinite", f64::INFINITY, 3);
    assert!(parse_f64(b"nan").unwrap().value.is_nan());
    assert!(parse_f64(b"-nan(payload)").unwrap().value.is_nan());
    assert_eq!(parse_f64(b"-nan(payload)").unwrap().consumed, 13);
    assert_eq!(parse_f64(b"nan(bad-char)").unwrap().consumed, 3);
}

#[test]
fn float_range_errors() {
    for input in ["1e309", "-1e309", "1e-4000", "2e-324"] {
        let error = parse_f64(input.as_bytes()).unwrap_err();
        assert_eq!(error.kind, ErrorKind::OutOfRange, "{input:?}");
        assert_eq!(error.consumed, input.len(), "{input:?}");
    }
    f64_ok("0e-4000", 0.0, 7);
    f64_ok("5e-324", f64::from_bits(1), 6);
    assert_eq!(
        parse_f32(b"3.4028236e38").unwrap_err().kind,
        ErrorKind::OutOfRange
    );
    assert_eq!(parse_f32(b"1e-50").unwrap_err().kind, ErrorKind::OutOfRange);
    f32_ok("1e-45", f32::from_bits(1), 5);
}

#[test]
fn unsigned_prefix_and_range() {
    assert_eq!(
        parse_u32(b"01x").unwrap(),
        Parsed {
            value: 1,
            consumed: 2
        }
    );
    assert_eq!(parse_u32(b"4294967295").unwrap().value, u32::MAX);
    assert_eq!(
        parse_u32(b"4294967296").unwrap_err().kind,
        ErrorKind::OutOfRange
    );
    for input in ["", "+1", "-1", " 1", "x1"] {
        assert_eq!(
            parse_u32(input.as_bytes()).unwrap_err().kind,
            ErrorKind::Invalid
        );
    }
}
