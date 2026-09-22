// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Synchronous caller-session SQL. Row data is borrowed only during its consumer
//! callback; copy it explicitly if it must survive to the next row. This is not
//! an independent transaction, an async interface, or a prepared cursor API.

pub use crate::catalog::RoutineKind;
use crate::{boundary, sys};
use std::ffi::c_void;
use std::marker::PhantomData;
use std::mem::size_of;
use std::ptr;
use std::rc::Rc;

const BYTE_LIMIT: u64 = 16 * 1024 * 1024;
const COLUMN_LIMIT: usize = 1024;

pub(crate) fn query_control(
    context: Option<&sys::ContextV2>,
) -> Result<(&sys::SqlApiV2, *mut sys::Handle), Error> {
    let context = context.ok_or(sys::UNAVAILABLE)?;
    if context.sql_api.is_null() || context.sql_context.is_null() {
        return Err(sys::UNAVAILABLE.into());
    }
    if context.reserved != [0; 4] || context.v1.reserved != [0; 6] {
        return Err(sys::INVALID.into());
    }
    let raw = context.sql_api;
    if unsafe { (*raw).struct_size } < size_of::<sys::SqlApiV2>() as u32 {
        return Err(sys::UNSUPPORTED_ABI.into());
    }
    let api = unsafe { &*raw.cast::<sys::SqlApiV2>() };
    if api.v1.spi_major != 1 || api.v1.spi_minor < 1 {
        return Err(sys::UNSUPPORTED_ABI.into());
    }
    if api.v1.reserved_word != 0 || api.v1.reserved != [0; 6] || api.reserved != [0; 4] {
        return Err(sys::INVALID.into());
    }
    if api.poll_query.is_none() {
        return Err(sys::UNAVAILABLE.into());
    }
    Ok((api, context.sql_context))
}

pub(crate) fn poll_query(
    context: Option<&sys::ContextV2>,
) -> Result<Option<std::time::Duration>, Error> {
    let (api, handle) = query_control(context)?;
    poll_callback(api.poll_query.unwrap(), handle)
}

pub(crate) fn poll_callback(
    poll: unsafe extern "C" fn(*mut sys::Handle, *mut sys::QueryStatus) -> sys::Status,
    handle: *mut sys::Handle,
) -> Result<Option<std::time::Duration>, Error> {
    let mut output = sys::QueryStatus {
        struct_size: size_of::<sys::QueryStatus>() as u32,
        reserved_word: 0,
        database_error: 0,
        remaining_us: 0,
        reserved: [0; 4],
    };
    let status = unsafe { poll(handle, &mut output) };
    if output.struct_size != size_of::<sys::QueryStatus>() as u32
        || output.reserved_word != 0
        || output.reserved != [0; 4]
    {
        return Err(sys::INVALID.into());
    }
    if status != sys::OK || output.database_error != 0 {
        return Err(Error {
            status: if status == sys::OK {
                sys::FAILED_PRECONDITION
            } else {
                status
            },
            database_error: output.database_error,
            consumer_status: None,
        });
    }
    match output.remaining_us {
        -1 => Ok(None),
        0.. => Ok(Some(std::time::Duration::from_micros(
            output.remaining_us as u64,
        ))),
        _ => Err(sys::INVALID.into()),
    }
}

/// Positional parameter or borrowed result cell. Numeric values are copied;
/// text and binary data retain distinct SQL types. NULL is not an empty string.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Value<'a> {
    Null,
    I64(i64),
    U64(u64),
    F64(f64),
    Text(&'a str),
    Bytes(&'a [u8]),
}

/// The host status and original database error are both retained. A consumer
/// error is recorded separately because the host can translate it to cancellation.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Error {
    pub status: sys::Status,
    pub database_error: i64,
    pub consumer_status: Option<sys::Status>,
}

impl From<sys::Status> for Error {
    fn from(status: sys::Status) -> Self {
        Self {
            status: if status == sys::OK {
                sys::INTERNAL
            } else {
                status
            },
            database_error: 0,
            consumer_status: None,
        }
    }
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "plugin SQL status {}, database error {}, consumer status {:?}",
            self.status, self.database_error, self.consumer_status
        )
    }
}
impl std::error::Error for Error {}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Outcome {
    pub affected_rows: i64,
    pub returned_rows: u64,
}

