// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.

//! Transaction-local catalog view undo, NOT a database transaction coordinator.
//! The host supplies barriers from its data transaction and owns all durable
//! writes/publication. No SQL savepoint names or independently committed state.

use crate::registration::{LIMIT, NO_MEMORY};
use crate::{INVALID, OK, STATE_MISMATCH};
use std::alloc::{alloc, Layout};
use std::ffi::c_void;
mod invalidation_queue;

const MAX_RECORDS: usize = 16_384;
const MAX_SEQUENCE: u64 = (1 << 47) - 1;
type Undo = unsafe extern "C" fn(*mut c_void) -> i32;
type Release = unsafe extern "C" fn(*mut c_void);

struct Record {
    sequence: u64,
    value: RecordValue,
}

enum RecordValue {
    View {
        payload: *mut c_void,
        undo: Undo,
        release: Release,
    },
    SchemaVersion {
        previous_max: u64,
        previous_sequence: u64,
    },
    RoutineInvalidation {
        ticket: u64,
        database: u64,
        routine: u64,
    },
}

#[derive(PartialEq, Eq)]
enum Phase {
    Open,
    Preparing,
    Sealed,
    Failed,
}

pub struct QueryTransaction {
    transaction_id: u64,
    // Data operation sequences never rewind, even when the view does.
    high_water: u64,
    records: Vec<Record>,
    schema_version: u64,
    schema_operations: u64,
    schema_sequence: u64,
    // Never rewound by savepoints, so a stale acknowledgement cannot consume
    // a later request for the same object after rollback/re-registration.
    invalidation_ticket: u64,
    invalidation_count: u64,
    first_error: i32,
    phase: Phase,
    // Finalization cannot roll back to a savepoint. Keep its one end-sign in a
    // fixed slot, so recording a successful SQL write never allocates, even at
    // the ordinary journal limit. Full abort still restores every private mark.
    end_sign_version: u64,
    // Host acquired its transaction-owned DDL lock after this root barrier.
    // Epoch is captured BEFORE the first schema write, never retrofitted at
    // commit. Rollback to/before the barrier invalidates this admission.
    ddl_epoch: u64,
    ddl_sequence: u64,
    finished: bool,
    committed: bool,
    delivery: Option<invalidation_queue::Reservation>,
}

impl QueryTransaction {
    fn check(&self, transaction_id: u64) -> i32 {
        if self.finished || transaction_id != self.transaction_id {
            STATE_MISMATCH
        } else {
            OK
        }
    }

    fn undo_from(&mut self, sequence: u64) {
        while self.records.last().is_some_and(|r| r.sequence >= sequence) {
            let record = self.records.pop().unwrap();
            // SAFETY: Admission transfers exclusive payload ownership. Pop
            // first: even a failing undo consumes it and is never retried.
            let status = match record.value {
                RecordValue::View { payload, undo, .. } => unsafe { undo(payload) },
                RecordValue::SchemaVersion {
                    previous_max,
                    previous_sequence,
                } => {
                    self.schema_version = previous_max;
                    self.schema_sequence = previous_sequence;
                    self.schema_operations -= 1;
                    0
                }
                RecordValue::RoutineInvalidation { .. } => {
                    self.invalidation_count -= 1;
                    0
                }
            };
            if self.first_error == 0 {
                self.first_error = status;
            }
        }
    }
}

impl Drop for QueryTransaction {
    fn drop(&mut self) {
        // Only restores the private view; cannot infer/alter database outcome.
        self.undo_from(0);
    }
}

#[cfg(test)]
mod invalidation_tests {
    use super::*;

