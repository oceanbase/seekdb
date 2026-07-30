/*
 * Copyright (c) 2026 seekdb contributors.
 *
 * Stable C ABI between seekdb and dynamically loaded plugins.
 *
 * This header is deliberately usable as both C99 and C++11.  The boundary
 * described here is a C ABI, even when either side is implemented in C++.
 * Only the entry point declared at the end of this file is to be exported by
 * a plugin.
 *
 * ABI RULES (normative):
 *
 *  - Values crossing this boundary MUST be the fixed-width POD values,
 *    pointers, and opaque handles declared in this file.  C++ standard-library
 *    types (including std::string, containers, smart pointers, and function
 *    objects) MUST NOT cross the boundary.
 *  - Exceptions MUST NOT cross the boundary.  A C++ implementation MUST catch
 *    every exception before returning from an ABI callback and translate it to
 *    seekdb_plugin_status_t.
 *  - A plugin MUST NOT expose, retain, cast, or dereference pointers to seekdb
 *    core implementation objects.  Likewise, the host MUST treat plugin
 *    handles as opaque.  Service pointers may point only to documented C ABI
 *    function tables whose first member is struct_size.
 *  - Memory MUST be released by the component that allocated it, unless the
 *    allocation was made with host_api->alloc; such memory MUST be released
 *    with the matching host_api->free callback.
 *  - Unless a member explicitly says otherwise, pointer data is borrowed and
 *    remains valid only for the duration of the call in which it is supplied.
 *    Manifest data returned by seekdb_plugin_entry_v1 is the exception: it MUST
 *    remain valid and immutable until the plugin library is unloaded.
 *    A successfully published static or dynamically registered service is
 *    another exception: its function table and all immutable plugin-owned data
 *    recursively reachable through that table MUST remain valid and immutable
 *    until the service has been unpublished, every lease that can reference it
 *    has drained, and plugin deinit has returned.  Publication does not transfer
 *    allocation ownership to the host.
 *  - ABI v1 defines logical quiesce/stop only.  A host may keep the library
 *    mapped until process exit; a plugin MUST NOT rely on dlclose or static
 *    destructors for correctness or durable state.
 *  - Every extensible structure starts with struct_size.  A producer MUST set
 *    it to sizeof(the structure it compiled against) and zero all reserved
 *    members.  A consumer MUST inspect struct_size before reading a member,
 *    MUST ignore unknown trailing members, and MUST accept a larger structure
 *    when its ABI major is supported.  New fields may only be appended; fields
 *    MUST NOT be reordered, removed, or change meaning within an ABI major.
 */

#ifndef SEEKDB_PLUGIN_SEEKDB_PLUGIN_ABI_H_
#define SEEKDB_PLUGIN_SEEKDB_PLUGIN_ABI_H_

#include <stddef.h>
#include <stdint.h>

#define SEEKDB_PLUGIN_ABI_MAJOR 1u
#define SEEKDB_PLUGIN_ABI_MINOR 0u
#define SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES 255u
#define SEEKDB_PLUGIN_MAX_BUILD_ID_BYTES 255u
#define SEEKDB_PLUGIN_MAX_SERVICES 4096u

#if defined(_WIN32) || defined(__CYGWIN__)
#define SEEKDB_PLUGIN_EXPORT __declspec(dllexport)
#define SEEKDB_PLUGIN_CALL __cdecl
#elif defined(__GNUC__) || defined(__clang__)
#define SEEKDB_PLUGIN_EXPORT __attribute__((visibility("default")))
#define SEEKDB_PLUGIN_CALL
#else
#define SEEKDB_PLUGIN_EXPORT
#define SEEKDB_PLUGIN_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Scalar "enum" types have an explicitly fixed representation.  The named
 * enum constants below are their only values currently defined by this ABI.
 */
