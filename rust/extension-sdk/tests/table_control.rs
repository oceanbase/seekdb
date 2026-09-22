// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{
    sys,
    table::{Arguments, Cell, Cursor, Rows, Service},
    Result,
};
use std::{
    mem::size_of,
    ptr,
    sync::atomic::{AtomicUsize, Ordering},
};

static DROPS: AtomicUsize = AtomicUsize::new(0);
struct Stream;
impl Drop for Stream {
    fn drop(&mut self) {
        DROPS.fetch_add(1, Ordering::SeqCst);
    }
}
impl Cursor for Stream {
    fn validate_instance(_: *mut sys::Handle) -> Result<()> {
        Ok(())
    }
    fn open(_: *mut sys::Handle, _: &Arguments<'_>) -> Result<Self> {
        Ok(Self)
    }
    fn next(&mut self, rows: &mut Rows<'_>) -> Result<()> {
        if rows.supports_query_control() {
            let first = rows.poll_query();
            if let Err(error) = first {
                assert_eq!(rows.poll_query().unwrap_err(), error);
                assert_eq!(
                    rows.emit(&[Cell {
                        type_id: c"core.type.bytes",
                        bytes: Some(b"x")
                    }]),
                    Err(error.status)
                );
                return Ok(()); // SDK must preserve an ignored polling failure.
            }
            assert!(first.unwrap().is_none());
        }
        rows.emit(&[Cell {
            type_id: c"core.type.bytes",
            bytes: Some(b"x"),
        }])
    }
}
#[derive(Default)]
struct Host {
    polls: usize,
    emits: usize,
    fail: bool,
}
unsafe extern "C" fn poll(raw: *mut sys::Handle, output: *mut sys::QueryStatus) -> sys::Status {
    let host = unsafe { &mut *raw.cast::<Host>() };
    host.polls += 1;
    let output = unsafe { &mut *output };
    output.remaining_us = if host.fail { 0 } else { -1 };
    output.database_error = if host.fail { -4999 } else { 0 };
    if host.fail {
        sys::TIMEOUT
    } else {
        sys::OK
    }
}
unsafe extern "C" fn emit(raw: *mut sys::Handle, row: *const sys::TableRow) -> sys::Status {
    let host = unsafe { &mut *raw.cast::<Host>() };
    assert_eq!(unsafe { (*row).column_count }, 1);
    host.emits += 1;
    sys::OK
}
#[test]
fn query_control_preserves_legacy_streams_poisoning_rescan_and_close() {
    let service = &Service::<Stream>::WITH_QUERY_CONTROL;
    assert_eq!(service.v1.spi_minor, 2);
    assert_eq!(
        service.v1.struct_size as usize,
        size_of::<sys::TableFunctionServiceV2>()
    );
    assert!(service.estimate.is_none()); // No planner implementation required.
    assert_eq!(Service::<Stream>::ABI.spi_minor, 0);
    let instance = ptr::dangling_mut();
    let mut host = Host::default();
    let context = sys::TableContextV2 {
        v1: sys::TableContext {
            struct_size: size_of::<sys::TableContextV2>() as u32,
            host: ptr::from_mut(&mut host).cast(),
            emit_row: Some(emit),
            reserved: [0; 6],
        },
        query_context: ptr::from_mut(&mut host).cast(),
        poll_query: Some(poll),
        reserved: [0; 4],
    };
    let raw = ptr::from_ref(&context).cast();
    let mut cursor = ptr::null_mut();
    assert_eq!(
        unsafe { service.v1.open.unwrap()(instance, raw, ptr::null(), 0, &mut cursor) },
        sys::OK
    );
    let mut count = 999;
    assert_eq!(
        unsafe { service.v1.next.unwrap()(instance, cursor, raw, 1, &mut count) },
        sys::OK
    );
    assert_eq!((count, host.polls, host.emits), (1, 1, 1));
    host.fail = true;
    assert_eq!(
        unsafe { service.v1.next.unwrap()(instance, cursor, raw, 1, &mut count) },
        sys::TIMEOUT
    );
    assert_eq!((count, host.polls, host.emits), (0, 2, 1));
    host.fail = false;
    assert_eq!(
        unsafe { service.v1.next.unwrap()(instance, cursor, raw, 1, &mut count) },
        sys::FAILED_PRECONDITION
    );
    assert_eq!((host.polls, host.emits), (2, 1));
    assert_eq!(
        unsafe { service.v1.rescan.unwrap()(instance, cursor, ptr::null(), 0) },
        sys::OK
    );
    assert_eq!(DROPS.load(Ordering::SeqCst), 1);
    let mut legacy = context.v1;
    legacy.struct_size = size_of::<sys::TableContext>() as u32;
    assert_eq!(
        unsafe { service.v1.next.unwrap()(instance, cursor, &legacy, 1, &mut count) },
        sys::OK
    );
    assert_eq!((count, host.polls, host.emits), (1, 2, 2));
    assert_eq!(
        unsafe { service.v1.close.unwrap()(instance, cursor) },
        sys::OK
    );
    assert_eq!(DROPS.load(Ordering::SeqCst), 2);
}
