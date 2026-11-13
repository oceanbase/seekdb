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

#ifndef SEEKDB_HIPVS_BRIDGE_H
#define SEEKDB_HIPVS_BRIDGE_H

/*
 * seekdb <-> hipVS bridge (C ABI).
 *
 * This library is the only place in the tree that talks to hipVS / ROCm.  It keeps
 * the database side free of HIP headers and of any link-time dependency on libcuvs:
 * seekdb dlopen()s the bridge on demand (see docs/amd-hipvs-hnsw.md) and falls back
 * to the native VSAG/CPU path whenever a symbol is missing or a call fails.
 *
 * Ownership: every object returned here is owned by the caller and must be released
 * with the matching seekdb_hipvs_*_free() function.  No function throws; failures are
 * reported through the return code plus an optional human readable `err` buffer.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEEKDB_HIPVS_ABI_VERSION 1

/* return codes */
#define SEEKDB_HIPVS_OK 0
#define SEEKDB_HIPVS_ERR_UNSUPPORTED 1 /* built without hipVS, or algo/metric not supported */
#define SEEKDB_HIPVS_ERR_INVALID_ARG 2
#define SEEKDB_HIPVS_ERR_NO_DEVICE 3
#define SEEKDB_HIPVS_ERR_OUT_OF_MEMORY 4
#define SEEKDB_HIPVS_ERR_RUNTIME 5

/* distance metrics, kept numerically stable across ABI versions */
#define SEEKDB_HIPVS_METRIC_L2 0
#define SEEKDB_HIPVS_METRIC_INNER_PRODUCT 1

/* graph construction algorithms */
#define SEEKDB_HIPVS_ALGO_AUTO 0
#define SEEKDB_HIPVS_ALGO_NN_DESCENT 1
#define SEEKDB_HIPVS_ALGO_IVF_PQ 2

/* hierarchy used when projecting the CAGRA graph onto an HNSW index */
#define SEEKDB_HIPVS_HIERARCHY_NONE 0 /* base layer only */
#define SEEKDB_HIPVS_HIERARCHY_CPU 1  /* hnswlib-compatible, built on host threads */
#define SEEKDB_HIPVS_HIERARCHY_GPU 2  /* hierarchy built on the GPU */

typedef struct seekdb_hipvs_index seekdb_hipvs_index;

typedef struct seekdb_hipvs_build_params {
  int64_t dim;    /* vector dimension, > 0 */
  int64_t count;  /* number of rows in `vectors`, > 0 */
  int32_t graph_degree;             /* CAGRA out-degree, maps to HNSW `m` */
  int32_t intermediate_graph_degree;/* kNN graph degree before pruning, >= graph_degree */
  int32_t ef_construction;          /* candidate list size, maps to HNSW ef_construction */
  int32_t build_algo;               /* SEEKDB_HIPVS_ALGO_* */
  int32_t metric;                   /* SEEKDB_HIPVS_METRIC_* */
  int32_t device_id;                /* ROCm device ordinal, -1 means "default visible" */
  int32_t hierarchy;                /* SEEKDB_HIPVS_HIERARCHY_* for the HNSW projection */
} seekdb_hipvs_build_params;

/* Version of the bridge itself, e.g. "seekdb-hipvs-bridge 1.0.0 (hipVS 25.10)". */
const char *seekdb_hipvs_bridge_version(void);

/*
 * Cheap capability probe, safe to call before anything else.  Returns
 * SEEKDB_HIPVS_OK when a usable GPU + hipVS runtime is present, otherwise
 * SEEKDB_HIPVS_ERR_UNSUPPORTED / SEEKDB_HIPVS_ERR_NO_DEVICE.
 */
int seekdb_hipvs_available(int32_t device_id);

/* Writes something like "AMD Radeon PRO W7900 (gfx1100)" into `buf`. */
int seekdb_hipvs_device_name(int32_t device_id, char *buf, size_t len);

/*
 * Build a graph index over `count` row-major `dim`-dimensional float vectors.
 * `ids` may be NULL, in which case row ordinals 0..count-1 are used as external ids.
 * The call is synchronous and blocks the calling thread while the GPU is busy.
 */
int seekdb_hipvs_build(const float *vectors,
                       const int64_t *ids,
                       int64_t count,
                       const seekdb_hipvs_build_params *params,
                       seekdb_hipvs_index **out_index,
                       char *err,
                       size_t errlen);

/*
 * Batched k-NN search: all `n_queries` rows are submitted to the GPU in one call,
 * which is where the GPU actually wins over per-row CPU HNSW.  Results are written
 * row-major into `out_ids` (n_queries * topk) and `out_dist` (same shape);
 * unreachable slots are filled with -1 / FLT_MAX.
 */
int seekdb_hipvs_search(seekdb_hipvs_index *index,
                        const float *queries,
                        int64_t n_queries,
                        int32_t topk,
                        int32_t ef_search,
                        int64_t *out_ids,
                        float *out_dist,
                        char *err,
                        size_t errlen);

/* Serialize the CAGRA graph to a binary blob that seekdb_hipvs_load() can read back. */
int seekdb_hipvs_save(seekdb_hipvs_index *index, const char *path, char *err, size_t errlen);

/* Inverse of seekdb_hipvs_save(). */
seekdb_hipvs_index *seekdb_hipvs_load(const char *path, char *err, size_t errlen);

/*
 * Project the CAGRA graph onto an HNSW index and write it out.  With
 * SEEKDB_HIPVS_HIERARCHY_CPU the resulting file is hnswlib-compatible, i.e. it can be
 * opened by the CPU-side HNSW readers that seekdb already ships (VSAG / hnswlib),
 * which is what makes "build on GPU, serve on CPU" possible.
 */
int seekdb_hipvs_export_hnsw(seekdb_hipvs_index *index,
                             const char *path,
                             int32_t hierarchy,
                             int32_t ef_construction,
                             char *err,
                             size_t errlen);

int64_t seekdb_hipvs_count(const seekdb_hipvs_index *index);
int64_t seekdb_hipvs_dim(const seekdb_hipvs_index *index);

void seekdb_hipvs_free(seekdb_hipvs_index *index);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SEEKDB_HIPVS_BRIDGE_H */
