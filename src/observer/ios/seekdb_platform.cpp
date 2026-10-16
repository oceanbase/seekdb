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

#include "seekdb_platform.h"
#include <cstdlib>
#include <TargetConditionals.h>
#include <jemalloc/jemalloc.h>

/** Complete allocator bootstrap on the loading thread before engine workers can initialize it. */
__attribute__((constructor)) static void initialize_framework_allocator()
{
  // Refuse a partially initialized allocator before a short-lived loader thread can retain bootstrap TSD.
  void *allocation = je_malloc(1);
  if (allocation == nullptr) {
    std::abort();
  }
  je_free(allocation);
}

namespace seekdb_platform {
const char *const *extra_startup_parameters()
{
  static const char *const parameters[] = {"sql_net_thread_count", "2", "cpu_count", "2", nullptr};
  return parameters;
}

const char *const *extra_initial_defaults()
{
  static const char *const parameters[] = {"vector_memory_limit", "128M", "datafile_maxsize", "20G", nullptr};
  return parameters;
}

/** Shorten long socket names inside the App home; simulator may use host /tmp. */
std::vector<std::string> socket_alias_patterns()
{
  std::vector<std::string> patterns;
  const char *home = std::getenv("HOME");
  if (home != nullptr && home[0] == '/') {
    std::string writable_home = home;
    // /var and /private/var name the same container; the short spelling leaves room for its writable tmp directory.
    if (writable_home.compare(0, 13, "/private/var/") == 0) {
      writable_home.erase(0, 8);
    }
    patterns.push_back(writable_home + "/tmp/.XXXXXX");
  }
#if TARGET_OS_SIMULATOR
  patterns.push_back("/tmp/seekdb-XXXXXX");
#endif
  return patterns;
}
}
