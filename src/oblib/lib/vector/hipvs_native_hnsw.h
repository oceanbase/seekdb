/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace oceanbase { namespace common { namespace obvsag {

// hipVS CPU-hierarchy export uses hnswlib's native 64-bit little-endian layout.
// Validate the complete graph before handing any of it to the VSAG deserializer.
// The export labels are source row numbers, not seekdb document IDs.
inline bool validate_hipvs_hnsw(std::vector<int8_t> &bytes,
                               const float *vectors, const int64_t *ids,
                               int64_t rows, int dim, int m, std::string &error)
{
  const auto fail = [&error](const char *why) { error = why; return false; };
  if (!vectors || !ids || rows <= 0 || rows > INT32_MAX || dim <= 0 || m < 5 || m > 128) {
    return fail("invalid HNSW import arguments");
  }
  const uint16_t endian = 1;
  if (sizeof(size_t) != 8 || *reinterpret_cast<const uint8_t *>(&endian) != 1 || bytes.size() < 96) {
    return fail("unsupported native HNSW layout");
  }
  const auto u64 = [&bytes](size_t pos) { uint64_t x; std::memcpy(&x, bytes.data() + pos, 8); return x; };
  const auto u32 = [&bytes](size_t pos) { uint32_t x; std::memcpy(&x, bytes.data() + pos, 4); return x; };
  const uint64_t n = static_cast<uint64_t>(rows);
  const uint64_t data_offset = 4 + 8 * static_cast<uint64_t>(m);
  const uint64_t label_offset = data_offset + 4 * static_cast<uint64_t>(dim);
  const uint64_t node_size = label_offset + 8;
  if (u64(0) != 0 || u64(8) < n || u64(16) != n || u64(24) != node_size ||
      u64(32) != label_offset || u64(40) != data_offset || u32(48) > 64 ||
      u32(52) >= n || u64(56) != static_cast<uint64_t>(m) ||
      u64(64) != static_cast<uint64_t>(2 * m) || u64(72) != static_cast<uint64_t>(m)) {
    return fail("native HNSW header does not match the requested index");
  }
  double multiplier;
  std::memcpy(&multiplier, bytes.data() + 80, 8);
  if (!std::isfinite(multiplier) || multiplier <= 0 || u64(88) < static_cast<uint64_t>(m)) {
    return fail("invalid native HNSW hierarchy parameters");
  }
  if (n > (bytes.size() - 96) / (node_size + 4)) {
    return fail("truncated HNSW level-zero graph");
  }
  std::vector<int64_t> sorted_ids(ids, ids + rows);
  std::sort(sorted_ids.begin(), sorted_ids.end());
  if (std::adjacent_find(sorted_ids.begin(), sorted_ids.end()) != sorted_ids.end()) {
    return fail("duplicate document IDs");
  }
  std::vector<uint8_t> seen(n, 0);
  for (uint64_t i = 0; i < n; ++i) {
    const size_t offset = 96 + i * node_size;
    const uint32_t degree = u32(offset);
    const uint64_t row = u64(offset + label_offset);
    if (degree > static_cast<uint32_t>(2 * m) || row >= n || seen[row]++) {
      return fail("invalid degree, deleted node, or duplicate source row");
    }
    for (uint32_t j = 0; j < degree; ++j) {
      if (u32(offset + 4 + 4 * j) >= n) return fail("invalid level-zero neighbor");
    }
    if (std::memcmp(bytes.data() + offset + data_offset, vectors + row * dim, 4ULL * dim) != 0) {
      return fail("export changed a vector or mapped it to the wrong row");
    }
    std::memcpy(bytes.data() + offset + label_offset, ids + row, 8);
  }
  const uint64_t upper_node_size = 4 + 4 * static_cast<uint64_t>(m);
  size_t pos = 96 + n * node_size;
  for (uint64_t i = 0; i < n; ++i) {
    if (bytes.size() - pos < 4) return fail("missing upper-layer size");
    const uint32_t length = u32(pos);
    pos += 4;
    if (length > bytes.size() - pos || length % upper_node_size != 0 ||
        length / upper_node_size > u32(48)) return fail("invalid upper-layer extent");
    for (size_t level = 0; level < length; level += upper_node_size) {
      const uint32_t degree = u32(pos + level);
      if (degree > static_cast<uint32_t>(m)) return fail("invalid upper-layer degree");
      for (uint32_t j = 0; j < degree; ++j) {
        if (u32(pos + level + 4 + 4 * j) >= n) return fail("invalid upper-layer neighbor");
      }
    }
    pos += length;
  }
  if (pos != bytes.size()) return fail("unexpected trailing HNSW data");
  return true;
}

}}} // oceanbase::common::obvsag
