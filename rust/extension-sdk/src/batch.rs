// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! A true multi-row scalar invocation, with indexed, host-owned results.
use crate::{boundary, result_type, sys, Call, Result};
use std::{ffi::CStr, marker::PhantomData, mem::size_of, ptr, rc::Rc};

/// Borrowed inputs and the sole indexed result channel for this invocation.
/// The batch is compact (no SQL skip mask). Its lifetime cannot cross threads
/// or async tasks. Rows may be processed together and emitted in any order.
/// ```compile_fail
/// fn send<T: Send>() {}
/// send::<seekdb_extension::batch::Batch<'static>>();
/// ```
pub struct Batch<'a> {
    context: &'a sys::BatchContext,
    rows: &'a [sys::BatchRow],
    emitted: Vec<bool>,
    output_bytes: u64,
    error: Option<sys::Status>,
    _thread: PhantomData<Rc<()>>,
}

/// Read-only view of one row. Query services and emission stay on Batch.
pub struct Row<'a>(Call<'a>);
impl<'a> Row<'a> {
    pub fn argument_count(&self) -> usize {
        self.0.argument_count()
    }
    pub fn argument_type(&self, column: usize) -> Result<Option<&'a CStr>> {
        self.0.argument_type(column)
    }
    pub fn text(&self, column: usize) -> Result<Option<&'a str>> {
        self.0.text(column)
    }
    pub fn bytes(&self, column: usize, type_id: &CStr) -> Result<Option<&'a [u8]>> {
        self.0.bytes(column, type_id)
    }
}

