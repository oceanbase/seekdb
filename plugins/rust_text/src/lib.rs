// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#![deny(unsafe_op_in_unsafe_fn)]
mod batch_count;
mod catalog;
mod concat;
mod planning;
mod query_catalog;
mod sql_series;
mod stored_text;
mod words;

use seekdb_extension::{
    boundary, sys, Call, CastContext, CastDefinition, DynamicFunctionDefinition,
    FunctionDefinition, ImplementationReference, Registration, TypeDefinition, TypeResolution,
};
use std::ffi::CStr;
use std::mem::size_of;
use std::ptr;
use std::sync::atomic::{AtomicBool, AtomicPtr, Ordering};

static STARTED: AtomicBool = AtomicBool::new(false);
static HOST: AtomicPtr<sys::HostApiV1> = AtomicPtr::new(ptr::null_mut());

// Called only inside a leased service callback. The loader keeps the immutable
// host table alive until all callbacks have ended and deinit is invoked. The
// higher-ranked closure prevents borrowed allocators/buffers escaping the call;
// the atomic pointer is not itself an ownership or module-lifetime mechanism.
fn with_host_allocator<T>(
    f: impl for<'host> FnOnce(
        &seekdb_extension::memory::HostAllocator<'host>,
    ) -> seekdb_extension::Result<T>,
) -> seekdb_extension::Result<T> {
    let allocator =
        unsafe { seekdb_extension::memory::HostAllocator::from_raw(HOST.load(Ordering::Acquire)) }?;
    f(&allocator)
}
fn instance() -> *mut sys::Handle {
    (&STARTED as *const AtomicBool).cast_mut().cast()
}
static ARGUMENT_TYPES: [&CStr; 1] = [c"core.type.bytes"];
const TEXT_TYPE: &CStr = c"org.seekdb.rust-text.utf8";
static TYPED_ARGUMENTS: [&CStr; 1] = [TEXT_TYPE];
seekdb_extension::scalar_function! {
    pub count_function {
        definition: FunctionDefinition {
            object_id: c"org.seekdb.rust-text.char-count",
            sql_name: c"seekdb_rust_char_count",
            argument_types: &ARGUMENT_TYPES,
            result_type: c"core.type.int64",
            service_id: c"org.seekdb.rust-text.char-count",
            minimum_version: sys::Version {
                major: 1,
                minor: 0,
                patch: 0,
            },
            maximum_version_exclusive: sys::Version {
                major: 2,
                minor: 0,
                patch: 0,
            },
            required_capabilities: sys::THREAD_SAFE,
            flags: sys::DETERMINISTIC | sys::IMMUTABLE,
        },
        sql: false,
        query_control: true,
        validate: validate_instance,
        execute: count_handler,
    }
}
seekdb_extension::scalar_function! {
    sql_count_function {
        definition: FunctionDefinition {
            object_id: c"org.seekdb.rust-text.sql-chars",
            sql_name: c"seekdb_rust_sql_chars",
            argument_types: &ARGUMENT_TYPES,
            result_type: c"core.type.int64",
            service_id: c"org.seekdb.rust-text.sql-chars",
            minimum_version: sys::Version {
                major: 1,
                minor: 0,
                patch: 0,
            },
            maximum_version_exclusive: sys::Version {
                major: 2,
                minor: 0,
                patch: 0,
            },
            required_capabilities: sys::THREAD_SAFE,
            flags: 0,
        },
        sql: true,
        validate: validate_instance,
        execute: sql_count_handler,
    }
}
static FUNCTIONS: [FunctionDefinition; 8] = [
    concat::function::DEFINITION,
    concat::called_definition(),
    query_catalog::mutation::DEFINITION,
    query_catalog::function::DEFINITION,
    count_function::DEFINITION,
    sql_count_function::DEFINITION,
    FunctionDefinition {
        object_id: c"org.seekdb.rust-text.make-text",
        sql_name: c"seekdb_rust_text",
        argument_types: &ARGUMENT_TYPES,
        result_type: TEXT_TYPE,
        service_id: c"org.seekdb.rust-text.from-bytes",
        minimum_version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        maximum_version_exclusive: sys::Version {
            major: 2,
            minor: 0,
            patch: 0,
        },
        required_capabilities: sys::THREAD_SAFE,
        flags: sys::DETERMINISTIC | sys::IMMUTABLE,
    },
    FunctionDefinition {
        object_id: c"org.seekdb.rust-text.typed-char-count",
        sql_name: c"seekdb_rust_char_count",
        argument_types: &TYPED_ARGUMENTS,
        result_type: c"core.type.int64",
        service_id: c"org.seekdb.rust-text.char-count",
        minimum_version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        maximum_version_exclusive: sys::Version {
            major: 2,
            minor: 0,
            patch: 0,
        },
        required_capabilities: sys::THREAD_SAFE,
        flags: sys::DETERMINISTIC | sys::IMMUTABLE,
    },
];

