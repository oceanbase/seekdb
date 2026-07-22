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

#include "rpc/ob_libeasy_mem_pool.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/utility/ob_tracepoint.h"

using namespace oceanbase;
using namespace oceanbase::common;

void *common::ob_easy_realloc(void *ptr, size_t size)
{
  void *ret = NULL;
  if (size != 0) {
    ObMemAttr attr;
    attr.label_ = "rpc";
    attr.ctx_id_ = ObCtxIds::DEFAULT_CTX_ID;
    
    {
      TP_SWITCH_GUARD(true);
      ret = ob_realloc(ptr, size, attr);
    }
    if (ret == NULL) {
      _OB_LOG_RET(WARN, OB_ALLOCATE_MEMORY_FAILED, "ob_tc_realloc failed, ptr:%p, size:%lu", ptr, size);
    }
  } else if (ptr) {
    ob_free(ptr);
  }
  return ret;
}
