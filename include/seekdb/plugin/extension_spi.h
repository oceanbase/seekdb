/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SEEKDB_PLUGIN_EXTENSION_SPI_H_
#define SEEKDB_PLUGIN_EXTENSION_SPI_H_

#include "seekdb/plugin/seekdb_plugin_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SEEKDB_PLUGIN_EXTENSION_SPI_MAJOR 1u
#define SEEKDB_PLUGIN_EXTENSION_SPI_MINOR 0u
#define SEEKDB_PLUGIN_MAX_EXTENSIONS 4096u
#define SEEKDB_PLUGIN_MAX_EXTENSION_DESCRIPTOR_BYTES UINT32_C(65536)
#define SEEKDB_PLUGIN_MAX_EXTENSION_ARRAY_BYTES UINT64_C(67108864)

/*
 * All strings are NUL-terminated ASCII and at most
 * SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES bytes excluding NUL.  object/service/
 * type/format/hook/object-kind IDs use [a-z0-9._-]+.  SQL names are canonical
 * unquoted lower-case segments using [a-z0-9_$]+; type/function/index names
 * may qualify segments with single dots, while catalog schema_name and
 * sql_name are separate unqualified segments.  definition_digest is exactly
 * "sha256:" plus 64 lower-case hexadecimal characters.
 */

/*
 * The extension catalog is metadata, not an execution back door.  Every
 * executable contribution refers to a separately registered named service;
 * consumers must acquire and retain that service's lease before invocation.
 * This header does not define those implementation table layouts; a consumer
 * MUST NOT cast or invoke a returned service until the corresponding
 * kind-specific execution SPI has validated it.
 */
typedef int32_t seekdb_plugin_extension_kind_t;
enum seekdb_plugin_extension_kind {
  SEEKDB_PLUGIN_EXTENSION_TYPE = 1,
  SEEKDB_PLUGIN_EXTENSION_FUNCTION = 2,
  SEEKDB_PLUGIN_EXTENSION_CAST = 3,
  SEEKDB_PLUGIN_EXTENSION_INDEX_ACCESS_METHOD = 4,
  SEEKDB_PLUGIN_EXTENSION_OPTIMIZER_HOOK = 5,
  SEEKDB_PLUGIN_EXTENSION_DAS_HOOK = 6,
  SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT = 7
};

typedef uint64_t seekdb_plugin_extension_flags_t;
enum seekdb_plugin_extension_flags {
  SEEKDB_PLUGIN_EXTENSION_FLAG_NONE = UINT64_C(0),
  SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC = UINT64_C(1) << 0,
  SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE = UINT64_C(1) << 1,
  SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING = UINT64_C(1) << 2,
  SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT = UINT64_C(1) << 3,
  SEEKDB_PLUGIN_EXTENSION_FLAG_PARALLEL_SAFE = UINT64_C(1) << 4,
  SEEKDB_PLUGIN_EXTENSION_FLAG_REQUIRES_CATALOG = UINT64_C(1) << 5
};

typedef int32_t seekdb_plugin_cast_context_t;
enum seekdb_plugin_cast_context {
  SEEKDB_PLUGIN_CAST_EXPLICIT = 1,
  SEEKDB_PLUGIN_CAST_ASSIGNMENT = 2,
  SEEKDB_PLUGIN_CAST_IMPLICIT = 3
};

/*
 * A reference to an implementation already staged in the service registry.
 * object descriptors never contain executable pointers.  A host must resolve
 * the descriptor and service in one registry operation and retain both leases
 * for the complete plan/iterator/task lifetime.  This is a fixed-layout v1
 * leaf embedded by value: struct_size MUST equal sizeof this type and it must
 * never grow in place.  A future shape requires a new parent descriptor ABI.
 */
typedef struct seekdb_plugin_implementation_ref_v1 {
  uint32_t struct_size;
  const char *service_id;
  seekdb_plugin_version_range_t version_range;
  seekdb_plugin_capability_t required_capabilities;
  uint64_t reserved[4];
} seekdb_plugin_implementation_ref_v1_t;

typedef struct seekdb_plugin_type_descriptor_v1 {
  uint32_t struct_size;
  const char *object_id;
  const char *sql_name;
  const char *physical_format_id;
  uint32_t physical_format_version;
  uint32_t reserved_word;
  seekdb_plugin_extension_flags_t flags;
  seekdb_plugin_implementation_ref_v1_t codec_service;
  uint64_t reserved[4];
} seekdb_plugin_type_descriptor_v1_t;

/*
 * Equal sql_name values are permitted for function overloads; object_id is the
 * stable overload identity.  v1 records an arity envelope.  A non-NULL
 * static_result_type_id is a fixed result type; NULL means the callable SPI
 * must resolve it from argument metadata.  A later minor may append typed
 * signature metadata without changing the identity or service reference below.
 */
typedef struct seekdb_plugin_function_descriptor_v1 {
  uint32_t struct_size;
  const char *object_id;
  const char *sql_name;
  uint32_t minimum_arity;
  uint32_t maximum_arity;
  const char *static_result_type_id;
  seekdb_plugin_extension_flags_t flags;
  seekdb_plugin_implementation_ref_v1_t implementation;
  uint64_t reserved[4];
} seekdb_plugin_function_descriptor_v1_t;

