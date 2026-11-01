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
use crate::{binary, Query, SearchOptions};
use crate::{
    parse, parse_binary, validate, BinaryDocument, BinaryErrorKind, Document, DuplicatePolicy,
    ErrorKind, JsonType, Number, ParseOptions, Path, PathErrorKind, PathStep, QueryOptions,
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
fn binary_document_reads_borrowed_and_owned_documents() {
    let input = br#"{"a":[1,-2,3.5,true,null,"x"],"b":{"c":18446744073709551615}}"#;
    let bytes = parse_binary(input, ParseOptions::default()).unwrap();
    let borrowed = BinaryDocument::new(bytes.as_slice()).unwrap();
    assert_eq!(
        borrowed.root().unwrap().json_type().unwrap(),
        JsonType::Object
    );
    assert_eq!(borrowed.depth().unwrap(), 3);
    let owned = BinaryDocument::new(bytes).unwrap();
    assert_eq!(owned.depth().unwrap(), 3);
    let array = owned.root().unwrap().value(0).unwrap();
    assert_eq!(array.json_type().unwrap(), JsonType::Array);
    assert_eq!(array.len().unwrap(), 6);
    assert_eq!(array.value(0).unwrap().json_type().unwrap(), JsonType::Int);
    assert_eq!(
        array.value(5).unwrap().json_type().unwrap(),
        JsonType::String
    );
    assert_eq!(array.value(0).unwrap().number().unwrap(), Number::I64(1));
    assert_eq!(array.value(1).unwrap().number().unwrap(), Number::I64(-2));
    assert_eq!(array.value(2).unwrap().number().unwrap(), Number::F64(3.5));
    assert!(array.value(3).unwrap().boolean().unwrap());
    assert_eq!(array.value(5).unwrap().string_bytes().unwrap(), b"x");
    let root = owned.root().unwrap();
    assert_eq!(root.key(0).unwrap(), b"a");
    assert_eq!(root.key(1).unwrap(), b"b");
    let nested = root.member(b"b").unwrap().unwrap();
    assert_eq!(
        nested.value(0).unwrap().number().unwrap(),
        Number::U64(u64::MAX)
    );
    assert!(root.member(b"missing").unwrap().is_none());
}

#[test]
fn binary_document_reads_forward_values() {
    let mut bytes = vec![0; 8];
    bytes.extend_from_slice(&[7, 0x40, 1, 6, 0, 31, 2, 42]);
    let header = 30u64 | (14u64 << 22);
    bytes[..8].copy_from_slice(&header.to_le_bytes());
    let document = BinaryDocument::new(bytes).unwrap();
    assert_eq!(document.depth().unwrap(), 2);
    let child = document.root().unwrap().value(0).unwrap();
    assert_eq!(child.json_type().unwrap(), JsonType::Int);
}

#[test]
fn binary_type_numbers_match_sql_node_types() {
    assert_eq!(
        BinaryDocument::new([32])
            .unwrap()
            .root()
            .unwrap()
            .json_type()
            .unwrap(),
        JsonType::MySqlDate
    );
    assert_eq!(
        BinaryDocument::new([33])
            .unwrap()
            .root()
            .unwrap()
            .json_type()
            .unwrap(),
        JsonType::MySqlDatetime
    );
    assert_eq!(JsonType::MySqlDate as u8, 30);
    assert_eq!(JsonType::MySqlDatetime as u8, 31);
}

#[test]
fn binary_document_rejects_malformed_input() {
    for bytes in [
        Vec::new(),
        vec![30],
        vec![6, 0x40, 1, 4],
        vec![7, 0x40, 1, 5, 0, 0, 2],
        vec![200],
    ] {
        assert!(BinaryDocument::new(bytes).is_err());
    }
    let mut bad_forward = vec![0; 8];
    bad_forward.extend_from_slice(&[7, 0x40, 1, 6, 0, 31]);
    let header = 30u64 | (14u64 << 22);
    bad_forward[..8].copy_from_slice(&header.to_le_bytes());
    let error = BinaryDocument::new(bad_forward)
        .unwrap()
        .depth()
        .unwrap_err();
    assert_eq!(error.kind, BinaryErrorKind::Truncated);
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

#[test]
fn mysql_path_parser_covers_standard_steps_and_errors() {
    let path = Path::parse_mysql(
        " $ . \"a\\u002eb\" [ 0 ] [last-1] [1 to last-2] .* [*] **.雪 ".as_bytes(),
    )
    .unwrap();
    assert_eq!(path.steps().len(), 8);
    assert_eq!(path.steps()[0], PathStep::Member("a.b".to_owned()));
    assert!(path.can_match_many());
    for (text, expected) in [
        ("$.a]", "a]"),
        ("$.a}b", "a}b"),
        ("$.😀", "😀"),
        ("$.①", "①"),
        ("$.\\u0061", "a"),
    ] {
        let path = Path::parse_mysql(text.as_bytes()).unwrap();
        assert_eq!(path.steps(), &[PathStep::Member(expected.to_owned())]);
    }

    for invalid in [
        "",
        "a",
        "$.",
        "$.0a",
        "$.a-b",
        "$.a(b",
        "$.a\x01b",
        "$.\"unterminated",
        "$.\"\\ud800\"",
        "$[",
        "$[]",
        "$[1",
        "$[1to2]",
        "$[2 to 1]",
        "$[last-1 to last-2]",
        "$[4294967296]",
        "$**",
        "$** ",
        "$***.a",
    ] {
        assert!(Path::parse_mysql(invalid.as_bytes()).is_err(), "{invalid}");
    }
    let too_long = "$".to_owned() + &".a".repeat(101);
    assert_eq!(
        Path::parse_mysql(too_long.as_bytes()).unwrap_err().kind,
        PathErrorKind::TooManyNodes
    );
}

#[test]
fn mysql_path_query_preserves_binary_values_and_result_order() {
    let bytes = parse_binary(
        "{\"a\":[0,1,2,3],\"b\":{\"a\":5},\"雪\":{\"x\":true},\"a.b\":9}".as_bytes(),
        ParseOptions::default(),
    )
    .unwrap();
    let document = BinaryDocument::new(bytes).unwrap();
    let root = document.root().unwrap();

    let query = |path: &str| {
        Path::parse_mysql(path.as_bytes())
            .unwrap()
            .query(root, QueryOptions::default())
            .unwrap()
    };
    let single = |path: &str| {
        let values = query(path);
        assert_eq!(values.len(), 1, "{path}");
        values[0].clone()
    };

    assert_eq!(single("$.a[1]").number().unwrap(), Number::I64(1));
    assert_eq!(single("$.a[last-1]").number().unwrap(), Number::I64(2));
    assert_eq!(single("$[0]").json_type().unwrap(), JsonType::Object);
    assert_eq!(single("$.\"a.b\"").number().unwrap(), Number::I64(9));
    assert!(single("$.雪.x").boolean().unwrap());
    assert_eq!(query("$.a[1 to 2]").len(), 2);
    assert_eq!(query("$.a[*]").len(), 4);
    assert_eq!(query("$.b.*").len(), 1);
    let recursive = query("$**.a");
    assert_eq!(recursive.len(), 2);
    assert_eq!(recursive[0].json_type().unwrap(), JsonType::Array);
    assert_eq!(recursive[1].number().unwrap(), Number::I64(5));
    assert_eq!(query("$.missing").len(), 0);
    assert_eq!(query("$.a[99]").len(), 0);

    let first = Path::parse_mysql(b"$**.a")
        .unwrap()
        .query(
            root,
            QueryOptions {
                first_only: true,
                ..QueryOptions::default()
            },
        )
        .unwrap();
    assert_eq!(first.len(), 1);
}

#[test]
fn extraction_builds_single_and_array_results_in_rust() {
    let text = "{\"a\":[-1000,1,2,300],\"b\":{\"a\":5},\"s\":\"雪\"}".as_bytes();
    let single = |path: &str| {
        let document = Query::from_text(text, ParseOptions::default()).unwrap();
        let path = Path::parse_mysql(path.as_bytes()).unwrap();
        let bytes = document.extract(&[path]).unwrap().unwrap();
        BinaryDocument::new(bytes).unwrap()
    };
    assert_eq!(
        single("$.a[0]").root().unwrap().number().unwrap(),
        Number::I64(-1000)
    );
    assert_eq!(
        single("$.a[3]").root().unwrap().number().unwrap(),
        Number::I64(300)
    );
    assert_eq!(
        single("$.s").root().unwrap().string_bytes().unwrap(),
        "雪".as_bytes()
    );
    let array = single("$.a[*]");
    assert_eq!(array.root().unwrap().len().unwrap(), 4);
    assert_eq!(
        array.root().unwrap().value(3).unwrap().number().unwrap(),
        Number::I64(300)
    );

    let multiple = Query::from_text(text, ParseOptions::default()).unwrap();
    let paths = [
        Path::parse_mysql(b"$.a[1]").unwrap(),
        Path::parse_mysql(b"$.b.a").unwrap(),
    ];
    let bytes = multiple.extract(&paths).unwrap().unwrap();
    let array = BinaryDocument::new(bytes).unwrap();
    assert_eq!(array.root().unwrap().len().unwrap(), 2);
    assert_eq!(
        array.root().unwrap().value(1).unwrap().number().unwrap(),
        Number::I64(5)
    );

    let missing = Query::from_text(text, ParseOptions::default()).unwrap();
    assert!(missing
        .extract(&[Path::parse_mysql(b"$.missing").unwrap()])
        .unwrap()
        .is_none());

    let keys = multiple.object_keys(None).unwrap().unwrap();
    let keys = BinaryDocument::new(keys).unwrap();
    assert_eq!(keys.root().unwrap().len().unwrap(), 3);
    assert_eq!(
        keys.root()
            .unwrap()
            .value(2)
            .unwrap()
            .string_bytes()
            .unwrap(),
        b"s"
    );
    let nested_keys = multiple
        .object_keys(Some(&Path::parse_mysql(b"$.b").unwrap()))
        .unwrap()
        .unwrap();
    let nested_keys = BinaryDocument::new(nested_keys).unwrap();
    assert_eq!(nested_keys.root().unwrap().len().unwrap(), 1);
    assert!(multiple
        .object_keys(Some(&Path::parse_mysql(b"$.a").unwrap()))
        .unwrap()
        .is_none());
}

fn search_paths(document: &Query, paths: &[Path], pattern: &str, escape: char) -> Vec<String> {
    let binary = document
        .search(
            paths,
            SearchOptions {
                pattern,
                escape,
                first_only: false,
            },
        )
        .unwrap()
        .unwrap();
    let result = BinaryDocument::new(binary).unwrap();
    let root = result.root().unwrap();
    if root.json_type().unwrap() == JsonType::Array {
        (0..root.len().unwrap())
            .map(|index| {
                String::from_utf8(root.value(index).unwrap().string_bytes().unwrap().to_vec())
                    .unwrap()
            })
            .collect()
    } else {
        vec![String::from_utf8(root.string_bytes().unwrap().to_vec()).unwrap()]
    }
}

#[test]
fn search_builds_ordered_unique_paths_in_rust() {
    let document = Query::from_text(
        r#"{"a":"abc","b":["a%c","ab",{"x-y":"abc"}],"雪":"雪"}"#.as_bytes(),
        ParseOptions::default(),
    )
    .unwrap();
    assert_eq!(
        search_paths(&document, &[], "a%", '\\'),
        ["$.a", "$.b[0]", "$.b[1]", "$.b[2].\"x-y\""]
    );
    assert_eq!(search_paths(&document, &[], "_", '\\'), ["$.雪"]);
    assert_eq!(
        search_paths(
            &document,
            &[
                Path::parse_mysql(b"$").unwrap(),
                Path::parse_mysql(b"$.a").unwrap()
            ],
            "abc",
            '\\'
        ),
        ["$.a", "$.b[2].\"x-y\""]
    );
    assert_eq!(
        search_paths(
            &document,
            &[Path::parse_mysql(b"$.b[*]").unwrap()],
            "abc",
            '\\'
        ),
        ["$.b[2].\"x-y\""]
    );
    let first = document
        .search(
            &[],
            SearchOptions {
                pattern: "a%",
                escape: '\\',
                first_only: true,
            },
        )
        .unwrap()
        .unwrap();
    let first = BinaryDocument::new(first).unwrap();
    assert_eq!(first.root().unwrap().string_bytes().unwrap(), b"$.a");
    assert!(document
        .search(
            &[],
            SearchOptions {
                pattern: "missing",
                escape: '\\',
                first_only: false,
            },
        )
        .unwrap()
        .is_none());
}

#[test]
fn search_wildcards_preserve_utf8_and_escape_rules() {
    let document = Query::from_text(
        "[\"a%c\",\"abc\",\"a_c\",\"a雪c\",\"ac\"]".as_bytes(),
        ParseOptions::default(),
    )
    .unwrap();
    assert_eq!(search_paths(&document, &[], r"a\%c", '\\'), ["$[0]"]);
    assert_eq!(
        search_paths(&document, &[], "a_c", '\\'),
        ["$[0]", "$[1]", "$[2]", "$[3]"]
    );
    assert_eq!(search_paths(&document, &[], r"a\_c", '\\'), ["$[2]"]);
    assert_eq!(search_paths(&document, &[], "a_c", '_'), ["$[4]"]);
    assert_eq!(search_paths(&document, &[], "a!%c", '!'), ["$[0]"]);
    assert_eq!(search_paths(&document, &[], "a%%c", '%'), ["$[0]"]);
}

#[test]
fn containment_membership_and_overlap_preserve_sql_semantics() {
    let query = |text: &str| Query::from_text(text.as_bytes(), ParseOptions::default()).unwrap();
    let target = query(r#"{"a":[1,{"b":2},[3,4]],"c":{"d":5}}"#);
    assert!(target
        .contains(&query(r#"{"a":[1,[3]]}"#), None)
        .unwrap()
        .unwrap());
    assert!(target
        .contains(&query(r#"{"c":{"d":5}}"#), None)
        .unwrap()
        .unwrap());
    assert!(!target
        .contains(&query(r#"{"missing":1}"#), None)
        .unwrap()
        .unwrap());
    assert!(target
        .contains(&query("5"), Some(&Path::parse_mysql(b"$.c.d").unwrap()))
        .unwrap()
        .unwrap());
    assert!(target
        .contains(&query("5"), Some(&Path::parse_mysql(b"$.missing").unwrap()))
        .unwrap()
        .is_none());

    assert!(query("[1,2,3]").overlaps(&query("[4,2]")).unwrap());
    assert!(query("[1,2,3]").overlaps(&query("2")).unwrap());
    assert!(!query("[1,2,3]").overlaps(&query("[4,5]")).unwrap());
    assert!(query(r#"{"a":[1]}"#)
        .overlaps(&query(r#"{"a":[1]}"#))
        .unwrap());
    assert!(query("2").member_of(&query("[1,2,3]")).unwrap());
    assert!(!query("2").member_of(&query("[1,3]")).unwrap());
    assert!(query("2").member_of(&query("2.0")).unwrap());

    let ordered = query(r#"{"a":1,"b":2}"#);
    let reversed = query(r#"{"b":2,"a":1}"#);
    assert!(ordered.member_of(&reversed).unwrap());
    assert!(ordered.contains(&reversed, None).unwrap().unwrap());
    assert!(!ordered.contains(&query("{}"), None).unwrap().unwrap());
    assert!(query("{}").contains(&query("{}"), None).unwrap().unwrap());
    assert!(!query("[1]")
        .contains(&query("null"), None)
        .unwrap()
        .unwrap());
    assert!(query(r#"[1,{"a":3}]"#)
        .contains(&query("{}"), None)
        .unwrap()
        .unwrap());
    assert!(query(r#"[{}]"#)
        .contains(&query(r#"{"a":1}"#), None)
        .unwrap()
        .unwrap());
    assert!(query(r#"{"a":1,"b":{"x":2}}"#)
        .contains(&query(r#"{"a":1,"b":{}}"#), None)
        .unwrap()
        .unwrap());
    assert!(query(r#"{"a":1,"b":{}}"#)
        .contains(&query(r#"{"a":1,"b":{"x":2}}"#), None)
        .unwrap()
        .unwrap());
    assert!(query("{}").overlaps(&query("{}")).unwrap());
}

#[test]
fn numeric_equality_checks_exact_integer_boundaries() {
    let query = |text: &str| Query::from_text(text.as_bytes(), ParseOptions::default()).unwrap();
    assert!(query("1").member_of(&query("1.0")).unwrap());
    assert!(query("-0").member_of(&query("-0.0")).unwrap());
    assert!(!query("9007199254740993")
        .member_of(&query("9007199254740992.0"))
        .unwrap());
    assert!(query("9007199254740992")
        .member_of(&query("9007199254740992.0"))
        .unwrap());
    assert!(!query("18446744073709551615")
        .member_of(&query("18446744073709551616.0"))
        .unwrap());
    assert!(!query("9223372036854775807")
        .member_of(&query("9223372036854775808.0"))
        .unwrap());
}

fn decimal_document(negative: bool, exponent: i32, digits: &[u32]) -> BinaryDocument<Vec<u8>> {
    let sign_exponent = if negative {
        0x40 - exponent
    } else {
        0xc0 + exponent
    };
    let desc = digits.len() as u32 | ((sign_exponent as u32) << 24);
    let mut bytes = vec![JsonType::Decimal as u8, 0, 0, 0, 0];
    bytes.extend_from_slice(&desc.to_be_bytes());
    for digit in digits {
        bytes.extend_from_slice(&digit.to_le_bytes());
    }
    BinaryDocument::new(bytes).unwrap()
}

#[test]
fn decimal_equality_preserves_sql_number_semantics() {
    let query = |text: &str| {
        BinaryDocument::new(parse_binary(text.as_bytes(), ParseOptions::default()).unwrap())
            .unwrap()
    };
    let one = decimal_document(false, 0, &[1]);
    assert!(crate::compare::equal(one.root().unwrap(), query("1").root().unwrap()).unwrap());
    assert!(crate::compare::equal(one.root().unwrap(), query("1.0").root().unwrap()).unwrap());
    assert!(crate::compare::equal(
        one.root().unwrap(),
        decimal_document(false, 0, &[1, 0]).root().unwrap()
    )
    .unwrap());

    let tenth = decimal_document(false, -1, &[100_000_000]);
    assert!(crate::compare::equal(tenth.root().unwrap(), query("0.1").root().unwrap()).unwrap());
    assert!(!crate::compare::equal(
        tenth.root().unwrap(),
        decimal_document(false, -1, &[100_000_000, 5])
            .root()
            .unwrap()
    )
    .unwrap());

    let negative = decimal_document(true, 0, &[1]);
    assert!(crate::compare::equal(negative.root().unwrap(), query("-1").root().unwrap()).unwrap());
    assert!(!crate::compare::equal(negative.root().unwrap(), query("1").root().unwrap()).unwrap());

    let uint_max = decimal_document(false, 2, &[18, 446_744_073, 709_551_615]);
    assert!(crate::compare::equal(
        uint_max.root().unwrap(),
        query("18446744073709551615").root().unwrap()
    )
    .unwrap());
    assert!(!crate::compare::equal(
        uint_max.root().unwrap(),
        query("18446744073709551616.0").root().unwrap()
    )
    .unwrap());

    assert_eq!(
        crate::compare::equal(one.root().unwrap(), query("1e126").root().unwrap()),
        Err(crate::CompareError::NumericOverflow)
    );

    let int_min = decimal_document(true, 2, &[9, 223_372_036, 854_775_808]);
    assert!(crate::compare::equal(
        int_min.root().unwrap(),
        query("-9223372036854775808").root().unwrap()
    )
    .unwrap());

    let mut malformed = tenth.into_bytes();
    malformed[8] = 10;
    assert_eq!(
        BinaryDocument::new(malformed).unwrap_err().kind,
        BinaryErrorKind::InvalidNumber
    );
}

#[test]
fn binary_value_serialization_round_trips_document_shapes() {
    for text in [
        &br#"null"#[..],
        &br#"true"#[..],
        &br#"-9223372036854775808"#[..],
        &br#"18446744073709551615"#[..],
        &br#"-0.0"#[..],
        &"\"雪\"".as_bytes()[..],
        &br#"[0,127,128,-128,-129,32767,32768]"#[..],
        &br#"{"a":[0,1,{"b":false}],"c":"d"}"#[..],
    ] {
        let bytes = parse_binary(text, ParseOptions::default()).unwrap();
        let document = BinaryDocument::new(bytes.clone()).unwrap();
        let serialized = binary::serialize_value(document.root().unwrap()).unwrap();
        assert_eq!(serialized, bytes, "{:?}", String::from_utf8_lossy(text));
        BinaryDocument::new(serialized).unwrap();
    }

    let large = format!(
        "[{}]",
        (0..1000)
            .map(|value| value.to_string())
            .collect::<Vec<_>>()
            .join(",")
    );
    let bytes = parse_binary(large.as_bytes(), ParseOptions::default()).unwrap();
    let document = BinaryDocument::new(bytes.clone()).unwrap();
    assert_eq!(
        binary::serialize_value(document.root().unwrap()).unwrap(),
        bytes
    );
}
