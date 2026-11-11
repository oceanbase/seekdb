/*
 * Copyright (c) 2026 OceanBase.
 * SPDX-License-Identifier: Apache-2.0
 *
 * AMD GPU HNSW builder for seekdb (hipVS / ROCm backend).
 *
 * Pipeline (everything below runs inside this shared object, on a dedicated
 * OS thread created by seekdb, so the ROCm runtime never touches a coroutine
 * stack):
 *
 *   1. validate the request (finite FP32 corpus, unique VIDs, M / efConstruction
 *      bounds) *before* any GPU work, so a bad DDL never leaves a half-built
 *      index behind;
 *   2. cuvs::neighbors::cagra::build() -- the graph itself is built by GPU
 *      kernels (NN-descent refinement + prune), which is the compute we want to
 *      accelerate;
 *   3. cuvs::neighbors::hnsw::from_cagra() -- turn the CAGRA k-NN graph into a
 *      real HNSW graph. The base layer comes from the GPU graph; the upper
 *      layers are HNSW's hierarchy and are built with the multi-threaded CPU
 *      routine of the same library (they are O(N) insertions, not O(N * M)
 *      distance computations, so they stay a small fraction of the time);
 *   4. cuvs::neighbors::hnsw::serialize() -- write the classic hnswlib layout;
 *   5. remap external labels to seekdb VIDs and re-validate the whole file, then
 *      publish it atomically.
 *
 * Every step is timed and reported on stderr as a single machine readable line
 * so that a benchmark harness (and a human) can prove that the GPU did the work.
 */
#include "../hipvs_build_api.h"

#include "hipvs_snapshot_export.hpp"

#include <hip/hip_runtime.h>

#include <cuvs/neighbors/cagra.hpp>
#include <cuvs/neighbors/hnsw.hpp>
#include <raft/core/host_mdspan.hpp>
#include <raft/core/resources.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_set>

#if !defined(__HIP_PLATFORM_AMD__)
#error "The seekdb hipVS backend must be compiled with the AMD ROCm hipcc"
#endif

namespace {

using Clock = std::chrono::steady_clock;

inline double elapsed_ms(Clock::time_point from, Clock::time_point to) {
  return std::chrono::duration<double, std::milli>(to - from).count();
}

void throw_on_hip(hipError_t status, const char *what) {
  if (status != hipSuccess) {
    throw std::runtime_error(std::string(what) + ": " + hipGetErrorString(status));
  }
}

/* Emits one machine readable build record.
 *
 * The observer redirects fd 2 into its own log file and writes to it with its
 * own buffering, so a plain fprintf(stderr) can be interleaved with a server log
 * line and become unparsable. When SEEKDB_HIPVS_TRACE_FILE is set the record is
 * appended to a dedicated file with one write() call, otherwise it goes to
 * stderr with a single write() so it stays on one line. */
void emit_record(const char *line, size_t length) {
  const char *trace_file = std::getenv("SEEKDB_HIPVS_TRACE_FILE");
  if (nullptr != trace_file && '\0' != trace_file[0]) {
    const int fd = ::open(trace_file, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd >= 0) {
      const ssize_t written = ::write(fd, line, length);
      static_cast<void>(written);
      ::close(fd);
    }
  }
  const ssize_t written = ::write(STDERR_FILENO, line, length);
  static_cast<void>(written);
}

void set_error(char *error, size_t capacity, const char *message) {
  if (nullptr != error && capacity > 0) {
    std::snprintf(error, capacity, "%s", nullptr == message ? "" : message);
  }
}

/* The ROCm runtime is not documented as reentrant for concurrent index builds
 * in one process, and one build already saturates the device; serialise them. */
std::mutex &device_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::string device_description(int device, const hipDeviceProp_t &props) {
  char buffer[512] = {0};
  std::snprintf(buffer, sizeof(buffer),
                "backend=hipVS vendor=AMD arch=%s name=%s gcnArch=%s device=%d "
                "hbm=%.1fGiB",
                props.gcnArchName, props.name, props.gcnArchName, device,
                static_cast<double>(props.totalGlobalMem) / (1024.0 * 1024.0 * 1024.0));
  return std::string(buffer);
}

/* Temporary files carrying the raw hipVS export and the converted snapshot. The
 * destructor guarantees they disappear on every exit path (success, error and
 * exception), which keeps /tmp clean on a long-running server. */
class StagingFiles {
 public:
  explicit StagingFiles(const std::string &final_path)
      : raw_(final_path + ".hipvs.raw"), converted_(final_path + ".hipvs.part") {}

  ~StagingFiles() {
    std::remove(raw_.c_str());
    std::remove(converted_.c_str());
  }

  const std::string &raw() const { return raw_; }
  const std::string &converted() const { return converted_; }

 private:
  std::string raw_;
  std::string converted_;
};

}  // namespace

/* The backend is compiled with -fvisibility=hidden so that only the three C
 * entry points below are reachable from the seekdb server. */
#define SEEKDB_HIPVS_EXPORT extern "C" __attribute__((visibility("default")))