    #[test]
    fn ticket_exhaustion_is_non_mutating_and_never_wraps() {
        // SAFETY: The test exclusively owns a live handle and disjoint outputs.
        unsafe {
            let tx = seekdb_runtime_query_transaction_create(77);
            assert!(!tx.is_null());
            assert_eq!(
                seekdb_runtime_query_transaction_admit_ddl(tx, 77, 10, 7),
                OK
            );
            assert_eq!(
                seekdb_runtime_query_transaction_record_schema_version(tx, 77, 10, 500),
                OK
            );
            (*tx).invalidation_ticket = u64::MAX - 1;
            assert_eq!(
                seekdb_runtime_query_transaction_record_invalidation(tx, 77, 10, 100, 900),
                OK
            );
            assert_eq!((*tx).invalidation_ticket, u64::MAX);
            assert_eq!(
                seekdb_runtime_query_transaction_record_invalidation(tx, 77, 10, 100, 901),
                LIMIT
            );
            assert_eq!((*tx).records.len(), 2);
            assert_eq!((*tx).invalidation_count, 1);
            let mut error = 0;
            assert_eq!(
                seekdb_runtime_query_transaction_rollback(tx, 77, 10, &mut error),
                OK
            );
            assert_eq!((*tx).schema_sequence, 0);
            assert_eq!((*tx).invalidation_count, 0);
            assert_eq!((*tx).invalidation_ticket, u64::MAX);
            assert_eq!(
                seekdb_runtime_query_transaction_admit_ddl(tx, 77, 20, 8),
                OK
            );
            assert_eq!(
                seekdb_runtime_query_transaction_record_schema_version(tx, 77, 20, 600),
                OK
            );
            assert_eq!(
                seekdb_runtime_query_transaction_record_invalidation(tx, 77, 20, 100, 900),
                LIMIT
            );
            seekdb_runtime_query_transaction_destroy(tx);
        }
    }

    #[test]
    fn routine_invalidation_ffi_layout() {
        assert_eq!(std::mem::size_of::<RoutineInvalidation>(), 24);
        assert_eq!(std::mem::offset_of!(RoutineInvalidation, ticket), 0);
        assert_eq!(std::mem::offset_of!(RoutineInvalidation, database), 8);
        assert_eq!(std::mem::offset_of!(RoutineInvalidation, routine), 16);
    }

    #[test]
    fn host_failure_is_sticky_in_every_unfinished_phase() {
        for phase in [Phase::Open, Phase::Preparing, Phase::Sealed, Phase::Failed] {
            // SAFETY: test owns a fresh handle exclusively and disjoint outputs.
            unsafe {
                let tx = seekdb_runtime_query_transaction_create(77);
                assert!(!tx.is_null());
                (*tx).phase = phase;
                assert_eq!(seekdb_runtime_query_transaction_fail(tx, 77, -4012), OK);
                assert_eq!(seekdb_runtime_query_transaction_fail(tx, 77, -4002), OK);
                assert_eq!((*tx).first_error, -4012);
                let mut error = 0;
                let (mut version, mut operations) = (9, 9);
                assert_eq!(
                    seekdb_runtime_query_transaction_schema_state(
                        tx,
                        77,
                        &mut version,
                        &mut operations,
                        &mut error
                    ),
                    STATE_MISMATCH
                );
                assert_eq!((version, operations, error), (0, 0, -4012));
                assert_eq!(
                    seekdb_runtime_query_transaction_prepare_commit(tx, 77, &mut error),
                    STATE_MISMATCH
                );
                assert_eq!(
                    seekdb_runtime_query_transaction_finish(tx, 77, 1, &mut error),
                    STATE_MISMATCH
                );
                assert_eq!(
                    seekdb_runtime_query_transaction_rollback(tx, 77, 10, &mut error),
                    STATE_MISMATCH
                );
                assert_eq!(
                    seekdb_runtime_query_transaction_finish(tx, 77, 0, &mut error),
                    OK
                );
                assert_eq!(error, -4012);
                assert_eq!(
                    seekdb_runtime_query_transaction_fail(tx, 77, -4013),
                    STATE_MISMATCH
                );
                seekdb_runtime_query_transaction_destroy(tx);
            }
        }
    }

