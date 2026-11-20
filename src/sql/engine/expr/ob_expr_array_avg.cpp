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
#include "sql/engine/expr/ob_expr_array_avg.h"
#include "common/udt/ob_array_type.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_result_type_util.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::omt;

namespace oceanbase
{
namespace sql
{
namespace
{
template <typename T>
int calc_wide_integer_average(const uint32_t len, const uint8_t *null_bitmaps,
                             const char *data, const uint32_t data_len, double &average)
{
  int ret = OB_SUCCESS;
  if (data_len / sizeof(T) != len) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected integer array length", K(ret), K(len), K(data_len));
  } else {
    // A uint32 length of 64-bit elements needs at most 96 signed sum bits.
    // Sum exactly before converting: converting each element to double loses
    // cancellation such as [INT64_MIN, INT64_MAX], whose average is -0.5.
    __int128 sum = 0;
    for (uint32_t i = 0; i < len; ++i) {
      if (null_bitmaps == nullptr || null_bitmaps[i] == 0) {
        T value;
        MEMCPY(&value, data + static_cast<uint64_t>(i) * sizeof(T), sizeof(T));
        sum += static_cast<__int128>(value);
      }
    }
    // Preserve the existing denominator and empty-array behavior.
    average = static_cast<double>(sum) / len;
  }
  return ret;
}
}

ObExprArrayAvg::ObExprArrayAvg(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUNC_SYS_ARRAY_AVG, N_ARRAY_AVG, 1, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprArrayAvg::~ObExprArrayAvg()
{
}

int ObExprArrayAvg::calc_result_type1(ObExprResType &type,
                                      ObExprResType &type1,
                                      ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = NULL;
  ObExecContext *exec_ctx = NULL;
  ObDataType src_elem_type;
  ObCollectionTypeBase *coll_type = NULL;
  uint32_t depth = 0;
  bool is_vec = false;

  if (OB_ISNULL(session = const_cast<ObSQLSessionInfo *>(type_ctx.get_session()))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(exec_ctx = session->get_cur_exec_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (ob_is_null(type1.get_type())) {
    // do nothing
  } else if (!ob_is_collection_sql_type(type1.get_type())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_USER_ERROR(OB_ERR_INVALID_TYPE_FOR_OP, "ARRAY", ob_obj_type_str(type1.get_type()));
  } else if (OB_FAIL(ObArrayExprUtils::get_coll_type_by_subschema_id(exec_ctx, type1.get_subschema_id(), coll_type))) {
  } else if (coll_type->type_id_ != ObNestedType::OB_ARRAY_TYPE && coll_type->type_id_ != ObNestedType::OB_VECTOR_TYPE) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
  } else if (OB_FAIL(ObArrayExprUtils::get_array_element_type(exec_ctx, type1.get_subschema_id(), src_elem_type, depth, is_vec))) {
  } else if (depth != 1) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "array_avg with multi-dimension array");
  } else if (!ob_is_numeric_type(src_elem_type.get_obj_type())) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "array_avg with non-numeric type");
  }
  if (OB_SUCC(ret)) {
    type.set_double();
    type.set_accuracy(common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObDoubleType]);
  }
  return ret;
}

int ObExprArrayAvg::eval_array_avg(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  common::ObArenaAllocator &tmp_allocator = tmp_alloc_g.get_allocator();
  const uint16_t subschema_id = expr.args_[0]->obj_meta_.get_subschema_id();
  ObDatum *arr_datum = NULL;
  ObCollectionArrayType *arr_type = NULL;
  ObIArrayType *src_arr = NULL;
  double res_average = 0;
  uint32_t depth = 0;

  if (OB_FAIL(expr.args_[0]->eval(ctx, arr_datum))) {
  } else if (arr_datum->is_null()) {
    res.set_null();
  } else if (OB_FAIL(ObArrayExprUtils::get_array_type_by_subschema_id(ctx, subschema_id, arr_type))) {
  } else {
    ObString data_str = arr_datum->get_string();
    uint32_t len = 0, data_len = 0;
    uint8_t *null_bitmaps = nullptr;
    const char *data = nullptr;

    if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, &tmp_allocator,
                                        ObLongTextType,
                                        CS_TYPE_BINARY,
                                        true,
                                        data_str))) {
    } else if (OB_FAIL(ObArrayExprUtils::get_array_data(data_str,
                                            arr_type,
                                            len,
                                            null_bitmaps,
                                            data,
                                            data_len))) {
    } else if (ob_is_integer_type(arr_type->get_basic_meta(depth).get_obj_type())) {
      depth = 0;
      const ObObjType element_type = arr_type->get_basic_meta(depth).get_obj_type();
      if (element_type == ObIntType || element_type == ObUInt64Type) {
        double average = 0;
        ret = element_type == ObIntType
            ? calc_wide_integer_average<int64_t>(len, null_bitmaps, data, data_len, average)
            : calc_wide_integer_average<uint64_t>(len, null_bitmaps, data, data_len, average);
        if (OB_SUCC(ret)) {
          res.set_double(average);
        }
      } else if (ob_is_unsigned_type(element_type)) {
        uint64_t res_sum = 0;
        if (OB_FAIL(ObArrayExprUtils::calc_array_sum(len, null_bitmaps, data, data_len, arr_type, res_sum))) {
        } else {
          res.set_double(static_cast<double>(res_sum) / len);
        }
      } else {
        int64_t res_sum = 0;
        if (OB_FAIL(ObArrayExprUtils::calc_array_sum(len, null_bitmaps, data, data_len, arr_type, res_sum))) {
        } else {
          res.set_double(static_cast<double>(res_sum) / len);
        }
      }
    } else {
      double res_sum = 0;
      if (OB_FAIL(ObArrayExprUtils::calc_array_sum(len, null_bitmaps, data, data_len, arr_type, res_sum))) {
      } else {
        res.set_double(res_sum / len);
      }
    }
  }

  return ret;
}

