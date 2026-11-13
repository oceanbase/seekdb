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

#include "seekdb_hipvs_bridge.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#if defined(SEEKDB_HAS_HIPVS)

#include <raft/core/copy.hpp>
#include <raft/core/device_mdarray.hpp>
#include <raft/core/device_mdspan.hpp>
#include <raft/core/device_resources.hpp>
#include <raft/core/host_mdarray.hpp>
#include <raft/core/host_mdspan.hpp>
#include <raft/core/mdspan_types.hpp>
#include <raft/core/resource/device_info.hpp>

#include <cuvs/distance/distance.hpp>
#include <cuvs/neighbors/cagra.hpp>
#include <cuvs/neighbors/hnsw.hpp>

#endif /* SEEKDB_HAS_HIPVS */

namespace {

constexpr const char *kBridgeVersion =
#if defined(SEEKDB_HAS_HIPVS)
    "seekdb-hipvs-bridge 1.0.0 (hipVS)";
#else
    "seekdb-hipvs-bridge 1.0.0 (stub, built without hipVS)";
#endif

/* Write a formatted message into the caller supplied buffer; never throws. */
void set_error(char *err, size_t errlen, const char *fmt, ...)
{
  if (err == nullptr || errlen == 0) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, errlen, fmt, ap);
  va_end(ap);
  err[errlen - 1] = '\0';
}

bool valid_common_params(const float *vectors,
                         int64_t count,
                         const seekdb_hipvs_build_params *params)
{
  return vectors != nullptr && params != nullptr && count > 0 && params->dim > 0 &&
         params->count == count && params->graph_degree > 0 &&
         params->intermediate_graph_degree >= params->graph_degree &&
         (params->metric == SEEKDB_HIPVS_METRIC_L2 ||
          params->metric == SEEKDB_HIPVS_METRIC_INNER_PRODUCT);
}

} /* namespace */

struct seekdb_hipvs_index {
#if defined(SEEKDB_HAS_HIPVS)
  std::unique_ptr<raft::device_resources> res;
  std::optional<cuvs::neighbors::cagra::index<float, uint32_t>> cagra_index;
#endif
  /* CAGRA addresses rows [0, count); we translate back to the caller's ids. */
  std::vector<int64_t> ids;
  int64_t dim = 0;
  int64_t count = 0;
  int32_t metric = SEEKDB_HIPVS_METRIC_L2;
  int32_t ef_construction = 200;
};

#if defined(SEEKDB_HAS_HIPVS)

namespace {

cuvs::distance::DistanceType to_cuvs_metric(int32_t metric)
{
  return metric == SEEKDB_HIPVS_METRIC_INNER_PRODUCT
             ? cuvs::distance::DistanceType::InnerProduct
             : cuvs::distance::DistanceType::L2Expanded;
}

cuvs::neighbors::hnsw::HnswHierarchy to_hnsw_hierarchy(int32_t hierarchy)
{
  switch (hierarchy) {
    case SEEKDB_HIPVS_HIERARCHY_CPU:
      return cuvs::neighbors::hnsw::HnswHierarchy::CPU;
    case SEEKDB_HIPVS_HIERARCHY_GPU:
      return cuvs::neighbors::hnsw::HnswHierarchy::GPU;
    default:
      return cuvs::neighbors::hnsw::HnswHierarchy::NONE;
  }
}

} /* namespace */

int seekdb_hipvs_available(int32_t device_id)
{
  try {
    (void)device_id;
    raft::device_resources res;
    /* Instantiating resources is enough: it initialises HIP and fails loudly when
     * no usable device / ROCm runtime is present. */
    return SEEKDB_HIPVS_OK;
  } catch (const std::exception &) {
    return SEEKDB_HIPVS_ERR_NO_DEVICE;
  }
}

int seekdb_hipvs_device_name(int32_t device_id, char *buf, size_t len)
{
  if (buf == nullptr || len == 0) {
    return SEEKDB_HIPVS_ERR_INVALID_ARG;
  }
  (void)device_id;
  try {
    raft::device_resources res;
    /* raft::resource resolves the marketing name + gfx target across ROCm versions,
     * which hipGetDeviceProperties() no longer does consistently. */
    const std::string name = raft::resource::get_device_name(res);
    snprintf(buf, len, "%s", name.c_str());
    return SEEKDB_HIPVS_OK;
  } catch (const std::exception &e) {
    set_error(buf, len, "device query failed: %s", e.what());
    return SEEKDB_HIPVS_ERR_RUNTIME;
  }
}

