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

use crate::binary::{self, ConstructedScalar, ConstructedSource, ValueLocation};
use crate::{
    BinaryDocument, BinaryError, BinaryErrorKind, BinaryResult, BinaryValue, Document, JsonType,
    Number, ParseOptions, Path, QueryOptions, QueryOutput, QueryOutputOptions,
};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

enum Storage {
    Binary(BinaryDocument<Vec<u8>>),
    Parsed(Document),
}
struct ValueDocument {
    storage: Storage,
    next_identity: AtomicU64,
}
impl Drop for ValueDocument {
    fn drop(&mut self) {
        if let Storage::Parsed(document) = &mut self.storage {
            let document = std::mem::replace(document, Document::Null);
            let mut pending = match document {
                Document::Array(values) => values,
                Document::Object(values) => values.into_iter().map(|(_, value)| value).collect(),
                _ => return,
            };
            while let Some(value) = pending.pop() {
                match value {
                    Document::Array(values) => pending.extend(values),
                    Document::Object(values) => {
                        pending.extend(values.into_iter().map(|(_, value)| value))
                    }
                    _ => {}
                }
            }
        }
    }
}
#[derive(Clone)]
enum Location {
    Binary(ValueLocation),
    Parsed(Vec<u64>),
}

/// A selected value keeps its immutable document alive independently of a scan.
#[derive(Clone)]
pub struct Value {
    document: Arc<ValueDocument>,
    location: Location,
    identity: u64,
}
pub struct Selection {
    values: Vec<Value>,
}
#[derive(Clone, Copy)]
enum ReadStorage<'a> {
    Binary(BinaryValue<'a>),
    Parsed(&'a Document),
}

/// A borrowed immutable JSON value, independent of its backing representation.
#[derive(Clone, Copy)]
pub struct ValueRef<'a>(ReadStorage<'a>);
impl<'a> ValueRef<'a> {
    pub(crate) fn binary(value: BinaryValue<'a>) -> Self {
        Self(ReadStorage::Binary(value))
    }
    pub(crate) fn parsed(value: &'a Document) -> Self {
        Self(ReadStorage::Parsed(value))
    }
    pub(crate) fn as_binary(self) -> Option<BinaryValue<'a>> {
        match self.0 {
            ReadStorage::Binary(value) => Some(value),
            _ => None,
        }
    }
    pub fn json_type(self) -> BinaryResult<JsonType> {
        match self.0 {
            ReadStorage::Binary(value) => value.json_type(),
            ReadStorage::Parsed(value) => ConstructedScalar::Document(value).kind(),
        }
    }
    pub fn number(self) -> BinaryResult<Number> {
        match self.0 {
            ReadStorage::Binary(value) => value.number(),
            ReadStorage::Parsed(Document::Number(value)) => Ok(value.clone()),
            _ => Err(self.error(BinaryErrorKind::InvalidType)),
        }
    }
    pub fn string_bytes(self) -> BinaryResult<&'a [u8]> {
        match self.0 {
            ReadStorage::Binary(value) => value.string_bytes(),
            ReadStorage::Parsed(Document::String(value)) => Ok(value.as_bytes()),
            _ => Err(self.error(BinaryErrorKind::InvalidType)),
        }
    }
    pub fn len(self) -> BinaryResult<u64> {
        match self.0 {
            ReadStorage::Binary(value) => value.len(),
            ReadStorage::Parsed(Document::Array(value)) => Ok(value.len() as u64),
            ReadStorage::Parsed(Document::Object(value)) => Ok(value.len() as u64),
            _ => Ok(1),
        }
    }
    pub fn is_empty(self) -> BinaryResult<bool> {
        self.len().map(|length| length == 0)
    }
    pub fn value(self, index: u64) -> BinaryResult<Self> {
        match self.0 {
            ReadStorage::Binary(value) => value.value(index).map(Self::binary),
            ReadStorage::Parsed(Document::Array(values)) => values
                .get(self.index(index)?)
                .map(Self::parsed)
                .ok_or_else(|| self.error(BinaryErrorKind::InvalidOffset)),
            ReadStorage::Parsed(Document::Object(values)) => values
                .get(self.index(index)?)
                .map(|(_, value)| Self::parsed(value))
                .ok_or_else(|| self.error(BinaryErrorKind::InvalidOffset)),
            _ => Err(self.error(BinaryErrorKind::InvalidType)),
        }
    }
    pub fn key(self, index: u64) -> BinaryResult<&'a [u8]> {
        match self.0 {
            ReadStorage::Binary(value) => value.key(index),
            ReadStorage::Parsed(Document::Object(values)) => values
                .get(self.index(index)?)
                .map(|(key, _)| key.as_bytes())
                .ok_or_else(|| self.error(BinaryErrorKind::InvalidOffset)),
            ReadStorage::Parsed(Document::Array(_)) => {
                Err(self.error(BinaryErrorKind::InvalidOffset))
            }
            _ => Err(self.error(BinaryErrorKind::InvalidType)),
        }
    }
    pub fn member(self, key: &[u8]) -> BinaryResult<Option<Self>> {
        Ok(self.member_index(key)?.map(|(_, value)| value))
    }
    pub(crate) fn member_index(self, key: &[u8]) -> BinaryResult<Option<(u64, Self)>> {
        match self.0 {
            ReadStorage::Binary(value) => Ok(value
                .member_index(key)?
                .map(|(index, value)| (index, Self::binary(value)))),
            ReadStorage::Parsed(Document::Object(values)) => Ok(values
                .iter()
                .enumerate()
                .find(|(_, (name, _))| name.as_bytes() == key)
                .map(|(index, (_, value))| (index as u64, Self::parsed(value)))),
            ReadStorage::Parsed(Document::Array(values)) if values.is_empty() => Ok(None),
            ReadStorage::Parsed(Document::Array(_)) => {
                Err(self.error(BinaryErrorKind::InvalidOffset))
            }
            _ => Err(self.error(BinaryErrorKind::InvalidType)),
        }
    }
    fn index(self, index: u64) -> BinaryResult<usize> {
        usize::try_from(index).map_err(|_| self.error(BinaryErrorKind::InvalidOffset))
    }
    pub(crate) fn identity(self) -> (usize, usize) {
        match self.0 {
            ReadStorage::Binary(value) => (0, value.storage_offset()),
            ReadStorage::Parsed(value) => (1, value as *const Document as usize),
        }
    }
    pub(crate) fn error_offset(self) -> usize {
        match self.0 {
            ReadStorage::Binary(value) => value.storage_offset(),
            _ => 0,
        }
    }
    fn error(self, kind: BinaryErrorKind) -> BinaryError {
        BinaryError::new(kind, self.error_offset())
    }
    fn scalar(self) -> ConstructedScalar<'a> {
        match self.0 {
            ReadStorage::Binary(value) => ConstructedScalar::Binary(value),
            ReadStorage::Parsed(value) => ConstructedScalar::Document(value),
        }
    }
    fn lexicographical(self) -> bool {
        match self.0 {
            ReadStorage::Binary(value) => value.lexicographical(),
            _ => false,
        }
    }
    fn entries(self, lexicographical: bool) -> BinaryResult<Vec<(&'a [u8], Self)>> {
        match self.0 {
            ReadStorage::Binary(value) => value
                .entries_in_order(lexicographical)?
                .map(|entry| entry.map(|(key, value)| (key, Self::binary(value))))
                .collect(),
            ReadStorage::Parsed(Document::Array(values)) => Ok(values
                .iter()
                .map(|value| (&[][..], Self::parsed(value)))
                .collect()),
            ReadStorage::Parsed(Document::Object(values)) => Ok(values
                .iter()
                .map(|(key, value)| (key.as_bytes(), Self::parsed(value)))
                .collect()),
            _ => Err(self.error(BinaryErrorKind::InvalidType)),
        }
    }
    /// Encodes this value with the shared binary codec. Binary inputs retain their
    /// root key order; parsed text uses stable length-first object key order.
    pub fn to_binary(self) -> BinaryResult<Vec<u8>> {
        render(&[self], false)
    }
}

