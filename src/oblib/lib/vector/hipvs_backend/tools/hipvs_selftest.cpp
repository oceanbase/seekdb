/*
 * Copyright (c) 2026 OceanBase.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Standalone acceptance tool for the seekdb hipVS backend.
 *
 * It is *not* a product path (seekdb itself calls the same C ABI from the DDL
 * path). It exists to prove two things quickly and reproducibly:
 *
 *   1. the shared object really runs the CAGRA build on the AMD GPU and exports
 *      a snapshot whose labels are the seekdb VIDs;
 *   2. that snapshot is byte-compatible with textbook hnswlib: this tool loads
 *      it back with *stock* hnswlib, searches it and compares the result with an
 *      exact brute-force ground truth (recall@k).
 *
 * Build: cmake --build build --target seekdb_hipvs_selftest
 * Run:   ./seekdb_hipvs_selftest <rows> <dim> <m> <ef_construction> <queries> <k>
 */
#include "hipvs_build_api.h"

#include <hnswlib/hnswlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

float random_unit(uint64_t &state) {
  /* xorshift64*, deterministic so runs are comparable. */
  state ^= state >> 12;
  state ^= state << 25;
  state ^= state >> 27;
  const uint64_t bits = state * 0x2545F4914F6CDD1DULL;
  return static_cast<float>(static_cast<double>(bits >> 11) / 9007199254740992.0);
}

std::vector<uint32_t> exact_knn(const std::vector<float> &base,
                                int64_t rows,
                                int64_t dim,
                                const std::vector<float> &query,
                                int64_t queries,
                                int64_t k) {
  std::vector<uint32_t> truth(static_cast<size_t>(queries * k), 0);
  std::vector<std::pair<float, uint32_t>> scratch(static_cast<size_t>(rows));
  for (int64_t q = 0; q < queries; ++q) {
    for (int64_t r = 0; r < rows; ++r) {
      float acc = 0.0F;
      for (int64_t d = 0; d < dim; ++d) {
        const float delta = query[static_cast<size_t>(q * dim + d)] -
                            base[static_cast<size_t>(r * dim + d)];
        acc += delta * delta;
      }
      scratch[static_cast<size_t>(r)] = {acc, static_cast<uint32_t>(r)};
    }
    std::partial_sort(scratch.begin(), scratch.begin() + k, scratch.end());
    for (int64_t i = 0; i < k; ++i) {
      truth[static_cast<size_t>(q * k + i)] = scratch[static_cast<size_t>(i)].second;
    }
  }
  return truth;
}

}  // namespace

int main(int argc, char **argv) {
  const int64_t rows = (argc > 1) ? std::atoll(argv[1]) : 20000;
  const int64_t dim = (argc > 2) ? std::atoll(argv[2]) : 128;
  const int64_t m = (argc > 3) ? std::atoll(argv[3]) : 16;
  const int64_t efc = (argc > 4) ? std::atoll(argv[4]) : 200;
  const int64_t queries = (argc > 5) ? std::atoll(argv[5]) : 200;
  const int64_t k = (argc > 6) ? std::atoll(argv[6]) : 10;

  char info[512] = {0};
  const int32_t probe = seekdb_hipvs_probe_v1(info, sizeof(info));
  std::printf("probe=%d %s\n", probe, info);
  if (probe != SEEKDB_HIPVS_OK) return 2;
  std::printf("abi_version=%d\n", seekdb_hipvs_abi_version_v1());

  std::vector<float> base(static_cast<size_t>(rows * dim));
  std::vector<int64_t> vids(static_cast<size_t>(rows));
  uint64_t state = 0x9E3779B97F4A7C15ULL;
  for (auto &value : base) value = random_unit(state);
  for (int64_t r = 0; r < rows; ++r) vids[static_cast<size_t>(r)] = 7000000 + r;

  const std::string path = "/tmp/seekdb_hipvs_selftest.index";
  char error[1024] = {0};
  const auto t0 = std::chrono::steady_clock::now();
  const int32_t build_rc = seekdb_hipvs_build_v1(base.data(), vids.data(), rows, dim, m, efc,
                                                 path.c_str(), error, sizeof(error));
  const auto t1 = std::chrono::steady_clock::now();
  const double backend_ms =
      std::chrono::duration<double, std::milli>(t1 - t0).count();
  std::printf("build_rc=%d backend_ms=%.3f error=%s\n", build_rc, backend_ms, error);
  if (build_rc != SEEKDB_HIPVS_OK) return 3;

  /* --- independent verification with stock hnswlib ----------------------- */
  hnswlib::L2Space<float> space(static_cast<size_t>(dim));
  auto *index = new hnswlib::HierarchicalNSW<float>(&space, path, false);
  const size_t loaded_elements = index->cur_element_count.load();
  std::printf("hnswlib_loaded elements=%zu maxlevel=%d ef_construction=%zu m=%zu\n",
              loaded_elements, index->maxlevel_, index->ef_construction_, index->M_);
  if (static_cast<int64_t>(loaded_elements) != rows) {
    std::printf("FAIL: element count mismatch\n");
    return 4;
  }

  std::vector<float> query(static_cast<size_t>(queries * dim));
  for (auto &value : query) value = random_unit(state);

  index->setEf(static_cast<size_t>(std::max<int64_t>(efc, 64)));
  std::vector<uint32_t> approx(static_cast<size_t>(queries * k), 0);
  int64_t label_mismatch = 0;
  for (int64_t q = 0; q < queries; ++q) {
    auto result = index->searchKnn(query.data() + q * dim, static_cast<size_t>(k));
    for (int64_t i = 0; i < k; ++i) {
      const hnswlib::labeltype label = result.top().second;
      result.pop();
      if (label < 7000000 || label >= 7000000 + rows) ++label_mismatch;
      approx[static_cast<size_t>(q * k + i)] = static_cast<uint32_t>(label - 7000000);
    }
  }

  const std::vector<uint32_t> truth = exact_knn(base, rows, dim, query, queries, k);
  int64_t hits = 0;
  for (int64_t q = 0; q < queries; ++q) {
    for (int64_t i = 0; i < k; ++i) {
      const uint32_t id = approx[static_cast<size_t>(q * k + i)];
      for (int64_t j = 0; j < k; ++j) {
        if (truth[static_cast<size_t>(q * k + j)] == id) {
          ++hits;
          break;
        }
      }
    }
  }
  const double recall = static_cast<double>(hits) / static_cast<double>(queries * k);
  std::printf("rows=%lld dim=%lld m=%lld efc=%lld recall@%lld=%.4f bad_labels=%lld\n",
              static_cast<long long>(rows), static_cast<long long>(dim),
              static_cast<long long>(m), static_cast<long long>(efc),
              static_cast<long long>(k), recall, static_cast<long long>(label_mismatch));
  delete index;
  const bool keep = (argc > 7) && (0 == std::strcmp(argv[7], "keep"));
  if (!keep) std::remove(path.c_str());
  else std::printf("snapshot_kept=%s\n", path.c_str());
  return recall >= 0.90 ? 0 : 5;
}