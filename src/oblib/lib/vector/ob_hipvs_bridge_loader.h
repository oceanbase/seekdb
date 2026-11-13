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

#ifndef OCEANBASE_OBLIB_VECTOR_OB_HIPVS_BRIDGE_LOADER_H_
#define OCEANBASE_OBLIB_VECTOR_OB_HIPVS_BRIDGE_LOADER_H_

#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Runtime loader for the hipVS bridge (libseekdb_cuvs_bridge.so).
 *
 * seekdb never links against hipVS: the vector index layer resolves the bridge with
 * dlopen()/dlsym() the first time an index opts in with `lib=cuvs` and keeps the
 * function table in a process-wide singleton.  Nothing here allocates or opens a
 * device by itself, so on a CPU-only build the cost of this header is one failed
 * dlopen() that is never retried.
 *
 * The ABI below mirrors tools/hipvs_bridge/seekdb_hipvs_bridge.h; keep both in sync
 * (SEEKDB_HIPVS_ABI_VERSION guards against drift at runtime).
 */

namespace oceanbase {
namespace common {
namespace obhipvs {

constexpr int HIPVS_OK = 0;
constexpr int HIPVS_ERR_UNSUPPORTED = 1;
constexpr int HIPVS_ERR_INVALID_ARG = 2;
constexpr int HIPVS_ERR_NO_DEVICE = 3;
constexpr int HIPVS_ERR_OUT_OF_MEMORY = 4;
constexpr int HIPVS_ERR_RUNTIME = 5;

constexpr int HIPVS_METRIC_L2 = 0;
constexpr int HIPVS_METRIC_INNER_PRODUCT = 1;

constexpr int HIPVS_ALGO_AUTO = 0;
constexpr int HIPVS_ALGO_NN_DESCENT = 1;
constexpr int HIPVS_ALGO_IVF_PQ = 2;

constexpr int HIPVS_HIERARCHY_NONE = 0;
constexpr int HIPVS_HIERARCHY_CPU = 1;
constexpr int HIPVS_HIERARCHY_GPU = 2;

struct HipvsIndex;
using hipvs_index_t = HipvsIndex;

struct HipvsBuildParams {
  int64_t dim;
  int64_t count;
  int32_t graph_degree;
  int32_t intermediate_graph_degree;
  int32_t ef_construction;
  int32_t build_algo;
  int32_t metric;
  int32_t device_id;
  int32_t hierarchy;
};

using fn_bridge_version = const char *(*)();
using fn_available = int (*)(int32_t);
using fn_device_name = int (*)(int32_t, char *, size_t);
using fn_build = int (*)(const float *, const int64_t *, int64_t,
                         const HipvsBuildParams *, hipvs_index_t **, char *, size_t);
using fn_search = int (*)(hipvs_index_t *, const float *, int64_t, int32_t, int32_t,
                          int64_t *, float *, char *, size_t);
using fn_save = int (*)(hipvs_index_t *, const char *, char *, size_t);
using fn_load = hipvs_index_t *(*)(const char *, char *, size_t);
using fn_export_hnsw = int (*)(hipvs_index_t *, const char *, int32_t, int32_t, char *, size_t);
using fn_count = int64_t (*)(const hipvs_index_t *);
using fn_dim = int64_t (*)(const hipvs_index_t *);
using fn_free = void (*)(hipvs_index_t *);

/*
 * Function table. `loaded_` means dlsym() found every entry point; `probe_` records
 * the device probe result so DDL validation does not touch the GPU on every CREATE.
 */
struct HipvsBridge {
  void *handle_ = nullptr;
  bool resolved_ = false;
  bool probed_ = false;
  int probe_rc_ = HIPVS_ERR_UNSUPPORTED;

  fn_bridge_version bridge_version_ = nullptr;
  fn_available available_ = nullptr;
  fn_device_name device_name_ = nullptr;
  fn_build build_ = nullptr;
  fn_search search_ = nullptr;
  fn_save save_ = nullptr;
  fn_load load_ = nullptr;
  fn_export_hnsw export_hnsw_ = nullptr;
  fn_count count_ = nullptr;
  fn_dim dim_ = nullptr;
  fn_free free_ = nullptr;

  static HipvsBridge &instance()
  {
    /* Intentionally leaked and never destroyed: an in-flight index build may still
     * be using the handle while the observer shuts down. */
    static HipvsBridge *bridge = new HipvsBridge();
    return *bridge;
  }

  bool resolve()
  {
    if (resolved_) {
      return handle_ != nullptr;
    }
    resolved_ = true;
    const char *candidates[] = {"libseekdb_cuvs_bridge.so", "./libseekdb_cuvs_bridge.so", nullptr};
    for (int i = 0; candidates[i] != nullptr && handle_ == nullptr; ++i) {
      handle_ = ::dlopen(candidates[i], RTLD_NOW | RTLD_LOCAL);
    }
    if (handle_ == nullptr) {
      return false;
    }
    bridge_version_ = reinterpret_cast<fn_bridge_version>(::dlsym(handle_, "seekdb_hipvs_bridge_version"));
    available_ = reinterpret_cast<fn_available>(::dlsym(handle_, "seekdb_hipvs_available"));
    device_name_ = reinterpret_cast<fn_device_name>(::dlsym(handle_, "seekdb_hipvs_device_name"));
    build_ = reinterpret_cast<fn_build>(::dlsym(handle_, "seekdb_hipvs_build"));
    search_ = reinterpret_cast<fn_search>(::dlsym(handle_, "seekdb_hipvs_search"));
    save_ = reinterpret_cast<fn_save>(::dlsym(handle_, "seekdb_hipvs_save"));
    load_ = reinterpret_cast<fn_load>(::dlsym(handle_, "seekdb_hipvs_load"));
    export_hnsw_ = reinterpret_cast<fn_export_hnsw>(::dlsym(handle_, "seekdb_hipvs_export_hnsw"));
    count_ = reinterpret_cast<fn_count>(::dlsym(handle_, "seekdb_hipvs_count"));
    dim_ = reinterpret_cast<fn_dim>(::dlsym(handle_, "seekdb_hipvs_dim"));
    free_ = reinterpret_cast<fn_free>(::dlsym(handle_, "seekdb_hipvs_free"));

    if (bridge_version_ == nullptr || available_ == nullptr || build_ == nullptr ||
        search_ == nullptr || free_ == nullptr) {
      ::dlclose(handle_);
      handle_ = nullptr;
      return false;
    }
    return true;
  }

  /* True only when the bridge is present AND a usable ROCm device was found. */
  bool usable(int32_t device_id = -1)
  {
    if (!resolve()) {
      return false;
    }
    if (!probed_) {
      probe_rc_ = available_(device_id);
      probed_ = true;
    }
    return probe_rc_ == HIPVS_OK;
  }

  void reset_probe()
  {
    probed_ = false;
    probe_rc_ = HIPVS_ERR_UNSUPPORTED;
  }
};

}  // namespace obhipvs
}  // namespace common
}  // namespace oceanbase

#endif  // OCEANBASE_OBLIB_VECTOR_OB_HIPVS_BRIDGE_LOADER_H_
