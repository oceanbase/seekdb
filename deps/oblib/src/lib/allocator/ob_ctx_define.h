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

#ifndef  OCEANBASE_COMMON_CTX_DEFINE_H_
#define  OCEANBASE_COMMON_CTX_DEFINE_H_

#include "lib/allocator/ob_mod_define.h"

namespace oceanbase
{
namespace common
{

struct ObCtxAttr
{
  const static int DEFAULT_CTX_PARALLEL = 8;

  bool enable_dirty_list_ = false;
  bool enable_no_log_ = false;
  int parallel_ = DEFAULT_CTX_PARALLEL;
  bool disable_sync_wash_ = false;
};

struct ObCtxAttrCenter
{
public:
  ObCtxAttrCenter()
  {
#define PARALLEL_DEF(name, parallel) ctx_attr_[ObCtxIds::name].parallel_ = parallel;
    PARALLEL_DEF(DEFAULT_CTX_ID, 32)
    PARALLEL_DEF(LIBEASY, 32)
    PARALLEL_DEF(PLAN_CACHE_CTX_ID, 4)
    PARALLEL_DEF(LOGGER_CTX_ID, 4)
#undef CTX_PARALLEL_DEF

#define ENABLE_DIRTY_LIST_DEF(name) ctx_attr_[ObCtxIds::name].enable_dirty_list_ = true;
    ENABLE_DIRTY_LIST_DEF(LIBEASY)
    ENABLE_DIRTY_LIST_DEF(LOGGER_CTX_ID)
#undef ENABLE_DIRTY_LIST_DEF

#define ENABLE_NO_LOG_DEF(name) ctx_attr_[ObCtxIds::name].enable_no_log_ = true;
    ENABLE_NO_LOG_DEF(LOGGER_CTX_ID)
#undef ENABLE_NO_LOG_DEF

#define DISABLE_SYNC_WASH_DEF(name) ctx_attr_[ObCtxIds::name].disable_sync_wash_ = true;
    DISABLE_SYNC_WASH_DEF(MERGE_RESERVE_CTX_ID)
#undef DISABLE_SYNC_WASH_DEF
  }
  static ObCtxAttrCenter &instance();
  ObCtxAttr attr_of_ctx(int64_t ctx_id) const
  {
    return ctx_attr_[ctx_id];
  }
private:
  ObCtxAttr ctx_attr_[ObCtxIds::MAX_CTX_ID];
};

#define CTX_ATTR(ctx_id) ObCtxAttrCenter::instance().attr_of_ctx(ctx_id)
}
}

#endif //OCEANBASE_COMMON_CTX_DEFINE_H_
