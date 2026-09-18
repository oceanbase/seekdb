// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.

use crate::{reserve, ParseError, Result};

// Match ObBase64Encoder::decode with skip_spaces=false, including its permissive
// padding and strict residual-buffer check. A canonical base64 decoder would
// change both the accepted language and the errors returned by existing callers.
pub(super) fn decode(input: &[u8], dimension: i64) -> Result<Vec<f32>> {
    let capacity = input.len() / 4 * 3;
    if capacity == 0 {
        return Err(ParseError::InvalidArgument);
    }
    let mut bytes = Vec::new();
    reserve(&mut bytes, capacity)?;
    let mut group = [0_u8; 4];
    let mut count = 0;
    let mut position = 0;
    while position < input.len() && input[position] != b'=' {
        group[count] = match input[position] {
            b'A'..=b'Z' => input[position] - b'A',
            b'a'..=b'z' => input[position] - b'a' + 26,
            b'0'..=b'9' => input[position] - b'0' + 52,
            b'+' => 62,
            b'/' => 63,
            _ => return Err(ParseError::InvalidArgument),
        };
        count += 1;
        position += 1;
        if count == 4 {
            bytes.extend_from_slice(&[
                (group[0] << 2) | (group[1] >> 4),
                (group[1] << 4) | (group[2] >> 2),
                (group[2] << 6) | group[3],
            ]);
            count = 0;
        }
    }
    if input.len() - position > 2 || input[position..].iter().any(|&byte| byte != b'=') {
        return Err(ParseError::InvalidArgument);
    }
    if count > 0 {
        // Equivalent to the legacy pos + i - 1 >= output_len check.
        if bytes.len() + count > capacity {
            return Err(ParseError::BufferNotEnough);
        }
        if count >= 2 {
            bytes.push((group[0] << 2) | (group[1] >> 4));
        }
        if count >= 3 {
            bytes.push((group[1] << 4) | (group[2] >> 2));
        }
    }
    if usize::try_from(dimension)
        .ok()
        .and_then(|dim| dim.checked_mul(size_of::<f32>()))
        != Some(bytes.len())
    {
        return Err(ParseError::DimensionMismatch);
    }
    let mut vector = Vec::new();
    reserve(&mut vector, bytes.len() / 4)?;
    for chunk in bytes.chunks_exact(4) {
        vector.push(f32::from_ne_bytes([chunk[0], chunk[1], chunk[2], chunk[3]]));
    }
    Ok(vector)
}
