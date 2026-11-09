/* GPU-built CAGRA -> full HNSW, with CPU HNSW search.
 * Uses only APIs present in the exported amd107 mirror headers.
 * No CPU-only index build, no CAGRA search, and no silent fallback.
 */
#define _POSIX_C_SOURCE 200809L
#include "seekdb_cuvs_bridge.h"
#include <cuvs/core/c_api.h>
#include <cuvs/neighbors/cagra.h>
#include <cuvs/neighbors/hnsw.h>
#include <dlpack/dlpack.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SNAPSHOT_HEADER_BYTES 80
#define SNAPSHOT_MAGIC "SKHNSW01"
#define FNV_OFFSET UINT64_C(14695981039346656037)
#define FNV_PRIME UINT64_C(1099511628211)

typedef struct {
  cuvsResources_t res;
  cuvsCagraIndexParams_t cagra_params;
  cuvsCagraIndex_t cagra;
  cuvsHnswIndexParams_t hnsw_params;
  cuvsHnswIndex_t hnsw;
  cuvsHnswSearchParams_t search_params;
  pthread_mutex_t mutex;
  int resources_ready;
  int mutex_ready;
  long n, dim;
  int hierarchy;
  int hnsw_m, graph_degree, intermediate_degree, nn_descent_niter;
  int ef_construction, search_ef, num_threads;
} sk_hnsw_index;

static double now_seconds(void)
{
  struct timespec value;
  clock_gettime(CLOCK_MONOTONIC, &value);
  return (double)value.tv_sec + (double)value.tv_nsec * 1e-9;
}

static void error_text(const char* stage, const char* detail)
{
  fprintf(stderr, "[seekdb_hipvs_hnsw] event=error stage=%s detail=%s fallback=disabled\n",
          stage, detail ? detail : "unknown");
  fflush(stderr);
}

static int check_cuvs(cuvsError_t status, const char* stage)
{
  if (status == CUVS_SUCCESS) return 0;
  error_text(stage, cuvsGetLastErrorText());
  return 1;
}

static int env_integer(const char* name, int default_value, int minimum, int maximum, int* result)
{
  const char* value = getenv(name);
  char* end = NULL;
  long parsed;
  *result = default_value;
  if (!value || !*value) return 0;
  errno = 0;
  parsed = strtol(value, &end, 10);
  if (errno || !end || *end || parsed < minimum || parsed > maximum) {
    error_text(name, "invalid integer configuration");
    return 1;
  }
  *result = (int)parsed;
  return 0;
}

static int valid_shape(long n, long dim)
{
  return n > 0 && (uint64_t)n <= UINT32_MAX && dim > 0 && dim <= INT_MAX
         && (uint64_t)n <= SIZE_MAX / sizeof(float) / (uint64_t)dim;
}

static DLManagedTensor host_matrix(void* data, int64_t* shape, uint8_t code, uint8_t bits)
{
  DLManagedTensor result;
  memset(&result, 0, sizeof(result));
  result.dl_tensor.data = data;
  result.dl_tensor.device.device_type = kDLCPU;
  result.dl_tensor.device.device_id = 0;
  result.dl_tensor.ndim = 2;
  result.dl_tensor.dtype.code = code;
  result.dl_tensor.dtype.bits = bits;
  result.dl_tensor.dtype.lanes = 1;
  result.dl_tensor.shape = shape;
  return result;
}

static int sync_index(sk_hnsw_index* h, const char* stage)
{
  return check_cuvs(cuvsStreamSync(h->res), stage);
}

static void destroy_index(sk_hnsw_index* h)
{
  if (!h) return;
  if (h->resources_ready) (void)sync_index(h, "cleanup_sync");
  if (h->search_params) (void)check_cuvs(cuvsHnswSearchParamsDestroy(h->search_params), "destroy_search_params");
  if (h->hnsw) (void)check_cuvs(cuvsHnswIndexDestroy(h->hnsw), "destroy_hnsw");
  if (h->hnsw_params) (void)check_cuvs(cuvsHnswIndexParamsDestroy(h->hnsw_params), "destroy_hnsw_params");
  if (h->cagra) (void)check_cuvs(cuvsCagraIndexDestroy(h->cagra), "destroy_cagra");
  if (h->cagra_params) (void)check_cuvs(cuvsCagraIndexParamsDestroy(h->cagra_params), "destroy_cagra_params");
  if (h->resources_ready) (void)check_cuvs(cuvsResourcesDestroy(h->res), "destroy_resources");
  if (h->mutex_ready) pthread_mutex_destroy(&h->mutex);
  free(h);
}

