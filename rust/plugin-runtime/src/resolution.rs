// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! SQL overload and direct cast selection over one immutable host catalog snapshot. Borrowed
//! metadata only: no callbacks, locks or retained host pointers. Overload/direct
//! cast selection does not allocate; common-type selection uses bounded scratch.
//! Typed overloads precede legacy untyped fallbacks, then conversion cost wins.

use crate::{INVALID, OK};
use std::{mem, ptr, slice};
mod common;
pub use common::seekdb_runtime_resolve_common_type;

pub const NOT_FOUND: i32 = 8;
pub const AMBIGUOUS: i32 = 9;
const MAX_OBJECTS: usize = 4096;
const MAX_ARGUMENTS: usize = 1024;
const IMPLICIT: u32 = 3;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Text {
    data: *const u8,
    length: u32,
}

#[repr(C)]
pub struct Candidate {
    object_id: Text,
    signature: *const Text,
    signature_count: u32,
    minimum_arity: u32,
    maximum_arity: u32,
    reserved: u32,
}

#[repr(C)]
pub struct Cast {
    source: Text,
    target: Text,
    context: u32,
    cost: u32,
}

// Bounds/alignment checks catch adapter mistakes; as with the rest of the host
// bridge, the caller must still supply valid initialized memory for the range.
unsafe fn borrow<'a, T>(data: *const T, count: u32, maximum: usize) -> Result<&'a [T], i32> {
    if count as usize > maximum {
        return Err(INVALID);
    }
    if count == 0 {
        return Ok(&[]);
    }
    if data.is_null() || !(data as usize).is_multiple_of(mem::align_of::<T>()) {
        return Err(INVALID);
    }
    Ok(unsafe { slice::from_raw_parts(data, count as usize) })
}

unsafe fn identifier<'a>(text: &Text, unknown_allowed: bool) -> Result<&'a [u8], i32> {
    if unknown_allowed && text.data.is_null() && text.length == 0 {
        return Ok(&[]);
    }
    let bytes = unsafe { borrow(text.data, text.length, 255) }?;
    if bytes.is_empty()
        || !bytes
            .iter()
            .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || b"._-".contains(c))
    {
        return Err(INVALID);
    }
    Ok(bytes)
}

unsafe fn resolve(
    candidates: &[Candidate],
    casts: &[Cast],
    arguments: &[Text],
    check_arity: bool,
) -> Result<usize, i32> {
    let mut known_argument = false;
    for argument in arguments {
        known_argument |= !unsafe { identifier(argument, true) }?.is_empty();
    }
    // Validate independently of candidate order or whether a match exists.
    for cast in casts {
        unsafe { identifier(&cast.source, false) }?;
        unsafe { identifier(&cast.target, false) }?;
        if !(1..=IMPLICIT).contains(&cast.context) {
            return Err(INVALID);
        }
    }
    let mut best: Option<(usize, (bool, u64), &[u8])> = None;
    let mut ambiguous = false;
    for (index, candidate) in candidates.iter().enumerate() {
        let object_id = unsafe { identifier(&candidate.object_id, false) }?;
        if candidate.reserved != 0
            || candidate.minimum_arity > candidate.maximum_arity
            || candidate.maximum_arity as usize > MAX_ARGUMENTS
        {
            return Err(INVALID);
        }
        let signature = unsafe {
            borrow(
                candidate.signature,
                candidate.signature_count,
                MAX_ARGUMENTS,
            )
        }?;
        for item in signature {
            unsafe { identifier(item, false) }?;
        }
        if check_arity
            && (arguments.len() < candidate.minimum_arity as usize
                || arguments.len() > candidate.maximum_arity as usize)
        {
            continue;
        }

        let mut cost = 0u64;
        let mut compatible = true;
        for (position, argument) in arguments.iter().enumerate() {
            if signature.is_empty() {
                continue;
            }
            let actual = unsafe { identifier(argument, true) }?;
            if actual.is_empty() {
                cost += 1;
                continue;
            }
            // Normalization validates variadic/optional signatures. For a
            // variadic tail the last declared type repeats, as in the C++ SPI.
            let expected =
                unsafe { identifier(&signature[position.min(signature.len() - 1)], false) }?;
            if actual == expected {
                continue;
            }
            let mut best_cast: Option<u32> = None;
            for cast in casts {
                if cast.context == IMPLICIT
                    && unsafe { identifier(&cast.source, false) }? == actual
                    && unsafe { identifier(&cast.target, false) }? == expected
                {
                    best_cast = Some(best_cast.map_or(cast.cost, |old| old.min(cast.cost)));
                }
            }
            match best_cast {
                Some(conversion) => cost += 1 + u64::from(conversion),
                None => {
                    compatible = false;
                    break;
                }
            }
        }
        if !compatible {
            continue;
        }
        // A numeric magic cost cannot implement fallback: a legal u32 cast
        // cost can exceed it. Specificity is a separate ordering dimension.
        let score = (signature.is_empty(), cost);
        match best {
            None => {
                best = Some((index, score, object_id));
                ambiguous = false;
            }
            Some((_, prior, _)) if score < prior => {
                best = Some((index, score, object_id));
                ambiguous = false;
            }
            Some((_, prior, prior_id)) if score == prior => {
                if known_argument || arguments.is_empty() {
                    ambiguous = true;
                } else if object_id < prior_id {
                    // Name/arity probing happens before child typing. Stable
                    // object order proves existence, not an execution binding.
                    best = Some((index, score, object_id));
                }
            }
            _ => {}
        }
    }
    if ambiguous {
        Err(AMBIGUOUS)
    } else {
        best.map(|(index, _, _)| index).ok_or(NOT_FOUND)
    }
}

