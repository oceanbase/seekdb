// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{
    sql, sys,
    table::{Arguments, Cell, Cursor, QueryContext, Rows, Service},
    Result,
};
use std::{
    mem::size_of,
    ptr,
    sync::{
        atomic::{AtomicUsize, Ordering},
        Arc,
    },
};

#[derive(Default)]
struct Host {
    opens: usize,
    calls: usize,
    emits: usize,
    fail: bool,
    drops: Arc<AtomicUsize>,
}
struct Stream {
    drops: Arc<AtomicUsize>,
}
impl Drop for Stream {
    fn drop(&mut self) {
        self.drops.fetch_add(1, Ordering::SeqCst);
    }
}
impl Cursor for Stream {
    fn validate_instance(instance: *mut sys::Handle) -> Result<()> {
        if instance.is_null() {
            Err(sys::INVALID)
        } else {
            Ok(())
        }
    }
    fn open(_: *mut sys::Handle, _: &Arguments<'_>) -> Result<Self> {
        Err(sys::UNAVAILABLE)
    }
    fn open_with_context(
        instance: *mut sys::Handle,
        _: &Arguments<'_>,
        query: &mut QueryContext<'_>,
    ) -> Result<Self> {
        let host = unsafe { &mut *instance.cast::<Host>() };
        host.opens += 1;
        let stream = Self {
            drops: host.drops.clone(),
        };
        assert!(query.supports_sql());
        let result = query.execute_sql("SELECT ?", &[sql::Value::I64(7)], 1, |row| {
            assert_eq!(row.len(), 1);
            assert!(matches!(row.get(0)?, sql::Value::I64(7)));
            Ok(())
        });
        if let Err(error) = result {
            assert_eq!(error.database_error, -4999);
            assert_eq!(query.poll_query().unwrap_err(), error);
            assert_eq!(
                query
                    .execute_sql("SELECT 1", &[], 1, |_| Ok(()))
                    .unwrap_err(),
                error
            );
        }
        Ok(stream) // Even success must not publish a cursor after ignored SQL failure.
    }
    fn next(&mut self, rows: &mut Rows<'_>) -> Result<()> {
        assert!(rows.supports_sql());
        let result = rows.execute_sql("SELECT ?", &[sql::Value::I64(7)], 1, |_| Ok(()));
        if let Err(error) = result {
            assert_eq!(error.database_error, -4999);
            assert_eq!(rows.poll_query().unwrap_err(), error);
            assert_eq!(
                rows.execute_sql("SELECT 1", &[], 1, |_| Ok(()))
                    .unwrap_err(),
                error
            );
            assert_eq!(
                rows.emit(&[Cell {
                    type_id: c"core.type.bytes",
                    bytes: Some(b"x")
                }]),
                Err(error.status)
            );
            Ok(()) // SDK must keep this failure too.
        } else {
            rows.emit(&[Cell {
                type_id: c"core.type.bytes",
                bytes: Some(b"x"),
            }])
        }
    }
}
unsafe extern "C" fn execute(
    context: *mut sys::Handle,
    statement: *const std::ffi::c_char,
    size: u64,
    values: *const sys::SqlValue,
    count: u32,
    max: u64,
    consume: Option<
        unsafe extern "C" fn(*mut std::ffi::c_void, *const sys::SqlValue, u32) -> sys::Status,
    >,
    consumer: *mut std::ffi::c_void,
    output: *mut sys::SqlResult,
) -> sys::Status {
    let host = unsafe { &mut *context.cast::<Host>() };
    host.calls += 1;
    assert_eq!(
        unsafe { std::slice::from_raw_parts(statement.cast::<u8>(), size as usize) },
        b"SELECT ?"
    );
    assert_eq!((count, max), (1, 1));
    if host.fail {
        unsafe { (*output).database_error = -4999 };
        return sys::TIMEOUT;
    }
    let status = unsafe { consume.unwrap()(consumer, values, count) };
    unsafe { (*output).returned_rows = u64::from(status == sys::OK) };
    status
}
unsafe extern "C" fn emit(context: *mut sys::Handle, _: *const sys::TableRow) -> sys::Status {
    unsafe { (*context.cast::<Host>()).emits += 1 };
    sys::OK
}

