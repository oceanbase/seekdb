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

//! The database's draft-4 schema policy, including its normalized binary map codec.
//! A schema owns compiled patterns; each validation has independent answer and report state.

mod compiler;
#[cfg(test)]
mod tests;
mod validator;

use crate::{
    binary, BinaryDocument, BinaryError, BinaryErrorKind, BinaryValue, CompareError, Document,
    JsonType, Number,
};
use std::collections::BTreeMap;
use std::fmt;

type Id = usize;
pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug)]
pub enum Error {
    Binary(BinaryError),
    Compare(CompareError),
    Parse(crate::ParseError),
    InvalidSchema,
    InvalidMap,
    MissingSchema,
    ObjectType,
    InvalidArgument,
    Composition(CompositionError),
    InvalidIndex,
    Pattern(String),
    Platform(i32),
    UnsupportedComparison,
}

/// Composition errors retain one terminal cause and a count of enclosing contexts.
pub struct CompositionError {
    cause: Box<Error>,
    depth: usize,
}

impl Error {
    pub fn composition(self) -> Self {
        match self {
            Self::Composition(mut context) => {
                context.depth = context.depth.saturating_add(1);
                Self::Composition(context)
            }
            cause => Self::Composition(CompositionError {
                cause: Box::new(cause),
                depth: 1,
            }),
        }
    }
}

impl CompositionError {
    pub fn depth(&self) -> usize {
        self.depth
    }
}

impl std::ops::Deref for CompositionError {
    type Target = Error;
    fn deref(&self) -> &Error {
        &self.cause
    }
}

impl fmt::Debug for CompositionError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        for _ in 1..self.depth {
            formatter.write_str("Composition(")?;
        }
        fmt::Debug::fmt(&self.cause, formatter)?;
        for _ in 1..self.depth {
            formatter.write_str(")")?;
        }
        Ok(())
    }
}

impl From<BinaryError> for Error {
    fn from(value: BinaryError) -> Self {
        Self::Binary(value)
    }
}
impl From<CompareError> for Error {
    fn from(value: CompareError) -> Self {
        Self::Compare(value)
    }
}
impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "JSON schema: {self:?}")
    }
}
impl std::error::Error for Error {}

#[derive(Debug)]
pub enum PatternError {
    Invalid,
    Resource(String),
}

/// The adapter owns its compiled expression and is used synchronously on the calling thread.
/// Implementations preserve the chosen regex dialect, including the handling of NUL bytes.
pub trait Pattern {
    fn is_match(&self, text: &[u8]) -> Result<bool>;
}
pub trait PatternCompiler {
    fn compile(&self, pattern: &[u8]) -> std::result::Result<Box<dyn Pattern>, PatternError>;
}

/// Synchronous scalar conversion capability; compiled schemas retain only the function.
pub type ScalarComparator = fn(BinaryValue<'_>, BinaryValue<'_>) -> Result<std::cmp::Ordering>;
fn unsupported_scalar(_: BinaryValue<'_>, _: BinaryValue<'_>) -> Result<std::cmp::Ordering> {
    Err(Error::UnsupportedComparison)
}
fn equal_values(
    left: BinaryValue<'_>,
    right: BinaryValue<'_>,
    mut scalar: ScalarComparator,
) -> Result<bool> {
    use crate::compare::OrderError;
    crate::compare::order(left, right, &mut scalar)
        .map(|order| order.is_eq())
        .map_err(|error| match error {
            OrderError::Value(error) => Error::Compare(error),
            OrderError::UnsupportedTypes => Error::UnsupportedComparison,
            OrderError::Scalar(error) => error,
        })
}

