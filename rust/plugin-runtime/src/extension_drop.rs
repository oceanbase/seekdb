// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Drop owns one transaction, including temporary removal of member protection.
//! Schema/dependency admission must finish before DETACH; a detached member is
//! visible only within the transaction and is restored if any later step fails.
use crate::extension_install::{
    valid_id, InstallResult, InstallStep, COMMITTED, COMMIT_UNKNOWN, NOT_STARTED, ROLLBACK_UNKNOWN,
    ROLLED_BACK,
};
use crate::{INVALID, OK};
use std::ffi::c_void;

pub const PREFLIGHT: u32 = 1;
pub const BEGIN: u32 = 2;
pub const LOCK_SNAPSHOT: u32 = 3;
pub const DETACH: u32 = 4;
pub const APPLY: u32 = 5;
pub const RECORD: u32 = 6;
pub const COMMIT: u32 = 7;
pub const ROLLBACK: u32 = 8;

/// # Safety
/// A nonempty name span must be readable. Validation retains no pointers,
/// performs no catalog access, and does not constitute privilege admission.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_extension_drop_validate(
    tenant: u64,
    database: u64,
    expected_id: u64,
    name: *const u8,
    length: u32,
) -> i32 {
    if !valid_id(tenant) || !valid_id(database) || (expected_id != 0 && !valid_id(expected_id)) {
        return INVALID;
    }
    match unsafe { crate::extension_install::text(name, length, 255, false) } {
        Ok(_) => OK,
        Err(status) => status,
    }
}

/// # Safety
/// Output is writable and disjoint from context. Callback/context stay live and
/// must not unwind/reenter. PREFLIGHT is read-only, BEGIN owns a fresh transaction,
/// LOCK_SNAPSHOT locks the instance then members and checks schema/permissions/
/// dependencies without writes. Only LOCK_SNAPSHOT may set the stable identity.
/// DETACH/APPLY/RECORD use that same transaction; COMMIT/ROLLBACK end it. Failed
/// BEGIN must allow cleanup. No native callbacks, independent commits or early
/// schema publication are allowed. Caller must reject already active transactions.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_extension_drop_run(
    context: *mut c_void,
    step: Option<InstallStep>,
    output: *mut InstallResult,
) -> i32 {
    unsafe { run_steps(context, step, output, None, false) }
}

