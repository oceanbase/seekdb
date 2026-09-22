// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{sys, Call};
use std::{mem::size_of, ptr, time::Duration};

#[derive(Default)]
struct Host {
    calls: usize,
    remaining: i64,
    error: i64,
    status: sys::Status,
    malformed: bool,
}
unsafe extern "C" fn emit(_: *mut sys::Handle, _: *const sys::Value) -> sys::Status {
    sys::OK
}
unsafe extern "C" fn poll(host: *mut sys::Handle, output: *mut sys::QueryStatus) -> sys::Status {
    let host = unsafe { &mut *host.cast::<Host>() };
    let output = unsafe { &mut *output };
    host.calls += 1;
    assert_eq!(output.struct_size as usize, size_of::<sys::QueryStatus>());
    output.remaining_us = host.remaining;
    output.database_error = host.error;
    if host.malformed {
        output.reserved[0] = 1;
    }
    host.status
}
fn api() -> sys::SqlApiV2 {
    sys::SqlApiV2 {
        v1: sys::SqlApi {
            struct_size: size_of::<sys::SqlApiV2>() as u32,
            spi_major: 1,
            spi_minor: 1,
            reserved_word: 0,
            execute: None,
            reserved: [0; 6],
        },
        poll_query: Some(poll),
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
fn deadline_snapshots_and_database_errors_survive_polling_without_sql() {
    let api = api();
    let mut host = Host::default();
    for remaining in [-1, 0, 1250, i64::MAX] {
        host.remaining = remaining;
        let context = context(&mut host, &api.v1);
        let mut call =
            unsafe { Call::from_raw(ptr::from_ref(&context).cast(), ptr::null(), 0) }.unwrap();
        assert!(call.supports_query_control());
        assert_eq!(
            call.poll_query().unwrap(),
            if remaining == -1 {
                None
            } else {
                Some(Duration::from_micros(remaining as u64))
            }
        );
    }
    assert_eq!(host.calls, 4);
    for status in [sys::TIMEOUT, sys::FAILED_PRECONDITION, sys::OK] {
        host.status = status;
        host.error = -4999;
        let context = context(&mut host, &api.v1);
        let mut call =
            unsafe { Call::from_raw(ptr::from_ref(&context).cast(), ptr::null(), 0) }.unwrap();
        let error = call.poll_query().unwrap_err();
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
fn old_contexts_and_old_api_allocations_never_read_the_suffix() {
    let api = api();
    let mut host = Host::default();
    let mut old = api.v1;
    old.struct_size = size_of::<sys::SqlApi>() as u32;
    let context = context(&mut host, &old);
    let mut call =
        unsafe { Call::from_raw(ptr::from_ref(&context).cast(), ptr::null(), 0) }.unwrap();
    assert!(!call.supports_query_control());
    assert_eq!(call.poll_query().unwrap_err().status, sys::UNSUPPORTED_ABI);
    let mut base = context.v1;
    base.struct_size = size_of::<sys::ContextV1>() as u32;
    let mut call = unsafe { Call::from_raw(&base, ptr::null(), 0) }.unwrap();
    assert!(!call.supports_query_control());
    assert_eq!(call.poll_query().unwrap_err().status, sys::UNAVAILABLE);
    assert_eq!(host.calls, 0);
}
#[test]
fn malformed_capabilities_are_rejected_before_the_host_callback() {
    let mut host = Host::default();
    for fault in 0..9 {
        let mut api = api();
        match fault {
            0 => api.v1.struct_size = 0,
            1 => api.v1.spi_major = 2,
            2 => api.v1.spi_minor = 0,
            3 => api.v1.reserved_word = 1,
            4 => api.v1.reserved[0] = 1,
            5 => api.reserved[0] = 1,
            6 => api.poll_query = None,
            _ => (),
        }
        let mut context = context(&mut host, &api.v1);
        if fault == 7 {
            context.sql_context = ptr::null_mut();
        }
        if fault == 8 {
            context.reserved[0] = 1;
        }
        let mut call =
            unsafe { Call::from_raw(ptr::from_ref(&context).cast(), ptr::null(), 0) }.unwrap();
        assert!(!call.supports_query_control());
        assert!(call.poll_query().is_err());
    }
    assert_eq!(host.calls, 0);
}
#[test]
fn invalid_deadline_and_output_layout_cannot_be_used_as_a_budget() {
    let api = api();
    for malformed in [false, true] {
        let mut host = Host {
            remaining: if malformed { 10 } else { -2 },
            malformed,
            ..Default::default()
        };
        let context = context(&mut host, &api.v1);
        let mut call =
            unsafe { Call::from_raw(ptr::from_ref(&context).cast(), ptr::null(), 0) }.unwrap();
        assert_eq!(call.poll_query().unwrap_err().status, sys::INVALID);
    }
}