    #[test]
    fn invalid_host_failure_never_changes_transaction_admission() {
        // SAFETY: test owns a fresh handle exclusively and disjoint outputs.
        unsafe {
            assert_eq!(
                seekdb_runtime_query_transaction_fail(std::ptr::null_mut(), 77, -1),
                INVALID
            );
            let tx = seekdb_runtime_query_transaction_create(77);
            assert!(!tx.is_null());
            assert_eq!(
                seekdb_runtime_query_transaction_fail(tx, 78, -1),
                STATE_MISMATCH
            );
            assert_eq!(seekdb_runtime_query_transaction_fail(tx, 77, 0), INVALID);
            assert_eq!((*tx).first_error, 0);
            assert!((*tx).phase == Phase::Open);
            let mut error = 0;
            assert_eq!(
                seekdb_runtime_query_transaction_prepare_commit(tx, 77, &mut error),
                OK
            );
            assert_eq!(
                seekdb_runtime_query_transaction_finish(tx, 77, 1, &mut error),
                OK
            );
            assert_eq!(
                seekdb_runtime_query_transaction_fail(tx, 77, -1),
                STATE_MISMATCH
            );
            assert_eq!((*tx).first_error, 0);
            seekdb_runtime_query_transaction_destroy(tx);
        }
    }
}

#[no_mangle]
pub extern "C" fn seekdb_runtime_query_transaction_create(
    transaction_id: u64,
) -> *mut QueryTransaction {
    if transaction_id == 0 {
        return std::ptr::null_mut();
    }
    // SAFETY: Matching layout, checked allocation; destroy reconstructs Box.
    unsafe {
        let pointer = alloc(Layout::new::<QueryTransaction>()).cast::<QueryTransaction>();
        if !pointer.is_null() {
            pointer.write(QueryTransaction {
                transaction_id,
                high_water: 0,
                records: Vec::new(),
                schema_version: 0,
                schema_operations: 0,
                schema_sequence: 0,
                invalidation_ticket: 0,
                invalidation_count: 0,
                first_error: 0,
                phase: Phase::Open,
                end_sign_version: 0,
                ddl_epoch: 0,
                ddl_sequence: 0,
                finished: false,
                committed: false,
                delivery: None,
            });
        }
        pointer
    }
}

/// # Safety
/// Exclusive live handle; callbacks/payload valid until consumed. Callbacks
/// must not unwind, reenter this handle or invoke arbitrary plugin code.
/// A non-OK result preserves caller ownership of payload.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_record(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    sequence: u64,
    payload: *mut c_void,
    undo: Option<Undo>,
    release: Option<Release>,
) -> i32 {
    // SAFETY: Caller guarantees exclusive handle access for the entire call.
    let Some(transaction) = (unsafe { transaction.as_mut() }) else {
        return INVALID;
    };
    let (Some(undo), Some(release)) = (undo, release) else {
        return INVALID;
    };
    if payload.is_null() || sequence == 0 || sequence > MAX_SEQUENCE {
        return INVALID;
    }
    if transaction.check(transaction_id) != OK
        || transaction.phase != Phase::Open
        || transaction.first_error != 0
        || sequence < transaction.high_water
    {
        return STATE_MISMATCH;
    }
    if transaction.records.len() == MAX_RECORDS {
        return LIMIT;
    }
    if transaction.records.try_reserve(1).is_err() {
        return NO_MEMORY;
    }
    transaction.records.push(Record {
        sequence,
        value: RecordValue::View {
            payload,
            undo,
            release,
        },
    });
    transaction.high_water = sequence;
    OK
}

/// Record a successfully written catalog operation, not merely a reserved
/// version. Host must roll back the enclosing data operation if recording fails.
/// Versions may be reserved out of order; publication needs the surviving max,
/// not the most recently appended value. Uses the same bounded barrier journal.
/// # Safety
/// Exclusive live handle. sequence is the host's enclosing root data barrier.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_record_schema_version(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    sequence: u64,
    version: u64,
) -> i32 {
    // SAFETY: The host provides exclusive ownership for this call.
    let Some(transaction) = (unsafe { transaction.as_mut() }) else {
        return INVALID;
    };
    if sequence == 0 || sequence > MAX_SEQUENCE || version == 0 || version > i64::MAX as u64 {
        return INVALID;
    }
    if transaction.check(transaction_id) != OK
        || transaction.phase != Phase::Open
        || transaction.first_error != 0
        || sequence < transaction.high_water
    {
        return STATE_MISMATCH;
    }
    if transaction.records.len() == MAX_RECORDS {
        return LIMIT;
    }
    if transaction.records.try_reserve(1).is_err() {
        return NO_MEMORY;
    }
    transaction.records.push(Record {
        sequence,
        value: RecordValue::SchemaVersion {
            previous_max: transaction.schema_version,
            previous_sequence: transaction.schema_sequence,
        },
    });
    transaction.schema_version = transaction.schema_version.max(version);
    transaction.schema_operations += 1;
    transaction.schema_sequence = sequence;
    transaction.high_water = sequence;
    OK
}