typedef struct seekdb_plugin_cast_descriptor_v1 {
  uint32_t struct_size;
  const char *object_id;
  const char *source_type_id;
  const char *target_type_id;
  seekdb_plugin_cast_context_t context;
  uint32_t cost;
  seekdb_plugin_extension_flags_t flags;
  seekdb_plugin_implementation_ref_v1_t implementation;
  uint64_t reserved[4];
} seekdb_plugin_cast_descriptor_v1_t;

typedef struct seekdb_plugin_index_access_method_descriptor_v1 {
  uint32_t struct_size;
  const char *object_id;
  const char *sql_name;
  seekdb_plugin_extension_flags_t flags;
  seekdb_plugin_implementation_ref_v1_t implementation;
  uint64_t reserved[4];
} seekdb_plugin_index_access_method_descriptor_v1_t;

/* Hooks at one hook_point run by descending priority, then ascending object_id. */

typedef struct seekdb_plugin_optimizer_hook_descriptor_v1 {
  uint32_t struct_size;
  const char *object_id;
  const char *hook_point;
  int32_t priority;
  uint32_t reserved_word;
  seekdb_plugin_extension_flags_t flags;
  seekdb_plugin_implementation_ref_v1_t implementation;
  uint64_t reserved[4];
} seekdb_plugin_optimizer_hook_descriptor_v1_t;

typedef struct seekdb_plugin_das_hook_descriptor_v1 {
  uint32_t struct_size;
  const char *object_id;
  const char *hook_point;
  int32_t priority;
  uint32_t reserved_word;
  seekdb_plugin_extension_flags_t flags;
  seekdb_plugin_implementation_ref_v1_t implementation;
  uint64_t reserved[4];
} seekdb_plugin_das_hook_descriptor_v1_t;

/*
 * Catalog objects are declarative identities.  definition_digest is a
 * plugin-supplied assertion, not proof that the definition is bound to package
 * content.  Before materialization or publication, a production verifier and
 * catalog MUST reconcile it with authenticated package metadata and payload;
 * syntax/format validation alone establishes neither binding nor trust.
 * Arbitrary SQL is deliberately not passed through this ABI.  The catalog
 * coordinator materializes the known object kind transactionally and records
 * plugin ownership.
 */
typedef struct seekdb_plugin_catalog_object_descriptor_v1 {
  uint32_t struct_size;
  const char *object_id;
  const char *object_kind;
  const char *schema_name;
  const char *sql_name;
  const char *definition_digest;
  seekdb_plugin_extension_flags_t flags;
  uint64_t reserved[4];
} seekdb_plugin_catalog_object_descriptor_v1_t;

/*
 * The snapshot and every referenced descriptor/string are immutable and must
 * remain valid from describe_extensions() until plugin deinit.  The host makes
 * bounded, pointer-free copies before publication and never retains these
 * plugin-owned addresses as catalog metadata.  Arrays are byte-contiguous and
 * each element's struct_size is its stride; this lets a newer minor append
 * fields while an older host reads the v1 prefix.  No descriptor or aggregate
 * descriptor arrays may exceed the public byte limits above.  Each *_bytes
 * field is the exact byte length of that array; walking count variable-stride
 * elements MUST consume it exactly.  A zero count requires a NULL pointer and
 * zero bytes.
 */
typedef struct seekdb_plugin_extension_snapshot_v1 {
  uint32_t struct_size;
  const seekdb_plugin_type_descriptor_v1_t *types;
  uint32_t type_count;
  uint32_t type_bytes;
  const seekdb_plugin_function_descriptor_v1_t *functions;
  uint32_t function_count;
  uint32_t function_bytes;
  const seekdb_plugin_cast_descriptor_v1_t *casts;
  uint32_t cast_count;
  uint32_t cast_bytes;
  const seekdb_plugin_index_access_method_descriptor_v1_t *index_access_methods;
  uint32_t index_access_method_count;
  uint32_t index_access_method_bytes;
  const seekdb_plugin_optimizer_hook_descriptor_v1_t *optimizer_hooks;
  uint32_t optimizer_hook_count;
  uint32_t optimizer_hook_bytes;
  const seekdb_plugin_das_hook_descriptor_v1_t *das_hooks;
  uint32_t das_hook_count;
  uint32_t das_hook_bytes;
  const seekdb_plugin_catalog_object_descriptor_v1_t *catalog_objects;
  uint32_t catalog_object_count;
  uint32_t catalog_object_bytes;
  uint64_t reserved[8];
} seekdb_plugin_extension_snapshot_v1_t;

typedef seekdb_plugin_status_t(
    SEEKDB_PLUGIN_CALL *seekdb_plugin_describe_extensions_fn)(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_extension_snapshot_v1_t **out_snapshot);

/*
 * A plugin marks at most one uniquely named provided service with
 * SEEKDB_PLUGIN_CAPABILITY_EXTENSION_CATALOG.  The loader recognizes that
 * capability and interprets its table using this layout after start succeeds.
 * A plugin which marks the capability must expose exactly this v1 table.  It
 * is a loader-discovery service and is not published for ordinary acquisition.
 */
typedef struct seekdb_plugin_extension_catalog_service_v1 {
  uint32_t struct_size;
  seekdb_plugin_describe_extensions_fn describe_extensions;
  uint64_t reserved[8];
} seekdb_plugin_extension_catalog_service_v1_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SEEKDB_PLUGIN_EXTENSION_SPI_H_ */
