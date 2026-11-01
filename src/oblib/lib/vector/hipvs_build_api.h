// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stddef.h>
#include <stdint.h>

// Independent C ABI keeps the ROCm/C++17 toolchain outside the database binary.
// Success means GPU construction, host synchronization and VID remapping have
// completed, and output_path contains a validated, seekable HNSW stream.
typedef int (*SeekdbHipvsBuildV1)(const float *vectors, const int64_t *vids,
    int64_t count, int64_t dim, int64_t max_degree, int64_t ef_construction,
    const char *output_path, char *error, size_t error_capacity);

#ifdef __cplusplus
extern "C" {
#endif
int seekdb_hipvs_build_v1(const float *vectors, const int64_t *vids,
    int64_t count, int64_t dim, int64_t max_degree, int64_t ef_construction,
    const char *output_path, char *error, size_t error_capacity);
#ifdef __cplusplus
}
#endif
