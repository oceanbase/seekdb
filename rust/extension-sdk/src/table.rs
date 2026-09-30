// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Synchronous native table streams. Host calls on one cursor must be exclusive,
//! but may move between threads. Borrowed input/context never escapes a callback.
use crate::{
    boundary, result_type, sql, status, sys, ImplementationReference, Registration, Result,
};
use std::{ffi::CStr, marker::PhantomData, mem::size_of, ptr, rc::Rc};

pub struct Column<'a> {
    pub name: &'a CStr,
    pub type_id: &'a CStr,
    pub nullable: bool,
}
pub struct Definition<'a> {
    pub object_id: &'a CStr,
    pub sql_name: &'a CStr,
    pub argument_types: &'a [&'a CStr],
    pub columns: &'a [Column<'a>],
    pub flags: u64,
    pub implementation: ImplementationReference<'a>,
}
impl Registration<'_> {
    /// Fixed typed signature; names and descriptors are copied synchronously by
    /// the host, in the same transaction as other extension objects.
    pub fn table_function(&mut self, definition: &Definition<'_>) -> Result<()> {
        if definition.argument_types.len() > 1024
            || definition.columns.is_empty()
            || definition.columns.len() > 4096
        {
            return Err(sys::INVALID);
        }
        let mut arguments = Vec::new();
        arguments
            .try_reserve_exact(definition.argument_types.len())
            .map_err(|_| sys::NO_MEMORY)?;
        arguments.extend(definition.argument_types.iter().map(|id| id.as_ptr()));
        let mut columns = Vec::new();
        columns
            .try_reserve_exact(definition.columns.len())
            .map_err(|_| sys::NO_MEMORY)?;
        for column in definition.columns {
            result_type::validate_type(column.type_id)?;
            columns.push(sys::TableColumn {
                struct_size: size_of::<sys::TableColumn>() as u32,
                sql_name: column.name.as_ptr(),
                type_id: column.type_id.as_ptr(),
                nullable: u8::from(column.nullable),
                reserved_bytes: [0; 7],
                reserved: [0; 4],
            });
        }
        self.register_descriptor(
            sys::TABLE_FUNCTION,
            &sys::TableFunction {
                struct_size: size_of::<sys::TableFunction>() as u32,
                object_id: definition.object_id.as_ptr(),
                sql_name: definition.sql_name.as_ptr(),
                minimum_arity: arguments.len() as u32,
                maximum_arity: arguments.len() as u32,
                argument_type_ids: if arguments.is_empty() {
                    ptr::null()
                } else {
                    arguments.as_ptr()
                },
                argument_type_count: arguments.len() as u32,
                signature_flags: 0,
                columns: columns.as_ptr(),
                column_count: columns.len() as u32,
                reserved_word: 0,
                flags: definition.flags,
                implementation: definition.implementation.raw(),
                reserved: [0; 4],
            },
        )
    }
}

/// Query-borrowed arguments cannot be saved as cursor-owned static bytes.
/// ```compile_fail
/// use seekdb_extension::table::Arguments;
/// fn escape(args: &Arguments<'_>) -> &'static [u8] {
///     args.bytes(0, c"example.bytes").unwrap().unwrap()
/// }
/// ```
/// Nor may callback contexts be sent to another worker.
/// ```compile_fail
/// use seekdb_extension::table::{Arguments, Rows};
/// fn require_send<T: Send>() {}
/// require_send::<Arguments<'static>>();
/// require_send::<Rows<'static>>();
/// ```
pub struct Arguments<'a> {
    values: &'a [sys::Value],
    _thread: PhantomData<Rc<()>>,
}
impl<'a> Arguments<'a> {
    // Caller guarantees aligned, live allocations including each value's
    // advertised byte slice and terminated type identifier.
    unsafe fn from_raw(values: *const sys::Value, count: u32) -> Result<Self> {
        if count > 1024 || (count != 0 && values.is_null()) {
            return Err(sys::INVALID);
        }
        let values = if count == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(values, count as usize) }
        };
        for value in values {
            if value.struct_size < size_of::<sys::Value>() as u32
                || value.is_null > 1
                || value.reserved_bytes != [0; 7]
                || value.reserved != [0; 4]
            {
                return Err(sys::INVALID);
            }
        }
        Ok(Self {
            values,
            _thread: PhantomData,
        })
    }
    pub fn len(&self) -> usize {
        self.values.len()
    }
    pub fn is_empty(&self) -> bool {
        self.values.is_empty()
    }
    pub fn bytes(&self, index: usize, type_id: &CStr) -> Result<Option<&'a [u8]>> {
        let value = self.values.get(index).ok_or(sys::INVALID)?;
        if value.is_null != 0 {
            return Ok(None);
        }
        if value.type_id.is_null()
            || unsafe { CStr::from_ptr(value.type_id) } != type_id
            || value.data_size > 16 * 1024 * 1024
            || (value.data_size != 0 && value.data.is_null())
        {
            return Err(sys::INVALID);
        }
        Ok(Some(if value.data_size == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(value.data, value.data_size as usize) }
        }))
    }
}

