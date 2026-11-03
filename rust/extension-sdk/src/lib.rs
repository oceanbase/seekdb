// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Public-ABI plugin SDK. Host runtime internals are deliberately not linked.
//! Query wrappers borrow callback-scoped data and are neither Send nor Sync.
//! Explicit owned-byte tokens separately support cross-thread data ownership.
#![deny(unsafe_op_in_unsafe_fn, improper_ctypes_definitions)]

pub mod batch;
pub mod candidate;
pub mod catalog;
pub mod custom_executor;
pub mod memory;
pub mod optimizer;
mod result_type;
mod scalar;
pub mod schema;
pub mod server_dev;
pub mod sql;
pub mod sys;
pub mod table;
pub mod table_planning;
pub mod type_comparison;
pub use result_type::TypeResolution;
pub use scalar::invoke_scalar;

use std::ffi::CStr;
use std::marker::PhantomData;
use std::mem::size_of;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::rc::Rc;
use sys::Status;

pub type Result<T> = std::result::Result<T, Status>;
mod query_catalog;

fn status(value: Status) -> Result<()> {
    if value == sys::OK {
        Ok(())
    } else {
        Err(value)
    }
}

/// Run inside every status-returning FFI entry. Only unwind-mode panics can be
/// caught; abort/OOM/memory corruption are not recoverable. A plugin mutating
/// shared state must independently poison or repair that state after failure.
pub fn boundary(f: impl FnOnce() -> Result<()>) -> Status {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(Ok(())) => sys::OK,
        Ok(Err(error)) => {
            if error == sys::OK {
                sys::INTERNAL
            } else {
                error
            }
        }
        Err(payload) => {
            // An arbitrary panic_any payload may itself panic on Drop. Dispose
            // it inside another boundary; never unwind across the C entry.
            if let Err(nested) = catch_unwind(AssertUnwindSafe(|| drop(payload))) {
                std::mem::forget(nested);
            }
            sys::INTERNAL
        }
    }
}

/// Borrowed registration data: names may be computed during init/start and do
/// not need static storage, since the host copies them before returning.
pub struct FunctionDefinition<'definition> {
    pub object_id: &'definition CStr,
    pub sql_name: &'definition CStr,
    pub argument_types: &'definition [&'definition CStr],
    pub result_type: &'definition CStr,
    pub service_id: &'definition CStr,
    pub minimum_version: sys::Version,
    pub maximum_version_exclusive: sys::Version,
    pub required_capabilities: u64,
    pub flags: u64,
}

/// A scalar whose result is determined by FunctionServiceV2::resolve_result.
/// None is an untyped arity envelope (not a named polymorphic SQL type).
/// A typed signature is coerced by the host before both resolution and execute.
pub struct DynamicFunctionDefinition<'definition> {
    pub object_id: &'definition CStr,
    pub sql_name: &'definition CStr,
    pub argument_types: Option<&'definition [&'definition CStr]>,
    pub minimum_arity: u32,
    pub maximum_arity: u32,
    pub variadic: bool,
    pub implementation: ImplementationReference<'definition>,
    pub flags: u64,
}

/// A named, leased implementation, not a callback pointer. The corresponding
/// service table must remain valid until the host finishes module shutdown.
pub struct ImplementationReference<'definition> {
    pub service_id: &'definition CStr,
    pub minimum_version: sys::Version,
    pub maximum_version_exclusive: sys::Version,
    pub required_capabilities: u64,
}

impl ImplementationReference<'_> {
    fn raw(&self) -> sys::Implementation {
        sys::Implementation {
            struct_size: size_of::<sys::Implementation>() as u32,
            service_id: self.service_id.as_ptr(),
            version_range: sys::VersionRange {
                struct_size: size_of::<sys::VersionRange>() as u32,
                minimum_inclusive: self.minimum_version,
                maximum_exclusive: self.maximum_version_exclusive,
                reserved: [0; 2],
            },
            required_capabilities: self.required_capabilities,
            reserved: [0; 4],
        }
    }
}

/// Public byte-oriented type registration, not a new physical storage protocol.
/// Host catalog admission still validates names, formats and dependencies.
pub struct TypeDefinition<'definition> {
    pub object_id: &'definition CStr,
    pub sql_name: &'definition CStr,
    pub physical_format_id: &'definition CStr,
    pub physical_format_version: u32,
    pub flags: u64,
    pub codec: ImplementationReference<'definition>,
}

