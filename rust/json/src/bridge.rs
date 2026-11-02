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

use crate::{BinaryResult, DuplicatePolicy, ParseOptions, Path};
use serde::de::{DeserializeSeed, Deserializer, MapAccess, SeqAccess, Visitor};
use std::collections::HashMap;
use std::fmt;
use std::hash::{Hash, Hasher};

#[cxx::bridge(namespace = "oceanbase::common::json")]
pub mod ffi {
    #[repr(u32)]
    enum Duplicate {
        Overwrite = 0,
        Preserve = 1,
        Reject = 2,
        KeepFirst = 3,
    }

    #[repr(i32)]
    #[derive(Debug)]
    enum Status {
        Ok = 0,
        Syntax = 1,
        DuplicateKey = 2,
        Depth = 3,
        UnsupportedSchemaRef = 4,
        InvalidBinary = 5,
        InvalidPath = 6,
        NumericOverflow = 7,
    }

    #[repr(u32)]
    #[derive(Debug)]
    enum EventKind {
        Null = 0,
        Bool = 1,
        I64 = 2,
        U64 = 3,
        F64 = 4,
        String = 5,
        StartArray = 6,
        EndArray = 7,
        StartObject = 8,
        Key = 9,
        EndObject = 10,
    }

    struct Event {
        kind: EventKind,
        bool_value: bool,
        i64_value: i64,
        u64_value: u64,
        f64_value: f64,
        bytes: Vec<u8>,
    }

    struct ParseResult {
        status: Status,
        byte_offset: u64,
        message: String,
        events: Vec<Event>,
    }

    struct BinaryResult {
        status: Status,
        byte_offset: u64,
        message: String,
        binary: Vec<u8>,
    }

    struct BinaryMetadataResult {
        status: Status,
        byte_offset: u64,
        value_type: u8,
        depth: u32,
        opaque_field_type: u16,
        length: u64,
    }

    struct PathQueryResult {
        status: Status,
        byte_offset: u64,
        match_count: u64,
        single_length: u64,
    }

    struct PathValueResult {
        status: Status,
        byte_offset: u64,
        match_count: u64,
        value_type: u8,
        use_wrapper: bool,
        wrapper_required: bool,
        binary: Vec<u8>,
    }

    struct BoolResult {
        status: Status,
        byte_offset: u64,
        value: bool,
        is_null: bool,
    }

    extern "Rust" {
        fn parse(input: &[u8], duplicate: Duplicate, max_depth: u32, schema: bool) -> ParseResult;
        fn validate(
            input: &[u8],
            duplicate: Duplicate,
            max_depth: u32,
            schema: bool,
        ) -> ParseResult;
        fn parse_binary(
            input: &[u8],
            duplicate: Duplicate,
            max_depth: u32,
            schema: bool,
        ) -> BinaryResult;
        fn inspect_binary(input: &[u8], with_depth: bool) -> BinaryMetadataResult;
        fn inspect_text(input: &[u8], with_depth: bool, max_depth: u32) -> BinaryMetadataResult;
        type Path;
        type PathParseResult;
        fn parse_mysql_path(input: &[u8]) -> Box<PathParseResult>;
        fn status(self: &PathParseResult) -> Status;
        fn byte_offset(self: &PathParseResult) -> u64;
        fn path(self: &PathParseResult) -> &Path;
        fn can_match_many(self: &Path) -> bool;
        fn query_binary(
            path: &Path,
            input: &[u8],
            auto_wrap: bool,
            first_only: bool,
        ) -> PathQueryResult;
        fn query_text(
            path: &Path,
            input: &[u8],
            auto_wrap: bool,
            first_only: bool,
            max_depth: u32,
        ) -> PathQueryResult;
        type Query;
        fn new_query(input: &[u8], text: bool, max_depth: u32) -> Box<Query>;
        fn status(self: &Query) -> Status;
        fn byte_offset(self: &Query) -> u64;
        fn add_path(self: &mut Query, path: &Path) -> Status;
        fn finish(self: &mut Query) -> BinaryResult;
        fn query(self: &Query, path: &Path, auto_wrap: bool, first_only: bool) -> PathQueryResult;
        fn query_value(self: &Query, path: &Path, wrap_result: bool) -> PathValueResult;
        fn query_value_with_options(
            self: &Query,
            path: &Path,
            wrapper: u8,
            scalars: u8,
            wrap_single: bool,
            wrap_multiple: bool,
        ) -> PathValueResult;
        fn root_value(self: &Query, wrap_result: bool) -> PathValueResult;
        fn empty_container(object: bool) -> Vec<u8>;
        fn object_keys(self: &Query) -> BinaryResult;
        fn object_keys_at_path(self: &Query, path: &Path) -> BinaryResult;
        fn contains(self: &Query, candidate: &Query) -> BoolResult;
        fn contains_at_path(self: &Query, candidate: &Query, path: &Path) -> BoolResult;
        fn overlaps(self: &Query, other: &Query) -> BoolResult;
        fn member_of(self: &Query, target: &Query) -> BoolResult;
        fn search(self: &Query, target: &[u8], escape: u32, first_only: bool) -> BinaryResult;
    }
}

