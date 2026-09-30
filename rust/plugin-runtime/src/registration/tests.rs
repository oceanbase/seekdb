// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
use super::*;
use std::sync::{
    atomic::{AtomicUsize, Ordering},
    Arc,
};

struct Payload(Arc<AtomicUsize>);
impl Drop for Payload {
    fn drop(&mut self) {
        self.0.fetch_add(1, Ordering::SeqCst);
    }
}
unsafe extern "C" fn release(pointer: *mut c_void) {
    drop(unsafe { Box::from_raw(pointer.cast::<Payload>()) });
}
fn stage(
    j: &mut Registration,
    token: *const RegistrationToken,
    family: u32,
    major: u32,
    key: &[u8],
    bytes: u64,
    drops: &Arc<AtomicUsize>,
) -> i32 {
    let payload = Box::into_raw(Box::new(Payload(drops.clone()))).cast();
    let status = unsafe { j.stage(token, family, major, key, bytes, payload, release) };
    if status != OK {
        unsafe {
            release(payload);
        }
    }
    status
}
fn journal() -> Registration {
    let mut j = Registration::default();
    assert_eq!(j.open(), OK);
    j
}

#[test]
fn atomic_mixed_commit_and_conflicting_commit_retains_ownership() {
    let drops = Arc::new(AtomicUsize::new(0));
    let mut j = journal();
    let a = j.begin().unwrap();
    let b = j.begin().unwrap();
    assert_eq!(stage(&mut j, a, EXTENSION, 0, b"type.id", 64, &drops), OK);
    assert_eq!(stage(&mut j, b, SERVICE, 1, b"call.id", 0, &drops), OK);
    assert_eq!(stage(&mut j, b, EXTENSION, 0, b"type.id", 64, &drops), OK);
    assert!(j.committed.is_empty());
    assert_eq!(j.commit(a), OK);
    assert_eq!(j.commit(b), CONFLICT);
    assert_eq!(j.committed.len(), 1);
    assert_eq!(drops.load(Ordering::SeqCst), 0);
    assert_eq!(j.abort(b), OK);
    assert_eq!(drops.load(Ordering::SeqCst), 2);
    assert_eq!(j.services, 0);
    assert_eq!(j.extensions, 1);
    assert_eq!(j.bytes, 64);
    assert_eq!(j.seal(), OK);
    j.clear();
    assert_eq!(drops.load(Ordering::SeqCst), 3);
    drop(j);
    assert_eq!(drops.load(Ordering::SeqCst), 3);
}

#[test]
fn keys_are_copied_and_service_major_is_part_of_identity() {
    let drops = Arc::new(AtomicUsize::new(0));
    let mut j = journal();
    let t = j.begin().unwrap();
    let mut key = b"call.id".to_vec();
    assert_eq!(stage(&mut j, t, SERVICE, 1, &key, 0, &drops), OK);
    key.fill(b'x');
    assert_eq!(
        stage(&mut j, t, SERVICE, 1, b"call.id", 0, &drops),
        CONFLICT
    );
    assert_eq!(stage(&mut j, t, SERVICE, 2, b"call.id", 0, &drops), OK);
    assert_eq!(stage(&mut j, t, EXTENSION, 0, b"call.id", 1, &drops), OK);
    assert_eq!(j.commit(t), OK);
    assert_eq!(j.committed.len(), 3);
    drop(j);
    assert_eq!(drops.load(Ordering::SeqCst), 4);
}

#[test]
fn stale_foreign_and_unknown_tokens_never_alias_new_transactions() {
    let mut j = journal();
    let old = j.begin().unwrap();
    assert_eq!(j.commit(old), OK);
    let next = j.begin().unwrap();
    assert_ne!(old, next);
    let mut other = journal();
    let foreign = other.begin().unwrap();
    for token in [
        old,
        foreign,
        ptr::null_mut(),
        ptr::without_provenance_mut(1),
    ] {
        assert_eq!(j.commit(token), STATE_MISMATCH);
        assert_eq!(j.abort(token), STATE_MISMATCH);
    }
    assert_eq!(j.abort(next), OK);
    assert_eq!(j.abort(next), STATE_MISMATCH);
}