#[derive(Clone)]
enum Node {
    Scalar(BinaryDocument<Vec<u8>>),
    Array(Vec<Id>),
    Object(Vec<(Vec<u8>, Id)>),
}
#[derive(Default)]
struct Arena {
    nodes: Vec<Node>,
    parent: Vec<Option<Id>>,
}
impl Arena {
    fn add(&mut self, node: Node) -> Id {
        let id = self.nodes.len();
        self.nodes.push(node);
        self.parent.push(None);
        match &self.nodes[id] {
            Node::Array(values) => {
                for child in values {
                    self.parent[*child] = Some(id);
                }
            }
            Node::Object(values) => {
                for (_, child) in values {
                    self.parent[*child] = Some(id);
                }
            }
            Node::Scalar(_) => {}
        }
        id
    }
    fn object(&mut self) -> Id {
        self.add(Node::Object(Vec::new()))
    }
    fn array(&mut self, values: Vec<Id>) -> Id {
        self.add(Node::Array(values))
    }
    fn scalar(&mut self, value: Document) -> Id {
        self.add(Node::Scalar(
            BinaryDocument::new(binary::serialize(value)).expect("serialized scalar"),
        ))
    }
    fn int(&mut self, value: i64) -> Id {
        self.scalar(Document::Number(Number::I64(value)))
    }
    fn uint(&mut self, value: u64) -> Id {
        self.scalar(Document::Number(Number::U64(value)))
    }
    fn string(&mut self, value: &[u8]) -> Result<Id> {
        let value = std::str::from_utf8(value).map_err(|_| Error::InvalidMap)?;
        Ok(self.scalar(Document::String(value.into())))
    }
    fn get(&self, id: Id, key: &[u8]) -> Option<Id> {
        match &self.nodes[id] {
            Node::Object(values) => values.iter().find(|(k, _)| k == key).map(|(_, v)| *v),
            _ => None,
        }
    }
    fn set(&mut self, id: Id, key: &[u8], value: Id) -> Result<()> {
        let Node::Object(values) = &mut self.nodes[id] else {
            return Err(Error::InvalidMap);
        };
        if let Some((_, old)) = values.iter_mut().find(|(k, _)| k == key) {
            *old = value;
        } else {
            values.push((key.to_vec(), value));
            values.sort_by(|(a, _), (b, _)| a.len().cmp(&b.len()).then_with(|| a.cmp(b)));
        }
        self.parent[value] = Some(id);
        Ok(())
    }
    fn child(&mut self, id: Id, key: &[u8]) -> Result<Id> {
        if let Some(child) = self.get(id, key) {
            if !self.is_object(child) {
                return Err(Error::InvalidMap);
            }
            Ok(child)
        } else {
            let child = self.object();
            self.set(id, key, child)?;
            Ok(child)
        }
    }
    fn remove(&mut self, id: Id, key: &[u8]) {
        if let Node::Object(values) = &mut self.nodes[id] {
            values.retain(|(k, _)| k != key);
        }
    }
    fn append(&mut self, id: Id, value: Id) -> Result<()> {
        let Node::Array(values) = &mut self.nodes[id] else {
            return Err(Error::InvalidMap);
        };
        values.push(value);
        self.parent[value] = Some(id);
        Ok(())
    }
    fn items(&self, id: Id) -> Result<&[Id]> {
        match &self.nodes[id] {
            Node::Array(values) => Ok(values),
            _ => Err(Error::InvalidMap),
        }
    }
    fn element(&self, id: Id, index: usize) -> Result<Id> {
        let child = *self.items(id)?.get(index).ok_or(Error::InvalidIndex)?;
        if self.parent[child] != Some(id) {
            return Err(Error::MissingSchema);
        }
        Ok(child)
    }
    fn replace_array(&mut self, id: Id, values: Vec<Id>) {
        for child in &values {
            self.parent[*child] = Some(id);
        }
        self.nodes[id] = Node::Array(values);
    }
    fn fields(&self, id: Id) -> Result<&[(Vec<u8>, Id)]> {
        match &self.nodes[id] {
            Node::Object(values) => Ok(values),
            _ => Err(Error::InvalidMap),
        }
    }
    fn is_object(&self, id: Id) -> bool {
        matches!(self.nodes[id], Node::Object(_))
    }
    fn is_array(&self, id: Id) -> bool {
        matches!(self.nodes[id], Node::Array(_))
    }
    fn value(&self, id: Id) -> Result<BinaryValue<'_>> {
        match &self.nodes[id] {
            Node::Scalar(bytes) => Ok(bytes.root()?),
            _ => Err(Error::InvalidMap),
        }
    }
    fn kind(&self, id: Id) -> Result<JsonType> {
        match &self.nodes[id] {
            Node::Array(_) => Ok(JsonType::Array),
            Node::Object(_) => Ok(JsonType::Object),
            _ => Ok(self.value(id)?.json_type()?),
        }
    }
    fn bytes(&self, id: Id) -> Result<&[u8]> {
        Ok(self.value(id)?.string_bytes()?)
    }
    fn integer(&self, id: Id) -> Result<i64> {
        match self.value(id)?.number()? {
            Number::I64(n) => Ok(n),
            Number::U64(n) => Ok(n as i64),
            _ => Err(Error::InvalidMap),
        }
    }
    fn boolean(&self, id: Id) -> Result<bool> {
        Ok(self.value(id)?.boolean()?)
    }
    fn number(&self, id: Id) -> Result<f64> {
        number(self.value(id)?)
    }
    fn len(&self, id: Id) -> usize {
        match &self.nodes[id] {
            Node::Array(v) => v.len(),
            Node::Object(v) => v.len(),
            _ => 1,
        }
    }
    fn load(&mut self, value: BinaryValue<'_>) -> Result<Id> {
        enum Pending<'a> {
            Value(BinaryValue<'a>),
            Exit(usize),
            Enum(BinaryValue<'a>),
            Array(usize),
            Object(Vec<Vec<u8>>),
        }
        let mut pending = vec![Pending::Value(value)];
        let mut results = Vec::new();
        let mut active = std::collections::HashSet::new();
        while let Some(task) = pending.pop() {
            if let Pending::Value(value) = &task {
                if value.json_type()?.is_container() {
                    if !active.insert(value.storage_offset()) {
                        return Err(BinaryError::new(
                            BinaryErrorKind::InvalidDepth,
                            value.storage_offset(),
                        )
                        .into());
                    }
                    pending.push(Pending::Exit(value.storage_offset()));
                }
            }
            match task {
                Pending::Exit(offset) => {
                    active.remove(&offset);
                }
                Pending::Value(value) => match value.json_type()? {
                    JsonType::Array => {
                        let len = value.len()?;
                        pending.push(Pending::Array(len as usize));
                        for index in (0..len).rev() {
                            pending.push(Pending::Value(value.value(index)?));
                        }
                    }
                    JsonType::Object => {
                        let len = value.len()?;
                        let mut keys = Vec::with_capacity(len as usize);
                        for index in 0..len {
                            keys.push(value.key(index)?.to_vec());
                        }
                        pending.push(Pending::Object(keys));
                        for index in (0..len).rev() {
                            let child = value.value(index)?;
                            pending.push(
                                if value.key(index)? == b"enum"
                                    && child.json_type()? == JsonType::Array
                                {
                                    Pending::Enum(child)
                                } else {
                                    Pending::Value(child)
                                },
                            );
                        }
                    }
                    _ => results.push(self.add(Node::Scalar(BinaryDocument::new(
                        binary::serialize_value(value)?,
                    )?))),
                },
                Pending::Enum(value) => {
                    let mut literals = Vec::new();
                    for index in 0..value.len()? {
                        literals.push(self.add(Node::Scalar(BinaryDocument::new(
                            binary::serialize_value(value.value(index)?)?,
                        )?)));
                    }
                    results.push(self.array(literals));
                }
                Pending::Array(len) => {
                    let values = results.split_off(results.len() - len);
                    results.push(self.array(values));
                }
                Pending::Object(keys) => {
                    let values = results.split_off(results.len() - keys.len());
                    results.push(self.add(Node::Object(keys.into_iter().zip(values).collect())));
                }
            }
        }
        results.pop().ok_or(Error::InvalidMap)
    }
    fn retain(&mut self, root: Id) -> Id {
        let mut reachable = vec![false; self.nodes.len()];
        let mut pending = vec![root];
        while let Some(id) = pending.pop() {
            if std::mem::replace(&mut reachable[id], true) {
                continue;
            }
            match &self.nodes[id] {
                Node::Array(values) => pending.extend(values),
                Node::Object(values) => pending.extend(values.iter().map(|(_, id)| id)),
                Node::Scalar(_) => {}
            }
        }
        let mut remap = vec![0; self.nodes.len()];
        let mut next = 0;
        for (id, retain) in reachable.iter().enumerate() {
            if *retain {
                remap[id] = next;
                next += 1;
            }
        }
        self.parent = self
            .parent
            .iter()
            .enumerate()
            .filter(|(id, _owner)| reachable[*id])
            .map(|(_, owner)| {
                owner
                    .filter(|parent| reachable[*parent])
                    .map(|parent| remap[parent])
            })
            .collect();
        self.nodes = std::mem::take(&mut self.nodes)
            .into_iter()
            .enumerate()
            .filter_map(|(id, mut node)| {
                if !reachable[id] {
                    return None;
                }
                match &mut node {
                    Node::Array(values) => {
                        for child in values {
                            *child = remap[*child];
                        }
                    }
                    Node::Object(values) => {
                        for (_, child) in values {
                            *child = remap[*child];
                        }
                    }
                    Node::Scalar(_) => {}
                }
                Some(node)
            })
            .collect();
        remap[root]
    }

    fn encode(&self, id: Id) -> Result<Vec<u8>> {
        struct Frame {
            id: Id,
            next: usize,
            children: Vec<Vec<u8>>,
        }
        let mut frames = vec![Frame {
            id,
            next: 0,
            children: Vec::new(),
        }];
        let mut active = std::collections::HashSet::from([id]);
        loop {
            let frame = frames.last_mut().ok_or(Error::InvalidMap)?;
            let next = match &self.nodes[frame.id] {
                Node::Scalar(_) => None,
                Node::Array(values) => values.get(frame.next).copied(),
                Node::Object(values) => values.get(frame.next).map(|(_, child)| *child),
            };
            if let Some(id) = next {
                frame.next += 1;
                if !active.insert(id) {
                    return Err(Error::InvalidMap);
                }
                frames.push(Frame {
                    id,
                    next: 0,
                    children: Vec::new(),
                });
                continue;
            }
            let frame = frames.pop().ok_or(Error::InvalidMap)?;
            active.remove(&frame.id);
            let encoded = match &self.nodes[frame.id] {
                Node::Scalar(bytes) => bytes.as_bytes().to_vec(),
                Node::Array(_) => {
                    let documents = frame
                        .children
                        .iter()
                        .map(|bytes| BinaryDocument::new(bytes.as_slice()))
                        .collect::<std::result::Result<Vec<_>, _>>()?;
                    let values = documents
                        .iter()
                        .map(|document| document.root())
                        .collect::<std::result::Result<Vec<_>, _>>()?;
                    binary::serialize_array(&values)?
                }
                Node::Object(fields) => {
                    let documents = frame
                        .children
                        .iter()
                        .map(|bytes| BinaryDocument::new(bytes.as_slice()))
                        .collect::<std::result::Result<Vec<_>, _>>()?;
                    let values = fields
                        .iter()
                        .zip(documents.iter())
                        .map(|((key, _), document)| {
                            document.root().map(|value| (key.as_slice(), value))
                        })
                        .collect::<std::result::Result<Vec<_>, _>>()?;
                    binary::serialize_object(&values)?
                }
            };
            if let Some(parent) = frames.last_mut() {
                parent.children.push(encoded);
            } else {
                return Ok(encoded);
            }
        }
    }

    fn equal(&self, left: Id, right: Id, same_tag: bool, scalar: ScalarComparator) -> Result<bool> {
        if same_tag && self.kind(left)? != self.kind(right)? {
            return Ok(false);
        }
        let left = BinaryDocument::new(self.encode(left)?)?;
        let right = BinaryDocument::new(self.encode(right)?)?;
        equal_values(left.root()?, right.root()?, scalar)
    }
}

