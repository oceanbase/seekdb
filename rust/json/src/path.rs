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
    binary, BinaryDocument, BinaryError, BinaryErrorKind, BinaryResult, BinaryValue, CompareResult, JsonType, ParseOptions,
};
use std::collections::HashSet;
use std::fmt;
use crate::ValueRef;

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
    pub use_wrapper: bool,
    pub wrapper_required: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WrapperPolicy {
    Without,
    With,
    Conditional,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct QueryOutputOptions {
    pub wrapper: WrapperPolicy,
    pub allow_scalar: bool,
    pub wrap_single: bool,
    pub wrap_multiple: bool,
}

impl Default for QueryOutputOptions {
    fn default() -> Self {
        Self {
            wrapper: WrapperPolicy::Without,
            allow_scalar: true,
            wrap_single: false,
            wrap_multiple: false,
        }
    }
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

    pub fn into_value(self) -> BinaryResult<crate::Value> {
        crate::Value::root(self.document)
    }

    pub fn query_output(&self, path: &Path, wrap_result: bool) -> BinaryResult<QueryOutput> {
        self.query_output_with_options(
            path,
            QueryOutputOptions {
                wrapper: if wrap_result {
                    WrapperPolicy::With
                } else {
                    WrapperPolicy::Without
                },
                ..QueryOutputOptions::default()
            },
        )
    }

    pub fn first_value(&self, path: &Path, auto_wrap: bool) -> BinaryResult<Option<Vec<u8>>> {
        let matches = self.query(path, QueryOptions { auto_wrap, first_only: true })?;
        matches.first().map(|value| binary::serialize_constructed_value(*value)).transpose()
    }

    pub fn root_output(&self, wrap_result: bool) -> BinaryResult<QueryOutput> {
        let root = self.document.root()?;
        let binary = if wrap_result {
            Some(constructed_array(&[root])?)
        } else {
            Some(binary::serialize_constructed_value(root)?)
        };
        Ok(QueryOutput {
            match_count: 1,
            value_type: Some(root.json_type()?),
            binary,
            use_wrapper: wrap_result,
            wrapper_required: false,
        })
    }

    pub fn query_output_with_options(
        &self,
        path: &Path,
        options: QueryOutputOptions,
    ) -> BinaryResult<QueryOutput> {
        let matches = self.query(
            path,
            QueryOptions {
                auto_wrap: true,
                first_only: false,
            },
        )?;
        build_query_output(matches, options)
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
            binary::serialize_constructed_value(matches[0]).map(Some)
        } else {
            constructed_array(&matches).map(Some)
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
        crate::compare::without_temporal(self.contains_with(
            candidate,
            path,
            &mut crate::compare::NoTemporal,
        ))
    }

    pub fn overlaps(&self, other: &Query) -> CompareResult<bool> {
        crate::compare::without_temporal(self.overlaps_with(other, &mut crate::compare::NoTemporal))
    }

    pub fn member_of(&self, target: &Query) -> CompareResult<bool> {
        crate::compare::without_temporal(
            self.member_of_with(target, &mut crate::compare::NoTemporal),
        )
    }

    pub fn contains_with<C: crate::compare::ScalarComparator>(
        &self,
        candidate: &Query,
        path: Option<&Path>,
        scalar: &mut C,
    ) -> Result<Option<bool>, crate::compare::OrderError<C::Error>> {
        let target = if let Some(path) = path {
            let matches = self.query(path, QueryOptions::default())?;
            let Some(target) = matches.first() else {
                return Ok(None);
            };
            *target
        } else {
            self.document.root()?
        };
        crate::compare::contains_with(target, candidate.document.root()?, scalar).map(Some)
    }

    pub fn overlaps_with<C: crate::compare::ScalarComparator>(
        &self,
        other: &Query,
        scalar: &mut C,
    ) -> Result<bool, crate::compare::OrderError<C::Error>> {
        crate::compare::overlaps_with(self.document.root()?, other.document.root()?, scalar)
    }

    pub fn member_of_with<C: crate::compare::ScalarComparator>(
        &self,
        target: &Query,
        scalar: &mut C,
    ) -> Result<bool, crate::compare::OrderError<C::Error>> {
        crate::compare::member_of_with(self.document.root()?, target.document.root()?, scalar)
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

fn constructed_array(values: &[crate::BinaryValue<'_>]) -> BinaryResult<Vec<u8>> {
    let entries: Vec<_> = values.iter().map(|value| (&[][..], *value)).collect();
    binary::serialize_constructed(false, &entries)
}

pub(crate) fn build_query_output(matches: Vec<BinaryValue<'_>>, options: QueryOutputOptions) -> BinaryResult<QueryOutput> {
    build_output(matches, options)
}
pub(crate) fn build_read_output(matches: Vec<ValueRef<'_>>, options: QueryOutputOptions) -> BinaryResult<QueryOutput> {
    build_output(matches, options)
}
fn build_output<'a, V: QueryRead<'a>>(matches: Vec<V>, options: QueryOutputOptions) -> BinaryResult<QueryOutput> {
    let value_type = match matches.first() {
        Some(value) => Some(value.kind()?),
        None => None,
    };
    let scalar_disallowed =
        matches.len() == 1 && !options.allow_scalar && !matches[0].kind()?.is_container();
    let wrapper_required = !matches.is_empty()
        && ((matches.len() > 1
            && !options.wrap_multiple
            && options.wrapper == WrapperPolicy::Without)
            || (scalar_disallowed
                && !options.wrap_single
                && options.wrapper == WrapperPolicy::Without));
    let use_wrapper = !wrapper_required
        && !matches.is_empty()
        && ((matches.len() == 1 && options.wrap_single)
            || (matches.len() > 1 && options.wrap_multiple)
            || options.wrapper == WrapperPolicy::With
            || (options.wrapper == WrapperPolicy::Conditional
                && (matches.len() > 1 || scalar_disallowed)));
    let binary = if use_wrapper {
        Some(V::render(&matches, true)?)
    } else if matches.len() == 1 && !wrapper_required {
        Some(V::render(&matches, false)?)
    } else {
        None
    };
    Ok(QueryOutput {
        match_count: matches.len(),
        value_type,
        binary,
        use_wrapper,
        wrapper_required,
    })
}

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

    pub fn query<'a>(&self, root: BinaryValue<'a>, options: QueryOptions) -> BinaryResult<Vec<BinaryValue<'a>>> {
        let mut matches = Vec::new();
        evaluate(root, &self.steps, 0, options, &mut |value, _: &Trace| matches.push(value), &mut Trace::default())?;
        Ok(deduplicate(matches))
    }

    pub(crate) fn query_read<'a>(&self, root: ValueRef<'a>, options: QueryOptions) -> BinaryResult<Vec<ValueRef<'a>>> {
        let mut matches = Vec::new();
        self.visit_read(root, options, false, |value, _| matches.push(value))?;
        Ok(matches)
    }

    pub(crate) fn visit_read<'a>(&self, root: ValueRef<'a>, options: QueryOptions, coordinates: bool,
        mut emit: impl FnMut(ValueRef<'a>, &[u64])) -> BinaryResult<()> {
        let mut trace = Trace { coordinates, ..Default::default() };
        evaluate(root, &self.steps, 0, options, &mut |value, trace: &Trace| emit(value, &trace.indices), &mut trace)?;
        Ok(())
    }

    pub(crate) fn query_located<'a>(&self, root: BinaryValue<'a>, options: QueryOptions)
        -> BinaryResult<Vec<(BinaryValue<'a>, String)>> {
        let mut trace = Trace { locations: true, location: String::from("$"), ..Default::default() };
        let mut matches = Vec::new();
        evaluate(root, &self.steps, 0, options,
            &mut |value, trace: &Trace| matches.push((value, trace.location.clone())), &mut trace)?;
        Ok(matches)
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

trait QueryRead<'a>: Copy {
    fn kind(self) -> BinaryResult<JsonType>;
    fn count(self) -> BinaryResult<u64>;
    fn child(self, index: u64) -> BinaryResult<Self>;
    fn key(self, index: u64) -> BinaryResult<&'a [u8]>;
    fn member(self, key: &[u8]) -> BinaryResult<Option<(u64, Self)>>;
    fn identity(self) -> usize;
    fn error_offset(self) -> usize;
    fn render(values: &[Self], wrapper: bool) -> BinaryResult<Vec<u8>>;
}
impl<'a> QueryRead<'a> for BinaryValue<'a> {
    fn kind(self) -> BinaryResult<JsonType> { self.json_type() }
    fn count(self) -> BinaryResult<u64> { self.len() }
    fn child(self, index: u64) -> BinaryResult<Self> { self.value(index) }
    fn key(self, index: u64) -> BinaryResult<&'a [u8]> { BinaryValue::key(&self, index) }
    fn member(self, key: &[u8]) -> BinaryResult<Option<(u64, Self)>> { self.member_index(key) }
    fn identity(self) -> usize { self.storage_offset() }
    fn error_offset(self) -> usize { self.storage_offset() }
    fn render(values: &[Self], wrapper: bool) -> BinaryResult<Vec<u8>> {
        if wrapper { constructed_array(values) } else { binary::serialize_constructed_value(values[0]) }
    }
}
impl<'a> QueryRead<'a> for ValueRef<'a> {
    fn kind(self) -> BinaryResult<JsonType> { self.json_type() }
    fn count(self) -> BinaryResult<u64> { self.len() }
    fn child(self, index: u64) -> BinaryResult<Self> { self.value(index) }
    fn key(self, index: u64) -> BinaryResult<&'a [u8]> { ValueRef::key(self, index) }
    fn member(self, key: &[u8]) -> BinaryResult<Option<(u64, Self)>> { self.member_index(key) }
    fn identity(self) -> usize { ValueRef::identity(self).1 }
    fn error_offset(self) -> usize { ValueRef::error_offset(self) }
    fn render(values: &[Self], wrapper: bool) -> BinaryResult<Vec<u8>> { crate::value::render(values, wrapper) }
}