pub struct Query {
    state: Result<crate::Query, (ffi::Status, u64)>,
    paths: Vec<Path>,
}

impl Query {
    fn status(&self) -> ffi::Status {
        match &self.state {
            Ok(_) => ffi::Status::Ok,
            Err((status, _)) => *status,
        }
    }

    fn byte_offset(&self) -> u64 {
        match &self.state {
            Ok(_) => 0,
            Err((_, offset)) => *offset,
        }
    }

    fn add_path(&mut self, path: &Path) -> ffi::Status {
        match &mut self.state {
            Ok(_) => {
                self.paths.push(path.clone());
                ffi::Status::Ok
            }
            Err((status, _)) => *status,
        }
    }

    fn finish(&mut self) -> ffi::BinaryResult {
        let state = std::mem::replace(&mut self.state, Err((ffi::Status::InvalidBinary, 0)));
        let paths = std::mem::take(&mut self.paths);
        match state {
            Ok(document) => optional_binary_result(document.extract(&paths)),
            Err((status, byte_offset)) => ffi::BinaryResult {
                status,
                byte_offset,
                message: String::new(),
                binary: Vec::new(),
            },
        }
    }

    fn query(&self, path: &Path, auto_wrap: bool, first_only: bool) -> ffi::PathQueryResult {
        match &self.state {
            Ok(document) => {
                let result = document
                    .query(
                        path,
                        crate::QueryOptions {
                            auto_wrap,
                            first_only,
                        },
                    )
                    .and_then(|values| {
                        let single_length = if values.len() == 1 {
                            values[0].len()?
                        } else {
                            0
                        };
                        Ok((values.len() as u64, single_length))
                    });
                query_result(result)
            }
            Err((status, byte_offset)) => ffi::PathQueryResult {
                status: *status,
                byte_offset: *byte_offset,
                match_count: 0,
                single_length: 0,
            },
        }
    }

    fn query_value(&self, path: &Path, wrap_result: bool) -> ffi::PathValueResult {
        let result = match &self.state {
            Ok(document) => document.query_output(path, wrap_result),
            Err((status, byte_offset)) => {
                return ffi::PathValueResult {
                    status: *status,
                    byte_offset: *byte_offset,
                    match_count: 0,
                    value_type: 0,
                    use_wrapper: false,
                    wrapper_required: false,
                    binary: Vec::new(),
                }
            }
        };
        match result {
            Ok(output) => ffi::PathValueResult {
                status: ffi::Status::Ok,
                byte_offset: 0,
                match_count: output.match_count as u64,
                value_type: output.value_type.map_or(0, |value_type| value_type as u8),
                use_wrapper: output.use_wrapper,
                wrapper_required: output.wrapper_required,
                binary: output.binary.unwrap_or_default(),
            },
            Err(error) => ffi::PathValueResult {
                status: ffi::Status::InvalidBinary,
                byte_offset: error.byte_offset as u64,
                match_count: 0,
                value_type: 0,
                use_wrapper: false,
                wrapper_required: false,
                binary: Vec::new(),
            },
        }
    }

