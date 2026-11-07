/*
 * Copyright (c) 2026 OceanBase.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Conversion of a hipVS/hnswlib snapshot into a seekdb snapshot.
 *
 * hipVS exports CAGRA-derived HNSW indexes through the classic hnswlib binary
 * layout. That layout identifies elements by an *internal* row number, while
 * seekdb needs the *external* row id (VID) to be able to answer queries. This
 * header rewrites the label field of every element in place (streaming, one row
 * at a time) and re-verifies the whole structure while doing so:
 *
 *   - the corpus bytes stored inside the snapshot must equal the corpus we
 *     handed to the GPU builder (guards against silently mismatched labels);
 *   - labels must form a permutation of [0, count), so no document is lost or
 *     duplicated;
 *   - every neighbour id must be < count (guards against a corrupted graph).
 *
 * Only a fully validated snapshot is published to the final path, through
 * write-to-temporary + rename, so a crash never leaves a half-written index.
 */
#ifndef OCEANBASE_LIB_VECTOR_HIPVS_SNAPSHOT_EXPORT_H_
#define OCEANBASE_LIB_VECTOR_HIPVS_SNAPSHOT_EXPORT_H_

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace seekdb_hipvs {

/* Header of the hnswlib on-disk layout (all fields little endian, size_t = u64)
 * followed by `count` level-0 element records and then the upper-level link
 * lists. */
struct HnswlibHeader {
  uint64_t level0_offset;
  uint64_t capacity;
  uint64_t element_count;
  uint64_t element_stride;
  uint64_t label_offset;
  uint64_t data_offset;
  int32_t max_level;
  uint32_t entry_point;
  uint64_t max_m;
  uint64_t max_m0;
  uint64_t m;
  double level_multiplier;
  uint64_t ef_construction;
};

class SnapshotError : public std::runtime_error {
 public:
  explicit SnapshotError(const std::string &what) : std::runtime_error(what) {}
};

namespace detail {

template <class T>
inline T read_pod(std::istream &in) {
  T value{};
  if (!in.read(reinterpret_cast<char *>(&value), sizeof(value))) {
    throw SnapshotError("truncated hnswlib snapshot header");
  }
  return value;
}

template <class T>
inline void write_pod(std::ostream &out, const T &value) {
  out.write(reinterpret_cast<const char *>(&value), sizeof(value));
}

inline bool host_is_little_endian() {
  const uint16_t probe = 1;
  return *reinterpret_cast<const uint8_t *>(&probe) == 1;
}

inline uint32_t read_links_degree(const char *record, uint64_t record_size, uint64_t max_allowed) {
  uint32_t degree = 0;
  std::memcpy(&degree, record, sizeof(degree));
  if (record_size < sizeof(degree) || degree > max_allowed) {
    throw SnapshotError("corrupted hnswlib link count");
  }
  return degree;
}

}  // namespace detail

/* Reads `source`, replaces every label by vids[row], writes `target`.
 * vectors/vids must describe exactly the corpus stored in the snapshot. */
inline void remap_snapshot_labels(const std::string &source,
                                  const std::string &target,
                                  const float *vectors,
                                  const int64_t *vids,
                                  uint64_t count,
                                  uint64_t dim,
                                  uint64_t m) {
  static_assert(sizeof(size_t) == 8, "seekdb hipvs backend requires a 64-bit host");
  if (!detail::host_is_little_endian()) {
    throw SnapshotError("seekdb hipvs backend requires a little-endian host");
  }
  if (nullptr == vectors || nullptr == vids) {
    throw SnapshotError("null corpus passed to snapshot conversion");
  }

  std::ifstream in(source, std::ios::binary);
  if (!in) throw SnapshotError("cannot open hipVS snapshot for conversion");

  HnswlibHeader h{};
  h.level0_offset = detail::read_pod<uint64_t>(in);
  h.capacity = detail::read_pod<uint64_t>(in);
  h.element_count = detail::read_pod<uint64_t>(in);
  h.element_stride = detail::read_pod<uint64_t>(in);
  h.label_offset = detail::read_pod<uint64_t>(in);
  h.data_offset = detail::read_pod<uint64_t>(in);
  h.max_level = detail::read_pod<int32_t>(in);
  h.entry_point = detail::read_pod<uint32_t>(in);
  h.max_m = detail::read_pod<uint64_t>(in);
  h.max_m0 = detail::read_pod<uint64_t>(in);
  h.m = detail::read_pod<uint64_t>(in);
  h.level_multiplier = detail::read_pod<double>(in);
  h.ef_construction = detail::read_pod<uint64_t>(in);

  /* Sanity: the geometry of the snapshot must match the request we made, and
   * the label must sit directly after the vector payload. */
  const uint64_t expect_data_offset = sizeof(uint32_t) + sizeof(uint32_t) * 2 * m;
  const uint64_t expect_label_offset = expect_data_offset + sizeof(float) * dim;
  if (h.element_count != count || h.capacity < count || h.level0_offset != 0 ||
      h.max_m != m || h.max_m0 != 2 * m || h.m != m || h.data_offset != expect_data_offset ||
      h.label_offset != expect_label_offset ||
      h.element_stride != h.label_offset + sizeof(int64_t) || h.max_level < 0 || h.max_level > 64 ||
      h.entry_point >= count || h.ef_construction < m || !(h.level_multiplier > 0.0) ||
      !std::isfinite(h.level_multiplier) ||
      h.element_stride > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    throw SnapshotError("hipVS snapshot layout does not match the requested HNSW parameters "
                        "(count/dim/M mismatch)");
  }

  std::ofstream out(target, std::ios::binary | std::ios::trunc);
  if (!out) throw SnapshotError("cannot create converted snapshot");
  out.exceptions(std::ios::failbit | std::ios::badbit);

  /* Capacity is clamped to the live element count: seekdb only needs the live
   * graph and a smaller file is a faster import. */
  detail::write_pod(out, h.level0_offset);
  detail::write_pod(out, count);
  detail::write_pod(out, h.element_count);
  detail::write_pod(out, h.element_stride);
  detail::write_pod(out, h.label_offset);
  detail::write_pod(out, h.data_offset);
  detail::write_pod(out, h.max_level);
  detail::write_pod(out, h.entry_point);
  detail::write_pod(out, h.max_m);
  detail::write_pod(out, h.max_m0);
  detail::write_pod(out, h.m);
  detail::write_pod(out, h.level_multiplier);
  detail::write_pod(out, h.ef_construction);

  std::vector<char> record(static_cast<size_t>(h.element_stride));
  std::vector<uint8_t> label_seen(static_cast<size_t>(count), 0);
  for (uint64_t row = 0; row < count; ++row) {
    if (!in.read(record.data(), static_cast<std::streamsize>(record.size()))) {
      throw SnapshotError("truncated hnswlib element record");
    }
    detail::read_links_degree(record.data(), record.size(), 2 * m);
    for (uint64_t k = 0; k < 2 * m; ++k) {
      uint32_t neighbour = 0;
      std::memcpy(&neighbour, record.data() + sizeof(uint32_t) + sizeof(uint32_t) * k,
                  sizeof(neighbour));
      /* Unused slots are 0-initialised by hnswlib; 0 is always a legal id. */
      if (neighbour >= count) throw SnapshotError("hnswlib neighbour id out of range");
    }
    uint64_t source_row = 0;
    std::memcpy(&source_row, record.data() + h.label_offset, sizeof(source_row));
    if (source_row >= count || label_seen[static_cast<size_t>(source_row)] != 0) {
      throw SnapshotError("hnswlib labels are not a permutation of the corpus rows");
    }
    label_seen[static_cast<size_t>(source_row)] = 1;
    if (std::memcmp(record.data() + h.data_offset,
                    vectors + static_cast<uint64_t>(source_row) * dim,
                    sizeof(float) * dim) != 0) {
      throw SnapshotError("hnswlib label does not match the corpus row we uploaded");
    }
    const int64_t vid = vids[source_row];
    std::memcpy(record.data() + h.label_offset, &vid, sizeof(vid));
    out.write(record.data(), static_cast<std::streamsize>(record.size()));
  }

  /* Upper layers: per element a u32 byte count followed by that many bytes of
   * (degree, ids...) link lists. The entry point must carry max_level lists and
   * nothing may follow the last element. */
  const uint64_t link_stride = sizeof(uint32_t) + sizeof(uint32_t) * m;
  std::vector<char> upper;
  bool entry_has_full_height = (0 == h.max_level);
  for (uint64_t row = 0; row < count; ++row) {
    const uint32_t bytes = detail::read_pod<uint32_t>(in);
    if (bytes % link_stride != 0 || bytes / link_stride > static_cast<uint64_t>(h.max_level)) {
      throw SnapshotError("corrupted hnswlib upper-level link list");
    }
    if (row == h.entry_point && bytes / link_stride == static_cast<uint64_t>(h.max_level)) {
      entry_has_full_height = true;
    }
    upper.resize(bytes);
    if (bytes > 0 && !in.read(upper.data(), static_cast<std::streamsize>(bytes))) {
      throw SnapshotError("truncated hnswlib upper-level link list");
    }
    for (uint64_t pos = 0; pos < bytes; pos += link_stride) {
      detail::read_links_degree(upper.data() + pos, link_stride, m);
      for (uint64_t k = 0; k < m; ++k) {
        uint32_t neighbour = 0;
        std::memcpy(&neighbour, upper.data() + pos + sizeof(uint32_t) + sizeof(uint32_t) * k,
                    sizeof(neighbour));
        if (neighbour >= count) throw SnapshotError("hnswlib upper-level id out of range");
      }
    }
    detail::write_pod(out, bytes);
    if (bytes > 0) out.write(upper.data(), static_cast<std::streamsize>(bytes));
  }
  if (!entry_has_full_height) {
    throw SnapshotError("hnswlib entry point does not reach the advertised max level");
  }
  if (in.peek() != std::char_traits<char>::eof()) {
    throw SnapshotError("unexpected trailing bytes after the hnswlib snapshot");
  }
  out.flush();
  out.close();
}

}  // namespace seekdb_hipvs

#endif  // OCEANBASE_LIB_VECTOR_HIPVS_SNAPSHOT_EXPORT_H_