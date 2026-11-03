// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use seekdb_extension::{
    batch,
    memory::{HostAllocator, HostBuffer},
    sys, Call, FunctionDefinition, Result,
};

const TYPES: [&std::ffi::CStr; 3] = [c"core.type.bytes"; 3];
const MAX_RESULT: usize = 16 * 1024 * 1024;
seekdb_extension::scalar_function! {
    pub function {
        definition: FunctionDefinition {
            object_id: c"org.seekdb.rust-text.concat3", sql_name: c"seekdb_rust_concat3",
            argument_types: &TYPES, result_type: c"core.type.bytes",
            service_id: c"org.seekdb.rust-text.concat3",
            minimum_version: sys::Version { major: 1, minor: 0, patch: 0 },
            maximum_version_exclusive: sys::Version { major: 2, minor: 0, patch: 0 },
            required_capabilities: sys::THREAD_SAFE,
            flags: sys::DETERMINISTIC | sys::IMMUTABLE | sys::NULL_PROPAGATING,
        },
        sql: false, query_control: true,
        validate: crate::validate_instance,
        execute: scalar_handler,
    }
}
pub const fn called_definition() -> FunctionDefinition<'static> {
    let mut definition = function::DEFINITION;
    definition.object_id = c"org.seekdb.rust-text.concat3-called";
    definition.sql_name = c"seekdb_rust_concat3_called";
    definition.flags &= !sys::NULL_PROPAGATING;
    definition
}

fn combine<'a>(
    allocator: &'a HostAllocator<'_>,
    parts: [Option<&[u8]>; 3],
) -> Result<Option<HostBuffer<'a>>> {
    let mut length = 0usize;
    for part in parts.into_iter().flatten() {
        std::str::from_utf8(part).map_err(|_| sys::INVALID)?;
        length = length.checked_add(part.len()).ok_or(sys::INVALID)?;
        if length > MAX_RESULT {
            return Err(sys::INVALID);
        }
    }
    if parts.iter().any(Option::is_none) {
        return Ok(None);
    }
    Ok(Some(
        allocator.copy_from_slices(&parts.map(Option::unwrap))?,
    ))
}
fn scalar_handler(_: *mut sys::Handle, call: &mut Call<'_>) -> Result<()> {
    if call.argument_count() != 3 {
        return Err(sys::INVALID);
    }
    if call.supports_query_control() {
        call.poll_query().map_err(|e| e.status)?;
    }
    super::with_host_allocator(|allocator| {
        let result = combine(
            allocator,
            [
                call.bytes(0, TYPES[0])?,
                call.bytes(1, TYPES[1])?,
                call.bytes(2, TYPES[2])?,
            ],
        )?;
        if call.supports_query_control() {
            call.poll_query().map_err(|e| e.status)?;
        }
        call.emit_bytes(c"core.type.bytes", result.as_deref())
    })
}
struct Concat;
impl batch::Handler for Concat {
    fn validate(instance: *mut sys::Handle) -> Result<()> {
        super::validate_instance(instance)
    }
    fn execute(_: *mut sys::Handle, batch: &mut batch::Batch<'_>) -> Result<()> {
        super::with_host_allocator(|allocator| {
            for i in 0..batch.row_count() {
                if batch.supports_query_control() {
                    batch.poll_query().map_err(|e| e.status)?;
                }
                let row = batch.row(i)?;
                if row.argument_count() != 3 {
                    return Err(sys::INVALID);
                }
                let result = combine(
                    allocator,
                    [
                        row.bytes(0, TYPES[0])?,
                        row.bytes(1, TYPES[1])?,
                        row.bytes(2, TYPES[2])?,
                    ],
                )?;
                batch.emit_bytes(i, c"core.type.bytes", result.as_deref())?;
            }
            Ok(())
        })
    }
}
unsafe extern "C" fn scalar(
    instance: *mut sys::Handle,
    context: *const sys::ContextV1,
    arguments: *const sys::Value,
    count: u32,
) -> sys::Status {
    unsafe { function::SERVICE.execute.unwrap()(instance, context, arguments, count) }
}
static SERVICE: sys::FunctionServiceV3 = batch::Service::<Concat>::with_scalar(scalar, None);
pub const fn provide() -> sys::ServiceProvide {
    let mut result = function::provide(
        sys::Version {
            major: 1,
            minor: 0,
            patch: 0,
        },
        sys::THREAD_SAFE,
    );
    result.service = (&SERVICE as *const sys::FunctionServiceV3).cast();
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    // Unit fixture uses the same SDK ownership path; real host quotas and
    // emit failures are tested separately through the loaded Rust DSO.
    fn combine(parts: [Option<&[u8]>; 3]) -> Result<Option<Vec<u8>>> {
        unsafe extern "C" fn allocate(
            _: *mut sys::Handle,
            bytes: u64,
            alignment: u32,
        ) -> *mut std::ffi::c_void {
            unsafe {
                std::alloc::alloc(
                    std::alloc::Layout::from_size_align(bytes as usize, alignment as usize)
                        .unwrap(),
                )
                .cast()
            }
        }
        unsafe extern "C" fn free(
            _: *mut sys::Handle,
            memory: *mut std::ffi::c_void,
            bytes: u64,
            alignment: u32,
        ) {
            unsafe {
                std::alloc::dealloc(
                    memory.cast(),
                    std::alloc::Layout::from_size_align(bytes as usize, alignment as usize)
                        .unwrap(),
                )
            };
        }
        let mut api: sys::HostApiV1 = unsafe { std::mem::zeroed() };
        api.struct_size = std::mem::size_of::<sys::HostApiV1>() as u32;
        api.abi_major = 1;
        let owner = 0u8;
        api.host_handle = (&owner as *const u8).cast_mut().cast();
        api.alloc = Some(allocate);
        api.free = Some(free);
        let allocator = unsafe { HostAllocator::from_raw(&api) }?;
        super::combine(&allocator, parts).map(|result| result.map(|bytes| bytes.to_vec()))
    }
    #[test]
    fn unicode_empty_nul_and_null_are_distinct() {
        assert_eq!(
            combine([Some("中".as_bytes()), Some(b""), Some(b"a\0b")]),
            Ok(Some("中a\0b".as_bytes().to_vec()))
        );
        assert_eq!(combine([Some(b""), Some(b""), Some(b"")]), Ok(Some(vec![])));
        assert_eq!(combine([Some(b"a"), None, Some(b"b")]), Ok(None));
        assert_eq!(
            combine([Some(&[0xff]), None, Some(b"b")]),
            Err(sys::INVALID)
        );
    }
    #[test]
    fn aggregate_result_limit_and_explicit_null_policy() {
        let bytes = vec![b'x'; MAX_RESULT];
        assert_eq!(
            combine([Some(&bytes), Some(b"x"), Some(b"")]),
            Err(sys::INVALID)
        );
        assert_eq!(
            combine([Some(&bytes), Some(b""), Some(b"")])
                .unwrap()
                .unwrap()
                .len(),
            MAX_RESULT
        );
        assert_ne!(function::DEFINITION.flags & sys::NULL_PROPAGATING, 0);
        assert_eq!(called_definition().flags & sys::NULL_PROPAGATING, 0);
        assert_eq!(
            function::DEFINITION.service_id,
            called_definition().service_id
        );
    }
}
