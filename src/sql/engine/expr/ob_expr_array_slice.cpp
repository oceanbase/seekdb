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
#include "sql/engine/expr/ob_expr_array_slice.h"
#include "common/udt/ob_array_type.h"
#include "common/udt/ob_collection_type.h"
#include "share/object/ob_array_cast.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/ob_exec_context.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::omt;

namespace oceanbase
{
namespace sql
{

ObExprArraySlice::ObExprArraySlice(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, 
          T_FUNC_SYS_ARRAY_SLICE, 
          N_ARRAY_SLICE, 
          TWO_OR_THREE,
          VALID_FOR_GENERATED_COL, 
          NOT_ROW_DIMENSION)
{
}

ObExprArraySlice::~ObExprArraySlice() {}

int ObExprArraySlice::calc_result_typeN(ObExprResType &type, 
                          ObExprResType *types,
                          int64_t param_num, 
                          ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = NULL;
  ObExecContext *exec_ctx = NULL;
  ObExprResType *arr_type = &types[0];
  ObExprResType *offset_type = &types[1];
  bool is_null = false;
  uint16_t subschema_id = arr_type->get_subschema_id();
  ObCollectionTypeBase *coll_type = NULL;

  if (OB_ISNULL(session = const_cast<ObSQLSessionInfo *>(type_ctx.get_session()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ObSQLSessionInfo is null", K(ret));
  } else if (OB_ISNULL(exec_ctx = session->get_cur_exec_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ObExecContext is null", K(ret));
  } else if (ob_is_null(arr_type->get_type())) {
    is_null = true;
  } else if (!ob_is_collection_sql_type(arr_type->get_type())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_USER_ERROR(OB_ERR_INVALID_TYPE_FOR_OP, "ARRAY", ob_obj_type_str(arr_type->get_type()));
  } else if (OB_FAIL(ObArrayExprUtils::get_coll_type_by_subschema_id(exec_ctx, arr_type->get_subschema_id(), coll_type))) {
  } else if (coll_type->type_id_ != ObNestedType::OB_ARRAY_TYPE && coll_type->type_id_ != ObNestedType::OB_VECTOR_TYPE) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("invalid collection type", K(ret), K(coll_type->type_id_));
  } else if (ob_is_null(offset_type->get_type())) {
    is_null = true;
  } else if (param_num == 3) {
    ObExprResType *len_type = &types[2];
    if (ob_is_null(len_type->get_type())) {
      is_null = true;
    } else {
      len_type->set_calc_type(ObIntType);
    }
  }

  if (OB_FAIL(ret)) {
  } else if (is_null) {
    type.set_null();
  } else {
    offset_type->set_calc_type(ObIntType);
    type_ctx.set_cast_mode(type_ctx.get_cast_mode() | CM_STRING_INTEGER_TRUNC);
    type.set_collection(subschema_id);
    type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObCollectionSQLType]).get_length());
  }