fn implementation(service_id: &CStr) -> ImplementationReference<'_> {
    ImplementationReference {
        service_id,
        minimum_version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        maximum_version_exclusive: sys::Version {
            major: 2,
            minor: 0,
            patch: 0,
        },
        required_capabilities: sys::THREAD_SAFE,
    }
}

unsafe extern "C" fn init(
    host: *const sys::HostApiV1,
    output: *mut *mut sys::Handle,
) -> sys::Status {
    boundary(|| {
        if output.is_null() {
            return Err(sys::INVALID);
        }
        unsafe { *output = ptr::null_mut() };
        unsafe { seekdb_extension::memory::HostAllocator::from_raw(host) }?.require_owned()?;
        let mut registration = unsafe { Registration::begin(host) }?;
        registration.optimizer_hook(&planning::definition())?;
        registration.function(&planning::COUNTER)?;
        planning::reset();
        stored_text::register(&mut registration)?;
        registration.data_type(&TypeDefinition {
            object_id: TEXT_TYPE,
            sql_name: c"rust_utf8",
            physical_format_id: c"org.seekdb.rust-text.utf8.v1",
            physical_format_version: 1,
            flags: 0,
            codec: implementation(c"org.seekdb.rust-text.codec"),
        })?;
        registration.cast(&CastDefinition {
            object_id: c"org.seekdb.rust-text.bytes-to-utf8",
            source_type_id: c"core.type.bytes",
            target_type_id: TEXT_TYPE,
            context: CastContext::Explicit,
            cost: 1,
            flags: sys::DETERMINISTIC | sys::IMMUTABLE,
            implementation: implementation(c"org.seekdb.rust-text.from-bytes"),
        })?;
        registration.cast(&CastDefinition {
            object_id: c"org.seekdb.rust-text.utf8-to-bytes",
            source_type_id: TEXT_TYPE,
            target_type_id: c"core.type.bytes",
            context: CastContext::Implicit,
            cost: 1,
            flags: sys::DETERMINISTIC | sys::IMMUTABLE,
            implementation: implementation(c"org.seekdb.rust-text.to-bytes"),
        })?;
        for function in &FUNCTIONS {
            registration.function(function)?;
        }
        for (object_id, sql_name, argument_types) in [
            (
                c"org.seekdb.rust-text.identity",
                c"seekdb_rust_identity",
                None,
            ),
            (
                c"org.seekdb.rust-text.identity-bytes",
                c"seekdb_rust_identity_bytes",
                Some(&ARGUMENT_TYPES[..]),
            ),
        ] {
            registration.dynamic_function(&DynamicFunctionDefinition {
                object_id,
                sql_name,
                argument_types,
                minimum_arity: 1,
                maximum_arity: 1,
                variadic: false,
                implementation: implementation(c"org.seekdb.rust-text.identity"),
                flags: sys::DETERMINISTIC | sys::IMMUTABLE,
            })?;
        }
        registration.table_function(&words::definition())?;
        registration.table_function(&words::bytes_definition())?;
        registration.table_function(&words::nullable_definition())?;
        registration.table_function(&words::strict_definition())?;
        registration.table_function(&sql_series::definition())?;
        registration.commit()?;
        HOST.store(host.cast_mut(), Ordering::Release);
        STARTED.store(false, Ordering::Release);
        unsafe { *output = instance() };
        Ok(())
    })
}

