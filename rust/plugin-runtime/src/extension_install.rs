// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! Database installation identity validation, independent of module generation.
//! The host verifies schema ownership/privileges and writes the validated plan
//! in its existing SQL catalog transaction. No independent transaction here.
use crate::registration::{CONFLICT, NO_MEMORY};
use crate::{INVALID, OK};
use std::collections::HashSet;
use std::ffi::c_void;
use std::{mem, slice};

pub const PREFLIGHT: u32 = 1;
pub const BEGIN: u32 = 2;
pub const APPLY: u32 = 3;
pub const RECORD: u32 = 4;
pub const COMMIT: u32 = 5;
pub const ROLLBACK: u32 = 6;
pub const NOT_STARTED: u32 = 0;
pub const ROLLED_BACK: u32 = 1;
pub const COMMITTED: u32 = 2;
pub const COMMIT_UNKNOWN: u32 = 3;
pub const ROLLBACK_UNKNOWN: u32 = 4;

#[repr(C)]
#[derive(Default)]
pub struct InstallResult {
    pub outcome: u32,
    pub failed_phase: u32,
    pub operation_status: i32,
    pub rollback_status: i32,
    pub extension_id: u64,
}

pub type InstallStep = unsafe extern "C" fn(*mut c_void, u32, *mut u64) -> i32;

/// Drive ONE owned catalog transaction. This is the autocommit management
/// path, not an operation that can be applied to an arbitrary caller transaction.
///
/// # Safety
/// output is writable and disjoint from context; step/context stay live for all
/// synchronous calls. Steps must not unwind or reenter this driver. PREFLIGHT
/// makes no writes; BEGIN owns a new transaction; APPLY and RECORD use exactly
/// that transaction. Only RECORD writes the provisional ID. COMMIT/ROLLBACK
/// end it. A failing BEGIN may require cleanup; rollback must tolerate that.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_extension_install_run(
    context: *mut c_void,
    step: Option<InstallStep>,
    output: *mut InstallResult,
) -> i32 {
    if output.is_null() {
        return INVALID;
    }
    unsafe { *output = InstallResult::default() };
    let Some(step) = step else {
        return INVALID;
    };
    let mut result = InstallResult::default();
    let mut identity = 0;
    for phase in [PREFLIGHT, BEGIN, APPLY, RECORD, COMMIT] {
        let mut status = unsafe { step(context, phase, &mut identity) };
        if status == OK && phase == RECORD && !valid_id(identity) {
            status = INVALID;
        }
        if status != OK {
            result.failed_phase = phase;
            result.operation_status = status;
            if phase == PREFLIGHT {
                result.outcome = NOT_STARTED;
            } else if phase == COMMIT {
                // No blind rollback/retry: a commit reply can be lost after
                // durable publication. Preserve the ID for reconciliation.
                result.outcome = COMMIT_UNKNOWN;
                result.extension_id = identity;
            } else {
                result.rollback_status = unsafe { step(context, ROLLBACK, &mut identity) };
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

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Member {
    pub object_class: u32,
    pub reserved: u32,
    pub object_id: u64,
}

pub(crate) fn valid_id(id: u64) -> bool {
    id != 0 && id <= i64::MAX as u64
}

// A module is a catalog identity, not a relative path or a package filename.
// Keep package admission and persistent installation validation in agreement.
pub(crate) fn valid_native_module(module: &str) -> bool {
    module.len() <= 255
        && module
            .bytes()
            .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || b"._-".contains(&c))
}

/// Validate the declared, same-database dependency names. Existence, stable IDs
/// and locking belong to the host's schema transaction, not this pure check.
/// # Safety
/// Name and dependency spans are readable for their lengths; arrays are aligned.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_extension_requires_validate(
    name: *const u8,
    name_length: u32,
    dependencies: *const crate::package::source::Text,
    count: u32,
) -> i32 {
    if count > 64
        || (count != 0
            && (dependencies.is_null()
                || !(dependencies as usize)
                    .is_multiple_of(mem::align_of::<crate::package::source::Text>())))
    {
        return INVALID;
    }
    let name = match unsafe { text(name, name_length, 255, false) } {
        Ok(name) => name,
        Err(status) => return status,
    };
    let entries = if count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(dependencies, count as usize) }
    };
    let mut seen = HashSet::new();
    for entry in entries {
        let dependency = match unsafe { text(entry.data, entry.length, 255, false) } {
            Ok(value) => value,
            Err(status) => return status,
        };
        if !crate::package::component(dependency) || dependency == name {
            return INVALID;
        }
        if !seen.insert(dependency) {
            return CONFLICT;
        }
    }
    OK
}

