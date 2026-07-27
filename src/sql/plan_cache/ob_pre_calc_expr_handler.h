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

#ifndef OCEANBASE_SQL_PLAN_CACHE_OB_PRE_CALC_EXPR_HANDLER_H_
#define OCEANBASE_SQL_PLAN_CACHE_OB_PRE_CALC_EXPR_HANDLER_H_

#include "lib/alloc/alloc_struct.h"
#include "lib/allocator/page_arena.h"
#include "lib/atomic/ob_atomic.h"
#include "lib/list/ob_dlist.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace sql
{
struct ObPreCalcExprFrameInfo;

class PreCalcExprHandler
{
public:
  PreCalcExprHandler()
    : pc_alloc_(nullptr),
      alloc_(),
      pre_calc_frames_(nullptr),
      ref_cnt_(1)
  {
  }

  ~PreCalcExprHandler()
  {
    alloc_.reset();
    pc_alloc_ = nullptr;
    pre_calc_frames_ = nullptr;
  }

  void init(common::ObIAllocator *pc_alloc)
  {
    lib::ObMemAttr attr;
    attr.label_ = "PRE_CALC_EXPR";
    attr.ctx_id_ = common::ObCtxIds::PLAN_CACHE_CTX_ID;
    alloc_.set_attr(attr);
    pc_alloc_ = pc_alloc;
  }

  int64_t get_ref_count() const
  {
    return ATOMIC_LOAD64(&ref_cnt_);
  }

  void inc_ref_cnt()
  {
    ATOMIC_AAF(&ref_cnt_, 1);
  }

  int64_t dec_ref_cnt()
  {
    return ATOMIC_SAF(&ref_cnt_, 1);
  }

public:
  common::ObIAllocator *pc_alloc_;
  common::ObArenaAllocator alloc_;
  common::ObDList<ObPreCalcExprFrameInfo> *pre_calc_frames_;
  volatile int64_t ref_cnt_;
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_PLAN_CACHE_OB_PRE_CALC_EXPR_HANDLER_H_
