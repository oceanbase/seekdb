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
    bridge::ffi::{
        LoadDataDefinition, LoadDuplicate, LoadFormatKind, LoadFormatOption, LoadLocation,
    },
    node::{Kind, Node},
    query::{child, list},
};

pub(crate) struct LoadData {
    pub table: Node,
    pub filename: Node,
    pub local: bool,
    pub duplicate: LoadDuplicate,
    pub compression: Option<Node>,
    pub charset: Option<Node>,
    pub ignore_rows: Option<Node>,
    pub formats: Vec<Node>,
    pub fields: Vec<Node>,
    pub assignments: Vec<Node>,
    pub partitions: Vec<Node>,
    pub hints: Vec<Node>,
}

impl LoadData {
    pub fn into_node(self) -> Node {
        let mut children = vec![self.table, Node::new(Kind::LoadFile, vec![self.filename])];
        if self.local {
            children.push(Node::new(Kind::LoadLocal, vec![]));
        }
        match self.duplicate {
            LoadDuplicate::Ignore => children.push(Node::new(Kind::Ignore, vec![])),
            LoadDuplicate::Replace => children.push(Node::new(Kind::Replace, vec![])),
            _ => {}
        }
        for (kind, value) in [
            (Kind::LoadCompression, self.compression),
            (Kind::LoadCharset, self.charset),
            (Kind::LoadIgnoreRows, self.ignore_rows),
        ] {
            if let Some(value) = value {
                children.push(Node::new(kind, vec![value]));
            }
        }
        for (kind, values) in [
            (Kind::LoadFormats, self.formats),
            (Kind::LoadFields, self.fields),
            (Kind::AssignList, self.assignments),
            (Kind::LoadPartitions, self.partitions),
            (Kind::Hints, self.hints),
        ] {
            if !values.is_empty() {
                children.push(Node::new(kind, values));
            }
        }
        Node::new(Kind::LoadData, children)
    }
}

pub(crate) fn load_data_definition(node: &Node) -> LoadDataDefinition<'_> {
    let filename = &child(node, Kind::LoadFile).unwrap().children[0];
    let compression = child(node, Kind::LoadCompression).map(|n| &n.children[0]);
    let charset = child(node, Kind::LoadCharset).map(|n| &n.children[0]);
    LoadDataDefinition {
        table: child(node, Kind::Relation).unwrap(),
        filename,
        location: if child(node, Kind::LoadLocal).is_some() {
            LoadLocation::Client
        } else {
            LoadLocation::Server
        },
        duplicate: if child(node, Kind::Ignore).is_some() {
            LoadDuplicate::Ignore
        } else if child(node, Kind::Replace).is_some() {
            LoadDuplicate::Replace
        } else {
            LoadDuplicate::Stop
        },
        has_compression: compression.is_some(),
        compression: compression.map_or(&[][..], |n| n.text.as_bytes()),
        default_charset: charset.is_none_or(|n| n.kind == Kind::Default),
        charset: charset.map_or(&[][..], |n| n.text.as_bytes()),
        ignore_rows: child(node, Kind::LoadIgnoreRows).map_or(&[], |n| n.children.as_slice()),
        format_options: list(node, Kind::LoadFormats),
        input_fields: list(node, Kind::LoadFields),
        assignments: list(node, Kind::AssignList),
        partitions: list(node, Kind::LoadPartitions),
        hints: list(node, Kind::Hints),
        contains_sensitive_data: filename
            .text
            .as_bytes()
            .windows(9)
            .any(|part| part.eq_ignore_ascii_case(b"access_id")),
    }
}

pub(crate) fn load_format_option(node: &Node) -> LoadFormatOption<'_> {
    LoadFormatOption {
        kind: match node.kind {
            Kind::LoadFieldTerminated => LoadFormatKind::FieldTerminated,
            Kind::LoadEnclosed => LoadFormatKind::Enclosed,
            Kind::LoadOptionallyEnclosed => LoadFormatKind::OptionallyEnclosed,
            Kind::LoadEscaped => LoadFormatKind::Escaped,
            Kind::LoadLineStarting => LoadFormatKind::LineStarting,
            Kind::LoadLineTerminated => LoadFormatKind::LineTerminated,
            _ => unreachable!(),
        },
        value: &node.children[0],
    }
}
