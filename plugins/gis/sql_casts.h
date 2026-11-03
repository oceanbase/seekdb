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

#pragma once

#include <errno.h>
#include <math.h>

/* Private GIS SQL coercions, declared as ordinary cast objects. No ObDatum or
 * other kernel representation crosses this boundary. The loader validates and
 * leases the selected casts together with the function before calling them. */
static int gis_sql_number(const seekdb_plugin_execution_value_v1_t *value, double *out)
{
  const char *type = value->type_id;
  if (!type || !value->data) return 0;
  if (!strcmp(type, "core.type.float64") || !strcmp(type, GIS_DOUBLE)) {
    if (value->data_size != sizeof(double)) return 0;
    memcpy(out, value->data, sizeof(double));
  } else if (!strcmp(type, "core.type.int64") || !strcmp(type, "org.seekdb.gis.scalar.int64")) {
    int64_t number;
    if (value->data_size != sizeof(number)) return 0;
    memcpy(&number, value->data, sizeof(number));
    *out = (double)number;
  } else if (!strcmp(type, "core.type.uint64") || !strcmp(type, "org.seekdb.gis.scalar.uint64")) {
    uint64_t number;
    if (value->data_size != sizeof(number)) return 0;
    memcpy(&number, value->data, sizeof(number));
    *out = (double)number;
  } else if (!strcmp(type, "org.seekdb.gis.scalar.int32")) {
    int32_t number;
    if (value->data_size != sizeof(number)) return 0;
    memcpy(&number, value->data, sizeof(number));
    *out = number;
  } else if (!strcmp(type, GIS_UINT32)) {
    uint32_t number;
    if (value->data_size != sizeof(number)) return 0;
    memcpy(&number, value->data, sizeof(number));
    *out = number;
  } else if (!strcmp(type, "core.type.bytes") || !strcmp(type, "core.type.text") || !strcmp(type, "core.type.decimal") ||
             !strcmp(type, GIS_BYTES)) {
    char buffer[256];
    char *end = NULL;
    if (!value->data_size || value->data_size >= sizeof(buffer) ||
        memchr(value->data, 0, value->data_size)) return 0;
    memcpy(buffer, value->data, value->data_size);
    buffer[value->data_size] = 0;
    errno = 0;
    *out = strtod(buffer, &end);
    if (end == buffer || errno == ERANGE) return 0;
    while (*end && isspace((unsigned char)*end)) ++end;
    if (*end) return 0;
  } else {
    return 0;
  }
  return isfinite(*out);
}

static seekdb_plugin_status_t gis_sql_cast(
    seekdb_plugin_instance_handle_t *instance,
    const seekdb_plugin_execution_context_v1_t *context,
    const seekdb_plugin_execution_value_v1_t *arguments,
    uint32_t count, const char *target)
{
  if (instance != &gis_instance || !gis_instance.started || !context ||
      context->struct_size < sizeof(*context) || !context->emit_result ||
      count != 1 || !arguments || arguments[0].struct_size < sizeof(arguments[0])) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  const seekdb_plugin_execution_value_v1_t *input = arguments;
  seekdb_plugin_execution_result_v1_t output = {0};
  output.struct_size = sizeof(output);
  output.type_id = target;
  output.is_null = input->is_null;
  double number = 0;
  uint32_t integer = 0;
  if (!input->is_null) {
    if (!input->type_id || (input->data_size && !input->data)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
    if (!strcmp(target, GIS_DOUBLE) || !strcmp(target, GIS_UINT32)) {
      if (!gis_sql_number(input, &number)) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
      if (!strcmp(target, GIS_UINT32)) {
        if (number < 0 || number > UINT32_MAX) return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
        integer = (uint32_t)number;
        output.data = (const uint8_t *)&integer;
        output.data_size = sizeof(integer);
      } else {
        output.data = (const uint8_t *)&number;
        output.data_size = sizeof(number);
      }
    } else {
      if (!strcmp(target, GIS_GEOMETRY) && SEEKDB_PLUGIN_STATUS_OK !=
          seekdb_gis_validate_encoded_geometry(instance, input->data, input->data_size)) {
        return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
      }
      output.data = input->data;
      output.data_size = input->data_size;
    }
  }
  return context->emit_result(context->host, &output);
}

#define GIS_CAST_SERVICE(suffix, target) \
static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL gis_cast_##suffix( \
    seekdb_plugin_instance_handle_t *instance, const seekdb_plugin_execution_context_v1_t *context, \
    const seekdb_plugin_execution_value_v1_t *arguments, uint32_t count) \
{ return gis_sql_cast(instance, context, arguments, count, target); } \
static const seekdb_plugin_function_service_v1_t gis_cast_##suffix##_service = { \
    sizeof(seekdb_plugin_function_service_v1_t), SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR, \
    SEEKDB_PLUGIN_EXECUTION_SPI_MINOR, 0, gis_cast_##suffix, {0, 0, 0, 0, 0, 0, 0, 0} };

