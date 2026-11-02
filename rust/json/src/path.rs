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
    binary, BinaryDocument, BinaryResult, BinaryValue, CompareResult, JsonType, ParseOptions,
};
use std::fmt;

pub const MAX_PATH_NODES: usize = 100;
pub const MAX_ARRAY_INDEX: u64 = u32::MAX as u64;

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PathStep {
    Member(String),
    MemberWildcard,
    ArrayCell(ArrayIndex),
    ArrayRange(ArrayIndex, ArrayIndex),
    ArrayWildcard,
    RecursiveDescent,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ArrayIndex {
    pub value: u64,
    pub from_end: bool,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Path {
    steps: Vec<PathStep>,
}

pub struct Query {
    pub(crate) document: BinaryDocument<Vec<u8>>,
}

#[derive(Clone, Debug)]
pub struct QueryOutput {
    pub match_count: usize,
    pub value_type: Option<JsonType>,
    pub binary: Option<Vec<u8>>,
}

impl Query {
    pub fn from_binary(input: Vec<u8>) -> BinaryResult<Self> {
        Ok(Self {
            document: BinaryDocument::new(input)?,
        })
    }

    pub fn from_text(input: &[u8], options: ParseOptions) -> crate::Result<Self> {
        Self::from_binary(crate::parse_binary(input, options)?).map_err(|error| crate::ParseError {
            kind: crate::ErrorKind::Syntax,
            byte_offset: error.byte_offset,
            message: error.to_string(),
        })
    }

    pub fn query(&self, path: &Path, options: QueryOptions) -> BinaryResult<Vec<BinaryValue<'_>>> {
        path.query(self.document.root()?, options)
    }

    pub fn query_output(&self, path: &Path, wrap_result: bool) -> BinaryResult<QueryOutput> {
        let matches = self.query(
            path,
            QueryOptions {
                auto_wrap: true,
                first_only: false,
            },
        )?;
        let value_type = match matches.first() {
            Some(value) => Some(value.json_type()?),
            None => None,
        };
        let binary = if wrap_result && !matches.is_empty() {
            Some(binary::serialize_array(&matches)?)
        } else if matches.len() == 1 {
            Some(binary::serialize_value(matches[0])?)
        } else {
            None
        };
        Ok(QueryOutput {
            match_count: matches.len(),
            value_type,
            binary,
        })
    }

    pub fn extract(&self, paths: &[Path]) -> BinaryResult<Option<Vec<u8>>> {
        let root = self.document.root()?;
        let mut matches = Vec::new();
        for path in paths {
            matches.extend(path.query(root, QueryOptions::default())?);
        }
        let force_array = paths.len() > 1 || paths.iter().any(Path::can_match_many);
        if matches.is_empty() {
            Ok(None)
        } else if matches.len() == 1 && !force_array {
            binary::serialize_value(matches[0]).map(Some)
        } else {
            binary::serialize_array(&matches).map(Some)
        }
    }

    pub fn object_keys(&self, path: Option<&Path>) -> BinaryResult<Option<Vec<u8>>> {
        let root = self.document.root()?;
        let value = if let Some(path) = path {
            let matches = path.query(
                root,
                QueryOptions {
                    auto_wrap: false,
                    first_only: true,
                },
            )?;
            if matches.len() != 1 {
                return Ok(None);
            }
            matches[0]
        } else {
            root
        };
        if value.json_type()? != JsonType::Object {
            return Ok(None);
        }
        binary::serialize_object_keys(value).map(Some)
    }

    pub fn contains(&self, candidate: &Query, path: Option<&Path>) -> CompareResult<Option<bool>> {
        let target = if let Some(path) = path {
            let matches = self.query(path, QueryOptions::default())?;
            let Some(target) = matches.first() else {
                return Ok(None);
            };
            *target
        } else {
            self.document.root()?
        };
        crate::compare::contains(target, candidate.document.root()?).map(Some)
    }

    pub fn overlaps(&self, other: &Query) -> CompareResult<bool> {
        crate::compare::overlaps(self.document.root()?, other.document.root()?)
    }

    pub fn member_of(&self, target: &Query) -> CompareResult<bool> {
        crate::compare::member_of(self.document.root()?, target.document.root()?)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PathErrorKind {
    Syntax,
    TooManyNodes,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PathError {
    pub kind: PathErrorKind,
    pub byte_offset: usize,
}

impl fmt::Display for PathError {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        write!(
            formatter,
            "invalid JSON path at byte {}: {:?}",
            self.byte_offset, self.kind
        )
    }
}

impl std::error::Error for PathError {}

pub type PathResult<T> = std::result::Result<T, PathError>;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct QueryOptions {
    pub auto_wrap: bool,
    pub first_only: bool,
}

impl Default for QueryOptions {
    fn default() -> Self {
        Self {
            auto_wrap: true,
            first_only: false,
        }
    }
}

impl Path {
    pub fn parse_mysql(input: &[u8]) -> PathResult<Self> {
        Parser::new(input).parse()
    }

    pub fn steps(&self) -> &[PathStep] {
        &self.steps
    }

    pub fn can_match_many(&self) -> bool {
        self.steps.iter().any(|step| {
            matches!(
                step,
                PathStep::MemberWildcard
                    | PathStep::ArrayRange(_, _)
                    | PathStep::ArrayWildcard
                    | PathStep::RecursiveDescent
            )
        })
    }

    pub fn query<'a>(
        &self,
        root: BinaryValue<'a>,
        options: QueryOptions,
    ) -> BinaryResult<Vec<BinaryValue<'a>>> {
        let mut matches = Vec::new();
        let mut location = String::new();
        evaluate(
            root,
            &self.steps,
            0,
            options,
            &mut matches,
            None,
            &mut location,
        )?;
        Ok(matches)
    }

    pub(crate) fn query_located<'a>(
        &self,
        root: BinaryValue<'a>,
        options: QueryOptions,
    ) -> BinaryResult<Vec<(BinaryValue<'a>, String)>> {
        let mut matches = Vec::new();
        let mut locations = Vec::new();
        let mut location = String::from("$");
        evaluate(
            root,
            &self.steps,
            0,
            options,
            &mut matches,
            Some(&mut locations),
            &mut location,
        )?;
        Ok(matches.into_iter().zip(locations).collect())
    }

    pub fn to_mysql_string(&self) -> String {
        let mut output = String::from("$");
        for step in &self.steps {
            match step {
                PathStep::Member(key) => append_member(&mut output, key, true),
                PathStep::MemberWildcard => output.push_str(".*"),
                PathStep::ArrayCell(index) => {
                    output.push('[');
                    append_array_index(&mut output, *index);
                    output.push(']');
                }
                PathStep::ArrayRange(first, last) => {
                    output.push('[');
                    append_array_index(&mut output, *first);
                    output.push_str(" to ");
                    append_array_index(&mut output, *last);
                    output.push(']');
                }
                PathStep::ArrayWildcard => output.push_str("[*]"),
                PathStep::RecursiveDescent => output.push_str("**"),
            }
        }
        output
    }
}