SEEKDB_HIPVS_EXPORT int32_t seekdb_hipvs_abi_version_v1(void) { return SEEKDB_HIPVS_ABI_VERSION; }

SEEKDB_HIPVS_EXPORT int32_t seekdb_hipvs_probe_v1(char *info, size_t info_capacity) {
  if (nullptr != info && info_capacity > 0) info[0] = '\0';
  try {
    std::lock_guard<std::mutex> guard(device_mutex());
    int device_count = 0;
    throw_on_hip(hipGetDeviceCount(&device_count), "hipGetDeviceCount");
    if (device_count < 1) return SEEKDB_HIPVS_ERR_NO_GPU;
    int device = 0;
    throw_on_hip(hipGetDevice(&device), "hipGetDevice");
    hipDeviceProp_t props{};
    throw_on_hip(hipGetDeviceProperties(&props, device), "hipGetDeviceProperties");
    const std::string text = device_description(device, props);
    if (nullptr != info && info_capacity > 0) {
      std::snprintf(info, info_capacity, "%s", text.c_str());
    }
    return SEEKDB_HIPVS_OK;
  } catch (const std::exception &e) {
    set_error(info, info_capacity, e.what());
    return SEEKDB_HIPVS_ERR_GPU_RUNTIME;
  }
}

SEEKDB_HIPVS_EXPORT int32_t seekdb_hipvs_build_v1(const float *vectors,
                                         const int64_t *vids,
                                         int64_t count,
                                         int64_t dim,
                                         int64_t max_degree,
                                         int64_t ef_construction,
                                         const char *output_path,
                                         char *error,
                                         size_t error_capacity) {
  set_error(error, error_capacity, "");
  try {
    /* ---- 1. request validation, before any GPU state is touched ---------- */
    if (nullptr == vectors || nullptr == vids || nullptr == output_path || '\0' == output_path[0]) {
      set_error(error, error_capacity, "null corpus, vids or output path");
      return SEEKDB_HIPVS_ERR_INVALID_ARGUMENT;
    }
    if (dim < 1 || dim > INT_MAX || count <= 0 || count > INT_MAX || max_degree < 2 ||
        max_degree > 1024 || ef_construction < max_degree || ef_construction > INT_MAX) {
      set_error(error, error_capacity,
                "invalid HNSW parameters (need 1<=dim, 2<=M<=1024, M<=efConstruction, "
                "0<N<=INT_MAX)");
      return SEEKDB_HIPVS_ERR_INVALID_ARGUMENT;
    }
    if (count <= 2 * max_degree) {
      set_error(error, error_capacity,
                "corpus too small for the requested M: need N > 2*M, because CAGRA builds a "
                "graph of degree 2*M");
      return SEEKDB_HIPVS_ERR_INVALID_ARGUMENT;
    }
    if (static_cast<uint64_t>(count) >
        SIZE_MAX / sizeof(float) / static_cast<uint64_t>(dim)) {
      set_error(error, error_capacity, "corpus size overflows size_t");
      return SEEKDB_HIPVS_ERR_INVALID_ARGUMENT;
    }
    {
      std::unordered_set<int64_t> unique_vids;
      unique_vids.reserve(static_cast<size_t>(count));
      for (int64_t row = 0; row < count; ++row) {
        if (!unique_vids.insert(vids[row]).second) {
          set_error(error, error_capacity, "duplicate document id in snapshot");
          return SEEKDB_HIPVS_ERR_INVALID_ARGUMENT;
        }
      }
    }
    for (int64_t i = 0; i < count * dim; ++i) {
      if (!std::isfinite(vectors[i])) {
        set_error(error, error_capacity, "non-finite component in the vector corpus");
        return SEEKDB_HIPVS_ERR_INVALID_ARGUMENT;
      }
    }

    /* ---- 2. GPU presence: explicit failure instead of a hidden CPU fallback */
    std::lock_guard<std::mutex> guard(device_mutex());
    int device_count = 0;
    try {
      throw_on_hip(hipGetDeviceCount(&device_count), "hipGetDeviceCount");
    } catch (const std::exception &e) {
      set_error(error, error_capacity, e.what());
      return SEEKDB_HIPVS_ERR_NO_GPU;
    }
    if (device_count < 1) {
      set_error(error, error_capacity,
                "no AMD ROCm GPU visible to the hipVS backend; CPU fallback is disabled");
      return SEEKDB_HIPVS_ERR_NO_GPU;
    }
    int device = 0;
    hipDeviceProp_t props{};
    try {
      throw_on_hip(hipGetDevice(&device), "hipGetDevice");
      throw_on_hip(hipGetDeviceProperties(&props, device), "hipGetDeviceProperties");
    } catch (const std::exception &e) {
      set_error(error, error_capacity, e.what());
      return SEEKDB_HIPVS_ERR_GPU_RUNTIME;
    }

    const std::string gpu = device_description(device, props);
    StagingFiles staging(output_path);
    const auto t_begin = Clock::now();

    /* ---- 3. GPU graph construction --------------------------------------- */
    raft::resources resources;
    const auto corpus = raft::make_host_matrix_view<const float, int64_t>(vectors, count, dim);

    /* Graph construction parameters.
     *
     * hnsw::to_cagra_params() (IVF-PQ based k-NN graph) is the fast path, but it
     * trades recall for speed: measured on 20k random 128-d vectors it produced
     * ~10 recall points less than the CPU HNSW build at the same M and ef. The
     * off-by-default NN-descent construction below costs more GPU time but
     * produces the same recall regime as the CPU baseline, which is what a
     * DDL-level accelerator must do - the index is used by real queries.
     *
     * Degrees are chosen to mirror HNSW semantics: the base layer stores up to
     * 2*M neighbours (hnswlib's maxM0), so the k-NN graph is built with degree
     * 2*M and a wider intermediate graph before pruning. */
    cuvs::neighbors::cagra::index_params cagra_params;
    cagra_params.metric = cuvs::distance::DistanceType::L2Expanded;
    cagra_params.graph_degree = static_cast<size_t>(2 * max_degree);
    cagra_params.intermediate_graph_degree = static_cast<size_t>(
        std::min<int64_t>(std::max<int64_t>(4 * max_degree, 64), std::max<int64_t>(count, 64)));
    {
      cuvs::neighbors::graph_build_params::nn_descent_params nn_params(
          cagra_params.intermediate_graph_degree, cuvs::distance::DistanceType::L2Expanded);
      nn_params.graph_degree = cagra_params.graph_degree;
      /* Converge further than the default: the graph is built once and then
       * served for the lifetime of the index. */
      nn_params.max_iterations = 30;
      nn_params.termination_threshold = 1e-5F;
      cagra_params.graph_build_params = nn_params;
    }
    auto graph = cuvs::neighbors::cagra::build(resources, cagra_params, corpus);
    throw_on_hip(hipDeviceSynchronize(), "hipDeviceSynchronize after CAGRA build");
    const auto t_graph = Clock::now();

    /* ---- 4. HNSW hierarchy on top of the GPU graph ------------------------ */
    cuvs::neighbors::hnsw::index_params hnsw_params;
    hnsw_params.hierarchy = cuvs::neighbors::hnsw::HnswHierarchy::CPU;
    hnsw_params.ef_construction = static_cast<int>(ef_construction);
    hnsw_params.num_threads = 0; /* auto: use every core of the host */
    auto hnsw_index = cuvs::neighbors::hnsw::from_cagra(resources, hnsw_params, graph, corpus);
    cuvs::neighbors::hnsw::serialize(resources, staging.raw(), *hnsw_index);
    throw_on_hip(hipDeviceSynchronize(), "hipDeviceSynchronize after HNSW conversion");
    const auto t_hnsw = Clock::now();

    /* ---- 5. label remap to seekdb VIDs, validation, atomic publish -------- */
    seekdb_hipvs::remap_snapshot_labels(staging.raw(), staging.converted(), vectors, vids,
                                        static_cast<uint64_t>(count), static_cast<uint64_t>(dim),
                                        static_cast<uint64_t>(max_degree));
    if (0 != std::rename(staging.converted().c_str(), output_path)) {
      set_error(error, error_capacity, "cannot publish the converted snapshot");
      return SEEKDB_HIPVS_ERR_IO;
    }
    const auto t_done = Clock::now();

    char record[1024] = {0};
    const int record_length = std::snprintf(
        record, sizeof(record),
        "SEEKDB_HIPVS_BUILD {\"gpu_compute\":true,\"cpu_fallback\":false,\"device\":\"%s\","
        "\"rows\":%lld,\"dim\":%lld,\"m\":%lld,\"ef_construction\":%lld,"
        "\"gpu_graph_ms\":%.3f,\"hnsw_hierarchy_and_export_ms\":%.3f,"
        "\"label_remap_ms\":%.3f,\"backend_total_ms\":%.3f}\n",
        gpu.c_str(), static_cast<long long>(count), static_cast<long long>(dim),
        static_cast<long long>(max_degree), static_cast<long long>(ef_construction),
        elapsed_ms(t_begin, t_graph), elapsed_ms(t_graph, t_hnsw), elapsed_ms(t_hnsw, t_done),
        elapsed_ms(t_begin, t_done));
    if (record_length > 0) {
      emit_record(record, static_cast<size_t>(
                              std::min<int>(record_length, static_cast<int>(sizeof(record) - 1))));
    }
    return SEEKDB_HIPVS_OK;
  } catch (const std::bad_alloc &) {
    set_error(error, error_capacity, "out of memory in the hipVS backend");
    return SEEKDB_HIPVS_ERR_INTERNAL;
  } catch (const std::exception &e) {
    set_error(error, error_capacity, e.what());
    return SEEKDB_HIPVS_ERR_INTERNAL;
  } catch (...) {
    set_error(error, error_capacity, "unknown exception in the hipVS backend");
    return SEEKDB_HIPVS_ERR_INTERNAL;
  }
}