/// Register a host-only cache invalidation after a successful schema write at
/// the SAME root data barrier. This neither flushes caches nor authorizes DROP.
/// Uses the shared record budget; rollback must undo data first. Duplicate
/// objects get distinct monotonic tickets (invalidations are idempotent).
/// # Safety
/// Exclusive live handle. Host owns the actual DDL locks and data transaction.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_record_invalidation(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    sequence: u64,
    database: u64,
    routine: u64,
) -> i32 {
    // SAFETY: Host guarantees exclusive access to the live handle.
    let Some(transaction) = (unsafe { transaction.as_mut() }) else {
        return INVALID;
    };
    if sequence == 0
        || sequence > MAX_SEQUENCE
        || database == 0
        || routine == 0
        || database > i64::MAX as u64
        || routine > i64::MAX as u64
    {
        return INVALID;
    }
    if transaction.check(transaction_id) != OK
        || transaction.phase != Phase::Open
        || transaction.first_error != 0
        || transaction.ddl_epoch == 0
        || sequence < transaction.high_water
        || transaction.schema_sequence != sequence
    {
        return STATE_MISMATCH;
    }
    if transaction.records.len() == MAX_RECORDS || transaction.invalidation_ticket == u64::MAX {
        return LIMIT;
    }
    if transaction.records.try_reserve(1).is_err() {
        return NO_MEMORY;
    }
    transaction.invalidation_ticket += 1;
    transaction.records.push(Record {
        sequence,
        value: RecordValue::RoutineInvalidation {
            ticket: transaction.invalidation_ticket,
            database,
            routine,
        },
    });
    transaction.invalidation_count += 1;
    transaction.high_water = sequence;
    OK
}

/// # Safety
/// Exclusive live handle, disjoint writable outputs. Count is available before
/// finish and after known commit, not after abort/poison. No delivery authority.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_invalidation_count(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    count: *mut u64,
    host_error: *mut i32,
) -> i32 {
    // SAFETY: Caller guarantees both output regions are writable and disjoint.
    let (Some(count), Some(host_error)) =
        (unsafe { count.as_mut() }, unsafe { host_error.as_mut() })
    else {
        return INVALID;
    };
    *count = 0;
    *host_error = 0;
    // SAFETY: Live handle is exclusively accessible throughout this call.
    let Some(transaction) = (unsafe { transaction.as_ref() }) else {
        return INVALID;
    };
    if transaction_id != transaction.transaction_id
        || (transaction.finished && !transaction.committed)
    {
        return STATE_MISMATCH;
    }
    *host_error = transaction.first_error;
    if transaction.first_error != 0 || transaction.phase == Phase::Failed {
        return STATE_MISMATCH;
    }
    *count = transaction.invalidation_count;
    OK
}

#[repr(C)]
#[derive(Default)]
pub struct RoutineInvalidation {
    ticket: u64,
    database: u64,
    routine: u64,
}

/// Inspect the oldest remaining request ONLY AFTER verified commit. Empty is
/// OK with all-zero output. Host releases this FFI borrow before scheduling;
/// failures leave the same request available for retry. No arbitrary callbacks.
/// # Safety
/// Exclusive live handle and disjoint writable output, valid for the call.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_peek_invalidation(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    output: *mut RoutineInvalidation,
) -> i32 {
    // SAFETY: Host supplies a writable disjoint ABI output.
    let Some(output) = (unsafe { output.as_mut() }) else {
        return INVALID;
    };
    *output = RoutineInvalidation::default();
    // SAFETY: Host supplies a live exclusively owned handle.
    let Some(transaction) = (unsafe { transaction.as_ref() }) else {
        return INVALID;
    };
    if transaction_id != transaction.transaction_id
        || !transaction.finished
        || !transaction.committed
    {
        return STATE_MISMATCH;
    }
    if let Some(record) = transaction.records.last() {
        let RecordValue::RoutineInvalidation {
            ticket,
            database,
            routine,
        } = record.value
        else {
            return STATE_MISMATCH;
        };
        *output = RoutineInvalidation {
            ticket,
            database,
            routine,
        };
    }
    OK
}

