/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
// Optional AMD-only build backend. No GPU query path and no CPU fallback.
#include <hip/hip_runtime.h>
#include <cuvs/core/c_api.h>
#include <cuvs/neighbors/cagra.h>
#include <cuvs/neighbors/hnsw.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <pthread.h>
#include <stdexcept>
#include <string>

namespace {
std::mutex build_mutex;
void check(cuvsError_t status, const char *operation)
{
  if (status != CUVS_SUCCESS) {
    const char *detail = cuvsGetLastErrorText();
    throw std::runtime_error(std::string(operation) + ": " + (detail ? detail : "unknown hipVS error"));
  }
}
void check_hip(hipError_t status, const char *operation)
{
  if (status != hipSuccess) throw std::runtime_error(std::string(operation) + ": " + hipGetErrorString(status));
}
struct Job {
  const float *vectors;
  int64_t rows;
  int dim, m, ef, threads;
  const char *path;
  char *error;
  size_t capacity;
  int status = 1;
};
void *run_build(void *opaque)
{
  Job &job = *static_cast<Job *>(opaque);
  // Keep destruction inside the same critical section as construction: the
  // process-wide hipVS allocator must not be torn down during another build.
  std::lock_guard<std::mutex> lock(build_mutex);
  cuvsResources_t resources = 0;
  cuvsCagraIndexParams_t cp = nullptr;
  cuvsCagraIndex_t ci = nullptr;
  cuvsHnswIndexParams_t hp = nullptr;
  cuvsHnswIndex_t hi = nullptr;
  bool have_resources = false;
  try {
    int count = 0;
    check_hip(hipGetDeviceCount(&count), "hipGetDeviceCount");
    if (count < 1) throw std::runtime_error("no AMD GPU available; CPU fallback is disabled");
    check_hip(hipSetDevice(0), "hipSetDevice");
    hipDeviceProp_t prop{};
    check_hip(hipGetDeviceProperties(&prop, 0), "hipGetDeviceProperties");
    if (std::strncmp(prop.gcnArchName, "gfx", 3) != 0) throw std::runtime_error("not an AMD GPU");
    check(cuvsResourcesCreate(&resources), "resources create");
    have_resources = true;
    check(cuvsCagraIndexParamsCreate(&cp), "CAGRA params create");
    check(cuvsCagraIndexCreate(&ci), "CAGRA index create");
    cp->metric = L2Expanded;
    cp->graph_degree = 2 * job.m;
    cp->intermediate_graph_degree = std::min<int64_t>(job.rows - 1, 4 * job.m);
    cp->build_algo = NN_DESCENT;
    cp->nn_descent_niter = 20;
    cp->compression = nullptr;
    int64_t shape[] = {job.rows, job.dim};
    DLManagedTensor data{};
    data.dl_tensor.data = const_cast<float *>(job.vectors);
    data.dl_tensor.device = {kDLCPU, 0};
    data.dl_tensor.ndim = 2;
    data.dl_tensor.dtype = {kDLFloat, 32, 1};
    data.dl_tensor.shape = shape;
    check(cuvsCagraBuild(resources, cp, &data, ci), "GPU CAGRA build");
    check(cuvsStreamSync(resources), "CAGRA synchronize");
    check(cuvsHnswIndexParamsCreate(&hp), "HNSW params create");
    check(cuvsHnswIndexCreate(&hi), "HNSW index create");
    hp->hierarchy = CPU;
    hp->ef_construction = job.ef;
    hp->num_threads = job.threads;
    // GPU supplies the base graph; CPU only completes the upper hierarchy.
    check(cuvsHnswFromCagraWithDataset(resources, hp, ci, hi, &data), "CAGRA to mutable HNSW");
    check(cuvsStreamSync(resources), "HNSW synchronize");
    check(cuvsHnswSerialize(resources, job.path, hi), "native HNSW export");
    check(cuvsStreamSync(resources), "export synchronize");
    std::fprintf(stderr, "[seekdb-hipvs-build] backend=hipvs device_vendor=AMD gpu_name=\"%s\" arch=%s rows=%lld dim=%d gpu_compute=true cpu_fallback=false export_completed=true\n",
                 prop.name, prop.gcnArchName, static_cast<long long>(job.rows), job.dim);
    job.status = 0;
  } catch (const std::exception &error) {
    std::snprintf(job.error, job.capacity, "%s", error.what());
  } catch (...) {
    std::snprintf(job.error, job.capacity, "unhandled hipVS build exception");
  }
  if (hi) cuvsHnswIndexDestroy(hi);
  if (hp) cuvsHnswIndexParamsDestroy(hp);
  if (ci) cuvsCagraIndexDestroy(ci);
  if (cp) cuvsCagraIndexParamsDestroy(cp);
  if (have_resources) cuvsResourcesDestroy(resources);
  return nullptr;
}
} // namespace

extern "C" int seekdb_hipvs_build_hnsw_v1(const float *vectors, int64_t rows, int32_t dim,
    int32_t m, int32_t ef, int32_t threads, const char *path, char *error, size_t capacity)
{
  if (!error || capacity == 0) return 1;
  error[0] = 0;
  if (!vectors || !path || rows <= 2 * m || rows > INT32_MAX || dim <= 0 ||
      m < 5 || m > 128 || ef <= m || threads < 1) {
    std::snprintf(error, capacity, "hipVS requires FP32/L2 rows > 2*M, 5<=M<=128 and valid dimensions/threads");
    return 1;
  }
  for (int64_t i = 0; i < rows * static_cast<int64_t>(dim); ++i) {
    if (!std::isfinite(vectors[i])) {
      std::snprintf(error, capacity, "non-finite input vector");
      return 1;
    }
  }
  Job job{vectors, rows, dim, m, ef, threads, path, error, capacity};
  // ROCm initialization can exceed seekdb worker coroutine stack size.
  pthread_attr_t attr;
  int rc = pthread_attr_init(&attr);
  if (rc == 0) {
    rc = pthread_attr_setstacksize(&attr, 32ULL * 1024 * 1024);
    pthread_t worker;
    if (rc == 0) rc = pthread_create(&worker, &attr, run_build, &job);
    pthread_attr_destroy(&attr);
    if (rc == 0) rc = pthread_join(worker, nullptr);
  }
  if (rc != 0) {
    std::snprintf(error, capacity, "hipVS build worker failed: %s", std::strerror(rc));
    return 1;
  }
  return job.status;
}
