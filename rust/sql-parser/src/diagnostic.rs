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

use crate::{budget::Budget, grammar, ParseDiagnostic, ParseErrorKind, Span, SqlMode};
use std::cell::Cell;

#[derive(Default)]
pub(crate) struct Diagnostics(Cell<Option<SemanticFailure>>);

#[derive(Clone, Copy)]
struct SemanticFailure {
    offset: usize,
    span: Span,
    message: &'static str,
}

impl Diagnostics {
    pub(crate) fn clear(&self) { self.0.take(); }

    pub(crate) fn record(&self, message: &'static str, span: Span, offset: usize) -> &'static str {
        let failure = self.0.get();
        if failure.as_ref().is_none_or(|previous| previous.offset < offset) {
            self.0.set(Some(SemanticFailure { offset, span, message }));
        }
        message
    }
}

#[derive(Debug)]
pub struct ParseFailure {
    pub kind: ParseErrorKind,
    pub message: String,
    pub diagnostic: ParseDiagnostic,
    pub expanded_sql: Vec<u8>,
}

impl ParseFailure {
    pub(crate) fn at(kind: ParseErrorKind, message: impl Into<String>, span: Span) -> Self {
        let message = message.into();
        Self {
            kind,
            message: message.clone(),
            diagnostic: ParseDiagnostic { span, expected: Vec::new(), detail: message },
            expanded_sql: Vec::new(),
        }
    }

    pub(crate) fn syntax(
        sql: &[u8],
        mode: SqlMode,
        error: peg::error::ParseError<usize>,
        diagnostics: &Diagnostics,
    ) -> Self {
        let (span, detail) = grammar::sql::diagnostic_token(
            sql, mode, false, &Budget::lexical_scan(), &Diagnostics::default(), error.location,
        ).expect("diagnostic tokenization accepts every byte");
        let mut diagnostic = ParseDiagnostic {
            span,
            expected: error.expected.tokens().map(str::to_owned).collect(),
            detail: detail.into(),
        };
        if let Some(failure) = diagnostics.0.get().filter(|failure|
            failure.offset == error.location
                && error.expected.tokens().any(|expected| expected == failure.message)) {
            diagnostic.span = failure.span;
            diagnostic.detail = failure.message.into();
        }
        Self {
            kind: ParseErrorKind::Syntax,
            message: error.to_string(),
            diagnostic,
            expanded_sql: Vec::new(),
        }
    }

    pub(crate) fn remove_source_prefix(mut self, prefix: usize) -> Self {
        self.diagnostic.span.start = self.diagnostic.span.start.saturating_sub(prefix);
        self.diagnostic.span.end = self.diagnostic.span.end.saturating_sub(prefix);
        if prefix != 0 && !self.expanded_sql.is_empty() {
            self.expanded_sql.drain(..prefix);
        }
        self
    }
}

impl std::fmt::Display for ParseFailure {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(&self.message)
    }
}

impl std::error::Error for ParseFailure {}