unsafe extern "C" fn start(handle: *mut sys::Handle) -> sys::Status {
    boundary(|| {
        if handle != instance() {
            return Err(sys::INVALID);
        }
        STARTED.store(true, Ordering::Release);
        Ok(())
    })
}
unsafe extern "C" fn stop(handle: *mut sys::Handle) -> sys::Status {
    boundary(|| {
        if handle != instance() {
            return Err(sys::INVALID);
        }
        STARTED.store(false, Ordering::Release);
        Ok(())
    })
}
unsafe extern "C" fn deinit(handle: *mut sys::Handle) {
    // The loader has drained service calls. Callback-scoped host buffers have
    // already been dropped; no borrowed allocator survives this publication.
    if handle == instance() {
        STARTED.store(false, Ordering::Release);
        HOST.store(ptr::null_mut(), Ordering::Release);
    }
}

fn validate_instance(handle: *mut sys::Handle) -> seekdb_extension::Result<()> {
    if handle != instance() || !STARTED.load(Ordering::Acquire) {
        Err(sys::FAILED_PRECONDITION)
    } else {
        Ok(())
    }
}

fn execute_count(call: &mut Call<'_>, sql: bool) -> seekdb_extension::Result<()> {
    if call.argument_count() != 1 {
        return Err(sys::INVALID);
    }
    let text = if sql {
        call.text(0)?
    } else {
        let bytes = call
            .bytes(0, c"core.type.bytes")
            .or_else(|_| call.bytes(0, TEXT_TYPE))?;
        bytes
            .map(|bytes| std::str::from_utf8(bytes).map_err(|_| sys::INVALID))
            .transpose()?
    };
    let result = if sql {
        call.query_i64(
            "SELECT CHAR_LENGTH(CAST(? AS CHAR CHARACTER SET utf8mb4))",
            text,
        )?
    } else {
        if call.supports_query_control() {
            call.poll_query().map_err(|error| error.status)?;
            let mut count = 0i64;
            if let Some(value) = text {
                for _ in value.chars() {
                    count += 1;
                    if count % 4096 == 0 {
                        call.poll_query().map_err(|error| error.status)?;
                    }
                }
            }
            call.poll_query().map_err(|error| error.status)?;
            text.map(|_| count)
        } else {
            text.map(|value| value.chars().count() as i64)
        }
    };
    call.emit_i64(result)
}
fn count_handler(_: *mut sys::Handle, call: &mut Call<'_>) -> seekdb_extension::Result<()> {
    execute_count(call, false)
}
fn sql_count_handler(_: *mut sys::Handle, call: &mut Call<'_>) -> seekdb_extension::Result<()> {
    execute_count(call, true)
}

unsafe extern "C" fn identity_type(
    handle: *mut sys::Handle,
    arguments: *const *const std::ffi::c_char,
    count: u32,
    output: *mut sys::ResolvedType,
) -> sys::Status {
    boundary(|| {
        if handle != instance() || !STARTED.load(Ordering::Acquire) {
            return Err(sys::FAILED_PRECONDITION);
        }
        let call = unsafe { TypeResolution::from_raw(arguments, count, output) }?;
        if call.argument_count() != 1 {
            return Err(sys::INVALID);
        }
        let result = call.argument(0)?.unwrap_or(c"core.type.bytes");
        call.finish(result)
    })
}

unsafe extern "C" fn identity(
    handle: *mut sys::Handle,
    context: *const sys::ContextV1,
    arguments: *const sys::Value,
    count: u32,
) -> sys::Status {
    boundary(|| {
        if handle != instance() || !STARTED.load(Ordering::Acquire) {
            return Err(sys::FAILED_PRECONDITION);
        }
        let mut call = unsafe { Call::from_raw(context, arguments, count) }?;
        if call.argument_count() != 1 {
            return Err(sys::INVALID);
        }
        let type_id = call.argument_type(0)?.unwrap_or(c"core.type.bytes");
        let value = call.bytes(0, type_id)?;
        call.emit_bytes(type_id, value)
    })
}

