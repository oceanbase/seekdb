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

use crate::{
    binary, BinaryDocument, BinaryError, BinaryErrorKind, BinaryValue, Document, JsonType,
};
use std::collections::HashSet;

#[derive(Clone, Debug)]
pub enum ValueError {
    Binary(BinaryError),
    Allocation,
}
impl From<BinaryError> for ValueError {
    fn from(error: BinaryError) -> Self {
        Self::Binary(error)
    }
}
type Result<T> = std::result::Result<T, ValueError>;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum KeyOrder {
    LengthFirst,
    Lexicographical,
}

#[derive(Clone, Copy)]
pub(crate) enum MergeStyle {
    Preserve,
    Patch,
}
impl KeyOrder {
    fn compare(self, left: &[u8], right: &[u8]) -> std::cmp::Ordering {
        match self {
            Self::LengthFirst => left.len().cmp(&right.len()).then_with(|| left.cmp(right)),
            Self::Lexicographical => left.cmp(right),
        }
    }
}
#[derive(Clone, Copy)]
enum MemberPolicy {
    Preserve,
    LastWins,
}

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
    fn len(&self) -> usize {
        self.0.len()
    }
    fn try_reserve(
        &mut self,
        count: usize,
    ) -> std::result::Result<(), std::collections::TryReserveError> {
        self.0.try_reserve(count)
    }
    fn position(&self, key: &[u8], order: KeyOrder) -> std::result::Result<usize, usize> {
        let (mut low, mut high) = (0, self.0.len());
        while low < high {
            let middle = low + (high - low) / 2;
            match order.compare(&self.0[middle].0, key) {
                std::cmp::Ordering::Less => low = middle + 1,
                std::cmp::Ordering::Greater => high = middle,
                std::cmp::Ordering::Equal => return Ok(middle),
            }
        }
        Err(low)
    }
    fn insert(&mut self, key: Vec<u8>, value: NodeId, order: KeyOrder) -> Option<NodeId> {
        match self.position(&key, order) {
            Ok(index) => Some(std::mem::replace(&mut self.0[index].1, value)),
            Err(index) => {
                self.0.insert(index, (key, value));
                None
            }
        }
    }
    fn remove(&mut self, key: &[u8], order: KeyOrder) -> Option<NodeId> {
        self.position(key, order)
            .ok()
            .map(|index| self.0.remove(index).1)
    }
}
impl IntoIterator for Members {
    type Item = (Vec<u8>, NodeId);
    type IntoIter = std::vec::IntoIter<Self::Item>;
    fn into_iter(self) -> Self::IntoIter {
        self.0.into_iter()
    }
}

pub struct OwnedValue {
    nodes: Vec<Node>,
    free: Vec<NodeId>,
    root: NodeId,
    order: KeyOrder,
}

enum Decode<'a> {
    Visit(BinaryValue<'a>),
    Owned(Document),
    Leave(usize),
    Array(usize),
    Object(Vec<(Vec<u8>, NodeId)>),
}

struct Pair {
    left: NodeId,
    right: NodeId,
    destination: Option<(NodeId, Vec<u8>)>,
}

impl OwnedValue {
    pub(crate) fn is_object(&self) -> bool {
        matches!(&self.nodes[self.root], Node::Object(_))
    }

    pub(crate) fn reorder(&mut self, order: KeyOrder) {
        if self.order != order {
            for node in &mut self.nodes {
                if let Node::Object(members) = node {
                    members.0.sort_by(|(a, _), (b, _)| order.compare(a, b));
                }
            }
            self.order = order;
        }
    }

    fn insert_member(&mut self, parent: NodeId, key: Vec<u8>, value: NodeId) -> Option<NodeId> {
        let order = self.order;
        self.object_mut(parent).insert(key, value, order)
    }
    fn remove_member(&mut self, parent: NodeId, key: &[u8]) -> Option<NodeId> {
        let order = self.order;
        self.object_mut(parent).remove(key, order)
    }