int seekdb_hipvs_build(const float *vectors,
                       const int64_t *ids,
                       int64_t count,
                       const seekdb_hipvs_build_params *params,
                       seekdb_hipvs_index **out_index,
                       char *err,
                       size_t errlen)
{
  if (out_index == nullptr) {
    set_error(err, errlen, "out_index is null");
    return SEEKDB_HIPVS_ERR_INVALID_ARG;
  }
  *out_index = nullptr;
  if (!valid_common_params(vectors, count, params)) {
    set_error(err, errlen, "invalid build params (dim=%lld count=%lld degree=%d metric=%d)",
              params ? static_cast<long long>(params->dim) : -1LL,
              static_cast<long long>(count),
              params ? params->graph_degree : -1,
              params ? params->metric : -1);
    return SEEKDB_HIPVS_ERR_INVALID_ARG;
  }

  std::unique_ptr<seekdb_hipvs_index> index(new (std::nothrow) seekdb_hipvs_index());
  if (!index) {
    set_error(err, errlen, "out of memory");
    return SEEKDB_HIPVS_ERR_OUT_OF_MEMORY;
  }

  try {
    index->dim = params->dim;
    index->count = count;
    index->metric = params->metric;
    index->ef_construction = params->ef_construction > 0 ? params->ef_construction : 200;
    index->ids.resize(static_cast<size_t>(count));
    if (ids != nullptr) {
      std::memcpy(index->ids.data(), ids, sizeof(int64_t) * static_cast<size_t>(count));
    } else {
      for (int64_t i = 0; i < count; ++i) {
        index->ids[static_cast<size_t>(i)] = i;
      }
    }

    index->res = std::make_unique<raft::device_resources>();
    raft::device_resources &res = *index->res;

    cuvs::neighbors::cagra::index_params cagra_params;
    cagra_params.metric = to_cuvs_metric(params->metric);
    cagra_params.graph_degree = static_cast<size_t>(params->graph_degree);
    cagra_params.intermediate_graph_degree =
        static_cast<size_t>(std::max(params->intermediate_graph_degree,
                                     params->graph_degree));
    /* Connectivity matters for a database index: a disconnected graph silently
     * loses recall and there is no way for the SQL layer to detect it. */
    cagra_params.guarantee_connectivity = true;

    if (params->build_algo == SEEKDB_HIPVS_ALGO_NN_DESCENT) {
      cagra_params.graph_build_params =
          cuvs::neighbors::cagra::graph_build_params::nn_descent_params(
              cagra_params.intermediate_graph_degree);
    } else if (params->build_algo == SEEKDB_HIPVS_ALGO_IVF_PQ) {
      cagra_params.graph_build_params =
          cuvs::neighbors::cagra::graph_build_params::ivf_pq_params(
              raft::matrix_extent<int64_t>(count, params->dim), cagra_params.metric);
    }
    /* SEEKDB_HIPVS_ALGO_AUTO keeps the heuristic picked by hipVS. */

    /* The host_matrix_view overload lets hipVS own the H2D copy, so the caller can
     * hand us the exact buffer it already has in memory. */
    auto host_dataset =
        raft::make_host_matrix_view<const float, int64_t, raft::row_major>(vectors,
                                                                          count,
                                                                          params->dim);
    index->cagra_index = cuvs::neighbors::cagra::build(res, cagra_params, host_dataset);
  } catch (const std::exception &e) {
    set_error(err, errlen, "hipVS build failed: %s", e.what());
    return SEEKDB_HIPVS_ERR_RUNTIME;
  }

  *out_index = index.release();
  return SEEKDB_HIPVS_OK;
}

