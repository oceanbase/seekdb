"""Compile actual snapshot I/O with explicit cuVS stubs; this is no GPU test."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


PREFIX = r'''
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#define SNAPSHOT_HEADER_BYTES 80
#define SNAPSHOT_MAGIC "SKHNSW01"
#define FNV_OFFSET UINT64_C(14695981039346656037)
#define FNV_PRIME UINT64_C(1099511628211)
#define CPU 1
#define GPU 2
#define L2Expanded 0
typedef struct {
  void *res, *hnsw, *hnsw_params;
  long n, dim;
  int hierarchy, graph_degree, intermediate_degree, ef_construction;
  int search_ef, num_threads, hnsw_m;
  pthread_mutex_t mutex;
} sk_hnsw_index;
static unsigned char payload[4096];
static int serialize_calls = 0, deserialize_calls = 0;
static void error_text(const char *stage, const char *text) {
  fprintf(stderr, "%s: %s\n", stage, text);
}
static int sync_index(sk_hnsw_index *h, const char *stage) { return 0; }
static int check_cuvs(int status, const char *stage) { return status; }
static int valid_shape(long n, long dim) { return n > 0 && dim > 0; }
static sk_hnsw_index *allocate_index(long n, long dim) {
  sk_hnsw_index *h = (sk_hnsw_index *)calloc(1, sizeof(*h));
  if (h) { h->n = n; h->dim = dim; pthread_mutex_init(&h->mutex, NULL); }
  return h;
}
static void destroy_index(sk_hnsw_index *h) {
  if (h) { pthread_mutex_destroy(&h->mutex); free(h); }
}
static void apply_hnsw_params(sk_hnsw_index *h) {}
static int cuvsHnswSerialize(void *res, const char *path, void *hnsw) {
  FILE *f = fopen(path, "wb");
  if (!f) return 1;
  ++serialize_calls;
  int fail = fwrite(payload, 1, sizeof(payload), f) != sizeof(payload);
  return fclose(f) || fail;
}
static int cuvsHnswDeserialize(void *res, void *params, const char *path,
                              int dim, int metric, void *hnsw) {
  unsigned char buffer[4096];
  FILE *f = fopen(path, "rb");
  if (!f) return 1;
  ++deserialize_calls;
  int fail = fread(buffer, 1, sizeof(buffer), f) != sizeof(buffer)
             || memcmp(buffer, payload, sizeof(buffer)) || fgetc(f) != EOF;
  return fclose(f) || fail;
}
'''

SUFFIX = r'''
static int checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "CHECK failed line %d: %s\n", __LINE__, #c); return 2; } } while (0)
int main(int argc, char **argv) {
  CHECK(argc == 2);
  CHECK(setenv("TMPDIR", argv[1], 1) == 0);
  for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = (unsigned char)(i * 31);
  sk_hnsw_index *h = allocate_index(1024, 32);
  CHECK(h != NULL);
  h->hierarchy = GPU; h->graph_degree = 32; h->intermediate_degree = 64;
  h->ef_construction = 200; h->search_ef = 512; h->num_threads = 1;
  FILE *save = tmpfile(), *load = tmpfile();
  CHECK(save && load);
  int save_fd = fileno(save), load_fd = fileno(load);
  char save_path[80], load_path[80], regular_path[1024];
  snprintf(save_path, sizeof(save_path), "/proc/self/fd/%d", save_fd);
  snprintf(load_path, sizeof(load_path), "/proc/self/fd/%d", load_fd);
  snprintf(regular_path, sizeof(regular_path), "%s/regular.bin", argv[1]);
  CHECK(is_proc_fd(save_path));
  CHECK(!is_proc_fd("/proc/self/fd/"));
  CHECK(!is_proc_fd("/proc/self/fd/12.raw"));
  CHECK(seekdb_cuvs_serialize(h, save_path) == 0);
  CHECK(fileno(save) == save_fd && fseeko(save, 0, SEEK_END) == 0);
  long length = ftello(save);
  CHECK(length == SNAPSHOT_HEADER_BYTES + sizeof(payload));
  unsigned char bytes[SNAPSHOT_HEADER_BYTES + sizeof(payload)];
  rewind(save);
  CHECK(fread(bytes, 1, sizeof(bytes), save) == sizeof(bytes));
  CHECK(!memcmp(bytes, SNAPSHOT_MAGIC, 8));
  CHECK(fwrite(payload, 1, sizeof(payload), save) == sizeof(payload));
  CHECK(fflush(save) == 0 && ftello(save) > length);
  CHECK(seekdb_cuvs_serialize(h, save_path) == 0);
  CHECK(fseeko(save, 0, SEEK_END) == 0 && ftello(save) == length);
  CHECK(fwrite(bytes, 1, sizeof(bytes), load) == sizeof(bytes) && fflush(load) == 0);
  CHECK(ftello(load) == length);
  sk_hnsw_index *restored = (sk_hnsw_index *)seekdb_cuvs_deserialize(load_path, 1024, 32);
  CHECK(restored && restored->n == 1024 && restored->dim == 32 && restored->search_ef == 512);
  CHECK(fileno(load) == load_fd && ftello(load) == length);
  CHECK(serialize_calls == 2 && deserialize_calls == 1);
  destroy_index(restored);
  CHECK(seekdb_cuvs_serialize(h, regular_path) == 0);
  restored = (sk_hnsw_index *)seekdb_cuvs_deserialize(regular_path, 1024, 32);
  CHECK(restored && deserialize_calls == 2);
  destroy_index(restored);
  CHECK(!seekdb_cuvs_deserialize(regular_path, 1025, 32));
  FILE *f = fopen(regular_path, "rb+");
  CHECK(f && fseeko(f, -1, SEEK_END) == 0);
  CHECK(fputc(17, f) != EOF && fclose(f) == 0);
  CHECK(!seekdb_cuvs_deserialize(regular_path, 1024, 32));
  CHECK(deserialize_calls == 2);
  CHECK(fclose(save) == 0 && fclose(load) == 0);
  CHECK(unlink(regular_path) == 0);
  DIR *dir = opendir(argv[1]);
  CHECK(dir != NULL);
  struct dirent *entry;
  int entries = 0;
  while ((entry = readdir(dir))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) ++entries;
  CHECK(closedir(dir) == 0 && entries == 0);
  destroy_index(h);
  printf("{\"passed\":true,\"checks\":%d,\"gpu_executed\":false,\"scope\":\"actual snapshot I/O; cuVS backend explicitly stubbed\"}\n", checks);
  return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generate", type=Path,
                        help="Only write the extracted C harness to this path")
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2] / "tools/hipvs_bridge/seekdb_cuvs_bridge.c"
    actual = source.read_text(encoding="utf-8")
    snapshot_functions = actual[actual.index("static void put32("):]
    harness = PREFIX + snapshot_functions + SUFFIX
    receipt = {"bridge_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
               "scope": "I/O contract only, cuVS stubs explicit",
               "gpu_executed": False}
    if args.generate:
        args.generate.write_text(harness, encoding="utf-8")
        print(json.dumps({**receipt, "generated": str(args.generate)}))
        return
    compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("clang")
    if not compiler:
        raise RuntimeError("A POSIX C11 compiler is required")
    with tempfile.TemporaryDirectory(prefix="seekdb-snapshot-contract-") as folder:
        root = Path(folder)
        c_file, binary, scratch = root / "contract.c", root / "contract", root / "scratch"
        c_file.write_text(harness, encoding="utf-8")
        scratch.mkdir()
        subprocess.run([compiler, "-std=c11", "-O2", "-pthread",
                        str(c_file), "-o", str(binary)], check=True)
        result = subprocess.run([str(binary), str(scratch)], check=True,
                                capture_output=True, text=True)
        print(json.dumps({**receipt, **json.loads(result.stdout),
                          "harness_sha256": hashlib.sha256(harness.encode()).hexdigest()}))


if __name__ == "__main__":
    main()