typedef int32_t seekdb_plugin_status_t;
enum seekdb_plugin_status {
  SEEKDB_PLUGIN_STATUS_OK = 0,
  SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT = 1,
  SEEKDB_PLUGIN_STATUS_UNSUPPORTED_ABI = 2,
  SEEKDB_PLUGIN_STATUS_NOT_FOUND = 3,
  SEEKDB_PLUGIN_STATUS_ALREADY_EXISTS = 4,
  SEEKDB_PLUGIN_STATUS_NO_MEMORY = 5,
  SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION = 6,
  SEEKDB_PLUGIN_STATUS_BUSY = 7,
  SEEKDB_PLUGIN_STATUS_UNAVAILABLE = 8,
  SEEKDB_PLUGIN_STATUS_INTERNAL = 9,
  SEEKDB_PLUGIN_STATUS_PERMISSION_DENIED = 10,
  SEEKDB_PLUGIN_STATUS_INVALID_MANIFEST = 11,
  SEEKDB_PLUGIN_STATUS_DEPENDENCY_CYCLE = 12,
  SEEKDB_PLUGIN_STATUS_TIMEOUT = 13,
  SEEKDB_PLUGIN_STATUS_VERIFY_FAILED = 14,
  SEEKDB_PLUGIN_STATUS_MIGRATION_FAILED = 15
};

typedef int32_t seekdb_plugin_state_t;
enum seekdb_plugin_state {
  SEEKDB_PLUGIN_STATE_DISCOVERED = 0,
  SEEKDB_PLUGIN_STATE_VALIDATED = 1,
  SEEKDB_PLUGIN_STATE_LOADED = 2,
  SEEKDB_PLUGIN_STATE_INITIALIZING = 3,
  SEEKDB_PLUGIN_STATE_ACTIVE = 4,
  SEEKDB_PLUGIN_STATE_QUIESCING = 5,
  SEEKDB_PLUGIN_STATE_STOPPED = 6,
  SEEKDB_PLUGIN_STATE_FAILED = 7,
  SEEKDB_PLUGIN_STATE_BLOCKED = 8
};

typedef int32_t seekdb_plugin_log_level_t;
enum seekdb_plugin_log_level {
  SEEKDB_PLUGIN_LOG_TRACE = 0,
  SEEKDB_PLUGIN_LOG_DEBUG = 1,
  SEEKDB_PLUGIN_LOG_INFO = 2,
  SEEKDB_PLUGIN_LOG_WARNING = 3,
  SEEKDB_PLUGIN_LOG_ERROR = 4,
  SEEKDB_PLUGIN_LOG_FATAL = 5
};

typedef uint64_t seekdb_plugin_capability_t;
enum seekdb_plugin_capability {
  SEEKDB_PLUGIN_CAPABILITY_NONE = UINT64_C(0),
  SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE = UINT64_C(1) << 0,
  SEEKDB_PLUGIN_CAPABILITY_MULTI_INSTANCE = UINT64_C(1) << 1,
  SEEKDB_PLUGIN_CAPABILITY_SIDE_BY_SIDE_UPGRADE = UINT64_C(1) << 2,
  SEEKDB_PLUGIN_CAPABILITY_PERSISTENT_DATA = UINT64_C(1) << 3,
  SEEKDB_PLUGIN_CAPABILITY_TRANSACTIONAL_SERVICES = UINT64_C(1) << 4,
  /*
   * Service-descriptor-only discovery marker.  It is invalid on the plugin
   * manifest itself, required-service descriptors, or executable
   * implementation references.  The loader does not publish this service.
   */
  SEEKDB_PLUGIN_CAPABILITY_EXTENSION_CATALOG = UINT64_C(1) << 5
};

/* Opaque identities.  No implementation may depend on their layout. */
typedef struct seekdb_plugin_host_handle seekdb_plugin_host_handle_t;
typedef struct seekdb_plugin_instance_handle seekdb_plugin_instance_handle_t;
typedef struct seekdb_plugin_service_lease seekdb_plugin_service_lease_t;
typedef struct seekdb_plugin_registration_txn seekdb_plugin_registration_txn_t;

typedef struct seekdb_plugin_semantic_version {
  uint32_t major;
  uint32_t minor;
  uint32_t patch;
} seekdb_plugin_semantic_version_t;

/*
 * Inclusive minimum and exclusive maximum for one ABI major.  An all-zero
 * maximum means unbounded within minimum_inclusive.major; a nonzero maximum
 * may stay in that major or be exactly {major + 1, 0, 0}.  Ranges never opt
 * into another incompatible major.
 * This is a fixed-layout v1 leaf because it is embedded by value in other ABI
 * structures: struct_size MUST equal sizeof(seekdb_plugin_version_range_t).
 * It must never grow in place; a future representation needs a new parent
 * service/descriptor version.
 */