// Shared locked-instance transaction protocol. A no-op UPDATE still performs
// BEGIN/LOCK/admission/COMMIT, but never detaches or changes schema/version.
pub(crate) unsafe fn run_steps(
    context: *mut c_void,
    step: Option<InstallStep>,
    output: *mut InstallResult,
    expected_id: Option<u64>,
    no_op: bool,
) -> i32 {
    if output.is_null() {
        return INVALID;
    }
    unsafe { *output = InstallResult::default() };
    let Some(step) = step else {
        return INVALID;
    };
    let mut result = InstallResult::default();
    let mut identity = expected_id.unwrap_or(0);
    for phase in [
        PREFLIGHT,
        BEGIN,
        LOCK_SNAPSHOT,
        DETACH,
        APPLY,
        RECORD,
        COMMIT,
    ] {
        if no_op && matches!(phase, DETACH | APPLY | RECORD) {
            continue;
        }
        let mut phase_identity = identity;
        let mut status = unsafe { step(context, phase, &mut phase_identity) };
        if phase == LOCK_SNAPSHOT {
            if valid_id(phase_identity)
                && (expected_id.is_none() || expected_id == Some(phase_identity))
            {
                identity = phase_identity;
            } else if status == OK {
                status = INVALID;
            }
        } else if status == OK && phase_identity != identity {
            status = INVALID;
        }
        if status != OK {
            result.failed_phase = phase;
            result.operation_status = status;
            if phase == PREFLIGHT {
                result.outcome = NOT_STARTED;
            } else if phase == COMMIT {
                // Even a lost commit acknowledgement must not trigger rollback
                // or retry. Retain the locked installation ID for reconciliation.
                result.outcome = COMMIT_UNKNOWN;
                result.extension_id = identity;
            } else {
                let mut cleanup_identity = identity;
                result.rollback_status = unsafe { step(context, ROLLBACK, &mut cleanup_identity) };
                result.outcome = if result.rollback_status == OK {
                    ROLLED_BACK
                } else {
                    ROLLBACK_UNKNOWN
                };
                if result.outcome == ROLLBACK_UNKNOWN {
                    result.extension_id = identity;
                }
            }
            unsafe { *output = result };
            return OK;
        }
    }
    result.outcome = COMMITTED;
    result.extension_id = identity;
    unsafe { *output = result };
    OK
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Default)]
    struct Model {
        fail: u32,
        rollback_fails: bool,
        corrupt_identity_at: u32,
        active: bool,
        committed: bool,
        detached: bool,
        objects_removed: bool,
        instance_removed: bool,
        phases: Vec<u32>,
    }

    unsafe extern "C" fn step(context: *mut c_void, phase: u32, identity: *mut u64) -> i32 {
        let model = unsafe { &mut *context.cast::<Model>() };
        model.phases.push(phase);
        match phase {
            PREFLIGHT => assert!(!model.active),
            BEGIN => model.active = true,
            LOCK_SNAPSHOT => {
                assert!(model.active && !model.detached);
                unsafe { *identity = 42 };
            }
            DETACH => {
                assert!(model.active);
                model.detached = true;
            }
            APPLY => {
                assert!(model.active && model.detached);
                model.objects_removed = true;
            }
            RECORD => {
                assert!(model.active && model.objects_removed);
                model.instance_removed = true;
            }
            COMMIT => {
                assert!(model.active && model.instance_removed);
                model.committed = true;
                model.active = false;
            }
            ROLLBACK => {
                if model.rollback_fails {
                    return -4002;
                }
                model.active = false;
                model.detached = false;
                model.objects_removed = false;
                model.instance_removed = false;
            }
            _ => unreachable!(),
        }
        if phase == model.corrupt_identity_at {
            unsafe { *identity = 0 };
        }
        if phase == model.fail {
            -4001
        } else {
            OK
        }
    }

    fn run(model: &mut Model) -> InstallResult {
        let mut result = InstallResult::default();
        assert_eq!(
            unsafe {
                seekdb_runtime_extension_drop_run(
                    (model as *mut Model).cast(),
                    Some(step),
                    &mut result,
                )
            },
            OK
        );
        result
    }

    #[test]
    fn drop_orders_admission_before_detach_and_commits_all_three_changes() {
        let mut model = Model::default();
        let result = run(&mut model);
        assert_eq!(
            model.phases,
            [
                PREFLIGHT,
                BEGIN,
                LOCK_SNAPSHOT,
                DETACH,
                APPLY,
                RECORD,
                COMMIT
            ]
        );
        assert!(
            model.committed && model.detached && model.objects_removed && model.instance_removed
        );
        assert_eq!((result.outcome, result.extension_id), (COMMITTED, 42));
    }

    #[test]
    fn every_precommit_failure_restores_member_protection_and_schema() {
        for fail in PREFLIGHT..=RECORD {
            let mut model = Model {
                fail,
                ..Default::default()
            };
            let result = run(&mut model);
            assert_eq!(result.failed_phase, fail);
            assert_eq!(result.operation_status, -4001);
            assert_eq!(result.extension_id, 0);
            assert!(
                !model.active
                    && !model.committed
                    && !model.detached
                    && !model.objects_removed
                    && !model.instance_removed
            );
            if fail == PREFLIGHT {
                assert_eq!(model.phases, [PREFLIGHT]);
                assert_eq!(result.outcome, NOT_STARTED);
            } else {
                assert_eq!(model.phases.last(), Some(&ROLLBACK));
                assert_eq!(result.outcome, ROLLED_BACK);
            }
        }
    }

    #[test]
    fn unknown_commit_is_never_rolled_back_or_retried() {
        let mut model = Model {
            fail: COMMIT,
            ..Default::default()
        };
        let result = run(&mut model);
        assert_eq!((result.outcome, result.extension_id), (COMMIT_UNKNOWN, 42));
        assert!(model.committed);
        assert_eq!(model.phases.last(), Some(&COMMIT));
        assert!(!model.phases.contains(&ROLLBACK));
    }

    #[test]
    fn rollback_failure_preserves_the_locked_identity_and_both_errors() {
        let mut model = Model {
            fail: APPLY,
            rollback_fails: true,
            ..Default::default()
        };
        let result = run(&mut model);
        assert_eq!(
            (result.outcome, result.extension_id),
            (ROLLBACK_UNKNOWN, 42)
        );
        assert_eq!(
            (result.operation_status, result.rollback_status),
            (-4001, -4002)
        );
        assert!(model.active && model.detached);
    }

    #[test]
    fn missing_or_changed_identity_cannot_commit_a_different_installation() {
        for phase in LOCK_SNAPSHOT..=COMMIT {
            let mut model = Model {
                corrupt_identity_at: phase,
                ..Default::default()
            };
            let result = run(&mut model);
            assert_eq!(
                (result.failed_phase, result.operation_status),
                (phase, INVALID)
            );
            assert_eq!(
                result.outcome,
                if phase == COMMIT {
                    COMMIT_UNKNOWN
                } else {
                    ROLLED_BACK
                }
            );
            assert_eq!(result.extension_id, if phase == COMMIT { 42 } else { 0 });
        }
    }

    #[test]
    fn invalid_ffi_clears_outputs_without_calling_steps() {
        let mut result = InstallResult {
            extension_id: 99,
            ..Default::default()
        };
        assert_eq!(
            unsafe { seekdb_runtime_extension_drop_run(std::ptr::null_mut(), None, &mut result) },
            INVALID
        );
        assert_eq!((result.outcome, result.extension_id), (NOT_STARTED, 0));
        assert_eq!(
            unsafe {
                seekdb_runtime_extension_drop_run(std::ptr::null_mut(), None, std::ptr::null_mut())
            },
            INVALID
        );
    }

    #[test]
    fn requests_validate_namespace_and_optional_identity_fence() {
        let name = b"text_ops";
        for expected in [0, 1, i64::MAX as u64] {
            assert_eq!(
                unsafe {
                    seekdb_runtime_extension_drop_validate(
                        1,
                        2,
                        expected,
                        name.as_ptr(),
                        name.len() as u32,
                    )
                },
                OK
            );
        }
        for (tenant, database, expected) in [(0, 2, 0), (1, 0, 0), (1, 2, u64::MAX)] {
            assert_eq!(
                unsafe {
                    seekdb_runtime_extension_drop_validate(
                        tenant,
                        database,
                        expected,
                        name.as_ptr(),
                        name.len() as u32,
                    )
                },
                INVALID
            );
        }
        for name in [b"".as_slice(), b"a\0b", b"bad\nname", b"\xff"] {
            assert_eq!(
                unsafe {
                    seekdb_runtime_extension_drop_validate(
                        1,
                        2,
                        0,
                        name.as_ptr(),
                        name.len() as u32,
                    )
                },
                INVALID
            );
        }
    }
}