GIS_CAST_SERVICE(geometry, GIS_GEOMETRY)
GIS_CAST_SERVICE(bytes, GIS_BYTES)
GIS_CAST_SERVICE(number, GIS_DOUBLE)
GIS_CAST_SERVICE(uint32, GIS_UINT32)
#undef GIS_CAST_SERVICE

#define GIS_CAST(id, source, target, service) \
  { sizeof(seekdb_plugin_cast_descriptor_v1_t), "org.seekdb.gis.cast." id, source, target, \
    SEEKDB_PLUGIN_CAST_IMPLICIT, 1, SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE | \
        SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC | SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING, \
    { sizeof(seekdb_plugin_implementation_ref_v1_t), "org.seekdb.gis.cast." service, \
      {sizeof(seekdb_plugin_version_range_t), {1, 0, 0}, {2, 0, 0}, {0, 0}}, \
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0} }, {0, 0, 0, 0} }
#define GIS_NUMERIC_CASTS(id, source) \
  GIS_CAST(id "_double", source, GIS_DOUBLE, "number"), \
  GIS_CAST(id "_uint32", source, GIS_UINT32, "uint32")

static const seekdb_plugin_cast_descriptor_v1_t gis_casts[] = {
  GIS_CAST("geometry", "core.type.geometry", GIS_GEOMETRY, "geometry"),
  GIS_CAST("encoded_geometry", "core.type.bytes", GIS_GEOMETRY, "geometry"),
  GIS_CAST("gis_encoded_geometry", GIS_BYTES, GIS_GEOMETRY, "geometry"),
  GIS_CAST("bytes", "core.type.bytes", GIS_BYTES, "bytes"),
  GIS_CAST("text_bytes", "core.type.text", GIS_BYTES, "bytes"),
  GIS_CAST("blob_bytes", "core.type.blob", GIS_BYTES, "bytes"),
  GIS_CAST("blob_geometry", "core.type.blob", GIS_GEOMETRY, "geometry"),
  GIS_NUMERIC_CASTS("longtext", "core.type.text"),
  GIS_NUMERIC_CASTS("int64", "core.type.int64"),
  GIS_NUMERIC_CASTS("uint64", "core.type.uint64"),
  GIS_NUMERIC_CASTS("float64", "core.type.float64"),
  GIS_NUMERIC_CASTS("decimal", "core.type.decimal"),
  GIS_NUMERIC_CASTS("text", "core.type.bytes"),
  GIS_NUMERIC_CASTS("gis_text", GIS_BYTES),
  GIS_NUMERIC_CASTS("gis_int32", "org.seekdb.gis.scalar.int32"),
  GIS_NUMERIC_CASTS("gis_int64", "org.seekdb.gis.scalar.int64"),
  GIS_NUMERIC_CASTS("gis_uint64", "org.seekdb.gis.scalar.uint64"),
  GIS_CAST("gis_double_uint32", GIS_DOUBLE, GIS_UINT32, "uint32"),
  GIS_CAST("gis_uint32_double", GIS_UINT32, GIS_DOUBLE, "number"),
};
#undef GIS_NUMERIC_CASTS
#undef GIS_CAST

#define GIS_CAST_PROVIDES(suffix) \
  { sizeof(seekdb_plugin_service_provide_descriptor_t), "org.seekdb.gis.cast." #suffix, \
    {1, 0, 0}, &gis_cast_##suffix##_service, SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE, {0, 0, 0, 0} }
