/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "hipvs_hnsw_bridge.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int write_all(int fd, const void *buffer, size_t size)
{
  const char *p = buffer;
  while (size) {
    ssize_t n = write(fd, p, size);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return -1;
    p += n;
    size -= (size_t)n;
  }
  return 0;
}

int main(int argc, char **argv)
{
  // FDs 3 and 4 are private anonymous files supplied by posix_spawn.
  SeekdbHipvsWorkerReply reply = {0};
  reply.status = 1;
  if (argc != 5) return 2;
  int64_t rows = strtoll(argv[1], NULL, 10), dim = strtoll(argv[2], NULL, 10);
  int m = atoi(argv[3]), ef = atoi(argv[4]);
  struct stat info;
  if (rows <= 0 || dim <= 0 || (uint64_t)rows > SIZE_MAX / sizeof(float) / dim ||
      fstat(3, &info) != 0 || (uint64_t)info.st_size != (uint64_t)rows * dim * sizeof(float)) {
    snprintf(reply.error, sizeof(reply.error), "invalid worker dataset size");
    write_all(4, &reply, sizeof(reply));
    return 1;
  }
  size_t bytes = rows * dim * sizeof(float);
  const float *vectors = mmap(NULL, bytes, PROT_READ, MAP_SHARED, 3, 0);
  if (vectors == MAP_FAILED) {
    snprintf(reply.error, sizeof(reply.error), "cannot map worker dataset");
    write_all(4, &reply, sizeof(reply));
    return 1;
  }
  unsigned char *output = NULL;
  reply.status = seekdb_hipvs_build_hnsw(vectors, rows, dim, m, ef, &output,
                                       &reply.output_size, &reply.stats, reply.error, sizeof(reply.error));
  int rc = write_all(4, &reply, sizeof(reply));
  if (rc == 0 && reply.status == 0) rc = write_all(4, output, reply.output_size);
  seekdb_hipvs_free(output);
  munmap((void *)vectors, bytes);
  return rc == 0 ? reply.status : 1;
}