typedef struct seekdb_plugin_version_range {
  uint32_t struct_size;
  seekdb_plugin_semantic_version_t minimum_inclusive;
  seekdb_plugin_semantic_version_t maximum_exclusive;
  uint64_t reserved[2];
} seekdb_plugin_version_range_t;

/*
 * A service pointer points to an immutable C function table.  That table MUST
 * begin with uint32_t struct_size and MUST obey all rules at the top of this
 * file, including the published-service lifetime for the table and all
 * immutable plugin-owned data recursively reachable from it.  service_id is a
 * stable, reverse-DNS UTF-8 identifier.  This is a fixed-layout v1 leaf because
 * manifest arrays use sizeof(v1) element stride: struct_size MUST equal sizeof
 * this type.  A future shape requires a new parent manifest ABI.
 */
typedef struct seekdb_plugin_service_provide_descriptor {
  uint32_t struct_size;
  const char *service_id;
  seekdb_plugin_semantic_version_t version;
  const void *service;
  seekdb_plugin_capability_t capabilities;
  uint64_t reserved[4];
} seekdb_plugin_service_provide_descriptor_t;

/*
 * service_slot points to plugin-owned pointer storage.  When non-NULL, the host
 * may store an acquired service table in it during initialization and clears it
 * before the corresponding lease is released.  A plugin that acquires services
 * explicitly may set service_slot to NULL.  Optional requirements may be absent.
 * This is also a fixed-layout v1 leaf: struct_size MUST equal sizeof this type;
 * it must not grow in place while embedded in a counted manifest array.
 */
typedef struct seekdb_plugin_service_require_descriptor {
  uint32_t struct_size;
  const char *service_id;
  seekdb_plugin_version_range_t version_range;
  const void **service_slot;
  uint8_t optional;
  uint8_t reserved_bytes[7];
  seekdb_plugin_capability_t required_capabilities;
  uint64_t reserved[4];
} seekdb_plugin_service_require_descriptor_t;

struct seekdb_plugin_host_api_v1;

typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *seekdb_plugin_init_fn)(
    const struct seekdb_plugin_host_api_v1 *host_api,
    seekdb_plugin_instance_handle_t **out_instance);
typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *seekdb_plugin_start_fn)(
    seekdb_plugin_instance_handle_t *instance);
typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *seekdb_plugin_stop_fn)(
    seekdb_plugin_instance_handle_t *instance);
typedef void(SEEKDB_PLUGIN_CALL *seekdb_plugin_deinit_fn)(
    seekdb_plugin_instance_handle_t *instance);

/* Allocation sizes and alignments are bytes; alignment MUST be a power of two. */
typedef void *(SEEKDB_PLUGIN_CALL *seekdb_plugin_alloc_fn)(
    seekdb_plugin_host_handle_t *host, uint64_t size, uint32_t alignment);
typedef void(SEEKDB_PLUGIN_CALL *seekdb_plugin_free_fn)(
    seekdb_plugin_host_handle_t *host,
    void *memory,
    uint64_t size,
    uint32_t alignment);

/* component and message are borrowed, NUL-terminated UTF-8 strings. */
typedef void(SEEKDB_PLUGIN_CALL *seekdb_plugin_log_fn)(
    seekdb_plugin_host_handle_t *host,
    seekdb_plugin_log_level_t level,
    const char *component,
    const char *message);

/*
 * A successful acquisition returns a service table and a non-NULL lease.  The
 * caller MUST eventually pass that lease to release_service exactly once and
 * MUST NOT use the service table afterward.
 */
typedef seekdb_plugin_status_t(SEEKDB_PLUGIN_CALL *seekdb_plugin_acquire_service_fn)(
    seekdb_plugin_host_handle_t *host,
    const char *service_id,
    const seekdb_plugin_version_range_t *version_range,
    seekdb_plugin_capability_t required_capabilities,
    const void **out_service,
    seekdb_plugin_semantic_version_t *out_version,
    seekdb_plugin_service_lease_t **out_lease);
typedef void(SEEKDB_PLUGIN_CALL *seekdb_plugin_release_service_fn)(
    seekdb_plugin_host_handle_t *host,
    seekdb_plugin_service_lease_t *lease);

