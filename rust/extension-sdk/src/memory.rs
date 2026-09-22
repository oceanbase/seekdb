// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Explicit module-owned bytes, not a global allocator or a query arena.
//! Borrowed buffers are thread-bound and use matching host.free. Optional owned
//! tokens retain their account independently and support transfer between threads.
//! Neither kind acquires a plugin-code lease or retains a query context.

use crate::{sys, Result};
use std::alloc::Layout;
use std::marker::PhantomData;
use std::mem::size_of;
use std::ops::{Deref, DerefMut};
use std::ptr::{self, NonNull};
use std::rc::Rc;

/// A borrowed, explicit host allocator. Its unsafe constructor establishes the
/// native lifetime contract; borrowed buffers cannot outlive this wrapper.
///
/// ```compile_fail
/// use seekdb_extension::memory::{HostAllocator, HostBuffer};
/// fn escape<'a>(allocator: HostAllocator<'a>) -> HostBuffer<'a> {
///     allocator.zeroed(16, 8).unwrap()
/// }
/// ```
/// ```compile_fail
/// use seekdb_extension::memory::HostAllocator;
/// fn send<T: Send>() {}
/// send::<HostAllocator<'static>>();
/// ```
pub struct HostAllocator<'host> {
    api: &'host sys::HostApiV1,
    raw: *const sys::HostApiV1,
    _thread: PhantomData<Rc<()>>,
}

impl<'host> HostAllocator<'host> {
    /// # Safety
    /// Non-null `host` is aligned with a readable size field and covers its
    /// advertised size. The v1 table and opaque host remain alive and immutable
    /// for `'host`, and alloc/free are callable on this thread throughout it.
    /// A successful allocation is exclusive, writable for the requested size
    /// and alignment, and remains valid until matching free. Callbacks must not
    /// unwind. The caller must hold the module's execution/lifecycle authority;
    /// this borrow itself does not pin the module or extend a query lifetime.
    /// If v3 owned bytes are advertised, their ownership/release callbacks obey
    /// memory_spi.h: independent account lifetime, thread-safe release with host
    /// code kept mapped until all owned tokens are released, no plugin callbacks.
    pub unsafe fn from_raw(host: *const sys::HostApiV1) -> Result<Self> {
        if host.is_null() {
            return Err(sys::INVALID);
        }
        // Check the advertised prefix before creating a full-table reference.
        if unsafe { ptr::addr_of!((*host).struct_size).read() } < size_of::<sys::HostApiV1>() as u32
        {
            return Err(sys::UNSUPPORTED_ABI);
        }
        let api = unsafe { &*host };
        if api.abi_major != 1 || api.alloc.is_none() || api.free.is_none() {
            return Err(sys::UNSUPPORTED_ABI);
        }
        if api.host_handle.is_null() {
            return Err(sys::INVALID);
        }
        Ok(Self {
            api,
            raw: host,
            _thread: PhantomData,
        })
    }

    // Never expose a slice over uninitialized memory. Both constructors below
    // initialize the whole allocation before returning a safe HostBuffer.
    fn allocate(&self, length: usize, alignment: u32) -> Result<HostBuffer<'_>> {
        Layout::from_size_align(length, alignment as usize).map_err(|_| sys::INVALID)?;
        let bytes = u64::try_from(length).map_err(|_| sys::INVALID)?;
        let pointer = if length == 0 {
            // Layout validated a nonzero alignment. An aligned, non-null
            // dangling address is sufficient for an empty slice; never free or
            // dereference it. Preserve requested alignment even without a host call.
            NonNull::new(alignment as usize as *mut u8).unwrap()
        } else {
            NonNull::new(unsafe {
                self.api.alloc.unwrap()(self.api.host_handle, bytes, alignment).cast::<u8>()
            })
            .ok_or(sys::NO_MEMORY)?
        };
        Ok(HostBuffer {
            api: self.api,
            pointer,
            length,
            alignment,
            _thread: PhantomData,
        })
    }

    /// Initialized writable bytes. Alignment must be a nonzero power of two,
    /// including for empty buffers. Empty buffers do not consume host quota.
    pub fn zeroed(&self, length: usize, alignment: u32) -> Result<HostBuffer<'_>> {
        let buffer = self.allocate(length, alignment)?;
        unsafe { buffer.pointer.as_ptr().write_bytes(0, length) };
        Ok(buffer)
    }

    pub fn copy_from_slice(&self, bytes: &[u8]) -> Result<HostBuffer<'_>> {
        self.copy_from_slices(&[bytes])
    }

    fn owned_api(&self) -> Result<&sys::HostApiV3> {
        if self.api.struct_size < size_of::<sys::HostApiV3>() as u32 {
            return Err(sys::UNSUPPORTED_ABI);
        }
        // Original advertised allocation, not a pointer widened from &v1.
        let api = unsafe { &*self.raw.cast::<sys::HostApiV3>() };
        if api.memory_spi_major != 1 || api.allocate_owned_bytes.is_none() {
            return Err(sys::UNSUPPORTED_ABI);
        }
        Ok(api)
    }

    /// Require the optional independent byte ownership protocol. Never fake an
    /// owned allocation by extending a v1 borrowed allocator to 'static.
    pub fn require_owned(&self) -> Result<()> {
        self.owned_api().map(|_| ())
    }

    fn allocate_owned(&self, length: usize, alignment: u32) -> Result<OwnedHostBuffer> {
        let api = self.owned_api()?;
        Layout::from_size_align(length, alignment as usize).map_err(|_| sys::INVALID)?;
        let size = u64::try_from(length).map_err(|_| sys::INVALID)?;
        let mut result = OwnedHostBuffer {
            raw: sys::OwnedBytesV1::default(),
        };
        if length == 0 {
            result.raw.data = alignment as usize as *mut u8;
            result.raw.alignment = alignment;
            return Ok(result);
        }
        crate::status(unsafe {
            api.allocate_owned_bytes.unwrap()(
                self.api.host_handle,
                size,
                alignment,
                &mut result.raw,
            )
        })?;
        if result.raw.struct_size != size_of::<sys::OwnedBytesV1>() as u32
            || result.raw.size != size
            || result.raw.alignment != alignment
            || result.raw.data.is_null()
            || !(result.raw.data as usize).is_multiple_of(alignment as usize)
            || result.raw.owner.is_null()
            || result.raw.release.is_none()
            || result.raw.reserved != [0; 4]
        {
            return Err(sys::INVALID);
        }
        Ok(result)
    }

    /// Initialized bytes owning an independent host account token. They may
    /// outlive this wrapper and move between threads, but do not pin plugin code.
    pub fn owned_zeroed(&self, length: usize, alignment: u32) -> Result<OwnedHostBuffer> {
        let buffer = self.allocate_owned(length, alignment)?;
        unsafe { buffer.raw.data.write_bytes(0, length) };
        Ok(buffer)
    }

    pub fn owned_copy_from_slice(&self, bytes: &[u8]) -> Result<OwnedHostBuffer> {
        let buffer = self.allocate_owned(bytes.len(), 1)?;
        unsafe { ptr::copy_nonoverlapping(bytes.as_ptr(), buffer.raw.data, bytes.len()) };
        Ok(buffer)
    }

    /// Copy borrowed inputs into one host-owned allocation, without a
    /// temporary Vec or a redundant zero-fill. Input borrows are not retained.
    pub fn copy_from_slices(&self, parts: &[&[u8]]) -> Result<HostBuffer<'_>> {
        let length = parts.iter().try_fold(0usize, |length, part| {
            length.checked_add(part.len()).ok_or(sys::INVALID)
        })?;
        let buffer = self.allocate(length, 1)?;
        let mut offset = 0;
        for part in parts {
            // A conforming host returns fresh, exclusive storage. Empty copies
            // still use non-null aligned byte pointers, including empty storage.
            unsafe {
                ptr::copy_nonoverlapping(
                    part.as_ptr(),
                    buffer.pointer.as_ptr().add(offset),
                    part.len(),
                );
            }
            offset += part.len();
        }
        Ok(buffer)
    }
}

