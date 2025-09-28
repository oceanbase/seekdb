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

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/dml/ob_group_by_checker.h"
#include "sql/rewrite/ob_transform_utils.h"
#include "sql/rewrite/ob_stmt_comparer.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

//oracle模式下, 某些分析函数的参数要求是partition by的表达式如ntile(c1) (partition by c2)需要报错
int ObGroupByChecker::check_analytic_function(const ParamStore *param_store,
                                              ObSelectStmt *ref_stmt,
                                              common::ObIArray<ObRawExpr *> &exp1_arr, //等价于查询项中表达式
                                              common::ObIArray<ObRawExpr *> &exp2_arr) //等价于group by项
{
  int ret = OB_SUCCESS;
  return ret;
}

// ref_stmt: current stmt
// group_by_exprs: refed exprs
// checked_exprs: need to check by refed exprs
// check checked_exprs should reference refed exprs
//  refed exprs  : c1,c2
//  checked_exprs: c3  --it's wrong
//  checked_exprs: c2,c2+1  --it's ok

int ObGroupByChecker::check_groupby_valid(ObRawExpr *expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expr should not be NULL", K(ret));
  } else {
    switch(expr->get_expr_type()) {
      case T_OP_CASE:
      {
        ObCaseOpRawExpr *case_when_expr = static_cast<ObCaseOpRawExpr*>(expr);
        for (int64_t i = 0; OB_SUCC(ret) && i < case_when_expr->get_when_expr_size(); i++) {
          if (OB_ISNULL(case_when_expr->get_when_param_expr(i))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("expr should not be NULL", K(ret));
          } else if (case_when_expr->get_when_param_expr(i)->has_flag(CNT_SUB_QUERY)) {
            if (case_when_expr->get_when_param_expr(i)->has_flag(IS_WITH_ANY)) {
              ObRawExpr *subquery = case_when_expr->get_when_param_expr(i)->get_param_expr(1);
              if (OB_ISNULL(subquery)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("expr should not be NULL", K(ret));
              } else {
                ObSelectStmt *stmt = static_cast<ObQueryRefRawExpr *>(subquery)->get_ref_stmt();
                if (OB_ISNULL(stmt)) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("expr should not be NULL", K(ret));
                } else if (!stmt->has_group_by()) {
                  /*do nothing*/
                } else {
                  ret = OB_ERR_INVALID_SUBQUERY_USE;
                  LOG_WARN("subquery expressions not allowed in case expresssion.", K(ret));
                }
              }
            } else {
              ret = OB_ERR_INVALID_SUBQUERY_USE;
              LOG_WARN("subquery expressions not allowed in case expresssion.", K(ret));
            }
          } else { /* do nothing. */ }
        }
        break;
      }
      default:
        break;
    }
  }
  return ret;
}

