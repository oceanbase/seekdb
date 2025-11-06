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

#define USING_LOG_PREFIX RS
#include "ob_resource_weight_parser.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
namespace oceanbase
{
namespace rootserver
{

int ObResourceWeightParser::MyCb::match(const char *key, const char *value)
{
  int ret = OB_SUCCESS;
  static const int key_len[]    = { 5,        7,          4,       5};
  static const char *keys[]     = {"iops",   "memory",   "cpu",   "disk"};
  static WeightSetter setters[] = {set_iops, set_memory, set_cpu, set_disk};
  bool found = false;

  for (int i = 0; i < 4; ++i) {
    if (0 == STRNCASECMP(keys[i], key, key_len[i])) {
      int64_t w = 0;
      if (OB_FAIL(ob_atoll(value, w))) {
        LOG_WARN("fail parse value to int64", K(value), K(ret));
      } else {
        setters[i](weight_, static_cast<double>(w) / 100);
        found = true;
      }
      break;
    }
  }
  if (!found) {
    ret = OB_INVALID_CONFIG;
    LOG_WARN("unknown kv pair", K(key), K(value), K(ret));
  }
  return ret;
}



}/* ns rootserver*/
}/* ns oceanbase */