/// Exclusive, initialized bytes backed by the optional host-owned token SPI.
/// Drop calls host code, never a plugin destructor or a borrowed HostContext.
/// Send/Sync permit owned transfer/immutable sharing; mutable byte access still
/// requires &mut. A plugin task or cursor independently needs its code lease.
pub struct OwnedHostBuffer {
    raw: sys::OwnedBytesV1,
}
// The owned SPI guarantees account/release lifetime independent of the original
// HostContext and thread-safe release. No other owner may access mutable bytes.
unsafe impl Send for OwnedHostBuffer {}
unsafe impl Sync for OwnedHostBuffer {}
impl Deref for OwnedHostBuffer {
    type Target = [u8];
    fn deref(&self) -> &[u8] {
        unsafe { std::slice::from_raw_parts(self.raw.data, self.raw.size as usize) }
    }
}
impl DerefMut for OwnedHostBuffer {
    fn deref_mut(&mut self) -> &mut [u8] {
        unsafe { std::slice::from_raw_parts_mut(self.raw.data, self.raw.size as usize) }
    }
}
impl AsRef<[u8]> for OwnedHostBuffer {
    fn as_ref(&self) -> &[u8] {
        self
    }
}
impl Drop for OwnedHostBuffer {
    fn drop(&mut self) {
        if let Some(release) = self.raw.release {
            if !self.raw.owner.is_null() {
                unsafe { release(self.raw.owner) };
            }
        }
    }
}

/// Fixed-length, initialized bytes. Not Clone, Send, Sync, or a Rust Vec.
/// `Drop` returns the exact original size/alignment to the original host even
/// after early errors or unwind-mode panic. It cannot recover from abort.
///
/// ```compile_fail
/// use seekdb_extension::memory::HostBuffer;
/// fn send<T: Send>() {}
/// send::<HostBuffer<'static>>();
/// ```
pub struct HostBuffer<'allocator> {
    api: &'allocator sys::HostApiV1,
    pointer: NonNull<u8>,
    length: usize,
    alignment: u32,
    _thread: PhantomData<Rc<()>>,
}

impl Deref for HostBuffer<'_> {
    type Target = [u8];
    fn deref(&self) -> &[u8] {
        unsafe { std::slice::from_raw_parts(self.pointer.as_ptr(), self.length) }
    }
}
impl DerefMut for HostBuffer<'_> {
    fn deref_mut(&mut self) -> &mut [u8] {
        unsafe { std::slice::from_raw_parts_mut(self.pointer.as_ptr(), self.length) }
    }
}
impl AsRef<[u8]> for HostBuffer<'_> {
    fn as_ref(&self) -> &[u8] {
        self
    }
}
impl Drop for HostBuffer<'_> {
    fn drop(&mut self) {
        if self.length != 0 {
            unsafe {
                self.api.free.unwrap()(
                    self.api.host_handle,
                    self.pointer.as_ptr().cast(),
                    self.length as u64,
                    self.alignment,
                );
            }
        }
    }
}