#[repr(i32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CastContext {
    Explicit = 1,
    Assignment = 2,
    Implicit = 3,
}

pub struct CastDefinition<'definition> {
    pub object_id: &'definition CStr,
    pub source_type_id: &'definition CStr,
    pub target_type_id: &'definition CStr,
    pub context: CastContext,
    pub cost: u32,
    pub flags: u64,
    pub implementation: ImplementationReference<'definition>,
}

pub struct Registration<'host> {
    api: &'host sys::HostApiV2,
    token: *mut sys::Handle,
    _thread_bound: PhantomData<Rc<()>>,
}

impl<'host> Registration<'host> {
    /// # Safety
    /// host is a valid, aligned host API table with at least its advertised
    /// readable size, alive for 'host. Called only in init/start on the owner
    /// thread. Host callbacks must satisfy the public no-unwinding contract.
    pub unsafe fn begin(host: *const sys::HostApiV1) -> Result<Self> {
        if host.is_null() {
            return Err(sys::INVALID);
        }
        if unsafe { (*host).struct_size } < size_of::<sys::HostApiV2>() as u32 {
            return Err(sys::UNSUPPORTED_ABI);
        }
        let api = unsafe { &*host.cast::<sys::HostApiV2>() };
        if api.host.abi_major != 1
            || api.registration_spi_major != 1
            || api.host.commit_registration.is_none()
            || api.host.abort_registration.is_none()
            || api.register_extension.is_none()
        {
            return Err(sys::UNSUPPORTED_ABI);
        }
        let begin = api.host.begin_registration.ok_or(sys::UNSUPPORTED_ABI)?;
        let mut token = ptr::null_mut();
        status(unsafe { begin(api.host.host_handle, &mut token) })?;
        if token.is_null() {
            return Err(sys::INTERNAL);
        }
        Ok(Self {
            api,
            token,
            _thread_bound: PhantomData,
        })
    }

    /// The implementation service must be contributed by the manifest. The
    /// direct registration ABI deep-copies descriptor data before returning.
    pub fn function(&mut self, definition: &FunctionDefinition<'_>) -> Result<()> {
        if definition.argument_types.len() > 1024 {
            return Err(sys::INVALID);
        }
        let mut types = Vec::new();
        types
            .try_reserve_exact(definition.argument_types.len())
            .map_err(|_| sys::NO_MEMORY)?;
        types.extend(definition.argument_types.iter().map(|name| name.as_ptr()));
        let descriptor = sys::FunctionV2 {
            descriptor: sys::FunctionV1 {
                struct_size: size_of::<sys::FunctionV2>() as u32,
                object_id: definition.object_id.as_ptr(),
                sql_name: definition.sql_name.as_ptr(),
                minimum_arity: types.len() as u32,
                maximum_arity: types.len() as u32,
                static_result_type_id: definition.result_type.as_ptr(),
                flags: definition.flags,
                implementation: ImplementationReference {
                    service_id: definition.service_id,
                    minimum_version: definition.minimum_version,
                    maximum_version_exclusive: definition.maximum_version_exclusive,
                    required_capabilities: definition.required_capabilities,
                }
                .raw(),
                reserved: [0; 4],
            },
            argument_type_ids: types.as_ptr(),
            argument_type_count: types.len() as u32,
            signature_flags: 0,
            signature_reserved: [0; 4],
        };
        self.register_descriptor(sys::FUNCTION, &descriptor)
    }

