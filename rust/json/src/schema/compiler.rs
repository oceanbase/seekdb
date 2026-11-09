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

const NULL: u64 = 1;
const BOOLEAN: u64 = 2;
const STRING: u64 = 4;
const NUMBER: u64 = 8;
const INTEGER: u64 = 16;
const OBJECT: u64 = 32;
const ARRAY: u64 = 64;
const INVALID: u64 = 128;

pub(super) fn compile(
    value: BinaryValue<'_>,
    patterns: &dyn PatternCompiler,
    scalar: ScalarComparator,
) -> Result<Schema> {
    if value.json_type()? != JsonType::Object {
        return Err(Error::InvalidSchema);
    }
    let mut builder = Builder {
        arena: Arena::default(),
        patterns: BTreeMap::new(),
        compiler: patterns,
        root: 0,
        scalar,
    };
    builder.root = builder.arena.load(value)?;
    let map = builder.map(builder.root, b"#")?;
    let map = builder.arena.retain(map);
    Ok(Schema {
        arena: builder.arena,
        map,
        patterns: builder.patterns,
        scalar,
    })
}
struct Map {
    values: Vec<Id>,
    refs: Id,
    pointer: Vec<u8>,
}
struct Builder<'a> {
    arena: Arena,
    patterns: BTreeMap<Vec<u8>, Box<dyn Pattern>>,
    compiler: &'a dyn PatternCompiler,
    root: Id,
    scalar: ScalarComparator,
}
#[derive(Clone)]
struct Frame {
    origin: Id,
    targets: Vec<Id>,
    composition: bool,
    terms: usize,
    map: usize,
}
struct Plan {
    properties: Option<Id>,
    patterns: Option<Id>,
    additional: Option<Id>,
    items: Option<Id>,
    tuple: Option<Id>,
    additional_items: Option<Id>,
    dependencies: Option<Id>,
    compositions: Vec<(&'static [u8], Id)>,
    negation: Option<Id>,
}
enum Prepared {
    Done,
    Reference(Id, Vec<u8>),
    Children(Plan),
}
enum Task {
    Enter(Frame),
    FinishMap {
        map: usize,
        refs: Id,
        key: Vec<u8>,
        origin: Id,
    },
    Properties(Frame, Id),
    Property(Frame, Vec<Id>, Vec<u8>, Id),
    Patterns(Frame, Id, Option<Id>),
    Pattern(Frame, Id, Option<Id>, Vec<Id>, Vec<u8>, Id),
    Additional(Frame, Id, Option<Id>, Option<Id>),
    Items(Frame, Id),
    Tuple(Frame, Id, Option<Id>),
    TupleItem(Frame, Vec<Id>, usize, Id),
    AdditionalItems(Frame, usize, Id),
    Dependencies(Frame, Id),
    SetDependency {
        object: Id,
        key: Vec<u8>,
        terms: usize,
    },
    PublishDependencies(Frame, Id),
    Composition(Frame, &'static [u8], Id),
    FinishBranch {
        frame: Frame,
        key: &'static [u8],
        terms: usize,
        branches: usize,
    },
    PublishComposition(Frame, &'static [u8], usize),
    Negation(Frame, Id),
    PublishNegation(Frame, usize),
}
impl Builder<'_> {
    fn map(&mut self, origin: Id, pointer: &[u8]) -> Result<Id> {
        let root = self.arena.object();
        let refs = self.arena.object();
        let mut maps = vec![Map {
            values: vec![root, refs],
            refs,
            pointer: pointer.to_vec(),
        }];
        let mut lists = vec![Vec::<Id>::new()];
        let mut pending = vec![Task::Enter(Frame {
            origin,
            targets: vec![root],
            composition: false,
            terms: 0,
            map: 0,
        })];
        let mut active = std::collections::HashSet::from([origin]);
        while let Some(task) = pending.pop() {
            match task {
                Task::Enter(frame) => {
                    match self.prepare(
                        frame.origin,
                        &frame.targets,
                        frame.composition,
                        &mut lists[frame.terms],
                        &mut maps[frame.map],
                    )? {
                        Prepared::Done => {}
                        Prepared::Reference(origin, key) => {
                            if !active.insert(origin) {
                                return Err(Error::InvalidSchema);
                            }
                            let root = self.arena.object();
                            let refs = self.arena.object();
                            let map = maps.len();
                            let terms = lists.len();
                            lists.push(Vec::new());
                            pending.push(Task::FinishMap {
                                map,
                                refs: maps[frame.map].refs,
                                key: key.clone(),
                                origin,
                            });
                            maps.push(Map {
                                values: vec![root, refs],
                                refs,
                                pointer: key,
                            });
                            pending.push(Task::Enter(Frame {
                                origin,
                                targets: vec![root],
                                composition: false,
                                terms,
                                map,
                            }));
                        }
                        Prepared::Children(plan) => {
                            if let Some(value) = plan.negation {
                                pending.push(Task::Negation(frame.clone(), value));
                            }
                            for (key, value) in plan.compositions.into_iter().rev() {
                                pending.push(Task::Composition(frame.clone(), key, value));
                            }
                            if let Some(value) = plan.dependencies {
                                pending.push(Task::Dependencies(frame.clone(), value));
                            }
                            if let Some(tuple) = plan.tuple {
                                pending.push(Task::Tuple(
                                    frame.clone(),
                                    tuple,
                                    plan.additional_items,
                                ));
                            }
                            if let Some(value) = plan.items {
                                pending.push(Task::Items(frame.clone(), value));
                            }
                            if let Some(value) = plan.additional {
                                pending.push(Task::Additional(
                                    frame.clone(),
                                    value,
                                    plan.properties,
                                    plan.patterns,
                                ));
                            }
                            if let Some(value) = plan.patterns {
                                pending.push(Task::Patterns(frame.clone(), value, plan.properties));
                            }
                            if let Some(value) = plan.properties {
                                pending.push(Task::Properties(frame, value));
                            }
                        }
                    }
                }
                Task::FinishMap {
                    map,
                    refs,
                    key,
                    origin,
                } => {
                    let value = self.arena.array(std::mem::take(&mut maps[map].values));
                    self.arena.set(refs, &key, value)?;
                    active.remove(&origin);
                }
                Task::Properties(frame, source) => {
                    let targets = self.enter(&frame.targets, b"properties")?;
                    for (key, source) in self.arena.fields(source)?.iter().rev() {
                        pending.push(Task::Property(
                            frame.clone(),
                            targets.clone(),
                            key.clone(),
                            *source,
                        ));
                    }
                }
                Task::Property(mut frame, targets, key, source) => {
                    if self.arena.is_object(source) {
                        frame.targets = self.enter(&targets, &key)?;
                        frame.origin = source;
                        pending.push(Task::Enter(frame));
                    }
                }
                Task::Patterns(frame, patterns, properties) => {
                    let targets = self.enter(&frame.targets, b"patternProperties")?;
                    for (key, source) in self.arena.fields(patterns)?.iter().rev() {
                        pending.push(Task::Pattern(
                            frame.clone(),
                            patterns,
                            properties,
                            targets.clone(),
                            key.clone(),
                            *source,
                        ));
                    }
                }
                Task::Pattern(mut frame, patterns, properties, targets, key, source) => {
                    if !self.arena.is_object(source) {
                        continue;
                    }
                    if !self.pattern(&key)? {
                        self.arena.remove(patterns, &key);
                        continue;
                    }
                    let mut children = self.enter(&targets, &key)?;
                    if let Some(properties) = properties {
                        let property_targets = self.enter(&frame.targets, b"properties")?;
                        for (name, _) in self.arena.fields(properties)?.to_vec() {
                            if self.patterns[&key].is_match(&name)? {
                                for target in &property_targets {
                                    children.push(
                                        self.arena
                                            .get(*target, &name)
                                            .ok_or(Error::MissingSchema)?,
                                    );
                                }
                            }
                        }
                    }
                    frame.origin = source;
                    frame.targets = children;
                    pending.push(Task::Enter(frame));
                }
                Task::Additional(mut frame, source, properties, patterns) => {
                    let allowed = self.allowed(frame.origin, properties, patterns, false)?;
                    let mut children = Vec::new();
                    for target in &frame.targets {
                        let values = if let Some(values) =
                            self.arena.get(*target, b"additionalProperties")
                        {
                            values
                        } else {
                            let values = self.arena.array(Vec::new());
                            self.arena.set(*target, b"additionalProperties", values)?;
                            values
                        };
                        let child = self.arena.object();
                        self.arena.append(values, allowed)?;
                        self.arena.append(values, child)?;
                        children.push(child);
                    }
                    frame.origin = source;
                    frame.targets = children;
                    pending.push(Task::Enter(frame));
                }
                Task::Items(mut frame, source) => {
                    frame.targets = self.enter(&frame.targets, b"items")?;
                    frame.origin = source;
                    pending.push(Task::Enter(frame));
                }
                Task::Tuple(frame, tuple, additional) => {
                    if let Some(value) = additional {
                        pending.push(Task::AdditionalItems(
                            frame.clone(),
                            self.arena.len(tuple),
                            value,
                        ));
                    }
                    let targets = self.enter(&frame.targets, b"tupleItems")?;
                    for (index, source) in self.arena.items(tuple)?.iter().enumerate().rev() {
                        pending.push(Task::TupleItem(
                            frame.clone(),
                            targets.clone(),
                            index,
                            *source,
                        ));
                    }
                }
                Task::TupleItem(mut frame, targets, index, source) => {
                    frame.targets = self.enter(&targets, index.to_string().as_bytes())?;
                    frame.origin = source;
                    pending.push(Task::Enter(frame));
                }
                Task::AdditionalItems(mut frame, count, source) => {
                    let targets = self.enter(&frame.targets, b"additionalItems")?;
                    frame.targets = self.enter(&targets, count.to_string().as_bytes())?;
                    frame.origin = source;
                    pending.push(Task::Enter(frame));
                }
                Task::Dependencies(frame, source) => {
                    let object = self.arena.object();
                    pending.push(Task::PublishDependencies(frame.clone(), object));
                    for (key, source) in self.arena.fields(source)?.iter().rev() {
                        if self.arena.is_object(*source) {
                            let terms = lists.len();
                            lists.push(Vec::new());
                            pending.push(Task::SetDependency {
                                object,
                                key: key.clone(),
                                terms,
                            });
                            pending.push(Task::Enter(Frame {
                                origin: *source,
                                composition: true,
                                terms,
                                ..frame.clone()
                            }));
                        }
                    }
                }
                Task::SetDependency { object, key, terms } => {
                    let children = self.arena.array(std::mem::take(&mut lists[terms]));
                    self.arena.set(object, &key, children)?;
                }
                Task::PublishDependencies(frame, value) => {
                    if frame.composition {
                        let rule = self.arena.object();
                        self.arena.set(rule, b"dependentSchemas", value)?;
                        lists[frame.terms].push(rule);
                    } else {
                        for target in &frame.targets {
                            if let Some(old) = self.arena.get(*target, b"dependentSchemas") {
                                for (key, new) in self.arena.fields(value)?.to_vec() {
                                    if let Some(previous) = self.arena.get(old, &key) {
                                        self.arena.append(previous, new)?;
                                    } else {
                                        self.arena.set(old, &key, new)?;
                                    }
                                }
                            } else {
                                self.arena.set(*target, b"dependentSchemas", value)?;
                            }
                        }
                    }
                }
                Task::Composition(frame, key, source) => {
                    let branches = lists.len();
                    lists.push(Vec::new());
                    pending.push(Task::PublishComposition(frame.clone(), key, branches));
                    for source in self.arena.items(source)?.iter().rev() {
                        let terms = lists.len();
                        lists.push(Vec::new());
                        pending.push(Task::FinishBranch {
                            frame: frame.clone(),
                            key,
                            terms,
                            branches,
                        });
                        pending.push(Task::Enter(Frame {
                            origin: *source,
                            composition: true,
                            terms,
                            ..frame.clone()
                        }));
                    }
                }
                Task::FinishBranch {
                    frame,
                    key,
                    terms,
                    branches,
                } => {
                    let children = self.arena.array(std::mem::take(&mut lists[terms]));
                    lists[branches].push(children);
                    if !frame.composition {
                        for target in &frame.targets {
                            self.append_keyword(*target, key, children)?;
                        }
                    }
                }
                Task::PublishComposition(frame, key, branches) => {
                    if frame.composition {
                        let value = self.arena.array(std::mem::take(&mut lists[branches]));
                        let rule = self.arena.object();
                        self.arena.set(rule, key, value)?;
                        lists[frame.terms].push(rule);
                    }
                }
                Task::Negation(frame, source) => {
                    let terms = lists.len();
                    lists.push(Vec::new());
                    pending.push(Task::PublishNegation(frame.clone(), terms));
                    pending.push(Task::Enter(Frame {
                        origin: source,
                        composition: true,
                        terms,
                        ..frame
                    }));
                }
                Task::PublishNegation(frame, terms) => {
                    let value = self.arena.array(std::mem::take(&mut lists[terms]));
                    if frame.composition {
                        let rule = self.arena.object();
                        self.arena.set(rule, b"not", value)?;
                        lists[frame.terms].push(rule);
                    } else {
                        for target in &frame.targets {
                            if let Some(previous) = self.arena.get(*target, b"not") {
                                for index in 0..self.arena.len(value) {
                                    let child = self
                                        .arena
                                        .element(value, index)
                                        .map_err(|_| Error::InvalidArgument)?;
                                    self.arena.append(previous, child)?;
                                }
                            } else {
                                self.arena.set(*target, b"not", value)?;
                            }
                        }
                    }
                }
            }
        }
        Ok(self.arena.array(std::mem::take(&mut maps[0].values)))
    }
    fn pattern(&mut self, text: &[u8]) -> Result<bool> {
        if self.patterns.contains_key(text) {
            return Ok(true);
        }
        match self.compiler.compile(text) {
            Ok(pattern) => {
                self.patterns.insert(text.to_vec(), pattern);
                Ok(true)
            }
            Err(PatternError::Invalid) => Ok(false),
            Err(PatternError::Resource(error)) => Err(Error::Pattern(error)),
        }
    }
    fn enter(&mut self, targets: &[Id], key: &[u8]) -> Result<Vec<Id>> {
        targets
            .iter()
            .map(|id| self.arena.child(*id, key))
            .collect()
    }
    fn containers(&mut self, targets: &[Id], composition: bool) -> Result<Vec<Id>> {
        if !composition {
            return self.enter(targets, b"schema");
        }
        targets
            .iter()
            .map(|id| {
                if let Some(value) = self.arena.get(*id, b"composition") {
                    self.arena.items(value)?;
                    Ok(value)
                } else {
                    let value = self.arena.array(Vec::new());
                    self.arena.set(*id, b"composition", value)?;
                    Ok(value)
                }
            })
            .collect()
    }
    fn emit(
        &mut self,
        key: &[u8],
        value: Id,
        containers: &[Id],
        composition: bool,
        terms: &mut Vec<Id>,
        map: &mut Map,
    ) -> Result<()> {
        if composition {
            let rule = self.arena.object();
            self.arena.set(rule, key, value)?;
            for (i, container) in containers.iter().enumerate() {
                let entry = if i == 0 { rule } else { self.arena.int(-1) };
                let index = self.arena.int(map.values.len() as i64);
                map.values.push(entry);
                self.arena.append(*container, index)?;
                let term = self.arena.int((map.values.len() - 1) as i64);
                terms.push(term);
            }
        } else {
            let mut value = value;
            for container in containers {
                if let Some(old) = self.arena.get(*container, key) {
                    if !self
                        .arena
                        .equal(old, value, false, self.scalar)
                        .unwrap_or(false)
                    {
                        if let Some(replacement) = self.merge(key, old, value)? {
                            value = replacement;
                            self.arena.set(*container, key, value)?;
                        }
                    }
                } else {
                    self.arena.set(*container, key, value)?;
                }
            }
        }
        Ok(())
    }
    fn merge(&mut self, key: &[u8], old: Id, new: Id) -> Result<Option<Id>> {
        match key {
            b"type" => {
                let a = self.arena.integer(old)? as u64;
                let b = self.arena.integer(new)? as u64;
                let mut flags = (a & b) & !INTEGER;
                if (a & INTEGER != 0 && b & (INTEGER | NUMBER) != 0)
                    || (b & INTEGER != 0 && a & (INTEGER | NUMBER) != 0)
                {
                    flags |= INTEGER
                }
                if a & INVALID != 0 || b & INVALID != 0 || flags == 0 {
                    flags = INVALID
                }
                self.arena.nodes[old] = Node::Scalar(BinaryDocument::new(binary::serialize(
                    Document::Number(Number::U64(flags)),
                ))?);
                Ok(None)
            }
            b"enum" | b"required" => {
                let before = self.arena.items(old)?.to_vec();
                let incoming = self.arena.items(new)?.to_vec();
                if before.is_empty() {
                    return Ok(Some(new));
                }
                if incoming.is_empty() {
                    return Ok(None);
                }
                let mut seen = before;
                let mut out = Vec::new();
                for id in incoming {
                    if self.arena.kind(id)? != JsonType::String {
                        continue;
                    }
                    let duplicate = seen.iter().any(|other| {
                        self.arena
                            .equal(*other, id, true, self.scalar)
                            .unwrap_or(false)
                    });
                    if (key == b"enum" && duplicate) || (key == b"required" && !duplicate) {
                        out.push(id)
                    }
                    if !duplicate {
                        seen.push(id)
                    }
                }
                self.arena.replace_array(old, out);
                Ok(None)
            }
            b"maxLength" | b"maxProperties" | b"maxItems" | b"maximum" | b"exclusiveMaximum"
            | b"additionalItems" => Ok((crate::decimal::compare_values(
                self.arena.value(old)?,
                self.arena.value(new)?,
            )? == std::cmp::Ordering::Greater)
                .then_some(new)),
            b"minLength" | b"minProperties" | b"minItems" | b"minimum" | b"exclusiveMinimum" => Ok(
                (crate::decimal::compare_values(self.arena.value(old)?, self.arena.value(new)?)?
                    == std::cmp::Ordering::Less)
                    .then_some(new),
            ),
            b"$ref" | b"pattern" | b"multipleOf" | b"dependentRequired" => {
                if self.arena.is_array(old) {
                    self.arena.append(old, new)?;
                    Ok(None)
                } else {
                    Ok(Some(self.arena.array(vec![old, new])))
                }
            }
            b"additionalProperties" => {
                let items = self.arena.items(new)?.to_vec();
                for id in items {
                    self.arena.append(old, id)?;
                }
                Ok(None)
            }
            _ => Err(Error::InvalidMap),
        }
    }
    fn prepare(
        &mut self,
        origin: Id,
        targets: &[Id],
        composition: bool,
        terms: &mut Vec<Id>,
        map: &mut Map,
    ) -> Result<Prepared> {
        let containers = self.containers(targets, composition)?;
        if let Some(reference) = self.arena.get(origin, b"$ref") {
            if self.arena.kind(reference)? == JsonType::String {
                let text = self.arena.bytes(reference)?.to_vec();
                if legal_reference(&text) {
                    if let Some(target) = self.reference(&text) {
                        self.emit(b"$ref", reference, &containers, composition, terms, map)?;
                        if self.arena.get(map.refs, &text).is_none() {
                            if text == map.pointer {
                                let value = self.arena.int(-1);
                                self.arena.set(map.refs, &text, value)?;
                            } else {
                                return Ok(Prepared::Reference(target, text));
                            }
                        }
                    }
                    return Ok(Prepared::Done);
                }
            }
        }
        let flags = if let Some(value) = self.arena.get(origin, b"type") {
            let flags = match self.arena.kind(value)? {
                JsonType::String => type_flag(self.arena.bytes(value)?),
                JsonType::Array => {
                    let mut flags = 0;
                    for id in self.arena.items(value)? {
                        if self.arena.kind(*id)? == JsonType::String {
                            flags |= type_flag(self.arena.bytes(*id)?) & !INVALID;
                        }
                    }
                    if flags == 0 {
                        INVALID
                    } else {
                        flags
                    }
                }
                _ => INVALID,
            };
            let value = self.arena.uint(flags);
            self.emit(b"type", value, &containers, composition, terms, map)?;
            flags
        } else {
            0
        };
        if flags & INVALID != 0 {
            return Ok(Prepared::Done);
        }
        if let Some(value) = self.arena.get(origin, b"enum") {
            if self.arena.is_array(value) && self.arena.len(value) > 0 {
                self.emit(b"enum", value, &containers, composition, terms, map)?;
            }
        }
        let has = |flag| flags == 0 || flags & flag != 0;
        if has(STRING) {
            for key in [b"maxLength".as_slice(), b"minLength"] {
                self.positive(origin, key, &containers, composition, terms, map)?;
            }
            if let Some(value) = self.arena.get(origin, b"pattern") {
                if self.arena.kind(value)? == JsonType::String {
                    let bytes = self.arena.bytes(value)?.to_vec();
                    if self.pattern(&bytes)? {
                        self.emit(b"pattern", value, &containers, composition, terms, map)?;
                    }
                }
            }
        }
        if has(NUMBER | INTEGER) {
            for (key, exclusive) in [
                (b"maximum".as_slice(), b"exclusiveMaximum".as_slice()),
                (b"minimum", b"exclusiveMinimum"),
            ] {
                if let Some(value) = self.arena.get(origin, key) {
                    if is_number(self.arena.kind(value)?) {
                        let key = if self
                            .arena
                            .get(origin, exclusive)
                            .is_some_and(|id| self.arena.boolean(id).unwrap_or(false))
                        {
                            exclusive
                        } else {
                            key
                        };
                        self.emit(key, value, &containers, composition, terms, map)?;
                    }
                }
            }
            if let Some(value) = self.arena.get(origin, b"multipleOf") {
                if is_number(self.arena.kind(value)?) && self.arena.number(value)? >= 0.0 {
                    self.emit(b"multipleOf", value, &containers, composition, terms, map)?;
                }
            }
        }
        let mut properties = None;
        let mut patterns = None;
        let mut additional = None;
        let mut dependencies = None;
        if has(OBJECT) {
            for key in [b"maxProperties".as_slice(), b"minProperties"] {
                self.positive(origin, key, &containers, composition, terms, map)?;
            }
            if let Some(deps) = self.nonempty_object(origin, b"dependencies") {
                let mut required = Vec::new();
                for (key, value) in self.arena.fields(deps)?.to_vec() {
                    if self.arena.is_object(value) && self.arena.len(value) > 0 {
                        dependencies = Some(deps)
                    } else if self.arena.is_array(value) && self.arena.len(value) > 0 {
                        required.push((key.clone(), value));
                        self.arena.remove(deps, &key);
                    }
                }
                if !required.is_empty() {
                    let value = self.arena.add(Node::Object(required));
                    self.emit(
                        b"dependentRequired",
                        value,
                        &containers,
                        composition,
                        terms,
                        map,
                    )?;
                }
            }
            if let Some(value) = self.arena.get(origin, b"required") {
                if self.arena.is_array(value) && self.arena.len(value) > 0 {
                    self.emit(b"required", value, &containers, composition, terms, map)?;
                }
            }
            properties = self.nonempty_object(origin, b"properties");
            patterns = self.nonempty_object(origin, b"patternProperties");
            if let Some(value) = self.arena.get(origin, b"additionalProperties") {
                if self.arena.kind(value)? == JsonType::Boolean && !self.arena.boolean(value)? {
                    let allowed = self.allowed(origin, properties, patterns, true)?;
                    self.emit(
                        b"additionalProperties",
                        allowed,
                        &containers,
                        composition,
                        terms,
                        map,
                    )?;
                } else if self.arena.is_object(value) && self.arena.len(value) > 0 {
                    additional = Some(value)
                }
            }
        }
        let mut items = None;
        let mut tuple = None;
        let mut additional_items = None;
        if has(ARRAY) {
            for key in [b"maxItems".as_slice(), b"minItems"] {
                self.positive(origin, key, &containers, composition, terms, map)?;
            }
            if let Some(value) = self.arena.get(origin, b"items") {
                if self.arena.len(value) > 0 {
                    if self.arena.is_object(value) {
                        items = Some(value)
                    } else if self.arena.is_array(value) {
                        self.subschemas(value)?;
                        tuple = Some(value)
                    }
                }
            }
            if let Some(tuple) = tuple {
                if let Some(value) = self.arena.get(origin, b"additionalItems") {
                    if self.arena.kind(value)? == JsonType::Boolean && !self.arena.boolean(value)? {
                        let count = self.arena.int(self.arena.len(tuple) as i64);
                        self.emit(
                            b"additionalItems",
                            count,
                            &containers,
                            composition,
                            terms,
                            map,
                        )?;
                    } else if self.arena.is_object(value) && self.arena.len(value) > 0 {
                        additional_items = Some(value)
                    }
                }
            }
            if let Some(value) = self.arena.get(origin, b"uniqueItems") {
                if self.arena.kind(value)? == JsonType::Boolean && self.arena.boolean(value)? {
                    self.emit(b"uniqueItems", value, &containers, composition, terms, map)?;
                }
            }
        }
        let mut compositions = Vec::new();
        for key in [b"allOf".as_slice(), b"anyOf", b"oneOf"] {
            if let Some(value) = self.arena.get(origin, key) {
                if self.arena.is_array(value) && self.arena.len(value) > 0 {
                    self.subschemas(value)?;
                    compositions.push((key, value));
                }
            }
        }
        let negation = self
            .arena
            .get(origin, b"not")
            .filter(|id| self.arena.is_object(*id));
        Ok(Prepared::Children(Plan {
            properties,
            patterns,
            additional,
            items,
            tuple,
            additional_items,
            dependencies,
            compositions,
            negation,
        }))
    }
    fn append_keyword(&mut self, target: Id, key: &[u8], value: Id) -> Result<()> {
        if let Some(old) = self.arena.get(target, key) {
            self.arena.append(old, value)
        } else {
            let values = self.arena.array(vec![value]);
            self.arena.set(target, key, values)
        }
    }
    fn positive(
        &mut self,
        origin: Id,
        key: &[u8],
        containers: &[Id],
        composition: bool,
        terms: &mut Vec<Id>,
        map: &mut Map,
    ) -> Result<()> {
        if let Some(value) = self.arena.get(origin, key) {
            if self.arena.kind(value)? == JsonType::Int && (self.arena.integer(value)? as i32) >= 0
            {
                self.emit(key, value, containers, composition, terms, map)?;
            }
        }
        Ok(())
    }
    fn nonempty_object(&self, origin: Id, key: &[u8]) -> Option<Id> {
        self.arena
            .get(origin, key)
            .filter(|id| self.arena.is_object(*id) && self.arena.len(*id) > 0)
    }
    fn subschemas(&self, value: Id) -> Result<()> {
        if self
            .arena
            .items(value)?
            .iter()
            .any(|id| !self.arena.is_object(*id))
        {
            Err(Error::InvalidSchema)
        } else {
            Ok(())
        }
    }
    fn allowed(
        &mut self,
        origin: Id,
        properties: Option<Id>,
        patterns: Option<Id>,
        check_pattern: bool,
    ) -> Result<Id> {
        let deps = self
            .arena
            .get(origin, b"dependencies")
            .filter(|id| self.arena.is_object(*id));
        let required = self
            .arena
            .get(origin, b"required")
            .filter(|id| self.arena.is_array(*id));
        let mut names = Vec::new();
        if let Some(object) = properties.or(deps) {
            if let Some(required) = required {
                for id in self.arena.items(required)?.to_vec() {
                    if self.arena.kind(id)? == JsonType::String {
                        let key = self.arena.bytes(id)?.to_vec();
                        if self.arena.get(object, &key).is_none() {
                            let typeless = self.arena.int(-1);
                            self.arena.set(object, &key, typeless)?;
                        }
                    }
                }
            }
            for (key, _) in self.arena.fields(object)?.to_vec() {
                names.push(self.arena.string(&key)?);
            }
            if properties.is_some() {
                if let Some(deps) = deps {
                    for (key, _) in self.arena.fields(deps)?.to_vec() {
                        if self.arena.get(object, &key).is_none() {
                            names.push(self.arena.string(&key)?);
                        }
                    }
                }
            }
        } else if let Some(required) = required {
            for id in self.arena.items(required)?.to_vec() {
                if self.arena.kind(id)? == JsonType::String {
                    let key = self.arena.bytes(id)?.to_vec();
                    names.push(self.arena.string(&key)?);
                }
            }
        }
        let mut regexes = Vec::new();
        if let Some(patterns) = patterns {
            for (key, _) in self.arena.fields(patterns)?.to_vec() {
                if !check_pattern || self.pattern(&key)? {
                    regexes.push(self.arena.string(&key)?);
                }
            }
        }
        let names = self.arena.array(names);
        let regexes = self.arena.array(regexes);
        Ok(self.arena.array(vec![names, regexes]))
    }
    fn reference(&self, text: &[u8]) -> Option<Id> {
        let mut value = self.root;
        for part in text.split(|b| *b == b'/') {
            if part.is_empty() {
                return None;
            }
            if part == b"#" {
                value = self.root
            } else {
                value = self.arena.get(value, part)?;
            }
        }
        self.arena.is_object(value).then_some(value)
    }
}
fn type_flag(text: &[u8]) -> u64 {
    match text {
        b"null" => NULL,
        b"boolean" => BOOLEAN,
        b"string" => STRING,
        b"number" => NUMBER,
        b"integer" => INTEGER,
        b"object" => OBJECT,
        b"array" => ARRAY,
        _ => INVALID,
    }
}
fn legal_reference(text: &[u8]) -> bool {
    !text.is_empty()
        && text.split(|b| *b == b'/').all(|part| {
            part == b"#"
                || part.iter().enumerate().all(|(i, b)| {
                    b.is_ascii_alphabetic()
                        || *b == b'-'
                        || *b == b'_'
                        || (i > 0 && b.is_ascii_digit())
                })
        })
}
pub(super) fn load_patterns(
    arena: &Arena,
    id: Id,
    compiler: &dyn PatternCompiler,
    patterns: &mut BTreeMap<Vec<u8>, Box<dyn Pattern>>,
) -> Result<()> {
    fn add(
        text: &[u8],
        compiler: &dyn PatternCompiler,
        patterns: &mut BTreeMap<Vec<u8>, Box<dyn Pattern>>,
    ) -> Result<()> {
        if !patterns.contains_key(text) {
            match compiler.compile(text) {
                Ok(pattern) => {
                    patterns.insert(text.to_vec(), pattern);
                }
                Err(PatternError::Invalid) => {}
                Err(PatternError::Resource(error)) => return Err(Error::Pattern(error)),
            }
        }
        Ok(())
    }
    enum Task {
        Visit(Id),
        Pattern(Id),
        PatternProperties(Id),
        Additional(Id),
    }
    let mut pending = vec![Task::Visit(id)];
    let mut visited = std::collections::HashSet::new();
    while let Some(task) = pending.pop() {
        match task {
            Task::Visit(id) => {
                if !visited.insert(id) {
                    continue;
                }
                match &arena.nodes[id] {
                    Node::Object(values) => {
                        pending.push(Task::Additional(id));
                        for (key, value) in values.iter().rev() {
                            if !matches!(
                                key.as_slice(),
                                b"enum" | b"required" | b"dependentRequired"
                            ) {
                                pending.push(Task::Visit(*value));
                            }
                            if key == b"patternProperties" {
                                pending.push(Task::PatternProperties(*value));
                            }
                            if key == b"pattern" {
                                pending.push(Task::Pattern(*value));
                            }
                        }
                    }
                    Node::Array(values) => {
                        pending.extend(values.iter().rev().map(|id| Task::Visit(*id)))
                    }
                    Node::Scalar(_) => {}
                }
            }
            Task::Pattern(value) => {
                if arena.kind(value)? == JsonType::String {
                    add(arena.bytes(value)?, compiler, patterns)?;
                } else if arena.is_array(value) {
                    for child in arena.items(value)? {
                        add(arena.bytes(*child)?, compiler, patterns)?;
                    }
                }
            }
            Task::PatternProperties(value) => {
                for (key, _) in arena.fields(value)? {
                    add(key, compiler, patterns)?;
                }
            }
            Task::Additional(id) => {
                for (key, value) in arena.fields(id)? {
                    if key != b"additionalProperties" {
                        continue;
                    }
                    if let Ok(parts) = arena.items(*value) {
                        let allowed = if parts.get(1).is_some_and(|id| arena.is_object(*id)) {
                            parts.iter().step_by(2).copied().collect::<Vec<_>>()
                        } else {
                            vec![*value]
                        };
                        for allowed in allowed {
                            for regexes in arena.items(allowed)?.iter().skip(1).step_by(2) {
                                for regex in arena.items(*regexes)? {
                                    add(arena.bytes(*regex)?, compiler, patterns)?;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    Ok(())
}