fn evaluate<'a>(
    value: BinaryValue<'a>,
    steps: &[PathStep],
    index: usize,
    options: QueryOptions,
    matches: &mut Vec<BinaryValue<'a>>,
    mut locations: Option<&mut Vec<String>>,
    location: &mut String,
) -> BinaryResult<bool> {
    if index == steps.len() {
        matches.push(value);
        if let Some(locations) = locations {
            locations.push(location.clone());
        }
        return Ok(options.first_only);
    }
    match &steps[index] {
        PathStep::Member(key) => {
            if value.json_type()? == JsonType::Object {
                if let Some(member) = value.member(key.as_bytes())? {
                    let previous_length = location.len();
                    if locations.is_some() {
                        append_raw_member(location, key);
                    }
                    let result = evaluate(
                        member,
                        steps,
                        index + 1,
                        options,
                        matches,
                        locations,
                        location,
                    );
                    location.truncate(previous_length);
                    return result;
                }
            }
        }
        PathStep::MemberWildcard => {
            if value.json_type()? == JsonType::Object {
                for child_index in 0..value.len()? {
                    let previous_length = location.len();
                    if locations.is_some() {
                        append_raw_member(
                            location,
                            &String::from_utf8_lossy(value.key(child_index)?),
                        );
                    }
                    let result = evaluate(
                        value.value(child_index)?,
                        steps,
                        index + 1,
                        options,
                        matches,
                        locations.as_deref_mut(),
                        location,
                    );
                    location.truncate(previous_length);
                    if result? {
                        return Ok(true);
                    }
                }
            }
        }
        PathStep::ArrayCell(cell) => {
            if value.json_type()? == JsonType::Array {
                if let Some(child_index) = resolve_index(*cell, value.len()?) {
                    let previous_length = location.len();
                    if locations.is_some() {
                        append_array_location(location, child_index);
                    }
                    let result = evaluate(
                        value.value(child_index)?,
                        steps,
                        index + 1,
                        options,
                        matches,
                        locations,
                        location,
                    );
                    location.truncate(previous_length);
                    return result;
                }
            } else if options.auto_wrap && resolve_index(*cell, 1).is_some() {
                return evaluate(
                    value,
                    steps,
                    index + 1,
                    options,
                    matches,
                    locations,
                    location,
                );
            }
        }
        PathStep::ArrayRange(first, last) => {
            if value.json_type()? == JsonType::Array {
                let (begin, end) = resolve_range(*first, *last, value.len()?);
                for child_index in begin..end {
                    let previous_length = location.len();
                    if locations.is_some() {
                        append_array_location(location, child_index);
                    }
                    let result = evaluate(
                        value.value(child_index)?,
                        steps,
                        index + 1,
                        options,
                        matches,
                        locations.as_deref_mut(),
                        location,
                    );
                    location.truncate(previous_length);
                    if result? {
                        return Ok(true);
                    }
                }
            } else if options.auto_wrap {
                let (begin, end) = resolve_range(*first, *last, 1);
                if begin < end {
                    return evaluate(
                        value,
                        steps,
                        index + 1,
                        options,
                        matches,
                        locations,
                        location,
                    );
                }
            }
        }
        PathStep::ArrayWildcard => {
            if value.json_type()? == JsonType::Array {
                for child_index in 0..value.len()? {
                    let previous_length = location.len();
                    if locations.is_some() {
                        append_array_location(location, child_index);
                    }
                    let result = evaluate(
                        value.value(child_index)?,
                        steps,
                        index + 1,
                        options,
                        matches,
                        locations.as_deref_mut(),
                        location,
                    );
                    location.truncate(previous_length);
                    if result? {
                        return Ok(true);
                    }
                }
            }
        }
        PathStep::RecursiveDescent => {
            if evaluate(
                value,
                steps,
                index + 1,
                options,
                matches,
                locations.as_deref_mut(),
                location,
            )? {
                return Ok(true);
            }
            let mut stack = Vec::new();
            if value.json_type()?.is_container() {
                stack.push((value, 0));
            }
            while let Some((container, child_index)) = stack.last_mut() {
                if *child_index == container.len()? {
                    stack.pop();
                    continue;
                }
                let child = container.value(*child_index)?;
                *child_index += 1;
                if evaluate(
                    child,
                    steps,
                    index + 1,
                    options,
                    matches,
                    locations.as_deref_mut(),
                    location,
                )? {
                    return Ok(true);
                }
                if child.json_type()?.is_container() {
                    stack.push((child, 0));
                }
            }
        }
    }
    Ok(false)
}

