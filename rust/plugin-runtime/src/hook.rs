// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Legacy around-hook ABI adapter. Both ABIs use the mode-aware Rust engine;
//! callback payloads/leases and catalog publication remain owned by the host.
use crate::{hook_v2, INVALID};
use std::{ffi::c_void, mem::size_of, slice};

pub type Next = unsafe extern "C" fn(*mut c_void) -> i32;
pub type Invoke = unsafe extern "C" fn(*mut c_void, Next, *mut c_void) -> i32;
#[repr(C)]
pub struct Hook {
    pub context: *mut c_void,
    pub invoke: Option<Invoke>,
}
const MAX_HOOKS: u32 = 64;

/// Run preordered, fully pinned hooks, then the host operation, at most once.
/// A hook may veto with an error before next; success requires exactly one next.
///
/// # Safety
/// Spans/output are valid and disjoint; every function/payload remains alive
/// until return. Callbacks are synchronous, exclusive to their borrowed frame,
/// and never unwind or retain/cross-thread-call the continuation. Reentry starts
/// a separate chain and is bounded by the host's query recursion policy.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_hook_run(
    hooks: *const Hook,
    count: u32,
    leaf: Option<Next>,
    context: *mut c_void,
    protocol_error: i32,
    output: *mut i32,
) -> i32 {
    if output.is_null() {
        return INVALID;
    }
    unsafe {
        *output = protocol_error;
    }
    if count > MAX_HOOKS || (count != 0 && hooks.is_null()) || leaf.is_none() || protocol_error >= 0
    {
        return INVALID;
    }
    let hooks = if count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(hooks, count as usize) }
    };
    // Bounded stack conversion, no allocation or duplicate dispatch algorithm.
    let mut entries = [hook_v2::Hook {
        struct_size: size_of::<hook_v2::Hook>() as u32,
        mode: hook_v2::AROUND,
        context: std::ptr::null_mut(),
        invoke: None,
        observe: None,
        reserved: [0; 4],
    }; MAX_HOOKS as usize];
    for (target, source) in entries.iter_mut().zip(hooks) {
        target.context = source.context;
        target.invoke = source.invoke;
    }
    // The legacy ABI has no result-state validator. Around-mode admission and
    // the exactly-once protocol still prevent success without the host leaf.
    unsafe extern "C" fn accept(_: *mut c_void) -> i32 {
        0
    }
    unsafe {
        hook_v2::seekdb_runtime_hook_run_v2(
            entries.as_ptr(),
            count,
            leaf,
            context,
            Some(accept),
            protocol_error,
            output,
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{OK, STATE_MISMATCH};
    struct Probe {
        events: Vec<i32>,
        mode: i32,
        leaf_error: i32,
    }
    unsafe extern "C" fn leaf(p: *mut c_void) -> i32 {
        let p = unsafe { &mut *p.cast::<Probe>() };
        p.events.push(9);
        p.leaf_error
    }
    unsafe extern "C" fn hook(p: *mut c_void, next: Next, frame: *mut c_void) -> i32 {
        // Do not hold a mutable reference across reentry into the same probe.
        let mode = unsafe { (*p.cast::<Probe>()).mode };
        unsafe {
            (*p.cast::<Probe>()).events.push(1);
        }
        if mode == 1 {
            return -88;
        }
        if mode == 2 {
            return 0;
        }
        let result = unsafe { next(frame) };
        if mode == 3 {
            unsafe {
                next(frame);
            }
        }
        unsafe {
            (*p.cast::<Probe>()).events.push(2);
        }
        if mode == 4 {
            0
        } else {
            result
        }
    }
    #[test]
    fn nested_order_veto_and_exact_downstream_failure() {
        let mut p = Probe {
            events: vec![],
            mode: 0,
            leaf_error: 0,
        };
        let ptr = (&mut p as *mut Probe).cast();
        let hooks = [
            Hook {
                context: ptr,
                invoke: Some(hook),
            },
            Hook {
                context: ptr,
                invoke: Some(hook),
            },
        ];
        let mut output = 99;
        assert_eq!(
            unsafe {
                seekdb_runtime_hook_run(hooks.as_ptr(), 2, Some(leaf), ptr, -22, &mut output)
            },
            OK
        );
        assert_eq!(output, 0);
        assert_eq!(p.events, [1, 1, 9, 2, 2]);
        for mode in [1, 2, 3, 4] {
            p.events.clear();
            p.mode = mode;
            p.leaf_error = -1234;
            let status = unsafe {
                seekdb_runtime_hook_run(hooks.as_ptr(), 2, Some(leaf), ptr, -22, &mut output)
            };
            assert_eq!(
                status,
                if mode == 2 || mode == 3 {
                    STATE_MISMATCH
                } else {
                    OK
                }
            );
            assert_eq!(
                output,
                match mode {
                    1 => -88,
                    4 => -1234,
                    _ => -22,
                }
            );
            assert_eq!(
                p.events.iter().filter(|e| **e == 9).count(),
                usize::from(mode >= 3)
            );
        }
    }
    #[test]
    fn admission_validates_entire_chain_before_callbacks() {
        let mut p = Probe {
            events: vec![],
            mode: 0,
            leaf_error: -31,
        };
        let ptr = (&mut p as *mut Probe).cast();
        let mut output = 99;
        let hooks = [
            Hook {
                context: ptr,
                invoke: Some(hook),
            },
            Hook {
                context: ptr,
                invoke: None,
            },
        ];
        assert_eq!(
            unsafe {
                seekdb_runtime_hook_run(hooks.as_ptr(), 2, Some(leaf), ptr, -22, &mut output)
            },
            INVALID
        );
        assert!(p.events.is_empty());
        assert_eq!(output, -22);
        assert_eq!(
            unsafe {
                seekdb_runtime_hook_run(hooks.as_ptr(), 65, Some(leaf), ptr, -22, &mut output)
            },
            INVALID
        );
        assert_eq!(
            unsafe {
                seekdb_runtime_hook_run(std::ptr::null(), 0, Some(leaf), ptr, -22, &mut output)
            },
            OK
        );
        assert_eq!(output, -31);
        assert_eq!(p.events, [9]);
    }
}
