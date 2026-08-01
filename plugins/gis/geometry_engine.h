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

#ifndef SEEKDB_GIS_GEOMETRY_ENGINE_H_
#define SEEKDB_GIS_GEOMETRY_ENGINE_H_

#include "seekdb/plugin/execution_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

enum seekdb_gis_geometry_operation {
  SEEKDB_GIS_OP_TRANSFORM = 1,
  SEEKDB_GIS_OP_BUFFER,
  SEEKDB_GIS_OP_CLIP_BY_BOX,
  SEEKDB_GIS_OP_UNION,
  SEEKDB_GIS_OP_DIFFERENCE,
  SEEKDB_GIS_OP_SYMMETRIC_DIFFERENCE,
  SEEKDB_GIS_OP_ASMVTGEOM,
  SEEKDB_GIS_OP_MAKE_VALID
};

enum seekdb_gis_relation_operation {
  SEEKDB_GIS_REL_EQUALS = 1,
  SEEKDB_GIS_REL_INTERSECTS,
  SEEKDB_GIS_REL_CONTAINS,
  SEEKDB_GIS_REL_WITHIN,
  SEEKDB_GIS_REL_COVERS,
  SEEKDB_GIS_REL_TOUCHES,
  SEEKDB_GIS_REL_CROSSES,
  SEEKDB_GIS_REL_OVERLAPS,
  SEEKDB_GIS_REL_DWITHIN
};

enum seekdb_gis_text_operation {
  SEEKDB_GIS_TEXT_AS_TEXT = 1,
  SEEKDB_GIS_TEXT_AS_GEOJSON,
  SEEKDB_GIS_TEXT_FROM_TEXT
};

enum seekdb_gis_metric_operation {
  SEEKDB_GIS_METRIC_AREA = 1,
  SEEKDB_GIS_METRIC_LENGTH,
  SEEKDB_GIS_METRIC_DISTANCE,
  SEEKDB_GIS_METRIC_DISTANCE_SPHERE
};

seekdb_plugin_status_t seekdb_gis_geometry_operation(
    uint32_t operation,
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count);

seekdb_plugin_status_t seekdb_gis_relation_operation(
    uint32_t operation,
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count);

seekdb_plugin_status_t seekdb_gis_centroid_operation(
    uint8_t surface_only,
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count);

seekdb_plugin_status_t seekdb_gis_mbr_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count);

seekdb_plugin_status_t seekdb_gis_valid_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count);

seekdb_plugin_status_t seekdb_gis_text_operation(
    uint32_t operation,
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count);

seekdb_plugin_status_t seekdb_gis_wkb_from_bytes(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count);

seekdb_plugin_status_t seekdb_gis_geohash_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count);

seekdb_plugin_status_t seekdb_gis_spatial_cellid_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count);

seekdb_plugin_status_t seekdb_gis_set_srid_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count);

seekdb_plugin_status_t seekdb_gis_geometrytype_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count);

seekdb_plugin_status_t seekdb_gis_collection_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count);

seekdb_plugin_status_t seekdb_gis_interior_rings_operation(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count);

seekdb_plugin_status_t seekdb_gis_metric_operation(
    uint32_t operation,
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count);

seekdb_plugin_status_t seekdb_gis_validate_encoded_geometry(
    seekdb_plugin_instance_handle_t *instance,
    const uint8_t *encoded,
    uint64_t encoded_size);

#ifdef __cplusplus
}
#endif

#endif /* SEEKDB_GIS_GEOMETRY_ENGINE_H_ */
