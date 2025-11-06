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


#include "deps/oblib/src/lib/allocator/ob_slice_alloc.h"

namespace oceanbase
{
namespace common
{

#define LABEL_ITEM_DEF(name, ...) constexpr const char ObModIds::name[];
#include "lib/allocator/ob_mod_define.h"
#undef LABEL_ITEM_DEF

bool ObCtxInfo::is_valid_ctx_name(const char *ctx_name, uint64_t& ctx_id) const
{
  bool ret = false;
  for (int i = 0; i < ObCtxIds::MAX_CTX_ID && !ret; ++i) {
    if (0 == strcmp(ctx_names_[i], ctx_name)) {
      ret = true;
      ctx_id = i;
    }
  }
  return ret;
}

}; // end namespace common
}; // end namespace oceanbase