#[test]
fn seal_with_unfinished_transaction_closes_admission_and_allows_abort() {
    let mut never_opened = Registration::default();
    never_opened.clear();
    assert_eq!(never_opened.open(), STATE_MISMATCH);
    let mut j = journal();
    let t = j.begin().unwrap();
    assert_eq!(j.seal(), STATE_MISMATCH);
    assert_eq!(j.begin(), Err(STATE_MISMATCH));
    assert_eq!(j.commit(t), STATE_MISMATCH);
    assert_eq!(j.abort(t), OK);
    assert_eq!(j.open(), STATE_MISMATCH);
}

#[test]
fn token_tombstones_are_bounded_including_empty_transactions() {
    let mut j = journal();
    for _ in 0..MAX_TOKENS {
        let t = j.begin().unwrap();
        assert_eq!(j.commit(t), OK);
    }
    assert_eq!(j.begin(), Err(LIMIT));
    j.clear();
    assert_eq!(j.open(), STATE_MISMATCH);
}

#[test]
fn byte_budget_spans_transactions_and_abort_reclaims_it() {
    let drops = Arc::new(AtomicUsize::new(0));
    let mut j = journal();
    let a = j.begin().unwrap();
    let b = j.begin().unwrap();
    for i in 0..1024 {
        let key = format!("object.{i}");
        assert_eq!(
            stage(
                &mut j,
                if i % 2 == 0 { a } else { b },
                EXTENSION,
                0,
                key.as_bytes(),
                65536,
                &drops
            ),
            OK
        );
    }
    assert_eq!(
        stage(&mut j, a, EXTENSION, 0, b"overflow", 1, &drops),
        LIMIT
    );
    assert_eq!(j.abort(b), OK);
    assert_eq!(
        stage(&mut j, a, EXTENSION, 0, b"after.abort", 1, &drops),
        OK
    );
    drop(j);
    assert_eq!(drops.load(Ordering::SeqCst), 1026);
}

#[test]
fn rejected_stage_leaves_payload_with_caller_and_clear_drops_open_entries() {
    let drops = Arc::new(AtomicUsize::new(0));
    let mut j = journal();
    let t = j.begin().unwrap();
    let payload = Box::into_raw(Box::new(Payload(drops.clone()))).cast();
    assert_eq!(
        unsafe { j.stage(t, 999, 0, b"key", 1, payload, release) },
        INVALID
    );
    assert_eq!(drops.load(Ordering::SeqCst), 0);
    unsafe {
        release(payload);
    }
    assert_eq!(stage(&mut j, t, SERVICE, 1, b"key", 0, &drops), OK);
    j.clear();
    assert_eq!(drops.load(Ordering::SeqCst), 2);
    assert_eq!(j.abort(t), STATE_MISMATCH);
}

#[test]
fn ffi_nulls_and_output_initialization() {
    unsafe {
        let j = seekdb_runtime_registration_create();
        assert!(!j.is_null());
        let mut token = ptr::without_provenance_mut(1);
        assert_eq!(
            seekdb_runtime_registration_begin(j, &mut token),
            STATE_MISMATCH
        );
        assert!(token.is_null());
        assert_eq!(seekdb_runtime_registration_open(j), OK);
        assert_eq!(
            seekdb_runtime_registration_begin(j, ptr::null_mut()),
            INVALID
        );
        assert_eq!(seekdb_runtime_registration_begin(j, &mut token), OK);
        let mut stats = RegistrationStats::default();
        assert_eq!(seekdb_runtime_registration_stats(j, &mut stats), OK);
        assert_eq!(stats.open_transactions, 1);
        assert_eq!(stats.issued_transactions, 1);
        assert_eq!(seekdb_runtime_registration_clear(j), OK);
        seekdb_runtime_registration_destroy(j);
        seekdb_runtime_registration_destroy(ptr::null_mut());
    }
}