#[test]
fn open_and_next_sql_failures_cannot_be_ignored_or_keep_live_cursors() {
    let service = &Service::<Stream>::WITH_SQL;
    assert_eq!(service.v1.spi_minor, 3);
    assert!(service.estimate.is_none());
    let mut host = Host::default();
    let instance = ptr::from_mut(&mut host).cast();
    let api = sys::SqlApi {
        struct_size: size_of::<sys::SqlApi>() as u32,
        spi_major: 1,
        spi_minor: 0,
        reserved_word: 0,
        execute: Some(execute),
        reserved: [0; 6],
    };
    let mut context = sys::TableContextV3 {
        v2: sys::TableContextV2 {
            v1: sys::TableContext {
                struct_size: size_of::<sys::TableContextV3>() as u32,
                host: instance,
                emit_row: Some(emit),
                reserved: [0; 6],
            },
            query_context: instance,
            poll_query: None,
            reserved: [0; 4],
        },
        sql_api: &api,
        reserved: [0; 4],
    };
    let mut cursor = ptr::null_mut();
    let open = |context: &sys::TableContext, out: &mut *mut sys::Handle| unsafe {
        service.v1.open.unwrap()(instance, context, ptr::null(), 0, out)
    };
    // Exact old allocation: SQL-dependent open must never read its suffix.
    let legacy = sys::TableContext {
        struct_size: size_of::<sys::TableContext>() as u32,
        host: instance,
        emit_row: Some(emit),
        reserved: [0; 6],
    };
    assert_eq!(open(&legacy, &mut cursor), sys::UNAVAILABLE);
    assert!(cursor.is_null());
    assert_eq!((host.opens, host.calls), (0, 0));
    context.reserved[0] = 1;
    assert_eq!(open(&context.v2.v1, &mut cursor), sys::INVALID);
    context.reserved[0] = 0;
    context.sql_api = ptr::null();
    assert_eq!(open(&context.v2.v1, &mut cursor), sys::UNAVAILABLE);
    context.sql_api = &api;
    assert_eq!((host.opens, host.calls), (0, 0));
    host.fail = true;
    assert_eq!(open(&context.v2.v1, &mut cursor), sys::TIMEOUT);
    assert!(cursor.is_null());
    assert_eq!(
        (host.opens, host.calls, host.drops.load(Ordering::SeqCst)),
        (1, 1, 1)
    );
    host.fail = false;
    assert_eq!(open(&context.v2.v1, &mut cursor), sys::OK);
    let mut count = 999;
    assert_eq!(
        unsafe { service.v1.next.unwrap()(instance, cursor, &context.v2.v1, 1, &mut count) },
        sys::OK
    );
    assert_eq!((count, host.calls, host.emits), (1, 3, 1));
    host.fail = true;
    assert_eq!(
        unsafe { service.v1.next.unwrap()(instance, cursor, &context.v2.v1, 1, &mut count) },
        sys::TIMEOUT
    );
    assert_eq!((count, host.calls, host.emits), (0, 4, 1));
    host.fail = false;
    assert_eq!(
        unsafe { service.v1.next.unwrap()(instance, cursor, &context.v2.v1, 1, &mut count) },
        sys::FAILED_PRECONDITION
    );
    assert_eq!(host.calls, 4);
    assert_eq!(
        unsafe { service.v1.close.unwrap()(instance, cursor) },
        sys::OK
    );
    assert_eq!(host.drops.load(Ordering::SeqCst), 2);
    assert_eq!(open(&context.v2.v1, &mut cursor), sys::OK);
    assert_eq!(
        unsafe { service.v1.next.unwrap()(instance, cursor, &legacy, 1, &mut count) },
        sys::UNAVAILABLE
    );
    assert_eq!(
        unsafe { service.v1.next.unwrap()(instance, cursor, &context.v2.v1, 1, &mut count) },
        sys::FAILED_PRECONDITION
    );
    assert_eq!(
        unsafe { service.v1.rescan.unwrap()(instance, cursor, ptr::null(), 0) },
        sys::UNAVAILABLE
    );
    assert_eq!(
        unsafe { service.v1.close.unwrap()(instance, cursor) },
        sys::OK
    );
    assert_eq!(host.drops.load(Ordering::SeqCst), 3);
}
