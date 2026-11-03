// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use crate::{
    sql::{Error, RoutineKind},
    sys,
};
use std::mem::size_of;

pub(crate) fn context(
    context: Option<&sys::ContextV2>,
) -> Result<(*const sys::SqlApi, *mut sys::Handle), Error> {
    let context = context.ok_or(sys::UNAVAILABLE)?;
    if context.reserved != [0; 4] || context.v1.reserved != [0; 6] {
        return Err(sys::INVALID.into());
    }
    Ok((context.sql_api, context.sql_context))
}

// The original host allocation must cover its advertised size, as for SQL.
pub(crate) unsafe fn callback(
    raw: *const sys::SqlApi,
    handle: *mut sys::Handle,
) -> Result<sys::LookupRoutine, Error> {
    if raw.is_null() || handle.is_null() {
        return Err(sys::UNAVAILABLE.into());
    }
    if unsafe { (*raw).struct_size } < size_of::<sys::SqlApiV3>() as u32 {
        return Err(sys::UNSUPPORTED_ABI.into());
    }
    let api = unsafe { &*raw.cast::<sys::SqlApiV3>() };
    if api.v2.v1.spi_major != 1 || api.v2.v1.spi_minor < 2 {
        return Err(sys::UNSUPPORTED_ABI.into());
    }
    if api.v2.v1.reserved_word != 0
        || api.v2.v1.reserved != [0; 6]
        || api.v2.reserved != [0; 4]
        || api.reserved != [0; 4]
    {
        return Err(sys::INVALID.into());
    }
    api.lookup_routine.ok_or(sys::UNAVAILABLE.into())
}

pub(crate) unsafe fn lookup(
    raw: *const sys::SqlApi,
    handle: *mut sys::Handle,
    kind: RoutineKind,
    name: &str,
) -> Result<Option<u64>, Error> {
    let lookup = unsafe { callback(raw, handle) }?;
    if name.is_empty() || name.len() > 2048 || name.contains('\0') {
        return Err(sys::INVALID.into());
    }
    let mut result = sys::RoutineLookupResult {
        struct_size: size_of::<sys::RoutineLookupResult>() as u32,
        reserved_word: 0,
        database_error: 0,
        object_id: 0,
        reserved: [0; 4],
    };
    let status = unsafe {
        lookup(
            handle,
            match kind {
                RoutineKind::Function => 1,
                RoutineKind::Procedure => 2,
            },
            name.as_ptr().cast(),
            name.len() as u64,
            &mut result,
        )
    };
    if result.struct_size != size_of::<sys::RoutineLookupResult>() as u32
        || result.reserved_word != 0
        || result.reserved != [0; 4]
    {
        return Err(sys::INVALID.into());
    }
    if status != sys::OK || result.database_error != 0 {
        return Err(Error {
            status: if status == sys::OK {
                sys::FAILED_PRECONDITION
            } else {
                status
            },
            database_error: result.database_error,
            consumer_status: None,
        });
    }
    if result.object_id > i64::MAX as u64 {
        return Err(sys::INVALID.into());
    }
    Ok((result.object_id != 0).then_some(result.object_id))
}

pub(crate) unsafe fn mutation_callback(
    raw: *const sys::SqlApi,
    handle: *mut sys::Handle,
) -> Result<sys::MutateRoutine, Error> {
    if raw.is_null() || handle.is_null() {
        return Err(sys::UNAVAILABLE.into());
    }
    if unsafe { (*raw).struct_size } < size_of::<sys::SqlApiV4>() as u32 {
        return Err(sys::UNSUPPORTED_ABI.into());
    }
    let api = unsafe { &*raw.cast::<sys::SqlApiV4>() };
    if api.v3.v2.v1.spi_major != 1 || api.v3.v2.v1.spi_minor < 3 {
        return Err(sys::UNSUPPORTED_ABI.into());
    }
    if api.v3.v2.v1.reserved_word != 0
        || api.v3.v2.v1.reserved != [0; 6]
        || api.v3.v2.reserved != [0; 4]
        || api.v3.reserved != [0; 4]
        || api.reserved != [0; 4]
    {
        return Err(sys::INVALID.into());
    }
    api.mutate_routine.ok_or(sys::UNAVAILABLE.into())
}

pub(crate) unsafe fn mutate(
    raw: *const sys::SqlApi,
    handle: *mut sys::Handle,
    sql: &str,
) -> Result<Option<u64>, Error> {
    let mutate = unsafe { mutation_callback(raw, handle) }?;
    // The host validates SQL/size/NUL before copying or parsing, and records
    // invalid input in its sticky invocation state. Do not short-circuit these
    // errors locally: an ignored result must still fail the outer invocation.
    let mut result = sys::RoutineMutationResult {
        struct_size: size_of::<sys::RoutineMutationResult>() as u32,
        outcome: 0,
        database_error: 0,
        object_id: 0,
        close_error: 0,
        identity_error: 0,
        data_rollback_error: 0,
        view_rollback_error: 0,
        poison_error: 0,
        reserved: [0; 4],
    };
    let status = unsafe { mutate(handle, sql.as_ptr().cast(), sql.len() as u64, &mut result) };
    if result.struct_size != size_of::<sys::RoutineMutationResult>() as u32
        || result.reserved != [0; 4]
        || result.outcome > 3
        || result.object_id > i64::MAX as u64
    {
        return Err(sys::INVALID.into());
    }
    if status != sys::OK || result.database_error != 0 {
        if result.object_id != 0 {
            return Err(sys::INVALID.into());
        }
        return Err(Error {
            status: if status == sys::OK {
                sys::FAILED_PRECONDITION
            } else {
                status
            },
            database_error: result.database_error,
            consumer_status: None,
        });
    }
    if result.outcome != 1
        || result.close_error != 0
        || result.identity_error != 0
        || result.data_rollback_error != 0
        || result.view_rollback_error != 0
        || result.poison_error != 0
    {
        return Err(sys::INVALID.into());
    }
    Ok((result.object_id != 0).then_some(result.object_id))
}
