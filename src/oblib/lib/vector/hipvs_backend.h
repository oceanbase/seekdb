/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(__linux__)
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace oceanbase {
namespace common {
namespace obvsag {

// Construct lazily, only for an explicitly requested HIPVS index. Never search
// the working directory, and never replace a broken explicit override silently.
class HipvsBackend {
public:
  using BuildFn = int (*)(const float *, int64_t, int32_t, int32_t, int32_t,
                         int32_t, const char *, char *, size_t);
  BuildFn build = nullptr;
  char error[4096] = {};

  HipvsBackend()
  {
#if defined(__linux__)
    const char *configured = std::getenv("SEEKDB_HIPVS_BUILD_LIBRARY");
    if (configured && configured[0]) {
      if (configured[0] != '/') {
        std::snprintf(error, sizeof(error), "SEEKDB_HIPVS_BUILD_LIBRARY must be an absolute path");
      } else {
        load(configured);
      }
      return;
    }
    char executable[4096] = {};
    const ssize_t size = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (size <= 0 || size >= static_cast<ssize_t>(sizeof(executable) - 1)) {
      std::snprintf(error, sizeof(error), "cannot resolve /proc/self/exe; set SEEKDB_HIPVS_BUILD_LIBRARY");
      return;
    }
    char *slash = std::strrchr(executable, '/');
    if (!slash) return;
    *slash = 0;
    // CMake build tree / copied binary+bridge, then the packaged usr/bin layout.
    const char *suffixes[] = {"/libseekdb_hipvs_build.so", "/../lib/seekdb/libseekdb_hipvs_build.so"};
    for (const char *suffix : suffixes) {
      char path[8192] = {};
      std::snprintf(path, sizeof(path), "%s%s", executable, suffix);
      if (access(path, F_OK) == 0) {
        load(path);
        return; // An installed but broken backend must not be masked.
      }
    }
    std::snprintf(error, sizeof(error),
        "libseekdb_hipvs_build.so not found next to executable or in ../lib/seekdb; "
        "build with -DSEEKDB_WITH_HIPVS=ON on a ROCm/hipVS host, or set SEEKDB_HIPVS_BUILD_LIBRARY");
#else
    std::snprintf(error, sizeof(error), "HIPVS construction requires Linux and AMD ROCm");
#endif
  }

  HipvsBackend(const HipvsBackend &) = delete;
  HipvsBackend &operator=(const HipvsBackend &) = delete;

private:
#if defined(__linux__)
  void load(const char *path)
  {
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
      const char *detail = dlerror();
      std::snprintf(error, sizeof(error), "dlopen(%s): %s", path, detail ? detail : "unknown error");
      return;
    }
    dlerror();
    void *symbol = dlsym(library, "seekdb_hipvs_build_hnsw_v1");
    const char *detail = dlerror();
    if (detail || !symbol) {
      std::snprintf(error, sizeof(error), "dlsym(%s, seekdb_hipvs_build_hnsw_v1): %s",
                    path, detail ? detail : "null symbol");
    } else {
      build = reinterpret_cast<BuildFn>(symbol);
    }
    // Keep ROCm loaded for process lifetime, even on symbol lookup failure:
    // unloading a library with active internal workers/TLS is unsafe.
  }
#endif
};

} // namespace obvsag
} // namespace common
} // namespace oceanbase
