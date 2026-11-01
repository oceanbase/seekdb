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

use crate::{Document, Number};

const NULL: u8 = 0;
const INT: u8 = 2;
const UINT: u8 = 3;
const DOUBLE: u8 = 4;
const STRING: u8 = 5;
const OBJECT: u8 = 6;
const ARRAY: u8 = 7;
const BOOLEAN: u8 = 8;
const DOC_HEADER: u8 = 30;
const INLINE: u8 = 0x80;

fn var_type(value: u64) -> u8 {
    if value <= u8::MAX as u64 {
        0
    } else if value <= u16::MAX as u64 {
        1
    } else if value <= u32::MAX as u64 {
        2
    } else {
        3
    }
}

fn signed_var_type(value: i64) -> u8 {
    if i8::try_from(value).is_ok() {
        0
    } else if i16::try_from(value).is_ok() {
        1
    } else if i32::try_from(value).is_ok() {
        2
    } else {
        3
    }
}

fn var_size(kind: u8) -> usize {
    1usize << kind
}

fn push_var(output: &mut Vec<u8>, value: u64, kind: u8) {
    let bytes = value.to_le_bytes();
    output.extend_from_slice(&bytes[..var_size(kind)]);
}

fn set_var(output: &mut [u8], position: usize, value: u64, kind: u8) {
    let size = var_size(kind);
    output[position..position + size].copy_from_slice(&value.to_le_bytes()[..size]);
}

fn vi64_len(value: u64) -> u64 {
    let bits = 64 - value.leading_zeros() as u64;
    ((bits.max(1) + 6) / 7).min(10)
}

fn push_vi64(output: &mut Vec<u8>, mut value: u64) {
    while value > 0x7f {
        output.push(value as u8 | 0x80);
        value >>= 7;
    }
    output.push(value as u8);
}

fn serialized_size(document: &Document) -> u64 {
    match document {
        Document::Null | Document::Bool(_) => 1,
        Document::Number(Number::I64(value)) => vi64_len(*value as u64),
        Document::Number(Number::U64(value)) => vi64_len(*value),
        Document::Number(Number::F64(_)) => 8,
        Document::String(value) => 1 + vi64_len(value.len() as u64) + value.len() as u64,
        Document::Array(values) => array_size(values),
        Document::Object(values) => object_size(values),
    }
}

fn container_size(
    count: usize,
    key_bytes: u64,
    key_length_bytes: u64,
    value_bytes: u64,
    last_value_bytes: u64,
    object: bool,
) -> u64 {
    let count_bytes = var_size(var_type(count as u64)) as u64;
    let estimated_entry_bytes = if object {
        (4 + 4 + 1) * count as u64 + key_length_bytes
    } else {
        (4 + 1) * count as u64
    };
    let estimated_total = 2 + count_bytes + 4 + estimated_entry_bytes + key_bytes + value_bytes;
    let last_offset = estimated_total - last_value_bytes;
    let entry_type = var_type(last_offset);
    let entry_bytes = var_size(entry_type) as u64;
    let entry_array_bytes = if object {
        (entry_bytes * 2 + 1) * count as u64 + key_length_bytes
    } else {
        (entry_bytes + 1) * count as u64
    };
    let total_with_entries = 2 + count_bytes + 4 + entry_array_bytes + key_bytes + value_bytes;
    let size_bytes = var_size(var_type(total_with_entries)) as u64;
    2 + count_bytes + size_bytes + entry_array_bytes + key_bytes + value_bytes
}

fn array_size(values: &[Document]) -> u64 {
    let mut value_bytes = 0;
    let mut last_value_bytes = 0;
    for value in values {
        last_value_bytes = serialized_size(value);
        value_bytes += last_value_bytes;
    }
    container_size(values.len(), 0, 0, value_bytes, last_value_bytes, false)
}

fn object_size(values: &[(String, Document)]) -> u64 {
    let key_bytes = values.iter().map(|(key, _)| key.len() as u64).sum();
    let key_length_bytes = values
        .iter()
        .map(|(key, _)| var_size(var_type(key.len() as u64)) as u64)
        .sum();
    let mut value_bytes = 0;
    let mut last_value_bytes = 0;
    for (_, value) in values {
        last_value_bytes = serialized_size(value);
        value_bytes += last_value_bytes;
    }
    container_size(
        values.len(),
        key_bytes,
        key_length_bytes,
        value_bytes,
        last_value_bytes,
        true,
    )
}

fn value_type(document: &Document) -> u8 {
    match document {
        Document::Null => NULL,
        Document::Bool(_) => BOOLEAN,
        Document::Number(Number::I64(_)) => INT,
        Document::Number(Number::U64(_)) => UINT,
        Document::Number(Number::F64(_)) => DOUBLE,
        Document::String(_) => STRING,
        Document::Array(_) => ARRAY,
        Document::Object(_) => OBJECT,
    }
}

