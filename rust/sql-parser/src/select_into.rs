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
    bridge::ffi::{ExternalFormatKind, ExternalFormatOption, FileOption, FileOptionKind,
        FileOutputDefinition, IntoKind},
    node::{Kind, Node},
    query::{child, into, list},
};

#[derive(Default)]
pub(crate) struct FileOutput {
    pub charset: Option<Node>,
    pub fields: Vec<Node>,
    pub lines: Vec<Node>,
    pub external_format: Vec<Node>,
    pub file_options: Vec<Node>,
}

impl FileOutput {
    pub fn into_node(self, kind: Kind, filename: Node) -> Node {
        let mut metadata = Vec::new();
        if let Some(charset) = self.charset {
            metadata.push(Node::new(Kind::OutputCharset, vec![charset]));
        }
        for (kind, values) in [
            (Kind::OutputFields, self.fields),
            (Kind::OutputLines, self.lines),
            (Kind::OutputFormat, self.external_format),
            (Kind::OutputFileOptions, self.file_options),
        ] {
            if !values.is_empty() {
                metadata.push(Node::new(kind, values));
            }
        }
        let mut children = vec![Node::new(Kind::OutputFilename, vec![filename])];
        if !metadata.is_empty() {
            children.push(Node::new(Kind::OutputMetadata, metadata));
        }
        Node::new(kind, children)
    }
}

pub(crate) fn query_into_kind(query: &Node) -> IntoKind {
    match into(query).map(|node| node.kind) {
        None => IntoKind::Absent,
        Some(Kind::IntoVariables | Kind::TrailingIntoVariables) => IntoKind::Variables,
        Some(Kind::IntoOutfile | Kind::TrailingIntoOutfile) => IntoKind::Outfile,
        Some(Kind::IntoDumpfile | Kind::TrailingIntoDumpfile) => IntoKind::Dumpfile,
        _ => unreachable!(),
    }
}

pub(crate) fn output_metadata(query: &Node) -> Option<&Node> {
    into(query).and_then(|node| child(node, Kind::OutputMetadata))
}

pub(crate) fn file_output_definition(query: &Node) -> FileOutputDefinition<'_> {
    let output = into(query).unwrap();
    let metadata = output_metadata(query);
    let charset = metadata.and_then(|node| child(node, Kind::OutputCharset));
    FileOutputDefinition {
        filename: &child(output, Kind::OutputFilename).unwrap().children[0],
        has_charset: charset.is_some(),
        charset: charset.map_or(&[][..], |node| node.children[0].text.as_bytes()),
        fields: metadata.map_or(&[], |node| list(node, Kind::OutputFields)),
        lines: metadata.map_or(&[], |node| list(node, Kind::OutputLines)),
        external_format: metadata.map_or(&[], |node| list(node, Kind::OutputFormat)),
        file_options: metadata.map_or(&[], |node| list(node, Kind::OutputFileOptions)),
    }
}

pub(crate) fn file_option(node: &Node) -> FileOption<'_> {
    FileOption {
        kind: match node.kind {
            Kind::OutputSingle => FileOptionKind::Single,
            Kind::OutputMaxFileSize => FileOptionKind::MaxFileSize,
            Kind::OutputBufferSize => FileOptionKind::BufferSize,
            _ => unreachable!(),
        },
        value: &node.children[0],
    }
}

pub(crate) fn external_format_option(node: &Node) -> ExternalFormatOption<'_> {
    ExternalFormatOption {
        kind: match node.kind {
            Kind::OutputFormatType => ExternalFormatKind::Type,
            Kind::OutputEncoding => ExternalFormatKind::Encoding,
            Kind::LoadFieldTerminated => ExternalFormatKind::FieldTerminated,
            Kind::LoadLineTerminated => ExternalFormatKind::LineTerminated,
            Kind::LoadEscaped => ExternalFormatKind::Escaped,
            Kind::LoadEnclosed => ExternalFormatKind::Enclosed,
            Kind::LoadOptionallyEnclosed => ExternalFormatKind::OptionallyEnclosed,
            Kind::OutputSkipHeader => ExternalFormatKind::SkipHeader,
            Kind::OutputSkipBlankLines => ExternalFormatKind::SkipBlankLines,
            Kind::OutputTrimSpace => ExternalFormatKind::TrimSpace,
            Kind::OutputNullIf => ExternalFormatKind::NullIf,
            Kind::OutputEmptyFieldAsNull => ExternalFormatKind::EmptyFieldAsNull,
            Kind::OutputCompression => ExternalFormatKind::Compression,
            Kind::OutputBinaryFormat => ExternalFormatKind::BinaryFormat,
            Kind::OutputFileExtension => ExternalFormatKind::FileExtension,
            Kind::OutputParseHeader => ExternalFormatKind::ParseHeader,
            Kind::OutputIgnoreLastEmptyColumn => ExternalFormatKind::IgnoreLastEmptyColumn,
            _ => unreachable!(),
        },
        value: &node.children[0],
        values: if node.kind == Kind::OutputNullIf { &node.children[0].children } else { &[] },
    }
}