static sk_hnsw_index* allocate_index(long n, long dim)
{
  sk_hnsw_index* h = (sk_hnsw_index*)calloc(1, sizeof(*h));
  if (!h) { error_text("allocate_handle", "out of host memory"); return NULL; }
  h->n = n;
  h->dim = dim;
  if (pthread_mutex_init(&h->mutex, NULL)) { error_text("initialize_mutex", "pthread_mutex_init failed"); free(h); return NULL; }
  h->mutex_ready = 1;
  if (check_cuvs(cuvsResourcesCreate(&h->res), "create_resources")) goto fail;
  h->resources_ready = 1;
  if (check_cuvs(cuvsHnswIndexParamsCreate(&h->hnsw_params), "create_hnsw_params")) goto fail;
  if (check_cuvs(cuvsHnswIndexCreate(&h->hnsw), "create_hnsw")) goto fail;
  if (check_cuvs(cuvsHnswSearchParamsCreate(&h->search_params), "create_search_params")) goto fail;
  h->hnsw->dtype.code = kDLFloat;
  h->hnsw->dtype.bits = 32;
  h->hnsw->dtype.lanes = 1;
  return h;
fail:
  destroy_index(h);
  return NULL;
}

static void apply_hnsw_params(sk_hnsw_index* h)
{
  h->hnsw_params->hierarchy = (enum cuvsHnswHierarchy)h->hierarchy;
  h->hnsw_params->ef_construction = h->ef_construction;
  h->hnsw_params->num_threads = h->num_threads;
  h->search_params->ef = h->search_ef;
  h->search_params->num_threads = h->num_threads;
}

