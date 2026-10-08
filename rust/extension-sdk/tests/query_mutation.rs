// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{
    sys,
    table::{Arguments, Cell, Cursor, QueryContext, Rows, Service},
    Call, Result,
};
use std::{mem::size_of, ptr};

#[derive(Default)]
struct Host {
    calls: usize,
    rows: usize,
    id: u64,
    status: i32,
    error: i64,
    outcome: u32,
    malformed: u32,
}
unsafe extern "C" fn emit(_: *mut sys::Handle, _: *const sys::Value) -> i32 {
    sys::OK
}
unsafe extern "C" fn mutate(
    raw: *mut sys::Handle,
    sql: *const std::ffi::c_char,
    size: u64,
    output: *mut sys::RoutineMutationResult,
) -> i32 {
    let host = unsafe { &mut *raw.cast::<Host>() };
    let sql = unsafe { std::slice::from_raw_parts(sql.cast::<u8>(), size as usize) };
    let result = unsafe { &mut *output };
    assert_eq!(
        result.struct_size as usize,
        size_of::<sys::RoutineMutationResult>()
    );
    host.calls += 1;
    // Host-side validation is intentional: invalid input reaches the host's
    // sticky error mechanism rather than disappearing inside the safe wrapper.
    if sql.is_empty() || sql.len() > 4 * 1024 * 1024 || sql.contains(&0) {
        result.database_error = -4002;
        return sys::INVALID;
    }
    result.object_id = host.id;
    result.outcome = host.outcome;
    result.database_error = host.error;
    match host.malformed {
        1 => result.struct_size -= 1,
        2 => result.reserved[0] = 1,
        3 => result.outcome = 4,
        4 => result.object_id = u64::MAX,
        5 => result.close_error = -4001,
        6 => result.identity_error = -4001,
        7 => result.data_rollback_error = -4001,
        8 => result.view_rollback_error = -4001,
        9 => result.poison_error = -4001,
        _ => (),
    }
    host.status
}
fn api() -> sys::SqlApiV4 {
    sys::SqlApiV4 {
        v3: sys::SqlApiV3 {
            v2: sys::SqlApiV2 {
                v1: sys::SqlApi {
                    struct_size: size_of::<sys::SqlApiV4>() as u32,
                    spi_major: 1,
                    spi_minor: 3,
                    reserved_word: 0,
                    execute: None,
                    reserved: [0; 6],
                },
                poll_query: None,
                reserved: [0; 4],
            },
            lookup_routine: None,
            reserved: [0; 4],
        },
        mutate_routine: Some(mutate),
        reserved: [0; 4],
    }
}
fn context(host: &mut Host, api: &sys::SqlApi) -> sys::ContextV2 {
    sys::ContextV2 {
        v1: sys::ContextV1 {
            struct_size: size_of::<sys::ContextV2>() as u32,
            host: ptr::null_mut(),
            emit_result: Some(emit),
            reserved: [0; 6],
        },
        sql_api: api,
        sql_context: ptr::from_mut(host).cast(),
        reserved: [0; 4],
    }
}
#[test]
fn capability_is_append_only_and_independent_of_other_sql_callbacks() {
    let mut host = Host::default();
    for fault in 0..11 {
        let mut api = api();
        match fault {
            0 => api.v3.v2.v1.struct_size = 0,
            1 => api.v3.v2.v1.spi_major = 2,
            2 => api.v3.v2.v1.spi_minor = 2,
            3 => api.v3.v2.v1.reserved_word = 1,
            4 => api.v3.v2.v1.reserved[0] = 1,
            5 => api.v3.v2.reserved[0] = 1,
            6 => api.v3.reserved[0] = 1,
            7 => api.reserved[0] = 1,
            8 => api.mutate_routine = None,
            _ => (),
        }
        let mut context = context(&mut host, &api.v3.v2.v1);
        if fault == 9 {
            context.sql_context = ptr::null_mut();
        }
        if fault == 10 {
            context.reserved[0] = 1;
        }
        let mut call =
            unsafe { Call::from_raw(ptr::from_ref(&context).cast(), ptr::null(), 0) }.unwrap();
        assert!(!call.supports_catalog_mutation());
        assert!(call.mutate_routine("DROP FUNCTION f;").is_err());
    }
    let mut old = api().v3;
    old.v2.v1.struct_size = size_of::<sys::SqlApiV3>() as u32;
    let context = context(&mut host, &old.v2.v1);
    let mut call =
        unsafe { Call::from_raw(ptr::from_ref(&context).cast(), ptr::null(), 0) }.unwrap();
    assert!(!call.supports_catalog_mutation());
    assert_eq!(
        call.mutate_routine("DROP FUNCTION f;").unwrap_err().status,
        sys::UNSUPPORTED_ABI
    );
    assert_eq!(host.calls, 0);
}
#[test]
fn provisional_ids_noop_and_errors_never_claim_commit() {
    let api = api();
    let mut host = Host {
        outcome: 1,
        ..Host::default()
    };
    for id in [0, 42, i64::MAX as u64] {
        host.id = id;
        let context = context(&mut host, &api.v3.v2.v1);
        let mut call =
            unsafe { Call::from_raw(ptr::from_ref(&context).cast(), ptr::null(), 0) }.unwrap();
        assert!(call.supports_catalog_mutation());
        assert!(!call.supports_catalog_lookup());
        assert_eq!(
            call.mutate_routine("DROP FUNCTION IF EXISTS f;").unwrap(),
            (id != 0).then_some(id)
        );
    }
    host.id = 0;
    for outcome in 0..4 {
        for status in [sys::TIMEOUT, sys::OK] {
            host.outcome = outcome;
            host.status = status;
            host.error = -4999;
            let context = context(&mut host, &api.v3.v2.v1);
            let mut call =
                unsafe { Call::from_raw(ptr::from_ref(&context).cast(), ptr::null(), 0) }.unwrap();
            let error = call.mutate_routine("DROP FUNCTION f;").unwrap_err();
            assert_eq!(error.database_error, -4999);
            assert_eq!(
                error.status,
                if status == sys::OK {
                    sys::FAILED_PRECONDITION
                } else {
                    status
                }
            );
        }
    }
    for malformed in 1..10 {
        let mut host = Host {
            id: 42,
            outcome: 1,
            malformed,
            ..Host::default()
        };
        let context = context(&mut host, &api.v3.v2.v1);
        let mut call =
            unsafe { Call::from_raw(ptr::from_ref(&context).cast(), ptr::null(), 0) }.unwrap();
        assert_eq!(
            call.mutate_routine("DROP FUNCTION f;").unwrap_err().status,
            sys::INVALID
        );
    }
    for statement in [
        "".to_owned(),
        "a\0b".to_owned(),
        "x".repeat(4 * 1024 * 1024 + 1),
    ] {
        let mut host = Host::default();
        let context = context(&mut host, &api.v3.v2.v1);
        let mut call =
            unsafe { Call::from_raw(ptr::from_ref(&context).cast(), ptr::null(), 0) }.unwrap();
        assert_eq!(
            call.mutate_routine(&statement).unwrap_err().database_error,
            -4002
        );
        assert_eq!(host.calls, 1);
    }
}

