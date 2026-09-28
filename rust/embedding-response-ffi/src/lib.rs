// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.

//! Thin synchronous ABI. See `include/embedding_response.h` for ownership rules.
#![deny(unsafe_op_in_unsafe_fn, improper_ctypes_definitions)]

use embedding_response::{parse_with, Encoding, ParseError};
use std::ffi::c_void;

/// Called only during parse, once per complete vector, until it returns nonzero.
pub type EmitVector = unsafe extern "C" fn(*mut c_void, *const f32, usize) -> i32;

struct Error(i32);

impl From<ParseError> for Error {
    fn from(error: ParseError) -> Self {
        Self(error.ob_error_code())
    }
}

/// Parse an embedding response and synchronously emit borrowed vectors.
///
/// # Safety
/// `data` must identify `length` readable bytes in one allocation, unchanged
/// until return. `emit` and `context` must obey the contract in the C header:
/// the callback must not unwind, retain the vector pointer, mutate the input, or
/// release objects still used by this call. The context itself may be null if
/// the callback supports that. Calls sharing mutable state require caller locking.
#[no_mangle]
pub unsafe extern "C" fn seekdb_embedding_response_parse(
    data: *const u8,
    length: usize,
    dimension: i64,
    encoding: u32,
    context: *mut c_void,
    emit: Option<EmitVector>,
) -> i32 {
    let invalid = ParseError::InvalidArgument.ob_error_code();
    if data.is_null() || length == 0 || length > isize::MAX as usize {
        return invalid;
    }
    let encoding = match encoding {
        0 => Encoding::Float,
        1 => Encoding::Base64,
        _ => return invalid,
    };
    let Some(emit) = emit else { return invalid };
    // SAFETY: the caller provides the readable allocation; the checks above
    // establish non-nullness and the slice size bound. The slice is not retained.
    let response = unsafe { std::slice::from_raw_parts(data, length) };
    let result = parse_with::<Error>(response, dimension, encoding, |vector| {
        // SAFETY: the vector remains live throughout the synchronous callback;
        // its pointer is aligned and valid for vector.len() floats. The caller
        // guarantees the callback/context contract and no exception unwinding.
        let code = unsafe { emit(context, vector.as_ptr(), vector.len()) };
        if code == 0 {
            Ok(())
        } else {
            Err(Error(code))
        }
    });
    result.map_or_else(|error| error.0, |()| 0)
}

/// Borrowed text descriptor; null is permitted only for zero length.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct EmbeddingBytes {
    pub data: *const u8,
    pub length: usize,
}

pub type EmitBody = unsafe extern "C" fn(*mut c_void, *const u8, usize) -> i32;

// SAFETY: callers must guarantee readable, unchanged bytes until the call ends.
unsafe fn borrow_bytes<'a>(value: EmbeddingBytes) -> Result<&'a [u8], ParseError> {
    if value.length > isize::MAX as usize || (value.length != 0 && value.data.is_null()) {
        Err(ParseError::InvalidArgument)
    } else if value.length == 0 {
        Ok(&[])
    } else {
        // SAFETY: non-nullness and size checked above; caller owns the allocation.
        Ok(unsafe { std::slice::from_raw_parts(value.data, value.length) })
    }
}

/// Serialize a request and synchronously emit its borrowed body.
///
/// # Safety
/// The descriptors and their bytes must be readable and unchanged until return.
/// `inputs` must identify `count` aligned descriptors (or null for zero count).
/// The callback/context must obey the ownership and no-unwind contract in
/// `include/embedding_request.h`. Calls sharing mutable state require locking.
#[no_mangle]
pub unsafe extern "C" fn seekdb_embedding_request_build(
    inputs: *const EmbeddingBytes,
    count: usize,
    model: EmbeddingBytes,
    dimension: i64,
    encoding: u32,
    context: *mut c_void,
    emit: Option<EmitBody>,
) -> i32 {
    let result = (|| -> Result<(), Error> {
        let encoding = match encoding {
            0 => Encoding::Float,
            1 => Encoding::Base64,
            _ => return Err(ParseError::InvalidArgument.into()),
        };
        let emit = emit.ok_or(ParseError::InvalidArgument)?;
        if count > isize::MAX as usize / std::mem::size_of::<EmbeddingBytes>()
            || (count != 0 && (inputs.is_null() || !inputs.is_aligned()))
        {
            return Err(ParseError::InvalidArgument.into());
        }
        let descriptors = if count == 0 {
            &[]
        } else {
            // SAFETY: size/alignment checked above; caller provides descriptors.
            unsafe { std::slice::from_raw_parts(inputs, count) }
        };
        // SAFETY: caller provides the model allocation, borrowed until return.
        let model = unsafe { borrow_bytes(model)? };
        let mut texts = Vec::new();
        texts
            .try_reserve_exact(count)
            .map_err(|_| ParseError::AllocationFailed)?;
        for &descriptor in descriptors {
            // SAFETY: caller provides each text allocation, borrowed until return.
            texts.push(unsafe { borrow_bytes(descriptor)? });
        }
        let body = embedding_response::build_request(&texts, model, dimension, encoding)?;
        // SAFETY: body is live throughout the synchronous callback. Caller
        // guarantees no unwinding or retaining the pointer after return.
        let code = unsafe { emit(context, body.as_ptr(), body.len()) };
        if code == 0 {
            Ok(())
        } else {
            Err(Error(code))
        }
    })();
    result.map_or_else(|error| error.0, |()| 0)
}