/// Acknowledge ONLY after successful, lifetime-safe handoff or actual eviction.
/// A ticket belongs to this specific transaction; wrong/repeated ack consumes
/// nothing, including for adjacent duplicate object requests. Not DB commit.
/// # Safety
/// Exclusive live handle. Host is responsible for eventual delivery/recovery
/// and must not destroy an owner with unacknowledged committed requests.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_ack_invalidation(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    ticket: u64,
) -> i32 {
    // SAFETY: Caller has exclusive access to the live handle.
    let Some(transaction) = (unsafe { transaction.as_mut() }) else {
        return INVALID;
    };
    if ticket == 0 {
        return INVALID;
    }
    if transaction_id != transaction.transaction_id
        || !transaction.finished
        || !transaction.committed
    {
        return STATE_MISMATCH;
    }
    if !matches!(transaction.records.last().map(|r| &r.value),
        Some(RecordValue::RoutineInvalidation { ticket: current, .. }) if *current == ticket)
    {
        return STATE_MISMATCH;
    }
    transaction.records.pop();
    transaction.invalidation_count -= 1;
    OK
}

/// Read transaction-owned publication inputs, including after seal but before
/// finish. Zero/zero means no surviving schema operations, not schema version 0.
/// This does not authorize commit or advance a shared schema watermark.
/// # Safety
/// Exclusive live handle and disjoint writable output pointers.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_schema_state(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    version: *mut u64,
    operations: *mut u64,
    host_error: *mut i32,
) -> i32 {
    // SAFETY: Outputs are disjoint and writable for the duration of the call.
    let (Some(version), Some(operations), Some(host_error)) = (
        unsafe { version.as_mut() },
        unsafe { operations.as_mut() },
        unsafe { host_error.as_mut() },
    ) else {
        return INVALID;
    };
    *version = 0;
    *operations = 0;
    *host_error = 0;
    // SAFETY: The host exclusively owns the handle.
    let Some(transaction) = (unsafe { transaction.as_mut() }) else {
        return INVALID;
    };
    if transaction.check(transaction_id) != OK {
        return STATE_MISMATCH;
    }
    *host_error = transaction.first_error;
    if transaction.first_error != 0 || transaction.phase == Phase::Failed {
        return STATE_MISMATCH;
    }
    *version = transaction.schema_version.max(transaction.end_sign_version);
    *operations = transaction.schema_operations + u64::from(transaction.end_sign_version != 0);
    OK
}

/// Reject all further operation/commit admission after unsafe host cleanup.
/// Does not undo views, infer a database outcome, release locks, or abort data.
/// The host must revoke view access separately and retain this owner until the
/// actual transaction ends. Repeated failure keeps the first cause.
/// # Safety
/// Exclusive live handle; no callbacks or SQL run during this call.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_fail(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    host_error: i32,
) -> i32 {
    let Some(transaction) = (unsafe { transaction.as_mut() }) else {
        return INVALID;
    };
    if host_error == OK {
        return INVALID;
    }
    if transaction.check(transaction_id) != OK {
        return STATE_MISMATCH;
    }
    if transaction.first_error == OK {
        transaction.first_error = host_error;
    }
    transaction.phase = Phase::Failed;
    OK
}

/// # Safety
/// Exclusive live handle and disjoint writable host_error. Host must already
/// have successfully rolled its data transaction back to the resolved barrier.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_rollback(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    sequence: u64,
    host_error: *mut i32,
) -> i32 {
    // SAFETY: Output/handle are writable, disjoint and exclusively borrowed.
    let Some(host_error) = (unsafe { host_error.as_mut() }) else {
        return INVALID;
    };
    *host_error = 0;
    let Some(transaction) = (unsafe { transaction.as_mut() }) else {
        return INVALID;
    };
    if sequence == 0 || sequence > MAX_SEQUENCE {
        return INVALID;
    }
    if transaction.check(transaction_id) != OK || transaction.phase != Phase::Open {
        return STATE_MISMATCH;
    }
    transaction.undo_from(sequence);
    if sequence <= transaction.ddl_sequence {
        transaction.ddl_epoch = 0;
        transaction.ddl_sequence = 0;
    }
    *host_error = transaction.first_error;
    OK
}

