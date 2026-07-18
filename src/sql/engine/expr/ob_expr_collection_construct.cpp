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

#include "ob_expr_collection_construct.h"
#include "pl/ob_pl_package.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

OB_SERIALIZE_MEMBER(
    (ObExprCollectionConstruct, ObFuncExprOperator),
    type_, not_null_, elem_type_, capacity_, udt_id_);

OB_SERIALIZE_MEMBER(ObExprCollectionConstruct::ExtraInfo,
                    type_, not_null_, elem_type_, capacity_, udt_id_);

int ObExprCollectionConstruct::ExtraInfo::deep_copy(common::ObIAllocator &allocator,
                                                    const ObExprOperatorType type,
                                                    ObIExprExtraInfo *&copied_info) const
{
  int ret = OB_SUCCESS;
  OZ(ObExprExtraInfoFactory::alloc(allocator, type, copied_info));
  ExtraInfo &other = *static_cast<ExtraInfo *>(copied_info);
  if (OB_SUCC(ret)) {
    other = *this;
  }
  return ret;
}

ObExprCollectionConstruct::ObExprCollectionConstruct(common::ObIAllocator &alloc)
    : ObFuncExprOperator(
        alloc, T_FUN_PL_COLLECTION_CONSTRUCT, N_PL_COLLECTION_CONSTRUCT,
        PARAM_NUM_UNKNOWN, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION, false, INTERNAL_IN_ORACLE_MODE),
      type_(pl::ObPLType::PL_INVALID_TYPE),
      not_null_(false),
      elem_type_(),
      capacity_(OB_INVALID_SIZE),
      udt_id_(OB_INVALID_ID) {}

ObExprCollectionConstruct::~ObExprCollectionConstruct() {}

int ObExprCollectionConstruct::calc_result_typeN(ObExprResType &type,
                                                 ObExprResType *types,
                                                 int64_t param_num,
                                                 ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObExprCollectionConstruct::cg_expr(ObExprCGCtx &op_cg_ctx,
                                       const ObRawExpr &raw_expr,
                                       ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;

  ObIAllocator &alloc = *op_cg_ctx.allocator_;
  if (OB_SUCC(ret)) {
    ExtraInfo *info = OB_NEWx(ExtraInfo, (&alloc), alloc, T_FUN_PL_COLLECTION_CONSTRUCT);
    if (NULL == info) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory failed", K(ret));
    } else {
      const ObCollectionConstructRawExpr &pl_expr
          = static_cast<const ObCollectionConstructRawExpr&>(raw_expr);
      info->type_ = pl_expr.get_type();
      info->not_null_ = pl_expr.is_not_null();
      info->capacity_ = pl_expr.get_capacity();
      info->udt_id_ = pl_expr.get_udt_id();
      if (pl_expr.get_elem_type().is_obj_type()) {
        CK(OB_NOT_NULL(pl_expr.get_elem_type().get_data_type()));
        OX(info->elem_type_ = *pl_expr.get_elem_type().get_data_type());
      } else {
        info->elem_type_.set_obj_type(ObExtendType);
      }

      if (OB_SUCC(ret)) {
        rt_expr.extra_info_ = info;
        rt_expr.eval_func_ = &eval_collection_construct;
      }
    }
  }
  return ret;
}



int ObExprCollectionConstruct::eval_collection_construct(const ObExpr &expr,
                                                         ObEvalCtx &ctx,
                                                         ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not support", K(ret));
  return ret;
}

} /* sql */
} /* oceanbase */
