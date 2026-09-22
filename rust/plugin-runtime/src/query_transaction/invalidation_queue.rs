// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
//! A bounded queue owned by ONE volatile plan cache. No worker, SQL or plugin
//! callback in Rust. Reserve an owned scalar snapshot while the journal is
//! frozen, before commit. Known commit publishes; known abort cancels. Losing
//! an unresolved owner conservatively evicts (does NOT publish schema or infer
//! commit). Retirement is legal only after the associated cache is inaccessible.
use super::*;
use std::sync::{Arc, Mutex, MutexGuard};

struct Batch {
    token: u64,
    ready: bool,
    required_version: u64,
    charge: usize,
    requests: Vec<RoutineInvalidation>, // reversed: pop is FIFO
}
struct State {
    slots: Vec<Option<Batch>>,
    next_token: u64,
    requests: usize,
    max_requests: usize,
    cursor: usize,
    closed: bool,
    retired: bool,
}
impl State {
    fn remove(&mut self, index: usize) {
        if let Some(batch) = self.slots[index].take() {
            self.requests -= batch.charge;
        }
    }
}
pub struct InvalidationQueue(Arc<Mutex<State>>);

// No host callback or fallible effect runs under this mutex. Recover poisoning
// only to permit ownership cleanup; ordinary methods have no panic-producing
// indexing unless our own bounded slot invariant has already been violated.
fn lock(state: &Mutex<State>) -> MutexGuard<'_, State> {
    state.lock().unwrap_or_else(|error| error.into_inner())
}

pub(super) struct Reservation {
    state: Arc<Mutex<State>>,
    index: usize,
    token: u64,
    resolved: bool,
}
impl Reservation {
    fn ready(&mut self, version: u64) {
        let mut state = lock(&self.state);
        if !state.retired {
            if let Some(batch) = state.slots[self.index].as_mut() {
                if batch.token == self.token {
                    batch.ready = true;
                    batch.required_version = batch.required_version.max(version);
                }
            }
        }
        self.resolved = true;
    }
    pub(super) fn publish(mut self, version: u64) {
        self.ready(version);
    }
    pub(super) fn cancel(mut self) {
        let mut state = lock(&self.state);
        if state.slots[self.index]
            .as_ref()
            .is_some_and(|b| b.token == self.token)
        {
            state.remove(self.index);
        }
        self.resolved = true;
    }
}
impl Drop for Reservation {
    fn drop(&mut self) {
        if !self.resolved {
            // An unknown outcome may actually have committed. Cache eviction is
            // safe even after rollback; dropping it could leave stale code.
            // This never makes provisional schemas or privileges visible.
            self.ready(0); // Keep the frozen schema-operation fence on unknown outcome.
        }
    }
}

/// # Safety
/// Returned handle has one host owner. Concurrent calls may borrow it, but its
/// destruction requires all borrows to have ended and cache retirement.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_invalidation_queue_create(
    max_batches: u32,
    max_requests: u32,
) -> *mut InvalidationQueue {
    if max_batches == 0 || max_batches > 1024 || max_requests == 0 || max_requests > 1_048_576 {
        return std::ptr::null_mut();
    }
    let mut slots = Vec::new();
    if slots.try_reserve_exact(max_batches as usize).is_err() {
        return std::ptr::null_mut();
    }
    slots.resize_with(max_batches as usize, || None);
    // SAFETY: Exact matching allocation and initialization; destroy uses Box.
    let pointer = unsafe { alloc(Layout::new::<InvalidationQueue>()).cast::<InvalidationQueue>() };
    if !pointer.is_null() {
        unsafe {
            pointer.write(InvalidationQueue(Arc::new(Mutex::new(State {
                slots,
                next_token: 0,
                requests: 0,
                max_requests: max_requests as usize,
                cursor: 0,
                closed: false,
                retired: false,
            }))));
        }
    }
    pointer
}

