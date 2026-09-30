// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Installation preparation and transaction-local object construction/lookup.
//! Emission stages SQL; build calls use the host's one schema transaction.
use crate::{boundary, status, sys, Result};
use std::{ffi::CStr, marker::PhantomData, mem::size_of, rc::Rc};

/// Borrowed preparation context, deliberately without query/transaction APIs.
/// Do not perform external side effects, background tasks or retain this view.
/// ```compile_fail
/// fn send(context: seekdb_extension::catalog::CatalogContext<'static>) {
///     std::thread::spawn(move || drop(context));
/// }
/// ```
pub struct CatalogContext<'a> {
    raw: &'a sys::CatalogContext,
    name: &'a str,
    version: &'a str,
    error: Option<sys::Status>,
    _thread: PhantomData<Rc<()>>,
}
impl CatalogContext<'_> {
    pub fn tenant_id(&self) -> u64 {
        self.raw.tenant_id
    }
    pub fn database_id(&self) -> u64 {
        self.raw.database_id
    }
    pub fn owner_id(&self) -> u64 {
        self.raw.owner_id
    }
    pub fn extension_name(&self) -> &str {
        self.name
    }
    pub fn extension_version(&self) -> &str {
        self.version
    }
    /// Stage a complete SQL fragment; host copies it before returning. Separate
    /// calls remain separate parser inputs (including trailing line comments).
    /// Unsupported DDL can parse successfully but still fail install preflight.
    pub fn declare_sql(&mut self, sql: &str) -> Result<()> {
        if let Some(error) = self.error {
            return Err(error);
        }
        let result = if sql.trim().is_empty() || sql.len() > 4 * 1024 * 1024 || sql.contains('\0') {
            Err(sys::INVALID)
        } else {
            let callback = self.raw.emit_sql.ok_or(sys::UNSUPPORTED_ABI)?;
            status(unsafe {
                callback(self.raw.host_context, sql.as_ptr().cast(), sql.len() as u64)
            })
        };
        if let Err(error) = result {
            self.error = Some(error);
        }
        result
    }
}

pub trait Installer {
    /// Must enforce module lifecycle/instance admission before preparing SQL.
    fn prepare(instance: *mut sys::Handle, context: &mut CatalogContext<'_>) -> Result<()>;
}

/// Identity observed in a build view, possibly reserved by this installation.
/// Not evidence of commit. Never persist/use a newly created ID after failure.
/// The lifetime prevents safe escape from the build view.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct RoutineId<'txn> {
    value: u64,
    _transaction: PhantomData<&'txn ()>,
}
impl RoutineId<'_> {
    pub fn value(self) -> u64 {
        self.value
    }
}

/// Standalone routine namespaces, not SQL keywords or arbitrary object kinds.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RoutineKind {
    Function,
    Procedure,
}