    fn add(&mut self, node: Node) -> Result<NodeId> {
        if let Some(index) = self.free.pop() {
            self.nodes[index] = node;
            Ok(index)
        } else {
            self.nodes
                .try_reserve(1)
                .map_err(|_| ValueError::Allocation)?;
            let index = self.nodes.len();
            self.nodes.push(node);
            Ok(index)
        }
    }

    fn take(&mut self, index: NodeId) -> Result<Node> {
        self.free
            .try_reserve(1)
            .map_err(|_| ValueError::Allocation)?;
        let node = std::mem::replace(&mut self.nodes[index], Node::Vacant);
        self.free.push(index);
        Ok(node)
    }

    fn discard(&mut self, root: NodeId) -> Result<()> {
        let mut pending = Vec::new();
        pending.try_reserve(1).map_err(|_| ValueError::Allocation)?;
        pending.push(root);
        while let Some(index) = pending.pop() {
            match self.take(index)? {
                Node::Array(values) => {
                    pending
                        .try_reserve(values.len())
                        .map_err(|_| ValueError::Allocation)?;
                    pending.extend(values);
                }
                Node::Object(values) => {
                    pending
                        .try_reserve(values.len())
                        .map_err(|_| ValueError::Allocation)?;
                    pending.extend(values.0.into_iter().map(|(_, value)| value));
                }
                Node::Scalar(_) | Node::BinaryScalar(_) => {}
                Node::Vacant => {}
            }
        }
        Ok(())
    }

    pub fn from_binary(value: BinaryValue<'_>) -> Result<Self> {
        Self::build(
            Decode::Visit(value),
            if value.lexicographical() {
                KeyOrder::Lexicographical
            } else {
                KeyOrder::LengthFirst
            },
            MemberPolicy::Preserve,
        )
    }

    pub fn from_document(document: Document) -> Result<Self> {
        Self::build(
            Decode::Owned(document),
            KeyOrder::LengthFirst,
            MemberPolicy::Preserve,
        )
    }

    pub(crate) fn merge_document(document: Document) -> Result<Self> {
        Self::build(
            Decode::Owned(document),
            KeyOrder::LengthFirst,
            MemberPolicy::LastWins,
        )
    }
    pub(crate) fn merge_binary(value: BinaryValue<'_>) -> Result<Self> {
        Self::build(
            Decode::Visit(value),
            KeyOrder::LengthFirst,
            MemberPolicy::LastWins,
        )
    }