impl<'a> Batch<'a> {
    /// # Safety
    /// Host pointers and advertised allocations must be aligned and valid for
    /// 'a, no longer than this synchronous callback. Identifiers must be valid
    /// NUL-terminated strings. Context callbacks cannot unwind or retain inputs.
    /// The host must discard all emitted results if the invocation fails.
    pub unsafe fn from_raw(
        context: *const sys::BatchContext,
        rows: *const sys::BatchRow,
        count: u32,
    ) -> Result<Self> {
        if context.is_null() || count > sys::MAX_BATCH_ROWS || (count != 0 && rows.is_null()) {
            return Err(sys::INVALID);
        }
        if unsafe { (*context).struct_size } < size_of::<sys::BatchContext>() as u32 {
            return Err(sys::UNSUPPORTED_ABI);
        }
        let context = unsafe { &*context };
        if context.reserved_word != 0 || context.reserved != [0; 4] || context.emit_result.is_none()
        {
            return Err(sys::INVALID);
        }
        // Validate the query-service prefix without exposing its scalar sink.
        let _ = unsafe { Call::from_raw(context.query_context, ptr::null(), 0) }?;
        if unsafe { (*context.query_context).reserved } != [0; 6] {
            return Err(sys::INVALID);
        }
        let rows = if count == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(rows, count as usize) }
        };
        let arity = rows.first().map_or(0, |row| row.argument_count);
        let mut bytes = 0u64;
        for row in rows {
            if row.struct_size < size_of::<sys::BatchRow>() as u32
                || row.reserved != [0; 4]
                || row.argument_count != arity
            {
                return Err(sys::INVALID);
            }
            let call = unsafe {
                Call::from_raw(context.query_context, row.arguments, row.argument_count)
            }?;
            for column in 0..call.argument_count() {
                let value = unsafe { &*row.arguments.add(column) };
                if value.is_null > 1
                    || value.reserved_bytes != [0; 7]
                    || value.reserved != [0; 4]
                    || value.data_size > 16 * 1024 * 1024
                    || (value.data_size != 0 && value.data.is_null())
                {
                    return Err(sys::INVALID);
                }
                call.argument_type(column)?;
                bytes = bytes.checked_add(value.data_size).ok_or(sys::INVALID)?;
                if bytes > sys::MAX_BATCH_BYTES {
                    return Err(sys::INVALID);
                }
            }
        }
        Ok(Self {
            context,
            rows,
            emitted: vec![false; rows.len()],
            output_bytes: 0,
            error: None,
            _thread: PhantomData,
        })
    }

    pub fn row_count(&self) -> usize {
        self.rows.len()
    }
    pub fn row(&self, index: usize) -> Result<Row<'a>> {
        let row = self.rows.get(index).ok_or(sys::INVALID)?;
        // The constructor validated these immutable host-owned allocations.
        unsafe {
            Call::from_raw(
                self.context.query_context,
                row.arguments,
                row.argument_count,
            )
            .map(Row)
        }
    }
    pub fn supports_query_control(&self) -> bool {
        unsafe { Call::from_raw(self.context.query_context, ptr::null(), 0) }
            .is_ok_and(|call| call.supports_query_control())
    }
    pub fn poll_query(
        &mut self,
    ) -> std::result::Result<Option<std::time::Duration>, crate::sql::Error> {
        let mut call = unsafe { Call::from_raw(self.context.query_context, ptr::null(), 0) }
            .map_err(crate::sql::Error::from)?;
        let result = call.poll_query();
        if let Err(error) = &result {
            self.error.get_or_insert(error.status);
        }
        result
    }
    fn fail<T>(&mut self, error: sys::Status) -> Result<T> {
        let error = if error == sys::END_OF_STREAM {
            sys::INVALID
        } else {
            error
        };
        self.error.get_or_insert(error);
        Err(error)
    }
    pub fn emit_i64(&mut self, row: usize, value: Option<i64>) -> Result<()> {
        let bytes = value.map(i64::to_ne_bytes);
        self.emit_bytes(
            row,
            c"core.type.int64",
            bytes.as_ref().map(|v| v.as_slice()),
        )
    }
    pub fn emit_bytes(&mut self, row: usize, type_id: &CStr, bytes: Option<&[u8]>) -> Result<()> {
        if let Some(error) = self.error {
            return Err(error);
        }
        if row >= self.rows.len() {
            return self.fail(sys::INVALID);
        }
        if self.emitted[row] {
            return self.fail(sys::FAILED_PRECONDITION);
        }
        if let Err(error) = result_type::validate_type(type_id) {
            return self.fail(error);
        }
        let size = bytes.map_or(0, |v| v.len()) as u64;
        if size > 16 * 1024 * 1024 || size > sys::MAX_BATCH_BYTES - self.output_bytes {
            return self.fail(sys::INVALID);
        }
        let value = sys::Value {
            struct_size: size_of::<sys::Value>() as u32,
            type_id: type_id.as_ptr(),
            data: bytes.map_or(ptr::null(), |v| v.as_ptr()),
            data_size: size,
            is_null: u8::from(bytes.is_none()),
            reserved_bytes: [0; 7],
            reserved: [0; 4],
        };
        self.emitted[row] = true;
        self.output_bytes += size;
        let status =
            unsafe { self.context.emit_result.unwrap()(self.context.host, row as u32, &value) };
        if status == sys::OK {
            Ok(())
        } else {
            self.fail(status)
        }
    }

    /// Adapt an existing scalar handler for one row while preserving the same
    /// query/SQL context. This is optional: a batch handler may instead gather
    /// all inputs and invoke a model/library once, then emit indexed results.
    pub fn with_row(
        &mut self,
        row: usize,
        handler: impl FnOnce(&mut Call<'_>) -> Result<()>,
    ) -> Result<()> {
        if let Some(error) = self.error {
            return Err(error);
        }
        let input = match self.rows.get(row) {
            Some(input) => input,
            None => return self.fail(sys::INVALID),
        };
        let (arguments, count) = (input.arguments, input.argument_count);
        let query = unsafe { &*self.context.query_context };
        let mut context = sys::ContextV2 {
            v1: sys::ContextV1 {
                struct_size: size_of::<sys::ContextV1>() as u32,
                host: ptr::null_mut(),
                emit_result: Some(emit_row),
                reserved: [0; 6],
            },
            sql_api: ptr::null(),
            sql_context: ptr::null_mut(),
            reserved: [0; 4],
        };
        if query.struct_size >= size_of::<sys::ContextV2>() as u32 {
            let services = unsafe { &*self.context.query_context.cast::<sys::ContextV2>() };
            context.v1.struct_size = size_of::<sys::ContextV2>() as u32;
            context.sql_api = services.sql_api;
            context.sql_context = services.sql_context;
            context.reserved = services.reserved;
        }
        let mut sink = RowSink { batch: self, row };
        context.v1.host = (&mut sink as *mut RowSink<'_, '_>).cast();
        let mut call = unsafe { Call::from_raw(&context.v1, arguments, count) }?;
        let result = handler(&mut call);
        if let Some(error) = sink.batch.error {
            return Err(error);
        }
        match result {
            Ok(()) if sink.batch.emitted[row] => Ok(()),
            Ok(()) => sink.batch.fail(sys::FAILED_PRECONDITION),
            Err(error) => sink.batch.fail(error),
        }
    }
    fn finish(&self) -> Result<()> {
        if let Some(error) = self.error {
            return Err(error);
        }
        if self.emitted.iter().all(|done| *done) {
            Ok(())
        } else {
            Err(sys::FAILED_PRECONDITION)
        }
    }
}
struct RowSink<'s, 'a> {
    batch: &'s mut Batch<'a>,
    row: usize,
}
unsafe extern "C" fn emit_row(host: *mut sys::Handle, value: *const sys::Value) -> sys::Status {
    boundary(|| {
        if host.is_null() || value.is_null() {
            return Err(sys::INVALID);
        }
        let sink = unsafe { &mut *host.cast::<RowSink<'_, '_>>() };
        let value = unsafe { &*value };
        if value.type_id.is_null()
            || value.data_size > 16 * 1024 * 1024
            || (value.data_size != 0 && value.data.is_null())
        {
            return sink.batch.fail(sys::INVALID);
        }
        let id = unsafe { CStr::from_ptr(value.type_id) };
        let bytes = if value.is_null != 0 {
            None
        } else if value.data_size == 0 {
            Some(&[][..])
        } else {
            Some(unsafe { std::slice::from_raw_parts(value.data, value.data_size as usize) })
        };
        sink.batch.emit_bytes(sink.row, id, bytes)
    })
}

