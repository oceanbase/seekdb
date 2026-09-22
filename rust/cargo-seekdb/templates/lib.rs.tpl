//! Public-ABI seekdb plugin. Edit the function, tests and manifest together.
#![deny(unsafe_op_in_unsafe_fn)]
use seekdb_extension::{boundary, sys, Call, FunctionDefinition, Registration, Result};
use std::{mem::size_of, ptr, sync::atomic::{AtomicBool, Ordering}};

static STARTED: AtomicBool = AtomicBool::new(false);
fn instance() -> *mut sys::Handle { (&STARTED as *const AtomicBool).cast_mut().cast() }
fn validate_instance(handle: *mut sys::Handle) -> Result<()> {
    if handle != instance() || !STARTED.load(Ordering::Acquire) {
        Err(sys::FAILED_PRECONDITION)
    } else { Ok(()) }
}
fn count_utf8(bytes: Option<&[u8]>) -> Result<Option<i64>> {
    bytes.map(|bytes| std::str::from_utf8(bytes).map(|s| s.chars().count() as i64)
        .map_err(|_| sys::INVALID)).transpose()
}
fn count(_: *mut sys::Handle, call: &mut Call<'_>) -> Result<()> {
    if call.argument_count() != 1 { return Err(sys::INVALID); }
    let result = count_utf8(call.bytes(0, c"core.type.bytes")?)?;
    call.emit_i64(result)
}
seekdb_extension::scalar_function! {
    pub chars {
        definition: FunctionDefinition {
            object_id: c"@@PLUGIN_ID@@.chars", sql_name: c"@@NAME@@_chars",
            argument_types: &[c"core.type.bytes"], result_type: c"core.type.int64",
            service_id: c"@@PLUGIN_ID@@.chars",
            minimum_version: sys::Version { major: 1, minor: 0, patch: 0 },
            maximum_version_exclusive: sys::Version { major: 2, minor: 0, patch: 0 },
            required_capabilities: sys::THREAD_SAFE,
            flags: sys::DETERMINISTIC | sys::IMMUTABLE,
        },
        sql: false,
        validate: validate_instance,
        execute: count,
    }
}
unsafe extern "C" fn init(host: *const sys::HostApiV1, output: *mut *mut sys::Handle) -> sys::Status {
    boundary(|| {
        if output.is_null() { return Err(sys::INVALID); }
        unsafe { *output = ptr::null_mut(); }
        let mut registration = unsafe { Registration::begin(host) }?;
        registration.function(&chars::DEFINITION)?;
        registration.commit()?;
        STARTED.store(false, Ordering::Release);
        unsafe { *output = instance(); }
        Ok(())
    })
}
unsafe extern "C" fn start(handle: *mut sys::Handle) -> sys::Status {
    boundary(|| {
        if handle != instance() { return Err(sys::INVALID); }
        STARTED.store(true, Ordering::Release); Ok(())
    })
}
unsafe extern "C" fn stop(handle: *mut sys::Handle) -> sys::Status {
    boundary(|| {
        if handle != instance() { return Err(sys::INVALID); }
        STARTED.store(false, Ordering::Release); Ok(())
    })
}
unsafe extern "C" fn deinit(handle: *mut sys::Handle) {
    // No allocation or background task in this template; this cannot panic.
    if handle == instance() { STARTED.store(false, Ordering::Release); }
}
struct Provides([sys::ServiceProvide; 1]);
// SAFETY: pointers refer only to immutable, module-lifetime strings/tables.
unsafe impl Sync for Provides {}
static PROVIDES: Provides = Provides([chars::provide(
    sys::Version { major: 1, minor: 0, patch: 0 }, sys::THREAD_SAFE)]);
struct Manifest(sys::Manifest);
// SAFETY: immutable module-lifetime ABI; mutable state is an independent atomic.
unsafe impl Sync for Manifest {}
static MANIFEST: Manifest = Manifest(sys::Manifest {
    struct_size: size_of::<sys::Manifest>() as u32, abi_major: 1, abi_minor: 0,
    plugin_id: c"@@PLUGIN_ID@@".as_ptr(), vendor: c"local".as_ptr(),
    version: sys::Version { major: 1, minor: 0, patch: 0 }, build_id: c"@@NAME@@-v1".as_ptr(),
    catalog_version: 1, data_format_version: 0, capabilities: sys::THREAD_SAFE,
    provides: PROVIDES.0.as_ptr(), provides_count: 1,
    required_services: ptr::null(), required_services_count: 0,
    init: Some(init), start: Some(start), stop: Some(stop), deinit: Some(deinit), reserved: [0; 8],
});
#[no_mangle]
pub extern "C" fn seekdb_plugin_entry_v1() -> *const sys::Manifest { &MANIFEST.0 }

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn counts_unicode_scalars_and_preserves_null_empty_and_invalid() {
        assert_eq!(count_utf8(Some("A中🙂".as_bytes())), Ok(Some(3)));
        assert_eq!(count_utf8(Some(b"")), Ok(Some(0)));
        assert_eq!(count_utf8(None), Ok(None));
        assert_eq!(count_utf8(Some(&[0xff])), Err(sys::INVALID));
    }
    #[test]
    fn lifecycle_admission_requires_started_instance() {
        assert!(validate_instance(instance()).is_err());
        assert_eq!(unsafe { start(ptr::null_mut()) }, sys::INVALID);
        assert_eq!(unsafe { start(instance()) }, sys::OK);
        assert_eq!(validate_instance(instance()), Ok(()));
        assert_eq!(unsafe { stop(instance()) }, sys::OK);
        assert!(validate_instance(instance()).is_err());
        unsafe { deinit(instance()); }
    }
}
