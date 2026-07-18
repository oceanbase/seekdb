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

#include "ob_trans_memory_stat.h"

namespace oceanbase
{
using namespace common;

namespace transaction
{
void ObTransMemoryStat::reset()
{
  addr_.reset();
  type_[0] = '\0';
  alloc_count_ = 0;
  release_count_ = 0;
}

int ObTransMemoryStat::init(const common::ObAddr &addr, const char *mod_type,
    const int64_t alloc_count, const int64_t release_count)
{
  int ret = OB_SUCCESS;

  if (!addr.is_valid() || OB_ISNULL(mod_type) || alloc_count < 0 || release_count < 0) {
    TRANS_LOG(WARN, "invalid argument", K(addr), KP(mod_type), K(alloc_count),
      K(release_count));
    ret = OB_INVALID_ARGUMENT;
  } else {
    int64_t len = strlen(mod_type);
    len = (OB_TRANS_MEMORY_MOD_TYPE_SIZE -1) > len ? len : OB_TRANS_MEMORY_MOD_TYPE_SIZE -1;
    strncpy(type_, mod_type, len);
    type_[len] = '\0';
    addr_ = addr;
    alloc_count_ = alloc_count;
    release_count_ = release_count;
  }

  return ret;
}

} // transaction
} // oceanbase
