// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Mode-aware host hook composition. This is not a public plan construction API.
use crate::hook::{Invoke, Next};
use crate::{INVALID, OK, STATE_MISMATCH};
use std::{cell::Cell, ffi::c_void, mem::size_of, slice};

pub const OBSERVE: u32 = 0;
pub const AROUND: u32 = 1;
pub const REPLACE: u32 = 2;
pub const BEFORE: u32 = 0;
pub const AFTER: u32 = 1;
pub type Observe = unsafe extern "C" fn(*mut c_void, u32, i32);

#[derive(Clone, Copy)]
#[repr(C)]
pub struct Hook {
    pub struct_size: u32,
    pub mode: u32,
    pub context: *mut c_void,
    pub invoke: Option<Invoke>,
    pub observe: Option<Observe>,
    pub reserved: [u64; 4],
}
struct Chain<'a> {
    hooks: &'a [Hook],
    leaf: Next,
    context: *mut c_void,
    protocol_error: i32,
    invalid: Cell<bool>,
}
struct Frame<'a> {
    chain: &'a Chain<'a>,
    index: usize,
    called: Cell<bool>,
    next_result: Cell<i32>,
}
unsafe extern "C" fn next(context: *mut c_void) -> i32 {
    // The callback only borrows this frame synchronously on the current thread.
    let frame = unsafe { &*context.cast::<Frame<'_>>() };
    if frame.called.replace(true) {
        frame.chain.invalid.set(true);
        return frame.chain.protocol_error;
    }
    let result = unsafe { run(frame.chain, frame.index + 1) };
    frame.next_result.set(result);
    result
}
unsafe fn run(chain: &Chain<'_>, index: usize) -> i32 {
    let Some(hook) = chain.hooks.get(index) else {
        return unsafe { (chain.leaf)(chain.context) };
    };
    if hook.mode == OBSERVE {
        // Observation has no continuation or return status: the host, not the
        // observer, advances the chain and preserves the downstream result.
        unsafe { hook.observe.unwrap()(hook.context, BEFORE, 0) };
        let result = unsafe { run(chain, index + 1) };
        let result = if chain.invalid.get() {
            chain.protocol_error
        } else {
            result
        };
        unsafe { hook.observe.unwrap()(hook.context, AFTER, result) };
        return result;
    }
    let frame = Frame {
        chain,
        index,
        called: Cell::new(false),
        next_result: Cell::new(0),
    };
    let result = unsafe {
        hook.invoke.unwrap()(
            hook.context,
            next,
            (&frame as *const Frame<'_>).cast_mut().cast(),
        )
    };
    if result == 0 && hook.mode == AROUND && !frame.called.get() {
        chain.invalid.set(true);
    }
    // Replacement may bypass downstream work, but calling next opts into its
    // exact error semantics. It cannot "recover" a failed downstream operation.
    if frame.called.get() && frame.next_result.get() != 0 {
        frame.next_result.get()
    } else {
        result
    }
}

/// Execute preordered, pinned hooks. On success the host must validate the
/// resulting operation state, including replacements, before publishing it.
/// Observers' AFTER result is the nested chain result, not final validation or
/// transaction commit. A replacement skips later hooks unless it calls next.
///
/// # Safety
/// Inputs/output are live, aligned, disjoint allocations of the declared size.
/// All code and payloads remain pinned through validation. Callbacks must not
/// unwind, retain/cross-thread-call their continuation, or mutate this table.
/// Observation callbacks must not mutate the host operation; native code is
/// trusted, and absence of a continuation/status is not a memory sandbox.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_hook_run_v2(
    hooks: *const Hook,
    count: u32,
    leaf: Option<Next>,
    context: *mut c_void,
    validate_result: Option<Next>,
    protocol_error: i32,
    output: *mut i32,
) -> i32 {
    if output.is_null() {
        return INVALID;
    }
    unsafe { *output = protocol_error };
    if count > 64
        || (count != 0 && hooks.is_null())
        || leaf.is_none()
        || validate_result.is_none()
        || protocol_error >= 0
    {
        return INVALID;
    }
    let hooks = if count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(hooks, count as usize) }
    };
    // Fixed-stride host-only v2 table. Future larger entries need a new version.
    if hooks.iter().any(|hook| {
        hook.struct_size != size_of::<Hook>() as u32
            || hook.reserved != [0; 4]
            || match hook.mode {
                OBSERVE => hook.observe.is_none() || hook.invoke.is_some(),
                AROUND | REPLACE => hook.invoke.is_none() || hook.observe.is_some(),
                _ => true,
            }
    }) {
        return INVALID;
    }
    let chain = Chain {
        hooks,
        leaf: leaf.unwrap(),
        context,
        protocol_error,
        invalid: Cell::new(false),
    };
    let mut result = unsafe { run(&chain, 0) };
    if chain.invalid.get() {
        return STATE_MISMATCH;
    }
    // Do not validate failed/protocol-invalid state or rerun the operation.
    // The validator sees the result AFTER every wrapping callback has returned.
    if result == 0 {
        result = unsafe { validate_result.unwrap()(context) };
    }
    unsafe { *output = result };
    OK
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    #[derive(Default)]
    struct Probe {
        events: RefCell<Vec<i32>>,
        ready: Cell<bool>,
        validations: Cell<u32>,
        leaf_error: Cell<i32>,
    }
    unsafe fn probe<'a>(p: *mut c_void) -> &'a Probe {
        unsafe { &*p.cast::<Probe>() }
    }
    unsafe extern "C" fn leaf(p: *mut c_void) -> i32 {
        let p = unsafe { probe(p) };
        p.events.borrow_mut().push(9);
        p.ready.set(p.leaf_error.get() == 0);
        p.leaf_error.get()
    }
    unsafe extern "C" fn validate(p: *mut c_void) -> i32 {
        let p = unsafe { probe(p) };
        p.events.borrow_mut().push(100);
        p.validations.set(p.validations.get() + 1);
        if p.ready.get() {
            0
        } else {
            -700
        }
    }
    unsafe extern "C" fn observe(p: *mut c_void, phase: u32, result: i32) {
        let p = unsafe { probe(p) };
        p.events
            .borrow_mut()
            .push(if phase == BEFORE { 10 } else { 11 });
        if phase == AFTER {
            assert_eq!(result, p.leaf_error.get());
        }
    }
    unsafe extern "C" fn around(p: *mut c_void, next: Next, frame: *mut c_void) -> i32 {
        let p = unsafe { probe(p) };
        p.events.borrow_mut().push(20);
        let result = unsafe { next(frame) };
        p.events.borrow_mut().push(21);
        result
    }
    unsafe extern "C" fn replace(p: *mut c_void, _: Next, _: *mut c_void) -> i32 {
        let p = unsafe { probe(p) };
        p.events.borrow_mut().push(30);
        p.ready.set(true);
        0
    }
    fn entry(mode: u32, p: &Probe) -> Hook {
        Hook {
            struct_size: size_of::<Hook>() as u32,
            mode,
            context: (p as *const Probe).cast_mut().cast(),
            invoke: match mode {
                AROUND => Some(around),
                REPLACE => Some(replace),
                _ => None,
            },
            observe: if mode == OBSERVE { Some(observe) } else { None },
            reserved: [0; 4],
        }
    }
    fn execute(hooks: &[Hook], p: &Probe) -> (i32, i32) {
        let mut result = 123;
        let status = unsafe {
            seekdb_runtime_hook_run_v2(
                hooks.as_ptr(),
                hooks.len() as u32,
                Some(leaf),
                (p as *const Probe).cast_mut().cast(),
                Some(validate),
                -22,
                &mut result,
            )
        };
        (status, result)
    }
    #[test]
    fn all_three_mode_compositions_match_ordered_reference() {
        for a in 0..3 {
            for b in 0..3 {
                for c in 0..3 {
                    let p = Probe::default();
                    let modes = [a, b, c];
                    let hooks = modes.map(|mode| entry(mode, &p));
                    assert_eq!(execute(&hooks, &p), (OK, 0));
                    let mut expected = Vec::new();
                    let mut after = Vec::new();
                    let mut replaced = false;
                    for mode in modes {
                        expected.push((mode as i32 + 1) * 10);
                        if mode == REPLACE {
                            replaced = true;
                            break;
                        }
                        after.push((mode as i32 + 1) * 10 + 1);
                    }
                    if !replaced {
                        expected.push(9);
                    }
                    expected.extend(after.into_iter().rev());
                    expected.push(100);
                    assert_eq!(*p.events.borrow(), expected);
                    assert_eq!(p.validations.get(), 1);
                }
            }
        }
    }
    #[test]
    fn replacement_cannot_suppress_a_downstream_error() {
        unsafe extern "C" fn swallow(_: *mut c_void, next: Next, frame: *mut c_void) -> i32 {
            unsafe { next(frame) };
            0
        }
        let p = Probe::default();
        p.leaf_error.set(-4567);
        let mut replacement = entry(REPLACE, &p);
        replacement.invoke = Some(swallow);
        assert_eq!(execute(&[entry(OBSERVE, &p), replacement], &p), (OK, -4567));
        assert_eq!(*p.events.borrow(), [10, 9, 11]);
        assert_eq!(p.validations.get(), 0);
    }
    #[test]
    fn validation_is_after_wrappers_and_cannot_be_skipped_by_replacement() {
        unsafe extern "C" fn invalidate(p: *mut c_void, next: Next, frame: *mut c_void) -> i32 {
            let result = unsafe { next(frame) };
            unsafe { probe(p) }.ready.set(false);
            result
        }
        let p = Probe::default();
        let mut wrapper = entry(AROUND, &p);
        wrapper.invoke = Some(invalidate);
        assert_eq!(execute(&[wrapper, entry(REPLACE, &p)], &p), (OK, -700));
        assert_eq!(*p.events.borrow(), [30, 100]);
        assert_eq!(p.validations.get(), 1);
    }
}
