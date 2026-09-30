/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_HOST_PLUGIN_RUNTIME_H_
#define SEEKDB_HOST_PLUGIN_RUNTIME_H_

#include <stdint.h>

/* Internal host bridge, NOT part of the installed plugin SDK. The C++ owner
 * must retain the handle for all borrowing calls, including drain waiters.
 * Concurrency is specified per handle: generation borrowing supports concurrent
 * calls; cursor/transaction handles below require exclusive serialized use.
 * Destroy requires exclusive ownership and no outstanding borrows or leases.
 * No Rust layout crosses this boundary. */
#ifdef __cplusplus
extern "C" {
#endif

/* Generation-local host allocator. Concurrent borrowing is supported. Limits
 * count requested payload bytes and live allocations, NOT allocator/tracking
 * overhead, arbitrary plugin heaps, GPU memory or RSS. Zero denies allocation;
 * UINT64_MAX disables an explicit limit. Empty accounts have no tracking table.
 * alloc rejects zero size/invalid alignment; free validates the original owner,
 * size and alignment without reading through an unknown pointer. Invalid free
 * leaves the allocation charged; nullptr free is a no-op on a valid account.
 * close only denies new allocation: outstanding blocks remain valid/freeable.
 * destroy requires exclusive ROOT ownership after root borrows/raw block users
 * and module deinit; it closes admission and releases the root. Owned-byte
 * tokens retain the account independently; the final release reclaims remaining
 * raw bytes without running plugin destructors. Tokens do not pin plugin code.
 * This does not make arbitrary native pointer misuse/ABA safe. */
typedef struct seekdb_runtime_memory_account seekdb_runtime_memory_account;
/* Startup-only limit parser: decimal or "unlimited"; bytes also accept exact
 * KiB/MiB/GiB/TiB suffixes. No signs/whitespace/fractions. kind=0 bytes, 1 count.
 * Input length 1..64; readable input and aligned writable output must be
 * disjoint. Nulls/invalid kinds fail. Output is zero on failure. No retention. */
enum { SEEKDB_RUNTIME_MEMORY_LIMIT_BYTES = 0, SEEKDB_RUNTIME_MEMORY_LIMIT_ALLOCATIONS = 1 };
int32_t seekdb_runtime_memory_parse_limit(const uint8_t *, uint32_t length, uint32_t kind, uint64_t *);
typedef struct seekdb_runtime_memory_usage_t {
  uint64_t bytes, peak_bytes, allocations, peak_allocations;
  uint64_t allocation_failures, invalid_frees, byte_limit, allocation_limit;
} seekdb_runtime_memory_usage_t;
seekdb_runtime_memory_account *seekdb_runtime_memory_create(uint64_t byte_limit, uint64_t allocation_limit);
void *seekdb_runtime_memory_alloc(const seekdb_runtime_memory_account *, uint64_t size, uint32_t alignment);
int32_t seekdb_runtime_memory_free(const seekdb_runtime_memory_account *, void *, uint64_t size, uint32_t alignment);
int32_t seekdb_runtime_memory_usage(const seekdb_runtime_memory_account *, seekdb_runtime_memory_usage_t *);
void seekdb_runtime_memory_close(const seekdb_runtime_memory_account *);
void seekdb_runtime_memory_destroy(seekdb_runtime_memory_account *);
typedef struct seekdb_runtime_memory_buffer seekdb_runtime_memory_buffer;
/* Returned token exclusively owns its data and one account reference. No raw
 * memory_free is permitted on token data. Data/destroy accept a live token;
 * destroy requires all byte users ended and is allowed on any host thread. */
seekdb_runtime_memory_buffer *seekdb_runtime_memory_buffer_create(const seekdb_runtime_memory_account *, uint64_t, uint32_t);
void *seekdb_runtime_memory_buffer_data(const seekdb_runtime_memory_buffer *);
void seekdb_runtime_memory_buffer_destroy(seekdb_runtime_memory_buffer *);

/* Cursor-local input dependency graph, NOT a plugin/public ABI. The host has
 * exclusive serialized ownership; no Rust borrow spans child/plugin callbacks.
 * Up to 64 inputs/4096 edges, duplicate edges collapse; cycles/self edges fail.
 * Rows and parameter bindings have separate readiness. Reading/rescanning an
 * input invalidates its row and all downstream rows/bindings, but preserves its
 * own incoming bound environment. Binding requires current rows from ALL direct
 * sources and invalidates the target environment until successful completion.
 * Apply effect masks even on errors BEFORE further host work. A failed/invalid
 * operation poisons this state; only a successful whole-cursor reset reopens it.
 * create copies edges. Outputs are aligned/writable/disjoint. State destruction
 * requires no host callback in flight. No SQL, values, callbacks or row pointers
 * cross this bridge. This graph does not validate planner parameter ownership. */
typedef struct seekdb_runtime_input_state seekdb_runtime_input_state;
typedef struct seekdb_runtime_input_edge { uint32_t source, target; } seekdb_runtime_input_edge;
typedef struct seekdb_runtime_input_effect { uint64_t rows, bindings, ticket; } seekdb_runtime_input_effect;
enum seekdb_runtime_input_operation { SEEKDB_RUNTIME_INPUT_READ = 1, SEEKDB_RUNTIME_INPUT_RESCAN = 2, SEEKDB_RUNTIME_INPUT_BIND = 3 };
enum seekdb_runtime_input_outcome { SEEKDB_RUNTIME_INPUT_ROW = 1, SEEKDB_RUNTIME_INPUT_EOF = 2, SEEKDB_RUNTIME_INPUT_DONE = 3, SEEKDB_RUNTIME_INPUT_ERROR = 4 };
int32_t seekdb_runtime_input_state_create(uint32_t, const seekdb_runtime_input_edge *, uint32_t, seekdb_runtime_input_state **);
void seekdb_runtime_input_state_destroy(seekdb_runtime_input_state *);
int32_t seekdb_runtime_input_state_begin(seekdb_runtime_input_state *, uint32_t operation, uint32_t input, seekdb_runtime_input_effect *);
int32_t seekdb_runtime_input_state_finish(seekdb_runtime_input_state *, uint64_t ticket, uint32_t outcome, seekdb_runtime_input_effect *);
int32_t seekdb_runtime_input_state_reset(seekdb_runtime_input_state *, uint32_t reusable, seekdb_runtime_input_effect *);

/* Transaction-local catalog view journal; exclusive, non-reentrant host use.
 * NOT a database transaction or a capability exposed to native plugins.
 * transaction_id identifies the actual caller transaction. sequence is the
 * logical ObTxSEQ::get_seq(), not its packed representation; caller serializes
 * mutations on the root branch. Record a mark BEFORE mutating the paired view.
 * After successful data rollback, pass its resolved target: records >= target
 * are undone in reverse order. No duplicate SQL savepoint-name stack exists.
 * Sequences remain monotonic across rollback; equal barriers are permitted.
 * record transfers payload ownership only on OK; both callbacks CONSUME it.
 * Callbacks must not unwind, reenter or invoke arbitrary plugin code. Undo
 * returns an opaque host error (0 success); first failure poisons admission and
 * commit, remains sticky, and never prevents cleanup of older owned records.
 * finish(1) only after verified commit; finish(0) after verified abort. Neither
 * commits data nor publishes schemas. Destroy restores/discards PRIVATE views
 * only, including on unknown outcomes; this is not proof of database rollback.
 * host_error is required, writable, disjoint; a bridge error clears it except
 * poisoned commit returns the first host error. Null destroy is safe. */
typedef struct seekdb_runtime_query_transaction seekdb_runtime_query_transaction;
/* Single caller-owned operation, NOT the Extension installer or transaction
 * commit. The host binds the normal resolver/ACL/writer. See query_operation.rs
 * for the effect contract. No phase may commit, publish or start an independent
 * transaction. PREPARE may partially acquire state even on failure; CLOSE must
 * release it. CHECK_TRANSACTION runs after CLOSE. Failed cleanup/identity skips
 * data rollback and requires POISON. Private undo follows confirmed data undo.
 * POISON receives the unsafe cleanup status; other phases receive zero. It must
 * revoke the captured view and prevent commit without acting on a reused TX.
 * Callbacks/context are synchronous, exclusive, non-reentrant and non-unwinding.
 * No Rust journal borrow spans callbacks. Output must be aligned and disjoint.
 * OK is bridge success: inspect outcome and errors, never infer DB commit. */
enum seekdb_runtime_query_operation_phase {
  SEEKDB_RUNTIME_QUERY_PREFLIGHT = 1, SEEKDB_RUNTIME_QUERY_PREPARE = 2,
  SEEKDB_RUNTIME_QUERY_APPLY = 3, SEEKDB_RUNTIME_QUERY_CLOSE = 4,
  SEEKDB_RUNTIME_QUERY_CHECK_TRANSACTION = 5, SEEKDB_RUNTIME_QUERY_ROLLBACK_DATA = 6,
  SEEKDB_RUNTIME_QUERY_ROLLBACK_VIEW = 7, SEEKDB_RUNTIME_QUERY_POISON = 8
};
enum seekdb_runtime_query_operation_outcome {
  SEEKDB_RUNTIME_QUERY_NOT_STARTED = 0, SEEKDB_RUNTIME_QUERY_APPLIED = 1,
  SEEKDB_RUNTIME_QUERY_ROLLED_BACK = 2, SEEKDB_RUNTIME_QUERY_REQUIRES_ABORT = 3
};
typedef struct seekdb_runtime_query_operation_result {
  uint32_t outcome;
  uint32_t failed_phase;
  int32_t operation_error;
  int32_t close_error;
  int32_t identity_error;
  int32_t data_rollback_error;
  int32_t view_rollback_error;
  int32_t poison_error;
} seekdb_runtime_query_operation_result;
typedef int32_t (*seekdb_runtime_query_operation_step)(void *, uint32_t phase, int32_t cause);
int32_t seekdb_runtime_query_operation_run(void *, seekdb_runtime_query_operation_step,
    seekdb_runtime_query_operation_result *);
typedef int32_t (*seekdb_runtime_query_undo_fn)(void *);
typedef void (*seekdb_runtime_query_release_fn)(void *);
seekdb_runtime_query_transaction *seekdb_runtime_query_transaction_create(uint64_t transaction_id);
int32_t seekdb_runtime_query_transaction_record(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, uint64_t sequence, void *payload,
    seekdb_runtime_query_undo_fn undo, seekdb_runtime_query_release_fn release);
int32_t seekdb_runtime_query_transaction_rollback(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, uint64_t sequence, int32_t *host_error);
/* Nonzero host failure irreversibly denies further marks, writes, savepoint
 * rollback and commit. No undo/SQL/callbacks. Preserve first cause; only verified
 * abort or private destruction may clean remaining records. Host must revoke
 * view access too. Wrong transaction/finished handle is never modified. */
int32_t seekdb_runtime_query_transaction_fail(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, int32_t host_error);
int32_t seekdb_runtime_query_transaction_finish(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, uint32_t committed, int32_t *host_error);
/* View-only fast path, rejects surviving schema writes. Call before submitting
 * commit; seals admission/rollback. Only a sealed, unpoisoned journal accepts
 * finish(1). Schema writers must use begin/record-end-sign/complete below. */
int32_t seekdb_runtime_query_transaction_prepare_commit(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, int32_t *host_error);
void seekdb_runtime_query_transaction_destroy(seekdb_runtime_query_transaction *);
/* Successful catalog writes only; same root barrier and shared record budget as
 * view marks. Failed admission requires host data rollback. Not publication. */
int32_t seekdb_runtime_query_transaction_record_schema_version(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, uint64_t sequence, uint64_t schema_version);
int32_t seekdb_runtime_query_transaction_schema_state(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, uint64_t *max_schema_version, uint64_t *operation_count, int32_t *host_error);
/* Host-only routine cache requests, not SQL/catalog authority. Record AFTER a
 * successful schema write at the same admitted root barrier. Uses the shared
 * journal budget. Data rollback must precede journal rollback; no eviction is
 * performed by rollback, finish, or destroy. Requests survive finish(1).
 * Count is readable before finish and after known commit (not abort/poison).
 * Peek/ack require known commit. Peek returns the oldest request, all zero if
 * empty, and does not consume it. Release the FFI borrow before host scheduling.
 * Ack ONLY after lifetime-safe queue handoff or successful eviction, using the
 * exact oldest ticket. Duplicate objects have distinct tickets; failed/repeated
 * acknowledgements consume nothing. Host MUST retain an unacknowledged owner;
 * destroy discards remaining private requests and cannot provide crash recovery.
 * All calls require exclusive access and writable, disjoint output regions. */
typedef struct seekdb_runtime_routine_invalidation {
  uint64_t ticket;
  uint64_t database;
  uint64_t routine;
} seekdb_runtime_routine_invalidation;
int32_t seekdb_runtime_query_transaction_record_invalidation(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, uint64_t sequence, uint64_t database, uint64_t routine);
int32_t seekdb_runtime_query_transaction_invalidation_count(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, uint64_t *count, int32_t *host_error);
int32_t seekdb_runtime_query_transaction_peek_invalidation(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, seekdb_runtime_routine_invalidation *output);
int32_t seekdb_runtime_query_transaction_ack_invalidation(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, uint64_t ticket);
/* One bounded queue per volatile plan cache, with a single serialized consumer.
 * Reserve scalar snapshots while Preparing, BEFORE database commit. finish(1)
 * hands off without allocation; finish(0) releases the reservation. Destruction
 * of an unresolved reserved journal conservatively requests eviction (unknown
 * may have committed), NEVER schema publication or a fabricated data outcome.
 * Peek copies a batch token, required schema version and oldest request; zero
 * means no ready work. Wait for refreshed schema >= required_version BEFORE
 * eviction, so old schemas cannot recreate entries after the one-shot eviction.
 * complete(success=1) only after actual eviction; 0 retains it and rotates to
 * another batch. No SQL/callback runs under the Rust queue lock. Close(0)
 * rejects new reservations but preserves accepted ones for completion/draining.
 * Close(1)/destroy ONLY after the associated cache is inaccessible and its worker
 * has stopped; then obsolete volatile requests can be discarded. Reservations
 * keep internal state alive independently of the handle. Concurrent producer
 * borrows allowed, but destroy requires exclusive handle ownership. */
typedef struct seekdb_runtime_invalidation_queue seekdb_runtime_invalidation_queue;
seekdb_runtime_invalidation_queue *seekdb_runtime_invalidation_queue_create(uint32_t max_batches, uint32_t max_requests);
int32_t seekdb_runtime_query_transaction_reserve_invalidations(seekdb_runtime_invalidation_queue *,
    seekdb_runtime_query_transaction *, uint64_t transaction_id);
int32_t seekdb_runtime_invalidation_queue_peek(seekdb_runtime_invalidation_queue *,
    uint64_t *batch_token, uint64_t *required_version, seekdb_runtime_routine_invalidation *output);
int32_t seekdb_runtime_invalidation_queue_complete(seekdb_runtime_invalidation_queue *,
    uint64_t batch_token, uint64_t ticket, uint32_t success);
int32_t seekdb_runtime_invalidation_queue_close(seekdb_runtime_invalidation_queue *, uint32_t retire);
void seekdb_runtime_invalidation_queue_destroy(seekdb_runtime_invalidation_queue *);
/* Host-only finalization; not plugin catalog authority. Begin freezes ordinary
 * mutation and savepoint rollback, returning surviving version/count. No Rust
 * borrow/callback spans host SQL. One allocation-free final end-sign slot is
 * separate from the bounded ordinary journal, requires a strictly newer version
 * and existing schema writes. It is included in schema_state's max/count.
 * Complete is one-shot: host_result covers ALL host preparation and transport
 * cleanup, NOT durable commit. Success requires a matching recorded end-sign
 * (zero for no schema writes); any failed completion permanently denies commit.
 * The host owns transaction identity/locks/epoch/MDS/watermark/publication.
 * After begin, only full abort/discard is legal on failure. */
int32_t seekdb_runtime_query_transaction_begin_prepare(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, uint64_t *max_schema_version, uint64_t *operation_count, int32_t *host_error);
int32_t seekdb_runtime_query_transaction_record_end_sign(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, uint64_t schema_version);
/* Read-only admission for each host finalization step, including after end-sign;
 * identity/Preparing/first-error checks. Does not seal or confer DDL authority. */
int32_t seekdb_runtime_query_transaction_check_preparing(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, int32_t *host_error);
/* Host records a captured epoch only AFTER acquiring its DDL lock, BEFORE any
 * schema writes. Uses a fixed slot. Rollback to/before sequence clears it;
 * begin_prepare rejects surviving schema writes without prior admission. */
int32_t seekdb_runtime_query_transaction_admit_ddl(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, uint64_t sequence, uint64_t epoch);
int32_t seekdb_runtime_query_transaction_ddl_admission(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, uint64_t *epoch, uint64_t *sequence, int32_t *host_error);
int32_t seekdb_runtime_query_transaction_check_ddl_write(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, uint64_t sequence, int32_t *host_error);
int32_t seekdb_runtime_query_transaction_complete_prepare(seekdb_runtime_query_transaction *,
    uint64_t transaction_id, uint64_t prepared_version, int32_t host_result, int32_t *host_error);

/* Preordered synchronous around hooks. Payloads and code are pinned by the
 * host; no callback/continuation may unwind, escape or cross threads. A hook
 * can veto before next; success requires exactly one next. Downstream errors
 * cannot be suppressed. <=64 entries bounds continuation stack depth.
 * operation_status is opaque to Rust (zero success); protocol_error is a
 * negative host error. Non-OK bridge status leaves output=protocol_error. */
typedef int32_t (*seekdb_runtime_hook_next_fn)(void *);
/* Linux ELF64 LE host build-ID match (1..64 bytes), cached only on success.
 * Linked identity only, no signature/integrity guarantee. Unsupported/missing
 * host notes return STATE_MISMATCH; malformed input returns INVALID. */
int32_t seekdb_runtime_match_host_build_id(const uint8_t *expected, uint32_t length);
typedef int32_t (*seekdb_runtime_hook_invoke_fn)(void *, seekdb_runtime_hook_next_fn, void *);
typedef struct seekdb_runtime_hook {
  void *context;
  seekdb_runtime_hook_invoke_fn invoke;
} seekdb_runtime_hook_t;
int32_t seekdb_runtime_hook_run(const seekdb_runtime_hook_t *hooks, uint32_t count,
    seekdb_runtime_hook_next_fn leaf, void *context, int32_t protocol_error, int32_t *operation_status);

/* Mode-aware host-only bridge, not a public plugin plan API. Retains the v1
 * lifetime, error, <=64-entry and output rules. The whole fixed-stride table is
 * checked before callbacks; struct_size must equal sizeof the v2 entry.
 * OBSERVE: host calls before/after and advances automatically; no status/next.
 * AROUND: success requires one next; an error before next vetoes the operation.
 * REPLACE: may succeed without next, skipping later entries and the leaf. If it
 * calls next, downstream failure still cannot be suppressed, nor next repeated.
 * validate_result is mandatory and runs once after the complete successful
 * chain, before the host publishes its state. Its exact error is preserved.
 * AFTER observes the nested result, not final validation or transaction commit.
 * Host must permit replacement at the selected hook point and pin all code and
 * state through validation. OBSERVE must not mutate operation state: native
 * callbacks are trusted, not sandboxed. No callback may unwind across Rust. */
enum seekdb_runtime_hook_mode {
  SEEKDB_RUNTIME_HOOK_OBSERVE = 0, SEEKDB_RUNTIME_HOOK_AROUND = 1,
  SEEKDB_RUNTIME_HOOK_REPLACE = 2
};
enum seekdb_runtime_hook_phase {
  SEEKDB_RUNTIME_HOOK_BEFORE = 0, SEEKDB_RUNTIME_HOOK_AFTER = 1
};
typedef void (*seekdb_runtime_hook_observe_fn)(void *, uint32_t phase, int32_t nested_result);
typedef struct seekdb_runtime_hook_v2 {
  uint32_t struct_size;
  uint32_t mode;
  void *context;
  seekdb_runtime_hook_invoke_fn invoke;
  seekdb_runtime_hook_observe_fn observe;
  uint64_t reserved[4];
} seekdb_runtime_hook_v2_t;
int32_t seekdb_runtime_hook_run_v2(const seekdb_runtime_hook_v2_t *hooks, uint32_t count,
    seekdb_runtime_hook_next_fn leaf, void *context, seekdb_runtime_hook_next_fn validate_result,
    int32_t protocol_error, int32_t *operation_status);

/* Owned control + install-SQL snapshot. Host-only; does not execute SQL, verify
 * signatures, grant trust, or load native modules. Root is administrator-owned
 * and must not race package replacement. Read inputs are bounded UTF-8 spans;
 * outputs/diagnostics are writable and disjoint. Failure clears the handle.
 * Text views remain valid until destroy; destroy requires exclusive ownership.
 * Control grammar and layout are documented in plugin-extension-package.md. */
typedef struct seekdb_runtime_package seekdb_runtime_package;
enum seekdb_runtime_package_field {
  SEEKDB_RUNTIME_PACKAGE_NAME = 1, SEEKDB_RUNTIME_PACKAGE_VERSION = 2,
  SEEKDB_RUNTIME_PACKAGE_MODULE = 3, SEEKDB_RUNTIME_PACKAGE_SCHEMA = 4,
  /* Legacy SQL field rejects multi-script plans, never returns a partial plan. */
  SEEKDB_RUNTIME_PACKAGE_SQL = 5, SEEKDB_RUNTIME_PACKAGE_DEPENDENCY = 6,
  SEEKDB_RUNTIME_PACKAGE_SCRIPT_SQL = 7, SEEKDB_RUNTIME_PACKAGE_SCRIPT_FROM = 8,
  SEEKDB_RUNTIME_PACKAGE_SCRIPT_TO = 9,
  SEEKDB_RUNTIME_PACKAGE_PREREQUISITE = 11,
  /* Empty for installation, otherwise the expected installed version. */
  SEEKDB_RUNTIME_PACKAGE_FROM_VERSION = 10
};
int32_t seekdb_runtime_package_read(const uint8_t *root, uint32_t root_length,
    const uint8_t *name, uint32_t name_length, const uint8_t *version, uint32_t version_length,
    seekdb_runtime_package **output, char *error, uint32_t error_capacity);
/* Updates never substitute an installation script. FROM must be nonempty.
 * Empty target selects control default. Same-version success has no scripts;
 * callers still validate the durable instance/version/privileges before no-op.
 * This source reader neither inspects nor changes the installed catalog. */
int32_t seekdb_runtime_package_read_update(const uint8_t *root, uint32_t root_length,
    const uint8_t *name, uint32_t name_length, const uint8_t *from, uint32_t from_length,
    const uint8_t *version, uint32_t version_length,
    seekdb_runtime_package **output, char *error, uint32_t error_capacity);
void seekdb_runtime_package_destroy(seekdb_runtime_package *package);
/* Host-owned, already selected in-memory source. Not a native plugin authority
 * surface: it does not bind a session, execute DDL or confer installation rights.
 * All spans/arrays are borrowed only during this synchronous call. A complete
 * validated, deeply owned snapshot is returned through the usual package API.
 * from is empty for install; an update's nonempty from and ordered script chain
 * must end at version. Same-version updates require zero scripts. No filesystem
 * access, version-path search, SQL concatenation or variable interpolation. */
typedef struct seekdb_runtime_package_input_text {
  const uint8_t *data;
  uint32_t length;
} seekdb_runtime_package_input_text;
typedef struct seekdb_runtime_package_input_script {
  seekdb_runtime_package_input_text from_version;
  seekdb_runtime_package_input_text to_version;
  seekdb_runtime_package_input_text sql;
} seekdb_runtime_package_input_script;
typedef struct seekdb_runtime_package_input_source {
  uint32_t struct_size;
  uint32_t relocatable;
  seekdb_runtime_package_input_text name;
  seekdb_runtime_package_input_text from_version;
  seekdb_runtime_package_input_text version;
  seekdb_runtime_package_input_text native_module;
  seekdb_runtime_package_input_text schema;
  const seekdb_runtime_package_input_text *dependencies;
  uint32_t dependency_count;
  const seekdb_runtime_package_input_script *scripts;
  uint32_t script_count;
  uint32_t native_install; /* 0=SQL base; 1=explicit native installation source. */
  const seekdb_runtime_package_input_text *prerequisites;
  uint32_t prerequisite_count;
} seekdb_runtime_package_input_source;
int32_t seekdb_runtime_package_from_source(const seekdb_runtime_package_input_source *source,
    seekdb_runtime_package **output, char *error, uint32_t error_capacity);
/* Additive host-only entrance; 1 means superuser=false, never elevated privileges.
 * Existing source layout and default entrance remain unchanged. */
int32_t seekdb_runtime_package_from_source_with_policy(const seekdb_runtime_package_input_source *source,
    uint32_t invoker_only, seekdb_runtime_package **output, char *error, uint32_t error_capacity);
int32_t seekdb_runtime_package_text(const seekdb_runtime_package *package,
    uint32_t field, uint32_t index, const uint8_t **data, uint32_t *length);
int32_t seekdb_runtime_package_info(const seekdb_runtime_package *package,
    uint32_t *dependency_count, uint32_t *relocatable);
int32_t seekdb_runtime_package_native_install(const seekdb_runtime_package *package, uint32_t *native_install);
int32_t seekdb_runtime_package_invoker_only(const seekdb_runtime_package *package, uint32_t *invoker_only);
int32_t seekdb_runtime_package_prerequisite_count(const seekdb_runtime_package *package, uint32_t *count);
/* Pure validation, no SQL/lookup: <=64 unique non-self package names. */
int32_t seekdb_runtime_extension_requires_validate(const uint8_t *name, uint32_t name_length,
    const seekdb_runtime_package_input_text *dependencies, uint32_t dependency_count);
/* A plan has <=1024 separately owned scripts, with <=4 MiB total SQL. Parse
 * each file independently and in order; do not concatenate SQL text. FROM is
 * empty for the base script and matches the previous TO for every update.
 * For an update plan the first FROM equals PACKAGE_FROM_VERSION. Zero scripts
 * means a same-version update or a fresh native-source installation. A fresh
 * native source owns no SQL scripts and requires the installation callback.
 * Empty SQL is valid only for update edges. */
int32_t seekdb_runtime_package_script_count(const seekdb_runtime_package *package,
    uint32_t *count);

/* Native mapping operations require exclusive host ownership. They must run
 * outside registry/loader locks: OS loading executes DSO constructors/destructors.
 * open accepts ONLY a host-verified, pinned artifact path (non-NUL byte span).
 * Diagnostic buffers are required, writable, disjoint, and always terminated.
 * A failed open clears output; a failed entry lookup clears the function.
 * A successful close consumes the handle; failure preserves caller ownership.
 * No implicit unload occurs. publish prevents load rollback from unloading a
 * published module. Terminal close requires the process-exit coordinator to
 * have drained callbacks, buffers, static borrows and plugin threads first.
 * These are internal host APIs, not capabilities exposed to plugins. */
typedef struct seekdb_runtime_native_module seekdb_runtime_native_module;
typedef const void *(*seekdb_runtime_native_entry_fn)(void);
enum seekdb_runtime_native_status { SEEKDB_RUNTIME_IO_ERROR = 10 };
enum seekdb_runtime_native_close_phase {
  SEEKDB_RUNTIME_ABORT_LOAD = 0,
  SEEKDB_RUNTIME_PROCESS_EXIT = 1
};
int32_t seekdb_runtime_native_open(const uint8_t *path, uint32_t length,
    seekdb_runtime_native_module **output, char *error, uint32_t error_capacity);
int32_t seekdb_runtime_native_entry(seekdb_runtime_native_module *module,
    seekdb_runtime_native_entry_fn *output, char *error, uint32_t error_capacity);
int32_t seekdb_runtime_native_publish(seekdb_runtime_native_module *module);
int32_t seekdb_runtime_native_close(seekdb_runtime_native_module *module,
    uint32_t phase, char *error, uint32_t error_capacity);

/* Journal calls (including get/stats/destroy) require external serialization.
 * The host mutex supplies it. Release callbacks destroy normalized host data,
 * must not unwind/reenter, and MUST NOT run arbitrary plugin callbacks.
 * Token addresses stay reserved until journal destruction. At most 4096
 * transactions may be open at once; up to 65536 tokens may be issued during
 * one activation, including ended/empty transactions. */
typedef struct seekdb_runtime_registration seekdb_runtime_registration;
typedef struct seekdb_runtime_registration_token seekdb_runtime_registration_token;
typedef void (*seekdb_runtime_registration_release)(void *payload);
enum seekdb_runtime_registration_family {
  SEEKDB_RUNTIME_SERVICE = 1,
  SEEKDB_RUNTIME_EXTENSION = 2
};
enum seekdb_runtime_registration_status {
  SEEKDB_RUNTIME_NO_MEMORY = 5,
  SEEKDB_RUNTIME_CONFLICT = 6,
  SEEKDB_RUNTIME_LIMIT = 7
};
typedef struct seekdb_runtime_registration_stats {
  uint32_t committed_services;
  uint32_t committed_extensions;
  uint32_t open_transactions;
  uint32_t total_services;
  uint32_t total_extensions;
  uint32_t issued_transactions;
  uint64_t extension_bytes;
} seekdb_runtime_registration_stats_t;

seekdb_runtime_registration *seekdb_runtime_registration_create(void);
void seekdb_runtime_registration_destroy(seekdb_runtime_registration *journal);
int32_t seekdb_runtime_registration_open(seekdb_runtime_registration *journal);
int32_t seekdb_runtime_registration_seal(seekdb_runtime_registration *journal);
int32_t seekdb_runtime_registration_clear(seekdb_runtime_registration *journal);
int32_t seekdb_runtime_registration_begin(seekdb_runtime_registration *journal,
    seekdb_runtime_registration_token **token);
int32_t seekdb_runtime_registration_check(seekdb_runtime_registration *journal,
    const seekdb_runtime_registration_token *token);
int32_t seekdb_runtime_registration_commit(seekdb_runtime_registration *journal,
    const seekdb_runtime_registration_token *token);
int32_t seekdb_runtime_registration_abort(seekdb_runtime_registration *journal,
    const seekdb_runtime_registration_token *token);
/* Payload ownership transfers only on OK. Keys are copied; release runs once
 * on abort/clear/destroy. Otherwise the caller retains ownership. */
int32_t seekdb_runtime_registration_stage(seekdb_runtime_registration *journal,
    const seekdb_runtime_registration_token *token, uint32_t family, uint32_t major,
    const uint8_t *key, uint32_t key_length, uint64_t descriptor_bytes,
    void *payload, seekdb_runtime_registration_release release);
int32_t seekdb_runtime_registration_stats(seekdb_runtime_registration *journal,
    seekdb_runtime_registration_stats_t *stats);
/* Borrow a normalized committed object; invalid after clear/destroy. */
int32_t seekdb_runtime_registration_get(seekdb_runtime_registration *journal,
    uint32_t index, uint32_t *family, const void **payload);

typedef struct seekdb_runtime_generation seekdb_runtime_generation;
/* Database installation metadata, NOT runtime generation/descriptor identity.
 * Namespace canonicalization, privileges and schema object existence are the
 * host's responsibility. Pure SQL packages have an empty native module ID.
 * Validation retains no pointers and starts no transaction. */
typedef struct seekdb_runtime_extension_member {
  uint32_t object_class;
  uint32_t reserved;
  uint64_t object_id;
} seekdb_runtime_extension_member_t;
enum seekdb_runtime_extension_install_phase {
  SEEKDB_RUNTIME_INSTALL_PREFLIGHT = 1,
  SEEKDB_RUNTIME_INSTALL_BEGIN = 2,
  SEEKDB_RUNTIME_INSTALL_APPLY = 3,
  SEEKDB_RUNTIME_INSTALL_RECORD = 4,
  SEEKDB_RUNTIME_INSTALL_COMMIT = 5,
  SEEKDB_RUNTIME_INSTALL_ROLLBACK = 6
};
enum seekdb_runtime_extension_install_outcome {
  SEEKDB_RUNTIME_INSTALL_NOT_STARTED = 0,
  SEEKDB_RUNTIME_INSTALL_ROLLED_BACK = 1,
  SEEKDB_RUNTIME_INSTALL_COMMITTED = 2,
  SEEKDB_RUNTIME_INSTALL_COMMIT_UNKNOWN = 3,
  SEEKDB_RUNTIME_INSTALL_ROLLBACK_UNKNOWN = 4
};
typedef struct seekdb_runtime_extension_install_result {
  uint32_t outcome;
  uint32_t failed_phase;
  int32_t operation_status;
  int32_t rollback_status;
  uint64_t extension_id;
} seekdb_runtime_extension_install_result_t;
/* One owned catalog transaction, not an arbitrary caller transaction. Host
 * steps are synchronous/non-unwinding; preflight has no writes; apply/record
 * must use exactly the begun transaction. Record supplies a provisional ID.
 * Failed BEGIN also requests cleanup. Commit failure is UNKNOWN, no automatic
 * rollback/retry. Output is disjoint from live callback context. Return code
 * reports bridge validation; operation/cleanup errors are preserved in output. */
int32_t seekdb_runtime_extension_install_run(void *context,
    int32_t (*step)(void *context, uint32_t phase, uint64_t *extension_id),
    seekdb_runtime_extension_install_result_t *output);
/* Drop uses the same outcome/result representation. Only LOCK_SNAPSHOT sets
 * identity, after locking instance then members and completing all admission
 * checks. DETACH removes member protection ONLY in this transaction; APPLY
 * drops schema objects; RECORD removes the instance. All three roll back as
 * one unit. No module availability requirement, automatic retry or unload. */
enum seekdb_runtime_extension_drop_phase {
  SEEKDB_RUNTIME_DROP_PREFLIGHT = 1, SEEKDB_RUNTIME_DROP_BEGIN = 2,
  SEEKDB_RUNTIME_DROP_LOCK_SNAPSHOT = 3, SEEKDB_RUNTIME_DROP_DETACH = 4,
  SEEKDB_RUNTIME_DROP_APPLY = 5, SEEKDB_RUNTIME_DROP_RECORD = 6,
  SEEKDB_RUNTIME_DROP_COMMIT = 7, SEEKDB_RUNTIME_DROP_ROLLBACK = 8
};
typedef seekdb_runtime_extension_install_result_t seekdb_runtime_extension_drop_result_t;
int32_t seekdb_runtime_extension_drop_run(void *context,
    int32_t (*step)(void *context, uint32_t phase, uint64_t *extension_id),
    seekdb_runtime_extension_drop_result_t *output);
int32_t seekdb_runtime_extension_drop_validate(uint64_t tenant_id, uint64_t database_id,
    uint64_t expected_extension_id, const uint8_t *name, uint32_t name_length);
/* UPDATE shares DROP's locked-instance phase protocol. Expected ID is mandatory,
 * and LOCK must also compare the expected version and authorize the caller.
 * No-op still locks/admit/commits but skips DETACH/APPLY/RECORD. Empty SQL does
 * NOT imply no-op when source and target versions differ. */
enum seekdb_runtime_extension_update_phase {
  SEEKDB_RUNTIME_UPDATE_PREFLIGHT = 1, SEEKDB_RUNTIME_UPDATE_BEGIN = 2,
  SEEKDB_RUNTIME_UPDATE_LOCK_SNAPSHOT = 3, SEEKDB_RUNTIME_UPDATE_DETACH = 4,
  SEEKDB_RUNTIME_UPDATE_APPLY = 5, SEEKDB_RUNTIME_UPDATE_RECORD = 6,
  SEEKDB_RUNTIME_UPDATE_COMMIT = 7, SEEKDB_RUNTIME_UPDATE_ROLLBACK = 8
};
typedef seekdb_runtime_extension_install_result_t seekdb_runtime_extension_update_result_t;
int32_t seekdb_runtime_extension_update_validate(uint64_t tenant_id, uint64_t database_id,
    uint64_t expected_extension_id, const uint8_t *name, uint32_t name_length,
    const uint8_t *from, uint32_t from_length, const uint8_t *to, uint32_t to_length);
int32_t seekdb_runtime_extension_update_run(void *context,
    int32_t (*step)(void *context, uint32_t phase, uint64_t *extension_id),
    uint64_t expected_extension_id, uint32_t no_op, seekdb_runtime_extension_update_result_t *output);
int32_t seekdb_runtime_extension_install_validate(
    uint64_t tenant_id, uint64_t database_id, uint64_t owner_id,
    const uint8_t *name, uint32_t name_length,
    const uint8_t *version, uint32_t version_length,
    const uint8_t *module, uint32_t module_length,
    const seekdb_runtime_extension_member_t *members, uint32_t member_count);
/* Runtime SQL object identity index, NOT durable Extension installation.
 * Each private image has one index owned by Rust; clones share immutable host
 * payloads. Published images allow concurrent reads/clone; mutations and
 * destruction require exclusive ownership of that image. Payload callbacks
 * must be thread-safe, non-unwinding, non-reentrant host-data destruction only.
 * insert transfers ownership ONLY on OK. get/find borrow until image mutation
 * or destruction; returned pointers never constitute an execution lease.
 * OOM in Rust Arc allocation retains the host abort policy; Vec reservations
 * and handle allocations report failure. Enumeration is sorted by kind/id. */
typedef struct seekdb_runtime_object_catalog seekdb_runtime_object_catalog;
seekdb_runtime_object_catalog *seekdb_runtime_objects_create(void);
seekdb_runtime_object_catalog *seekdb_runtime_objects_clone(const seekdb_runtime_object_catalog *catalog);
void seekdb_runtime_objects_destroy(seekdb_runtime_object_catalog *catalog);
int32_t seekdb_runtime_objects_insert(seekdb_runtime_object_catalog *catalog,
    uint32_t kind, const uint8_t *id, uint32_t length, void *payload,
    seekdb_runtime_registration_release release);
const void *seekdb_runtime_objects_at(const seekdb_runtime_object_catalog *catalog, uint32_t index);
const void *seekdb_runtime_objects_find(const seekdb_runtime_object_catalog *catalog,
    uint32_t kind, const uint8_t *id, uint32_t length);
uint32_t seekdb_runtime_objects_count(const seekdb_runtime_object_catalog *catalog);
int32_t seekdb_runtime_objects_remove(seekdb_runtime_object_catalog *catalog, uint32_t index);
/* Linear bulk removal. Predicate is host-only, synchronous, non-unwinding,
 * non-reentrant, and may only read payload metadata; nonzero removes an entry. */
int32_t seekdb_runtime_objects_remove_if(seekdb_runtime_object_catalog *catalog,
    const void *context, uint8_t (*predicate)(const void *context, const void *payload));
/* Dependency nodes are ordinals from ONE caller snapshot, with names/versions/
 * generations/visibility already resolved by the host. Edges are provider ->
 * consumer; duplicates count once. Output chooses the lowest ready ordinal,
 * so sorted host IDs preserve deterministic ordering. No output is written on
 * failure. blocked is UINT32_MAX except on a cycle: then it is a blocked node,
 * which may be downstream of, rather than part of, the actual cycle.
 * ignore_self_edges=1 admits self-service dependencies for module activation;
 * 0 treats a self edge as a cycle. No locks, callbacks or retained pointers. */
#define SEEKDB_RUNTIME_DEPENDENCY_MAX_NODES 65536u
#define SEEKDB_RUNTIME_DEPENDENCY_MAX_EDGES 1048576u
enum seekdb_runtime_dependency_status { SEEKDB_RUNTIME_DEPENDENCY_CYCLE = 11 };
typedef struct seekdb_runtime_dependency_edge {
  uint32_t provider;
  uint32_t consumer;
} seekdb_runtime_dependency_edge_t;
/* Stable SQL Extension identities. Host serializes graph updates and holds
 * provider/transaction locks until commit. Replaces all providers of consumer
 * in the supplied graph and rejects cycles in the resulting graph. */
typedef struct seekdb_runtime_extension_dependency {
  uint64_t provider;
  uint64_t consumer;
} seekdb_runtime_extension_dependency_t;
int32_t seekdb_runtime_extension_dependency_replace_validate(uint64_t consumer,
    const seekdb_runtime_extension_dependency_t *existing, uint32_t count,
    const uint64_t *providers, uint32_t provider_count);
int32_t seekdb_runtime_dependency_plan(uint32_t node_count,
    const seekdb_runtime_dependency_edge_t *edges, uint32_t edge_count,
    uint8_t ignore_self_edges, uint32_t *order, uint32_t order_capacity,
    uint32_t *blocked);
/* SQL resolution borrows one immutable host snapshot. Normalized candidates
 * are already filtered by kind/name/namespace. Text is a byte span (no NUL);
 * only argument {NULL, 0} represents an unknown type during name probing.
 * No pointers are retained and no callbacks are invoked. The selected index
 * is UINT32_MAX on failure. check_arity is 0 for type lookup, 1 for functions.
 */
typedef struct seekdb_runtime_text {
  const uint8_t *data;
  uint32_t length;
} seekdb_runtime_text_t;
typedef struct seekdb_runtime_sql_candidate {
  seekdb_runtime_text_t object_id;
  const seekdb_runtime_text_t *signature;
  uint32_t signature_count;
  uint32_t minimum_arity;
  uint32_t maximum_arity;
  uint32_t reserved;
} seekdb_runtime_sql_candidate_t;
typedef struct seekdb_runtime_sql_cast {
  seekdb_runtime_text_t source;
  seekdb_runtime_text_t target;
  uint32_t context;
  uint32_t cost;
} seekdb_runtime_sql_cast_t;
enum seekdb_runtime_resolution_status {
  SEEKDB_RUNTIME_NOT_FOUND = 8,
  SEEKDB_RUNTIME_AMBIGUOUS = 9
};
int32_t seekdb_runtime_resolve_sql(
    const seekdb_runtime_sql_candidate_t *candidates, uint32_t candidate_count,
    const seekdb_runtime_sql_cast_t *casts, uint32_t cast_count,
    const seekdb_runtime_text_t *arguments, uint32_t argument_count,
    uint8_t check_arity, uint32_t *selected);
/* Direct cast selection: declared context >= requested context (1 explicit,
 * 2 assignment, 3 implicit). Minimum cost wins; a minimum-cost tie is ambiguous.
 * Source/target must be known identifiers. No implicit identity or NULL cast is
 * synthesized. Same borrowed-memory/output-failure contract as resolve_sql. */
int32_t seekdb_runtime_resolve_cast(
    const seekdb_runtime_sql_cast_t *casts, uint32_t cast_count,
    seekdb_runtime_text_t source, seekdb_runtime_text_t target,
    uint32_t requested_context, uint32_t *selected);
/* Common logical type for already-resolved expression branches. Candidates are
 * known input identities only; duplicate identities/unknown NULLs do not vote.
 * Direct IMPLICIT edges contribute 1 + cost per distinct non-identity source.
 * Minimum total cost wins; tied targets or cheapest required casts are ambiguous.
 * Empty/all-unknown inputs return NOT_FOUND for the SQL caller's native default.
 * selected is the first index for the chosen identity, UINT32_MAX on failure.
 * No category/typmod preference, new supertype, multi-hop or callback execution.
 * Borrowed input rules match resolve_sql; this call uses bounded fallible scratch
 * (1024 arguments, 4096 casts) and may return SEEKDB_RUNTIME_NO_MEMORY. */
int32_t seekdb_runtime_resolve_common_type(
    const seekdb_runtime_text_t *arguments, uint32_t argument_count,
    const seekdb_runtime_sql_cast_t *casts, uint32_t cast_count, uint32_t *selected);

enum seekdb_runtime_status {
  SEEKDB_RUNTIME_OK = 0,
  SEEKDB_RUNTIME_INVALID = 1,
  SEEKDB_RUNTIME_STATE_MISMATCH = 2,
  SEEKDB_RUNTIME_BUSY = 3,
  SEEKDB_RUNTIME_TIMEOUT = 4
};
enum seekdb_runtime_state {
  SEEKDB_RUNTIME_DISCOVERED = 0,
  SEEKDB_RUNTIME_VALIDATED = 1,
  SEEKDB_RUNTIME_LOADED = 2,
  SEEKDB_RUNTIME_INITIALIZING = 3,
  SEEKDB_RUNTIME_ACTIVE = 4,
  SEEKDB_RUNTIME_QUIESCING = 5,
  SEEKDB_RUNTIME_STOPPED = 6,
  SEEKDB_RUNTIME_FAILED = 7,
  SEEKDB_RUNTIME_BLOCKED = 8
};

seekdb_runtime_generation *seekdb_runtime_generation_create(void);
void seekdb_runtime_generation_destroy(seekdb_runtime_generation *generation);
uint8_t seekdb_runtime_generation_state(const seekdb_runtime_generation *generation);
int64_t seekdb_runtime_generation_leases(const seekdb_runtime_generation *generation);
int32_t seekdb_runtime_generation_transition(const seekdb_runtime_generation *generation, uint8_t next);
int32_t seekdb_runtime_generation_reserve(const seekdb_runtime_generation *generation);
int32_t seekdb_runtime_generation_abort(const seekdb_runtime_generation *generation);
int32_t seekdb_runtime_generation_promote(const seekdb_runtime_generation *generation);
uint8_t seekdb_runtime_generation_acquire(const seekdb_runtime_generation *generation);
int32_t seekdb_runtime_generation_release(const seekdb_runtime_generation *generation);
int32_t seekdb_runtime_generation_quiesce(const seekdb_runtime_generation *generation);
int32_t seekdb_runtime_generation_drain(const seekdb_runtime_generation *generation, int64_t timeout_us);
/* Only the process-exit adapter may use this, with terminal stop authority. */
int32_t seekdb_runtime_generation_terminal_stop(const seekdb_runtime_generation *generation);

#ifdef __cplusplus
}
#endif
#endif
