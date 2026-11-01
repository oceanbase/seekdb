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

fn decode(builder: &LayerBuilder) -> protocol::tile::Layer {
    protocol::Tile::decode(builder.encode().as_slice())
        .unwrap()
        .layers
        .remove(0)
}

#[test]
fn empty_layer_emits_required_fields() {
    let builder = LayerBuilder::default();
    let layer = decode(&builder);
    assert_eq!(
        (layer.version, layer.name.as_slice(), layer.extent),
        (2, b"default".as_slice(), 4096)
    );
    assert_eq!(
        LayerBuilder::new(Vec::new(), 0).encode(),
        [0x1a, 6, 0x0a, 0, 0x28, 0, 0x78, 2]
    );
    let mut output = vec![42];
    builder.encode_into(&mut output);
    assert_eq!(&output[1..], builder.encode());
}

#[test]
fn dictionaries_keep_seed_order_and_first_case_insensitive_match() {
    let mut builder = LayerBuilder::default();
    assert_eq!(builder.seed_key_bytes(b"Name").unwrap(), 0);
    assert_eq!(builder.seed_key_bytes(b"NAME").unwrap(), 1);
    assert_eq!(builder.intern_key_bytes(b"name").unwrap(), 0);
    assert_eq!(builder.find_key(b"nAmE"), 0);
    assert_eq!(builder.find_key(b"missing"), u32::MAX);
    // A JSON member whose value is ignored still contributes its key.
    assert_eq!(builder.intern_key_bytes(b"ignored").unwrap(), 2);
    assert_eq!(builder.intern_key_bytes(b"visible").unwrap(), 3);
    builder
        .push_feature(FeatureInput {
            properties: vec![
                (3, Value::Int(7)),
                (0, Value::Bool(false)),
                (3, Value::Int(7)),
            ],
            ..FeatureInput::default()
        })
        .unwrap();
    let layer = decode(&builder);
    assert_eq!(
        layer.keys,
        [
            b"Name".to_vec(),
            b"NAME".to_vec(),
            b"ignored".to_vec(),
            b"visible".to_vec()
        ]
    );
    assert_eq!(layer.features[0].tags, [3, 0, 0, 1, 3, 0]);
    assert_eq!(layer.values.len(), 2);
}

#[test]
fn floating_point_dictionary_uses_bits() {
    let values = vec![
        Value::float(0.0),
        Value::float(-0.0),
        Value::Float(0x7fc00001),
        Value::Float(0x7fc00002),
        Value::double(0.0),
        Value::double(-0.0),
        Value::Double(0x7ff8000000000001),
        Value::Double(0x7ff8000000000002),
    ];
    let mut builder = LayerBuilder::default();
    let key = builder.intern_key_bytes(b"x").unwrap();
    builder
        .push_feature(FeatureInput {
            properties: values
                .iter()
                .cloned()
                .chain(values.iter().cloned())
                .map(|v| (key, v))
                .collect(),
            ..FeatureInput::default()
        })
        .unwrap();
    let layer = decode(&builder);
    assert_eq!(layer.values.len(), values.len());
    for (encoded, expected) in layer.values.into_iter().zip(values) {
        use protocol::tile::value::TestOneof;
        match (encoded.test_oneof.unwrap(), expected) {
            (TestOneof::FloatValue(value), Value::Float(bits)) => assert_eq!(value.to_bits(), bits),
            (TestOneof::DoubleValue(value), Value::Double(bits)) => {
                assert_eq!(value.to_bits(), bits)
            }
            pair => panic!("unexpected value {pair:?}"),
        }
    }
}

