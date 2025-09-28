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
 */

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_coll_pred.h"
#include "sql/engine/expr/ob_expr_multiset.h"
#include "src/pl/ob_pl.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
OB_SERIALIZE_MEMBER((ObExprCollPred, ObExprOperator),
                    ms_type_,
                    ms_modifier_);

typedef hash::ObHashMap<ObObj, int64_t, common::hash::NoPthreadDefendMode> LocalNTSHashMap;

static const ObString MEMBER_OF_EXPR_NAME = ObString("MEMBER OF");

ObExprCollPred::ObExprCollPred(ObIAllocator &alloc)
  : ObExprOperator(alloc, T_OP_COLL_PRED, N_COLL_PRED, 2, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION),
    ms_type_(ObMultiSetType::MULTISET_TYPE_INVALID),
    ms_modifier_(ObMultiSetModifier::MULTISET_MODIFIER_INVALID)
{
}

ObExprCollPred::~ObExprCollPred()
{
}

void ObExprCollPred::reset()
{
  ms_type_ = ObMultiSetType::MULTISET_TYPE_INVALID;
  ms_modifier_ = ObMultiSetModifier::MULTISET_MODIFIER_INVALID;
  ObExprOperator::reset();
}

int ObExprCollPred::assign(const ObExprOperator &other)
{
  int ret = OB_SUCCESS;
  if (other.get_type() != T_OP_COLL_PRED) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expr operator is mismatch", K(other.get_type()));
  } else if (OB_FAIL(ObExprOperator::assign(other))) {
    LOG_WARN("assign parent expr failed", K(ret));
  } else {
    const ObExprCollPred &other_expr = static_cast<const ObExprCollPred &>(other);
    ms_type_ = other_expr.ms_type_;
    ms_modifier_ = other_expr.ms_modifier_;
  }
  return ret;
}

int ObExprCollPred::calc_result_type2(ObExprResType &type,
                                    ObExprResType &type1,
                                    ObExprResType &type2,
                                    ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("multiset operator not support non udt type", K(type1), K(type2), K(ret));
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "multiset operator non udt type");
  return ret;
}

#define FILL_HASH_MAP(map_name, coll, map_cnt) \
do { \
  int64_t count = coll->get_actual_count(); \
  if (OB_FAIL(map_name.create(std::max(count, 1L), ObModIds::OB_SQL_HASH_SET))) { \
    LOG_WARN("fail create hash map", K(count), K(ret)); \
  } else if (0 == count) { \
    /* do nothing */ \
  } else { \
    const ObObj *elem = NULL; \
    bool is_del = false; \
    ObObj *data_arr = static_cast<ObObj*>(coll->get_data()); \
    for (int64_t i = 0; OB_SUCC(ret) && i < coll->get_count(); ++i) {\
      if (OB_FAIL(coll->is_elem_deleted(i, is_del))) { \
        LOG_WARN("failed to check is collection elem deleted", K(ret)); \
      } else if (!is_del) { \
        elem = data_arr + i;\
        ret = map_name.set_refactored(*elem, i);\
        if (OB_HASH_EXIST == ret) {\
          ret = OB_SUCCESS;\
          continue;\
        } else if (OB_SUCC(ret)) {\
          map_cnt++;\
        } else {\
          LOG_WARN("insert elem into hashmap failed.", K(ret));\
        }\
      } \
    }\
  }\
} while(0)

int ObExprCollPred::compare_obj(const ObObj &obj1, const ObObj &obj2, ObCompareCtx &cmp_ctx) {
  UNUSED(cmp_ctx);
  return obj1 == obj2 ? 0 : 1;
}



OB_DEF_SERIALIZE(ObExprCollPredInfo)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              ms_type_,
              ms_modifier_,
              tz_offset_,
              result_type_);
  return ret;
}

OB_DEF_DESERIALIZE(ObExprCollPredInfo)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
              ms_type_,
              ms_modifier_,
              tz_offset_,
              result_type_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObExprCollPredInfo)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              ms_type_,
              ms_modifier_,
              tz_offset_,
              result_type_);
  return len;
}

int ObExprCollPredInfo::deep_copy(common::ObIAllocator &allocator,
                         const ObExprOperatorType type,
                         ObIExprExtraInfo *&copied_info) const
{
  int ret = common::OB_SUCCESS;
  OZ(ObExprExtraInfoFactory::alloc(allocator, type, copied_info));
  ObExprCollPredInfo &other = *static_cast<ObExprCollPredInfo *>(copied_info);
  other.ms_type_ = ms_type_;
  other.ms_modifier_ = ms_modifier_;
  other.tz_offset_ = tz_offset_;
  other.result_type_ = result_type_;
  return ret;
}

template <typename RE>
int ObExprCollPredInfo::from_raw_expr(RE &raw_expr)
{
  int ret = OB_SUCCESS;
  ObCollPredRawExpr &set_expr 
       = const_cast<ObCollPredRawExpr &> (static_cast<const ObCollPredRawExpr&>(raw_expr));
  ms_type_ = set_expr.get_multiset_type();
  ms_modifier_ = set_expr.get_multiset_modifier();
  return ret;
}

int ObExprCollPred::cg_expr(ObExprCGCtx &expr_cg_ctx,
                            const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  if (2 != rt_expr.arg_cnt_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("wrong param num for coll pred", K(ret), K(rt_expr.arg_cnt_));
  } else if (OB_ISNULL(expr_cg_ctx.session_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info is nullptr", K(ret)); 
  } else {
    ObIAllocator &alloc = *expr_cg_ctx.allocator_;
    const ObCollPredRawExpr &fun_sys = static_cast<const ObCollPredRawExpr &>(raw_expr);
    ObExprCollPredInfo *info = OB_NEWx(ObExprCollPredInfo, (&alloc), alloc, T_OP_COLL_PRED);
    if (NULL == info) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory failed", K(ret));
    } else {
      OZ(info->from_raw_expr(fun_sys));
      const ObTimeZoneInfo *tz_info = get_timezone_info(expr_cg_ctx.session_);
      int64_t tz_offset = 0;
      if (OB_FAIL(get_tz_offset(tz_info, tz_offset))) {
        LOG_WARN("failed to get tz offset", K(ret));
      } else {
        info->tz_offset_ = tz_offset;
        info->result_type_ = result_type_;
        rt_expr.extra_info_ = info;
        rt_expr.eval_func_ = eval_coll_pred;
      }
    }
  }
  return ret;
}

int ObExprCollPred::eval_coll_pred(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *datum1 = nullptr;
  ObDatum *datum2 = nullptr;
  ObObj result;
  ObObj obj1;
  ObObj obj2;
  const ObExprCollPredInfo *info = static_cast<ObExprCollPredInfo *>(expr.extra_info_);
  if (OB_FAIL(expr.eval_param_value(ctx, datum1, datum2))) {
    LOG_WARN("failed to eval params", K(ret));
  }
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("not supported expr coll_pred", KR(ret));
  return ret;
}



} // namespace sql
}  // namespace oceanbase