fn append_array_location(output: &mut String, index: u64) {
    output.push('[');
    output.push_str(&index.to_string());
    output.push(']');
}

fn append_raw_member(output: &mut String, key: &str) {
    output.push('.');
    output.push_str(key);
}

fn append_array_index(output: &mut String, index: ArrayIndex) {
    if index.from_end {
        output.push_str("last");
        if index.value > 0 {
            output.push('-');
            output.push_str(&index.value.to_string());
        }
    } else {
        output.push_str(&index.value.to_string());
    }
}

pub(crate) fn append_member(output: &mut String, key: &str, quote_empty: bool) {
    output.push('.');
    if key.is_empty() && quote_empty {
        output.push_str("\"\"");
    } else if is_ecmascript_identifier(key) {
        output.push_str(key);
    } else {
        append_quoted(output, key);
    }
}

fn is_ecmascript_identifier(key: &str) -> bool {
    key.chars().enumerate().all(|(index, character)| {
        let codepoint = character as u32;
        character.is_ascii_alphabetic()
            || (0x2e80..=0x9fff).contains(&codepoint)
            || matches!(character, '$' | '_')
            || (index > 0
                && (character.is_ascii_digit()
                    || (0x300..=0x36f).contains(&codepoint)
                    || matches!(
                        codepoint,
                        0x203f
                            | 0x2040
                            | 0x2054
                            | 0xfe33
                            | 0xfe34
                            | 0xfe4d
                            | 0xfe4e
                            | 0xfe4f
                            | 0xff3f
                            | 0x200c
                            | 0x200d
                    )))
    })
}