fn is_number(kind: JsonType) -> bool {
    matches!(
        kind,
        JsonType::Int
            | JsonType::Uint
            | JsonType::Double
            | JsonType::Decimal
            | JsonType::OInt
            | JsonType::OLong
            | JsonType::OFloat
            | JsonType::ODouble
            | JsonType::ODecimal
    )
}
fn number(value: BinaryValue<'_>) -> Result<f64> {
    if matches!(value.json_type()?, JsonType::Decimal | JsonType::ODecimal) {
        return Ok(value.decimal()?.to_f64());
    }
    Ok(match value.number()? {
        Number::I64(n) => n as f64,
        Number::U64(n) => n as f64,
        Number::F64(n) => n,
    })
}

/// One successful compilation, reusable for both Boolean and detailed validation.
pub struct Schema {
    arena: Arena,
    map: Id,
    patterns: BTreeMap<Vec<u8>, Box<dyn Pattern>>,
    scalar: ScalarComparator,
}
impl Schema {
    pub fn compile(value: BinaryValue<'_>, compiler: &dyn PatternCompiler) -> Result<Self> {
        Self::compile_with(value, compiler, unsupported_scalar)
    }
    pub fn compile_with(
        value: BinaryValue<'_>,
        compiler: &dyn PatternCompiler,
        scalar: ScalarComparator,
    ) -> Result<Self> {
        compiler::compile(value, compiler, scalar)
    }
    pub fn decode(bytes: &[u8], compiler: &dyn PatternCompiler) -> Result<Self> {
        Self::decode_with(bytes, compiler, unsupported_scalar)
    }
    pub fn decode_with(
        bytes: &[u8],
        compiler: &dyn PatternCompiler,
        scalar: ScalarComparator,
    ) -> Result<Self> {
        let document = BinaryDocument::new(bytes)?;
        let mut arena = Arena::default();
        let map = arena.load(document.root()?)?;
        let values = arena.items(map)?;
        if values.len() < 2 || !arena.is_object(values[0]) || !arena.is_object(values[1]) {
            return Err(Error::InvalidMap);
        }
        let mut patterns = BTreeMap::new();
        compiler::load_patterns(&arena, map, compiler, &mut patterns)?;
        Ok(Self {
            arena,
            map,
            patterns,
            scalar,
        })
    }
    pub fn from_input(
        input: &[u8],
        format: InputFormat,
        max_depth: u32,
        compiler: &dyn PatternCompiler,
        scalar: ScalarComparator,
    ) -> Result<Self> {
        if format == InputFormat::Normalized {
            return Self::decode_with(input, compiler, scalar);
        }
        let parsed;
        let bytes = if format == InputFormat::Text {
            parsed = crate::parse_binary(
                input,
                crate::ParseOptions {
                    duplicate_policy: crate::DuplicatePolicy::KeepFirst,
                    schema: true,
                    max_depth,
                },
            )
            .map_err(Error::Parse)?;
            parsed.as_slice()
        } else {
            input
        };
        let document = BinaryDocument::new(bytes)?;
        Self::compile_with(document.root()?, compiler, scalar)
    }
    pub fn encode(&self) -> Result<Vec<u8>> {
        self.arena.encode(self.map)
    }
    pub fn validate(&self, value: BinaryValue<'_>) -> Result<Validation> {
        validator::validate(self, value)
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Failure {
    pub keyword: String,
    pub document_location: String,
    pub schema_location: String,
}
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Validation {
    pub failure: Option<Failure>,
}
impl Validation {
    pub fn is_valid(&self) -> bool {
        self.failure.is_none()
    }
    pub fn report(&self) -> Vec<u8> {
        let mut values = vec![("valid".into(), Document::Bool(self.is_valid()))];
        if let Some(f) = &self.failure {
            let reason=format!("The JSON document location '{}' failed requirement '{}' at JSON Schema location '{}",f.document_location,f.keyword,f.schema_location);
            for (key, value) in [
                ("reason", reason),
                ("schema-location", f.schema_location.clone()),
                ("document-location", f.document_location.clone()),
                ("schema-failed-keyword", f.keyword.clone()),
            ] {
                values.push((key.into(), Document::String(value)));
            }
        }
        binary::serialize(Document::Object(values))
    }
}

/// Each execution slot caches only its most recent successful schema. A failed
/// replacement leaves the previous successful entry available for the next row.
#[derive(Clone, Copy, Eq, PartialEq)]
pub enum InputFormat {
    Text,
    Binary,
    Normalized,
}
pub struct Cache {
    entries: Vec<Option<(Vec<u8>, InputFormat, Schema)>>,
    scalar: ScalarComparator,
}
impl Default for Cache {
    fn default() -> Self {
        Self::with_comparator(unsupported_scalar)
    }
}
impl Cache {
    pub fn with_comparator(scalar: ScalarComparator) -> Self {
        Self {
            entries: Vec::new(),
            scalar,
        }
    }
    pub fn get_or_compile(
        &mut self,
        slot: usize,
        input: &[u8],
        binary_input: bool,
        compiler: &dyn PatternCompiler,
    ) -> Result<&Schema> {
        self.prepare(
            slot,
            input,
            if binary_input {
                InputFormat::Binary
            } else {
                InputFormat::Text
            },
            compiler,
        )
    }
    pub fn prepare(
        &mut self,
        slot: usize,
        input: &[u8],
        format: InputFormat,
        compiler: &dyn PatternCompiler,
    ) -> Result<&Schema> {
        let matched = self
            .entries
            .get(slot)
            .and_then(Option::as_ref)
            .is_some_and(|(key, kind, _)| !key.is_empty() && key == input && *kind == format);
        if !matched {
            let schema = Schema::from_input(input, format, 101, compiler, self.scalar)?;
            if self.entries.len() <= slot {
                self.entries.resize_with(slot + 1, || None);
            }
            self.entries[slot] = Some((input.to_vec(), format, schema));
        }
        self.get(slot)
    }
    pub fn get(&self, slot: usize) -> Result<&Schema> {
        self.entries
            .get(slot)
            .and_then(Option::as_ref)
            .map(|(_, _, schema)| schema)
            .ok_or(Error::InvalidIndex)
    }
}

/// Empty and fragment references are local; all other string references are rejected.
pub fn references_are_local(value: BinaryValue<'_>) -> crate::BinaryResult<bool> {
    let path = crate::Path::parse_mysql(br#"$**."$ref""#).expect("constant reference path");
    for reference in path.query(
        value,
        crate::QueryOptions {
            auto_wrap: false,
            first_only: false,
        },
    )? {
        if reference.json_type()? == JsonType::String {
            let text = reference.string_bytes()?;
            if !text.is_empty() && text[0] != b'#' {
                return Ok(false);
            }
        }
    }
    Ok(true)
}
