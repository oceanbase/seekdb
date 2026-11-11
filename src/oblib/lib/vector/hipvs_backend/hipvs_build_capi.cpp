/*
 * Copyright (c) 2026 OceanBase.
 * SPDX-License-Identifier: Apache-2.0
 *
 * AMD GPU HNSW builder for seekdb (hipVS / ROCm backend), C-API variant.
 *
 * Why this variant exists
 * ----------------------
 * The ROCm hipVS distribution used by the competition image ships the *runtime*
 * package: the self-contained C API headers (cuvs/neighbors/cagra.h,
 * cuvs/neighbors/hnsw.h), the shared objects (libcuvs_c.so, libcuvs.so) and the
 * examples, but no raft/CCL C++ headers and no cuvs CMake package. The C++
 * backend in hipvs_build.cpp therefore cannot be compiled against it, while this
 * file needs nothing but the published C ABI.
 *
 * Pipeline (identical in semantics to the C++ backend):
 *
 *   1. validate the request (finite FP32 corpus, unique VIDs, M/efConstruction
 *      bounds) before touching any GPU state;
 *   2. cuvsCagraBuild() -- the k-NN graph is built by GPU kernels (NN-descent),
 *      which is the compute we want to accelerate;
 *   3. cuvsHnswFromCagra() -- turn the CAGRA graph into a real HNSW graph (the
 *      base layer is the GPU graph, the O(N) upper layers are the HNSW library's);
 *   4. cuvsHnswSerialize() -- write the classic hnswlib layout;
 *   5. remap the internal row numbers to seekdb VIDs, re-validate the file and
 *      publish it atomically.
 *
 * Build (no hipcc, no CMake package needed):
 *   g++ -O3 -std=c++17 -fPIC -shared -fvisibility=hidden \
 *       -I/opt/hipvs/include -I/opt/rocm/include -I.. \
 *       hipvs_build_capi.cpp -o build/libseekdb_hipvs.so \
 *       -L/opt/hipvs/lib -lcuvs_c -lcuvs -L/opt/rocm/lib -lamdhip64 \
 *       -Wl,--no-undefined
 */
#include "../hipvs_build_api.h"

#include "hipvs_snapshot_export.hpp"

#include <cuvs/core/c_api.h>
#include <cuvs/neighbors/cagra.h>
#include <cuvs/neighbors/hnsw.h>
#include <cuvs/cuda_runtime.h>
#include <dlpack/dlpack.h>

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
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point from, Clock::time_point to) {
  return std::chrono::duration<double, std::milli>(to - from).count();
}

void set_error(char *error, size_t capacity, const char *message) {
  if (nullptr != error && capacity > 0) {
    std::snprintf(error, capacity, "%s", nullptr == message ? "" : message);
  }
}

/* One machine readable build record, written with a single write() so it cannot
 * interleave with a buffered server log line. SEEKDB_HIPVS_TRACE_FILE redirects
 * it to a dedicated file for benchmark harnesses. */
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

/* One build already saturates the device; serialise concurrent builds. */
std::mutex &device_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::string device_description(int device, const hipDeviceProp_t &props) {
  char buffer[1024] = {0};
  std::snprintf(buffer, sizeof(buffer),
                "backend=hipVS_capi vendor=AMD arch=%s name=%s device=%d hbm=%.1fGiB",
                props.gcnArchName, props.name, device,
                static_cast<double>(props.totalGlobalMem) / (1024.0 * 1024.0 * 1024.0));
  return std::string(buffer);
}

const char *cuvs_status_name(cuvsError_t status) {
  return CUVS_SUCCESS == status ? "CUVS_SUCCESS" : "CUVS_ERROR";
}

/* RAII owner of every cuvs handle, so no early return can leak device memory or
 * a live HNSW index. */
struct CuvsHandles {
  cuvsResources_t res = 0;
  cuvsCagraIndexParams_t cagra_params = nullptr;
  cuvsCagraIndex_t cagra_index = nullptr;
  cuvsHnswIndexParams_t hnsw_params = nullptr;
  cuvsHnswIndex_t hnsw_index = nullptr;

  ~CuvsHandles() {
    if (nullptr != hnsw_index) cuvsHnswIndexDestroy(hnsw_index);
    if (nullptr != hnsw_params) cuvsHnswIndexParamsDestroy(hnsw_params);
    if (nullptr != cagra_index) cuvsCagraIndexDestroy(cagra_index);
    if (nullptr != cagra_params) cuvsCagraIndexParamsDestroy(cagra_params);
    if (0 != res) cuvsResourcesDestroy(res);
  }
};

/* Staging files: the raw cuvs export and the converted snapshot. */
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

