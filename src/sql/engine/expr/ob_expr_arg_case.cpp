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
#include "sql/engine/expr/ob_expr_arg_case.h"
#include "sql/engine/expr/ob_expr_equal.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprArgCase::ObExprArgCase(ObIAllocator &alloc)
    : ObExprOperator(alloc, T_OP_ARG_CASE,
                     N_ARG_CASE, MORE_THAN_ONE, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION,
                     INTERNAL_IN_MYSQL_MODE, INTERNAL_IN_ORACLE_MODE), need_cast_(true)
{
  disable_operand_auto_cast();
}

ObExprArgCase::~ObExprArgCase()
{
}

int ObExprArgCase::deserialize(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  need_cast_ = true;
  ret = ObExprOperator::deserialize(buf, data_len, pos);
  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(input_types_.count() < 2 || input_types_.count() % 2 != 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error. invalid input type count", K(input_types_.count()));
    } else {
      int64_t loop = input_types_.count() / 2;
      bool all_are_numeric = ob_is_accurate_numeric_type(input_types_.at(0).get_calc_type());
      bool all_same_type = true;
      ObObjMeta case_meta = input_types_.at(0).get_calc_meta();
      for (int64_t i = 1; OB_SUCC(ret) && i < loop; ++i) {
        ObObjType type = input_types_.at(i).get_calc_type();
        ObObjMeta param_meta = input_types_.at(i).get_calc_meta();
        if (all_same_type && !ObSQLUtils::is_same_type_for_compare(case_meta, param_meta)) {
          all_same_type = false;
        }
        if (all_are_numeric && (!ob_is_accurate_numeric_type(type))) {
          all_are_numeric = false;
        }
      }//end for
      if (OB_SUCC(ret)) {
        need_cast_ = !(all_same_type || all_are_numeric);
      }
    }
  }
  return ret;
}

int ObExprArgCase::assign(const ObExprOperator &other)
{
  int ret = OB_SUCCESS;
  const ObExprArgCase *tmp_other = dynamic_cast<const ObExprArgCase *>(&other);
  if (OB_UNLIKELY(NULL == tmp_other)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument. wrong type for other", K(ret), K(other));
  } else if (OB_LIKELY(this != tmp_other)) {
    if (OB_FAIL(ObExprOperator::assign(other))) {
      LOG_WARN("copy in Base class ObExprOperator failed", K(ret));
    } else {
      this->need_cast_ = tmp_other->need_cast_;
    }
  }
  return ret;
}


/*
 * TODO: Here the compare type is not calculated, which may lead to results incompatible with MySQL
 *       The current approach is dynamic comparison in calc_result, is it consistent with MySQL behavior?
 */
int ObExprArgCase::calc_result_typeN(ObExprResType &type,
                                     ObExprResType *types_stack,
                                     int64_t param_num,
                                     ObExprTypeCtx &type_ctx) const
{
  // mysql> drop table t1; create table t1(a date, b int);
  // mysql> insert into t1 values (20160622, 20160622);
  // mysql> select a, b, case '160622' when b then 'equal to b' when a then 'equal to a' end from t1;
  // +------------+----------+---------------------------------------------------------------------+
  // | a          | b        | case '160622' when b then 'equal to b' when a then 'equal to a' end |
  // +------------+----------+---------------------------------------------------------------------+
  // | 2016-06-22 | 20160622 | equal to a                                                          |
  // +------------+----------+---------------------------------------------------------------------+
  // cmp type of '160622' VS b(int 20160622) is NOT date, not equal.
  // cmp type of '160622' VS a(date 2016-06-22) is date (or maybe datetime), equal.
  // so we need a cmp type array, not a single cmp type.


  // case c1
  //  when 10 then expr1
  //  when 11 then expr2
  //  else expr3
  // param_count = 3, param[i] is the type of expri
  // if [else expr3] not provided, param[2] is NULL and param_count is 3
  int ret = OB_SUCCESS;
  if (OB_ISNULL(types_stack)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("null types", K(ret));
  } else if (OB_UNLIKELY(param_num < 3 || param_num % 2 != 0)) { // an implicit 'else expr' element is added by caller
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("param_num is not correct", K(param_num));
  } else { // param_num >= 4 and param_num is even

    /*  take "case c1 when x1 then y1 when x2 then y2 else y3" as an example
        After studying the behavior of mysql,
        we use c1, x1, x2 to deduce calc collation which will be used in comparison
        we use y1, y2, y3 to deduce the result type and collation
    */
    int64_t cond_type_count = param_num / 2;
    int64_t val_type_count = param_num / 2;
    ObExprResType tmp_res_type;
    if (OB_FAIL(aggregate_result_type_for_case(
                  tmp_res_type,
                  types_stack,
                  cond_type_count,
                  false,
                  type_ctx,
                  FALSE, FALSE,
                  is_called_in_sql_))) {
      LOG_WARN("failed to get result type for cmp", K(ret));
    } else if (OB_FAIL(aggregate_result_type_for_case(
                         type,
                         types_stack + cond_type_count,
                         val_type_count,
                         false,
                         type_ctx,
                         true, false,
                         is_called_in_sql_))) {
      LOG_WARN("failed to get result type", K(ret));
    } else {
      // cmp type will be computed dynamically
      type.set_calc_collation_type(tmp_res_type.get_collation_type());
      type.set_calc_collation_level(tmp_res_type.get_collation_level());
      ObExprOperator::calc_result_flagN(type, types_stack + cond_type_count, val_type_count);
      for (int64_t i = 1; OB_SUCC(ret) && i < cond_type_count; ++i) {
        ObObjType calc_type;
        if (ob_is_enumset_tc(types_stack[i].get_type())) {
          if (OB_FAIL(ObExprResultTypeUtil::get_relational_cmp_type(calc_type,
                                                                    types_stack[0].get_type(),
                                                                    types_stack[i].get_type()))) {
            LOG_WARN("failed to get_cmp_type", K(types_stack[0]), K(types_stack[i]), K(ret));
          } else {
            types_stack[i].set_calc_type(calc_type);
          }
        }
      }
    }
  }
  return ret;
}