/// # Safety
/// Live queue handle, exclusive live transaction. Queue need not outlive the
/// journal: reservation owns shared scalar-only state. Neither grants DDL rights.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_reserve_invalidations(
    queue: *mut InvalidationQueue,
    transaction: *mut QueryTransaction,
    transaction_id: u64,
) -> i32 {
    let (Some(queue), Some(transaction)) =
        (unsafe { queue.as_ref() }, unsafe { transaction.as_mut() })
    else {
        return INVALID;
    };
    if transaction.check(transaction_id) != OK
        || transaction.phase != Phase::Preparing
        || transaction.first_error != 0
        || transaction.delivery.is_some()
    {
        return STATE_MISMATCH;
    }
    if transaction.invalidation_count == 0 {
        return OK;
    }
    let count = transaction.invalidation_count as usize;
    let mut state = lock(&queue.0);
    if state.closed || state.retired {
        return STATE_MISMATCH;
    }
    if state.next_token == u64::MAX || count > state.max_requests - state.requests {
        return LIMIT;
    }
    let Some(index) = state.slots.iter().position(Option::is_none) else {
        return LIMIT;
    };
    let mut requests = Vec::new();
    if requests.try_reserve_exact(count).is_err() {
        return NO_MEMORY;
    }
    let charge = requests.capacity();
    if charge > state.max_requests - state.requests {
        return LIMIT;
    }
    for record in &transaction.records {
        if let RecordValue::RoutineInvalidation {
            ticket,
            database,
            routine,
        } = record.value
        {
            requests.push(RoutineInvalidation {
                ticket,
                database,
                routine,
            });
        }
    }
    if requests.len() != count {
        return STATE_MISMATCH;
    }
    requests.reverse();
    state.next_token += 1;
    let token = state.next_token;
    state.slots[index] = Some(Batch {
        token,
        ready: false,
        required_version: transaction.schema_version,
        charge,
        requests,
    });
    state.requests += charge;
    transaction.delivery = Some(Reservation {
        state: Arc::clone(&queue.0),
        index,
        token,
        resolved: false,
    });
    OK
}

/// # Safety
/// Live queue and disjoint writable outputs. Single serialized host consumer;
/// producers/close may run concurrently. Zero token/request means no ready work.
/// Release the borrow before eviction. Failed eviction must NOT acknowledge.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_invalidation_queue_peek(
    queue: *mut InvalidationQueue,
    token: *mut u64,
    required_version: *mut u64,
    output: *mut RoutineInvalidation,
) -> i32 {
    let (Some(token), Some(required_version), Some(output)) = (
        unsafe { token.as_mut() },
        unsafe { required_version.as_mut() },
        unsafe { output.as_mut() },
    ) else {
        return INVALID;
    };
    *token = 0;
    *required_version = 0;
    *output = RoutineInvalidation::default();
    let Some(queue) = (unsafe { queue.as_ref() }) else {
        return INVALID;
    };
    let state = lock(&queue.0);
    if state.retired {
        return STATE_MISMATCH;
    }
    for offset in 0..state.slots.len() {
        let index = (state.cursor + offset) % state.slots.len();
        if let Some(batch) = state.slots[index].as_ref().filter(|b| b.ready) {
            if let Some(request) = batch.requests.last() {
                *token = batch.token;
                *required_version = batch.required_version;
                *output = RoutineInvalidation {
                    ticket: request.ticket,
                    database: request.database,
                    routine: request.routine,
                };
                break;
            }
        }
    }
    OK
}

/// # Safety
/// Live handle; same serialized consumer as peek. success=1 only after actual
/// eviction, success=0 retains the request and rotates to the next batch. Neither
/// can remove a reservation or a different/duplicate acknowledgement's request.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_invalidation_queue_complete(
    queue: *mut InvalidationQueue,
    token: u64,
    ticket: u64,
    success: u32,
) -> i32 {
    let Some(queue) = (unsafe { queue.as_ref() }) else {
        return INVALID;
    };
    if token == 0 || ticket == 0 || success > 1 {
        return INVALID;
    }
    let mut state = lock(&queue.0);
    if state.retired {
        return STATE_MISMATCH;
    }
    let Some(index) = state.slots.iter().position(|slot| {
        slot.as_ref().is_some_and(|b| {
            b.token == token && b.ready && b.requests.last().is_some_and(|r| r.ticket == ticket)
        })
    }) else {
        return STATE_MISMATCH;
    };
    state.cursor = (index + 1) % state.slots.len();
    if success == 1 {
        let batch = state.slots[index].as_mut().unwrap();
        batch.requests.pop();
        let empty = batch.requests.is_empty();
        if empty {
            // Vec::pop does not free its backing allocation. Keep the entire
            // batch charged until it is actually dropped, including after retry.
            state.remove(index);
        }
    }
    OK
}

