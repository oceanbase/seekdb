// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "share/geo/ob_vector_tile.pb-c.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

static void check_bytes(const ProtobufCMessage *message, std::initializer_list<uint8_t> expected)
{
  std::array<uint8_t, 64> bytes;
  bytes.fill(0xa5);
  assert(protobuf_c_message_get_packed_size(message) == expected.size());
  assert(protobuf_c_message_pack(message, bytes.data() + 1) == expected.size());
  assert(memcmp(bytes.data() + 1, expected.begin(), expected.size()) == 0);
  assert(bytes[0] == 0xa5 && bytes[expected.size() + 1] == 0xa5);
}

struct Allocations {
  size_t attempts = 0;
  size_t live = 0;
  size_t fail_at = SIZE_MAX;
  static void *alloc(void *context, size_t size) {
    auto &state = *static_cast<Allocations *>(context);
    if (state.attempts++ == state.fail_at) return nullptr;
    void *result = malloc(size);
    if (result) ++state.live;
    return result;
  }
  static void free(void *context, void *ptr) {
    auto &state = *static_cast<Allocations *>(context);
    if (ptr) { assert(state.live > 0); --state.live; std::free(ptr); }
  }
};

int main()
{
  assert(protobuf_c_version_number() == 1004001);
  VectorTile__Tile__Value value = VECTOR_TILE__TILE__VALUE__INIT;
  value.test_oneof_case = VECTOR_TILE__TILE__VALUE__TEST_ONEOF_UINT_VALUE;
  value.uint_value = UINT64_MAX;
  check_bytes(&value.base, {0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01});
  value.test_oneof_case = VECTOR_TILE__TILE__VALUE__TEST_ONEOF_SINT_VALUE;
  value.sint_value = INT64_MIN;
  check_bytes(&value.base, {0x30, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01});
  value.sint_value = -1;
  check_bytes(&value.base, {0x30, 0x01});
  value.test_oneof_case = VECTOR_TILE__TILE__VALUE__TEST_ONEOF_DOUBLE_VALUE;
  value.double_value = 1.0;
  check_bytes(&value.base, {0x19, 0, 0, 0, 0, 0, 0, 0xf0, 0x3f});

  VectorTile__Tile__Feature feature = VECTOR_TILE__TILE__FEATURE__INIT;
  feature.has_id = 1;
  feature.id = UINT64_MAX;
  feature.type = VECTOR_TILE__TILE__GEOM_TYPE__POINT;
  uint32_t tags[] = {0, 0}, geometry[] = {9, 50, 34};
  feature.n_tags = 2;
  feature.tags = tags;
  feature.n_geometry = 3;
  feature.geometry = geometry;
  check_bytes(&feature.base, {8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 1,
    0x12, 2, 0, 0, 0x18, 1, 0x22, 3, 9, 50, 34});
  VectorTile__Tile__Feature *features[] = {&feature};
  VectorTile__Tile__Value *values[] = {&value};
  char name[] = "places", key[] = "测量";
  char *keys[] = {key};
  VectorTile__Tile__Layer layer = VECTOR_TILE__TILE__LAYER__INIT;
  layer.version = 2;
  layer.name = name;
  layer.n_features = layer.n_keys = layer.n_values = 1;
  layer.features = features;
  layer.keys = keys;
  layer.values = values;
  VectorTile__Tile__Layer *layers[] = {&layer};
  VectorTile__Tile tile = VECTOR_TILE__TILE__INIT;
  tile.n_layers = 1;
  tile.layers = layers;
  const size_t size = vector_tile__tile__get_packed_size(&tile);
  std::vector<uint8_t> bytes(size + 2, 0xa5);
  assert(vector_tile__tile__pack(&tile, bytes.data() + 1) == size);
  assert(bytes.front() == 0xa5 && bytes.back() == 0xa5);
  Allocations state;
  ProtobufCAllocator allocator = {Allocations::alloc, Allocations::free, &state};
  auto *decoded = reinterpret_cast<VectorTile__Tile *>(protobuf_c_message_unpack(
    &vector_tile__tile__descriptor, &allocator, size, bytes.data() + 1));
  assert(decoded != nullptr && decoded->n_layers == 1);
  const auto *result = decoded->layers[0];
  assert(result->version == 2 && result->extent == 4096 && strcmp(result->name, name) == 0);
  assert(result->n_features == 1 && result->features[0]->id == UINT64_MAX);
  assert(result->features[0]->n_geometry == 3 && result->features[0]->geometry[1] == 50);
  assert(result->n_keys == 1 && strcmp(result->keys[0], key) == 0);
  assert(result->n_values == 1 && result->values[0]->double_value == 1.0);
  const size_t count = state.attempts;
  protobuf_c_message_free_unpacked(&decoded->base, &allocator);
  assert(state.live == 0);
  for (size_t failure = 0; failure < count; ++failure) {
    state = Allocations{0, 0, failure};
    assert(protobuf_c_message_unpack(&vector_tile__tile__descriptor, &allocator,
      size, bytes.data() + 1) == nullptr);
    assert(state.live == 0);
  }
  state = Allocations{};
  assert(protobuf_c_message_unpack(&vector_tile__tile__descriptor, &allocator,
    size - 1, bytes.data() + 1) == nullptr);
  assert(state.live == 0);
  puts("PASS: seekdb vector tile descriptors, known wire bytes, wasm32 layout and allocation-failure cleanup");
}
