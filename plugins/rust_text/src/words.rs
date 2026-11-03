// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{
    sys,
    table::{Arguments, Cell, Column, Cursor, Definition, Rows},
    table_planning, Result,
};
use std::mem::size_of;

static COLUMNS: [Column<'static>; 2] = [
    Column {
        name: c"token",
        type_id: super::TEXT_TYPE,
        nullable: false,
    },
    Column {
        name: c"ordinal",
        type_id: c"core.type.int64",
        nullable: false,
    },
];
pub fn definition() -> Definition<'static> {
    Definition {
        object_id: c"org.seekdb.rust-text.words",
        sql_name: c"seekdb_rust_words",
        argument_types: &super::TYPED_ARGUMENTS,
        columns: &COLUMNS,
        flags: sys::DETERMINISTIC | sys::IMMUTABLE,
        implementation: super::implementation(c"org.seekdb.rust-text.words"),
    }
}
pub fn bytes_definition() -> Definition<'static> {
    let mut definition = definition();
    definition.object_id = c"org.seekdb.rust-text.words-bytes";
    definition.sql_name = c"seekdb_rust_words_bytes";
    definition.argument_types = &super::ARGUMENT_TYPES;
    definition.implementation = super::implementation(c"org.seekdb.rust-text.words-bytes");
    definition
}
pub fn nullable_definition() -> Definition<'static> {
    let mut definition = bytes_definition();
    definition.object_id = c"org.seekdb.rust-text.words-or-null";
    definition.sql_name = c"seekdb_rust_words_or_null";
    definition.implementation = super::implementation(c"org.seekdb.rust-text.words-or-null");
    definition
}
pub fn strict_definition() -> Definition<'static> {
    let mut definition = nullable_definition();
    definition.object_id = c"org.seekdb.rust-text.words-strict";
    definition.sql_name = c"seekdb_rust_words_strict";
    definition.flags |= sys::NULL_PROPAGATING;
    definition
}
pub const fn nullable_provide() -> sys::ServiceProvide {
    let mut descriptor = provide();
    descriptor.service_id = c"org.seekdb.rust-text.words-or-null".as_ptr();
    descriptor.service = (&table_planning::Service::<Words<true, true>>::WITH_PROJECTION
        as *const sys::TableFunctionServiceV2)
        .cast();
    descriptor
}
pub const fn bytes_provide() -> sys::ServiceProvide {
    let mut descriptor = provide();
    descriptor.service_id = c"org.seekdb.rust-text.words-bytes".as_ptr();
    descriptor.service = (&table_planning::Service::<Words<true>>::WITH_PROJECTION
        as *const sys::TableFunctionServiceV2)
        .cast();
    descriptor
}
pub const fn provide() -> sys::ServiceProvide {
    sys::ServiceProvide {
        struct_size: size_of::<sys::ServiceProvide>() as u32,
        service_id: c"org.seekdb.rust-text.words".as_ptr(),
        version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        service: (&table_planning::Service::<Words>::WITH_PROJECTION
            as *const sys::TableFunctionServiceV2)
            .cast(),
        capabilities: sys::THREAD_SAFE,
        reserved: [0; 4],
    }
}
struct Words<const BYTES: bool = false, const NULL_TOKEN: bool = false> {
    text: seekdb_extension::memory::OwnedHostBuffer,
    position: usize,
    ordinal: i64,
}
impl<const BYTES: bool, const NULL_TOKEN: bool> table_planning::Planner
    for Words<BYTES, NULL_TOKEN>
{
    fn estimate(
        _: *mut sys::Handle,
        context: &table_planning::Context<'_>,
    ) -> Result<table_planning::Estimate> {
        if context.argument_count() != 1
            || context.column_count() != 2
            || context.argument_type(0)?
                != if BYTES {
                    c"core.type.bytes"
                } else {
                    super::TEXT_TYPE
                }
        {
            return Err(sys::INVALID);
        }
        // Reference prior, not a measurement or evaluation of query arguments.
        Ok(table_planning::Estimate {
            rows: 8.0,
            row_width: 40.0,
            total_cost: 4.0,
        })
    }
}
impl<const BYTES: bool, const NULL_TOKEN: bool> Cursor for Words<BYTES, NULL_TOKEN> {
    fn validate_instance(instance: *mut sys::Handle) -> Result<()> {
        super::validate_instance(instance)
    }
    fn open(_: *mut sys::Handle, arguments: &Arguments<'_>) -> Result<Self> {
        if arguments.len() != 1 {
            return Err(sys::INVALID);
        }
        // Each entry accepts only its declared representation. In particular,
        // the bytes entry cannot silently accept an unconverted custom value.
        let bytes = arguments
            .bytes(
                0,
                if BYTES {
                    c"core.type.bytes"
                } else {
                    super::TEXT_TYPE
                },
            )?
            .unwrap_or(if NULL_TOKEN { b"<NULL>" } else { &[] });
        let text = std::str::from_utf8(bytes).map_err(|_| sys::INVALID)?;
        let owned = super::with_host_allocator(|allocator| {
            allocator.owned_copy_from_slice(text.as_bytes())
        })?;
        Ok(Self {
            text: owned,
            position: 0,
            ordinal: 0,
        })
    }
    fn next(&mut self, rows: &mut Rows<'_>) -> Result<()> {
        if rows
            .projection_column_count()
            .is_some_and(|count| count != 2)
        {
            return Err(sys::INVALID);
        }
        let token_requested = rows.column_requested(0)?;
        let ordinal_requested = rows.column_requested(1)?;
        let controlled = rows.supports_query_control();
        while rows.remaining() != 0 {
            if controlled {
                rows.poll_query().map_err(|error| error.status)?;
            }
            // open validates UTF-8 before copying into exclusive owned bytes.
            // No method mutates text; position advances only at char boundaries.
            let tail = unsafe { std::str::from_utf8_unchecked(&self.text[self.position..]) };
            let (start, length) = if controlled {
                let mut start = None;
                let mut end = tail.len();
                for (visited, (offset, character)) in tail.char_indices().enumerate() {
                    if visited != 0 && visited % 4096 == 0 {
                        rows.poll_query().map_err(|error| error.status)?;
                    }
                    if start.is_none() {
                        if !character.is_whitespace() {
                            start = Some(offset);
                        }
                    } else if character.is_whitespace() {
                        end = offset;
                        break;
                    }
                }
                let Some(start) = start else {
                    break;
                };
                rows.poll_query().map_err(|error| error.status)?;
                (start, end - start)
            } else {
                let Some(start) = tail.find(|c: char| !c.is_whitespace()) else {
                    break;
                };
                let token = &tail[start..];
                (
                    start,
                    token.find(char::is_whitespace).unwrap_or(token.len()),
                )
            };
            let token = &tail[start..];
            // Preserve the stream and ordinal state even when SQL only counts
            // rows. Unrequested cells use valid non-NULL typed placeholders.
            let ordinal = (if ordinal_requested {
                self.ordinal + 1
            } else {
                0
            })
            .to_ne_bytes();
            rows.emit(&[
                Cell {
                    type_id: super::TEXT_TYPE,
                    bytes: Some(if token_requested {
                        &token.as_bytes()[..length]
                    } else {
                        &[]
                    }),
                },
                Cell {
                    type_id: c"core.type.int64",
                    bytes: Some(&ordinal),
                },
            ])?;
            self.position += start + length;
            self.ordinal += 1;
        }
        Ok(())
    }
}
