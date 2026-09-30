// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{
    batch::{Batch, Handler, Service},
    sys, Call, Result,
};
use std::{
    cell::{Cell, RefCell},
    ffi::CStr,
    mem::size_of,
    ptr,
};

#[derive(Default)]
struct State {
    mode: Cell<u32>,
    calls: Cell<u32>,
    scalars: Cell<u32>,
    polls: Cell<u32>,
    fail: Cell<sys::Status>,
    cancel: Cell<bool>,
    results: RefCell<Vec<(u32, Option<i64>)>>,
}
struct Counting;
impl Handler for Counting {
    fn validate(instance: *mut sys::Handle) -> Result<()> {
        if instance.is_null() {
            Err(sys::FAILED_PRECONDITION)
        } else {
            Ok(())
        }
    }
    fn execute(instance: *mut sys::Handle, batch: &mut Batch<'_>) -> Result<()> {
        let state = unsafe { &*instance.cast::<State>() };
        state.calls.set(state.calls.get() + 1);
        if state.mode.get() == 10 {
            let _ = batch.poll_query();
        }
        match state.mode.get() {
            2 => return Ok(()), // Missing all outputs is not success.
            3 => {
                batch.emit_i64(0, Some(1))?;
                let _ = batch.emit_i64(0, Some(2)); // Ignored duplicates remain errors.
                return Ok(());
            }
            4 => {
                let _ = batch.emit_i64(batch.row_count(), Some(1));
                return Ok(());
            }
            5 => {
                let _ = batch.emit_i64(0, Some(1));
                return Ok(());
            }
            6 => panic!("batch handler panic"),
            7 => return Err(sys::TIMEOUT),
            8 => return Err(sys::END_OF_STREAM),
            _ => (),
        }
        // Gather every input before emitting. A model can process this vector
        // once; this fixture deliberately emits in reverse index order.
        let mut results = Vec::new();
        for index in 0..batch.row_count() {
            let row = batch.row(index)?;
            assert_eq!(row.argument_count(), 1);
            assert_eq!(row.argument_type(0)?, Some(c"core.type.bytes"));
            results.push(row.text(0)?.map(|text| text.chars().count() as i64));
        }
        for index in (0..batch.row_count()).rev() {
            if state.mode.get() == 1 {
                batch.with_row(index, |call| {
                    state.scalars.set(state.scalars.get() + 1);
                    if call.supports_query_control() {
                        call.poll_query().map_err(|e| e.status)?;
                    }
                    call.emit_i64(call.text(0)?.map(|text| text.chars().count() as i64))
                })?;
            } else {
                batch.emit_i64(index, results[index])?;
            }
        }
        if state.mode.get() == 9 {
            state.cancel.set(true);
        }
        Ok(())
    }
}
unsafe extern "C" fn scalar(
    instance: *mut sys::Handle,
    context: *const sys::ContextV1,
    arguments: *const sys::Value,
    count: u32,
) -> sys::Status {
    seekdb_extension::boundary(|| {
        Counting::validate(instance)?;
        let state = unsafe { &*instance.cast::<State>() };
        state.scalars.set(state.scalars.get() + 1);
        let mut call = unsafe { Call::from_raw(context, arguments, count) }?;
        call.emit_i64(call.text(0)?.map(|s| s.chars().count() as i64))
    })
}
const ABI: sys::FunctionServiceV3 = Service::<Counting>::with_scalar(scalar, None);
unsafe extern "C" fn emit(
    host: *mut sys::Handle,
    row: u32,
    value: *const sys::Value,
) -> sys::Status {
    let state = unsafe { &*host.cast::<State>() };
    let value = unsafe { &*value };
    assert_eq!(unsafe { CStr::from_ptr(value.type_id) }, c"core.type.int64");
    let number = if value.is_null != 0 {
        None
    } else {
        assert_eq!(value.data_size, 8);
        Some(i64::from_ne_bytes(
            unsafe { std::slice::from_raw_parts(value.data, 8) }
                .try_into()
                .unwrap(),
        ))
    };
    state.results.borrow_mut().push((row, number));
    state.fail.get()
}
unsafe extern "C" fn scalar_emit(host: *mut sys::Handle, value: *const sys::Value) -> sys::Status {
    unsafe { emit(host, 99, value) }
}
unsafe extern "C" fn poll(host: *mut sys::Handle, out: *mut sys::QueryStatus) -> sys::Status {
    let state = unsafe { &*host.cast::<State>() };
    state.polls.set(state.polls.get() + 1);
    unsafe {
        (*out).remaining_us = 123;
    }
    if state.cancel.get() || (state.mode.get() == 10 && state.polls.get() == 2) {
        sys::TIMEOUT
    } else {
        sys::OK
    }
}
struct Fixture {
    state: Box<State>,
    values: Vec<sys::Value>,
    rows: Vec<sys::BatchRow>,
    _api: Box<sys::SqlApiV2>,
    query: Box<sys::ContextV2>,
    context: sys::BatchContext,
}
impl Fixture {
    fn new(extended: bool) -> Self {
        let state = Box::<State>::default();
        let handle = ptr::from_ref(&*state).cast_mut().cast();
        let values: Vec<_> = [Some("a"), Some("中🙂"), None, Some("")]
            .into_iter()
            .map(|text| sys::Value {
                struct_size: size_of::<sys::Value>() as u32,
                type_id: c"core.type.bytes".as_ptr(),
                data: text.map_or(ptr::null(), |s| s.as_ptr()),
                data_size: text.map_or(0, |s| s.len() as u64),
                is_null: u8::from(text.is_none()),
                reserved_bytes: [0; 7],
                reserved: [0; 4],
            })
            .collect();
        let rows = values
            .iter()
            .map(|value| sys::BatchRow {
                struct_size: size_of::<sys::BatchRow>() as u32,
                argument_count: 1,
                arguments: value,
                reserved: [0; 4],
            })
            .collect();
        let api = Box::new(sys::SqlApiV2 {
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
        });
        let query = Box::new(sys::ContextV2 {
            v1: sys::ContextV1 {
                struct_size: if extended {
                    size_of::<sys::ContextV2>()
                } else {
                    size_of::<sys::ContextV1>()
                } as u32,
                host: handle,
                emit_result: Some(scalar_emit),
                reserved: [0; 6],
            },
            sql_api: &api.v1,
            sql_context: handle,
            reserved: [0; 4],
        });
        let context = sys::BatchContext {
            struct_size: size_of::<sys::BatchContext>() as u32,
            reserved_word: 0,
            query_context: &query.v1,
            host: handle,
            emit_result: Some(emit),
            reserved: [0; 4],
        };
        Self {
            state,
            values,
            rows,
            _api: api,
            query,
            context,
        }
    }
    fn execute(&self) -> sys::Status {
        unsafe {
            ABI.execute_batch.unwrap()(
                ptr::from_ref(&*self.state).cast_mut().cast(),
                &self.context,
                self.rows.as_ptr(),
                self.rows.len() as u32,
            )
        }
    }
}
#[test]
fn one_batch_callback_gathers_rows_and_emits_indexed_results() {
    for extended in [false, true] {
        for mode in [0, 1] {
            let f = Fixture::new(extended);
            f.state.mode.set(mode);
            assert_eq!(f.execute(), sys::OK);
            assert_eq!(f.state.calls.get(), 1);
            assert_eq!(f.state.scalars.get(), if mode == 1 { 4 } else { 0 });
            assert_eq!(
                *f.state.results.borrow(),
                vec![(3, Some(0)), (2, None), (1, Some(2)), (0, Some(1))]
            );
            assert_eq!(
                f.state.polls.get(),
                if !extended {
                    0
                } else if mode == 1 {
                    6
                } else {
                    2
                }
            );
        }
    }
    let f = Fixture::new(false);
    assert_eq!(
        unsafe {
            ABI.v2.v1.execute.unwrap()(
                ptr::from_ref(&*f.state).cast_mut().cast(),
                &f.query.v1,
                f.values.as_ptr(),
                1,
            )
        },
        sys::OK
    );
    assert_eq!(*f.state.results.borrow(), vec![(99, Some(1))]);
    assert_eq!(ABI.v2.v1.spi_minor, sys::BATCH_MINOR);
    assert!(ABI.v2.resolve_result.is_none());
}
#[test]
fn missing_duplicate_failed_and_panicking_outputs_never_succeed() {
    for (mode, expected) in [
        (2, sys::FAILED_PRECONDITION),
        (3, sys::FAILED_PRECONDITION),
        (4, sys::INVALID),
        (5, sys::NO_MEMORY),
        (6, sys::INTERNAL),
        (7, sys::TIMEOUT),
        (8, sys::INVALID),
    ] {
        let f = Fixture::new(false);
        f.state.mode.set(mode);
        if mode == 5 {
            f.state.fail.set(sys::NO_MEMORY);
        }
        assert_eq!(f.execute(), expected, "mode={mode}");
        assert_eq!(f.state.calls.get(), 1);
    }
    let f = Fixture::new(false);
    f.state.fail.set(sys::END_OF_STREAM);
    assert_eq!(f.execute(), sys::INVALID);
}
#[test]
fn cancellation_before_and_after_callback_discards_the_batch() {
    let f = Fixture::new(true);
    f.state.cancel.set(true);
    assert_eq!(f.execute(), sys::TIMEOUT);
    assert_eq!(f.state.calls.get(), 0);
    assert!(f.state.results.borrow().is_empty());
    let f = Fixture::new(true);
    f.state.mode.set(9);
    assert_eq!(f.execute(), sys::TIMEOUT);
    // Transport saw partial results. The host must not publish them on failure.
    assert_eq!(f.state.results.borrow().len(), 4);
    let f = Fixture::new(true);
    f.state.mode.set(10);
    // A handler cannot erase a transient polling error by ignoring it.
    assert_eq!(f.execute(), sys::TIMEOUT);
}
#[test]
fn malformed_inputs_are_rejected_before_the_handler() {
    for fault in 0..14 {
        let mut f = Fixture::new(false);
        match fault {
            0 => f.context.struct_size = 0,
            1 => f.context.reserved_word = 1,
            2 => f.context.reserved[0] = 1,
            3 => f.context.query_context = ptr::null(),
            4 => f.context.emit_result = None,
            5 => f.rows[0].struct_size = 0,
            6 => f.rows[0].reserved[0] = 1,
            7 => f.rows[1].argument_count = 0,
            8 => f.rows[0].arguments = ptr::null(),
            9 => f.values[0].is_null = 2,
            10 => f.values[0].reserved[0] = 1,
            11 => f.values[0].data = ptr::null(),
            12 => f.values[0].type_id = ptr::null(),
            _ => f.query.v1.reserved[0] = 1,
        }
        assert_ne!(f.execute(), sys::OK, "fault={fault}");
        assert_eq!(f.state.calls.get(), 0);
        assert!(f.state.results.borrow().is_empty());
    }
}
#[test]
fn empty_batch_and_aggregate_byte_limit() {
    let mut f = Fixture::new(false);
    f.rows.clear();
    assert_eq!(f.execute(), sys::OK);
    assert_eq!(f.state.calls.get(), 0);
    let bytes = vec![0u8; 16 * 1024 * 1024];
    let f = Fixture::new(false);
    let value = sys::Value {
        struct_size: size_of::<sys::Value>() as u32,
        type_id: c"core.type.bytes".as_ptr(),
        data: bytes.as_ptr(),
        data_size: bytes.len() as u64,
        is_null: 0,
        reserved_bytes: [0; 7],
        reserved: [0; 4],
    };
    let rows: Vec<_> = (0..5)
        .map(|_| sys::BatchRow {
            struct_size: size_of::<sys::BatchRow>() as u32,
            argument_count: 1,
            arguments: &value,
            reserved: [0; 4],
        })
        .collect();
    assert_eq!(
        unsafe {
            ABI.execute_batch.unwrap()(
                ptr::from_ref(&*f.state).cast_mut().cast(),
                &f.context,
                rows.as_ptr(),
                5,
            )
        },
        sys::INVALID
    );
    assert_eq!(f.state.calls.get(), 0);
}
