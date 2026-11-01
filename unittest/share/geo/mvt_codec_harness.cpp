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
// Standalone compatibility and construction benchmark. The legacy branch is
// compiled against a baseline extracted by mvt_codec_compatibility.py only.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#ifdef LEGACY
#include "ob_vector_tile.pb-c.h"
#else
#include "rust/mvt.h"
#endif

static float float_bits(uint32_t bits) { float x; std::memcpy(&x, &bits, sizeof(x)); return x; }
static double double_bits(uint64_t bits) { double x; std::memcpy(&x, &bits, sizeof(x)); return x; }
using Encoded = std::vector<uint8_t>;
static Encoded construct(size_t count, size_t properties, size_t vertices, bool empty)
{
  // A valid MVT multipoint: one MoveTo(count) followed by zigzag deltas.
  std::vector<uint32_t> geometry;
  if (vertices > 0) {
    geometry.push_back((uint32_t(vertices) << 3) | 1);
    for (size_t i = 0; i < vertices; ++i) { geometry.push_back(2); geometry.push_back(2); }
  }
  std::vector<std::string> keys;
  for (size_t p = 0; p < properties; ++p) keys.push_back("property_" + std::to_string(p));
#ifdef LEGACY
  VectorTile__Tile tile = VECTOR_TILE__TILE__INIT;
  VectorTile__Tile__Layer layer = VECTOR_TILE__TILE__LAYER__INIT;
  layer.name = const_cast<char *>("compat"); layer.version = 2; layer.extent = 8192;
  VectorTile__Tile__Layer *layer_ptr = &layer; tile.n_layers = 1; tile.layers = &layer_ptr;
  std::vector<char *> key_ptrs;
  for (auto &key : keys) {
    key_ptrs.push_back(key.data());
  }
  layer.n_keys = key_ptrs.size(); layer.keys = key_ptrs.data();
  std::vector<VectorTile__Tile__Feature> features(empty ? 0 : count, VECTOR_TILE__TILE__FEATURE__INIT);
  std::vector<VectorTile__Tile__Feature *> feature_ptrs;
  std::vector<std::vector<uint32_t>> tags(features.size());
  std::vector<std::vector<uint32_t>> feature_geometry(features.size());
  std::vector<VectorTile__Tile__Value> values;
  std::vector<VectorTile__Tile__Value *> value_ptrs;
  std::unordered_map<size_t, uint32_t> value_dictionary;
  values.reserve(properties);
  for (size_t i = 0; i < features.size(); ++i) {
    auto &f = features[i]; f.has_id = i % 3 != 0; f.id = i % 3 == 1 ? 0 : UINT64_MAX;
    f.type = VECTOR_TILE__TILE__GEOM_TYPE__POINT;
    feature_geometry[i] = geometry;
    f.n_geometry = feature_geometry[i].size(); f.geometry = feature_geometry[i].data();
    for (size_t p = 0; p < properties; ++p) {
      const auto inserted = value_dictionary.emplace(p % 12, values.size());
      if (inserted.second) {
        VectorTile__Tile__Value v = VECTOR_TILE__TILE__VALUE__INIT;
        switch (p % 12) {
          case 0: v.test_oneof_case = VECTOR_TILE__TILE__VALUE__TEST_ONEOF_STRING_VALUE; v.string_value = const_cast<char *>("value-\xff"); break;
          case 1: v.test_oneof_case = VECTOR_TILE__TILE__VALUE__TEST_ONEOF_FLOAT_VALUE; v.float_value = float_bits(0x80000000); break;
          case 2: v.test_oneof_case = VECTOR_TILE__TILE__VALUE__TEST_ONEOF_DOUBLE_VALUE; v.double_value = double_bits(0x7ff8000000000042ULL); break;
          case 3: v.test_oneof_case = VECTOR_TILE__TILE__VALUE__TEST_ONEOF_INT_VALUE; v.int_value = INT64_MIN; break;
          case 4: v.test_oneof_case = VECTOR_TILE__TILE__VALUE__TEST_ONEOF_UINT_VALUE; v.uint_value = UINT64_MAX; break;
          case 5: v.test_oneof_case = VECTOR_TILE__TILE__VALUE__TEST_ONEOF_SINT_VALUE; v.sint_value = INT64_MIN; break;
          case 6: v.test_oneof_case = VECTOR_TILE__TILE__VALUE__TEST_ONEOF_BOOL_VALUE; v.bool_value = false; break;
          case 7: v.test_oneof_case = VECTOR_TILE__TILE__VALUE__TEST_ONEOF_INT_VALUE; v.int_value = INT64_MAX; break;
          case 8: v.test_oneof_case = VECTOR_TILE__TILE__VALUE__TEST_ONEOF_SINT_VALUE; v.sint_value = INT64_MAX; break;
          case 9: v.test_oneof_case = VECTOR_TILE__TILE__VALUE__TEST_ONEOF_FLOAT_VALUE; v.float_value = float_bits(0x7fc00042); break;
          case 10: v.test_oneof_case = VECTOR_TILE__TILE__VALUE__TEST_ONEOF_DOUBLE_VALUE; v.double_value = double_bits(0x8000000000000000ULL); break;
          case 11: v.test_oneof_case = VECTOR_TILE__TILE__VALUE__TEST_ONEOF_BOOL_VALUE; v.bool_value = true; break;
        }
        values.push_back(v);
      }
      // The former C++ encoder scans the seeded property names for an index.
      uint32_t key_index = 0;
      while (key_index < keys.size() && keys[key_index] != keys[p]) ++key_index;
      tags[i].push_back(key_index); tags[i].push_back(inserted.first->second);
    }
    f.n_tags = tags[i].size(); f.tags = tags[i].data(); feature_ptrs.push_back(&f);
  }
  for (auto &v : values) value_ptrs.push_back(&v);
  layer.n_features = features.size(); layer.features = feature_ptrs.data();
  layer.n_values = values.size(); layer.values = value_ptrs.data();
  std::vector<uint8_t> result(vector_tile__tile__get_packed_size(&tile));
  vector_tile__tile__pack(&tile, result.data());
#else
  auto builder = oceanbase::mvt::new_builder();
  const std::string name = "compat";
  builder->configure({reinterpret_cast<const uint8_t *>(name.data()), name.size()}, 8192);
  for (const auto &key : keys) builder->seed_key({reinterpret_cast<const uint8_t *>(key.data()), key.size()});
  for (size_t i = 0; i < (empty ? 0 : count); ++i) {
    builder->begin_feature(1, {geometry.data(), geometry.size()});
    if (i % 3 != 0) builder->set_id(i % 3 == 1 ? 0 : UINT64_MAX);
    for (size_t p = 0; p < properties; ++p) {
      const auto &key = keys[p];
      const uint32_t k = builder->intern_key({reinterpret_cast<const uint8_t *>(key.data()), key.size()});
      switch (p % 12) {
        case 0: { const std::string s = "value-\xff"; builder->add_string(k, {reinterpret_cast<const uint8_t *>(s.data()), s.size()}); break; }
        case 1: builder->add_float(k, float_bits(0x80000000)); break;
        case 2: builder->add_double(k, double_bits(0x7ff8000000000042ULL)); break;
        case 3: builder->add_int(k, INT64_MIN); break;
        case 4: builder->add_uint(k, UINT64_MAX); break;
        case 5: builder->add_sint(k, INT64_MIN); break;
        case 6: builder->add_bool(k, false); break;
        case 7: builder->add_int(k, INT64_MAX); break;
        case 8: builder->add_sint(k, INT64_MAX); break;
        case 9: builder->add_float(k, float_bits(0x7fc00042)); break;
        case 10: builder->add_double(k, double_bits(0x8000000000000000ULL)); break;
        case 11: builder->add_bool(k, true); break;
      }
    }
    builder->commit_feature();
  }
  Encoded result(builder->encoded_len());
  if (!builder->encode_to({result.data(), result.size()})) throw std::runtime_error("MVT encode failed");
#endif
  return result;
}

