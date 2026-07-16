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

#define USING_LOG_PREFIX STORAGE_FTS

#include "sql/engine/expr/ob_expr_pos_list.h"
#include "objit/common/ob_item_type.h"
#include "share/ob_fts_pos_list_codec.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprPosList::ObExprPosList(ObIAllocator &allocator)
  : ObFuncExprOperator(allocator, T_FUN_SYS_POS_LIST, N_POS_LIST, MORE_THAN_ZERO, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
  need_charset_convert_ = false;
}

int ObExprPosList::calc_result_typeN(
    ObExprResType &type,
    ObExprResType *types,
    int64_t param_num,
    ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSEDx(types, type_ctx);
  if (OB_UNLIKELY(param_num < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument for pos list expr", K(ret), K(param_num));
  } else {
    type.set_varbinary();
    type.set_length(share::ObFTSPositionListStore::MAX_INLINE_ENCODED_LENGTH);
    type.set_collation_level(CS_LEVEL_COERCIBLE);
  }
  return ret;
}

int ObExprPosList::calc_resultN(
    ObObj &result,
    const ObObj *objs_array,
    int64_t param_num,
    ObExprCtx &expr_ctx) const
{
  UNUSEDx(result, objs_array, param_num, expr_ctx);
  return OB_NOT_SUPPORTED;
}

int ObExprPosList::cg_expr(
    ObExprCGCtx &expr_cg_ctx,
    const ObRawExpr &raw_expr,
    ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSEDx(expr_cg_ctx, raw_expr);
  if (OB_UNLIKELY(rt_expr.arg_cnt_ < 1) || OB_ISNULL(rt_expr.args_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(rt_expr.arg_cnt_), KP(rt_expr.args_), K(rt_expr.type_));
  } else {
    rt_expr.eval_func_ = generate_pos_list;
  }
  return ret;
}

int ObExprPosList::generate_pos_list(
    const ObExpr &raw_ctx,
    ObEvalCtx &eval_ctx,
    ObDatum &expr_datum)
{
  UNUSEDx(raw_ctx, eval_ctx);
  expr_datum.set_null();
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
