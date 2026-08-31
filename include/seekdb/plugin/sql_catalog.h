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

#ifndef SEEKDB_PLUGIN_SQL_CATALOG_H_
#define SEEKDB_PLUGIN_SQL_CATALOG_H_

#include "seekdb/plugin/extension_spi.h"

#define SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER "seekdb.plugin.type:v1"
#define SEEKDB_PLUGIN_SQL_TYPE_METADATA_FIELD_COUNT 7u

enum seekdb_plugin_sql_type_metadata_field {
  SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER_FIELD = 0,
  SEEKDB_PLUGIN_SQL_TYPE_METADATA_SQL_NAME_FIELD = 1,
  SEEKDB_PLUGIN_SQL_TYPE_METADATA_OBJECT_ID_FIELD = 2,
  SEEKDB_PLUGIN_SQL_TYPE_METADATA_OWNER_PLUGIN_ID_FIELD = 3,
  SEEKDB_PLUGIN_SQL_TYPE_METADATA_OWNER_GENERATION_FIELD = 4,
  SEEKDB_PLUGIN_SQL_TYPE_METADATA_PHYSICAL_FORMAT_ID_FIELD = 5,
  SEEKDB_PLUGIN_SQL_TYPE_METADATA_PHYSICAL_FORMAT_VERSION_FIELD = 6
};

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A host-owned, pointer-free result returned by SQL name resolution.  It does
 * not pin executable code: execution must revalidate owner/generation and
 * acquire the matching object and implementation leases atomically.
 */
typedef struct seekdb_plugin_sql_binding_v1 {
  uint32_t struct_size;
  seekdb_plugin_extension_kind_t kind;
  char object_id[SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1];
  char sql_name[SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1];
  char result_type_id[SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1];
  char owner_plugin_id[SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1];
  char physical_format_id[SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1];
  uint64_t owner_generation;
  uint64_t catalog_epoch;
  uint64_t flags;
  uint32_t minimum_arity;
  uint32_t maximum_arity;
  uint32_t column_count;
  uint32_t physical_format_version;
  uint64_t reserved[4];
} seekdb_plugin_sql_binding_v1_t;

/* A bounded, host-owned table column description with no plugin pointers. */
typedef struct seekdb_plugin_sql_column_v1 {
  uint32_t struct_size;
  char sql_name[SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1];
  char type_id[SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1];
  uint8_t nullable;
  uint8_t reserved_bytes[7];
  uint64_t reserved[4];
} seekdb_plugin_sql_column_v1_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SEEKDB_PLUGIN_SQL_CATALOG_H_ */