#[test]
fn all_variants_preserve_zero_and_integer_boundaries() {
    let values = vec![
        Value::String(Vec::new()),
        Value::float(0.0),
        Value::double(0.0),
        Value::Int(0),
        Value::Uint(0),
        Value::Sint(0),
        Value::Bool(false),
        Value::Int(i64::MIN),
        Value::Int(i64::MAX),
        Value::Uint(u64::MAX),
        Value::Sint(i64::MIN),
        Value::Sint(i64::MAX),
    ];
    let mut builder = LayerBuilder::default();
    let key = builder.intern_key_bytes(b"x").unwrap();
    builder
        .push_feature(FeatureInput {
            properties: values.iter().cloned().map(|v| (key, v)).collect(),
            ..FeatureInput::default()
        })
        .unwrap();
    let layer = decode(&builder);
    assert_eq!(layer.values.len(), values.len());
    for (actual, expected) in layer.values.into_iter().zip(values) {
        assert_eq!(actual, expected.into_protocol(false));
        assert!(actual.test_oneof.is_some());
    }
}

#[test]
fn absent_and_zero_id_are_distinct() {
    let mut builder = LayerBuilder::default();
    builder.push_feature(FeatureInput::default()).unwrap();
    builder
        .push_feature(FeatureInput {
            id: Some(0),
            ..FeatureInput::default()
        })
        .unwrap();
    let layer = decode(&builder);
    assert_eq!(layer.features[0].id, None);
    assert_eq!(layer.features[1].id, Some(0));
    assert_eq!(layer.features[0].encode_to_vec(), [0x18, 0]);
    assert_eq!(layer.features[1].encode_to_vec(), [0x08, 0, 0x18, 0]);
}

#[test]
fn bridge_preserves_c_string_lookup_and_full_length() {
    let mut builder = LayerBuilder::default();
    builder.configure(b"layer\0suffix", 4096);
    builder.seed_key(b"key\0one");
    builder.seed_key(b"KEY\0one");
    assert_eq!(builder.intern_key(b"Key\0one"), 0);
    assert_eq!(builder.intern_key(b"key\0two"), 0);
    assert_eq!(builder.intern_key(b"key"), 2);
    builder.begin_feature(1, &[9, 2, 4]);
    builder.set_id(0);
    builder.add_string(0, b"value\0tail");
    builder.commit_feature();
    let layer = decode(&builder);
    assert_eq!(layer.name, b"layer");
    assert_eq!(
        layer.keys,
        [b"key".to_vec(), b"KEY".to_vec(), b"key".to_vec()]
    );
    assert_eq!(
        layer.values[0],
        Value::String(b"value".to_vec()).into_protocol(false)
    );
    assert_eq!(layer.features[0].geometry, [9, 2, 4]);
}

#[test]
fn native_bytes_keep_nul_and_non_utf8() {
    let mut builder = LayerBuilder::new(b"n\0\xff".to_vec(), 4096);
    let key = builder.intern_key_bytes(b"k\0\xff").unwrap();
    builder
        .push_feature(FeatureInput {
            properties: vec![(key, Value::String(b"v\0\xff".to_vec()))],
            ..FeatureInput::default()
        })
        .unwrap();
    let layer = decode(&builder);
    assert_eq!(layer.name, b"n\0\xff");
    assert_eq!(layer.keys[0], b"k\0\xff");
    assert_eq!(
        layer.values[0],
        Value::String(b"v\0\xff".to_vec()).into_protocol(false)
    );
}

