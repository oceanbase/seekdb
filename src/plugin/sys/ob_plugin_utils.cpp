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

#include "plugin/sys/ob_plugin_utils.h"

namespace oceanbase {
using namespace common;
namespace plugin {

lib::ObLabel OB_PLUGIN_MEMORY_LABEL = lib::ObLabel("PluginMod");

const char *ob_plugin_type_to_string(ObPluginType type)
{
  switch (type) {
    case OBP_PLUGIN_TYPE_INVALID:      return "INVALID";
    case OBP_PLUGIN_TYPE_FT_PARSER:    return "FTPARSER";
    default:                           return "UNKNOWN PLUGIN TYPE";
  }
}

static uint64_t data_tolower(uint64_t data)
{
  char *p = reinterpret_cast<char *>(&data);
  for (int i = 0; i < 8; i++) {
    p[i] = tolower(p[i]);
  }
  return data;
}

int ObPluginNameHash::operator() (const ObString &name, uint64_t &res) const
{
  // copy from murmurhash64A
  int ret = OB_SUCCESS;

  const char *key = name.ptr();
  int64_t len = name.length();

  const uint64_t multiply = 0xc6a4a7935bd1e995;
  const int rotate = 47;

  const uint64_t seed = 0;
  uint64_t hash_ret = seed ^ (len * multiply);

  const uint64_t *data = (const uint64_t *)key;
  const uint64_t *end = data + (len / 8);
  for (; len >= 8; len -= 8) {
    uint64_t val = data_tolower(*data);
    val *= multiply;
    val ^= val >> rotate;
    val *= multiply;
    hash_ret ^= val;
    hash_ret *= multiply;
    ++data;
  }

  const unsigned char *data2 = (const unsigned char *)data;
  while (len > 0) {
    --len;
    hash_ret ^= uint64_t(tolower(data2[len])) << (len * 8);
    if (0 == len) {
      hash_ret *= multiply;
    }
  }
  hash_ret ^= hash_ret >> rotate;
  hash_ret *= multiply;
  hash_ret ^= hash_ret >> rotate;

  res = hash_ret;
  return ret;
}

bool ObPluginNameEqual::operator()(const ObString &name1, const ObString &name2) const
{
  bool ret = false;
  if (name1.length() != name2.length()) {
  } else {
    ret = (0 == name1.case_compare(name2));
  }
  return ret;
}

} // namespace plugin
} // namespace oceanbase