/* Host DLManagedTensor over the caller's corpus (cuVS copies it to the device). */
void init_host_tensor(DLManagedTensor &tensor,
                      const float *data,
                      int64_t rows,
                      int64_t cols,
                      int64_t *shape) {
  std::memset(&tensor, 0, sizeof(tensor));
  shape[0] = rows;
  shape[1] = cols;
  tensor.dl_tensor.data = const_cast<float *>(data);
  tensor.dl_tensor.device.device_type = kDLCPU;
  tensor.dl_tensor.device.device_id = 0;
  tensor.dl_tensor.ndim = 2;
  tensor.dl_tensor.dtype.code = kDLFloat;
  tensor.dl_tensor.dtype.bits = 32;
  tensor.dl_tensor.dtype.lanes = 1;
  tensor.dl_tensor.shape = shape;
  tensor.dl_tensor.strides = nullptr;
  tensor.dl_tensor.byte_offset = 0;
  tensor.manager_ctx = nullptr;
  tensor.deleter = nullptr;
}

}  // namespace

#define SEEKDB_HIPVS_EXPORT extern "C" __attribute__((visibility("default")))

SEEKDB_HIPVS_EXPORT int32_t seekdb_hipvs_abi_version_v1(void) { return SEEKDB_HIPVS_ABI_VERSION; }

