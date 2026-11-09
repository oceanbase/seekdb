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

use crate::path::{ArrayIndex, PathResult, MAX_PATH_NODES};
use crate::{PathError, PathErrorKind};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PathRoot {
    Root,
    Current,
}

#[derive(Clone, Debug, PartialEq)]
pub struct SqlPath {
    root: PathRoot,
    steps: Vec<SqlPathStep>,
    lax: bool,
}

#[derive(Clone, Debug, PartialEq)]
pub enum SqlPathStep {
    Member(String),
    MemberWildcard,
    Array(Vec<ArraySelector>),
    ArrayWildcard,
    Descendant,
    Method(ItemMethod),
    Filter(FilterExpr),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ArraySelector {
    Cell(ArrayIndex),
    Range(ArrayIndex, ArrayIndex),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ItemMethod {
    Abs,
    Boolean,
    BooleanOnly,
    Ceiling,
    Date,
    Double,
    Floor,
    Length,
    Lower,
    Number,
    NumberOnly,
    Size,
    String,
    StringOnly,
    Timestamp,
    Type,
    Upper,
}

#[derive(Clone, Debug, PartialEq)]
pub enum FilterExpr {
    Or(Box<FilterExpr>, Box<FilterExpr>),
    And(Box<FilterExpr>, Box<FilterExpr>),
    Not(Box<FilterExpr>),
    Exists(SqlPath, bool),
    Compare(Comparison, Operand, Operand),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Comparison {
    Equal,
    NotEqual,
    Greater,
    GreaterEqual,
    Less,
    LessEqual,
    Substring,
    StartsWith,
    Like,
    LikeRegex,
    EqRegex,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Operand {
    Path(SqlPath),
    Literal(FilterLiteral),
    Variable(String),
}

#[derive(Clone, Debug, PartialEq)]
pub enum FilterLiteral {
    Null,
    Boolean(bool),
    Number(String),
    String(String),
}

impl SqlPath {
    pub fn parse(input: &[u8]) -> PathResult<Self> {
        Parser::new(input).parse()
    }

    pub fn root(&self) -> PathRoot {
        self.root
    }

    pub fn steps(&self) -> &[SqlPathStep] {
        &self.steps
    }

    pub fn is_lax(&self) -> bool {
        self.lax
    }

    pub fn can_match_many(&self) -> bool {
        self.steps.iter().any(|step| {
            matches!(
                step,
                SqlPathStep::MemberWildcard
                    | SqlPathStep::ArrayWildcard
                    | SqlPathStep::Descendant
                    | SqlPathStep::Filter(_)
            ) || match step {
                SqlPathStep::Array(selectors) => {
                    selectors.len() > 1
                        || selectors
                            .iter()
                            .any(|selector| matches!(selector, ArraySelector::Range(_, _)))
                }
                _ => false,
            }
        })
    }

    pub fn last_method(&self) -> Option<ItemMethod> {
        match self.steps.last() {
            Some(SqlPathStep::Method(method)) => Some(*method),
            _ => None,
        }
    }
}

struct Parser<'a> {
    input: &'a [u8],
    position: usize,
    filter_depth: u32,
    lax: bool,
}

impl<'a> Parser<'a> {
    fn new(input: &'a [u8]) -> Self {
        Self {
            input,
            position: 0,
            filter_depth: 0,
            lax: true,
        }
    }

    fn parse(mut self) -> PathResult<SqlPath> {
        self.skip_whitespace();
        let lax = if self.consume_keyword(b"lax") {
            true
        } else {
            !self.consume_keyword(b"strict")
        };
        self.lax = lax;
        self.skip_whitespace();
        let path = self.parse_path(PathRoot::Root, lax)?;
        self.skip_whitespace();
        if self.position != self.input.len() {
            return self.error();
        }
        Ok(path)
    }

    fn parse_path(&mut self, root: PathRoot, lax: bool) -> PathResult<SqlPath> {
        self.skip_whitespace();
        self.expect(match root {
            PathRoot::Root => b'$',
            PathRoot::Current => b'@',
        })?;
        let mut steps = Vec::new();
        loop {
            self.skip_whitespace();
            match self.peek() {
                Some(b'.') => {
                    self.position += 1;
                    if self.consume(b'.') {
                        self.push_step(&mut steps, SqlPathStep::Descendant)?;
                        self.skip_whitespace();
                        let step = if self.consume(b'[') {
                            self.parse_array()?
                        } else {
                            self.parse_member()?
                        };
                        self.push_step(&mut steps, step)?;
                    } else {
                        let step = self.parse_member()?;
                        self.push_step(&mut steps, step)?;
                    }
                }
                Some(b'[') => {
                    self.position += 1;
                    let step = self.parse_array()?;
                    self.push_step(&mut steps, step)?;
                }
                Some(b'?') => {
                    self.position += 1;
                    let filter = self.parse_filter()?;
                    self.push_step(&mut steps, SqlPathStep::Filter(filter))?;
                    self.skip_whitespace();
                    if matches!(self.peek(), Some(b'[' | b'?')) {
                        return self.error();
                    }
                }
                _ => break,
            }
        }
        if matches!(steps.last(), Some(SqlPathStep::Descendant))
            || steps.iter().enumerate().any(|(index, step)| {
                matches!(step, SqlPathStep::Method(_)) && index + 1 != steps.len()
            })
        {
            return self.error();
        }
        Ok(SqlPath { root, steps, lax })
    }

    fn parse_member(&mut self) -> PathResult<SqlPathStep> {
        self.skip_whitespace();
        if self.consume(b'*') {
            return Ok(SqlPathStep::MemberWildcard);
        }
        if self.peek() == Some(b'"') {
            return self.parse_quoted_string().map(SqlPathStep::Member);
        }
        let start = self.position;
        while self.peek().is_some_and(|byte| !is_member_terminator(byte)) {
            self.position += 1;
        }
        if self.position == start {
            return self.error();
        }
        let name = &self.input[start..self.position];
        if self.peek() == Some(b'(') {
            self.position += 1;
            self.skip_whitespace();
            self.expect(b')')?;
            return Ok(SqlPathStep::Method(parse_method(name).ok_or(PathError {
                kind: PathErrorKind::Syntax,
                byte_offset: start,
            })?));
        }
        let key = {
            if name[0].is_ascii_digit() {
                return self.error();
            }
            String::from_utf8(name.to_vec()).map_err(|_| PathError {
                kind: PathErrorKind::Syntax,
                byte_offset: start,
            })?
        };
        Ok(SqlPathStep::Member(key))
    }

    fn parse_array(&mut self) -> PathResult<SqlPathStep> {
        self.skip_whitespace();
        if self.consume(b'*') {
            self.skip_whitespace();
            self.expect(b']')?;
            return Ok(SqlPathStep::ArrayWildcard);
        }
        let mut selectors = Vec::new();
        loop {
            self.skip_whitespace();
            let first = self.parse_array_index()?;
            self.skip_whitespace();
            let selector = if self.consume_keyword(b"to") {
                self.skip_whitespace();
                let last = self.parse_array_index()?;
                ArraySelector::Range(first, last)
            } else {
                ArraySelector::Cell(first)
            };
            selectors.push(selector);
            self.skip_whitespace();
            if !self.consume(b',') {
                break;
            }
        }
        self.expect(b']')?;
        if selectors.is_empty() {
            return self.error();
        }
        Ok(SqlPathStep::Array(selectors))
    }

    fn parse_array_index(&mut self) -> PathResult<ArrayIndex> {
        self.skip_whitespace();
        let from_end = if self.consume_keyword(b"last") {
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
        let start = self.position;
        let mut value = 0u64;
        while self.peek().is_some_and(|byte| byte.is_ascii_digit()) {
            value = value
                .checked_mul(10)
                .and_then(|value| value.checked_add(u64::from(self.input[self.position] - b'0')))
                .ok_or(PathError {
                    kind: PathErrorKind::Syntax,
                    byte_offset: start,
                })?;
            self.position += 1;
        }
        if self.position == start {
            return self.error();
        }
        Ok(value)
    }

    fn parse_filter(&mut self) -> PathResult<FilterExpr> {
        self.skip_whitespace();
        self.expect(b'(')?;
        self.enter_filter()?;
        self.skip_whitespace();
        let result = self.parse_or();
        self.skip_whitespace();
        let result = result.and_then(|filter| {
            self.expect(b')')?;
            Ok(filter)
        });
        self.leave_filter();
        result
    }

    fn enter_filter(&mut self) -> PathResult<()> {
        self.filter_depth += 1;
        if self.filter_depth > MAX_PATH_NODES as u32 {
            return Err(PathError {
                kind: PathErrorKind::TooManyNodes,
                byte_offset: self.position,
            });
        }
        Ok(())
    }

    fn leave_filter(&mut self) {
        self.filter_depth -= 1;
    }

    fn parse_or(&mut self) -> PathResult<FilterExpr> {
        let mut expression = self.parse_and()?;
        loop {
            self.skip_whitespace();
            if !self.consume_bytes(b"||") {
                break;
            }
            self.skip_whitespace();
            expression = FilterExpr::Or(Box::new(expression), Box::new(self.parse_and()?));
        }
        Ok(expression)
    }

    fn parse_and(&mut self) -> PathResult<FilterExpr> {
        let mut expression = self.parse_unary()?;
        loop {
            self.skip_whitespace();
            if !self.consume_bytes(b"&&") {
                break;
            }
            self.skip_whitespace();
            expression = FilterExpr::And(Box::new(expression), Box::new(self.parse_unary()?));
        }
        Ok(expression)
    }

    fn parse_unary(&mut self) -> PathResult<FilterExpr> {
        if self.peek() == Some(b'!') && !self.starts_exists() {
            self.position += 1;
            self.skip_whitespace();
            self.enter_filter()?;
            let expression = self.parse_unary();
            self.leave_filter();
            return Ok(FilterExpr::Not(Box::new(expression?)));
        }
        if self.consume(b'(') {
            self.skip_whitespace();
            self.enter_filter()?;
            let expression = self.parse_or();
            self.leave_filter();
            let expression = expression?;
            self.skip_whitespace();
            self.expect(b')')?;
            return Ok(expression);
        }
        self.parse_predicate()
    }

    fn parse_predicate(&mut self) -> PathResult<FilterExpr> {
        let negated = if self.consume(b'!') {
            self.skip_whitespace();
            self.expect_keyword(b"exists")?;
            true
        } else if self.consume_keyword(b"exists") {
            false
        } else {
            let left = self.parse_operand()?;
            self.skip_whitespace();
            let comparison = self.parse_comparison()?;
            self.skip_whitespace();
            let right = self.parse_operand()?;
            return Ok(FilterExpr::Compare(comparison, left, right));
        };
        self.skip_whitespace();
        self.expect(b'(')?;
        self.skip_whitespace();
        let path = self.parse_path(PathRoot::Current, self.lax)?;
        self.skip_whitespace();
        self.expect(b')')?;
        Ok(FilterExpr::Exists(path, negated))
    }

    fn parse_operand(&mut self) -> PathResult<Operand> {
        self.skip_whitespace();
        match self.peek() {
            Some(b'@') => self
                .parse_path(PathRoot::Current, self.lax)
                .map(Operand::Path),
            Some(b'$') => self.parse_path(PathRoot::Root, self.lax).map(Operand::Path),
            Some(b'"') => {
                let value = self.parse_quoted_string()?;
                Ok(Operand::Literal(FilterLiteral::String(value)))
            }
            Some(b'0'..=b'9' | b'-' | b'.') => {
                let start = self.position;
                while self.peek().is_some_and(|byte| {
                    matches!(byte, b'0'..=b'9' | b'-' | b'+' | b'.' | b'e' | b'E')
                }) {
                    self.position += 1;
                }
                let number =
                    String::from_utf8(self.input[start..self.position].to_vec()).map_err(|_| {
                        PathError {
                            kind: PathErrorKind::Syntax,
                            byte_offset: start,
                        }
                    })?;
                if number.parse::<f64>().is_err() {
                    return Err(PathError {
                        kind: PathErrorKind::Syntax,
                        byte_offset: start,
                    });
                }
                Ok(Operand::Literal(FilterLiteral::Number(number)))
            }
            Some(byte) if is_letter(byte) => {
                let start = self.position;
                while self.peek().is_some_and(|byte| {
                    is_letter(byte) || byte.is_ascii_digit() || matches!(byte, b'_' | b'$')
                }) {
                    self.position += 1;
                }
                let name = &self.input[start..self.position];
                match name {
                    b"true" => Ok(Operand::Literal(FilterLiteral::Boolean(true))),
                    b"false" => Ok(Operand::Literal(FilterLiteral::Boolean(false))),
                    b"null" => Ok(Operand::Literal(FilterLiteral::Null)),
                    _ => Ok(Operand::Variable(
                        String::from_utf8(name.to_vec()).map_err(|_| PathError {
                            kind: PathErrorKind::Syntax,
                            byte_offset: start,
                        })?,
                    )),
                }
            }
            _ => self.error(),
        }
    }

    fn parse_quoted_string(&mut self) -> PathResult<String> {
        let start = self.position;
        self.position += 1;
        while self.position < self.input.len() {
            match self.input[self.position] {
                b'\\' => self.position = (self.position + 2).min(self.input.len()),
                b'"' => {
                    self.position += 1;
                    return serde_json::from_slice(&self.input[start..self.position]).map_err(
                        |error| PathError {
                            kind: PathErrorKind::Syntax,
                            byte_offset: start + error.column().saturating_sub(1),
                        },
                    );
                }
                _ => self.position += 1,
            }
        }
        self.error()
    }

    fn parse_comparison(&mut self) -> PathResult<Comparison> {
        let comparison = if self.consume_bytes(b"==") {
            Comparison::Equal
        } else if self.consume_bytes(b"!=") {
            Comparison::NotEqual
        } else if self.consume_bytes(b">=") {
            Comparison::GreaterEqual
        } else if self.consume_bytes(b"<=") {
            Comparison::LessEqual
        } else if self.consume(b'>') {
            Comparison::Greater
        } else if self.consume(b'<') {
            Comparison::Less
        } else if self.consume_keyword(b"like_regex") {
            Comparison::LikeRegex
        } else if self.consume_keyword(b"eq_regex") {
            Comparison::EqRegex
        } else if self.consume_keyword(b"like") {
            Comparison::Like
        } else if self.consume_keyword(b"has") {
            self.skip_whitespace();
            self.expect_keyword(b"substring")?;
            Comparison::Substring
        } else if self.consume_keyword(b"starts") {
            self.skip_whitespace();
            self.expect_keyword(b"with")?;
            Comparison::StartsWith
        } else {
            return self.error();
        };
        Ok(comparison)
    }

    fn push_step(&self, steps: &mut Vec<SqlPathStep>, step: SqlPathStep) -> PathResult<()> {
        if steps.len() >= MAX_PATH_NODES {
            return Err(PathError {
                kind: PathErrorKind::TooManyNodes,
                byte_offset: self.position,
            });
        }
        steps.push(step);
        Ok(())
    }

    fn consume_keyword(&mut self, keyword: &[u8]) -> bool {
        if !self.remaining().starts_with(keyword) {
            return false;
        }
        self.position += keyword.len();
        true
    }

    fn starts_exists(&self) -> bool {
        let mut position = self.position;
        if self.input.get(position) == Some(&b'!') {
            position += 1;
            while self
                .input
                .get(position)
                .is_some_and(u8::is_ascii_whitespace)
            {
                position += 1;
            }
        }
        self.input[position..].starts_with(b"exists")
    }

    fn expect_keyword(&mut self, keyword: &[u8]) -> PathResult<()> {
        if self.consume_keyword(keyword) {
            Ok(())
        } else {
            self.error()
        }
    }

    fn remaining(&self) -> &[u8] {
        &self.input[self.position..]
    }

    fn peek(&self) -> Option<u8> {
        self.input.get(self.position).copied()
    }

    fn consume(&mut self, expected: u8) -> bool {
        if self.peek() == Some(expected) {
            self.position += 1;
            true
        } else {
            false
        }
    }

    fn consume_bytes(&mut self, expected: &[u8]) -> bool {
        if self.remaining().starts_with(expected) {
            self.position += expected.len();
            true
        } else {
            false
        }
    }

    fn expect(&mut self, expected: u8) -> PathResult<()> {
        if self.consume(expected) {
            Ok(())
        } else {
            self.error()
        }
    }

    fn skip_whitespace(&mut self) {
        while self.peek().is_some_and(|byte| byte.is_ascii_whitespace()) {
            self.position += 1;
        }
    }

    fn error<T>(&self) -> PathResult<T> {
        Err(PathError {
            kind: PathErrorKind::Syntax,
            byte_offset: self.position,
        })
    }
}

fn parse_method(name: &[u8]) -> Option<ItemMethod> {
    Some(match name {
        b"abs" => ItemMethod::Abs,
        b"boolean" => ItemMethod::Boolean,
        b"booleanOnly" => ItemMethod::BooleanOnly,
        b"ceiling" => ItemMethod::Ceiling,
        b"date" => ItemMethod::Date,
        b"double" => ItemMethod::Double,
        b"floor" => ItemMethod::Floor,
        b"length" => ItemMethod::Length,
        b"lower" => ItemMethod::Lower,
        b"number" => ItemMethod::Number,
        b"numberOnly" => ItemMethod::NumberOnly,
        b"size" => ItemMethod::Size,
        b"string" => ItemMethod::String,
        b"stringOnly" => ItemMethod::StringOnly,
        b"timestamp" => ItemMethod::Timestamp,
        b"type" => ItemMethod::Type,
        b"upper" => ItemMethod::Upper,
        _ => return None,
    })
}

fn is_member_terminator(byte: u8) -> bool {
    byte.is_ascii_whitespace()
        || matches!(
            byte,
            b'.' | b'['
                | b']'
                | b'?'
                | b'('
                | b')'
                | b','
                | b'='
                | b'!'
                | b'<'
                | b'>'
                | b'&'
                | b'|'
        )
}

fn is_letter(byte: u8) -> bool {
    byte.is_ascii_alphabetic() || byte >= 0x80 || byte == b'_'
}
