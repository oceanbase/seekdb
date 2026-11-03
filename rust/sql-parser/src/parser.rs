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

use crate::{grammar, node::{Kind, Node}, prefix::{normalized_sql_length, classify_statement}, ParseMode, SqlMode};
use crate::bridge::ffi::ParseErrorKind;
use crate::{budget::Budget, ParseFailure, Span, StackBounds};
use crate::diagnostic::Diagnostics;

pub struct Parsed {
    root: Node,
    expanded_sql: Vec<u8>,
    source_prefix: usize,
    parameters: Parameters,
    pl: bool,
}

struct Parameters {
    count: usize,
    ordinal: bool,
    named: bool,
}

impl Parsed {
    pub fn root(&self) -> &Node { &self.root }
    pub fn expanded_sql(&self) -> &[u8] { &self.expanded_sql }
    pub fn original_span(&self, node: &Node) -> crate::bridge::ffi::Span {
        crate::bridge::ffi::Span {
            start: node.start.saturating_sub(self.source_prefix),
            end: node.end.saturating_sub(self.source_prefix),
        }
    }
    pub fn parameters(&self) -> usize { self.parameters.count }
    pub fn has_ordinal_parameters(&self) -> bool { self.parameters.ordinal }
    pub fn named_parameters(&self) -> bool { self.parameters.named }
    pub fn is_pl(&self) -> bool { self.pl }
}

fn stack_failure(offset: usize) -> ParseFailure {
    ParseFailure::at(ParseErrorKind::StackLimit, "parser stack limit exceeded", Span { start:offset,end:offset })
}

pub fn parse(sql: &[u8], mode: SqlMode, parse_mode: ParseMode, bounds: StackBounds) -> Result<Parsed, ParseFailure> {
    let budget = &Budget::new(bounds);
    budget.checkpoint(0).map_err(|_| stack_failure(0))?;
    let sql = &sql[..normalized_sql_length(sql)];
    let source = if parse_mode == ParseMode::RoutineDefinition {
        let mut source = Vec::with_capacity(b"CREATE\n".len() + sql.len());
        source.extend_from_slice(b"CREATE\n");
        source.extend_from_slice(sql);
        std::borrow::Cow::Owned(source)
    } else {
        std::borrow::Cow::Borrowed(sql)
    };
    let source_prefix = source.len() - sql.len();
    parse_source(source, source_prefix, mode, parse_mode, budget)
        .map_err(|error| error.remove_source_prefix(source_prefix))
}

fn parse_source(source: std::borrow::Cow<'_, [u8]>, source_prefix: usize,
                mode: SqlMode, parse_mode: ParseMode, budget: &Budget) -> Result<Parsed, ParseFailure> {
    let sql = source.as_ref();
    let diagnostics = &Diagnostics::default();
    let entry = |text: &[u8]| {
        diagnostics.clear();
        let pl = matches!(parse_mode, ParseMode::Routine | ParseMode::Trigger | ParseMode::RoutineDefinition
            | ParseMode::PlSql | ParseMode::PlExpression)
            || (parse_mode == ParseMode::Sql && classify_statement(text, mode).pl != crate::bridge::ffi::PlPrefix::None);
        match parse_mode {
        ParseMode::Routine => grammar::sql::routine_body(text, mode, pl, budget, diagnostics),
        ParseMode::Trigger => grammar::sql::trigger(text, mode, pl, budget, diagnostics),
        ParseMode::Statistics => grammar::sql::statistics_options(text, mode, pl, budget, diagnostics),
        ParseMode::Expression | ParseMode::PlExpression => grammar::sql::expression(text, mode, pl, budget, diagnostics),
        ParseMode::Sql | ParseMode::RoutineDefinition | ParseMode::PlSql => grammar::sql::query(text, mode, pl, budget, diagnostics),
        _ => unreachable!("unknown parser entry point"),
        }.map(|root| (root, pl))
    };
    let mut expanded_sql = Vec::new();
    let parsed = grammar::executable_sql(sql, mode, true).and_then(|mut text| {
        let mut parsed = entry(&text);
        if budget.failure().is_none() && parsed.is_err() && matches!(text, std::borrow::Cow::Owned(_)) {
            text = grammar::executable_sql(sql, mode, false)?;
            parsed = entry(&text);
        }
        if let std::borrow::Cow::Owned(text) = text {
            expanded_sql = text;
        }
        parsed
    });
    if let Some(offset) = budget.failure() { return Err(stack_failure(offset)); }
    let parsed = parsed.map_err(|error| ParseFailure::syntax(
        if expanded_sql.is_empty() { sql } else { &expanded_sql }, mode, error, diagnostics,
    )).and_then(|(query, pl)| {
        if parse_mode == ParseMode::RoutineDefinition
            && !matches!(query.kind, Kind::CreateProcedure | Kind::CreateFunction) {
            Err(ParseFailure::at(ParseErrorKind::Syntax, "expected procedure or function definition",
                Span { start:query.start,end:query.end }))
        } else {
            Ok((query, pl))
        }
    });
    if expanded_sql.is_empty() {
        if let std::borrow::Cow::Owned(source) = source {
            expanded_sql = source;
        }
    }
    let (mut root, pl) = parsed.map_err(|mut error| {
        error.expanded_sql = std::mem::take(&mut expanded_sql);
        error
    })?;
    let parameters = number_parameters(&mut root);
    Ok(Parsed { root, expanded_sql, source_prefix, parameters, pl })
}

fn number_parameters(node: &mut Node) -> Parameters {
    let mut bindings = Parameters { count: 0, ordinal: false, named: false };
    let mut names: Vec<(String, usize)> = Vec::new();
    let mut parameters = Vec::new();
    let mut pending = vec![node];
    while let Some(node) = pending.pop() {
        if node.kind == Kind::Parameter {
            parameters.push(node);
        } else {
            pending.extend(node.children.iter_mut().rev());
        }
    }
    parameters.sort_by_key(|parameter| parameter.start);
    for node in parameters {
        if node.text.is_empty() {
            bindings.ordinal = true;
            node.value = bindings.count as u64;
            bindings.count += 1;
        } else {
            if !node.text.as_bytes()[1].is_ascii_digit() {
                bindings.named = true;
            }
            let index = names
                .iter()
                .find(|(name, _)| name.eq_ignore_ascii_case(node.text.as_str()))
                .map(|(_, index)| *index);
            node.value = index.unwrap_or_else(|| {
                let index = bindings.count;
                names.push((node.text.as_str().to_owned(), index));
                bindings.count += 1;
                index
            }) as u64;
        }
    }
    bindings
}