    fn query_value_with_options(
        &self,
        path: &Path,
        wrapper: u8,
        scalars: u8,
        wrap_single: bool,
        wrap_multiple: bool,
    ) -> ffi::PathValueResult {
        let wrapper = match wrapper {
            2 | 3 | 4 | 6 => crate::WrapperPolicy::With,
            5 | 7 => crate::WrapperPolicy::Conditional,
            _ => crate::WrapperPolicy::Without,
        };
        let result = match &self.state {
            Ok(document) => document.query_output_with_options(
                path,
                crate::QueryOutputOptions {
                    wrapper,
                    allow_scalar: scalars != 1,
                    wrap_single,
                    wrap_multiple,
                },
            ),
            Err((status, byte_offset)) => {
                return ffi::PathValueResult {
                    status: *status,
                    byte_offset: *byte_offset,
                    match_count: 0,
                    value_type: 0,
                    use_wrapper: false,
                    wrapper_required: false,
                    binary: Vec::new(),
                }
            }
        };
        match result {
            Ok(output) => ffi::PathValueResult {
                status: ffi::Status::Ok,
                byte_offset: 0,
                match_count: output.match_count as u64,
                value_type: output.value_type.map_or(0, |value_type| value_type as u8),
                use_wrapper: output.use_wrapper,
                wrapper_required: output.wrapper_required,
                binary: output.binary.unwrap_or_default(),
            },
            Err(error) => ffi::PathValueResult {
                status: ffi::Status::InvalidBinary,
                byte_offset: error.byte_offset as u64,
                match_count: 0,
                value_type: 0,
                use_wrapper: false,
                wrapper_required: false,
                binary: Vec::new(),
            },
        }
    }

    fn root_value(&self, wrap_result: bool) -> ffi::PathValueResult {
        let result = match &self.state {
            Ok(document) => document.root_output(wrap_result),
            Err((status, byte_offset)) => {
                return ffi::PathValueResult {
                    status: *status,
                    byte_offset: *byte_offset,
                    match_count: 0,
                    value_type: 0,
                    use_wrapper: false,
                    wrapper_required: false,
                    binary: Vec::new(),
                }
            }
        };
        match result {
            Ok(output) => ffi::PathValueResult {
                status: ffi::Status::Ok,
                byte_offset: 0,
                match_count: output.match_count as u64,
                value_type: output.value_type.map_or(0, |value_type| value_type as u8),
                use_wrapper: output.use_wrapper,
                wrapper_required: output.wrapper_required,
                binary: output.binary.unwrap_or_default(),
            },
            Err(error) => ffi::PathValueResult {
                status: ffi::Status::InvalidBinary,
                byte_offset: error.byte_offset as u64,
                match_count: 0,
                value_type: 0,
                use_wrapper: false,
                wrapper_required: false,
                binary: Vec::new(),
            },
        }
    }

    fn object_keys(&self) -> ffi::BinaryResult {
        match &self.state {
            Ok(document) => optional_binary_result(document.object_keys(None)),
            Err((status, byte_offset)) => failed_binary_result(*status, *byte_offset),
        }
    }

    fn object_keys_at_path(&self, path: &Path) -> ffi::BinaryResult {
        match &self.state {
            Ok(document) => optional_binary_result(document.object_keys(Some(path))),
            Err((status, byte_offset)) => failed_binary_result(*status, *byte_offset),
        }
    }

