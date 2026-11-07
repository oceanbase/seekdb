/*
 * Copyright (c) 2026 OceanBase.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-side probe: does the snapshot produced by the hipVS backend import into
 * VSAG (the same library seekdb uses for `lib=vsag`) and answer queries with the
 * right document ids?
 *
 * It reads a snapshot from disk, recovers the corpus and the ids from it (the
 * hnswlib layout stores both), builds a brute-force ground truth for a few
 * queries and then replays the exact import path used by seekdb:
 *
 *     vsag::Factory::CreateIndex("hnsw", <seekdb's JSON>,
 *                                allocator)->Deserialize(ifstream)
 *     index->KnnSearch(query, k, SearchParam(..,"{\"hnsw\":{\"ef_search\":..}}"))
 *
 * This lets the DDL path be de-risked without booting a server.
 *
 * Usage: vsag_import_probe <snapshot> <dim> <M> <ef_construction> <ef_search> [k]
 */
#include <vsag/vsag.h>

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Snapshot {
  int64_t rows = 0;
  int64_t dim = 0;
  uint64_t stride = 0;
  uint64_t label_offset = 0;
  uint64_t data_offset = 0;
  std::vector<float> corpus;
  std::vector<int64_t> labels;
};

template <class T>
T read_pod(std::istream &in) {
  T v{};
  if (!in.read(reinterpret_cast<char *>(&v), sizeof(v))) {
    std::fprintf(stderr, "ERROR truncated snapshot header\n");
    std::exit(2);
  }
  return v;
}

