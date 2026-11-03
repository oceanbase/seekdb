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
use crate::path::{ArrayIndex, Path, PathStep};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SqlOperation {
    Set,
    Insert,
    Replace,
    Remove,
    ArrayAppend,
    ArrayInsert,
}

pub struct PreparedEdit {
    generation: u64,
    operation: SqlOperation,
    action: Action,
}

enum Action {
    None,
    Evaluate,
    Replace(Vec<u64>),
    Insert {
        parent: Vec<u64>,
        index: u64,
        key: Vec<u8>,
    },
    Remove {
        parent: Vec<u64>,
        index: u64,
    },
    Wrap {
        target: Vec<u64>,
        prepend: bool,
    },
}

impl PreparedEdit {
    /// SQL SET/REPLACE evaluate their value argument even when no path matches.
    pub fn needs_value(&self) -> bool {
        matches!(self.operation, SqlOperation::Set | SqlOperation::Replace)
            || !matches!(self.action, Action::None | Action::Remove { .. })
    }
}

impl<S: BinarySource + ?Sized> Editor<'_, S> {
    pub fn prepare(&self, path: &Path, operation: SqlOperation) -> Result<PreparedEdit> {
        let steps = path.steps();
        if path.can_match_many() {
            return Err(EditError::InvalidPath);
        }
        if operation == SqlOperation::Remove && steps.is_empty() {
            return Err(EditError::VacuousPath);
        }
        if operation == SqlOperation::ArrayInsert
            && !matches!(steps.last(), Some(PathStep::ArrayCell(_)))
        {
            return Err(EditError::InvalidPath);
        }
        let action = if operation == SqlOperation::ArrayInsert {
            let Some((node, parent)) = self.select(&steps[..steps.len() - 1], false)? else {
                return Ok(self.prepared(operation, Action::None));
            };
            if self.kind(node)? == JsonType::Array {
                let PathStep::ArrayCell(cell) = steps.last().unwrap() else {
                    unreachable!()
                };
                Action::Insert {
                    index: insertion_index(*cell, self.container(node)?.count),
                    parent,
                    key: Vec::new(),
                }
            } else {
                Action::None
            }
        } else if let Some((node, target)) = self.select(steps, true)? {
            match operation {
                SqlOperation::Set | SqlOperation::Replace => Action::Replace(target),
                SqlOperation::Insert => Action::None,
                SqlOperation::ArrayAppend => {
                    if self.kind(node)? == JsonType::Array {
                        Action::Insert {
                            index: self.container(node)?.count,
                            parent: target,
                            key: Vec::new(),
                        }
                    } else {
                        Action::Wrap {
                            target,
                            prepend: false,
                        }
                    }
                }
                SqlOperation::Remove => {
                    if let Some((&child_index, parent)) = target.split_last() {
                        let (container, _) = self.locate(parent)?;
                        match (self.kind(container.node)?, steps.last().unwrap()) {
                            (JsonType::Object, PathStep::Member(_)) => Action::Remove {
                                parent: parent.to_vec(),
                                index: child_index,
                            },
                            (JsonType::Array, PathStep::ArrayCell(cell)) => {
                                match existing_index(*cell, container.count) {
                                    Some(index) => Action::Remove {
                                        parent: parent.to_vec(),
                                        index,
                                    },
                                    None => Action::None,
                                }
                            }
                            _ => Action::None,
                        }
                    } else {
                        Action::None
                    }
                }
                SqlOperation::ArrayInsert => unreachable!(),
            }
        } else if matches!(operation, SqlOperation::Set | SqlOperation::Insert) && !steps.is_empty()
        {
            let Some((node, parent)) = self.select(&steps[..steps.len() - 1], true)? else {
                return Ok(self.prepared(operation, Action::None));
            };
            match steps.last().unwrap() {
                PathStep::Member(key) if self.kind(node)? == JsonType::Object => Action::Insert {
                    parent,
                    index: 0,
                    key: key.as_bytes().to_vec(),
                },
                PathStep::ArrayCell(cell) if self.kind(node)? == JsonType::Array => {
                    Action::Insert {
                        index: insertion_index(*cell, self.container(node)?.count),
                        parent,
                        key: Vec::new(),
                    }
                }
                PathStep::ArrayCell(cell) if existing_index(*cell, 1).is_none() => Action::Wrap {
                    target: parent,
                    prepend: operation == SqlOperation::Set && cell.from_end,
                },
                _ if operation == SqlOperation::Insert => Action::Evaluate,
                _ => Action::None,
            }
        } else {
            Action::None
        };
        Ok(self.prepared(operation, action))
    }

    pub fn apply(&mut self, prepared: PreparedEdit, value: Option<BinaryValue<'_>>) -> Result<()> {
        if prepared.generation != self.generation {
            return Err(EditError::StalePlan);
        }
        if prepared.needs_value() && value.is_none() {
            return Err(EditError::InvalidIndex);
        }
        match prepared.action {
            Action::None | Action::Evaluate => Ok(()),
            Action::Replace(target) => {
                let order = !target.is_empty() && self.lexicographical;
                self.replace_selected(&target, Value::from_binary(value.unwrap(), order)?)
            }
            Action::Insert { parent, index, key } => {
                self.insert(&parent, index, &key, value.unwrap())
            }
            Action::Remove { parent, index } => self.remove(&parent, index),
            Action::Wrap { target, prepend } => {
                let source_order = self.lexicographical;
                let output_order = !target.is_empty() && source_order;
                let value = Value::from_binary(value.unwrap(), source_order)?;
                self.atomic(|editor| {
                    let old = editor.selected_value(&target)?;
                    let array = if prepend {
                        array_value(&[&value, &old], source_order, output_order)?
                    } else {
                        array_value(&[&old, &value], source_order, output_order)?
                    };
                    editor.replace_selected_inner(&target, array)
                })
            }
        }
    }

    fn prepared(&self, operation: SqlOperation, action: Action) -> PreparedEdit {
        PreparedEdit {
            generation: self.generation,
            operation,
            action,
        }
    }

    fn select(&self, steps: &[PathStep], auto_wrap: bool) -> Result<Option<(Node, Vec<u64>)>> {
        let mut node = self.root;
        let mut target = Vec::new();
        for step in steps {
            match step {
                PathStep::Member(key) if self.kind(node)? == JsonType::Object => {
                    let container = self.container(node)?;
                    let (found, index) = self.find_key(container, key.as_bytes())?;
                    if !found {
                        return Ok(None);
                    }
                    node = self.child(container, index)?;
                    target.push(index);
                }
                PathStep::ArrayCell(cell) if self.kind(node)? == JsonType::Array => {
                    let container = self.container(node)?;
                    let Some(index) = existing_index(*cell, container.count) else {
                        return Ok(None);
                    };
                    node = self.child(container, index)?;
                    target.push(index);
                }
                PathStep::ArrayCell(cell) if auto_wrap && existing_index(*cell, 1).is_some() => {}
                PathStep::Member(_) | PathStep::ArrayCell(_) => return Ok(None),
                _ => return Err(EditError::InvalidPath),
            }
        }
        Ok(Some((node, target)))
    }

    fn selected_value(&self, target: &[u64]) -> Result<Value> {
        let mut node = self.root;
        for index in target {
            node = self.child(self.container(node)?, *index)?;
        }
        self.rebuild(node)
    }

    fn replace_selected(&mut self, target: &[u64], value: Value) -> Result<()> {
        self.atomic(|editor| editor.replace_selected_inner(target, value))
    }

    fn replace_selected_inner(&mut self, target: &[u64], value: Value) -> Result<()> {
        if let Some((&index, parent)) = target.split_last() {
            let (container, parents) = self.locate(parent)?;
            self.replace_child(container, &parents, index, value)
        } else {
            self.reset_document(value)
        }
    }
}