/// # Safety
/// Same exclusive ownership as rollback. committed is 1 only after verified
/// durable success, 0 after verified abort. Neither path publishes schemas.
/// Unknown commit outcomes must invalidate/discard the private view via destroy,
/// without claiming either commit or abort of the underlying data transaction.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_finish(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    committed: u32,
    host_error: *mut i32,
) -> i32 {
    // SAFETY: Output/handle are writable, disjoint and exclusively borrowed.
    let Some(host_error) = (unsafe { host_error.as_mut() }) else {
        return INVALID;
    };
    *host_error = 0;
    let Some(transaction) = (unsafe { transaction.as_mut() }) else {
        return INVALID;
    };
    if committed > 1 {
        return INVALID;
    }
    if transaction.check(transaction_id) != OK {
        return STATE_MISMATCH;
    }
    *host_error = transaction.first_error;
    if committed == 1 && (transaction.first_error != 0 || transaction.phase != Phase::Sealed) {
        return STATE_MISMATCH;
    }
    if committed == 0 {
        if let Some(reservation) = transaction.delivery.take() {
            reservation.cancel();
        }
        transaction.undo_from(0);
    } else {
        // Compact in place: no allocation at known-commit notification. Preserve
        // cache requests until host acknowledgement; release private marks once.
        // These release callbacks cannot unwind/reenter/run SQL or plugin code.
        // Keep the established newest-first mark release order. Popping the
        // retained requests later delivers them in original registration order.
        transaction.records.reverse();
        transaction.records.retain(|record| match record.value {
            RecordValue::View {
                payload, release, ..
            } => {
                // SAFETY: Release consumes the admitted exclusively owned mark.
                unsafe { release(payload) };
                false
            }
            RecordValue::SchemaVersion { .. } => false,
            RecordValue::RoutineInvalidation { .. } => true,
        });
        transaction.schema_version = 0;
        transaction.schema_operations = 0;
        transaction.schema_sequence = 0;
        if let Some(reservation) = transaction.delivery.take() {
            // Capacity and an owned scalar snapshot were reserved while frozen,
            // before durable commit. No allocator, SQL, or callback at handoff.
            reservation.publish(transaction.end_sign_version);
            transaction.records.clear();
            transaction.invalidation_count = 0;
        }
    }
    transaction.finished = true;
    transaction.committed = committed == 1;
    transaction.end_sign_version = 0;
    transaction.ddl_epoch = 0;
    transaction.ddl_sequence = 0;
    *host_error = transaction.first_error;
    OK
}

/// # Safety
/// Exclusive live handle and disjoint writable output. Called BEFORE submitting
/// commit for a view-only transaction; schema writes MUST use begin/complete
/// preparation. A rejected submission must abort or invalidate, never reopen.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_prepare_commit(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    host_error: *mut i32,
) -> i32 {
    // SAFETY: Writable disjoint output and exclusively borrowed handle.
    let Some(host_error) = (unsafe { host_error.as_mut() }) else {
        return INVALID;
    };
    *host_error = 0;
    let Some(transaction) = (unsafe { transaction.as_mut() }) else {
        return INVALID;
    };
    if transaction.check(transaction_id) != OK {
        return STATE_MISMATCH;
    }
    *host_error = transaction.first_error;
    if transaction.first_error != 0
        || transaction.phase != Phase::Open
        || transaction.schema_operations != 0
    {
        return STATE_MISMATCH;
    }
    transaction.phase = Phase::Sealed;
    OK
}

