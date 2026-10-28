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
#include "ob_expr_json_length.h"
#include "ob_expr_json_func_helper.h"
#include "ob_expr_json_native_path.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObExprJsonLength::ObExprJsonLength(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_JSON_LENGTH, N_JSON_LENGTH,
                         ONE_OR_TWO, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprJsonLength::~ObExprJsonLength()
{
}

int ObExprJsonLength::calc_result_typeN(ObExprResType& type,
                                        ObExprResType* types_stack,
                                        int64_t param_num,
                                        ObExprTypeCtx& type_ctx) const
{
  INIT_SUCC(ret);
  UNUSED(type_ctx);

  // set result type to int32
  type.set_int32();
  type.set_scale(common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObIntType].scale_);
  type.set_precision(common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObIntType].precision_);

  // 0 position is json doc
  if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types_stack, 0, N_JSON_LENGTH))) {
  } else if (param_num > 1 && OB_FAIL(ObJsonExprHelper::is_valid_for_path(types_stack, 1))) {
  }

  return ret;
}

int ObExprJsonLength::calc(ObEvalCtx &ctx, const ObDatum &data1, ObDatumMeta meta1, bool has_lob_header1,
                           const ObDatum *data2, ObDatumMeta meta2, bool has_lob_header2,
                           MultimodeAlloctor *allocator, ObDatum &res,
                           JsonPathCache* path_cache)
{
  INIT_SUCC(ret);
  bool is_null = false;
  uint32_t res_len = 0;
  ObString j_doc;
  ObJsonInType j_in_type = ObJsonInType::JSON_TEXT;
  ObObjType type1 = meta1.type_;
  ObCollationType cs_type1 = meta1.cs_type_;
  // handle data1(json text)
  if (type1 == ObNullType || data1.is_null()) { // null should display "NULL"
    is_null = true;
  } else if (type1 != ObJsonType && !ob_is_string_type(type1)) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("input type error", K(type1));
  } else if (OB_FAIL(ObJsonExprHelper::ensure_collation(type1, cs_type1))) {
  } else {
    j_doc = data1.get_string();
    j_in_type = ObJsonExprHelper::get_json_internal_type(type1);
    if (OB_FAIL(ObTextStringHelper::read_real_string_data(
            ctx.exec_ctx_, *allocator, data1, meta1, has_lob_header1, j_doc))) {
    }
  }

  // handle data2(path text)
  if (OB_SUCC(ret) && OB_LIKELY(!is_null)) {
    if (OB_ISNULL(data2)) {
      JsonMetadata metadata;
      if (OB_FAIL(ObJsonExprHelper::get_json_metadata(
              j_doc, j_in_type, false, ObJsonExprHelper::get_json_max_depth_config(), metadata))) {
      } else {
        res_len = metadata.length_;
      }
    } else { // handle json path
      ObObjType type2 = meta2.type_;
      if (type2 == ObNullType) { // null should display "NULL"
        is_null = true;
      } else { // ObLongTextType
        ObString j_path_text = data2->get_string();
        const common::json::Path *j_path = NULL;
        JsonPathMatches matches;
        if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, *allocator, *data2, meta2, has_lob_header2, j_path_text))) {
        } else if (OB_FAIL(find_and_add_native_path_cache(path_cache, j_path, j_path_text, 1, true))) {
          LOG_USER_ERROR(OB_ERR_INVALID_JSON_PATH);
        } else if (OB_FAIL(query_native_json_path(
                       j_doc, j_in_type, *j_path, true, false,
                       ObJsonExprHelper::get_json_max_depth_config(), matches))) {
        } else if (matches.count_ == 0) {
          is_null = true;
        } else if (matches.count_ > 1) {
          res_len = matches.count_;
        } else {
          res_len = matches.single_length_;
        }
      }
    }
  }

  // set result
  if (OB_SUCC(ret)) {
    if (is_null) {
      res.set_null();
    } else {
      res.set_int32(res_len);
    }
  }

  return ret;
}

// for new sql engine 
int ObExprJsonLength::eval_json_length(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  INIT_SUCC(ret);
  ObDatum *datum1 = NULL;
  ObDatumMeta meta1;
  bool has_lob_header1 = false;
  ObDatum *datum0 = NULL;
  ObExpr *arg0 = expr.args_[0];
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  
  MultimodeAlloctor tmp_allocator(tmp_alloc_g.get_allocator());

  if (OB_FAIL(tmp_allocator.eval_arg(arg0, ctx, datum0))) {
  } else {
    if (expr.arg_cnt_ > 1) { // json path
      ObExpr *arg1 = expr.args_[1];
      meta1 = arg1->datum_meta_;
      has_lob_header1 = arg1->obj_meta_.has_lob_header();
      if (OB_FAIL(tmp_allocator.eval_arg(arg1, ctx, datum1))) {
      }
    }
  }

  if (OB_SUCC(ret)) {
    JsonPathCache ctx_cache;
    JsonPathCache* path_cache = get_native_path_cache_ctx(expr.expr_ctx_id_, &ctx.exec_ctx_);
    path_cache = ((path_cache != NULL) ? path_cache : &ctx_cache);

    if (OB_FAIL(calc(ctx, *datum0, arg0->datum_meta_, arg0->obj_meta_.has_lob_header(),
                     datum1, meta1, has_lob_header1, &tmp_allocator, res, path_cache))) {
    }
  }

  return ret;
}

int ObExprJsonLength::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_json_length;
  return OB_SUCCESS;
}


}
}
