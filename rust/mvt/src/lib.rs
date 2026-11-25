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

//! Mapbox vector tile dictionaries and protobuf encoding.

#[cfg(feature = "cxx-bridge")]
mod bridge;

use prost::Message;
use std::{borrow::Cow, collections::HashMap};

/// Generated from the repository's authoritative vector tile schema.
pub mod protocol {
    include!(env!("MVT_PROTO_RS"));
}

/// Floating point values use their wire bits so signed zero and NaN payloads
/// remain distinct dictionary entries.
#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub enum Value {
    String(Vec<u8>),
    Float(u32),
    Double(u64),
    Int(i64),
    Uint(u64),
    Sint(i64),
    Bool(bool),
}

impl Value {
    pub fn float(value: f32) -> Self {
        Self::Float(value.to_bits())
    }
    pub fn double(value: f64) -> Self {
        Self::Double(value.to_bits())
    }

    fn into_protocol(self, legacy_strings: bool) -> protocol::tile::Value {
        use protocol::tile::value::TestOneof;
        let value = match self {
            Self::String(mut v) => {
                if legacy_strings {
                    v.truncate(c_string(&v).len());
                }
                TestOneof::StringValue(v)
            }
            Self::Float(v) => TestOneof::FloatValue(f32::from_bits(v)),
            Self::Double(v) => TestOneof::DoubleValue(f64::from_bits(v)),
            Self::Int(v) => TestOneof::IntValue(v),
            Self::Uint(v) => TestOneof::UintValue(v),
            Self::Sint(v) => TestOneof::SintValue(v),
            Self::Bool(v) => TestOneof::BoolValue(v),
        };
        protocol::tile::Value {
            test_oneof: Some(value),
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct FeatureInput {
    pub id: Option<u64>,
    pub kind: u32,
    pub geometry: Vec<u32>,
    /// Key dictionary indices and values in property order.
    pub properties: Vec<(u32, Value)>,
}

pub struct LayerBuilder {
    tile: protocol::Tile,
    keys: HashMap<Vec<u8>, u32>,
    values: HashMap<Value, u32>,
    pending: Option<FeatureInput>,
    legacy_strings: bool,
    error: Option<BuilderError>,
}

impl Default for LayerBuilder {
    fn default() -> Self {
        Self {
            tile: protocol::Tile {
                layers: vec![protocol::tile::Layer {
                    version: 2,
                    name: b"default".to_vec(),
                    extent: 4096,
                    features: Vec::new(),
                    keys: Vec::new(),
                    values: Vec::new(),
                }],
            },
            keys: HashMap::new(),
            values: HashMap::new(),
            pending: None,
            legacy_strings: false,
            error: None,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BuilderError {
    Allocation,
    InvalidArgument,
    SizeOverflow,
}

impl BuilderError {
    pub fn code(self) -> i32 {
        match self {
            Self::Allocation => -4013,
            Self::InvalidArgument => -4002,
            Self::SizeOverflow => -4019,
        }
    }
}

impl std::fmt::Display for BuilderError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(match self {
            Self::Allocation => "MVT allocation failed",
            Self::InvalidArgument => "invalid MVT builder argument or operation",
            Self::SizeOverflow => "MVT size overflow",
        })
    }
}

impl std::error::Error for BuilderError {}

type Result<T> = std::result::Result<T, BuilderError>;

fn reserve<T>(buffer: &mut Vec<T>, additional: usize) -> Result<()> {
    buffer
        .try_reserve(additional)
        .map_err(|_| BuilderError::Allocation)
}

fn copy_slice<T: Copy>(input: &[T]) -> Result<Vec<T>> {
    let mut output = Vec::new();
    reserve(&mut output, input.len())?;
    output.extend_from_slice(input);
    Ok(output)
}

fn folded(key: &[u8], legacy: bool) -> Result<Cow<'_, [u8]>> {
    if legacy && key.contains(&0) {
        // ObString::case_compare stops at NUL but also compares full lengths.
        let prefix = c_string(key);
        let capacity = prefix
            .len()
            .checked_add(1 + size_of::<usize>())
            .ok_or(BuilderError::SizeOverflow)?;
        let mut folded = Vec::new();
        reserve(&mut folded, capacity)?;
        folded.extend(prefix.iter().map(u8::to_ascii_lowercase));
        folded.push(0);
        folded.extend_from_slice(&key.len().to_le_bytes());
        Ok(Cow::Owned(folded))
    } else if key.iter().any(u8::is_ascii_uppercase) {
        let mut folded = copy_slice(key)?;
        folded.make_ascii_lowercase();
        Ok(Cow::Owned(folded))
    } else {
        Ok(Cow::Borrowed(key))
    }
}

fn c_string(bytes: &[u8]) -> &[u8] {
    &bytes[..bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len())]
}

impl LayerBuilder {
    pub fn new(name: impl Into<Vec<u8>>, extent: u32) -> Self {
        let mut builder = Self::default();
        builder.tile.layers[0].name = name.into();
        builder.tile.layers[0].extent = extent;
        builder
    }

    pub fn error_code(&self) -> i32 {
        self.error.map_or(0, BuilderError::code)
    }

    fn status(&self) -> Result<()> {
        self.error.map_or(Ok(()), Err)
    }

    fn record<T>(&mut self, result: Result<T>) -> Result<T> {
        if let Err(error) = result {
            self.error.get_or_insert(error);
        }
        result
    }

    fn append_key(&mut self, lookup: &[u8], spelling: &[u8]) -> Result<u32> {
        self.status()?;
        let result = (|| {
            let index = u32::try_from(self.tile.layers[0].keys.len())
                .map_err(|_| BuilderError::SizeOverflow)?;
            let lookup = match folded(lookup, self.legacy_strings)? {
                Cow::Borrowed(bytes) => copy_slice(bytes)?,
                Cow::Owned(bytes) => bytes,
            };
            let spelling = copy_slice(spelling)?;
            reserve(&mut self.tile.layers[0].keys, 1)?;
            self.keys
                .try_reserve(1)
                .map_err(|_| BuilderError::Allocation)?;
            self.tile.layers[0].keys.push(spelling);
            self.keys.entry(lookup).or_insert(index);
            Ok(index)
        })();
        self.record(result)
    }

    /// Always append a seeded key, retaining duplicates and their order.
    pub fn seed_key_bytes(&mut self, key: &[u8]) -> Result<u32> {
        self.append_key(key, key)
    }

    pub fn intern_key_bytes(&mut self, key: &[u8]) -> Result<u32> {
        self.intern_spelling(key, key)
    }

    fn intern_spelling(&mut self, key: &[u8], spelling: &[u8]) -> Result<u32> {
        self.status()?;
        let result =
            folded(key, self.legacy_strings).map(|key| self.keys.get(key.as_ref()).copied());
        match self.record(result)? {
            Some(index) => Ok(index),
            None => self.append_key(key, spelling),
        }
    }

    pub fn push_feature(&mut self, feature: FeatureInput) -> Result<()> {
        self.status()?;
        let result = (|| {
            if feature
                .properties
                .iter()
                .any(|(key, _)| (*key as usize) >= self.tile.layers[0].keys.len())
            {
                return Err(BuilderError::InvalidArgument);
            }
            let mut tags = Vec::new();
            reserve(
                &mut tags,
                feature
                    .properties
                    .len()
                    .checked_mul(2)
                    .ok_or(BuilderError::SizeOverflow)?,
            )?;
            reserve(&mut self.tile.layers[0].features, 1)?;
            for (key, value) in feature.properties {
                let index = match self.values.get(&value) {
                    Some(&index) => index,
                    None => {
                        let index = u32::try_from(self.tile.layers[0].values.len())
                            .map_err(|_| BuilderError::SizeOverflow)?;
                        let wire_value = match &value {
                            Value::String(bytes) => Value::String(copy_slice(bytes)?),
                            other => other.clone(),
                        }
                        .into_protocol(self.legacy_strings);
                        reserve(&mut self.tile.layers[0].values, 1)?;
                        self.values
                            .try_reserve(1)
                            .map_err(|_| BuilderError::Allocation)?;
                        self.tile.layers[0].values.push(wire_value);
                        self.values.insert(value, index);
                        index
                    }
                };
                tags.extend_from_slice(&[key, index]);
            }
            self.tile.layers[0].features.push(protocol::tile::Feature {
                id: feature.id,
                r#type: feature.kind as i32,
                tags,
                geometry: feature.geometry,
            });
            Ok(())
        })();
        self.record(result)
    }

    pub fn encode(&self) -> Vec<u8> {
        self.tile.encode_to_vec()
    }

    pub fn encoded_len(&self) -> usize {
        self.tile.encoded_len()
    }

    /// Encode into caller-owned storage without a temporary result allocation.
    pub fn encode_to(&self, mut output: &mut [u8]) -> bool {
        if self.error.is_some() {
            return false;
        }
        self.tile.encode(&mut output).is_ok()
    }

    /// Allocate and encode a tile, reporting allocation failure.
    pub fn try_encode(&self) -> Result<Vec<u8>> {
        self.status()?;
        let mut output = Vec::new();
        reserve(&mut output, self.encoded_len())?;
        self.tile
            .encode(&mut output)
            .map_err(|_| BuilderError::SizeOverflow)?;
        Ok(output)
    }

    /// Append an encoded tile to an existing buffer.
    pub fn encode_into(&self, output: &mut Vec<u8>) {
        self.tile
            .encode(output)
            .expect("encoding into Vec cannot fail");
    }

    pub fn configure(&mut self, name: &[u8], extent: u32) {
        if self.error.is_some() {
            return;
        }
        let result = copy_slice(c_string(name));
        if let Ok(name) = self.record(result) {
            self.legacy_strings = true;
            self.tile.layers[0].name = name;
            self.tile.layers[0].extent = extent;
        }
    }

    pub fn seed_key(&mut self, key: &[u8]) {
        let _ = self.append_key(key, c_string(key));
    }

    pub fn find_key(&mut self, key: &[u8]) -> u32 {
        if self.error.is_some() {
            return u32::MAX;
        }
        let result = folded(key, self.legacy_strings)
            .map(|key| self.keys.get(key.as_ref()).copied().unwrap_or(u32::MAX));
        self.record(result).unwrap_or(u32::MAX)
    }

    pub fn intern_key(&mut self, key: &[u8]) -> u32 {
        self.intern_spelling(key, c_string(key)).unwrap_or(u32::MAX)
    }

    pub fn begin_feature(&mut self, kind: u32, geometry: &[u32]) {
        if self.error.is_some() {
            return;
        }
        if self.pending.is_some() {
            self.error = Some(BuilderError::InvalidArgument);
            return;
        }
        let result = copy_slice(geometry);
        if let Ok(geometry) = self.record(result) {
            self.pending = Some(FeatureInput {
                kind,
                geometry,
                ..FeatureInput::default()
            });
        }
    }

    pub fn set_id(&mut self, id: u64) {
        if self.error.is_some() {
            return;
        }
        match self.pending.as_mut() {
            Some(feature) => feature.id = Some(id),
            None => self.error = Some(BuilderError::InvalidArgument),
        }
    }

    fn add_value(&mut self, key: u32, value: Value) {
        if self.error.is_some() {
            return;
        }
        let result = (|| {
            if key as usize >= self.tile.layers[0].keys.len() {
                return Err(BuilderError::InvalidArgument);
            }
            let feature = self.pending.as_mut().ok_or(BuilderError::InvalidArgument)?;
            reserve(&mut feature.properties, 1)?;
            feature.properties.push((key, value));
            Ok(())
        })();
        let _ = self.record(result);
    }

    pub fn add_string(&mut self, key: u32, value: &[u8]) {
        if self.error.is_some() {
            return;
        }
        let result = copy_slice(value);
        if let Ok(value) = self.record(result) {
            self.add_value(key, Value::String(value));
        }
    }
    pub fn add_float(&mut self, key: u32, value: f32) {
        self.add_value(key, Value::float(value));
    }
    pub fn add_double(&mut self, key: u32, value: f64) {
        self.add_value(key, Value::double(value));
    }
    pub fn add_int(&mut self, key: u32, value: i64) {
        self.add_value(key, Value::Int(value));
    }
    pub fn add_uint(&mut self, key: u32, value: u64) {
        self.add_value(key, Value::Uint(value));
    }
    pub fn add_sint(&mut self, key: u32, value: i64) {
        self.add_value(key, Value::Sint(value));
    }
    pub fn add_bool(&mut self, key: u32, value: bool) {
        self.add_value(key, Value::Bool(value));
    }
    pub fn commit_feature(&mut self) {
        if self.error.is_some() {
            return;
        }
        match self.pending.take() {
            Some(feature) => {
                let _ = self.push_feature(feature);
            }
            None => self.error = Some(BuilderError::InvalidArgument),
        }
    }
}

#[cfg(any(feature = "cxx-bridge", test))]
fn new_builder() -> Box<LayerBuilder> {
    Box::new(LayerBuilder {
        legacy_strings: true,
        ..LayerBuilder::default()
    })
}