    /// Register a metadata-resolved scalar in this same transaction. The
    /// service must expose minor 2 and the complete FunctionServiceV2 suffix.
    pub fn dynamic_function(&mut self, definition: &DynamicFunctionDefinition<'_>) -> Result<()> {
        let signature = definition.argument_types.unwrap_or(&[]);
        if definition.minimum_arity > definition.maximum_arity
            || definition.maximum_arity > 1024
            || (definition.argument_types.is_some()
                && signature.is_empty()
                && definition.maximum_arity != 0)
            || signature.len() > definition.maximum_arity as usize
            || (definition.variadic && signature.is_empty())
            || (!signature.is_empty()
                && !definition.variadic
                && signature.len() != definition.maximum_arity as usize)
        {
            return Err(sys::INVALID);
        }
        let mut types = Vec::new();
        types
            .try_reserve_exact(signature.len())
            .map_err(|_| sys::NO_MEMORY)?;
        types.extend(signature.iter().map(|id| id.as_ptr()));
        let descriptor = sys::FunctionV2 {
            descriptor: sys::FunctionV1 {
                struct_size: size_of::<sys::FunctionV2>() as u32,
                object_id: definition.object_id.as_ptr(),
                sql_name: definition.sql_name.as_ptr(),
                minimum_arity: definition.minimum_arity,
                maximum_arity: definition.maximum_arity,
                static_result_type_id: ptr::null(),
                flags: definition.flags,
                implementation: definition.implementation.raw(),
                reserved: [0; 4],
            },
            argument_type_ids: types.as_ptr(),
            argument_type_count: types.len() as u32,
            signature_flags: u32::from(definition.variadic),
            signature_reserved: [0; 4],
        };
        self.register_descriptor(sys::FUNCTION, &descriptor)
    }

    /// Stage a type in this same registration transaction. The host copies the
    /// descriptor and strings synchronously; staging does not publish SQL state.
    pub fn data_type(&mut self, definition: &TypeDefinition<'_>) -> Result<()> {
        let descriptor = sys::TypeDescriptor {
            struct_size: size_of::<sys::TypeDescriptor>() as u32,
            object_id: definition.object_id.as_ptr(),
            sql_name: definition.sql_name.as_ptr(),
            physical_format_id: definition.physical_format_id.as_ptr(),
            physical_format_version: definition.physical_format_version,
            reserved_word: 0,
            flags: definition.flags,
            codec_service: definition.codec.raw(),
            reserved: [0; 4],
        };
        self.register_descriptor(sys::TYPE, &descriptor)
    }

    /// Stage a conversion with an explicit plugin-selected context and cost.
    /// This does not enable new resolver coercion rules beyond the host SPI.
    pub fn cast(&mut self, definition: &CastDefinition<'_>) -> Result<()> {
        let descriptor = sys::CastDescriptor {
            struct_size: size_of::<sys::CastDescriptor>() as u32,
            object_id: definition.object_id.as_ptr(),
            source_type_id: definition.source_type_id.as_ptr(),
            target_type_id: definition.target_type_id.as_ptr(),
            context: definition.context as i32,
            cost: definition.cost,
            flags: definition.flags,
            implementation: definition.implementation.raw(),
            reserved: [0; 4],
        };
        self.register_descriptor(sys::CAST, &descriptor)
    }

    // Only fixed public ABI structs constructed above reach this helper.
    fn register_descriptor<T>(&mut self, kind: i32, descriptor: &T) -> Result<()> {
        status(unsafe {
            self.api.register_extension.unwrap()(
                self.api.host.host_handle,
                self.token,
                kind,
                (descriptor as *const T).cast(),
                size_of::<T>() as u32,
            )
        })
    }

    /// Commit stages contributions; catalog activation still controls visibility.
    /// A failed commit is aborted by Drop, as required by the public ABI.
    pub fn commit(mut self) -> Result<()> {
        status(unsafe {
            self.api.host.commit_registration.unwrap()(self.api.host.host_handle, self.token)
        })?;
        self.token = ptr::null_mut();
        Ok(())
    }
}

impl Drop for Registration<'_> {
    fn drop(&mut self) {
        if !self.token.is_null() {
            unsafe {
                self.api.host.abort_registration.unwrap()(self.api.host.host_handle, self.token)
            };
        }
    }
}

pub struct Call<'query> {
    context: &'query sys::ContextV1,
    extended_context: Option<&'query sys::ContextV2>,
    arguments: &'query [sys::Value],
    emitted: bool,
    _thread_bound: PhantomData<Rc<()>>,
}