fn existing_index(index: ArrayIndex, length: u64) -> Option<u64> {
    if index.value >= length {
        None
    } else if index.from_end {
        Some(length - index.value - 1)
    } else {
        Some(index.value)
    }
}
fn insertion_index(index: ArrayIndex, length: u64) -> u64 {
    if index.from_end {
        length.saturating_sub(index.value.saturating_add(1))
    } else {
        index.value.min(length)
    }
}

fn array_value(values: &[&Value], source_order: bool, output_order: bool) -> Result<Value> {
    let mut documents = Vec::new();
    for value in values {
        let mut bytes = Vec::new();
        if matches!(value.raw_type, 6 | 7) {
            bytes.resize(8, 0);
            bytes.extend_from_slice(&value.bytes);
            let header = 30 | (u64::from(source_order) << 8) | ((bytes.len() as u64) << 22);
            bytes[..8].copy_from_slice(&header.to_le_bytes());
        } else {
            if !binary::needs_type_prefix(value.raw_type) {
                bytes.push(value.raw_type);
            }
            bytes.extend_from_slice(&value.bytes);
        }
        documents.push(crate::BinaryDocument::new(bytes)?);
    }
    let roots = documents
        .iter()
        .map(|document| document.root())
        .collect::<std::result::Result<Vec<_>, _>>()?;
    let bytes = binary::serialize_array(&roots)?;
    let document = crate::BinaryDocument::new(bytes)?;
    Value::from_binary(document.root()?, output_order)
}