int main(int argc, char **argv)
{
  try {
#ifdef LEGACY
    if (argc == 4 && std::string(argv[1]) == "decode") {
      std::ifstream in(argv[2], std::ios::binary);
      std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
      auto *tile = reinterpret_cast<VectorTile__Tile *>(protobuf_c_message_unpack(
          &vector_tile__tile__descriptor, nullptr, bytes.size(), bytes.data()));
      if (!tile) return 2;
      std::vector<uint8_t> out(vector_tile__tile__get_packed_size(tile));
      vector_tile__tile__pack(tile, out.data());
      protobuf_c_message_free_unpacked(&tile->base, nullptr);
      std::ofstream(argv[3], std::ios::binary).write(reinterpret_cast<char *>(out.data()), out.size());
      return 0;
    }
#endif
    if (argc != 7) throw std::runtime_error("usage: harness encode|bench OUTPUT FEATURES PROPERTIES GEOMETRY_VERTICES ITERATIONS");
    const size_t features = std::stoull(argv[3]), props = std::stoull(argv[4]);
    const size_t geometry = std::stoull(argv[5]), iterations = std::stoull(argv[6]);
    uint64_t checksum = 0; size_t output_bytes = 0;
    const auto start = std::chrono::steady_clock::now();
    for (size_t n = 0; n < iterations; ++n) {
      auto bytes = construct(features, props, geometry, features == 0);
      // Match the SQL result handoff: final allocation, copy, and release.
      void *result = std::malloc(bytes.size());
      if (!result && !bytes.empty()) throw std::bad_alloc();
      std::memcpy(result, bytes.data(), bytes.size());
      asm volatile("" : : "g"(result) : "memory");
      checksum += bytes.size() + (bytes.empty() ? 0 : static_cast<uint8_t *>(result)[bytes.size() / 2]);
      output_bytes = bytes.size();
      if (n == 0 && std::string(argv[1]) == "encode")
        std::ofstream(argv[2], std::ios::binary).write(static_cast<char *>(result), bytes.size());
      std::free(result);
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << "{\"ns\":" << ns << ",\"iterations\":" << iterations << ",\"bytes\":" << output_bytes << ",\"checksum\":" << checksum << "}\n";
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