struct Stream;
impl Cursor for Stream {
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn open(_: *mut sys::Handle, _: &Arguments<'_>) -> Result<Self> {
        Ok(Self)
    }
    fn open_with_context(
        _: *mut sys::Handle,
        _: &Arguments<'_>,
        query: &mut QueryContext<'_>,
    ) -> Result<Self> {
        assert!(query.supports_catalog_mutation());
        let _ = query.mutate_routine("DROP FUNCTION IF EXISTS f;");
        Ok(Self)
    }
    fn next(&mut self, rows: &mut Rows<'_>) -> Result<()> {
        assert!(rows.supports_catalog_mutation());
        let _ = rows.mutate_routine("DROP FUNCTION IF EXISTS f;");
        let _ = rows.emit(&[Cell {
            type_id: c"core.type.bytes",
            bytes: Some(b"x"),
        }]);
        Ok(())
    }
}
unsafe extern "C" fn row(raw: *mut sys::Handle, _: *const sys::TableRow) -> i32 {
    unsafe {
        (*raw.cast::<Host>()).rows += 1;
    }
    sys::OK
}
#[test]
fn ignored_table_mutation_error_prevents_rows_and_still_allows_close() {
    let api = api();
    let mut host = Host {
        outcome: 1,
        ..Host::default()
    };
    let instance = ptr::from_mut(&mut host).cast();
    let context = sys::TableContextV3 {
        v2: sys::TableContextV2 {
            v1: sys::TableContext {
                struct_size: size_of::<sys::TableContextV3>() as u32,
                host: instance,
                emit_row: Some(row),
                reserved: [0; 6],
            },
            query_context: instance,
            poll_query: None,
            reserved: [0; 4],
        },
        sql_api: &api.v3.v2.v1,
        reserved: [0; 4],
    };
    let service = &Service::<Stream>::WITH_PROJECTION.v1;
    let mut cursor = ptr::null_mut();
    assert_eq!(
        unsafe { service.open.unwrap()(instance, &context.v2.v1, ptr::null(), 0, &mut cursor) },
        sys::OK
    );
    host.outcome = 2;
    host.status = sys::TIMEOUT;
    host.error = -4012;
    let mut emitted = 99;
    assert_eq!(
        unsafe { service.next.unwrap()(instance, cursor, &context.v2.v1, 1, &mut emitted) },
        sys::TIMEOUT
    );
    assert_eq!(emitted, 0);
    assert_eq!(host.rows, 0);
    assert_eq!(unsafe { service.close.unwrap()(instance, cursor) }, sys::OK);
    assert_eq!(
        unsafe { service.open.unwrap()(instance, &context.v2.v1, ptr::null(), 0, &mut cursor) },
        sys::TIMEOUT
    );
    assert!(cursor.is_null());
}