int seekdb_hipvs_search(seekdb_hipvs_index *index,
                        const float *queries,
                        int64_t n_queries,
                        int32_t topk,
                        int32_t ef_search,
                        int64_t *out_ids,
                        float *out_dist,
                        char *err,
                        size_t errlen)
{
  if (index == nullptr || queries == nullptr || out_ids == nullptr || out_dist == nullptr ||
      n_queries <= 0 || topk <= 0) {
    set_error(err, errlen, "invalid search arguments");
    return SEEKDB_HIPVS_ERR_INVALID_ARG;
  }
  if (!index->cagra_index.has_value()) {
    set_error(err, errlen, "index has no CAGRA graph loaded");
    return SEEKDB_HIPVS_ERR_INVALID_ARG;
  }

  try {
    raft::device_resources &res = *index->res;

    cuvs::neighbors::cagra::search_params search_params;
    /* itopk_size must be at least the requested k; hipVS rounds internally. */
    search_params.itopk_size = static_cast<size_t>(std::max<int32_t>(topk, 64));
    if (ef_search > 0) {
      search_params.itopk_size =
          static_cast<size_t>(std::max<int32_t>(topk, ef_search));
    }

    auto host_queries =
        raft::make_host_matrix_view<const float, int64_t, raft::row_major>(queries,
                                                                         n_queries,
                                                                         index->dim);
    auto device_queries = raft::make_device_matrix<float, int64_t>(res, n_queries, index->dim);
    raft::copy(res, device_queries.view(), host_queries);

    auto device_neighbors =
        raft::make_device_matrix<uint32_t, int64_t>(res, n_queries, topk);
    auto device_distances = raft::make_device_matrix<float, int64_t>(res, n_queries, topk);

    cuvs::neighbors::cagra::search(res,
                                   search_params,
                                   *index->cagra_index,
                                   device_queries.view(),
                                   device_neighbors.view(),
                                   device_distances.view());

    auto host_neighbors = raft::make_host_matrix<uint32_t, int64_t>(n_queries, topk);
    auto host_distances = raft::make_host_matrix<float, int64_t>(n_queries, topk);
    raft::copy(res, host_neighbors.view(), device_neighbors.view());
    raft::copy(res, host_distances.view(), device_distances.view());

    for (int64_t q = 0; q < n_queries; ++q) {
      for (int32_t k = 0; k < topk; ++k) {
        const size_t slot = static_cast<size_t>(q * topk + k);
        const uint32_t row = host_neighbors.data_handle()[slot];
        if (row < static_cast<uint32_t>(index->count)) {
          out_ids[slot] = index->ids[static_cast<size_t>(row)];
          out_dist[slot] = host_distances.data_handle()[slot];
        } else {
          out_ids[slot] = -1;
          out_dist[slot] = std::numeric_limits<float>::max();
        }
      }
    }
  } catch (const std::exception &e) {
    set_error(err, errlen, "hipVS search failed: %s", e.what());
    return SEEKDB_HIPVS_ERR_RUNTIME;
  }
  return SEEKDB_HIPVS_OK;
}

int seekdb_hipvs_save(seekdb_hipvs_index *index, const char *path, char *err, size_t errlen)
{
  if (index == nullptr || path == nullptr) {
    set_error(err, errlen, "invalid save arguments");
    return SEEKDB_HIPVS_ERR_INVALID_ARG;
  }
  if (!index->cagra_index.has_value()) {
    set_error(err, errlen, "index has no CAGRA graph loaded");
    return SEEKDB_HIPVS_ERR_INVALID_ARG;
  }
  try {
    cuvs::neighbors::cagra::serialize(*index->res, std::string(path), *index->cagra_index);
  } catch (const std::exception &e) {
    set_error(err, errlen, "hipVS serialize failed: %s", e.what());
    return SEEKDB_HIPVS_ERR_RUNTIME;
  }
  return SEEKDB_HIPVS_OK;
}

seekdb_hipvs_index *seekdb_hipvs_load(const char *path, char *err, size_t errlen)
{
  if (path == nullptr) {
    set_error(err, errlen, "invalid load arguments");
    return nullptr;
  }
  std::unique_ptr<seekdb_hipvs_index> index(new (std::nothrow) seekdb_hipvs_index());
  if (!index) {
    set_error(err, errlen, "out of memory");
    return nullptr;
  }
  try {
    index->res = std::make_unique<raft::device_resources>();
    index->cagra_index =
        cuvs::neighbors::cagra::deserialize(*index->res, std::string(path));
    index->dim = static_cast<int64_t>(index->cagra_index->dim());
    index->count = static_cast<int64_t>(index->cagra_index->size());
    index->ids.resize(static_cast<size_t>(index->count));
    for (int64_t i = 0; i < index->count; ++i) {
      index->ids[static_cast<size_t>(i)] = i;
    }
  } catch (const std::exception &e) {
    set_error(err, errlen, "hipVS deserialize failed: %s", e.what());
    return nullptr;
  }
  return index.release();
}