    fn contains(&self, candidate: &Query) -> ffi::BoolResult {
        bool_result(match (&self.state, &candidate.state) {
            (Ok(target), Ok(candidate)) => target.contains(candidate, None).map_err(compare_error),
            (Err(error), _) | (_, Err(error)) => Err(*error),
        })
    }

    fn contains_at_path(&self, candidate: &Query, path: &Path) -> ffi::BoolResult {
        bool_result(match (&self.state, &candidate.state) {
            (Ok(target), Ok(candidate)) => target
                .contains(candidate, Some(path))
                .map_err(compare_error),
            (Err(error), _) | (_, Err(error)) => Err(*error),
        })
    }

    fn overlaps(&self, other: &Query) -> ffi::BoolResult {
        bool_result(match (&self.state, &other.state) {
            (Ok(left), Ok(right)) => left.overlaps(right).map(Some).map_err(compare_error),
            (Err(error), _) | (_, Err(error)) => Err(*error),
        })
    }

    fn member_of(&self, target: &Query) -> ffi::BoolResult {
        bool_result(match (&self.state, &target.state) {
            (Ok(candidate), Ok(target)) => {
                candidate.member_of(target).map(Some).map_err(compare_error)
            }
            (Err(error), _) | (_, Err(error)) => Err(*error),
        })
    }

    fn search(&self, target: &[u8], escape: u32, first_only: bool) -> ffi::BinaryResult {
        let document = match &self.state {
            Ok(document) => document,
            Err((status, byte_offset)) => return failed_binary_result(*status, *byte_offset),
        };
        let target = match std::str::from_utf8(target) {
            Ok(target) => target,
            Err(error) => {
                return failed_binary_result(ffi::Status::Syntax, error.valid_up_to() as u64)
            }
        };
        let Some(escape) = char::from_u32(escape) else {
            return failed_binary_result(ffi::Status::Syntax, 0);
        };
        optional_binary_result(document.search(
            &self.paths,
            crate::SearchOptions {
                pattern: target,
                escape,
                first_only,
            },
        ))
    }
}

fn binary_error(error: crate::BinaryError) -> (ffi::Status, u64) {
    (ffi::Status::InvalidBinary, error.byte_offset as u64)
}

fn compare_error(error: crate::CompareError) -> (ffi::Status, u64) {
    match error {
        crate::CompareError::Binary(error) => binary_error(error),
        crate::CompareError::NumericOverflow => (ffi::Status::NumericOverflow, 0),
    }
}

fn bool_result(result: Result<Option<bool>, (ffi::Status, u64)>) -> ffi::BoolResult {
    match result {
        Ok(value) => ffi::BoolResult {
            status: ffi::Status::Ok,
            byte_offset: 0,
            value: value.unwrap_or(false),
            is_null: value.is_none(),
        },
        Err((status, byte_offset)) => ffi::BoolResult {
            status,
            byte_offset,
            value: false,
            is_null: false,
        },
    }
}

fn new_query(input: &[u8], text: bool, max_depth: u32) -> Box<Query> {
    let state = if text {
        crate::Query::from_text(
            input,
            ParseOptions {
                max_depth,
                ..ParseOptions::default()
            },
        )
        .map_err(|error| (parse_status(&error), error.byte_offset as u64))
    } else {
        crate::Query::from_binary(input.to_vec())
            .map_err(|error| (ffi::Status::InvalidBinary, error.byte_offset as u64))
    };
    Box::new(Query {
        state,
        paths: Vec::new(),
    })
}

fn optional_binary_result(result: BinaryResult<Option<Vec<u8>>>) -> ffi::BinaryResult {
    match result {
        Ok(binary) => ffi::BinaryResult {
            status: ffi::Status::Ok,
            byte_offset: 0,
            message: String::new(),
            binary: binary.unwrap_or_default(),
        },
        Err(error) => ffi::BinaryResult {
            status: ffi::Status::InvalidBinary,
            byte_offset: error.byte_offset as u64,
            message: error.to_string(),
            binary: Vec::new(),
        },
    }
}