/// # Safety
/// Live handle. Close rejects NEW reservations, preserves accepted promises and
/// permits draining. retire=1 only after cache access and its worker have ended:
/// all entries in that volatile cache are now unreachable, so requests can die.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_invalidation_queue_close(
    queue: *mut InvalidationQueue,
    retire: u32,
) -> i32 {
    let Some(queue) = (unsafe { queue.as_ref() }) else {
        return INVALID;
    };
    if retire > 1 {
        return INVALID;
    }
    let mut state = lock(&queue.0);
    state.closed = true;
    if retire == 1 {
        state.retired = true;
        for index in 0..state.slots.len() {
            state.remove(index);
        }
    }
    OK
}

/// # Safety
/// Exclusive ownership, after retirement of the associated cache and all queue
/// consumers. Outstanding producer reservations remain memory-safe via Arc.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_invalidation_queue_destroy(queue: *mut InvalidationQueue) {
    if !queue.is_null() {
        unsafe {
            seekdb_runtime_invalidation_queue_close(queue, 1);
            drop(Box::from_raw(queue));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    // All helpers own exclusively accessed journal handles and valid outputs.
    unsafe fn pending(routine: u64) -> *mut QueryTransaction {
        unsafe { pending_n(routine, 1) }
    }
    unsafe fn pending_n(routine: u64, requests: u64) -> *mut QueryTransaction {
        let tx = seekdb_runtime_query_transaction_create(77);
        assert!(!tx.is_null());
        unsafe {
            assert_eq!(
                seekdb_runtime_query_transaction_admit_ddl(tx, 77, 10, 7),
                OK
            );
            assert_eq!(
                seekdb_runtime_query_transaction_record_schema_version(tx, 77, 10, 500),
                OK
            );
            for offset in 0..requests {
                assert_eq!(
                    seekdb_runtime_query_transaction_record_invalidation(
                        tx,
                        77,
                        10,
                        100,
                        routine + offset
                    ),
                    OK
                );
            }
            let (mut version, mut count, mut error) = (0, 0, 0);
            assert_eq!(
                seekdb_runtime_query_transaction_begin_prepare(
                    tx,
                    77,
                    &mut version,
                    &mut count,
                    &mut error
                ),
                OK
            );
        }
        tx
    }
    unsafe fn commit(tx: *mut QueryTransaction) {
        unsafe {
            let mut error = 0;
            assert_eq!(
                seekdb_runtime_query_transaction_record_end_sign(tx, 77, 501),
                OK
            );
            assert_eq!(
                seekdb_runtime_query_transaction_complete_prepare(tx, 77, 501, 0, &mut error),
                OK
            );
            assert_eq!(
                seekdb_runtime_query_transaction_finish(tx, 77, 1, &mut error),
                OK
            );
            assert_eq!((*tx).invalidation_count, 0);
            seekdb_runtime_query_transaction_destroy(tx);
        }
    }
    unsafe fn peek(queue: *mut InvalidationQueue) -> (u64, RoutineInvalidation) {
        let mut token = 99;
        let mut version = 99;
        let mut output = RoutineInvalidation::default();
        unsafe {
            assert_eq!(
                seekdb_runtime_invalidation_queue_peek(
                    queue,
                    &mut token,
                    &mut version,
                    &mut output
                ),
                OK
            );
        }
        assert_eq!(version == 0, token == 0);
        (token, output)
    }

    #[test]
    fn precommit_reservation_commit_retry_and_exact_ack() {
        unsafe {
            let queue = seekdb_runtime_invalidation_queue_create(2, 2);
            assert!(!queue.is_null());
            let first = pending(900);
            let second = pending(901);
            let third = pending(902);
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, first, 78),
                STATE_MISMATCH
            );
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, first, 77),
                OK
            );
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, first, 77),
                STATE_MISMATCH
            );
            assert_eq!(peek(queue).0, 0); // Prepared is not deliverable.
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, second, 77),
                OK
            );
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, third, 77),
                LIMIT
            );
            assert!((*third).delivery.is_none());
            commit(first);
            commit(second);
            let (a, request) = peek(queue);
            assert_eq!(request.routine, 900);
            assert_eq!(peek(queue).0, a);
            assert_eq!(
                seekdb_runtime_invalidation_queue_complete(queue, a, request.ticket + 1, 1),
                STATE_MISMATCH
            );
            assert_eq!(
                seekdb_runtime_invalidation_queue_complete(queue, a, request.ticket, 2),
                INVALID
            );
            assert_eq!(
                seekdb_runtime_invalidation_queue_complete(queue, a, request.ticket, 0),
                OK
            );
            let (b, other) = peek(queue);
            assert_ne!(a, b);
            assert_eq!(other.routine, 901);
            assert_eq!(
                seekdb_runtime_invalidation_queue_complete(queue, b, other.ticket, 1),
                OK
            );
            assert_eq!(
                seekdb_runtime_invalidation_queue_complete(queue, b, other.ticket, 1),
                STATE_MISMATCH
            );
            assert_eq!(peek(queue).0, a); // Failed work survived owner destruction and rotation.
            assert_eq!(
                seekdb_runtime_invalidation_queue_complete(queue, a, request.ticket, 1),
                OK
            );
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, third, 77),
                OK
            );
            commit(third);
            let (c, last) = peek(queue);
            assert!(c > b);
            assert_eq!(last.routine, 902);
            assert_eq!(
                seekdb_runtime_invalidation_queue_complete(queue, a, last.ticket, 1),
                STATE_MISMATCH
            );
            assert_eq!(
                seekdb_runtime_invalidation_queue_complete(queue, c, last.ticket, 1),
                OK
            );
            assert_eq!(peek(queue).0, 0);
            assert_eq!(lock(&(*queue).0).requests, 0);
            seekdb_runtime_invalidation_queue_destroy(queue);
        }
    }

    #[test]
    fn abort_reclaims_but_unknown_conservatively_evicts_without_schema_publication() {
        unsafe {
            let queue = seekdb_runtime_invalidation_queue_create(1, 1);
            let aborted = pending(900);
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, aborted, 77),
                OK
            );
            let mut error = 0;
            assert_eq!(
                seekdb_runtime_query_transaction_finish(aborted, 77, 0, &mut error),
                OK
            );
            seekdb_runtime_query_transaction_destroy(aborted);
            assert_eq!(peek(queue).0, 0);
            let unknown = pending(901);
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, unknown, 77),
                OK
            );
            seekdb_runtime_query_transaction_destroy(unknown);
            let (token, request) = peek(queue);
            assert_ne!(token, 0);
            assert_eq!(request.routine, 901);
            assert_eq!(
                seekdb_runtime_invalidation_queue_complete(queue, token, request.ticket, 1),
                OK
            );
            assert_eq!(peek(queue).0, 0);
            seekdb_runtime_invalidation_queue_destroy(queue);
        }
    }

    #[test]
    fn close_keeps_promises_retirement_allows_late_outcomes_and_releases_owners() {
        unsafe {
            let queue = seekdb_runtime_invalidation_queue_create(2, 2);
            let first = pending(900);
            let second = pending(901);
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, first, 77),
                OK
            );
            assert_eq!(seekdb_runtime_invalidation_queue_close(queue, 0), OK);
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, second, 77),
                STATE_MISMATCH
            );
            commit(first);
            assert_ne!(peek(queue).0, 0); // Closing admission never cancels accepted work.
            seekdb_runtime_query_transaction_destroy(second);
            seekdb_runtime_invalidation_queue_destroy(queue);

            let queue = seekdb_runtime_invalidation_queue_create(1, 1);
            let late = pending(902);
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, late, 77),
                OK
            );
            let weak = Arc::downgrade(&(*queue).0);
            seekdb_runtime_invalidation_queue_destroy(queue); // Cache itself has gone away.
            assert!(weak.upgrade().is_some());
            commit(late); // No stale queue handle dereference or allocation.
            assert!(weak.upgrade().is_none());
        }
    }

    #[test]
    fn reserve_requires_freeze_request_budget_and_nonwrapping_batch_identity() {
        unsafe {
            assert!(seekdb_runtime_invalidation_queue_create(0, 1).is_null());
            assert!(seekdb_runtime_invalidation_queue_create(1, 0).is_null());
            let queue = seekdb_runtime_invalidation_queue_create(3, 1);
            let open = seekdb_runtime_query_transaction_create(77);
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, open, 77),
                STATE_MISMATCH
            );
            seekdb_runtime_query_transaction_destroy(open);
            let first = pending(900);
            let second = pending(901);
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, first, 77),
                OK
            );
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, second, 77),
                LIMIT
            );
            let mut error = 0;
            assert_eq!(
                seekdb_runtime_query_transaction_finish(first, 77, 0, &mut error),
                OK
            );
            seekdb_runtime_query_transaction_destroy(first);
            lock(&(*queue).0).next_token = u64::MAX;
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, second, 77),
                LIMIT
            );
            assert!((*second).delivery.is_none());
            seekdb_runtime_query_transaction_destroy(second);
            seekdb_runtime_invalidation_queue_destroy(queue);
        }
    }

    #[test]
    fn concurrent_producers_publish_distinct_batches_for_one_serialized_consumer() {
        unsafe {
            let queue = seekdb_runtime_invalidation_queue_create(32, 32);
            let shared = &*queue;
            std::thread::scope(|scope| {
                for routine in 1..=32 {
                    scope.spawn(move || {
                        let mut handle = InvalidationQueue(Arc::clone(&shared.0));
                        let tx = pending(routine);
                        assert_eq!(
                            seekdb_runtime_query_transaction_reserve_invalidations(
                                &mut handle,
                                tx,
                                77
                            ),
                            OK
                        );
                        commit(tx);
                    });
                }
            });
            let mut seen = std::collections::BTreeSet::new();
            for _ in 0..32 {
                let (token, request) = peek(queue);
                assert_ne!(token, 0);
                assert!(seen.insert(request.routine));
                assert_eq!(
                    seekdb_runtime_invalidation_queue_complete(queue, token, request.ticket, 1),
                    OK
                );
            }
            assert_eq!(peek(queue).0, 0);
            seekdb_runtime_invalidation_queue_destroy(queue);
        }
    }

    #[test]
    fn partial_ack_keeps_backing_storage_charged_until_batch_is_freed() {
        unsafe {
            let queue = seekdb_runtime_invalidation_queue_create(2, 2);
            let first = pending_n(900, 2);
            let second = pending(902);
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, first, 77),
                OK
            );
            commit(first);
            let (token, request) = peek(queue);
            assert_eq!(request.routine, 900);
            assert_eq!(
                seekdb_runtime_invalidation_queue_complete(queue, token, request.ticket, 1),
                OK
            );
            assert_eq!(lock(&(*queue).0).requests, 2);
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, second, 77),
                LIMIT
            );
            let (token, request) = peek(queue);
            assert_eq!(request.routine, 901);
            assert_eq!(
                seekdb_runtime_invalidation_queue_complete(queue, token, request.ticket, 1),
                OK
            );
            assert_eq!(lock(&(*queue).0).requests, 0);
            assert_eq!(
                seekdb_runtime_query_transaction_reserve_invalidations(queue, second, 77),
                OK
            );
            commit(second);
            seekdb_runtime_invalidation_queue_destroy(queue);
        }
    }
}