SEEKDB_HIPVS_EXPORT int32_t seekdb_hipvs_probe_v1(char *info, size_t info_capacity) {
  if (nullptr != info && info_capacity > 0) info[0] = '\0';
  try {
    std::lock_guard<std::mutex> guard(device_mutex());
    int device_count = 0;
    if (hipSuccess != hipGetDeviceCount(&device_count) || device_count < 1) {
      set_error(info, info_capacity, "no AMD ROCm GPU visible to the hipVS C API backend");
      return SEEKDB_HIPVS_ERR_NO_GPU;
    }
    int device = 0;
    hipDeviceProp_t props{};
    if (hipSuccess != hipGetDevice(&device) ||
        hipSuccess != hipGetDeviceProperties(&props, device)) {
      set_error(info, info_capacity, "cannot query the AMD GPU properties");
      return SEEKDB_HIPVS_ERR_GPU_RUNTIME;
    }
    const std::string text = device_description(device, props);
    if (nullptr != info && info_capacity > 0) {
      std::snprintf(info, info_capacity, "%s", text.c_str());
    }
    return SEEKDB_HIPVS_OK;
  } catch (const std::exception &e) {
    set_error(info, info_capacity, e.what());
    return SEEKDB_HIPVS_ERR_GPU_RUNTIME;
  } catch (...) {
    set_error(info, info_capacity, "unknown exception in the hipVS C API probe");
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
                "invalid HNSW parameters (need 1<=dim, 2<=M<=1024, M<=efConstruction, 0<N<=INT_MAX)");
      return SEEKDB_HIPVS_ERR_INVALID_ARGUMENT;
    }
    if (count <= 2 * max_degree) {
      set_error(error, error_capacity,
                "corpus too small for the requested M: need N > 2*M, because CAGRA builds a graph "
                "of degree 2*M");
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

    std::lock_guard<std::mutex> guard(device_mutex());
    int device_count = 0;
    if (hipSuccess != hipGetDeviceCount(&device_count) || device_count < 1) {
      set_error(error, error_capacity,
                "no AMD ROCm GPU visible to the hipVS C API backend; CPU fallback is disabled "
                "inside the backend");
      return SEEKDB_HIPVS_ERR_NO_GPU;
    }
    int device = 0;
    hipDeviceProp_t props{};
    if (hipSuccess != hipGetDevice(&device) ||
        hipSuccess != hipGetDeviceProperties(&props, device)) {
      set_error(error, error_capacity, "cannot query the AMD GPU properties");
      return SEEKDB_HIPVS_ERR_GPU_RUNTIME;
    }
    const std::string gpu = device_description(device, props);

    CuvsHandles handles;
    StagingFiles staging(output_path);
    const auto t_begin = Clock::now();

    if (CUVS_SUCCESS != cuvsResourcesCreate(&handles.res)) {
      set_error(error, error_capacity, "cuvsResourcesCreate failed");
      return SEEKDB_HIPVS_ERR_GPU_RUNTIME;
    }

    /* ---- 2. GPU graph construction --------------------------------------- */
    if (CUVS_SUCCESS != cuvsCagraIndexParamsCreate(&handles.cagra_params)) {
      set_error(error, error_capacity, "cuvsCagraIndexParamsCreate failed");
      return SEEKDB_HIPVS_ERR_INTERNAL;
    }
    handles.cagra_params->metric = L2Expanded;
    handles.cagra_params->graph_degree = static_cast<size_t>(2 * max_degree);
    handles.cagra_params->intermediate_graph_degree = static_cast<size_t>(
        std::min<int64_t>(std::max<int64_t>(4 * max_degree, 64), std::max<int64_t>(count, 64)));
    /* NN-descent (not IVF-PQ) keeps recall in the same regime as the CPU HNSW
     * baseline; the graph is built once and then served for the index lifetime. */
    handles.cagra_params->build_algo = NN_DESCENT;
    handles.cagra_params->nn_descent_niter = 30;

    int64_t shape[2] = {0, 0};
    DLManagedTensor dataset_tensor;
    init_host_tensor(dataset_tensor, vectors, count, dim, shape);

    if (CUVS_SUCCESS != cuvsCagraIndexCreate(&handles.cagra_index)) {
      set_error(error, error_capacity, "cuvsCagraIndexCreate failed");
      return SEEKDB_HIPVS_ERR_INTERNAL;
    }
    const cuvsError_t build_status =
        cuvsCagraBuild(handles.res, handles.cagra_params, &dataset_tensor, handles.cagra_index);
    if (CUVS_SUCCESS != build_status) {
      set_error(error, error_capacity, "cuvsCagraBuild failed");
      return SEEKDB_HIPVS_ERR_GPU_RUNTIME;
    }
    if (hipSuccess != hipDeviceSynchronize()) {
      set_error(error, error_capacity, "hipDeviceSynchronize after cuvsCagraBuild failed");
      return SEEKDB_HIPVS_ERR_GPU_RUNTIME;
    }
    const auto t_graph = Clock::now();

    /* ---- 3. HNSW hierarchy on top of the GPU graph ------------------------ */
    if (CUVS_SUCCESS != cuvsHnswIndexParamsCreate(&handles.hnsw_params)) {
      set_error(error, error_capacity, "cuvsHnswIndexParamsCreate failed");
      return SEEKDB_HIPVS_ERR_INTERNAL;
    }
    handles.hnsw_params->hierarchy = CPU;
    handles.hnsw_params->ef_construction = static_cast<int>(ef_construction);
    handles.hnsw_params->num_threads = 0; /* auto: every host core */
    if (CUVS_SUCCESS != cuvsHnswIndexCreate(&handles.hnsw_index)) {
      set_error(error, error_capacity, "cuvsHnswIndexCreate failed");
      return SEEKDB_HIPVS_ERR_INTERNAL;
    }
    cuvsError_t from_cagra = cuvsHnswFromCagra(handles.res, handles.hnsw_params,
                                               handles.cagra_index, handles.hnsw_index);
    if (CUVS_SUCCESS != from_cagra) {
      /* Some builds need the corpus explicitly when the CAGRA index does not own
       * the dataset it was built from. */
      from_cagra = cuvsHnswFromCagraWithDataset(handles.res, handles.hnsw_params,
                                                handles.cagra_index, handles.hnsw_index,
                                                &dataset_tensor);
    }
    if (CUVS_SUCCESS != from_cagra) {
      set_error(error, error_capacity, "cuvsHnswFromCagra failed");
      return SEEKDB_HIPVS_ERR_GPU_RUNTIME;
    }
    if (CUVS_SUCCESS != cuvsStreamSync(handles.res)) {
      set_error(error, error_capacity, "cuvsStreamSync after cuvsHnswFromCagra failed");
      return SEEKDB_HIPVS_ERR_GPU_RUNTIME;
    }

    /* ---- 4. serialize the classic hnswlib layout -------------------------- */
    if (CUVS_SUCCESS != cuvsHnswSerialize(handles.res, staging.raw().c_str(), handles.hnsw_index)) {
      set_error(error, error_capacity, "cuvsHnswSerialize failed");
      return SEEKDB_HIPVS_ERR_IO;
    }
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
        "SEEKDB_HIPVS_BUILD {\"gpu_compute\":true,\"cpu_fallback\":false,\"api\":\"cuvs_c\","
        "\"device\":\"%s\",\"rows\":%lld,\"dim\":%lld,\"m\":%lld,\"ef_construction\":%lld,"
        "\"gpu_graph_ms\":%.3f,\"hnsw_hierarchy_and_export_ms\":%.3f,\"label_remap_ms\":%.3f,"
        "\"backend_total_ms\":%.3f}\n",
        gpu.c_str(), static_cast<long long>(count), static_cast<long long>(dim),
        static_cast<long long>(max_degree), static_cast<long long>(ef_construction),
        elapsed_ms(t_begin, t_graph), elapsed_ms(t_graph, t_hnsw), elapsed_ms(t_hnsw, t_done),
        elapsed_ms(t_begin, t_done));
    if (record_length > 0) {
      emit_record(record, static_cast<size_t>(
                              std::min<int>(record_length, static_cast<int>(sizeof(record) - 1))));
    }
    static_cast<void>(cuvs_status_name);
    return SEEKDB_HIPVS_OK;
  } catch (const std::bad_alloc &) {
    set_error(error, error_capacity, "out of memory in the hipVS C API backend");
    return SEEKDB_HIPVS_ERR_INTERNAL;
  } catch (const std::exception &e) {
    set_error(error, error_capacity, e.what());
    return SEEKDB_HIPVS_ERR_INTERNAL;
  } catch (...) {
    set_error(error, error_capacity, "unknown exception in the hipVS C API backend");
    return SEEKDB_HIPVS_ERR_INTERNAL;
  }
}