void* seekdb_cuvs_build(const float* base, long n, long dim)
{
  sk_hnsw_index* h;
  const char* hierarchy;
  int64_t shape[2] = { (int64_t)n, (int64_t)dim };
  DLManagedTensor dataset;
  double started = now_seconds(), cagra_finished;
  if (!base || !valid_shape(n, dim)) { error_text("build_input", "invalid shape or NULL base"); return NULL; }
  h = allocate_index(n, dim);
  if (!h) return NULL;
  hierarchy = getenv("SEEKDB_HIPVS_HNSW_HIERARCHY");
  if (!hierarchy || !*hierarchy || !strcmp(hierarchy, "gpu")) h->hierarchy = GPU;
  else if (!strcmp(hierarchy, "cpu")) h->hierarchy = CPU;
  else { error_text("hierarchy", "use explicit gpu or cpu full hierarchy; NONE is not supported"); goto fail; }
  if (env_integer("SEEKDB_HIPVS_HNSW_M", 16, 2, INT_MAX / 8, &h->hnsw_m)
      || env_integer("SEEKDB_HIPVS_GRAPH_DEGREE", 2 * h->hnsw_m, 2, INT_MAX / 2, &h->graph_degree)
      || env_integer("SEEKDB_HIPVS_INTERMEDIATE_DEGREE", 2 * h->graph_degree, 2, INT_MAX / 2, &h->intermediate_degree)
      || env_integer("SEEKDB_HIPVS_NN_DESCENT_NITER", 20, 1, INT_MAX, &h->nn_descent_niter)
      || env_integer("SEEKDB_HIPVS_EF_CONSTRUCTION", 200, 1, INT_MAX, &h->ef_construction)
      || env_integer("SEEKDB_HIPVS_SEARCH_EF", 512, 1, INT_MAX, &h->search_ef)
      || env_integer("SEEKDB_HIPVS_CPU_THREADS", 1, 0, INT_MAX, &h->num_threads)) goto fail;
  if (h->graph_degree >= n || h->intermediate_degree >= n || h->intermediate_degree < h->graph_degree
      || h->graph_degree != 2 * h->hnsw_m) {
    error_text("graph_degree", "require graph_degree=2*M, graph_degree <= intermediate_degree < n"); goto fail;
  }
  apply_hnsw_params(h);
  fprintf(stderr, "[seekdb_hipvs_hnsw] event=build_start n=%ld dim=%ld metric=L2Expanded hierarchy=%s hnsw_m=%d graph_degree=%d intermediate_degree=%d nn_descent_niter=%d ef_construction=%d search_ef=%d threads=%d fallback=disabled\n",
          n, dim, h->hierarchy == GPU ? "gpu" : "cpu-explicit", h->hnsw_m, h->graph_degree, h->intermediate_degree,
          h->nn_descent_niter, h->ef_construction, h->search_ef, h->num_threads);
  fflush(stderr);
  if (check_cuvs(cuvsCagraIndexParamsCreate(&h->cagra_params), "create_cagra_params")) goto fail;
  h->cagra_params->metric = L2Expanded;
  h->cagra_params->build_algo = NN_DESCENT;
  h->cagra_params->graph_degree = (size_t)h->graph_degree;
  h->cagra_params->intermediate_graph_degree = (size_t)h->intermediate_degree;
  h->cagra_params->nn_descent_niter = (size_t)h->nn_descent_niter;
  if (check_cuvs(cuvsCagraIndexCreate(&h->cagra), "create_cagra")) goto fail;
  dataset = host_matrix((void*)base, shape, kDLFloat, 32);
  if (check_cuvs(cuvsCagraBuild(h->res, h->cagra_params, &dataset, h->cagra), "gpu_cagra_build")) goto fail;
  if (sync_index(h, "gpu_cagra_build_sync")) goto fail;
  cagra_finished = now_seconds();
  fprintf(stderr, "[seekdb_hipvs_hnsw] event=gpu_cagra_complete elapsed_ms=%.3f\n", (cagra_finished - started) * 1000.0);
  fflush(stderr);
  if (check_cuvs(cuvsHnswFromCagra(h->res, h->hnsw_params, h->cagra, h->hnsw), "cagra_to_full_hnsw")) goto fail;
  if (sync_index(h, "cagra_to_full_hnsw_sync")) goto fail;
  fprintf(stderr, "[seekdb_hipvs_hnsw] event=build_complete backend=cagra-to-hnsw hierarchy=%s hnsw_conversion_ms=%.3f total_ms=%.3f search_backend=cpu-hnsw fallback=disabled\n",
          h->hierarchy == GPU ? "gpu" : "cpu-explicit", (now_seconds() - cagra_finished) * 1000.0,
          (now_seconds() - started) * 1000.0);
  fflush(stderr);
  return h;
fail:
  destroy_index(h);
  return NULL;
}

