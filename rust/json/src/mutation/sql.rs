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

use super::{
    editor::{EditError, Result},
    owned::OwnedValue,
};
use crate::path::{ArrayIndex, Path, PathStep};
use crate::{BinaryValue, JsonType};

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
    pub(crate) generation: u64,
    operation: SqlOperation,
    pub(crate) action: Action,
}

pub(crate) enum Action {
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

pub(crate) trait MutationView {
    type Node: Copy;
    fn root(&self) -> Self::Node;
    fn kind(&self, node: Self::Node) -> Result<JsonType>;
    fn len(&self, node: Self::Node) -> Result<u64>;
    fn child(&self, node: Self::Node, index: u64) -> Result<Self::Node>;
    fn member(&self, node: Self::Node, key: &[u8]) -> Result<Option<(u64, Self::Node)>>;
}

pub(crate) fn node_at<V: MutationView>(view: &V, path: &[u64]) -> Result<V::Node> {
    let mut node = view.root();
    for &index in path {
        node = view.child(node, index)?;
    }
    Ok(node)
}

pub(crate) fn prepare<V: MutationView>(
    view: &V,
    generation: u64,
    path: &Path,
    operation: SqlOperation,
) -> Result<PreparedEdit> {
    Planner { view, generation }.prepare(path, operation)
}

struct Planner<'a, V> {
    view: &'a V,
    generation: u64,
}
impl<V: MutationView> Planner<'_, V> {
    fn kind(&self, node: V::Node) -> Result<JsonType> {
        self.view.kind(node)
    }
    fn prepare(&self, path: &Path, operation: SqlOperation) -> Result<PreparedEdit> {
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
                    index: insertion_index(*cell, self.view.len(node)?),
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
                            index: self.view.len(node)?,
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
                        let node = node_at(self.view, parent)?;
                        match (self.kind(node)?, steps.last().unwrap()) {
                            (JsonType::Object, PathStep::Member(_)) => Action::Remove {
                                parent: parent.to_vec(),
                                index: child_index,
                            },
                            (JsonType::Array, PathStep::ArrayCell(cell)) => {
                                match existing_index(*cell, self.view.len(node)?) {
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
                        index: insertion_index(*cell, self.view.len(node)?),
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

    fn prepared(&self, operation: SqlOperation, action: Action) -> PreparedEdit {
        PreparedEdit {
            generation: self.generation,
            operation,
            action,
        }
    }

    fn select(&self, steps: &[PathStep], auto_wrap: bool) -> Result<Option<(V::Node, Vec<u64>)>> {
        let mut node = self.view.root();
        let mut target = Vec::new();
        for step in steps {
            match step {
                PathStep::Member(key) if self.kind(node)? == JsonType::Object => {
                    let Some((index, child)) = self.view.member(node, key.as_bytes())? else {
                        return Ok(None);
                    };
                    node = child;
                    target.push(index);
                }
                PathStep::ArrayCell(cell) if self.kind(node)? == JsonType::Array => {
                    let Some(index) = existing_index(*cell, self.view.len(node)?) else {
                        return Ok(None);
                    };
                    node = self.view.child(node, index)?;
                    target.push(index);
                }
                PathStep::ArrayCell(cell) if auto_wrap && existing_index(*cell, 1).is_some() => {}
                PathStep::Member(_) | PathStep::ArrayCell(_) => return Ok(None),
                _ => return Err(EditError::InvalidPath),
            }
        }
        Ok(Some((node, target)))
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

pub struct OwnedEditor {
    value: OwnedValue,
    generation: u64,
}
impl OwnedEditor {
    pub fn from_document(document: crate::Document) -> Result<Self> {
        Ok(Self {
            value: OwnedValue::from_document(document)?,
            generation: 0,
        })
    }
    pub fn from_binary(value: BinaryValue<'_>) -> Result<Self> {
        Ok(Self {
            value: OwnedValue::from_binary(value)?,
            generation: 0,
        })
    }
    pub fn prepare(&self, path: &Path, operation: SqlOperation) -> Result<PreparedEdit> {
        prepare(&self.value, self.generation, path, operation)
    }
    pub fn apply(mut self, prepared: PreparedEdit, value: Option<BinaryValue<'_>>) -> Result<Self> {
        if prepared.generation != self.generation {
            return Err(EditError::StalePlan);
        }
        if prepared.needs_value() && value.is_none() {
            return Err(EditError::InvalidIndex);
        }
        if matches!(prepared.action, Action::None | Action::Evaluate) {
            return Ok(self);
        }
        self.value = self.value.apply_action(prepared.action, value)?;
        self.generation = self.generation.wrapping_add(1);
        Ok(self)
    }
    pub fn finish(self) -> Result<Vec<u8>> {
        Ok(self.value.encode()?)
    }
}
