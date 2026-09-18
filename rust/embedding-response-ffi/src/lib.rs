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
