// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{sys, Call};
use std::ffi::c_void;
use std::mem::size_of;
use std::ptr;

#[derive(Default)]
struct Sink {
    emitted: u32,
    value: Option<i64>,
    fail: bool,
}
unsafe extern "C" fn emit(host: *mut c_void, value: *const sys::Value) -> sys::Status {
    let sink = unsafe { &mut *host.cast::<Sink>() };
    let value = unsafe { &*value };
    sink.emitted += 1;
    sink.value = if value.is_null != 0 {
        None
    } else {
        Some(unsafe { ptr::read_unaligned(value.data.cast::<i64>()) })
    };
    if sink.fail {
        sys::INTERNAL
    } else {
        sys::OK
    }
}
fn context(sink: &mut Sink) -> sys::ContextV1 {
    sys::ContextV1 {
        struct_size: size_of::<sys::ContextV1>() as u32,
        host: (sink as *mut Sink).cast(),
        emit_result: Some(emit),
        reserved: [0; 6],
    }
}
fn value(bytes: &[u8]) -> sys::Value {
    sys::Value {
        struct_size: size_of::<sys::Value>() as u32,
        type_id: c"core.type.bytes".as_ptr(),
        data: bytes.as_ptr(),
        data_size: bytes.len() as u64,
        is_null: 0,
        reserved_bytes: [0; 7],
        reserved: [0; 4],
    }
}

#[test]
fn borrowed_utf8_empty_null_and_invalid_input() {
    let mut sink = Sink::default();
    let context = context(&mut sink);
    let mut null = value(&[]);
    null.type_id = ptr::null();
    null.data = ptr::null();
    null.is_null = 1;
    let mut empty = value(&[]);
    empty.data = ptr::null();
    let values = [value("A中🙂".as_bytes()), empty, null, value(&[0xff])];
    let call = unsafe { Call::from_raw(&context, values.as_ptr(), 4) }.unwrap();
    assert_eq!(call.argument_count(), 4);
    assert_eq!(call.argument_type(0), Ok(Some(c"core.type.bytes")));
    assert_eq!(call.argument_type(2), Ok(None));
    assert_eq!(call.argument_type(4), Err(sys::INVALID));
    assert_eq!(call.text(0), Ok(Some("A中🙂")));
    assert_eq!(call.text(1), Ok(Some("")));
    assert_eq!(call.text(2), Ok(None));
    assert_eq!(call.text(3), Err(sys::INVALID));
    assert_eq!(call.text(4), Err(sys::INVALID));
}

#[test]
fn emit_is_single_attempt_even_if_host_reports_failure() {
    let mut sink = Sink {
        fail: true,
        ..Sink::default()
    };
    let context = context(&mut sink);
    let mut call = unsafe { Call::from_raw(&context, ptr::null(), 0) }.unwrap();
    assert_eq!(call.emit_i64(Some(42)), Err(sys::INTERNAL));
    assert_eq!(call.emit_i64(Some(43)), Err(sys::FAILED_PRECONDITION));
    assert_eq!((sink.emitted, sink.value), (1, Some(42)));
}

#[test]
fn null_result_and_missing_sql_context() {
    let mut sink = Sink::default();
    let context = context(&mut sink);
    let mut call = unsafe { Call::from_raw(&context, ptr::null(), 0) }.unwrap();
    assert_eq!(call.query_i64("SELECT ?", Some("x")), Err(sys::UNAVAILABLE));
    call.emit_i64(None).unwrap();
    assert_eq!((sink.emitted, sink.value), (1, None));
}

#[test]
fn malformed_context_and_values_are_rejected_before_use() {
    let mut sink = Sink::default();
    let mut context = context(&mut sink);
    assert!(matches!(
        unsafe { Call::from_raw(&context, ptr::null(), 1) },
        Err(sys::INVALID)
    ));
    assert!(matches!(
        unsafe { Call::from_raw(&context, ptr::null(), 1025) },
        Err(sys::INVALID)
    ));
    context.emit_result = None;
    assert!(matches!(
        unsafe { Call::from_raw(&context, ptr::null(), 0) },
        Err(sys::INVALID)
    ));
    context.struct_size = 4;
    assert!(matches!(
        unsafe { Call::from_raw(&context, ptr::null(), 0) },
        Err(sys::UNSUPPORTED_ABI)
    ));
}

