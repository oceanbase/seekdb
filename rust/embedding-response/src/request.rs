// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.

//! Request serialization. Text bytes are preserved, with JSON syntax and control
//! bytes escaped. As at the existing boundary, callers supply UTF-8 text.
use crate::{Encoding, ParseError, Result};

/// Build a compact request, allocating only after counting its serialized size.
/// Nonpositive dimensions are omitted. Empty strings and empty input are valid.
pub fn build_request(
    inputs: &[&[u8]],
    model: &[u8],
    dimension: i64,
    encoding: Encoding,
) -> Result<Vec<u8>> {
    let mut length = 0usize;
    serialize(inputs, model, dimension, encoding, |bytes| {
        length = length
            .checked_add(bytes.len())
            .ok_or(ParseError::BufferNotEnough)?;
        Ok(())
    })?;
    let mut output = Vec::new();
    output
        .try_reserve_exact(length)
        .map_err(|_| ParseError::AllocationFailed)?;
    serialize(inputs, model, dimension, encoding, |bytes| {
        output.extend_from_slice(bytes);
        Ok(())
    })?;
    debug_assert_eq!(length, output.len());
    Ok(output)
}

fn string(bytes: &[u8], emit: &mut impl FnMut(&[u8]) -> Result<()>) -> Result<()> {
    emit(b"\"")?;
    let mut start = 0;
    const HEX: &[u8] = b"0123456789abcdef";
    for (index, &byte) in bytes.iter().enumerate() {
        if byte < 0x20 || byte == b'"' || byte == b'\\' {
            emit(&bytes[start..index])?;
            match byte {
                b'"' => emit(b"\\\"")?,
                b'\\' => emit(b"\\\\")?,
                _ => emit(&[
                    b'\\',
                    b'u',
                    b'0',
                    b'0',
                    HEX[(byte >> 4) as usize],
                    HEX[(byte & 15) as usize],
                ])?,
            }
            start = index + 1;
        }
    }
    emit(&bytes[start..])?;
    emit(b"\"")
}

fn serialize(
    inputs: &[&[u8]],
    model: &[u8],
    dimension: i64,
    encoding: Encoding,
    mut emit: impl FnMut(&[u8]) -> Result<()>,
) -> Result<()> {
    emit(b"{\"input\":[")?;
    for (index, input) in inputs.iter().enumerate() {
        if index != 0 {
            emit(b",")?;
        }
        string(input, &mut emit)?;
    }
    emit(b"],\"model\":")?;
    string(model, &mut emit)?;
    emit(b",\"encoding_format\":")?;
    emit(match encoding {
        Encoding::Float => b"\"float\"",
        Encoding::Base64 => b"\"base64\"",
    })?;
    if dimension > 0 {
        emit(b",\"dimensions\":")?;
        let mut digits = [0u8; 19];
        let mut number = dimension;
        let mut start = digits.len();
        while number > 0 {
            start -= 1;
            digits[start] = b'0' + (number % 10) as u8;
            number /= 10;
        }
        emit(&digits[start..])?;
    }
    emit(b"}")
}
