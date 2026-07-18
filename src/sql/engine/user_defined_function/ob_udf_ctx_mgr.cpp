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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_udf_ctx_mgr.h"
#include "sql/engine/expr/ob_expr_dll_udf.h"

namespace oceanbase
{
namespace sql
{

using namespace common;
using ObUdfCtx = ObUdfFunction::ObUdfCtx;

ObUdfCtxMgr::~ObUdfCtxMgr() {
  if (ctxs_.created()) {
    common::hash::ObHashMap<uint64_t, ObNormalUdfExeUnit *,
        common::hash::NoPthreadDefendMode>::iterator iter = ctxs_.begin();
    for (; iter != ctxs_.end(); iter++) {
      ObNormalUdfExeUnit *normal_unit = iter->second;
      if (OB_NOT_NULL(normal_unit->normal_func_) && OB_NOT_NULL(normal_unit->udf_ctx_)) {
        IGNORE_RETURN normal_unit->normal_func_->process_deinit_func(*normal_unit->udf_ctx_);
      }
    }
  }
  allocator_.reset();
  ctxs_.destroy();
}



int ObUdfCtxMgr::try_init_map()
{
  int ret = OB_SUCCESS;
  if (!ctxs_.created()) {
    if (OB_FAIL(ctxs_.create(common::hash::cal_next_prime(BUKET_NUM),
                             common::ObModIds::OB_SQL_UDF,
                             common::ObModIds::OB_SQL_UDF))) {
      LOG_WARN("create hash failed", K(ret));
    }
  }
  return ret;
}

int ObUdfCtxMgr::reset()
{
  int ret = OB_SUCCESS;
  if (ctxs_.created()) {
    common::hash::ObHashMap<uint64_t, ObNormalUdfExeUnit *, common::hash::NoPthreadDefendMode>::iterator iter = ctxs_.begin();
    for (; iter != ctxs_.end(); iter++) {
      ObNormalUdfExeUnit *normal_unit = iter->second;
      if (OB_NOT_NULL(normal_unit->normal_func_) && OB_NOT_NULL(normal_unit->udf_ctx_)) {
        IGNORE_RETURN normal_unit->normal_func_->process_deinit_func(*normal_unit->udf_ctx_);
      }
    }
    ctxs_.clear();
  }
  allocator_.reset();
  return ret;
}



}
}