Snapshot read_snapshot(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "ERROR cannot open %s\n", path.c_str());
    std::exit(2);
  }
  Snapshot s;
  (void)read_pod<uint64_t>(in);  // level0 offset
  (void)read_pod<uint64_t>(in);  // capacity
  s.rows = static_cast<int64_t>(read_pod<uint64_t>(in));
  s.stride = read_pod<uint64_t>(in);
  s.label_offset = read_pod<uint64_t>(in);
  s.data_offset = read_pod<uint64_t>(in);
  (void)read_pod<int32_t>(in);
  (void)read_pod<uint32_t>(in);
  (void)read_pod<uint64_t>(in);
  (void)read_pod<uint64_t>(in);
  (void)read_pod<uint64_t>(in);
  (void)read_pod<double>(in);
  (void)read_pod<uint64_t>(in);
  s.dim = static_cast<int64_t>((s.label_offset - s.data_offset) / sizeof(float));
  s.corpus.resize(static_cast<size_t>(s.rows * s.dim));
  s.labels.resize(static_cast<size_t>(s.rows));
  std::vector<char> record(static_cast<size_t>(s.stride));
  for (int64_t r = 0; r < s.rows; ++r) {
    if (!in.read(record.data(), static_cast<std::streamsize>(record.size()))) {
      std::fprintf(stderr, "ERROR truncated snapshot row\n");
      std::exit(2);
    }
    std::memcpy(&s.labels[static_cast<size_t>(r)], record.data() + s.label_offset,
                sizeof(int64_t));
    std::memcpy(&s.corpus[static_cast<size_t>(r * s.dim)], record.data() + s.data_offset,
                sizeof(float) * static_cast<size_t>(s.dim));
  }
  return s;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 6) {
    std::fprintf(stderr,
                 "usage: %s <snapshot> <dim> <M> <ef_construction> <ef_search> [k]\n", argv[0]);
    return 1;
  }
  const std::string path = argv[1];
  const int dim = std::atoi(argv[2]);
  const int m = std::atoi(argv[3]);
  const int efc = std::atoi(argv[4]);
  const int efs = std::atoi(argv[5]);
  const int k = (argc > 6) ? std::atoi(argv[6]) : 10;

  Snapshot snap = read_snapshot(path);
  std::printf("snapshot rows=%" PRId64 " dim=%" PRId64 " (expected dim=%d)\n", snap.rows,
              snap.dim, dim);
  if (snap.dim != dim) {
    std::fprintf(stderr, "ERROR dim mismatch\n");
    return 2;
  }

  char json[1024];
  std::snprintf(json, sizeof(json),
                "{\"dim\":%d,\"dtype\":\"float32\",\"metric_type\":\"l2\","
                "\"use_old_serial_format\":true,"
                "\"hnsw\":{\"ef_construction\":%d,\"ef_search\":%d,\"max_degree\":%d}}",
                dim, efc, efs, m);
  std::printf("vsag create param: %s\n", json);

  auto created = vsag::Factory::CreateIndex("hnsw", std::string(json), nullptr);
  if (!created.has_value()) {
    std::fprintf(stderr, "ERROR vsag CreateIndex failed: %s\n",
                 created.error().message.c_str());
    return 3;
  }
  std::shared_ptr<vsag::Index> index = created.value();

  std::ifstream in(path, std::ios::binary);
  if (!in) return 2;
  auto deserialized = index->Deserialize(in);
  if (!deserialized.has_value()) {
    std::fprintf(stderr, "ERROR vsag Deserialize(hnswlib stream) failed: %s\n",
                 deserialized.error().message.c_str());
    return 4;
  }
  std::printf("vsag Deserialize OK, elements=%" PRId64 "\n", index->GetNumElements());
  if (index->GetNumElements() != snap.rows) {
    std::fprintf(stderr, "ERROR element count mismatch after import\n");
    return 5;
  }

  const int queries = 64;
  std::vector<float> query(static_cast<size_t>(queries * dim));
  for (int q = 0; q < queries; ++q) {
    const int64_t row = (snap.rows / queries) * q;
    std::memcpy(query.data() + static_cast<size_t>(q) * dim,
                snap.corpus.data() + static_cast<size_t>(row) * dim,
                sizeof(float) * static_cast<size_t>(dim));
  }

  /* Ground truth with the ids stored in the snapshot. */
  std::vector<std::vector<int64_t>> truth(static_cast<size_t>(queries));
  std::vector<std::pair<float, int64_t>> scratch(static_cast<size_t>(snap.rows));
  for (int q = 0; q < queries; ++q) {
    for (int64_t r = 0; r < snap.rows; ++r) {
      float acc = 0.0F;
      for (int d = 0; d < dim; ++d) {
        const float delta = query[static_cast<size_t>(q) * dim + d] -
                            snap.corpus[static_cast<size_t>(r) * dim + d];
        acc += delta * delta;
      }
      scratch[static_cast<size_t>(r)] = {acc, snap.labels[static_cast<size_t>(r)]};
    }
    std::partial_sort(scratch.begin(), scratch.begin() + k, scratch.end());
    truth[static_cast<size_t>(q)].resize(static_cast<size_t>(k));
    for (int i = 0; i < k; ++i) {
      truth[static_cast<size_t>(q)][static_cast<size_t>(i)] = scratch[static_cast<size_t>(i)].second;
    }
  }

  char search_json[256];
  std::snprintf(search_json, sizeof(search_json),
                "{\"hnsw\":{\"ef_search\":%d,\"skip_ratio\":0.7}}", efs);
  /* SearchParam keeps a reference to the parameter string: it must outlive it. */
  const std::string search_parameters(search_json);
  vsag::SearchParam search_param(false, search_parameters, nullptr, nullptr);

  int64_t hits = 0;
  int64_t total = 0;
  for (int q = 0; q < queries; ++q) {
    auto dataset = vsag::Dataset::Make();
    dataset->NumElements(1)->Dim(dim)->Float32Vectors(query.data() + static_cast<size_t>(q) * dim)
        ->Owner(false);
    auto result = index->KnnSearch(dataset, k, search_param);
    if (!result.has_value()) {
      std::fprintf(stderr, "ERROR KnnSearch failed: %s\n",
                   result.error().message.c_str());
      return 6;
    }
    result.value()->Owner(false);
    const int64_t *ids = result.value()->GetIds();
    if (result.value()->GetDim() != k) {
      std::fprintf(stderr, "ERROR unexpected result dim %" PRId64 "\n", result.value()->GetDim());
      return 7;
    }
    for (int i = 0; i < k; ++i) {
      ++total;
      if (std::find(truth[static_cast<size_t>(q)].begin(),
                    truth[static_cast<size_t>(q)].end(),
                    ids[i]) != truth[static_cast<size_t>(q)].end()) {
        ++hits;
      }
    }
  }
  std::printf("vsag_import_recall@%d=%.4f queries=%d\n", k,
              static_cast<double>(hits) / static_cast<double>(total), queries);
  return 0;
}