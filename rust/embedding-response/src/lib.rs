// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.

//! Embedding request serialization and response parsing, independent of HTTP,
//! task state and C++ allocation.
//!
//! [`parse_into`] appends complete vectors in response order. A semantic error in
//! a later item leaves earlier vectors appended; invalid JSON appends nothing.
//! Returned vectors are owned by the caller and use Rust's allocator.
//! The C ABI lives in `ffi`; parsing and serialization modules stay safe Rust.

#![deny(unsafe_code)]

#[allow(unsafe_code)]
pub mod ffi;

mod base64;
mod json;
mod number;
mod request;

pub use request::build_request;

use json::Value;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Encoding {
    Float,
    Base64,
}

/// Parser failure categories; the C++ adapter maps these to server errors.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ParseError {
    InvalidArgument,
    AllocationFailed,
    DimensionMismatch,
    BufferNotEnough,
    MissingField,
    InvalidJson,
}

impl std::fmt::Display for ParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?}")
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn whole_json_validation_and_partial_semantic_results() {
        let mut vectors = vec![vec![42.0]];
        assert_eq!(
            parse_into(
                br#"{"data":[{"embedding":[1]},{}]}"#,
                1,
                Encoding::Float,
                &mut vectors
            ),
            Err(ParseError::MissingField)
        );
        assert_eq!(vectors, vec![vec![42.0], vec![1.0]]);
        assert_eq!(
            parse_into(
                br#"{"data":[{"embedding":[2]}]} trailing"#,
                1,
                Encoding::Float,
                &mut vectors
            ),
            Err(ParseError::InvalidJson)
        );
        assert_eq!(vectors.len(), 2);
    }

    #[test]
    fn float_and_base64_keep_vector_bits() {
        let mut vectors = Vec::new();
        parse_into(
            br#"{"data":[{"embedding":[-0.0,1.25,-2.5]}]}"#,
            3,
            Encoding::Float,
            &mut vectors,
        )
        .unwrap();
        assert_eq!(
            vectors[0].iter().map(|v| v.to_bits()).collect::<Vec<_>>(),
            vec![
                (-0.0_f32).to_bits(),
                1.25_f32.to_bits(),
                (-2.5_f32).to_bits()
            ]
        );
        vectors.clear();
        parse_into(
            br#"{"data":[{"embedding":"AAAAAA=="}]}"#,
            1,
            Encoding::Base64,
            &mut vectors,
        )
        .unwrap();
        assert_eq!(vectors[0][0].to_bits(), 0);
        assert_eq!(
            parse_into(
                br#"{"data":[{"embedding":[1]}]}"#,
                2,
                Encoding::Float,
                &mut vectors
            ),
            Err(ParseError::DimensionMismatch)
        );
    }
}