/// Borrowed field data is copied by the host before `Rows::emit` returns.
pub struct Cell<'a> {
    pub type_id: &'a CStr,
    pub bytes: Option<&'a [u8]>,
}

/// Owned builtin number, decoded without alignment assumptions or lossy casts.
/// These are native-endian in-process ABI values, not a storage format.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Number {
    Bool(bool),
    I32(i32),
    U32(u32),
    I64(i64),
    U64(u64),
    F64(f64),
}
impl Number {
    pub fn recognizes(type_id: &CStr) -> bool {
        Self::builtin_name(type_id).is_some()
    }
    fn builtin_name(type_id: &CStr) -> Option<&[u8]> {
        let id = type_id.to_bytes();
        // The GIS namespace is an explicit compatibility family, not a rule
        // that arbitrary user-defined IDs inherit semantics from their suffix.
        let name = id
            .strip_prefix(b"core.type.")
            .or_else(|| id.strip_prefix(b"org.seekdb.gis.scalar."))?;
        matches!(
            name,
            b"bool" | b"int32" | b"uint32" | b"int64" | b"uint64" | b"float64"
        )
        .then_some(name)
    }
}
impl Cell<'_> {
    /// Read an exact builtin number. A typed SQL NULL returns None; an unknown
    /// type (even a NULL with a builtin-looking suffix) or malformed bytes is
    /// INVALID. Custom types remain available through `bytes` and their codec.
    pub fn number(&self) -> Result<Option<Number>> {
        let name = Number::builtin_name(self.type_id).ok_or(sys::INVALID)?;
        let Some(bytes) = self.bytes else {
            return Ok(None);
        };
        Ok(Some(match name {
            b"bool" => match bytes {
                [0] => Number::Bool(false),
                [1] => Number::Bool(true),
                _ => return Err(sys::INVALID),
            },
            b"int32" => Number::I32(i32::from_ne_bytes(
                bytes.try_into().map_err(|_| sys::INVALID)?,
            )),
            b"uint32" => Number::U32(u32::from_ne_bytes(
                bytes.try_into().map_err(|_| sys::INVALID)?,
            )),
            b"int64" => Number::I64(i64::from_ne_bytes(
                bytes.try_into().map_err(|_| sys::INVALID)?,
            )),
            b"uint64" => Number::U64(u64::from_ne_bytes(
                bytes.try_into().map_err(|_| sys::INVALID)?,
            )),
            b"float64" => Number::F64(f64::from_ne_bytes(
                bytes.try_into().map_err(|_| sys::INVALID)?,
            )),
            _ => return Err(sys::INVALID),
        }))
    }
}