fn failed_binary_result(status: ffi::Status, byte_offset: u64) -> ffi::BinaryResult {
    ffi::BinaryResult {
        status,
        byte_offset,
        message: String::new(),
        binary: Vec::new(),
    }
}

fn policy(duplicate: ffi::Duplicate) -> DuplicatePolicy {
    match duplicate {
        ffi::Duplicate::Overwrite => DuplicatePolicy::Overwrite,
        ffi::Duplicate::Preserve => DuplicatePolicy::Preserve,
        ffi::Duplicate::Reject => DuplicatePolicy::Reject,
        ffi::Duplicate::KeepFirst => DuplicatePolicy::KeepFirst,
        _ => DuplicatePolicy::Overwrite,
    }
}

fn result(result: crate::Result<()>, events: Vec<ffi::Event>) -> ffi::ParseResult {
    match result {
        Ok(()) => ffi::ParseResult {
            status: ffi::Status::Ok,
            byte_offset: 0,
            message: String::new(),
            events,
        },
        Err(error) => ffi::ParseResult {
            status: match error.kind {
                crate::ErrorKind::Syntax => ffi::Status::Syntax,
                crate::ErrorKind::DuplicateKey => ffi::Status::DuplicateKey,
                crate::ErrorKind::Depth => ffi::Status::Depth,
                crate::ErrorKind::UnsupportedSchemaRef => ffi::Status::UnsupportedSchemaRef,
            },
            byte_offset: error.byte_offset as u64,
            message: error.message,
            events: Vec::new(),
        },
    }
}

fn event(kind: ffi::EventKind) -> ffi::Event {
    ffi::Event {
        kind,
        bool_value: false,
        i64_value: 0,
        u64_value: 0,
        f64_value: 0.0,
        bytes: Vec::new(),
    }
}

struct EventSeed<'a> {
    depth: u32,
    options: ParseOptions,
    events: &'a mut Vec<ffi::Event>,
}

impl<'de, 'a> DeserializeSeed<'de> for EventSeed<'a> {
    type Value = ();

    fn deserialize<D>(self, deserializer: D) -> std::result::Result<(), D::Error>
    where
        D: Deserializer<'de>,
    {
        deserializer.deserialize_any(self)
    }
}

struct ObjectEntry {
    key_index: usize,
    value_start: usize,
    value_end: usize,
}

impl<'de, 'a> Visitor<'de> for EventSeed<'a> {
    type Value = ();

    fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        formatter.write_str("a standard JSON value")
    }

    fn visit_unit<E>(self) -> std::result::Result<(), E> {
        self.events.push(event(ffi::EventKind::Null));
        Ok(())
    }

    fn visit_none<E: serde::de::Error>(self) -> std::result::Result<(), E> {
        self.visit_unit()
    }

    fn visit_some<D>(self, deserializer: D) -> std::result::Result<(), D::Error>
    where
        D: Deserializer<'de>,
    {
        self.deserialize(deserializer)
    }

    fn visit_bool<E>(self, value: bool) -> std::result::Result<(), E> {
        let mut item = event(ffi::EventKind::Bool);
        item.bool_value = value;
        self.events.push(item);
        Ok(())
    }

    fn visit_i64<E>(self, value: i64) -> std::result::Result<(), E> {
        let mut item = event(ffi::EventKind::I64);
        item.i64_value = value;
        self.events.push(item);
        Ok(())
    }

    fn visit_u64<E: serde::de::Error>(self, value: u64) -> std::result::Result<(), E> {
        if value <= u32::MAX as u64 {
            self.visit_i64(value as i64)
        } else {
            let mut item = event(ffi::EventKind::U64);
            item.u64_value = value;
            self.events.push(item);
            Ok(())
        }
    }

    fn visit_f64<E>(self, value: f64) -> std::result::Result<(), E> {
        let mut item = event(ffi::EventKind::F64);
        item.f64_value = value;
        self.events.push(item);
        Ok(())
    }

    fn visit_str<E: serde::de::Error>(self, value: &str) -> std::result::Result<(), E> {
        self.visit_string(value.to_owned())
    }

    fn visit_string<E>(self, value: String) -> std::result::Result<(), E> {
        let mut item = event(ffi::EventKind::String);
        item.bytes = value.into_bytes();
        self.events.push(item);
        Ok(())
    }

    fn visit_seq<A>(self, mut sequence: A) -> std::result::Result<(), A::Error>
    where
        A: SeqAccess<'de>,
    {
        if self.depth >= self.options.max_depth {
            return Err(serde::de::Error::custom(crate::DEPTH_ERROR));
        }
        self.events.push(event(ffi::EventKind::StartArray));
        while sequence
            .next_element_seed(EventSeed {
                depth: self.depth + 1,
                options: self.options,
                events: &mut *self.events,
            })?
            .is_some()
        {}
        self.events.push(event(ffi::EventKind::EndArray));
        Ok(())
    }

    fn visit_map<A>(self, mut map: A) -> std::result::Result<(), A::Error>
    where
        A: MapAccess<'de>,
    {
        if self.depth >= self.options.max_depth {
            return Err(serde::de::Error::custom(crate::DEPTH_ERROR));
        }
        self.events.push(event(ffi::EventKind::StartObject));
        let policy = self.options.duplicate_policy;
        let mut entries: Vec<ObjectEntry> = Vec::new();
        let mut positions: HashMap<u64, Vec<usize>> = HashMap::new();
        while let Some(key) = map.next_key::<String>()? {
            let mut hasher = std::collections::hash_map::DefaultHasher::new();
            key.hash(&mut hasher);
            let hash = hasher.finish();
            let duplicate = if policy == DuplicatePolicy::Preserve {
                None
            } else {
                positions.get(&hash).and_then(|candidates| {
                    candidates.iter().copied().find(|index| {
                        self.events[entries[*index].key_index].bytes == key.as_bytes()
                    })
                })
            };
            let is_new = duplicate.is_none();
            if is_new {
                let mut key_event = event(ffi::EventKind::Key);
                key_event.bytes = key.clone().into_bytes();
                self.events.push(key_event);
            }
            let key_index = is_new.then(|| self.events.len() - 1);
            let value_start = self.events.len();
            let nested = EventSeed {
                depth: self.depth + 1,
                options: self.options,
                events: &mut *self.events,
            };
            map.next_value_seed(nested)?;
            let value_end = self.events.len();

            if self.options.schema && key == "$ref" && value_end == value_start + 1 {
                let value = &self.events[value_start];
                if value.kind == ffi::EventKind::String && !value.bytes.starts_with(b"#") {
                    return Err(serde::de::Error::custom(crate::SCHEMA_REF_ERROR));
                }
            }

            if let Some(index) = duplicate {
                match policy {
                    DuplicatePolicy::Reject => {
                        return Err(serde::de::Error::custom(crate::DUPLICATE_ERROR));
                    }
                    DuplicatePolicy::KeepFirst => {
                        self.events.truncate(value_start);
                    }
                    DuplicatePolicy::Overwrite => {
                        let replacement = self.events.split_off(value_start);
                        let old_start = entries[index].value_start;
                        let old_end = entries[index].value_end;
                        let old_len = old_end - old_start;
                        let new_len = replacement.len();
                        self.events.splice(old_start..old_end, replacement);
                        entries[index].value_end = old_start + new_len;
                        if new_len != old_len {
                            let delta = new_len as i64 - old_len as i64;
                            for entry in entries.iter_mut().skip(index + 1) {
                                entry.key_index = (entry.key_index as i64 + delta) as usize;
                                entry.value_start = (entry.value_start as i64 + delta) as usize;
                                entry.value_end = (entry.value_end as i64 + delta) as usize;
                            }
                        }
                    }
                    DuplicatePolicy::Preserve => unreachable!(),
                }
            } else {
                if policy != DuplicatePolicy::Preserve {
                    entries.push(ObjectEntry {
                        key_index: key_index.expect("new object key"),
                        value_start,
                        value_end,
                    });
                    positions.entry(hash).or_default().push(entries.len() - 1);
                }
            }
        }
        self.events.push(event(ffi::EventKind::EndObject));
        Ok(())
    }
}

