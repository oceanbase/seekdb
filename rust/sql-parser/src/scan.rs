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

use crate::bridge::ffi::{Literal, LiteralKind as Kind, ScanOptions, ScanResult, ValuesPosition};
use crate::lex::{decode_string_into, identifier, quoted_end, space};

pub fn begin_scan(input: &[u8], options: ScanOptions) -> Box<Scanner<'_>> {
    let result = ScanResult {
        sql: Vec::with_capacity(input.len()),
        literals: Vec::new(),
        literal_text: Vec::new(),
        parameter_names: Vec::new(),
        ordinal_parameters: 0,
        values: Vec::new(),
        insert_values_offset: 0,
        error: String::new(),
        error_offset: 0,
    };
    let sql = &input[..crate::prefix::normalized_sql_length(input)];
    Box::new(Scanner {
        sql,
        pos: 0,
        options,
        result,
        inserts: 0,
        brace_depth: 0,
    })
}

pub fn finish_scan(scanner: Box<Scanner<'_>>) -> ScanResult {
    scanner.result
}

pub struct Scanner<'a> {
    sql: &'a [u8],
    pos: usize,
    options: ScanOptions,
    result: ScanResult,
    inserts: usize,
    brace_depth: usize,
}

impl Scanner<'_> {
    /// Scan up to 512 tokens, then yield so the caller can check cancellation.
    pub fn advance(&mut self) -> bool {
        match self.run() {
            Ok(finished) => finished,
            Err(offset) => {
                self.result.error = "invalid SQL token".into();
                self.result.error_offset = offset;
                true
            }
        }
    }

    fn byte(&self, pos: usize) -> u8 {
        self.sql.get(pos).copied().unwrap_or(0)
    }

    fn skip_space(&self, mut pos: usize) -> usize {
        while space(self.byte(pos)) {
            pos += 1;
        }
        pos
    }

    fn word_end(&self, mut pos: usize) -> usize {
        while identifier(self.byte(pos)) {
            pos += 1;
        }
        pos
    }

    fn qualified_end(&self, mut pos: usize) -> usize {
        while identifier(self.byte(pos)) {
            pos = self.word_end(pos);
            if self.byte(pos) == b'.' {
                pos += 1;
            }
        }
        pos
    }

    fn qualified_tail(&self, mut pos: usize) -> usize {
        while self.byte(pos) == b'.' {
            let next = pos + 1;
            if self.byte(next) == b'`' {
                match quoted_end(self.sql, next, false) {
                    Some(end) => pos = end,
                    None => break,
                }
            } else if identifier(self.byte(next)) {
                pos = self.qualified_end(next);
            } else {
                break;
            }
        }
        pos
    }

    fn normal(&mut self, start: usize, preserve_case: bool) {
        let bytes = &self.sql[start..self.pos];
        if self.options.format && !preserve_case {
            if let Ok(text) = std::str::from_utf8(bytes) {
                for ch in text.chars() {
                    let mut uppercase = ch.to_uppercase();
                    let first = uppercase.next().unwrap();
                    let formatted = if uppercase.next().is_none() { first } else { ch };
                    let mut encoded = [0; 4];
                    self.result.sql.extend_from_slice(formatted.encode_utf8(&mut encoded).as_bytes());
                }
            } else {
                self.result.sql.extend_from_slice(bytes);
            }
        } else {
            self.result.sql.extend_from_slice(bytes);
        }
        if self.options.format {
            self.result.sql.push(b' ');
        }
    }

    fn trim_output_space(&mut self) {
        while self.result.sql.last().is_some_and(|byte| space(*byte)) {
            self.result.sql.pop();
        }
    }

    fn comment_separator(&mut self) {
        if self.pos < self.sql.len()
            && !space(self.byte(self.pos))
            && self.result.sql.last().is_some_and(|byte| !space(*byte))
        {
            self.result.sql.push(b' ');
        }
    }

    fn literal(
        &mut self,
        start: usize,
        kind: Kind,
        value: u64,
        content_start: usize,
        first: Option<usize>,
    ) {
        if !self.options.parameterize && !self.options.format {
            self.normal(start, false);
            return;
        }
        self.result.literals.push(Literal {
            kind,
            start,
            end: self.pos,
            sql_offset: self.result.sql.len(),
            value,
            content_start,
            content_end: self.result.literal_text.len(),
            concatenated: first.is_some(),
            first_part_length: first.unwrap_or(0),
        });
        self.result.sql.push(b'?');
        if self.options.format {
            self.result.sql.push(b' ');
        }
    }

    fn comment_end(&self, start: usize) -> Result<usize, usize> {
        let nested = self.byte(start + 2) == b'!';
        let mut depth = 1;
        let mut pos = start + 2;
        while pos + 1 < self.sql.len() {
            match (self.byte(pos), self.byte(pos + 1)) {
                (b'/', b'*') if nested => {
                    depth += 1;
                    pos += 2;
                }
                (b'*', b'/') => {
                    depth -= 1;
                    pos += 2;
                    if depth == 0 {
                        return Ok(pos);
                    }
                }
                _ => pos += 1,
            }
        }
        Err(start)
    }

    fn whitespace_end(&self, mut pos: usize) -> Option<usize> {
        if space(self.byte(pos)) {
            return Some(self.skip_space(pos));
        }
        if self.byte(pos) == b'#' {
            pos += 1;
        } else if self.byte(pos) == b'-' && self.byte(pos + 1) == b'-' && space(self.byte(pos + 2))
        {
            pos = self.skip_space(pos + 2);
        } else {
            return None;
        }
        while pos < self.sql.len() && !matches!(self.byte(pos), b'\n' | b'\r') {
            pos += 1;
        }
        Some(pos)
    }

    fn string(&mut self, start: usize, kind: Kind) -> Result<(), usize> {
        let quote = self.byte(self.pos);
        if quote == b'"' && self.options.mode.ansi_quotes {
            self.pos = quoted_end(self.sql, self.pos, false).ok_or(start)?;
            self.normal(start, false);
            return Ok(());
        }
        let content_start = self.result.literal_text.len();
        let mut first = None;
        loop {
            let end = quoted_end(
                self.sql,
                self.pos,
                !self.options.mode.no_backslash_escapes,
            )
            .ok_or(start)?;
            decode_string_into(
                &self.sql[self.pos..end],
                self.options.mode.no_backslash_escapes,
                &mut self.result.literal_text,
            );
            self.pos = end;
            if let Some(next) = self.whitespace_end(end) {
                if self.byte(next) == quote {
                    first.get_or_insert(self.result.literal_text.len() - content_start);
                    self.pos = next;
                    continue;
                }
            }
            break;
        }
        self.literal(start, kind, i64::MAX as u64, content_start, first);
        Ok(())
    }

    fn binary(&mut self, start: usize, quoted: bool, hex: bool) -> Result<bool, usize> {
        let begin = start + 2;
        let mut end = begin;
        while if hex {
            self.byte(end).is_ascii_hexdigit()
        } else {
            matches!(self.byte(end), b'0' | b'1')
        } {
            end += 1;
        }
        if quoted && self.byte(end) != b'\'' {
            if end == self.sql.len() {
                return Err(start);
            }
            self.pos = start + 1;
            self.normal(start, false);
            return Ok(true);
        }
        if !quoted && (end == begin || hex && identifier(self.byte(end))) {
            return Ok(false);
        }
        if quoted && hex && !(end - begin).is_multiple_of(2)
            || !quoted && matches!(self.byte(start + 1), b'X' | b'B')
        {
            return Err(start);
        }
        let content_start = self.result.literal_text.len();
        crate::lex::decode_binary_into(
            &self.sql[begin..end],
            if hex { crate::bridge::ffi::BinaryNotation::Hex }
            else { crate::bridge::ffi::BinaryNotation::Bit },
            &mut self.result.literal_text,
        );
        self.pos = end + usize::from(quoted);
        self.literal(start, Kind::Binary, i64::MAX as u64, content_start, None);
        Ok(true)
    }

    fn integer(&self, start: usize, end: usize) -> Option<u64> {
        self.sql[start..end].iter().try_fold(0u64, |value, byte| {
            value.checked_mul(10)?.checked_add(u64::from(byte - b'0'))
        })
    }

    fn number(&mut self, start: usize) {
        let digits = self.pos;
        while self.byte(self.pos).is_ascii_digit() {
            self.pos += 1;
        }
        let mut kind = Kind::Integer;
        let dot = self.byte(self.pos) == b'.';
        if dot {
            kind = Kind::Decimal;
            self.pos += 1;
            while self.byte(self.pos).is_ascii_digit() {
                self.pos += 1;
            }
        }
        let mut exponent_sign = false;
        if matches!(self.byte(self.pos), b'e' | b'E') {
            let exponent = self.pos;
            self.pos += 1;
            exponent_sign = matches!(self.byte(self.pos), b'+' | b'-');
            if exponent_sign {
                self.pos += 1;
            }
            let exponent_digits = self.pos;
            while self.byte(self.pos).is_ascii_digit() {
                self.pos += 1;
            }
            if self.pos > exponent_digits {
                kind = Kind::Float;
            } else if dot {
                self.pos = exponent;
            } else {
                self.pos = self.qualified_end(exponent + 1);
                self.normal(start, false);
                return;
            }
        }
        if !dot && !exponent_sign && identifier(self.byte(self.pos)) {
            self.pos = self.qualified_end(self.pos);
            self.normal(start, false);
            return;
        }
        let mut value = i64::MAX as u64;
        if kind == Kind::Integer {
            if self.byte(start) == b'-' {
                match self.integer(digits, self.pos) {
                    Some(n) if n <= (1u64 << 63) => value = n.wrapping_neg(),
                    _ => {
                        kind = Kind::Decimal;
                        value = i64::MIN as u64;
                    }
                }
            } else {
                match self.integer(start, self.pos) {
                    Some(n) => {
                        value = n;
                        if n > i64::MAX as u64 {
                            kind = Kind::Unsigned;
                        }
                    }
                    None => {
                        kind = Kind::Decimal;
                        value = u64::MAX;
                    }
                }
            }
        }
        self.literal(start, kind, value, self.result.literal_text.len(), None);
    }

    fn placeholder(&mut self, start: usize) -> Result<(), usize> {
        let value = if self.byte(start) == b'?' {
            self.pos += 1;
            let index = self.result.ordinal_parameters;
            self.result.ordinal_parameters += 1;
            index as u64
        } else {
            self.pos += 1;
            if self.byte(self.pos).is_ascii_digit() {
                while self.byte(self.pos).is_ascii_digit() {
                    self.pos += 1;
                }
                self.integer(start + 1, self.pos).ok_or(start)?
            } else {
                self.pos = self.word_end(self.pos);
                let name = std::str::from_utf8(&self.sql[start..self.pos])
                    .map_err(|error| start + error.valid_up_to())?;
                let names = &mut self.result.parameter_names;
                if let Some(index) = names
                    .iter()
                    .position(|item| item.eq_ignore_ascii_case(name))
                {
                    index as u64
                } else {
                    names.push(name.into());
                    (names.len() - 1) as u64
                }
            }
        };
        self.literal(
            start,
            Kind::Placeholder,
            value,
            self.result.literal_text.len(),
            None,
        );
        Ok(())
    }

    fn hint(&mut self, start: usize) -> Result<bool, usize> {
        let mut pos = self.skip_space(self.pos);
        let comment_start = pos;
        while self.byte(pos) == b'/' && self.byte(pos + 1) == b'*' {
            let hint = self.byte(pos + 2) == b'+';
            let end = self.comment_end(pos)?;
            if hint {
                if self.options.filter_hints {
                    self.pos = comment_start;
                    self.normal(start, false);
                    self.pos = end;
                    self.comment_separator();
                    if self.options.format {
                        self.trim_output_space();
                        self.result.sql.push(b' ');
                    }
                } else {
                    self.pos = end;
                    self.normal(start, false);
                }
                return Ok(true);
            }
            pos = self.skip_space(end);
        }
        Ok(false)
    }

    fn word(&mut self, start: usize) -> Result<(), usize> {
        self.pos = self.word_end(self.pos);
        if self.byte(self.pos) == b'.' {
            self.pos = self.qualified_end(self.pos + 1);
            self.normal(start, false);
            return Ok(());
        }
        let word = &self.sql[start..self.pos];
        if word.eq_ignore_ascii_case(b"true") || word.eq_ignore_ascii_case(b"false") {
            self.literal(
                start,
                Kind::Boolean,
                u64::from(word.eq_ignore_ascii_case(b"true")),
                self.result.literal_text.len(),
                None,
            );
            return Ok(());
        }
        if word.eq_ignore_ascii_case(b"null") || word == b"\\N" {
            self.literal(
                start,
                Kind::Null,
                i64::MAX as u64,
                self.result.literal_text.len(),
                None,
            );
            return Ok(());
        }
        if word.eq_ignore_ascii_case(b"date")
            || word.eq_ignore_ascii_case(b"time")
            || word.eq_ignore_ascii_case(b"timestamp")
        {
            let kind = if word.eq_ignore_ascii_case(b"date") {
                Kind::Date
            } else if word.eq_ignore_ascii_case(b"time") {
                Kind::Time
            } else {
                Kind::Timestamp
            };
            let quote = self.whitespace_end(self.pos).unwrap_or(self.pos);
            if matches!(self.byte(quote), b'\'' | b'"') {
                let end = self.sql[quote + 1..]
                    .iter()
                    .position(|&byte| byte == self.byte(quote))
                    .map(|n| quote + 1 + n)
                    .ok_or(start)?;
                self.pos = end + 1;
                let content_start = self.result.literal_text.len();
                self.result
                    .literal_text
                    .extend_from_slice(&self.sql[quote + 1..end]);
                self.literal(start, kind, i64::MAX as u64, content_start, None);
                return Ok(());
            }
        }
        if word.eq_ignore_ascii_case(b"insert") || word.eq_ignore_ascii_case(b"replace") {
            self.inserts += 1;
            if self.inserts > 1 {
                self.result.insert_values_offset = 0;
            }
        }
        if word.eq_ignore_ascii_case(b"values") || word.eq_ignore_ascii_case(b"value") {
            if self.inserts == 1 && self.result.insert_values_offset == 0 {
                self.result.insert_values_offset = self.result.sql.len() + 1;
            }
            if word.eq_ignore_ascii_case(b"values") {
                self.result.values.push(ValuesPosition {
                    sql_offset: self.result.sql.len(),
                    literal_index: self.result.literals.len(),
                });
            }
        }
        if [b"select".as_slice(), b"insert", b"replace", b"update", b"delete", b"hint"]
            .iter()
            .any(|kw| word.eq_ignore_ascii_case(kw))
        {
            if self.hint(start)? {
                return Ok(());
            }
        } else if word.eq_ignore_ascii_case(b"load") {
            let data = self.skip_space(self.pos);
            if self
                .sql
                .get(data..data + 4)
                .is_some_and(|s| s.eq_ignore_ascii_case(b"data"))
            {
                self.pos = data + 4;
                if self.hint(start)? {
                    return Ok(());
                }
            }
        }
        self.normal(start, false);
        Ok(())
    }

    fn run(&mut self) -> Result<bool, usize> {
        for _ in 0..512 {
            if self.pos == self.sql.len() {
                return Ok(true);
            }
            let whitespace = self.pos;
            self.pos = self.skip_space(self.pos);
            if !self.options.format {
                self.result.sql.extend_from_slice(&self.sql[whitespace..self.pos]);
            }
            if self.pos == self.sql.len() {
                return Ok(true);
            }
            let start = self.pos;
            let byte = self.byte(start);
            match byte {
                b'\'' | b'"' => self.string(start, Kind::String)?,
                b'`' => {
                    self.pos = quoted_end(self.sql, start, false).ok_or(start)?;
                    self.pos = self.qualified_tail(self.pos);
                    self.normal(start, true);
                }
                b'n' | b'N' if self.byte(start + 1) == b'\'' => {
                    self.pos += 1;
                    self.string(start, Kind::NationalString)?;
                }
                b'x' | b'X' | b'b' | b'B' if self.byte(start + 1) == b'\'' => {
                    self.binary(start, true, byte.eq_ignore_ascii_case(&b'x'))?;
                }
                b'0' if matches!(self.byte(start + 1), b'x' | b'X' | b'b' | b'B') => {
                    if !self.binary(
                        start,
                        false,
                        self.byte(start + 1).eq_ignore_ascii_case(&b'x'),
                    )? {
                        self.pos = self.qualified_end(start);
                        self.normal(start, false);
                    }
                }
                b'0'..=b'9' => self.number(start),
                b'.' if self.byte(start + 1).is_ascii_digit() => self.number(start),
                b'#' => {
                    while self.pos < self.sql.len() && !matches!(self.byte(self.pos), b'\n' | b'\r')
                    {
                        self.pos += 1;
                    }
                    if !self.options.format && self.brace_depth > 0 {
                        self.normal(start, false);
                    }
                }
                b'-' if self.byte(start + 1) == b'-'
                    && matches!(self.byte(start + 2), b' ' | b'\t' | b'\n' | b'\r') =>
                {
                    self.pos += 2;
                    while self.pos < self.sql.len() && !matches!(self.byte(self.pos), b'\n' | b'\r')
                    {
                        self.pos += 1;
                    }
                    if !self.options.format && self.brace_depth > 0 {
                        self.normal(start, false);
                    }
                }
                b'/' if self.byte(start + 1) == b'*' => {
                    self.pos = self.comment_end(start)?;
                    if self.byte(start + 2) == b'!'
                        || !self.options.format && self.brace_depth > 0
                    {
                        self.normal(start, false);
                    } else {
                        self.comment_separator();
                    }
                }
                b'-' => {
                    self.pos = self.skip_space(start + 1);
                    if (self.byte(self.pos).is_ascii_digit()
                        && !matches!(self.byte(self.pos + 1), b'x' | b'X' | b'b' | b'B'))
                        || self.byte(self.pos) == b'.' && self.byte(self.pos + 1).is_ascii_digit()
                    {
                        self.number(start);
                    } else {
                        self.normal(start, false);
                    }
                }
                b'?' => self.placeholder(start)?,
                b':' if self.byte(start + 1).is_ascii_alphanumeric()
                    || self.byte(start + 1) >= 0x80 =>
                {
                    self.placeholder(start)?;
                }
                b'@' => {
                    self.pos += 1;
                    if self.byte(self.pos) == b'@' {
                        self.pos += 1;
                    }
                    if matches!(self.byte(self.pos), b'\'' | b'"' | b'`') {
                        self.pos = quoted_end(
                            self.sql,
                            self.pos,
                            !self.options.mode.no_backslash_escapes,
                        )
                        .ok_or(start)?;
                    } else {
                        while identifier(self.byte(self.pos)) || self.byte(self.pos) == b'.' {
                            self.pos += 1;
                        }
                    }
                    self.normal(start, true);
                }
                b';' => {
                    self.pos += 1;
                    if self.options.batch_statement {
                        self.trim_output_space();
                    } else {
                        self.normal(start, false);
                    }
                    return Ok(true);
                }
                b'\\' => {
                    self.pos += 1;
                    self.word(start)?;
                }
                byte if identifier(byte) => self.word(start)?,
                b'{' | b'}' => {
                    self.brace_depth = if byte == b'{' {
                        self.brace_depth + 1
                    } else {
                        self.brace_depth.saturating_sub(1)
                    };
                    self.pos += 1;
                    self.normal(start, false);
                }
                b'!' | b'%' | b'&' | b'(' | b')' | b'*' | b'+' | b',' | b'.' | b'/' | b':'
                | b'<' | b'=' | b'>' | b'[' | b']' | b'^' | b'|' | b'~' => {
                    self.pos += 1;
                    self.normal(start, false);
                }
                _ => return Err(start),
            }
        }
        Ok(self.pos == self.sql.len())
    }
}
