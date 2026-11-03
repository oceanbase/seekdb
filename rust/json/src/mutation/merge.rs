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

//! SQL merge state owns a flat tree of binary scalar payloads and child IDs.

use crate::{binary, BinaryDocument, BinaryError, BinaryErrorKind, BinaryValue, JsonType, Document};
use std::collections::HashSet;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MergeMode { Preserve, Patch, OraclePatch }

pub enum MergeInput<'a> { SqlNull, Value(BinaryValue<'a>) }

#[derive(Clone, Debug)]
pub enum MergeError { Binary(BinaryError), Allocation, InvalidPatch }

impl From<BinaryError> for MergeError {
    fn from(error: BinaryError) -> Self { Self::Binary(error) }
}

type Result<T> = std::result::Result<T, MergeError>;
type NodeId = usize;

enum Node {
    Vacant,
    Scalar(Document),
    BinaryScalar(BinaryDocument<Vec<u8>>),
    Array(Vec<NodeId>),
    Object(Members),
}

#[derive(Default)]
struct Members(Vec<(Vec<u8>, NodeId)>);
impl Members {
    fn len(&self) -> usize { self.0.len() }
    fn try_reserve(&mut self, count: usize) -> std::result::Result<(), std::collections::TryReserveError> { self.0.try_reserve(count) }
    fn position(&self, key: &[u8]) -> std::result::Result<usize, usize> {
        self.0.binary_search_by(|(existing, _)| existing.len().cmp(&key.len()).then_with(|| existing.as_slice().cmp(key)))
    }
    fn insert(&mut self, key: Vec<u8>, value: NodeId) -> Option<NodeId> {
        match self.position(&key) {
            Ok(index) => Some(std::mem::replace(&mut self.0[index].1, value)),
            Err(index) => { self.0.insert(index, (key, value)); None }
        }
    }
    fn remove(&mut self, key: &[u8]) -> Option<NodeId> { self.position(key).ok().map(|index| self.0.remove(index).1) }
}
impl IntoIterator for Members {
    type Item = (Vec<u8>, NodeId);
    type IntoIter = std::vec::IntoIter<Self::Item>;
    fn into_iter(self) -> Self::IntoIter { self.0.into_iter() }
}

struct Arena {
    nodes: Vec<Node>,
    free: Vec<NodeId>,
    root: NodeId,
}

pub struct MergeArgument(PreparedInput);
enum PreparedInput { SqlNull, Value(Arena) }

impl MergeArgument {
    pub fn from_document(document: Document) -> Result<Self> {
        Ok(Self(PreparedInput::Value(Arena::build(Decode::Owned(document))?)))
    }

    pub fn prepare(input: MergeInput<'_>) -> Result<Self> {
        Ok(Self(match input {
            MergeInput::SqlNull => PreparedInput::SqlNull,
            MergeInput::Value(value) => {
                PreparedInput::Value(Arena::decode(value)?)
            }
        }))
    }
}

enum Accumulated { Empty, SqlNull, Value(Arena) }

pub struct MergeAccumulator {
    mode: MergeMode,
    state: Accumulated,
}

impl MergeAccumulator {
    pub fn new(mode: MergeMode) -> Self { Self { mode, state: Accumulated::Empty } }

    pub fn needs_input(&self) -> bool {
        !(matches!(self.mode, MergeMode::Preserve | MergeMode::OraclePatch)
            && matches!(self.state, Accumulated::SqlNull))
    }

    /// Consumes the accumulator. Failure leaves no publishable result.
    pub fn push(self, input: MergeInput<'_>) -> Result<Self> {
        if !self.needs_input() { return Ok(self); }
        self.push_prepared(MergeArgument::prepare(input)?)
    }

    pub fn push_prepared(self, input: MergeArgument) -> Result<Self> {
        if !self.needs_input() { return Ok(self); }
        let mode = self.mode;
        let state = match input.0 {
            PreparedInput::SqlNull
                if mode == MergeMode::OraclePatch && !matches!(self.state, Accumulated::Empty) => {
                    return Err(MergeError::InvalidPatch);
                }
            PreparedInput::SqlNull => Accumulated::SqlNull,
            PreparedInput::Value(value) => match (self.state, mode) {
                (Accumulated::Empty, _) => Accumulated::Value(value),
                (Accumulated::SqlNull, MergeMode::Preserve) => Accumulated::SqlNull,
                (Accumulated::SqlNull, MergeMode::Patch | MergeMode::OraclePatch)
                    if matches!(&value.nodes[value.root], Node::Object(_)) => Accumulated::SqlNull,
                (Accumulated::SqlNull, MergeMode::Patch | MergeMode::OraclePatch) => Accumulated::Value(value),
                (Accumulated::Value(previous), _) => Accumulated::Value(previous.merge(value, mode)?),
            }
        };
        Ok(Self { mode, state })
    }

