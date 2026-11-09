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

use super::*;
struct LiteralCompiler;
struct Literal(Vec<u8>);
impl Pattern for Literal {
    fn is_match(&self, text: &[u8]) -> Result<bool> {
        Ok(self.0.is_empty() || text.windows(self.0.len()).any(|part| part == self.0))
    }
}
impl PatternCompiler for LiteralCompiler {
    fn compile(&self, text: &[u8]) -> std::result::Result<Box<dyn Pattern>, PatternError> {
        if text == b"[" {
            Err(PatternError::Invalid)
        } else {
            Ok(Box::new(Literal(text.to_vec())))
        }
    }
}
fn binary(text: &str) -> Vec<u8> {
    crate::parse_binary(text.as_bytes(), Default::default()).unwrap()
}
fn schema(text: &str) -> Schema {
    let bytes = binary(text);
    Schema::compile(
        BinaryDocument::new(bytes).unwrap().root().unwrap(),
        &LiteralCompiler,
    )
    .unwrap()
}
fn check(schema: &Schema, text: &str) -> Validation {
    let bytes = binary(text);
    schema
        .validate(BinaryDocument::new(bytes).unwrap().root().unwrap())
        .unwrap()
}
#[test]
fn normalized_map_round_trip() {
    let schema = schema(
        r#"{"type":"object","properties":{"x":{"type":"integer"}},"required":["x"],"additionalProperties":false}"#,
    );
    let expected = binary(
        r#"[{"schema":{"type":32,"required":["x"],"additionalProperties":[["x"],[]]},"properties":{"x":{"schema":{"type":16}}}},{}]"#,
    );
    let actual = schema.encode().unwrap();
    assert!(crate::compare::equal(
        BinaryDocument::new(expected).unwrap().root().unwrap(),
        BinaryDocument::new(&actual[..]).unwrap().root().unwrap()
    )
    .unwrap());
    let decoded = Schema::decode(&actual, &LiteralCompiler).unwrap();
    assert!(check(&decoded, r#"{"x":1}"#).is_valid());
    let failed = check(&decoded, r#"{"x":1.0}"#).failure.unwrap();
    assert_eq!(
        failed,
        Failure {
            keyword: "type".into(),
            document_location: "#/x".into(),
            schema_location: "#/properties/x".into()
        }
    );
}
#[test]
fn draft_four_policy_and_failure_order() {
    let schema = schema(r#"{"type":"number","minimum":5,"maximum":0,"exclusiveMinimum":true}"#);
    assert_eq!(check(&schema, "3").failure.unwrap().keyword, "maximum");
    assert_eq!(check(&schema, "null").failure.unwrap().keyword, "type");
    assert!(check(&super::tests::schema(r#"{"multipleOf":0}"#), "17").is_valid());
    assert!(!check(&super::tests::schema(r#"{"multipleOf":0.1}"#), "0.3").is_valid());
    assert!(check(&super::tests::schema(r#"{"exclusiveMinimum":5}"#), "1").is_valid());
}
#[test]
fn ignored_keywords_and_tag_sensitive_uniqueness() {
    for text in [
        r#"{"enum":[]}"#,
        r#"{"pattern":"["}"#,
        r#"{"minLength":1.5}"#,
        r#"{"type":["unknown","string"]}"#,
    ] {
        assert!(check(&schema(text), r#""x""#).is_valid());
    }
    assert!(!check(&schema(r#"{"type":"unknown"}"#), "null").is_valid());
    assert!(!check(&schema(r#"{"maxLength":1}"#), r#""é""#).is_valid());
    assert!(check(&schema(r#"{"enum":[1]}"#), "1.0").is_valid());
    assert!(check(&schema(r#"{"uniqueItems":true}"#), "[1,1.0]").is_valid());
    assert!(!check(&schema(r#"{"uniqueItems":true}"#), "[1,1]").is_valid());
}
#[test]
fn composition_preserves_count_policy_and_accumulates_item_answers() {
    assert!(check(&schema(r#"{"allOf":[{"maxProperties":0}]}"#), r#"{"x":1}"#).is_valid());
    assert!(check(&schema(r#"{"allOf":[{"maxItems":0}]}"#), "[1]").is_valid());
    assert!(!check(
        &schema(r#"{"anyOf":[{"items":{"type":"string"}},{"items":{"type":"integer"}}]}"#),
        r#"[1,"s"]"#
    )
    .is_valid());
    assert!(check(
        &schema(r#"{"anyOf":[{"items":{"type":"string"}},{"items":{"type":"integer"}}]}"#),
        "[1,2]"
    )
    .is_valid());
    assert!(!check(&schema(r#"{"not":{}}"#), "null").is_valid());
    assert!(check(&schema(r#"{"allOf":[{"not":{}}]}"#), "null").is_valid());
}
#[test]
fn local_refs_and_additional_property_policy() {
    assert!(check(
        &schema(r##"{"$ref":"#/missing","type":"integer"}"##),
        r#""s""#
    )
    .is_valid());
    assert!(!check(
        &schema(r##"{"definitions":{"foo":{"type":"integer"}},"$ref":"#/definitions/foo"}"##),
        r#""s""#
    )
    .is_valid());
    assert!(check(
        &schema(r#"{"required":["x"],"additionalProperties":false}"#),
        r#"{"x":1}"#
    )
    .is_valid());
    assert!(!check(
        &schema(
            r#"{"properties":{"x":{"type":"integer"}},"additionalProperties":{"type":"boolean"}}"#
        ),
        r#"{"x":1,"y":0}"#
    )
    .is_valid());
}
#[test]
fn compilation_errors_and_failed_cache_replacement() {
    let mut cache = Cache::default();
    assert!(cache
        .get_or_compile(1, br#"{"type":"integer"}"#, false, &LiteralCompiler)
        .is_ok());
    assert!(matches!(
        cache.get_or_compile(1, b"{\"allOf\":[false]}", false, &LiteralCompiler),
        Err(Error::InvalidSchema)
    ));
    assert!(cache
        .get_or_compile(1, br#"{"type":"integer"}"#, false, &LiteralCompiler)
        .is_ok());
    assert!(matches!(
        cache.get_or_compile(
            0,
            br#"{"properties":{"a":false},"patternProperties":{"a":{"type":"integer"}}}"#,
            false,
            &LiteralCompiler
        ),
        Err(Error::MissingSchema)
    ));
    assert!(matches!(
        cache.get_or_compile(0, b"false", false, &LiteralCompiler),
        Err(Error::InvalidSchema)
    ));
}

#[test]
fn binary_scalar_tags_and_exact_bounds() {
    let mut bytes = vec![JsonType::Decimal as u8, 0, 0, 0, 0];
    bytes.extend_from_slice(&0xc0000001u32.to_be_bytes());
    bytes.extend_from_slice(&1u32.to_le_bytes());
    let decimal = BinaryDocument::new(bytes).unwrap();
    assert!(!schema(r#"{"type":"integer"}"#)
        .validate(decimal.root().unwrap())
        .unwrap()
        .is_valid());
    assert!(
        schema(r#"{"type":"number","enum":[1],"minimum":1,"maximum":1}"#)
            .validate(decimal.root().unwrap())
            .unwrap()
            .is_valid()
    );

    let mut fraction = vec![JsonType::Decimal as u8, 0, 0, 0, 0];
    fraction.extend_from_slice(&0xbf000001u32.to_be_bytes());
    fraction.extend_from_slice(&300_000_000u32.to_le_bytes());
    let fraction = BinaryDocument::new(fraction).unwrap();
    assert!(!schema(r#"{"multipleOf":0.1}"#)
        .validate(fraction.root().unwrap())
        .unwrap()
        .is_valid());
    assert!(schema(r#"{"multipleOf":0.01}"#)
        .validate(fraction.root().unwrap())
        .unwrap()
        .is_valid());

    let mut zero_bytes = vec![JsonType::Decimal as u8, 0, 0, 0, 0];
    zero_bytes.extend_from_slice(&0xc0000000u32.to_be_bytes());
    let zero = BinaryDocument::new(zero_bytes).unwrap();
    assert!(schema(r#"{"maximum":1e200,"minimum":-1e200}"#)
        .validate(zero.root().unwrap())
        .unwrap()
        .is_valid());
    assert!(matches!(
        schema(r#"{"maximum":1e200}"#).validate(decimal.root().unwrap()),
        Err(Error::Compare(CompareError::NumericOverflow))
    ));
    assert!(!check(
        &schema(r#"{"maximum":9223372036854775808}"#),
        "9223372036854775808.0"
    )
    .is_valid());
    let one = BinaryDocument::new(binary("1")).unwrap();
    let values = [decimal.root().unwrap(), one.root().unwrap()];
    let array = BinaryDocument::new(binary::serialize_array(&values).unwrap()).unwrap();
    assert!(schema(r#"{"uniqueItems":true}"#)
        .validate(array.root().unwrap())
        .unwrap()
        .is_valid());
    let enumeration =
        BinaryDocument::new(binary::serialize_array(&[decimal.root().unwrap()]).unwrap()).unwrap();
    let source = BinaryDocument::new(
        binary::serialize_object(&[(b"enum", enumeration.root().unwrap())]).unwrap(),
    )
    .unwrap();
    let compiled = Schema::compile(source.root().unwrap(), &LiteralCompiler).unwrap();
    let decoded = Schema::decode(&compiled.encode().unwrap(), &LiteralCompiler).unwrap();
    assert!(decoded.validate(one.root().unwrap()).unwrap().is_valid());

    let date = BinaryDocument::new(vec![JsonType::Date as u8, 1, 0, 0, 0]).unwrap();
    assert!(schema(r#"{"type":"string","minLength":1,"maxLength":1}"#)
        .validate(date.root().unwrap())
        .unwrap()
        .is_valid());
    assert!(!schema(r#"{"type":"number"}"#)
        .validate(date.root().unwrap())
        .unwrap()
        .is_valid());
    for text in ["1e308", "-1e308", "5e-324", "-5e-324"] {
        assert!(check(
            &schema(&format!("{{\"minimum\":{text},\"maximum\":{text}}}")),
            text
        )
        .is_valid());
    }
    assert!(!check(
        &schema(r#"{"minimum":9007199254740993}"#),
        "9007199254740992"
    )
    .is_valid());
}

#[test]
fn normalized_patterns_and_container_literals() {
    let original =
        schema(r#"{"patternProperties":{"a":{}},"additionalProperties":{"enum":[{"x":1},[2]]}}"#);
    let decoded = Schema::decode(&original.encode().unwrap(), &LiteralCompiler).unwrap();
    for text in [r#"{"a":0,"b":{"x":1.0}}"#, r#"{"b":[2]}"#, r#"{"b":0}"#] {
        assert_eq!(check(&original, text), check(&decoded, text));
    }
    assert!(check(&decoded, r#"{"a":0,"b":{"x":1.0}}"#).is_valid());
    assert!(!check(&decoded, r#"{"b":0}"#).is_valid());
    let dependencies = schema(r#"{"dependencies":{"a":{"required":["b"]}}}"#);
    let null = BinaryDocument::new(binary("null")).unwrap();
    assert!(matches!(
        dependencies.validate(null.root().unwrap()),
        Err(Error::Composition(error)) if matches!(*error, Error::ObjectType)
    ));
}

#[test]
fn cache_releases_patterns_on_successful_replacement_and_drop() {
    use std::cell::Cell;
    use std::rc::Rc;
    struct Compiler(Rc<Cell<usize>>);
    struct Compiled(Rc<Cell<usize>>);
    impl Pattern for Compiled {
        fn is_match(&self, _: &[u8]) -> Result<bool> {
            Ok(true)
        }
    }
    impl Drop for Compiled {
        fn drop(&mut self) {
            self.0.set(self.0.get() - 1);
        }
    }
    impl PatternCompiler for Compiler {
        fn compile(&self, _: &[u8]) -> std::result::Result<Box<dyn Pattern>, PatternError> {
            self.0.set(self.0.get() + 1);
            Ok(Box::new(Compiled(self.0.clone())))
        }
    }
    let live = Rc::new(Cell::new(0));
    let compiler = Compiler(live.clone());
    let dependencies = schema(r#"{"dependencies":{"pattern":["a"]}}"#);
    let decoded = Schema::decode(&dependencies.encode().unwrap(), &compiler).unwrap();
    assert_eq!(0, live.get());
    assert!(check(&decoded, r#"{"pattern":0,"a":1}"#).is_valid());
    {
        let mut cache = Cache::default();
        cache
            .get_or_compile(0, br#"{"pattern":"a"}"#, false, &compiler)
            .unwrap();
        assert_eq!(1, live.get());
        cache
            .get_or_compile(0, br#"{"pattern":"a"}"#, false, &compiler)
            .unwrap();
        assert_eq!(1, live.get());
        cache
            .get_or_compile(0, br#"{"pattern":"b"}"#, false, &compiler)
            .unwrap();
        assert_eq!(1, live.get());
        assert!(cache
            .get_or_compile(0, br#"{"pattern":"c","allOf":[false]}"#, false, &compiler)
            .is_err());
        assert_eq!(1, live.get());
    }
    assert_eq!(0, live.get());
}

#[test]
fn compiled_aliases_and_normalized_bytes_preserve_distinct_policies() {
    let compiled = schema(
        r#"{"properties":{"a":{"not":{"minimum":0}}},"patternProperties":{"a":{"not":{"type":"integer"}}}}"#,
    );
    let document = BinaryDocument::new(binary(r#"{"ba":1}"#)).unwrap();
    assert!(matches!(
        compiled.validate(document.root().unwrap()),
        Err(Error::MissingSchema)
    ));
    let decoded = Schema::decode(&compiled.encode().unwrap(), &LiteralCompiler).unwrap();
    assert_eq!(
        check(&decoded, r#"{"ba":1}"#).failure.unwrap().keyword,
        "not"
    );
    assert_eq!(
        check(&compiled, r#"{"a":1}"#),
        check(&decoded, r#"{"a":1}"#)
    );
    let invalid = binary(
        r#"{"properties":{"a":{"required":["b"]}},"patternProperties":{"a":{"properties":{"a":{"not":{"minimum":0}}},"patternProperties":{"a":{"not":{"enum":[0,1,"x"]}}}}}}"#,
    );
    assert!(matches!(
        Schema::compile(
            BinaryDocument::new(invalid).unwrap().root().unwrap(),
            &LiteralCompiler
        ),
        Err(Error::InvalidArgument)
    ));
}

#[test]
fn binary_schema_ingestion_has_no_text_depth_limit() {
    // An ignored binary subtree does not constrain the schema compiler's depth.
    let mut bytes = vec![JsonType::Array as u8, 0x40, 0, 4];
    for _ in 0..600 {
        let mut parent = vec![JsonType::Array as u8, 0x62, 1];
        parent.extend_from_slice(&((12 + bytes.len()) as u32).to_le_bytes());
        parent.extend_from_slice(&12u32.to_le_bytes());
        parent.push(JsonType::Array as u8);
        parent.extend_from_slice(&bytes);
        bytes = parent;
    }
    let mut object = vec![JsonType::Object as u8, 0x62, 1];
    object.extend_from_slice(&((27 + bytes.len()) as u32).to_le_bytes());
    object.extend_from_slice(&20u32.to_le_bytes());
    object.extend_from_slice(&7u32.to_le_bytes());
    object.extend_from_slice(&27u32.to_le_bytes());
    object.push(JsonType::Array as u8);
    object.extend_from_slice(b"ignored");
    object.extend_from_slice(&bytes);
    let bytes = object;
    let mut cache = Cache::default();
    let compiled = cache
        .get_or_compile(0, &bytes, true, &LiteralCompiler)
        .unwrap();
    assert!(check(compiled, "null").is_valid());
    let text = format!("{{\"ignored\":{}null{}}}", "[".repeat(101), "]".repeat(101));
    assert!(matches!(
        cache.get_or_compile(0, text.as_bytes(), false, &LiteralCompiler),
        Err(Error::Parse(_))
    ));
}

#[test]
fn schema_text_keeps_first_duplicate_and_constant_depth_is_explicit() {
    let input = br#"{"type":"object","type":"array"}"#;
    let mut cache = Cache::default();
    let compiled = cache
        .get_or_compile(0, input, false, &LiteralCompiler)
        .unwrap();
    assert!(check(compiled, "{}").is_valid());
    assert!(!check(compiled, "[]").is_valid());
    let constant = Schema::from_input(
        input,
        InputFormat::Text,
        101,
        &LiteralCompiler,
        unsupported_scalar,
    )
    .unwrap();
    assert_eq!(check(compiled, "{}"), check(&constant, "{}"));
    let input = br#"{"ignored":[[[[0]]]]}"#;
    assert!(Schema::from_input(
        input,
        InputFormat::Text,
        6,
        &LiteralCompiler,
        unsupported_scalar
    )
    .is_ok());
    assert!(matches!(
        Schema::from_input(
            input,
            InputFormat::Text,
            4,
            &LiteralCompiler,
            unsupported_scalar
        ),
        Err(Error::Parse(_))
    ));
}

#[test]
fn binary_enum_uses_scalar_capability_and_preserves_platform_error() {
    fn scalar(_: BinaryValue<'_>, _: BinaryValue<'_>) -> Result<std::cmp::Ordering> {
        Ok(std::cmp::Ordering::Equal)
    }
    fn failure(_: BinaryValue<'_>, _: BinaryValue<'_>) -> Result<std::cmp::Ordering> {
        Err(Error::Platform(-12345))
    }
    let date = BinaryDocument::new(vec![JsonType::Time as u8, 0, 0, 0, 0, 0, 0, 0, 0]).unwrap();
    let enumeration =
        BinaryDocument::new(binary::serialize_array(&[date.root().unwrap()]).unwrap()).unwrap();
    let bytes = binary::serialize_object(&[(b"enum", enumeration.root().unwrap())]).unwrap();
    let compiled =
        Schema::from_input(&bytes, InputFormat::Binary, 101, &LiteralCompiler, scalar).unwrap();
    assert!(compiled.validate(date.root().unwrap()).unwrap().is_valid());
    let decoded =
        Schema::decode_with(&compiled.encode().unwrap(), &LiteralCompiler, failure).unwrap();
    assert!(matches!(
        decoded.validate(date.root().unwrap()),
        Err(Error::Platform(-12345))
    ));
    let mut cache = Cache::with_comparator(scalar);
    assert!(cache
        .get_or_compile(0, &bytes, true, &LiteralCompiler)
        .unwrap()
        .validate(date.root().unwrap())
        .unwrap()
        .is_valid());
    let unsupported = BinaryDocument::new(vec![JsonType::OInt as u8, 1]).unwrap();
    assert!(matches!(
        schema(r#"{"enum":[1]}"#).validate(unsupported.root().unwrap()),
        Err(Error::UnsupportedComparison)
    ));
}

#[test]
fn cyclic_schema_binary_returns_an_error_without_expanding_forever() {
    let bytes = vec![6, 0, 1, 9, 8, 1, 0, 6, b'x'];
    let document = BinaryDocument::new(bytes.as_slice()).unwrap();
    assert!(
        matches!(Schema::compile(document.root().unwrap(), &LiteralCompiler),
        Err(Error::Binary(error)) if error.kind == crate::BinaryErrorKind::InvalidDepth)
    );
    assert!(matches!(Schema::decode(&bytes, &LiteralCompiler),
        Err(Error::Binary(error)) if error.kind == crate::BinaryErrorKind::InvalidDepth));
    let shared = BinaryDocument::new(vec![
        6, 0x40, 2, 18, 12, 1, 13, 1, 14, 6, 14, 6, b'a', b'b', 6, 0x40, 0, 4,
    ])
    .unwrap();
    assert!(Schema::compile(shared.root().unwrap(), &LiteralCompiler).is_ok());
}

#[test]
fn composition_error_contexts_format_and_drop_without_recursion() {
    let mut error = Error::ObjectType;
    for _ in 0..12_000 {
        error = error.composition();
    }
    let printed = error.to_string();
    assert_eq!(
        printed,
        "JSON schema: ".to_owned()
            + &"Composition(".repeat(12_000)
            + "ObjectType"
            + &")".repeat(12_000)
    );
    let Error::Composition(context) = &error else {
        panic!("composition context");
    };
    assert!(matches!(**context, Error::ObjectType));
    assert_eq!(context.depth(), 12_000);
    drop(error);
}

#[test]
fn normalized_schema_encoding_matches_frozen_recursive_codec_results() {
    let mut inputs: Vec<String> = [r#"{}"#,r#"{"type":"number","minimum":1,"maximum":5}"#,r#"{"properties":{"a":{"type":"integer"}},"patternProperties":{"x":{"type":"string"}},"additionalProperties":false}"#,r#"{"dependencies":{"x":["a","b"],"a":{"required":["y"]}}}"#,r#"{"allOf":[{"type":"integer"},{"maximum":5}],"anyOf":[{"type":"number"},{"enum":[1,"x"]}],"not":{"minimum":2}}"#].into_iter().map(str::to_owned).collect();
    for depth in [1, 2, 5, 10, 20, 40] {
        for (prefix, suffix) in [
            (r#"{"properties":{"a":"#, "}}"),
            (r#"{"items":"#, "}"),
            (r#"{"allOf":["#, "]}"),
        ] {
            inputs.push(prefix.repeat(depth) + r#"{"type":"integer"}"# + &suffix.repeat(depth));
        }
        let mut definitions = String::new();
        for index in 0..depth {
            if index != 0 {
                definitions.push(',');
            }
            definitions.push_str(&format!(
                r##""a{}":{{"$ref":"#/definitions/a{}"}}"##,
                index,
                index + 1
            ));
        }
        definitions.push_str(&format!(r#", "a{}":{{"type":"integer"}}"#, depth));
        inputs.push(
            r##"{"$ref":"#/definitions/a0","definitions":{"##.to_owned() + &definitions + "}}",
        );
    }
    let goldens = [
        (38, 9447701046009062345u64),
        (68, 10099966283552072114u64),
        (193, 11618353958700205974u64),
        (165, 17029948741649046590u64),
        (225, 4037466127930845398u64),
        (87, 654660249603131185u64),
        (73, 3822352899213247736u64),
        (94, 3271378546319647625u64),
        (198, 1022856080468529417u64),
        (128, 5440177171984239362u64),
        (100, 343760973332607323u64),
        (119, 8759831852023364943u64),
        (277, 13905109702812333427u64),
        (251, 5436199346299972638u64),
        (181, 8991837306767548639u64),
        (194, 12972632416410150472u64),
        (526, 4509870948271335934u64),
        (510, 14351597079266443624u64),
        (333, 10200885769903676691u64),
        (333, 13605084619392344673u64),
        (943, 6144241634136565138u64),
        (1030, 4357972832231203779u64),
        (673, 736879124110592881u64),
        (659, 11182917090801675263u64),
        (1793, 5459910549957608175u64),
        (2070, 15159458455437677677u64),
        (1353, 16329080719559729801u64),
        (1319, 7509618998483260074u64),
        (3493, 11985576543198607585u64),
    ];
    assert_eq!(inputs.len(), goldens.len());
    for (input, (length, expected_hash)) in inputs.iter().zip(goldens) {
        let bytes = crate::parse_binary(
            input.as_bytes(),
            crate::ParseOptions {
                max_depth: 1000,
                ..Default::default()
            },
        )
        .unwrap();
        let document = BinaryDocument::new(bytes).unwrap();
        let schema = Schema::compile(document.root().unwrap(), &LiteralCompiler).unwrap();
        let encoded = schema.encode().unwrap();
        let hash = encoded.iter().fold(14695981039346656037u64, |hash, byte| {
            (hash ^ u64::from(*byte)).wrapping_mul(1099511628211)
        });
        assert_eq!((encoded.len(), hash), (length, expected_hash), "{input}");
    }
}
mod deep_validation {
    use crate::schema::{Pattern, PatternCompiler, PatternError, Schema};
    use crate::{construction::Scalar, writer::BinaryWriter, BinaryDocument};
    struct NoPatterns;
    impl PatternCompiler for NoPatterns {
        fn compile(&self, _: &[u8]) -> Result<Box<dyn Pattern>, PatternError> {
            Err(PatternError::Invalid)
        }
    }
    fn schema_chain(depth: usize, kind: &str) -> Schema {
        let mut writer = BinaryWriter::default();
        let mut frames = Vec::new();
        for _ in 0..depth {
            frames.push(writer.begin(true, 1, 2).unwrap());
            writer.key(kind.as_bytes()).unwrap();
            if kind == "properties" {
                frames.push(writer.begin(true, 1, 2).unwrap());
                writer.key(b"a").unwrap();
            } else if kind != "items" {
                frames.push(writer.begin(false, 1, 2).unwrap());
            }
        }
        let leaf = writer.begin(true, 1, 0).unwrap();
        writer.key(b"type").unwrap();
        writer.string(b"string").unwrap();
        writer.end(leaf).unwrap();
        for frame in frames.into_iter().rev() {
            writer.end(frame).unwrap();
        }
        let binary = BinaryDocument::new(writer.finish(false).unwrap()).unwrap();
        Schema::compile(binary.root().unwrap(), &NoPatterns).unwrap()
    }
    fn document_chain(depth: usize, object: bool, valid: bool) -> BinaryDocument<Vec<u8>> {
        if depth == 0 {
            return BinaryDocument::new(if valid {
                Scalar::String(b"leaf").encode().unwrap()
            } else {
                Scalar::Signed(1).encode().unwrap()
            })
            .unwrap();
        }
        let mut writer = BinaryWriter::default();
        let mut frames = Vec::new();
        for _ in 0..depth {
            frames.push(writer.begin(object, 1, 2).unwrap());
            if object {
                writer.key(b"a").unwrap();
            }
        }
        if valid {
            writer.string(b"leaf").unwrap();
        } else {
            writer.double(1.0).unwrap();
        }
        for frame in frames.into_iter().rev() {
            writer.end(frame).unwrap();
        }
        BinaryDocument::new(writer.finish(false).unwrap()).unwrap()
    }
    #[test]
    fn deeply_nested_properties_and_items_validate_and_report_without_recursion() {
        for kind in ["properties", "items"] {
            let schema = schema_chain(1024, kind);
            let valid = document_chain(1024, kind == "properties", true);
            assert!(schema.validate(valid.root().unwrap()).unwrap().is_valid());
            let invalid = document_chain(1024, kind == "properties", false);
            let failed = schema
                .validate(invalid.root().unwrap())
                .unwrap()
                .failure
                .unwrap();
            assert_eq!(failed.keyword, "type");
            assert_eq!(
                failed.document_location,
                "#".to_owned()
                    + &if kind == "properties" {
                        "/a".repeat(1024)
                    } else {
                        "/0".repeat(1024)
                    }
            );
            let encoded = schema.encode().unwrap();
            let decoded = Schema::decode(&encoded, &NoPatterns).unwrap();
            assert!(decoded.validate(valid.root().unwrap()).unwrap().is_valid());
            assert_eq!(
                decoded.validate(invalid.root().unwrap()).unwrap().failure,
                Some(failed)
            );
        }
    }
    #[test]
    fn deeply_nested_compositions_keep_short_circuit_results() {
        for kind in ["allOf", "anyOf", "oneOf"] {
            let schema = schema_chain(1024, kind);
            for valid in [true, false] {
                let document = document_chain(0, false, valid);
                assert_eq!(
                    schema
                        .validate(document.root().unwrap())
                        .unwrap()
                        .is_valid(),
                    valid
                );
            }
        }
    }
    #[test]
    fn deeply_productive_self_references_use_the_document_position() {
        let bytes = crate::parse_binary(
            br##"{"properties":{"a":{"$ref":"#"}},"required":["a"]}"##,
            Default::default(),
        )
        .unwrap();
        let binary = BinaryDocument::new(bytes).unwrap();
        let schema = Schema::compile(binary.root().unwrap(), &NoPatterns).unwrap();
        let document = document_chain(12_000, true, true);
        assert!(schema
            .validate(document.root().unwrap())
            .unwrap()
            .is_valid());
    }
    #[test]
    fn cyclic_documents_fail_without_blocking_productive_reference_validation() {
        let bytes = crate::parse_binary(br##"{"items":{"$ref":"#"}}"##, Default::default()).unwrap();
        let binary = BinaryDocument::new(bytes).unwrap();
        let schema = Schema::compile(binary.root().unwrap(), &NoPatterns).unwrap();
        let cyclic = BinaryDocument::new(vec![7, 0, 1, 6, 0, 7]).unwrap();
        assert!(schema.validate(cyclic.root().unwrap()).is_err());
    }
    #[test]
    fn long_reference_chains_keep_independent_map_answers() {
        let depth = 1024;
        let names: Vec<_> = (0..depth).map(|index| format!("a{index:06}")).collect();
        let mut writer = BinaryWriter::default();
        let root = writer.begin(true, 2, 2).unwrap();
        writer.key(b"$ref").unwrap();
        writer.key(b"definitions").unwrap();
        writer
            .string(format!("#/definitions/{}", names[0]).as_bytes())
            .unwrap();
        let definitions = writer.begin(true, depth as u64, 2).unwrap();
        for name in &names {
            writer.key(name.as_bytes()).unwrap();
        }
        for index in 0..depth {
            let schema = writer.begin(true, 1, 0).unwrap();
            if index + 1 == depth {
                writer.key(b"type").unwrap();
                writer.string(b"string").unwrap();
            } else {
                writer.key(b"$ref").unwrap();
                writer
                    .string(format!("#/definitions/{}", names[index + 1]).as_bytes())
                    .unwrap();
            }
            writer.end(schema).unwrap();
        }
        writer.end(definitions).unwrap();
        writer.end(root).unwrap();
        let binary = BinaryDocument::new(writer.finish(false).unwrap()).unwrap();
        let schema = Schema::compile(binary.root().unwrap(), &NoPatterns).unwrap();
        for valid in [true, false] {
            let document = document_chain(0, false, valid);
            assert_eq!(
                schema
                    .validate(document.root().unwrap())
                    .unwrap()
                    .is_valid(),
                valid
            );
        }
    }

    #[test]
    fn minimum_value_copy_handles_deep_values_and_rejects_cycles() {
        for object in [false, true] {
            let document = document_chain(12_000, object, true);
            let copied = crate::binary::serialize_value(document.root().unwrap()).unwrap();
            let copied = BinaryDocument::new(copied).unwrap();
            assert_eq!(copied.depth().unwrap(), 12_001);
            let mut leaf = copied.root().unwrap();
            for _ in 0..12_000 {
                leaf = leaf.value(0).unwrap();
            }
            assert_eq!(leaf.string_bytes().unwrap(), b"leaf");
        }
        let cyclic = BinaryDocument::new(vec![7, 0, 1, 6, 0, 7]).unwrap();
        assert!(crate::binary::serialize_value(cyclic.root().unwrap()).is_err());
    }
}
