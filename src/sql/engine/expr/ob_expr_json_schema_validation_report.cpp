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

#include "ob_expr_json_schema_valid.h"
#include "ob_expr_json_schema_validation_report.h"
#include "ob_expr_json_func_helper.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
OB_SERIALIZE_MEMBER((ObExprJsonSchemaValidationReport, ObFuncExprOperator), json_schema_);

ObExprJsonSchemaValidationReport::ObExprJsonSchemaValidationReport(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_JSON_SCHEMA_VALIDATION_REPORT, N_JSON_SCHEMA_VALIDATION_REPORT, OB_JSON_SCHEMA_EXPR_ARG_NUM, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION),
      json_schema_(ObString::make_empty_string()) {}

ObExprJsonSchemaValidationReport::~ObExprJsonSchemaValidationReport()
{
}

int ObExprJsonSchemaValidationReport::calc_result_type2(ObExprResType &type,
                                                        ObExprResType &type1,
                                                        ObExprResType &type2,
                                                        ObExprTypeCtx &type_ctx) const
{
  INIT_SUCC(ret);
  UNUSED(type_ctx);

  // set the result type: json
  type.set_json();
  type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());

  // 1st param is json schema (also json doc)
  // 2nd param is json schema (also json doc)
  if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(type1, 1, N_JSON_SCHEMA_VALID))) {
  } else if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(type2, 2, N_JSON_SCHEMA_VALID))) {
  }

  return ret;
}

int ObExprJsonSchemaValidationReport::cg_expr(ObExprCGCtx &op_cg_ctx,
                                              const ObRawExpr &raw_expr,
                                              ObExpr &rt_expr) const
{
  INIT_SUCC(ret);
  const ObRawExpr *schema = raw_expr.get_param_expr(0);
  if (OB_JSON_SCHEMA_EXPR_ARG_NUM == rt_expr.arg_cnt_
     && OB_NOT_NULL(schema) && (schema->is_const_expr() || schema->is_static_scalar_const_expr())
     && schema->get_expr_type() != T_OP_GET_USER_VAR) {
    ObIAllocator &alloc = *op_cg_ctx.allocator_;
    ObExprJsonSchemaValidInfo *info
                  = OB_NEWx(ObExprJsonSchemaValidInfo, (&alloc), alloc, T_FUN_SYS_JSON_SCHEMA_VALIDATION_REPORT);
    bool got_data = false;
    if (OB_ISNULL(info)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(info->init_json_schema_extra_info(alloc, op_cg_ctx, schema, got_data))) {
    } else if (got_data) {
      rt_expr.extra_info_ = info;
    }
  }
  
  if (OB_SUCC(ret)) {
    rt_expr.eval_func_ = eval_json_schema_validation_report;
  }
  return ret;
}

int ObExprJsonSchemaValidationReport::eval_json_schema_validation_report(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  INIT_SUCC(ret);
  const ObExprJsonSchemaValidInfo *info
                  = static_cast<ObExprJsonSchemaValidInfo *>(expr.extra_info_);
  ObJsonSchemaCache* j_schema = nullptr;
  ObIJsonBase* j_doc = nullptr;
  bool is_null_result = false;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr("JSONModule"));

  if (OB_ISNULL(info)) {
    // schema is not const
    if (OB_FAIL(ObJsonExprHelper::get_json_schema(expr, ctx, temp_allocator, 0,
                                                  j_schema, is_null_result))) {
    }
  } else {
    j_schema = ObJsonExprHelper::get_schema_cache_ctx(expr.expr_ctx_id_, &ctx.exec_ctx_);
    if (OB_ISNULL(j_schema)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(j_schema->prepare(info->json_schema_, 1, ObJsonInType::JSON_BIN, true))) {
    }
  }

  if (OB_FAIL(ret)) {
  } else if (!is_null_result && OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, temp_allocator, 1,
                                             j_doc, is_null_result, true))) {
  } else if (is_null_result) {
    res.set_null();
  } else {
    ObString binary;
    if (OB_FAIL(j_doc->get_raw_binary(binary, &temp_allocator))) {
    } else {
      bool valid = false;
      ObString report;
      if (OB_FAIL(j_schema->validate(binary, 1, true, valid, report, temp_allocator))) {
      } else {
        ret = ObJsonExprHelper::pack_json_str_res(expr, ctx, res, report);
      }
    }
  }
  return ret;
}

} /* sql */
} /* oceanbase */