#[derive(Default)]
struct Trace {
    locations: bool,
    location: String,
    coordinates: bool,
    indices: Vec<u64>,
}
impl Trace {
    fn mark(&self) -> (usize, usize) { (self.location.len(), self.indices.len()) }
    fn enter_member(&mut self, index: u64, key: &[u8]) {
        if self.locations { append_raw_member(&mut self.location, &String::from_utf8_lossy(key)); }
        if self.coordinates { self.indices.push(index); }
    }
    fn enter_array(&mut self, index: u64) {
        if self.locations { append_array_location(&mut self.location, index); }
        if self.coordinates { self.indices.push(index); }
    }
    fn restore(&mut self, mark: (usize, usize)) {
        if self.locations { self.location.truncate(mark.0); }
        if self.coordinates { self.indices.truncate(mark.1); }
    }
}

fn deduplicate<'a, V: QueryRead<'a>>(matches: Vec<V>) -> Vec<V> {
    let mut seen = HashSet::with_capacity(matches.len());
    matches
        .into_iter()
        .filter(|value| seen.insert(value.identity()))
        .collect()
}

fn evaluate<'a, V: QueryRead<'a>, F: FnMut(V, &Trace)>(
    value: V, steps: &[PathStep], index: usize, options: QueryOptions,
    emit: &mut F, trace: &mut Trace,
) -> BinaryResult<bool> {
    if index == steps.len() {
        emit(value, trace); return Ok(options.first_only);
    }
    match &steps[index] {
        PathStep::Member(key) => {
            if value.kind()? == JsonType::Object {
                if let Some((child_index, member)) = value.member(key.as_bytes())? {
                    let mark = trace.mark(); trace.enter_member(child_index, key.as_bytes());
                    let result = evaluate(member, steps, index + 1, options, emit, trace);
                    trace.restore(mark); return result;
                }
            }
        }
        PathStep::MemberWildcard => {
            if value.kind()? == JsonType::Object {
                for child_index in 0..value.count()? {
                    let mark = trace.mark();
                    if trace.locations { trace.enter_member(child_index, value.key(child_index)?); }
                    else if trace.coordinates { trace.indices.push(child_index); }
                    let result = evaluate(value.child(child_index)?, steps, index + 1, options, emit, trace);
                    trace.restore(mark); if result? { return Ok(true); }
                }
            }
        }
        PathStep::ArrayCell(cell) => {
            if value.kind()? == JsonType::Array {
                if let Some(child_index) = resolve_index(*cell, value.count()?) {
                    let mark = trace.mark(); trace.enter_array(child_index);
                    let result = evaluate(value.child(child_index)?, steps, index + 1, options, emit, trace);
                    trace.restore(mark); return result;
                }
            } else if options.auto_wrap && resolve_index(*cell, 1).is_some() {
                return evaluate(value, steps, index + 1, options, emit, trace);
            }
        }
        PathStep::ArrayRange(first, last) => {
            if value.kind()? == JsonType::Array {
                let (begin, end) = resolve_range(*first, *last, value.count()?);
                for child_index in begin..end {
                    let mark = trace.mark(); trace.enter_array(child_index);
                    let result = evaluate(value.child(child_index)?, steps, index + 1, options, emit, trace);
                    trace.restore(mark); if result? { return Ok(true); }
                }
            } else if options.auto_wrap {
                let (begin, end) = resolve_range(*first, *last, 1);
                if begin < end { return evaluate(value, steps, index + 1, options, emit, trace); }
            }
        }
        PathStep::ArrayWildcard => {
            if value.kind()? == JsonType::Array {
                for child_index in 0..value.count()? {
                    let mark = trace.mark(); trace.enter_array(child_index);
                    let result = evaluate(value.child(child_index)?, steps, index + 1, options, emit, trace);
                    trace.restore(mark); if result? { return Ok(true); }
                }
            }
        }
        PathStep::RecursiveDescent => {
            if evaluate(value, steps, index + 1, options, emit, trace)? { return Ok(true); }
            let original = trace.mark();
            let result = (|| {
                let mut stack = Vec::new();
                let mut active = std::collections::HashSet::new();
                if value.kind()?.is_container() { active.insert(value.identity()); stack.push((value, 0, trace.mark())); }
                while let Some((container, child_index, mark)) = stack.last_mut() {
                    trace.restore(*mark);
                    if *child_index == container.count()? { active.remove(&container.identity()); stack.pop(); continue; }
                    let index_in_container = *child_index;
                    let child = container.child(index_in_container)?; *child_index += 1;
                    if trace.locations || trace.coordinates {
                        if container.kind()? == JsonType::Object {
                            if trace.locations { trace.enter_member(index_in_container, container.key(index_in_container)?); }
                            else { trace.indices.push(index_in_container); }
                        } else { trace.enter_array(index_in_container); }
                    }
                    if evaluate(child, steps, index + 1, options, emit, trace)? { return Ok(true); }
                    if child.kind()?.is_container() {
                        if !active.insert(child.identity()) { return Err(BinaryError::new(BinaryErrorKind::InvalidDepth, child.error_offset())); }
                        stack.push((child, 0, trace.mark()));
                    }
                }
                Ok(false)
            })();
            trace.restore(original); return result;
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
                .ok_or(PathError {
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
