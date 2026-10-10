// Disposable scheduling controls; never include this file in production builds.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace {
bool candidate_probe_path(const char *suffix, char (&path)[4096])
{
  const char *prefix = getenv("SEEKDB_MERGE_CANDIDATES_PROBE");
  return prefix != nullptr && snprintf(path, sizeof(path), "%s.%s", prefix, suffix) < sizeof(path);
}

int64_t candidate_probe_value(const char *suffix)
{
  char path[4096];
  long value = 0;
  FILE *file = candidate_probe_path(suffix, path) ? fopen(path, "r") : nullptr;
  if (file != nullptr) {
    if (fscanf(file, "%ld", &value) != 1) { value = 0; }
    fclose(file);
  }
  return value;
}

void candidate_probe_log(const char *event, const int64_t f, const int64_t a = 0, const int64_t b = 0)
{
  char path[4096];
  FILE *file = candidate_probe_path("events", path) ? fopen(path, "a") : nullptr;
  if (file != nullptr) {
    fprintf(file, "%s F=%ld a=%ld b=%ld\n", event, f, a, b);
    fclose(file);
  }
}
}
