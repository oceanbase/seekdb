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

//! JSON parsing, binary values, queries, schema validation, construction and mutation.
//! Owned documents and selections keep their backing bytes alive. Borrowed values
//! and database capabilities are used synchronously; text errors use byte offsets.

pub mod binary;
mod bridge;
pub mod compare;
pub mod construction;
pub mod format;
pub mod hash;
pub mod mutation;
mod decimal;
pub mod path;
mod search;
pub mod schema;
pub mod sql_path;
mod value;
pub mod writer;

pub use binary::{
    BinaryDocument, BinaryError, BinaryErrorKind, BinaryResult, BinaryValue, JsonType,
};
pub use compare::{CompareError, CompareResult};
pub use decimal::Decimal;
pub use path::{
    Path, PathError, PathErrorKind, PathStep, Query, QueryOptions, QueryOutput, QueryOutputOptions,
    WrapperPolicy,
};
pub use search::SearchOptions;
pub use sql_path::SqlPath;
pub use value::{Selection, Value, ValueRef};

use serde::de::{DeserializeSeed, Deserializer, MapAccess, SeqAccess, Visitor};
use std::borrow::Cow;
use std::collections::HashMap;
use std::fmt;
use std::hash::{Hash, Hasher};

pub const DEFAULT_MAX_DEPTH: u32 = 100;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DuplicatePolicy {
    Overwrite,
    Preserve,
    Reject,
    KeepFirst,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ParseOptions {
    pub duplicate_policy: DuplicatePolicy,
    pub max_depth: u32,
    pub schema: bool,
}

impl Default for ParseOptions {
    fn default() -> Self {
        Self {
            duplicate_policy: DuplicatePolicy::Overwrite,
            max_depth: DEFAULT_MAX_DEPTH,
            schema: false,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum Number {
    I64(i64),
    U64(u64),
    F64(f64),
}

#[derive(Clone, Debug, PartialEq)]
pub enum Document {
    Null,
    Bool(bool),
    Number(Number),
    String(String),
    Array(Vec<Document>),
    Object(Vec<(String, Document)>),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ErrorKind {
    Syntax,
    DuplicateKey,
    Depth,
    UnsupportedSchemaRef,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ParseError {
    pub kind: ErrorKind,
    pub byte_offset: usize,
    pub message: String,
}

pub type Result<T> = std::result::Result<T, ParseError>;

pub fn parse(input: &[u8], options: ParseOptions) -> Result<Document> {
    let input = normalized_input(input);
    run_on_deep_stack(input.as_ref(), || parse_inline(input.as_ref(), options))
        .unwrap_or_else(|| Err(thread_failure()))
}

pub fn parse_binary(input: &[u8], options: ParseOptions) -> Result<Vec<u8>> {
    let input = normalized_input(input);
    run_on_deep_stack(input.as_ref(), || {
        parse_inline(input.as_ref(), options).map(binary::serialize)
    })
    .unwrap_or_else(|| Err(thread_failure()))
}

fn parse_inline(input: &[u8], options: ParseOptions) -> Result<Document> {
    let mut deserializer = serde_json::Deserializer::from_slice(input);
    deserializer.disable_recursion_limit();
    let result = DocumentSeed { depth: 0, options }
        .deserialize(&mut deserializer)
        .and_then(|document| deserializer.end().map(|()| document));
    result.map_err(|error| convert_error(input, error))
}

pub fn validate(input: &[u8], options: ParseOptions) -> Result<()> {
    if options.schema || options.duplicate_policy != DuplicatePolicy::Overwrite {
        return parse(input, options).map(|_| ());
    }
    let input = normalized_input(input);
    run_on_deep_stack(input.as_ref(), || validate_inline(input.as_ref(), options))
        .unwrap_or_else(|| Err(thread_failure()))
}

fn validate_inline(input: &[u8], options: ParseOptions) -> Result<()> {
    let mut deserializer = serde_json::Deserializer::from_slice(input);
    deserializer.disable_recursion_limit();
    let result = ValidateSeed {
        depth: 0,
        max_depth: options.max_depth,
    }
    .deserialize(&mut deserializer)
    .and_then(|()| deserializer.end());
    result.map_err(|error| convert_error(input, error))
}

// Deeply nested documents exceed the stack available to server threads because
// serde builds the document recursively. Parse those on a dedicated stack and
// keep shallow documents on the caller's thread.
const DEEP_PARSE_NESTING_LIMIT: usize = 256;
const DEEP_PARSE_STACK_SIZE: usize = 64 * 1024 * 1024;
const DEEP_PARSE_FAILURE: &str = "unable to allocate a stack for JSON parsing";

fn nesting_exceeds(input: &[u8], limit: usize) -> bool {
    let mut depth = 0usize;
    let mut in_string = false;
    let mut escaped = false;
    for byte in input {
        if in_string {
            if escaped {
                escaped = false;
            } else if *byte == b'\\' {
                escaped = true;
            } else if *byte == b'"' {
                in_string = false;
            }
        } else {
            match byte {
                b'"' => in_string = true,
                b'[' | b'{' => {
                    depth += 1;
                    if depth > limit {
                        return true;
                    }
                }
                b']' | b'}' => depth = depth.saturating_sub(1),
                _ => {}
            }
        }
    }
    false
}

fn run_on_deep_stack<T: Send>(
    input: &[u8],
    operation: impl FnOnce() -> T + Send,
) -> Option<T> {
    if !nesting_exceeds(input, DEEP_PARSE_NESTING_LIMIT) {
        return Some(operation());
    }
    std::thread::scope(|scope| {
        std::thread::Builder::new()
            .stack_size(DEEP_PARSE_STACK_SIZE)
            .spawn_scoped(scope, operation)
            .ok()
            .and_then(|handle| handle.join().ok())
    })
}

fn thread_failure() -> ParseError {
    ParseError {
        kind: ErrorKind::Syntax,
        byte_offset: 0,
        message: DEEP_PARSE_FAILURE.to_owned(),
    }
}

pub(crate) fn normalized_input(input: &[u8]) -> Cow<'_, [u8]> {
    let mut normalized = None;
    let mut index = 0;
    let mut in_string = false;
    while index < input.len() {
        let Some(relative) =
            memchr::memchr2(b'"', if in_string { b'\\' } else { b'-' }, &input[index..])
        else {
            break;
        };
        let position = index + relative;
        if in_string {
            if input[position] == b'\\' {
                index = (position + 2).min(input.len());
            } else {
                in_string = false;
                index = position + 1;
            }
            continue;
        }
        if input[position] == b'"' {
            in_string = true;
            index = position + 1;
            continue;
        }
        let integer_zero_terminator = match input.get(position + 2) {
            None => true,
            Some(byte) => matches!(*byte, b',' | b']' | b'}' | b' ' | b'\t' | b'\r' | b'\n'),
        };
        let value_start = position == 0
            || matches!(
                input[position - 1],
                b'{' | b'[' | b',' | b':' | b' ' | b'\t' | b'\r' | b'\n'
            );
        if value_start && input.get(position + 1) == Some(&b'0') && integer_zero_terminator {
            let bytes = normalized.get_or_insert_with(|| input.to_vec());
            bytes[position] = b' ';
        }
        index = position + 2;
    }
    match normalized {
        Some(bytes) => Cow::Owned(bytes),
        None => Cow::Borrowed(input),
    }
}

fn convert_error(input: &[u8], error: serde_json::Error) -> ParseError {
    let message = error.to_string();
    let kind = if message.starts_with(DUPLICATE_ERROR) {
        ErrorKind::DuplicateKey
    } else if message.starts_with(DEPTH_ERROR) {
        ErrorKind::Depth
    } else if message.starts_with(SCHEMA_REF_ERROR) {
        ErrorKind::UnsupportedSchemaRef
    } else {
        ErrorKind::Syntax
    };
    ParseError {
        kind,
        byte_offset: byte_offset(input, error.line(), error.column()),
        message,
    }
}

fn byte_offset(input: &[u8], line: usize, column: usize) -> usize {
    if line == 0 {
        return 0;
    }
    let mut current_line = 1;
    let mut line_start = 0;
    for (index, byte) in input.iter().enumerate() {
        if current_line == line {
            line_start = index;
            break;
        }
        if *byte == b'\n' {
            current_line += 1;
            line_start = index + 1;
        }
    }
    line_start
        .saturating_add(column.saturating_sub(1))
        .min(input.len())
}

const DUPLICATE_ERROR: &str = "duplicate object key";
const DEPTH_ERROR: &str = "JSON document exceeds configured depth";
const SCHEMA_REF_ERROR: &str = "external $ref is not supported in JSON schema";

fn key_hash(key: &str) -> u64 {
    let mut hasher = std::collections::hash_map::DefaultHasher::new();
    key.hash(&mut hasher);
    hasher.finish()
}

#[derive(Clone, Copy)]
struct DocumentSeed {
    depth: u32,
    options: ParseOptions,
}

impl<'de> DeserializeSeed<'de> for DocumentSeed {
    type Value = Document;

    fn deserialize<D>(self, deserializer: D) -> std::result::Result<Document, D::Error>
    where
        D: Deserializer<'de>,
    {
        deserializer.deserialize_any(DocumentVisitor { seed: self })
    }
}

struct DocumentVisitor {
    seed: DocumentSeed,
}

impl DocumentVisitor {
    fn nested(&self) -> std::result::Result<DocumentSeed, &'static str> {
        if self.seed.depth >= self.seed.options.max_depth {
            Err(DEPTH_ERROR)
        } else {
            Ok(DocumentSeed {
                depth: self.seed.depth + 1,
                ..self.seed
            })
        }
    }
}

impl<'de> Visitor<'de> for DocumentVisitor {
    type Value = Document;

    fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        formatter.write_str("a standard JSON value")
    }

    fn visit_unit<E>(self) -> std::result::Result<Document, E> {
        Ok(Document::Null)
    }
    fn visit_none<E>(self) -> std::result::Result<Document, E> {
        Ok(Document::Null)
    }
    fn visit_some<D>(self, deserializer: D) -> std::result::Result<Document, D::Error>
    where
        D: Deserializer<'de>,
    {
        self.seed.deserialize(deserializer)
    }
    fn visit_bool<E>(self, value: bool) -> std::result::Result<Document, E> {
        Ok(Document::Bool(value))
    }
    fn visit_i64<E>(self, value: i64) -> std::result::Result<Document, E> {
        Ok(Document::Number(Number::I64(value)))
    }
    fn visit_u64<E>(self, value: u64) -> std::result::Result<Document, E> {
        Ok(Document::Number(if value <= u32::MAX as u64 {
            Number::I64(value as i64)
        } else {
            Number::U64(value)
        }))
    }
    fn visit_f64<E>(self, value: f64) -> std::result::Result<Document, E> {
        Ok(Document::Number(Number::F64(value)))
    }
    fn visit_str<E>(self, value: &str) -> std::result::Result<Document, E>
    where
        E: serde::de::Error,
    {
        Ok(Document::String(value.to_owned()))
    }
    fn visit_string<E>(self, value: String) -> std::result::Result<Document, E> {
        Ok(Document::String(value))
    }

    fn visit_seq<A>(self, mut sequence: A) -> std::result::Result<Document, A::Error>
    where
        A: SeqAccess<'de>,
    {
        let nested = self.nested().map_err(serde::de::Error::custom)?;
        let mut values = Vec::with_capacity(sequence.size_hint().unwrap_or(0));
        while let Some(value) = sequence.next_element_seed(nested)? {
            values.push(value);
        }
        Ok(Document::Array(values))
    }

    fn visit_map<A>(self, mut map: A) -> std::result::Result<Document, A::Error>
    where
        A: MapAccess<'de>,
    {
        let nested = self.nested().map_err(serde::de::Error::custom)?;
        let policy = self.seed.options.duplicate_policy;
        let mut entries = Vec::with_capacity(map.size_hint().unwrap_or(0));
        let mut positions: Option<HashMap<u64, Vec<usize>>> = None;
        while let Some(key) = map.next_key::<String>()? {
            let value = map.next_value_seed(nested)?;
            if self.seed.options.schema && key == "$ref" {
                if let Document::String(reference) = &value {
                    if !reference.starts_with('#') {
                        return Err(serde::de::Error::custom(SCHEMA_REF_ERROR));
                    }
                }
            }
            if policy == DuplicatePolicy::Preserve {
                entries.push((key, value));
                continue;
            }
            let duplicate = match &positions {
                Some(positions) => positions.get(&key_hash(&key)).and_then(|candidates| {
                    candidates
                        .iter()
                        .copied()
                        .find(|index| entries[*index].0 == key)
                }),
                None => entries.iter().position(|(existing, _)| existing == &key),
            };
            if let Some(index) = duplicate {
                match policy {
                    DuplicatePolicy::Reject => {
                        return Err(serde::de::Error::custom(DUPLICATE_ERROR))
                    }
                    DuplicatePolicy::KeepFirst => {}
                    DuplicatePolicy::Overwrite => entries[index].1 = value,
                    DuplicatePolicy::Preserve => unreachable!(),
                }
            } else {
                let index = entries.len();
                entries.push((key, value));
                if entries.len() == 16 {
                    let mut indexed = HashMap::with_capacity(32);
                    for (index, (key, _)) in entries.iter().enumerate() {
                        indexed
                            .entry(key_hash(key))
                            .or_insert_with(Vec::new)
                            .push(index);
                    }
                    positions = Some(indexed);
                } else if let Some(positions) = &mut positions {
                    positions
                        .entry(key_hash(&entries[index].0))
                        .or_default()
                        .push(index);
                }
            }
        }
        Ok(Document::Object(entries))
    }
}

#[derive(Clone, Copy)]
struct ValidateSeed {
    depth: u32,
    max_depth: u32,
}

impl<'de> DeserializeSeed<'de> for ValidateSeed {
    type Value = ();
    fn deserialize<D>(self, deserializer: D) -> std::result::Result<(), D::Error>
    where
        D: Deserializer<'de>,
    {
        deserializer.deserialize_any(self)
    }
}

impl<'de> Visitor<'de> for ValidateSeed {
    type Value = ();
    fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        formatter.write_str("a standard JSON value")
    }
    fn visit_unit<E>(self) -> std::result::Result<(), E> {
        Ok(())
    }
    fn visit_bool<E>(self, _: bool) -> std::result::Result<(), E> {
        Ok(())
    }
    fn visit_i64<E>(self, _: i64) -> std::result::Result<(), E> {
        Ok(())
    }
    fn visit_u64<E>(self, _: u64) -> std::result::Result<(), E> {
        Ok(())
    }
    fn visit_f64<E>(self, _: f64) -> std::result::Result<(), E> {
        Ok(())
    }
    fn visit_str<E>(self, _: &str) -> std::result::Result<(), E> {
        Ok(())
    }
    fn visit_string<E>(self, _: String) -> std::result::Result<(), E> {
        Ok(())
    }
    fn visit_seq<A>(self, mut sequence: A) -> std::result::Result<(), A::Error>
    where
        A: SeqAccess<'de>,
    {
        if self.depth >= self.max_depth {
            return Err(serde::de::Error::custom(DEPTH_ERROR));
        }
        let nested = Self {
            depth: self.depth + 1,
            max_depth: self.max_depth,
        };
        while sequence.next_element_seed(nested)?.is_some() {}
        Ok(())
    }
    fn visit_map<A>(self, mut map: A) -> std::result::Result<(), A::Error>
    where
        A: MapAccess<'de>,
    {
        if self.depth >= self.max_depth {
            return Err(serde::de::Error::custom(DEPTH_ERROR));
        }
        let nested = Self {
            depth: self.depth + 1,
            max_depth: self.max_depth,
        };
        while map
            .next_key_seed(ValidateSeed {
                depth: 0,
                max_depth: self.max_depth,
            })?
            .is_some()
        {
            map.next_value_seed(nested)?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests;
