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
use crate::Path;
use crate::mutation::sql::{self, Action, MutationView};

impl<S: BinarySource + ?Sized> MutationView for Editor<'_, S> {
    type Node = Node;
    fn root(&self) -> Node { self.root }
    fn kind(&self, node: Node) -> Result<JsonType> { Editor::kind(self, node) }
    fn len(&self, node: Node) -> Result<u64> { Ok(self.container(node)?.count) }
    fn child(&self, node: Node, index: u64) -> Result<Node> { Editor::child(self, self.container(node)?, index) }
    fn member(&self, node: Node, key: &[u8]) -> Result<Option<(u64, Node)>> {
        let container = self.container(node)?;
        let (found, index) = self.find_key(container, key)?;
        Ok(if found { Some((index, self.child(container, index)?)) } else { None })
    }
}

impl<S: BinarySource + ?Sized> Editor<'_, S> {
    pub fn prepare(&self, path: &crate::Path, operation: SqlOperation) -> Result<PreparedEdit> {
        sql::prepare(self, self.generation, path, operation)
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

    pub fn should_pack(&self, prior: &Delta) -> Result<bool> {
        self.should_pack_with(prior.state())
    }

    pub fn should_pack_with(&self, prior: crate::mutation::UpdateState) -> Result<bool> {
        use crate::mutation::{should_pack_state, UpdateState};
        let snapshot = self.snapshot.as_ref().unwrap();
        if snapshot.root.offset == 0 || snapshot.extension == 0 {
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
            root: self.root,
            writes: self.writes,
            delta: self.delta,
        }
    }
}