fn append_quoted(output: &mut String, key: &str) {
    use std::fmt::Write;

    output.push('"');
    for character in key.chars() {
        match character {
            '\u{08}' => output.push_str("\\b"),
            '\t' => output.push_str("\\t"),
            '\n' => output.push_str("\\n"),
            '\u{0c}' => output.push_str("\\f"),
            '\r' => output.push_str("\\r"),
            '"' | '\\' => {
                output.push('\\');
                output.push(character);
            }
            character if character <= '\u{1f}' => {
                write!(output, "\\u{:04x}", character as u32)
                    .expect("writing to String cannot fail");
            }
            character => output.push(character),
        }
    }
    output.push('"');
}

fn resolve_index(index: ArrayIndex, length: u64) -> Option<u64> {
    if index.value >= length {
        return None;
    }
    if index.from_end {
        Some(length - index.value - 1)
    } else {
        Some(index.value)
    }
}

fn resolve_range(first: ArrayIndex, last: ArrayIndex, length: u64) -> (u64, u64) {
    fn bound(index: ArrayIndex, length: u64) -> (u64, bool) {
        if index.from_end {
            if index.value < length {
                (length - index.value - 1, true)
            } else {
                (0, false)
            }
        } else {
            (index.value.min(length), index.value < length)
        }
    }
    let (begin, _) = bound(first, length);
    let (last, within_bounds) = bound(last, length);
    let end = if within_bounds { last + 1 } else { last };
    (begin, end)
}

struct Parser<'a> {
    input: &'a [u8],
    position: usize,
    steps: Vec<PathStep>,
}

impl<'a> Parser<'a> {
    fn new(input: &'a [u8]) -> Self {
        Self {
            input,
            position: 0,
            steps: Vec::new(),
        }
    }

    fn parse(mut self) -> PathResult<Path> {
        self.skip_whitespace();
        self.expect(b'$')?;
        self.skip_whitespace();
        while self.position < self.input.len() {
            match self.input[self.position] {
                b'.' => self.parse_member()?,
                b'[' => self.parse_array()?,
                b'*' => self.parse_recursive_descent()?,
                _ => return self.error(PathErrorKind::Syntax),
            }
            self.skip_whitespace();
        }
        Ok(Path { steps: self.steps })
    }

    fn parse_member(&mut self) -> PathResult<()> {
        self.position += 1;
        self.skip_whitespace();
        if self.consume(b'*') {
            return self.push(PathStep::MemberWildcard);
        }
        if self.position >= self.input.len() {
            return self.error(PathErrorKind::Syntax);
        }
        let key = if self.input[self.position] == b'"' {
            self.parse_quoted_key()?
        } else {
            self.parse_unquoted_key()?
        };
        self.push(PathStep::Member(key))
    }

    fn parse_quoted_key(&mut self) -> PathResult<String> {
        let start = self.position;
        self.position += 1;
        while self.position < self.input.len() {
            match self.input[self.position] {
                b'\\' => {
                    self.position = (self.position + 2).min(self.input.len());
                }
                b'"' => {
                    self.position += 1;
                    let encoded = &self.input[start..self.position];
                    return serde_json::from_slice(encoded).map_err(|error| PathError {
                        kind: PathErrorKind::Syntax,
                        byte_offset: start + error.column().saturating_sub(1),
                    });
                }
                _ => self.position += 1,
            }
        }
        self.error(PathErrorKind::Syntax)
    }

