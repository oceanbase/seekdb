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

use crate::{binary, BinaryDocument, BinaryError, DuplicatePolicy};

#[derive(Debug)]
pub enum BuildError {
    Binary(BinaryError),
    Allocation,
    DuplicateKey,
    Depth,
    InvalidKind,
    MissingKey,
    UnsupportedType,
}

impl From<BinaryError> for BuildError {
    fn from(error: BinaryError) -> Self { Self::Binary(error) }
}

pub enum Scalar<'a> {
    Null,
    Boolean(bool),
    Signed(i64),
    Unsigned(u64),
    Float(f64),
    String(&'a [u8]),
    Opaque { field_type: u16, bytes: &'a [u8] },
    Database { kind: u8, payload: &'a [u8] },
}

impl Scalar<'_> {
    pub fn encode(&self) -> Result<Vec<u8>, BuildError> {
        binary::encode_scalar(self).map_err(BuildError::from)
    }
}

struct Entry {
    key: Vec<u8>,
    value: BinaryDocument<Vec<u8>>,
    depth: u32,
}

pub struct ContainerBuilder {
    object: bool,
    entries: Vec<Entry>,
    approximate_length: u64,
}

pub struct ContainerOutput {
    pub binary: Vec<u8>,
    pub input_count: usize,
    pub output_count: usize,
}

impl ContainerBuilder {
    pub fn array() -> Self { Self { object: false, entries: Vec::new(), approximate_length: 0 } }
    pub fn object() -> Self { Self { object: true, entries: Vec::new(), approximate_length: 0 } }

    pub fn append(&mut self, input: &[u8]) -> Result<(), BuildError> {
        if self.object { return Err(BuildError::InvalidKind); }
        self.push(&[], input)
    }

    pub fn insert(&mut self, key: &[u8], input: &[u8]) -> Result<(), BuildError> {
        if !self.object { return Err(BuildError::InvalidKind); }
        self.push(key, input)
    }

    pub fn append_owned(&mut self, input: Vec<u8>) -> Result<(), BuildError> {
        if self.object { return Err(BuildError::InvalidKind); }
        self.push_owned(&[], input)
    }

    pub fn insert_owned(&mut self, key: &[u8], input: Vec<u8>) -> Result<(), BuildError> {
        if !self.object { return Err(BuildError::InvalidKind); }
        self.push_owned(key, input)
    }

    /// Preserve SQL aggregate errors for extended scalar roots; nested values
    /// retain their own type tags in the serialized container payload.
    pub fn append_aggregate(&mut self, key: &[u8], input: &[u8]) -> Result<(), BuildError> {
        let document = BinaryDocument::new(input)?;
        if matches!(document.root()?.json_type()?,
            crate::JsonType::OBinary | crate::JsonType::OOid | crate::JsonType::ORawHex
            | crate::JsonType::ORawId | crate::JsonType::ODaySecond | crate::JsonType::OYearMonth
            | crate::JsonType::MySqlDatetime) {
            return Err(BuildError::UnsupportedType);
        }
        self.push(if self.object { key } else { &[] }, input)
    }

    pub fn rename_key(&mut self, old: &[u8], new: &[u8]) -> Result<(), BuildError> {
        if !self.object || old.is_empty() || new.is_empty() { return Err(BuildError::InvalidKind); }
        let position = self.entries.iter().position(|entry| entry.key == old).ok_or(BuildError::MissingKey)?;
        if self.entries.iter().any(|entry| entry.key == new) { return Err(BuildError::DuplicateKey); }
        let mut key = Vec::new();
        key.try_reserve_exact(new.len()).map_err(|_| BuildError::Allocation)?;
        key.extend_from_slice(new);
        self.approximate_length = self.approximate_length - self.entries[position].key.len() as u64 + new.len() as u64;
        self.entries[position].key = key;
        Ok(())
    }

    pub fn extend(&mut self, input: &[u8]) -> Result<(), BuildError> {
        let document = BinaryDocument::new(input)?;
        document.depth()?;
        let root = document.root()?;
        let expected = if self.object { crate::JsonType::Object } else { crate::JsonType::Array };
        if root.json_type()? != expected { return Err(BuildError::InvalidKind); }
        let mut staged = if self.object { Self::object() } else { Self::array() };
        for index in 0..root.len()? {
            let key = if self.object { root.key(index)? } else { &[] };
            staged.push(key, &binary::serialize_value(root.value(index)?)?)?;
        }
        let length = self.approximate_length.checked_add(staged.approximate_length).ok_or(BuildError::Allocation)?;
        self.entries.try_reserve(staged.entries.len()).map_err(|_| BuildError::Allocation)?;
        self.entries.append(&mut staged.entries);
        self.approximate_length = length;
        Ok(())
    }

    fn push(&mut self, key: &[u8], input: &[u8]) -> Result<(), BuildError> {
        let source = BinaryDocument::new(input)?;
        let depth = source.depth()?;
        let payload_length = binary::aggregate_payload_length(source.root()?)?;
        let mut bytes = Vec::new();
        bytes.try_reserve_exact(input.len()).map_err(|_| BuildError::Allocation)?;
        bytes.extend_from_slice(input);
        self.push_entry(key, BinaryDocument::new(bytes)?, depth, payload_length)
    }

    fn push_owned(&mut self, key: &[u8], input: Vec<u8>) -> Result<(), BuildError> {
        let value = BinaryDocument::new(input)?;
        let depth = value.depth()?;
        let payload_length = binary::aggregate_payload_length(value.root()?)?;
        self.push_entry(key, value, depth, payload_length)
    }

    fn push_entry(&mut self, key: &[u8], value: BinaryDocument<Vec<u8>>, depth: u32,
                  payload_length: u64) -> Result<(), BuildError> {
        let mut owned_key = Vec::new();
        owned_key.try_reserve_exact(key.len()).map_err(|_| BuildError::Allocation)?;
        self.entries.try_reserve(1).map_err(|_| BuildError::Allocation)?;
        let approximate_length = self.approximate_length.checked_add(key.len() as u64)
            .and_then(|length| length.checked_add(payload_length)).ok_or(BuildError::Allocation)?;
        owned_key.extend_from_slice(key);
        self.entries.push(Entry { key: owned_key, value, depth });
        self.approximate_length = approximate_length;
        Ok(())
    }

    pub fn approximate_length(&self) -> u64 { self.approximate_length }

    pub fn finish(&self, duplicate: DuplicatePolicy, sort_keys: bool, aggregate: bool,
                  max_depth: Option<u32>) -> Result<ContainerOutput, BuildError> {
        let mut entries: Vec<_> = self.entries.iter().collect();
        if self.object && (sort_keys || duplicate != DuplicatePolicy::Preserve) {
            entries.sort_by(|a, b| a.key.len().cmp(&b.key.len()).then_with(|| a.key.cmp(&b.key)));
        }
        if self.object && duplicate != DuplicatePolicy::Preserve {
            let mut unique: Vec<&Entry> = Vec::new();
            for entry in entries {
                if let Some(previous) = unique.last_mut().filter(|previous| previous.key == entry.key) {
                    match duplicate {
                        DuplicatePolicy::Overwrite => *previous = entry,
                        DuplicatePolicy::KeepFirst => (),
                        DuplicatePolicy::Reject => return Err(BuildError::DuplicateKey),
                        DuplicatePolicy::Preserve => unreachable!(),
                    }
                } else { unique.push(entry); }
            }
            entries = unique;
        }
        let depth = entries.iter().map(|entry| entry.depth.saturating_add(1)).max().unwrap_or(1);
        if max_depth.is_some_and(|limit| depth > limit) { return Err(BuildError::Depth); }
        let values = entries.iter().map(|entry| Ok((entry.key.as_slice(), entry.value.root()?)))
            .collect::<Result<Vec<_>, BinaryError>>()?;
        let binary = if aggregate {
            binary::serialize_aggregate(self.object, &values)?
        } else {
            binary::serialize_constructed(self.object, &values)?
        };
        Ok(ContainerOutput { binary, input_count: self.entries.len(), output_count: entries.len() })
    }
}
