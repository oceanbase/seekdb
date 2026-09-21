// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! One catalog operation inside the caller's transaction, never its owner.
//! Host effects run without a Rust journal borrow, mutex, or allocated state.
use crate::{INVALID, OK};
use std::ffi::c_void;

pub const PREFLIGHT: u32 = 1;
pub const PREPARE: u32 = 2;
pub const APPLY: u32 = 3;
pub const CLOSE: u32 = 4;
pub const CHECK_TRANSACTION: u32 = 5;
pub const ROLLBACK_DATA: u32 = 6;
pub const ROLLBACK_VIEW: u32 = 7;
pub const POISON: u32 = 8;

pub const NOT_STARTED: u32 = 0;
pub const APPLIED: u32 = 1;
pub const ROLLED_BACK: u32 = 2;
pub const REQUIRES_ABORT: u32 = 3;

#[repr(C)]
#[derive(Default, Debug)]
pub struct OperationResult {
    pub outcome: u32,
    pub failed_phase: u32,
    pub operation_error: i32,
    pub close_error: i32,
    pub identity_error: i32,
    pub data_rollback_error: i32,
    pub view_rollback_error: i32,
    pub poison_error: i32,
}

pub type OperationStep = unsafe extern "C" fn(*mut c_void, u32, i32) -> i32;

impl OperationResult {
    fn failure(&mut self, phase: u32, error: i32) {
        if error != OK && self.operation_error == OK {
            self.failed_phase = phase;
            self.operation_error = error;
        }
    }
}

