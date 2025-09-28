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
 * This file contains implementation of json_object.
 */

#define USING_LOG_PREFIX SQL_ENG
#include "ob_expr_json_object.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "share/ob_json_access_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObExprJsonObject::ObExprJsonObject(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_JSON_OBJECT, N_JSON_OBJECT, OCCUR_AS_PAIR, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprJsonObject::~ObExprJsonObject()
{
}

int ObExprJsonObject::calc_result_typeN(ObExprResType& type,
                                        ObExprResType* types_stack,
                                        int64_t param_num,
                                        ObExprTypeCtx& type_ctx) const
{
  INIT_SUCC(ret);
  if (OB_UNLIKELY(param_num < 0 || param_num % 2 != 0)) {
    ret = OB_ERR_PARAM_SIZE;
    const ObString name = "json_object";
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, name.length(), name.ptr());
  } else {
    type.set_json();
    type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());
 
    ObSQLSessionInfo *session = const_cast<ObSQLSessionInfo *>(type_ctx.get_session());
    ObExecContext* ctx = nullptr;

    bool is_deduce_input = true;
    if (OB_NOT_NULL(session)) {
      is_deduce_input = (!session->is_varparams_sql_prepare());
    }

    for (int64_t i = 0; OB_SUCC(ret) && is_deduce_input && i < param_num; i += 2) {
      if ((types_stack[i].get_type() == ObNullType)) {
        ret = OB_ERR_JSON_DOCUMENT_NULL_KEY;
        LOG_USER_ERROR(OB_ERR_JSON_DOCUMENT_NULL_KEY);
      } else if (ob_is_string_type(types_stack[i].get_type())) {
        if (types_stack[i].get_charset_type() == CHARSET_BINARY) {
          ret = OB_ERR_INVALID_JSON_CHARSET;
          LOG_USER_ERROR(OB_ERR_INVALID_JSON_CHARSET);
        } else if (types_stack[i].get_charset_type() != CHARSET_UTF8MB4) {
          types_stack[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
        }
      } else {
        types_stack[i].set_calc_type(ObLongTextType);
        types_stack[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
      }

      if (OB_SUCC(ret)) {
        if (ob_is_string_type(types_stack[i+1].get_type())) {
          if (types_stack[i+1].get_charset_type() != CHARSET_UTF8MB4) {
            types_stack[i+1].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
          }
        } else if (types_stack[i+1].get_type() == ObJsonType) {
          types_stack[i+1].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
        }
      }
    }
  }

  return ret;
}

// for new sql engine
int ObExprJsonObject::eval_json_object(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  INIT_SUCC(ret);
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  uint64_t tenant_id = ObMultiModeExprHelper::get_tenant_id(ctx.exec_ctx_.get_my_session());
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator(), expr.type_, tenant_id, ret);
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(tenant_id, "JSONModule"));
  ObJsonObject j_obj(&temp_allocator);
  ObIJsonBase *j_base = &j_obj;

  if (expr.datum_meta_.cs_type_ != CS_TYPE_UTF8MB4_BIN) {
    ret = OB_ERR_INVALID_JSON_CHARSET;
    LOG_WARN("invalid out put charset", K(ret), K(expr.datum_meta_.cs_type_));
  }

  for (int32 i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; i += 2) {
    ObExpr *arg = expr.args_[i];
    ObDatum *json_datum = NULL;  
    if (OB_FAIL(temp_allocator.eval_arg(arg, ctx, json_datum))) {
      LOG_WARN("failed: eval json args datum failed", K(ret));
    } else if (json_datum->is_null()) {
      ret = OB_ERR_JSON_DOCUMENT_NULL_KEY;
      LOG_USER_ERROR(OB_ERR_JSON_DOCUMENT_NULL_KEY);
      LOG_WARN("failed:json key is null", K(ret));
    } else {
      ObString key = json_datum->get_string();
      ObIJsonBase *j_val = NULL;
      bool is_null = false;
      if (OB_FAIL(ObJsonExprHelper::get_json_or_str_data(arg, ctx, temp_allocator, key, is_null))) {
        LOG_WARN("fail to get real data.", K(ret), K(key));
      } else if (OB_FALSE_IT(temp_allocator.add_baseline_size(key.length()))) {
      } else if (OB_FAIL(temp_allocator.add_baseline_size(expr.args_[i+1], ctx))) {
        LOG_WARN("failed to add baseline size.", K(ret), K(i+1));
      } else if (OB_FAIL(ObJsonExprHelper::get_json_val(expr, ctx, &temp_allocator, i+1, j_val))) {
        ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT_IN_PARAM);
      } else if (OB_FAIL(j_obj.add(key, static_cast<ObJsonNode*>(j_val), false, true, false))) {
        if (ret == OB_ERR_JSON_DOCUMENT_NULL_KEY) {
          LOG_USER_ERROR(OB_ERR_JSON_DOCUMENT_NULL_KEY);
        }
        LOG_WARN("failed: append json object kv", K(ret));
      }
    }
  }

  if (OB_SUCC(ret)) {
    ObString raw_bin;
    j_obj.stable_sort();
    j_obj.unique();
    if (ObJsonExprHelper::is_json_depth_exceed_limit(j_base->depth())) {
      ret = OB_ERR_JSON_OUT_OF_DEPTH;
      LOG_WARN("current json over depth", K(ret), K(j_base->depth()));
    } else if (OB_FAIL(ObJsonWrapper::get_raw_binary(j_base, raw_bin, &temp_allocator))) {
      LOG_WARN("failed: get json raw binary", K(ret));
    } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, res, raw_bin))) {
      LOG_WARN("fail to pack json result", K(ret));
    }
  }

  return ret;
}

int ObExprJsonObject::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_json_object;
  return OB_SUCCESS;
}                    




} // sql
} // oceanbase