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

use crate::{binary, BinaryDocument, BinaryResult, BinaryValue, JsonType, ParseOptions};
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
    document: BinaryDocument<Vec<u8>>,
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
        evaluate(root, &self.steps, 0, options, &mut matches)?;
        Ok(matches)
    }
}

fn evaluate<'a>(
    value: BinaryValue<'a>,
    steps: &[PathStep],
    index: usize,
    options: QueryOptions,
    matches: &mut Vec<BinaryValue<'a>>,
) -> BinaryResult<bool> {
    if index == steps.len() {
        matches.push(value);
        return Ok(options.first_only);
    }
    match &steps[index] {
        PathStep::Member(key) => {
            if value.json_type()? == JsonType::Object {
                if let Some(member) = value.member(key.as_bytes())? {
                    return evaluate(member, steps, index + 1, options, matches);
                }
            }
        }
        PathStep::MemberWildcard => {
            if value.json_type()? == JsonType::Object {
                for child_index in 0..value.len()? {
                    if evaluate(
                        value.value(child_index)?,
                        steps,
                        index + 1,
                        options,
                        matches,
                    )? {
                        return Ok(true);
                    }
                }
            }
        }
        PathStep::ArrayCell(cell) => {
            if value.json_type()? == JsonType::Array {
                if let Some(child_index) = resolve_index(*cell, value.len()?) {
                    return evaluate(
                        value.value(child_index)?,
                        steps,
                        index + 1,
                        options,
                        matches,
                    );
                }
            } else if options.auto_wrap && resolve_index(*cell, 1).is_some() {
                return evaluate(value, steps, index + 1, options, matches);
            }
        }
        PathStep::ArrayRange(first, last) => {
            if value.json_type()? == JsonType::Array {
                let (begin, end) = resolve_range(*first, *last, value.len()?);
                for child_index in begin..end {
                    if evaluate(
                        value.value(child_index)?,
                        steps,
                        index + 1,
                        options,
                        matches,
                    )? {
                        return Ok(true);
                    }
                }
            } else if options.auto_wrap {
                let (begin, end) = resolve_range(*first, *last, 1);
                if begin < end {
                    return evaluate(value, steps, index + 1, options, matches);
                }
            }
        }
        PathStep::ArrayWildcard => {
            if value.json_type()? == JsonType::Array {
                for child_index in 0..value.len()? {
                    if evaluate(
                        value.value(child_index)?,
                        steps,
                        index + 1,
                        options,
                        matches,
                    )? {
                        return Ok(true);
                    }
                }
            }
        }
        PathStep::RecursiveDescent => {
            if evaluate(value, steps, index + 1, options, matches)? {
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
                if evaluate(child, steps, index + 1, options, matches)? {
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