/// An owned overlay that borrows its stable source only for each synchronous call.
pub struct Session {
    snapshot: Option<Snapshot>,
}

struct Snapshot {
    length: u64,
    extension: u64,
    lexicographical: bool,
    generation: u64,
    logical_diffs: bool,
    root: Node,
    writes: Vec<Write>,
    delta: Delta,
}

impl Session {
    pub fn open<S: BinarySource + ?Sized>(source: &S) -> Result<Self> {
        Ok(Self {
            snapshot: Some(Editor::open(source)?.detach()),
        })
    }

    pub fn open_for_rebuild<S: BinarySource + ?Sized>(source: &S) -> Result<Self> {
        let mut editor = Editor::open(source)?;
        editor.logical_diffs = false;
        Ok(Self {
            snapshot: Some(editor.detach()),
        })
    }

    pub fn prepare<S: BinarySource + ?Sized>(
        &mut self,
        source: &S,
        path: &Path,
        operation: SqlOperation,
    ) -> Result<PreparedEdit> {
        self.with_source(source, |editor| editor.prepare(path, operation))
    }

    pub fn apply<S: BinarySource + ?Sized>(
        &mut self,
        source: &S,
        prepared: PreparedEdit,
        value: Option<BinaryValue<'_>>,
    ) -> Result<()> {
        self.with_source(source, |editor| editor.apply(prepared, value))
    }

    pub fn binary<S: BinarySource + ?Sized>(&mut self, source: &S) -> Result<Vec<u8>> {
        self.with_source(source, |editor| {
            let value = editor.selected_value(&[])?;
            let mut bytes = Vec::new();
            if matches!(value.raw_type, 6 | 7) {
                bytes.resize(8, 0);
                bytes.extend_from_slice(&value.bytes);
                let header =
                    30 | (u64::from(editor.lexicographical) << 8) | ((bytes.len() as u64) << 22);
                bytes[..8].copy_from_slice(&header.to_le_bytes());
            } else {
                if !binary::needs_type_prefix(value.raw_type) {
                    bytes.push(value.raw_type);
                }
                bytes.extend_from_slice(&value.bytes);
            }
            Ok(bytes)
        })
    }

    pub fn constructed_binary<S: BinarySource + ?Sized>(&mut self, source: &S) -> Result<Vec<u8>> {
        self.with_source(source, |editor| {
            let bytes = editor.read(0, usize::try_from(editor.length)
                .map_err(|_| EditError::InvalidIndex)?)?;
            let document = crate::BinaryDocument::new(bytes.as_ref())?;
            Ok(binary::serialize_constructed_value(document.root()?)?)
        })
    }

    pub fn should_pack(&self, prior: &Delta) -> Result<bool> {
        self.should_pack_with(prior.state())
    }

    pub fn should_pack_with(&self, prior: crate::mutation::UpdateState) -> Result<bool> {
        use crate::mutation::{should_pack_state, UpdateState};
        let snapshot = self.snapshot.as_ref().unwrap();
        if !snapshot.logical_diffs || snapshot.root.offset == 0 || snapshot.extension == 0 {
            return Ok(false);
        }
        let rebuilt = snapshot.delta.is_rebuilt() || prior == UpdateState::Rebuilt;
        let unchanged = snapshot.delta.is_unchanged() && prior == UpdateState::Unchanged;
        Ok(should_pack_state(
            UpdateState::from_flags(rebuilt, unchanged),
            snapshot.length,
            snapshot.extension,
            snapshot.root.offset,
        )?)
    }

    pub fn finish(self) -> Update {
        let snapshot = self.snapshot.unwrap();
        Update {
            writes: snapshot.writes,
            delta: snapshot.delta,
        }
    }

    fn with_source<S: BinarySource + ?Sized, T>(
        &mut self,
        source: &S,
        apply: impl FnOnce(&mut Editor<'_, S>) -> Result<T>,
    ) -> Result<T> {
        let snapshot = self.snapshot.take().unwrap();
        let mut editor = Editor {
            source,
            length: snapshot.length,
            extension: snapshot.extension,
            lexicographical: snapshot.lexicographical,
            generation: snapshot.generation,
            logical_diffs: snapshot.logical_diffs,
            root: snapshot.root,
            writes: snapshot.writes,
            delta: snapshot.delta,
        };
        let result = apply(&mut editor);
        self.snapshot = Some(editor.detach());
        result
    }
}

impl<S: BinarySource + ?Sized> Editor<'_, S> {
    fn detach(self) -> Snapshot {
        Snapshot {
            length: self.length,
            extension: self.extension,
            lexicographical: self.lexicographical,
            generation: self.generation,
            logical_diffs: self.logical_diffs,
            root: self.root,
            writes: self.writes,
            delta: self.delta,
        }
    }
}