    fn build(initial: Decode<'_>, order: KeyOrder, members: MemberPolicy) -> Result<Self> {
        let remaining = match &initial {
            Decode::Visit(value) => value.traversal_limit(),
            _ => usize::MAX,
        };
        let mut arena = Self {
            nodes: Vec::new(),
            free: Vec::new(),
            root: 0,
            order,
        };
        let mut active = HashSet::new();
        let mut remaining = remaining;
        let mut tasks = Vec::new();
        let mut completed = Vec::new();
        tasks.try_reserve(1).map_err(|_| ValueError::Allocation)?;
        tasks.push(initial);
        while let Some(task) = tasks.pop() {
            let node = match task {
                Decode::Owned(document) => match document {
                    Document::Array(values) => {
                        tasks
                            .try_reserve(values.len().saturating_add(1))
                            .map_err(|_| ValueError::Allocation)?;
                        tasks.push(Decode::Array(values.len()));
                        tasks.extend(values.into_iter().rev().map(Decode::Owned));
                        continue;
                    }
                    Document::Object(values) => {
                        tasks
                            .try_reserve(values.len().saturating_add(1))
                            .map_err(|_| ValueError::Allocation)?;
                        let marker = tasks.len();
                        tasks.push(Decode::Array(0));
                        let values = values.into_iter().map(|(key, value)| {
                            tasks.push(Decode::Owned(value));
                            (key.into_bytes(), 0)
                        }).collect();
                        tasks[marker + 1..].reverse();
                        tasks[marker] = Decode::Object(values);
                        continue;
                    }
                    scalar => Node::Scalar(scalar),
                },
                Decode::Visit(value) => {
                    if remaining == 0 {
                        return Err(BinaryError::new(
                            BinaryErrorKind::InvalidOffset,
                            value.storage_offset(),
                        )
                        .into());
                    }
                    remaining -= 1;
                    let kind = value.json_type()?;
                    if kind.is_container() {
                        if !active.insert(value.storage_offset()) {
                            return Err(BinaryError::new(
                                BinaryErrorKind::InvalidOffset,
                                value.storage_offset(),
                            )
                            .into());
                        }
                        tasks.push(Decode::Leave(value.storage_offset()));
                        let entries = value.entries()?;
                        let count = entries.len();
                        tasks
                            .try_reserve(count.saturating_add(1))
                            .map_err(|_| ValueError::Allocation)?;
                        let marker = tasks.len();
                        tasks.push(Decode::Array(count));
                        let mut keys = Vec::new();
                        if kind == JsonType::Object {
                            keys.try_reserve_exact(count)
                                .map_err(|_| ValueError::Allocation)?;
                        }
                        for entry in entries.rev() {
                            let (key, value) = entry?;
                            if kind == JsonType::Object {
                                keys.push((copy_bytes(key)?, 0));
                            }
                            tasks.push(Decode::Visit(value));
                        }
                        if kind == JsonType::Object {
                            keys.reverse();
                            tasks[marker] = Decode::Object(keys);
                        }
                        continue;
                    }
                    Self::scalar_node(value)?
                }
                Decode::Leave(offset) => {
                    active.remove(&offset);
                    continue;
                }
                Decode::Array(count) => {
                    let mut values = Vec::new();
                    values
                        .try_reserve_exact(count)
                        .map_err(|_| ValueError::Allocation)?;
                    values.extend(completed.drain(completed.len() - count..));
                    Node::Array(values)
                }
                Decode::Object(mut values) => {
                    let start = completed.len() - values.len();
                    for ((_, slot), value) in values.iter_mut().zip(completed.drain(start..)) {
                        *slot = value;
                    }
                    values.sort_by(|(left, _), (right, _)| order.compare(left, right));
                    if matches!(members, MemberPolicy::LastWins) {
                        for index in 0..values.len().saturating_sub(1) {
                            if values[index].0 == values[index + 1].0 {
                                arena.discard(values[index].1)?;
                            }
                        }
                        values.dedup_by(|next, previous| {
                            if next.0 != previous.0 {
                                return false;
                            }
                            previous.1 = next.1;
                            true
                        });
                    }
                    Node::Object(Members(values))
                }
            };
            completed
                .try_reserve(1)
                .map_err(|_| ValueError::Allocation)?;
            completed.push(arena.add(node)?);
        }
        arena.root = completed[0];
        Ok(arena)
    }

    fn scalar_node(value: BinaryValue<'_>) -> Result<Node> {
        Ok(match value.json_type()? {
            JsonType::Null => Node::Scalar(Document::Null),
            JsonType::Boolean => Node::Scalar(Document::Bool(value.boolean()?)),
            JsonType::Int | JsonType::Uint | JsonType::Double => {
                Node::Scalar(Document::Number(value.number()?))
            }
            JsonType::String => match std::str::from_utf8(value.string_bytes()?) {
                Ok(value) => Node::Scalar(Document::String(value.to_owned())),
                Err(_) => Node::BinaryScalar(BinaryDocument::new(binary::serialize_value(value)?)?),
            },
            _ => Node::BinaryScalar(BinaryDocument::new(binary::serialize_value(value)?)?),
        })
    }

