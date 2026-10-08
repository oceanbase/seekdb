// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use super::{
    borrow, identifier, Cast, Text, AMBIGUOUS, IMPLICIT, MAX_ARGUMENTS, MAX_OBJECTS, NOT_FOUND,
};
use crate::{registration::NO_MEMORY, INVALID, OK};
use std::mem;

unsafe fn common_type(arguments: &[Text], casts: &[Cast]) -> Result<usize, i32> {
    let mut types = Vec::new();
    types
        .try_reserve_exact(arguments.len())
        .map_err(|_| NO_MEMORY)?;
    for (index, argument) in arguments.iter().enumerate() {
        let id = unsafe { identifier(argument, true) }?;
        if !id.is_empty() {
            types.push((id, index));
        }
    }
    types.sort_unstable();
    // Keep the first input position for each identity. Repeated branches do
    // not vote for a different result type or create a spurious target tie.
    types.dedup_by(|next, prior| next.0 == prior.0);
    let mut edges = Vec::new();
    edges
        .try_reserve_exact(casts.len())
        .map_err(|_| NO_MEMORY)?;
    for cast in casts {
        let source = unsafe { identifier(&cast.source, false) }?;
        let target = unsafe { identifier(&cast.target, false) }?;
        if !(1..=IMPLICIT).contains(&cast.context) {
            return Err(INVALID);
        }
        if cast.context != IMPLICIT || source == target {
            continue;
        }
        if let (Ok(source), Ok(target)) = (
            types.binary_search_by(|(id, _)| id.cmp(&source)),
            types.binary_search_by(|(id, _)| id.cmp(&target)),
        ) {
            edges.push((target, source, cast.cost));
        }
    }
    // One pass over grouped edges, not arguments² × casts repeated lookup.
    // The first cost for each (target, source) is its cheapest direct cast.
    edges.sort_unstable();
    let mut position = 0;
    let mut best: Option<(usize, u64)> = None;
    let mut ambiguous = false;
    for (target, (_, original)) in types.iter().enumerate() {
        let mut sources = 1; // Same-type identity needs no registered callback.
        let mut total = 0u64;
        let mut cast_tie = false;
        while position < edges.len() && edges[position].0 == target {
            let (_, source, cost) = edges[position];
            sources += 1;
            total += 1 + u64::from(cost);
            position += 1;
            while position < edges.len()
                && edges[position].0 == target
                && edges[position].1 == source
            {
                cast_tie |= edges[position].2 == cost;
                position += 1;
            }
        }
        if sources != types.len() {
            continue;
        }
        match best {
            None => {
                best = Some((*original, total));
                ambiguous = cast_tie;
            }
            Some((_, prior)) if total < prior => {
                best = Some((*original, total));
                ambiguous = cast_tie;
            }
            Some((_, prior)) if total == prior => ambiguous = true,
            _ => {}
        }
    }
    if ambiguous {
        Err(AMBIGUOUS)
    } else {
        best.map(|(index, _)| index).ok_or(NOT_FOUND)
    }
}

/// Choose among known input type identities using direct implicit casts only.
/// Unknown NULLs and duplicate identities do not affect cost. Sum 1 + cast cost
/// per distinct non-identity source; the cheapest target wins. Tied targets or
/// ambiguous minimum casts required by the cheapest target return AMBIGUOUS.
/// All-unknown/empty input returns NOT_FOUND so SQL can apply its native default.
/// No common supertype outside the inputs, multi-hop coercion, category/typmod
/// preference, identity callback or plugin execution is synthesized here.
///
/// # Safety
/// Nonempty spans are initialized immutable memory for this synchronous call.
/// selected is aligned, writable and disjoint from inputs. It receives the
/// first input index for the chosen identity, or u32::MAX on every failure.
/// No pointer is retained. Fallible scratch allocations are bounded by 1024
/// arguments and 4096 casts; sorting is in-place, without recursive cast search.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_resolve_common_type(
    arguments: *const Text,
    argument_count: u32,
    casts: *const Cast,
    cast_count: u32,
    selected: *mut u32,
) -> i32 {
    if selected.is_null() || !(selected as usize).is_multiple_of(mem::align_of::<u32>()) {
        return INVALID;
    }
    unsafe { selected.write(u32::MAX) };
    let result = (|| {
        let arguments = unsafe { borrow(arguments, argument_count, MAX_ARGUMENTS) }?;
        let casts = unsafe { borrow(casts, cast_count, MAX_OBJECTS) }?;
        unsafe { common_type(arguments, casts) }
    })();
    match result {
        Ok(index) => {
            unsafe { selected.write(index as u32) };
            OK
        }
        Err(error) => error,
    }
}
