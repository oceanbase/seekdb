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

use crate::bridge::ffi::{PlPrefix, StatementClass, StatementInfo, StatementSplits, TransactionCommand};
use crate::lex::{identifier, quoted_end, space};
use crate::{budget::Budget, grammar, ParseErrorKind, SqlMode, StackBounds};

pub fn transaction_command(sql: &[u8]) -> TransactionCommand {
    if sql.eq_ignore_ascii_case(b"begin") {
        TransactionCommand::Begin
    } else if sql.eq_ignore_ascii_case(b"commit") {
        TransactionCommand::Commit
    } else if sql.eq_ignore_ascii_case(b"rollback") {
        TransactionCommand::Rollback
    } else {
        TransactionCommand::None
    }
}

pub fn insert_outline_hint(sql: &[u8], hint: &[u8], mode: SqlMode) -> Vec<u8> {
    let Ok(sql) = grammar::executable_sql(sql, mode, true) else {
        return Vec::new();
    };
    let sql = sql.as_ref();
    let mut tokens = Tokens::new(sql, mode);
    let Ok(Some(mut first)) = tokens.next() else {
        return Vec::new();
    };
    let start = first.start;
    while first.is(b"(") {
        let Ok(Some(next)) = tokens.next() else {
            return Vec::new();
        };
        first = next;
    }
    let mut result = Vec::with_capacity(sql.len() + hint.len() + 1);
    result.extend_from_slice(&sql[start..first.end]);
    result.push(b' ');
    result.extend_from_slice(hint);
    let rest = if sql.get(first.end).is_some_and(|byte| space(*byte)) {
        first.end + 1
    } else {
        first.end
    };
    result.extend_from_slice(&sql[rest..]);
    result
}

#[derive(Clone, Copy)]
pub(crate) struct Token<'a> {
    pub start: usize,
    pub end: usize,
    pub text: &'a [u8],
}

impl Token<'_> {
    pub fn is(&self, text: &[u8]) -> bool {
        self.text.eq_ignore_ascii_case(text)
    }
}

pub(crate) struct Tokens<'a> {
    sql: &'a [u8],
    pos: usize,
    mode: SqlMode,
}

impl<'a> Tokens<'a> {
    pub fn new(sql: &'a [u8], mode: SqlMode) -> Self {
        Self { sql, pos: 0, mode }
    }

    pub fn at(sql: &'a [u8], pos: usize) -> Self {
        Self {
            sql,
            pos,
            mode: SqlMode::default(),
        }
    }

    pub fn next(&mut self) -> Result<Option<Token<'a>>, ()> {
        while let Some(&byte) = self.sql.get(self.pos) {
            if space(byte) {
                self.pos += 1;
            } else if byte == b'#'
                || (byte == b'-'
                    && self.sql.get(self.pos + 1) == Some(&b'-')
                    && self.sql.get(self.pos + 2).is_none_or(|byte| space(*byte)))
            {
                while self.pos < self.sql.len() && self.sql[self.pos] != b'\n' {
                    self.pos += 1;
                }
            } else if self.sql.get(self.pos..self.pos + 2) == Some(b"/*") {
                let start = self.pos;
                let end = self.sql[self.pos + 2..]
                    .windows(2)
                    .position(|text| text == b"*/")
                    .ok_or(())?
                    + self.pos
                    + 4;
                self.pos = end;
                if self.sql.get(start + 2) == Some(&b'!') {
                    return Ok(Some(Token {
                        start,
                        end,
                        text: &self.sql[start..end],
                    }));
                }
            } else {
                break;
            }
        }
        let start = self.pos;
        let Some(&byte) = self.sql.get(start) else {
            return Ok(None);
        };
        self.pos = if matches!(byte, b'\'' | b'"' | b'`') {
            let escaped = byte != b'`'
                && !(byte == b'"' && self.mode.ansi_quotes)
                && !self.mode.no_backslash_escapes;
            quoted_end(self.sql, start, escaped).ok_or(())?
        } else if identifier(byte) {
            let mut end = start + 1;
            while self.sql.get(end).is_some_and(|byte| identifier(*byte)) {
                end += 1;
            }
            end
        } else {
            [
                b"<=>".as_slice(),
                b"->>",
                b">=",
                b"<=",
                b"<>",
                b"!=",
                b"<<",
                b">>",
                b":=",
                b"||",
                b"&&",
                b"->",
                b"::",
            ]
            .iter()
            .find(|operator| self.sql[start..].starts_with(operator))
            .map_or(start + 1, |operator| start + operator.len())
        };
        Ok(Some(Token {
            start,
            end: self.pos,
            text: &self.sql[start..self.pos],
        }))
    }
}

