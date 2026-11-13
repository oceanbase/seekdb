/*
 * Copyright (c) 2025 OceanBase.
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

/*
 * seekdb_hipvs_selfcheck: exercises the whole bridge ABI with a synthetic dataset and
 * measures build time + recall@k, so a bring-up on a new GPU/ROCm version is one
 * command instead of a database deploy.
 *
 *   ./seekdb_hipvs_selfcheck            # 50k x 128, k=10
 *   ./seekdb_hipvs_selfcheck 200000 256 10
 *
 * Exit codes: 0 = ran and produced a sane graph, 2 = bridge unavailable (stub build
 * or no ROCm device), 1 = any other failure.
 */

#include "seekdb_hipvs_bridge.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <unordered_set>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(const Clock::time_point &start)
{
  return std::chrono::duration<double>(Clock::now() - start).count();
}

double l2_sqr(const float *a, const float *b, int64_t dim)
{
  double sum = 0.0;
  for (int64_t i = 0; i < dim; ++i) {
    const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
    sum += d * d;
  }
  return sum;
}

/* Exact top-k by brute force; used as ground truth for recall and as the CPU cost
 * reference (it is a lower bound on what a graph build has to pay per point). */
std::vector<int64_t> brute_force_topk(const std::vector<float> &base,
                                      int64_t count,
                                      const float *query,
                                      int64_t dim,
                                      int64_t k)
{
  std::vector<std::pair<double, int64_t>> all;
  all.reserve(static_cast<size_t>(count));
  for (int64_t i = 0; i < count; ++i) {
    all.emplace_back(l2_sqr(base.data() + i * dim, query, dim), i);
  }
  const size_t kk = static_cast<size_t>(std::min<int64_t>(k, count));
  std::partial_sort(all.begin(), all.begin() + kk, all.end());
  std::vector<int64_t> ids(kk);
  for (size_t i = 0; i < kk; ++i) {
    ids[i] = all[i].second;
  }
  return ids;
}

} /* namespace */

int main(int argc, char **argv)
{
  int64_t count = argc > 1 ? std::atoll(argv[1]) : 50000;
  int64_t dim = argc > 2 ? std::atoll(argv[2]) : 128;
  int32_t topk = argc > 3 ? static_cast<int32_t>(std::atoi(argv[3])) : 10;
  if (count <= 0 || dim <= 0 || topk <= 0) {
    std::fprintf(stderr, "usage: %s [count] [dim] [k]\n", argv[0]);
    return 1;
  }

  std::printf("bridge : %s\n", seekdb_hipvs_bridge_version());
  char device[256] = {0};
  const int probe = seekdb_hipvs_device_name(-1, device, sizeof(device));
  if (probe != SEEKDB_HIPVS_OK) {
    std::printf("device : unavailable (%s)\n", device);
    std::printf("result : SKIP - build with -DSEEKDB_HIPVS_BRIDGE_ENABLE_HIPVS=ON on ROCm\n");
    return 2;
  }
  std::printf("device : %s\n", device);

  std::mt19937 rng(42);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  std::vector<float> base(static_cast<size_t>(count * dim));
  for (float &v : base) {
    v = dist(rng);
  }

  seekdb_hipvs_build_params params{};
  params.dim = dim;
  params.count = count;
  params.graph_degree = 32;
  params.intermediate_graph_degree = 64;
  params.ef_construction = 200;
  params.build_algo = SEEKDB_HIPVS_ALGO_NN_DESCENT;
  params.metric = SEEKDB_HIPVS_METRIC_L2;
  params.device_id = -1;
  params.hierarchy = SEEKDB_HIPVS_HIERARCHY_CPU;

  char err[512] = {0};
  seekdb_hipvs_index *index = nullptr;
  const auto build_start = Clock::now();
  const int build_rc = seekdb_hipvs_build(base.data(),
                                          nullptr,
                                          count,
                                          &params,
                                          &index,
                                          err,
                                          sizeof(err));
  const double build_s = seconds_since(build_start);
  if (build_rc != SEEKDB_HIPVS_OK || index == nullptr) {
    std::fprintf(stderr, "build failed (%d): %s\n", build_rc, err);
    return 1;
  }
  std::printf("build  : %.3f s  (%.1f kvec/s, %lld x %lld, degree=%d)\n",
              build_s,
              static_cast<double>(count) / build_s / 1000.0,
              static_cast<long long>(count),
              static_cast<long long>(dim),
              params.graph_degree);

  /* recall over a small query sample: brute force is O(n*d) per query and this is a
   * smoke test, not a benchmark harness. */
  const int64_t n_queries = std::min<int64_t>(64, count);
  std::vector<float> queries(static_cast<size_t>(n_queries * dim));
  for (float &v : queries) {
    v = dist(rng);
  }
  std::vector<int64_t> out_ids(static_cast<size_t>(n_queries * topk), -1);
  std::vector<float> out_dist(static_cast<size_t>(n_queries * topk), 0.0f);
  const auto search_start = Clock::now();
  const int search_rc = seekdb_hipvs_search(index,
                                            queries.data(),
                                            n_queries,
                                            topk,
                                            params.ef_construction,
                                            out_ids.data(),
                                            out_dist.data(),
                                            err,
                                            sizeof(err));
  const double search_s = seconds_since(search_start);
  if (search_rc != SEEKDB_HIPVS_OK) {
    std::fprintf(stderr, "search failed (%d): %s\n", search_rc, err);
    seekdb_hipvs_free(index);
    return 1;
  }

  int64_t hit = 0;
  int64_t total = 0;
  for (int64_t q = 0; q < n_queries; ++q) {
    const std::vector<int64_t> truth =
        brute_force_topk(base, count, queries.data() + q * dim, dim, topk);
    const std::unordered_set<int64_t> truth_set(truth.begin(), truth.end());
    for (int32_t k = 0; k < topk; ++k) {
      const int64_t id = out_ids[static_cast<size_t>(q * topk + k)];
      if (id < 0) {
        continue;
      }
      ++total;
      hit += truth_set.count(id) ? 1 : 0;
    }
  }
  std::printf("search : %.1f ms for %lld queries (%.3f ms/query)\n",
              search_s * 1000.0,
              static_cast<long long>(n_queries),
              search_s * 1000.0 / static_cast<double>(n_queries));
  std::printf("recall@%d : %.4f\n", topk, total > 0 ? static_cast<double>(hit) / total : 0.0);

  const int export_rc = seekdb_hipvs_export_hnsw(index,
                                                 "seekdb_hipvs_selfcheck.hnsw",
                                                 SEEKDB_HIPVS_HIERARCHY_CPU,
                                                 params.ef_construction,
                                                 err,
                                                 sizeof(err));
  std::printf("export : %s\n",
              export_rc == SEEKDB_HIPVS_OK ? "seekdb_hipvs_selfcheck.hnsw" : err);

  seekdb_hipvs_free(index);
  return 0;
}