int ObExprArrayAvg::eval_array_avg_batch(const ObExpr &expr, ObEvalCtx &ctx,
                                         const ObBitVector &skip, const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  ObDatumVector res_datum = expr.locate_expr_datumvector(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  common::ObArenaAllocator &tmp_allocator = tmp_alloc_g.get_allocator();
  const uint16_t subschema_id = expr.args_[0]->obj_meta_.get_subschema_id();
  ObCollectionArrayType *arr_type = NULL;
  ObIArrayType *src_arr = NULL;
  uint32_t depth = 0;

  if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
  } else {
    ObDatumVector arr_array = expr.args_[0]->locate_expr_datumvector(ctx);
    for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
      bool is_null_res = false;
      if (skip.at(j) || eval_flags.at(j)) {
        continue;
      }
      eval_flags.set(j);
      if (arr_array.at(j)->is_null()) {
        res_datum.at(j)->set_null();
      } else if (OB_FAIL(ObArrayExprUtils::get_array_type_by_subschema_id(ctx, subschema_id, arr_type))) {
      } else {
        ObString data_str = arr_array.at(j)->get_string();
        uint32_t len = 0, data_len = 0;
        uint8_t *null_bitmaps = nullptr;
        const char *data = nullptr;

        if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, &tmp_allocator,
                                            ObLongTextType,
                                            CS_TYPE_BINARY,
                                            true,
                                            data_str))) {
        } else if (OB_FAIL(ObArrayExprUtils::get_array_data(data_str,
                                                arr_type,
                                                len,
                                                null_bitmaps,
                                                data,
                                                data_len))) {
        } else if (ob_is_integer_type(arr_type->get_basic_meta(depth).get_obj_type())) {
          depth = 0;
          const ObObjType element_type = arr_type->get_basic_meta(depth).get_obj_type();
          if (element_type == ObIntType || element_type == ObUInt64Type) {
            double average = 0;
            ret = element_type == ObIntType
                ? calc_wide_integer_average<int64_t>(len, null_bitmaps, data, data_len, average)
                : calc_wide_integer_average<uint64_t>(len, null_bitmaps, data, data_len, average);
            if (OB_SUCC(ret)) {
              res_datum.at(j)->set_double(average);
            }
          } else if (ob_is_unsigned_type(element_type)) {
            uint64_t res_sum = 0;
            if (OB_FAIL(ObArrayExprUtils::calc_array_sum(len, null_bitmaps, data, data_len, arr_type, res_sum))) {
            } else {
              res_datum.at(j)->set_double(static_cast<double>(res_sum) / len);
            }
          } else {
            int64_t res_sum = 0;
            if (OB_FAIL(ObArrayExprUtils::calc_array_sum(len, null_bitmaps, data, data_len, arr_type, res_sum))) {
            } else {
              res_datum.at(j)->set_double(static_cast<double>(res_sum) / len);
            }
          }
        } else {
          double res_sum = 0;
          if (OB_FAIL(ObArrayExprUtils::calc_array_sum(len,
                                            null_bitmaps,
                                            data,
                                            data_len,
                                            arr_type,
                                            res_sum))) {
          } else {
            res_datum.at(j)->set_double(res_sum / len);
          }
        }
      }
    } // end for
  }
  return ret;
}

int ObExprArrayAvg::cg_expr(ObExprCGCtx &expr_cg_ctx,
                            const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_array_avg;
  rt_expr.eval_batch_func_ = eval_array_avg_batch;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
