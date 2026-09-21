// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Provider-before-consumer dependency plans. The host resolves durable names
//! and generation fences; this module alone owns graph normalization/ordering.
//! No callbacks, retained pointers, recursive traversal or partial output plans.

use crate::{INVALID, OK};
use std::cmp::Reverse;
use std::collections::BinaryHeap;
use std::{mem, ptr, slice};

const NO_MEMORY: i32 = 5;
pub const LIMIT: i32 = 7;
pub const CYCLE: i32 = 11;
pub const MAX_NODES: u32 = 65536;
pub const MAX_EDGES: u32 = 1048576;

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct Edge {
    pub provider: u32,
    pub consumer: u32,
}

fn vector<T>(capacity: usize) -> Result<Vec<T>, i32> {
    let mut result = Vec::new();
    result.try_reserve_exact(capacity).map_err(|_| NO_MEMORY)?;
    Ok(result)
}

pub(crate) fn plan(count: u32, input: &[Edge], ignore_self: bool) -> Result<Vec<u32>, (i32, u32)> {
    let build = || -> Result<_, i32> {
        let mut edges = vector(input.len())?;
        for edge in input {
            if edge.provider >= count || edge.consumer >= count {
                return Err(INVALID);
            }
            if !ignore_self || edge.provider != edge.consumer {
                edges.push(*edge);
            }
        }
        // Multiple service/object dependencies between the same modules express
        // one ordering edge. Sorting is in-place and independent of row order.
        edges.sort_unstable();
        edges.dedup();
        let mut degree = vector(count as usize)?;
        degree.resize(count as usize, 0u32);
        let mut offsets = vector(count as usize + 1)?;
        offsets.resize(count as usize + 1, 0usize);
        for edge in &edges {
            degree[edge.consumer as usize] += 1;
            offsets[edge.provider as usize + 1] += 1;
        }
        for index in 1..offsets.len() {
            offsets[index] += offsets[index - 1];
        }
        let mut ready = BinaryHeap::new();
        ready
            .try_reserve_exact(count as usize)
            .map_err(|_| NO_MEMORY)?;
        for (index, degree) in degree.iter().enumerate() {
            if *degree == 0 {
                ready.push(Reverse(index as u32));
            }
        }
        let order = vector(count as usize)?;
        Ok((edges, degree, offsets, ready, order))
    };
    let (edges, mut degree, offsets, mut ready, mut order) =
        build().map_err(|status| (status, u32::MAX))?;
    // Every node is queued at most once; all allocations finished above.
    while let Some(Reverse(provider)) = ready.pop() {
        order.push(provider);
        for edge in &edges[offsets[provider as usize]..offsets[provider as usize + 1]] {
            degree[edge.consumer as usize] -= 1;
            if degree[edge.consumer as usize] == 0 {
                ready.push(Reverse(edge.consumer));
            }
        }
    }
    if order.len() != count as usize {
        // This is a blocked node, not necessarily a member of the cycle: a
        // dependent downstream of a cycle can have the lowest ordinal.
        let blocked = degree.iter().position(|degree| *degree != 0).unwrap();
        Err((CYCLE, blocked as u32))
    } else {
        Ok(order)
    }
}

/// Construct a deterministic complete activation plan, with lowest ready node
/// ordinal first. Self edges are either ignored explicitly or treated as cycles.
/// Error output is atomic: order is written only on success, never partially.
///
/// # Safety
/// Input points to edge_count initialized edges; output has order_capacity
/// writable u32 slots. blocked points to one writable u32. Nonempty pointers
/// must be aligned and all buffers disjoint; input remains immutable until
/// return. Node ordinals must come from one host snapshot with resolved names
/// and validated generation/visibility fences. This function does not validate
/// the SQL transaction or module version constraints on behalf of the caller.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_dependency_plan(
    node_count: u32,
    edges: *const Edge,
    edge_count: u32,
    ignore_self_edges: u8,
    order: *mut u32,
    order_capacity: u32,
    blocked: *mut u32,
) -> i32 {
    if blocked.is_null() || !(blocked as usize).is_multiple_of(mem::align_of::<u32>()) {
        return INVALID;
    }
    unsafe { *blocked = u32::MAX };
    if node_count > MAX_NODES || edge_count > MAX_EDGES {
        return LIMIT;
    }
    if ignore_self_edges > 1
        || order_capacity < node_count
        || (node_count != 0
            && (order.is_null() || !(order as usize).is_multiple_of(mem::align_of::<u32>())))
        || (edge_count != 0
            && (edges.is_null() || !(edges as usize).is_multiple_of(mem::align_of::<Edge>())))
    {
        return INVALID;
    }
    let edges = if edge_count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(edges, edge_count as usize) }
    };
    match plan(node_count, edges, ignore_self_edges != 0) {
        Ok(result) => {
            if !result.is_empty() {
                unsafe { ptr::copy_nonoverlapping(result.as_ptr(), order, result.len()) };
            }
            OK
        }
        Err((status, node)) => {
            unsafe { *blocked = node };
            status
        }
    }
}

#[cfg(test)]
mod tests;