static IDENTITY_SERVICE: sys::FunctionServiceV2 = sys::FunctionServiceV2 {
    v1: sys::FunctionService {
        struct_size: size_of::<sys::FunctionServiceV2>() as u32,
        spi_major: 1,
        spi_minor: sys::RESULT_TYPE_MINOR,
        reserved_word: 0,
        execute: Some(identity),
        reserved: [0; 8],
    },
    resolve_result: Some(identity_type),
    resolution_reserved: [0; 4],
};

unsafe fn transcode(
    handle: *mut sys::Handle,
    context: *const sys::ContextV1,
    values: *const sys::Value,
    count: u32,
    encode: bool,
) -> sys::Status {
    boundary(|| {
        if handle != instance() || !STARTED.load(Ordering::Acquire) {
            return Err(sys::FAILED_PRECONDITION);
        }
        let mut call = unsafe { Call::from_raw(context, values, count) }?;
        if call.argument_count() != 1 {
            return Err(sys::INVALID);
        }
        let bytes = call.bytes(
            0,
            if encode {
                TEXT_TYPE
            } else {
                c"core.type.bytes"
            },
        )?;
        if let Some(bytes) = bytes {
            std::str::from_utf8(bytes).map_err(|_| sys::INVALID)?;
        }
        call.emit_bytes(
            if encode {
                c"core.type.bytes"
            } else {
                TEXT_TYPE
            },
            bytes,
        )
    })
}
unsafe extern "C" fn from_bytes(
    handle: *mut sys::Handle,
    context: *const sys::ContextV1,
    values: *const sys::Value,
    count: u32,
) -> sys::Status {
    unsafe { transcode(handle, context, values, count, false) }
}
unsafe extern "C" fn decode(
    handle: *mut sys::Handle,
    context: *const sys::ContextV1,
    encoded: *const u8,
    size: u64,
) -> sys::Status {
    if context.is_null() || unsafe { (*context).struct_size } != size_of::<sys::ContextV1>() as u32
    {
        return sys::UNSUPPORTED_ABI;
    }
    let value = sys::Value {
        struct_size: size_of::<sys::Value>() as u32,
        type_id: c"core.type.bytes".as_ptr(),
        data: encoded,
        data_size: size,
        is_null: 0,
        reserved_bytes: [0; 7],
        reserved: [0; 4],
    };
    unsafe { transcode(handle, context, &value, 1, false) }
}
unsafe extern "C" fn to_bytes(
    handle: *mut sys::Handle,
    context: *const sys::ContextV1,
    values: *const sys::Value,
    count: u32,
) -> sys::Status {
    unsafe { transcode(handle, context, values, count, true) }
}
unsafe extern "C" fn encode(
    handle: *mut sys::Handle,
    context: *const sys::ContextV1,
    value: *const sys::Value,
) -> sys::Status {
    if context.is_null() || unsafe { (*context).struct_size } != size_of::<sys::ContextV1>() as u32
    {
        return sys::UNSUPPORTED_ABI;
    }
    unsafe { transcode(handle, context, value, 1, true) }
}
struct TextComparator;
impl seekdb_extension::type_comparison::Comparator for TextComparator {
    fn compare(
        handle: *mut sys::Handle,
        logical_type: &CStr,
        left: &[u8],
        right: &[u8],
    ) -> seekdb_extension::Result<std::cmp::Ordering> {
        validate_instance(handle)?;
        if logical_type != TEXT_TYPE {
            return Err(sys::INVALID);
        }
        let left = std::str::from_utf8(left).map_err(|_| sys::INVALID)?;
        let right = std::str::from_utf8(right).map_err(|_| sys::INVALID)?;
        // Deliberately differs from carrier byte ordering: Unicode scalar
        // count first, then exact UTF-8 bytes. No locale or mutable state.
        Ok(left
            .chars()
            .count()
            .cmp(&right.chars().count())
            .then_with(|| left.cmp(right)))
    }
}
static CODEC_SERVICE: sys::TypeCodecServiceV2 = seekdb_extension::type_comparison::Service::<
    TextComparator,
>::with_codec(sys::TypeCodecService {
    struct_size: size_of::<sys::TypeCodecService>() as u32,
    spi_major: 1,
    spi_minor: 0,
    reserved_word: 0,
    decode: Some(decode),
    encode: Some(encode),
    reserved: [0; 8],
});
static FROM_BYTES_SERVICE: sys::FunctionService = sys::FunctionService {
    struct_size: size_of::<sys::FunctionService>() as u32,
    spi_major: 1,
    spi_minor: 0,
    reserved_word: 0,
    execute: Some(from_bytes),
    reserved: [0; 8],
};

