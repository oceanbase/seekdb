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

use super::*;
use std::cmp::Ordering;
use std::collections::HashSet;

pub(super) fn validate(schema: &Schema, value: BinaryValue<'_>) -> Result<Validation> {
    Evaluator::new(schema).validate(value)
}

fn pointer(parts: &[Vec<u8>]) -> String {
    parts
        .iter()
        .map(|part| String::from_utf8_lossy(part))
        .collect::<Vec<_>>()
        .join("/")
}

struct State<'a> {
    schema: &'a Schema,
    map: Id,
    answers: Vec<Option<bool>>,
    recorded: bool,
    keyword: Option<String>,
    document: Vec<Vec<u8>>,
    pointer: Vec<Vec<u8>>,
    active_values: HashSet<usize>,
}

struct Walk<'a> {
    context: usize,
    value: BinaryValue<'a>,
    schemas: Vec<Id>,
}

struct Rules<'a> {
    context: usize,
    value: BinaryValue<'a>,
    checks: Vec<(&'static [u8], Id, &'static [u8])>,
    next: usize,
}

struct Record<'a> {
    walk: Walk<'a>,
    schema: usize,
    next: usize,
}

struct Children<'a> {
    walk: Walk<'a>,
    object: bool,
    next: u64,
    count: u64,
}

struct Post<'a> {
    walk: Walk<'a>,
    key: usize,
    schema: usize,
}

#[derive(Clone, Copy)]
enum Composition {
    Dependencies,
    All,
    Any,
    One,
    Not,
}
impl Composition {
    fn parse(key: &[u8]) -> Result<Self> {
        Ok(match key {
            b"dependentSchemas" => Self::Dependencies,
            b"allOf" => Self::All,
            b"anyOf" => Self::Any,
            b"oneOf" => Self::One,
            b"not" => Self::Not,
            _ => return Err(Error::InvalidMap),
        })
    }
}

struct Branches<'a> {
    context: usize,
    value: BinaryValue<'a>,
    kind: Composition,
    rule: Id,
    next: usize,
    found: bool,
}

struct Terms<'a> {
    context: usize,
    value: BinaryValue<'a>,
    rule: Id,
    next: usize,
}

struct References<'a> {
    context: usize,
    value: BinaryValue<'a>,
    rule: Id,
    next: usize,
}

#[derive(Clone, Copy, Eq, PartialEq)]
enum RuleScope { Merged, Composition }