  return ret;
}

int ObExprArraySlice::eval_array_slice(const ObExpr &expr, 
                          ObEvalCtx &ctx, 
                          ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  ObArenaAllocator &tmp_allocator = tmp_alloc_g.get_allocator();
  uint16_t subschema_id = expr.obj_meta_.get_subschema_id();
  ObDatum *arr_datum = NULL;
  ObDatum *offset_datum = NULL;
  ObDatum *len_datum = NULL;
  ObIArrayType *src_arr = NULL;
  ObIArrayType *res_arr = NULL;

  if (OB_FAIL(expr.args_[0]->eval(ctx, arr_datum))) {
  } else if (OB_FAIL(expr.args_[1]->eval(ctx, offset_datum))) {
  } else if (expr.arg_cnt_ > 2 && OB_FAIL(expr.args_[2]->eval(ctx, len_datum))) {
    LOG_WARN("eval len failed", K(ret));
  } else if (arr_datum->is_null() || offset_datum->is_null() ||
             (expr.arg_cnt_ > 2 && len_datum->is_null())) {
    res.set_null();
  } else if (OB_FAIL(ObArrayExprUtils::get_array_obj(tmp_allocator, 
                                          ctx, 
                                          subschema_id,
                                          arr_datum->get_string(), 
                                          src_arr))) {
  } else if (OB_FAIL(ObArrayExprUtils::construct_array_obj(tmp_allocator,
                                          ctx, 
                                          subschema_id, 
                                          res_arr, 
                                          false))) {
  } else {
    uint32_t arr_len = src_arr->size();
    int64_t offset = offset_datum->get_int();
    int64_t len = 0;
    bool has_len = false;
    if (expr.arg_cnt_ > 2) {
      has_len = true;
      len = len_datum->get_int();
    }
    if (OB_FAIL(get_subarray(res_arr, src_arr, offset, len, has_len))) {
    } else {
      ObString res_str;
      if (OB_FAIL(ObArrayExprUtils::set_array_res(res_arr, 
                                        res_arr->get_raw_binary_len(), 
                                        expr, 
                                        ctx,
                                        res_str))) {
      } else {
        res.set_string(res_str);
      }
    }
  }

  return ret;
}

int ObExprArraySlice::eval_array_slice_batch(const ObExpr &expr, 
                          ObEvalCtx &ctx,
                          const ObBitVector &skip, 
                          const int64_t batch_size)
{
  int ret = OB_SUCCESS;

  ObDatumVector res_datum = expr.locate_expr_datumvector(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  ObArenaAllocator &tmp_allocator = tmp_alloc_g.get_allocator();
  const uint16_t subschema_id = expr.obj_meta_.get_subschema_id();
  ObIArrayType *src_arr = NULL;
  ObIArrayType *res_arr = NULL;

  if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
  } else if (OB_FAIL(expr.args_[1]->eval_batch(ctx, skip, batch_size))) {
  } else if (expr.arg_cnt_ > 2 && OB_FAIL(expr.args_[2]->eval_batch(ctx, skip, batch_size))) {
    LOG_WARN("eval len failed", K(ret));
  } else {
    ObDatumVector arr_array = expr.args_[0]->locate_expr_datumvector(ctx);
    ObDatumVector offset_array = expr.args_[1]->locate_expr_datumvector(ctx);
    ObDatumVector len_array =
        expr.arg_cnt_ > 2 ? expr.args_[2]->locate_expr_datumvector(ctx) : ObDatumVector();
    for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
      if (skip.at(j) || eval_flags.at(j)) {
        continue;
      }
      eval_flags.set(j);
      if (arr_array.at(j)->is_null() || offset_array.at(j)->is_null() ||
          (expr.arg_cnt_ > 2 && len_array.at(j)->is_null())) {
        res_datum.at(j)->set_null();
      } else if (OB_FAIL(ObArrayExprUtils::get_array_obj(tmp_allocator, 
                                              ctx, 
                                              subschema_id,
                                              arr_array.at(j)->get_string(),
                                              src_arr))) {
      } else if (OB_FAIL(ObArrayExprUtils::construct_array_obj(tmp_allocator,
                                              ctx, 
                                              subschema_id, 
                                              res_arr, 
                                              false))) {
      } else {
        uint32_t arr_len = src_arr->size();
        int64_t offset = offset_array.at(j)->get_int();
        int64_t len = 0;
        bool has_len = false;
        if (expr.arg_cnt_ > 2) {
          has_len = true;
          len = len_array.at(j)->get_int();
        }
        if (OB_FAIL(get_subarray(res_arr, src_arr, offset, len, has_len))) {
        } else {
          int32_t res_size = res_arr->get_raw_binary_len();
          char *res_buf = nullptr;
          int64_t res_buf_len = 0;
          ObTextStringDatumResult output_result(expr.datum_meta_.type_, &expr, &ctx, res_datum.at(j));
          if (OB_FAIL(output_result.init_with_batch_idx(res_size, j))) {
          } else if (OB_FAIL(output_result.get_reserved_buffer(res_buf, res_buf_len))) {
          } else if (res_buf_len < res_size) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get invalid res buf len", K(ret), K(res_buf_len), K(res_size));
          } else if (OB_FAIL(res_arr->get_raw_binary(res_buf, res_buf_len))) {
          } else if (OB_FAIL(output_result.lseek(res_size, 0))) {
          } else {
            output_result.set_result();
          }
        }
      }
    } // end for
  }

  return ret;
}

int ObExprArraySlice::get_subarray(ObIArrayType *&res_arr, 
                          ObIArrayType *src_arr, 
                          int64_t offset,
                          int64_t len, 
                          bool has_len)
{
  int ret = OB_SUCCESS;

  int64_t arr_len = src_arr->size();
  int64_t left = offset > 0 ? offset : max(1, arr_len + offset + 1);
  int64_t right = 0;
  if (offset == 0 || offset > arr_len || (has_len && len < 0 && arr_len + len <= 0)) {
    // do nothing
  } else {
    if (has_len) {
      if (len < 0) {
        right = arr_len + len + 1;
      } else {
        if (offset >= 0) {
          if (len > INT64_MAX - offset) {
            right = arr_len + 1;
          } else {
            right = offset + len;
          }
        } else {
          right = left + max(0, offset + arr_len >= 0 ? len : arr_len + offset + len);
        }
        right = right > arr_len + 1 ? arr_len + 1 : right;
      }
    } else {
      right = arr_len + 1;
    }
  }
  if (left > right) {
    left = 1, right = 1;
  }
  if (OB_FAIL(res_arr->insert_from(*src_arr, left - 1, right - left))) {
  }

  return ret;
}

int ObExprArraySlice::cg_expr(ObExprCGCtx &expr_cg_ctx, 
                          const ObRawExpr &raw_expr,
                          ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_array_slice;
  rt_expr.eval_batch_func_ = eval_array_slice_batch;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
