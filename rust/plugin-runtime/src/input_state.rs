// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Cursor-local parameter dependency state. Values and child operators stay in
//! C++; Rust owns readiness, transitive invalidation and operation sequencing.
//! No callbacks, allocations or borrowed host data after construction.
use crate::{
    dependency::CYCLE, dependency::LIMIT, registration::NO_MEMORY, INVALID, OK, STATE_MISMATCH,
};
use std::{
    alloc::{alloc, Layout},
    mem, ptr, slice,
};

pub const READ: u32 = 1;
pub const RESCAN: u32 = 2;
pub const BIND: u32 = 3;
pub const ROW: u32 = 1;
pub const EOF: u32 = 2;
pub const DONE: u32 = 3;
pub const ERROR: u32 = 4;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Edge {
    pub source: u32,
    pub target: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct Effect {
    pub rows: u64,
    pub bindings: u64,
    pub ticket: u64,
}
#[derive(Clone)]
pub struct InputState {
    count: u32,
    parents: [u64; 64],
    descendants: [u64; 64],
    rows: u64,
    bound: u64,
    pending: Option<(u32, u32, u64)>,
    next_ticket: u64,
    failed: bool,
}
impl InputState {
    fn new(count: u32, edges: &[Edge]) -> Result<Self, i32> {
        if count > 64 || edges.len() > 4096 {
            return Err(LIMIT);
        }
        let mut state = Self {
            count,
            parents: [0; 64],
            descendants: [0; 64],
            rows: 0,
            bound: 0,
            pending: None,
            next_ticket: 1,
            failed: false,
        };
        for edge in edges {
            if edge.source >= count || edge.target >= count {
                return Err(INVALID);
            }
            if edge.source == edge.target {
                return Err(CYCLE);
            }
            state.parents[edge.target as usize] |= 1u64 << edge.source;
            state.descendants[edge.source as usize] |= 1u64 << edge.target;
        }
        for via in 0..count as usize {
            for source in 0..count as usize {
                if state.descendants[source] & (1u64 << via) != 0 {
                    state.descendants[source] |= state.descendants[via];
                }
            }
        }
        if (0..count as usize).any(|i| state.descendants[i] & (1u64 << i) != 0) {
            return Err(CYCLE);
        }
        Ok(state)
    }
    fn all(&self) -> u64 {
        if self.count == 64 {
            u64::MAX
        } else {
            (1u64 << self.count) - 1
        }
    }
    fn reset(&mut self, reusable: bool) -> Effect {
        self.rows = 0;
        self.bound = 0;
        self.pending = None;
        self.failed = !reusable;
        // Tickets never rewind, including recovery from a failed operation.
        Effect {
            rows: self.all(),
            bindings: self.all(),
            ticket: 0,
        }
    }
    fn reject(&mut self, status: i32, out: &mut Effect) -> i32 {
        *out = self.reset(false);
        status
    }
    fn begin(&mut self, operation: u32, input: u32, out: &mut Effect) -> i32 {
        *out = Effect::default();
        if self.failed || self.pending.is_some() || self.next_ticket == u64::MAX {
            return self.reject(STATE_MISMATCH, out);
        }
        if input >= self.count || !matches!(operation, READ | RESCAN | BIND) {
            return self.reject(INVALID, out);
        }
        let bit = 1u64 << input;
        let parents = self.parents[input as usize];
        if operation == BIND {
            if parents == 0 {
                return self.reject(INVALID, out);
            }
            if self.rows & parents != parents {
                return self.reject(STATE_MISMATCH, out);
            }
        } else if parents != 0 && self.bound & bit == 0 {
            return self.reject(STATE_MISMATCH, out);
        }
        let descendants = self.descendants[input as usize];
        *out = Effect {
            rows: descendants | bit,
            bindings: descendants | if operation == BIND { bit } else { 0 },
            ticket: self.next_ticket,
        };
        self.rows &= !out.rows;
        self.bound &= !out.bindings;
        self.pending = Some((operation, input, self.next_ticket));
        self.next_ticket += 1;
        OK
    }
    fn finish(&mut self, ticket: u64, outcome: u32, out: &mut Effect) -> i32 {
        *out = Effect::default();
        let Some((operation, input, expected)) = self.pending else {
            return self.reject(STATE_MISMATCH, out);
        };
        if self.failed || ticket == 0 || ticket != expected {
            return self.reject(STATE_MISMATCH, out);
        }
        if outcome == ERROR {
            *out = self.reset(false);
            return OK;
        }
        if !matches!(
            (operation, outcome),
            (READ, ROW | EOF) | (RESCAN | BIND, DONE)
        ) {
            return self.reject(INVALID, out);
        }
        self.pending = None;
        if outcome == ROW {
            self.rows |= 1u64 << input;
        }
        if operation == BIND {
            self.bound |= 1u64 << input;
        }
        OK
    }
}
fn aligned<T>(p: *const T) -> bool {
    !p.is_null() && (p as usize).is_multiple_of(mem::align_of::<T>())
}

/// # Safety
/// Inputs are readable, aligned and disjoint from writable output. No host
/// pointer is retained. Returned handle has one exclusive, serialized owner.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_input_state_create(
    count: u32,
    edges: *const Edge,
    edge_count: u32,
    out: *mut *mut InputState,
) -> i32 {
    if !aligned(out) {
        return INVALID;
    }
    unsafe {
        *out = ptr::null_mut();
    }
    if count > 64 || edge_count > 4096 {
        return LIMIT;
    }
    if edge_count != 0 && !aligned(edges) {
        return INVALID;
    }
    let edges = if edge_count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(edges, edge_count as usize) }
    };
    let state = match InputState::new(count, edges) {
        Ok(s) => s,
        Err(e) => return e,
    };
    let pointer = unsafe { alloc(Layout::new::<InputState>()).cast::<InputState>() };
    if pointer.is_null() {
        return NO_MEMORY;
    }
    unsafe {
        pointer.write(state);
        *out = pointer;
    }
    OK
}
/// # Safety
/// Null or an exclusively owned live handle, with no host operation in flight.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_input_state_destroy(state: *mut InputState) {
    if !state.is_null() {
        drop(unsafe { Box::from_raw(state) });
    }
}
/// # Safety
/// Exclusive live handle; aligned, disjoint writable output. Apply returned
/// invalidations before invoking host work, including on errors. No Rust borrow
/// may span host calls; complete the exact returned ticket after host work.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_input_state_begin(
    state: *mut InputState,
    operation: u32,
    input: u32,
    out: *mut Effect,
) -> i32 {
    if !aligned(out) {
        return INVALID;
    }
    unsafe {
        *out = Effect::default();
    }
    if !aligned(state) {
        return INVALID;
    }
    unsafe { (&mut *state).begin(operation, input, &mut *out) }
}
/// # Safety
/// Same ownership/output contract as begin. ERROR reports failed host work;
/// an OK bridge status in that case does not make the host operation succeed.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_input_state_finish(
    state: *mut InputState,
    ticket: u64,
    outcome: u32,
    out: *mut Effect,
) -> i32 {
    if !aligned(out) {
        return INVALID;
    }
    unsafe {
        *out = Effect::default();
    }
    if !aligned(state) {
        return INVALID;
    }
    unsafe { (&mut *state).finish(ticket, outcome, &mut *out) }
}
/// # Safety
/// Exclusive live handle, no operation in flight. reusable=1 only after a
/// successful whole-cursor reset. Invalid flags are non-mutating. Always apply
/// returned invalidations; reset does not reset host children or commit SQL.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_input_state_reset(
    state: *mut InputState,
    reusable: u32,
    out: *mut Effect,
) -> i32 {
    if !aligned(out) {
        return INVALID;
    }
    unsafe {
        *out = Effect::default();
    }
    if !aligned(state) || reusable > 1 {
        return INVALID;
    }
    unsafe {
        *out = (&mut *state).reset(reusable == 1);
    }
    OK
}

#[cfg(test)]
mod tests;
