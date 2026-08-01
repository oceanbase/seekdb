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

#include "seekdb/plugin/execution_spi.h"
#include "seekdb/plugin/extension_spi.h"
#include "geometry_engine.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

struct seekdb_plugin_instance_handle {
  uint8_t started;
};

static struct seekdb_plugin_instance_handle gis_instance;

static seekdb_plugin_status_t emit_geometry_bytes(
    const seekdb_plugin_execution_context_v1_t *context,
    const uint8_t *data,
    uint64_t data_size)
{
  if (NULL == context || context->struct_size != sizeof(*context) ||
      NULL == context->emit_result || NULL == data || data_size == 0) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.geometry", data, data_size, 0,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

static seekdb_plugin_status_t emit_bytes_result(
    const seekdb_plugin_execution_context_v1_t *context,
    const uint8_t *data,
    uint64_t data_size)
{
  if (NULL == context || context->struct_size != sizeof(*context) ||
      NULL == context->emit_result || NULL == data || data_size == 0) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.scalar.bytes", data, data_size, 0,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_geometry_decode(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const uint8_t *encoded,
    uint64_t encoded_size)
{
  if (instance != &gis_instance || !gis_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  if (encoded == NULL || SEEKDB_PLUGIN_STATUS_OK !=
      seekdb_gis_validate_encoded_geometry(instance, encoded, encoded_size)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  return emit_geometry_bytes(context, encoded, encoded_size);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_geometry_encode(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *value)
{
  if (instance != &gis_instance || !gis_instance.started || NULL == value ||
      value->struct_size != sizeof(*value) || value->is_null ||
      NULL == value->data || value->data_size == 0 || value->type_id == NULL ||
      0 != strcmp(value->type_id, "org.seekdb.gis.geometry")) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  if (SEEKDB_PLUGIN_STATUS_OK != seekdb_gis_validate_encoded_geometry(
          instance, value->data, value->data_size)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  return emit_geometry_bytes(context, value->data, value->data_size);
}

static const seekdb_plugin_type_codec_service_v1_t gis_geometry_codec_service = {
    sizeof(gis_geometry_codec_service),
    SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR,
    0,
    gis_geometry_decode,
    gis_geometry_encode,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static uint32_t gis_read_u32_le(const uint8_t *data);

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_st_point_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    const uint32_t argument_count)
{
  if (instance != &gis_instance || !gis_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  if (NULL == context || context->struct_size != sizeof(*context) ||
      NULL == context->emit_result || NULL == arguments ||
      (argument_count != 2 && argument_count != 3)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  if (arguments[0].struct_size != sizeof(arguments[0]) ||
      arguments[1].struct_size != sizeof(arguments[1]) ||
      arguments[0].is_null || arguments[1].is_null ||
      NULL == arguments[0].data || NULL == arguments[1].data ||
      arguments[0].data_size != sizeof(double) ||
      arguments[1].data_size != sizeof(double)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }

  /* The first vertical slice accepts host little-endian IEEE-754 doubles. */
  uint8_t wkb[34] = {0};
  /* seekdb geometry payload: SRID(4), version(1), byte-order(1), type(4), XY. */
  wkb[4] = 1; /* geometry binary version */
  wkb[5] = 1; /* little endian */
  wkb[6] = argument_count == 3 ? 0xe9 : 1; /* PointZ=1001 or Point */
  wkb[7] = argument_count == 3 ? 0x03 : 0;
  memcpy(wkb + 10, arguments[0].data, sizeof(double));
  memcpy(wkb + 18, arguments[1].data, sizeof(double));
  if (argument_count == 3) memcpy(wkb + 26, arguments[2].data, sizeof(double));
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result),
      "org.seekdb.gis.geometry",
      wkb,
      argument_count == 3 ? 34 : 26,
      0,
      {0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

static int valid_double_arguments(const seekdb_plugin_execution_value_v1_t *arguments,
                                  uint32_t argument_count)
{
  if (NULL == arguments || argument_count == 0) return 0;
  for (uint32_t i = 0; i < argument_count; ++i) {
    if (arguments[i].struct_size != sizeof(arguments[i]) || arguments[i].is_null ||
        NULL == arguments[i].data || arguments[i].data_size != sizeof(double)) {
      return 0;
    }
  }
  return 1;
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_function_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    const uint32_t argument_count)
{
  if (instance != &gis_instance || !gis_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  if (NULL == context || context->struct_size != sizeof(*context) ||
      NULL == context->emit_result || !valid_double_arguments(arguments, argument_count) ||
      (argument_count != 2 && argument_count != 3 && argument_count != 4)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }

  if (argument_count == 2 || argument_count == 3) {
    return gis_st_point_execute(instance, context, arguments, argument_count);
  }

  /* Four-argument ST_MakeEnvelope, default SRID=0, polygon WKB envelope. */
  uint8_t wkb[98] = {0};
  wkb[4] = 1;  /* geometry binary version */
  wkb[5] = 1;  /* little endian */
  wkb[6] = 3;  /* WKB Polygon */
  wkb[10] = 1; /* one ring */
  wkb[14] = 5; /* five points, including closure */
  double values[4] = {0, 0, 0, 0};
  for (uint32_t i = 0; i < 4; ++i) {
    memcpy(&values[i], arguments[i].data, sizeof(double));
  }
  const double points[10] = {
      values[0], values[1], values[0], values[3], values[2], values[3],
      values[2], values[1], values[0], values[1]};
  memcpy(wkb + 18, points, sizeof(points));
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result),
      "org.seekdb.gis.geometry",
      wkb,
      sizeof(wkb),
      0,
      {0, 0, 0, 0, 0, 0, 0},
      {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

static const seekdb_plugin_function_service_v1_t gis_function_service = {
    sizeof(gis_function_service),
    SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR,
    0,
    gis_function_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static seekdb_plugin_status_t gis_coordinate_execute_common(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count,
    uint32_t coordinate_offset)
{
  if (instance != &gis_instance || !gis_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  if (NULL == context || context->struct_size != sizeof(*context) ||
      NULL == context->emit_result || NULL == arguments || argument_count != 1 ||
      arguments[0].struct_size != sizeof(arguments[0]) || arguments[0].is_null ||
      NULL == arguments[0].data || arguments[0].data_size < 26 ||
      NULL == arguments[0].type_id ||
      0 != strcmp(arguments[0].type_id, "org.seekdb.gis.geometry") ||
      arguments[0].data[5] != 1 ||
      (gis_read_u32_le(arguments[0].data + 6) != 1 &&
       gis_read_u32_le(arguments[0].data + 6) != 1001)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  double coordinate = 0.0;
  memcpy(&coordinate, arguments[0].data + coordinate_offset, sizeof(coordinate));
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.scalar.float64",
      (const uint8_t *)&coordinate, sizeof(coordinate), 0,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_st_x_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  return gis_coordinate_execute_common(instance, context, arguments, argument_count, 10);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_st_y_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  return gis_coordinate_execute_common(instance, context, arguments, argument_count, 18);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_st_srid_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance != &gis_instance || !gis_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  if (NULL == context || context->struct_size != sizeof(*context) ||
      NULL == context->emit_result || NULL == arguments || argument_count != 1 ||
      arguments[0].struct_size != sizeof(arguments[0]) || arguments[0].is_null ||
      NULL == arguments[0].data || arguments[0].data_size < 26 ||
      NULL == arguments[0].type_id ||
      0 != strcmp(arguments[0].type_id, "org.seekdb.gis.geometry") ||
      arguments[0].data[5] != 1) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  uint32_t srid = (uint32_t)arguments[0].data[0] |
                  ((uint32_t)arguments[0].data[1] << 8) |
                  ((uint32_t)arguments[0].data[2] << 16) |
                  ((uint32_t)arguments[0].data[3] << 24);
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.scalar.uint32",
      (const uint8_t *)&srid, sizeof(srid), 0,
      {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_st_aswkb_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance != &gis_instance || !gis_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  if (NULL == context || context->struct_size != sizeof(*context) ||
      NULL == context->emit_result || NULL == arguments || argument_count != 1 ||
      arguments[0].struct_size != sizeof(arguments[0]) || arguments[0].is_null ||
      NULL == arguments[0].data || arguments[0].data_size <= 5 ||
      NULL == arguments[0].type_id ||
      0 != strcmp(arguments[0].type_id, "org.seekdb.gis.geometry") ||
      arguments[0].data[4] != 1 || arguments[0].data[5] != 1) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  /* The seekdb geometry envelope is SRID(4) + version(1) + standard WKB. */
  return emit_bytes_result(context, arguments[0].data + 5, arguments[0].data_size - 5);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_geometrytype_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance != &gis_instance || !gis_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  return seekdb_gis_geometrytype_operation(instance, context, arguments, argument_count);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_st_iscollection_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance != &gis_instance || !gis_instance.started ||
      NULL == context) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  return seekdb_gis_collection_operation(instance, context, arguments, argument_count);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_st_numinteriorrings_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance != &gis_instance || !gis_instance.started ||
      NULL == context) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  return seekdb_gis_interior_rings_operation(instance, context, arguments, argument_count);
}

static seekdb_plugin_status_t gis_geometry_operation_execute_with_kind(
    uint32_t operation,
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance != &gis_instance || !gis_instance.started) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  return seekdb_gis_geometry_operation(operation, instance, context, arguments, argument_count);
}

#define GIS_GEOMETRY_OPERATION_WRAPPER(name, operation_kind) \
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL name( \
    seekdb_plugin_instance_handle_t *instance, \
    const seekdb_plugin_execution_context_v1_t *context, \
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t argument_count) \
{ return gis_geometry_operation_execute_with_kind(operation_kind, instance, context, arguments, argument_count); }

GIS_GEOMETRY_OPERATION_WRAPPER(gis_st_transform_engine_execute, SEEKDB_GIS_OP_TRANSFORM)
GIS_GEOMETRY_OPERATION_WRAPPER(gis_st_priv_transform_engine_execute, SEEKDB_GIS_OP_TRANSFORM)
GIS_GEOMETRY_OPERATION_WRAPPER(gis_st_buffer_engine_execute, SEEKDB_GIS_OP_BUFFER)
GIS_GEOMETRY_OPERATION_WRAPPER(gis_st_priv_buffer_engine_execute, SEEKDB_GIS_OP_BUFFER)
GIS_GEOMETRY_OPERATION_WRAPPER(gis_st_clipbybox2d_engine_execute, SEEKDB_GIS_OP_CLIP_BY_BOX)
GIS_GEOMETRY_OPERATION_WRAPPER(gis_st_union_engine_execute, SEEKDB_GIS_OP_UNION)
GIS_GEOMETRY_OPERATION_WRAPPER(gis_st_difference_engine_execute, SEEKDB_GIS_OP_DIFFERENCE)
GIS_GEOMETRY_OPERATION_WRAPPER(gis_st_symdifference_engine_execute, SEEKDB_GIS_OP_SYMMETRIC_DIFFERENCE)
GIS_GEOMETRY_OPERATION_WRAPPER(gis_st_asmvtgeom_engine_execute, SEEKDB_GIS_OP_ASMVTGEOM)
GIS_GEOMETRY_OPERATION_WRAPPER(gis_st_makevalid_engine_execute, SEEKDB_GIS_OP_MAKE_VALID)

#undef GIS_GEOMETRY_OPERATION_WRAPPER

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_st_centroid_engine_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t argument_count)
{
  if (instance != &gis_instance || !gis_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  return seekdb_gis_centroid_operation(0, instance, context, arguments, argument_count);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_st_pointonsurface_engine_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t argument_count)
{
  if (instance != &gis_instance || !gis_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  return seekdb_gis_centroid_operation(1, instance, context, arguments, argument_count);
}

#define GIS_TEXT_ENGINE_WRAPPER(name, operation_kind) \
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL name( \
    seekdb_plugin_instance_handle_t *instance, \
    const seekdb_plugin_execution_context_v1_t *context, \
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t argument_count) \
{ \
  if (instance != &gis_instance || !gis_instance.started) return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION; \
  return seekdb_gis_text_operation(operation_kind, instance, context, arguments, argument_count); \
}
GIS_TEXT_ENGINE_WRAPPER(gis_st_astext_engine_execute, SEEKDB_GIS_TEXT_AS_TEXT)
GIS_TEXT_ENGINE_WRAPPER(gis_st_asgeojson_engine_execute, SEEKDB_GIS_TEXT_AS_GEOJSON)
GIS_TEXT_ENGINE_WRAPPER(gis_geomfromtext_engine_execute, SEEKDB_GIS_TEXT_FROM_TEXT)
#undef GIS_TEXT_ENGINE_WRAPPER

#define GIS_METRIC_ENGINE_WRAPPER(name, operation_kind) \
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL name( \
    seekdb_plugin_instance_handle_t *instance, \
    const seekdb_plugin_execution_context_v1_t *context, \
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t argument_count) \
{ \
  if (instance != &gis_instance || !gis_instance.started) return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION; \
  return seekdb_gis_metric_operation(operation_kind, instance, context, arguments, argument_count); \
}
GIS_METRIC_ENGINE_WRAPPER(gis_st_area_engine_execute, SEEKDB_GIS_METRIC_AREA)
GIS_METRIC_ENGINE_WRAPPER(gis_st_length_engine_execute, SEEKDB_GIS_METRIC_LENGTH)
GIS_METRIC_ENGINE_WRAPPER(gis_st_distance_engine_execute, SEEKDB_GIS_METRIC_DISTANCE)
GIS_METRIC_ENGINE_WRAPPER(gis_st_distance_sphere_engine_execute, SEEKDB_GIS_METRIC_DISTANCE_SPHERE)
#undef GIS_METRIC_ENGINE_WRAPPER

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_st_buffer_strategy_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance != &gis_instance || !gis_instance.started ||
      NULL == context || context->struct_size != sizeof(*context) ||
      NULL == context->emit_result || NULL == arguments ||
      (argument_count != 1 && argument_count != 2)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  /* Stable 16-byte strategy payload: strategy id, join/end masks and
   * reserved words consumed by the plugin buffer implementation. */
  uint32_t strategy[4] = {0, 0, 0, 0};
  if (arguments[0].struct_size != sizeof(arguments[0]) || arguments[0].is_null ||
      NULL == arguments[0].data || arguments[0].data_size == 0) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  if (arguments[0].type_id != NULL &&
      0 == strcmp(arguments[0].type_id, "org.seekdb.gis.scalar.bytes")) {
    const char *name = (const char *)arguments[0].data;
    if (arguments[0].data_size >= 8 && 0 == strncmp(name, "end_flat", 8)) strategy[0] = 2;
    else if (arguments[0].data_size >= 10 && 0 == strncmp(name, "join_round", 10)) strategy[0] = 3;
    else if (arguments[0].data_size >= 10 && 0 == strncmp(name, "join_miter", 10)) strategy[0] = 4;
    else strategy[0] = 1;
  }
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.scalar.bytes", (const uint8_t *)strategy,
      sizeof(strategy), 0, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_st_bestsrid_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance != &gis_instance || !gis_instance.started ||
      NULL == context || context->struct_size != sizeof(*context) ||
      NULL == context->emit_result || NULL == arguments ||
      (argument_count != 1 && argument_count != 2)) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  for (uint32_t i = 0; i < argument_count; ++i) {
    if (arguments[i].struct_size != sizeof(arguments[i]) || arguments[i].is_null ||
        NULL == arguments[i].data || arguments[i].data_size < 6 ||
        NULL == arguments[i].type_id ||
        0 != strcmp(arguments[i].type_id, "org.seekdb.gis.geometry")) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
  }
  uint32_t first_srid = 0;
  memcpy(&first_srid, arguments[0].data, sizeof(first_srid));
  uint32_t selected_srid = first_srid;
  if (argument_count == 2) {
    uint32_t second_srid = 0;
    memcpy(&second_srid, arguments[1].data, sizeof(second_srid));
    if (second_srid != first_srid) selected_srid = 3857;
  }
  const int32_t srid = (int32_t)selected_srid;
  const seekdb_plugin_execution_result_v1_t result = {
      sizeof(result), "org.seekdb.gis.scalar.int32", (const uint8_t *)&srid,
      sizeof(srid), 0, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
  return context->emit_result(context->host, &result);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_st_spatial_cellid_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance != &gis_instance || !gis_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  return seekdb_gis_spatial_cellid_operation(instance, context, arguments, argument_count);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_st_geohash_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance != &gis_instance || !gis_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  return seekdb_gis_geohash_operation(instance, context, arguments, argument_count);
}

/* Build the standard WKB collection envelope from already validated geometry
 * slices.  This is intentionally independent of the server's C++ geometry
 * classes: collection construction is one of the plugin's ABI-owned codec
 * responsibilities. */
static seekdb_plugin_status_t gis_collection_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count,
    uint32_t output_type)
{
  if (instance != &gis_instance || !gis_instance.started ||
      NULL == context || context->struct_size != sizeof(*context) ||
      NULL == context->emit_result || NULL == arguments || argument_count == 0 ||
      argument_count > 64 || output_type < 2 || output_type > 7) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  uint32_t srid = 0;
  uint64_t total = 14; /* SRID/version + byte-order/type/count. */
  for (uint32_t i = 0; i < argument_count; ++i) {
    const seekdb_plugin_execution_value_v1_t *value = &arguments[i];
    if (value->struct_size != sizeof(*value) || value->is_null ||
        NULL == value->data || value->data_size < 10 || NULL == value->type_id ||
        0 != strcmp(value->type_id, "org.seekdb.gis.geometry") ||
        value->data[4] != 1 || value->data[5] != 1) {
      return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    }
    if (i == 0) memcpy(&srid, value->data, sizeof(srid));
    if (output_type == 2) {
      const uint32_t child_type = gis_read_u32_le(value->data + 6);
      if ((child_type != 1 && child_type != 1001) ||
          (child_type == 1 && value->data_size != 26) ||
          (child_type == 1001 && value->data_size != 34)) {
        return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
      }
      total += child_type == 1001 ? 24 : 16;
    } else if (output_type == 3) {
      const uint32_t child_type = gis_read_u32_le(value->data + 6);
      if (child_type != 2 && child_type != 1002) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
      if (value->data_size < 14) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
      total += value->data_size - 5;
    } else {
      total += value->data_size - 5;
    }
  }
  if (total > 1024 * 1024) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  uint8_t *geometry = (uint8_t *)malloc((size_t)total);
  if (NULL == geometry) return SEEKDB_PLUGIN_STATUS_NO_MEMORY;
  memset(geometry, 0, (size_t)total);
  memcpy(geometry, &srid, sizeof(srid));
  geometry[4] = 1;
  geometry[5] = 1;
  memcpy(geometry + 6, &output_type, sizeof(output_type));
  memcpy(geometry + 10, &argument_count, sizeof(argument_count));
  uint64_t offset = 14;
  for (uint32_t i = 0; i < argument_count; ++i) {
    const uint8_t *child = arguments[i].data;
    if (output_type == 2) {
      const uint32_t child_type = gis_read_u32_le(child + 6);
      const uint32_t dimensions = child_type == 1001 ? 3 : 2;
      memcpy(geometry + offset, child + 10, dimensions * sizeof(double));
      offset += dimensions * sizeof(double);
    } else if (output_type == 3) {
      /* Polygon stores each input LINESTRING as a ring: point count followed
       * by XY(Z) tuples, without the child byte-order/type header. */
      const uint32_t dimensions = gis_read_u32_le(child + 6) == 1002 ? 3 : 2;
      const uint32_t points = gis_read_u32_le(child + 10);
      memcpy(geometry + offset, &points, sizeof(points));
      offset += sizeof(points);
      const uint64_t bytes = (uint64_t)points * dimensions * sizeof(double);
      if (14 + bytes > arguments[i].data_size) {
        free(geometry);
        return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
      }
      memcpy(geometry + offset, child + 14, (size_t)bytes);
      offset += bytes;
    } else {
      const uint8_t *child_wkb = child + 5;
      const uint64_t child_size = arguments[i].data_size - 5;
      memcpy(geometry + offset, child_wkb, (size_t)child_size);
      offset += child_size;
    }
  }
  const seekdb_plugin_status_t status = emit_geometry_bytes(context, geometry, total);
  free(geometry);
  return status;
}

#define GIS_COLLECTION_EXECUTOR(name, type) \
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL name( \
    seekdb_plugin_instance_handle_t *instance, \
    const seekdb_plugin_execution_context_v1_t *context, \
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t argument_count) \
{ return gis_collection_execute(instance, context, arguments, argument_count, type); }

GIS_COLLECTION_EXECUTOR(gis_st_linestring_execute, 2)
GIS_COLLECTION_EXECUTOR(gis_st_polygon_execute, 3)
GIS_COLLECTION_EXECUTOR(gis_st_multipoint_execute, 4)
GIS_COLLECTION_EXECUTOR(gis_st_multilinestring_execute, 5)
GIS_COLLECTION_EXECUTOR(gis_st_multipolygon_execute, 6)
GIS_COLLECTION_EXECUTOR(gis_st_geometrycollection_execute, 7)

#undef GIS_COLLECTION_EXECUTOR

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_geomfromwkb_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance != &gis_instance || !gis_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  return seekdb_gis_wkb_from_bytes(instance, context, arguments, argument_count);
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_st_setsrid_execute(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t argument_count)
{
  if (instance != &gis_instance || !gis_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  return seekdb_gis_set_srid_operation(instance, context, arguments, argument_count);
}

static uint32_t gis_read_u32_le(const uint8_t *data)
{
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

enum gis_relation_kind {
  GIS_REL_EQUALS = 0,
  GIS_REL_INTERSECTS,
  GIS_REL_CONTAINS,
  GIS_REL_WITHIN,
  GIS_REL_COVERS,
  GIS_REL_TOUCHES,
  GIS_REL_CROSSES,
  GIS_REL_OVERLAPS,
  GIS_REL_DWITHIN
};

#define GIS_RELATION_WRAPPER(name, relation_kind) \
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL name( \
    seekdb_plugin_instance_handle_t *instance, \
    const seekdb_plugin_execution_context_v1_t *context, \
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t argument_count) \
{ \
  if (instance != &gis_instance || !gis_instance.started) { \
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION; \
  } \
  return seekdb_gis_relation_operation((uint32_t)(relation_kind + 1), instance, context, arguments, argument_count); \
}

GIS_RELATION_WRAPPER(gis_st_equals_execute, GIS_REL_EQUALS)
GIS_RELATION_WRAPPER(gis_st_intersects_execute, GIS_REL_INTERSECTS)
GIS_RELATION_WRAPPER(gis_st_contains_execute, GIS_REL_CONTAINS)
GIS_RELATION_WRAPPER(gis_st_within_execute, GIS_REL_WITHIN)
GIS_RELATION_WRAPPER(gis_st_covers_execute, GIS_REL_COVERS)
GIS_RELATION_WRAPPER(gis_st_touches_execute, GIS_REL_TOUCHES)
GIS_RELATION_WRAPPER(gis_st_crosses_execute, GIS_REL_CROSSES)
GIS_RELATION_WRAPPER(gis_st_overlaps_execute, GIS_REL_OVERLAPS)
GIS_RELATION_WRAPPER(gis_st_dwithin_execute, GIS_REL_DWITHIN)

#undef GIS_RELATION_WRAPPER

static const seekdb_plugin_function_service_v1_t gis_st_x_service = {
    sizeof(gis_st_x_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_x_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_y_service = {
    sizeof(gis_st_y_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_y_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

/* Latitude/longitude are coordinate aliases with the same wire contract as
 * ST_Y/ST_X.  Keeping separate service names preserves SQL-level discovery
 * while sharing the ABI-closed implementation. */
static const seekdb_plugin_function_service_v1_t gis_st_latitude_service = {
    sizeof(gis_st_latitude_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_y_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_longitude_service = {
    sizeof(gis_st_longitude_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_x_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_srid_service = {
    sizeof(gis_st_srid_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_srid_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_aswkb_service = {
    sizeof(gis_st_aswkb_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_aswkb_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_asbinary_service = {
    sizeof(gis_st_asbinary_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_aswkb_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_asewkb_service = {
    sizeof(gis_st_asewkb_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_aswkb_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_geometrytype_service = {
    sizeof(gis_geometrytype_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_geometrytype_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_isvalid_service = {
    sizeof(gis_st_isvalid_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, seekdb_gis_valid_operation,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_iscollection_service = {
    sizeof(gis_st_iscollection_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_iscollection_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_numinteriorrings_service = {
    sizeof(gis_st_numinteriorrings_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_numinteriorrings_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_astext_service = {
    sizeof(gis_st_astext_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_astext_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_aswkt_service = {
    sizeof(gis_st_aswkt_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_astext_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_asgeojson_service = {
    sizeof(gis_st_asgeojson_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_asgeojson_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_asewkt_service = {
    sizeof(gis_st_asewkt_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_astext_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_geomfromwkb_service = {
    sizeof(gis_geomfromwkb_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_geomfromwkb_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_geometryfromwkb_service = {
    sizeof(gis_geometryfromwkb_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_geomfromwkb_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_geomfromewkb_service = {
    sizeof(gis_st_geomfromewkb_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_geomfromwkb_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_setsrid_service = {
    sizeof(gis_st_setsrid_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_setsrid_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_geomfromtext_service = {
    sizeof(gis_geomfromtext_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_geomfromtext_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_geometryfromtext_service = {
    sizeof(gis_geometryfromtext_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_geomfromtext_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_geomfromewkt_service = {
    sizeof(gis_st_geomfromewkt_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_geomfromtext_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_area_service = {
    sizeof(gis_st_area_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_area_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_length_service = {
    sizeof(gis_st_length_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_length_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_distance_service = {
    sizeof(gis_st_distance_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_distance_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_distance_sphere_service = {
    sizeof(gis_st_distance_sphere_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_distance_sphere_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_centroid_service = {
    sizeof(gis_st_centroid_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_centroid_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_pointonsurface_service = {
    sizeof(gis_st_pointonsurface_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_pointonsurface_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_geogfromtext_service = {
    sizeof(gis_st_geogfromtext_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_geomfromtext_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_geographyfromtext_service = {
    sizeof(gis_st_geographyfromtext_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_geomfromtext_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

#define GIS_COLLECTION_SERVICE(name, execute_fn) \
static const seekdb_plugin_function_service_v1_t name = { \
    sizeof(name), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR, \
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, execute_fn, \
    {0, 0, 0, 0, 0, 0, 0, 0}};
GIS_COLLECTION_SERVICE(gis_st_linestring_service, gis_st_linestring_execute)
GIS_COLLECTION_SERVICE(gis_st_polygon_service, gis_st_polygon_execute)
GIS_COLLECTION_SERVICE(gis_st_multipoint_service, gis_st_multipoint_execute)
GIS_COLLECTION_SERVICE(gis_st_multilinestring_service, gis_st_multilinestring_execute)
GIS_COLLECTION_SERVICE(gis_st_multipolygon_service, gis_st_multipolygon_execute)
GIS_COLLECTION_SERVICE(gis_st_geometrycollection_service, gis_st_geometrycollection_execute)
GIS_COLLECTION_SERVICE(gis_st_geomcollection_service, gis_st_geometrycollection_execute)
#undef GIS_COLLECTION_SERVICE

static const seekdb_plugin_function_service_v1_t gis_st_transform_service = {
    sizeof(gis_st_transform_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_transform_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_priv_transform_service = {
    sizeof(gis_st_priv_transform_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_priv_transform_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};
static const seekdb_plugin_function_service_v1_t gis_st_buffer_service = {
    sizeof(gis_st_buffer_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_buffer_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};
static const seekdb_plugin_function_service_v1_t gis_st_priv_buffer_service = {
    sizeof(gis_st_priv_buffer_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_priv_buffer_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};
static const seekdb_plugin_function_service_v1_t gis_st_clipbybox2d_service = {
    sizeof(gis_st_clipbybox2d_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_clipbybox2d_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};
static const seekdb_plugin_function_service_v1_t gis_st_union_service = {
    sizeof(gis_st_union_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_union_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};
static const seekdb_plugin_function_service_v1_t gis_st_difference_service = {
    sizeof(gis_st_difference_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_difference_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};
static const seekdb_plugin_function_service_v1_t gis_st_symdifference_service = {
    sizeof(gis_st_symdifference_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_symdifference_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};
static const seekdb_plugin_function_service_v1_t gis_st_asmvtgeom_service = {
    sizeof(gis_st_asmvtgeom_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_asmvtgeom_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};
static const seekdb_plugin_function_service_v1_t gis_st_makevalid_service = {
    sizeof(gis_st_makevalid_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_makevalid_engine_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_buffer_strategy_service = {
    sizeof(gis_st_buffer_strategy_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_buffer_strategy_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};
static const seekdb_plugin_function_service_v1_t gis_st_bestsrid_service = {
    sizeof(gis_st_bestsrid_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_bestsrid_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_function_service_v1_t gis_st_spatial_cellid_service = {
    sizeof(gis_st_spatial_cellid_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_spatial_cellid_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};
static const seekdb_plugin_function_service_v1_t gis_st_spatial_mbr_service = {
    sizeof(gis_st_spatial_mbr_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, seekdb_gis_mbr_operation,
    {0, 0, 0, 0, 0, 0, 0, 0}};
static const seekdb_plugin_function_service_v1_t gis_st_geohash_service = {
    sizeof(gis_st_geohash_service), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR,
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_st_geohash_execute,
    {0, 0, 0, 0, 0, 0, 0, 0}};

#define GIS_RELATION_SERVICE(name, execute_fn) \
static const seekdb_plugin_function_service_v1_t name = { \
    sizeof(name), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR, \
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, execute_fn, \
    {0, 0, 0, 0, 0, 0, 0, 0}};

GIS_RELATION_SERVICE(gis_st_equals_service, gis_st_equals_execute)
GIS_RELATION_SERVICE(gis_st_intersects_service, gis_st_intersects_execute)
GIS_RELATION_SERVICE(gis_st_contains_service, gis_st_contains_execute)
GIS_RELATION_SERVICE(gis_st_within_service, gis_st_within_execute)
GIS_RELATION_SERVICE(gis_st_covers_service, gis_st_covers_execute)
GIS_RELATION_SERVICE(gis_st_touches_service, gis_st_touches_execute)
GIS_RELATION_SERVICE(gis_st_crosses_service, gis_st_crosses_execute)
GIS_RELATION_SERVICE(gis_st_overlaps_service, gis_st_overlaps_execute)
GIS_RELATION_SERVICE(gis_st_dwithin_service, gis_st_dwithin_execute)

#undef GIS_RELATION_SERVICE

#define GIS_RELATION_DESCRIPTOR(service_name, fq_name, sql_name, min_args, max_args) \
    { sizeof(seekdb_plugin_function_descriptor_v1_t), fq_name, sql_name, min_args, max_args, \
      "org.seekdb.gis.scalar.bool", \
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC | SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE | \
          SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING, \
      { sizeof(seekdb_plugin_implementation_ref_v1_t), fq_name, \
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}}, \
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0} }, \
      {0, 0, 0, 0} }

#define GIS_CODEC_DESCRIPTOR(fq_name, sql_name, min_args, max_args, result_type) \
    { sizeof(seekdb_plugin_function_descriptor_v1_t), fq_name, sql_name, min_args, max_args, \
      result_type, SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC | \
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE, \
      { sizeof(seekdb_plugin_implementation_ref_v1_t), fq_name, \
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}}, \
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0} }, \
      {0, 0, 0, 0} }

#define GIS_GEOMETRY_DESCRIPTOR(fq_name, sql_name, min_args, max_args) \
    GIS_CODEC_DESCRIPTOR(fq_name, sql_name, min_args, max_args, "org.seekdb.gis.geometry")

static const seekdb_plugin_function_descriptor_v1_t gis_functions[] = {
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_point",
      "st_point",
      2,
      2,
      "org.seekdb.gis.geometry",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function",
        {
          sizeof(seekdb_plugin_version_range_t),
          {1, 0, 0},
          {2, 0, 0},
          {0, 0}
        },
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE,
        {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_makeenvelope",
      "st_makeenvelope",
      4,
      4,
      "org.seekdb.gis.geometry",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function",
        {
          sizeof(seekdb_plugin_version_range_t),
          {1, 0, 0},
          {2, 0, 0},
          {0, 0}
        },
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE,
        {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_makepoint",
      "st_makepoint",
      2,
      3,
      "org.seekdb.gis.geometry",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function",
        {
          sizeof(seekdb_plugin_version_range_t),
          {1, 0, 0},
          {2, 0, 0},
          {0, 0}
        },
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE,
        {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_linestring", "st_linestring", 1, 64,
                         "org.seekdb.gis.geometry"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_polygon", "st_polygon", 1, 64,
                         "org.seekdb.gis.geometry"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_multipoint", "st_multipoint", 1, 64,
                         "org.seekdb.gis.geometry"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_multilinestring", "st_multilinestring", 1, 64,
                         "org.seekdb.gis.geometry"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_multipolygon", "st_multipolygon", 1, 64,
                         "org.seekdb.gis.geometry"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_geometrycollection", "st_geometrycollection", 1, 64,
                         "org.seekdb.gis.geometry"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_geomcollection", "st_geomcollection", 1, 64,
                         "org.seekdb.gis.geometry"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_transform", "st_transform", 2, 2,
                         "org.seekdb.gis.geometry"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.spatial_cellid", "spatial_cellid", 1, 1,
                         "org.seekdb.gis.scalar.uint64"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.spatial_mbr", "spatial_mbr", 1, 1,
                         "org.seekdb.gis.scalar.bytes"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_geohash", "_st_geohash", 1, 2,
                         "org.seekdb.gis.scalar.bytes"),
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_x", "st_x", 1, 1,
      "org.seekdb.gis.scalar.float64",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE |
          SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_x",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_y", "st_y", 1, 1,
      "org.seekdb.gis.scalar.float64",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE |
          SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_y",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_latitude", "st_latitude", 1, 1,
      "org.seekdb.gis.scalar.float64",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE |
          SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_latitude",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_longitude", "st_longitude", 1, 1,
      "org.seekdb.gis.scalar.float64",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE |
          SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_longitude",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_srid", "st_srid", 1, 1,
      "org.seekdb.gis.scalar.uint32",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE |
          SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_srid",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_aswkb", "st_aswkb", 1, 1,
      "org.seekdb.gis.scalar.bytes",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE |
          SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_aswkb",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_asbinary", "st_asbinary", 1, 1,
      "org.seekdb.gis.scalar.bytes",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE |
          SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_asbinary",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.geometrytype", "_st_geometrytype", 1, 1,
      "org.seekdb.gis.scalar.bytes",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE |
          SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.geometrytype",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_isvalid", "st_isvalid", 1, 1,
      "org.seekdb.gis.scalar.bool",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE |
          SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_isvalid",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_astext", "st_astext", 1, 1,
      "org.seekdb.gis.scalar.bytes",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE |
          SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_astext",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_aswkt", "st_aswkt", 1, 1,
      "org.seekdb.gis.scalar.bytes",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE |
          SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_aswkt",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_geomfromwkb", "st_geomfromwkb", 1, 2,
      "org.seekdb.gis.geometry",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_geomfromwkb",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_geometryfromwkb", "st_geometryfromwkb", 1, 2,
      "org.seekdb.gis.geometry",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_geometryfromwkb",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_setsrid", "_st_setsrid", 2, 2,
      "org.seekdb.gis.geometry",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_setsrid",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_geomfromtext", "st_geomfromtext", 1, 2,
      "org.seekdb.gis.geometry",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_geomfromtext",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_geometryfromtext", "st_geometryfromtext", 1, 2,
      "org.seekdb.gis.geometry",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_geometryfromtext",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_area", "st_area", 1, 1,
      "org.seekdb.gis.scalar.float64",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_area",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_length", "st_length", 1, 1,
      "org.seekdb.gis.scalar.float64",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_length",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_function_descriptor_v1_t),
      "org.seekdb.gis.function.st_distance", "st_distance", 2, 2,
      "org.seekdb.gis.scalar.float64",
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
          SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.function.st_distance",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
      },
      {0, 0, 0, 0}
    },
    GIS_RELATION_DESCRIPTOR(gis_st_equals_service, "org.seekdb.gis.function.st_equals", "st_equals", 2, 2),
    GIS_RELATION_DESCRIPTOR(gis_st_intersects_service, "org.seekdb.gis.function.st_intersects", "st_intersects", 2, 2),
    GIS_RELATION_DESCRIPTOR(gis_st_contains_service, "org.seekdb.gis.function.st_contains", "st_contains", 2, 2),
    GIS_RELATION_DESCRIPTOR(gis_st_within_service, "org.seekdb.gis.function.st_within", "st_within", 2, 2),
    GIS_RELATION_DESCRIPTOR(gis_st_covers_service, "org.seekdb.gis.function.st_covers", "st_covers", 2, 2),
    GIS_RELATION_DESCRIPTOR(gis_st_touches_service, "org.seekdb.gis.function.st_touches", "st_touches", 2, 2),
    GIS_RELATION_DESCRIPTOR(gis_st_crosses_service, "org.seekdb.gis.function.st_crosses", "st_crosses", 2, 2),
    GIS_RELATION_DESCRIPTOR(gis_st_overlaps_service, "org.seekdb.gis.function.st_overlaps", "st_overlaps", 2, 2),
    GIS_RELATION_DESCRIPTOR(gis_st_dwithin_service, "org.seekdb.gis.function.st_dwithin", "st_dwithin", 3, 3),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_asewkb", "_st_asewkb", 1, 1,
                         "org.seekdb.gis.scalar.bytes"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_asewkt", "_st_asewkt", 1, 1,
                         "org.seekdb.gis.scalar.bytes"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_geomfromewkb", "_st_geomfromewkb", 1, 2,
                         "org.seekdb.gis.geometry"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_geomfromewkt", "_st_geomfromewkt", 1, 2,
                         "org.seekdb.gis.geometry"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_geogfromtext", "_st_geogfromtext", 1, 2,
                         "org.seekdb.gis.geometry"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_geographyfromtext", "_st_geographyfromtext", 1, 2,
                         "org.seekdb.gis.geometry"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_iscollection", "_st_iscollection", 1, 1,
                         "org.seekdb.gis.scalar.bool"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_numinteriorrings", "_st_numinteriorrings", 1, 1,
                         "org.seekdb.gis.scalar.int32"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_asgeojson", "st_asgeojson", 1, 3,
                         "org.seekdb.gis.scalar.bytes"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_distance_sphere", "st_distance_sphere", 2, 3,
                         "org.seekdb.gis.scalar.float64"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_centroid", "st_centroid", 1, 1,
                         "org.seekdb.gis.geometry"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_pointonsurface", "_st_pointonsurface", 1, 1,
                         "org.seekdb.gis.geometry"),
    GIS_GEOMETRY_DESCRIPTOR("org.seekdb.gis.function.st_priv_transform", "_st_transform", 2, 3),
    GIS_GEOMETRY_DESCRIPTOR("org.seekdb.gis.function.st_buffer", "st_buffer", 2, 8),
    GIS_GEOMETRY_DESCRIPTOR("org.seekdb.gis.function.st_priv_buffer", "_st_buffer", 2, 8),
    GIS_GEOMETRY_DESCRIPTOR("org.seekdb.gis.function.st_clipbybox2d", "_st_clipbybox2d", 2, 2),
    GIS_GEOMETRY_DESCRIPTOR("org.seekdb.gis.function.st_union", "st_union", 2, 2),
    GIS_GEOMETRY_DESCRIPTOR("org.seekdb.gis.function.st_difference", "st_difference", 2, 2),
    GIS_GEOMETRY_DESCRIPTOR("org.seekdb.gis.function.st_symdifference", "st_symdifference", 2, 2),
    GIS_GEOMETRY_DESCRIPTOR("org.seekdb.gis.function.st_asmvtgeom", "_st_asmvtgeom", 2, 5),
    GIS_GEOMETRY_DESCRIPTOR("org.seekdb.gis.function.st_makevalid", "_st_makevalid", 1, 2),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_buffer_strategy", "st_buffer_strategy", 1, 2,
                         "org.seekdb.gis.scalar.bytes"),
    GIS_CODEC_DESCRIPTOR("org.seekdb.gis.function.st_bestsrid", "_st_bestsrid", 1, 2,
                         "org.seekdb.gis.scalar.int32")
};

#undef GIS_RELATION_DESCRIPTOR
#undef GIS_CODEC_DESCRIPTOR
#undef GIS_GEOMETRY_DESCRIPTOR

static const seekdb_plugin_extension_snapshot_v1_t gis_snapshot = {
    sizeof(seekdb_plugin_extension_snapshot_v1_t),
    &(const seekdb_plugin_type_descriptor_v1_t){
      sizeof(seekdb_plugin_type_descriptor_v1_t),
      "org.seekdb.gis.type.geometry",
      "geometry",
      "org.seekdb.gis.wkb.v1",
      1,
      0,
      SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT,
      {
        sizeof(seekdb_plugin_implementation_ref_v1_t),
        "org.seekdb.gis.codec",
        {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}},
        SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE,
        {0, 0, 0, 0}
      },
      {0, 0, 0, 0}},
    1, sizeof(seekdb_plugin_type_descriptor_v1_t),
    gis_functions, sizeof(gis_functions) / sizeof(gis_functions[0]),
    sizeof(gis_functions),
    NULL, 0, 0,
    NULL, 0, 0,
    NULL, 0, 0,
    NULL, 0, 0,
    NULL, 0, 0,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_describe_extensions(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_extension_snapshot_v1_t **out_snapshot)
{
  if (instance != &gis_instance || NULL == out_snapshot ||
      !gis_instance.started) {
    return SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION;
  }
  *out_snapshot = &gis_snapshot;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static const seekdb_plugin_extension_catalog_service_v1_t gis_catalog_service = {
    sizeof(gis_catalog_service),
    gis_describe_extensions,
    {0, 0, 0, 0, 0, 0, 0, 0}};

static const seekdb_plugin_service_provide_descriptor_t gis_services[] = {
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function",
      {1, 0, 0},
      &gis_function_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE,
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.extensions",
      {1, 0, 0},
      &gis_catalog_service,
      SEEKDB_PLUGIN_CAPABILITY_EXTENSION_CATALOG,
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.codec",
      {1, 0, 0},
      &gis_geometry_codec_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE,
      {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_x", {1, 0, 0}, &gis_st_x_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_y", {1, 0, 0}, &gis_st_y_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_latitude", {1, 0, 0}, &gis_st_latitude_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_longitude", {1, 0, 0}, &gis_st_longitude_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_linestring", {1, 0, 0}, &gis_st_linestring_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_polygon", {1, 0, 0}, &gis_st_polygon_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_multipoint", {1, 0, 0}, &gis_st_multipoint_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_multilinestring", {1, 0, 0}, &gis_st_multilinestring_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_multipolygon", {1, 0, 0}, &gis_st_multipolygon_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_geometrycollection", {1, 0, 0}, &gis_st_geometrycollection_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_geomcollection", {1, 0, 0}, &gis_st_geomcollection_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_transform", {1, 0, 0}, &gis_st_transform_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_priv_transform", {1, 0, 0}, &gis_st_priv_transform_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_buffer", {1, 0, 0}, &gis_st_buffer_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_priv_buffer", {1, 0, 0}, &gis_st_priv_buffer_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_clipbybox2d", {1, 0, 0}, &gis_st_clipbybox2d_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_union", {1, 0, 0}, &gis_st_union_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_difference", {1, 0, 0}, &gis_st_difference_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_symdifference", {1, 0, 0}, &gis_st_symdifference_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_asmvtgeom", {1, 0, 0}, &gis_st_asmvtgeom_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_makevalid", {1, 0, 0}, &gis_st_makevalid_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_buffer_strategy", {1, 0, 0}, &gis_st_buffer_strategy_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_bestsrid", {1, 0, 0}, &gis_st_bestsrid_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.spatial_cellid", {1, 0, 0}, &gis_st_spatial_cellid_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.spatial_mbr", {1, 0, 0}, &gis_st_spatial_mbr_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_geohash", {1, 0, 0}, &gis_st_geohash_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_srid", {1, 0, 0}, &gis_st_srid_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_aswkb", {1, 0, 0}, &gis_st_aswkb_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_asbinary", {1, 0, 0}, &gis_st_asbinary_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_asewkb", {1, 0, 0}, &gis_st_asewkb_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.geometrytype", {1, 0, 0}, &gis_geometrytype_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_isvalid", {1, 0, 0}, &gis_st_isvalid_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_iscollection", {1, 0, 0}, &gis_st_iscollection_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_numinteriorrings", {1, 0, 0}, &gis_st_numinteriorrings_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_astext", {1, 0, 0}, &gis_st_astext_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_aswkt", {1, 0, 0}, &gis_st_aswkt_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_asgeojson", {1, 0, 0}, &gis_st_asgeojson_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_asewkt", {1, 0, 0}, &gis_st_asewkt_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_geomfromwkb", {1, 0, 0}, &gis_geomfromwkb_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_geometryfromwkb", {1, 0, 0}, &gis_geometryfromwkb_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_geomfromewkb", {1, 0, 0}, &gis_st_geomfromewkb_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_setsrid", {1, 0, 0}, &gis_st_setsrid_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_geomfromtext", {1, 0, 0}, &gis_geomfromtext_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_geometryfromtext", {1, 0, 0}, &gis_geometryfromtext_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_geomfromewkt", {1, 0, 0}, &gis_st_geomfromewkt_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_geogfromtext", {1, 0, 0}, &gis_st_geogfromtext_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_geographyfromtext", {1, 0, 0}, &gis_st_geographyfromtext_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_area", {1, 0, 0}, &gis_st_area_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_length", {1, 0, 0}, &gis_st_length_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_distance", {1, 0, 0}, &gis_st_distance_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_distance_sphere", {1, 0, 0}, &gis_st_distance_sphere_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_centroid", {1, 0, 0}, &gis_st_centroid_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_pointonsurface", {1, 0, 0}, &gis_st_pointonsurface_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_equals", {1, 0, 0}, &gis_st_equals_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_intersects", {1, 0, 0}, &gis_st_intersects_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_contains", {1, 0, 0}, &gis_st_contains_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_within", {1, 0, 0}, &gis_st_within_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_covers", {1, 0, 0}, &gis_st_covers_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_touches", {1, 0, 0}, &gis_st_touches_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_crosses", {1, 0, 0}, &gis_st_crosses_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_overlaps", {1, 0, 0}, &gis_st_overlaps_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    },
    {
      sizeof(seekdb_plugin_service_provide_descriptor_t),
      "org.seekdb.gis.function.st_dwithin", {1, 0, 0}, &gis_st_dwithin_service,
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0}
    }
};

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_init(
    const seekdb_plugin_host_api_v1_t *host_api,
    seekdb_plugin_instance_handle_t **out_instance)
{
  if (NULL == host_api || NULL == out_instance) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  gis_instance.started = 0;
  *out_instance = &gis_instance;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_start(
    seekdb_plugin_instance_handle_t *instance)
{
  if (instance != &gis_instance) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  gis_instance.started = 1;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_stop(
    seekdb_plugin_instance_handle_t *instance)
{
  if (instance != &gis_instance) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  gis_instance.started = 0;
  return SEEKDB_PLUGIN_STATUS_OK;
}

static void SEEKDB_PLUGIN_CALL gis_deinit(
    seekdb_plugin_instance_handle_t *instance)
{
  if (instance == &gis_instance) gis_instance.started = 0;
}

static const seekdb_plugin_manifest_v1_t gis_manifest = {
    sizeof(seekdb_plugin_manifest_v1_t),
    SEEKDB_PLUGIN_ABI_MAJOR,
    SEEKDB_PLUGIN_ABI_MINOR,
    "org.seekdb.gis",
    "seekdb",
    {1, 0, 0},
    "gis-execution-spi-v1",
    1,
    1,
    SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE,
    gis_services,
    sizeof(gis_services) / sizeof(gis_services[0]),
    NULL,
    0,
    gis_init,
    gis_start,
    gis_stop,
    gis_deinit,
    {0, 0, 0, 0, 0, 0, 0, 0}};

SEEKDB_PLUGIN_EXPORT const seekdb_plugin_manifest_v1_t *SEEKDB_PLUGIN_CALL
seekdb_plugin_entry_v1(void)
{
  return &gis_manifest;
}