enum Task<'a> {
    Map {
        map: Id,
        value: BinaryValue<'a>,
    },
    EndMap {
        context: usize,
        identity: (Id, usize),
    },
    Walk(Walk<'a>),
    EndWalk {
        context: usize,
        offset: usize,
    },
    Rules(Rules<'a>),
    AfterRule {
        rules: Rules<'a>,
        failure: &'static [u8],
    },
    AfterRules(Walk<'a>),
    Record(Record<'a>),
    AfterRecordRule {
        record: Record<'a>,
        answer: usize,
    },
    AfterRecord(Walk<'a>),
    Children(Children<'a>),
    AfterChild(Children<'a>),
    AfterChildren(Walk<'a>),
    Post(Post<'a>),
    AfterPost {
        post: Post<'a>,
        key: &'static [u8],
    },
    CompositionScope,
    Rule {
        context: usize,
        value: BinaryValue<'a>,
        key: Vec<u8>,
        rule: Id,
        scope: RuleScope,
    },
    References(References<'a>),
    AfterReference(References<'a>),
    Branches(Branches<'a>),
    AfterBranch(Branches<'a>),
    Negate,
    Terms(Terms<'a>),
    AfterTerm(Terms<'a>),
}

struct Evaluator<'s, 'v> {
    schema: &'s Schema,
    contexts: Vec<State<'s>>,
    tasks: Vec<Task<'v>>,
    active_maps: HashSet<(Id, usize)>,
    answer: bool,
}

impl<'s, 'v> Evaluator<'s, 'v> {
    fn new(schema: &'s Schema) -> Self {
        Self {
            schema,
            contexts: Vec::new(),
            tasks: Vec::new(),
            active_maps: HashSet::new(),
            answer: true,
        }
    }

    fn validate(mut self, value: BinaryValue<'v>) -> Result<Validation> {
        self.tasks.push(Task::Map {
            map: self.schema.map,
            value,
        });
        while let Some(task) = self.tasks.pop() {
            if let Err(mut error) = self.step(task) {
                while let Some(task) = self.tasks.pop() {
                    if let Task::CompositionScope = task {
                        error = error.composition();
                    }
                }
                return Err(error);
            }
        }
        let state = self.contexts.pop().ok_or(Error::InvalidMap)?;
        Ok(Validation {
            failure: if self.answer {
                None
            } else {
                Some(Failure {
                    keyword: state.keyword.unwrap_or_default(),
                    document_location: pointer(&state.document),
                    schema_location: pointer(&state.pointer),
                })
            },
        })
    }

    fn step(&mut self, task: Task<'v>) -> Result<()> {
        match task {
            Task::Map { map, value } => {
                let entries = self.schema.arena.items(map)?;
                if entries.len() < 2 {
                    return Err(Error::InvalidMap);
                }
                let identity = (map, value.storage_offset());
                if !self.active_maps.insert(identity) {
                    return Err(Error::InvalidMap);
                }
                let context = self.contexts.len();
                self.contexts.push(State {
                    schema: self.schema,
                    map,
                    answers: vec![None; entries.len()],
                    recorded: false,
                    keyword: None,
                    document: vec![b"#".to_vec()],
                    pointer: vec![b"#".to_vec()],
                    active_values: HashSet::new(),
                });
                self.tasks.push(Task::EndMap { context, identity });
                self.tasks.push(Task::Walk(Walk {
                    context,
                    value,
                    schemas: vec![self.schema.arena.element(map, 0)?],
                }));
            }
            Task::EndMap { context, identity } => {
                self.active_maps.remove(&identity);
                if context != 0 {
                    self.contexts.pop();
                }
            }
            Task::Walk(walk) => {
                let kind = walk.value.json_type()?;
                if kind.is_container() {
                    if !self.contexts[walk.context]
                        .active_values
                        .insert(walk.value.storage_offset())
                    {
                        return Err(BinaryError::new(
                            crate::BinaryErrorKind::InvalidDepth,
                            walk.value.storage_offset(),
                        )
                        .into());
                    }
                    self.tasks.push(Task::EndWalk {
                        context: walk.context,
                        offset: walk.value.storage_offset(),
                    });
                }
                let arena = &self.schema.arena;
                let defs: Vec<_> = walk
                    .schemas
                    .iter()
                    .filter_map(|id| arena.get(*id, b"schema"))
                    .collect();
                let mut checks = Vec::new();
                for definition in &defs {
                    for key in [b"$ref".as_slice(), b"type", b"enum"] {
                        if let Some(rule) = arena.get(*definition, key) {
                            checks.push((key, rule, key));
                        }
                    }
                }
                let keys: &[&'static [u8]] = if is_number(kind) {
                    &[
                        b"maximum",
                        b"minimum",
                        b"exclusiveMaximum",
                        b"exclusiveMinimum",
                        b"multipleOf",
                    ]
                } else if string_type(kind) {
                    &[b"maxLength", b"minLength", b"pattern"]
                } else if kind == JsonType::Object {
                    &[
                        b"maxProperties",
                        b"minProperties",
                        b"required",
                        b"dependentRequired",
                        b"additionalProperties",
                    ]
                } else if kind == JsonType::Array {
                    &[b"maxItems", b"minItems", b"additionalItems", b"uniqueItems"]
                } else {
                    &[]
                };
                for definition in defs {
                    for key in keys {
                        if let Some(rule) = arena.get(definition, key) {
                            checks.push((*key, rule, report_keyword(key)));
                        }
                    }
                }
                let rules = Rules {
                    context: walk.context,
                    value: walk.value,
                    checks,
                    next: 0,
                };
                self.tasks.push(Task::AfterRules(walk));
                self.tasks.push(Task::Rules(rules));
            }
            Task::EndWalk { context, offset } => {
                self.contexts[context].active_values.remove(&offset);
            }
            Task::Rules(mut rules) => {
                if let Some((key, rule, failure)) = rules.checks.get(rules.next).copied() {
                    rules.next += 1;
                    let context = rules.context;
                    let value = rules.value;
                    self.tasks.push(Task::AfterRule { rules, failure });
                    self.tasks.push(Task::Rule {
                        context,
                        value,
                        key: key.to_vec(),
                        rule,
                        scope: RuleScope::Merged,
                    });
                } else {
                    self.answer = true;
                }
            }
            Task::AfterRule { rules, failure } => {
                if self.answer {
                    self.tasks.push(Task::Rules(rules));
                } else {
                    self.contexts[rules.context].fail(failure);
                }
            }
            Task::AfterRules(walk) => {
                if self.answer {
                    let record = Record {
                        walk: Walk {
                            context: walk.context,
                            value: walk.value,
                            schemas: walk.schemas.clone(),
                        },
                        schema: 0,
                        next: 0,
                    };
                    self.tasks.push(Task::AfterRecord(walk));
                    self.tasks.push(Task::Record(record));
                }
            }
            Task::Record(mut record) => {
                let arena = &self.schema.arena;
                let context = record.walk.context;
                while record.schema < record.walk.schemas.len() {
                    let schema = record.walk.schemas[record.schema];
                    if let Some(composition) = arena.get(schema, b"composition") {
                        while record.next < arena.len(composition) {
                            let reference = arena.element(composition, record.next)?;
                            record.next += 1;
                            let id = arena.integer(reference)? as usize;
                            if id < 2 || id >= self.contexts[context].answers.len() {
                                return Err(Error::InvalidIndex);
                            }
                            if self.contexts[context].answers[id] == Some(false) {
                                continue;
                            }
                            let map = self.contexts[context].map;
                            let entries = arena.items(map)?;
                            let mut definition = id;
                            while definition > 0
                                && arena
                                    .element(map, definition)
                                    .map_or(true, |id| !arena.is_object(id))
                            {
                                definition -= 1;
                            }
                            let fields = arena.fields(entries[definition])?;
                            if fields.len() != 1 {
                                return Err(Error::InvalidMap);
                            }
                            let (key, rule) = fields[0].clone();
                            let value = record.walk.value;
                            self.tasks
                                .push(Task::AfterRecordRule { record, answer: id });
                            self.tasks.push(Task::Rule {
                                context,
                                value,
                                key,
                                rule,
                                scope: RuleScope::Composition,
                            });
                            return Ok(());
                        }
                    }
                    record.schema += 1;
                    record.next = 0;
                }
                self.answer = true;
            }
            Task::AfterRecordRule { record, answer } => {
                let state = &mut self.contexts[record.walk.context];
                state.answers[answer] = Some(self.answer);
                state.recorded = true;
                self.tasks.push(Task::Record(record));
            }
            Task::AfterRecord(walk) => {
                let kind = walk.value.json_type()?;
                let keys: &[&[u8]] = if kind == JsonType::Object {
                    &[b"properties", b"patternProperties", b"additionalProperties"]
                } else if kind == JsonType::Array {
                    &[b"items", b"tupleItems", b"additionalItems"]
                } else {
                    &[]
                };
                let recursive = walk.schemas.iter().any(|id| {
                    keys.iter()
                        .any(|key| self.schema.arena.get(*id, key).is_some())
                });
                if recursive {
                    self.contexts[walk.context]
                        .pointer
                        .push(if kind == JsonType::Object {
                            b"properties".to_vec()
                        } else {
                            b"items".to_vec()
                        });
                    let children = Children {
                        object: kind == JsonType::Object,
                        next: 0,
                        count: walk.value.len()?,
                        walk: Walk {
                            context: walk.context,
                            value: walk.value,
                            schemas: walk.schemas.clone(),
                        },
                    };
                    self.tasks.push(Task::AfterChildren(walk));
                    self.tasks.push(Task::Children(children));
                } else {
                    self.tasks.push(Task::Post(Post {
                        walk,
                        key: 0,
                        schema: 0,
                    }));
                }
            }
            Task::Children(mut children) => {
                let context = children.walk.context;
                while children.next < children.count {
                    let index = children.next;
                    children.next += 1;
                    let key = if children.object {
                        children.walk.value.key(index)?.to_vec()
                    } else {
                        index.to_string().into_bytes()
                    };
                    let schemas = if children.object {
                        self.contexts[context].object_children(
                            children.walk.value,
                            &key,
                            &children.walk.schemas,
                        )?
                    } else {
                        self.contexts[context].array_children(
                            index,
                            &key,
                            &children.walk.schemas,
                        )?
                    };
                    if schemas.is_empty() {
                        continue;
                    }
                    let state = &mut self.contexts[context];
                    state.document.push(key.clone());
                    state.pointer.push(key);
                    let value = children.walk.value.value(index)?;
                    self.tasks.push(Task::AfterChild(children));
                    self.tasks.push(Task::Walk(Walk {
                        context,
                        value,
                        schemas,
                    }));
                    return Ok(());
                }
                if self.contexts[context].keyword.is_none() {
                    self.contexts[context].pointer.pop();
                }
                self.answer = true;
            }
            Task::AfterChild(children) => {
                if self.answer {
                    let state = &mut self.contexts[children.walk.context];
                    if state.keyword.is_none() {
                        state.document.pop();
                        state.pointer.pop();
                    }
                    self.tasks.push(Task::Children(children));
                }
            }
            Task::AfterChildren(walk) => {
                if self.answer {
                    self.tasks.push(Task::Post(Post {
                        walk,
                        key: 0,
                        schema: 0,
                    }));
                }
            }
            Task::Post(mut post) => {
                const KEYS: &[&[u8]] = &[b"dependentSchemas", b"allOf", b"anyOf", b"oneOf", b"not"];
                let context = post.walk.context;
                while post.key < KEYS.len() {
                    let key = KEYS[post.key];
                    if key != b"not" && !self.contexts[context].recorded {
                        post.key += 1;
                        post.schema = 0;
                        continue;
                    }
                    while post.schema < post.walk.schemas.len() {
                        let schema = post.walk.schemas[post.schema];
                        post.schema += 1;
                        if let Some(rule) = self.schema.arena.get(schema, key) {
                            if self.schema.arena.len(rule) == 0 {
                                if key == b"not" {
                                    self.answer = self.contexts[context].fail(key);
                                    return Ok(());
                                }
                                continue;
                            }
                            let value = post.walk.value;
                            self.tasks.push(Task::AfterPost { post, key });
                            if matches!(key, b"dependentSchemas" | b"oneOf") {
                                self.tasks.push(Task::CompositionScope);
                            }
                            self.tasks.push(Task::Branches(Branches {
                                context,
                                value,
                                kind: Composition::parse(key)?,
                                rule,
                                next: 0,
                                found: false,
                            }));
                            return Ok(());
                        }
                    }
                    post.key += 1;
                    post.schema = 0;
                }
                self.answer = true;
            }
            Task::AfterPost { post, key } => {
                if self.answer {
                    self.tasks.push(Task::Post(post));
                } else {
                    self.contexts[post.walk.context].fail(report_keyword(key));
                }
            }
            Task::CompositionScope => {}
            Task::Rule {
                context,
                value,
                key,
                rule,
                scope,
            } => {
                if key == b"$ref" {
                    self.tasks.push(Task::References(References {
                        context,
                        value,
                        rule,
                        next: 0,
                    }));
                } else {
                    self.answer = self.contexts[context].rule(value, &key, rule, scope)?;
                }
            }
            Task::References(mut references) => {
                let state = &self.contexts[references.context];
                let arena = state.arena();
                let array = arena.is_array(references.rule);
                let count = if array { arena.len(references.rule) } else { 1 };
                while references.next < count {
                    let index = references.next;
                    references.next += 1;
                    let rule = if array {
                        arena.element(references.rule, index)?
                    } else {
                        references.rule
                    };
                    if arena.kind(rule)? != JsonType::String {
                        continue;
                    }
                    let reference = arena.bytes(rule)?;
                    let target = if reference == b"#" {
                        (state.document.len() != 1).then_some(state.map)
                    } else {
                        let refs = arena.element(state.map, 1)?;
                        match arena.get(refs, reference) {
                            None => None,
                            Some(id) if arena.is_array(id) => Some(id),
                            Some(id) if arena.kind(id)? == JsonType::Int => {
                                (state.document.len() != 1).then_some(state.map)
                            }
                            _ => return Err(Error::InvalidMap),
                        }
                    };
                    if let Some(map) = target {
                        let value = references.value;
                        self.tasks.push(Task::AfterReference(references));
                        self.tasks.push(Task::Map { map, value });
                        return Ok(());
                    }
                }
                self.answer = true;
            }
            Task::AfterReference(references) => {
                if self.answer {
                    self.tasks.push(Task::References(references));
                }
            }
            Task::Terms(mut terms) => {
                let arena = &self.schema.arena;
                while terms.next < arena.len(terms.rule) {
                    let term = arena.element(terms.rule, terms.next)?;
                    terms.next += 1;
                    if arena.kind(term)? == JsonType::Int {
                        let index = arena.integer(term)? as usize;
                        if index < 2 || index >= self.contexts[terms.context].answers.len() {
                            return Err(Error::InvalidIndex);
                        }
                        if self.contexts[terms.context].answers[index] == Some(false) {
                            self.answer = false;
                            return Ok(());
                        }
                    } else if arena.is_object(term) {
                        let fields = arena.fields(term)?;
                        if fields.len() != 1 {
                            return Err(Error::InvalidMap);
                        }
                        let (key, rule) = &fields[0];
                        if arena.len(*rule) == 0 {
                            continue;
                        }
                        let frame = Branches {
                            context: terms.context,
                            value: terms.value,
                            kind: Composition::parse(key)?,
                            rule: *rule,
                            next: 0,
                            found: false,
                        };
                        self.tasks.push(Task::AfterTerm(terms));
                        self.tasks.push(Task::Branches(frame));
                        return Ok(());
                    }
                }
                self.answer = true;
            }
            Task::AfterTerm(terms) => {
                if self.answer {
                    self.tasks.push(Task::Terms(terms));
                }
            }
            Task::Branches(mut branches) => {
                if matches!(branches.kind, Composition::Not) {
                    self.tasks.push(Task::Negate);
                    self.tasks.push(Task::Terms(Terms {
                        context: branches.context,
                        value: branches.value,
                        rule: branches.rule,
                        next: 0,
                    }));
                    return Ok(());
                }
                let arena = &self.schema.arena;
                if matches!(branches.kind, Composition::Dependencies)
                    && branches.value.json_type()? != JsonType::Object
                {
                    return Err(Error::ObjectType);
                }
                while branches.next < arena.len(branches.rule) {
                    let index = branches.next;
                    branches.next += 1;
                    let terms = if matches!(branches.kind, Composition::Dependencies) {
                        let (key, terms) = &arena.fields(branches.rule)?[index];
                        if branches.value.member(key)?.is_none() {
                            continue;
                        }
                        *terms
                    } else {
                        arena.element(branches.rule, index)?
                    };
                    let frame = Terms {
                        context: branches.context,
                        value: branches.value,
                        rule: terms,
                        next: 0,
                    };
                    self.tasks.push(Task::AfterBranch(branches));
                    self.tasks.push(Task::Terms(frame));
                    return Ok(());
                }
                self.answer = match branches.kind {
                    Composition::Any => self.contexts[branches.context].fail(b"anyOf"),
                    Composition::One => branches.found,
                    _ => true,
                };
            }
            Task::AfterBranch(mut branches) => {
                match branches.kind {
                    Composition::Dependencies if !self.answer => {
                        self.contexts[branches.context].fail(b"dependencies");
                        return Ok(());
                    }
                    Composition::All if !self.answer => {
                        self.contexts[branches.context].fail(b"allOf");
                        return Ok(());
                    }
                    Composition::Any if self.answer => return Ok(()),
                    Composition::One if self.answer => {
                        if branches.found {
                            self.answer = self.contexts[branches.context].fail(b"oneOf");
                            return Ok(());
                        }
                        branches.found = true;
                    }
                    _ => {}
                }
                self.tasks.push(Task::Branches(branches));
            }
            Task::Negate => {
                self.answer = !self.answer;
            }
        }
        Ok(())
    }
}
impl State<'_> {
    fn arena(&self) -> &Arena {
        &self.schema.arena
    }
    fn fail(&mut self, key: &[u8]) -> bool {
        self.keyword = Some(String::from_utf8_lossy(key).into_owned());
        false
    }
    fn object_children(
        &self,
        value: BinaryValue<'_>,
        key: &[u8],
        schemas: &[Id],
    ) -> Result<Vec<Id>> {
        let mut out = Vec::new();
        for schema in schemas {
            let property = self
                .arena()
                .get(*schema, b"properties")
                .and_then(|properties| self.arena().get(properties, key));
            if let Some(child) = property {
                if !self.arena().is_object(child) {
                    return Err(Error::InvalidMap);
                }
                out.push(child)
            } else if let Some(patterns) = self.arena().get(*schema, b"patternProperties") {
                for (pattern, child) in self.arena().fields(patterns)? {
                    if self.pattern(pattern, key)? {
                        out.push(*child);
                    }
                }
            }
            if let Some(additional) = self.arena().get(*schema, b"additionalProperties") {
                let items = self.arena().items(additional)?;
                if items.len() % 2 != 0 {
                    return Err(Error::InvalidMap);
                }
                for index in (0..items.len()).step_by(2) {
                    let allowed = self.arena().element(additional, index)?;
                    if !self.allowed(value, allowed, Some(key))? {
                        out.push(self.arena().element(additional, index + 1)?);
                    }
                }
            }
        }
        Ok(out)
    }
    fn array_children(&self, index: u64, key: &[u8], schemas: &[Id]) -> Result<Vec<Id>> {
        let mut out = Vec::new();
        for schema in schemas {
            if let Some(items) = self.arena().get(*schema, b"items") {
                out.push(items)
            }
            if let Some(tuple) = self.arena().get(*schema, b"tupleItems") {
                if let Some(child) = self.arena().get(tuple, key) {
                    out.push(child)
                }
            }
            if let Some(additional) = self.arena().get(*schema, b"additionalItems") {
                for (start, child) in self.arena().fields(additional)? {
                    let start = std::str::from_utf8(start)
                        .ok()
                        .and_then(|v| v.parse::<i64>().ok())
                        .unwrap_or(0);
                    if start <= index as i64 {
                        out.push(*child)
                    }
                }
            }
        }
        Ok(out)
    }
    fn pattern(&self, pattern: &[u8], text: &[u8]) -> Result<bool> {
        match self.schema.patterns.get(pattern) {
            Some(pattern) => pattern.is_match(text),
            None => Ok(false),
        }
    }
    fn rule(
        &mut self,
        value: BinaryValue<'_>,
        key: &[u8],
        rule: Id,
        scope: RuleScope,
    ) -> Result<bool> {
        let kind = value.json_type()?;
        match key {
            b"type" => Ok((self.arena().integer(rule)? as u64) & type_flags(kind) != 0),
            b"enum" => {
                let options = self.arena().items(rule)?;
                if options.is_empty() {
                    return Ok(true);
                }
                for index in 0..options.len() {
                    let option = self.arena().element(rule, index)?;
                    if equal_values(value, self.arena().value(option)?, self.schema.scalar)? {
                        return Ok(true);
                    }
                }
                Ok(false)
            }
            b"maximum" | b"minimum" | b"exclusiveMaximum" | b"exclusiveMinimum"
                if is_number(kind) =>
            {
                let ordering = numeric_order(value, self.arena().value(rule)?)?;
                Ok(match key {
                    b"maximum" => ordering != Ordering::Greater,
                    b"minimum" => ordering != Ordering::Less,
                    b"exclusiveMaximum" => ordering == Ordering::Less,
                    _ => ordering == Ordering::Greater,
                })
            }
            b"multipleOf" if is_number(kind) => {
                if self.arena().is_array(rule) {
                    if scope == RuleScope::Merged {
                        if self.arena().len(rule) == 0 {
                            return Ok(true);
                        }
                        self.arena().element(rule, 0)?;
                        return Ok(false);
                    }
                    for index in 0..self.arena().len(rule) {
                        let rule = self.arena().element(rule, index)?;
                        if is_number(self.arena().kind(rule)?)
                            && !multiple(number(value)?, self.arena().number(rule)?)
                        {
                            return Ok(false);
                        }
                    }
                    Ok(true)
                } else {
                    Ok(multiple(number(value)?, self.arena().number(rule)?))
                }
            }
            b"maxLength" | b"minLength" if string_type(kind) => {
                let count = if kind == JsonType::String {
                    value.string_bytes()?.len() as i64
                } else {
                    1
                };
                let bound = self.arena().integer(rule)?;
                let (count, bound) = if scope == RuleScope::Composition {
                    (count as i32 as i64, bound as i32 as i64)
                } else {
                    (count, bound)
                };
                Ok(if key == b"maxLength" {
                    count <= bound
                } else {
                    count >= bound
                })
            }
            b"pattern" if string_type(kind) => {
                let bytes = if kind == JsonType::String {
                    value.string_bytes()?.to_vec()
                } else {
                    let scalar = binary::serialize_value(value)?;
                    scalar.get(1..2).unwrap_or_default().to_vec()
                };
                let count = if self.arena().is_array(rule) {
                    self.arena().len(rule)
                } else {
                    1
                };
                for index in 0..count {
                    let pattern = if self.arena().is_array(rule) {
                        self.arena().element(rule, index)?
                    } else {
                        rule
                    };
                    if !self.pattern(self.arena().bytes(pattern)?, &bytes)? {
                        return Ok(false);
                    }
                }
                Ok(true)
            }
            b"maxProperties" | b"minProperties" if kind == JsonType::Object => {
                if scope == RuleScope::Composition {
                    return Ok(true);
                }
                Ok(if key == b"maxProperties" {
                    value.len()? <= self.arena().integer(rule)? as u64
                } else {
                    value.len()? >= self.arena().integer(rule)? as u64
                })
            }
            b"required" if kind == JsonType::Object => self.required(value, rule),
            b"dependentRequired" if kind == JsonType::Object => {
                if self.arena().is_array(rule) {
                    if self.arena().len(rule) == 0 {
                        Ok(true)
                    } else {
                        self.arena().element(rule, 0)?;
                        Err(Error::InvalidMap)
                    }
                } else {
                    self.dependent_required(value, rule)
                }
            }
            b"additionalProperties" if kind == JsonType::Object => self.allowed(value, rule, None),
            b"maxItems" | b"minItems" | b"additionalItems" if kind == JsonType::Array => {
                if scope == RuleScope::Composition {
                    return Ok(true);
                }
                Ok(if key == b"minItems" {
                    value.len()? >= self.arena().integer(rule)? as u64
                } else {
                    value.len()? <= self.arena().integer(rule)? as u64
                })
            }
            b"uniqueItems" if kind == JsonType::Array => {
                for i in 0..value.len()? {
                    let a = value.value(i)?;
                    for j in 0..i {
                        let b = value.value(j)?;
                        if a.json_type()? == b.json_type()?
                            && equal_values(a, b, self.schema.scalar).unwrap_or_else(|error| {
                                matches!(error, Error::Platform(_) | Error::Binary(_))
                            })
                        {
                            return Ok(false);
                        }
                    }
                }
                Ok(true)
            }
            _ => Ok(true),
        }
    }
    fn required(&self, value: BinaryValue<'_>, rule: Id) -> Result<bool> {
        for index in 0..self.arena().len(rule) {
            let key = self.arena().element(rule, index)?;
            if self.arena().kind(key)? == JsonType::String
                && value.member(self.arena().bytes(key)?)?.is_none()
            {
                return Ok(false);
            }
        }
        Ok(true)
    }
    fn dependent_required(&self, value: BinaryValue<'_>, rule: Id) -> Result<bool> {
        for (key, requirements) in self.arena().fields(rule)? {
            if value.member(key)?.is_some()
                && self.arena().is_array(*requirements)
                && !self.required(value, *requirements)?
            {
                return Ok(false);
            }
        }
        Ok(true)
    }
    fn allowed(&self, value: BinaryValue<'_>, rule: Id, key: Option<&[u8]>) -> Result<bool> {
        let parts = self.arena().items(rule)?;
        if parts.len() % 2 != 0 {
            return Err(Error::InvalidMap);
        }
        let mut names = Vec::new();
        for index in (0..parts.len()).step_by(2) {
            let array = self.arena().element(rule, index)?;
            for index in 0..self.arena().len(array) {
                names.push(self.arena().bytes(self.arena().element(array, index)?)?);
            }
        }
        let is_allowed = |key: &[u8]| -> Result<bool> {
            if names.contains(&key) {
                return Ok(true);
            }
            for index in (1..parts.len()).step_by(2) {
                let array = self.arena().element(rule, index)?;
                for index in 0..self.arena().len(array) {
                    let pattern = self.arena().element(array, index)?;
                    if self.pattern(self.arena().bytes(pattern)?, key)? {
                        return Ok(true);
                    }
                }
            }
            Ok(false)
        };
        if let Some(key) = key.filter(|key| !key.is_empty()) {
            return is_allowed(key);
        }
        for index in 0..value.len()? {
            if !is_allowed(value.key(index)?)? {
                return Ok(false);
            }
        }
        Ok(true)
    }
}
fn string_type(kind: JsonType) -> bool {
    matches!(
        kind,
        JsonType::String
            | JsonType::Date
            | JsonType::Time
            | JsonType::Datetime
            | JsonType::Timestamp
            | JsonType::MySqlDate
            | JsonType::MySqlDatetime
            | JsonType::OBinary
            | JsonType::OOid
            | JsonType::ORawHex
            | JsonType::ORawId
            | JsonType::ODaySecond
            | JsonType::OYearMonth
    )
}
fn type_flags(kind: JsonType) -> u64 {
    match kind {
        JsonType::Null => 1,
        JsonType::Boolean => 2,
        JsonType::Object => 32,
        JsonType::Array => 64,
        JsonType::Int | JsonType::Uint | JsonType::OInt => 24,
        _ if is_number(kind) => 8,
        _ if string_type(kind) => 4,
        _ => 0,
    }
}
fn report_keyword(key: &[u8]) -> &[u8] {
    match key {
        b"exclusiveMaximum" => b"maximum",
        b"exclusiveMinimum" => b"minimum",
        b"dependentRequired" | b"dependentSchemas" => b"dependencies",
        _ => key,
    }
}
fn multiple(value: f64, divisor: f64) -> bool {
    let value = value.abs();
    let divisor = divisor.abs();
    let result = value - (value / divisor).floor() * divisor;
    result.is_nan() || result <= 0.0
}
fn numeric_order(left: BinaryValue<'_>, right: BinaryValue<'_>) -> Result<Ordering> {
    crate::decimal::compare_values(left, right).map_err(Error::Compare)
}
