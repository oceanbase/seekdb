// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Allocation-free startup policy parsing, shared by native host entry points.

use crate::{INVALID, OK};

/// Decimal bytes/counts, or `unlimited`. Byte limits additionally accept the
/// exact binary suffixes KiB/MiB/GiB/TiB. No trimming, signs or partial parses.
fn parse(input: &[u8], kind: u32) -> Option<u64> {
    if input.is_empty() || input.len() > 64 || kind > 1 {
        return None;
    }
    if input == b"unlimited" {
        return Some(u64::MAX);
    }
    let digits = input
        .iter()
        .take_while(|byte| byte.is_ascii_digit())
        .count();
    if digits == 0 {
        return None;
    }
    let multiplier = match (&input[digits..], kind) {
        (b"", _) => 1,
        (b"KiB", 0) => 1 << 10,
        (b"MiB", 0) => 1 << 20,
        (b"GiB", 0) => 1 << 30,
        (b"TiB", 0) => 1 << 40,
        _ => return None,
    };
    input[..digits]
        .iter()
        .try_fold(0_u64, |value, digit| {
            value.checked_mul(10)?.checked_add((digit - b'0') as u64)
        })?
        .checked_mul(multiplier)
}

/// Parse an administrator limit; output is zero on failure, never a partially
/// parsed value. kind=0 bytes, kind=1 live allocations. All other kinds fail.
///
/// # Safety
/// Non-null output must be writable/aligned for a u64 and disjoint from input.
/// For lengths 1..=64, non-null input must reference that many readable bytes
/// for this call. Neither pointer is retained. Null pointers are rejected.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_memory_parse_limit(
    input: *const u8,
    length: u32,
    kind: u32,
    output: *mut u64,
) -> i32 {
    if output.is_null() {
        return INVALID;
    }
    // SAFETY: caller provides an exclusive, aligned output slot.
    unsafe { output.write(0) };
    if input.is_null() || length == 0 || length > 64 || kind > 1 {
        return INVALID;
    }
    // SAFETY: bounded readable input is required by the caller contract.
    let input = unsafe { std::slice::from_raw_parts(input, length as usize) };
    match parse(input, kind) {
        Some(value) => {
            // SAFETY: same exclusive slot as above; disjoint from input.
            unsafe { output.write(value) };
            OK
        }
        None => INVALID,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn exact_decimal_binary_and_unlimited() {
        for kind in [0, 1] {
            for (text, value) in [
                ("0", 0),
                ("000123", 123),
                ("18446744073709551615", u64::MAX),
                ("unlimited", u64::MAX),
            ] {
                assert_eq!(parse(text.as_bytes(), kind), Some(value));
            }
        }
        for (suffix, factor) in [("KiB", 10), ("MiB", 20), ("GiB", 30), ("TiB", 40)] {
            assert_eq!(
                parse(format!("17{suffix}").as_bytes(), 0),
                Some(17 << factor)
            );
            assert_eq!(parse(format!("0{suffix}").as_bytes(), 0), Some(0));
            assert_eq!(parse(format!("1{suffix}").as_bytes(), 1), None);
            let maximum = u64::MAX >> factor;
            assert_eq!(
                parse(format!("{maximum}{suffix}").as_bytes(), 0),
                Some(maximum << factor)
            );
            assert_eq!(
                parse(format!("{}{suffix}", maximum + 1).as_bytes(), 0),
                None
            );
        }
    }

    #[test]
    fn rejects_ambiguous_truncated_and_overflowing_input() {
        for text in [
            "",
            "-1",
            "+1",
            " 1",
            "1 ",
            "1\n",
            "1\t",
            "1.5",
            "1e3",
            "0xff",
            "1KB",
            "1K",
            "1kb",
            "1kib",
            "1B",
            "KiB",
            "1MiBjunk",
            "unlimitedMiB",
            "Unlimited",
            "unlimited\0",
            "1\x002",
            "18446744073709551616",
            "99999999999999999999999999",
        ] {
            for kind in [0, 1] {
                assert_eq!(parse(text.as_bytes(), kind), None, "{text:?}");
            }
        }
        assert_eq!(parse(&[b'0'; 64], 0), Some(0));
        assert_eq!(parse(&[b'0'; 65], 0), None);
        assert_eq!(parse(&[255], 0), None);
        assert_eq!(parse(b"1", 2), None);
        assert_eq!(parse(b"unlimited", u32::MAX), None);
    }

    #[test]
    fn ffi_clears_failure_and_does_not_retain_input() {
        let mut output = 99;
        // SAFETY: all non-null inputs are valid for their stated bounded length;
        // oversize input is rejected before reading. Output is disjoint/aligned.
        unsafe {
            assert_eq!(
                seekdb_runtime_memory_parse_limit(b"2MiB".as_ptr(), 4, 0, &mut output),
                OK
            );
            assert_eq!(output, 2 << 20);
            for (input, len, kind) in [
                (std::ptr::null(), 1, 0),
                (b"x".as_ptr(), 1, 0),
                (b"0".as_ptr(), 0, 0),
                (b"0".as_ptr(), 65, 0),
                (b"0".as_ptr(), u32::MAX, 0),
                (b"0".as_ptr(), 1, 2),
            ] {
                output = 99;
                assert_eq!(
                    seekdb_runtime_memory_parse_limit(input, len, kind, &mut output),
                    INVALID
                );
                assert_eq!(output, 0);
            }
            assert_eq!(
                seekdb_runtime_memory_parse_limit(b"1".as_ptr(), 1, 0, std::ptr::null_mut()),
                INVALID
            );
        }
    }
}
