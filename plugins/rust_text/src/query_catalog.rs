// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{sql::RoutineKind, sys, Call, FunctionDefinition, Result};

fn lookup(_: *mut sys::Handle, call: &mut Call<'_>) -> Result<()> {
    let id = match call.text(0)? {
        None => None,
        Some(name) => call
            .lookup_routine(RoutineKind::Function, name)
            .map_err(|error| error.status)?,
    };
    call.emit_i64(id.map(|id| id as i64))
}
seekdb_extension::scalar_function! {
    pub(crate) function {
        definition: FunctionDefinition {
            object_id: c"org.seekdb.rust-text.routine-id",
            sql_name: c"seekdb_rust_routine_id",
            argument_types: &crate::ARGUMENT_TYPES,
            result_type: c"core.type.int64",
            service_id: c"org.seekdb.rust-text.routine-id",
            minimum_version: sys::Version { major: 1, minor: 0, patch: 0 },
            maximum_version_exclusive: sys::Version { major: 2, minor: 0, patch: 0 },
            required_capabilities: sys::THREAD_SAFE,
            flags: 0,
        },
        sql: true,
        validate: crate::validate_instance,
        execute: lookup,
    }
}

fn mutate(_: *mut sys::Handle, call: &mut Call<'_>) -> Result<()> {
    let id = match call.text(0)? {
        None => None,
        Some(sql) => call.mutate_routine(sql).map_err(|error| error.status)?,
    };
    call.emit_i64(id.map(|id| id as i64))
}
seekdb_extension::scalar_function! {
    pub(crate) mutation {
        definition: FunctionDefinition {
            object_id: c"org.seekdb.rust-text.routine-ddl",
            sql_name: c"seekdb_rust_routine_ddl",
            argument_types: &crate::ARGUMENT_TYPES,
            result_type: c"core.type.int64",
            service_id: c"org.seekdb.rust-text.routine-ddl",
            minimum_version: sys::Version { major: 1, minor: 0, patch: 0 },
            maximum_version_exclusive: sys::Version { major: 2, minor: 0, patch: 0 },
            required_capabilities: sys::THREAD_SAFE,
            flags: 0,
        },
        sql: true,
        validate: crate::validate_instance,
        execute: mutate,
    }
}
