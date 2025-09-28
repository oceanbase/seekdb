/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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

