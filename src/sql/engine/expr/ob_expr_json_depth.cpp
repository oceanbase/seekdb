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
#include "ob_expr_json_func_helper.h"
#include "ob_expr_json_depth.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObExprJsonDepth::ObExprJsonDepth(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_JSON_DEPTH, N_JSON_DEPTH, 1, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprJsonDepth::~ObExprJsonDepth()
{
}

int ObExprJsonDepth::calc_result_type1(ObExprResType &type,
                                       ObExprResType &type1,
                                       common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx); 
  INIT_SUCC(ret);
  type.set_int32();
  type.set_scale(common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObIntType].scale_);
  type.set_precision(common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObIntType].precision_);

  if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(type1, 1, N_JSON_DEPTH))) {
  }
  
  return ret;
}

int ObExprJsonDepth::eval_json_depth(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  // get json doc
  INIT_SUCC(ret);
  ObDatum *json_datum = NULL;
  ObExpr *json_arg = expr.args_[0];
  ObObjType val_type = json_arg->datum_meta_.type_;
  bool is_null_result = false;
  JsonMetadata metadata;
  bool allow_partial_update = false;

  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
  if (OB_FAIL(temp_allocator.eval_arg(json_arg, ctx, json_datum))) {
  } else if (val_type == ObNullType || json_datum->is_null()) {
    is_null_result = true;
  } else if (val_type != ObJsonType && !ob_is_string_type(val_type)) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
  } else if (OB_FAIL(ObJsonExprHelper::ensure_collation(
                 val_type, json_arg->datum_meta_.cs_type_))) {
  } else if (ob_is_json(val_type)
             && OB_FAIL(ObJsonExprHelper::is_allow_partial_update(
                 expr, ctx, json_datum->get_string(), allow_partial_update))) {
  } else if (allow_partial_update) {
    ObIJsonBase *json_doc = nullptr;
    if (OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, temp_allocator, 0,
                                               json_doc, is_null_result))) {
    } else if (!is_null_result) {
      metadata.depth_ = json_doc->depth();
    }
    if (json_doc != nullptr) {
      json_doc->reset();
    }
  } else {
    ObString data;
    if (OB_FAIL(ObTextStringHelper::read_real_string_data(
            ctx.exec_ctx_, temp_allocator, *json_datum, json_arg->datum_meta_,
            json_arg->obj_meta_.has_lob_header(), data))) {
    } else if (OB_FAIL(ObJsonExprHelper::get_json_metadata(
                   data, ObJsonExprHelper::get_json_internal_type(val_type), true, 0, metadata))) {
      ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
      LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT_IN_PARAM);
    }
  }

  // set result
  if (OB_FAIL(ret)) {
  } else if (is_null_result) {
    res.set_null();
  } else {
    res.set_int32(metadata.depth_);
  }

  return ret;
}

int ObExprJsonDepth::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_json_depth;
  return OB_SUCCESS;
}


}
}