// check group by when stmt has group by or aggregate function or having that contains columns belongs to self stmt
// if select stmt has no group by and no aggregate function, but if has having clause
// then if having clause has columns belongs to current stmt, then need check group by
// eg:
//  select c1 +1 +2 from t1 having c1+1 >0;  // it need check, report error
//  select c1 from t1 having 1>0; // it don't need check, it will success
int ObGroupByChecker::check_group_by(const ParamStore *param_store,
                                     ObSelectStmt *ref_stmt,
                                     bool has_having_self_column/*default false*/,
                                     bool has_group_by_clause,
                                     bool only_need_constraints)
{
  int ret = OB_SUCCESS;
  // group by checker
  if (OB_ISNULL(ref_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stmt is null", K(ret));
  } else if (ref_stmt->has_group_by() || ref_stmt->has_rollup()) {
    ObSEArray<ObRawExpr*, 4> all_rollup_exprs;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(append(all_rollup_exprs, ref_stmt->get_rollup_exprs()))) {
      LOG_WARN("failed to expr", K(ret));
    }
    if (OB_SUCC(ret)) {
      ObGroupByChecker checker(param_store,
                               &ref_stmt->get_group_exprs(),
                               &all_rollup_exprs);
      checker.set_query_ctx(ref_stmt->get_query_ctx());
      checker.set_nested_aggr(ref_stmt->contain_nested_aggr());
      checker.only_need_contraints_ = only_need_constraints;
      if (OB_FAIL(checker.check_select_stmt(ref_stmt))) {
        LOG_WARN("failed to check group by", K(ret));
      } else if (!only_need_constraints) {
        for (int64_t i = 0; OB_SUCC(ret) && i < ref_stmt->get_group_expr_size(); i++) {
          if (OB_FAIL(check_groupby_valid(ref_stmt->get_group_exprs().at(i)))) {
            LOG_WARN("failed to check groupby valid.", K(ret));
          } else { /* do nothing. */ }
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < ref_stmt->get_rollup_expr_size(); i++) {
          if (OB_FAIL(check_groupby_valid(ref_stmt->get_rollup_exprs().at(i)))) {
            LOG_WARN("failed to check groupby valid.", K(ret));
          } else { /* do nothing. */ }
        }
      }
    }
  } else {
    /* no group by && no aggregate function && no having that exists column belongs to self stmt */
  }
  return ret;
}