fn parse_events(input: &[u8], options: ParseOptions) -> crate::Result<Vec<ffi::Event>> {
    let input = crate::normalized_input(input);
    let input = input.as_ref();
    let mut deserializer = serde_json::Deserializer::from_slice(input);
    deserializer.disable_recursion_limit();
    let mut events = Vec::new();
    let result = EventSeed {
        depth: 0,
        options,
        events: &mut events,
    }
    .deserialize(&mut deserializer)
    .and_then(|()| deserializer.end());
    match result {
        Ok(()) => Ok(events),
        Err(error) => Err(crate::convert_error(input, error)),
    }
}

pub(crate) fn parse(
    input: &[u8],
    duplicate: ffi::Duplicate,
    max_depth: u32,
    schema: bool,
) -> ffi::ParseResult {
    let options = ParseOptions {
        duplicate_policy: policy(duplicate),
        max_depth,
        schema,
    };
    match parse_events(input, options) {
        Ok(events) => result(Ok(()), events),
        Err(error) => result(Err(error), Vec::new()),
    }
}

pub(crate) fn validate(
    input: &[u8],
    duplicate: ffi::Duplicate,
    max_depth: u32,
    schema: bool,
) -> ffi::ParseResult {
    let options = ParseOptions {
        duplicate_policy: policy(duplicate),
        max_depth,
        schema,
    };
    result(crate::validate(input, options), Vec::new())
}

pub(crate) fn parse_binary(
    input: &[u8],
    duplicate: ffi::Duplicate,
    max_depth: u32,
    schema: bool,
) -> ffi::BinaryResult {
    let options = ParseOptions {
        duplicate_policy: policy(duplicate),
        max_depth,
        schema,
    };
    match crate::parse_binary(input, options) {
        Ok(binary) => ffi::BinaryResult {
            status: ffi::Status::Ok,
            byte_offset: 0,
            message: String::new(),
            binary,
        },
        Err(error) => ffi::BinaryResult {
            status: match error.kind {
                crate::ErrorKind::Syntax => ffi::Status::Syntax,
                crate::ErrorKind::DuplicateKey => ffi::Status::DuplicateKey,
                crate::ErrorKind::Depth => ffi::Status::Depth,
                crate::ErrorKind::UnsupportedSchemaRef => ffi::Status::UnsupportedSchemaRef,
            },
            byte_offset: error.byte_offset as u64,
            message: error.message,
            binary: Vec::new(),
        },
    }
}

pub(crate) fn inspect_binary(input: &[u8], with_depth: bool) -> ffi::BinaryMetadataResult {
    match crate::BinaryDocument::new(input).and_then(|document| {
        let root = document.root()?;
        let length = root.len()?;
        let value_type = root.json_type()? as u8;
        let opaque_field_type = if value_type == crate::JsonType::Opaque as u8 {
            root.opaque_field_type()?
        } else {
            0
        };
        let depth = if with_depth { document.depth()? } else { 0 };
        Ok((value_type, depth, opaque_field_type, length))
    }) {
        Ok((value_type, depth, opaque_field_type, length)) => ffi::BinaryMetadataResult {
            status: ffi::Status::Ok,
            byte_offset: 0,
            value_type,
            depth,
            opaque_field_type,
            length,
        },
        Err(error) => ffi::BinaryMetadataResult {
            status: ffi::Status::InvalidBinary,
            byte_offset: error.byte_offset as u64,
            value_type: 0,
            depth: 0,
            opaque_field_type: 0,
            length: 0,
        },
    }
}