/// A validated, thread-bound view. Its lifetime is chosen by the host callback,
/// not by the caller. Retaining a borrowed cell beyond the callback is rejected:
///
/// ```compile_fail
/// use seekdb_extension::{Call, sql::Value};
/// fn escape(call: &mut Call<'_>) {
///     let mut retained = None;
///     call.execute_sql("SELECT 'x'", &[], 1, |row| {
///         if let Value::Text(text) = row.get(0)? { retained = Some(text); }
///         Ok(())
///     }).unwrap();
///     println!("{retained:?}");
/// }
/// ```
///
/// A row also cannot be moved to a foreign thread, even a scoped one:
///
/// ```compile_fail
/// use seekdb_extension::sql::Row;
/// fn cross_thread(row: Row<'_>) {
///     std::thread::scope(|scope| { scope.spawn(move || row.len()); });
/// }
/// ```
pub struct Row<'row> {
    columns: &'row [sys::SqlValue],
    _thread_bound: PhantomData<Rc<()>>,
}

impl<'row> Row<'row> {
    pub fn len(&self) -> usize {
        self.columns.len()
    }
    pub fn is_empty(&self) -> bool {
        self.columns.is_empty()
    }
    pub fn get(&self, index: usize) -> crate::Result<Value<'row>> {
        decode(self.columns.get(index).ok_or(sys::INVALID)?)
    }
}

fn parameter(value: &Value<'_>) -> sys::SqlValue {
    let (kind, data, data_size) = match value {
        Value::Null => (0, ptr::null(), 0),
        Value::I64(v) => (1, (v as *const i64).cast(), 8),
        Value::U64(v) => (2, (v as *const u64).cast(), 8),
        Value::F64(v) => (3, (v as *const f64).cast(), 8),
        Value::Text(v) => (4, v.as_ptr().cast(), v.len() as u64),
        Value::Bytes(v) => (5, v.as_ptr().cast(), v.len() as u64),
    };
    sys::SqlValue {
        struct_size: size_of::<sys::SqlValue>() as u32,
        kind,
        data,
        data_size,
        reserved: [0; 2],
    }
}

// Only Row and the synchronous host consumer construct these borrowed views.
// The Call::from_raw safety contract includes the host callback's allocations.
fn decode(value: &sys::SqlValue) -> crate::Result<Value<'_>> {
    if value.struct_size < size_of::<sys::SqlValue>() as u32
        || value.data_size > BYTE_LIMIT
        || (value.data_size != 0 && value.data.is_null())
    {
        return Err(sys::INVALID);
    }
    Ok(match value.kind {
        0 if value.data_size == 0 => Value::Null,
        1 if value.data_size == 8 => Value::I64(unsafe { ptr::read_unaligned(value.data.cast()) }),
        2 if value.data_size == 8 => Value::U64(unsafe { ptr::read_unaligned(value.data.cast()) }),
        3 if value.data_size == 8 => Value::F64(unsafe { ptr::read_unaligned(value.data.cast()) }),
        4 | 5 => {
            let bytes = if value.data_size == 0 {
                &[]
            } else {
                unsafe {
                    std::slice::from_raw_parts(value.data.cast::<u8>(), value.data_size as usize)
                }
            };
            if value.kind == 4 {
                Value::Text(std::str::from_utf8(bytes).map_err(|_| sys::INVALID)?)
            } else {
                Value::Bytes(bytes)
            }
        }
        _ => return Err(sys::INVALID),
    })
}

struct Consumer<F> {
    callback: F,
    rows: u64,
    bytes: u64,
    max_rows: u64,
    error: Option<sys::Status>,
}

unsafe extern "C" fn consume<F>(
    opaque: *mut c_void,
    columns: *const sys::SqlValue,
    count: u32,
) -> sys::Status
where
    F: for<'row> FnMut(Row<'row>) -> crate::Result<()>,
{
    if opaque.is_null() {
        return sys::INVALID;
    }
    let consumer = unsafe { &mut *opaque.cast::<Consumer<F>>() };
    if let Some(error) = consumer.error {
        return error;
    }
    let status = boundary(|| {
        if count as usize > COLUMN_LIMIT || (count != 0 && columns.is_null()) {
            return Err(sys::INVALID);
        }
        if consumer.rows >= consumer.max_rows {
            return Err(sys::FAILED_PRECONDITION);
        }
        let columns = if count == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(columns, count as usize) }
        };
        for column in columns {
            // Validate ignored columns too, and enforce the aggregate byte limit.
            decode(column)?;
            if column.data_size > BYTE_LIMIT - consumer.bytes {
                return Err(sys::INVALID);
            }
            consumer.bytes += column.data_size;
        }
        (consumer.callback)(Row {
            columns,
            _thread_bound: PhantomData,
        })?;
        consumer.rows += 1;
        Ok(())
    });
    if status != sys::OK {
        consumer.error = Some(status);
    }
    status
}