fn inline_value(document: &Document, entry_type: u8) -> Option<(u64, u8)> {
    match document {
        Document::Null => Some((0, NULL | INLINE)),
        Document::Bool(value) => Some((*value as u64, BOOLEAN | INLINE)),
        Document::Number(Number::I64(value)) if signed_var_type(*value) <= entry_type => {
            let encoded = match signed_var_type(*value) {
                0 => *value as i8 as u8 as u64,
                1 => *value as i16 as u16 as u64,
                2 => *value as i32 as u32 as u64,
                _ => *value as u64,
            };
            Some((encoded, INT | INLINE))
        }
        Document::Number(Number::U64(value)) if var_type(*value) <= entry_type => {
            Some((*value, UINT | INLINE))
        }
        _ => None,
    }
}

fn write_string(output: &mut Vec<u8>, value: &str) {
    output.push(STRING);
    push_vi64(output, value.len() as u64);
    output.extend_from_slice(value.as_bytes());
}

fn write_value(output: &mut Vec<u8>, document: &Document) {
    match document {
        Document::Null => output.push(NULL),
        Document::Bool(value) => output.push(*value as u8),
        Document::Number(Number::I64(value)) => push_vi64(output, *value as u64),
        Document::Number(Number::U64(value)) => push_vi64(output, *value),
        Document::Number(Number::F64(value)) => output.extend_from_slice(&value.to_le_bytes()),
        Document::String(value) => write_string(output, value),
        Document::Array(values) => write_array(output, values),
        Document::Object(values) => write_object(output, values),
    }
}

#[derive(Clone, Copy)]
enum Container<'a> {
    Array(&'a [Document]),
    Object(&'a [(String, Document)]),
}

impl<'a> Container<'a> {
    fn len(self) -> usize {
        match self {
            Self::Array(values) => values.len(),
            Self::Object(values) => values.len(),
        }
    }

    fn value(self, index: usize) -> &'a Document {
        match self {
            Self::Array(values) => &values[index],
            Self::Object(values) => &values[index].1,
        }
    }
}

fn write_container(output: &mut Vec<u8>, container: Container<'_>, estimated_size: u64) {
    let start = output.len();
    let count = container.len();
    let count_type = var_type(count as u64);
    let size_type = var_type(estimated_size);
    let entry_type = size_type;
    let entry_size = var_size(entry_type);
    let kind = match container {
        Container::Array(_) => ARRAY,
        Container::Object(_) => OBJECT,
    };
    output.push(kind);
    output.push(entry_type | (count_type << 2) | (size_type << 4) | 0x40);
    push_var(output, count as u64, count_type);
    let size_position = output.len();
    push_var(output, estimated_size, size_type);
    let key_entries = output.len();
    if matches!(container, Container::Object(_)) {
        output.resize(output.len() + count * entry_size * 2, 0);
    }
    let value_entries = output.len();
    output.resize(output.len() + count * (entry_size + 1), 0);

    if let Container::Object(entries) = container {
        for (index, (key, _)) in entries.iter().enumerate() {
            let key_offset = (output.len() - start) as u64;
            let position = key_entries + index * entry_size * 2;
            set_var(output, position, key_offset, entry_type);
            set_var(output, position + entry_size, key.len() as u64, entry_type);
            output.extend_from_slice(key.as_bytes());
        }
    }

    for index in 0..count {
        let value = container.value(index);
        let value_offset = (output.len() - start) as u64;
        let position = value_entries + index * (entry_size + 1);
        if let Some((encoded, kind)) = inline_value(value, entry_type) {
            set_var(output, position, encoded, entry_type);
            output[position + entry_size] = kind;
        } else {
            set_var(output, position, value_offset, entry_type);
            output[position + entry_size] = value_type(value);
            write_value(output, value);
        }
    }

    let actual_size = (output.len() - start) as u64;
    if var_type(actual_size) > size_type {
        output.truncate(start);
        write_container(output, container, actual_size);
    } else {
        set_var(output, size_position, actual_size, size_type);
    }
}

fn write_array(output: &mut Vec<u8>, values: &[Document]) {
    write_container(output, Container::Array(values), array_size(values));
}

fn write_object(output: &mut Vec<u8>, values: &[(String, Document)]) {
    write_container(output, Container::Object(values), object_size(values));
}

fn sort_objects(document: &mut Document) {
    match document {
        Document::Array(values) => {
            for value in values {
                sort_objects(value);
            }
        }
        Document::Object(values) => {
            for (_, value) in values.iter_mut() {
                sort_objects(value);
            }
            values.sort_by(|(left, _), (right, _)| {
                left.len()
                    .cmp(&right.len())
                    .then_with(|| left.as_bytes().cmp(right.as_bytes()))
            });
        }
        _ => {}
    }
}

pub(crate) fn serialize(mut document: Document) -> Vec<u8> {
    sort_objects(&mut document);
    let mut output = Vec::with_capacity(serialized_size(&document) as usize + 8);
    match &document {
        Document::Array(_) | Document::Object(_) => {
            output.resize(8, 0);
            write_value(&mut output, &document);
            let header = DOC_HEADER as u64 | ((output.len() as u64) << 22);
            output[..8].copy_from_slice(&header.to_le_bytes());
        }
        Document::String(value) => write_string(&mut output, value),
        _ => {
            output.push(value_type(&document));
            write_value(&mut output, &document);
        }
    }
    output
}
