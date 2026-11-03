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

#ifndef SEEKDB_PLUGIN_SPATIAL_INDEX_SPI_H_
#define SEEKDB_PLUGIN_SPATIAL_INDEX_SPI_H_

#include "seekdb/plugin/execution_spi.h"

/* Spatial covering backend, NOT a SQL scalar function or a complete index AM.
 * Invoked through a leased execution service v1. Arguments are:
 *   0: non-NULL org.seekdb.gis.geometry (SeekDB SRID/version/WKB encoding)
 *   1: non-NULL REQUEST_TYPE bytes containing the request below.
 * The result is one non-NULL RESULT_TYPE byte buffer: result header followed
 * by cell_count, ancestor_count, vertex_count native-endian uint64_t arrays,
 * in that order. No padding between arrays. Buffers may be unaligned; memcpy
 * fields and IDs. All sizes include only the declared v1 layout. Reserved
 * fields must be zero; unknown flags/versions are rejected, never ignored.
 * This is an in-process ABI, NOT a persisted format. No S2/STL/host objects or
 * plugin-owned pointers cross the boundary. Copy the result during emit_result.
 * Exactly one emit on success, none on input/algorithm failure; emit errors
 * propagate without retry. The registry lease must span execution and emit.
 * Neither plugin code nor host callbacks may unwind exceptions across this
 * C ABI; report failures using seekdb_plugin_status_t.
 *
 * The caller supplies resolved SRS metadata: SRID must match the geometry;
 * geographic input is already normalized longitude/latitude in degrees.
 * Projected input uses the SRS's authoritative bounds, not a guessed SRID map.
 * Bounds are ignored (and must be zero) for geographic input. XY is indexed;
 * Z is not. Query buffering is an angle in radians, after the host's existing
 * ellipsoid/radius conversion; projected buffering happens BEFORE this call.
 * Declaring this service does not enable storage/index SQL admission by itself.
 */
#define SEEKDB_PLUGIN_SPATIAL_COVER_SERVICE "org.seekdb.gis.index.cover"
#define SEEKDB_PLUGIN_SPATIAL_COVER_REQUEST_TYPE "org.seekdb.gis.index.cover_request.v1"
#define SEEKDB_PLUGIN_SPATIAL_COVER_RESULT_TYPE "org.seekdb.gis.index.cover_result.v1"
#define SEEKDB_PLUGIN_SPATIAL_MAX_BYTES (UINT64_C(16) * 1024u * 1024u)

#define SEEKDB_PLUGIN_SPATIAL_GEOGRAPHIC 1u
#define SEEKDB_PLUGIN_SPATIAL_QUERY 2u
#define SEEKDB_PLUGIN_SPATIAL_ANCESTORS 4u
#define SEEKDB_PLUGIN_SPATIAL_VERTICES 8u
#define SEEKDB_PLUGIN_SPATIAL_BUFFER 16u
/* Original query-window constructor uses 50 cells; covered-by and geographic
 * distance-buffer constructors use 4. QUERY alone must not silently select 50.
 * QUERY_WINDOW requires QUERY and is incompatible with BUFFER. */
#define SEEKDB_PLUGIN_SPATIAL_QUERY_WINDOW 32u
/* Cover service version 1.1 adds ALL_VIEWS. Request QUERY, ANCESTORS and
 * VERTICES too; the response uses the v2 result below. Version 1.0 requests
 * and responses are unchanged. Execution SPI/context itself remains v1.0. */
#define SEEKDB_PLUGIN_SPATIAL_ALL_VIEWS 64u
#define SEEKDB_PLUGIN_SPATIAL_ALL_VIEWS_MINOR 1u
#define SEEKDB_PLUGIN_SPATIAL_COVER_ALL_RESULT_TYPE "org.seekdb.gis.index.cover_result.v2"

typedef struct seekdb_plugin_spatial_cover_request_v1 {
  uint32_t struct_size;
  uint32_t flags;
  uint32_t srid;
  uint32_t reserved_word;
  double xmin;
  double xmax;
  double ymin;
  double ymax;
  double buffer_radians;
  uint64_t reserved[4];
} seekdb_plugin_spatial_cover_request_v1_t;

#define SEEKDB_PLUGIN_SPATIAL_RESULT_GEOGRAPHIC 1u
#define SEEKDB_PLUGIN_SPATIAL_RESULT_POINT 2u
#define SEEKDB_PLUGIN_SPATIAL_RESULT_EMPTY 4u
#define SEEKDB_PLUGIN_SPATIAL_RESULT_OUTSIDE_BOUNDS 8u
#define SEEKDB_PLUGIN_SPATIAL_OUTSIDE_CELL UINT64_MAX

/* Stateless MBR candidate rejection, not the exact geometry predicate. Uses
 * a leased execution v1 service, one non-NULL FILTER_REQUEST_TYPE argument,
 * and emits one core.type.bool byte: 1 means REJECT this index row, 0 means
 * retain the candidate for exact geometry evaluation. Same copy/no-unwind
 * rules as the covering service. Unknown flags/operations are errors. */
