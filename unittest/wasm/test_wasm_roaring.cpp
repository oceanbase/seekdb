// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include <roaring/roaring64.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

using namespace roaring::api;

int main()
{
  const std::array<uint64_t, 8> keys = {0, 1, UINT32_MAX, UINT64_C(1) << 32,
      (UINT64_C(1) << 32) + 1, UINT64_C(1) << 48, INT64_MAX, UINT64_MAX};
  auto *bitmap = roaring64_bitmap_create();
  auto *removed = roaring64_bitmap_create();
  assert(bitmap && removed);
  for (uint64_t key : keys) {
    roaring64_bitmap_add(bitmap, key);
    roaring64_bitmap_add(bitmap, key);
    assert(roaring64_bitmap_contains(bitmap, key));
  }
  assert(roaring64_bitmap_get_cardinality(bitmap) == keys.size());
  auto *copy = roaring64_bitmap_copy(bitmap);
  assert(copy && roaring64_bitmap_is_subset(copy, bitmap));
  for (size_t i = 0; i < keys.size(); i += 2) roaring64_bitmap_add(removed, keys[i]);
  auto *difference = roaring64_bitmap_andnot(bitmap, removed);
  assert(difference && roaring64_bitmap_get_cardinality(difference) == keys.size() / 2);
  roaring64_bitmap_andnot_inplace(copy, removed);
  assert(roaring64_bitmap_equals(copy, difference));
  roaring64_bitmap_or_inplace(copy, removed);
  assert(roaring64_bitmap_equals(copy, bitmap));
  const size_t bytes = roaring64_bitmap_portable_size_in_bytes(bitmap);
  std::vector<char> serialized(bytes);
  assert(roaring64_bitmap_portable_serialize(bitmap, serialized.data()) == bytes);
  auto *restored = roaring64_bitmap_portable_deserialize_safe(serialized.data(), bytes);
  assert(restored && roaring64_bitmap_equals(restored, bitmap));
  auto *iter = roaring64_iterator_create(restored);
  assert(iter);
  for (uint64_t key : keys) {
    assert(roaring64_iterator_has_value(iter));
    assert(roaring64_iterator_value(iter) == key);
    roaring64_iterator_advance(iter);
  }
  assert(!roaring64_iterator_has_value(iter));
  roaring64_iterator_free(iter);
  for (uint64_t key : keys) roaring64_bitmap_remove(restored, key);
  assert(roaring64_bitmap_get_cardinality(restored) == 0);
  for (auto *ptr : {bitmap, removed, copy, difference, restored}) roaring64_bitmap_free(ptr);
  puts("PASS: CRoaring target dependency, 64-bit keys, set operations and serialized roundtrip");
}
