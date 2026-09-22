// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.

//! Native mapping ownership. These are host-only operations, not plugin APIs.
//! The C++ activation coordinator supplies exclusive ownership and verifies the
//! artifact before open. No Rust lock spans an OS loader call (DSO constructors
//! and destructors may execute). Dropping a record must NEVER implicitly unload.

use crate::{INVALID, OK, STATE_MISMATCH};
use std::alloc::{alloc, Layout};
use std::ffi::{c_char, c_void, CStr};
use std::ptr;

const NO_MEMORY: i32 = 5;
const NOT_FOUND: i32 = 8;
pub const IO_ERROR: i32 = 10;
const MAX_PATH: usize = 32768;

pub type Entry = unsafe extern "C" fn() -> *const c_void;

pub struct NativeModule {
    mapping: *mut c_void,
    published: bool,
}

// Intentionally no Drop implementation: only close() may release the mapping.
// A published module remains resident even after its runtime is disabled.

struct Diagnostic<'a>(&'a mut [u8]);

impl Diagnostic<'_> {
    fn set(&mut self, message: &[u8]) {
        let length = message.len().min(self.0.len() - 1);
        self.0[..length].copy_from_slice(&message[..length]);
        self.0[length] = 0;
    }
}

unsafe fn diagnostic<'a>(buffer: *mut c_char, capacity: u32) -> Option<Diagnostic<'a>> {
    if buffer.is_null() || capacity == 0 {
        None
    } else {
        // SAFETY: the caller provides a writable buffer of capacity bytes.
        let bytes = unsafe { std::slice::from_raw_parts_mut(buffer.cast(), capacity as usize) };
        bytes[0] = 0;
        Some(Diagnostic(bytes))
    }
}

#[cfg(unix)]
mod platform {
    use super::*;

    unsafe fn error(diagnostic: &mut Diagnostic<'_>, fallback: &[u8]) {
        // SAFETY: dlerror returns a thread-local, NUL-terminated borrowed string.
        let message = unsafe { libc::dlerror() };
        diagnostic.set(if message.is_null() {
            fallback
        } else {
            unsafe { CStr::from_ptr(message) }.to_bytes()
        });
    }

    pub unsafe fn open(path: &CStr, diagnostic: &mut Diagnostic<'_>) -> *mut c_void {
        unsafe { libc::dlerror() };
        let mapping = unsafe { libc::dlopen(path.as_ptr(), libc::RTLD_NOW | libc::RTLD_LOCAL) };
        if mapping.is_null() {
            unsafe { error(diagnostic, b"dlopen failed") };
        }
        mapping
    }

    pub unsafe fn entry(mapping: *mut c_void, diagnostic: &mut Diagnostic<'_>) -> Option<Entry> {
        unsafe { libc::dlerror() };
        let symbol = unsafe { libc::dlsym(mapping, c"seekdb_plugin_entry_v1".as_ptr()) };
        let message = unsafe { libc::dlerror() };
        if !message.is_null() {
            diagnostic.set(unsafe { CStr::from_ptr(message) }.to_bytes());
            None
        } else if symbol.is_null() {
            diagnostic.set(b"plugin entry symbol resolved to null");
            None
        } else {
            // POSIX dlsym function addresses are callable after this conversion.
            // Calling the entry and validating its manifest remain host duties.
            Some(unsafe { std::mem::transmute::<*mut c_void, Entry>(symbol) })
        }
    }

    pub unsafe fn close(mapping: *mut c_void, diagnostic: &mut Diagnostic<'_>) -> bool {
        unsafe { libc::dlerror() };
        if unsafe { libc::dlclose(mapping) } == 0 {
            true
        } else {
            unsafe { error(diagnostic, b"dlclose failed; mapping retained") };
            false
        }
    }
}

#[cfg(windows)]
mod platform {
    use super::*;
    use windows_sys::Win32::Foundation::{FreeLibrary, GetLastError};
    use windows_sys::Win32::System::LibraryLoader::{
        GetProcAddress, LoadLibraryExA, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR,
    };

    fn error(diagnostic: &mut Diagnostic<'_>) {
        // Keep the numeric OS error without allocating on load/close failures.
        let mut code = unsafe { GetLastError() };
        let mut digits = [0u8; 10];
        let mut start = digits.len();
        loop {
            start -= 1;
            digits[start] = b'0' + (code % 10) as u8;
            code /= 10;
            if code == 0 {
                break;
            }
        }
        let prefix = b"Windows loader error ";
        let mut message = [0u8; 32];
        message[..prefix.len()].copy_from_slice(prefix);
        let length = digits.len() - start;
        message[prefix.len()..prefix.len() + length].copy_from_slice(&digits[start..]);
        diagnostic.set(&message[..prefix.len() + length]);
    }