pub fn normalized_sql_length(sql: &[u8]) -> usize {
    let mut end = sql.len();
    while end > 0 && space(sql[end - 1]) {
        end -= 1;
    }
    if end > 0 && sql[end - 1] == 0 {
        end -= 1;
    }
    while end > 0 && space(sql[end - 1]) {
        end -= 1;
    }
    end
}

fn routine_kind(token: Token<'_>) -> Option<PlPrefix> {
    if token.is(b"function") {
        Some(PlPrefix::Function)
    } else if [
        b"procedure".as_slice(),
        b"package",
        b"trigger",
        b"type",
        b"event",
    ]
    .iter()
    .any(|word| token.is(word))
    {
        Some(PlPrefix::Routine)
    } else {
        None
    }
}

fn classify_pl(first: Token<'_>, tokens: &mut Tokens<'_>) -> PlPrefix {
    if first.is(b"call") {
        return PlPrefix::Call;
    }
    if first.is(b"do") || first.is(b"declare") || first.is(b"signal") || first.is(b"resignal") {
        return PlPrefix::Anonymous;
    }
    if let Some(kind) = routine_kind(first) {
        return kind;
    }
    if first.is(b"<<") {
        return PlPrefix::Anonymous;
    }
    if first.is(b"<") {
        return if tokens
            .next()
            .ok()
            .flatten()
            .is_some_and(|token| token.is(b"<"))
        {
            PlPrefix::Anonymous
        } else {
            PlPrefix::None
        };
    }
    let Some(next) = tokens.next().ok().flatten() else {
        return PlPrefix::None;
    };
    if first.is(b"begin") {
        return if next.is(b";") || next.is(b"work") {
            PlPrefix::None
        } else {
            PlPrefix::Anonymous
        };
    }
    if first.is(b"dd") && next.is(b"begin") || first.is(b"update") && next.is(b"of") {
        return PlPrefix::Anonymous;
    }
    if first.is(b"create") {
        let mut token = next;
        loop {
            if let Some(kind) = routine_kind(token) {
                return kind;
            }
            if token.is(b"definer") {
                if !tokens
                    .next()
                    .ok()
                    .flatten()
                    .is_some_and(|token| token.is(b"="))
                {
                    return PlPrefix::None;
                }
                if tokens.next().ok().flatten().is_none() {
                    return PlPrefix::None;
                }
                let Some(after) = tokens.next().ok().flatten() else {
                    return PlPrefix::None;
                };
                token = after;
                if token.is(b"(") {
                    if !tokens
                        .next()
                        .ok()
                        .flatten()
                        .is_some_and(|token| token.is(b")"))
                    {
                        return PlPrefix::None;
                    }
                    let Some(after) = tokens.next().ok().flatten() else {
                        return PlPrefix::None;
                    };
                    token = after;
                }
                if token.is(b"@") {
                    if tokens.next().ok().flatten().is_none() {
                        return PlPrefix::None;
                    }
                    let Some(after) = tokens.next().ok().flatten() else {
                        return PlPrefix::None;
                    };
                    token = after;
                }
                continue;
            }
            if ![
                b"or".as_slice(),
                b"replace",
                b"editionable",
                b"noneditionable",
                b"force",
            ]
            .iter()
            .any(|word| token.is(word))
            {
                return PlPrefix::None;
            }
            let Some(after) = tokens.next().ok().flatten() else {
                return PlPrefix::None;
            };
            token = after;
        }
    }
    if first.is(b"drop") || first.is(b"alter") {
        return routine_kind(next).unwrap_or(PlPrefix::None);
    }
    PlPrefix::None
}

