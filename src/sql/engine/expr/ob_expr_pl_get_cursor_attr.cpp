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

#include "sql/engine/expr/ob_expr_pl_get_cursor_attr.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
using namespace pl;

namespace sql
{
OB_SERIALIZE_MEMBER(ObExprPLGetCursorAttr::ExtraInfo,
                    pl_cursor_info_.type_,
                    pl_cursor_info_.is_explicit_);

int ObExprPLGetCursorAttr::ExtraInfo::init_pl_cursor_info(ObIAllocator *allocator,
                                        const ObExprOperatorType type,
                                        const pl::ObPLGetCursorAttrInfo &cursor_info,
                                        ObExpr &rt_expr)
{
  int ret = OB_SUCCESS;
  ExtraInfo *extra_info = NULL;
  void *buf = NULL;
  if (OB_ISNULL(allocator)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("allocator is null", K(ret), K(allocator));
  } else if (OB_ISNULL(buf = allocator->alloc(sizeof(ExtraInfo)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else {
    extra_info = new(buf) ExtraInfo(*allocator, type);
    extra_info->pl_cursor_info_ = cursor_info;
    if (OB_SUCC(ret)) {
      rt_expr.extra_info_ = extra_info;
    }
  }
  return ret;
}

int ObExprPLGetCursorAttr::ExtraInfo::deep_copy(common::ObIAllocator &allocator,
                                        const ObExprOperatorType type,
                                        ObIExprExtraInfo *&copied_info) const
{
  int ret = OB_SUCCESS;
  ExtraInfo *copied_cursor_info = NULL;
  if (OB_FAIL(ObExprExtraInfoFactory::alloc(allocator, type, copied_info))) {
  } else if (OB_ISNULL(copied_cursor_info = static_cast<ExtraInfo *>(copied_info))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", K(ret));
  } else {
    copied_cursor_info->pl_cursor_info_ = pl_cursor_info_;
  }
  return ret;
}

OB_SERIALIZE_MEMBER((ObExprPLGetCursorAttr, ObFuncExprOperator));

ObExprPLGetCursorAttr::ObExprPLGetCursorAttr(ObIAllocator &alloc)
  : ObFuncExprOperator(
      alloc, T_FUN_PL_GET_CURSOR_ATTR, N_PL_GET_CURSOR_ATTR, ZERO_OR_ONE, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION, false),
    pl_cursor_info_() {}

ObExprPLGetCursorAttr::~ObExprPLGetCursorAttr() {}

int ObExprPLGetCursorAttr::assign(const ObExprOperator &other)
{
  int ret = OB_SUCCESS;
  const ObExprPLGetCursorAttr *tmp = static_cast<const ObExprPLGetCursorAttr *>(&other);
  if (OB_UNLIKELY(OB_ISNULL(tmp))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument. wrong type for other", K(other), K(ret));
  } else if (OB_LIKELY(this != tmp)) {
    if (OB_FAIL(ObExprOperator::assign(other))) {
    } else {
      this->pl_cursor_info_ = tmp->pl_cursor_info_;
    }
  }
  return ret;
}

int ObExprPLGetCursorAttr::calc_result_typeN(ObExprResType &type,
                                             ObExprResType *types,
                                             int64_t param_num,
                                             common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  UNUSED(types);
  if (param_num > 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid number of arguments", K(param_num), K(ret));
  } else if (!pl_cursor_info_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pl cursor info is invalid", K(ret));
  } else {
    if (pl_cursor_info_.is_isopen()
        || pl_cursor_info_.is_found()
        || pl_cursor_info_.is_notfound()) {
      type.set_tinyint();
      type.set_precision(DEFAULT_PRECISION_FOR_BOOL);
      type.set_scale(DEFAULT_SCALE_FOR_INTEGER);
    } else {
      type.set_int();
      type.set_precision(DEFAULT_PRECISION_FOR_BOOL);
      type.set_scale(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].scale_);
    }
    if (1 == param_num) {
      if (pl_cursor_info_.is_explicit_cursor()) {
        types[0].set_calc_type(ObExtendType);
      } else {
        types[0].set_calc_type(ObIntType);
      }
    }
  }
  return ret;
}

int ObExprPLGetCursorAttr::cg_expr(
    ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  const ObPLGetCursorAttrRawExpr &pl_expr = static_cast<const ObPLGetCursorAttrRawExpr&>(raw_expr);
  if (OB_FAIL(ExtraInfo::init_pl_cursor_info(
      op_cg_ctx.allocator_, type_, pl_expr.get_pl_get_cursor_attr_info(), rt_expr))) {
  } else {
    rt_expr.eval_func_ = &calc_pl_get_cursor_attr;
  }
  return ret;
}

int ObExprPLGetCursorAttr::calc_pl_get_cursor_attr(
    const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *datum = NULL;
  ObDatumMeta datum_meta;
  ObObj obj;
  ObObjMeta obj_meta;
  obj_meta.set_ext();
  ObSQLSessionInfo *session = ctx.exec_ctx_.get_my_session();
  ExtraInfo *info = static_cast<ExtraInfo *>(expr.extra_info_);
  CK(OB_NOT_NULL(info));
  CK(OB_NOT_NULL(session));
  CK(expr.arg_cnt_ <= 1);
  const pl::ObPLCursorInfo* cursor = NULL;
  const pl::ObPLCursorInfo implicit_cursor(false/*not explicit cursor*/);
  if (OB_FAIL(ret)) {
  } else if (!info->pl_cursor_info_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pl cursor info is invalid", K(ret), K(info->pl_cursor_info_));
  } else if (info->pl_cursor_info_.is_explicit_cursor()) {
    if (1 != expr.arg_cnt_) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), K(info->pl_cursor_info_), K(expr.arg_cnt_));
    }
  } else {
    if (1 == expr.arg_cnt_) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), K(info->pl_cursor_info_), K(expr.arg_cnt_));
    }
  }
  if (OB_SUCC(ret) && 1 == expr.arg_cnt_) {
    datum_meta = expr.args_[0]->datum_meta_;
    if (OB_FAIL(expr.args_[0]->eval(ctx, datum))) {
    } else if (info->pl_cursor_info_.is_explicit_cursor()) {
      if (datum_meta.type_ != ObExtendType) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid argument: expected extend type", K(ret));
      }
    } else {
      if (datum->is_null()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("bulk index count can not null", K(ret));
      } else {
        if (datum_meta.type_ != ObIntType) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid argument: expected int type", K(ret));
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (info->pl_cursor_info_.is_explicit_cursor()) {
      CK (ObExtendType == datum_meta.type_);
      OX (datum->to_obj(obj, obj_meta));
      if (OB_SUCC(ret)) {
        if (obj.is_null()) {
          // do nothing, null cursor is legal...
        } else if (!obj.is_ext()
                    || obj.get_meta().get_extend_type() != pl::PL_CURSOR_TYPE) {
          ret = OB_ERR_CURSOR_ATTR_APPLY;
          LOG_WARN("cursor attribute may not applied to non-cursor", K(ret), K(obj.get_meta()));
        }
      }
      OX (cursor = reinterpret_cast<const pl::ObPLCursorInfo*>(obj.get_ext()));
    } else {
      if (OB_ISNULL(cursor = session->get_pl_implicit_cursor())) {
        cursor = &implicit_cursor;
      }
    }
  }
  // get attr
  if (OB_SUCC(ret)) {
    int64_t type = info->pl_cursor_info_.get_type();
    switch (type)
    {
      case pl::ObPLGetCursorAttrInfo::PL_CURSOR_ISOPEN: {
        expr_datum.set_bool(OB_ISNULL(cursor) ? false : cursor->get_isopen());
        break;
      }
      case pl::ObPLGetCursorAttrInfo::PL_CURSOR_FOUND: {
        if (OB_ISNULL(cursor)) {
          ret = OB_ERR_INVALID_CURSOR;
          LOG_WARN("cursor is null", K(ret));
        } else {
          bool found = false, isnull = false;
          if (OB_FAIL(cursor->get_found(found, isnull))) {
          } else if (isnull) {
            expr_datum.set_null();
          } else {
            expr_datum.set_bool(found);
          }
        }
        break;
      }
      case pl::ObPLGetCursorAttrInfo::PL_CURSOR_NOTFOUND: {
        if (OB_ISNULL(cursor)) {
          ret = OB_ERR_INVALID_CURSOR;
          LOG_WARN("cursor is null", K(ret));
        } else {
          bool notfound = false, isnull = false;
          if (OB_FAIL(cursor->get_notfound(notfound, isnull))) {
          } else if (isnull) {
            expr_datum.set_null();
          } else {
            expr_datum.set_bool(notfound);
          }
        }
        break;
      }
      case pl::ObPLGetCursorAttrInfo::PL_CURSOR_ROWCOUNT: {
        if (OB_ISNULL(cursor)) {
          ret = OB_ERR_INVALID_CURSOR;
          LOG_WARN("cursor is null", K(ret));
        } else {
          int64_t rowcount = 0;
          bool isnull = false;
          if (OB_FAIL(cursor->get_rowcount(rowcount, isnull))) {
          } else if (isnull) {
            expr_datum.set_null();
          } else {
            expr_datum.set_int(rowcount);
          }
        }
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("wrong pl get cursor attribute info type", K(ret), K(type));
      }
    }
  }
  return ret;
}

}
}