pub(crate) unsafe fn text<'a>(
    bytes: *const u8,
    length: u32,
    maximum: u32,
    empty: bool,
) -> Result<&'a str, i32> {
    if length > maximum || (!empty && length == 0) || (length != 0 && bytes.is_null()) {
        return Err(INVALID);
    }
    let bytes = if length == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(bytes, length as usize) }
    };
    let text = std::str::from_utf8(bytes).map_err(|_| INVALID)?;
    if text.chars().any(char::is_control) {
        return Err(INVALID);
    }
    Ok(text)
}

/// # Safety
/// All nonempty spans must be readable and members aligned for their declared
/// length. Names are canonicalized by the SQL namespace service, not folded by
/// Rust. Member IDs refer to schema objects, NEVER module generation numbers.
#[no_mangle]
#[allow(clippy::too_many_arguments)]
pub unsafe extern "C" fn seekdb_runtime_extension_install_validate(
    tenant_id: u64,
    database_id: u64,
    owner_id: u64,
    name: *const u8,
    name_length: u32,
    version: *const u8,
    version_length: u32,
    module: *const u8,
    module_length: u32,
    members: *const Member,
    member_count: u32,
) -> i32 {
    if !valid_id(tenant_id)
        || !valid_id(database_id)
        || !valid_id(owner_id)
        || member_count > 4096
        || (member_count != 0
            && (members.is_null() || !(members as usize).is_multiple_of(mem::align_of::<Member>())))
    {
        return INVALID;
    }
    if unsafe { text(name, name_length, 255, false) }.is_err()
        || unsafe { text(version, version_length, 255, false) }.is_err()
    {
        return INVALID;
    }
    let module = match unsafe { text(module, module_length, 255, true) } {
        Ok(module) => module,
        Err(error) => return error,
    };
    if !valid_native_module(module) {
        return INVALID;
    }
    let members = if member_count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(members, member_count as usize) }
    };
    let mut identities = HashSet::new();
    if identities.try_reserve(members.len()).is_err() {
        return NO_MEMORY;
    }
    for member in members {
        if member.object_class == 0 || member.reserved != 0 || !valid_id(member.object_id) {
            return INVALID;
        }
        if !identities.insert((member.object_class, member.object_id)) {
            return CONFLICT;
        }
    }
    OK
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn requirement_names_are_bounded_unique_and_not_self() {
        use crate::package::source::Text;
        let span = |text: &str| Text {
            data: text.as_ptr(),
            length: text.len() as u32,
        };
        let check = |names: &[Text]| unsafe {
            seekdb_runtime_extension_requires_validate(
                b"consumer".as_ptr(),
                8,
                names.as_ptr(),
                names.len() as u32,
            )
        };
        assert_eq!(check(&[]), OK);
        assert_eq!(check(&[span("alpha"), span("zulu")]), OK);
        assert_eq!(check(&[span("alpha"), span("alpha")]), CONFLICT);
        for invalid in ["consumer", "", "../escape", "x--y", "not a package", "a\0b"] {
            assert_eq!(check(&[span(invalid)]), INVALID);
        }
        assert_eq!(check(&[span("alpha"); 65]), INVALID);
        assert_eq!(
            unsafe {
                seekdb_runtime_extension_requires_validate(
                    b"consumer".as_ptr(),
                    8,
                    std::ptr::null(),
                    1,
                )
            },
            INVALID
        );
    }
    #[derive(Default)]
    struct Driver {
        phases: Vec<u32>,
        fail: u32,
        rollback_error: i32,
        missing_id: bool,
    }
    unsafe extern "C" fn step(context: *mut c_void, phase: u32, id: *mut u64) -> i32 {
        let driver = unsafe { &mut *context.cast::<Driver>() };
        driver.phases.push(phase);
        if phase == RECORD && !driver.missing_id {
            unsafe { *id = 42 };
        }
        if phase == ROLLBACK {
            driver.rollback_error
        } else if phase == driver.fail {
            -4001
        } else {
            OK
        }
    }
    fn run(driver: &mut Driver) -> InstallResult {
        let mut result = InstallResult::default();
        assert_eq!(
            unsafe {
                seekdb_runtime_extension_install_run(
                    (driver as *mut Driver).cast(),
                    Some(step),
                    &mut result,
                )
            },
            OK
        );
        result
    }
    #[test]
    fn coordinator_commits_only_after_schema_and_membership_recording() {
        let mut driver = Driver::default();
        let result = run(&mut driver);
        assert_eq!(driver.phases, [PREFLIGHT, BEGIN, APPLY, RECORD, COMMIT]);
        assert_eq!(
            (result.outcome, result.extension_id, result.failed_phase),
            (COMMITTED, 42, 0)
        );
    }
    #[test]
    fn coordinator_rolls_back_each_precommit_failure_but_not_preflight_or_commit() {
        for fail in [PREFLIGHT, BEGIN, APPLY, RECORD, COMMIT] {
            let mut driver = Driver {
                fail,
                ..Driver::default()
            };
            let result = run(&mut driver);
            assert_eq!(
                (result.failed_phase, result.operation_status),
                (fail, -4001)
            );
            if fail == PREFLIGHT {
                assert_eq!(result.outcome, NOT_STARTED);
                assert_eq!(driver.phases, [PREFLIGHT]);
            } else if fail == COMMIT {
                assert_eq!((result.outcome, result.extension_id), (COMMIT_UNKNOWN, 42));
                assert!(!driver.phases.contains(&ROLLBACK));
            } else {
                assert_eq!((result.outcome, result.extension_id), (ROLLED_BACK, 0));
                assert_eq!(driver.phases.last(), Some(&ROLLBACK));
                assert!(!driver.phases.contains(&COMMIT));
            }
        }
    }
    #[test]
    fn coordinator_retains_primary_and_cleanup_error_without_false_rollback_success() {
        let mut driver = Driver {
            fail: RECORD,
            rollback_error: -4002,
            ..Driver::default()
        };
        let result = run(&mut driver);
        assert_eq!(
            (
                result.outcome,
                result.operation_status,
                result.rollback_status
            ),
            (ROLLBACK_UNKNOWN, -4001, -4002)
        );
        assert_eq!(result.extension_id, 42);
    }
    #[test]
    fn coordinator_rejects_missing_identity_and_invalid_ffi() {
        let mut driver = Driver {
            missing_id: true,
            ..Driver::default()
        };
        let result = run(&mut driver);
        assert_eq!((result.failed_phase, result.outcome), (RECORD, ROLLED_BACK));
        assert!(!driver.phases.contains(&COMMIT));
        assert_eq!(
            unsafe {
                seekdb_runtime_extension_install_run(
                    std::ptr::null_mut(),
                    None,
                    std::ptr::null_mut(),
                )
            },
            INVALID
        );
        let mut result = InstallResult {
            extension_id: 99,
            ..InstallResult::default()
        };
        assert_eq!(
            unsafe {
                seekdb_runtime_extension_install_run(std::ptr::null_mut(), None, &mut result)
            },
            INVALID
        );
        assert_eq!(result.extension_id, 0);
    }
    fn validate(
        tenant: u64,
        database: u64,
        owner: u64,
        name: &[u8],
        module: &[u8],
        members: &[Member],
    ) -> i32 {
        unsafe {
            seekdb_runtime_extension_install_validate(
                tenant,
                database,
                owner,
                name.as_ptr(),
                name.len() as u32,
                b"1.0".as_ptr(),
                3,
                module.as_ptr(),
                module.len() as u32,
                members.as_ptr(),
                members.len() as u32,
            )
        }
    }
    #[test]
    fn database_identity_has_no_generation_and_allows_pure_sql_packages() {
        assert_eq!(validate(1, 1001, 500, "检索扩展".as_bytes(), b"", &[]), OK);
        assert_eq!(
            validate(1, 1002, 500, "检索扩展".as_bytes(), b"org.seekdb.ai", &[]),
            OK
        );
        for (tenant, database, owner) in [(0, 1, 1), (1, 0, 1), (1, 1, 0), (1, u64::MAX, 1)] {
            assert_eq!(
                validate(tenant, database, owner, b"extension", b"", &[]),
                INVALID
            );
        }
    }
    #[test]
    fn member_identity_is_class_and_object_not_plugin_descriptor_or_generation() {
        let a = Member {
            object_class: 1,
            reserved: 0,
            object_id: 42,
        };
        let b = Member {
            object_class: 2,
            ..a
        };
        assert_eq!(validate(1, 2, 3, b"e", b"", &[a, b]), OK);
        assert_eq!(validate(1, 2, 3, b"e", b"", &[a, b, a]), CONFLICT);
        assert_eq!(
            validate(1, 2, 3, b"e", b"", &[Member { object_id: 0, ..a }]),
            INVALID
        );
        assert_eq!(
            validate(1, 2, 3, b"e", b"", &[Member { reserved: 1, ..a }]),
            INVALID
        );
    }
    #[test]
    fn invalid_utf8_controls_module_names_and_bounds_are_rejected() {
        for name in [&b""[..], &b"bad\0name"[..], &b"bad\nname"[..], &[0xff][..]] {
            assert_eq!(validate(1, 2, 3, name, b"", &[]), INVALID);
        }
        assert_eq!(validate(1, 2, 3, b"e", b"../foreign/lib.so", &[]), INVALID);
        assert_eq!(validate(1, 2, 3, &[b'a'; 256], b"", &[]), INVALID);
        let member = Member {
            object_class: 1,
            reserved: 0,
            object_id: 1,
        };
        assert_eq!(validate(1, 2, 3, b"e", b"", &[member; 4097]), INVALID);
    }
}