int seekdb_cuvs_search(void* handle, const float* query, long nq, long topk,
                       unsigned int* out_ids, float* out_dist)
{
  sk_hnsw_index* h = (sk_hnsw_index*)handle;
  uint64_t* ids = NULL;
  float* distances = NULL;
  size_t count, i;
  int rc = 1;
  int64_t query_shape[2], output_shape[2];
  DLManagedTensor queries, neighbors, scores;
  if (!h || !query || !out_ids || nq <= 0 || topk <= 0 || topk > INT_MAX || topk > h->n
      || (uint64_t)nq > SIZE_MAX / (uint64_t)topk / sizeof(uint64_t)
      || (uint64_t)nq > SIZE_MAX / (uint64_t)h->dim / sizeof(float)) {
    error_text("search_input", "invalid handle, shape, topk, or output pointer"); return 1;
  }
  count = (size_t)nq * (size_t)topk;
  ids = (uint64_t*)malloc(count * sizeof(*ids));
  distances = (float*)malloc(count * sizeof(*distances));
  if (!ids || !distances) { error_text("search_allocate", "out of host memory"); goto done; }
  query_shape[0] = nq; query_shape[1] = h->dim;
  output_shape[0] = nq; output_shape[1] = topk;
  queries = host_matrix((void*)query, query_shape, kDLFloat, 32);
  neighbors = host_matrix(ids, output_shape, kDLUInt, 64);
  scores = host_matrix(distances, output_shape, kDLFloat, 32);
  pthread_mutex_lock(&h->mutex);
  h->search_params->ef = h->search_ef > topk ? h->search_ef : (int)topk;
  if (check_cuvs(cuvsHnswSearch(h->res, h->search_params, h->hnsw, &queries, &neighbors, &scores), "cpu_hnsw_search")) goto unlock;
  if (sync_index(h, "cpu_hnsw_search_sync")) goto unlock;
  for (i = 0; i < count; ++i) {
    if (ids[i] >= (uint64_t)h->n || ids[i] > UINT32_MAX || !isfinite(distances[i])) {
      error_text("search_output", "out-of-range row offset or nonfinite distance"); goto unlock;
    }
  }
  for (i = 0; i < count; ++i) out_ids[i] = (unsigned int)ids[i];
  if (out_dist) memcpy(out_dist, distances, count * sizeof(*distances));
  rc = 0;
unlock:
  pthread_mutex_unlock(&h->mutex);
done:
  free(ids);
  free(distances);
  return rc;
}

void seekdb_cuvs_free(void* handle)
{
  destroy_index((sk_hnsw_index*)handle);
}

int seekdb_cuvs_cagra_knn(const float* base, long n, long dim,
                          const float* query, long nq, long topk,
                          unsigned int* out_ids)
{
  void* h = seekdb_cuvs_build(base, n, dim);
  int rc;
  if (!h) return 10;
  rc = seekdb_cuvs_search(h, query, nq, topk, out_ids, NULL);
  seekdb_cuvs_free(h);
  return rc;
}

static void put32(unsigned char* p, uint32_t value)
{
  int i;
  for (i = 0; i < 4; ++i) p[i] = (unsigned char)(value >> (8 * i));
}
static void put64(unsigned char* p, uint64_t value)
{
  int i;
  for (i = 0; i < 8; ++i) p[i] = (unsigned char)(value >> (8 * i));
}
static uint32_t get32(const unsigned char* p)
{
  uint32_t value = 0;
  int i;
  for (i = 0; i < 4; ++i) value |= (uint32_t)p[i] << (8 * i);
  return value;
}
static uint64_t get64(const unsigned char* p)
{
  uint64_t value = 0;
  int i;
  for (i = 0; i < 8; ++i) value |= (uint64_t)p[i] << (8 * i);
  return value;
}

static char* temporary_path(const char* filename, const char* tag, int* fd)
{
  size_t size = strlen(filename) + strlen(tag) + 8;
  char* path = (char*)malloc(size);
  if (!path) { error_text("snapshot_path", "out of host memory"); return NULL; }
  snprintf(path, size, "%s%sXXXXXX", filename, tag);
  *fd = mkstemp(path);
  if (*fd < 0) { error_text("snapshot_mkstemp", strerror(errno)); free(path); return NULL; }
  return path;
}

static char* scratch_path(const char* tag, int* fd)
{
  const char* directory = getenv("TMPDIR");
  char* prefix;
  char* result;
  size_t length;
  if (!directory || !*directory) directory = "/tmp";
  length = strlen(directory) + sizeof("/seekdb-hnsw");
  prefix = (char*)malloc(length);
  if (!prefix) { error_text("scratch_path", "out of host memory"); return NULL; }
  snprintf(prefix, length, "%s/seekdb-hnsw", directory);
  result = temporary_path(prefix, tag, fd);
  free(prefix);
  return result;
}

static int is_proc_fd(const char* filename)
{
  const char* suffix;
  if (strncmp(filename, "/proc/self/fd/", 14)) return 0;
  suffix = filename + 14;
  if (!*suffix) return 0;
  for (; *suffix; ++suffix) if (*suffix < '0' || *suffix > '9') return 0;
  return 1;
}