/// Batch computation with plugin-specific lifecycle admission. It is invoked
/// once for the entire batch, not once per row. No implicit thread pool exists.
pub trait Handler {
    fn validate(instance: *mut sys::Handle) -> Result<()>;
    fn execute(instance: *mut sys::Handle, batch: &mut Batch<'_>) -> Result<()>;
}
pub struct Service<H>(PhantomData<H>);
impl<H: Handler> Service<H> {
    pub const fn with_scalar(
        scalar: sys::Execute,
        resolve_result: Option<sys::ResolveResult>,
    ) -> sys::FunctionServiceV3 {
        sys::FunctionServiceV3 {
            v2: sys::FunctionServiceV2 {
                v1: sys::FunctionService {
                    struct_size: size_of::<sys::FunctionServiceV3>() as u32,
                    spi_major: 1,
                    spi_minor: sys::BATCH_MINOR,
                    reserved_word: 0,
                    execute: Some(scalar),
                    reserved: [0; 8],
                },
                resolve_result,
                resolution_reserved: [0; 4],
            },
            execute_batch: Some(execute::<H>),
            batch_reserved: [0; 4],
        }
    }
}
unsafe extern "C" fn execute<H: Handler>(
    instance: *mut sys::Handle,
    context: *const sys::BatchContext,
    rows: *const sys::BatchRow,
    count: u32,
) -> sys::Status {
    boundary(|| {
        H::validate(instance)?;
        let mut batch = unsafe { Batch::from_raw(context, rows, count) }?;
        if count == 0 {
            return Ok(());
        }
        if batch.supports_query_control() {
            batch.poll_query().map_err(|e| e.status)?;
        }
        H::execute(instance, &mut batch).map_err(|error| {
            if error == sys::END_OF_STREAM {
                sys::INVALID
            } else {
                error
            }
        })?;
        if batch.supports_query_control() {
            batch.poll_query().map_err(|e| e.status)?;
        }
        batch.finish()
    })
}