/// Same-thread, callback-borrowed query access. Own query results in the cursor,
/// never this context or a SQL row borrowed from its consumer.
/// ```compile_fail
/// use seekdb_extension::table::QueryContext;
/// fn require_send<T: Send>() {}
/// require_send::<QueryContext<'static>>();
/// ```
/// ```compile_fail
/// use seekdb_extension::table::QueryContext;
/// fn reenter(query: &mut QueryContext<'_>) {
///     query.execute_sql("SELECT 1", &[], 1, |_| {
///         query.poll_query().map_err(|error| error.status)?;
///         Ok(())
///     }).unwrap();
/// }
/// ```
pub struct QueryContext<'a> {
    control: Option<&'a sys::TableContextV2>,
    sql: Option<&'a sys::TableContextV3>,
    projection: Option<&'a [u8]>,
    error: Option<sql::Error>,
    _thread: PhantomData<Rc<()>>,
}
impl QueryContext<'_> {
    // raw must refer to the original allocation, not a narrowed prefix borrow.
    unsafe fn from_raw<'a>(raw: *const sys::TableContext) -> Result<QueryContext<'a>> {
        let size = unsafe { (*raw).struct_size } as usize;
        let control = if size >= size_of::<sys::TableContextV2>() {
            let context = unsafe { &*raw.cast::<sys::TableContextV2>() };
            if context.reserved != [0; 4] {
                return Err(sys::INVALID);
            }
            Some(context)
        } else {
            None
        };
        let sql = if size >= size_of::<sys::TableContextV3>() {
            let context = unsafe { &*raw.cast::<sys::TableContextV3>() };
            if context.reserved != [0; 4] {
                return Err(sys::INVALID);
            }
            Some(context)
        } else {
            None
        };
        let projection = if size >= size_of::<sys::TableContextV4>() {
            let context = unsafe { &*raw.cast::<sys::TableContextV4>() };
            if context.reserved_word != 0
                || context.reserved != [0; 4]
                || context.column_count > 4096
                || (context.column_count == 0) != context.requested_columns.is_null()
            {
                return Err(sys::INVALID);
            }
            if context.column_count == 0 {
                None
            } else {
                let columns = unsafe {
                    std::slice::from_raw_parts(
                        context.requested_columns,
                        context.column_count as usize,
                    )
                };
                if columns.iter().any(|value| *value > 1) {
                    return Err(sys::INVALID);
                }
                Some(columns)
            }
        } else {
            None
        };
        Ok(QueryContext {
            control,
            sql,
            projection,
            error: None,
            _thread: PhantomData,
        })
    }
    pub fn supports_query_control(&self) -> bool {
        self.control
            .is_some_and(|raw| !raw.query_context.is_null() && raw.poll_query.is_some())
    }
    /// Full declared column count when projection metadata is available.
    pub fn projection_column_count(&self) -> Option<usize> {
        self.projection.map(<[u8]>::len)
    }
    /// Missing metadata conservatively requests every column. Re-read on each
    /// callback; never change stream cardinality or side effects. Unrequested
    /// columns still need valid typed placeholders in the existing row format.
    pub fn column_requested(&self, index: usize) -> Result<bool> {
        if index >= 4096 {
            return Err(sys::INVALID);
        }
        match self.projection {
            Some(columns) => columns
                .get(index)
                .map(|value| *value != 0)
                .ok_or(sys::INVALID),
            None => Ok(true),
        }
    }
    fn require_sql(&self) -> Result<()> {
        let context = self.sql.ok_or(sys::UNAVAILABLE)?;
        let raw = context.sql_api;
        if raw.is_null() || context.v2.query_context.is_null() {
            return Err(sys::UNAVAILABLE);
        }
        if unsafe { (*raw).struct_size } < size_of::<sys::SqlApi>() as u32 {
            return Err(sys::UNSUPPORTED_ABI);
        }
        let api = unsafe { &*raw };
        if api.spi_major != 1 {
            return Err(sys::UNSUPPORTED_ABI);
        }
        if api.reserved_word != 0 || api.reserved != [0; 6] {
            return Err(sys::INVALID);
        }
        api.execute.ok_or(sys::UNAVAILABLE)?;
        Ok(())
    }
    pub fn supports_sql(&self) -> bool {
        self.require_sql().is_ok()
    }
    pub fn supports_catalog_lookup(&self) -> bool {
        self.sql.is_some_and(|raw| unsafe {
            crate::query_catalog::callback(raw.sql_api, raw.v2.query_context).is_ok()
        })
    }
    pub fn lookup_routine(
        &mut self,
        kind: sql::RoutineKind,
        name: &str,
    ) -> std::result::Result<Option<u64>, sql::Error> {
        if let Some(error) = self.error {
            return Err(error);
        }
        let result = match self.sql {
            Some(raw) => unsafe {
                crate::query_catalog::lookup(raw.sql_api, raw.v2.query_context, kind, name)
            },
            None => Err(sys::UNAVAILABLE.into()),
        };
        if let Err(error) = result {
            self.error = Some(error);
        }
        result
    }
    pub fn supports_catalog_mutation(&self) -> bool {
        self.sql.is_some_and(|raw| unsafe {
            crate::query_catalog::mutation_callback(raw.sql_api, raw.v2.query_context).is_ok()
        })
    }
    /// Same provisional, caller-transaction routine mutation as Call. This
    /// per-callback context cannot be retained by a cursor or moved to a thread.
    pub fn mutate_routine(
        &mut self,
        statement: &str,
    ) -> std::result::Result<Option<u64>, sql::Error> {
        if let Some(error) = self.error {
            return Err(error);
        }
        let result = match self.sql {
            Some(raw) => unsafe {
                crate::query_catalog::mutate(raw.sql_api, raw.v2.query_context, statement)
            },
            None => Err(sys::UNAVAILABLE.into()),
        };
        if let Err(error) = result {
            self.error = Some(error);
        }
        result
    }
    pub fn poll_query(&mut self) -> std::result::Result<Option<std::time::Duration>, sql::Error> {
        if let Some(error) = self.error {
            return Err(error);
        }
        let result = match self.control {
            Some(raw) if !raw.query_context.is_null() => match raw.poll_query {
                Some(poll) => sql::poll_callback(poll, raw.query_context),
                None => Err(sys::UNAVAILABLE.into()),
            },
            _ => Err(sys::UNAVAILABLE.into()),
        };
        if let Err(error) = result {
            self.error = Some(error);
        }
        result
    }
    /// Synchronous caller-session SQL; all rows are consumed before returning.
    /// No DDL, commit, retained prepared handle, or asynchronous access.
    pub fn execute_sql<F>(
        &mut self,
        statement: &str,
        parameters: &[sql::Value<'_>],
        max_rows: u64,
        consumer: F,
    ) -> std::result::Result<sql::Outcome, sql::Error>
    where
        F: for<'row> FnMut(sql::Row<'row>) -> Result<()>,
    {
        if let Some(error) = self.error {
            return Err(error);
        }
        let result = match self.sql {
            Some(raw) => unsafe {
                sql::execute_api(
                    raw.sql_api,
                    raw.v2.query_context,
                    statement,
                    parameters,
                    max_rows,
                    consumer,
                )
            },
            None => Err(sys::UNAVAILABLE.into()),
        };
        if let Err(error) = result {
            self.error = Some(error);
        }
        result
    }
}