    fn import_binary(&mut self, value: BinaryValue<'_>) -> Result<NodeId> {
        if value.json_type()?.is_container() {
            self.import(Self::from_binary(value)?)
        } else {
            let node = Self::scalar_node(value)?;
            self.add(node)
        }
    }

    fn import(&mut self, mut other: Self) -> Result<NodeId> {
        other.reorder(self.order);
        let mut ids = Vec::new();
        ids.try_reserve_exact(other.nodes.len())
            .map_err(|_| ValueError::Allocation)?;
        for node in &other.nodes {
            ids.push(if matches!(node, Node::Vacant) {
                usize::MAX
            } else {
                self.add(Node::Vacant)?
            });
        }
        let root = ids[other.root];
        for (index, mut node) in other.nodes.into_iter().enumerate() {
            match &mut node {
                Node::Array(values) => values.iter_mut().for_each(|value| *value = ids[*value]),
                Node::Object(values) => values
                    .0
                    .iter_mut()
                    .for_each(|(_, value)| *value = ids[*value]),
                Node::Scalar(_) | Node::BinaryScalar(_) => {}
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
        match &mut self.nodes[index] {
            Node::Object(value) => value,
            _ => unreachable!("object merge destination"),
        }
    }

    pub(crate) fn merge(mut self, other: Self, mode: MergeStyle) -> Result<Self> {
        let right = self.import(other)?;
        let mut pairs = Vec::new();
        pairs.try_reserve(1).map_err(|_| ValueError::Allocation)?;
        pairs.push(Pair {
            left: self.root,
            right,
            destination: None,
        });
        while let Some(pair) = pairs.pop() {
            let result = if matches!(mode, MergeStyle::Preserve) {
                self.preserve_pair(pair.left, pair.right, &mut pairs)?
            } else {
                self.patch_pair(pair.left, pair.right, &mut pairs)?
            };
            if let Some((parent, key)) = pair.destination {
                self.insert_member(parent, key, result);
            } else {
                self.root = result;
            }
        }
        Ok(self)
    }

    fn preserve_pair(
        &mut self,
        left: NodeId,
        right: NodeId,
        pairs: &mut Vec<Pair>,
    ) -> Result<NodeId> {
        if matches!(
            (&self.nodes[left], &self.nodes[right]),
            (Node::Object(_), Node::Object(_))
        ) {
            let Node::Object(values) = self.take(right)? else {
                unreachable!()
            };
            self.object_mut(left)
                .try_reserve(values.len())
                .map_err(|_| ValueError::Allocation)?;
            pairs
                .try_reserve(values.len())
                .map_err(|_| ValueError::Allocation)?;
            for (key, value) in values {
                if let Some(previous) = self.remove_member(left, &key) {
                    pairs.push(Pair {
                        left: previous,
                        right: value,
                        destination: Some((left, key)),
                    });
                } else {
                    self.insert_member(left, key, value);
                }
            }
            return Ok(left);
        }
        let left_array = matches!(&self.nodes[left], Node::Array(_));
        let right_array = matches!(&self.nodes[right], Node::Array(_));
        if left_array && right_array {
            let Node::Array(values) = self.take(right)? else {
                unreachable!()
            };
            let Node::Array(previous) = &mut self.nodes[left] else {
                unreachable!()
            };
            previous
                .try_reserve(values.len())
                .map_err(|_| ValueError::Allocation)?;
            previous.extend(values);
            Ok(left)
        } else if left_array {
            let Node::Array(previous) = &mut self.nodes[left] else {
                unreachable!()
            };
            previous
                .try_reserve(1)
                .map_err(|_| ValueError::Allocation)?;
            previous.push(right);
            Ok(left)
        } else if right_array {
            let Node::Array(values) = &mut self.nodes[right] else {
                unreachable!()
            };
            values.try_reserve(1).map_err(|_| ValueError::Allocation)?;
            values.insert(0, left);
            Ok(right)
        } else {
            let mut values = Vec::new();
            values
                .try_reserve_exact(2)
                .map_err(|_| ValueError::Allocation)?;
            values.extend([left, right]);
            self.add(Node::Array(values))
        }
    }

    fn patch_pair(
        &mut self,
        mut left: NodeId,
        right: NodeId,
        pairs: &mut Vec<Pair>,
    ) -> Result<NodeId> {
        if !matches!(&self.nodes[right], Node::Object(_)) {
            self.discard(left)?;
            return Ok(right);
        }
        let Node::Object(values) = self.take(right)? else {
            unreachable!()
        };
        if !matches!(&self.nodes[left], Node::Object(_)) {
            self.discard(left)?;
            left = self.add(Node::Object(Members::default()))?;
        }
        self.object_mut(left)
            .try_reserve(values.len())
            .map_err(|_| ValueError::Allocation)?;
        pairs
            .try_reserve(values.len())
            .map_err(|_| ValueError::Allocation)?;
        for (key, value) in values {
            if self.is_null(value) {
                self.discard(value)?;
                if let Some(previous) = self.remove_member(left, &key) {
                    self.discard(previous)?;
                }
            } else {
                let previous = match self.remove_member(left, &key) {
                    Some(previous) => previous,
                    None => self.add(Node::Object(Members::default()))?,
                };
                pairs.push(Pair {
                    left: previous,
                    right: value,
                    destination: Some((left, key)),
                });
            }
        }
        Ok(left)
    }

    fn node_at(&self, path: &[u64]) -> Result<NodeId> {
        let mut node = self.root;
        for &index in path {
            node = self.child_id(node, index)?;
        }
        Ok(node)
    }

    fn child_id(&self, node: NodeId, index: u64) -> Result<NodeId> {
        let index = usize::try_from(index).map_err(|_| self.invalid(node))?;
        let child = match self.nodes.get(node) {
            Some(Node::Array(values)) => values.get(index).copied(),
            Some(Node::Object(values)) => values.0.get(index).map(|(_, value)| *value),
            _ => None,
        };
        child.ok_or_else(|| self.invalid(node))
    }

    fn invalid(&self, node: NodeId) -> ValueError {
        BinaryError::new(BinaryErrorKind::InvalidOffset, node).into()
    }

    fn assign(&mut self, path: &[u64], value: NodeId) -> Result<()> {
        if let Some((&index, parent)) = path.split_last() {
            let parent = self.node_at(parent)?;
            let index = usize::try_from(index).map_err(|_| self.invalid(parent))?;
            let slot = match &mut self.nodes[parent] {
                Node::Array(values) => values.get_mut(index),
                Node::Object(values) => values.0.get_mut(index).map(|(_, value)| value),
                _ => None,
            }
            .ok_or_else(|| {
                ValueError::Binary(BinaryError::new(BinaryErrorKind::InvalidOffset, parent))
            })?;
            *slot = value;
        } else {
            self.root = value;
            self.reorder(KeyOrder::LengthFirst);
        }
        Ok(())
    }

    pub(crate) fn apply_action(
        mut self,
        action: super::sql::Action,
        argument: Option<BinaryValue<'_>>,
    ) -> Result<Self> {
        use super::sql::Action;
        let argument = if matches!(
            action,
            Action::Replace(_) | Action::Insert { .. } | Action::Wrap { .. }
        ) {
            argument
                .map(|value| self.import_binary(value))
                .transpose()?
        } else {
            None
        };
        match action {
            Action::None | Action::Evaluate => {}
            Action::Replace(path) => {
                let previous = self.node_at(&path)?;
                let value = argument.ok_or_else(|| self.invalid(previous))?;
                self.assign(&path, value)?;
                self.discard(previous)?;
            }
            Action::Insert { parent, index, key } => {
                let parent = self.node_at(&parent)?;
                let value = argument.ok_or_else(|| self.invalid(parent))?;
                match &mut self.nodes[parent] {
                    Node::Array(values) => {
                        let index = usize::try_from(index).map_err(|_| {
                            ValueError::Binary(BinaryError::new(
                                BinaryErrorKind::InvalidOffset,
                                parent,
                            ))
                        })?;
                        if index > values.len() {
                            return Err(self.invalid(parent));
                        }
                        values.try_reserve(1).map_err(|_| ValueError::Allocation)?;
                        values.insert(index, value);
                    }
                    Node::Object(members) => {
                        members.try_reserve(1).map_err(|_| ValueError::Allocation)?;
                        if let Some(previous) = members.insert(key, value, self.order) {
                            self.discard(previous)?;
                        }
                    }
                    _ => return Err(self.invalid(parent)),
                }
            }
            Action::Remove { parent, index } => {
                let parent = self.node_at(&parent)?;
                let index = usize::try_from(index).map_err(|_| self.invalid(parent))?;
                let previous = match &mut self.nodes[parent] {
                    Node::Array(values) if index < values.len() => values.remove(index),
                    Node::Object(members) if index < members.len() => members.0.remove(index).1,
                    _ => return Err(self.invalid(parent)),
                };
                self.discard(previous)?;
            }
            Action::Wrap { target, prepend } => {
                let previous = self.node_at(&target)?;
                let value = argument.ok_or_else(|| self.invalid(previous))?;
                let mut values = Vec::new();
                values
                    .try_reserve_exact(2)
                    .map_err(|_| ValueError::Allocation)?;
                if prepend {
                    values.extend([value, previous]);
                } else {
                    values.extend([previous, value]);
                }
                let array = self.add(Node::Array(values))?;
                self.assign(&target, array)?;
            }
        }
        Ok(self)
    }

    pub fn encode(self) -> Result<Vec<u8>> {
        binary::serialize_constructed_source(&self, self.root).map_err(ValueError::from)
    }
}

fn copy_bytes(bytes: &[u8]) -> Result<Vec<u8>> {
    let mut output = Vec::new();
    output
        .try_reserve_exact(bytes.len())
        .map_err(|_| ValueError::Allocation)?;
    output.extend_from_slice(bytes);
    Ok(output)
}

impl binary::ConstructedSource for OwnedValue {
    const CHECK_CYCLES: bool = false;
    type Value = NodeId;
    fn lexicographical(&self) -> bool {
        self.order == KeyOrder::Lexicographical
    }
    fn identity(&self, value: NodeId) -> (usize, usize) {
        (0, value)
    }
    fn traversal_limit(&self) -> usize {
        self.nodes.len().saturating_add(1)
    }
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
            Node::Object(values) => Ok(values
                .0
                .iter()
                .map(|(key, value)| (key.as_slice(), *value))
                .collect()),
            _ => Err(BinaryError::new(BinaryErrorKind::InvalidType, index)),
        }
    }
}

impl super::sql::MutationView for OwnedValue {
    type Node = NodeId;
    fn root(&self) -> NodeId {
        self.root
    }
    fn kind(&self, node: NodeId) -> super::editor::Result<JsonType> {
        Ok(binary::ConstructedSource::kind(self, node)?)
    }
    fn len(&self, node: NodeId) -> super::editor::Result<u64> {
        match &self.nodes[node] {
            Node::Array(values) => Ok(values.len() as u64),
            Node::Object(values) => Ok(values.len() as u64),
            _ => Err(super::editor::EditError::InvalidIndex),
        }
    }
    fn child(&self, node: NodeId, index: u64) -> super::editor::Result<NodeId> {
        Ok(self.child_id(node, index)?)
    }
    fn member(&self, node: NodeId, key: &[u8]) -> super::editor::Result<Option<(u64, NodeId)>> {
        let Node::Object(members) = &self.nodes[node] else {
            return Err(super::editor::EditError::InvalidIndex);
        };
        Ok(members
            .position(key, self.order)
            .ok()
            .map(|index| (index as u64, members.0[index].1)))
    }
}
