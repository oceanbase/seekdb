/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The GPU worker owns all hipVS allocations. Its reply contains only native
 * scalars and a serialized HNSW; no C++ objects cross the process boundary. */
typedef struct {
  double cagra_ms;
  double hierarchy_ms;
  double serialize_ms;
} SeekdbHipvsBuildStats;

typedef struct {
  int status;
  size_t output_size;
  SeekdbHipvsBuildStats stats;
  char error[2048];
} SeekdbHipvsWorkerReply;

int seekdb_hipvs_build_hnsw(const float *, int64_t, int64_t, int, int,
                          unsigned char **, size_t *, SeekdbHipvsBuildStats *, char *, size_t);
void seekdb_hipvs_free(unsigned char *);

#ifdef __cplusplus
}
#endif