pub struct Rows<'a> {
    context: &'a sys::TableContext,
    query: QueryContext<'a>,
    maximum: u32,
    emitted: u32,
    error: Option<sys::Status>,
    _thread: PhantomData<Rc<()>>,
}
impl Rows<'_> {
    pub fn projection_column_count(&self) -> Option<usize> {
        self.query.projection_column_count()
    }
    pub fn column_requested(&self, index: usize) -> Result<bool> {
        self.query.column_requested(index)
    }
    pub fn supports_query_control(&self) -> bool {
        self.query.supports_query_control()
    }
    /// Cooperatively check the host query between bounded work chunks. No SQL
    /// or retained cursor token. Errors poison emit/next even when ignored.
    pub fn poll_query(&mut self) -> std::result::Result<Option<std::time::Duration>, sql::Error> {
        if let Some(error) = self.query.error {
            return Err(error);
        }
        if let Some(error) = self.error {
            return Err(error.into());
        }
        let result = self.query.poll_query();
        if let Err(error) = result {
            self.error = Some(error.status);
        }
        result
    }
    pub fn supports_sql(&self) -> bool {
        self.query.supports_sql()
    }
    pub fn supports_catalog_lookup(&self) -> bool {
        self.query.supports_catalog_lookup()
    }
    pub fn supports_catalog_mutation(&self) -> bool {
        self.query.supports_catalog_mutation()
    }
    pub fn mutate_routine(
        &mut self,
        statement: &str,
    ) -> std::result::Result<Option<u64>, sql::Error> {
        if let Some(error) = self.query.error {
            return Err(error);
        }
        if let Some(error) = self.error {
            return Err(error.into());
        }
        let result = self.query.mutate_routine(statement);
        if let Err(error) = result {
            self.error = Some(error.status);
        }
        result
    }
    pub fn lookup_routine(
        &mut self,
        kind: sql::RoutineKind,
        name: &str,
    ) -> std::result::Result<Option<u64>, sql::Error> {
        if let Some(error) = self.query.error {
            return Err(error);
        }
        if let Some(error) = self.error {
            return Err(error.into());
        }
        let result = self.query.lookup_routine(kind, name);
        if let Err(error) = result {
            self.error = Some(error.status);
        }
        result
    }
    pub fn execute_sql<F>(
        &mut self,
        statement: &str,
        parameters: &[sql::Value<'_>],
        max_rows: u64,
        consumer: F,
    ) -> std::result::Result<sql::Outcome, sql::Error>
    where
        F: for<'row> FnMut(sql::Row<'row>) -> Result<()>,
    {
        if let Some(error) = self.query.error {
            return Err(error);
        }
        if let Some(error) = self.error {
            return Err(error.into());
        }
        let result = self
            .query
            .execute_sql(statement, parameters, max_rows, consumer);
        if let Err(error) = result {
            self.error = Some(error.status);
        }
        result
    }
    pub fn remaining(&self) -> u32 {
        self.maximum - self.emitted
    }
    pub fn emit(&mut self, cells: &[Cell<'_>]) -> Result<()> {
        if let Some(error) = self.error {
            return Err(error);
        }
        let result = self.emit_inner(cells);
        if let Err(error) = result {
            self.error = Some(error);
        }
        result
    }
    fn emit_inner(&mut self, cells: &[Cell<'_>]) -> Result<()> {
        if self.remaining() == 0 || cells.is_empty() || cells.len() > 4096 {
            return Err(sys::INVALID);
        }
        if self
            .projection_column_count()
            .is_some_and(|count| count != cells.len())
        {
            return Err(sys::INVALID);
        }
        let mut values = Vec::new();
        values
            .try_reserve_exact(cells.len())
            .map_err(|_| sys::NO_MEMORY)?;
        let mut bytes = 0usize;
        for cell in cells {
            result_type::validate_type(cell.type_id)?;
            bytes = bytes
                .checked_add(cell.bytes.map_or(0, <[u8]>::len))
                .ok_or(sys::INVALID)?;
            if bytes > 16 * 1024 * 1024 {
                return Err(sys::INVALID);
            }
            values.push(sys::Value {
                struct_size: size_of::<sys::Value>() as u32,
                type_id: cell.type_id.as_ptr(),
                data: cell.bytes.map_or(ptr::null(), <[u8]>::as_ptr),
                data_size: cell.bytes.map_or(0, |b| b.len() as u64),
                is_null: u8::from(cell.bytes.is_none()),
                reserved_bytes: [0; 7],
                reserved: [0; 4],
            });
        }
        let row = sys::TableRow {
            struct_size: size_of::<sys::TableRow>() as u32,
            columns: values.as_ptr(),
            column_count: values.len() as u32,
            reserved_word: 0,
            reserved: [0; 4],
        };
        status(unsafe { self.context.emit_row.unwrap()(self.context.host, &row) })?;
        self.emitted += 1;
        Ok(())
    }
}

/// Own all state used after open, including copies of input bytes. `Send` permits
/// serial calls on different host workers; it does not permit concurrent calls.
/// Emit up to `Rows::remaining()` rows. Returning success with no rows marks EOF.
/// Query-local errors/panics poison the cursor until a successful rescan or close.
pub trait Cursor: Sized + Send + 'static {
    fn validate_instance(instance: *mut sys::Handle) -> Result<()>;
    fn open(instance: *mut sys::Handle, arguments: &Arguments<'_>) -> Result<Self>;
    /// Override for SQL/query control during open. Legacy raw rescan still
    /// calls open without a context; SQL-dependent cursors can reject that path.
    fn open_with_context(
        instance: *mut sys::Handle,
        arguments: &Arguments<'_>,
        _query: &mut QueryContext<'_>,
    ) -> Result<Self> {
        Self::open(instance, arguments)
    }
    fn next(&mut self, rows: &mut Rows<'_>) -> Result<()>;
}
struct State<C> {
    cursor: C,
    failed: bool,
    ended: bool,
    instance: *mut sys::Handle,
    requires_sql: bool,
}
pub struct Service<C>(PhantomData<C>);
impl<C: Cursor> Service<C> {
    /// Request optional projection/query metadata; older contexts mean all
    /// columns required. SQL is optional, as for WITH_QUERY_CONTROL.
    pub const WITH_PROJECTION: sys::TableFunctionServiceV2 = {
        let mut service = Self::WITH_QUERY_CONTROL;
        service.v1.spi_minor = 4;
        service
    };
    /// Require SQL while optionally using projection metadata.
    pub const WITH_SQL_AND_PROJECTION: sys::TableFunctionServiceV2 = {
        let mut service = Self::WITH_SQL;
        service.v1.spi_minor = 4;
        service
    };
    /// Require SQL contexts for open and every next; no silent legacy fallback.
    /// Estimates remain optional and use the host defaults unless supplied.
    pub const WITH_SQL: sys::TableFunctionServiceV2 = {
        let mut service = Self::WITH_QUERY_CONTROL;
        service.v1.spi_minor = 3;
        service.v1.open = Some(Self::open_sql);
        service
    };
    /// Request optional query-control contexts without requiring a planner.
    /// The null estimate selects host defaults on table SPI minor-2 hosts.
    pub const WITH_QUERY_CONTROL: sys::TableFunctionServiceV2 = {
        let mut v1 = Self::ABI;
        v1.struct_size = size_of::<sys::TableFunctionServiceV2>() as u32;
        v1.spi_minor = 2;
        sys::TableFunctionServiceV2 {
            v1,
            estimate: None,
            reserved: [0; 4],
        }
    };
    /// The public service's unsafe callbacks require host-owned live ABI inputs,
    /// a cursor from this exact service, exclusive access, and exactly one close.
    /// Host emit callbacks must consume rows synchronously without reentry or unwind.
    /// Close remains available after instance stop so resource release is possible.
    pub const ABI: sys::TableFunctionService = sys::TableFunctionService {
        struct_size: size_of::<sys::TableFunctionService>() as u32,
        spi_major: 1,
        spi_minor: 0,
        reserved_word: 0,
        open: Some(Self::open),
        next: Some(Self::next),
        rescan: Some(Self::rescan),
        close: Some(Self::close),
        reserved: [0; 8],
    };
    unsafe fn context<'a>(context: *const sys::TableContext) -> Result<&'a sys::TableContext> {
        if context.is_null() {
            return Err(sys::INVALID);
        }
        if unsafe { (*context).struct_size } < size_of::<sys::TableContext>() as u32 {
            return Err(sys::UNSUPPORTED_ABI);
        }
        let context = unsafe { &*context };
        if context.emit_row.is_none() || context.reserved != [0; 6] {
            return Err(sys::INVALID);
        }
        Ok(context)
    }
    unsafe extern "C" fn open(
        instance: *mut sys::Handle,
        context: *const sys::TableContext,
        values: *const sys::Value,
        count: u32,
        out: *mut *mut sys::Handle,
    ) -> sys::Status {
        unsafe { Self::open_impl(instance, context, values, count, out, false) }
    }
    unsafe extern "C" fn open_sql(
        instance: *mut sys::Handle,
        context: *const sys::TableContext,
        values: *const sys::Value,
        count: u32,
        out: *mut *mut sys::Handle,
    ) -> sys::Status {
        unsafe { Self::open_impl(instance, context, values, count, out, true) }
    }
    unsafe fn open_impl(
        instance: *mut sys::Handle,
        context: *const sys::TableContext,
        values: *const sys::Value,
        count: u32,
        out: *mut *mut sys::Handle,
        requires_sql: bool,
    ) -> sys::Status {
        boundary(|| {
            if out.is_null() {
                return Err(sys::INVALID);
            }
            unsafe { *out = ptr::null_mut() };
            C::validate_instance(instance)?;
            unsafe { Self::context(context) }?;
            let mut query = unsafe { QueryContext::from_raw(context) }?;
            if requires_sql {
                query.require_sql()?;
            }
            let arguments = unsafe { Arguments::from_raw(values, count) }?;
            let result = C::open_with_context(instance, &arguments, &mut query);
            if let Some(error) = query.error {
                return Err(error.status);
            }
            let cursor = result?;
            let state = Box::new(State {
                cursor,
                failed: false,
                ended: false,
                instance,
                requires_sql,
            });
            unsafe { *out = Box::into_raw(state).cast() };
            Ok(())
        })
    }
    unsafe extern "C" fn next(
        instance: *mut sys::Handle,
        cursor: *mut sys::Handle,
        context: *const sys::TableContext,
        maximum: u32,
        out: *mut u32,
    ) -> sys::Status {
        boundary(|| {
            if out.is_null() {
                return Err(sys::INVALID);
            }
            unsafe { *out = 0 };
            C::validate_instance(instance)?;
            if cursor.is_null() || maximum == 0 {
                return Err(sys::INVALID);
            }
            let state = unsafe { &mut *cursor.cast::<State<C>>() };
            if state.instance != instance || state.failed {
                return Err(sys::FAILED_PRECONDITION);
            }
            let raw_context = context;
            let context = unsafe { Self::context(raw_context) }?;
            let query = unsafe { QueryContext::from_raw(raw_context) }?;
            if state.requires_sql {
                if let Err(error) = query.require_sql() {
                    state.failed = true;
                    return Err(error);
                }
            }
            if state.ended {
                return Err(sys::END_OF_STREAM);
            }
            state.failed = true;
            let mut rows = Rows {
                context,
                query,
                maximum,
                emitted: 0,
                error: None,
                _thread: PhantomData,
            };
            let result = state.cursor.next(&mut rows);
            unsafe { *out = rows.emitted };
            if let Some(error) = rows.error {
                return Err(error);
            }
            result?;
            state.failed = false;
            state.ended = rows.emitted == 0;
            if state.ended {
                Err(sys::END_OF_STREAM)
            } else {
                Ok(())
            }
        })
    }
    unsafe extern "C" fn rescan(
        instance: *mut sys::Handle,
        cursor: *mut sys::Handle,
        values: *const sys::Value,
        count: u32,
    ) -> sys::Status {
        boundary(|| {
            C::validate_instance(instance)?;
            if cursor.is_null() {
                return Err(sys::INVALID);
            }
            let state = unsafe { &mut *cursor.cast::<State<C>>() };
            if state.instance != instance {
                return Err(sys::FAILED_PRECONDITION);
            }
            state.failed = true;
            let arguments = unsafe { Arguments::from_raw(values, count) }?;
            let replacement = C::open(instance, &arguments)?;
            drop(std::mem::replace(&mut state.cursor, replacement));
            state.failed = false;
            state.ended = false;
            Ok(())
        })
    }
    unsafe extern "C" fn close(
        instance: *mut sys::Handle,
        cursor: *mut sys::Handle,
    ) -> sys::Status {
        boundary(|| {
            if cursor.is_null() {
                return Err(sys::INVALID);
            }
            if unsafe { (*cursor.cast::<State<C>>()).instance } != instance {
                return Err(sys::FAILED_PRECONDITION);
            }
            drop(unsafe { Box::from_raw(cursor.cast::<State<C>>()) });
            Ok(())
        })
    }
}