#[test]
fn standard_point_fixture_preserves_command_order() {
    // Standard MVT wire fields: layer name "p", point MoveTo(1), x=1, y=2,
    // extent 4096, version 2. Encoded independently of the generated types.
    let fixture = [
        0x1a, 17, 0x0a, 1, b'p', 0x12, 7, 0x18, 1, 0x22, 3, 9, 2, 4, 0x28, 0x80, 0x20, 0x78, 2,
    ];
    let tile = protocol::Tile::decode(fixture.as_slice()).unwrap();
    assert_eq!(tile.layers[0].features[0].geometry, [9, 2, 4]);
    assert_eq!(tile.layers[0].features[0].r#type, 1);
    let mut builder = LayerBuilder::new(b"p".to_vec(), 4096);
    builder
        .push_feature(FeatureInput {
            kind: 1,
            geometry: vec![9, 2, 4],
            ..FeatureInput::default()
        })
        .unwrap();
    assert_eq!(builder.encode(), fixture);
}

#[test]
fn bridge_string_dictionary_keeps_full_value_identity() {
    let mut builder = new_builder();
    builder.seed_key(b"k");
    builder.begin_feature(1, &[9, 0, 0]);
    builder.add_string(0, b"a\0x");
    builder.add_string(0, b"a\0y");
    builder.add_string(0, b"a\0x");
    builder.commit_feature();
    let layer = decode(&builder);
    assert_eq!(layer.values.len(), 2);
    assert_eq!(layer.values[0], layer.values[1]);
    assert_eq!(layer.features[0].tags, [0, 0, 0, 1, 0, 0]);
}

#[test]
fn caller_owned_output_matches_allocating_encoder() {
    let mut builder = LayerBuilder::new(b"slice".to_vec(), 8192);
    builder
        .push_feature(FeatureInput {
            id: Some(0),
            kind: 1,
            geometry: vec![9, 2, 4],
            ..FeatureInput::default()
        })
        .unwrap();
    let mut output = vec![0; builder.encoded_len()];
    assert!(builder.encode_to(&mut output));
    assert_eq!(output, builder.encode());
    let mut short = vec![42; builder.encoded_len() - 1];
    assert!(!builder.encode_to(&mut short));
    assert!(short.iter().all(|&byte| byte == 42));
}

#[test]
fn allocation_failure_is_reported_without_allocating() {
    let mut buffer = Vec::<u8>::new();
    let error = reserve(&mut buffer, usize::MAX).unwrap_err();
    assert_eq!(error, BuilderError::Allocation);
    let mut builder = LayerBuilder::default();
    assert_eq!(builder.record::<()>(Err(error)), Err(error));
    assert_eq!(builder.error_code(), -4013);
    builder.seed_key(b"ignored");
    assert!(builder.tile.layers[0].keys.is_empty());
    assert_eq!(builder.intern_key_bytes(b"ignored"), Err(error));
    assert_eq!(builder.try_encode(), Err(error));
}

#[test]
fn invalid_native_key_sets_sticky_bridge_status() {
    let mut builder = LayerBuilder::default();
    let result = builder.push_feature(FeatureInput {
        properties: vec![(0, Value::Int(1))],
        ..FeatureInput::default()
    });
    assert_eq!(result, Err(BuilderError::InvalidArgument));
    assert_eq!(builder.error_code(), -4002);
    let before = builder.encode();
    builder.configure(b"ignored", 7);
    builder.seed_key(b"ignored");
    builder.begin_feature(1, &[9, 0, 0]);
    builder.set_id(1);
    builder.add_string(0, b"ignored");
    builder.commit_feature();
    assert_eq!(builder.find_key(b"ignored"), u32::MAX);
    assert_eq!(builder.intern_key(b"ignored"), u32::MAX);
    assert_eq!(builder.error_code(), -4002);
    assert_eq!(builder.encode(), before);
}

#[test]
fn bridge_copies_borrowed_inputs_before_returning() {
    let mut builder = new_builder();
    let mut name = b"layer".to_vec();
    let mut key = b"Key".to_vec();
    let mut geometry = vec![9, 2, 4];
    let mut value = b"value".to_vec();
    builder.configure(&name, 4096);
    builder.seed_key(&key);
    builder.begin_feature(1, &geometry);
    builder.add_string(0, &value);
    name.fill(0);
    key.fill(0);
    geometry.fill(0);
    value.fill(0);
    builder.commit_feature();
    assert_eq!(builder.error_code(), 0);
    let layer = decode(&builder);
    assert_eq!(layer.name, b"layer");
    assert_eq!(layer.keys[0], b"Key");
    assert_eq!(layer.features[0].geometry, [9, 2, 4]);
    assert_eq!(
        layer.values[0],
        Value::String(b"value".to_vec()).into_protocol(false)
    );
    assert_eq!(builder.try_encode().unwrap(), builder.encode());
}