    pub unsafe fn open(path: &CStr, diagnostic: &mut Diagnostic<'_>) -> *mut c_void {
        // Preserve the existing host's ANSI path and restricted search policy.
        let mapping = unsafe {
            LoadLibraryExA(
                path.as_ptr().cast(),
                ptr::null_mut(),
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS,
            )
        };
        if mapping.is_null() {
            error(diagnostic);
        }
        mapping
    }

    pub unsafe fn entry(mapping: *mut c_void, diagnostic: &mut Diagnostic<'_>) -> Option<Entry> {
        let symbol = unsafe { GetProcAddress(mapping, c"seekdb_plugin_entry_v1".as_ptr().cast()) };
        match symbol {
            Some(symbol) => Some(unsafe {
                std::mem::transmute::<unsafe extern "system" fn() -> isize, Entry>(symbol)
            }),
            None => {
                error(diagnostic);
                None
            }
        }
    }

    pub unsafe fn close(mapping: *mut c_void, diagnostic: &mut Diagnostic<'_>) -> bool {
        if unsafe { FreeLibrary(mapping) } != 0 {
            true
        } else {
            error(diagnostic);
            false
        }
    }
}

/// Open a host-verified native artifact. Allocation finishes before OS loading.
///
/// # Safety
/// Non-null path points to length readable bytes; output and diagnostic point to
/// disjoint writable storage. The host must keep its verified artifact pinned
/// through this call and provide its verified load path, not an unchecked path.
/// DSO constructors must obey the host's no-unwinding callback contract.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_native_open(
    path: *const u8,
    length: u32,
    output: *mut *mut NativeModule,
    buffer: *mut c_char,
    capacity: u32,
) -> i32 {
    if output.is_null() {
        return INVALID;
    }
    unsafe { *output = ptr::null_mut() };
    let Some(mut diagnostic) = (unsafe { diagnostic(buffer, capacity) }) else {
        return INVALID;
    };
    if path.is_null() || length == 0 || length as usize > MAX_PATH {
        diagnostic.set(b"invalid native module path");
        return INVALID;
    }
    let bytes = unsafe { std::slice::from_raw_parts(path, length as usize) };
    if bytes.contains(&0) {
        diagnostic.set(b"native module path contains NUL");
        return INVALID;
    }
    let mut terminated = Vec::new();
    if terminated.try_reserve_exact(bytes.len() + 1).is_err() {
        diagnostic.set(b"native module path allocation failed");
        return NO_MEMORY;
    }
    terminated.extend_from_slice(bytes);
    terminated.push(0);
    let raw = unsafe { alloc(Layout::new::<NativeModule>()) }.cast::<NativeModule>();
    if raw.is_null() {
        diagnostic.set(b"native module allocation failed");
        return NO_MEMORY;
    }
    unsafe {
        raw.write(NativeModule {
            mapping: ptr::null_mut(),
            published: false,
        })
    };
    let mut module = unsafe { Box::from_raw(raw) };
    let path = unsafe { CStr::from_bytes_with_nul_unchecked(&terminated) };
    module.mapping = unsafe { platform::open(path, &mut diagnostic) };
    if module.mapping.is_null() {
        return IO_ERROR;
    }
    unsafe { *output = Box::into_raw(module) };
    OK
}

/// Look up the fixed ABI entry; no plugin function is invoked.
///
/// # Safety
/// module is exclusively borrowed from open; output and buffer are disjoint
/// writable storage. The returned function is valid only while module is mapped.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_native_entry(
    module: *mut NativeModule,
    output: *mut Option<Entry>,
    buffer: *mut c_char,
    capacity: u32,
) -> i32 {
    if output.is_null() {
        return INVALID;
    }
    unsafe { *output = None };
    let Some(mut diagnostic) = (unsafe { diagnostic(buffer, capacity) }) else {
        return INVALID;
    };
    let Some(module) = (unsafe { module.as_ref() }) else {
        diagnostic.set(b"invalid native module handle");
        return INVALID;
    };
    let entry = unsafe { platform::entry(module.mapping, &mut diagnostic) };
    if entry.is_none() {
        return NOT_FOUND;
    }
    unsafe { *output = entry };
    OK
}

/// Prevent failed-load cleanup from unloading an externally published module.
///
/// # Safety
/// module is an exclusively borrowed live handle returned by open.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_native_publish(module: *mut NativeModule) -> i32 {
    let Some(module) = (unsafe { module.as_mut() }) else {
        return INVALID;
    };
    module.published = true;
    OK
}

/// Explicit close: 0 = unpublished load rollback, 1 = terminal process exit.
/// On failure the handle remains owned by the caller. On success it is consumed.
///
/// # Safety
/// module has exclusive ownership, all callbacks/leases/static borrowed data are
/// drained, and no plugin threads remain. terminal=1 is authorized only by the
/// host's process-exit coordinator, never by ordinary disable. DSO destructors
/// must not unwind or reenter operations on this handle. Buffer is writable and
/// disjoint from module. This function does not prove the host's drain contract.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_native_close(
    module: *mut NativeModule,
    terminal: u32,
    buffer: *mut c_char,
    capacity: u32,
) -> i32 {
    let Some(mut diagnostic) = (unsafe { diagnostic(buffer, capacity) }) else {
        return INVALID;
    };
    unsafe {
        close_with(module, terminal, &mut diagnostic, |mapping, diagnostic| {
            platform::close(mapping, diagnostic)
        })
    }
}

