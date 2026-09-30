// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use crate::{boundary, sys, Call, Result};
use std::mem::size_of;

/// Invoke a scalar handler inside the SDK error/panic boundary. The validator
/// runs first, including for NULL inputs, and must enforce instance admission.
/// SQL-enabled services require the extended execution context, not a fallback.
///
/// # Safety
/// The raw context/values must satisfy `Call::from_raw`'s allocation, lifetime,
/// thread and foreign-unwinding contract. The instance belongs to this module
/// and remains alive for the callback; the validator/handler must not fabricate
/// longer-lived references from it. This function does not synchronize module
/// state or recover shared state poisoned by a caught panic.
pub unsafe fn invoke_scalar(
    instance: *mut sys::Handle,
    context: *const sys::ContextV1,
    values: *const sys::Value,
    count: u32,
    sql: bool,
    validate: fn(*mut sys::Handle) -> Result<()>,
    handler: for<'q> fn(*mut sys::Handle, &mut Call<'q>) -> Result<()>,
) -> sys::Status {
    boundary(|| {
        validate(instance)?;
        if context.is_null() {
            return Err(sys::INVALID);
        }
        let minimum = if sql {
            size_of::<sys::ContextV2>()
        } else {
            size_of::<sys::ContextV1>()
        };
        if unsafe { (*context).struct_size } < minimum as u32 {
            return Err(sys::UNSUPPORTED_ABI);
        }
        let mut call = unsafe { Call::from_raw(context, values, count) }?;
        handler(instance, &mut call)
    })
}

/// Declare a fixed-result scalar function and its native service in one module.
///
/// Generated members are `DEFINITION`, `SERVICE` and `provide(version, caps)`.
/// Register DEFINITION with `Registration::function`; include the provided
/// service in the native manifest. No registration happens merely by declaring
/// this macro. It does not generate SQL, infer permissions/volatility, implement
/// lifecycle management, or choose which overloads may share the service.
/// The handler must check arity and argument types appropriate for all its
/// callers, emit its result, and obey callback-scoped borrowing.
/// Optional `query_control: true` requests the extended host context without
/// requiring it: `sql: false` handlers still accept legacy v1 and can check
/// `Call::supports_query_control()` before polling. `sql: true` remains strict.
///
/// ```
/// use seekdb_extension::{scalar_function, sys, Call, FunctionDefinition, Result};
/// fn admitted(_: *mut sys::Handle) -> Result<()> { Ok(()) } // Supply real lifecycle admission.
/// fn count(_: *mut sys::Handle, call: &mut Call<'_>) -> Result<()> {
///     if call.argument_count() != 1 { return Err(sys::INVALID); }
///     call.emit_i64(call.text(0)?.map(|s| s.chars().count() as i64))
/// }
/// scalar_function! {
///     pub text_count {
///         definition: FunctionDefinition {
///             object_id: c"example.text-count", sql_name: c"text_count",
///             argument_types: &[c"core.type.bytes"], result_type: c"core.type.int64",
///             service_id: c"example.text-count",
///             minimum_version: sys::Version { major: 1, minor: 0, patch: 0 },
///             maximum_version_exclusive: sys::Version { major: 2, minor: 0, patch: 0 },
///             required_capabilities: sys::THREAD_SAFE, flags: 0,
///         },
///         sql: false,
///         validate: admitted,
///         execute: count,
///     }
/// }
/// # fn main() {
/// assert_eq!(text_count::DEFINITION.sql_name, c"text_count");
/// assert_eq!(text_count::SERVICE.spi_minor, 0);
/// # }
/// ```
#[macro_export]
macro_rules! scalar_function {
    ($(#[$attribute:meta])* $visibility:vis $name:ident {
        definition: $definition:expr,
        sql: $sql:expr,
        $(query_control: $query_control:expr,)?
        validate: $validate:path,
        execute: $handler:path $(,)?
    }) => {
        $(#[$attribute])*
        $visibility mod $name {
            #[allow(unused_imports)]
            use super::*;
            pub const DEFINITION: $crate::FunctionDefinition<'static> = $definition;
            pub static SERVICE: $crate::sys::FunctionService = $crate::sys::FunctionService {
                struct_size: ::std::mem::size_of::<$crate::sys::FunctionService>() as u32,
                spi_major: 1,
                spi_minor: if $sql $(|| $query_control)? { 1 } else { 0 },
                reserved_word: 0,
                execute: Some(__seekdb_execute),
                reserved: [0; 8],
            };
            unsafe extern "C" fn __seekdb_execute(
                __seekdb_instance: *mut $crate::sys::Handle,
                __seekdb_context: *const $crate::sys::ContextV1,
                __seekdb_values: *const $crate::sys::Value,
                __seekdb_count: u32,
            ) -> $crate::sys::Status {
                // SAFETY: this private FFI entry has the host scalar ABI
                // contract. No pointer escapes the synchronous SDK wrapper.
                unsafe { $crate::invoke_scalar(__seekdb_instance, __seekdb_context, __seekdb_values, __seekdb_count, $sql, $validate, $handler) }
            }
            pub const fn provide(version: $crate::sys::Version, capabilities: u64) -> $crate::sys::ServiceProvide {
                $crate::sys::ServiceProvide {
                    struct_size: ::std::mem::size_of::<$crate::sys::ServiceProvide>() as u32,
                    service_id: DEFINITION.service_id.as_ptr(),
                    version,
                    service: (&SERVICE as *const $crate::sys::FunctionService).cast(),
                    capabilities,
                    reserved: [0; 4],
                }
            }
        }
    };
}