    fn parse_unquoted_key(&mut self) -> PathResult<String> {
        let start = self.position;
        while self.position < self.input.len() && !is_key_terminator(self.input[self.position]) {
            self.position += 1;
        }
        if self.position == start || self.input[start].is_ascii_digit() {
            return self.error(PathErrorKind::Syntax);
        }
        let mut encoded = Vec::with_capacity(self.position - start + 2);
        encoded.push(b'"');
        encoded.extend_from_slice(&self.input[start..self.position]);
        encoded.push(b'"');
        serde_json::from_slice(&encoded).map_err(|_| PathError {
            kind: PathErrorKind::Syntax,
            byte_offset: start,
        })
    }

    fn parse_recursive_descent(&mut self) -> PathResult<()> {
        self.position += 1;
        self.expect(b'*')?;
        self.skip_whitespace();
        if self.position == self.input.len() || self.input[self.position] == b'*' {
            return self.error(PathErrorKind::Syntax);
        }
        self.push(PathStep::RecursiveDescent)
    }

    fn parse_array(&mut self) -> PathResult<()> {
        self.position += 1;
        self.skip_whitespace();
        if self.consume(b'*') {
            self.skip_whitespace();
            self.expect(b']')?;
            return self.push(PathStep::ArrayWildcard);
        }
        let first = self.parse_array_index()?;
        self.skip_whitespace();
        let step = if self.consume_range_separator() {
            let last = self.parse_array_index()?;
            if first.from_end == last.from_end
                && ((first.from_end && first.value < last.value)
                    || (!first.from_end && last.value < first.value))
            {
                return self.error(PathErrorKind::Syntax);
            }
            PathStep::ArrayRange(first, last)
        } else {
            PathStep::ArrayCell(first)
        };
        self.skip_whitespace();
        self.expect(b']')?;
        self.push(step)
    }

    fn parse_array_index(&mut self) -> PathResult<ArrayIndex> {
        self.skip_whitespace();
        let from_end = if self.input[self.position..].starts_with(b"last") {
            self.position += 4;
            self.skip_whitespace();
            true
        } else {
            false
        };
        let value = if from_end {
            if self.consume(b'-') {
                self.parse_unsigned()?
            } else {
                0
            }
        } else {
            self.parse_unsigned()?
        };
        Ok(ArrayIndex { value, from_end })
    }

    fn parse_unsigned(&mut self) -> PathResult<u64> {
        self.skip_whitespace();
        let start = self.position;
        let mut value = 0u64;
        while self.position < self.input.len() && self.input[self.position].is_ascii_digit() {
            value = value
                .checked_mul(10)
                .and_then(|value| value.checked_add(u64::from(self.input[self.position] - b'0')))
                .filter(|value| *value <= MAX_ARRAY_INDEX)
                .ok_or_else(|| PathError {
                    kind: PathErrorKind::Syntax,
                    byte_offset: start,
                })?;
            self.position += 1;
        }
        if self.position == start {
            return self.error(PathErrorKind::Syntax);
        }
        Ok(value)
    }

    fn consume_range_separator(&mut self) -> bool {
        if self.position == 0
            || self.input[self.position - 1] != b' '
            || !self.input[self.position..].starts_with(b"to")
            || self.input.get(self.position + 2) != Some(&b' ')
        {
            return false;
        }
        self.position += 3;
        true
    }

    fn push(&mut self, step: PathStep) -> PathResult<()> {
        if self.steps.len() >= MAX_PATH_NODES {
            return self.error(PathErrorKind::TooManyNodes);
        }
        self.steps.push(step);
        Ok(())
    }

    fn skip_whitespace(&mut self) {
        while self.position < self.input.len()
            && matches!(self.input[self.position], b' ' | b'\t' | b'\n')
        {
            self.position += 1;
        }
    }

    fn expect(&mut self, expected: u8) -> PathResult<()> {
        if self.consume(expected) {
            Ok(())
        } else {
            self.error(PathErrorKind::Syntax)
        }
    }

    fn consume(&mut self, expected: u8) -> bool {
        if self.input.get(self.position) == Some(&expected) {
            self.position += 1;
            true
        } else {
            false
        }
    }

    fn error<T>(&self, kind: PathErrorKind) -> PathResult<T> {
        Err(PathError {
            kind,
            byte_offset: self.position,
        })
    }
}

fn is_key_terminator(byte: u8) -> bool {
    matches!(byte, b' ' | b'.' | b'[' | b'*' | b'(' | b'?' | b'-')
}