fn explain_body(tokens: &mut Tokens<'_>) -> Option<usize> {
    let mut token = tokens.next().ok()??;
    if [
        b"basic".as_slice(),
        b"outline",
        b"extended",
        b"extended_noaddr",
        b"partitions",
        b"planregress",
    ]
    .iter()
    .any(|word| token.is(word))
    {
        token = tokens.next().ok()??;
    } else if token.is(b"format") {
        if !tokens.next().ok()??.is(b"=") {
            return None;
        }
        let format = tokens.next().ok()??;
        if !format.is(b"traditional") && !format.is(b"json") {
            return None;
        }
        token = tokens.next().ok()??;
    }
    if token.is(b"pretty") || token.is(b"pretty_color") {
        token = tokens.next().ok()??;
    }
    if token.is(b"into") {
        tokens.next().ok()??;
        token = tokens.next().ok()??;
    }
    if token.is(b"set") {
        if !tokens.next().ok()??.is(b"statement_id") || !tokens.next().ok()??.is(b"=") {
            return None;
        }
        tokens.next().ok()??;
        token = tokens.next().ok()??;
    }
    Some(token.start)
}

fn trace_range(sql: &[u8]) -> (usize, usize) {
    let start = sql
        .iter()
        .position(|byte| !space(*byte))
        .unwrap_or(sql.len());
    if sql.get(start..start + 2) != Some(b"/*") {
        return (0, 0);
    }
    let body_start = start + 2;
    let Some(end) = sql[body_start..].windows(2).position(|text| text == b"*/") else {
        return (0, 0);
    };
    let body = &sql[body_start..body_start + end];
    let Some(found) = body
        .windows(9)
        .position(|text| text.eq_ignore_ascii_case(b"trace_id="))
    else {
        return (0, 0);
    };
    let value_start = body_start + found + 9;
    let value_end = sql[value_start..body_start + end]
        .iter()
        .position(|byte| space(*byte) || matches!(byte, b',' | b'*'))
        .map_or(body_start + end, |length| value_start + length);
    (value_start, value_end)
}

pub fn explain_body_offset(input: &[u8], mode: SqlMode) -> usize {
    let mut tokens = Tokens::new(input, mode);
    match tokens.next() {
        Ok(Some(first)) if first.is(b"explain") => {
            let offset = explain_body(&mut tokens).unwrap_or(input.len());
            if input[..tokens.pos].windows(3).any(|text| text == b"/*!") {
                classify_statement(input, mode).explain_offset
            } else {
                offset
            }
        }
        Ok(Some(first)) if first.text.starts_with(b"/*!") => {
            classify_statement(input, mode).explain_offset
        }
        _ => input.len(),
    }
}

