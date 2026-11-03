// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{
    optimizer::{Context, Hook, Service},
    sys, Result,
};
use std::{mem::size_of, ptr};

struct Probe {
    calls: usize,
    status: sys::Status,
    error: i32,
}
unsafe extern "C" fn next(raw: *mut sys::Handle, error: *mut i32) -> sys::Status {
    let probe = unsafe { &mut *raw.cast::<Probe>() };
    probe.calls += 1;
    unsafe {
        *error = probe.error;
    }
    probe.status
}
struct TestHook;
impl Hook for TestHook {
    fn validate_instance(instance: *mut sys::Handle) -> Result<()> {
        if instance.is_null() {
            Err(sys::UNAVAILABLE)
        } else {
            Ok(())
        }
    }
    fn invoke(instance: *mut sys::Handle, context: &mut Context<'_>) -> Result<()> {
        let mode = unsafe { *instance.cast::<u32>() };
        assert_eq!(context.statement_kind(), 1);
        assert_eq!(context.database_id(), 42);
        assert_eq!(context.user_id(), 123);
        assert_eq!(context.database_error(), 0);
        match mode {
            1 => return Err(sys::UNAVAILABLE), // veto before core
            2 => return Ok(()),                // missing continuation
            3 => panic!("plugin hook panic"),
            _ => {}
        }
        let result = context.call_next();
        if mode == 4 {
            let _ = context.call_next();
        }
        if mode == 5 {
            assert_eq!(context.database_error(), -5432);
            return Ok(()); // must not swallow a downstream error
        }
        if mode == 6 {
            return Err(sys::UNAVAILABLE);
        }
        result
    }
}
fn info() -> sys::OptimizerInfo {
    sys::OptimizerInfo {
        struct_size: size_of::<sys::OptimizerInfo>() as u32,
        statement_kind: 1,
        database_id: 42,
        user_id: 123,
        reserved: [0; 4],
    }
}
fn context(info: &sys::OptimizerInfo, probe: &mut Probe) -> sys::OptimizerContext {
    sys::OptimizerContext {
        struct_size: size_of::<sys::OptimizerContext>() as u32,
        info,
        continuation: (probe as *mut Probe).cast(),
        next: Some(next),
        reserved: [0; 4],
    }
}
fn call(mode: &mut u32, context: &sys::OptimizerContext) -> sys::Status {
    unsafe { Service::<TestHook>::ABI.invoke.unwrap()((mode as *mut u32).cast(), context) }
}

#[test]
fn continuation_veto_protocol_and_panic_boundaries() {
    for (mut mode, expected, calls) in [
        (0, sys::OK, 1),
        (1, sys::UNAVAILABLE, 0),
        (2, sys::FAILED_PRECONDITION, 0),
        (3, sys::INTERNAL, 0),
        (4, sys::FAILED_PRECONDITION, 1),
        (6, sys::UNAVAILABLE, 1),
    ] {
        let info = info();
        let mut probe = Probe {
            calls: 0,
            status: sys::OK,
            error: 0,
        };
        let raw = context(&info, &mut probe);
        assert_eq!(call(&mut mode, &raw), expected);
        assert_eq!(probe.calls, calls);
    }
}

#[test]
fn downstream_failures_are_sticky_and_inconsistent_success_is_rejected() {
    for (status, expected) in [
        (sys::INVALID, sys::INVALID),
        (sys::OK, sys::FAILED_PRECONDITION),
    ] {
        let info = info();
        let mut probe = Probe {
            calls: 0,
            status,
            error: -5432,
        };
        let raw = context(&info, &mut probe);
        assert_eq!(call(&mut 5, &raw), expected);
        assert_eq!(probe.calls, 1);
    }
}

#[test]
fn malformed_metadata_is_rejected_before_callback_or_continuation() {
    let mut probe = Probe {
        calls: 0,
        status: sys::OK,
        error: 0,
    };
    let mut mode = 3; // invocation would panic and return INTERNAL
    for variant in 0..8 {
        let mut info = info();
        match variant {
            4 => info.struct_size -= 1,
            5 => info.statement_kind = 6,
            6 => info.reserved[3] = 1,
            _ => {}
        }
        let mut raw = context(&info, &mut probe);
        match variant {
            0 => raw.struct_size -= 1,
            1 => raw.info = ptr::null(),
            2 => raw.next = None,
            3 => raw.reserved[0] = 1,
            _ => {}
        }
        let status = if variant == 7 {
            unsafe {
                Service::<TestHook>::ABI.invoke.unwrap()(
                    (&mut mode as *mut u32).cast(),
                    ptr::null(),
                )
            }
        } else {
            call(&mut mode, &raw)
        };
        assert_eq!(status, sys::INVALID, "variant {variant}");
    }
    let info = info();
    let raw = context(&info, &mut probe);
    assert_eq!(
        unsafe { Service::<TestHook>::ABI.invoke.unwrap()(ptr::null_mut(), &raw) },
        sys::UNAVAILABLE
    );
    assert_eq!(probe.calls, 0);
}