/// Select one candidate; NOT_FOUND/AMBIGUOUS leave selected at u32::MAX.
///
/// # Safety
/// All nonempty spans/arrays must be valid initialized, immutable memory for
/// the duration of this call. `selected` must be a writable aligned u32 and
/// must not alias inputs. No input pointer is retained. The host must supply
/// one name/kind/namespace-filtered snapshot with normalized signatures.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_resolve_sql(
    candidates: *const Candidate,
    candidate_count: u32,
    casts: *const Cast,
    cast_count: u32,
    arguments: *const Text,
    argument_count: u32,
    check_arity: u8,
    selected: *mut u32,
) -> i32 {
    if selected.is_null() || !(selected as usize).is_multiple_of(mem::align_of::<u32>()) {
        return INVALID;
    }
    unsafe { selected.write(u32::MAX) };
    if check_arity > 1 {
        return INVALID;
    }
    let result = (|| {
        let candidates = unsafe { borrow(candidates, candidate_count, MAX_OBJECTS) }?;
        let casts = unsafe { borrow(casts, cast_count, MAX_OBJECTS) }?;
        let arguments = unsafe { borrow(arguments, argument_count, MAX_ARGUMENTS) }?;
        unsafe { resolve(candidates, casts, arguments, check_arity != 0) }
    })();
    match result {
        Ok(index) => {
            unsafe { ptr::write(selected, index as u32) };
            OK
        }
        Err(error) => error,
    }
}

/// Select a direct conversion permitted in the requested SQL context. A cast
/// declared IMPLICIT is also available in ASSIGNMENT/EXPLICIT contexts, never
/// the reverse. Equal minimum costs are ambiguous; input order is not policy.
/// Identity/unknown-NULL coercions are handled by the SQL caller, not invented
/// as callbacks here. Validate the entire snapshot even after finding a match.
///
/// # Safety
/// Nonempty input spans must be initialized immutable memory for this call.
/// `selected` must be writable, aligned and disjoint from every input span.
/// No input pointer is retained, no allocation or plugin callback occurs.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_resolve_cast(
    casts: *const Cast,
    cast_count: u32,
    source: Text,
    target: Text,
    requested_context: u32,
    selected: *mut u32,
) -> i32 {
    if selected.is_null() || !(selected as usize).is_multiple_of(mem::align_of::<u32>()) {
        return INVALID;
    }
    unsafe { selected.write(u32::MAX) };
    let result = (|| {
        if !(1..=IMPLICIT).contains(&requested_context) {
            return Err(INVALID);
        }
        let source = unsafe { identifier(&source, false) }?;
        let target = unsafe { identifier(&target, false) }?;
        let casts = unsafe { borrow(casts, cast_count, MAX_OBJECTS) }?;
        let mut best: Option<(usize, u32)> = None;
        let mut ambiguous = false;
        for (index, cast) in casts.iter().enumerate() {
            let from = unsafe { identifier(&cast.source, false) }?;
            let to = unsafe { identifier(&cast.target, false) }?;
            if !(1..=IMPLICIT).contains(&cast.context) {
                return Err(INVALID);
            }
            if from != source || to != target || cast.context < requested_context {
                continue;
            }
            match best {
                None => best = Some((index, cast.cost)),
                Some((_, cost)) if cast.cost < cost => {
                    best = Some((index, cast.cost));
                    ambiguous = false;
                }
                Some((_, cost)) if cast.cost == cost => ambiguous = true,
                _ => {}
            }
        }
        if ambiguous {
            Err(AMBIGUOUS)
        } else {
            best.map(|(index, _)| index).ok_or(NOT_FOUND)
        }
    })();
    match result {
        Ok(index) => {
            unsafe { selected.write(index as u32) };
            OK
        }
        Err(error) => error,
    }
}

#[cfg(test)]
mod tests;
