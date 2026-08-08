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

#define USING_LOG_PREFIX COMMON

#include "sql/engine/expr/ob_expr_vec_ivf_pq_center_vector.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_calc_partition_id.h"
#include "query/vector/ob_vector_index_util.h"
#include "data_plane/vector/ob_vector_common_util.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
ObExprVecIVFPQCenterVector::ObExprVecIVFPQCenterVector(ObIAllocator &allocator)
  : ObFuncExprOperator(allocator, T_FUN_SYS_VEC_IVF_PQ_CENTER_VECTOR, N_VEC_IVF_PQ_CENTER_VECTOR, MORE_THAN_ZERO, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
  need_charset_convert_ = false;
}

int ObExprVecIVFPQCenterVector::calc_result_typeN(ObExprResType &type,
                                       ObExprResType *types,
                                       int64_t param_num,
                                       ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = const_cast<ObSQLSessionInfo *>(type_ctx.get_session());
  ObExecContext *exec_ctx = OB_ISNULL(session) ? NULL : session->get_cur_exec_ctx();
  ObDataType elem_type;
  elem_type.meta_.set_float();
  uint16_t subschema_id;
  if (OB_ISNULL(exec_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("exec ctx is null", K(ret));
  } else if (OB_FAIL(exec_ctx->get_subschema_id_by_collection_elem_type(ObNestedType::OB_VECTOR_TYPE,
                                                                        elem_type, subschema_id))) {
  } else {
    type.set_collection(subschema_id);
  } 
  return ret;
}

int ObExprVecIVFPQCenterVector::calc_resultN(ObObj &result,
                                  const ObObj *objs_array,
                                  int64_t param_num,
                                  ObExprCtx &expr_ctx) const
{
  // TODO by query ivf index
  return OB_NOT_SUPPORTED;
}

int ObExprVecIVFPQCenterVector::cg_expr(
    ObExprCGCtx &expr_cg_ctx,
    const ObRawExpr &raw_expr,
    ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(raw_expr);
  UNUSED(expr_cg_ctx);
  if (OB_UNLIKELY(rt_expr.arg_cnt_ != 1) && OB_UNLIKELY(rt_expr.arg_cnt_ != 4)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(rt_expr.arg_cnt_), KP(rt_expr.args_), K(rt_expr.type_));
  } else if (OB_ISNULL(rt_expr.args_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments, null args", K(rt_expr.arg_cnt_), KP(rt_expr.args_), K(rt_expr.type_));
  } else {
    rt_expr.eval_func_ = generate_pq_center_vector;
  }
  return ret;
}

int ObExprVecIVFPQCenterVector::generate_pq_center_vector(
    const ObExpr &expr,
    ObEvalCtx &eval_ctx,
    ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *datum = nullptr;
  if (1 == expr.arg_cnt_) {
    expr_datum.set_null();
  } else if (4 == expr.arg_cnt_) {
    // for pq centroid table, return residual vector
    common::ObArenaAllocator tmp_allocator("IVFPQExprPQCVec", OB_MALLOC_NORMAL_BLOCK_SIZE);
    ObTableID table_id;
    ObTabletID tablet_id;
    share::ObVectorIndexDistAlgorithm dis_algo = share::VIDA_MAX;
    ObSEArray<float*, 64> centers;
    bool contain_null = false;
    ObIArrayType *arr = NULL;
    uint64_t center_prefix = 0;
    if (OB_FAIL(share::ObVectorIndexUtil::eval_ivf_centers_common(
        tmp_allocator, expr, eval_ctx, centers, table_id, tablet_id, dis_algo, contain_null, arr, center_prefix))) {
    } else if (contain_null) {
      // do nothing
      expr_datum.set_null();
    } else {
      share::ObVectorNormalizeInfo norm_info;
      float *residual_vec = nullptr;
      int64_t center_idx = 0;
      if (centers.count() == 0) {
        residual_vec = reinterpret_cast<float*>(arr->get_data());
      } else if (OB_FAIL(share::ObVectorIndexUtil::calc_residual_vector(
          tmp_allocator,
          arr->size(),
          centers,
          reinterpret_cast<float*>(arr->get_data()),
          share::VIDA_COS != dis_algo ? nullptr: &norm_info, // cos need norm
          residual_vec))) {
      }
      if (OB_FAIL(ret)) {
      } else {
        ObString data_str(arr->size() * sizeof(float), reinterpret_cast<char*>(residual_vec));
        ObString res_str;
        if (OB_FAIL(ObArrayExprUtils::set_array_res(nullptr,
                                          data_str.length(),
                                          expr,
                                          eval_ctx,
                                          res_str,
                                          data_str.ptr()))) {
        } else {
          expr_datum.set_string(res_str);
        }
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected arg cnt", K(ret), K(expr.arg_cnt_));
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
