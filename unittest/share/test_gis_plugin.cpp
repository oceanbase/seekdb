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

#include <cstring>
#include <cmath>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "seekdb/plugin/execution_spi.h"
#include "seekdb/plugin/extension_spi.h"
#include "seekdb/plugin/seekdb_plugin_abi.h"

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

#ifndef SEEKDB_TEST_GIS_PLUGIN_DIR
#error "SEEKDB_TEST_GIS_PLUGIN_DIR must name the GIS plugin directory"
#endif

#ifndef SEEKDB_TEST_GIS_PLUGIN_FILE
#error "SEEKDB_TEST_GIS_PLUGIN_FILE must name the GIS plugin file"
#endif

namespace
{

struct ResultSink
{
  uint8_t data_[16384];
  uint64_t data_size_;
};

seekdb_plugin_status_t SEEKDB_PLUGIN_CALL capture_result(
    seekdb_plugin_host_handle_t *host,
    const seekdb_plugin_execution_result_v1_t *result)
{
  if (nullptr == host || nullptr == result) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  ResultSink *sink = reinterpret_cast<ResultSink *>(host);
  if (result->data_size > sizeof(sink->data_) || nullptr == result->data) {
    return SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT;
  }
  std::memcpy(sink->data_, result->data, static_cast<size_t>(result->data_size));
  sink->data_size_ = result->data_size;
  return SEEKDB_PLUGIN_STATUS_OK;
}

TEST(GisPlugin, StPointExecutionAbi)
{
#if defined(_WIN32)
  GTEST_SKIP() << "GIS plugin ABI smoke test currently targets POSIX loader";
#else
  const std::string path = std::string(SEEKDB_TEST_GIS_PLUGIN_DIR) + "/" +
      SEEKDB_TEST_GIS_PLUGIN_FILE;
  void *handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  ASSERT_NE(nullptr, handle) << (::dlerror() == nullptr ? "dlopen failed" : ::dlerror());
  auto *entry = reinterpret_cast<seekdb_plugin_entry_v1_fn>(
      ::dlsym(handle, "seekdb_plugin_entry_v1"));
  ASSERT_NE(nullptr, entry);
  const seekdb_plugin_manifest_v1_t *manifest = entry();
  ASSERT_NE(nullptr, manifest);
  ASSERT_STREQ("org.seekdb.gis", manifest->plugin_id);
  ASSERT_EQ(65u, manifest->provides_count);

  seekdb_plugin_host_api_v1_t host_api = {};
  host_api.struct_size = sizeof(host_api);
  host_api.abi_major = SEEKDB_PLUGIN_ABI_MAJOR;
  host_api.abi_minor = SEEKDB_PLUGIN_ABI_MINOR;
  seekdb_plugin_instance_handle_t *instance = nullptr;
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK, manifest->init(&host_api, &instance));
  ASSERT_NE(nullptr, instance);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK, manifest->start(instance));

  const seekdb_plugin_function_service_v1_t *service = nullptr;
  for (uint32_t i = 0; i < manifest->provides_count; ++i) {
    const auto &provided = manifest->provides[i];
    if (0 == std::strcmp("org.seekdb.gis.function", provided.service_id)) {
      service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    }
  }
  ASSERT_NE(nullptr, service);
  ASSERT_EQ(sizeof(*service), service->struct_size);
  ASSERT_EQ(SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR, service->spi_major);

  const seekdb_plugin_extension_catalog_service_v1_t *catalog = nullptr;
  for (uint32_t i = 0; i < manifest->provides_count; ++i) {
    const auto &provided = manifest->provides[i];
    if (0 == std::strcmp("org.seekdb.gis.extensions", provided.service_id)) {
      catalog = reinterpret_cast<const seekdb_plugin_extension_catalog_service_v1_t *>(
          provided.service);
    }
  }
  ASSERT_NE(nullptr, catalog);
  const seekdb_plugin_extension_snapshot_v1_t *snapshot = nullptr;
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK, catalog->describe_extensions(instance, &snapshot));
  ASSERT_NE(nullptr, snapshot);
  ASSERT_EQ(1u, snapshot->type_count);
  ASSERT_EQ(sizeof(seekdb_plugin_type_descriptor_v1_t), snapshot->type_bytes);
  ASSERT_STREQ("geometry", snapshot->types[0].sql_name);
  ASSERT_STREQ("org.seekdb.gis.wkb.v1", snapshot->types[0].physical_format_id);
  ASSERT_EQ(65u, snapshot->function_count);
  ASSERT_EQ(sizeof(seekdb_plugin_function_descriptor_v1_t) * 65u,
            snapshot->function_bytes);

  const double x = 12.5;
  const double y = -3.25;
  seekdb_plugin_execution_value_v1_t args[2] = {};
  for (auto &arg : args) {
    arg.struct_size = sizeof(arg);
    arg.type_id = "org.seekdb.gis.scalar.float64";
    arg.data_size = sizeof(double);
  }
  args[0].data = reinterpret_cast<const uint8_t *>(&x);
  args[1].data = reinterpret_cast<const uint8_t *>(&y);
  ResultSink sink = {{0}, 0};
  seekdb_plugin_execution_context_v1_t context = {};
  context.struct_size = sizeof(context);
  context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.emit_result = capture_result;
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            service->execute(instance, &context, args, 2));
  ASSERT_EQ(26u, sink.data_size_);
  ASSERT_EQ(0, std::memcmp(sink.data_ + 10, &x, sizeof(x)));
  ASSERT_EQ(0, std::memcmp(sink.data_ + 18, &y, sizeof(y)));

  const seekdb_plugin_function_service_v1_t *x_service = nullptr;
  const seekdb_plugin_function_service_v1_t *y_service = nullptr;
  const seekdb_plugin_function_service_v1_t *latitude_service = nullptr;
  const seekdb_plugin_function_service_v1_t *longitude_service = nullptr;
  const seekdb_plugin_function_service_v1_t *linestring_service = nullptr;
  const seekdb_plugin_function_service_v1_t *transform_service = nullptr;
  for (uint32_t i = 0; i < manifest->provides_count; ++i) {
    const auto &provided = manifest->provides[i];
    if (0 == std::strcmp("org.seekdb.gis.function.st_x", provided.service_id)) {
      x_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_y", provided.service_id)) {
      y_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_latitude", provided.service_id)) {
      latitude_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_longitude", provided.service_id)) {
      longitude_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_linestring", provided.service_id)) {
      linestring_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_transform", provided.service_id)) {
      transform_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    }
  }
  ASSERT_NE(nullptr, x_service);
  ASSERT_NE(nullptr, y_service);
  ASSERT_NE(nullptr, latitude_service);
  ASSERT_NE(nullptr, longitude_service);
  ASSERT_NE(nullptr, linestring_service);
  ASSERT_NE(nullptr, transform_service);
  seekdb_plugin_execution_value_v1_t geometry_arg = {};
  geometry_arg.struct_size = sizeof(geometry_arg);
  geometry_arg.type_id = "org.seekdb.gis.geometry";
  geometry_arg.data = sink.data_;
  geometry_arg.data_size = sink.data_size_;
  ResultSink scalar_sink = {{0}, 0};
  seekdb_plugin_execution_context_v1_t scalar_context = {};
  scalar_context.struct_size = sizeof(scalar_context);
  scalar_context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&scalar_sink);
  scalar_context.emit_result = capture_result;
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            x_service->execute(instance, &scalar_context, &geometry_arg, 1));
  ASSERT_EQ(sizeof(double), scalar_sink.data_size_);
  ASSERT_EQ(0, std::memcmp(scalar_sink.data_, &x, sizeof(x)));

  seekdb_plugin_execution_value_v1_t collection_args[2] = {geometry_arg, geometry_arg};
  ResultSink collection_sink = {{0}, 0};
  seekdb_plugin_execution_context_v1_t collection_context = {};
  collection_context.struct_size = sizeof(collection_context);
  collection_context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&collection_sink);
  collection_context.emit_result = capture_result;
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            linestring_service->execute(instance, &collection_context, collection_args, 2));
  ASSERT_EQ(46u, collection_sink.data_size_);
  ASSERT_EQ(2u, *reinterpret_cast<const uint32_t *>(collection_sink.data_ + 10));

  const uint32_t target_srid = 3857;
  seekdb_plugin_execution_value_v1_t transform_args[2] = {geometry_arg, {}};
  transform_args[1].struct_size = sizeof(transform_args[1]);
  transform_args[1].type_id = "org.seekdb.gis.scalar.uint32";
  transform_args[1].data = reinterpret_cast<const uint8_t *>(&target_srid);
  transform_args[1].data_size = sizeof(target_srid);
  ResultSink transform_sink = {{0}, 0};
  seekdb_plugin_execution_context_v1_t transform_context = {};
  transform_context.struct_size = sizeof(transform_context);
  transform_context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&transform_sink);
  transform_context.emit_result = capture_result;
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            transform_service->execute(instance, &transform_context, transform_args, 2));
  ASSERT_EQ(target_srid, *reinterpret_cast<const uint32_t *>(transform_sink.data_));
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            y_service->execute(instance, &scalar_context, &geometry_arg, 1));
  ASSERT_EQ(sizeof(double), scalar_sink.data_size_);
  ASSERT_EQ(0, std::memcmp(scalar_sink.data_, &y, sizeof(y)));
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            latitude_service->execute(instance, &scalar_context, &geometry_arg, 1));
  ASSERT_EQ(sizeof(double), scalar_sink.data_size_);
  ASSERT_EQ(0, std::memcmp(scalar_sink.data_, &y, sizeof(y)));
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            longitude_service->execute(instance, &scalar_context, &geometry_arg, 1));
  ASSERT_EQ(sizeof(double), scalar_sink.data_size_);
  ASSERT_EQ(0, std::memcmp(scalar_sink.data_, &x, sizeof(x)));

  const seekdb_plugin_function_service_v1_t *srid_service = nullptr;
  const seekdb_plugin_function_service_v1_t *aswkb_service = nullptr;
  const seekdb_plugin_function_service_v1_t *asbinary_service = nullptr;
  const seekdb_plugin_function_service_v1_t *geometrytype_service = nullptr;
  const seekdb_plugin_function_service_v1_t *isvalid_service = nullptr;
  const seekdb_plugin_function_service_v1_t *astext_service = nullptr;
  const seekdb_plugin_function_service_v1_t *aswkt_service = nullptr;
  const seekdb_plugin_function_service_v1_t *geomfromwkb_service = nullptr;
  const seekdb_plugin_function_service_v1_t *geometryfromwkb_service = nullptr;
  const seekdb_plugin_function_service_v1_t *setsrid_service = nullptr;
  const seekdb_plugin_function_service_v1_t *geomfromtext_service = nullptr;
  const seekdb_plugin_function_service_v1_t *geometryfromtext_service = nullptr;
  const seekdb_plugin_function_service_v1_t *area_service = nullptr;
  const seekdb_plugin_function_service_v1_t *length_service = nullptr;
  const seekdb_plugin_function_service_v1_t *distance_service = nullptr;
  for (uint32_t i = 0; i < manifest->provides_count; ++i) {
    const auto &provided = manifest->provides[i];
    if (0 == std::strcmp("org.seekdb.gis.function.st_srid", provided.service_id)) {
      srid_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_aswkb", provided.service_id)) {
      aswkb_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_asbinary", provided.service_id)) {
      asbinary_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.geometrytype", provided.service_id)) {
      geometrytype_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_isvalid", provided.service_id)) {
      isvalid_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_astext", provided.service_id)) {
      astext_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_aswkt", provided.service_id)) {
      aswkt_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_geomfromwkb", provided.service_id)) {
      geomfromwkb_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_geometryfromwkb", provided.service_id)) {
      geometryfromwkb_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_setsrid", provided.service_id)) {
      setsrid_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_geomfromtext", provided.service_id)) {
      geomfromtext_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_geometryfromtext", provided.service_id)) {
      geometryfromtext_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_area", provided.service_id)) {
      area_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_length", provided.service_id)) {
      length_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    } else if (0 == std::strcmp("org.seekdb.gis.function.st_distance", provided.service_id)) {
      distance_service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
          provided.service);
    }
  }
  ASSERT_NE(nullptr, srid_service);
  sink.data_[0] = 0xe6;
  sink.data_[1] = 0x10;
  sink.data_[2] = 0;
  sink.data_[3] = 0;
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            srid_service->execute(instance, &scalar_context, &geometry_arg, 1));
  ASSERT_EQ(sizeof(uint32_t), scalar_sink.data_size_);
  uint32_t srid = 0;
  std::memcpy(&srid, scalar_sink.data_, sizeof(srid));
  ASSERT_EQ(4326u, srid);

  ASSERT_NE(nullptr, aswkb_service);
  ASSERT_NE(nullptr, asbinary_service);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            aswkb_service->execute(instance, &scalar_context, &geometry_arg, 1));
  ASSERT_EQ(21u, scalar_sink.data_size_);
  ASSERT_EQ(1, scalar_sink.data_[0]);
  ASSERT_EQ(1, scalar_sink.data_[1]);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            asbinary_service->execute(instance, &scalar_context, &geometry_arg, 1));
  ASSERT_EQ(21u, scalar_sink.data_size_);
  ASSERT_EQ(0, std::memcmp(scalar_sink.data_, sink.data_ + 5, 21));
  ASSERT_NE(nullptr, geometrytype_service);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            geometrytype_service->execute(instance, &scalar_context, &geometry_arg, 1));
  ASSERT_EQ(5u, scalar_sink.data_size_);
  ASSERT_EQ(0, std::memcmp(scalar_sink.data_, "POINT", 5));
  ASSERT_NE(nullptr, isvalid_service);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            isvalid_service->execute(instance, &scalar_context, &geometry_arg, 1));
  ASSERT_EQ(sizeof(uint8_t), scalar_sink.data_size_);
  ASSERT_EQ(1u, scalar_sink.data_[0]);
  ASSERT_NE(nullptr, astext_service);
  ASSERT_NE(nullptr, aswkt_service);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            astext_service->execute(instance, &scalar_context, &geometry_arg, 1));
  const std::string expected_wkt = "POINT(12.5 -3.25)";
  ASSERT_EQ(expected_wkt.size(), scalar_sink.data_size_);
  ASSERT_EQ(0, std::memcmp(scalar_sink.data_, expected_wkt.data(), expected_wkt.size()));
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            aswkt_service->execute(instance, &scalar_context, &geometry_arg, 1));
  ASSERT_EQ(expected_wkt.size(), scalar_sink.data_size_);
  ASSERT_EQ(0, std::memcmp(scalar_sink.data_, expected_wkt.data(), expected_wkt.size()));

  ASSERT_NE(nullptr, geomfromwkb_service);
  ASSERT_NE(nullptr, geometryfromwkb_service);
  seekdb_plugin_execution_value_v1_t wkb_arg = {};
  uint8_t standard_wkb[21] = {0};
  std::memcpy(standard_wkb, sink.data_ + 5, sizeof(standard_wkb));
  wkb_arg.struct_size = sizeof(wkb_arg);
  wkb_arg.type_id = "org.seekdb.gis.scalar.bytes";
  wkb_arg.data = standard_wkb;
  wkb_arg.data_size = sizeof(standard_wkb);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            geomfromwkb_service->execute(instance, &context, &wkb_arg, 1));
  ASSERT_EQ(26u, sink.data_size_);
  ASSERT_EQ(1, sink.data_[4]);
  ASSERT_EQ(1, sink.data_[5]);
  uint32_t input_srid = 4326;
  seekdb_plugin_execution_value_v1_t srid_arg = {};
  srid_arg.struct_size = sizeof(srid_arg);
  srid_arg.type_id = "org.seekdb.gis.scalar.uint32";
  srid_arg.data = reinterpret_cast<const uint8_t *>(&input_srid);
  srid_arg.data_size = sizeof(input_srid);
  seekdb_plugin_execution_value_v1_t wkb_args[2] = {wkb_arg, srid_arg};
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            geometryfromwkb_service->execute(instance, &context, wkb_args, 2));
  ASSERT_EQ(26u, sink.data_size_);
  uint32_t output_srid = 0;
  std::memcpy(&output_srid, sink.data_, sizeof(output_srid));
  ASSERT_EQ(input_srid, output_srid);
  ASSERT_NE(nullptr, setsrid_service);
  uint32_t replaced_srid = 3857;
  srid_arg.data = reinterpret_cast<const uint8_t *>(&replaced_srid);
  seekdb_plugin_execution_value_v1_t set_args[2] = {geometry_arg, srid_arg};
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            setsrid_service->execute(instance, &context, set_args, 2));
  std::memcpy(&output_srid, sink.data_, sizeof(output_srid));
  ASSERT_EQ(replaced_srid, output_srid);

  ASSERT_NE(nullptr, geomfromtext_service);
  ASSERT_NE(nullptr, geometryfromtext_service);
  const char wkt_input[] = "POINT(1.25 2.5)";
  seekdb_plugin_execution_value_v1_t wkt_arg = {};
  wkt_arg.struct_size = sizeof(wkt_arg);
  wkt_arg.type_id = "org.seekdb.gis.scalar.bytes";
  wkt_arg.data = reinterpret_cast<const uint8_t *>(wkt_input);
  wkt_arg.data_size = sizeof(wkt_input) - 1;
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            geomfromtext_service->execute(instance, &context, &wkt_arg, 1));
  ASSERT_EQ(26u, sink.data_size_);
  double parsed_x = 0.0;
  double parsed_y = 0.0;
  std::memcpy(&parsed_x, sink.data_ + 10, sizeof(parsed_x));
  std::memcpy(&parsed_y, sink.data_ + 18, sizeof(parsed_y));
  ASSERT_DOUBLE_EQ(1.25, parsed_x);
  ASSERT_DOUBLE_EQ(2.5, parsed_y);
  seekdb_plugin_execution_value_v1_t text_args[2] = {wkt_arg, srid_arg};
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            geometryfromtext_service->execute(instance, &context, text_args, 2));
  std::memcpy(&output_srid, sink.data_, sizeof(output_srid));
  ASSERT_EQ(replaced_srid, output_srid);

  const seekdb_plugin_type_codec_service_v1_t *codec = nullptr;
  for (uint32_t i = 0; i < manifest->provides_count; ++i) {
    const auto &provided = manifest->provides[i];
    if (0 == std::strcmp("org.seekdb.gis.codec", provided.service_id)) {
      codec = reinterpret_cast<const seekdb_plugin_type_codec_service_v1_t *>(
          provided.service);
    }
  }
  ASSERT_NE(nullptr, codec);
  ASSERT_EQ(sizeof(*codec), codec->struct_size);
  ASSERT_EQ(SEEKDB_PLUGIN_EXECUTION_SPI_MAJOR, codec->spi_major);
  seekdb_plugin_execution_value_v1_t encoded_value = {};
  encoded_value.struct_size = sizeof(encoded_value);
  encoded_value.type_id = "org.seekdb.gis.geometry";
  encoded_value.data = sink.data_;
  encoded_value.data_size = sink.data_size_;
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            codec->encode(instance, &context, &encoded_value));
  ASSERT_EQ(26u, sink.data_size_);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            codec->decode(instance, &context, sink.data_, sink.data_size_));
  ASSERT_EQ(26u, sink.data_size_);
  uint8_t corrupt_geometry[26] = {};
  std::memcpy(corrupt_geometry, sink.data_, sizeof(corrupt_geometry));
  corrupt_geometry[6] = 9;
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT,
            codec->decode(instance, &context, corrupt_geometry, sizeof(corrupt_geometry)));

  const double z = 7.75;
  seekdb_plugin_execution_value_v1_t pointz_args[3] = {};
  const double pointz_values[3] = {x, y, z};
  for (int i = 0; i < 3; ++i) {
    pointz_args[i].struct_size = sizeof(pointz_args[i]);
    pointz_args[i].type_id = "org.seekdb.gis.scalar.float64";
    pointz_args[i].data = reinterpret_cast<const uint8_t *>(&pointz_values[i]);
    pointz_args[i].data_size = sizeof(double);
  }
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            service->execute(instance, &context, pointz_args, 3));
  ASSERT_EQ(34u, sink.data_size_);
  ASSERT_EQ(0xe9, sink.data_[6]);
  ASSERT_EQ(0x03, sink.data_[7]);
  ASSERT_EQ(0, std::memcmp(sink.data_ + 26, &z, sizeof(z)));

  const double envelope_values[4] = {-1.0, -2.0, 3.0, 4.0};
  seekdb_plugin_execution_value_v1_t envelope_args[4] = {};
  for (int i = 0; i < 4; ++i) {
    envelope_args[i].struct_size = sizeof(envelope_args[i]);
    envelope_args[i].type_id = "org.seekdb.gis.scalar.float64";
    envelope_args[i].data = reinterpret_cast<const uint8_t *>(&envelope_values[i]);
    envelope_args[i].data_size = sizeof(double);
  }
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            service->execute(instance, &context, envelope_args, 4));
  ASSERT_EQ(98u, sink.data_size_);
  ASSERT_EQ(0, std::memcmp(sink.data_ + 18, &envelope_values[0], sizeof(double)));
  ASSERT_EQ(0, std::memcmp(sink.data_ + 26, &envelope_values[1], sizeof(double)));
  geometry_arg.data_size = sink.data_size_;
  ASSERT_NE(nullptr, area_service);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            area_service->execute(instance, &scalar_context, &geometry_arg, 1));
  ASSERT_EQ(sizeof(double), scalar_sink.data_size_);
  double envelope_area = 0.0;
  std::memcpy(&envelope_area, scalar_sink.data_, sizeof(envelope_area));
  ASSERT_DOUBLE_EQ(24.0, envelope_area);

  ASSERT_NE(nullptr, length_service);
  uint8_t line_geometry[46] = {0};
  line_geometry[4] = 1;
  line_geometry[5] = 1;
  line_geometry[6] = 2;
  line_geometry[10] = 2;
  const double line_values[4] = {0.0, 0.0, 3.0, 4.0};
  std::memcpy(line_geometry + 14, &line_values[0], sizeof(double));
  std::memcpy(line_geometry + 22, &line_values[1], sizeof(double));
  std::memcpy(line_geometry + 30, &line_values[2], sizeof(double));
  std::memcpy(line_geometry + 38, &line_values[3], sizeof(double));
  geometry_arg.data = line_geometry;
  geometry_arg.data_size = sizeof(line_geometry);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            length_service->execute(instance, &scalar_context, &geometry_arg, 1));
  double line_length = 0.0;
  std::memcpy(&line_length, scalar_sink.data_, sizeof(line_length));
  ASSERT_DOUBLE_EQ(5.0, line_length);

  ASSERT_NE(nullptr, distance_service);
  uint8_t first_point[26] = {0};
  uint8_t second_point[26] = {0};
  first_point[4] = second_point[4] = 1;
  first_point[5] = second_point[5] = 1;
  first_point[6] = second_point[6] = 1;
  const double first_values[2] = {0.0, 0.0};
  const double second_values[2] = {3.0, 4.0};
  std::memcpy(first_point + 10, first_values, sizeof(first_values));
  std::memcpy(second_point + 10, second_values, sizeof(second_values));
  seekdb_plugin_execution_value_v1_t distance_args[2] = {};
  distance_args[0] = geometry_arg;
  distance_args[1] = geometry_arg;
  distance_args[0].data = first_point;
  distance_args[0].data_size = sizeof(first_point);
  distance_args[1].data = second_point;
  distance_args[1].data_size = sizeof(second_point);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            distance_service->execute(instance, &scalar_context, distance_args, 2));
  double point_distance = 0.0;
  std::memcpy(&point_distance, scalar_sink.data_, sizeof(point_distance));
  ASSERT_DOUBLE_EQ(5.0, point_distance);

  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK, manifest->stop(instance));
  manifest->deinit(instance);
  EXPECT_EQ(0, ::dlclose(handle));