int ObGroupByChecker::add_pc_const_param_info(ObExprEqualCheckContext &check_ctx)
{
  int ret = OB_SUCCESS;
  // 如果是oracle模式，group by表达式的常量会被参数化
  // select a + 1 from t group by a + 1 => select a + ? from t group by a + ?
  // 原先是为了抽取一种约束，两个问号对应的常量必须都是1，用于后续计划匹配
  // 但现在改为了抽取两个问号相等的约束
  // 注意：在这里没有办法拿到obj，只能拿到问号在param store中的index
  ObPCConstParamInfo const_param_info;
  if (OB_ISNULL(query_ctx_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (param_store_ != NULL) {
    for (int64_t i = 0; OB_SUCC(ret) && i < check_ctx.param_expr_.count(); i++) {
      ObExprEqualCheckContext::ParamExprPair &param_pair = check_ctx.param_expr_.at(i);
      if (OB_FAIL(const_param_info.const_idx_.push_back(param_pair.param_idx_))) {
        LOG_WARN("failed to push back element", K(ret));
      } else if (param_pair.param_idx_ < 0 || param_pair.param_idx_ >= param_store_->count()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("get invalid param idx", K(ret), K(param_pair.param_idx_), K(param_store_->count()));
      } else if (OB_FAIL(const_param_info.const_params_.push_back(param_store_->at(param_pair.param_idx_)))) {
        LOG_WARN("failed to psuh back param const value", K(ret));
      }
    }
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (const_param_info.const_idx_.count() > 0
               && OB_FAIL(query_ctx_->all_plan_const_param_constraints_.push_back(const_param_info))) {
      LOG_WARN("failed to push back element", K(ret));
    } else if (const_param_info.const_idx_.count() > 0
               && OB_FAIL(query_ctx_->all_possible_const_param_constraints_.push_back(const_param_info))) {
      LOG_WARN("failed to push back element", K(ret));
    } else {
      // do nothing
    }
  }
  return ret;
}

bool ObGroupByChecker::find_in_rollup(ObRawExpr &expr)
{
  bool found = false;
  bool found_same_structure = false;
  ObStmtCompareContext check_ctx;
  int ret = OB_SUCCESS;
  if (OB_ISNULL(query_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null pointer.", K(ret));
  } else if (nullptr != rollup_exprs_) {
    check_ctx.init(&query_ctx_->calculable_items_);
    int64_t rollup_cnt = rollup_exprs_->count();
    for (int64_t nth_rollup = 0; !found && nth_rollup < rollup_cnt; ++nth_rollup) {
      check_ctx.reset();
      check_ctx.override_const_compare_ = true;
      check_ctx.override_query_compare_ = true;
      if (expr.same_as(*rollup_exprs_->at(nth_rollup), &check_ctx)) {
        found = true;
        LOG_DEBUG("found in rollup exprs", K(expr));
      }
    }
    if (OB_SUCCESS == check_ctx.err_code_ && !found && is_top_select_stmt()) {
      for (int64_t nth_rollup = 0; !found_same_structure && nth_rollup < rollup_cnt; ++nth_rollup) {
        //in oracle mode, only non static const expr will be replaced later in replace_group_by_exprs
        if (is_mysql_mode() || !rollup_exprs_->at(nth_rollup)->is_static_const_expr()) {
          check_ctx.reset();
          check_ctx.ignore_param_ = true;
          check_ctx.override_const_compare_ = true;
          check_ctx.override_query_compare_ = true;
          if (expr.same_as(*rollup_exprs_->at(nth_rollup), &check_ctx)) {
            found_same_structure = true;
            LOG_DEBUG("found same structure in rollup exprs", K(expr));
          }
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if ((found || found_same_structure) && OB_SUCCESS == check_ctx.err_code_) {
    if (OB_FAIL(append(query_ctx_->all_equal_param_constraints_,
                       check_ctx.equal_param_info_))) {
      LOG_WARN("failed to append equal params constraints", K(ret));
    } else if (OB_FAIL(add_pc_const_param_info(check_ctx))) {
      LOG_WARN("failed to add pc const param info.", K(ret));
    } else { /*do nothing.*/ }
  }
  return found;
}

bool ObGroupByChecker::find_in_group_by(ObRawExpr &expr)
{
  int ret = OB_SUCCESS;
  bool found = false;
  ObStmtCompareContext check_ctx;
  if (OB_ISNULL(query_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null pointer.", K(ret));
  } else if (nullptr != group_by_exprs_) {
    check_ctx.init(&query_ctx_->calculable_items_);
    int64_t group_by_cnt = group_by_exprs_->count();
    for (int64_t nth_group_by = 0; !found && nth_group_by < group_by_cnt; ++nth_group_by) {
      check_ctx.reset();
      check_ctx.override_const_compare_ = true;
      check_ctx.override_query_compare_ = !is_check_order_by_;
      if (expr.same_as(*group_by_exprs_->at(nth_group_by), &check_ctx)) {
        found = true;
        LOG_DEBUG("found in group by exprs", K(expr));
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (found && OB_SUCCESS == check_ctx.err_code_) {
    // 这里抽取了两种约束：
    // 1. select a+1 from t1 group by a+1 --> select a+? from t1 group by a+?
    //    抽取一种等值约束，使得只要是两个问号值相同就可以匹配，比如：
    //    select a+3 from t1 group by a+3
    // 2. select a+1 from t1 group by a+1 order by a+1 -->
    //                            select a+? from t1 group by a+? order by a+1
    //    抽取一种检查参数=常量的约束，因为order by不能参数化(order by 1代表第一列)
    //    所以需要使得两个问号和order by 后的常量相同就可以匹配。
    if (OB_FAIL(append(query_ctx_->all_equal_param_constraints_,
                       check_ctx.equal_param_info_))) {
      LOG_WARN("failed to append equal params constraints", K(ret));
    } else if (OB_FAIL(add_pc_const_param_info(check_ctx))) {
      LOG_WARN("failed to add pc const param info.", K(ret));
    } else { /*do nothing.*/ }
  }
  return found;
}

// check whether exprs belongs to stmt checked group by
// select c1,(select d1 from t1 b) from t2 a group by c1;
// when check subquery (select d1 from t1 b), d1 not belongs to the stmt need checked
int ObGroupByChecker::belongs_to_check_stmt(ObRawExpr &expr, bool &belongs_to)
{
  int ret = OB_SUCCESS;
  belongs_to = false;
  if (cur_stmts_.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get stmt", K(ret));
  } else {
    // Stmt needed check
    // For ObPseudoColumnRawExpr only the expr belongs to top select stmt, it need check group by
    // eg: select prior c1 from t1 connect by prior c1=c2 group by prior c1; it's ok, because prior c1 exists in group by
    //   But
    //     select prior c1,(select prior a.c1 from t1 b connect by nocycle prior c1=c2) as c2 from t1 a connect by nocycle prior c1=c2 group by prior c1;
    //  it report error, because "prior a.c1" in subquery, it's not belongs to refered subquery, so a.c1 not exists in group by, report error
    // and for aggregate function, if aggregate function belongs to checked select stmt, the arguemnt of aggregate function don't need check group by
    // eg: select count(c1) from t1 group by c2;
    // But if aggregate function not belongs to checked select stmt, then the argument of aggregate function need check
    // eg: select count(c1), (select count(a.c1) from t2 b) from t1 a group by c2; --then "count(a.c1)" in subquery should report error
    const ObSelectStmt *top_stmt = cur_stmts_.at(0);
    UNUSED(top_stmt);
    if (is_top_select_stmt()) {
      // the expr is not from checked stmt
      belongs_to = true;
      LOG_DEBUG("same level", K(ret), K(expr));
    } else {
      LOG_DEBUG("different level", K(ret), K(expr));
    }
  }
  return ret;
}

// 判断列column是否属于当前的select stmt
// eg: select a.c1+1 from t1 a group by c1;  -- a.c1 belongs to "from a group by c1"
//  But
//     select (select a.c1 from t1 b where c1=10 group by b.c1) c1 from t1 a group by a.c1;
//     a.c1 belongs the select stmt that contains table a
//     so when check subquery that contains table b, don't check a.c1 whether exists in group by b.c1
int ObGroupByChecker::colref_belongs_to_check_stmt(ObColumnRefRawExpr &expr, bool &belongs_to)
{
  int ret = OB_SUCCESS;
  belongs_to = false;
  if (cur_stmts_.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get stmt", K(ret), K(cur_stmts_));
  } else {
    // the stmt needed check
    const ObSelectStmt *top_stmt = cur_stmts_.at(0);
    // For ObPseudoColumnRawExpr only the expr belongs to top select stmt, it need check group by
    // eg: select prior c1 from t1 connect by prior c1=c2 group by prior c1; it's ok, because prior c1 exists in group by
    //   But
    //     select prior c1,(select prior a.c1 from t1 b connect by nocycle prior c1=c2) as c2 from t1 a connect by nocycle prior c1=c2 group by prior c1;
    //  it report error, because "prior a.c1" in subquery, it's not belongs to refered subquery, so a.c1 not exists in group by, report error
    // and for aggregate function, if aggregate function belongs to checked select stmt, the arguemnt of aggregate function don't need check group by
    // eg: select count(c1) from t1 group by c2;
    // But if aggregate function not belongs to checked select stmt, then the argument of aggregate function need check
    // eg: select count(c1), (select count(a.c1) from t2 b) from t1 a group by c2; --then "count(a.c1)" in subquery should report error
    if (OB_ISNULL(top_stmt)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("top stmt is null", K(ret));
    } else if (NULL != top_stmt->get_column_expr_by_id(expr.get_table_id(), expr.get_column_id())) {
      // the expr is not from checked stmt
      belongs_to = true;
      LOG_DEBUG("same level", K(ret), K(expr));
    } else {
      LOG_DEBUG("different level", K(ret), K(expr));
    }
  }
  return ret;
}

int ObGroupByChecker::visit(ObConstRawExpr &expr)
{
  int ret = OB_SUCCESS;
  if (is_mysql_mode()) {
    if (find_in_group_by(expr) || find_in_rollup(expr)) {
      set_skip_expr(&expr);
    }
  }
  return ret;
}

int ObGroupByChecker::visit(ObExecParamRawExpr &expr)
{
  int ret = OB_SUCCESS;
  if (only_need_contraints_) {
    // do nothing
  } else if (OB_ISNULL(expr.get_ref_expr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ref expr is invalid", K(ret));
  } else if (OB_FAIL(expr.get_ref_expr()->preorder_accept(*this))) {
    LOG_WARN("failed to visit child", K(ret));
  }
  return ret;
}

int ObGroupByChecker::visit(ObVarRawExpr &expr)
{
  UNUSED(expr);
  return OB_SUCCESS;
}

int ObGroupByChecker::visit(ObOpPseudoColumnRawExpr &)
{
  return OB_SUCCESS;
}

int ObGroupByChecker::check_select_stmt(const ObSelectStmt *ref_stmt)
{
  int ret = OB_SUCCESS;
  ++level_;
  LOG_DEBUG("check group by start stmt", K(ret));

  if (is_top_select_stmt()) {
    top_stmt_ = ref_stmt;
  }

  if (OB_ISNULL(ref_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ref_stmt should not be NULL", K(ret));
  } else if (OB_FAIL(cur_stmts_.push_back(ref_stmt))) {
    LOG_WARN("failed to push back stmt", K(ret));
  } else {
    ObStmtExprGetter visitor;
    if (is_top_select_stmt() || only_need_contraints_) {
      // 当前select stmt,则仅仅check having, select item, order
      // eg:
      // select c1,c2,(select d2 from t2 where t1.c1=t2.d1) as c3 from t1 group by c1,c2;
      //  level_=0, the stmt is from "select c1,c2,(select d2 from t2 where t1.c1=t2.d1) as c3 from t1 group by c1,c2"
      //  level_=1, the stmt is from "(select d2 from t2 where t1.c1=t2.d1)"
      visitor.remove_all();
      visitor.add_scope(SCOPE_HAVING);
      visitor.add_scope(SCOPE_SELECT);
      visitor.add_scope(SCOPE_ORDERBY);
    } else {
      // 如果是subquery，则需要check所有expression
      // following is not allow, keep the old logic
      // select (select d2 from t2 where max(t1.c1)=t2.d1) as c3 from t1 group by c1,c2;
    }
    // special case: select count(*) from t1 order by c1; skip to check order by scope
    // distinct case: select distinct count(*) from t1 group by c1 order by c2;  --report error "not a SELECTed expression" instead of "not a GROUP BY expression"
    //                it report error by order by check instead of group by check
    if ((is_top_select_stmt() &&
        (NULL == group_by_exprs_ || group_by_exprs_->empty() || ref_stmt->has_distinct()) &&
        (NULL == rollup_exprs_ || rollup_exprs_->empty() || ref_stmt->has_distinct()))) {
      visitor.remove_scope(SCOPE_ORDERBY);
    }
    ObArray<ObRawExpr*> relation_expr_pointers;
    if (OB_FAIL(ref_stmt->get_relation_exprs(relation_expr_pointers, visitor))) {
      LOG_WARN("get stmt relation exprs fail", K(ret));
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < relation_expr_pointers.count(); ++i) {
      ObRawExpr *expr = relation_expr_pointers.at(i);
      if (OB_FAIL(expr->preorder_accept(*this)))  {
        LOG_WARN("fail to check group by", K(i), K(ret));
      }
    }
  
    //处理结束，则退出
    int tmp_ret = OB_SUCCESS;
    const ObSelectStmt *pop_stmt = NULL;
    tmp_ret = cur_stmts_.pop_back(pop_stmt);
    // even if ret is not success, it must
    if (OB_SUCCESS != tmp_ret) {
      ret = tmp_ret;
      LOG_WARN("failed to pop back stmt", K(ret));
    }
    if (OB_SUCC(ret) && !only_need_contraints_) {
      const ObIArray<ObSelectStmt*> &child_stmts = ref_stmt->get_set_query();
      for (int64_t i = 0; OB_SUCC(ret) && i < child_stmts.count(); ++i) {
        ret = check_select_stmt(child_stmts.at(i));
      }
    }
  }
  LOG_DEBUG("check group by end stmt", K(ret));
  --level_;
  return ret;
}

// for subquery
// if it's refered subquery, then check all expressions including select_items, where, connect by, group by, having, order by
// ref query also exists in group by
// eg:
//  select case when c1 in (select a.c1 from t1 b) then 1 else 0 end c1 from t1 a group by case when c1 in (select a.c1 from t1 b) then 1 else 0 end;
int ObGroupByChecker::visit(ObQueryRefRawExpr &expr)
{
  int ret = OB_SUCCESS;
  if (find_in_group_by(expr) || find_in_rollup(expr)) {
    set_skip_expr(&expr);
  } else if (is_top_select_stmt() && !expr.has_exec_param()) {
    // do nothing
  } else if (!only_need_contraints_) {
    const ObSelectStmt *ref_stmt = expr.get_ref_stmt();
    if (OB_FAIL(check_select_stmt(ref_stmt))) {
      LOG_WARN("failed to check select stmt", K(ret));
    }
  }
  return ret;
}

// check column ref
// 检查仅当表达式level与group by exprs的level一致才进行check，即属于同一层的expr
// 但对于子查询出现的列，仅仅检查与自己level一致的表达式
// eg: select case when 1 in(select d1 from t1 where c1=d2) then 1 else 0 end as c1 from t2 group by c1;
//   only check c1, but d1 and d2 are not checked
int ObGroupByChecker::visit(ObColumnRefRawExpr &expr)
{
  int ret = OB_SUCCESS;
  bool belongs_to = true;
  if (only_need_contraints_) {
    if (find_in_group_by(expr) || find_in_rollup(expr)) {
      set_skip_expr(&expr);
    }
  } else if (OB_FAIL(colref_belongs_to_check_stmt(expr, belongs_to))) {
    LOG_WARN("failed to get belongs to stmt", K(ret));
  } else if (!belongs_to) {
  } else if (find_in_group_by(expr) || find_in_rollup(expr)) {
  } else if (NULL != dblink_groupby_expr_) {
    if (OB_FAIL(dblink_groupby_expr_->push_back(static_cast<oceanbase::sql::ObRawExpr *>(&expr)))) {
      LOG_WARN("failed to push checked_expr into group_by_exprs", K(ret));
    } else {
      LOG_DEBUG("succ to push checked_expr into group_by_exprs", K(expr));
    }
  } else {
    ret = OB_ERR_WRONG_FIELD_WITH_GROUP;
    ObString column_name = concat_qualified_name(expr.get_database_name(),
                                                 expr.get_table_name(),
                                                 expr.get_column_name());
    LOG_USER_ERROR(OB_ERR_WRONG_FIELD_WITH_GROUP, column_name.length(), column_name.ptr());
    LOG_DEBUG("column not in group by", K(*group_by_exprs_), K(expr));
  }
  return ret;
}

int ObGroupByChecker::visit(ObPlQueryRefRawExpr &expr)
{
  int ret = OB_SUCCESS;
  UNUSED(expr);
  LOG_WARN("pl query ref in group by clause does not supported", K(ret));
  return ret;
}

// need check the whole expression
int ObGroupByChecker::visit(ObOpRawExpr &expr)
{
  int ret = OB_SUCCESS;
  if (find_in_group_by(expr) || find_in_rollup(expr)) {
    set_skip_expr(&expr);
  }
  return ret;
}

int ObGroupByChecker::visit(ObCaseOpRawExpr &expr)
{
  int ret = OB_SUCCESS;
  if (find_in_group_by(expr) || find_in_rollup(expr)) {
    set_skip_expr(&expr);
  }
  return ret;
}

int ObGroupByChecker::visit(ObMatchFunRawExpr &expr)
{
  int ret = OB_SUCCESS;
  if (find_in_group_by(expr) || find_in_rollup(expr)) {
    set_skip_expr(&expr);
  }
  return ret;
}

// following case is allowed
// select max(max(data)) from test group by id order by id, max(max(data)) ; 
// select max(max(data)) from test group by id order by id; 
// select max(max(data)) from test group by id having id=1;
// select sum(sum(data)) from test group by id order by sum(data);
// select sum(sum(data)) from test group by id having max(data) = 4 order by max(max(data))
// following case is blocked
// select max(data) from test group by id order by max(max(data))
// select max(data) from test group by id order by max(max(data)),max(data)
// having orderby select的顺序检查
// having一定在内层，order by如果包含max max就在外层，否则在内层，select在外层
int ObGroupByChecker::visit(ObAggFunRawExpr &expr)
{
  int ret = OB_SUCCESS;
  bool belongs_to = true;
  // expr is not in the current stmt, the expr is from subquery, it will check the expr from parent stmt
    // eg: select (select max(a.d2)from t1 b where b.c2=a.d1) from t2 b group by d1;
    //    then a.d2 that is in max(a.d2) is not in group by d1, it will report error
  if (find_in_group_by(expr) || find_in_rollup(expr)) {
    set_skip_expr(&expr);
  } else if (only_need_contraints_) {
    // do nothing
  } else if (OB_FAIL(belongs_to_check_stmt(expr, belongs_to))) {
    LOG_WARN("failed to get belongs to stmt", K(ret));
  } else if (belongs_to) {
    // expr is aggregate function in current stmt, then skip it
    if (expr.in_inner_stmt()) {
      set_skip_expr(&expr);
    } else if (has_nested_aggr_) {
      // do nothing
    } else {
      set_skip_expr(&expr);
    }
  } else {
    set_skip_expr(&expr);
  }
  return ret;
}

int ObGroupByChecker::visit(ObSysFunRawExpr &expr)
{
  int ret = OB_SUCCESS;
  if (find_in_group_by(expr) || find_in_rollup(expr)) {
    set_skip_expr(&expr);
  }
  return ret;
}

// select (select c1 from t1 union select d1 from t2) as c1 from t3;
// then QueryRefRaw: (select c1 from t1 union select d1 from t2)
// and the select stmt->select_itmes are SetOpRawExpr
// then SetOpRawExpr is union(c1,d1)
int ObGroupByChecker::visit(ObSetOpRawExpr &expr)
{
  int ret = OB_SUCCESS;
  if (find_in_group_by(expr) || find_in_rollup(expr)) {
    set_skip_expr(&expr);
  }
  return ret;
}

int ObGroupByChecker::visit(ObAliasRefRawExpr &expr)
{
  int ret = OB_SUCCESS;
  if (find_in_group_by(expr) || find_in_rollup(expr)) {
    set_skip_expr(&expr);
  }
  return ret;
}

// don't check the whole window function, but visit all argument of window function
int ObGroupByChecker::visit(ObWinFunRawExpr &expr)
{
  int ret = OB_SUCCESS;
  if (find_in_group_by(expr) || find_in_rollup(expr)) {
    set_skip_expr(&expr);
  }
  return ret;
}

int ObGroupByChecker::visit(ObPseudoColumnRawExpr &expr)
{
  int ret = OB_SUCCESS;
  bool belongs_to = true;
  if (only_need_contraints_) {
    if (find_in_group_by(expr) || find_in_rollup(expr)) {
      set_skip_expr(&expr);
    }
  } else if (OB_FAIL(belongs_to_check_stmt(expr, belongs_to))) {
    LOG_WARN("failed to get belongs to stmt", K(ret));
  } else if (belongs_to) {
    if (find_in_group_by(expr) || find_in_rollup(expr)) {
      set_skip_expr(&expr);
    } else if (T_ORA_ROWSCN != expr.get_expr_type() &&
               NULL == dblink_groupby_expr_){
      ret = OB_ERR_WRONG_FIELD_WITH_GROUP;
      //LOG_USER_ERROR(ret, column_name.length(), column_name.ptr());
      LOG_DEBUG("pseudo column not in group by", K(*group_by_exprs_), K(expr));
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