int seekdb_hipvs_export_hnsw(seekdb_hipvs_index *index,
                             const char *path,
                             int32_t hierarchy,
                             int32_t ef_construction,
                             char *err,
                             size_t errlen)
{
  if (index == nullptr || path == nullptr) {
    set_error(err, errlen, "invalid export arguments");
    return SEEKDB_HIPVS_ERR_INVALID_ARG;
  }
  if (!index->cagra_index.has_value()) {
    set_error(err, errlen, "index has no CAGRA graph loaded");
    return SEEKDB_HIPVS_ERR_INVALID_ARG;
  }
  if (hierarchy == SEEKDB_HIPVS_HIERARCHY_NONE) {
    set_error(err, errlen, "export_hnsw requires a CPU or GPU hierarchy");
    return SEEKDB_HIPVS_ERR_INVALID_ARG;
  }
  try {
    cuvs::neighbors::hnsw::index_params hnsw_params;
    hnsw_params.hierarchy = to_hnsw_hierarchy(hierarchy);
    hnsw_params.ef_construction =
        ef_construction > 0 ? ef_construction : index->ef_construction;
    auto hnsw_index =
        cuvs::neighbors::hnsw::from_cagra(*index->res, hnsw_params, *index->cagra_index);
    cuvs::neighbors::hnsw::serialize(*index->res, std::string(path), *hnsw_index);
  } catch (const std::exception &e) {
    set_error(err, errlen, "hipVS hnsw export failed: %s", e.what());
    return SEEKDB_HIPVS_ERR_RUNTIME;
  }
  return SEEKDB_HIPVS_OK;
}

int64_t seekdb_hipvs_count(const seekdb_hipvs_index *index)
{
  return index == nullptr ? 0 : index->count;
}

int64_t seekdb_hipvs_dim(const seekdb_hipvs_index *index)
{
  return index == nullptr ? 0 : index->dim;
}

void seekdb_hipvs_free(seekdb_hipvs_index *index)
{
  delete index;
}

#else /* !SEEKDB_HAS_HIPVS */

/*
 * Stub build.  We still ship a real shared object so that seekdb can dlopen() and
 * probe it unconditionally instead of guessing from file names; every entry point
 * reports SEEKDB_HIPVS_ERR_UNSUPPORTED and the caller keeps its CPU path.
 */

int seekdb_hipvs_available(int32_t device_id)
{
  (void)device_id;
  return SEEKDB_HIPVS_ERR_UNSUPPORTED;
}

int seekdb_hipvs_device_name(int32_t device_id, char *buf, size_t len)
{
  (void)device_id;
  if (buf != nullptr && len > 0) {
    set_error(buf, len, "built without hipVS");
  }
  return SEEKDB_HIPVS_ERR_UNSUPPORTED;
}

int seekdb_hipvs_build(const float *vectors,
                       const int64_t *ids,
                       int64_t count,
                       const seekdb_hipvs_build_params *params,
                       seekdb_hipvs_index **out_index,
                       char *err,
                       size_t errlen)
{
  (void)vectors;
  (void)ids;
  (void)count;
  (void)params;
  if (out_index != nullptr) {
    *out_index = nullptr;
  }
  set_error(err, errlen, "bridge was built without hipVS support");
  return SEEKDB_HIPVS_ERR_UNSUPPORTED;
}

int seekdb_hipvs_search(seekdb_hipvs_index *index,
                        const float *queries,
                        int64_t n_queries,
                        int32_t topk,
                        int32_t ef_search,
                        int64_t *out_ids,
                        float *out_dist,
                        char *err,
                        size_t errlen)
{
  (void)index;
  (void)queries;
  (void)n_queries;
  (void)topk;
  (void)ef_search;
  (void)out_ids;
  (void)out_dist;
  set_error(err, errlen, "bridge was built without hipVS support");
  return SEEKDB_HIPVS_ERR_UNSUPPORTED;
}

int seekdb_hipvs_save(seekdb_hipvs_index *index, const char *path, char *err, size_t errlen)
{
  (void)index;
  (void)path;
  set_error(err, errlen, "bridge was built without hipVS support");
  return SEEKDB_HIPVS_ERR_UNSUPPORTED;
}

seekdb_hipvs_index *seekdb_hipvs_load(const char *path, char *err, size_t errlen)
{
  (void)path;
  set_error(err, errlen, "bridge was built without hipVS support");
  return nullptr;
}

int seekdb_hipvs_export_hnsw(seekdb_hipvs_index *index,
                             const char *path,
                             int32_t hierarchy,
                             int32_t ef_construction,
                             char *err,
                             size_t errlen)
{
  (void)index;
  (void)path;
  (void)hierarchy;
  (void)ef_construction;
  set_error(err, errlen, "bridge was built without hipVS support");
  return SEEKDB_HIPVS_ERR_UNSUPPORTED;
}

int64_t seekdb_hipvs_count(const seekdb_hipvs_index *index)
{
  return index == nullptr ? 0 : index->count;
}

int64_t seekdb_hipvs_dim(const seekdb_hipvs_index *index)
{
  return index == nullptr ? 0 : index->dim;
}

void seekdb_hipvs_free(seekdb_hipvs_index *index)
{
  delete index;
}

#endif /* SEEKDB_HAS_HIPVS */

const char *seekdb_hipvs_bridge_version(void)
{
  return kBridgeVersion;
}
