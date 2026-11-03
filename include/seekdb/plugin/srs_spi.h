/*
 * Copyright (c) 2025 OceanBase.
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
#ifndef SEEKDB_PLUGIN_SRS_SPI_H_
#define SEEKDB_PLUGIN_SRS_SPI_H_
#include "seekdb/plugin/execution_spi.h"

/* Private execution-service discovery through the stable C ABI. Two non-NULL
 * arguments: core.type.bytes containing length-delimited WKT1, and REQUEST_TYPE
 * containing request_v1. No catalog lookup, guessed SRID, bounds or proj4 text:
 * the host owns authoritative catalog resolution and supplies those separately.
 * This service describes original SRS metadata; it does NOT execute a transform
 * or establish that an unknown method is executable.
 *
 * Exactly one RESULT_TYPE emission on success: metadata_v1 followed immediately
 * by parameter_count parameter_v1 records in original required-parameter order.
 * Native-endian POD, memcpy for unaligned input/output. Reserved fields zero.
 * Copy during emit; no plugin-owned pointer survives execution. Registry lease
 * spans parse/prepare/emit. Never unwind exceptions through this C ABI. */
#define SEEKDB_PLUGIN_SRS_DESCRIBE_SERVICE "org.seekdb.gis.srs.describe"
#define SEEKDB_PLUGIN_SRS_REQUEST_TYPE "org.seekdb.gis.srs.request.v1"
#define SEEKDB_PLUGIN_SRS_RESULT_TYPE "org.seekdb.gis.srs.metadata.v1"
#define SEEKDB_PLUGIN_SRS_MAX_WKT_BYTES 16384u
#define SEEKDB_PLUGIN_SRS_MAX_PARAMETERS 19u
#define SEEKDB_PLUGIN_SRS_GEOGRAPHIC 1u
#define SEEKDB_PLUGIN_SRS_WGS84 2u
#define SEEKDB_PLUGIN_SRS_HAS_TOWGS84 4u

/* Low-level transform service, independent of catalog lookup and SQL policy.
 * Exactly four non-NULL arguments: org.seekdb.gis.geometry (normal internal
 * SRID/version/WKB envelope), core.type.bytes source proj4, core.type.bytes
 * target proj4, core.type.uint32 target SRID. Strings are length-delimited,
 * nonempty and NUL-free. The source envelope's SRID is not used to guess CRS.
 * Geographic XY MUST be longitude/latitude radians relative to the definition's
 * prime meridian (+pm is applied by Boost, not pre-applied again by the caller).
 * Projected XY use proj4 linear units; geographic results are radians too.
 * Z participates in Boost's 3D datum transform, using proj4 vertical units;
 * absent vunits/vto_meter, Boost defaults to the definition's linear units.
 * Axis/unit/catalog normalization belongs to the caller; this is not a SQL
 * WKB payload until the caller denormalizes it. Output is one geometry with
 * the requested SRID, emitted only after all vertices succeed. Grid files,
 * init-file expansion, geocentric and non-ENU proj4 axes are unsupported.
 * Both definitions are validated even for empty geometry or equal SRIDs.
 * The synchronous registry lease covers construction, transformation and emit.
 */
#define SEEKDB_PLUGIN_SRS_TRANSFORM_SERVICE "org.seekdb.gis.srs.transform"
#define SEEKDB_PLUGIN_SRS_MAX_PROJ4_BYTES 16384u

/* Borrowed raw host catalog record, unrelated to a GIS implementation's
 * parsed metadata. Bounds are catalog values; NaN denotes absent bounds.
 * Text has explicit lengths, no embedded NUL, WKT is nonempty; proj4 may be
 * absent (size=0). Each text is limited by MAX_WKT_BYTES / MAX_PROJ4_BYTES.
 * The SQL API's lookup_srs callback defines snapshot/lifetime semantics. */
typedef struct seekdb_plugin_srs_definition_v1 {
  uint32_t struct_size;
  uint32_t srid;
  const char *definition;
  uint64_t definition_size;
  const char *proj4text;
  uint64_t proj4text_size;
  double min_x, min_y, max_x, max_y;
  uint64_t reserved[4];
} seekdb_plugin_srs_definition_v1_t;

typedef struct seekdb_plugin_srs_request_v1 {
  uint32_t struct_size;
  uint32_t srid; /* UINT32_MAX is invalid; no narrowing of a larger host ID. */
  uint64_t reserved[4];
} seekdb_plugin_srs_request_v1_t;

typedef struct seekdb_plugin_srs_metadata_v1 {
  uint32_t struct_size;
  uint32_t flags;
  uint32_t srid;
  int32_t projection_method; /* Recognized factory EPSG, zero for unknown/geographic. */
  /* Directions: INIT=0, EAST=1, SOUTH=2, WEST=3, NORTH=4, OTHER=5.
   * Geographic base axes are retained separately for projected metadata. */
  uint32_t axis0, axis1, geographic_axis0, geographic_axis1;
  uint32_t parameter_count;
  uint32_t reserved_word;
  /* Always describe the geographic base ellipsoid, including projected SRS.
   * This is not the legacy projected accessor's synthetic zero ellipsoid.
   * Units/prime meridian retain WKT values; normalization is a separate step. */
  double semi_major, inverse_flattening, prime_meridian, angular_unit, linear_unit;
  double towgs84[7]; /* Original absent values are NaN, HAS tests the first value. */
  uint64_t reserved[4];
} seekdb_plugin_srs_metadata_v1_t;

typedef struct seekdb_plugin_srs_parameter_v1 {
  int32_t authority_code;
  uint32_t reserved_word;
  double value;
} seekdb_plugin_srs_parameter_v1_t;
#endif
