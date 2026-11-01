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

use crate::bridge::{ffi, parse as parse_events};
use crate::{
    parse, parse_binary, validate, Document, DuplicatePolicy, ErrorKind, Number, ParseOptions,
};

fn options(policy: DuplicatePolicy) -> ParseOptions {
    ParseOptions {
        duplicate_policy: policy,
        ..ParseOptions::default()
    }
}

#[test]
fn standard_json_corpus() {
    let valid: &[&[u8]] = &[
        br#"null"#,
        br#"true"#,
        br#"false"#,
        br#""text""#,
        br#"[]"#,
        br#"{}"#,
        br#"{"a":[1,-2,3.5e-2,true,false,null],"b":{"c":"d"}}"#,
        br#""\u4f60\u597d\ud83d\ude00""#,
        "\"雪😀\"".as_bytes(),
        br#"0"#,
        br#"-0"#,
        br#"0.0"#,
        br#"-0.0"#,
        br#"1e308"#,
        br#"5e-324"#,
        br#"9223372036854775807"#,
        br#"-9223372036854775808"#,
        br#"18446744073709551615"#,
    ];
    for input in valid {
        validate(input, ParseOptions::default())
            .unwrap_or_else(|error| panic!("{:?}: {:?}", input, error));
    }
    let rejected: &[&[u8]] = &[
        br#"{a:1}"#,
        br#"{'a':1}"#,
        br#"/* comment */null"#,
        br#"{"a":1 // comment
}"#,
        br#"[1,]"#,
        br#"{"a":1,}"#,
        br#"TRUE"#,
        br#"False"#,
        br#"NULL"#,
        br#"+1"#,
        br#".5"#,
        br#"01"#,
        br#"0x10"#,
        br#"NaN"#,
        br#"Infinity"#,
        br#"1."#,
        br#""unterminated"#,
        br#"[1 2]"#,
        br#"true false"#,
        br#""\x20""#,
        b"\"\xff\"",
        br#""\ud800""#,
    ];
    for input in rejected {
        let error = match validate(input, ParseOptions::default()) {
            Err(error) => error,
            Ok(()) => panic!("accepted {:?}", input),
        };
        assert_eq!(error.kind, ErrorKind::Syntax, "{:?}", input);
        assert!(error.byte_offset <= input.len());
    }
}

#[test]
fn numeric_classification_and_boundaries() {
    assert_eq!(
        parse(b"9223372036854775807", ParseOptions::default()).unwrap(),
        Document::Number(Number::U64(i64::MAX as u64))
    );
    assert_eq!(
        parse(b"-9223372036854775808", ParseOptions::default()).unwrap(),
        Document::Number(Number::I64(i64::MIN))
    );
    assert_eq!(
        parse(b"9223372036854775808", ParseOptions::default()).unwrap(),
        Document::Number(Number::U64(9223372036854775808))
    );
    assert_eq!(
        parse(b"4294967295", ParseOptions::default()).unwrap(),
        Document::Number(Number::I64(u32::MAX as i64))
    );
    assert_eq!(
        parse(b"4294967296", ParseOptions::default()).unwrap(),
        Document::Number(Number::U64(u32::MAX as u64 + 1))
    );
    assert_eq!(
        parse(b"18446744073709551615", ParseOptions::default()).unwrap(),
        Document::Number(Number::U64(u64::MAX))
    );
    assert!(matches!(
        parse(b"18446744073709551616", ParseOptions::default()).unwrap(),
        Document::Number(Number::F64(_))
    ));
    assert_eq!(
        parse(b"-0", ParseOptions::default()).unwrap(),
        Document::Number(Number::I64(0))
    );
    let Document::Number(Number::F64(exponent_negative_zero)) =
        parse(b"-0e0", ParseOptions::default()).unwrap()
    else {
        panic!("not a float")
    };
    assert!(exponent_negative_zero.is_sign_negative());
    let Document::Number(Number::F64(negative_zero)) =
        parse(b"-0.0", ParseOptions::default()).unwrap()
    else {
        panic!("not a float")
    };
    assert!(negative_zero.is_sign_negative());
    let Document::Object(object) = parse(
        br#"{"$serde_json::private::Number":"-0","value":-0}"#,
        ParseOptions::default(),
    )
    .unwrap() else {
        panic!("not an object")
    };
    assert_eq!(object[1].1, Document::Number(Number::I64(0)));
}

#[test]
fn duplicate_policies() {
    let input = br#"{"a":1,"b":2,"a":3}"#;
    let Document::Object(overwrite) = parse(input, options(DuplicatePolicy::Overwrite)).unwrap()
    else {
        panic!()
    };
    assert_eq!(overwrite.len(), 2);
    assert_eq!(overwrite[0].1, Document::Number(Number::I64(3)));
    let Document::Object(preserved) = parse(input, options(DuplicatePolicy::Preserve)).unwrap()
    else {
        panic!()
    };
    assert_eq!(preserved.len(), 3);
    let Document::Object(first) = parse(input, options(DuplicatePolicy::KeepFirst)).unwrap() else {
        panic!()
    };
    assert_eq!(first[0].1, Document::Number(Number::I64(1)));
    assert_eq!(
        parse(input, options(DuplicatePolicy::Reject))
            .unwrap_err()
            .kind,
        ErrorKind::DuplicateKey
    );
}

