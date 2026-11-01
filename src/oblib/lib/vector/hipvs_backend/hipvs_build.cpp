// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "../hipvs_build_api.h"
#include "hnsw_stream.hpp"
#include <hip/hip_runtime.h>
#include <cuvs/neighbors/cagra.hpp>
#include <cuvs/neighbors/hnsw.hpp>
#include <raft/core/host_mdspan.hpp>
#include <raft/core/resources.hpp>
#include <chrono>
#include <climits>
#include <cstdio>
#include <mutex>
#include <unordered_set>

#if !defined(__HIP_PLATFORM_AMD__)
#error "Build this backend using AMD ROCm hipcc"
#endif

namespace {
void hip_check(hipError_t status)
{
  if (status != hipSuccess) throw std::runtime_error(hipGetErrorString(status));
}

struct TemporaryFiles {
  std::string raw;
  std::string part;
  explicit TemporaryFiles(const char *path) : raw(std::string(path) + ".raw"), part(std::string(path) + ".part") {}
  ~TemporaryFiles() { std::remove(raw.c_str()); std::remove(part.c_str()); }
};
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point a, Clock::time_point b)
{
  return std::chrono::duration<double, std::milli>(b - a).count();
}
}

extern "C" int seekdb_hipvs_build_v1(const float *vectors, const int64_t *vids,
    int64_t count, int64_t dim, int64_t max_degree, int64_t ef_construction,
    const char *output_path, char *error, size_t error_capacity)
{
  if (error && error_capacity) error[0] = '\0';
  try {
    if (!vectors || !vids || !output_path || !output_path[0] || dim < 1 || dim > INT_MAX ||
        max_degree < 2 || max_degree > 1024 || ef_construction < max_degree ||
        ef_construction > INT_MAX || count <= 2 * max_degree || count > INT_MAX ||
        static_cast<uint64_t>(count) > SIZE_MAX / sizeof(float) / static_cast<uint64_t>(dim)) {
      throw std::invalid_argument("HIPVS requires finite FP32/L2, 2*M < N <= INT_MAX, 2 <= M <= 1024, efc >= M");
    }
    // Validation happens before any GPU computation or partial graph publication.
    std::unordered_set<int64_t> unique;
    unique.reserve(count);
    for (int64_t row = 0; row < count; ++row) {
      if (!unique.insert(vids[row]).second) throw std::invalid_argument("duplicate seekdb VID");
    }
    for (int64_t i = 0; i < count * dim; ++i) {
      if (!std::isfinite(vectors[i])) throw std::invalid_argument("non-finite vector component");
    }
    static std::mutex device_mutex;
    std::lock_guard<std::mutex> lock(device_mutex);
    int devices = 0;
    hip_check(hipGetDeviceCount(&devices));
    if (devices < 1) throw std::runtime_error("no AMD ROCm GPU; CPU fallback disabled");
    int device = 0;
    hip_check(hipGetDevice(&device));
    hipDeviceProp_t properties{};
    hip_check(hipGetDeviceProperties(&properties, device));
    TemporaryFiles files(output_path);
    const auto start = Clock::now();
    raft::resources resources;
    const auto dataset = raft::make_host_matrix_view<const float, int64_t>(vectors, count, dim);
    auto parameters = cuvs::neighbors::hnsw::to_cagra_params(dataset.extents(),
        static_cast<int>(max_degree), static_cast<int>(ef_construction),
        cuvs::distance::DistanceType::L2Expanded);
    auto graph = cuvs::neighbors::cagra::build(resources, parameters, dataset);
    hip_check(hipDeviceSynchronize());
    const auto graph_done = Clock::now();
    cuvs::neighbors::hnsw::index_params hnsw_parameters;
    hnsw_parameters.hierarchy = cuvs::neighbors::hnsw::HnswHierarchy::CPU;
    hnsw_parameters.ef_construction = static_cast<int>(ef_construction);
    hnsw_parameters.num_threads = 0;
    auto hnsw = cuvs::neighbors::hnsw::from_cagra(resources, hnsw_parameters, graph, dataset);
    cuvs::neighbors::hnsw::serialize(resources, files.raw, *hnsw);
    hip_check(hipDeviceSynchronize());
    const auto hierarchy_done = Clock::now();
    seekdb_hipvs::remap_hnsw_stream(files.raw, files.part, vectors, vids,
                                   count, dim, max_degree);
    if (std::rename(files.part.c_str(), output_path) != 0) {
      throw std::runtime_error("cannot publish validated HIPVS stream");
    }
    const auto finish = Clock::now();
    std::fprintf(stderr, "SEEKDB_HIPVS {\"gpu_compute\":true,\"cpu_fallback\":false,"
        "\"arch\":\"%s\",\"rows\":%lld,\"dim\":%lld,\"m\":%lld,\"ef_construction\":%lld,"
        "\"gpu_graph_ms\":%.3f,\"cpu_hierarchy_and_export_ms\":%.3f,\"remap_ms\":%.3f,\"backend_ms\":%.3f}\n",
        properties.gcnArchName, static_cast<long long>(count), static_cast<long long>(dim),
        static_cast<long long>(max_degree), static_cast<long long>(ef_construction),
        milliseconds(start, graph_done), milliseconds(graph_done, hierarchy_done),
        milliseconds(hierarchy_done, finish), milliseconds(start, finish));
    return 0;
  } catch (const std::exception &exception) {
    if (error && error_capacity) std::snprintf(error, error_capacity, "%s", exception.what());
  } catch (...) {
    if (error && error_capacity) std::snprintf(error, error_capacity, "%s", "unknown HIPVS exception");
  }
  return -1;
}