fn parse_status(error: &crate::ParseError) -> ffi::Status {
    match error.kind {
        crate::ErrorKind::Syntax => ffi::Status::Syntax,
        crate::ErrorKind::DuplicateKey => ffi::Status::DuplicateKey,
        crate::ErrorKind::Depth => ffi::Status::Depth,
        crate::ErrorKind::UnsupportedSchemaRef => ffi::Status::UnsupportedSchemaRef,
    }
}

pub(crate) fn inspect_text(
    input: &[u8],
    with_depth: bool,
    max_depth: u32,
) -> ffi::BinaryMetadataResult {
    let options = ParseOptions {
        max_depth,
        ..ParseOptions::default()
    };
    match crate::parse_binary(input, options) {
        Ok(binary) => inspect_binary(&binary, with_depth),
        Err(error) => ffi::BinaryMetadataResult {
            status: parse_status(&error),
            byte_offset: error.byte_offset as u64,
            value_type: 0,
            depth: 0,
            opaque_field_type: 0,
            length: 0,
        },
    }
}

pub struct PathParseResult {
    path: Option<crate::Path>,
    error: Option<crate::PathError>,
}

fn empty_container(object: bool) -> Vec<u8> {
    crate::binary::serialize(if object {
        crate::Document::Object(Vec::new())
    } else {
        crate::Document::Array(Vec::new())
    })
}

impl PathParseResult {
    fn status(&self) -> ffi::Status {
        if self.path.is_some() {
            ffi::Status::Ok
        } else {
            ffi::Status::InvalidPath
        }
    }

    fn byte_offset(&self) -> u64 {
        self.error
            .as_ref()
            .map_or(0, |error| error.byte_offset as u64)
    }

    fn path(&self) -> &crate::Path {
        self.path
            .as_ref()
            .expect("valid paths are checked before use")
    }
}

pub(crate) fn parse_mysql_path(input: &[u8]) -> Box<PathParseResult> {
    Box::new(match crate::Path::parse_mysql(input) {
        Ok(path) => PathParseResult {
            path: Some(path),
            error: None,
        },
        Err(error) => PathParseResult {
            path: None,
            error: Some(error),
        },
    })
}

fn query_result(result: crate::BinaryResult<(u64, u64)>) -> ffi::PathQueryResult {
    match result {
        Ok((match_count, single_length)) => ffi::PathQueryResult {
            status: ffi::Status::Ok,
            byte_offset: 0,
            match_count,
            single_length,
        },
        Err(error) => ffi::PathQueryResult {
            status: ffi::Status::InvalidBinary,
            byte_offset: error.byte_offset as u64,
            match_count: 0,
            single_length: 0,
        },
    }
}

pub(crate) fn query_binary(
    path: &crate::Path,
    input: &[u8],
    auto_wrap: bool,
    first_only: bool,
) -> ffi::PathQueryResult {
    let result = crate::BinaryDocument::new(input).and_then(|document| {
        let matches = path.query(
            document.root()?,
            crate::QueryOptions {
                auto_wrap,
                first_only,
            },
        )?;
        let single_length = if matches.len() == 1 {
            matches[0].len()?
        } else {
            0
        };
        Ok((matches.len() as u64, single_length))
    });
    query_result(result)
}

pub(crate) fn query_text(
    path: &crate::Path,
    input: &[u8],
    auto_wrap: bool,
    first_only: bool,
    max_depth: u32,
) -> ffi::PathQueryResult {
    let options = ParseOptions {
        max_depth,
        ..ParseOptions::default()
    };
    match crate::parse_binary(input, options) {
        Ok(binary) => query_binary(path, &binary, auto_wrap, first_only),
        Err(error) => ffi::PathQueryResult {
            status: parse_status(&error),
            byte_offset: error.byte_offset as u64,
            match_count: 0,
            single_length: 0,
        },
    }
}