/// No phase commits, publishes, or starts an independent transaction. APPLIED
/// is provisional, not durable success. The host retains all operation objects
/// until this function returns and exposes IDs only on APPLIED.
///
/// # Safety
/// Non-null context/step remain live and exclusively used during this call;
/// output is writable, aligned and disjoint. Steps must not unwind or reenter
/// this operation. They may access the caller's journal through separate FFI
/// calls: this driver never borrows that journal across a host effect.
/// PREFLIGHT performs read-only parsing/ordinary ACL checks. PREPARE establishes
/// the caller's statement ownership, data barrier and paired journal mark before
/// any catalog mutation. A failing PREPARE still requires CLOSE and may need
/// rollback; the host tracks whether it actually acquired a barrier.
/// APPLY locks/admit/reserves/writes/stages through normal host catalog code.
/// CLOSE always releases every borrowed result, reservation and transport, even
/// on failure; nonzero means context restoration cannot be trusted for rollback.
/// CHECK_TRANSACTION validates the captured session/transaction AFTER closing.
/// ROLLBACK_DATA uses only that operation's real barrier; if none was acquired
/// it is a no-op, never a full rollback of the caller's earlier work.
/// ROLLBACK_VIEW runs ONLY after confirmed data rollback and may then undo marks.
/// POISON receives the unsafe cleanup error; it must revoke the captured private
/// view and prohibit commit (including if data abort fails), without touching a
/// replacement transaction. REQUIRES_ABORT remains such even if POISON fails.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_operation_run(
    context: *mut c_void,
    step: Option<OperationStep>,
    output: *mut OperationResult,
) -> i32 {
    if output.is_null() {
        return INVALID;
    }
    // SAFETY: caller supplies disjoint writable output.
    unsafe { *output = OperationResult::default() };
    let Some(step) = step.filter(|_| !context.is_null()) else {
        return INVALID;
    };
    // SAFETY: every synchronous call uses the same live host frame, with no
    // journal reference, lock or borrowed callback result retained in Rust.
    let invoke = |phase, cause| unsafe { step(context, phase, cause) };
    let mut result = OperationResult::default();
    result.failure(PREFLIGHT, invoke(PREFLIGHT, OK));
    if result.operation_error != OK {
        unsafe { *output = result };
        return OK;
    }
    result.failure(PREPARE, invoke(PREPARE, OK));
    if result.operation_error == OK {
        result.failure(APPLY, invoke(APPLY, OK));
    }
    result.close_error = invoke(CLOSE, OK);
    result.failure(CLOSE, result.close_error);
    result.identity_error = invoke(CHECK_TRANSACTION, OK);
    result.failure(CHECK_TRANSACTION, result.identity_error);
    if result.operation_error == OK {
        result.outcome = APPLIED;
    } else {
        let mut unsafe_cleanup = if result.close_error != OK {
            result.close_error
        } else {
            result.identity_error
        };
        if unsafe_cleanup == OK {
            result.data_rollback_error = invoke(ROLLBACK_DATA, OK);
            unsafe_cleanup = result.data_rollback_error;
            if unsafe_cleanup == OK {
                result.view_rollback_error = invoke(ROLLBACK_VIEW, OK);
                unsafe_cleanup = result.view_rollback_error;
            }
        }
        if unsafe_cleanup == OK {
            result.outcome = ROLLED_BACK;
        } else {
            result.outcome = REQUIRES_ABORT;
            result.poison_error = invoke(POISON, unsafe_cleanup);
        }
    }
    unsafe { *output = result };
    OK
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Default)]
    struct Host {
        events: Vec<(u32, i32)>,
        errors: [i32; 9],
    }
    unsafe extern "C" fn step(context: *mut c_void, phase: u32, cause: i32) -> i32 {
        let host = unsafe { &mut *context.cast::<Host>() };
        host.events.push((phase, cause));
        host.errors[phase as usize]
    }
    fn run(host: &mut Host) -> OperationResult {
        let mut result = OperationResult::default();
        assert_eq!(
            unsafe {
                seekdb_runtime_query_operation_run(
                    (host as *mut Host).cast(),
                    Some(step),
                    &mut result,
                )
            },
            OK
        );
        result
    }
    fn phases(host: &Host) -> Vec<u32> {
        host.events.iter().map(|event| event.0).collect()
    }

    #[test]
    fn success_is_provisional_and_closes_before_identity_check() {
        let mut host = Host::default();
        let result = run(&mut host);
        assert_eq!(result.outcome, APPLIED);
        assert_eq!(result.operation_error, 0);
        assert_eq!(
            phases(&host),
            [PREFLIGHT, PREPARE, APPLY, CLOSE, CHECK_TRANSACTION]
        );
        assert!(host.events.iter().all(|event| event.1 == 0));
    }

    #[test]
    fn preflight_never_acquires_or_rolls_back_a_transaction() {
        let mut host = Host::default();
        host.errors[PREFLIGHT as usize] = -4001;
        let result = run(&mut host);
        assert_eq!(result.outcome, NOT_STARTED);
        assert_eq!(result.failed_phase, PREFLIGHT);
        assert_eq!(result.operation_error, -4001);
        assert_eq!(phases(&host), [PREFLIGHT]);
    }

    #[test]
    fn prepare_and_apply_failure_close_then_undo_data_before_view() {
        for failed in [PREPARE, APPLY] {
            let mut host = Host::default();
            host.errors[failed as usize] = -4002;
            let result = run(&mut host);
            assert_eq!(result.outcome, ROLLED_BACK);
            assert_eq!(result.failed_phase, failed);
            assert_eq!(result.operation_error, -4002);
            let mut expected = vec![PREFLIGHT, PREPARE];
            if failed == APPLY {
                expected.push(APPLY);
            }
            expected.extend([CLOSE, CHECK_TRANSACTION, ROLLBACK_DATA, ROLLBACK_VIEW]);
            assert_eq!(phases(&host), expected);
        }
    }

    #[test]
    fn every_cleanup_failure_requires_abort_and_preserves_all_errors() {
        for primary in [0, PREPARE, APPLY] {
            for cleanup in [CLOSE, CHECK_TRANSACTION, ROLLBACK_DATA, ROLLBACK_VIEW] {
                // Rollback callbacks are not entered after a successful apply.
                if primary == 0 && cleanup >= ROLLBACK_DATA {
                    continue;
                }
                for poison_error in [0, -4999] {
                    let mut host = Host::default();
                    if primary != 0 {
                        host.errors[primary as usize] = -4002;
                    }
                    host.errors[cleanup as usize] = -4012;
                    host.errors[POISON as usize] = poison_error;
                    let result = run(&mut host);
                    assert_eq!(result.outcome, REQUIRES_ABORT);
                    assert_eq!(
                        result.operation_error,
                        if primary == 0 { -4012 } else { -4002 }
                    );
                    assert_eq!(
                        result.failed_phase,
                        if primary == 0 { cleanup } else { primary }
                    );
                    assert_eq!(result.poison_error, poison_error);
                    assert_eq!(host.events.last(), Some(&(POISON, -4012)));
                    let phases = phases(&host);
                    if cleanup <= CHECK_TRANSACTION {
                        assert!(!phases.contains(&ROLLBACK_DATA));
                    }
                    if cleanup <= ROLLBACK_DATA {
                        assert!(!phases.contains(&ROLLBACK_VIEW));
                    }
                }
            }
        }
        let mut host = Host::default();
        host.errors[APPLY as usize] = -4002;
        host.errors[CLOSE as usize] = -4003;
        host.errors[CHECK_TRANSACTION as usize] = -4004;
        let result = run(&mut host);
        assert_eq!(
            (
                result.operation_error,
                result.close_error,
                result.identity_error
            ),
            (-4002, -4003, -4004)
        );
        assert_eq!(host.events.last(), Some(&(POISON, -4003)));
    }

    #[test]
    fn invalid_ffi_is_non_mutating_and_layout_matches_host() {
        assert_eq!(std::mem::size_of::<OperationResult>(), 32);
        assert_eq!(std::mem::offset_of!(OperationResult, operation_error), 8);
        assert_eq!(std::mem::offset_of!(OperationResult, poison_error), 28);
        let mut result = OperationResult {
            outcome: 99,
            operation_error: -999,
            ..Default::default()
        };
        let mut host = Host::default();
        unsafe {
            assert_eq!(
                seekdb_runtime_query_operation_run(std::ptr::null_mut(), Some(step), &mut result),
                INVALID
            );
            assert_eq!(result.outcome, NOT_STARTED);
            assert_eq!(result.operation_error, 0);
            assert_eq!(
                seekdb_runtime_query_operation_run(
                    (&mut host as *mut Host).cast(),
                    None,
                    &mut result
                ),
                INVALID
            );
            assert_eq!(
                seekdb_runtime_query_operation_run(
                    (&mut host as *mut Host).cast(),
                    Some(step),
                    std::ptr::null_mut()
                ),
                INVALID
            );
        }
        assert!(host.events.is_empty());
    }
}