/// Transaction-local construction, not query SQL or transaction ownership.
/// ```compile_fail
/// fn send(context: seekdb_extension::catalog::TransactionContext<'static>) {
///     std::thread::spawn(move || drop(context));
/// }
/// ```
/// ```compile_fail
/// fn escape(context: &mut seekdb_extension::catalog::TransactionContext<'_>)
///     -> seekdb_extension::catalog::RoutineId<'static> {
///     context.create_routine("CREATE FUNCTION f() RETURNS INT RETURN 1;").unwrap()
/// }
/// ```
/// ```compile_fail
/// fn escape(context: &mut seekdb_extension::catalog::TransactionContext<'_>)
///     -> seekdb_extension::catalog::RoutineId<'static> {
///     context.lookup_routine(seekdb_extension::catalog::RoutineKind::Function, "f").unwrap().unwrap()
/// }
/// ```
pub struct TransactionContext<'txn> {
    raw: &'txn sys::CatalogBuildContext,
    lookup: Option<&'txn sys::CatalogBuildContextV2>,
    name: &'txn str,
    version: &'txn str,
    error: Option<sys::Status>,
    _thread: PhantomData<Rc<()>>,
}
impl<'txn> TransactionContext<'txn> {
    pub fn tenant_id(&self) -> u64 {
        self.raw.tenant_id
    }
    pub fn database_id(&self) -> u64 {
        self.raw.database_id
    }
    pub fn owner_id(&self) -> u64 {
        self.raw.owner_id
    }
    pub fn extension_name(&self) -> &str {
        self.name
    }
    pub fn extension_version(&self) -> &str {
        self.version
    }
    /// Size-negotiated optional capability; querying it does not fail the build.
    pub fn supports_lookup(&self) -> bool {
        self.lookup.is_some_and(|raw| raw.lookup_routine.is_some())
    }
    /// Find a visible standalone routine by its unquoted name in this build's
    /// database. Host collation applies. None means absent, not a build error.
    /// The ID is bound to this view (possibly provisional); it does not confer
    /// execution privilege or automatically record membership/dependencies.
    pub fn lookup_routine(
        &mut self,
        kind: RoutineKind,
        name: &str,
    ) -> Result<Option<RoutineId<'txn>>> {
        if let Some(error) = self.error {
            return Err(error);
        }
        let mut id = 0;
        let result = if name.is_empty()
            || name.len() > sys::CATALOG_MAX_ROUTINE_NAME_BYTES
            || name.contains('\0')
        {
            Err(sys::INVALID)
        } else if let Some(lookup) = self.lookup.and_then(|raw| raw.lookup_routine) {
            let kind = match kind {
                RoutineKind::Function => sys::CATALOG_ROUTINE_FUNCTION,
                RoutineKind::Procedure => sys::CATALOG_ROUTINE_PROCEDURE,
            };
            status(unsafe {
                lookup(
                    self.raw.host_context,
                    kind,
                    name.as_ptr().cast(),
                    name.len() as u64,
                    &mut id,
                )
            })
            .and(if id > i64::MAX as u64 {
                Err(sys::INVALID)
            } else {
                Ok(())
            })
        } else {
            Err(sys::UNSUPPORTED_ABI)
        };
        match result {
            Ok(()) => Ok((id != 0).then_some(RoutineId {
                value: id,
                _transaction: PhantomData,
            })),
            Err(error) => {
                self.error = Some(error);
                Err(error)
            }
        }
    }
    /// One new FUNCTION/PROCEDURE, normally resolved and admitted by the host.
    /// Success makes it available to subsequent creates in this build view,
    /// not to other sessions. Unsupported DDL fails; errors stay sticky.
    pub fn create_routine(&mut self, sql: &str) -> Result<RoutineId<'txn>> {
        if let Some(error) = self.error {
            return Err(error);
        }
        let mut id = 0;
        let result = if sql.trim().is_empty() || sql.len() > 4 * 1024 * 1024 || sql.contains('\0') {
            Err(sys::INVALID)
        } else if let Some(create) = self.raw.create_routine {
            status(unsafe {
                create(
                    self.raw.host_context,
                    sql.as_ptr().cast(),
                    sql.len() as u64,
                    &mut id,
                )
            })
            .and({
                if id == 0 || id > i64::MAX as u64 {
                    Err(sys::INVALID)
                } else {
                    Ok(())
                }
            })
        } else {
            Err(sys::UNSUPPORTED_ABI)
        };
        match result {
            Ok(()) => Ok(RoutineId {
                value: id,
                _transaction: PhantomData,
            }),
            Err(error) => {
                self.error = Some(error);
                Err(error)
            }
        }
    }
}

