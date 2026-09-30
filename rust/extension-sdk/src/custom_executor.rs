// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Version-bound custom execution. Algorithms own their state; input rows and
//! context are borrowed from one exclusive callback, never sent to workers.
//! Different cursors, including open/close, may run concurrently on the same
//! instance. Register the service with THREAD_SAFE and synchronize shared state;
//! State: Send only permits exclusive cursor handoff, not shared unsynchronized
//! access to plugin globals. Live cursor leases delay lifecycle stop/deinit.
use crate::{boundary, result_type, status, sys, table::Cell, Result};
mod schema;
pub use schema::{Column, Encoding, Schema};
use std::{
    ffi::{c_void, CStr},
    marker::PhantomData,
    mem::size_of,
    ptr,
    rc::Rc,
};

const MAX_BYTES: usize = 16 * 1024 * 1024;
/// Borrowed until the next host callback, enforced by the mutable Context borrow.
/// ```compile_fail
/// use seekdb_extension::custom_executor::Context;
/// fn invalid(c: &mut Context<'_>) {
///     let row = c.next_input(0).unwrap().unwrap();
///     c.check_interrupt().unwrap();
///     let _ = row.len();
/// }
/// ```
pub struct Row<'a> {
    values: &'a [sys::Value],
    _thread: PhantomData<Rc<()>>,
}
impl Row<'_> {
    pub fn len(&self) -> usize {
        self.values.len()
    }
    pub fn is_empty(&self) -> bool {
        self.values.is_empty()
    }
    pub fn cell(&self, index: usize) -> Result<Cell<'_>> {
        let value = self.values.get(index).ok_or(sys::INVALID)?;
        Ok(Cell {
            type_id: unsafe { CStr::from_ptr(value.type_id) },
            bytes: if value.is_null != 0 {
                None
            } else if value.data_size == 0 {
                Some(&[])
            } else {
                Some(unsafe { std::slice::from_raw_parts(value.data, value.data_size as usize) })
            },
        })
    }
}