/// Freeze mutation BEFORE host DDL epoch/end-sign/MDS/watermark work. Outputs
/// are the surviving inputs, not a publication or commit decision. There is no
/// callback here: no Rust borrow remains live while the host performs SQL.
/// # Safety
/// Exclusive live handle and disjoint writable outputs. The host owns the
/// actual transaction and all required DDL locks/admission.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_begin_prepare(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    version: *mut u64,
    operations: *mut u64,
    host_error: *mut i32,
) -> i32 {
    // SAFETY: Same exclusive/disjoint contract as schema_state.
    let status = unsafe {
        seekdb_runtime_query_transaction_schema_state(
            transaction,
            transaction_id,
            version,
            operations,
            host_error,
        )
    };
    if status != OK {
        return status;
    }
    // SAFETY: schema_state validated pointers; its borrow has ended.
    let transaction = unsafe { &mut *transaction };
    if transaction.phase != Phase::Open
        || (transaction.schema_operations != 0 && transaction.ddl_epoch == 0)
    {
        // SAFETY: Validated, disjoint writable outputs.
        unsafe {
            *version = 0;
            *operations = 0;
        }
        return STATE_MISMATCH;
    }
    transaction.phase = Phase::Preparing;
    OK
}

/// Record host DDL lock admission before ANY schema write; cannot retroactively
/// bless existing writes. No SQL/lock is acquired here. A full data rollback to
/// the supplied barrier is required if subsequent admission/mutation fails.
/// # Safety
/// Exclusive live handle; host acquired the DDL lock on this actual transaction
/// after the supplied root barrier and captured a positive epoch beforehand.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_admit_ddl(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    sequence: u64,
    epoch: u64,
) -> i32 {
    // SAFETY: Host supplies an exclusively owned live handle.
    let Some(transaction) = (unsafe { transaction.as_mut() }) else {
        return INVALID;
    };
    if sequence == 0 || sequence > MAX_SEQUENCE || epoch == 0 || epoch > i64::MAX as u64 {
        return INVALID;
    }
    if transaction.check(transaction_id) != OK
        || transaction.phase != Phase::Open
        || transaction.first_error != 0
        || transaction.ddl_epoch != 0
        || transaction.schema_operations != 0
        || sequence < transaction.high_water
    {
        return STATE_MISMATCH;
    }
    transaction.ddl_epoch = epoch;
    transaction.ddl_sequence = sequence;
    transaction.high_water = sequence;
    OK
}

/// Read captured admission; zero/zero means none. Not proof that a SQL caller
/// holds locks: identity and actual data rollback sequencing remain host-owned.
/// # Safety
/// Exclusive live handle and disjoint writable outputs.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_ddl_admission(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    epoch: *mut u64,
    sequence: *mut u64,
    host_error: *mut i32,
) -> i32 {
    // SAFETY: Disjoint writable outputs and exclusive live handle.
    let (Some(epoch), Some(sequence), Some(host_error)) = (
        unsafe { epoch.as_mut() },
        unsafe { sequence.as_mut() },
        unsafe { host_error.as_mut() },
    ) else {
        return INVALID;
    };
    *epoch = 0;
    *sequence = 0;
    *host_error = 0;
    let Some(transaction) = (unsafe { transaction.as_ref() }) else {
        return INVALID;
    };
    if transaction.check(transaction_id) != OK {
        return STATE_MISMATCH;
    }
    *host_error = transaction.first_error;
    if transaction.first_error != 0 || transaction.phase == Phase::Failed {
        return STATE_MISMATCH;
    }
    *epoch = transaction.ddl_epoch;
    *sequence = transaction.ddl_sequence;
    OK
}

/// Check ordinary catalog writes against pre-write admission and journal order.
/// # Safety
/// Exclusive live handle and disjoint writable error output.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_check_ddl_write(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    sequence: u64,
    host_error: *mut i32,
) -> i32 {
    // SAFETY: Host owns the handle and disjoint output.
    let Some(host_error) = (unsafe { host_error.as_mut() }) else {
        return INVALID;
    };
    *host_error = 0;
    let Some(transaction) = (unsafe { transaction.as_ref() }) else {
        return INVALID;
    };
    if sequence == 0 || sequence > MAX_SEQUENCE {
        return INVALID;
    }
    if transaction.check(transaction_id) != OK {
        return STATE_MISMATCH;
    }
    *host_error = transaction.first_error;
    if transaction.first_error != 0
        || transaction.phase != Phase::Open
        || transaction.ddl_epoch == 0
        || sequence < transaction.high_water
    {
        return STATE_MISMATCH;
    }
    OK
}