    pub fn finish(self) -> Result<Option<Vec<u8>>> {
        match self.state {
            Accumulated::Empty | Accumulated::SqlNull => Ok(None),
            Accumulated::Value(value) => value.encode().map(Some),
        }
    }
}

enum Decode<'a> {
    Visit(BinaryValue<'a>),
    Owned(Document),
    Leave(usize),
    Array(usize),
    Object(Vec<Vec<u8>>),
}

struct Pair {
    left: NodeId,
    right: NodeId,
    destination: Option<(NodeId, Vec<u8>)>,
}

impl Arena {
    fn add(&mut self, node: Node) -> Result<NodeId> {
        if let Some(index) = self.free.pop() {
            self.nodes[index] = node;
            Ok(index)
        } else {
            self.nodes.try_reserve(1).map_err(|_| MergeError::Allocation)?;
            let index = self.nodes.len();
            self.nodes.push(node);
            Ok(index)
        }
    }

    fn take(&mut self, index: NodeId) -> Result<Node> {
        self.free.try_reserve(1).map_err(|_| MergeError::Allocation)?;
        let node = std::mem::replace(&mut self.nodes[index], Node::Vacant);
        self.free.push(index);
        Ok(node)
    }

    fn discard(&mut self, root: NodeId) -> Result<()> {
        let mut pending = Vec::new();
        pending.try_reserve(1).map_err(|_| MergeError::Allocation)?;
        pending.push(root);
        while let Some(index) = pending.pop() {
            match self.take(index)? {
                Node::Array(values) => {
                    pending.try_reserve(values.len()).map_err(|_| MergeError::Allocation)?;
                    pending.extend(values);
                }
                Node::Object(values) => {
                    pending.try_reserve(values.len()).map_err(|_| MergeError::Allocation)?;
                    pending.extend(values.0.into_iter().map(|(_, value)| value));
                }
                Node::Scalar(_) | Node::BinaryScalar(_) => {}
                Node::Vacant => {}
            }
        }
        Ok(())
    }