int ObExprArgCase::calc_with_cast(ObObj &result,
                        const ObObj *objs_stack,
                        int64_t param_num,
                        ObCompareCtx &cmp_ctx,
                        ObCastCtx &cast_ctx,
                        const ObExprResType &res_type,
                        const ob_get_cmp_type_func get_cmp_type_func)
{
  int ret = OB_SUCCESS;
  bool match_when = false;
  ObObj tmp_result;
  ObObj equal_result;
  if (OB_ISNULL(objs_stack) || OB_ISNULL(get_cmp_type_func)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("stack or get_cmp_type_func is null", K(objs_stack), K(get_cmp_type_func));
  } else if (OB_UNLIKELY(param_num < 2) ) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid param_num", K(param_num));
  } else {
    // case c1
    //  when 10 then expr1
    //  when 11 then expr2
    //  else expr3
    // param_count = 2+2N
    //
    // case c1 when 10 then expr1
    //         when 11 then expr2
    // param_count = 1+2N

    const bool has_else = (param_num % 2 == 0);
    int64_t loop = (has_else) ? param_num - 2 : param_num;
    for (int64_t i = 1; OB_SUCC(ret) && i < loop; i += 2) {
      if (OB_FAIL(get_cmp_type_func(cmp_ctx.cmp_type_,
                                    objs_stack[0].get_type(),
                                    objs_stack[i].get_type(),
                                    res_type.get_calc_type()))) {
        LOG_WARN("Get cmp type failed", K(ret));
      } else {
        cast_ctx.dest_collation_ = cmp_ctx.cmp_cs_type_;
        ret = ObExprEqual::calc_cast(equal_result, objs_stack[0], objs_stack[i], cmp_ctx, cast_ctx);
        if (OB_SUCC(ret)) {
          if (ObNullType == equal_result.get_type()) {
          } else if (OB_UNLIKELY(ObInt32Type != equal_result.get_type())) { // ObExprEqual::calc returns int32
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected calc result type. must be tiny", K(equal_result));
          } else if (equal_result.is_true()) {
            match_when = true;
            tmp_result = objs_stack[i + 1];
            break;
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (!match_when) {
        if (param_num % 2 == 0) {
          LOG_DEBUG("match else wrong", K(param_num));
          tmp_result = objs_stack[param_num - 1]; // match else (default value)
        } else {
          tmp_result.set_null();
        }
      }
      cast_ctx.dest_collation_ = res_type.get_collation_type();
      ret = ObObjCaster::to_type(res_type.get_type(), res_type.get_collation_type(), cast_ctx, tmp_result, result);
    }
  }
  return ret;
}

int ObExprArgCase::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &) const
{
  int ret = OB_ERR_UNEXPECTED;
  LOG_WARN("this expr should be rewrote in new engine", K(ret));
  return ret;
}

DEF_SET_LOCAL_SESSION_VARS(ObExprArgCase, raw_expr) {
  int ret = OB_SUCCESS;
  SET_LOCAL_SYSVAR_CAPACITY(1);
  EXPR_ADD_LOCAL_SYSVAR(share::SYS_VAR_COLLATION_CONNECTION);
  return ret;
}

}
}