/// ```compile_fail
/// fn send<T: Send>() {}
/// send::<seekdb_extension::custom_executor::Context<'static>>();
/// ```
pub struct Context<'a> {
    raw: &'a sys::CustomContext,
    schemas: Option<&'a sys::CustomContextV2>,
    input_control: Option<&'a sys::CustomContextV3>,
    binding_control: Option<&'a sys::CustomContextV4>,
    error: sys::Status,
    database_error: i32,
    emitted: bool,
    _thread: PhantomData<Rc<()>>,
}
impl Context<'_> {
    pub fn has_input_bindings(&self) -> bool {
        self.binding_control.is_some()
    }
    /// Bind declared parameters from the host-owned snapshot of the source
    /// row, then rescan this input. No arbitrary parameter writes or retained
    /// plugin buffer. A source advance invalidates the previous environment.
    /// ```compile_fail
    /// use seekdb_extension::custom_executor::Context;
    /// fn invalid(c: &mut Context<'_>) {
    ///     let row = c.next_input(0).unwrap().unwrap();
    ///     c.bind_rescan_input(1).unwrap();
    ///     let _ = row.len();
    /// }
    /// ```
    pub fn bind_rescan_input(&mut self, input: u32) -> Result<()> {
        status(self.error)?;
        if input >= self.input_count() || self.emitted {
            return self.record(sys::INVALID, 0);
        }
        let Some(control) = self.binding_control else {
            return self.record(sys::UNSUPPORTED_ABI, 0);
        };
        self.check_interrupt()?;
        let mut db = 0;
        let code =
            unsafe { control.bind_rescan_input.unwrap()(self.raw.host_context, input, &mut db) };
        self.record(
            if code == sys::END_OF_STREAM {
                sys::INVALID
            } else {
                code
            },
            db,
        )?;
        self.check_interrupt()
    }
    pub fn has_input_rescan(&self) -> bool {
        self.input_control.is_some()
    }
    /// Rewind one child in its current parameter environment, without resetting
    /// siblings or plugin state. This does not bind parameters or guarantee
    /// repeatable rows. Invalidates all borrowed rows; forbidden after emit.
    /// Failures are sticky, even if the algorithm ignores the returned Result.
    /// With an owned binding graph, rewinding a source invalidates its snapshot
    /// and dependent input. Read a new source row and bind before target access.
    /// ```compile_fail
    /// use seekdb_extension::custom_executor::Context;
    /// fn invalid(c: &mut Context<'_>) {
    ///     let row = c.next_input(0).unwrap().unwrap();
    ///     c.rescan_input(0).unwrap();
    ///     let _ = row.len();
    /// }
    /// ```
    pub fn rescan_input(&mut self, input: u32) -> Result<()> {
        status(self.error)?;
        if input >= self.input_count() || self.emitted {
            return self.record(sys::INVALID, 0);
        }
        let Some(control) = self.input_control else {
            return self.record(sys::UNSUPPORTED_ABI, 0);
        };
        self.check_interrupt()?;
        let mut db = 0;
        let code = unsafe { control.rescan_input.unwrap()(self.raw.host_context, input, &mut db) };
        self.record(
            if code == sys::END_OF_STREAM {
                sys::INVALID
            } else {
                code
            },
            db,
        )?;
        self.check_interrupt()
    }
    pub fn has_schema(&self) -> bool {
        self.schemas.is_some()
    }
    /// Read-only metadata; does not fetch input, poll, or invoke a host callback.
    /// Like Row::cell, invalid lookup returns an error without poisoning context.
    pub fn input_schema(&self, index: u32) -> Result<Schema<'_>> {
        let schemas = self.schemas.ok_or(sys::UNSUPPORTED_ABI)?;
        if index >= self.input_count() {
            return Err(sys::INVALID);
        }
        Ok(unsafe { Schema::from_validated(&*schemas.inputs.add(index as usize)) })
    }
    pub fn output_schema(&self) -> Result<Schema<'_>> {
        let schemas = self.schemas.ok_or(sys::UNSUPPORTED_ABI)?;
        Ok(unsafe { Schema::from_validated(&*schemas.output) })
    }
    pub fn input_count(&self) -> u32 {
        self.raw.input_count
    }
    pub fn output_column_count(&self) -> u32 {
        self.raw.output_column_count
    }
    pub fn database_error(&self) -> i32 {
        self.database_error
    }
    fn record(&mut self, code: sys::Status, database_error: i32) -> Result<()> {
        if self.error == sys::OK {
            self.error = if code == sys::OK && database_error != 0 {
                sys::FAILED_PRECONDITION
            } else {
                code
            };
            self.database_error = database_error;
        }
        status(self.error)
    }
    pub fn check_interrupt(&mut self) -> Result<()> {
        status(self.error)?;
        let mut db = 0;
        let code = unsafe { self.raw.check_interrupt.unwrap()(self.raw.host_context, &mut db) };
        self.record(
            if code == sys::END_OF_STREAM {
                sys::INVALID
            } else {
                code
            },
            db,
        )
    }
    pub fn next_input(&mut self, input: u32) -> Result<Option<Row<'_>>> {
        status(self.error)?;
        if input >= self.input_count() {
            self.record(sys::INVALID, 0)?;
        }
        let mut row = sys::CustomRow {
            struct_size: size_of::<sys::CustomRow>() as u32,
            column_count: 0,
            values: ptr::null(),
            reserved: [0; 4],
        };
        let mut db = 0;
        let code = unsafe {
            self.raw.next_input.unwrap()(self.raw.host_context, input, &mut row, &mut db)
        };
        if code == sys::END_OF_STREAM && db == 0 {
            return Ok(None);
        }
        self.record(
            if code == sys::END_OF_STREAM {
                sys::INVALID
            } else {
                code
            },
            db,
        )?;
        if row.struct_size != size_of::<sys::CustomRow>() as u32
            || row.column_count > 1024
            || (row.column_count > 0 && row.values.is_null())
            || row.reserved != [0; 4]
        {
            self.record(sys::INVALID, 0)?;
        }
        let values = if row.column_count == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(row.values, row.column_count as usize) }
        };
        let mut bytes = 0usize;
        for value in values {
            if value.struct_size != size_of::<sys::Value>() as u32
                || value.is_null > 1
                || value.type_id.is_null()
                || value.reserved != [0; 4]
                || value.reserved_bytes != [0; 7]
                || value.data_size > MAX_BYTES as u64
                || (value.data_size > 0 && value.data.is_null())
                || (value.is_null != 0 && (value.data_size != 0 || !value.data.is_null()))
            {
                self.record(sys::INVALID, 0)?;
            }
            if result_type::validate_type(unsafe { CStr::from_ptr(value.type_id) }).is_err() {
                self.record(sys::INVALID, 0)?;
            }
            bytes += value.data_size as usize;
            if bytes > MAX_BYTES {
                self.record(sys::INVALID, 0)?;
            }
        }
        if self.has_schema() && !self.input_schema(input)?.accepts_values(values) {
            self.record(sys::INVALID, 0)?;
        }
        Ok(Some(Row {
            values,
            _thread: PhantomData,
        }))
    }
    pub fn emit(&mut self, cells: &[Cell<'_>]) -> Result<()> {
        status(self.error)?;
        if self.emitted || cells.len() != self.output_column_count() as usize {
            return self.record(sys::INVALID, 0);
        }
        if self.has_schema() && !self.output_schema()?.accepts_cells(cells) {
            return self.record(sys::INVALID, 0);
        }
        let mut values = Vec::new();
        if values.try_reserve_exact(cells.len()).is_err() {
            return self.record(sys::NO_MEMORY, 0);
        }
        let mut bytes = 0usize;
        for cell in cells {
            let size = cell.bytes.map_or(0, |v| v.len());
            if result_type::validate_type(cell.type_id).is_err() || size > MAX_BYTES - bytes {
                return self.record(sys::INVALID, 0);
            }
            bytes += size;
            values.push(sys::Value {
                struct_size: size_of::<sys::Value>() as u32,
                type_id: cell.type_id.as_ptr(),
                data: cell.bytes.map_or(ptr::null(), |v| v.as_ptr()),
                data_size: size as u64,
                is_null: u8::from(cell.bytes.is_none()),
                reserved_bytes: [0; 7],
                reserved: [0; 4],
            });
        }
        let mut db = 0;
        self.emitted = true;
        let code = unsafe {
            self.raw.emit.unwrap()(
                self.raw.host_context,
                values.as_ptr(),
                values.len() as u32,
                &mut db,
            )
        };
        self.record(
            if code == sys::END_OF_STREAM {
                sys::INVALID
            } else {
                code
            },
            db,
        )
    }
}
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Step {
    Row,
    End,
}
/// State owns retained data, is Send, and is never concurrently accessed by
/// the host. close consumes it even on error. A panic poisons next until rescan.
pub trait Executor {
    type State: Send + 'static;
    /// Advertise understanding of optional v3 input control (service minor=1).
    /// Hosts may still supply v1/v2; check has_input_rescan before relying on it.
    const INPUT_RESCAN: bool = false;
    /// Understand optional v4 binding control; implies INPUT_RESCAN, minor=2.
    const INPUT_BINDINGS: bool = false;
    fn validate_instance(instance: *mut sys::Handle) -> Result<()>;
    fn open(instance: *mut sys::Handle, plan: &[u8]) -> Result<Self::State>;
    fn next(state: &mut Self::State, context: &mut Context<'_>) -> Result<Step>;
    fn rescan(state: &mut Self::State) -> Result<()>;
    fn close(state: Self::State) -> Result<()> {
        drop(state);
        Ok(())
    }
}
struct Cursor<E: Executor> {
    state: E::State,
    failed: bool,
}
pub struct Service<E: Executor>(PhantomData<E>);
impl<E: Executor> Service<E> {
    pub const ABI: sys::CustomExecutor = sys::CustomExecutor {
        struct_size: size_of::<sys::CustomExecutor>() as u32,
        spi_major: 1,
        spi_minor: if E::INPUT_BINDINGS {
            2
        } else if E::INPUT_RESCAN {
            1
        } else {
            0
        },
        reserved_word: 0,
        open: Some(open::<E>),
        next: Some(next::<E>),
        rescan: Some(rescan::<E>),
        close: Some(close::<E>),
        reserved: [0; 4],
    };
}
unsafe extern "C" fn open<E: Executor>(
    instance: *mut sys::Handle,
    plan: *const u8,
    size: u32,
    out: *mut *mut c_void,
) -> sys::Status {
    boundary(|| {
        if out.is_null() {
            return Err(sys::INVALID);
        }
        unsafe {
            *out = ptr::null_mut();
        }
        E::validate_instance(instance)?;
        if size > 65536 || (size > 0 && plan.is_null()) {
            return Err(sys::INVALID);
        }
        let plan = if size == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(plan, size as usize) }
        };
        let state = E::open(instance, plan)?;
        unsafe {
            *out = Box::into_raw(Box::new(Cursor::<E> {
                state,
                failed: false,
            }))
            .cast();
        }
        Ok(())
    })
}
unsafe extern "C" fn next<E: Executor>(
    instance: *mut sys::Handle,
    cursor: *mut c_void,
    raw: *const sys::CustomContext,
) -> sys::Status {
    let mut ended = false;
    let code = boundary(|| {
        E::validate_instance(instance)?;
        if cursor.is_null() || raw.is_null() {
            return Err(sys::INVALID);
        }
        let cursor = unsafe { &mut *cursor.cast::<Cursor<E>>() };
        if cursor.failed {
            return Err(sys::FAILED_PRECONDITION);
        }
        cursor.failed = true;
        let raw = unsafe { &*raw };
        let bound = raw.struct_size == size_of::<sys::CustomContextV4>() as u32;
        let controlled = bound || raw.struct_size == size_of::<sys::CustomContextV3>() as u32;
        let described = controlled || raw.struct_size == size_of::<sys::CustomContextV2>() as u32;
        if (!described && raw.struct_size != size_of::<sys::CustomContext>() as u32)
            || raw.input_count > 64
            || raw.output_column_count > 1024
            || raw.reserved_word != 0
            || raw.host_context.is_null()
            || raw.next_input.is_none()
            || raw.emit.is_none()
            || raw.check_interrupt.is_none()
            || raw.reserved != [0; 4]
        {
            return Err(sys::INVALID);
        }
        let schemas = if described {
            let schemas =
                unsafe { &*(raw as *const sys::CustomContext).cast::<sys::CustomContextV2>() };
            if (raw.input_count != 0 && schemas.inputs.is_null())
                || schemas.output.is_null()
                || schemas.reserved != [0; 4]
            {
                return Err(sys::INVALID);
            }
            for index in 0..raw.input_count {
                unsafe { schema::validate(&*schemas.inputs.add(index as usize)) }?;
            }
            unsafe { schema::validate(&*schemas.output) }?;
            if unsafe { (*schemas.output).column_count } != raw.output_column_count {
                return Err(sys::INVALID);
            }
            Some(schemas)
        } else {
            None
        };
        let input_control = if controlled {
            let control =
                unsafe { &*(raw as *const sys::CustomContext).cast::<sys::CustomContextV3>() };
            if control.rescan_input.is_none() || control.reserved != [0; 4] {
                return Err(sys::INVALID);
            }
            Some(control)
        } else {
            None
        };
        let binding_control = if bound {
            let control =
                unsafe { &*(raw as *const sys::CustomContext).cast::<sys::CustomContextV4>() };
            if control.bind_rescan_input.is_none() || control.reserved != [0; 4] {
                return Err(sys::INVALID);
            }
            Some(control)
        } else {
            None
        };
        let mut context = Context {
            raw,
            schemas,
            input_control,
            binding_control,
            error: sys::OK,
            database_error: 0,
            emitted: false,
            _thread: PhantomData,
        };
        context.check_interrupt()?;
        let step = E::next(&mut cursor.state, &mut context);
        status(context.error)?;
        let step = step?;
        context.check_interrupt()?;
        if (step == Step::Row) != context.emitted {
            return Err(sys::FAILED_PRECONDITION);
        }
        ended = step == Step::End;
        cursor.failed = false;
        Ok(())
    });
    if code == sys::OK && ended {
        sys::END_OF_STREAM
    } else if code == sys::END_OF_STREAM {
        sys::INVALID
    } else {
        code
    }
}
unsafe extern "C" fn rescan<E: Executor>(
    instance: *mut sys::Handle,
    cursor: *mut c_void,
) -> sys::Status {
    boundary(|| {
        E::validate_instance(instance)?;
        if cursor.is_null() {
            return Err(sys::INVALID);
        }
        let cursor = unsafe { &mut *cursor.cast::<Cursor<E>>() };
        cursor.failed = true;
        E::rescan(&mut cursor.state)?;
        cursor.failed = false;
        Ok(())
    })
}
unsafe extern "C" fn close<E: Executor>(_: *mut sys::Handle, cursor: *mut c_void) -> sys::Status {
    boundary(|| {
        if cursor.is_null() {
            return Err(sys::INVALID);
        }
        let cursor = unsafe { Box::from_raw(cursor.cast::<Cursor<E>>()) };
        E::close(cursor.state)
    })
}
