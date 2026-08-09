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
#include "sql/engine/expr/ob_expr_case.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_array_expr_utils.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

typedef int (*CheckIsMatchFunc)(const ObDatum *when_datum, bool &match_when);

ObExprCase::ObExprCase(ObIAllocator &alloc)
    : ObExprOperator(alloc, T_OP_CASE, N_CASE, MORE_THAN_ONE, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
  disable_operand_auto_cast();
  param_lazy_eval_ = true;
}

ObExprCase::~ObExprCase()
{
}

/*
 * NOTE: calc_result_typeNinparam_num only covers the then/else expressions, not the when expression
 */
int ObExprCase::calc_result_typeN(ObExprResType &type,
                                  ObExprResType *types_stack,
                                  int64_t param_num,
                                  ObExprTypeCtx &type_ctx) const
{
  // case
  //  when 10 then expr1
  //  when 11 then expr2
  //  [else expr3]
  int ret = OB_SUCCESS;
  if (OB_ISNULL(types_stack)) {
    LOG_WARN("null types");
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(param_num < 3 || param_num % 2 == 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("param num is not correct", K(param_num));
  } else { //param_num >=3 and param_num is odd

    /* in order to be compatible with mysql
     * both in ob_expr_case.cpp and ob_expr_arg_case.cpp
     * types_stack includes the condition exprs.
     * In expr_case, there is no arg param expr compared with expr_arg_case
     */
    const int64_t cond_type_count = param_num / 2;
    const int64_t val_type_count = param_num - cond_type_count;

    if (OB_FAIL(aggregate_result_type_for_case(
                  type,
                  types_stack + cond_type_count,
                  val_type_count,
                  type_ctx,
                  true, false,
                  is_called_in_sql_))) {
    } else {
      ObExprOperator::calc_result_flagN(type, types_stack + cond_type_count, val_type_count);
    }

    if (OB_SUCC(ret)) {
      for (int64_t i = 0; i < cond_type_count; ++i) {
        const ObObjType cond_type = types_stack[i].get_type();
        const ObObjTypeClass cond_tc = ob_obj_type_class(cond_type);
        if (ObIntTC == cond_tc || ObUIntTC == cond_tc
            || ObNumberTC == cond_tc || ObDecimalIntTC == cond_tc
            || ObNullTC == cond_tc) {
          types_stack[i].set_calc_type(cond_type);
          types_stack[i].set_calc_collation(types_stack[i]);
        } else {
          types_stack[i].set_calc_type(ObDoubleType);
          types_stack[i].set_calc_collation_type(CS_TYPE_BINARY);
          types_stack[i].set_calc_collation_level(CS_LEVEL_NUMERIC);
        }
      }

      bool is_expr_integer_type = (ob_is_int_tc(type.get_type()) ||
                                   ob_is_uint_tc(type.get_type()));
      for (int64_t i = cond_type_count; OB_SUCC(ret) && i < param_num; ++i) {
        bool is_arg_integer_type = (ob_is_int_tc(types_stack[i].get_type()) ||
                                    ob_is_uint_tc(types_stack[i].get_type()));
        if ((is_arg_integer_type && is_expr_integer_type) ||
            ObNullType == types_stack[i].get_type()) {
          // see ObExprCoalesce::calc_result_typeN
          types_stack[i].set_calc_meta(types_stack[i].get_obj_meta());
        } else {
          types_stack[i].set_calc_meta(type.get_obj_meta());
          if (ObNumberType == type.get_obj_meta().get_type()
              || ObDecimalIntType == type.get_obj_meta().get_type()
              || ob_is_double_type(type.get_obj_meta().get_type())) {
            types_stack[i].set_calc_accuracy(type.get_accuracy());
          }
        }
      }
    }
  }
  return ret;
}

int ObExprCase::cg_expr(ObExprCGCtx &op_cg_ctx,
                        const ObRawExpr &raw_expr,
                        ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(op_cg_ctx);

  const ObCaseOpRawExpr &case_expr = dynamic_cast<const ObCaseOpRawExpr&>(raw_expr);
  // New engine case expression when expr must return int/null, i.e., when expr is always a boolean semantic expression
  for (int64_t i = 0; OB_SUCC(ret) && i < case_expr.get_when_expr_size(); ++i) {
    const ObRawExpr *when_expr = case_expr.get_when_param_expr(i);
    const ObObjType &when_expr_res_type = when_expr->get_result_type().get_type();
    if (OB_UNLIKELY(ObNullType != when_expr_res_type &&
                    !ob_is_integer_type(when_expr_res_type))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("when expr must return integer", K(ret), K(when_expr_res_type));
    }
  }

  if (OB_SUCC(ret)) {
    rt_expr.eval_func_ = calc_case_expr;
    rt_expr.eval_batch_func_ = eval_case_batch;
  }
  return ret;
}

static int check_is_match(const ObDatum &when_datum, bool &match_when)
{
  int ret = OB_SUCCESS;
  if (when_datum.is_null()) {
    match_when = false;
  } else {
    int64_t v = when_datum.get_int();
    match_when = (v != 0) ? true : false;
  }
  return ret;
}

int ObExprCase::calc_case_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  const bool has_else = (expr.arg_cnt_ % 2 != 0);
  int64_t loop = (has_else) ? expr.arg_cnt_ - 1 : expr.arg_cnt_;
  bool match_when = false;
  ObDatum *when_datum = NULL;
  ObDatum *then_datum = NULL;
  bool has_result = false;
  int64_t expr_idx = 0;
  for ( ; OB_SUCC(ret) && !match_when && expr_idx < loop; expr_idx += 2) {
    if (OB_FAIL(expr.args_[expr_idx]->eval(ctx, when_datum))) {
    } else if (OB_FAIL(check_is_match(*when_datum, match_when))) {
    } else if (match_when) {
      if (OB_FAIL(expr.args_[expr_idx+1]->eval(ctx, then_datum))) {
      } else {
        has_result = true;
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (!match_when) {
      if (has_else) {
        if (OB_FAIL(expr.args_[expr.arg_cnt_-1]->eval(ctx, then_datum))) {
        } else {
          has_result = true;
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (!has_result) {
      res_datum.set_null();
    } else {
      if (OB_ISNULL(then_datum)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("then_datum is NULL", K(ret));
      } else {
        res_datum.set_datum(*then_datum);
      }
    }
  }
  return ret;
}
// During type deduction, the when/then expression types need to stay consistent.

int ObExprCase::eval_case_batch(const ObExpr &expr,
                                ObEvalCtx &ctx,
                                const ObBitVector &skip,
                                const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  const bool has_else = (expr.arg_cnt_ % 2 != 0);
  int64_t loop = (has_else) ? expr.arg_cnt_ - 1 : expr.arg_cnt_;
  bool match_when = false;
  ObDatum *results = expr.locate_batch_datums(ctx);
  if (OB_ISNULL(results)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("results frame is not init", K(ret));
  } else if (batch_size <= 0) {
    // do nothing
  } else {
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
    ObBitVector *case_when_match = nullptr;
    ObBitVector *case_not_match = nullptr;
    void * data = nullptr;
    void * data1 = nullptr;
    ObEvalCtx::TempAllocGuard alloc_guard(ctx);
    if (OB_ISNULL(data = alloc_guard.get_allocator().alloc(ObBitVector::memory_size(batch_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory for case_when_match", K(ret), K(batch_size));
    } else if (OB_ISNULL(data1 = alloc_guard.get_allocator().alloc(ObBitVector::memory_size(batch_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory for case_when_match", K(ret), K(batch_size));
    } else {
      case_when_match = to_bit_vector(data);
      case_not_match = to_bit_vector(data1);
      case_when_match->reset(batch_size);
      case_not_match->reset(batch_size);
      //case_when_match = eval_flags | skip
      case_when_match->bit_calculate(skip, eval_flags, batch_size,
                               [](const uint64_t l, const uint64_t r) { return (l | r); });
      case_not_match->bit_calculate(skip, eval_flags, batch_size,
                               [](const uint64_t l, const uint64_t r) { return (l | r); });
    }
    // E.G
    // SELECT CASE WHEN expr1 THEN expr2 WHEN expr3 THEN expr4 ... ELSE exprN END
    // the logic is
    // 1. calc when branch, save result in when_datums and use match_when flag
    //    to mark which rows are matched in when branch and these rows should be
    //    calculated in then branch 
    // 2. calc then branch, put matching result(then_datums) into output datums
    //    (results)
    // REPEAT 1. and 2.
    // ...
    // LAST.
    //    calc else branch and put matching result(then_datums) into output datums
    for (int64_t expr_idx = 0; OB_SUCC(ret) && expr_idx < loop; expr_idx += 2) {
      if (OB_FAIL(expr.args_[expr_idx]->eval_batch(ctx, *case_when_match, batch_size))) {
      } else {
        ObDatumVector when_datums = expr.args_[expr_idx]->locate_expr_datumvector(ctx);
        //first eval when datums
        for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
          if (case_when_match->at(j)) {
            continue;
          }
          if (OB_FAIL(check_is_match(*when_datums.at(j), match_when))) {
          } else if (match_when) {
            case_when_match->set(j);
          } else {
            // not match, mark case_not_match to stop calculating then branch
            case_not_match->set(j);
          }
        }
        //now eval then datums
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(expr.args_[expr_idx + 1]->eval_batch(ctx, *case_not_match, batch_size))) {
        } else {
          ObDatumVector then_datums = expr.args_[expr_idx + 1]->locate_expr_datumvector(ctx);
          for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
            if (case_not_match->at(j)) {
              continue;
            }
            results[j].set_datum(*then_datums.at(j));
            eval_flags.set(j);
          }

          // rows matched in this round should not match in next round, therefor,
          // copy last round matched rows flag(case_when_match) into case_not_match
          case_not_match->deep_copy(*case_when_match, batch_size);
        }
      }
    }
    //now set the result of the rest, skip rows already matched (case_when_match)
    if (OB_SUCC(ret)) {
      if (has_else) {
        if (OB_FAIL(expr.args_[expr.arg_cnt_ - 1]->eval_batch(ctx, *case_when_match, batch_size))) {
        } else {
          ObDatumVector else_datums = expr.args_[expr.arg_cnt_ - 1]->locate_expr_datumvector(ctx);
          for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
            if (case_when_match->at(j)) {
              continue;
            }
            results[j].set_datum(*else_datums.at(j));
            eval_flags.set(j);
          }
        }
      } else {
        for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
          if (case_when_match->at(j)) {
            continue;
          }
          results[j].set_null();
          eval_flags.set(j);
        }
      }
    }
  }
  return ret;
}

DEF_SET_LOCAL_SESSION_VARS(ObExprCase, raw_expr) {
  int ret = OB_SUCCESS;
  SET_LOCAL_SYSVAR_CAPACITY(1);
  EXPR_ADD_LOCAL_SYSVAR(share::SYS_VAR_COLLATION_CONNECTION);
  return ret;
}

}
}