/// Check finalization admission without consuming or changing its state. A
/// recorded end-sign still permits the subsequent MDS/watermark SQL steps.
/// # Safety
/// Exclusive live handle and disjoint writable error output.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_check_preparing(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    host_error: *mut i32,
) -> i32 {
    // SAFETY: Exclusive handle and disjoint writable output supplied by host.
    let Some(host_error) = (unsafe { host_error.as_mut() }) else {
        return INVALID;
    };
    *host_error = 0;
    let Some(transaction) = (unsafe { transaction.as_ref() }) else {
        return INVALID;
    };
    if transaction.check(transaction_id) != OK {
        return STATE_MISMATCH;
    }
    *host_error = transaction.first_error;
    if transaction.phase != Phase::Preparing || transaction.first_error != 0 {
        return STATE_MISMATCH;
    }
    OK
}

/// Record ONLY the host finalizer's successfully written DDL end-sign. Ordinary
/// operation recording is closed during preparation. No allocation or callback.
/// # Safety
/// Exclusive live handle; host has written this version in the SAME data
/// transaction. This interface is host-only, not a plugin registration API.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_record_end_sign(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    version: u64,
) -> i32 {
    // SAFETY: Exclusive live handle supplied by the host.
    let Some(transaction) = (unsafe { transaction.as_mut() }) else {
        return INVALID;
    };
    if version == 0 || version > i64::MAX as u64 {
        return INVALID;
    }
    if transaction.check(transaction_id) != OK
        || transaction.phase != Phase::Preparing
        || transaction.first_error != 0
        || transaction.schema_operations == 0
        || transaction.end_sign_version != 0
        || version <= transaction.schema_version
    {
        return STATE_MISMATCH;
    }
    transaction.end_sign_version = version;
    OK
}

/// Complete ONE host preparation attempt, after ALL database steps (including
/// transport restoration). Success seals; failure permanently denies commit.
/// A matching end-sign alone is insufficient: host_result attests completion of
/// the host protocol, never durable commit. Only full abort/discard is allowed
/// after failure. prepared_version is zero for no surviving schema operations.
/// # Safety
/// Exclusive live handle and disjoint writable output. Host must not report
/// success before epoch/end-sign/MDS/watermark work actually succeeds.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_complete_prepare(
    transaction: *mut QueryTransaction,
    transaction_id: u64,
    prepared_version: u64,
    host_result: i32,
    host_error: *mut i32,
) -> i32 {
    // SAFETY: Writable disjoint output and exclusively borrowed handle.
    let Some(host_error) = (unsafe { host_error.as_mut() }) else {
        return INVALID;
    };
    *host_error = 0;
    let Some(transaction) = (unsafe { transaction.as_mut() }) else {
        return INVALID;
    };
    if transaction.check(transaction_id) != OK {
        return STATE_MISMATCH;
    }
    *host_error = transaction.first_error;
    if transaction.phase != Phase::Preparing {
        return STATE_MISMATCH;
    }
    transaction.phase = Phase::Failed;
    if host_result != 0 {
        transaction.first_error = host_result;
        *host_error = host_result;
        return STATE_MISMATCH;
    }
    if (transaction.schema_operations != 0 && transaction.end_sign_version == 0)
        || prepared_version != transaction.end_sign_version
    {
        return STATE_MISMATCH;
    }
    transaction.phase = Phase::Sealed;
    OK
}

/// # Safety
/// Null or exclusively owned live handle; all payloads/callbacks remain valid.
/// No database operation or external publication is performed by destruction.
#[no_mangle]
pub unsafe extern "C" fn seekdb_runtime_query_transaction_destroy(
    transaction: *mut QueryTransaction,
) {
    if !transaction.is_null() {
        // SAFETY: Exactly one owner consumes the allocation from create.
        drop(unsafe { Box::from_raw(transaction) });
    }
}
