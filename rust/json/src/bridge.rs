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

use crate::{DuplicatePolicy, ParseOptions};
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

    extern "Rust" {
        fn parse(input: &[u8], duplicate: Duplicate, max_depth: u32, schema: bool) -> ParseResult;
        fn validate(
            input: &[u8],
            duplicate: Duplicate,
            max_depth: u32,
            schema: bool,
        ) -> ParseResult;
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
