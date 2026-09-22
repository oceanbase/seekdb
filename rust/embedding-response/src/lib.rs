// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.

//! Embedding response parsing, independent of HTTP, task state and C++ allocation.
//!
//! [`parse_into`] appends complete vectors in response order. A semantic error in
//! a later item leaves earlier vectors appended; invalid JSON appends nothing.
//! Returned vectors are owned by the caller and use Rust's allocator. The separate
//! `embedding-response-ffi` crate contains the C ABI; this core stays safe Rust.

#![forbid(unsafe_code)]

mod base64;
mod json;
mod number;

use json::Value;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Encoding {
    Float,
    Base64,
}

/// Failure categories retain the error codes of `EmbeddingResponseParser`.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ParseError {
    InvalidArgument,
    AllocationFailed,
    DimensionMismatch,
    BufferNotEnough,
    MissingField,
    InvalidJson,
}

impl ParseError {
    /// Values from `src/oblib/lib/ob_errno.h`.
    pub const fn ob_error_code(self) -> i32 {
        match self {
            Self::InvalidArgument => -4002,
            Self::AllocationFailed => -4013,
            Self::DimensionMismatch => -4016,
            Self::BufferNotEnough => -4024,
            Self::MissingField => -4182,
            Self::InvalidJson => -5411,
        }
    }
}

impl std::fmt::Display for ParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?} ({})", self.ob_error_code())
    }
}

impl std::error::Error for ParseError {}

type Result<T> = std::result::Result<T, ParseError>;

fn reserve<T>(values: &mut Vec<T>, additional: usize) -> Result<()> {
    values
        .try_reserve(additional)
        .map_err(|_| ParseError::AllocationFailed)
}

fn push<T>(values: &mut Vec<T>, value: T) -> Result<()> {
    reserve(values, 1)?;
    values.push(value);
    Ok(())
}

/// Parse and append embeddings without clearing `output`.
///
/// Every JSON value is validated before extracting `data`, including ignored
/// metadata and overwritten duplicate fields. Duplicate keys use the last value.
/// The `index` field is ignored, as in the existing C++ implementation.
///
/// Errors leave existing output and earlier complete embeddings intact. Empty
/// `data` succeeds regardless of `dimension`. Empty input is an invalid argument.
/// Base64 encodes native-endian IEEE 754 floats, preserving all bits, including NaNs.
pub fn parse_into(
    response: &[u8],
    dimension: i64,
    encoding: Encoding,
    output: &mut Vec<Vec<f32>>,
) -> Result<()> {
    parse_with(response, dimension, encoding, |vector| push(output, vector))
}

/// Parse and synchronously emit each complete vector to `emit`.
///
/// All JSON is validated before the first call. A parse error or sink error stops
/// processing immediately; a sink error is returned unchanged. The sink owns each
/// vector, so it may move it into output or copy it before dropping it. This lets
/// the C++ adapter keep at most one decoded Rust vector live at a time.
pub fn parse_with<E: From<ParseError>>(
    response: &[u8],
    dimension: i64,
    encoding: Encoding,
    mut emit: impl FnMut(Vec<f32>) -> std::result::Result<(), E>,
) -> std::result::Result<(), E> {
    if response.is_empty() {
        return Err(ParseError::InvalidArgument.into());
    }
    let root = json::parse(response)?;
    let data = root.field(b"data")?.array()?;
    for item in data {
        let embedding = item.field(b"embedding")?;
        let vector = match encoding {
            Encoding::Float => {
                let values = embedding.array()?;
                if usize::try_from(dimension).ok() != Some(values.len()) {
                    return Err(ParseError::DimensionMismatch.into());
                }
                // The task's ObArenaAllocator returns null for alloc(0).
                // Keep that error instead of accepting a zero-dimensional vector.
                if values.is_empty() {
                    return Err(ParseError::AllocationFailed.into());
                }
                let mut vector = Vec::new();
                reserve(&mut vector, values.len())?;
                for value in values {
                    match value {
                        Value::Number(number) => vector.push(*number),
                        _ => return Err(ParseError::InvalidArgument.into()),
                    }
                }
                vector
            }
            Encoding::Base64 => match embedding {
                Value::String(bytes) => base64::decode(bytes, dimension)?,
                _ => return Err(ParseError::InvalidArgument.into()),
            },
        };
        emit(vector)?;
    }
    Ok(())
}