pub fn classify_statement(input: &[u8], mode: SqlMode) -> StatementInfo {
    let (trace_start, trace_end) = trace_range(input);
    let mut result = StatementInfo {
        kind: StatementClass::Other,
        pl: PlPrefix::None,
        explain_offset: input.len(),
        supports_values_table: false,
        trace_start,
        trace_end,
    };
    let Ok(sql) = grammar::executable_sql(input, mode, true) else {
        return result;
    };
    let mut tokens = Tokens::new(&sql, mode);
    let Ok(Some(first)) = tokens.next() else {
        return result;
    };
    result.kind = if first.is(b"select") {
        StatementClass::Select
    } else if first.is(b"show") {
        StatementClass::Show
    } else if first.is(b"insert") {
        StatementClass::Insert
    } else if first.is(b"replace") {
        StatementClass::Replace
    } else if first.is(b"update") {
        StatementClass::Update
    } else if first.is(b"delete") {
        StatementClass::Delete
    } else if first.is(b"values") {
        StatementClass::Values
    } else if first.is(b"explain") {
        StatementClass::Explain
    } else {
        StatementClass::Other
    };
    result.supports_values_table = matches!(
        result.kind,
        StatementClass::Select
            | StatementClass::Update
            | StatementClass::Delete
            | StatementClass::Values
    );
    if result.kind == StatementClass::Explain {
        result.explain_offset = explain_body(&mut tokens).unwrap_or(input.len());
    } else {
        if result.kind == StatementClass::Insert {
            result.supports_values_table = tokens
                .next()
                .ok()
                .flatten()
                .is_some_and(|token| token.is(b"into"));
        } else {
            result.pl = classify_pl(first, &mut tokens);
        }
    }
    result
}

pub fn is_single_statement(input: &[u8]) -> bool {
    let mut end = input.len();
    while end > 0 && space(input[end - 1]) {
        end -= 1;
    }
    let sql = &input[..end];
    let mut semicolons = sql.iter().enumerate().filter(|(_, byte)| **byte == b';');
    match (semicolons.next(), semicolons.next()) {
        (None, _) => true,
        (Some((offset, _)), None) => offset + 1 == sql.len(),
        _ => false,
    }
}

pub fn split_sql(input: &[u8], mode: SqlMode, first_only: bool, bounds: StackBounds) -> StatementSplits {
    let budget = Budget::new(bounds);
    let stack_failure = || StatementSplits {
        ends: Vec::new(), failed: true, error_kind: ParseErrorKind::StackLimit,
    };
    if budget.checkpoint(0).is_err() { return stack_failure(); }
    let mut result = StatementSplits {
        ends: Vec::new(),
        failed: false,
        error_kind: ParseErrorKind::Syntax,
    };
    if is_single_statement(input) {
        result.ends.push(input.len());
        return result;
    }
    let mut end = normalized_sql_length(input);
    let mut last_semicolon = None;
    while end > 0 && (space(input[end - 1]) || input[end - 1] == b';') {
        end -= 1;
        if input[end] == b';' {
            last_semicolon = Some(end);
        }
    }
    end = last_semicolon.map_or(end, |semicolon| semicolon + 1);
    if end == 0 {
        result.ends.push(0);
        return result;
    }
    let input = &input[..end];
    let original = input;
    let Ok(sql) = grammar::executable_sql(input, mode, true) else {
        result.ends.push(input.len());
        result.failed = true;
        return result;
    };
    let mut offset = 0;
    while offset < sql.len() {
        let pl = classify_statement(&sql[offset..], mode).pl != PlPrefix::None;
        let diagnostics = crate::diagnostic::Diagnostics::default();
        let end = grammar::sql::statement_end(&sql[offset..], mode, pl, &budget, &diagnostics).or_else(|error| {
            if budget.failure().is_none() && matches!(sql, std::borrow::Cow::Owned(_)) {
                let ignored = grammar::executable_sql(&original[offset..], mode, false)?;
                let pl = classify_statement(&ignored, mode).pl != PlPrefix::None;
                grammar::sql::statement_end(&ignored, mode, pl, &budget, &diagnostics)
            } else {
                Err(error)
            }
        });
        if budget.failure().is_some() { return stack_failure(); }
        match end {
            Ok(length) if length > 0 => {
                offset += length;
                result.ends.push(offset);
            }
            _ => {
                result.ends.push(sql.len());
                result.failed = true;
                break;
            }
        }
        if first_only {
            break;
        }
    }
    result
}

pub fn fast_parameterization_allowed(sql: &[u8], mode: SqlMode) -> bool {
    let info = classify_statement(sql, mode);
    info.kind != StatementClass::Show && matches!(info.pl, PlPrefix::None | PlPrefix::Call)
}
