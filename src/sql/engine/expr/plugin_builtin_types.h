/*
 * Copyright (c) 2026 OceanBase.
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
#pragma once
#include "lib/string/ob_string.h"
#include <cstring>

namespace oceanbase { namespace sql {

// Only these namespaces have kernel-owned datum representations. Matching a
// suffix alone must never turn an arbitrary plugin type into a native datum.
struct PluginBuiltinTypes final
{
  static bool matches(const char *id, const char *suffix)
  {
    if (!id || !suffix) return false;
    constexpr const char *core = "core.type";
    constexpr const char *gis = "org.seekdb.gis.scalar";
    return (std::strncmp(id, core, std::strlen(core)) == 0 &&
            std::strcmp(id + std::strlen(core), suffix) == 0) ||
           (std::strncmp(id, gis, std::strlen(gis)) == 0 &&
            std::strcmp(id + std::strlen(gis), suffix) == 0) ||
           (std::strcmp(suffix, ".geometry") == 0 && std::strcmp(id, "org.seekdb.gis.geometry") == 0);
  }

  static bool contains(const common::ObString &id)
  {
    char text[256] = {};
    if (!id.ptr() || id.length() <= 0 || id.length() >= sizeof(text) ||
        std::memchr(id.ptr(), 0, id.length())) return false;
    std::memcpy(text, id.ptr(), id.length());
    for (const char *suffix : {".geometry", ".bool", ".int32", ".uint32", ".int64", ".uint64",
                               ".float64", ".decimal", ".bytes", ".text", ".blob"}) {
      if (matches(text, suffix)) return true;
    }
    return false;
  }
};
} }
