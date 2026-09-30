// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{scalar_function, sys, Call, FunctionDefinition, Result};
use std::ffi::{c_void, CStr};
use std::mem::size_of;
use std::ptr;

const DEFINITION: FunctionDefinition<'static> = FunctionDefinition {
    object_id: c"test.scalar",
    sql_name: c"scalar",
    argument_types: &[c"core.type.bytes"],
    result_type: c"core.type.int64",
    service_id: c"test.scalar.service",
    minimum_version: sys::Version {
        major: 3,
        minor: 2,
        patch: 1,
    },
    maximum_version_exclusive: sys::Version {
        major: 4,
        minor: 0,
        patch: 0,
    },
    required_capabilities: sys::THREAD_SAFE,
    flags: sys::DETERMINISTIC,
};
fn admitted(handle: *mut sys::Handle) -> Result<()> {
    if handle.is_null() {
        Err(sys::FAILED_PRECONDITION)
    } else {
        Ok(())
    }
}
// Names intentionally overlap conventional callback locals and generated
// entry names: macro-generated bindings must not shadow these handler paths.
fn count(_: *mut sys::Handle, call: &mut Call<'_>) -> Result<()> {
    if call.argument_count() != 1 {
        return Err(sys::INVALID);
    }
    call.emit_i64(call.text(0)?.map(|s| s.chars().count() as i64))
}
fn execute(_: *mut sys::Handle, _: &mut Call<'_>) -> Result<()> {
    panic!("handler panic")
}
fn validator_panic(_: *mut sys::Handle) -> Result<()> {
    panic!("admission panic")
}
scalar_function! { native {
    definition: super::DEFINITION, sql: false, validate: admitted, execute: count,
} }
scalar_function! { sql_enabled {
    definition: super::DEFINITION, sql: true, validate: admitted, execute: count,
} }
scalar_function! { panicking {
    definition: super::DEFINITION, sql: false, validate: admitted, execute: execute,
} }
scalar_function! { rejected {
    definition: super::DEFINITION, sql: false, validate: validator_panic, execute: count,
} }

#[derive(Default)]
struct Sink {
    calls: usize,
    value: Option<i64>,
    fail: bool,
}
unsafe extern "C" fn emit(host: *mut c_void, value: *const sys::Value) -> sys::Status {
    let sink = unsafe { &mut *host.cast::<Sink>() };
    let value = unsafe { &*value };
    assert_eq!(unsafe { CStr::from_ptr(value.type_id) }, c"core.type.int64");
    sink.calls += 1;
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
fn value(bytes: Option<&[u8]>) -> sys::Value {
    sys::Value {
        struct_size: size_of::<sys::Value>() as u32,
        type_id: c"core.type.bytes".as_ptr(),
        data: bytes.map_or(ptr::null(), |b| b.as_ptr()),
        data_size: bytes.map_or(0, |b| b.len() as u64),
        is_null: u8::from(bytes.is_none()),
        reserved_bytes: [0; 7],
        reserved: [0; 4],
    }
}

#[test]
fn generated_metadata_preserves_explicit_contracts() {
    let service = native::provide(
        sys::Version {
            major: 3,
            minor: 7,
            patch: 0,
        },
        sys::THREAD_SAFE,
    );
    assert_eq!(native::DEFINITION.minimum_version.minor, 2);
    assert_eq!(native::DEFINITION.flags, sys::DETERMINISTIC);
    assert_eq!(
        unsafe { CStr::from_ptr(service.service_id) },
        DEFINITION.service_id
    );
    assert_eq!(service.version.minor, 7);
    assert_eq!(
        service.service,
        (&native::SERVICE as *const sys::FunctionService).cast()
    );
    assert_eq!(service.reserved, [0; 4]);
    assert_eq!(
        native::SERVICE.struct_size as usize,
        size_of::<sys::FunctionService>()
    );
    assert_eq!(native::SERVICE.spi_minor, 0);
    assert_eq!(sql_enabled::SERVICE.spi_minor, 1);
    assert_eq!(native::SERVICE.reserved, [0; 8]);
}

#[test]
fn generated_callback_handles_utf8_null_empty_and_emit_failure() {
    let mut instance = 0u8;
    for (input, result, expected) in [
        (Some("A中🙂".as_bytes()), sys::OK, Some(3)),
        (Some(b"".as_slice()), sys::OK, Some(0)),
        (None, sys::OK, None),
        (Some(b"\xff".as_slice()), sys::INVALID, None),
    ] {
        let mut sink = Sink::default();
        let status = unsafe {
            native::SERVICE.execute.unwrap()(
                (&mut instance as *mut u8).cast(),
                &context(&mut sink),
                &value(input),
                1,
            )
        };
        assert_eq!(status, result);
        assert_eq!(sink.value, expected);
        assert_eq!(sink.calls, usize::from(result == sys::OK));
    }
    let mut sink = Sink {
        fail: true,
        ..Sink::default()
    };
    let status = unsafe {
        native::SERVICE.execute.unwrap()(
            (&mut instance as *mut u8).cast(),
            &context(&mut sink),
            &value(Some(b"ok")),
            1,
        )
    };
    assert_eq!(status, sys::INTERNAL);
    assert_eq!(sink.calls, 1);
}

#[test]
fn admission_context_and_panic_boundaries_precede_handler() {
    let mut instance = 0u8;
    let handle = (&mut instance as *mut u8).cast();
    let mut sink = Sink::default();
    let context = context(&mut sink);
    let input = value(None);
    unsafe {
        assert_eq!(
            native::SERVICE.execute.unwrap()(ptr::null_mut(), &context, &input, 1),
            sys::FAILED_PRECONDITION
        );
        assert_eq!(
            native::SERVICE.execute.unwrap()(handle, ptr::null(), &input, 1),
            sys::INVALID
        );
        assert_eq!(
            native::SERVICE.execute.unwrap()(handle, &context, ptr::null(), 1),
            sys::INVALID
        );
        assert_eq!(
            sql_enabled::SERVICE.execute.unwrap()(handle, &context, &input, 1),
            sys::UNSUPPORTED_ABI
        );
        assert_eq!(
            panicking::SERVICE.execute.unwrap()(handle, &context, &input, 1),
            sys::INTERNAL
        );
        assert_eq!(
            rejected::SERVICE.execute.unwrap()(handle, &context, &input, 1),
            sys::INTERNAL
        );
    }
    assert_eq!(sink.calls, 0);
}
