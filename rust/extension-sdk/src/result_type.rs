// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use crate::{sys, Result};
use std::{
    ffi::{c_char, CStr},
    marker::PhantomData,
    mem::size_of,
    rc::Rc,
};

pub(crate) fn validate_type(id: &CStr) -> Result<()> {
    let bytes = id.to_bytes();
    if bytes.is_empty()
        || bytes.len() > sys::MAX_IDENTIFIER_BYTES
        || !bytes.iter().all(|b| {
            b.is_ascii_lowercase() || b.is_ascii_digit() || matches!(b, b'.' | b'_' | b'-')
        })
    {
        Err(sys::INVALID)
    } else {
        Ok(())
    }
}

/// Metadata-only planning call. No argument values, SQL, transaction, or query
/// allocator are exposed. The resolver must be pure and generation-deterministic.
/// Borrowed types cannot outlive the call; the output is copied into host memory.
///
/// ```compile_fail
/// use seekdb_extension::TypeResolution;
/// fn needs_send<T: Send>() {}
/// needs_send::<TypeResolution<'static>>();
/// ```
/// ```compile_fail
/// use seekdb_extension::TypeResolution;
/// fn needs_sync<T: Sync>() {}
/// needs_sync::<TypeResolution<'static>>();
/// ```
pub struct TypeResolution<'call> {
    arguments: &'call [*const c_char],
    output: &'call mut sys::ResolvedType,
    _thread_bound: PhantomData<Rc<()>>,
}

impl<'call> TypeResolution<'call> {
    /// # Safety
    /// Arguments are a valid aligned array of count pointers, each NULL or a
    /// readable NUL-terminated C string, alive for 'call. Output is exclusively
    /// borrowed, aligned, initialized host memory for ResolvedType, disjoint
    /// from those inputs. No pointers are retained beyond the callback.
    pub unsafe fn from_raw(
        arguments: *const *const c_char,
        count: u32,
        output: *mut sys::ResolvedType,
    ) -> Result<Self> {
        if output.is_null() || count > 1024 || (count != 0 && arguments.is_null()) {
            return Err(sys::INVALID);
        }
        let output = unsafe { &mut *output };
        if output.struct_size != size_of::<sys::ResolvedType>() as u32 || output.reserved != [0; 4]
        {
            return Err(sys::UNSUPPORTED_ABI);
        }
        output.type_id.fill(0);
        let arguments = if count == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(arguments, count as usize) }
        };
        let call = Self {
            arguments,
            output,
            _thread_bound: PhantomData,
        };
        for i in 0..call.argument_count() {
            call.argument(i)?;
        }
        Ok(call)
    }

    pub fn argument_count(&self) -> usize {
        self.arguments.len()
    }

    pub fn argument(&self, index: usize) -> Result<Option<&'call CStr>> {
        let &pointer = self.arguments.get(index).ok_or(sys::INVALID)?;
        if pointer.is_null() {
            return Ok(None);
        }
        let id = unsafe { CStr::from_ptr(pointer) };
        validate_type(id)?;
        Ok(Some(id))
    }

    /// Consume the call: there is exactly one successful result assignment.
    /// The ID can be dynamically computed; it is copied before returning.
    pub fn finish(self, type_id: &CStr) -> Result<()> {
        validate_type(type_id)?;
        for (out, byte) in self
            .output
            .type_id
            .iter_mut()
            .zip(type_id.to_bytes_with_nul())
        {
            *out = *byte as c_char;
        }
        Ok(())
    }
}