pub trait TransactionalInstaller: Installer {
    /// Called once after static/prepared declarations have been staged. No
    /// external side effects, background work, commit or rollback. Normal
    /// module lifecycle admission remains the implementation's responsibility.
    fn build(instance: *mut sys::Handle, context: &mut TransactionContext<'_>) -> Result<()>;
}

impl<T: TransactionalInstaller> Service<T> {
    pub const V2: sys::CatalogServiceV2 = sys::CatalogServiceV2 {
        v1: sys::CatalogService {
            struct_size: size_of::<sys::CatalogServiceV2>() as u32,
            spi_major: 1,
            spi_minor: 1,
            reserved_word: 0,
            prepare: Some(Self::prepare),
            reserved: [0; 4],
        },
        build: Some(Self::build),
        reserved: [0; 4],
    };
    unsafe extern "C" fn build(
        instance: *mut sys::Handle,
        raw: *const sys::CatalogBuildContext,
    ) -> sys::Status {
        boundary(|| {
            if instance.is_null() || raw.is_null() {
                return Err(sys::INVALID);
            }
            if unsafe { (*raw).struct_size } < size_of::<sys::CatalogBuildContext>() as u32 {
                return Err(sys::UNSUPPORTED_ABI);
            }
            // Preserve the original raw pointer's allocation provenance when
            // inspecting the optional suffix; do not widen a v1 reference.
            let lookup = if unsafe { (*raw).struct_size }
                >= size_of::<sys::CatalogBuildContextV2>() as u32
            {
                let extended = unsafe { &*raw.cast::<sys::CatalogBuildContextV2>() };
                if extended.reserved != [0; 4] {
                    return Err(sys::INVALID);
                }
                Some(extended)
            } else {
                None
            };
            let raw = unsafe { &*raw };
            if raw.reserved_word != 0
                || raw.reserved != [0; 4]
                || raw.tenant_id != 1
                || raw.database_id == 0
                || raw.database_id > i64::MAX as u64
                || raw.owner_id == 0
                || raw.owner_id > i64::MAX as u64
                || raw.host_context.is_null()
                || raw.create_routine.is_none()
                || raw.extension_name.is_null()
                || raw.extension_version.is_null()
            {
                return Err(sys::INVALID);
            }
            let name = unsafe { CStr::from_ptr(raw.extension_name) }
                .to_str()
                .map_err(|_| sys::INVALID)?;
            let version = unsafe { CStr::from_ptr(raw.extension_version) }
                .to_str()
                .map_err(|_| sys::INVALID)?;
            if name.is_empty() || name.len() > 255 || version.is_empty() || version.len() > 255 {
                return Err(sys::INVALID);
            }
            let mut context = TransactionContext {
                raw,
                lookup,
                name,
                version,
                error: None,
                _thread: PhantomData,
            };
            let result = T::build(instance, &mut context);
            context.error.map_or(result, Err)
        })
    }
}
pub struct Service<T: Installer>(PhantomData<T>);
impl<T: Installer> Service<T> {
    pub const V1: sys::CatalogService = sys::CatalogService {
        struct_size: size_of::<sys::CatalogService>() as u32,
        spi_major: 1,
        spi_minor: 0,
        reserved_word: 0,
        prepare: Some(Self::prepare),
        reserved: [0; 4],
    };
    unsafe extern "C" fn prepare(
        instance: *mut sys::Handle,
        raw: *const sys::CatalogContext,
    ) -> sys::Status {
        boundary(|| {
            if instance.is_null() || raw.is_null() {
                return Err(sys::INVALID);
            }
            if unsafe { (*raw).struct_size } < size_of::<sys::CatalogContext>() as u32 {
                return Err(sys::UNSUPPORTED_ABI);
            }
            let raw = unsafe { &*raw };
            if raw.reserved_word != 0
                || raw.reserved != [0; 4]
                || raw.tenant_id != 1
                || raw.database_id == 0
                || raw.database_id > i64::MAX as u64
                || raw.owner_id == 0
                || raw.owner_id > i64::MAX as u64
                || raw.host_context.is_null()
                || raw.emit_sql.is_none()
                || raw.extension_name.is_null()
                || raw.extension_version.is_null()
            {
                return Err(sys::INVALID);
            }
            // Host-provided NUL-terminated metadata is borrowed for this callback.
            let name = unsafe { CStr::from_ptr(raw.extension_name) }
                .to_str()
                .map_err(|_| sys::INVALID)?;
            let version = unsafe { CStr::from_ptr(raw.extension_version) }
                .to_str()
                .map_err(|_| sys::INVALID)?;
            if name.is_empty() || name.len() > 255 || version.is_empty() || version.len() > 255 {
                return Err(sys::INVALID);
            }
            let mut context = CatalogContext {
                raw,
                name,
                version,
                error: None,
                _thread: PhantomData,
            };
            let result = T::prepare(instance, &mut context);
            if let Some(error) = context.error {
                Err(error)
            } else {
                result
            }
        })
    }
}