// Call retains the original allocation's extended reference, never widening a
// reference that only grants access to the prefix.
pub(crate) fn execute<F>(
    context: Option<&sys::ContextV2>,
    sql: &str,
    parameters: &[Value<'_>],
    max_rows: u64,
    callback: F,
) -> Result<Outcome, Error>
where
    F: for<'row> FnMut(Row<'row>) -> crate::Result<()>,
{
    let context = context.ok_or(sys::UNAVAILABLE)?;
    if context.reserved != [0; 4] || context.v1.reserved != [0; 6] {
        return Err(sys::INVALID.into());
    }
    unsafe {
        execute_api(
            context.sql_api,
            context.sql_context,
            sql,
            parameters,
            max_rows,
            callback,
        )
    }
}

// Caller guarantees a live API allocation covering its advertised struct_size,
// a valid borrowed host handle, and synchronous same-thread use.
pub(crate) unsafe fn execute_api<F>(
    raw_api: *const sys::SqlApi,
    handle: *mut sys::Handle,
    sql: &str,
    parameters: &[Value<'_>],
    max_rows: u64,
    callback: F,
) -> Result<Outcome, Error>
where
    F: for<'row> FnMut(Row<'row>) -> crate::Result<()>,
{
    if raw_api.is_null() || handle.is_null() {
        return Err(sys::UNAVAILABLE.into());
    }
    if unsafe { (*raw_api).struct_size } < size_of::<sys::SqlApi>() as u32 {
        return Err(sys::UNSUPPORTED_ABI.into());
    }
    let api = unsafe { &*raw_api };
    if api.spi_major != 1 {
        return Err(sys::UNSUPPORTED_ABI.into());
    }
    if api.reserved_word != 0 || api.reserved != [0; 6] {
        return Err(sys::INVALID.into());
    }
    let execute = api.execute.ok_or(sys::UNAVAILABLE)?;
    if sql.is_empty()
        || sql.len() as u64 > BYTE_LIMIT
        || sql.as_bytes().contains(&0)
        || parameters.len() > COLUMN_LIMIT
    {
        return Err(sys::INVALID.into());
    }
    let mut raw = Vec::new();
    raw.try_reserve_exact(parameters.len())
        .map_err(|_| Error::from(sys::NO_MEMORY))?;
    let mut bytes = 0;
    for value in parameters {
        let value = parameter(value);
        if value.data_size > BYTE_LIMIT - bytes {
            return Err(sys::INVALID.into());
        }
        bytes += value.data_size;
        raw.push(value);
    }
    let mut consumer = Consumer {
        callback,
        rows: 0,
        bytes: 0,
        max_rows,
        error: None,
    };
    let mut result = sys::SqlResult {
        struct_size: size_of::<sys::SqlResult>() as u32,
        reserved_word: 0,
        database_error: 0,
        affected_rows: 0,
        returned_rows: 0,
        reserved: [0; 2],
    };
    let status = unsafe {
        execute(
            handle,
            sql.as_ptr().cast(),
            sql.len() as u64,
            raw.as_ptr(),
            raw.len() as u32,
            max_rows,
            Some(consume::<F>),
            (&mut consumer as *mut Consumer<F>).cast(),
            &mut result,
        )
    };
    if status != sys::OK || result.database_error != 0 || consumer.error.is_some() {
        return Err(Error {
            status: if status != sys::OK {
                status
            } else {
                consumer.error.unwrap_or(sys::INTERNAL)
            },
            database_error: result.database_error,
            consumer_status: consumer.error,
        });
    }
    if result.struct_size < size_of::<sys::SqlResult>() as u32
        || result.returned_rows != consumer.rows
    {
        return Err(sys::INTERNAL.into());
    }
    Ok(Outcome {
        affected_rows: result.affected_rows,
        returned_rows: result.returned_rows,
    })
}