#define SEEKDB_PLUGIN_SPATIAL_FILTER_SERVICE "org.seekdb.gis.index.filter"
#define SEEKDB_PLUGIN_SPATIAL_FILTER_REQUEST_TYPE "org.seekdb.gis.index.filter_request.v1"
#define SEEKDB_PLUGIN_SPATIAL_FILTER_GEOGRAPHIC 1u
#define SEEKDB_PLUGIN_SPATIAL_FILTER_ROW_POINT 2u
#define SEEKDB_PLUGIN_SPATIAL_FILTER_QUERY_POINT 4u
#define SEEKDB_PLUGIN_SPATIAL_FILTER_COVERS 1u
#define SEEKDB_PLUGIN_SPATIAL_FILTER_INTERSECTS 2u
#define SEEKDB_PLUGIN_SPATIAL_FILTER_COVERED_BY 3u
/* DWithin uses INTERSECTS on its already-buffered query MBR. DFullWithin
 * remains unsupported; do not silently substitute a different predicate. */
typedef struct seekdb_plugin_spatial_filter_request_v1 {
  uint32_t struct_size;
  uint32_t flags;
  uint32_t operation;
  uint32_t reserved_word;
  double row_xmin;
  double row_xmax;
  double row_ymin;
  double row_ymax;
  double query_xmin;
  double query_xmax;
  double query_ymin;
  double query_ymax;
  uint64_t reserved[4];
} seekdb_plugin_spatial_filter_request_v1_t;

typedef struct seekdb_plugin_spatial_cover_result_v1 {
  uint32_t struct_size;
  uint32_t flags;
  uint32_t cell_count;
  uint32_t ancestor_count;
  uint32_t vertex_count;
  uint32_t reserved_word;
  /* Original index MBR coordinates. Geographic longitude can wrap (xmin >
   * xmax). Projected MBR is the original geometry envelope even after the
   * bounds retry. Empty geometry has zero coordinates and EMPTY set.
   * Do not persist this struct; host storage codecs own the row format. */
  double xmin;
  double xmax;
  double ymin;
  double ymax;
  uint64_t reserved[4];
} seekdb_plugin_spatial_cover_result_v1_t;

/* One leased invocation computes all views from the same geometry/SRS and
 * provider generation. v1.struct_size is sizeof(this full v2 header). Arrays
 * follow the FULL header: cover cells, unique ancestors, vertex cells, then
 * query_cells (the legacy get_cellids(true) interleaving of cells/ancestors).
 * Thus consumers need not recreate S2 parent arithmetic or mix generations
 * through repeated executions merely to obtain a different view. */
typedef struct seekdb_plugin_spatial_cover_result_v2 {
  seekdb_plugin_spatial_cover_result_v1_t v1;
  uint32_t query_cell_count;
  uint32_t reserved_word;
  uint64_t reserved[4];
} seekdb_plugin_spatial_cover_result_v2_t;

#define SEEKDB_PLUGIN_SPATIAL_CELLS_SERVICE "org.seekdb.gis.index.cells"
#define SEEKDB_PLUGIN_SPATIAL_CELLS_REQUEST_TYPE "org.seekdb.gis.index.cells_request.v1"
#define SEEKDB_PLUGIN_SPATIAL_CELLS_RESULT_TYPE "org.seekdb.gis.index.cells_result.v1"
#define SEEKDB_PLUGIN_SPATIAL_MAX_CELL_BATCH 4096u
/* Index-query preprocessing, not a SQL function declaration. The caller must
 * resolve the SRS and establish Cartesian coordinates before invoking this
 * service. Arguments: geometry envelope v1, core.type.double distance in the
 * SRS coordinate units. Result: one non-NULL geometry envelope with the same
 * SRID. Uses the original default buffer strategies; no CRS transformation or
 * geographic approximation is performed. Geometry dimensionality is validated
 * by the plugin; result bytes must be copied during emit under the lease. */
#define SEEKDB_PLUGIN_SPATIAL_BUFFER_SERVICE "org.seekdb.gis.index.planar_buffer"
/* Stateless, bounded cell metadata batch. One request header followed by
 * cell_count native-endian uint64_t IDs, 1..MAX_CELL_BATCH. One response header
 * followed by the same number of fixed-sized entries in input order. All
 * fields are copied with memcpy; unused ancestor slots/reserved fields zero.
 * Sentinel OUTSIDE_CELL maps to the exact [sentinel,sentinel] key with no
 * ancestors. Other invalid IDs are errors, never empty/synthetic ranges. */
typedef struct seekdb_plugin_spatial_cells_request_v1 {
  uint32_t struct_size;
  uint32_t cell_count;
  uint64_t reserved[4];
} seekdb_plugin_spatial_cells_request_v1_t;

typedef struct seekdb_plugin_spatial_cell_v1 {
  uint64_t cell_id;
  uint64_t range_min;
  uint64_t range_max;
  uint32_t ancestor_count;
  uint32_t reserved_word;
  uint64_t ancestors[30];
} seekdb_plugin_spatial_cell_v1_t;

typedef struct seekdb_plugin_spatial_cells_result_v1 {
  uint32_t struct_size;
  uint32_t cell_count;
  uint64_t reserved[4];
} seekdb_plugin_spatial_cells_result_v1_t;

/* Cover cells are in S2 order, with OUTSIDE_CELL appended on an outside-bounds
 * retry (or the sole cell when no intersection exists). It is not an S2 cell;
 * do not ask S2 for its level/parent/range. Ancestors are unique and do not
 * include cover cells. Vertex cells reproduce the legacy "inner cover", NOT
 * S2RegionCoverer interior covering and NOT proof that a cell lies inside the
 * geometry. max_cells is 50 with QUERY_WINDOW, otherwise 4 (including buffered
 * geographic queries); level_mod=1/max_level=30;
 * max_cells is an S2 target per region, not an output count limit. */

#endif /* SEEKDB_PLUGIN_SPATIAL_INDEX_SPI_H_ */