#[test]
fn schema_policy_is_independent() {
    let schema = ParseOptions {
        schema: true,
        duplicate_policy: DuplicatePolicy::KeepFirst,
        ..ParseOptions::default()
    };
    assert!(parse(br##"{"$ref":"#/definitions/a"}"##, schema).is_ok());
    assert_eq!(
        parse(br#"{"$ref":"https://example.com/a"}"#, schema)
            .unwrap_err()
            .kind,
        ErrorKind::UnsupportedSchemaRef
    );
}

#[test]
fn depth_boundary() {
    let shallow = ParseOptions {
        max_depth: 2,
        ..ParseOptions::default()
    };
    assert!(parse(b"[[0]]", shallow).is_ok());
    assert_eq!(
        parse(b"[[[0]]]", shallow).unwrap_err().kind,
        ErrorKind::Depth
    );
    assert_eq!(
        validate(b"[[[0]]]", shallow).unwrap_err().kind,
        ErrorKind::Depth
    );
}

#[test]
fn binary_document() {
    let binary = parse_binary(b"[1]", ParseOptions::default()).unwrap();
    assert_eq!(
        binary,
        [0x1e, 0, 0x80, 0x03, 0, 0, 0, 0, 0x07, 0x40, 1, 6, 1, 0x82]
    );
}

#[test]
fn error_offsets_are_bytes() {
    let input = "\"雪\"\n  ?".as_bytes();
    let error = validate(input, ParseOptions::default()).unwrap_err();
    assert_eq!(error.byte_offset, input.len() - 1);
    let input = "{\"雪\":1,}".as_bytes();
    let error = parse(input, ParseOptions::default()).unwrap_err();
    assert_eq!(error.byte_offset, input.len() - 1);
}

fn bridge_options(policy: DuplicatePolicy) -> ffi::Duplicate {
    match policy {
        DuplicatePolicy::Overwrite => ffi::Duplicate::Overwrite,
        DuplicatePolicy::Preserve => ffi::Duplicate::Preserve,
        DuplicatePolicy::Reject => ffi::Duplicate::Reject,
        DuplicatePolicy::KeepFirst => ffi::Duplicate::KeepFirst,
    }
}

fn document_from_events(events: &[ffi::Event], index: &mut usize) -> Document {
    let event = &events[*index];
    *index += 1;
    match event.kind {
        ffi::EventKind::Null => Document::Null,
        ffi::EventKind::Bool => Document::Bool(event.bool_value),
        ffi::EventKind::I64 => Document::Number(Number::I64(event.i64_value)),
        ffi::EventKind::U64 => Document::Number(Number::U64(event.u64_value)),
        ffi::EventKind::F64 => Document::Number(Number::F64(event.f64_value)),
        ffi::EventKind::String => Document::String(String::from_utf8(event.bytes.clone()).unwrap()),
        ffi::EventKind::StartArray => {
            let mut values = Vec::new();
            while events[*index].kind != ffi::EventKind::EndArray {
                values.push(document_from_events(events, index));
            }
            *index += 1;
            Document::Array(values)
        }
        ffi::EventKind::StartObject => {
            let mut entries = Vec::new();
            while events[*index].kind != ffi::EventKind::EndObject {
                assert_eq!(events[*index].kind, ffi::EventKind::Key);
                let key = String::from_utf8(events[*index].bytes.clone()).unwrap();
                *index += 1;
                entries.push((key, document_from_events(events, index)));
            }
            *index += 1;
            Document::Object(entries)
        }
        _ => panic!("unexpected bridge event"),
    }
}

fn assert_bridge_matches_native(input: &[u8], policy: DuplicatePolicy) {
    let result = parse_events(input, bridge_options(policy), 100, false);
    assert_eq!(result.status, ffi::Status::Ok, "{}", result.message);
    let mut index = 0;
    let document = document_from_events(&result.events, &mut index);
    assert_eq!(index, result.events.len());
    assert_eq!(document, parse(input, options(policy)).unwrap());
}

#[test]
fn direct_bridge_event_policies() {
    let complex = br#"{"a":[1,2],"b":0,"a":{"x":3},"c":4}"#;
    for policy in [
        DuplicatePolicy::Overwrite,
        DuplicatePolicy::Preserve,
        DuplicatePolicy::KeepFirst,
    ] {
        assert_bridge_matches_native(complex, policy);
    }
    let rejected = parse_events(complex, ffi::Duplicate::Reject, 100, false);
    assert_eq!(rejected.status, ffi::Status::DuplicateKey);
    assert!(rejected.events.is_empty());

    let nested = br#"{"a":{"x":1},"b":[true],"a":{"y":[2]},"b":null}"#;
    assert_bridge_matches_native(nested, DuplicatePolicy::Overwrite);
}

#[test]
fn direct_bridge_numbers_schema_and_offsets() {
    for input in [
        &b"9223372036854775807"[..],
        &b"9223372036854775808"[..],
        &b"18446744073709551615"[..],
        &b"18446744073709551616"[..],
        &b"-0"[..],
        &b"-0.0"[..],
    ] {
        assert_bridge_matches_native(input, DuplicatePolicy::Overwrite);
    }

    let schema = parse_events(
        br#"{"$ref":"https://example.com/a"}"#,
        ffi::Duplicate::KeepFirst,
        100,
        true,
    );
    assert_eq!(schema.status, ffi::Status::UnsupportedSchemaRef);

    let input = "{\"雪\":1,}".as_bytes();
    let malformed = parse_events(input, ffi::Duplicate::Overwrite, 100, false);
    assert_eq!(malformed.status, ffi::Status::Syntax);
    assert_eq!(malformed.byte_offset as usize, input.len() - 1);
}