// The injected operation is private and used to exercise close failure without
// handing dlclose a fabricated or already-closed (undefined behavior) handle.
unsafe fn close_with(
    module: *mut NativeModule,
    terminal: u32,
    diagnostic: &mut Diagnostic<'_>,
    close: impl FnOnce(*mut c_void, &mut Diagnostic<'_>) -> bool,
) -> i32 {
    let Some(value) = (unsafe { module.as_ref() }) else {
        diagnostic.set(b"invalid native module handle");
        return INVALID;
    };
    if terminal > 1 {
        diagnostic.set(b"invalid native close phase");
        return INVALID;
    }
    if value.published && terminal == 0 {
        diagnostic.set(b"published native module requires terminal shutdown");
        return STATE_MISMATCH;
    }
    if !close(value.mapping, diagnostic) {
        return IO_ERROR;
    }
    unsafe { drop(Box::from_raw(module)) };
    OK
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ffi_rejects_invalid_paths_and_initializes_outputs() {
        let mut output = ptr::dangling_mut();
        let mut buffer = [1i8; 64];
        for (path, length) in [
            (ptr::null(), 1),
            (b"x".as_ptr(), 0),
            (b"x\0y".as_ptr(), 3),
            (b"x".as_ptr(), MAX_PATH as u32 + 1),
        ] {
            assert_eq!(
                unsafe {
                    seekdb_runtime_native_open(
                        path,
                        length,
                        &mut output,
                        buffer.as_mut_ptr().cast(),
                        buffer.len() as u32,
                    )
                },
                INVALID
            );
            assert!(output.is_null());
            assert!(buffer.contains(&0));
        }
        assert_eq!(
            unsafe { seekdb_runtime_native_publish(ptr::null_mut()) },
            INVALID
        );
        let mut entry: Option<Entry> = Some(dummy_entry);
        assert_eq!(
            unsafe {
                seekdb_runtime_native_entry(
                    ptr::null_mut(),
                    &mut entry,
                    buffer.as_mut_ptr().cast(),
                    buffer.len() as u32,
                )
            },
            INVALID
        );
        assert!(entry.is_none());
    }

    unsafe extern "C" fn dummy_entry() -> *const c_void {
        ptr::null()
    }

    #[test]
    fn diagnostic_truncation_is_terminated_even_at_one_byte() {
        let mut bytes = [1; 1];
        Diagnostic(&mut bytes).set(b"long loader diagnostic");
        assert_eq!(bytes, [0]);
        let mut bytes = [1; 4];
        Diagnostic(&mut bytes).set(b"long loader diagnostic");
        assert_eq!(&bytes, b"lon\0");
    }

    fn module() -> *mut NativeModule {
        Box::into_raw(Box::new(NativeModule {
            mapping: ptr::null_mut(),
            published: false,
        }))
    }

    #[test]
    fn publication_disallows_abort_without_calling_os_close() {
        let module = module();
        let mut bytes = [0; 128];
        let mut diagnostic = Diagnostic(&mut bytes);
        assert_eq!(unsafe { seekdb_runtime_native_publish(module) }, OK);
        assert_eq!(unsafe { seekdb_runtime_native_publish(module) }, OK);
        assert_eq!(
            unsafe {
                close_with(module, 0, &mut diagnostic, |_, _| {
                    panic!("published module must remain mapped")
                })
            },
            STATE_MISMATCH
        );
        assert_eq!(
            unsafe {
                close_with(module, 2, &mut diagnostic, |_, _| {
                    panic!("invalid phase must not close")
                })
            },
            INVALID
        );
        assert!(unsafe { (*module).published });
        assert_eq!(
            unsafe { close_with(module, 1, &mut diagnostic, |_, _| true) },
            OK
        );
    }

    #[test]
    fn failed_close_retains_ownership_for_retry_in_both_phases() {
        for phase in [0, 1] {
            let module = module();
            if phase == 1 {
                assert_eq!(unsafe { seekdb_runtime_native_publish(module) }, OK);
            }
            let mut bytes = [0; 128];
            let mut diagnostic = Diagnostic(&mut bytes);
            assert_eq!(
                unsafe {
                    close_with(module, phase, &mut diagnostic, |_, diagnostic| {
                        diagnostic.set(b"test OS failure");
                        false
                    })
                },
                IO_ERROR
            );
            assert_eq!(unsafe { (*module).published }, phase == 1);
            assert_eq!(
                unsafe { close_with(module, phase, &mut diagnostic, |_, _| true) },
                OK
            );
        }
    }
}