    fn decode(value: BinaryValue<'_>) -> Result<Self> { Self::build(Decode::Visit(value)) }

    fn build(initial: Decode<'_>) -> Result<Self> {
        let remaining = match &initial { Decode::Visit(value) => value.traversal_limit(), _ => usize::MAX };
        let mut arena = Self { nodes: Vec::new(), free: Vec::new(), root: 0 };
        let mut active = HashSet::new();
        let mut remaining = remaining;
        let mut tasks = Vec::new();
        let mut completed = Vec::new();
        tasks.try_reserve(1).map_err(|_| MergeError::Allocation)?;
        tasks.push(initial);
        while let Some(task) = tasks.pop() {
            let node = match task {
                Decode::Owned(document) => match document {
                    Document::Array(values) => {
                        tasks.try_reserve(values.len().saturating_add(1)).map_err(|_| MergeError::Allocation)?;
                        tasks.push(Decode::Array(values.len()));
                        tasks.extend(values.into_iter().rev().map(Decode::Owned));
                        continue;
                    }
                    Document::Object(values) => {
                        tasks.try_reserve(values.len().saturating_add(1)).map_err(|_| MergeError::Allocation)?;
                        let marker = tasks.len();
                        tasks.push(Decode::Array(0));
                        let mut keys = Vec::new();
                        keys.try_reserve_exact(values.len()).map_err(|_| MergeError::Allocation)?;
                        for (key, value) in values.into_iter().rev() {
                            keys.push(key.into_bytes());
                            tasks.push(Decode::Owned(value));
                        }
                        keys.reverse();
                        tasks[marker] = Decode::Object(keys);
                        continue;
                    }
                    scalar => Node::Scalar(scalar),
                },
                Decode::Visit(value) => {
                    if remaining == 0 { return Err(BinaryError::new(BinaryErrorKind::InvalidOffset, value.storage_offset()).into()); }
                    remaining -= 1;
                    let kind = value.json_type()?;
                    if kind.is_container() {
                        if !active.insert(value.storage_offset()) { return Err(BinaryError::new(BinaryErrorKind::InvalidOffset, value.storage_offset()).into()); }
                        tasks.push(Decode::Leave(value.storage_offset()));
                        let entries = value.entries()?;
                        let count = entries.len();
                        tasks.try_reserve(count.saturating_add(1)).map_err(|_| MergeError::Allocation)?;
                        let marker = tasks.len();
                        tasks.push(Decode::Array(count));
                        let mut keys = Vec::new();
                        if kind == JsonType::Object { keys.try_reserve_exact(count).map_err(|_| MergeError::Allocation)?; }
                        for entry in entries.rev() {
                            let (key, value) = entry?;
                            if kind == JsonType::Object { keys.push(copy_bytes(key)?); }
                            tasks.push(Decode::Visit(value));
                        }
                        if kind == JsonType::Object {
                            keys.reverse();
                            tasks[marker] = Decode::Object(keys);
                        }
                        continue;
                    }
                    match kind {
                        JsonType::Null => Node::Scalar(Document::Null),
                        JsonType::Boolean => Node::Scalar(Document::Bool(value.boolean()?)),
                        JsonType::Int | JsonType::Uint | JsonType::Double => Node::Scalar(Document::Number(value.number()?)),
                        JsonType::String => match std::str::from_utf8(value.string_bytes()?) {
                            Ok(value) => Node::Scalar(Document::String(value.to_owned())),
                            Err(_) => Node::BinaryScalar(BinaryDocument::new(binary::serialize_value(value)?)?),
                        },
                        _ => Node::BinaryScalar(BinaryDocument::new(binary::serialize_value(value)?)?),
                    }
                }
                Decode::Leave(offset) => { active.remove(&offset); continue; }
                Decode::Array(count) => {
                    let mut values = Vec::new();
                    values.try_reserve_exact(count).map_err(|_| MergeError::Allocation)?;
                    values.extend(completed.drain(completed.len() - count..));
                    Node::Array(values)
                }
                Decode::Object(keys) => {
                    let start = completed.len() - keys.len();
                    let mut values: Vec<_> = keys.into_iter().zip(completed.drain(start..)).collect();
                    values.sort_by(|(left, _), (right, _)| left.len().cmp(&right.len()).then_with(|| left.cmp(right)));
                    for index in 0..values.len().saturating_sub(1) {
                        if values[index].0 == values[index + 1].0 { arena.discard(values[index].1)?; }
                    }
                    values.dedup_by(|next, previous| {
                        if next.0 != previous.0 { return false; }
                        previous.1 = next.1;
                        true
                    });
                    Node::Object(Members(values))
                }
            };
            completed.try_reserve(1).map_err(|_| MergeError::Allocation)?;
            completed.push(arena.add(node)?);
        }
        arena.root = completed[0];
        Ok(arena)
    }

    fn import(&mut self, other: Self) -> Result<NodeId> {
        let mut ids = Vec::new();
        ids.try_reserve_exact(other.nodes.len()).map_err(|_| MergeError::Allocation)?;
        for node in &other.nodes {
            ids.push(if matches!(node, Node::Vacant) { usize::MAX } else { self.add(Node::Vacant)? });
        }
        let root = ids[other.root];
        for (index, mut node) in other.nodes.into_iter().enumerate() {
            match &mut node {
                Node::Array(values) => values.iter_mut().for_each(|value| *value = ids[*value]),
                Node::Object(values) => values.0.iter_mut().for_each(|(_, value)| *value = ids[*value]),
                Node::Scalar(_) | Node::BinaryScalar(_) => {},
                Node::Vacant => continue,
            }
            self.nodes[ids[index]] = node;
        }
        Ok(root)
    }

    fn is_null(&self, index: NodeId) -> bool {
        matches!(&self.nodes[index], Node::Scalar(Document::Null))
    }

    fn object_mut(&mut self, index: NodeId) -> &mut Members {
        match &mut self.nodes[index] { Node::Object(value) => value, _ => unreachable!("object merge destination") }
    }

    fn merge(mut self, other: Self, mode: MergeMode) -> Result<Self> {
        let right = self.import(other)?;
        let mut pairs = Vec::new();
        pairs.try_reserve(1).map_err(|_| MergeError::Allocation)?;
        pairs.push(Pair { left: self.root, right, destination: None });
        while let Some(pair) = pairs.pop() {
            let result = if mode == MergeMode::Preserve {
                self.preserve_pair(pair.left, pair.right, &mut pairs)?
            } else { self.patch_pair(pair.left, pair.right, &mut pairs)? };
            if let Some((parent, key)) = pair.destination { self.object_mut(parent).insert(key, result); }
            else { self.root = result; }
        }
        Ok(self)
    }

    fn preserve_pair(&mut self, left: NodeId, right: NodeId, pairs: &mut Vec<Pair>) -> Result<NodeId> {
        if matches!((&self.nodes[left], &self.nodes[right]), (Node::Object(_), Node::Object(_))) {
            let Node::Object(values) = self.take(right)? else { unreachable!() };
            self.object_mut(left).try_reserve(values.len()).map_err(|_| MergeError::Allocation)?;
            pairs.try_reserve(values.len()).map_err(|_| MergeError::Allocation)?;
            for (key, value) in values {
                if let Some(previous) = self.object_mut(left).remove(&key) {
                    pairs.push(Pair { left: previous, right: value, destination: Some((left, key)) });
                } else { self.object_mut(left).insert(key, value); }
            }
            return Ok(left);
        }
        let left_array = matches!(&self.nodes[left], Node::Array(_));
        let right_array = matches!(&self.nodes[right], Node::Array(_));
        if left_array && right_array {
            let Node::Array(values) = self.take(right)? else { unreachable!() };
            let Node::Array(previous) = &mut self.nodes[left] else { unreachable!() };
            previous.try_reserve(values.len()).map_err(|_| MergeError::Allocation)?;
            previous.extend(values);
            Ok(left)
        } else if left_array {
            let Node::Array(previous) = &mut self.nodes[left] else { unreachable!() };
            previous.try_reserve(1).map_err(|_| MergeError::Allocation)?;
            previous.push(right);
            Ok(left)
        } else if right_array {
            let Node::Array(values) = &mut self.nodes[right] else { unreachable!() };
            values.try_reserve(1).map_err(|_| MergeError::Allocation)?;
            values.insert(0, left);
            Ok(right)
        } else {
            let mut values = Vec::new();
            values.try_reserve_exact(2).map_err(|_| MergeError::Allocation)?;
            values.extend([left, right]);
            self.add(Node::Array(values))
        }
    }

    fn patch_pair(&mut self, mut left: NodeId, right: NodeId, pairs: &mut Vec<Pair>) -> Result<NodeId> {
        if !matches!(&self.nodes[right], Node::Object(_)) {
            self.discard(left)?;
            return Ok(right);
        }
        let Node::Object(values) = self.take(right)? else { unreachable!() };
        if !matches!(&self.nodes[left], Node::Object(_)) {
            self.discard(left)?;
            left = self.add(Node::Object(Members::default()))?;
        }
        self.object_mut(left).try_reserve(values.len()).map_err(|_| MergeError::Allocation)?;
        pairs.try_reserve(values.len()).map_err(|_| MergeError::Allocation)?;
        for (key, value) in values {
            if self.is_null(value) {
                self.discard(value)?;
                if let Some(previous) = self.object_mut(left).remove(&key) { self.discard(previous)?; }
            } else {
                let previous = match self.object_mut(left).remove(&key) {
                    Some(previous) => previous,
                    None => self.add(Node::Object(Members::default()))?,
                };
                pairs.push(Pair { left: previous, right: value, destination: Some((left, key)) });
            }
        }
        Ok(left)
    }

    fn encode(self) -> Result<Vec<u8>> {
        binary::serialize_constructed_source(&self, self.root).map_err(MergeError::from)
    }
}

fn copy_bytes(bytes: &[u8]) -> Result<Vec<u8>> {
    let mut output = Vec::new();
    output.try_reserve_exact(bytes.len()).map_err(|_| MergeError::Allocation)?;
    output.extend_from_slice(bytes);
    Ok(output)
}

impl binary::ConstructedSource for Arena {
    const CHECK_CYCLES: bool = false;
    type Value = NodeId;
    fn identity(&self, value: NodeId) -> (usize, usize) { (0, value) }
    fn traversal_limit(&self) -> usize { self.nodes.len().saturating_add(1) }
    fn kind(&self, index: NodeId) -> crate::BinaryResult<JsonType> {
        match &self.nodes[index] {
            Node::Scalar(value) => binary::ConstructedScalar::Document(value).kind(),
            Node::BinaryScalar(value) => value.root()?.json_type(),
            Node::Array(_) => Ok(JsonType::Array),
            Node::Object(_) => Ok(JsonType::Object),
            Node::Vacant => Err(BinaryError::new(BinaryErrorKind::InvalidOffset, index)),
        }
    }
    fn scalar(&self, index: NodeId) -> crate::BinaryResult<binary::ConstructedScalar<'_>> {
        match &self.nodes[index] {
            Node::Scalar(value) => Ok(binary::ConstructedScalar::Document(value)),
            Node::BinaryScalar(value) => Ok(binary::ConstructedScalar::Binary(value.root()?)),
            _ => Err(BinaryError::new(BinaryErrorKind::InvalidType, index)),
        }
    }
    fn entries(&self, index: NodeId) -> crate::BinaryResult<Vec<(&[u8], NodeId)>> {
        match &self.nodes[index] {
            Node::Array(values) => Ok(values.iter().map(|&value| (&[][..], value)).collect()),
            Node::Object(values) => {
                Ok(values.0.iter().map(|(key, value)| (key.as_slice(), *value)).collect())
            }
            _ => Err(BinaryError::new(BinaryErrorKind::InvalidType, index)),
        }
    }
}