#[derive(Default)]
struct BytesSink {
    calls: u32,
    type_id: String,
    bytes: Option<Vec<u8>>,
    fail: bool,
}
unsafe extern "C" fn emit_bytes(host: *mut c_void, value: *const sys::Value) -> sys::Status {
    seekdb_extension::boundary(|| {
        let sink = unsafe { &mut *host.cast::<BytesSink>() };
        let value = unsafe { &*value };
        sink.calls += 1;
        sink.type_id = unsafe { std::ffi::CStr::from_ptr(value.type_id) }
            .to_str()
            .map_err(|_| sys::INVALID)?
            .to_owned();
        sink.bytes = if value.is_null != 0 {
            None
        } else if value.data_size == 0 {
            Some(vec![])
        } else {
            Some(
                unsafe { std::slice::from_raw_parts(value.data, value.data_size as usize) }
                    .to_vec(),
            )
        };
        if sink.fail {
            Err(sys::INTERNAL)
        } else {
            Ok(())
        }
    })
}
fn bytes_context(sink: &mut BytesSink) -> sys::ContextV1 {
    sys::ContextV1 {
        struct_size: size_of::<sys::ContextV1>() as u32,
        host: (sink as *mut BytesSink).cast(),
        emit_result: Some(emit_bytes),
        reserved: [0; 6],
    }
}

#[test]
fn custom_typed_bytes_round_trip_without_utf8_or_integer_assumptions() {
    let mut sink = BytesSink::default();
    let context = bytes_context(&mut sink);
    let payload = vec![0xff, 0, 0x80];
    let mut argument = value(&payload);
    argument.type_id = c"test.type".as_ptr();
    {
        let mut call = unsafe { Call::from_raw(&context, &argument, 1) }.unwrap();
        assert_eq!(call.bytes(0, c"test.other"), Err(sys::INVALID));
        assert_eq!(call.text(0), Err(sys::INVALID));
        assert_eq!(call.bytes(1, c"test.type"), Err(sys::INVALID));
        let bytes = call.bytes(0, c"test.type").unwrap();
        call.emit_bytes(c"test.type", bytes).unwrap();
        assert_eq!(call.emit_i64(Some(1)), Err(sys::FAILED_PRECONDITION));
    }
    drop(payload);
    assert_eq!(sink.type_id, "test.type");
    assert_eq!(sink.bytes, Some(vec![0xff, 0, 0x80]));
    assert_eq!(sink.calls, 1);
}

#[test]
fn byte_results_distinguish_null_empty_and_host_failure() {
    for (bytes, fail) in [
        (None, false),
        (Some(&[][..]), false),
        (Some(&[1, 2][..]), true),
    ] {
        let mut sink = BytesSink {
            fail,
            ..BytesSink::default()
        };
        let context = bytes_context(&mut sink);
        let mut call = unsafe { Call::from_raw(&context, ptr::null(), 0) }.unwrap();
        assert_eq!(
            call.emit_bytes(c"test.type", bytes),
            if fail { Err(sys::INTERNAL) } else { Ok(()) }
        );
        assert_eq!(
            call.emit_bytes(c"test.type", None),
            Err(sys::FAILED_PRECONDITION)
        );
        assert_eq!(sink.bytes, bytes.map(|bytes| bytes.to_vec()));
        assert_eq!(sink.calls, 1);
    }
}

#[test]
fn oversized_bytes_are_rejected_before_host_and_null_input_has_no_payload() {
    let payload = vec![0u8; 16 * 1024 * 1024 + 1];
    let mut sink = BytesSink::default();
    let context = bytes_context(&mut sink);
    let mut argument = value(&payload);
    argument.type_id = c"test.type".as_ptr();
    let mut call = unsafe { Call::from_raw(&context, &argument, 1) }.unwrap();
    assert_eq!(call.bytes(0, c"test.type"), Err(sys::INVALID));
    assert_eq!(
        call.emit_bytes(c"test.type", Some(&payload)),
        Err(sys::INVALID)
    );
    assert_eq!(sink.calls, 0);
    call.emit_bytes(c"test.type", None).unwrap();
    argument.is_null = 1;
    argument.type_id = ptr::null();
    argument.data = ptr::null();
    let call = unsafe { Call::from_raw(&context, &argument, 1) }.unwrap();
    assert_eq!(call.bytes(0, c"test.type"), Ok(None));
}
