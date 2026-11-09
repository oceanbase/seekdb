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

use crate::mutation::sql::{OwnedEditor, SqlOperation};
use crate::bridge::{ffi, parse_binary as parse_bridge_binary};
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
fn exponent_signs_are_not_rewritten_as_negative_zero() {
    for text in [
        &b"1e-0"[..],
        b"1E-0",
        b"[1e-0]",
        b"{\"a\":-0,\"b\":1e-0}",
        b"-0",
    ] {
        assert!(parse(text, ParseOptions::default()).is_ok(), "{}", String::from_utf8_lossy(text));
    }
    assert_eq!(
        parse(b"1e-0", ParseOptions::default()).unwrap(),
        Document::Number(Number::F64(1.0))
    );
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
    let serialized = binary::serialize_value(document.root().unwrap()).unwrap();
    let roundtrip = BinaryDocument::new(serialized).unwrap();
    let restored = roundtrip.root().unwrap().value(0).unwrap();
    assert_eq!(restored.json_type().unwrap(), JsonType::Int);
    assert_eq!(restored.number().unwrap(), child.number().unwrap());
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

fn assert_bridge_matches_native(input: &[u8], policy: DuplicatePolicy) {
    let result = parse_bridge_binary(input, bridge_options(policy), 100, false);
    assert_eq!(result.status, ffi::Status::Ok, "{}", result.message);
    assert_eq!(result.binary, parse_binary(input, options(policy)).unwrap());
}

#[test]
fn direct_bridge_binary_policies() {
    let complex = br#"{"a":[1,2],"b":0,"a":{"x":3},"c":4}"#;
    for policy in [
        DuplicatePolicy::Overwrite,
        DuplicatePolicy::Preserve,
        DuplicatePolicy::KeepFirst,
    ] {
        assert_bridge_matches_native(complex, policy);
    }
    let rejected = parse_bridge_binary(complex, ffi::Duplicate::Reject, 100, false);
    assert_eq!(rejected.status, ffi::Status::DuplicateKey);
    assert!(rejected.binary.is_empty());

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

    let schema = parse_bridge_binary(
        br#"{"$ref":"https://example.com/a"}"#,
        ffi::Duplicate::KeepFirst,
        100,
        true,
    );
    assert_eq!(schema.status, ffi::Status::UnsupportedSchemaRef);

    let input = "{\"雪\":1,}".as_bytes();
    let malformed = parse_bridge_binary(input, ffi::Duplicate::Overwrite, 100, false);
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
fn deeply_nested_filter_expressions_are_bounded() {
    let mut negated = String::from("$?(");
    negated.push_str(&"!".repeat(10_000));
    negated.push_str("@.a)");
    assert_eq!(
        crate::SqlPath::parse(negated.as_bytes()).unwrap_err().kind,
        PathErrorKind::TooManyNodes
    );
    let mut parenthesized = String::from("$?(");
    parenthesized.push_str(&"(".repeat(10_000));
    parenthesized.push_str("@.a");
    parenthesized.push_str(&")".repeat(10_001));
    assert_eq!(
        crate::SqlPath::parse(parenthesized.as_bytes()).unwrap_err().kind,
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
        values[0]
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
fn query_output_preserves_match_metadata_and_wraps_in_rust() {
    let document = Query::from_text(br#"{"a":[1,2],"b":3}"#, ParseOptions::default()).unwrap();
    let array_path = Path::parse_mysql(b"$.a[*]").unwrap();
    let unwrapped = document.query_output(&array_path, false).unwrap();
    assert_eq!(unwrapped.match_count, 2);
    assert_eq!(unwrapped.value_type, Some(JsonType::Int));
    assert!(unwrapped.binary.is_none());

    let wrapped = document.query_output(&array_path, true).unwrap();
    assert_eq!(wrapped.match_count, 2);
    let wrapped = Query::from_binary(wrapped.binary.unwrap()).unwrap();
    let values = wrapped
        .query(
            &Path::parse_mysql(b"$[*]").unwrap(),
            QueryOptions::default(),
        )
        .unwrap();
    assert_eq!(values.len(), 2);
    assert_eq!(values[0].number().unwrap(), crate::Number::I64(1));
    assert_eq!(values[1].number().unwrap(), crate::Number::I64(2));

    let single = document
        .query_output(&Path::parse_mysql(b"$.b").unwrap(), false)
        .unwrap();
    assert_eq!(single.match_count, 1);
    let single = Query::from_binary(single.binary.unwrap()).unwrap();
    assert_eq!(
        single.document.root().unwrap().number().unwrap(),
        crate::Number::I64(3)
    );

    let missing = document
        .query_output(&Path::parse_mysql(b"$.missing").unwrap(), true)
        .unwrap();
    assert_eq!(missing.match_count, 0);
    assert_eq!(missing.value_type, None);
    assert!(missing.binary.is_none());
}

#[test]
fn query_output_applies_wrapper_and_scalar_policies() {
    use crate::{QueryOutputOptions, WrapperPolicy};

    let document = Query::from_text(
        br#"{"scalar":1,"object":{"a":1},"array":[1,2]}"#,
        ParseOptions::default(),
    )
    .unwrap();
    let scalar_path = Path::parse_mysql(b"$.scalar").unwrap();
    let without = document
        .query_output_with_options(
            &scalar_path,
            QueryOutputOptions {
                allow_scalar: false,
                ..QueryOutputOptions::default()
            },
        )
        .unwrap();
    assert!(without.wrapper_required);
    assert!(!without.use_wrapper);
    assert!(without.binary.is_none());

    let conditional = document
        .query_output_with_options(
            &scalar_path,
            QueryOutputOptions {
                wrapper: WrapperPolicy::Conditional,
                allow_scalar: false,
                ..QueryOutputOptions::default()
            },
        )
        .unwrap();
    assert!(!conditional.wrapper_required);
    assert!(conditional.use_wrapper);
    let conditional = Query::from_binary(conditional.binary.unwrap()).unwrap();
    assert_eq!(conditional.document.root().unwrap().len().unwrap(), 1);

    let multiple = document
        .query_output_with_options(
            &Path::parse_mysql(b"$.array[*]").unwrap(),
            QueryOutputOptions::default(),
        )
        .unwrap();
    assert!(multiple.wrapper_required);
    assert!(multiple.binary.is_none());

    let conditional_multiple = document
        .query_output_with_options(
            &Path::parse_mysql(b"$.array[*]").unwrap(),
            QueryOutputOptions {
                wrapper: WrapperPolicy::Conditional,
                ..QueryOutputOptions::default()
            },
        )
        .unwrap();
    assert_eq!(conditional_multiple.match_count, 2);
    assert!(!conditional_multiple.wrapper_required);
    assert!(conditional_multiple.use_wrapper);
    let conditional_multiple = Query::from_binary(conditional_multiple.binary.unwrap()).unwrap();
    assert_eq!(conditional_multiple.document.root().unwrap().len().unwrap(), 2);

    let forced = document
        .query_output_with_options(
            &scalar_path,
            QueryOutputOptions {
                allow_scalar: false,
                wrap_single: true,
                ..QueryOutputOptions::default()
            },
        )
        .unwrap();
    assert!(!forced.wrapper_required);
    assert!(forced.use_wrapper);
}

#[test]
fn sql_json_path_parser_builds_complete_ast() {
    use crate::sql_path::{
        ArraySelector, Comparison, FilterExpr, FilterLiteral, ItemMethod, Operand, PathRoot,
        SqlPathStep,
    };

    let path = crate::SqlPath::parse(
        br#"strict $..book[0,2 to 4,last]?(!(@.price < 10) && @.title like_regex "^A")."a.b".ceiling()"#,
    )
    .unwrap();
    assert_eq!(path.root(), PathRoot::Root);
    assert!(!path.is_lax());
    assert!(path.can_match_many());
    assert_eq!(path.last_method(), Some(ItemMethod::Ceiling));
    assert!(matches!(path.steps()[0], SqlPathStep::Descendant));
    assert_eq!(path.steps()[1], SqlPathStep::Member("book".into()));
    let SqlPathStep::Array(selectors) = &path.steps()[2] else {
        panic!("expected array selectors")
    };
    assert_eq!(selectors.len(), 3);
    assert!(matches!(selectors[1], ArraySelector::Range(_, _)));
    let SqlPathStep::Filter(filter) = &path.steps()[3] else {
        panic!("expected filter")
    };
    let FilterExpr::And(negated, regex) = filter else {
        panic!("expected conjunction")
    };
    assert!(matches!(**negated, FilterExpr::Not(_)));
    let FilterExpr::Compare(
        Comparison::LikeRegex,
        Operand::Path(title),
        Operand::Literal(FilterLiteral::String(pattern)),
    ) = &**regex
    else {
        panic!("expected regular-expression predicate")
    };
    assert_eq!(pattern, "^A");
    assert_eq!(title.root(), PathRoot::Current);
    assert!(!title.is_lax());
    assert_eq!(path.steps()[4], SqlPathStep::Member("a.b".into()));

    let exists =
        crate::SqlPath::parse(br#"lax $?(! exists(@.missing) || exists(@.a[0]))"#).unwrap();
    let SqlPathStep::Filter(FilterExpr::Or(not_exists, exists)) = &exists.steps()[0] else {
        panic!("expected existence predicates")
    };
    assert!(matches!(&**not_exists, FilterExpr::Exists(_, true)));
    assert!(matches!(&**exists, FilterExpr::Exists(_, false)));
    let invalid_paths: &[&[u8]] = &[
        &b"$."[..],
        &b"$.."[..],
        &b"$.abs().a"[..],
        &b"$.unknown()"[..],
        &b"$[1,]"[..],
        &b"$?(@.a == )"[..],
        &b"$?(@.a == 1"[..],
    ];
    for invalid in invalid_paths {
        assert!(crate::SqlPath::parse(invalid).is_err(), "{:?}", invalid);
    }
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
    for (integer, double) in [
        ("9223372036854775808", "9223372036854775808.0"),
        ("-9223372036854775808", "-9223372036854775808.0"),
    ] {
        assert!(!query(integer).member_of(&query(double)).unwrap());
        assert!(!query(integer).contains(&query(double), None).unwrap().unwrap());
        assert!(!query(integer).overlaps(&query(double)).unwrap());
    }

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
    let texts: [&[u8]; 8] = [
        br#"null"#,
        br#"true"#,
        br#"-9223372036854775808"#,
        br#"18446744073709551615"#,
        br#"-0.0"#,
        "\"雪\"".as_bytes(),
        br#"[0,127,128,-128,-129,32767,32768]"#,
        br#"{"a":[0,1,{"b":false}],"c":"d"}"#,
    ];
    for text in texts {
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

#[test]
fn binary_serialization_deep_containers() {
    for depth in [60, 99, 101] {
        for object in [false, true] {
            let text = if object {
                format!("{}0{}", "{\"k\":".repeat(depth), "}".repeat(depth))
            } else {
                format!("{}0{}", "[".repeat(depth), "]".repeat(depth))
            };
            let options = ParseOptions {
                max_depth: 256,
                ..ParseOptions::default()
            };
            let bytes = parse_binary(text.as_bytes(), options).unwrap();
            let document = BinaryDocument::new(bytes.clone()).unwrap();
            let root = document.root().unwrap();
            assert_eq!(binary::serialize_value(root).unwrap(), bytes);
            let wrapped = binary::serialize_array(&[root]).unwrap();
            let expected = format!("[{text}]");
            assert_eq!(wrapped, parse_binary(expected.as_bytes(), options).unwrap());
        }
    }
}

#[test]
fn binary_serialization_container_width_boundaries() {
    for length in [230, 250, 255, 256, 65_000, 65_520, 65_535, 65_536] {
        let text = format!(
            "{{\"a\":[-32769,-32768,-129,-128,127,128,255,256,32767,32768,65535,65536],\"b\":\"{}\"}}",
            "x".repeat(length)
        );
        let bytes = parse_binary(text.as_bytes(), ParseOptions::default()).unwrap();
        let document = BinaryDocument::new(bytes.clone()).unwrap();
        assert_eq!(binary::serialize_value(document.root().unwrap()).unwrap(), bytes);
    }
}

#[test]
fn mutation_delta_matches_persisted_unis_records() {
    use crate::mutation::{Delta, Operation, PhysicalDiff};
    let golden = [
        0, 1, 1, 0x8d, 0x80, 0x80, 0x80, 0, 1, 0x82, 2, 4, b'$', b'[', b'0', b']', 0, 2, 1, 0xff, 0,
    ];
    let mut delta = Delta::default();
    let mut path = b"$[0]".to_vec();
    let mut value = vec![1, 0xff];
    delta
        .record(Operation::Replace, 0x82, 2, &path, &value)
        .unwrap();
    path.fill(0);
    value.fill(0);
    delta
        .record_physical(PhysicalDiff::new(42, 5).unwrap())
        .unwrap();
    assert_eq!(delta.encode_logical().unwrap(), golden);
    assert_eq!(delta.encoded_logical_size(), golden.len());
    assert!(!delta.is_unchanged());
    let mut restored = Delta::default();
    assert_eq!(
        restored.restore(&golden, delta.physical()).unwrap(),
        golden.len()
    );
    assert_eq!(restored.logical(), delta.logical());
    assert_eq!(restored.physical(), delta.physical());
    assert_eq!(restored.encode_logical().unwrap(), golden);
    assert!(restored.intersects(40, 4));
    assert!(restored.intersects(43, 1));
    assert!(!restored.intersects(47, 1));
    assert!(!restored.intersects(40, 2));
}

#[test]
fn mutation_delta_restore_is_atomic_and_retains_binary_scalar_payloads() {
    use crate::mutation::{Delta, Operation, PhysicalDiff};
    let mut delta = Delta::default();
    for value_type in 0..=33 {
        delta
            .record(
                Operation::Replace,
                value_type,
                3,
                b"$.a\0b",
                &[0, 0xff, value_type],
            )
            .unwrap();
    }
    delta.record(Operation::Remove, 0, 0, b"$[1]", b"").unwrap();
    let bytes = delta.encode_logical().unwrap();
    let physical = [
        PhysicalDiff::new(0, 10).unwrap(),
        PhysicalDiff::new(99, 5).unwrap(),
    ];
    for end in 0..bytes.len() {
        let mut restored = Delta::default();
        restored
            .record(Operation::Insert, 8, 0, b"$.b", &[1])
            .unwrap();
        assert!(
            restored.restore(&bytes[..end], &physical).is_err(),
            "truncated at {end}"
        );
        assert_eq!(restored.logical().len(), 1);
        assert!(restored.physical().is_empty());
    }
    let mut restored = Delta::default();
    restored.restore(&bytes, &physical).unwrap();
    assert_eq!(restored.logical(), delta.logical());
    assert_eq!(restored.physical(), physical);
    assert!(!restored.is_rebuilt());
    restored.mark_rebuilt();
    assert!(restored.is_rebuilt());
    assert!(PhysicalDiff::new(u64::MAX, 1).is_err());
    assert!(PhysicalDiff::new(i64::MAX as u64, 1).is_err());
}

#[test]
fn mutation_delta_preserves_unis_trailing_field_and_count_contracts() {
    use crate::mutation::{Delta, DeltaError, Operation};
    let mut delta = Delta::default();
    let bytes = [0, 1, 1, 0x80, 0x80, 0x80, 0x80, 0];
    assert_eq!(delta.restore(&bytes, &[]).unwrap(), bytes.len());
    assert_eq!(delta.logical()[0].operation, Operation::Invalid);
    let mut invalid_version = bytes;
    invalid_version[2] = 2;
    assert_eq!(
        delta.restore(&invalid_version, &[]),
        Err(DeltaError::UnsupportedVersion)
    );
    assert_eq!(delta.logical().len(), 1);
    let mut many = Delta::default();
    for _ in 0..256 {
        many.record(Operation::Remove, 0, 0, b"$.a", b"").unwrap();
    }
    let bytes = many.encode_logical().unwrap();
    assert_eq!(&bytes[..2], &[0, 0]);
    let mut restored = Delta::default();
    assert_eq!(restored.restore(&bytes, &[]).unwrap(), 2);
    assert!(restored.logical().is_empty());
}

fn publish_native_update(mut bytes: Vec<u8>, update: &crate::mutation::editor::Update) -> Vec<u8> {
    for write in &update.writes {
        if write.reset { bytes = write.bytes.clone(); continue; }
        let offset = write.offset as usize;
        bytes.resize(bytes.len().max(offset + write.bytes.len()), 0);
        bytes[offset..offset + write.bytes.len()].copy_from_slice(&write.bytes);
    }
    bytes
}

#[test]
fn sparse_editor_replaces_inline_inplace_and_forwarded_values() {
    use crate::mutation::editor::Editor;
    let binary = crate::parse_binary(
        br#"{"a":1,"b":"long initial string"}"#,
        ParseOptions::default(),
    )
    .unwrap();
    let value = BinaryDocument::new(crate::parse_binary(b"-128", ParseOptions::default()).unwrap())
        .unwrap();
    let mut editor = Editor::open(binary.as_slice()).unwrap();
    editor.replace(&[], 0, value.root().unwrap()).unwrap();
    let update = editor.finish();
    assert!(!update.delta.is_rebuilt());
    assert_eq!(update.delta.logical()[0].value_type, 0x82);
    assert_eq!(update.delta.physical().len(), 1);
    let bytes = publish_native_update(binary, &update);
    let document = BinaryDocument::new(bytes).unwrap();
    assert_eq!(
        document.root().unwrap().value(0).unwrap().number().unwrap(),
        Number::I64(-128)
    );

    let value =
        BinaryDocument::new(crate::parse_binary(b"\"x\"", ParseOptions::default()).unwrap())
            .unwrap();
    let mut editor = Editor::open(document.as_bytes()).unwrap();
    editor.replace(&[], 1, value.root().unwrap()).unwrap();
    let update = editor.finish();
    assert!(!update.delta.is_rebuilt());
    assert_eq!(update.delta.physical().len(), 1);
    let bytes = publish_native_update(document.into_bytes(), &update);
    let document = BinaryDocument::new(bytes).unwrap();
    assert_eq!(
        document
            .root()
            .unwrap()
            .value(1)
            .unwrap()
            .string_bytes()
            .unwrap(),
        b"x"
    );

    let value = BinaryDocument::new(
        crate::parse_binary(
            b"\"larger string after inplace shrink\"",
            ParseOptions::default(),
        )
        .unwrap(),
    )
    .unwrap();
    let mut editor = Editor::open(document.as_bytes()).unwrap();
    editor.replace(&[], 1, value.root().unwrap()).unwrap();
    let update = editor.finish();
    assert!(!update.delta.is_rebuilt());
    assert_eq!(update.delta.physical().len(), 2);
    let bytes = publish_native_update(document.into_bytes(), &update);
    let document = BinaryDocument::new(bytes).unwrap();
    assert_eq!(
        document
            .root()
            .unwrap()
            .value(1)
            .unwrap()
            .string_bytes()
            .unwrap(),
        b"larger string after inplace shrink"
    );

    let value =
        BinaryDocument::new(crate::parse_binary(b"\"small\"", ParseOptions::default()).unwrap())
            .unwrap();
    let mut editor = Editor::open(document.as_bytes()).unwrap();
    editor.replace(&[], 1, value.root().unwrap()).unwrap();
    let update = editor.finish();
    assert!(!update.delta.is_rebuilt());
    assert_eq!(update.delta.physical().len(), 1);
    let bytes = publish_native_update(document.into_bytes(), &update);
    let document = BinaryDocument::new(bytes).unwrap();
    assert_eq!(
        document
            .root()
            .unwrap()
            .value(1)
            .unwrap()
            .string_bytes()
            .unwrap(),
        b"small"
    );
}

#[test]
fn sparse_editor_rebuilds_ancestors_and_removes_objects_and_arrays() {
    use crate::mutation::editor::Editor;
    let binary = crate::parse_binary(
        br#"{"a":[1,2,3],"b":{"x":4,"y":5}}"#,
        ParseOptions::default(),
    )
    .unwrap();
    let value =
        BinaryDocument::new(crate::parse_binary(b"10", ParseOptions::default()).unwrap()).unwrap();
    let mut editor = Editor::open(binary.as_slice()).unwrap();
    editor.insert(&[0], 1, b"", value.root().unwrap()).unwrap();
    editor.remove(&[1], 0).unwrap();
    editor.remove(&[0], 2).unwrap();
    let update = editor.finish();
    let binary = publish_native_update(binary, &update);
    let document = BinaryDocument::new(binary).unwrap();
    let root = document.root().unwrap();
    let array = root.value(0).unwrap();
    assert_eq!(array.len().unwrap(), 3);
    assert_eq!(array.value(0).unwrap().number().unwrap(), Number::I64(1));
    assert_eq!(array.value(1).unwrap().number().unwrap(), Number::I64(10));
    assert_eq!(array.value(2).unwrap().number().unwrap(), Number::I64(3));
    let object = root.value(1).unwrap();
    assert_eq!(object.len().unwrap(), 1);
    assert_eq!(object.key(0).unwrap(), b"y");

    let binary = crate::parse_binary(b"[0,1]", ParseOptions::default()).unwrap();
    let mut editor = Editor::open(binary.as_slice()).unwrap();
    for _ in 0..24 {
        editor.insert(&[], 0, b"", value.root().unwrap()).unwrap();
    }
    editor.remove(&[], 0).unwrap();
    let update = editor.finish();
    assert!(update.delta.is_rebuilt());
    let binary = publish_native_update(binary, &update);
    assert_eq!(
        BinaryDocument::new(binary)
            .unwrap()
            .root()
            .unwrap()
            .len()
            .unwrap(),
        25
    );
}

#[test]
fn sparse_editor_preserves_successful_changes_after_a_failed_operation() {
    use crate::mutation::editor::{BinarySource, Editor};
    use std::cell::Cell;
    struct CountingSource {
        bytes: Vec<u8>,
        read: Cell<usize>,
    }
    impl BinarySource for CountingSource {
        fn len(&self) -> u64 {
            self.bytes.len() as u64
        }
        fn read_exact(&self, offset: u64, output: &mut [u8]) -> std::io::Result<()> {
            self.read.set(self.read.get() + output.len());
            self.bytes.as_slice().read_exact(offset, output)
        }
    }
    let text = format!("{{\"a\":1,\"b\":\"{}\"}}", "x".repeat(1024 * 1024));
    let source = CountingSource {
        bytes: crate::parse_binary(text.as_bytes(), ParseOptions::default()).unwrap(),
        read: Cell::new(0),
    };
    let value =
        BinaryDocument::new(crate::parse_binary(b"2", ParseOptions::default()).unwrap()).unwrap();
    let mut editor = Editor::open(&source).unwrap();
    editor.replace(&[], 0, value.root().unwrap()).unwrap();
    assert!(editor.replace(&[], 99, value.root().unwrap()).is_err());
    let update = editor.finish();
    assert!(source.read.get() < 128, "read {} bytes", source.read.get());
    assert_eq!(update.delta.logical().len(), 1);
    assert_eq!(update.delta.physical().len(), 1);
    assert!(!update.delta.is_rebuilt());
    let bytes = publish_native_update(source.bytes, &update);
    let document = BinaryDocument::new(bytes).unwrap();
    assert_eq!(
        document.root().unwrap().value(0).unwrap().number().unwrap(),
        Number::I64(2)
    );
    assert_eq!(
        document
            .root()
            .unwrap()
            .value(1)
            .unwrap()
            .string_bytes()
            .unwrap()
            .len(),
        1024 * 1024
    );
}

#[test]
fn sparse_editor_reports_source_failures_without_publishing_writes() {
    use crate::mutation::editor::{BinarySource, Editor};
    struct ShortSource(Vec<u8>);
    impl BinarySource for ShortSource {
        fn len(&self) -> u64 {
            self.0.len() as u64
        }
        fn read_exact(&self, offset: u64, output: &mut [u8]) -> std::io::Result<()> {
            if offset > 10 {
                return Err(std::io::ErrorKind::UnexpectedEof.into());
            }
            self.0.as_slice().read_exact(offset, output)
        }
    }
    let source = ShortSource(crate::parse_binary(b"[1,2]", ParseOptions::default()).unwrap());
    let value =
        BinaryDocument::new(crate::parse_binary(b"2", ParseOptions::default()).unwrap()).unwrap();
    let mut editor = Editor::open(&source).unwrap();
    assert!(editor.replace(&[], 0, value.root().unwrap()).is_err());
    let update = editor.finish();
    assert!(update.writes.is_empty());
    assert!(update.delta.is_unchanged());
    assert!(update.delta.logical().is_empty());
}

#[test]
fn sparse_editor_rejects_corrupt_offsets_and_lengths() {
    use crate::mutation::editor::Editor;
    let original =
        crate::parse_binary(br#"{"a":[1,2],"b":"value"}"#, ParseOptions::default()).unwrap();
    let replacement =
        BinaryDocument::new(crate::parse_binary(b"42", ParseOptions::default()).unwrap()).unwrap();
    for index in 0..original.len() {
        for byte in [0, 0x80, 0xff] {
            let mut bytes = original.clone();
            bytes[index] = byte;
            let outcome = std::panic::catch_unwind(|| {
                if let Ok(mut editor) = Editor::open(bytes.as_slice()) {
                    let _ = editor.replace(&[], 0, replacement.root().unwrap());
                    let _ = editor.insert(&[], 1, b"c", replacement.root().unwrap());
                    let _ = editor.remove(&[], 0);
                }
            });
            assert!(outcome.is_ok(), "panicked after byte {index} became {byte}");
        }
    }
}

#[test]
fn mutation_delta_owns_partial_lob_rebuild_policy() {
    use crate::mutation::{Delta, PhysicalDiff};
    let mut delta = Delta::default();
    assert!(delta.should_pack(130, 100, 8).unwrap());
    delta
        .record_physical(PhysicalDiff::new(20, 2).unwrap())
        .unwrap();
    assert!(delta.should_pack(129, 100, 8).unwrap());
    assert!(!delta.should_pack(130, 100, 8).unwrap());
    assert!(!delta.should_pack(131, 100, 8).unwrap());
    assert!(delta.should_pack(100, 100, 8).unwrap());
    assert!(!delta.should_pack(100, 100, 0).unwrap());
    assert!(delta.should_pack(100, 0, 8).is_err());
    assert!(delta.should_pack(0, 0, 8).is_err());
    delta.mark_rebuilt();
    assert!(!delta.should_pack(100, 100, 8).unwrap());
}


#[test]
fn sparse_editor_rebuilds_headerless_root_containers() {
    use crate::mutation::editor::Editor;
    let binary = crate::parse_binary(b"[1,2]", ParseOptions::default()).unwrap()[8..].to_vec();
    let value = BinaryDocument::new(crate::parse_binary(b"3", ParseOptions::default()).unwrap()).unwrap();
    let mut editor = Editor::open(binary.as_slice()).unwrap();
    editor.insert(&[], 1, b"", value.root().unwrap()).unwrap();
    let update = editor.finish();
    assert!(update.delta.is_rebuilt());
    let bytes = publish_native_update(binary, &update);
    let document = BinaryDocument::new(bytes).unwrap();
    let root = document.root().unwrap();
    assert_eq!(root.len().unwrap(), 3);
    for (index, expected) in [1, 3, 2].into_iter().enumerate() {
        assert_eq!(root.value(index as u64).unwrap().number().unwrap(), Number::I64(expected));
    }
}

#[test]
fn selected_values_outlive_query_and_parent_selection() {
    let root = Query::from_text(
        br#"{"a":[{"b":[-129,256]},{"b":[9]}]}"#,
        ParseOptions::default(),
    ).unwrap().into_value().unwrap();
    let rows = root.select(&Path::parse_mysql(b"$.a[*]").unwrap(), QueryOptions::default()).unwrap();
    let row = rows.get(0).unwrap().clone();
    let other_row = rows.get(1).unwrap().clone();
    drop(rows);
    drop(root);
    let children = row.select(&Path::parse_mysql(b"$.b[*]").unwrap(), QueryOptions::default()).unwrap();
    let child = children.get(0).unwrap().clone();
    drop(children);
    drop(row);
    assert_eq!(child.read().number().unwrap(), Number::I64(-129));
    let output = other_row.query_output(
        &Path::parse_mysql(b"$.b[*]").unwrap(), crate::QueryOutputOptions::default(),
    ).unwrap();
    let output = BinaryDocument::new(output.binary.unwrap()).unwrap();
    assert_eq!(output.root().unwrap().number().unwrap(), Number::I64(9));
}

#[test]
fn native_order_preserves_container_and_numeric_rules() {
    use std::cmp::Ordering::*;
    let cases = [
        ("null", "false", Less), ("{}", "[]", Less), ("[]", "false", Less),
        ("[1,2]", "[1,2,0]", Less), ("[2]", "[1,9]", Greater),
        (r#"{"z":0}"#, r#"{"a":0,"b":0}"#, Less),
        (r#"{"b":0}"#, r#"{"a":1}"#, Greater),
        ("9223372036854775808.0", "9223372036854775808", Greater),
        ("9007199254740992.0", "9007199254740993", Less),
        ("18446744073709551616.0", "18446744073709551615", Greater),
        ("-9223372036854775808.0", "-9223372036854775808", Less),
        ("1.0", "1", Equal), ("-0.0", "0", Equal),
    ];
    for (a,b,expected) in cases {
        let a = BinaryDocument::new(parse_binary(a.as_bytes(), ParseOptions::default()).unwrap()).unwrap();
        let b = BinaryDocument::new(parse_binary(b.as_bytes(), ParseOptions::default()).unwrap()).unwrap();
        let actual = crate::compare::order(a.root().unwrap(), b.root().unwrap(), &mut |_: crate::BinaryValue<'_>, _: crate::BinaryValue<'_>| -> Result<_, ()> { panic!("ordinary values need no database callback") }).unwrap();
        assert_eq!(actual, expected);
    }
}

#[test]
fn native_order_preserves_database_type_matrix_and_scalar_errors() {
    use std::cmp::Ordering::*;
    let timestamp = BinaryDocument::new([12,0,0,0,0,0,0,0,0]).unwrap();
    let date = BinaryDocument::new([32,0,0,0,0]).unwrap();
    let datetime = BinaryDocument::new([33,0,0,0,0,0,0,0,0]).unwrap();
    let mut no_callback = |_: crate::BinaryValue<'_>, _: crate::BinaryValue<'_>| -> Result<_, ()> { panic!("matrix decided the order") };
    assert_eq!(crate::compare::order(date.root().unwrap(), datetime.root().unwrap(), &mut no_callback).unwrap(), Greater);
    assert_eq!(crate::compare::order(datetime.root().unwrap(), date.root().unwrap(), &mut no_callback).unwrap(), Greater);
    let mut callback = |_: crate::BinaryValue<'_>, _: crate::BinaryValue<'_>| Err(37);
    assert!(matches!(crate::compare::order(timestamp.root().unwrap(), datetime.root().unwrap(), &mut callback), Err(crate::compare::OrderError::Scalar(37))));
    let oracle_number = BinaryDocument::new([18,1]).unwrap();
    assert!(matches!(crate::compare::order(oracle_number.root().unwrap(), date.root().unwrap(), &mut no_callback), Err(crate::compare::OrderError::UnsupportedTypes)));
}

#[test]
fn native_format_preserves_pretty_escaping_and_number_bytes() {
    let cases = [
        ("-0.0", "-0.0"), ("1.0", "1.0"), ("1e15", "1e15"),
        ("1e-15", "0.000000000000001"), ("1e-16", "1e-16"),
        ("1e23", "1e23"), ("5e-324", "5e-324"),
        ("1.7976931348623157e308", "1.7976931348623157e308"),
        (r#"{"z":[],"a":[null,true,"雪😀\u0000\b\t\n\f\r\"\\/"]}"#,
         r#"{"a": [null, true, "雪😀\u0000\b\t\n\f\r\"\\/"], "z": []}"#),
    ];
    let mut no_callback = |_: crate::BinaryValue<'_>, _: bool| -> Result<Vec<u8>, ()> { panic!("ordinary values need no database callback") };
    for (input, expected) in cases {
        let value = BinaryDocument::new(parse_binary(input.as_bytes(), ParseOptions::default()).unwrap()).unwrap();
        let output = crate::format::format(value.root().unwrap(), crate::format::FormatOptions::default(), &mut no_callback).unwrap();
        assert_eq!(output, expected.as_bytes(), "{input}");
    }
    let value = BinaryDocument::new(parse_binary(br#"{"a":[1,{}],"b":[]}"#, ParseOptions::default()).unwrap()).unwrap();
    let output = crate::format::format(value.root().unwrap(), crate::format::FormatOptions { pretty:true, ..Default::default() }, &mut no_callback).unwrap();
    assert_eq!(output, b"{\n  \"a\": [\n    1,\n    {}\n  ],\n  \"b\": []\n}");
}

#[test]
fn native_format_retains_byte_strings_and_special_scalar_capability() {
    let mut bytes = vec![20,4];
    bytes.extend_from_slice(&[0, 255, b'"', b'\\']);
    let value = BinaryDocument::new(bytes.clone()).unwrap();
    let mut no_callback = |_: crate::BinaryValue<'_>, _: bool| -> Result<Vec<u8>, ()> { panic!("extended strings are native") };
    let output = crate::format::format(value.root().unwrap(), crate::format::FormatOptions { quoted:false, ..Default::default() }, &mut no_callback).unwrap();
    assert_eq!(output, [0, 255, b'"', b'\\']);
    assert_eq!(binary::serialize_value(value.root().unwrap()).unwrap(), bytes);
    let scalar = decimal_document(false, 0, &[1]);
    let array = binary::serialize_array(&[scalar.root().unwrap(), value.root().unwrap()]).unwrap();
    let array = BinaryDocument::new(array).unwrap();
    let mut calls = 0;
    let output = crate::format::format(array.root().unwrap(), crate::format::FormatOptions::default(), &mut |value, quoted| -> Result<_, ()> {
        calls += 1;
        assert_eq!(value.json_type().unwrap(), JsonType::Decimal);
        assert!(quoted);
        Ok(b"1.00".to_vec())
    }).unwrap();
    assert_eq!(calls, 1);
    assert_eq!(output, b"[1.00, \"\\u0000\xff\\\"\\\\\"]");
    assert!(matches!(crate::format::format(scalar.root().unwrap(), Default::default(), &mut |_,_| Err::<Vec<u8>,_>(41)), Err(crate::format::FormatError::Scalar(41))));
}

#[test]
fn native_predicates_share_temporal_capability_and_type_compatibility() {
    struct IgnoreFractions;
    impl crate::compare::ScalarComparator for IgnoreFractions {
        type Error = u32;
        fn compare_temporal(&mut self, left: crate::BinaryValue<'_>, right: crate::BinaryValue<'_>) -> Result<std::cmp::Ordering, u32> {
            assert_eq!(left.json_type().unwrap(), JsonType::Time);
            assert_eq!(right.json_type().unwrap(), JsonType::Time);
            Ok(std::cmp::Ordering::Equal)
        }
    }
    let a = BinaryDocument::new([10, 1,0,0,0,0,0,0,0]).unwrap();
    let b = BinaryDocument::new([10, 2,0,0,0,0,0,0,0]).unwrap();
    let a = Query::from_binary(binary::serialize_array(&[a.root().unwrap()]).unwrap()).unwrap();
    let b = Query::from_binary(binary::serialize_array(&[b.root().unwrap()]).unwrap()).unwrap();
    assert!(a.contains_with(&b, None, &mut IgnoreFractions).unwrap().unwrap());
    assert!(a.overlaps_with(&b, &mut IgnoreFractions).unwrap());
    let path = Path::parse_mysql(b"$[0]").unwrap();
    assert!(a.contains_with(&b, Some(&path), &mut IgnoreFractions).unwrap().is_some());
    let mut error = |_: crate::BinaryValue<'_>, _: crate::BinaryValue<'_>| Err(99u32);
    assert!(matches!(a.overlaps_with(&b, &mut error), Err(crate::compare::OrderError::Scalar(99))));
    let standard = BinaryDocument::new([5,1,b'x']).unwrap();
    let extended = BinaryDocument::new([20,1,b'x']).unwrap();
    assert!(crate::compare::equal(standard.root().unwrap(), extended.root().unwrap()).unwrap());
    let scalar = Query::from_binary(standard.into_bytes().to_vec()).unwrap();
    let array = Query::from_binary(binary::serialize_array(&[extended.root().unwrap()]).unwrap()).unwrap();
    assert!(scalar.member_of(&array).unwrap());
}

#[test]
fn read_operations_reject_binary_cycles_without_recursive_descent() {
    let document = BinaryDocument::new([7, 0, 1, 6, 0, 7]).unwrap();
    let mut no_scalar = |_: crate::BinaryValue<'_>, _: bool| -> Result<Vec<u8>, ()> { unreachable!() };
    assert!(matches!(crate::format::format(document.root().unwrap(), Default::default(), &mut no_scalar), Err(crate::format::FormatError::Binary(error)) if error.kind == BinaryErrorKind::InvalidDepth));
    let mut no_temporal = |_: crate::BinaryValue<'_>, _: crate::BinaryValue<'_>| -> Result<std::cmp::Ordering, ()> { unreachable!() };
    assert!(matches!(crate::compare::order(document.root().unwrap(), document.root().unwrap(), &mut no_temporal), Err(crate::compare::OrderError::Value(crate::CompareError::Binary(error))) if error.kind == BinaryErrorKind::InvalidDepth));
}


#[test]
fn sql_mutation_prepares_paths_and_preserves_lazy_value_evaluation() {
    use crate::mutation::editor::{Editor};
    let original = crate::parse_binary(br#"{"a":[1,2],"b":3}"#, ParseOptions::default()).unwrap();
    let mut editor = Editor::open(original.as_slice()).unwrap();
    let value =
        BinaryDocument::new(crate::parse_binary(b"42", ParseOptions::default()).unwrap()).unwrap();
    assert!(editor
        .prepare(&Path::parse_mysql(b"$.b.member").unwrap(), SqlOperation::Insert)
        .unwrap()
        .needs_value());
    assert!(!editor
        .prepare(&Path::parse_mysql(b"$.a[0]").unwrap(), SqlOperation::Insert)
        .unwrap()
        .needs_value());
    assert!(editor
        .prepare(&Path::parse_mysql(b"$.missing").unwrap(), SqlOperation::Set)
        .unwrap()
        .needs_value());
    assert!(editor
        .prepare(
            &Path::parse_mysql(b"$.missing").unwrap(),
            SqlOperation::Replace
        )
        .unwrap()
        .needs_value());
    assert!(!editor
        .prepare(
            &Path::parse_mysql(b"$.missing.x").unwrap(),
            SqlOperation::Insert
        )
        .unwrap()
        .needs_value());
    assert!(!editor
        .prepare(
            &Path::parse_mysql(b"$.missing").unwrap(),
            SqlOperation::ArrayAppend
        )
        .unwrap()
        .needs_value());
    let stale = editor
        .prepare(
            &Path::parse_mysql(b"$.a[1]").unwrap(),
            SqlOperation::Replace,
        )
        .unwrap();
    let edit = editor
        .prepare(&Path::parse_mysql(b"$.a[9]").unwrap(), SqlOperation::Set)
        .unwrap();
    editor.apply(edit, Some(value.root().unwrap())).unwrap();
    assert!(editor.apply(stale, Some(value.root().unwrap())).is_err());
    let edit = editor
        .prepare(&Path::parse_mysql(b"$.b[9]").unwrap(), SqlOperation::Set)
        .unwrap();
    editor.apply(edit, Some(value.root().unwrap())).unwrap();
    let edit = editor
        .prepare(&Path::parse_mysql(b"$.new").unwrap(), SqlOperation::Insert)
        .unwrap();
    editor.apply(edit, Some(value.root().unwrap())).unwrap();
    let edit = editor
        .prepare(&Path::parse_mysql(b"$.a[0]").unwrap(), SqlOperation::Remove)
        .unwrap();
    editor.apply(edit, None).unwrap();
    let update = editor.finish();
    let document = BinaryDocument::new(publish_native_update(original, &update)).unwrap();
    let expected = crate::parse_binary(
        br#"{"a":[2,42],"b":[3,42],"new":42}"#,
        ParseOptions::default(),
    )
    .unwrap();
    assert_eq!(
        binary::serialize_value(document.root().unwrap()).unwrap(),
        expected
    );
}

#[test]
fn sql_mutation_handles_root_replacement_wrapping_and_array_insert() {
    use crate::mutation::editor::{Editor};
    let original = crate::parse_binary(b"1", ParseOptions::default()).unwrap();
    let value =
        BinaryDocument::new(crate::parse_binary(b"42", ParseOptions::default()).unwrap()).unwrap();
    let mut editor = Editor::open(original.as_slice()).unwrap();
    let edit = editor
        .prepare(&Path::parse_mysql(b"$").unwrap(), SqlOperation::ArrayAppend)
        .unwrap();
    editor.apply(edit, Some(value.root().unwrap())).unwrap();
    let edit = editor
        .prepare(
            &Path::parse_mysql(b"$[last-99]").unwrap(),
            SqlOperation::ArrayInsert,
        )
        .unwrap();
    editor.apply(edit, Some(value.root().unwrap())).unwrap();
    let update = editor.finish();
    let document = BinaryDocument::new(publish_native_update(original, &update)).unwrap();
    assert_eq!(
        binary::serialize_value(document.root().unwrap()).unwrap(),
        crate::parse_binary(b"[42,1,42]", ParseOptions::default()).unwrap()
    );
    let original = document.into_bytes();
    let mut editor = Editor::open(original.as_slice()).unwrap();
    let edit = editor
        .prepare(&Path::parse_mysql(b"$").unwrap(), SqlOperation::Replace)
        .unwrap();
    editor.apply(edit, Some(value.root().unwrap())).unwrap();
    assert!(editor
        .prepare(&Path::parse_mysql(b"$").unwrap(), SqlOperation::Remove)
        .is_err());
    let edit = editor
        .prepare(
            &Path::parse_mysql(b"$[last-99]").unwrap(),
            SqlOperation::Set,
        )
        .unwrap();
    editor.apply(edit, Some(value.root().unwrap())).unwrap();
    let update = editor.finish();
    let document = BinaryDocument::new(publish_native_update(original, &update)).unwrap();
    assert_eq!(
        binary::serialize_value(document.root().unwrap()).unwrap(),
        crate::parse_binary(b"[42,42]", ParseOptions::default()).unwrap()
    );
}

#[test]
fn owned_mutations_accept_empty_object_keys() {
    let mut editor = OwnedEditor::from_document(crate::parse(b"{}", Default::default()).unwrap()).unwrap();
    let value = BinaryDocument::new(crate::parse_binary(b"42", Default::default()).unwrap()).unwrap();
    let path = Path::parse_mysql(br#"$."""#).unwrap();
    let edit = editor.prepare(&path, SqlOperation::Set).unwrap();
    editor = editor.apply(edit, Some(value.root().unwrap())).unwrap();
    let inserted = BinaryDocument::new(editor.finish().unwrap()).unwrap();
    assert_eq!(inserted.root().unwrap().key(0).unwrap(), b"");
    assert_eq!(inserted.root().unwrap().value(0).unwrap().number().unwrap(), Number::I64(42));
    let editor = OwnedEditor::from_binary(inserted.root().unwrap()).unwrap();
    let edit = editor.prepare(&path, SqlOperation::Remove).unwrap();
    let editor = editor.apply(edit, None).unwrap();
    let document = BinaryDocument::new(editor.finish().unwrap()).unwrap();
    assert_eq!(document.root().unwrap().len().unwrap(), 0);
}

#[test]
fn first_value_owns_output_and_preserves_array_wrap_policy() {
    let query = Query::from_text(br#"{"choices":[{"message":{"content":"answer"}}]}"#, Default::default()).unwrap();
    let path = Path::parse_mysql(b"$.choices[0].message.content").unwrap();
    let selected = query.first_value(&path, false).unwrap().unwrap();
    drop(query);
    assert_eq!(BinaryDocument::new(selected).unwrap().root().unwrap().string_bytes().unwrap(), b"answer");
    let query = Query::from_text(br#"{"choices":{"message":{"content":"answer"}}}"#, Default::default()).unwrap();
    assert!(query.first_value(&path, false).unwrap().is_none());
    assert!(query.first_value(&path, true).unwrap().is_some());
}

#[test]
fn schema_references_reject_remote_strings_in_every_subtree() {
    for (text, expected) in [
        (r##"{"$ref":"#"}"##, true),
        (r##"{"$ref":""}"##, true),
        (r##"{"$ref":12}"##, true),
        (r##"{"ignored":[{"$ref":"#/local"}]}"##, true),
        (r##"{"ignored":[{"$ref":"https://remote"}]}"##, false),
    ] {
        let query = Query::from_text(text.as_bytes(), Default::default()).unwrap();
        assert_eq!(crate::schema::references_are_local(query.document.root().unwrap()).unwrap(), expected);
    }
}

#[test]
fn stored_value_packing_preserves_existing_widths_and_inline_contracts() {
    use crate::mutation::editor::StoredValue;
    let wide_array = [7, 0x51, 1, 8, 0, 1, 0, 0x82];
    let value = StoredValue::open(wide_array.as_slice(), 0, 7, 0).unwrap();
    assert_eq!(value.area_size().unwrap(), 8);
    assert_eq!(value.serialized_size().unwrap(), 8);
    assert_eq!(value.pack_payload().unwrap(), wide_array);
    let inline = StoredValue::open(wide_array.as_slice(), 5, 0x82, 1).unwrap();
    assert!(inline.area_size().is_err());
    assert!(inline.serialized_size().is_err());
    assert_eq!(inline.pack_payload().unwrap(), [1]);
    assert!(StoredValue::open(wide_array.as_slice(), 5, 0x82, 4).is_err());
    for text in [br#"{"a":[1,2],"b":"long"}"#.as_slice(), br#""long""#, b"-1024"] {
        let binary = crate::parse_binary(text, Default::default()).unwrap();
        let offset = if binary[0] == 30 { 8 } else { 0 };
        let kind = binary[offset];
        let payload = offset + usize::from(!crate::binary::needs_type_prefix(kind));
        let value = StoredValue::open(binary.as_slice(), payload as u64, kind, 0).unwrap();
        assert_eq!(value.pack_payload().unwrap(), binary[payload..]);
    }
}


#[test]
fn native_construction_owns_binary_children_and_applies_duplicate_policy() {
    use crate::construction::{BuildError, ContainerBuilder, Scalar};
    let mut object = ContainerBuilder::object();
    object.insert(b"bb", &Scalar::Signed(1).encode().unwrap()).unwrap();
    object.insert(b"a", &Scalar::String(b"old").encode().unwrap()).unwrap();
    object.insert(b"a", &Scalar::String(b"new").encode().unwrap()).unwrap();
    let output = object.finish(DuplicatePolicy::Overwrite, true, false, Some(2)).unwrap();
    assert_eq!((output.input_count, output.output_count), (3, 2));
    let document = BinaryDocument::new(output.binary).unwrap();
    let root = document.root().unwrap();
    assert_eq!(root.key(0).unwrap(), b"a");
    assert_eq!(root.value(0).unwrap().string_bytes().unwrap(), b"new");
    assert!(matches!(object.finish(DuplicatePolicy::Reject, true, false, None), Err(BuildError::DuplicateKey)));
    let first = object.finish(DuplicatePolicy::KeepFirst, true, false, None).unwrap();
    assert_eq!(BinaryDocument::new(first.binary).unwrap().root().unwrap().value(0).unwrap().string_bytes().unwrap(), b"old");
    let preserved = object.finish(DuplicatePolicy::Preserve, false, true, None).unwrap();
    let preserved = BinaryDocument::new(preserved.binary).unwrap();
    assert_eq!(preserved.root().unwrap().key(0).unwrap(), b"bb");
    assert_eq!(preserved.root().unwrap().len().unwrap(), 3);
    assert!(matches!(object.finish(DuplicatePolicy::Overwrite, true, false, Some(1)), Err(BuildError::Depth)));
}

#[test]
fn native_construction_preserves_scalar_tags_and_aggregate_layout() {
    use crate::construction::{ContainerBuilder, Scalar};
    let scalars = [Scalar::Null, Scalar::Boolean(true), Scalar::Signed(-1), Scalar::Unsigned(u64::MAX),
        Scalar::Float(1.5), Scalar::String(b"x"), Scalar::Opaque { field_type: 27, bytes: b"abc" },
        Scalar::Database { kind: 10, payload: &123456_i64.to_le_bytes() }];
    let mut builder = ContainerBuilder::array();
    for scalar in scalars { builder.append(&scalar.encode().unwrap()).unwrap(); }
    for aggregate in [false, true] {
        let output = builder.finish(DuplicatePolicy::Preserve, false, aggregate, Some(2)).unwrap();
        let output = BinaryDocument::new(output.binary).unwrap();
        let root = output.root().unwrap();
        assert_eq!(root.len().unwrap(), 8);
        assert_eq!(root.value(3).unwrap().number().unwrap(), Number::U64(u64::MAX));
        assert_eq!(root.value(6).unwrap().json_type().unwrap(), JsonType::Opaque);
        assert_eq!(root.value(7).unwrap().json_type().unwrap(), JsonType::Time);
    }
    let mut builder = ContainerBuilder::array();
    builder.append(&Scalar::Signed(1).encode().unwrap()).unwrap();
    builder.append(&Scalar::Signed(2).encode().unwrap()).unwrap();
    assert_eq!(builder.approximate_length(), 2);
    let output = builder.finish(DuplicatePolicy::Preserve, false, true, None).unwrap();
    assert_eq!(output.binary, [7, 64, 2, 10, 8, 2, 9, 2, 1, 2]);
}

#[test]
fn native_construction_keeps_oracle_string_tags() {
    use crate::construction::{ContainerBuilder, Scalar};
    let mut builder = ContainerBuilder::array();
    for kind in [20, 21, 22, 23, 28, 29] {
        let binary = Scalar::Database { kind, payload: &[1, b'x'] }.encode().unwrap();
        builder.append(&binary).unwrap();
    }
    for aggregate in [false, true] {
        let output = builder.finish(DuplicatePolicy::Preserve, false, aggregate, None).unwrap();
        let output = BinaryDocument::new(output.binary).unwrap();
        for (index, kind) in [JsonType::OBinary, JsonType::OOid, JsonType::ORawHex, JsonType::ORawId,
                              JsonType::ODaySecond, JsonType::OYearMonth].into_iter().enumerate() {
            let value = output.root().unwrap().value(index as u64).unwrap();
            assert_eq!(value.json_type().unwrap(), kind);
            assert_eq!(value.string_bytes().unwrap(), b"x");
        }
    }
}

#[test]
fn native_construction_matches_text_constructor_size_estimates() {
    use crate::construction::{ContainerBuilder, Scalar};
    for length in [0, 1, 220, 230, 240, 250, 255, 256, 257, 65520, 65536] {
        let text = "x".repeat(length);
        let mut array = ContainerBuilder::array();
        array.append(&Scalar::String(text.as_bytes()).encode().unwrap()).unwrap();
        let nested = crate::parse_binary(br#"[1,2,{"a":null}]"#, ParseOptions::default()).unwrap();
        array.append(&nested).unwrap();
        let output = array.finish(DuplicatePolicy::Overwrite, true, false, None).unwrap();
        let expected = crate::parse_binary(format!(r#"["{}",[1,2,{{"a":null}}]]"#, text).as_bytes(), ParseOptions::default()).unwrap();
        assert_eq!(output.binary, expected, "array length {}", length);
        let mut object = ContainerBuilder::object();
        object.insert(b"bb", &nested).unwrap();
        object.insert(b"a", &Scalar::String(text.as_bytes()).encode().unwrap()).unwrap();
        let output = object.finish(DuplicatePolicy::Overwrite, true, false, None).unwrap();
        let expected = crate::parse_binary(format!(r#"{{"a":"{}","bb":[1,2,{{"a":null}}]}}"#, text).as_bytes(), ParseOptions::default()).unwrap();
        assert_eq!(output.binary, expected, "object length {}", length);
    }
}

#[test]
fn native_merge_preserves_sql_null_and_binary_domain_semantics() {
    use crate::mutation::merge::{MergeAccumulator, MergeInput, MergeMode};
    let left = BinaryDocument::new(
        crate::parse_binary(
            br#"{"a":[1],"b":{"x":1},"n":null}"#,
            ParseOptions::default(),
        )
        .unwrap(),
    )
    .unwrap();
    let right = BinaryDocument::new(
        crate::parse_binary(br#"{"a":[2],"b":{"y":2},"n":3}"#, ParseOptions::default()).unwrap(),
    )
    .unwrap();
    let merge = MergeAccumulator::new(MergeMode::Preserve)
        .push(MergeInput::Value(left.root().unwrap()))
        .unwrap()
        .push(MergeInput::Value(right.root().unwrap()))
        .unwrap();
    let expected = crate::parse_binary(
        br#"{"a":[1,2],"b":{"x":1,"y":2},"n":[null,3]}"#,
        ParseOptions::default(),
    )
    .unwrap();
    assert_eq!(merge.finish().unwrap().unwrap(), expected);
    let merge = MergeAccumulator::new(MergeMode::Preserve)
        .push(MergeInput::SqlNull)
        .unwrap();
    assert!(!merge.needs_input());
    assert!(merge.finish().unwrap().is_none());

    let patch = BinaryDocument::new(
        crate::parse_binary(
            br#"{"a":null,"b":{"x":null,"z":4},"n":null}"#,
            ParseOptions::default(),
        )
        .unwrap(),
    )
    .unwrap();
    let merge = MergeAccumulator::new(MergeMode::Patch)
        .push(MergeInput::Value(left.root().unwrap()))
        .unwrap()
        .push(MergeInput::Value(patch.root().unwrap()))
        .unwrap();
    assert_eq!(
        merge.finish().unwrap().unwrap(),
        crate::parse_binary(br#"{"b":{"z":4}}"#, ParseOptions::default()).unwrap()
    );
    let scalar =
        BinaryDocument::new(crate::parse_binary(b"42", ParseOptions::default()).unwrap()).unwrap();
    let merge = MergeAccumulator::new(MergeMode::Patch)
        .push(MergeInput::SqlNull)
        .unwrap()
        .push(MergeInput::Value(patch.root().unwrap()))
        .unwrap()
        .push(MergeInput::Value(scalar.root().unwrap()))
        .unwrap();
    assert_eq!(merge.finish().unwrap().unwrap(), scalar.as_bytes());
}

#[test]
fn native_merge_oracle_null_patch_and_extended_scalar_tags() {
    use crate::mutation::merge::{MergeAccumulator, MergeError, MergeInput, MergeMode};
    let target =
        BinaryDocument::new(crate::parse_binary(b"{}", ParseOptions::default()).unwrap()).unwrap();
    let merger = MergeAccumulator::new(MergeMode::OraclePatch)
        .push(MergeInput::SqlNull)
        .unwrap();
    assert!(!merger.needs_input());
    assert!(merger.finish().unwrap().is_none());
    let merger = MergeAccumulator::new(MergeMode::OraclePatch)
        .push(MergeInput::Value(target.root().unwrap()))
        .unwrap();
    assert!(matches!(
        merger.push(MergeInput::SqlNull),
        Err(MergeError::InvalidPatch)
    ));
    for tag in [20, 21, 22, 23, 28, 29] {
        let scalar = BinaryDocument::new(vec![tag, 3, b'a', 0, b'b']).unwrap();
        let expected = scalar.root().unwrap().json_type().unwrap();
        let merger = MergeAccumulator::new(MergeMode::Preserve)
            .push(MergeInput::Value(scalar.root().unwrap()))
            .unwrap()
            .push(MergeInput::Value(scalar.root().unwrap()))
            .unwrap();
        let binary = merger.finish().unwrap().unwrap();
        let result = BinaryDocument::new(binary).unwrap();
        for index in 0..2 {
            let child = result.root().unwrap().value(index).unwrap();
            assert_eq!(child.json_type().unwrap(), expected);
            assert_eq!(child.string_bytes().unwrap(), b"a\0b");
        }
    }
}

#[test]
fn native_merge_rejects_cyclic_binary_input() {
    use crate::mutation::merge::{MergeAccumulator, MergeInput, MergeMode};
    let mut bytes = (30_u64 | (14_u64 << 22)).to_le_bytes().to_vec();
    bytes.extend_from_slice(&[7, 0x40, 1, 6, 0, 7]);
    let document = BinaryDocument::new(bytes).unwrap();
    assert!(MergeAccumulator::new(MergeMode::Preserve)
        .push(MergeInput::Value(document.root().unwrap()))
        .is_err());
}


#[test]
fn stored_value_reads_metadata_keys_and_forwarded_values() {
    use crate::mutation::editor::{Editor, StoredKind, StoredValue};
    let original = crate::parse_binary(br#"{"a":[1,true],"bb":"short"}"#, Default::default()).unwrap();
    let replacement = BinaryDocument::new(crate::parse_binary(b"\"a larger forwarded string\"", Default::default()).unwrap()).unwrap();
    let mut editor = Editor::open(original.as_slice()).unwrap();
    editor.replace(&[], 1, replacement.root().unwrap()).unwrap();
    let update = editor.finish();
    let binary = publish_native_update(original, &update);
    let root = StoredValue::open(binary.as_slice(), 8, 6, 0).unwrap();
    assert_eq!(root.depth().unwrap(), 3);
    assert_eq!(root.find_key(b"bb").unwrap(), Some(1));
    assert_eq!(root.find_key(b"missing").unwrap(), None);
    let key = root.key_range(1).unwrap();
    assert_eq!(&binary[key.start as usize..key.end as usize], b"bb");
    let StoredKind::Container(container) = root.metadata().unwrap().kind else { panic!("container"); };
    assert_eq!(container.count, 2);
    let child = root.child_position(1).unwrap();
    let value = StoredValue::open(binary.as_slice(), child.offset, child.raw_type, child.entry_type).unwrap();
    let StoredKind::Text(bytes) = value.metadata().unwrap().kind else { panic!("text"); };
    assert_eq!(&binary[bytes.start as usize..bytes.end as usize], b"a larger forwarded string");
    for tag in [20,21,22,23,28,29] {
        let bytes = [tag, 3, b'a', 0, b'z'];
        let value = StoredValue::open(bytes.as_slice(), 0, tag, 0).unwrap();
        let StoredKind::Text(range) = value.metadata().unwrap().kind else { panic!("extended string"); };
        assert_eq!(&bytes[range.start as usize..range.end as usize], b"a\0z");
        assert_eq!(value.pack_payload().unwrap(), bytes);
        let document = BinaryDocument::new(bytes.as_slice()).unwrap();
        let mut no_scalar = |_: crate::BinaryValue<'_>, _: bool| -> Result<Vec<u8>, ()> { panic!("string formatting is native"); };
        let printed = crate::format::format(document.root().unwrap(), Default::default(), &mut no_scalar).unwrap();
        assert_eq!(printed, br#""a\u0000z""#);

    }
}

#[test]
fn streaming_writer_preserves_geometry_layout_and_rejects_incomplete_output() {
    use crate::writer::{BinaryWriter, WriteError};
    let mut writer = BinaryWriter::default();
    let object = writer.begin(true, 2, 3).unwrap();
    writer.key(b"type").unwrap();
    writer.key(b"coordinates").unwrap();
    writer.string(b"Point").unwrap();
    let coordinates = writer.begin(false, 2, 0).unwrap();
    writer.double(1.0).unwrap(); writer.double(2.0).unwrap();
    writer.end(coordinates).unwrap(); writer.end(object).unwrap();
    let bytes = writer.finish(false).unwrap();
    let expected = [
        30,0,192,28,0,0,0,0,6,115,2,107,0,0,0,0,0,0,0,
        61,0,0,0,0,0,0,0,4,0,0,0,0,0,0,0,
        65,0,0,0,0,0,0,0,11,0,0,0,0,0,0,0,
        76,0,0,0,0,0,0,0,5,83,0,0,0,0,0,0,0,7,
        b't',b'y',b'p',b'e',b'c',b'o',b'o',b'r',b'd',b'i',b'n',b'a',b't',b'e',b's',
        5,5,b'P',b'o',b'i',b'n',b't',7,64,2,24,8,4,16,4,
        0,0,0,0,0,0,240,63,0,0,0,0,0,0,0,64,
    ];
    assert_eq!(bytes, expected);
    assert_eq!(bytes.len(), 115);
    let document = BinaryDocument::new(bytes).unwrap();
    assert_eq!(document.root().unwrap().member(b"type").unwrap().unwrap().string_bytes().unwrap(), b"Point");
    let mut incomplete = BinaryWriter::default();
    incomplete.begin(false, 1, 0).unwrap();
    assert_eq!(incomplete.finish(false), Err(WriteError::State));
    let mut failed = BinaryWriter::default();
    let root = failed.begin(false, 1, 0).unwrap();
    failed.string(&[b'x';300]).unwrap();
    assert_eq!(failed.end(root), Err(WriteError::Width));
    assert_eq!(failed.double(1.0), Err(WriteError::State));
    assert_eq!(failed.finish(false), Err(WriteError::State));
    let mut fields = BinaryWriter::default();
    fields.begin(true, 1, 0).unwrap();
    assert_eq!(fields.double(1.0), Err(WriteError::State));
    assert_eq!(fields.finish(false), Err(WriteError::State));
}


#[test]
fn native_construction_extends_owned_snapshots_and_rejects_wrong_kind_atomically() {
    use crate::construction::{BuildError, ContainerBuilder, Scalar};
    let mut object = ContainerBuilder::object();
    object.insert(b"b", &Scalar::Signed(0).encode().unwrap()).unwrap();
    let source = crate::parse_binary(br#"{"a":1,"b":2}"#, ParseOptions::default()).unwrap();
    object.extend(&source).unwrap();
    drop(source);
    let bad = crate::parse_binary(b"[]", ParseOptions::default()).unwrap();
    assert!(matches!(object.extend(&bad), Err(BuildError::InvalidKind)));
    let result = object.finish(DuplicatePolicy::Overwrite, true, false, None).unwrap();
    assert_eq!((result.input_count, result.output_count), (3, 2));
    assert_eq!(result.binary, crate::parse_binary(br#"{"a":1,"b":2}"#, ParseOptions::default()).unwrap());
    let mut array = ContainerBuilder::array();
    array.extend(&crate::parse_binary(br#"[[1],{"a":2}]"#, ParseOptions::default()).unwrap()).unwrap();
    array.append(&Scalar::String(b"last").encode().unwrap()).unwrap();
    let result = array.finish(DuplicatePolicy::Preserve, false, false, None).unwrap();
    assert_eq!(result.binary, crate::parse_binary(br#"[[1],{"a":2},"last"]"#, ParseOptions::default()).unwrap());
}


#[test]
fn native_construction_renames_keys_atomically() {
    use crate::construction::{BuildError, ContainerBuilder};
    let mut builder = ContainerBuilder::object();
    builder.extend(&crate::parse_binary(br#"{"dimensions":3,"other":true}"#, ParseOptions::default()).unwrap()).unwrap();
    builder.rename_key(b"dimensions", b"dimension").unwrap();
    let expected = crate::parse_binary(br#"{"dimension":3,"other":true}"#, ParseOptions::default()).unwrap();
    assert_eq!(builder.finish(DuplicatePolicy::Overwrite, true, false, None).unwrap().binary, expected);
    assert!(matches!(builder.rename_key(b"dimension", b"other"), Err(BuildError::DuplicateKey)));
    assert!(matches!(builder.rename_key(b"missing", b"other"), Err(BuildError::MissingKey)));
    assert_eq!(builder.finish(DuplicatePolicy::Overwrite, true, false, None).unwrap().binary, expected);
}


#[test]
fn native_aggregate_preserves_signed_width_thresholds() {
    use crate::construction::{ContainerBuilder, Scalar};
    for (count, expected) in [(50, 205), (127, 513), (128, 518), (255, 1026), (256, 1030),
                              (4095, 16386), (4096, 24584)] {
        let mut builder = ContainerBuilder::array();
        for _ in 0..count { builder.append(&Scalar::Null.encode().unwrap()).unwrap(); }
        let output = builder.finish(DuplicatePolicy::Preserve, false, true, None).unwrap();
        assert_eq!(output.binary.len(), expected, "null array count {}", count);
        assert_eq!(BinaryDocument::new(output.binary).unwrap().root().unwrap().len().unwrap(), count);
    }
    let mut builder = ContainerBuilder::array();
    for _ in 0..10 { builder.append(&Scalar::Signed(-1).encode().unwrap()).unwrap(); }
    assert_eq!(builder.finish(DuplicatePolicy::Preserve, false, true, None).unwrap().binary.len(), 135);
    let mut builder = ContainerBuilder::object();
    for index in 0..10 { builder.insert(index.to_string().as_bytes(), &Scalar::Float(1e-200).encode().unwrap()).unwrap(); }
    assert_eq!(builder.finish(DuplicatePolicy::Overwrite, true, true, None).unwrap().binary.len(), 165);
}

#[test]
fn containment_rejects_binary_cycles() {
    let cyclic = BinaryDocument::new([7, 0, 1, 6, 0, 7]).unwrap();
    let candidate = BinaryDocument::new(parse_binary(b"1", Default::default()).unwrap()).unwrap();
    assert!(matches!(
        crate::compare::contains(cyclic.root().unwrap(), candidate.root().unwrap()),
        Err(crate::CompareError::Binary(error)) if error.kind == BinaryErrorKind::InvalidDepth
    ));
}

#[test]
fn containment_handles_deep_binary_values_without_recursive_descent() {
    for object in [false, true] {
        let mut writer = crate::writer::BinaryWriter::default();
        let mut containers = Vec::new();
        for _ in 0..12_000 {
            containers.push(writer.begin(object, 1, 2).unwrap());
            if object { writer.key(b"a").unwrap(); }
        }
        writer.string(b"needle").unwrap();
        for container in containers.into_iter().rev() { writer.end(container).unwrap(); }
        let document = BinaryDocument::new(writer.finish(false).unwrap()).unwrap();
        let candidate = BinaryDocument::new(parse_binary(br#""needle""#, Default::default()).unwrap()).unwrap();
        assert!(crate::compare::contains(
            document.root().unwrap(),
            if object { document.root().unwrap() } else { candidate.root().unwrap() },
        ).unwrap());
    }
}

#[test]
fn containment_preserves_nested_search_and_empty_object_semantics() {
    let query = |text: &str| Query::from_text(text.as_bytes(), Default::default()).unwrap();
    assert!(query("[[2],1]").contains(&query("1"), None).unwrap().unwrap());
    assert!(query("[[2],1]").contains(&query("[1,2]"), None).unwrap().unwrap());
    assert!(query(r#"[{"a":2},1]"#).contains(&query("[1]"), None).unwrap().unwrap());
    assert!(query("[1,[2]]").contains(&query("1"), None).unwrap().unwrap());
    assert!(query(r#"{"a":1,"b":{}}"#)
        .contains(&query(r#"{"a":1,"b":{"x":2}}"#), None).unwrap().unwrap());
    assert!(!query(r#"{"b":{}}"#)
        .contains(&query(r#"{"b":{"x":2}}"#), None).unwrap().unwrap());
}

#[test]
fn containment_allows_reused_subtrees_and_propagates_scalar_errors() {
    let target = BinaryDocument::new([7, 0, 2, 14, 8, 7, 8, 7, 7, 0, 1, 6, 1, 0x82]).unwrap();
    let candidate = BinaryDocument::new(parse_binary(b"[1,1]", Default::default()).unwrap()).unwrap();
    assert!(crate::compare::contains(target.root().unwrap(), candidate.root().unwrap()).unwrap());

    let dates = BinaryDocument::new([7, 0, 1, 10, 6, 9, 0, 0, 0, 0]).unwrap();
    let date = dates.root().unwrap().value(0).unwrap();
    let mut unavailable = |_: crate::BinaryValue<'_>, _: crate::BinaryValue<'_>| -> Result<std::cmp::Ordering, i32> { Err(17) };
    assert!(matches!(
        crate::compare::contains_with(dates.root().unwrap(), date, &mut unavailable),
        Err(crate::compare::OrderError::Scalar(17))
    ));
}

#[test]
fn aggregate_scalar_errors_preserve_prior_values() {
    use crate::construction::{BuildError, ContainerBuilder, Scalar};
    let mut builder = ContainerBuilder::array();
    builder.append_aggregate(&[], &Scalar::Signed(1).encode().unwrap()).unwrap();
    let extended = Scalar::Database { kind: 33, payload: &0i64.to_le_bytes() }.encode().unwrap();
    assert!(matches!(builder.append_aggregate(&[], &extended), Err(BuildError::UnsupportedType)));
    let output = builder.finish(DuplicatePolicy::Preserve, false, true, None).unwrap();
    assert_eq!(BinaryDocument::new(output.binary).unwrap().root().unwrap().len().unwrap(), 1);
    let mut nested = ContainerBuilder::array();
    nested.append(&extended).unwrap();
    let inner = nested.finish(DuplicatePolicy::Preserve, false, false, None).unwrap();
    builder.append_aggregate(&[], &inner.binary).unwrap();
}


#[test]
fn stored_documents_own_framing_and_preserve_scalar_and_container_bytes() {
    use crate::mutation::editor::{Editor, StoredValue};
    for text in [b"null".as_slice(), b"-1024", b"18446744073709551615", b"true", b"2.5", br#""a\u0000z""#,
                 br#"{"a":[1,2],"b":"text"}"#] {
        let mut input = crate::parse_binary(text, Default::default()).unwrap();
        let position = StoredValue::encoded_position(input.as_slice(), 0).unwrap();
        let value = StoredValue::open(input.as_slice(), position.offset, position.raw_type, position.entry_type).unwrap();
        assert_eq!(value.to_document().unwrap(), input);
        assert_eq!(value.free_space().unwrap(), 0);
        assert_eq!(value.metadata().unwrap().value_type, BinaryDocument::new(input.as_slice()).unwrap().root().unwrap().json_type().unwrap());
        if input[0] == 30 {
            input[1] |= 1;
            let value = StoredValue::open(input.as_slice(), position.offset, position.raw_type, position.entry_type).unwrap();
            assert_eq!(value.to_document().unwrap(), input);
            assert!(value.document_info().lexicographical);
        }
    }
    for tag in [20,21,22,23,28,29] {
        let input = [tag,3,b'a',0,b'z'];
        let position = StoredValue::encoded_position(input.as_slice(), 0).unwrap();
        let value = StoredValue::open(input.as_slice(), position.offset, position.raw_type, position.entry_type).unwrap();
        assert_eq!(value.to_document().unwrap(), input);
    }
    let original = crate::parse_binary(br#"{"a":"short","b":[1,2]}"#, Default::default()).unwrap();
    let replacement = BinaryDocument::new(crate::parse_binary(br#""a much longer forwarded value""#, Default::default()).unwrap()).unwrap();
    let mut editor = Editor::open(original.as_slice()).unwrap();
    editor.replace(&[], 0, replacement.root().unwrap()).unwrap();
    let update = editor.finish();
    let fragmented = publish_native_update(original, &update);
    let root = StoredValue::open(fragmented.as_slice(), 8, 6, 0).unwrap();
    let position = root.child_position(1).unwrap();
    let child = StoredValue::open(fragmented.as_slice(), position.offset, position.raw_type, position.entry_type).unwrap();
    let array = child.to_document().unwrap();
    let packed = root.to_document().unwrap();
    assert!(root.free_space().unwrap() > 0);
    assert!(child.free_space().is_err());
    assert_eq!(u64::from_le_bytes(packed[..8].try_into().unwrap()) >> 22, packed.len() as u64);
    assert!(crate::compare::equal(BinaryDocument::new(packed).unwrap().root().unwrap(), BinaryDocument::new(fragmented).unwrap().root().unwrap()).unwrap());
    assert_eq!(BinaryDocument::new(array).unwrap().root().unwrap().len().unwrap(), 2);
}


#[test]
fn native_merge_deep_binary_ownership_and_encoding_do_not_recurse() {
    use crate::mutation::merge::{MergeAccumulator, MergeInput, MergeMode};
    let mut writer = crate::writer::BinaryWriter::default();
    let mut containers = Vec::new();
    for _ in 0..3000 { containers.push(writer.begin(false, 1, 3).unwrap()); }
    writer.double(1.0).unwrap();
    for id in containers.into_iter().rev() { writer.end(id).unwrap(); }
    let input = BinaryDocument::new(writer.finish(false).unwrap()).unwrap();
    let merger = MergeAccumulator::new(MergeMode::Preserve)
        .push(MergeInput::Value(input.root().unwrap())).unwrap();
    let output = BinaryDocument::new(merger.finish().unwrap().unwrap()).unwrap();
    assert_eq!(output.depth().unwrap(), 3001);
    drop(MergeAccumulator::new(MergeMode::Preserve)
        .push(MergeInput::Value(input.root().unwrap())).unwrap());
}

#[test]
fn constructed_encoding_preserves_legacy_merge_and_update_size_boundaries() {
    use crate::mutation::merge::{MergeAccumulator, MergeInput, MergeMode};
    let empty = BinaryDocument::new(crate::parse_binary(b"{}", Default::default()).unwrap()).unwrap();
    for (count, expected) in [(30, 273), (40, 363)] {
        let text = format!("{{{}}}", (0..count).map(|i| format!("\"{i}\":{i}")).collect::<Vec<_>>().join(","));
        let input = BinaryDocument::new(crate::parse_binary(text.as_bytes(), Default::default()).unwrap()).unwrap();
        let encoded = crate::binary::serialize_constructed_value(input.root().unwrap()).unwrap();
        assert_eq!(encoded.len(), expected);
        assert_eq!(encoded, input.as_bytes());
        for mode in [MergeMode::Preserve, MergeMode::Patch] {
            let output = MergeAccumulator::new(mode)
                .push(MergeInput::Value(input.root().unwrap())).unwrap()
                .push(MergeInput::Value(empty.root().unwrap())).unwrap()
                .finish().unwrap().unwrap();
            assert_eq!(output.len(), expected);
            assert_eq!(output, encoded);
        }
    }
    let text = format!("{{{},\"new\":1}}", (0..30).map(|i| format!("\"{i}\":{i}")).collect::<Vec<_>>().join(","));
    let input = BinaryDocument::new(crate::parse_binary(text.as_bytes(), Default::default()).unwrap()).unwrap();
    assert_eq!(crate::binary::serialize_constructed_value(input.root().unwrap()).unwrap().len(), 283);
    let text = format!("[{}]", vec!["null"; 100].join(","));
    let input = BinaryDocument::new(crate::parse_binary(text.as_bytes(), Default::default()).unwrap()).unwrap();
    let array = BinaryDocument::new(crate::parse_binary(b"[]", Default::default()).unwrap()).unwrap();
    let output = MergeAccumulator::new(MergeMode::Preserve)
        .push(MergeInput::Value(input.root().unwrap())).unwrap()
        .push(MergeInput::Value(array.root().unwrap())).unwrap()
        .finish().unwrap().unwrap();
    assert_eq!(output.len(), 313);
}

#[test]
fn constructed_sources_encode_deep_values_and_reject_cycles_without_recursion() {
    let mut writer = crate::writer::BinaryWriter::default();
    let mut containers = Vec::new();
    for _ in 0..10000 { containers.push(writer.begin(false, 1, 3).unwrap()); }
    writer.double(1.0).unwrap();
    for id in containers.into_iter().rev() { writer.end(id).unwrap(); }
    let input = BinaryDocument::new(writer.finish(false).unwrap()).unwrap();
    let output = BinaryDocument::new(crate::binary::serialize_constructed_value(input.root().unwrap()).unwrap()).unwrap();
    assert_eq!(output.depth().unwrap(), 10001);
    let mut array = crate::construction::ContainerBuilder::array();
    array.append(input.as_bytes()).unwrap();
    let output = array.finish(DuplicatePolicy::Overwrite, true, false, None).unwrap();
    assert_eq!(BinaryDocument::new(output.binary).unwrap().depth().unwrap(), 10002);
    let cycle = BinaryDocument::new(vec![7, 0, 1, 6, 0, 7]).unwrap();
    assert!(matches!(crate::binary::serialize_constructed_value(cycle.root().unwrap()),
        Err(crate::BinaryError { kind: BinaryErrorKind::InvalidDepth, .. })));
}

#[test]
fn native_merge_deep_objects_patch_preserve_and_failure_drop_are_bounded() {
    use crate::mutation::merge::{MergeAccumulator, MergeInput, MergeMode, MergeError};
    let mut writer = crate::writer::BinaryWriter::default();
    let mut containers = Vec::new();
    for _ in 0..3000 {
        containers.push(writer.begin(true, 1, 3).unwrap());
        writer.key(b"a").unwrap();
    }
    writer.double(1.0).unwrap();
    for id in containers.into_iter().rev() { writer.end(id).unwrap(); }
    let input = BinaryDocument::new(writer.finish(false).unwrap()).unwrap();
    for (mode, depth) in [(MergeMode::Preserve, 3002), (MergeMode::Patch, 3001)] {
        let output = MergeAccumulator::new(mode)
            .push(MergeInput::Value(input.root().unwrap())).unwrap()
            .push(MergeInput::Value(input.root().unwrap())).unwrap()
            .finish().unwrap().unwrap();
        assert_eq!(BinaryDocument::new(output).unwrap().depth().unwrap(), depth);
    }
    let merger = MergeAccumulator::new(MergeMode::OraclePatch)
        .push(MergeInput::Value(input.root().unwrap())).unwrap();
    assert!(matches!(merger.push(MergeInput::SqlNull), Err(MergeError::InvalidPatch)));
}

#[test]
fn binary_construction_and_copy_preserve_widened_signed_inline_values() {
    for length in [300, 65536] {
        for number in [-1i64, -128, -129, -32768, -32769, i32::MIN as i64, i32::MIN as i64 - 1] {
            let text = format!("[\"{}\",{number}]", "x".repeat(length));
            let input = BinaryDocument::new(crate::parse_binary(text.as_bytes(), Default::default()).unwrap()).unwrap();
            assert_eq!(input.root().unwrap().value(1).unwrap().number().unwrap(), Number::I64(number));
            for encoded in [crate::binary::serialize_value(input.root().unwrap()).unwrap(),
                            crate::binary::serialize_constructed_value(input.root().unwrap()).unwrap()] {
                let result = BinaryDocument::new(encoded).unwrap();
                assert_eq!(result.root().unwrap().value(1).unwrap().number().unwrap(), Number::I64(number));
            }
        }
    }
}


#[test]
fn native_hash_preserves_seed_flags_key_and_child_mix_sequence() {
    struct Trace(Vec<(u64, Vec<u8>)>);
    impl crate::hash::HashCapability for Trace {
        type Error = i32;
        fn append_bytes(&mut self, seed: u64, bytes: &[u8]) -> u64 {
            self.0.push((seed, bytes.to_vec()));
            self.0.len() as u64
        }
        fn hash_database_scalar(&mut self, _: u64, _: crate::BinaryValue<'_>) -> Result<u64, i32> { Err(17) }
    }
    let document = BinaryDocument::new(parse_binary(br#"{"a":[true,null],"bb":false}"#, Default::default()).unwrap()).unwrap();
    let mut trace = Trace(Vec::new());
    assert_eq!(crate::hash::hash(document.root().unwrap(), 7, &mut trace).unwrap(), 11);
    assert_eq!(trace.0, vec![
        (7, vec![0x31]), (1, b"a".to_vec()), (2, vec![0x30]), (3, vec![0x33]),
        (3, 4_u64.to_ne_bytes().to_vec()), (5, vec![0x32]), (5, 6_u64.to_ne_bytes().to_vec()),
        (2, 7_u64.to_ne_bytes().to_vec()), (8, b"bb".to_vec()), (9, vec![0x34]),
        (9, 10_u64.to_ne_bytes().to_vec()),
    ]);
    let number = BinaryDocument::new(parse_binary(b"[1]", Default::default()).unwrap()).unwrap();
    let mut trace = Trace(Vec::new());
    assert!(matches!(crate::hash::hash(number.root().unwrap(), 9, &mut trace), Err(crate::hash::HashError::Scalar(17))));
    assert_eq!(trace.0, vec![(9, vec![0x30])]);
}

#[test]
fn native_hash_rejects_cycles_and_handles_deep_documents_iteratively() {
    struct Counter(u64);
    impl crate::hash::HashCapability for Counter {
        type Error = ();
        fn append_bytes(&mut self, _: u64, _: &[u8]) -> u64 { self.0 += 1; self.0 }
        fn hash_database_scalar(&mut self, _: u64, _: crate::BinaryValue<'_>) -> Result<u64, ()> { unreachable!() }
    }
    let cyclic = BinaryDocument::new([7, 0, 1, 6, 0, 7]).unwrap();
    assert!(matches!(crate::hash::hash(cyclic.root().unwrap(), 0, &mut Counter(0)),
        Err(crate::hash::HashError::Binary(error)) if error.kind == BinaryErrorKind::InvalidDepth));
    for object in [false, true] {
        let mut writer = crate::writer::BinaryWriter::default();
        let mut containers = Vec::new();
        for _ in 0..12_000 {
            containers.push(writer.begin(object, 1, 2).unwrap());
            if object { writer.key(b"a").unwrap(); }
        }
        writer.string(b"leaf").unwrap();
        for container in containers.into_iter().rev() { writer.end(container).unwrap(); }
        let document = BinaryDocument::new(writer.finish(false).unwrap()).unwrap();
        assert_eq!(crate::hash::hash(document.root().unwrap(), 99, &mut Counter(0)).unwrap(),
            if object { 36_001 } else { 24_001 });
    }
}

#[test]
fn native_hash_keeps_scalar_capability_and_string_bytes_separate() {
    struct Capability { strings: Vec<Vec<u8>>, scalars: Vec<crate::JsonType> }
    impl crate::hash::HashCapability for Capability {
        type Error = ();
        fn append_bytes(&mut self, _: u64, bytes: &[u8]) -> u64 { self.strings.push(bytes.to_vec()); 1 }
        fn hash_database_scalar(&mut self, seed: u64, value: crate::BinaryValue<'_>) -> Result<u64, ()> {
            self.scalars.push(value.json_type().unwrap()); Ok(seed + 10)
        }
    }
    for kind in [5, 20, 21, 22, 23, 28, 29] {
        let bytes = [kind, 3, b'a', 0, b'b'];
        let document = BinaryDocument::new(bytes).unwrap();
        let mut capability = Capability { strings: Vec::new(), scalars: Vec::new() };
        assert_eq!(crate::hash::hash(document.root().unwrap(), 99, &mut capability).unwrap(), 1);
        assert_eq!(capability.strings, vec![b"a\0b".to_vec()]);
        assert!(capability.scalars.is_empty());
    }
    for bytes in [vec![2, 1], vec![9, 0, 0, 0, 0], vec![13, 27, 0, 1, 0, 0, 0, 0, 0, 0, 0, b'x']] {
        let document = BinaryDocument::new(bytes).unwrap();
        let mut capability = Capability { strings: Vec::new(), scalars: Vec::new() };
        assert_eq!(crate::hash::hash(document.root().unwrap(), 99, &mut capability).unwrap(), 109);
        assert_eq!(capability.scalars.len(), 1);
        assert!(capability.strings.is_empty());
    }
}


#[test]
fn search_rejects_cyclic_binary_and_handles_deep_values_iteratively() {
    let query = Query::from_binary(vec![7, 0, 1, 6, 0, 7]).unwrap();
    for first_only in [false, true] {
        let error = query.search(&[], SearchOptions { pattern: "%", escape: '\\', first_only }).unwrap_err();
        assert_eq!(error.kind, BinaryErrorKind::InvalidDepth);
    }
    for object in [false, true] {
        let mut writer = crate::writer::BinaryWriter::default();
        let mut containers = Vec::new();
        for _ in 0..12_000 {
            containers.push(writer.begin(object, 1, 2).unwrap());
            if object { writer.key(b"a").unwrap(); }
        }
        writer.string(b"leaf").unwrap();
        for container in containers.into_iter().rev() { writer.end(container).unwrap(); }
        let query = Query::from_binary(writer.finish(false).unwrap()).unwrap();
        let expected = "$".to_owned() + &if object { ".a".repeat(12_000) } else { "[0]".repeat(12_000) };
        assert_eq!(search_paths(&query, &[], "leaf", '\\'), vec![expected]);
        assert!(query.search(&[], SearchOptions { pattern: "missing", escape: '\\', first_only: false }).unwrap().is_none());
    }
}

#[test]
fn search_revisits_shared_values_at_each_distinct_path() {
    let query = Query::from_binary(vec![7, 0x40, 2, 17, 8, 7, 8, 7, 7, 0x40, 1, 9, 6, 5, 5, 1, b'x']).unwrap();
    assert_eq!(search_paths(&query, &[], "x", '\\'), ["$[0][0]", "$[1][0]"]);
    let result = query.search(&[], SearchOptions { pattern: "x", escape: '\\', first_only: true }).unwrap().unwrap();
    assert_eq!(BinaryDocument::new(result).unwrap().root().unwrap().string_bytes().unwrap(), b"$[0][0]");
}


#[test]
fn recursive_search_scopes_preserve_all_ancestor_locations() {
    let query = Query::from_text(br#"{"a":"leaf","arr":[{"a":"leaf"},{"b":{"a":"leaf"}}],"nested":{"a":"leaf"}}"#, Default::default()).unwrap();
    let path = Path::parse_mysql(b"$**.a").unwrap();
    assert_eq!(search_paths(&query, &[path], "leaf", '\\'),
        ["$.a", "$.arr[0].a", "$.arr[1].b.a", "$.nested.a"]);
    let cyclic = Query::from_binary(vec![7, 0, 1, 6, 0, 7]).unwrap();
    let path = Path::parse_mysql(b"$**.missing").unwrap();
    assert_eq!(cyclic.query(&path, Default::default()).unwrap_err().kind, BinaryErrorKind::InvalidDepth);
    assert_eq!(cyclic.search(&[path], SearchOptions { pattern: "%", escape: '\\', first_only: false }).unwrap_err().kind,
        BinaryErrorKind::InvalidDepth);
    let shared = Query::from_binary(vec![7, 0x40, 2, 17, 8, 7, 8, 7, 7, 0x40, 1, 9, 6, 5, 5, 1, b'x']).unwrap();
    assert_eq!(search_paths(&shared, &[Path::parse_mysql(b"$**[0]").unwrap()], "x", '\\'),
        ["$[0][0]", "$[1][0]"]);
}

#[test]
fn binary_copy_and_construction_keep_object_lookup_order_consistent() {
    use crate::mutation::editor::StoredValue;
    fn document(lexicographical: bool) -> Vec<u8> {
        let mut writer = crate::writer::BinaryWriter::default();
        let object = writer.begin(true, 2, 0).unwrap();
        for key in if lexicographical { [b"aa".as_slice(), b"b"] } else { [b"b".as_slice(), b"aa"] } {
            writer.key(key).unwrap();
        }
        writer.string(b"first").unwrap();
        writer.string(b"second").unwrap();
        writer.end(object).unwrap();
        writer.finish(lexicographical).unwrap()
    }
    fn check(bytes: &[u8], child: bool, expected: u64) {
        let root = StoredValue::encoded_position(bytes, 0).unwrap();
        let root = StoredValue::open(bytes, root.offset, root.raw_type, root.entry_type).unwrap();
        let value = if child { let p = root.child_position(0).unwrap(); StoredValue::open(bytes, p.offset, p.raw_type, p.entry_type).unwrap() } else { root };
        assert_eq!(value.find_key(b"b").unwrap(), Some(expected));
        assert_eq!(value.find_key(b"aa").unwrap(), Some(1 - expected));
    }
    let input = BinaryDocument::new(document(true)).unwrap();
    check(input.as_bytes(), false, 1);
    for output in [binary::serialize_value(input.root().unwrap()).unwrap(),
                   binary::serialize_constructed_value(input.root().unwrap()).unwrap()] {
        check(&output, false, 1);
        assert_ne!(output[1] & 1, 0);
    }
    let other = BinaryDocument::new(document(false)).unwrap();
    let array = binary::serialize_array(&[input.root().unwrap(), other.root().unwrap()]).unwrap();
    check(&array, true, 0);
    let object = binary::serialize_object(&[(b"value", input.root().unwrap())]).unwrap();
    check(&object, true, 0);
    for aggregate in [false, true] {
        let mut builder = crate::construction::ContainerBuilder::array();
        builder.append(input.as_bytes()).unwrap();
        builder.append(other.as_bytes()).unwrap();
        let output = builder.finish(DuplicatePolicy::Preserve, false, aggregate, None).unwrap();
        check(&output.binary, true, 0);
    }
    let outer = binary::serialize_array(&[input.root().unwrap()]).unwrap();
    let mut builder = crate::construction::ContainerBuilder::array();
    builder.extend(&outer).unwrap();
    check(&builder.finish(DuplicatePolicy::Preserve, false, false, None).unwrap().binary, true, 0);
}

#[test]
fn owned_sql_mutations_construct_from_the_complete_overlay() {
    use crate::mutation::editor::{Session};
    let mut documents = vec!["1".to_owned(), "[1,2]".into(), "{\"a\":[1,2],\"x\":{\"y\":3}}".into()];
    documents.push(format!("{{\"a\":\"{}\",\"x\":-1}}", "x".repeat(300)));
    documents.push(format!("{{{}}}", (0..30).map(|i| format!("\"{i}\":{i}")).collect::<Vec<_>>().join(",")));
    let values = ["-1".to_owned(), format!("\"{}\"", "z".repeat(2000))];
    for document in documents {
        let source = parse_binary(document.as_bytes(), Default::default()).unwrap();
        for operation in [SqlOperation::Set, SqlOperation::Replace, SqlOperation::Insert,
                          SqlOperation::Remove, SqlOperation::ArrayAppend, SqlOperation::ArrayInsert] {
            for path in ["$", "$.a", "$.new", "$[1]", "$.a[0]", "$.missing[0]"] {
                for value in &values {
                    let value = BinaryDocument::new(parse_binary(value.as_bytes(), Default::default()).unwrap()).unwrap();
                    let mut session = Session::open(source.as_slice()).unwrap();
                    let mut owned = OwnedEditor::from_binary(BinaryDocument::new(source.as_slice()).unwrap().root().unwrap()).unwrap();
                    let prepared = match session.prepare(source.as_slice(), &Path::parse_mysql(path.as_bytes()).unwrap(), operation) {
                        Ok(prepared) => prepared,
                        Err(crate::mutation::editor::EditError::VacuousPath
                            | crate::mutation::editor::EditError::InvalidPath) => continue,
                        Err(error) => panic!("{document} {path} {operation:?}: {error:?}"),
                    };
                    let owned_plan = owned.prepare(&Path::parse_mysql(path.as_bytes()).unwrap(), operation).unwrap();
                    assert_eq!(prepared.needs_value(), owned_plan.needs_value());
                    let value = prepared.needs_value().then(|| value.root().unwrap());
                    session.apply(source.as_slice(), prepared, value).unwrap();
                    owned = owned.apply(owned_plan, value).unwrap();
                    let packed = BinaryDocument::new(session.binary(source.as_slice()).unwrap()).unwrap();
                    let expected = crate::binary::serialize_constructed_value(packed.root().unwrap()).unwrap();
                    assert_eq!(owned.finish().unwrap(), expected,
                               "{document} {path} {operation:?}");
                }
            }
        }
    }
}

#[test]
fn merge_owned_documents_match_binary_arguments_and_survive_deep_drop() {
    use crate::mutation::merge::{MergeAccumulator, MergeArgument, MergeInput, MergeMode};
    let documents = [
        "null", "true", "-1", "18446744073709551615", "-0.0", "1e-200", "\"雪😀\"",
        "[1,{\"a\":null}]", "{\"x\":[1,2],\"nested\":{\"a\":3}}",
        "{\"x\":null,\"nested\":{\"b\":4}}",
    ];
    for mode in [MergeMode::Patch, MergeMode::Preserve, MergeMode::OraclePatch] {
        for left in documents {
            for right in documents {
                let mut owned = MergeAccumulator::new(mode);
                let mut binary = MergeAccumulator::new(mode);
                for text in [left, right] {
                    owned = owned.push_prepared(MergeArgument::from_document(crate::parse(text.as_bytes(), Default::default()).unwrap()).unwrap()).unwrap();
                    let bytes = BinaryDocument::new(crate::parse_binary(text.as_bytes(), Default::default()).unwrap()).unwrap();
                    binary = binary.push(MergeInput::Value(bytes.root().unwrap())).unwrap();
                }
                assert_eq!(owned.finish().unwrap(), binary.finish().unwrap(), "{left} / {right}");
            }
        }
    }
    let mut document = Document::String("leaf".into());
    for _ in 0..12_000 { document = Document::Array(vec![document]); }
    let argument = MergeArgument::from_document(document).unwrap();
    let bytes = MergeAccumulator::new(MergeMode::Patch).push_prepared(argument).unwrap().finish().unwrap().unwrap();
    assert_eq!(BinaryDocument::new(bytes).unwrap().depth().unwrap(), 12_001);
}

#[test]
fn merge_binary_strings_keep_invalid_utf8_bytes_and_duplicate_last_values() {
    use crate::mutation::merge::{MergeAccumulator, MergeArgument, MergeInput, MergeMode};
    let scalar = BinaryDocument::new(vec![5, 1, 255]).unwrap();
    let merger = MergeAccumulator::new(MergeMode::Patch).push(MergeInput::Value(scalar.root().unwrap())).unwrap();
    assert_eq!(merger.finish().unwrap().unwrap(), scalar.as_bytes());
    let document = Document::Object(vec![("key".into(), Document::String("discard".into())),
        ("key".into(), Document::Array(vec![Document::Bool(false)]))]);
    let merger = MergeAccumulator::new(MergeMode::Preserve).push_prepared(MergeArgument::from_document(document).unwrap()).unwrap();
    assert_eq!(merger.finish().unwrap().unwrap(), crate::parse_binary(br#"{"key":[false]}"#, Default::default()).unwrap());
}

#[test]
fn numeric_order_preserves_signed_and_unsigned_integer_boundaries() {
    use crate::construction::Scalar;
    fn no_temporal(_: crate::BinaryValue<'_>, _: crate::BinaryValue<'_>) -> Result<std::cmp::Ordering, ()> {
        unreachable!()
    }
    let values: Vec<(Vec<u8>, i128)> = [i64::MIN, -1, 0, 1, 127, 256, i64::MAX]
        .into_iter().map(|value| (Scalar::Signed(value).encode().unwrap(), value as i128))
        .chain([0, 1, 127, 256, i64::MAX as u64, i64::MAX as u64 + 1, u64::MAX]
            .into_iter().map(|value| (Scalar::Unsigned(value).encode().unwrap(), value as i128)))
        .collect();
    for (a, expected_a) in &values {
        for (b, expected_b) in &values {
            let a = BinaryDocument::new(a.as_slice()).unwrap();
            let b = BinaryDocument::new(b.as_slice()).unwrap();
            let actual = crate::compare::order(a.root().unwrap(), b.root().unwrap(),
                &mut no_temporal).unwrap();
            assert_eq!(actual, expected_a.cmp(expected_b));
        }
    }
}

mod mutation_order_tests {
    use crate::{BinaryDocument, Path, Query, QueryOptions, construction::Scalar, writer::BinaryWriter};
    use crate::mutation::editor::{Editor, StoredValue};

    use crate::mutation::sql::{OwnedEditor, SqlOperation};
    fn object(lexicographical: bool) -> Vec<u8> {
        let mut writer = BinaryWriter::default();
        let root = writer.begin(true, 2, 1).unwrap();
        let keys = if lexicographical { [b"aa".as_slice(), b"b".as_slice()] }
            else { [b"b".as_slice(), b"aa".as_slice()] };
        for key in keys { writer.key(key).unwrap(); }
        for key in keys { writer.string(key).unwrap(); }
        writer.end(root).unwrap();
        writer.finish(lexicographical).unwrap()
    }

    fn outer(lexicographical: bool, array: bool) -> Vec<u8> {
        let mut writer = BinaryWriter::default();
        let root = writer.begin(true, 1, 1).unwrap();
        writer.key(b"target").unwrap();
        let array = array.then(|| writer.begin(false, 1, 1).unwrap());
        writer.string(b"old").unwrap();
        if let Some(array) = array { writer.end(array).unwrap(); }
        writer.end(root).unwrap();
        writer.finish(lexicographical).unwrap()
    }

    fn apply(session: &mut Option<OwnedEditor>, _source: &[u8], operation: SqlOperation, path: &str, value: &BinaryDocument<Vec<u8>>) {
        let editor = session.take().unwrap();
        let prepared = editor.prepare(&Path::parse_mysql(path.as_bytes()).unwrap(), operation).unwrap();
        let value = prepared.needs_value().then(|| value.root().unwrap());
        *session = Some(editor.apply(prepared, value).unwrap());
    }

    fn assert_updated(session: &mut Option<OwnedEditor>, _source: &[u8], path: &str) {
        let query = Query::from_binary(session.take().unwrap().finish().unwrap()).unwrap();
        let hits = query.query(&Path::parse_mysql(path.as_bytes()).unwrap(), QueryOptions::default()).unwrap();
        assert_eq!(hits.len(), 1, "{path}");
        assert_eq!(hits[0].string_bytes().unwrap(), b"updated", "{path}");
    }

    #[test]
    fn sql_imports_keep_all_nested_objects_in_the_document_order() {
        let updated = BinaryDocument::new(Scalar::String(b"updated").encode().unwrap()).unwrap();
        for lexicographical in [false, true] {
            let replacement = BinaryDocument::new(object(!lexicographical)).unwrap();
            let key = if lexicographical { "aa" } else { "b" };
            for (operation, path, array, selected) in [
                (SqlOperation::Set, "$.target", false, format!("$.target.{key}")),
                (SqlOperation::Replace, "$.target", false, format!("$.target.{key}")),
                (SqlOperation::Insert, "$.new", false, format!("$.new.{key}")),
                (SqlOperation::ArrayAppend, "$.target", false, format!("$.target[1].{key}")),
                (SqlOperation::ArrayInsert, "$.target[0]", true, format!("$.target[0].{key}")),
                (SqlOperation::Replace, "$", false, "$.b".into()),
            ] {
                let source = outer(lexicographical, array);
                let mut session = Some(OwnedEditor::from_binary(BinaryDocument::new(source.as_slice()).unwrap().root().unwrap()).unwrap());
                apply(&mut session, &source, operation, path, &replacement);
                apply(&mut session, &source, SqlOperation::Replace, &selected, &updated);
                assert_updated(&mut session, &source, &selected);
            }
        }
    }

    #[test]
    fn root_array_wrapping_normalizes_the_previous_document() {
        let updated = BinaryDocument::new(Scalar::String(b"updated").encode().unwrap()).unwrap();
        for lexicographical in [false, true] {
            let source = object(lexicographical);
            let mut session = Some(OwnedEditor::from_binary(BinaryDocument::new(source.as_slice()).unwrap().root().unwrap()).unwrap());
            apply(&mut session, &source, SqlOperation::ArrayAppend, "$", &updated);
            apply(&mut session, &source, SqlOperation::Replace, "$[0].b", &updated);
            assert_updated(&mut session, &source, "$[0].b");
        }
    }

    #[test]
    fn sparse_imports_preserve_lookup_order_after_insert_and_replace() {
        for lexicographical in [false, true] {
            let replacement = BinaryDocument::new(object(!lexicographical)).unwrap();
            for insert in [false, true] {
                let mut writer = BinaryWriter::default();
                let root = writer.begin(false, 1, 1).unwrap();
                writer.string(b"old").unwrap();
                writer.end(root).unwrap();
                let mut source = writer.finish(lexicographical).unwrap();
                let mut editor = Editor::open(source.as_slice()).unwrap();
                if insert { editor.insert(&[], 0, &[], replacement.root().unwrap()).unwrap(); }
                else { editor.replace(&[], 0, replacement.root().unwrap()).unwrap(); }
                let update = editor.finish();
                for write in update.writes {
                    if write.reset { source.clear(); }
                    source.resize(source.len().max(write.offset as usize + write.bytes.len()), 0);
                    source[write.offset as usize..write.offset as usize + write.bytes.len()].copy_from_slice(&write.bytes);
                }
                let root = StoredValue::encoded_position(source.as_slice(), 0).unwrap();
                let root = StoredValue::open(source.as_slice(), root.offset, root.raw_type, root.entry_type).unwrap();
                let child = root.child_position(0).unwrap();
                let child = StoredValue::open(source.as_slice(), child.offset, child.raw_type, child.entry_type).unwrap();
                assert!(child.find_key(if lexicographical { b"aa" } else { b"b" }).unwrap().is_some());
            }
        }
    }
}

#[test]
fn owned_mutations_keep_duplicate_binary_members_and_reject_stale_plans() {
    let values = [1, 2, 3].map(|value| BinaryDocument::new(crate::construction::Scalar::Signed(value).encode().unwrap()).unwrap());
    let pairs: Vec<_> = values.iter().map(|value| (b"a".as_slice(), value.root().unwrap())).collect();
    let source = BinaryDocument::new(binary::serialize_object(&pairs).unwrap()).unwrap();
    let replacement = BinaryDocument::new(crate::construction::Scalar::Signed(9).encode().unwrap()).unwrap();
    let editor = OwnedEditor::from_binary(source.root().unwrap()).unwrap();
    let path = Path::parse_mysql(b"$.a").unwrap();
    let plan = editor.prepare(&path, SqlOperation::Replace).unwrap();
    let stale = editor.prepare(&path, SqlOperation::Replace).unwrap();
    let editor = editor.apply(plan, Some(replacement.root().unwrap())).unwrap();
    assert!(matches!(editor.apply(stale, Some(replacement.root().unwrap())), Err(crate::mutation::editor::EditError::StalePlan)));
    let editor = OwnedEditor::from_binary(source.root().unwrap()).unwrap();
    let plan = editor.prepare(&path, SqlOperation::Replace).unwrap();
    let editor = editor.apply(plan, Some(replacement.root().unwrap())).unwrap();
    let output = BinaryDocument::new(editor.finish().unwrap()).unwrap();
    assert_eq!(output.root().unwrap().len().unwrap(), 3);
    let actual: Vec<_> = (0..3).map(|index| output.root().unwrap().value(index).unwrap().number().unwrap()).collect();
    assert_eq!(actual, [Number::I64(1), Number::I64(9), Number::I64(3)]);
}

#[test]
fn owned_mutation_no_ops_keep_generation_and_required_value_errors() {
    let source = crate::parse(br#"{"a":1}"#, Default::default()).unwrap();
    let editor = OwnedEditor::from_document(source).unwrap();
    let path = Path::parse_mysql(b"$.a").unwrap();
    let replace = editor.prepare(&path, SqlOperation::Set).unwrap();
    let no_op = editor.prepare(&path, SqlOperation::Insert).unwrap();
    assert!(!no_op.needs_value());
    let editor = editor.apply(no_op, None).unwrap();
    let value = BinaryDocument::new(crate::construction::Scalar::Signed(9).encode().unwrap()).unwrap();
    let editor = editor.apply(replace, Some(value.root().unwrap())).unwrap();
    assert_eq!(editor.finish().unwrap(), crate::parse_binary(br#"{"a":9}"#, Default::default()).unwrap());
    let editor = OwnedEditor::from_document(crate::Document::Null).unwrap();
    let plan = editor.prepare(&Path::parse_mysql(b"$.missing").unwrap(), SqlOperation::Set).unwrap();
    assert!(plan.needs_value());
    assert!(matches!(editor.apply(plan, None), Err(crate::mutation::editor::EditError::InvalidIndex)));
}

#[test]
fn owned_mutations_edit_and_discard_deep_values_without_recursive_state() {
    let mut document = crate::Document::Null;
    for _ in 0..12_000 { document = crate::Document::Array(vec![document]); }
    let editor = OwnedEditor::from_document(document).unwrap();
    let original = BinaryDocument::new(editor.finish().unwrap()).unwrap();
    assert_eq!(original.depth().unwrap(), 12_001);
    let editor = OwnedEditor::from_binary(original.root().unwrap()).unwrap();
    let path = Path::parse_mysql(b"$[0]").unwrap();
    let plan = editor.prepare(&path, SqlOperation::Set).unwrap();
    let editor = editor.apply(plan, Some(original.root().unwrap())).unwrap();
    let output = BinaryDocument::new(editor.finish().unwrap()).unwrap();
    assert_eq!(output.depth().unwrap(), 12_002);
    let value = BinaryDocument::new(crate::construction::Scalar::Signed(-1).encode().unwrap()).unwrap();
    let editor = OwnedEditor::from_binary(output.root().unwrap()).unwrap();
    let plan = editor.prepare(&Path::parse_mysql(b"$").unwrap(), SqlOperation::Replace).unwrap();
    let editor = editor.apply(plan, Some(value.root().unwrap())).unwrap();
    assert_eq!(editor.finish().unwrap(), value.as_bytes());
}

#[test]
fn owned_constructor_inputs_preserve_bytes_and_failed_append_state() {
    use crate::construction::{ContainerBuilder, Scalar};
    let inputs = [Scalar::Null.encode().unwrap(), Scalar::Signed(-1).encode().unwrap(),
        Scalar::Unsigned(u64::MAX).encode().unwrap(), Scalar::Float(-0.0).encode().unwrap(),
        Scalar::String(&[b'x'; 300]).encode().unwrap(),
        Scalar::String(&[b'y'; 65_536]).encode().unwrap(),
        Scalar::Opaque { field_type: 28, bytes: &[0, 255] }.encode().unwrap(),
        parse_binary(br#"{"aa":[-1,null],"b":{"":true}}"#, Default::default()).unwrap()];
    for object in [false, true] {
        let mut borrowed = if object { ContainerBuilder::object() } else { ContainerBuilder::array() };
        let mut owned = if object { ContainerBuilder::object() } else { ContainerBuilder::array() };
        for (index, input) in inputs.iter().enumerate() {
            let key = format!("key{}", index % 3);
            if object {
                borrowed.insert(key.as_bytes(), input).unwrap();
                owned.insert_owned(key.as_bytes(), input.clone()).unwrap();
            } else {
                borrowed.append(input).unwrap();
                owned.append_owned(input.clone()).unwrap();
            }
        }
        assert_eq!(borrowed.approximate_length(), owned.approximate_length());
        for duplicate in [DuplicatePolicy::Preserve, DuplicatePolicy::Overwrite, DuplicatePolicy::KeepFirst, DuplicatePolicy::Reject] {
            for aggregate in [false, true] {
                let before = borrowed.finish(duplicate, true, aggregate, None).map(|output| output.binary);
                let after = owned.finish(duplicate, true, aggregate, None).map(|output| output.binary);
                match (before, after) {
                    (Ok(before), Ok(after)) => assert_eq!(before, after),
                    (Err(crate::construction::BuildError::DuplicateKey),
                     Err(crate::construction::BuildError::DuplicateKey)) => {},
                    (before, after) => panic!("{before:?} != {after:?}"),
                }
            }
        }
        let prior = owned.finish(DuplicatePolicy::Preserve, true, false, None).unwrap().binary;
        for invalid in [Vec::new(), vec![255], vec![7, 0, 1, 6, 0, 7]] {
            let result = if object { owned.insert_owned(b"bad", invalid) } else { owned.append_owned(invalid) };
            assert!(result.is_err());
            assert_eq!(owned.finish(DuplicatePolicy::Preserve, true, false, None).unwrap().binary, prior);
        }
    }
}

#[test]
fn parsed_selections_preserve_binary_query_policy_and_occurrence_ownership() {
    let texts = ["null", "1", "[1,2,3]", "{\"a\":[{\"b\":[-129,256]},{\"b\":[9]}]}",
        "{\"a\":1,\"a\":2,\"\":{\"雪\":\"v\"},\"bb\":null}",
        "{\"a\":{\"a\":1},\"arr\":[{\"a\":2},[3]],\"雪\":true}",
        "{\"aa\":1,\"b\":2,\"雪\":3,\"\":4,\"b\":5}"];
    let paths = ["$", "$.a", "$.a[*]", "$.a[*].b[*]", "$.*", "$[*]", "$[0]", "$[last]", "$[1 to 3]", "$**.a", "$**[*]", "$**.\"雪\"", "$.\"\""];
    for policy in [DuplicatePolicy::Overwrite, DuplicatePolicy::Preserve, DuplicatePolicy::KeepFirst] {
        for text in texts {
            let parsed = crate::Value::from_text(text.as_bytes(), options(policy)).unwrap();
            let binary = Query::from_text(text.as_bytes(), options(policy)).unwrap().into_value().unwrap();
            for path in paths {
                let path = Path::parse_mysql(path.as_bytes()).unwrap();
                for first_only in [false, true] {
                    for auto_wrap in [false, true] {
                        let options = QueryOptions { first_only, auto_wrap };
                        let a = parsed.select(&path, options).unwrap();
                        let b = binary.select(&path, options).unwrap();
                        assert_eq!(a.len(), b.len());
                        for (a, b) in a.iter().zip(b.iter()) {
                            assert_eq!(a.read().to_binary().unwrap(), b.read().to_binary().unwrap());
                        }
                        let identities: std::collections::HashSet<_> = a.iter().map(crate::Value::identity).collect();
                        assert_eq!(identities.len(), a.len());
                    }
                }
                for wrapper in [crate::WrapperPolicy::Without, crate::WrapperPolicy::With, crate::WrapperPolicy::Conditional] {
                    for allow_scalar in [false, true] {
                        for wrap_single in [false, true] {
                            for wrap_multiple in [false, true] {
                                let options = crate::QueryOutputOptions { wrapper, allow_scalar, wrap_single, wrap_multiple };
                                let a = parsed.query_output(&path, options).unwrap();
                                let b = binary.query_output(&path, options).unwrap();
                                assert_eq!((a.binary, a.match_count, a.value_type, a.use_wrapper, a.wrapper_required),
                                    (b.binary, b.match_count, b.value_type, b.use_wrapper, b.wrapper_required));
                            }
                        }
                    }
                }
            }
        }
    }
    let root = crate::Value::from_text(br#"{"a":[{"b":[-129,256]},{"b":[9]}]}"#, Default::default()).unwrap();
    let rows = root.select(&Path::parse_mysql(b"$.a[*]").unwrap(), QueryOptions::default()).unwrap();
    let row = rows.get(0).unwrap().clone();
    drop(rows); drop(root);
    let children = row.select(&Path::parse_mysql(b"$.b[*]").unwrap(), QueryOptions::default()).unwrap();
    let child = children.get(0).unwrap().clone();
    drop(children); drop(row);
    assert_eq!(child.read().number().unwrap(), Number::I64(-129));
}

#[test]
fn parsed_value_deep_selection_encoding_and_drop_do_not_recurse() {
    let mut document = Document::Object(vec![("leaf".into(), Document::Number(Number::I64(7)))]);
    for _ in 0..12_000 { document = Document::Array(vec![document]); }
    let root = crate::Value::from_document(document);
    let selected = root.select(&Path::parse_mysql(b"$**.leaf").unwrap(), QueryOptions { first_only: true, auto_wrap: true }).unwrap();
    let leaf = selected.get(0).unwrap().clone();
    drop(selected);
    let encoded = root.query_output(&Path::parse_mysql(b"$").unwrap(), Default::default()).unwrap().binary.unwrap();
    assert_eq!(BinaryDocument::new(encoded).unwrap().depth().unwrap(), 12_002);
    drop(root);
    assert_eq!(leaf.read().number().unwrap(), Number::I64(7));
    drop(leaf);
}

#[test]
fn constructed_scalar_containers_preserve_wire_format_at_width_boundaries() {
    use crate::construction::{ContainerBuilder, Scalar};
    for object in [false, true] {
        for count in [0, 1, 2, 30, 40, 255, 256, 257] {
            for length in [0, 300, 65_536] {
                let string = format!("\"{}\"", "x".repeat(length));
                let values = ["-1", "-129", "-9223372036854775808", "18446744073709551615", "-0.0", "null", "true", &string];
                let body = (0..count).map(|index| {
                    let value = values[index % values.len()];
                    if object { format!("\"key{index}\":{value}") } else { value.to_owned() }
                }).collect::<Vec<_>>().join(",");
                let text = if object { format!("{{{body}}}") } else { format!("[{body}]") };
                let expected = parse_binary(text.as_bytes(), Default::default()).unwrap();
                let input = BinaryDocument::new(expected.as_slice()).unwrap();
                assert_eq!(binary::serialize_constructed_value(input.root().unwrap()).unwrap(), expected,
                    "object={object}, count={count}, string length={length}");
            }
        }
    }
    let mut values = vec![Scalar::Opaque { field_type: 28, bytes: &[0, 255] }.encode().unwrap()];
    for kind in [9, 10, 11, 12, 32, 33] {
        let payload = if [9, 32].contains(&kind) { vec![0; 4] } else { vec![0; 8] };
        values.push(Scalar::Database { kind, payload: &payload }.encode().unwrap());
    }
    for kind in [20, 21, 22, 23, 28, 29] { values.push(vec![kind, 3, b'a', 0, b'z']); }
    for length in [300, 65_536] {
        let mut builder = ContainerBuilder::array();
        builder.append(&Scalar::Signed(-129).encode().unwrap()).unwrap();
        builder.append(&Scalar::String(&vec![b'x'; length]).encode().unwrap()).unwrap();
        for value in &values { builder.append(value).unwrap(); }
        let output = BinaryDocument::new(builder.finish(DuplicatePolicy::Preserve, false, false, None).unwrap().binary).unwrap();
        assert_eq!(output.root().unwrap().value(0).unwrap().number().unwrap(), Number::I64(-129));
        for (index, expected) in values.iter().enumerate() {
            assert_eq!(binary::serialize_value(output.root().unwrap().value(index as u64 + 2).unwrap()).unwrap(), *expected);
        }
    }
}

#[test]
fn owned_document_decode_keeps_nested_values_and_duplicate_order() {
    let input = br#"{"z":[1,{"b":2,"a":3}],"a":{"dup":4,"dup":[5,6]},"z":7}"#;
    let options = ParseOptions {
        duplicate_policy: DuplicatePolicy::Preserve,
        ..Default::default()
    };
    let document = crate::parse(input, options).unwrap();
    let expected = binary::serialize(document.clone());
    let output = OwnedEditor::from_document(document).unwrap().finish().unwrap();
    assert_eq!(output, expected);
}