static int copy_payload(FILE* source, FILE* destination, uint64_t limit, uint64_t* checksum)
{
  unsigned char buffer[65536];
  uint64_t left = limit, hash = FNV_OFFSET;
  while (left) {
    size_t want = left > sizeof(buffer) ? sizeof(buffer) : (size_t)left;
    size_t got = fread(buffer, 1, want, source), i;
    if (got != want || fwrite(buffer, 1, got, destination) != got) return 1;
    for (i = 0; i < got; ++i) { hash ^= buffer[i]; hash *= FNV_PRIME; }
    left -= got;
  }
  *checksum = hash;
  return 0;
}

int seekdb_cuvs_serialize(void* handle, const char* filename)
{
  sk_hnsw_index* h = (sk_hnsw_index*)handle;
  char* raw_path = NULL;
  char* output_path = NULL;
  int fd = -1, rc = 1, fd_target;
  FILE* raw = NULL;
  FILE* output = NULL;
  struct stat info;
  unsigned char header[SNAPSHOT_HEADER_BYTES] = {0};
  uint64_t checksum = 0;
  if (!h || !filename || !*filename) { error_text("serialize_input", "invalid handle or path"); return 1; }
  fd_target = is_proc_fd(filename);
  pthread_mutex_lock(&h->mutex);
  if (sync_index(h, "serialize_input_sync")) goto done;
  raw_path = scratch_path(".raw.", &fd);
  if (!raw_path) goto done;
  close(fd); fd = -1;
  if (check_cuvs(cuvsHnswSerialize(h->res, raw_path, h->hnsw), "serialize_full_hnsw")) goto done;
  if (sync_index(h, "serialize_full_hnsw_sync")) goto done;
  raw = fopen(raw_path, "rb");
  if (!raw || fstat(fileno(raw), &info) || info.st_size <= 0) { error_text("serialize_read", "missing or empty HNSW payload"); goto done; }
  if (fd_target) {
    /* A proc FD names the caller's already-open tmpfile inode. Reopening it
     * gives our own stream/offset; do not rename/unlink/close the caller FD. */
    output = fopen(filename, "wb+");
    if (!output) { error_text("serialize_proc_fd_open", strerror(errno)); goto done; }
  } else {
    output_path = temporary_path(filename, ".writing.", &fd);
    if (!output_path) goto done;
    output = fdopen(fd, "wb+");
    if (!output) { close(fd); fd = -1; error_text("serialize_open", strerror(errno)); goto done; }
    fd = -1;
  }
  memcpy(header, SNAPSHOT_MAGIC, 8);
  put32(header + 8, 1); put32(header + 12, SNAPSHOT_HEADER_BYTES);
  put64(header + 16, (uint64_t)h->n); put64(header + 24, (uint64_t)h->dim);
  put32(header + 32, (uint32_t)h->hierarchy); put32(header + 36, (uint32_t)h->graph_degree);
  put32(header + 40, (uint32_t)h->intermediate_degree); put32(header + 44, (uint32_t)h->ef_construction);
  put32(header + 48, (uint32_t)h->search_ef); put32(header + 52, (uint32_t)h->num_threads);
  put64(header + 56, (uint64_t)info.st_size);
  if (fwrite(header, 1, sizeof(header), output) != sizeof(header)
      || copy_payload(raw, output, (uint64_t)info.st_size, &checksum)) { error_text("serialize_write", "payload I/O failed"); goto done; }
  put64(header + 64, checksum);
  if (fseek(output, 0, SEEK_SET) || fwrite(header, 1, sizeof(header), output) != sizeof(header)
      || fflush(output) || fsync(fileno(output))) { error_text("serialize_flush", strerror(errno)); goto done; }
  if (fclose(output)) { output = NULL; error_text("serialize_close", strerror(errno)); goto done; }
  output = NULL;
  if (!fd_target && rename(output_path, filename)) { error_text("serialize_rename", strerror(errno)); goto done; }
  fprintf(stderr, "[seekdb_hipvs_hnsw] event=serialize_complete format=%s payload_bytes=%" PRIu64 " hnsw_data=included target=%s\n", SNAPSHOT_MAGIC, (uint64_t)info.st_size, fd_target ? "caller-proc-fd" : "regular-path");
  fflush(stderr);
  rc = 0;
done:
  if (output) fclose(output);
  if (raw) fclose(raw);
  if (raw_path) { unlink(raw_path); free(raw_path); }
  if (output_path) { if (rc) unlink(output_path); free(output_path); }
  pthread_mutex_unlock(&h->mutex);
  return rc;
}

