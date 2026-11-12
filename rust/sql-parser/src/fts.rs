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

use crate::bridge::ffi::SearchOccurrence;

const MAX_DEPTH: usize = 128;

enum SearchValue {
    Term(String),
    List(Vec<SearchNode>),
}

pub struct SearchNode {
    value: SearchValue,
    occurrence: SearchOccurrence,
}

impl SearchNode {
    fn list(children: Vec<Self>) -> Self {
        Self { value: SearchValue::List(children), occurrence: SearchOccurrence::Optional }
    }

    fn term(text: &str) -> Self {
        Self { value: SearchValue::Term(text.into()), occurrence: SearchOccurrence::Optional }
    }

    fn group(children: Vec<Self>) -> Self {
        let inner = Self::list(children).with_occurrence(SearchOccurrence::Alternative);
        Self::list(vec![inner]).with_occurrence(SearchOccurrence::Alternative)
    }

    fn with_occurrence(mut self, occurrence: SearchOccurrence) -> Self {
        self.occurrence = occurrence;
        self
    }

    pub fn is_term(&self) -> bool {
        matches!(self.value, SearchValue::Term(_))
    }

    pub fn text(&self) -> &str {
        match &self.value { SearchValue::Term(text) => text, SearchValue::List(_) => "" }
    }

    pub fn children(&self) -> &[Self] {
        match &self.value { SearchValue::List(children) => children, SearchValue::Term(_) => &[] }
    }

    pub fn occurrence(&self) -> SearchOccurrence { self.occurrence }
}

pub struct SearchTree {
    root: SearchNode,
    error: String,
}

impl SearchTree {
    pub fn root(&self) -> &SearchNode { &self.root }
    pub fn error(&self) -> &str { &self.error }
}

peg::parser! {
    grammar search() for str {
        // Tabs inside a token remain part of it, matching the existing search tokenizer.
        rule separator() = ([' '|'\n'] / "\t"+ [' '|'\n']) [' '|'\t'|'\n']*
            / "\t"+ &(['"'|'*'|'('|')'|'+'|'-'|'<'|'>'|'~'|'@'] / ![_])
            / "\""
        rule _() = separator()*
        rule depth(level: usize) = {? if level < MAX_DEPTH { Ok(()) } else { Err("search expression depth below 128 levels") } }
        rule terms(level: usize) -> Vec<SearchNode> = values:(value:expression(level) _ { value })* {
            values.into_iter().flatten().collect()
        }
        rule expression(level: usize) -> Option<SearchNode> = depth(level)
            prefix:("+" { SearchOccurrence::Required } / "-" { SearchOccurrence::Excluded })? _
            value:("(" _ values:terms(level+1) ")" {
                if values.is_empty() { None }
                else { Some(SearchNode::group(values)) }
            } / text:$([^'"'|' '|'\n'|'*'|'('|')'|'+'|'-'|'<'|'>'|'~'|'@']+) {
                Some(SearchNode::term(text))
            }) {
                value.map(|value| match prefix {
                    Some(occurrence) => value.with_occurrence(occurrence),
                    None => value,
                })
            }
        pub rule query() -> Vec<SearchNode> = _ values:terms(0) { values }
    }
}

pub fn parse_search_query(input: &[u8]) -> Box<SearchTree> {
    let parsed = std::str::from_utf8(input)
        .map_err(|error| error.to_string())
        .and_then(|input| search::query(input).map_err(|error| error.to_string()));
    Box::new(match parsed {
        Ok(children) => SearchTree {
            root: SearchNode::list(children),
            error: String::new(),
        },
        Err(error) => SearchTree {
            root: SearchNode::list(vec![]),
            error,
        },
    })
}
