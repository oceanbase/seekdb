// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.

use embedding_response::{parse_into, Encoding, ParseError};

fn parse(response: &[u8], dimension: i64, encoding: Encoding) -> (i32, Vec<Vec<u32>>) {
    let mut output = vec![vec![42.0]];
    let code = parse_into(response, dimension, encoding, &mut output)
        .map_or_else(ParseError::ob_error_code, |()| 0);
    (
        code,
        output
            .iter()
            .map(|v| v.iter().map(|f| f.to_bits()).collect())
            .collect(),
    )
}

fn floats(response: &str, dimension: i64) -> (i32, Vec<Vec<u32>>) {
    parse(response.as_bytes(), dimension, Encoding::Float)
}

#[test]
fn appends_in_response_order_and_ignores_index() {
    let (code, values) = floats(
        r#"{"data":[{"index":9,"embedding":[1,-2.5]},{"index":0,"embedding":[3,4]}]}"#,
        2,
    );
    assert_eq!(code, 0);
    assert_eq!(
        values,
        vec![
            vec![42_f32.to_bits()],
            vec![1_f32.to_bits(), (-2.5_f32).to_bits()],
            vec![3_f32.to_bits(), 4_f32.to_bits()]
        ]
    );
}

#[test]
fn missing_fields_and_wrong_shapes_remain_distinct() {
    for (response, expected) in [
        ("", -4002),
        (" ", -5411),
        ("null", -4002),
        ("[]", -4002),
        ("{}", -4182),
        (r#"{"data":null}"#, -4002),
        (r#"{"data":[1]}"#, -4002),
        (r#"{"data":[{}]}"#, -4182),
        (r#"{"data":[{"embedding":null}]}"#, -4002),
        (r#"{"data":[{"embedding":"AAAAAA=="}]}"#, -4002),
        (r#"{"data":[{"embedding":[true]}]}"#, -4002),
        (r#"{"data":[{"embedding":["1"]}]}"#, -4002),
        (r#"{"data":[{"embedding":[null]}]}"#, -4002),
        (r#"{"data":[{"embedding":[[],1]}]}"#, -4016),
    ] {
        let (code, output) = floats(response, 1);
        assert_eq!(code, expected, "{response}");
        assert_eq!(output.len(), 1, "{response}");
    }
}

#[test]
fn semantic_errors_keep_only_complete_prior_vectors() {
    for (bad, error) in [
        ("{}", -4182),
        (r#"{"embedding":[2,3]}"#, -4016),
        (r#"{"embedding":[false]}"#, -4002),
    ] {
        let response = format!(r#"{{"data":[{{"embedding":[1]}},{bad},{{"embedding":[3]}}]}}"#);
        let (code, output) = floats(&response, 1);
        assert_eq!(code, error);
        assert_eq!(output, vec![vec![42_f32.to_bits()], vec![1_f32.to_bits()]]);
    }
}

#[test]
fn validates_all_json_before_appending() {
    for suffix in [
        r#", "unused":1e309}"#,
        r#", "unused":[1,]}"#,
        "} trailing",
        r#", "unused":"\uD800"}"#,
    ] {
        let response = format!(r#"{{"data":[{{"embedding":[1]}}]{suffix}"#);
        let (code, output) = floats(&response, 1);
        assert_eq!(code, -5411, "{response}");
        assert_eq!(output.len(), 1);
    }
    assert_eq!(floats(r#"{"data":1e999,"data":[]}"#, 1).0, -5411);
}

#[test]
fn duplicate_and_escaped_keys_use_the_last_value() {
    let (code, output) = floats(
        r#"{"data":false,"d\u0061ta":[{"embedding":[9],"embedd\u0069ng":[2]}]}"#,
        1,
    );
    assert_eq!(code, 0);
    assert_eq!(output[1], vec![2_f32.to_bits()]);
    assert_eq!(floats(r#"{"data":[],"data":null}"#, 1).0, -4002);
    assert_eq!(floats(r#"{"data\u0000":[]}"#, 1).0, -4182);
}

#[test]
fn preserves_legacy_byte_strings_and_nul_termination() {
    assert_eq!(
        parse(b"{\"data\":[],\"unused\":\"\xff\"}", 1, Encoding::Float).0,
        0
    );
    assert_eq!(parse(b"{\"data\":[]}\0garbage", 1, Encoding::Float).0, 0);
    assert_eq!(parse(b"{\"data\":\"\0\"}", 1, Encoding::Float).0, -5411);
    assert_eq!(
        floats(r#"{"data":[],"unused":"\uD83D\uDE00\b\f\n\r\t\/\\\""}"#, 1).0,
        0
    );
    for escape in [r"\uDC00", r"\uD800a", r"\uD800\u0041", r"\u000g", r"\x00"] {
        assert_eq!(
            floats(&format!(r#"{{"data":[],"unused":"{escape}"}}"#), 1).0,
            -5411
        );
    }
}

#[test]
fn container_depth_limit_includes_ignored_metadata() {
    for (depth, expected) in [(100, 0), (101, -5411)] {
        let response = format!(
            r#"{{"data":[],"unused":{}0{}}}"#,
            "[".repeat(depth),
            "]".repeat(depth)
        );
        assert_eq!(floats(&response, 1).0, expected);
    }
}

#[test]
fn number_kinds_rounding_and_signed_zero() {
    let (code, output) = floats(
        r#"{"data":[{"embedding":[-0,-0.0,0e0,-1e-999,1e39,-1e39,18446744073709551615,-9223372036854775808,9007199791611905]}]}"#,
        9,
    );
    assert_eq!(code, 0);
    assert_eq!(
        output[1],
        vec![
            0,
            0x80000000,
            0,
            0x80000000,
            f32::INFINITY.to_bits(),
            f32::NEG_INFINITY.to_bits(),
            (u64::MAX as f32).to_bits(),
            (i64::MIN as f32).to_bits(),
            (9007199791611905_u64 as f32).to_bits()
        ]
    );
    // This integer rounds differently if first converted to f64.
    assert_ne!(
        (9007199791611905_u64 as f32).to_bits(),
        (9007199791611905_u64 as f64 as f32).to_bits()
    );
}

#[test]
fn rejects_invalid_numbers_even_in_metadata() {
    for number in [
        "01", "-", "+1", ".1", "1.", "1e", "1e+", "--1", "NaN", "Infinity", "1e309", "0e999",
        "1e-+1",
    ] {
        assert_eq!(
            floats(&format!(r#"{{"data":[],"n":{number}}}"#), 1).0,
            -5411,
            "{number}"
        );
    }
}

#[test]
fn dimensions_and_empty_batches() {
    for dimension in [-1, 0, 1, i64::MAX] {
        assert_eq!(floats(r#"{"data":[]}"#, dimension).0, 0);
    }
    assert_eq!(floats(r#"{"data":[{"embedding":[]}] }"#, 0).0, -4013);
    assert_eq!(floats(r#"{"data":[{"embedding":[]}] }"#, -1).0, -4016);
}

fn encode(bytes: &[u8]) -> String {
    let alphabet = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut result = String::new();
    for chunk in bytes.chunks(3) {
        let word = (u32::from(chunk[0]) << 16)
            | (u32::from(*chunk.get(1).unwrap_or(&0)) << 8)
            | u32::from(*chunk.get(2).unwrap_or(&0));
        for i in 0..4 {
            result.push(if i > chunk.len() {
                '='
            } else {
                alphabet[((word >> (18 - i * 6)) & 63) as usize] as char
            });
        }
    }
    result
}

#[test]
fn base64_preserves_native_endian_float_bits() {
    let bits = [
        0_u32, 0x80000000, 0x3f800000, 0x7f800000, 0xff800000, 0x7fc01234, 1,
    ];
    let bytes: Vec<_> = bits.iter().flat_map(|value| value.to_ne_bytes()).collect();
    let response = format!(r#"{{"data":[{{"embedding":"{}"}}]}}"#, encode(&bytes));
    let (code, output) = parse(response.as_bytes(), bits.len() as i64, Encoding::Base64);
    assert_eq!(code, 0);
    assert_eq!(output[1], bits);
}

#[test]
fn base64_padding_and_error_codes_match_legacy() {
    for (encoded, dimension, expected) in [
        ("", 0, -4002),
        ("AAA", 1, -4002),
        ("AAAAAA==", 1, 0),
        ("AAAAAP==", 1, 0), // unused low bits are ignored
        ("AAAAAA=", 1, -4024),
        ("AAAAAA", 1, -4024),
        ("AAAAAA===", 1, -4002),
        ("AAAA A==", 1, -4002),
        ("AAAAAA-_", 1, -4002),
        ("AAAA=AAA", 1, -4002),
        ("AAAAAA==", 2, -4016),
        ("AAAAAA==", -1, -4016),
        ("AAAAAAAAAAAAAAAA=", 3, 0),
        ("AAAAAAAAAAAAAAAA==", 3, 0),
        ("AAAAAAAAAAAAAAAAA", 3, -4024),
    ] {
        let response = format!(r#"{{"data":[{{"embedding":"{encoded}"}}]}}"#);
        assert_eq!(
            parse(response.as_bytes(), dimension, Encoding::Base64).0,
            expected,
            "{encoded}"
        );
    }
    assert_eq!(
        parse(br#"{"data":[{"embedding":[0]}]}"#, 1, Encoding::Base64).0,
        -4002
    );
    let (code, output) = parse(
        br#"{"data":[{"embedding":"AAAAAA=="},{"embedding":"bad"}]}"#,
        1,
        Encoding::Base64,
    );
    assert_eq!(code, -4002);
    assert_eq!(output, vec![vec![42_f32.to_bits()], vec![0]]);
}