void* seekdb_cuvs_deserialize(const char* filename, long n, long dim)
{
  sk_hnsw_index* h = NULL;
  FILE* input = NULL;
  FILE* raw = NULL;
  char* raw_path = NULL;
  int fd = -1, success = 0;
  unsigned char header[SNAPSHOT_HEADER_BYTES];
  uint64_t size, checksum = 0;
  struct stat info;
  if (!filename || !*filename || !valid_shape(n, dim)) { error_text("deserialize_input", "invalid path or shape"); return NULL; }
  input = fopen(filename, "rb");
  if (!input || fstat(fileno(input), &info) || fread(header, 1, sizeof(header), input) != sizeof(header)) {
    error_text("deserialize_header", "missing or truncated snapshot"); goto done;
  }
  size = get64(header + 56);
  if (memcmp(header, SNAPSHOT_MAGIC, 8) || get32(header + 8) != 1 || get32(header + 12) != sizeof(header)
      || get64(header + 16) != (uint64_t)n || get64(header + 24) != (uint64_t)dim
      || (get32(header + 32) != CPU && get32(header + 32) != GPU)
      || size == 0 || size > (uint64_t)INT64_MAX - sizeof(header)
      || (uint64_t)info.st_size != size + sizeof(header)
      || get32(header + 44) == 0 || get32(header + 44) > INT_MAX
      || get32(header + 48) == 0 || get32(header + 48) > INT_MAX || get32(header + 52) > INT_MAX) {
    error_text("deserialize_header", "format, shape, hierarchy, size or parameters mismatch"); goto done;
  }
  raw_path = scratch_path(".loading.", &fd);
  if (!raw_path) goto done;
  raw = fdopen(fd, "wb");
  if (!raw) { close(fd); fd = -1; error_text("deserialize_open", strerror(errno)); goto done; }
  fd = -1;
  if (copy_payload(input, raw, size, &checksum) || checksum != get64(header + 64) || fflush(raw)) {
    error_text("deserialize_payload", "payload I/O or checksum mismatch"); goto done;
  }
  if (fclose(raw)) { raw = NULL; error_text("deserialize_close", strerror(errno)); goto done; }
  raw = NULL;
  h = allocate_index(n, dim);
  if (!h) goto done;
  h->hierarchy = (int)get32(header + 32); h->graph_degree = (int)get32(header + 36);
  h->hnsw_m = h->graph_degree / 2;
  h->intermediate_degree = (int)get32(header + 40); h->ef_construction = (int)get32(header + 44);
  h->search_ef = (int)get32(header + 48); h->num_threads = (int)get32(header + 52);
  apply_hnsw_params(h);
  if (check_cuvs(cuvsHnswDeserialize(h->res, h->hnsw_params, raw_path, (int)dim, L2Expanded, h->hnsw), "deserialize_full_hnsw")) goto done;
  if (sync_index(h, "deserialize_full_hnsw_sync")) goto done;
  fprintf(stderr, "[seekdb_hipvs_hnsw] event=deserialize_complete format=%s n=%ld dim=%ld hierarchy=%s search_backend=cpu-hnsw rebuild=none\n",
          SNAPSHOT_MAGIC, n, dim, h->hierarchy == GPU ? "gpu" : "cpu-explicit");
  fflush(stderr);
  success = 1;
done:
  if (raw) fclose(raw);
  if (input) fclose(input);
  if (raw_path) { unlink(raw_path); free(raw_path); }
  if (!success) { destroy_index(h); h = NULL; }
  return h;
}
