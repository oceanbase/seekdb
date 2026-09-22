// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Updates preserve a locked installation identity and change schema, members
//! and version in one host-owned DDL transaction. No second catalog or loader.
use crate::extension_install::{text, valid_id, InstallResult, InstallStep};
use crate::{INVALID, OK};
use std::ffi::c_void;

pub use crate::extension_drop::{
    APPLY, BEGIN, COMMIT, DETACH, LOCK_SNAPSHOT, PREFLIGHT, RECORD, ROLLBACK,
};

/// # Safety
/// Nonempty spans must be readable. Validation retains no pointers and does not
/// look up a durable installation or authorize its modification. Versions are
/// opaque catalog labels, not paths or semver. File readers validate filenames.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_extension_update_validate(
    tenant: u64,
    database: u64,
    expected_id: u64,
    name: *const u8,
    name_length: u32,
    from: *const u8,
    from_length: u32,
    to: *const u8,
    to_length: u32,
) -> i32 {
    if !valid_id(tenant) || !valid_id(database) || !valid_id(expected_id) {
        return INVALID;
    }
    for (data, length) in [(name, name_length), (from, from_length), (to, to_length)] {
        if let Err(status) = unsafe { text(data, length, 255, false) } {
            return status;
        }
    }
    OK
}

/// # Safety
/// Same callback, output and transaction ownership requirements as DROP.
/// Expected ID is positive; every callback receives that identity and cannot
/// replace it. LOCK_SNAPSHOT must lock the instance/members, compare the current
/// version with the source plan and perform authenticated schema/dependency
/// admission. No-op (0/1) comes from equal validated source/target versions, not
/// merely an empty SQL list: an empty update edge can still change the version.
/// DETACH/APPLY/RECORD replace membership and version in the SAME transaction.
/// Nothing publishes schema/hooks before commit. No unwinding or reentry.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_extension_update_run(
    context: *mut c_void,
    step: Option<InstallStep>,
    expected_id: u64,
    no_op: u32,
    output: *mut InstallResult,
) -> i32 {
    if output.is_null() {
        return INVALID;
    }
    unsafe { *output = InstallResult::default() };
    if !valid_id(expected_id) || no_op > 1 {
        return INVALID;
    }
    unsafe {
        crate::extension_drop::run_steps(context, step, output, Some(expected_id), no_op == 1)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::extension_install::{
        COMMITTED, COMMIT_UNKNOWN, NOT_STARTED, ROLLBACK_UNKNOWN, ROLLED_BACK,
    };
    use std::ptr;

    struct Model {
        fail: u32,
        rollback_fails: bool,
        observed_id: u64,
        version: u32,
        target: u32,
        pending_version: u32,
        members: Vec<u64>,
        pending_members: Vec<u64>,
        active: bool,
        phases: Vec<u32>,
        empty_script: bool,
    }
    impl Default for Model {
        fn default() -> Self {
            Self {
                fail: 0,
                rollback_fails: false,
                observed_id: 91,
                version: 1,
                target: 2,
                pending_version: 0,
                members: vec![10, 20],
                pending_members: Vec::new(),
                active: false,
                phases: Vec::new(),
                empty_script: false,
            }
        }
    }
    unsafe extern "C" fn step(context: *mut c_void, phase: u32, id: *mut u64) -> i32 {
        let model = unsafe { &mut *context.cast::<Model>() };
        assert_eq!(unsafe { *id }, 91);
        model.phases.push(phase);
        match phase {
            PREFLIGHT => assert!(!model.active),
            BEGIN => {
                model.active = true;
                model.pending_members = model.members.clone();
                model.pending_version = model.version;
            }
            LOCK_SNAPSHOT => {
                assert!(model.active);
                unsafe { *id = model.observed_id };
                if model.version != 1 {
                    return -4001;
                }
            }
            DETACH => {
                assert!(model.active);
                model.pending_members.clear();
            }
            APPLY => {
                assert!(model.active && model.pending_members.is_empty());
                model.pending_members = if model.empty_script {
                    model.members.clone()
                } else {
                    vec![20, 30]
                };
            }
            RECORD => {
                assert!(model.active);
                model.pending_version = model.target;
            }
            COMMIT => {
                assert!(model.active);
                model.members = model.pending_members.clone();
                model.version = model.pending_version;
                model.active = false;
            }
            ROLLBACK => {
                if model.rollback_fails {
                    return -4002;
                }
                model.pending_members.clear();
                model.pending_version = 0;
                model.active = false;
            }
            _ => panic!("invalid phase"),
        }
        if model.fail == phase {
            -4001
        } else {
            OK
        }
    }
    fn run(model: &mut Model, no_op: bool) -> InstallResult {
        let mut result = InstallResult::default();
        assert_eq!(
            unsafe {
                seekdb_runtime_extension_update_run(
                    (model as *mut Model).cast(),
                    Some(step),
                    91,
                    u32::from(no_op),
                    &mut result,
                )
            },
            OK
        );
        result
    }

    #[test]
    fn update_commits_new_members_and_version_without_replacing_identity() {
        let mut model = Model::default();
        let result = run(&mut model, false);
        assert_eq!(result.outcome, COMMITTED);
        assert_eq!(result.extension_id, 91);
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
        assert_eq!(model.members, [20, 30]);
        assert_eq!(model.version, 2);
        assert!(!model.active);
    }

    #[test]
    fn every_precommit_failure_preserves_original_version_and_members() {
        for fail in PREFLIGHT..COMMIT {
            let mut model = Model {
                fail,
                ..Model::default()
            };
            let result = run(&mut model, false);
            assert_eq!(
                result.outcome,
                if fail == PREFLIGHT {
                    NOT_STARTED
                } else {
                    ROLLED_BACK
                }
            );
            assert_eq!(result.extension_id, 0);
            assert_eq!(model.version, 1);
            assert_eq!(model.members, [10, 20]);
            assert!(!model.active);
            if fail != PREFLIGHT {
                assert_eq!(model.phases.last(), Some(&ROLLBACK));
            }
        }
    }

    #[test]
    fn same_version_noop_still_locks_and_authorizes_but_never_detaches() {
        let mut model = Model {
            target: 1,
            ..Model::default()
        };
        assert_eq!(run(&mut model, true).outcome, COMMITTED);
        assert_eq!(model.phases, [PREFLIGHT, BEGIN, LOCK_SNAPSHOT, COMMIT]);
        assert_eq!(model.members, [10, 20]);
        assert_eq!(model.version, 1);
        let mut denied = Model {
            fail: LOCK_SNAPSHOT,
            target: 1,
            ..Model::default()
        };
        assert_eq!(run(&mut denied, true).outcome, ROLLED_BACK);
        assert_eq!(denied.phases, [PREFLIGHT, BEGIN, LOCK_SNAPSHOT, ROLLBACK]);
    }

    #[test]
    fn empty_update_script_still_records_a_different_version() {
        let mut model = Model {
            empty_script: true,
            ..Model::default()
        };
        assert_eq!(run(&mut model, false).outcome, COMMITTED);
        assert!(model.phases.contains(&RECORD));
        assert_eq!(model.version, 2);
        assert_eq!(model.members, [10, 20]);
    }

    #[test]
    fn stale_identity_or_version_fails_before_member_detach() {
        for mut model in [
            Model {
                observed_id: 92,
                ..Model::default()
            },
            Model {
                version: 2,
                ..Model::default()
            },
        ] {
            let result = run(&mut model, false);
            assert_eq!(result.outcome, ROLLED_BACK);
            assert_eq!(result.failed_phase, LOCK_SNAPSHOT);
            assert_eq!(model.phases, [PREFLIGHT, BEGIN, LOCK_SNAPSHOT, ROLLBACK]);
            assert_eq!(model.members, [10, 20]);
        }
    }

    #[test]
    fn uncertain_results_preserve_expected_identity_without_retry_or_false_rollback() {
        for no_op in [false, true] {
            let mut model = Model {
                fail: COMMIT,
                target: if no_op { 1 } else { 2 },
                ..Model::default()
            };
            let result = run(&mut model, no_op);
            assert_eq!(result.outcome, COMMIT_UNKNOWN);
            assert_eq!(result.extension_id, 91);
            assert_eq!(model.phases.last(), Some(&COMMIT));
            assert!(!model.phases.contains(&ROLLBACK));
        }
        let mut model = Model {
            fail: RECORD,
            rollback_fails: true,
            ..Model::default()
        };
        let result = run(&mut model, false);
        assert_eq!(result.outcome, ROLLBACK_UNKNOWN);
        assert_eq!(
            (
                result.extension_id,
                result.operation_status,
                result.rollback_status
            ),
            (91, -4001, -4002)
        );
        assert!(model.active);
    }

    #[test]
    fn invalid_ffi_never_calls_steps_and_clears_outputs() {
        for (id, no_op) in [(0, 0), (u64::MAX, 0), (91, 2)] {
            let mut result = InstallResult {
                extension_id: 42,
                ..InstallResult::default()
            };
            assert_eq!(
                unsafe {
                    seekdb_runtime_extension_update_run(
                        ptr::null_mut(),
                        Some(step),
                        id,
                        no_op,
                        &mut result,
                    )
                },
                INVALID
            );
            assert_eq!(result.extension_id, 0);
        }
        let mut result = InstallResult {
            extension_id: 42,
            ..InstallResult::default()
        };
        assert_eq!(
            unsafe {
                seekdb_runtime_extension_update_run(ptr::null_mut(), None, 91, 0, &mut result)
            },
            INVALID
        );
        assert_eq!(result.extension_id, 0);
        for version in [b"".as_slice(), b"x\0y", b"\xff", &[b'x'; 256]] {
            assert_eq!(
                unsafe {
                    seekdb_runtime_extension_update_validate(
                        1,
                        2,
                        91,
                        b"demo".as_ptr(),
                        4,
                        version.as_ptr(),
                        version.len() as u32,
                        b"2".as_ptr(),
                        1,
                    )
                },
                INVALID
            );
        }
        assert_eq!(
            unsafe {
                seekdb_runtime_extension_update_validate(
                    1,
                    2,
                    0,
                    b"demo".as_ptr(),
                    4,
                    b"1".as_ptr(),
                    1,
                    b"2".as_ptr(),
                    1,
                )
            },
            INVALID
        );
        assert_eq!(
            unsafe {
                seekdb_runtime_extension_update_validate(
                    1,
                    2,
                    91,
                    b"demo".as_ptr(),
                    4,
                    b"2".as_ptr(),
                    1,
                    b"1".as_ptr(),
                    1,
                )
            },
            OK
        );
    }
}
