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
#include "sql/engine/expr/ob_expr_array.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include "sql/engine/ob_exec_context.h"


using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::omt;

namespace oceanbase
{
namespace sql
{
ObExprArray::ObExprArray(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_ARRAY, N_ARRAY, PARAM_NUM_UNKNOWN, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprArray::ObExprArray(ObIAllocator &alloc,
                         ObExprOperatorType type,
                         const char *name,
                         int32_t param_num, 
                         int32_t dimension) : ObFuncExprOperator(alloc, type, name, param_num, VALID_FOR_GENERATED_COL, dimension) 
{
}

ObExprArray::~ObExprArray()
{
}

int ObExprArray::calc_result_typeN(ObExprResType& type,
                                   ObExprResType* types_stack,
                                   int64_t param_num,
                                   ObExprTypeCtx& type_ctx) const
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = const_cast<ObSQLSessionInfo *>(type_ctx.get_session());
  ObExecContext *exec_ctx = OB_ISNULL(session) ? NULL : session->get_cur_exec_ctx();
  ObDataType elem_type;
  uint16_t subschema_id;
  if (OB_ISNULL(exec_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("exec ctx is null", K(ret));
  } else if (param_num > MAX_ARRAY_ELEMENT_SIZE) {
    ret = OB_SIZE_OVERFLOW;
    OB_LOG(WARN, "array element size exceed max", K(ret), K(param_num), K(MAX_ARRAY_ELEMENT_SIZE));
  } else if (OB_FAIL(ObArrayExprUtils::deduce_array_element_type(exec_ctx, types_stack, param_num, elem_type))) {
  } else if (ob_is_collection_sql_type(elem_type.get_obj_type())) {
    for (int64_t i = 0; i < param_num; ++i) {
      if (types_stack[i].get_subschema_id() != elem_type.meta_.get_subschema_id()) {
        types_stack[i].set_calc_meta(elem_type.meta_);
      }
    }
    if (OB_FAIL(ObArrayExprUtils::deduce_nested_array_subschema_id(exec_ctx, elem_type, subschema_id))) {
    }
  } else if (OB_FAIL(exec_ctx->get_subschema_id_by_collection_elem_type(ObNestedType::OB_ARRAY_TYPE,
                                                                        elem_type, subschema_id))) {
  }
  if (OB_SUCC(ret)) {
    type.set_collection(subschema_id);
  }  
  return ret;
}

int ObExprArray::eval_array(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  common::ObArenaAllocator &tmp_allocator = tmp_alloc_g.get_allocator();
  ObSubSchemaValue value;
  uint16_t subschema_id = expr.obj_meta_.get_subschema_id();
  const ObSqlCollectionInfo *coll_info = NULL;
  if (OB_FAIL(ctx.exec_ctx_.get_sqludt_meta_by_subschema_id(subschema_id, value))) {
  } else if (value.type_ >= OB_SUBSCHEMA_MAX_TYPE) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid subschema type", K(ret), K(value));
  } else {
    ObIArrayType *arr_obj = NULL;
    coll_info = reinterpret_cast<const ObSqlCollectionInfo *>(value.value_);
    ObCollectionArrayType *arr_type = static_cast<ObCollectionArrayType *>(coll_info->collection_meta_);
    if (OB_ISNULL(coll_info)) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("collect info is null", K(ret), K(subschema_id));
    } else if (OB_FAIL(ObArrayTypeObjFactory::construct(tmp_allocator, *arr_type, arr_obj))) {
    } else {
      int num_args = expr.arg_cnt_;
      ObCollectionArrayType *arr_type = static_cast<ObCollectionArrayType *>(coll_info->collection_meta_);
      for (int i = 0; i < num_args && OB_SUCC(ret); i++) {
        ObDatum *datum = NULL;
        if (OB_FAIL(expr.args_[i]->eval(ctx, datum))) {
        } else {
          if (arr_type->element_type_->type_id_ == ObNestedType::OB_BASIC_TYPE) {
            ObCollectionBasicType *elem_type = static_cast<ObCollectionBasicType *>(arr_type->element_type_);
            if (OB_FAIL(ObArrayUtil::append(*arr_obj, elem_type->basic_meta_.get_obj_type(), datum))) {
            }
          } else if (arr_type->element_type_->type_id_ == ObNestedType::OB_ARRAY_TYPE ||
                     arr_type->element_type_->type_id_ == ObNestedType::OB_VECTOR_TYPE) {
            ObString raw_bin;
            uint16_t elem_subid;
            ObArrayNested *nest_array = static_cast<ObArrayNested *>(arr_obj);
            if (datum->is_null()) {
              if (OB_FAIL(nest_array->push_null())) {
              }
            } else if (FALSE_IT(raw_bin = datum->get_string())) {
            } else if (FALSE_IT(elem_subid = expr.args_[i]->obj_meta_.get_subschema_id())) {
            } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(tmp_allocator,
                           *datum,
                           expr.args_[i]->datum_meta_,
                           expr.args_[i]->obj_meta_.has_lob_header(),
                           raw_bin))) {
            } else if (OB_FAIL(add_elem_to_nested_array(tmp_allocator, ctx, elem_subid, raw_bin, nest_array))) {
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        ObString res_str;
        if (OB_FAIL(ObArrayExprUtils::set_array_res(arr_obj, arr_obj->get_raw_binary_len(), expr, ctx, res_str))) {
        } else {
          res.set_string(res_str);
        }
      }
    }
  }
  return ret;
}

int ObExprArray::add_elem_to_nested_array(ObIAllocator &tmp_allocator, ObEvalCtx &ctx, uint16_t subschema_id,
                                          ObString &raw_bin, ObArrayNested *nest_array)
{
  int ret = OB_SUCCESS;
  ObSubSchemaValue value;
  if (OB_FAIL(ctx.exec_ctx_.get_sqludt_meta_by_subschema_id(subschema_id, value))) {
  } else if (value.type_ >= OB_SUBSCHEMA_MAX_TYPE) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid subschema type", K(ret), K(value));
  } else {
    ObIArrayType *arr_obj = NULL;
    const ObSqlCollectionInfo *coll_info = reinterpret_cast<const ObSqlCollectionInfo *>(value.value_);
    ObCollectionArrayType *arr_type = static_cast<ObCollectionArrayType *>(coll_info->collection_meta_);
    if (OB_ISNULL(coll_info)) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("collect info is null", K(ret), K(subschema_id));
    } else if (OB_FAIL(ObArrayTypeObjFactory::construct(tmp_allocator, *arr_type, arr_obj))) {
    } else if (OB_FAIL(arr_obj->init(raw_bin))) {
    } else if (OB_FAIL(nest_array->push_back(*arr_obj))) {
    } 
  }
  return ret;
}

int ObExprArray::cg_expr(ObExprCGCtx &expr_cg_ctx,
                         const ObRawExpr &raw_expr,
                         ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_array;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
