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

#ifndef SEEKDB_PLUGIN_CATALOG_SPI_H_
#define SEEKDB_PLUGIN_CATALOG_SPI_H_
#include "seekdb/plugin/seekdb_plugin_abi.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Optional service named <native_module_id>.catalog.install, version major 1.
 * CREATE EXTENSION calls it after reading its declared source and before admitting
 * objects. All declarations are parsed/preflighted and applied by the normal
 * installer in its one schema transaction. Emission stages a declaration, NOT
 * a created object. No SQL execution/read, transaction control, background work
 * or external side effects are allowed during this preparation callback.
 * This revision is installation-only; updates use explicit migration scripts.
 * The host pins the module through preparation, installation and publication.
 * SQL-source packages use it optionally, after their static SQL. Explicit native
 * sources require it to supply all initial statements, without a base SQL file.
 */
#define SEEKDB_PLUGIN_CATALOG_INSTALL_SUFFIX ".catalog.install"
typedef struct seekdb_plugin_catalog_context_v1 {
  uint32_t struct_size;
  uint32_t reserved_word;
  uint64_t tenant_id;
  uint64_t database_id;
  uint64_t owner_id;
  const char *extension_name;
  const char *extension_version;
  void *host_context;
  /* Complete UTF-8 SQL fragments, copied synchronously and parsed separately.
   * <=4096 fragments, <=4 MiB combined with static scripts. Errors are sticky:
   * ignoring an emit failure cannot turn preparation into success. Context and
   * callback pointers are borrowed, same-thread, and cannot escape prepare. */
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *emit_sql)(void *, const char *, uint64_t);
  uint64_t reserved[4];
} seekdb_plugin_catalog_context_v1_t;

typedef struct seekdb_plugin_catalog_service_v1 {
  uint32_t struct_size;
  uint32_t spi_major;
  uint32_t spi_minor;
  uint32_t reserved_word;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *prepare)(seekdb_plugin_instance_handle_t *,
      const seekdb_plugin_catalog_context_v1_t *);
  uint64_t reserved[4];
} seekdb_plugin_catalog_service_v1_t;

/* SPI 1.1: transaction-bound construction, after static/prepared declarations.
 * This separate context does not change the v1 prepare context. Each call
 * creates one new FUNCTION/PROCEDURE through normal host admission, and returns
 * a reserved ID visible to subsequent construction in this transaction's view.
 * IDs are provisional until installation commits; never publish/use them as
 * durable identities on callback return. No SQL execution, transaction control,
 * background work or external side effects. Same-thread, borrowed during build.
 * Combined static/dynamic limits: 4 MiB SQL, 4096 objects. Errors are sticky.
 */
typedef struct seekdb_plugin_catalog_build_context_v1 {
  uint32_t struct_size;
  uint32_t reserved_word;
  uint64_t tenant_id;
  uint64_t database_id;
  uint64_t owner_id;
  const char *extension_name;
  const char *extension_version;
  void *host_context;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *create_routine)(void *, const char *, uint64_t, uint64_t *);
  uint64_t reserved[4];
} seekdb_plugin_catalog_build_context_v1_t;

#define SEEKDB_PLUGIN_CATALOG_ROUTINE_FUNCTION 1u
#define SEEKDB_PLUGIN_CATALOG_ROUTINE_PROCEDURE 2u
#define SEEKDB_PLUGIN_CATALOG_MAX_ROUTINE_NAME_BYTES 2048u

/* Optional, size-negotiated suffix for SPI 1.1 build contexts. Older plugins
 * can consume v1 unchanged; newer plugins must check v1.struct_size before
 * accessing this suffix. No change to the service or callback signature.
 * lookup takes an unquoted UTF-8 name, at most MAX_ROUTINE_NAME_BYTES (not SQL
 * or a qualified path), scoped to
 * v1.database_id. Host name comparison and routine visibility rules apply.
 * OK with ID=0 means absent; a visible routine returns its transaction-view ID.
 * Lookup grants no execution privilege, membership, or dependency. Using an
 * existing routine in a body still goes through ordinary semantic resolution.
 * Errors are sticky across lookup/create; absence is not an error.
 */
typedef struct seekdb_plugin_catalog_build_context_v2 {
  seekdb_plugin_catalog_build_context_v1_t v1;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *lookup_routine)(void *, uint32_t,
      const char *, uint64_t, uint64_t *);
  uint64_t reserved[4];
} seekdb_plugin_catalog_build_context_v2_t;

typedef struct seekdb_plugin_catalog_service_v2 {
  /* v1.struct_size = sizeof(v2), spi_major = 1, spi_minor = 1.
   * prepare remains required and may emit nothing when build supplies objects.
   * Old hosts reject the new SPI minor rather than ignoring build. */
  seekdb_plugin_catalog_service_v1_t v1;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *build)(seekdb_plugin_instance_handle_t *,
      const seekdb_plugin_catalog_build_context_v1_t *);
  uint64_t reserved[4];
} seekdb_plugin_catalog_service_v2_t;
#ifdef __cplusplus
}
#endif
#endif
