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
#include "ob_expr_multi_mode_func_helper.h"

namespace oceanbase
{
namespace sql
{


MultimodeAlloctor::MultimodeAlloctor(ObArenaAllocator &arena)
    : arena_(arena)
{
}

void *MultimodeAlloctor::alloc(const int64_t sz)
{
  return arena_.alloc(sz);
}

void *MultimodeAlloctor::alloc(const int64_t size, const ObMemAttr &attr)
{
  return arena_.alloc(size, attr);
}


int MultimodeAlloctor::eval_arg(const ObExpr *arg, ObEvalCtx &ctx, common::ObDatum *&datum)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(arg)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("invalid null expr argument", K(ret), K(arg));
  } else if (OB_FAIL(arg->eval(ctx, datum))) {
    LOG_WARN("eval geo arg failed", K(ret));
  }
  return ret;
}

};
};