#[derive(Clone, Copy)]
enum OutputNode<'a> {
    Root,
    Value(ValueRef<'a>),
}
struct OutputSource<'a> {
    values: &'a [ValueRef<'a>],
    lexicographical: bool,
}
impl<'a> ConstructedSource for OutputSource<'a> {
    type Value = OutputNode<'a>;
    fn lexicographical(&self) -> bool {
        self.lexicographical
    }
    fn identity(&self, value: Self::Value) -> (usize, usize) {
        match value {
            OutputNode::Root => (usize::MAX, 0),
            OutputNode::Value(value) => value.identity(),
        }
    }
    fn traversal_limit(&self) -> usize {
        self.values
            .iter()
            .fold(1usize, |limit, value| match value.0 {
                ReadStorage::Binary(value) => limit.saturating_add(value.traversal_limit()),
                ReadStorage::Parsed(_) => usize::MAX,
            })
    }
    fn kind(&self, value: Self::Value) -> BinaryResult<JsonType> {
        match value {
            OutputNode::Root => Ok(JsonType::Array),
            OutputNode::Value(value) => value.json_type(),
        }
    }
    fn scalar(&self, value: Self::Value) -> BinaryResult<ConstructedScalar<'_>> {
        match value {
            OutputNode::Value(value) => Ok(value.scalar()),
            OutputNode::Root => Err(BinaryError::new(BinaryErrorKind::InvalidType, 0)),
        }
    }
    fn entries(&self, value: Self::Value) -> BinaryResult<Vec<(&[u8], Self::Value)>> {
        match value {
            OutputNode::Root => Ok(self
                .values
                .iter()
                .map(|value| (&[][..], OutputNode::Value(*value)))
                .collect()),
            OutputNode::Value(value) => Ok(value
                .entries(self.lexicographical)?
                .into_iter()
                .map(|(key, value)| (key, OutputNode::Value(value)))
                .collect()),
        }
    }
}
pub(crate) fn render(values: &[ValueRef<'_>], wrapper: bool) -> BinaryResult<Vec<u8>> {
    if !wrapper && values.len() == 1 {
        if let Some(value) = values[0].as_binary() {
            return binary::serialize_constructed_value(value);
        }
        if !values[0].json_type()?.is_container() {
            return values[0].scalar().serialize_root();
        }
    }
    let source = OutputSource {
        values,
        lexicographical: !wrapper && values[0].lexicographical(),
    };
    binary::serialize_constructed_source(
        &source,
        if wrapper {
            OutputNode::Root
        } else {
            OutputNode::Value(values[0])
        },
    )
}

impl Value {
    pub(crate) fn root(document: BinaryDocument<Vec<u8>>) -> BinaryResult<Self> {
        let location = Location::Binary(document.root()?.location());
        Ok(Self {
            document: Arc::new(ValueDocument {
                storage: Storage::Binary(document),
                next_identity: AtomicU64::new(2),
            }),
            location,
            identity: 1,
        })
    }
    /// Parses and freezes text without serializing a second whole-document image.
    /// Objects use the same stable length-first key order as binary JSON input.
    pub fn from_text(input: &[u8], options: ParseOptions) -> crate::Result<Self> {
        Ok(Self::from_document(crate::parse(input, options)?))
    }
    pub(crate) fn from_document(mut document: Document) -> Self {
        binary::sort_objects(&mut document);
        Self {
            document: Arc::new(ValueDocument {
                storage: Storage::Parsed(document),
                next_identity: AtomicU64::new(2),
            }),
            location: Location::Parsed(Vec::new()),
            identity: 1,
        }
    }
    /// Borrows this occurrence while retaining the owner through `self`.
    pub fn read(&self) -> ValueRef<'_> {
        match (&self.document.storage, &self.location) {
            (Storage::Binary(binary), Location::Binary(location)) => {
                ValueRef::binary(binary.value_at(*location))
            }
            (Storage::Parsed(document), Location::Parsed(indices)) => {
                let mut value = ValueRef::parsed(document);
                for index in indices {
                    value = value
                        .value(*index)
                        .expect("selection coordinates remain valid");
                }
                value
            }
            _ => unreachable!("selection and document representations agree"),
        }
    }
    /// Stable for this selection occurrence, including across clones.
    pub fn identity(&self) -> u64 {
        self.identity
    }
    pub fn select(&self, path: &Path, options: QueryOptions) -> BinaryResult<Selection> {
        let parsed = matches!(self.location, Location::Parsed(_));
        let mut values = Vec::new();
        path.visit_read(self.read(), options, parsed, |value, coordinates| {
            let location = match &self.location {
                Location::Binary(_) => Location::Binary(
                    value
                        .as_binary()
                        .expect("binary selection remains binary")
                        .location(),
                ),
                Location::Parsed(prefix) => {
                    let mut path = prefix.clone();
                    path.extend_from_slice(coordinates);
                    Location::Parsed(path)
                }
            };
            values.push(Self {
                document: Arc::clone(&self.document),
                location,
                identity: 0,
            });
        })?;
        let first_identity = self
            .document
            .next_identity
            .fetch_update(Ordering::Relaxed, Ordering::Relaxed, |next| {
                next.checked_add(values.len() as u64)
            })
            .map_err(|_| BinaryError::new(BinaryErrorKind::InvalidOffset, 0))?;
        for (index, value) in values.iter_mut().enumerate() {
            value.identity = first_identity + index as u64;
        }
        Ok(Selection { values })
    }
    pub fn query_output(
        &self,
        path: &Path,
        options: QueryOutputOptions,
    ) -> BinaryResult<QueryOutput> {
        let values = path.query_read(self.read(), QueryOptions::default())?;
        crate::path::build_read_output(values, options)
    }
}
impl Selection {
    pub fn len(&self) -> usize {
        self.values.len()
    }
    pub fn is_empty(&self) -> bool {
        self.values.is_empty()
    }
    pub fn get(&self, index: usize) -> Option<&Value> {
        self.values.get(index)
    }
    pub fn iter(&self) -> impl ExactSizeIterator<Item = &Value> {
        self.values.iter()
    }
}
