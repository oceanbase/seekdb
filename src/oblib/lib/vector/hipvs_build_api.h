/*
 * Copyright (c) 2026 OceanBase.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Narrow C ABI between the seekdb server binary and the optional AMD GPU
 * backend (libseekdb_hipvs.so).
 *
 * Rationale for a C ABI in a separate shared object:
 *   - the seekdb server keeps building and shipping with a pure CPU toolchain
 *     (gcc 12 / libstdc++, no ROCm, no hipcc, no HSA runtime in its link line);
 *   - every ROCm/hipVS dependency (libcuvs, libraft, HIP runtime) stays inside
 *     the optional shared object, which is dlopen()ed only when the user asks
 *     for `lib=hipvs` and the operator points SEEKDB_HIPVS_LIBRARY at it.
 *   - only POD scalars and raw pointers cross the boundary, so libstdc++ /
 *     ROCm C++ runtime mismatches cannot leak into the database process.
 *
 * Contract of seekdb_hipvs_build_v1():
 *   On success (return 0) `output_path` holds one complete, fsync-ready serialized
 *   HNSW snapshot in the classic 64-bit little-endian hnswlib layout whose
 *   external labels are exactly the seekdb VIDs from `vids`, in the same order as
 *   the rows of `vectors`. The file is published atomically (tmp + rename), so a
 *   reader never observes a partial graph.
 *   On failure a non-zero value is returned and `error` holds a human readable
 *   reason. The backend never falls back to CPU: a failed GPU build is a failed
 *   index build.
 */
#ifndef OCEANBASE_LIB_VECTOR_HIPVS_BUILD_API_H_
#define OCEANBASE_LIB_VECTOR_HIPVS_BUILD_API_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped whenever the exported C surface changes; seekdb refuses to use a
 * backend whose ABI version is not exactly the one it was compiled against. */
#define SEEKDB_HIPVS_ABI_VERSION 1

enum {
  SEEKDB_HIPVS_OK = 0,
  SEEKDB_HIPVS_ERR_INVALID_ARGUMENT = -1,
  SEEKDB_HIPVS_ERR_UNSUPPORTED = -2,
  SEEKDB_HIPVS_ERR_NO_GPU = -3,
  SEEKDB_HIPVS_ERR_GPU_RUNTIME = -4,
  SEEKDB_HIPVS_ERR_VSAG_INCOMPATIBLE = -5,
  SEEKDB_HIPVS_ERR_IO = -6,
  SEEKDB_HIPVS_ERR_INTERNAL = -7
};

/* Main entry point: build a HNSW graph on the AMD GPU and export it.
 *
 * vectors          row-major float32 corpus, count * dim values, must be finite
 * vids             external document ids, one per row, must be unique
 * count            number of rows, must be > 2 * max_degree
 * dim              vector dimension
 * max_degree       HNSW M
 * ef_construction  HNSW efConstruction
 * output_path      destination file for the serialized snapshot
 * error/error_capacity  optional buffer for a NUL-terminated error message
 */
int32_t seekdb_hipvs_build_v1(const float *vectors,
                              const int64_t *vids,
                              int64_t count,
                              int64_t dim,
                              int64_t max_degree,
                              int64_t ef_construction,
                              const char *output_path,
                              char *error,
                              size_t error_capacity);

/* ABI gate: returns SEEKDB_HIPVS_ABI_VERSION of the loaded shared object. */
int32_t seekdb_hipvs_abi_version_v1(void);

/* Capability probe used for explicit logging before any index is built.
 * Returns SEEKDB_HIPVS_OK and writes a short description such as
 * "backend=hipVS vendor=AMD arch=gfx1100 name=Radeon ... devices=1" when a
 * usable GPU is present, otherwise SEEKDB_HIPVS_ERR_NO_GPU. */
int32_t seekdb_hipvs_probe_v1(char *info, size_t info_capacity);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OCEANBASE_LIB_VECTOR_HIPVS_BUILD_API_H_ */