impl<'query> Call<'query> {
    /// # Safety
    /// context/arguments and all borrowed pointees are host-owned, correctly
    /// aligned and valid for 'query (no longer than this synchronous callback).
    /// The context backing allocation has its advertised size. No arbitrary
    /// foreign threads may use it; callbacks cannot unwind across the C ABI.
    /// SQL execution must obey sql_spi.h: valid borrowed rows, synchronous and
    /// nonrecursive consumer calls on this thread, and no retained consumer.
    pub unsafe fn from_raw(
        context: *const sys::ContextV1,
        arguments: *const sys::Value,
        count: u32,
    ) -> Result<Self> {
        if context.is_null() || count > 1024 || (count != 0 && arguments.is_null()) {
            return Err(sys::INVALID);
        }
        if unsafe { (*context).struct_size } < size_of::<sys::ContextV1>() as u32 {
            return Err(sys::UNSUPPORTED_ABI);
        }
        let extended_context =
            if unsafe { (*context).struct_size } >= size_of::<sys::ContextV2>() as u32 {
                Some(unsafe { &*context.cast::<sys::ContextV2>() })
            } else {
                None
            };
        let context = unsafe { &*context };
        if context.emit_result.is_none() {
            return Err(sys::INVALID);
        }
        let arguments = if count == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(arguments, count as usize) }
        };
        if arguments
            .iter()
            .any(|value| value.struct_size < size_of::<sys::Value>() as u32)
        {
            return Err(sys::INVALID);
        }
        Ok(Self {
            context,
            extended_context,
            arguments,
            emitted: false,
            _thread_bound: PhantomData,
        })
    }

    pub fn argument_count(&self) -> usize {
        self.arguments.len()
    }

    /// Whether this host offers cooperative query cancellation/deadline checks.
    pub fn supports_query_control(&self) -> bool {
        sql::query_control(self.extended_context).is_ok()
    }

    /// Check query cancellation/timeout between bounded computation or I/O
    /// chunks. None is an unlimited deadline; a duration is only a snapshot,
    /// not a reservation. This neither executes SQL nor interrupts a thread.
    pub fn poll_query(&mut self) -> std::result::Result<Option<std::time::Duration>, sql::Error> {
        sql::poll_query(self.extended_context)
    }

    /// Borrow the logical type, including a typed NULL. None is an unknown NULL
    /// type, not an empty identifier. The callback's input lifetime still applies.
    pub fn argument_type(&self, index: usize) -> Result<Option<&'query CStr>> {
        let value = self.arguments.get(index).ok_or(sys::INVALID)?;
        if value.type_id.is_null() {
            return if value.is_null != 0 {
                Ok(None)
            } else {
                Err(sys::INVALID)
            };
        }
        let id = unsafe { CStr::from_ptr(value.type_id) };
        result_type::validate_type(id)?;
        Ok(Some(id))
    }

    pub fn text(&self, index: usize) -> Result<Option<&'query str>> {
        self.bytes(index, c"core.type.bytes")?
            .map(|bytes| std::str::from_utf8(bytes).map_err(|_| sys::INVALID))
            .transpose()
    }

    /// Borrow a value with a declared logical type ID, including plugin types.
    /// NULL remains NULL regardless of its payload/type metadata.
    pub fn bytes(&self, index: usize, type_id: &CStr) -> Result<Option<&'query [u8]>> {
        let value = self.arguments.get(index).ok_or(sys::INVALID)?;
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
        let bytes = if value.data_size == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(value.data, value.data_size as usize) }
        };
        Ok(Some(bytes))
    }

    pub fn emit_i64(&mut self, value: Option<i64>) -> Result<()> {
        let bytes = value.map(i64::to_ne_bytes);
        self.emit_bytes(
            c"core.type.int64",
            bytes.as_ref().map(|bytes| bytes.as_slice()),
        )
    }

    /// Emit byte-oriented data under its logical type ID. The host validates
    /// the bound result type and synchronously copies it; no buffer is retained.
    pub fn emit_bytes(&mut self, type_id: &CStr, bytes: Option<&[u8]>) -> Result<()> {
        if self.emitted {
            return Err(sys::FAILED_PRECONDITION);
        }
        if bytes.is_some_and(|bytes| bytes.len() > 16 * 1024 * 1024) {
            return Err(sys::INVALID);
        }
        let result = sys::Value {
            struct_size: size_of::<sys::Value>() as u32,
            type_id: type_id.as_ptr(),
            data: bytes.map_or(ptr::null(), |bytes| bytes.as_ptr()),
            data_size: bytes.map_or(0, |bytes| bytes.len() as u64),
            is_null: u8::from(bytes.is_none()),
            reserved_bytes: [0; 7],
            reserved: [0; 4],
        };
        self.emitted = true;
        status(unsafe { self.context.emit_result.unwrap()(self.context.host, &result) })
    }

    pub fn supports_catalog_lookup(&self) -> bool {
        query_catalog::context(self.extended_context)
            .and_then(|(api, handle)| unsafe { query_catalog::callback(api, handle) })
            .is_ok()
    }

    /// Look up a standalone routine in the current database and caller's schema
    /// view with normal SHOW visibility. None means absent, not permission denied.
    /// The ID is a snapshot only, not a lease, dependency or execution privilege.
    /// No DDL, transaction start/commit or fresh schema snapshot is performed.
    pub fn lookup_routine(
        &mut self,
        kind: sql::RoutineKind,
        name: &str,
    ) -> std::result::Result<Option<u64>, sql::Error> {
        let (api, handle) = query_catalog::context(self.extended_context)?;
        unsafe { query_catalog::lookup(api, handle, kind, name) }
    }

    pub fn supports_catalog_mutation(&self) -> bool {
        query_catalog::context(self.extended_context)
            .and_then(|(api, handle)| unsafe { query_catalog::mutation_callback(api, handle) })
            .is_ok()
    }

    /// Execute one routine CREATE / attribute ALTER / DROP in the caller's
    /// transaction, with ordinary permissions and dependency checks. The ID
    /// is provisional until the OUTER transaction commits; None is an absent
    /// DROP IF EXISTS. No implicit commit or Extension member adoption.
    /// Propagate errors: they fail the invocation, even after operation rollback.
    /// SQL is UTF-8, <=4 MiB, without NUL. Other DDL is not supported yet.
    pub fn mutate_routine(
        &mut self,
        statement: &str,
    ) -> std::result::Result<Option<u64>, sql::Error> {
        let (api, handle) = query_catalog::context(self.extended_context)?;
        unsafe { query_catalog::mutate(api, handle, statement) }
    }

    /// Execute parameterized SELECT or DML as the current caller, consuming rows
    /// synchronously. max_rows is an error limit, not silent truncation; zero is
    /// useful for DML that must not return rows. No commit/DDL/background access.
    /// The mutable borrow prevents reentering this Call from the consumer.
    ///
    /// ```compile_fail
    /// use seekdb_extension::Call;
    /// fn reenter(call: &mut Call<'_>) {
    ///     call.execute_sql("SELECT 1", &[], 1, |_| {
    ///         call.query_i64("SELECT ?", None)?;
    ///         Ok(())
    ///     }).unwrap();
    /// }
    /// ```
    pub fn execute_sql<F>(
        &mut self,
        sql: &str,
        parameters: &[sql::Value<'_>],
        max_rows: u64,
        consumer: F,
    ) -> std::result::Result<sql::Outcome, sql::Error>
    where
        F: for<'row> FnMut(sql::Row<'row>) -> Result<()>,
    {
        sql::execute(self.extended_context, sql, parameters, max_rows, consumer)
    }

    /// Convenience scalar query with one nullable UTF-8 parameter. Use
    /// execute_sql for other types, multiple rows/parameters, or database errors.
    pub fn query_i64(&mut self, sql: &str, text: Option<&str>) -> Result<Option<i64>> {
        let mut value = None;
        let outcome = self
            .execute_sql(
                sql,
                &[text.map_or(sql::Value::Null, sql::Value::Text)],
                1,
                |row| {
                    if row.len() != 1 {
                        return Err(sys::INVALID);
                    }
                    value = match row.get(0)? {
                        sql::Value::Null => None,
                        sql::Value::I64(v) => Some(v),
                        sql::Value::U64(v) => Some(i64::try_from(v).map_err(|_| sys::INVALID)?),
                        _ => return Err(sys::INVALID),
                    };
                    Ok(())
                },
            )
            .map_err(|error| error.status)?;
        if outcome.returned_rows != 1 {
            return Err(sys::INTERNAL);
        }
        Ok(value)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn boundary_converts_results_and_panics() {
        assert_eq!(boundary(|| Ok(())), sys::OK);
        assert_eq!(boundary(|| Err(sys::INVALID)), sys::INVALID);
        assert_eq!(boundary(|| Err(sys::OK)), sys::INTERNAL);
        assert_eq!(boundary(|| panic!("query-local panic")), sys::INTERNAL);
        struct BadDrop;
        impl Drop for BadDrop {
            fn drop(&mut self) {
                panic!("payload drop panic");
            }
        }
        assert_eq!(boundary(|| std::panic::panic_any(BadDrop)), sys::INTERNAL);
    }
}
