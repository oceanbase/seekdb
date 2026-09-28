// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use embedding_response::{build_request, Encoding};

#[test]
fn fields_dimensions_and_empty_input() {
    assert_eq!(
        build_request(&[b"hello", b""], b"model", 3, Encoding::Float).unwrap(),
        br#"{"input":["hello",""],"model":"model","encoding_format":"float","dimensions":3}"#
    );
    for dimension in [0, -1, i64::MIN] {
        assert_eq!(
            build_request(&[], b"", dimension, Encoding::Base64).unwrap(),
            br#"{"input":[],"model":"","encoding_format":"base64"}"#
        );
    }
    assert!(
        String::from_utf8(build_request(&[], b"m", i64::MAX, Encoding::Float).unwrap())
            .unwrap()
            .ends_with("\"dimensions\":9223372036854775807}")
    );
}

#[test]
fn escapes_controls_quotes_backslashes_and_preserves_utf8() {
    let controls: Vec<u8> = (0..32).collect();
    let escaped: String = (0..32).map(|byte| format!("\\u{byte:04x}")).collect();
    let expected = format!("{{\"input\":[\"{escaped}\",\"中文🙂\\\"\\\\/\"],\"model\":\"m\\\"\\u0000\\\\\",\"encoding_format\":\"float\",\"dimensions\":1}}");
    assert_eq!(
        build_request(
            &[&controls, "中文🙂\"\\/".as_bytes()],
            b"m\"\0\\",
            1,
            Encoding::Float
        )
        .unwrap(),
        expected.as_bytes()
    );
}

#[test]
fn no_fixed_buffer_limit_for_model_or_escaped_text() {
    let input = vec![0; 32_000];
    let model = vec![b'"'; 8_000];
    let output = build_request(&[&input], &model, 1, Encoding::Base64).unwrap();
    let expected = format!(
        "{{\"input\":[\"{}\"],\"model\":\"{}\",\"encoding_format\":\"base64\",\"dimensions\":1}}",
        "\\u0000".repeat(input.len()),
        "\\\"".repeat(model.len())
    );
    assert_eq!(output, expected.as_bytes());
}
