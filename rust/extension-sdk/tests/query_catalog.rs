// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{
    sql::RoutineKind,
    sys,
    table::{Arguments, Cell, Cursor, QueryContext, Rows, Service},
    Call, Result,
};
use std::{mem::size_of, ptr};

#[derive(Default)]
struct Host {
    calls: usize,
    id: u64,
    status: sys::Status,
    error: i64,
    malformed: bool,
    rows: usize,
}
unsafe extern "C" fn emit(_: *mut sys::Handle, _: *const sys::Value) -> sys::Status {
    sys::OK
}
unsafe extern "C" fn lookup(
    raw: *mut sys::Handle,
    kind: u32,
    name: *const std::ffi::c_char,
    size: u64,
    result: *mut sys::RoutineLookupResult,
) -> sys::Status {
    let host = unsafe { &mut *raw.cast::<Host>() };
    assert!(kind == 1 || kind == 2);
    assert_eq!(
        unsafe { std::slice::from_raw_parts(name.cast::<u8>(), size as usize) },
        b"name"
    );
    let output = unsafe { &mut *result };
    assert_eq!(
        output.struct_size as usize,
        size_of::<sys::RoutineLookupResult>()
    );
    host.calls += 1;
    output.object_id = host.id;
    output.database_error = host.error;
    if host.malformed {
        output.reserved[0] = 1;
    }
    host.status
}
fn api() -> sys::SqlApiV3 {
    sys::SqlApiV3 {
        v2: sys::SqlApiV2 {
            v1: sys::SqlApi {
                struct_size: size_of::<sys::SqlApiV3>() as u32,
                spi_major: 1,
                spi_minor: 2,
                reserved_word: 0,
                execute: None,
                reserved: [0; 6],
            },
            poll_query: None,
            reserved: [0; 4],
        },
        lookup_routine: Some(lookup),
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
fn snapshot_absence_and_host_errors_are_distinct_without_sql_or_poll() {
    let api = api();
    let mut host = Host::default();
    for id in [0, 42, i64::MAX as u64] {
        host.id = id;
        let context = context(&mut host, &api.v2.v1);
        let mut call =
            unsafe { Call::from_raw(ptr::from_ref(&context).cast(), ptr::null(), 0) }.unwrap();
        assert!(call.supports_catalog_lookup());
        assert!(!call.supports_query_control());
        for kind in [RoutineKind::Function, RoutineKind::Procedure] {
            assert_eq!(
                call.lookup_routine(kind, "name").unwrap(),
                (id != 0).then_some(id)
            );
        }
    }
    for status in [sys::TIMEOUT, sys::FAILED_PRECONDITION, sys::OK] {
        host.status = status;
        host.error = -4999;
        let context = context(&mut host, &api.v2.v1);
        let mut call =
            unsafe { Call::from_raw(ptr::from_ref(&context).cast(), ptr::null(), 0) }.unwrap();
        let error = call
            .lookup_routine(RoutineKind::Function, "name")
            .unwrap_err();
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
#[test]
fn capabilities_inputs_and_outputs_are_checked_before_identity_is_exposed() {
    let mut host = Host::default();
    for fault in 0..9 {
        let mut api = api();
        match fault {
            0 => api.v2.v1.struct_size = 0,
            1 => api.v2.v1.spi_major = 2,
            2 => api.v2.v1.spi_minor = 1,
            3 => api.v2.v1.reserved_word = 1,
            4 => api.v2.v1.reserved[0] = 1,
            5 => api.v2.reserved[0] = 1,
            6 => api.reserved[0] = 1,
            7 => api.lookup_routine = None,
            _ => (),
        }
        let mut context = context(&mut host, &api.v2.v1);
        if fault == 8 {
            context.sql_context = ptr::null_mut();
        }
        let mut call =
            unsafe { Call::from_raw(ptr::from_ref(&context).cast(), ptr::null(), 0) }.unwrap();
        assert!(!call.supports_catalog_lookup());
        assert!(call.lookup_routine(RoutineKind::Function, "name").is_err());
    }
    let extended = api();
    let mut old = extended.v2;
    old.v1.struct_size = size_of::<sys::SqlApiV2>() as u32;
    let old_context = context(&mut host, &old.v1);
    let mut call =
        unsafe { Call::from_raw(ptr::from_ref(&old_context).cast(), ptr::null(), 0) }.unwrap();
    assert!(!call.supports_catalog_lookup());
    assert_eq!(
        call.lookup_routine(RoutineKind::Function, "name")
            .unwrap_err()
            .status,
        sys::UNSUPPORTED_ABI
    );
    let mut legacy = old_context.v1;
    legacy.struct_size = size_of::<sys::ContextV1>() as u32;
    let call = unsafe { Call::from_raw(&legacy, ptr::null(), 0) }.unwrap();
    assert!(!call.supports_catalog_lookup());
    let api = api();
    let scalar_context = context(&mut host, &api.v2.v1);
    let mut call =
        unsafe { Call::from_raw(ptr::from_ref(&scalar_context).cast(), ptr::null(), 0) }.unwrap();
    for invalid in ["".to_string(), "a\0b".to_string(), "x".repeat(2049)] {
        assert_eq!(
            call.lookup_routine(RoutineKind::Function, &invalid)
                .unwrap_err()
                .status,
            sys::INVALID
        );
    }
    assert_eq!(host.calls, 0);
    for malformed in [false, true] {
        let mut host = Host {
            id: if malformed { 42 } else { u64::MAX },
            malformed,
            ..Host::default()
        };
        let context = context(&mut host, &api.v2.v1);
        let mut call =
            unsafe { Call::from_raw(ptr::from_ref(&context).cast(), ptr::null(), 0) }.unwrap();
        assert_eq!(
            call.lookup_routine(RoutineKind::Function, "name")
                .unwrap_err()
                .status,
            sys::INVALID
        );
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
        assert!(query.supports_catalog_lookup());
        let _ = query.lookup_routine(RoutineKind::Function, "name"); // Deliberately ignore failure.
        Ok(Self)
    }
    fn next(&mut self, rows: &mut Rows<'_>) -> Result<()> {
        assert!(rows.supports_catalog_lookup());
        let _ = rows.lookup_routine(RoutineKind::Procedure, "name");
        let _ = rows.emit(&[Cell {
            type_id: c"core.type.bytes",
            bytes: Some(b"x"),
        }]);
        Ok(())
    }
}
unsafe extern "C" fn row(raw: *mut sys::Handle, _: *const sys::TableRow) -> sys::Status {
    unsafe {
        (*raw.cast::<Host>()).rows += 1;
    }
    sys::OK
}
#[test]
fn table_open_and_next_share_sticky_lookup_errors_and_close_remains_available() {
    let api = api();
    let mut host = Host {
        id: 42,
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
        sql_api: &api.v2.v1,
        reserved: [0; 4],
    };
    let service = &Service::<Stream>::WITH_PROJECTION.v1;
    let mut cursor = ptr::null_mut();
    assert_eq!(
        unsafe { service.open.unwrap()(instance, &context.v2.v1, ptr::null(), 0, &mut cursor) },
        sys::OK
    );
    host.status = sys::TIMEOUT;
    host.error = -4012;
    let mut emitted = 99;
    assert_eq!(
        unsafe { service.next.unwrap()(instance, cursor, &context.v2.v1, 1, &mut emitted) },
        sys::TIMEOUT
    );
    assert_eq!(emitted, 0);
    assert_eq!(host.rows, 0);
    assert_eq!(
        unsafe { service.next.unwrap()(instance, cursor, &context.v2.v1, 1, &mut emitted) },
        sys::FAILED_PRECONDITION
    );
    assert_eq!(unsafe { service.close.unwrap()(instance, cursor) }, sys::OK);
    assert_eq!(
        unsafe { service.open.unwrap()(instance, &context.v2.v1, ptr::null(), 0, &mut cursor) },
        sys::TIMEOUT
    );
    assert!(cursor.is_null());
}