/*
 * Service publication is atomic.  After begin succeeds, exactly one commit or
 * abort call MUST be made.  Descriptors passed to register remain borrowed only
 * until that transaction completes.  A successful commit does not retain the
 * descriptor or its strings, but the plugin MUST retain each accepted service
 * table according to the published-service lifetime above; the outer activation
 * publication may occur after this callback returns.  If a transaction fails or
 * aborts and its service was never published, the plugin may release that table
 * after the transaction completes.  A failed register does not end the txn.
 */
typedef seekdb_plugin_status_t(
    SEEKDB_PLUGIN_CALL *seekdb_plugin_begin_registration_fn)(
    seekdb_plugin_host_handle_t *host,
    seekdb_plugin_registration_txn_t **out_txn);
typedef seekdb_plugin_status_t(
    SEEKDB_PLUGIN_CALL *seekdb_plugin_register_service_fn)(
    seekdb_plugin_host_handle_t *host,
    seekdb_plugin_registration_txn_t *txn,
    const seekdb_plugin_service_provide_descriptor_t *service);
typedef seekdb_plugin_status_t(
    SEEKDB_PLUGIN_CALL *seekdb_plugin_commit_registration_fn)(
    seekdb_plugin_host_handle_t *host,
    seekdb_plugin_registration_txn_t *txn);
typedef void(SEEKDB_PLUGIN_CALL *seekdb_plugin_abort_registration_fn)(
    seekdb_plugin_host_handle_t *host,
    seekdb_plugin_registration_txn_t *txn);

/*
 * host_handle and all callback pointers are owned by seekdb.  A plugin may
 * retain this table only from successful init until deinit returns.  It MUST
 * check struct_size before using callbacks appended by a newer ABI minor.
 */
typedef struct seekdb_plugin_host_api_v1 {
  uint32_t struct_size;
  uint32_t abi_major;
  uint32_t abi_minor;
  seekdb_plugin_host_handle_t *host_handle;
  seekdb_plugin_alloc_fn alloc;
  seekdb_plugin_free_fn free;
  seekdb_plugin_log_fn log;
  seekdb_plugin_acquire_service_fn acquire_service;
  seekdb_plugin_release_service_fn release_service;
  seekdb_plugin_begin_registration_fn begin_registration;
  seekdb_plugin_register_service_fn register_service;
  seekdb_plugin_commit_registration_fn commit_registration;
  seekdb_plugin_abort_registration_fn abort_registration;
  uint64_t reserved[8];
} seekdb_plugin_host_api_v1_t;

/*
 * catalog_version and data_format_version describe persistent schemas written
 * or understood by the plugin; zero means that the plugin has no such format.
 * plugin_id, vendor, and build_id are immutable NUL-terminated UTF-8 strings.
 * provides/required_services point to immutable arrays with the stated element counts.
 */
typedef struct seekdb_plugin_manifest_v1 {
  uint32_t struct_size;
  uint32_t abi_major;
  uint32_t abi_minor;
  const char *plugin_id;
  const char *vendor;
  seekdb_plugin_semantic_version_t version;
  const char *build_id;
  uint32_t catalog_version;
  uint32_t data_format_version;
  seekdb_plugin_capability_t capabilities;
  const seekdb_plugin_service_provide_descriptor_t *provides;
  uint32_t provides_count;
  const seekdb_plugin_service_require_descriptor_t *required_services;
  uint32_t required_services_count;
  seekdb_plugin_init_fn init;
  seekdb_plugin_start_fn start;
  seekdb_plugin_stop_fn stop;
  seekdb_plugin_deinit_fn deinit;
  uint64_t reserved[8];
} seekdb_plugin_manifest_v1_t;

typedef const seekdb_plugin_manifest_v1_t *(
    SEEKDB_PLUGIN_CALL *seekdb_plugin_entry_v1_fn)(void);

/*
 * The plugin's single exported symbol.  The returned manifest MUST have
 * abi_major == SEEKDB_PLUGIN_ABI_MAJOR and remain valid until library unload.
 * Returning NULL rejects loading.  This function and every callback are
 * noexcept in the ABI sense described above.
 */
SEEKDB_PLUGIN_EXPORT const seekdb_plugin_manifest_v1_t *SEEKDB_PLUGIN_CALL
seekdb_plugin_entry_v1(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SEEKDB_PLUGIN_SEEKDB_PLUGIN_ABI_H_ */