#endif
}

TEST(GisPlugin, GeometryEngineOperations)
{
#if defined(_WIN32)
  GTEST_SKIP() << "GIS plugin ABI smoke test currently targets POSIX loader";
#else
  const std::string path = std::string(SEEKDB_TEST_GIS_PLUGIN_DIR) + "/" +
      SEEKDB_TEST_GIS_PLUGIN_FILE;
  void *handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  ASSERT_NE(nullptr, handle) << (::dlerror() == nullptr ? "dlopen failed" : ::dlerror());
  auto *entry = reinterpret_cast<seekdb_plugin_entry_v1_fn>(
      ::dlsym(handle, "seekdb_plugin_entry_v1"));
  ASSERT_NE(nullptr, entry);
  const seekdb_plugin_manifest_v1_t *manifest = entry();
  ASSERT_NE(nullptr, manifest);

  seekdb_plugin_host_api_v1_t host_api = {};
  host_api.struct_size = sizeof(host_api);
  host_api.abi_major = SEEKDB_PLUGIN_ABI_MAJOR;
  host_api.abi_minor = SEEKDB_PLUGIN_ABI_MINOR;
  seekdb_plugin_instance_handle_t *instance = nullptr;
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK, manifest->init(&host_api, &instance));
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK, manifest->start(instance));

  auto find_service = [&](const char *service_id) {
    const seekdb_plugin_function_service_v1_t *service = nullptr;
    for (uint32_t i = 0; i < manifest->provides_count; ++i) {
      if (0 == std::strcmp(service_id, manifest->provides[i].service_id)) {
        service = reinterpret_cast<const seekdb_plugin_function_service_v1_t *>(
            manifest->provides[i].service);
        break;
      }
    }
    return service;
  };

  ResultSink sink = {{0}, 0};
  seekdb_plugin_execution_context_v1_t context = {};
  context.struct_size = sizeof(context);
  context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
  context.emit_result = capture_result;
  const double coordinates[2] = {1.0, 1.0};
  seekdb_plugin_execution_value_v1_t point_args[2] = {};
  for (int i = 0; i < 2; ++i) {
    point_args[i].struct_size = sizeof(point_args[i]);
    point_args[i].type_id = "org.seekdb.gis.scalar.float64";
    point_args[i].data = reinterpret_cast<const uint8_t *>(&coordinates[i]);
    point_args[i].data_size = sizeof(double);
  }
  const auto *function_service = find_service("org.seekdb.gis.function");
  ASSERT_NE(nullptr, function_service);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            function_service->execute(instance, &context, point_args, 2));
  std::vector<uint8_t> point(sink.data_, sink.data_ + sink.data_size_);
  ASSERT_EQ(26u, point.size());

  const auto *buffer_service = find_service("org.seekdb.gis.function.st_buffer");
  const auto *centroid_service = find_service("org.seekdb.gis.function.st_centroid");
  const auto *valid_service = find_service("org.seekdb.gis.function.st_isvalid");
  const auto *mbr_service = find_service("org.seekdb.gis.function.spatial_mbr");
  ASSERT_NE(nullptr, buffer_service);
  ASSERT_NE(nullptr, centroid_service);
  ASSERT_NE(nullptr, valid_service);
  ASSERT_NE(nullptr, mbr_service);
  const double radius = 2.0;
  seekdb_plugin_execution_value_v1_t geometry_and_distance[2] = {};
  geometry_and_distance[0].struct_size = sizeof(geometry_and_distance[0]);
  geometry_and_distance[0].type_id = "org.seekdb.gis.geometry";
  geometry_and_distance[0].data = point.data();
  geometry_and_distance[0].data_size = point.size();
  geometry_and_distance[1].struct_size = sizeof(geometry_and_distance[1]);
  geometry_and_distance[1].type_id = "org.seekdb.gis.scalar.float64";
  geometry_and_distance[1].data = reinterpret_cast<const uint8_t *>(&radius);
  geometry_and_distance[1].data_size = sizeof(radius);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            buffer_service->execute(instance, &context, geometry_and_distance, 2));
  std::vector<uint8_t> buffered(sink.data_, sink.data_ + sink.data_size_);
  ASSERT_GT(buffered.size(), 500u);
  ASSERT_EQ(3u, *reinterpret_cast<const uint32_t *>(buffered.data() + 6));

  seekdb_plugin_execution_value_v1_t geometry_arg = {};
  geometry_arg.struct_size = sizeof(geometry_arg);
  geometry_arg.type_id = "org.seekdb.gis.geometry";
  geometry_arg.data = buffered.data();
  geometry_arg.data_size = buffered.size();
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            valid_service->execute(instance, &context, &geometry_arg, 1));
  ASSERT_EQ(1u, sink.data_[0]);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            centroid_service->execute(instance, &context, &geometry_arg, 1));
  ASSERT_EQ(26u, sink.data_size_);
  double centroid_x = 0.0;
  std::memcpy(&centroid_x, sink.data_ + 10, sizeof(centroid_x));
  ASSERT_NEAR(1.0, centroid_x, 0.05);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            mbr_service->execute(instance, &context, &geometry_arg, 1));
  ASSERT_EQ(4 * sizeof(double), sink.data_size_);
  double bounds[4] = {};
  std::memcpy(bounds, sink.data_, sizeof(bounds));
  ASSERT_NEAR(-1.0, bounds[0], 0.05);
  ASSERT_NEAR(3.0, bounds[2], 0.05);

  const auto *from_text_service = find_service("org.seekdb.gis.function.st_geomfromtext");
  const auto *as_text_service = find_service("org.seekdb.gis.function.st_astext");
  const auto *area_service = find_service("org.seekdb.gis.function.st_area");
  ASSERT_NE(nullptr, from_text_service);
  ASSERT_NE(nullptr, as_text_service);
  ASSERT_NE(nullptr, area_service);
  const char polygon_text[] = "POLYGON ((0 0, 0 4, 3 4, 3 0, 0 0))";
  seekdb_plugin_execution_value_v1_t text_arg = {};
  text_arg.struct_size = sizeof(text_arg);
  text_arg.type_id = "org.seekdb.gis.scalar.bytes";
  text_arg.data = reinterpret_cast<const uint8_t *>(polygon_text);
  text_arg.data_size = std::strlen(polygon_text);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            from_text_service->execute(instance, &context, &text_arg, 1));
  std::vector<uint8_t> polygon(sink.data_, sink.data_ + sink.data_size_);
  ASSERT_EQ(98u, polygon.size());
  geometry_arg.data = polygon.data();
  geometry_arg.data_size = polygon.size();
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            area_service->execute(instance, &context, &geometry_arg, 1));
  double polygon_area = 0.0;
  std::memcpy(&polygon_area, sink.data_, sizeof(polygon_area));
  ASSERT_DOUBLE_EQ(12.0, polygon_area);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            as_text_service->execute(instance, &context, &geometry_arg, 1));
  const std::string normalized_wkt(reinterpret_cast<const char *>(sink.data_), sink.data_size_);
  ASSERT_NE(std::string::npos, normalized_wkt.find("POLYGON"));
  const auto *geometrytype_service = find_service("org.seekdb.gis.function.geometrytype");
  const auto *iscollection_service = find_service("org.seekdb.gis.function.st_iscollection");
  const auto *interior_rings_service = find_service(
      "org.seekdb.gis.function.st_numinteriorrings");
  ASSERT_NE(nullptr, geometrytype_service);
  ASSERT_NE(nullptr, iscollection_service);
  ASSERT_NE(nullptr, interior_rings_service);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            geometrytype_service->execute(instance, &context, &geometry_arg, 1));
  ASSERT_EQ(7u, sink.data_size_);
  ASSERT_EQ(0, std::memcmp(sink.data_, "POLYGON", 7));
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            iscollection_service->execute(instance, &context, &geometry_arg, 1));
  ASSERT_EQ(1u, sink.data_size_);
  ASSERT_EQ(0u, sink.data_[0]);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            interior_rings_service->execute(instance, &context, &geometry_arg, 1));
  ASSERT_EQ(sizeof(int32_t), sink.data_size_);
  ASSERT_EQ(0u, sink.data_[0]);

  const auto *union_service = find_service("org.seekdb.gis.function.st_union");
  const auto *intersects_service = find_service("org.seekdb.gis.function.st_intersects");
  const auto *transform_service = find_service("org.seekdb.gis.function.st_transform");
  ASSERT_NE(nullptr, union_service);
  ASSERT_NE(nullptr, intersects_service);
  ASSERT_NE(nullptr, transform_service);
  const char second_polygon_text[] = "POLYGON ((2 1, 2 5, 5 5, 5 1, 2 1))";
  text_arg.data = reinterpret_cast<const uint8_t *>(second_polygon_text);
  text_arg.data_size = std::strlen(second_polygon_text);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            from_text_service->execute(instance, &context, &text_arg, 1));
  std::vector<uint8_t> second_polygon(sink.data_, sink.data_ + sink.data_size_);
  seekdb_plugin_execution_value_v1_t union_args[2] = {};
  union_args[0] = geometry_arg;
  union_args[0].data = polygon.data();
  union_args[0].data_size = polygon.size();
  union_args[1] = geometry_arg;
  union_args[1].data = second_polygon.data();
  union_args[1].data_size = second_polygon.size();
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            union_service->execute(instance, &context, union_args, 2));
  ASSERT_EQ(98u, sink.data_size_);
  ASSERT_EQ(3u, *reinterpret_cast<const uint32_t *>(sink.data_ + 6));
  seekdb_plugin_execution_value_v1_t relation_args[2] = {};
  relation_args[0] = union_args[0];
  relation_args[1] = union_args[1];
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            intersects_service->execute(instance, &context, relation_args, 2));
  ASSERT_EQ(1u, sink.data_size_);
  ASSERT_EQ(1u, sink.data_[0]);

  uint32_t source_srid = 4326;
  std::memcpy(point.data(), &source_srid, sizeof(source_srid));
  uint32_t target_srid = 3857;
  seekdb_plugin_execution_value_v1_t transform_args[2] = {};
  transform_args[0] = geometry_arg;
  transform_args[0].data = point.data();
  transform_args[0].data_size = point.size();
  transform_args[1].struct_size = sizeof(transform_args[1]);
  transform_args[1].type_id = "org.seekdb.gis.scalar.uint32";
  transform_args[1].data = reinterpret_cast<const uint8_t *>(&target_srid);
  transform_args[1].data_size = sizeof(target_srid);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            transform_service->execute(instance, &context, transform_args, 2));
  ASSERT_EQ(target_srid, *reinterpret_cast<const uint32_t *>(sink.data_));

  const auto *asmvt_service = find_service("org.seekdb.gis.function.st_asmvtgeom");
  ASSERT_NE(nullptr, asmvt_service);
  const char tile_bounds_text[] = "POLYGON ((0 0, 0 10, 10 10, 10 0, 0 0))";
  text_arg.data = reinterpret_cast<const uint8_t *>(tile_bounds_text);
  text_arg.data_size = std::strlen(tile_bounds_text);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            from_text_service->execute(instance, &context, &text_arg, 1));
  std::vector<uint8_t> tile_bounds(sink.data_, sink.data_ + sink.data_size_);
  const double extent = 100.0;
  const double buffer = 0.0;
  const double clip = 1.0;
  seekdb_plugin_execution_value_v1_t mvt_args[5] = {};
  mvt_args[0] = geometry_arg;
  mvt_args[0].data = point.data();
  mvt_args[0].data_size = point.size();
  mvt_args[1] = geometry_arg;
  mvt_args[1].data = tile_bounds.data();
  mvt_args[1].data_size = tile_bounds.size();
  for (int i = 2; i < 5; ++i) {
    mvt_args[i].struct_size = sizeof(mvt_args[i]);
    mvt_args[i].type_id = "org.seekdb.gis.scalar.float64";
    mvt_args[i].data_size = sizeof(double);
  }
  mvt_args[2].data = reinterpret_cast<const uint8_t *>(&extent);
  mvt_args[3].data = reinterpret_cast<const uint8_t *>(&buffer);
  mvt_args[4].data = reinterpret_cast<const uint8_t *>(&clip);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            asmvt_service->execute(instance, &context, mvt_args, 5));
  ASSERT_EQ(0u, *reinterpret_cast<const uint32_t *>(sink.data_));
  double tile_x = 0.0;
  double tile_y = 0.0;
  std::memcpy(&tile_x, sink.data_ + 10, sizeof(tile_x));
  std::memcpy(&tile_y, sink.data_ + 18, sizeof(tile_y));
  ASSERT_NEAR(10.0, tile_x, 1e-9);
  ASSERT_NEAR(90.0, tile_y, 1e-9);

  const auto *aswkb_service = find_service("org.seekdb.gis.function.st_aswkb");
  const auto *geomfromwkb_service = find_service("org.seekdb.gis.function.st_geomfromwkb");
  const auto *geohash_service = find_service("org.seekdb.gis.function.st_geohash");
  const auto *cellid_service = find_service("org.seekdb.gis.function.spatial_cellid");
  ASSERT_NE(nullptr, aswkb_service);
  ASSERT_NE(nullptr, geomfromwkb_service);
  ASSERT_NE(nullptr, geohash_service);
  ASSERT_NE(nullptr, cellid_service);
  geometry_arg.data = point.data();
  geometry_arg.data_size = point.size();
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            aswkb_service->execute(instance, &context, &geometry_arg, 1));
  std::vector<uint8_t> raw_wkb(sink.data_, sink.data_ + sink.data_size_);
  ASSERT_EQ(21u, raw_wkb.size());
  seekdb_plugin_execution_value_v1_t wkb_args[2] = {};
  wkb_args[0].struct_size = sizeof(wkb_args[0]);
  wkb_args[0].type_id = "org.seekdb.gis.scalar.bytes";
  wkb_args[0].data = raw_wkb.data();
  wkb_args[0].data_size = raw_wkb.size();
  wkb_args[1].struct_size = sizeof(wkb_args[1]);
  wkb_args[1].type_id = "org.seekdb.gis.scalar.uint32";
  const uint32_t wkb_srid = 4326;
  wkb_args[1].data = reinterpret_cast<const uint8_t *>(&wkb_srid);
  wkb_args[1].data_size = sizeof(wkb_srid);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            geomfromwkb_service->execute(instance, &context, wkb_args, 2));
  ASSERT_EQ(point.size(), sink.data_size_);
  ASSERT_EQ(wkb_srid, *reinterpret_cast<const uint32_t *>(sink.data_));
  std::vector<uint8_t> big_endian(21, 0);
  big_endian[0] = 0;
  big_endian[4] = 1;
  for (size_t i = 0; i < sizeof(double); ++i) {
    big_endian[5 + i] = reinterpret_cast<const uint8_t *>(&coordinates[0])[sizeof(double) - i - 1];
    big_endian[13 + i] = reinterpret_cast<const uint8_t *>(&coordinates[1])[sizeof(double) - i - 1];
  }
  wkb_args[0].data = big_endian.data();
  wkb_args[0].data_size = big_endian.size();
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            geomfromwkb_service->execute(instance, &context, wkb_args, 2));
  ASSERT_EQ(wkb_srid, *reinterpret_cast<const uint32_t *>(sink.data_));
  std::vector<uint8_t> ewkb(25, 0);
  ewkb[0] = 1;
  const uint32_t ewkb_type = 0x20000001U;
  const uint32_t ewkb_srid = 4326;
  std::memcpy(ewkb.data() + 1, &ewkb_type, sizeof(ewkb_type));
  std::memcpy(ewkb.data() + 5, &ewkb_srid, sizeof(ewkb_srid));
  std::memcpy(ewkb.data() + 9, &coordinates[0], sizeof(double));
  std::memcpy(ewkb.data() + 17, &coordinates[1], sizeof(double));
  wkb_args[0].data = ewkb.data();
  wkb_args[0].data_size = ewkb.size();
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            geomfromwkb_service->execute(instance, &context, wkb_args, 1));
  ASSERT_EQ(ewkb_srid, *reinterpret_cast<const uint32_t *>(sink.data_));
  const uint32_t precision = 8;
  seekdb_plugin_execution_value_v1_t geohash_args[2] = {};
  geohash_args[0].struct_size = sizeof(geohash_args[0]);
  geohash_args[0].type_id = "org.seekdb.gis.geometry";
  geohash_args[0].data = point.data();
  geohash_args[0].data_size = point.size();
  geohash_args[1].struct_size = sizeof(geohash_args[1]);
  geohash_args[1].type_id = "org.seekdb.gis.scalar.uint32";
  geohash_args[1].data = reinterpret_cast<const uint8_t *>(&precision);
  geohash_args[1].data_size = sizeof(precision);
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            geohash_service->execute(instance, &context, geohash_args, 2));
  ASSERT_EQ(precision, sink.data_size_);
  EXPECT_EQ('s', static_cast<char>(sink.data_[0]));
  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK,
            cellid_service->execute(instance, &context, geohash_args, 1));
  ASSERT_EQ(sizeof(uint64_t), sink.data_size_);

  ASSERT_EQ(SEEKDB_PLUGIN_STATUS_OK, manifest->stop(instance));
  manifest->deinit(instance);
  EXPECT_EQ(0, ::dlclose(handle));
#endif
}

} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
