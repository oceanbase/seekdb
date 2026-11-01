/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "hipvs_hnsw_bridge.h"
#include <cuvs/core/c_api.h>
#include <cuvs/neighbors/cagra.h>
#include <cuvs/neighbors/hnsw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

__attribute__((visibility("default")))
int seekdb_hipvs_build_hnsw(const float *vectors, int64_t rows, int64_t dim,
                          int m, int ef_construction, unsigned char **output,
                          size_t *output_size, SeekdbHipvsBuildStats *stats,
                          char *error, size_t error_size)
{
  int result = 1;
  cuvsResources_t res = 0;
  cuvsCagraIndexParams_t cp = NULL;
  cuvsCagraIndex_t cagra = NULL;
  cuvsHnswIndexParams_t hp = NULL;
  cuvsHnswIndex_t hnsw = NULL;
  FILE *file = NULL;
  int fd = -1;
  int temp_created = 0;
  char path[] = "/tmp/seekdb-hipvs-XXXXXX";
  const char *stage = "parameters";
  *output = NULL;
  *output_size = 0;
  memset(stats, 0, sizeof(*stats));
  if (vectors == NULL || dim <= 0 || rows <= 4 * (int64_t)m ||
      (m != 16 && m != 32 && m != 64) || ef_construction <= m) {
    snprintf(error, error_size, "invalid hipVS build parameters");
    return result;
  }
#define CHECK(call) do { if ((call) != CUVS_SUCCESS) { \
  const char *detail = cuvsGetLastErrorText(); \
  snprintf(error, error_size, "%s: %s", stage, detail ? detail : "hipVS error"); \
  goto cleanup; } } while (0)
  CHECK(cuvsResourcesCreate(&res));
  CHECK(cuvsCagraIndexParamsCreate(&cp));
  CHECK(cuvsCagraIndexCreate(&cagra));
  cp->metric = L2Expanded;
  cp->graph_degree = 2 * (size_t)m;
  cp->intermediate_graph_degree = m <= 32 ? 256 : 512;
  if (cp->intermediate_graph_degree >= (size_t)rows) {
    cp->intermediate_graph_degree = ((size_t)rows - 1) / 32 * 32;
  }
  cp->build_algo = NN_DESCENT;
  cp->compression = NULL;
  int64_t shape[2] = {rows, dim};
  DLManagedTensor tensor = {0};
  tensor.dl_tensor.data = (void *)vectors;
  tensor.dl_tensor.device.device_type = kDLCPU;
  tensor.dl_tensor.ndim = 2;
  tensor.dl_tensor.dtype.code = kDLFloat;
  tensor.dl_tensor.dtype.bits = 32;
  tensor.dl_tensor.dtype.lanes = 1;
  tensor.dl_tensor.shape = shape;
  stage = "cagra_gpu_build";
  double start = now_ms();
  CHECK(cuvsCagraBuild(res, cp, &tensor, cagra));
  CHECK(cuvsStreamSync(res));
  stats->cagra_ms = now_ms() - start;
  CHECK(cuvsHnswIndexParamsCreate(&hp));
  CHECK(cuvsHnswIndexCreate(&hnsw));
  /* GPU computes the base graph. CPU constructs the much smaller hierarchy,
   * giving a mutable, standard hnswlib representation (NONE is incompatible). */
  hp->hierarchy = CPU;
  hp->ef_construction = ef_construction;
  hp->num_threads = 8;
  stage = "hnsw_hierarchy";
  start = now_ms();
  CHECK(cuvsHnswFromCagra(res, hp, cagra, hnsw));
  CHECK(cuvsStreamSync(res));
  stats->hierarchy_ms = now_ms() - start;
  stage = "hnsw_serialize";
  start = now_ms();
  fd = mkstemp(path);
  if (fd < 0) {
    snprintf(error, error_size, "cannot create private hipVS serialization file");
    goto cleanup;
  }
  temp_created = 1;
  close(fd);
  fd = -1;
  CHECK(cuvsHnswSerialize(res, path, hnsw));
  CHECK(cuvsStreamSync(res));
  file = fopen(path, "rb");
  long length;
  if (file == NULL || fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 96 ||
      fseek(file, 0, SEEK_SET) != 0) {
    snprintf(error, error_size, "cannot read hipVS serialized HNSW");
    goto cleanup;
  }
  *output = malloc((size_t)length);
  if (*output == NULL || fread(*output, 1, (size_t)length, file) != (size_t)length) {
    snprintf(error, error_size, "cannot allocate/read hipVS serialized HNSW");
    goto cleanup;
  }
  *output_size = (size_t)length;
  stats->serialize_ms = now_ms() - start;
  result = 0;
cleanup:
  if (file != NULL) fclose(file);
  if (fd >= 0) close(fd);
  if (temp_created) unlink(path);
  if (hnsw != NULL) cuvsHnswIndexDestroy(hnsw);
  if (hp != NULL) cuvsHnswIndexParamsDestroy(hp);
  if (cagra != NULL) cuvsCagraIndexDestroy(cagra);
  if (cp != NULL) cuvsCagraIndexParamsDestroy(cp);
  if (res != 0) cuvsResourcesDestroy(res);
  if (result != 0) { free(*output); *output = NULL; *output_size = 0; }
  return result;
#undef CHECK
}

__attribute__((visibility("default")))
void seekdb_hipvs_free(unsigned char *output) { free(output); }
