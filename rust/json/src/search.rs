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

use crate::path::append_member;
use crate::{binary, BinaryError, BinaryErrorKind, BinaryResult, BinaryValue, JsonType, Path, Query, QueryOptions};
use std::collections::HashSet;
use std::hash::{Hash, Hasher};

#[derive(Clone, Copy, Debug)]
pub struct SearchOptions<'a> {
    pub pattern: &'a str,
    pub escape: char,
    pub first_only: bool,
}

impl Query {
    pub fn search(
        &self,
        paths: &[Path],
        options: SearchOptions<'_>,
    ) -> BinaryResult<Option<Vec<u8>>> {
        let pattern = Pattern::new(options.pattern, options.escape);
        let mut hits = HashSet::new();
        let root = self.document.root()?;
        if paths.is_empty() {
            let mut location = String::from("$");
            find_matches(root, &mut location, &pattern, options.first_only, &mut hits)?;
        } else {
            for path in paths {
                if path.can_match_many() {
                    for (value, mut location) in
                        path.query_located(root, QueryOptions::default())?
                    {
                        find_matches(
                            value,
                            &mut location,
                            &pattern,
                            options.first_only,
                            &mut hits,
                        )?;
                        if options.first_only && !hits.is_empty() {
                            break;
                        }
                    }
                } else {
                    let matches = path.query(root, QueryOptions::default())?;
                    let mut location = path.to_mysql_string();
                    for value in matches {
                        find_matches(
                            value,
                            &mut location,
                            &pattern,
                            options.first_only,
                            &mut hits,
                        )?;
                    }
                }
                if options.first_only && !hits.is_empty() {
                    break;
                }
            }
        }
        if hits.is_empty() {
            return Ok(None);
        }
        let mut ordered: Vec<_> = hits.into_iter().collect();
        ordered.sort_unstable_by_key(|hit| hit.order);
        let paths: Vec<_> = ordered.into_iter().map(|hit| hit.path).collect();
        Ok(Some(binary::serialize_strings(&paths)))
    }
}

struct Hit {
    path: String,
    order: usize,
}

impl PartialEq for Hit {
    fn eq(&self, other: &Self) -> bool {
        self.path == other.path
    }
}

impl Eq for Hit {}

impl Hash for Hit {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.path.hash(state);
    }
}

fn find_matches(
    value: BinaryValue<'_>,
    location: &mut String,
    pattern: &Pattern<'_>,
    first_only: bool,
    hits: &mut HashSet<Hit>,
) -> BinaryResult<()> {
    struct Frame<'a> {
        value: BinaryValue<'a>,
        next: u64,
        count: u64,
        object: bool,
        location_length: usize,
    }
    let original_length = location.len();
    let mut frames = Vec::<Frame<'_>>::new();
    let mut active = HashSet::new();
    let mut next = Some(value);
    let result = (|| {
        loop {
            if first_only && !hits.is_empty() { return Ok(()); }
            if let Some(value) = next.take() {
                match value.json_type()? {
                    JsonType::String => {
                        if pattern.matches(value.string_bytes()?) {
                            let order = hits.len();
                            hits.insert(Hit { path: location.clone(), order });
                        }
                    }
                    JsonType::Object | JsonType::Array => {
                        if !active.insert(value.storage_offset()) {
                            return Err(BinaryError::new(BinaryErrorKind::InvalidDepth, value.storage_offset()));
                        }
                        frames.push(Frame { value, next: 0, count: value.len()?,
                            object: value.json_type()? == JsonType::Object,
                            location_length: location.len() });
                    }
                    _ => {}
                }
            }
            if first_only && !hits.is_empty() { return Ok(()); }
            let Some(frame) = frames.last_mut() else { return Ok(()); };
            location.truncate(frame.location_length);
            if frame.next == frame.count {
                active.remove(&frame.value.storage_offset());
                frames.pop();
                continue;
            }
            let index = frame.next;
            frame.next += 1;
            if frame.object {
                let key = String::from_utf8_lossy(frame.value.key(index)?);
                append_member(location, &key, false);
            } else {
                location.push('[');
                location.push_str(&index.to_string());
                location.push(']');
            }
            next = Some(frame.value.value(index)?);
        }
    })();
    location.truncate(original_length);
    result
}

#[derive(Clone, Copy)]
enum Token<'a> {
    Literal(&'a [u8]),
    One,
    Many,
}

struct Pattern<'a> {
    tokens: Vec<Token<'a>>,
}

impl<'a> Pattern<'a> {
    fn new(pattern: &'a str, escape: char) -> Self {
        let mut tokens = Vec::new();
        let mut position = 0;
        while position < pattern.len() {
            let character = pattern[position..]
                .chars()
                .next()
                .expect("position is on a character boundary");
            let start = position;
            position += character.len_utf8();
            if character == escape && position < pattern.len() {
                let literal_start = position;
                let escaped = pattern[position..]
                    .chars()
                    .next()
                    .expect("position is on a character boundary");
                position += escaped.len_utf8();
                tokens.push(Token::Literal(&pattern.as_bytes()[literal_start..position]));
            } else {
                tokens.push(match character {
                    '_' if character != escape => Token::One,
                    '%' if character != escape => Token::Many,
                    _ => Token::Literal(&pattern.as_bytes()[start..position]),
                });
            }
        }
        Self { tokens }
    }

    fn matches(&self, text: &[u8]) -> bool {
        let mut pattern_position = 0;
        let mut text_position = 0;
        let mut backtrack = None;
        while text_position < text.len() {
            match self.tokens.get(pattern_position).copied() {
                Some(Token::Literal(literal)) if text[text_position..].starts_with(literal) => {
                    pattern_position += 1;
                    text_position += literal.len();
                }
                Some(Token::One) => {
                    pattern_position += 1;
                    text_position += next_character_length(text, text_position);
                }
                Some(Token::Many) => {
                    pattern_position += 1;
                    backtrack = Some((pattern_position, text_position));
                }
                _ => {
                    let Some((resume_pattern, resume_text)) = backtrack else {
                        return false;
                    };
                    let next_text = resume_text + next_character_length(text, resume_text);
                    backtrack = Some((resume_pattern, next_text));
                    pattern_position = resume_pattern;
                    text_position = next_text;
                }
            }
        }
        while matches!(self.tokens.get(pattern_position), Some(Token::Many)) {
            pattern_position += 1;
        }
        pattern_position == self.tokens.len()
    }
}

fn next_character_length(input: &[u8], position: usize) -> usize {
    let first = input[position];
    let expected = match first {
        0x00..=0x7f => 1,
        0xc2..=0xdf => 2,
        0xe0..=0xef => 3,
        0xf0..=0xf4 => 4,
        _ => return 1,
    };
    if position + expected <= input.len()
        && input[position + 1..position + expected]
            .iter()
            .all(|byte| byte & 0xc0 == 0x80)
    {
        expected
    } else {
        1
    }
}
