// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace seekdb_hipvs {

template <class T> T read_scalar(std::istream &input)
{
  T value{};
  if (!input.read(reinterpret_cast<char *>(&value), sizeof(value))) {
    throw std::runtime_error("truncated HNSW stream");
  }
  return value;
}

template <class T> void write_scalar(std::ostream &output, T value)
{
  output.write(reinterpret_cast<const char *>(&value), sizeof(value));
}

inline void validate_links(const char *data, uint64_t capacity, uint64_t count)
{
  uint32_t degree = 0;
  std::memcpy(&degree, data, sizeof(degree));
  // The upper bits of hnswlib's count word include deletion flags. A fresh
  // snapshot must contain neither flags nor invalid internal row numbers.
  if (degree > capacity) throw std::runtime_error("invalid HNSW degree or deletion flag");
  for (uint32_t j = 0; j < degree; ++j) {
    uint32_t neighbor = 0;
    std::memcpy(&neighbor, data + 4 + 4 * j, sizeof(neighbor));
    if (neighbor >= count) throw std::runtime_error("HNSW neighbor outside snapshot");
  }
}

// hipVS CPU hierarchy exports the original 64-bit little-endian hnswlib
// format. VSAG's plain FP32/L2 legacy reader accepts this layout. Do not pass
// the incompatible base-layer-only wrapper format to VSAG.
inline void remap_hnsw_stream(const std::string &source, const std::string &target,
                             const float *vectors, const int64_t *vids,
                             uint64_t count, uint64_t dim, uint64_t m)
{
  static_assert(sizeof(size_t) == 8 && sizeof(float) == 4, "64-bit FP32 platform required");
  const uint16_t endian = 1;
  if (*reinterpret_cast<const uint8_t *>(&endian) != 1) {
    throw std::runtime_error("little-endian platform required");
  }
  std::ifstream input(source, std::ios::binary);
  std::ofstream output(target, std::ios::binary | std::ios::trunc);
  if (!input || !output) throw std::runtime_error("cannot open HNSW conversion file");
  output.exceptions(std::ios::failbit | std::ios::badbit);
  const uint64_t offset0 = read_scalar<uint64_t>(input);
  const uint64_t allocated = read_scalar<uint64_t>(input);
  const uint64_t rows = read_scalar<uint64_t>(input);
  const uint64_t stride = read_scalar<uint64_t>(input);
  const uint64_t label_offset = read_scalar<uint64_t>(input);
  const uint64_t data_offset = read_scalar<uint64_t>(input);
  const int32_t max_level = read_scalar<int32_t>(input);
  const uint32_t entry = read_scalar<uint32_t>(input);
  const uint64_t max_m = read_scalar<uint64_t>(input);
  const uint64_t max_m0 = read_scalar<uint64_t>(input);
  const uint64_t stored_m = read_scalar<uint64_t>(input);
  const double level_multiplier = read_scalar<double>(input);
  const uint64_t efc = read_scalar<uint64_t>(input);
  if (rows != count || allocated < count || offset0 != 0 || max_m != m ||
      max_m0 != 2 * m || stored_m != m || data_offset != 4 + 8 * m ||
      label_offset != data_offset + 4 * dim || stride != label_offset + 8 ||
      max_level < 0 || max_level > 64 || entry >= count || efc < m ||
      !std::isfinite(level_multiplier) || level_multiplier <= 0 ||
      stride > std::numeric_limits<size_t>::max()) {
    throw std::runtime_error("unsupported hipVS HNSW layout");
  }
  write_scalar(output, offset0);
  write_scalar(output, count); // Avoid importing unnecessarily large capacity.
  write_scalar(output, rows);
  write_scalar(output, stride);
  write_scalar(output, label_offset);
  write_scalar(output, data_offset);
  write_scalar(output, max_level);
  write_scalar(output, entry);
  write_scalar(output, max_m);
  write_scalar(output, max_m0);
  write_scalar(output, stored_m);
  write_scalar(output, level_multiplier);
  write_scalar(output, efc);

  std::vector<char> record(static_cast<size_t>(stride));
  std::vector<uint8_t> seen(count, 0);
  for (uint64_t i = 0; i < count; ++i) {
    if (!input.read(record.data(), record.size())) throw std::runtime_error("truncated HNSW row");
    validate_links(record.data(), 2 * m, count);
    uint64_t source_row = 0;
    std::memcpy(&source_row, record.data() + label_offset, sizeof(source_row));
    if (source_row >= count || seen[source_row]++) {
      throw std::runtime_error("HNSW labels are not a source-row permutation");
    }
    if (std::memcmp(record.data() + data_offset, vectors + source_row * dim, dim * 4) != 0) {
      throw std::runtime_error("HNSW label/vector correspondence changed");
    }
    std::memcpy(record.data() + label_offset, vids + source_row, sizeof(int64_t));
    output.write(record.data(), record.size());
  }
  const uint64_t link_stride = 4 + 4 * m;
  std::vector<char> links;
  bool entry_has_max_level = false;
  for (uint64_t i = 0; i < count; ++i) {
    const uint32_t bytes = read_scalar<uint32_t>(input);
    if (bytes % link_stride != 0 || bytes / link_stride > static_cast<uint64_t>(max_level)) {
      throw std::runtime_error("invalid HNSW upper-level extent");
    }
    if (i == entry) entry_has_max_level = bytes / link_stride == static_cast<uint64_t>(max_level);
    links.resize(bytes);
    if (bytes && !input.read(links.data(), bytes)) throw std::runtime_error("truncated upper links");
    for (uint64_t pos = 0; pos < bytes; pos += link_stride) {
      validate_links(links.data() + pos, m, count);
    }
    write_scalar(output, bytes);
    if (bytes) output.write(links.data(), bytes);
  }
  if (!entry_has_max_level || input.peek() != std::char_traits<char>::eof()) {
    throw std::runtime_error("invalid entry point or trailing HNSW data");
  }
  output.flush();
  output.close();
}

} // namespace seekdb_hipvs
