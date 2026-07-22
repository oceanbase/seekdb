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

#define USING_LOG_PREFIX SQL_RESV
#include "sql/engine/expr/ob_expr_uuid_short.h"
#include "observer/ob_server_struct.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

ObExprUuidShort::ObExprUuidShort(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_FUN_SYS_UUID_SHORT, N_UUID_SHORT, 0, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprUuidShort::~ObExprUuidShort()
{
}

uint64_t ObExprUuidShort::generate_uuid_short()
{
  //                        uuid_short
  // |             <40>             |       <24>
  //       process startup time          counter
  const int64_t process_start_time = GCTX.start_time_ > 0
      ? GCTX.start_time_ : common::ObTimeUtility::current_time();
  static volatile uint64_t startup_time_and_counter =
      (static_cast<uint64_t>(process_start_time / 1000000) & ((1ULL << 40) - 1)) << 24;
  uint64_t uuid_short = ATOMIC_AAF(&startup_time_and_counter, 1);
  LOG_DEBUG("uuid_short generated.", K(uuid_short));
  return uuid_short;
}

int ObExprUuidShort::cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const
{
  UNUSED(raw_expr);
  UNUSED(expr_cg_ctx);
  rt_expr.eval_func_ = ObExprUuidShort::eval_uuid_short;
  return OB_SUCCESS;
}

int ObExprUuidShort::eval_uuid_short(const ObExpr &expr,
                      ObEvalCtx &ctx,
                      ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  UNUSED(expr);
  UNUSED(ctx);
  expr_datum.set_uint(generate_uuid_short());
  return ret;
}

} // namespace sql
} // namespace oceanbase