static TO_BYTES_SERVICE: sys::FunctionService = sys::FunctionService {
    struct_size: size_of::<sys::FunctionService>() as u32,
    spi_major: 1,
    spi_minor: 0,
    reserved_word: 0,
    execute: Some(to_bytes),
    reserved: [0; 8],
};

struct Provides([sys::ServiceProvide; 19]);
// SAFETY: every pointer references immutable static strings/function tables.
// No service slots or mutable data are reachable through these descriptors.
unsafe impl Sync for Provides {}
static PROVIDES: Provides = Provides([
    concat::provide(),
    stored_text::provide(0),
    stored_text::provide(1),
    stored_text::provide(2),
    query_catalog::mutation::provide(
        sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        sys::THREAD_SAFE,
    ),
    query_catalog::function::provide(
        sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        sys::THREAD_SAFE,
    ),
    catalog::provide(),
    planning::provide(),
    planning::counter_provide(),
    words::provide(),
    words::bytes_provide(),
    words::nullable_provide(),
    sql_series::provide(),
    sys::ServiceProvide {
        struct_size: size_of::<sys::ServiceProvide>() as u32,
        service_id: c"org.seekdb.rust-text.identity".as_ptr(),
        version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        service: (&IDENTITY_SERVICE as *const sys::FunctionServiceV2).cast(),
        capabilities: sys::THREAD_SAFE,
        reserved: [0; 4],
    },
    sys::ServiceProvide {
        struct_size: size_of::<sys::ServiceProvide>() as u32,
        service_id: c"org.seekdb.rust-text.to-bytes".as_ptr(),
        version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        service: (&TO_BYTES_SERVICE as *const sys::FunctionService).cast(),
        capabilities: sys::THREAD_SAFE,
        reserved: [0; 4],
    },
    sys::ServiceProvide {
        struct_size: size_of::<sys::ServiceProvide>() as u32,
        service_id: c"org.seekdb.rust-text.codec".as_ptr(),
        version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        service: (&CODEC_SERVICE as *const sys::TypeCodecServiceV2).cast(),
        capabilities: sys::THREAD_SAFE,
        reserved: [0; 4],
    },
    sys::ServiceProvide {
        struct_size: size_of::<sys::ServiceProvide>() as u32,
        service_id: c"org.seekdb.rust-text.from-bytes".as_ptr(),
        version: sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        service: (&FROM_BYTES_SERVICE as *const sys::FunctionService).cast(),
        capabilities: sys::THREAD_SAFE,
        reserved: [0; 4],
    },
    batch_count::provide(),
    sql_count_function::provide(
        sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        sys::THREAD_SAFE,
    ),
]);
struct Manifest(sys::Manifest);
// SAFETY: immutable module-lifetime manifest; mutable plugin state is accessed
// only through callbacks using an AtomicBool, never through manifest pointers.
unsafe impl Sync for Manifest {}
static MANIFEST: Manifest = Manifest(sys::Manifest {
    struct_size: size_of::<sys::Manifest>() as u32,
    abi_major: 1,
    abi_minor: 0,
    plugin_id: c"org.seekdb.rust-text".as_ptr(),
    vendor: c"seekdb".as_ptr(),
    version: sys::Version {
        major: 1,
        minor: 0,
        patch: 0,
    },
    build_id: c"rust-text-owned-memory-v15".as_ptr(),
    catalog_version: 1,
    data_format_version: 1,
    capabilities: sys::THREAD_SAFE | sys::PERSISTENT_DATA,
    provides: PROVIDES.0.as_ptr(),
    provides_count: PROVIDES.0.len() as u32,
    required_services: ptr::null(),
    required_services_count: 0,
    init: Some(init),
    start: Some(start),
    stop: Some(stop),
    deinit: Some(deinit),
    reserved: [0; 8],
});

#[no_mangle]
pub extern "C" fn seekdb_plugin_entry_v1() -> *const sys::Manifest {
    &MANIFEST.0
}
