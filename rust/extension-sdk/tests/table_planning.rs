// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{
    sys,
    table::{Arguments, Cursor, Rows},
    table_planning::{Context, Estimate, Planner, Service},
    Result,
};
use std::{mem::size_of, ptr};
struct TestCursor;
impl Cursor for TestCursor {
    fn validate_instance(instance: *mut sys::Handle) -> Result<()> {
        if instance.is_null() {
            Err(sys::UNAVAILABLE)
        } else {
            Ok(())
        }
    }
    fn open(_: *mut sys::Handle, _: &Arguments<'_>) -> Result<Self> {
        panic!("planning opened cursor")
    }
    fn next(&mut self, _: &mut Rows<'_>) -> Result<()> {
        panic!("planning executed cursor")
    }
}
impl Planner for TestCursor {
    fn estimate(instance: *mut sys::Handle, context: &Context<'_>) -> Result<Estimate> {
        assert_eq!(context.object_id(), c"test.words");
        assert_eq!(context.column_count(), 2);
        assert_eq!(context.argument_count(), 1);
        assert_eq!(context.argument_type(0)?, c"core.type.bytes");
        assert_eq!(context.argument_type(1), Err(sys::INVALID));
        let mode = unsafe { *instance.cast::<u32>() };
        if mode == 1 {
            return Err(sys::UNAVAILABLE);
        }
        if mode == 2 {
            panic!("planning panic");
        }
        let mut result = Estimate {
            rows: 8.0,
            row_width: 40.0,
            total_cost: 4.0,
        };
        match mode {
            3 => result.rows = f64::NAN,
            4 => result.row_width = f64::INFINITY,
            5 => result.total_cost = -1.0,
            6 => {
                result.rows = 0.0;
                result.row_width = 0.0;
                result.total_cost = 0.0;
            }
            _ => {}
        }
        Ok(result)
    }
}
fn info(types: &[*const std::ffi::c_char]) -> sys::TablePlanningInfo {
    sys::TablePlanningInfo {
        struct_size: size_of::<sys::TablePlanningInfo>() as u32,
        argument_count: types.len() as u32,
        object_id: c"test.words".as_ptr(),
        argument_type_ids: types.as_ptr(),
        column_count: 2,
        reserved_word: 0,
        reserved: [0; 4],
    }
}
fn output() -> sys::TableEstimate {
    sys::TableEstimate {
        struct_size: size_of::<sys::TableEstimate>() as u32,
        reserved_word: 123,
        rows: 999.0,
        row_width: 999.0,
        total_cost: 999.0,
        reserved: [123; 4],
    }
}
fn call(
    mode: &mut u32,
    info: &sys::TablePlanningInfo,
    out: &mut sys::TableEstimate,
) -> sys::Status {
    unsafe { Service::<TestCursor>::ABI.estimate.unwrap()((mode as *mut u32).cast(), info, out) }
}
#[test]
fn estimates_and_errors_use_planning_boundary_without_opening_cursor() {
    let types = [c"core.type.bytes".as_ptr()];
    let info = info(&types);
    for (mut mode, expected) in [
        (0, sys::OK),
        (1, sys::UNAVAILABLE),
        (2, sys::INTERNAL),
        (3, sys::INVALID),
        (4, sys::INVALID),
        (5, sys::INVALID),
        (6, sys::OK),
    ] {
        let mut out = output();
        assert_eq!(call(&mut mode, &info, &mut out), expected);
        assert_eq!((out.reserved_word, out.reserved), (0, [0; 4]));
        assert_eq!(out.struct_size, size_of::<sys::TableEstimate>() as u32);
        assert_eq!(
            (out.rows, out.row_width, out.total_cost),
            if mode == 0 {
                (8.0, 40.0, 4.0)
            } else {
                (0.0, 0.0, 0.0)
            }
        );
    }
    assert_eq!(
        Service::<TestCursor>::ABI.v1.struct_size,
        size_of::<sys::TableFunctionServiceV2>() as u32
    );
    assert_eq!(Service::<TestCursor>::ABI.v1.spi_minor, 1);
    assert!(Service::<TestCursor>::ABI.v1.open.is_some());
    assert!(Service::<TestCursor>::ABI.v1.close.is_some());
}
#[test]
fn malformed_info_is_rejected_before_planner() {
    for variant in 0..10 {
        let mut types = [c"core.type.bytes".as_ptr()];
        if variant == 8 {
            types[0] = ptr::null();
        }
        if variant == 9 {
            types[0] = c"INVALID TYPE".as_ptr();
        }
        let mut info = info(&types);
        match variant {
            0 => info.struct_size -= 1,
            1 => info.argument_count = 1025,
            2 => info.column_count = 0,
            3 => info.column_count = 4097,
            4 => info.object_id = ptr::null(),
            5 => info.argument_type_ids = ptr::null(),
            6 => info.reserved_word = 1,
            7 => info.reserved[3] = 1,
            _ => {}
        }
        let mut out = output();
        assert_eq!(
            call(&mut 2, &info, &mut out),
            sys::INVALID,
            "variant {variant}"
        );
        assert_eq!(out.rows, 0.0);
    }
    let types = [c"core.type.bytes".as_ptr()];
    let info = info(&types);
    let mut out = output();
    // Null instance admission runs before input use and keeps cleared output.
    assert_eq!(
        unsafe { Service::<TestCursor>::ABI.estimate.unwrap()(ptr::null_mut(), &info, &mut out) },
        sys::UNAVAILABLE
    );
    let mut mode = 2u32;
    let instance = (&mut mode as *mut u32).cast();
    assert_eq!(
        unsafe { Service::<TestCursor>::ABI.estimate.unwrap()(instance, ptr::null(), &mut out) },
        sys::INVALID
    );
    assert_eq!(
        unsafe { Service::<TestCursor>::ABI.estimate.unwrap()(instance, &info, ptr::null_mut()) },
        sys::INVALID
    );
    out.struct_size = 0;
    assert_eq!(
        unsafe { Service::<TestCursor>::ABI.estimate.unwrap()(instance, &info, &mut out) },
        sys::INVALID
    );
}
