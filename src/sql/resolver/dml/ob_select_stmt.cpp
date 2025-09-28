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
#include "sql/resolver/dml/ob_select_stmt.h"
#include "sql/resolver/expr/ob_shared_expr_resolver.h"
#include "sql/optimizer/ob_optimizer_util.h"

using namespace oceanbase::sql;
using namespace oceanbase::common;
using namespace oceanbase::share::schema;

int SelectItem::deep_copy(ObIRawExprCopier &expr_copier,
                          const SelectItem &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(expr_copier.copy(other.expr_, expr_))) {
    LOG_WARN("failed to copy expr", K(ret));
  } else {
    is_real_alias_ = other.is_real_alias_;
    alias_name_ = other.alias_name_;
    paramed_alias_name_ = other.paramed_alias_name_;
    expr_name_ = other.expr_name_;
    questions_pos_ = other.questions_pos_;
    params_idx_ = other.params_idx_;
    neg_param_idx_ = other.neg_param_idx_;
    esc_str_flag_ = other.esc_str_flag_;
    need_check_dup_name_ = other.need_check_dup_name_;
    implicit_filled_ = other.implicit_filled_;
    is_implicit_added_ = other.is_implicit_added_;
    is_hidden_rowid_ = other.is_hidden_rowid_;
  }
  return ret;
}

int ObSelectIntoItem::deep_copy(ObIAllocator &allocator,
                                ObIRawExprCopier &copier,
                                const ObSelectIntoItem &other)
{
  int ret = OB_SUCCESS;
  into_type_ = other.into_type_;
  outfile_name_ = other.outfile_name_;
  field_str_ = other.field_str_;
  line_str_ = other.line_str_;
  closed_cht_ = other.closed_cht_;
  is_optional_ = other.is_optional_;
  is_single_ = other.is_single_;
  max_file_size_ = other.max_file_size_;
  escaped_cht_ = other.escaped_cht_;
  cs_type_ = other.cs_type_;
  file_partition_expr_ = other.file_partition_expr_;
  buffer_size_ = other.buffer_size_;
  user_vars_.assign(other.user_vars_);
  if (OB_FAIL(copier.copy(other.file_partition_expr_, file_partition_expr_))) {
    LOG_WARN("deep copy file partition expr failed", K(ret));
  } else if (OB_FAIL(ob_write_string(allocator, other.external_properties_, external_properties_))) {
    LOG_WARN("failed to deep copy string", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < other.pl_vars_.count(); ++i) {
    ObRawExpr* pl_var;
    if (OB_FAIL(copier.copy(other.pl_vars_.at(i), pl_var))) {
      LOG_WARN("failed to copy exprs", K(ret));
    } else if (OB_FAIL(pl_vars_.push_back(pl_var))) {
      LOG_WARN("failed to push back group by expr", K(ret));
    }
  }
  return ret;
}
const char* const ObSelectIntoItem::DEFAULT_LINE_TERM_STR = "\n";
const char* const ObSelectIntoItem::DEFAULT_FIELD_TERM_STR = "\t";
const char ObSelectIntoItem::DEFAULT_FIELD_ENCLOSED_CHAR = 0;
const bool ObSelectIntoItem::DEFAULT_OPTIONAL_ENCLOSED = false;
const bool ObSelectIntoItem::DEFAULT_SINGLE_OPT = true;
const int64_t ObSelectIntoItem::DEFAULT_MAX_FILE_SIZE = 256 * 1024 * 1024;
const int64_t ObSelectIntoItem::DEFAULT_BUFFER_SIZE = 1 * 1024 * 1024;
const char ObSelectIntoItem::DEFAULT_FIELD_ESCAPED_CHAR = '\\';

//对于select .. for update 也认为是被更改
int ObSelectStmt::check_table_be_modified(uint64_t ref_table_id, bool& is_exists) const
{
  int ret = OB_SUCCESS;
  is_exists = false;
  for (int64_t i = 0; OB_SUCC(ret) && !is_exists && i < table_items_.count(); ++i) {
    TableItem *table_item = table_items_.at(i);
    if (OB_ISNULL(table_item)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("table item is NULL", K(ret), K(i), K(table_items_.count()));
    } else if (table_item->for_update_ && ref_table_id == table_item->ref_id_) {
      is_exists = true;
      LOG_DEBUG("duplicate table is used in select for update", K(is_exists), K(ref_table_id), K(table_items_.count()));
    }
  }
  if (OB_SUCC(ret) && !is_exists) {
    ObSEArray<ObSelectStmt*, 16> child_stmts;
    if (OB_FAIL(get_child_stmts(child_stmts))) {
      LOG_ERROR("get child stmt failed", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && !is_exists && i < child_stmts.count(); ++i) {
        ObSelectStmt *sub_stmt = child_stmts.at(i);
        if (OB_ISNULL(sub_stmt)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("sub stmt is null", K(ret));
        } else if (OB_FAIL(SMART_CALL(sub_stmt->check_table_be_modified(ref_table_id, is_exists)))) {
          LOG_WARN("check sub stmt whether has select for update failed", K(ret), K(i));
        }
      }
    }
  }
  return ret;
}

bool ObSelectStmt::has_distinct_or_concat_agg() const
{
  bool has = false;
  for (int64_t i = 0; !has && i < get_aggr_item_size(); ++i) {
    const ObAggFunRawExpr *aggr = get_aggr_item(i);
    if (NULL != aggr) {
      has = aggr->is_param_distinct() ||
            // Consistent with the has_group_concat_ flag in ObAggregateProcessor.
            T_FUN_GROUP_CONCAT == aggr->get_expr_type() ||
            T_FUN_KEEP_WM_CONCAT == aggr->get_expr_type() ||
            T_FUN_WM_CONCAT == aggr->get_expr_type() ||
            T_FUN_JSON_ARRAYAGG == aggr->get_expr_type() ||
            T_FUN_ORA_JSON_ARRAYAGG == aggr->get_expr_type() ||
            T_FUN_JSON_OBJECTAGG == aggr->get_expr_type() ||
            T_FUN_ORA_JSON_OBJECTAGG == aggr->get_expr_type();
    }
  }
  return has;
}

int ObSelectStmt::add_window_func_expr(ObWinFunRawExpr *expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (OB_FAIL(win_func_exprs_.push_back(expr))) {
    LOG_WARN("failed to add expr", K(ret));
  } else {
    expr->set_explicited_reference();
  }
  return ret;
}

int ObSelectStmt::set_qualify_filters(common::ObIArray<ObRawExpr *> &exprs)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(qualify_filters_.assign(exprs))) {
    LOG_WARN("failed to add expr", K(ret));
  }
  return ret;
}

int ObSelectStmt::remove_window_func_expr(ObWinFunRawExpr *expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < win_func_exprs_.count(); i++) {
      if (OB_ISNULL(win_func_exprs_.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret));
      } else if (expr == win_func_exprs_.at(i)) {
        ret = win_func_exprs_.remove(i);
        break;
      } else { /*do nothing*/ }
    }
  }
  return ret;
}

int ObSelectStmt::check_aggr_and_winfunc(ObRawExpr &expr)
{
  int ret = OB_SUCCESS;
  if (expr.is_aggr_expr() &&
      !ObRawExprUtils::find_expr(agg_items_, &expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("aggr expr does not exist in the stmt", K(agg_items_), K(expr), K(ret));
  } else if (expr.is_win_func_expr() &&
             !ObRawExprUtils::find_expr(win_func_exprs_, &expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("win func expr does not exist in the stmt", K(ret), K(expr));
  }
  return ret;
}

int ObSelectStmt::assign(const ObSelectStmt &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDMLStmt::assign(other))) {
    LOG_WARN("failed to copy stmt");
  } else if (OB_FAIL(select_items_.assign(other.select_items_))) {
    LOG_WARN("assign other select items failed", K(ret));
  } else if (OB_FAIL(group_exprs_.assign(other.group_exprs_))) {
    LOG_WARN("assign other group exprs failed", K(ret));
  } else if (OB_FAIL(rollup_exprs_.assign(other.rollup_exprs_))) {
    LOG_WARN("assign other rollup exprs failed", K(ret));
  } else if (OB_FAIL(having_exprs_.assign(other.having_exprs_))) {
    LOG_WARN("assign other having exprs failed", K(ret));
  } else if (OB_FAIL(agg_items_.assign(other.agg_items_))) {
    LOG_WARN("assign other aggr items failed", K(ret));
  } else if (OB_FAIL(win_func_exprs_.assign(other.win_func_exprs_))) {
    LOG_WARN("assign window function exprs failed", K(ret));
  } else if (OB_FAIL(qualify_filters_.assign(other.qualify_filters_))) {
    LOG_WARN("assign window function filter exprs failed", K(ret));
  } else if (OB_FAIL(rollup_directions_.assign(other.rollup_directions_))) {
    LOG_WARN("assign other rollup directions.", K(ret));
  } else if (OB_FAIL(set_query_.assign(other.set_query_))) {
    LOG_WARN("assign set query failed", K(ret));
  } else {
    set_op_ = other.set_op_;
    is_recursive_cte_ = other.is_recursive_cte_;
    is_distinct_ = other.is_distinct_;
    is_view_stmt_ = other.is_view_stmt_;
    view_ref_id_ = other.view_ref_id_;
    is_match_topk_ = other.is_match_topk_;
    is_set_distinct_ = other.is_set_distinct();
    show_stmt_ctx_.assign(other.show_stmt_ctx_);
    select_type_ = other.select_type_;
    into_item_ = other.into_item_;
    children_swapped_ = other.children_swapped_;
    check_option_ = other.check_option_;
    contain_ab_param_ = other.contain_ab_param_;
    has_reverse_link_ = other.has_reverse_link_;
    is_expanded_mview_ = other.is_expanded_mview_;
    is_select_straight_join_ = other.is_select_straight_join_;
    is_implicit_distinct_ = false; // it is a property from upper stmt, do not copy
  }
  return ret;
}

int ObSelectStmt::deep_copy_stmt_struct(ObIAllocator &allocator,
                                        ObRawExprCopier &expr_copier,
                                        const ObDMLStmt &input)
{
  int ret = OB_SUCCESS;
  const ObSelectStmt &other = static_cast<const ObSelectStmt &>(input);
  if (OB_UNLIKELY(!input.is_select_stmt())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("input stmt is invalid", K(ret));
  } else if (OB_FAIL(set_query_.assign(other.set_query_))) {
    LOG_WARN("failed to assgin set query", K(ret));
  } else if (OB_FAIL(ObDMLStmt::deep_copy_stmt_struct(allocator, expr_copier, other))) {
    LOG_WARN("deep copy DML stmt failed", K(ret));
  } else if (OB_FAIL(expr_copier.copy(other.group_exprs_, group_exprs_))) {
    LOG_WARN("deep copy group expr failed", K(ret));
  } else if (OB_FAIL(expr_copier.copy(other.rollup_exprs_, rollup_exprs_))) {
    LOG_WARN("deep copy rollup expr failed", K(ret));
  } else if (OB_FAIL(expr_copier.copy(other.having_exprs_, having_exprs_))) {
    LOG_WARN("deep copy having expr failed", K(ret));
  } else if (OB_FAIL(expr_copier.copy(other.agg_items_, agg_items_))) {
    LOG_WARN("deep copy agg item failed", K(ret));
  } else if (OB_FAIL(expr_copier.copy(other.win_func_exprs_, win_func_exprs_))) {
    LOG_WARN("deep copy window function expr failed", K(ret));
  } else if (OB_FAIL(expr_copier.copy(other.qualify_filters_, qualify_filters_))) {
    LOG_WARN("deep copy window function expr failed", K(ret));
  } else if (OB_FAIL(rollup_directions_.assign(other.rollup_directions_))) {
    LOG_WARN("assign rollup directions failed", K(ret));
  } else if (OB_FAIL(deep_copy_stmt_objects<SelectItem>(expr_copier,
                                                        other.select_items_,
                                                        select_items_))) {
    LOG_WARN("deep copy select items failed", K(ret));
  } else if (OB_FAIL(deep_copy_stmt_objects<OrderItem>(expr_copier,
                                                       other.order_items_,
                                                       order_items_))) {
    LOG_WARN("deep copy order items failed", K(ret));
  } else {
    set_op_ = other.set_op_;
    is_recursive_cte_ = other.is_recursive_cte_;
    is_distinct_ = other.is_distinct_;
    is_view_stmt_ = other.is_view_stmt_;
    view_ref_id_ = other.view_ref_id_;
    is_match_topk_ = other.is_match_topk_;
    is_set_distinct_ = other.is_set_distinct_;
    show_stmt_ctx_.assign(other.show_stmt_ctx_);
    select_type_ = other.select_type_;
    children_swapped_ = other.children_swapped_;
    is_fetch_with_ties_ = other.is_fetch_with_ties_;
    check_option_ = other.check_option_;
    contain_ab_param_ = other.contain_ab_param_;
    has_reverse_link_ = other.has_reverse_link_;
    is_expanded_mview_ = other.is_expanded_mview_;
    is_select_straight_join_ = other.is_select_straight_join_;
    is_implicit_distinct_ = false; // it is a property from upper stmt, do not copy
    // copy insert into statement
    if (OB_SUCC(ret) && NULL != other.into_item_) {
      ObSelectIntoItem *temp_into_item = NULL;
      void *ptr = allocator.alloc(sizeof(ObSelectIntoItem));
      if (OB_ISNULL(ptr)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate select into item", K(ret));
      } else {
        temp_into_item = new(ptr) ObSelectIntoItem();
        if (OB_FAIL(temp_into_item->deep_copy(allocator, expr_copier, *other.into_item_))) {
          LOG_WARN("deep copy into item failed", K(ret));
        } else {
          into_item_ = temp_into_item;
        }
      }
    }
  }
  return ret;
}

int ObSelectStmt::create_select_list_for_set_stmt(ObRawExprFactory &expr_factory)
{
  int ret = OB_SUCCESS;
  SelectItem new_select_item;
  ObRawExprResType res_type;
  ObSelectStmt *child_stmt = NULL;
  if (OB_ISNULL(child_stmt = get_set_query(0))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null stmt", K(ret), K(child_stmt), K(get_set_op()));
  } else {
    int64_t num = child_stmt->get_select_item_size();
    for (int64_t i = 0; OB_SUCC(ret) && i < num; i++) {
      SelectItem &child_select_item = child_stmt->get_select_item(i);
      // unused
      // ObString set_column_name = left_select_item.alias_name_;
      ObItemType set_op_type = static_cast<ObItemType>(T_OP_SET + get_set_op());
      res_type.reset();
      new_select_item.alias_name_ = child_select_item.alias_name_;
      new_select_item.expr_name_ = child_select_item.expr_name_;
      new_select_item.is_real_alias_ = child_select_item.is_real_alias_ || child_select_item.expr_->is_column_ref_expr();
      res_type = child_select_item.expr_->get_result_type();
      if (OB_FAIL(ObRawExprUtils::make_set_op_expr(expr_factory, i, set_op_type, res_type,
                                                   NULL, new_select_item.expr_))) {
        LOG_WARN("create set op expr failed", K(ret));
      } else if (OB_FAIL(add_select_item(new_select_item))) {
        LOG_WARN("push back set select item failed", K(ret));
      } else if (OB_ISNULL(new_select_item.expr_) ||
                 OB_UNLIKELY(!new_select_item.expr_->is_set_op_expr())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("expr is null or is not set op expr", "set op", PC(new_select_item.expr_));
      }
    }
  }
  return ret;
}

int ObSelectStmt::update_stmt_table_id(ObIAllocator *allocator, const ObSelectStmt &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDMLStmt::update_stmt_table_id(allocator, other))) {
    LOG_WARN("failed to update stmt table id", K(ret));
  } else if (OB_UNLIKELY(set_query_.count() != other.set_query_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected child query count", K(ret), K(set_query_.count()),
                                                 K(other.set_query_.count()));
  } else {
    ObSelectStmt *child_query = NULL;
    ObSelectStmt *other_child_query = NULL;
    for (int64_t i = 0; OB_SUCC(ret) && i < set_query_.count(); i++) {
      if (OB_ISNULL(other_child_query = other.set_query_.at(i))
          || OB_ISNULL(child_query = set_query_.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("null statement", K(ret), K(child_query), K(other_child_query));
      } else if (OB_FAIL(SMART_CALL(child_query->update_stmt_table_id(allocator, *other_child_query)))) {
        LOG_WARN("failed to update stmt table id", K(ret));
      } else { /* do nothing*/ }
    }
  }
  return ret;
}

int ObSelectStmt::iterate_stmt_expr(ObStmtExprVisitor &visitor)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDMLStmt::iterate_stmt_expr(visitor))) {
    LOG_WARN("failed to replace inner stmt expr", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < select_items_.count(); i++) {
    if (OB_FAIL(visitor.visit(select_items_.at(i).expr_, SCOPE_SELECT))) {
      LOG_WARN("failed to visit select exprs", K(ret));
    } else { /*do nothing*/ }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(visitor.visit(group_exprs_, SCOPE_GROUPBY))) {
      LOG_WARN("failed to visit group exprs", K(ret));
    } else if (OB_FAIL(visitor.visit(rollup_exprs_, SCOPE_GROUPBY))) {
      LOG_WARN("failed to visit rollup exprs", K(ret));
    } else if (OB_FAIL(visitor.visit(having_exprs_, SCOPE_HAVING))) {
      LOG_WARN("failed to visit having exprs", K(ret));
    } else if (OB_FAIL(visitor.visit(agg_items_, SCOPE_DICT_FIELDS))) {
      LOG_WARN("failed to visit aggr items", K(ret));   
    } else {/* do nothing */}
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(visitor.visit(win_func_exprs_, SCOPE_DICT_FIELDS))) {
      LOG_WARN("failed to visit winfunc exprs", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(visitor.visit(qualify_filters_, SCOPE_QUALIFY_FILTER))) {
      LOG_WARN("failed to visit winfunc exprs", K(ret));
    }
  }
  if (OB_SUCC(ret) && NULL != into_item_) {
    for (int64_t i = 0; OB_SUCC(ret) && i < into_item_->pl_vars_.count(); ++i) {
      if (OB_FAIL(visitor.visit(into_item_->pl_vars_.at(i), SCOPE_SELECT_INTO))) {
        LOG_WARN("failed to visit select into", K(ret));
      }
    }
    if (OB_SUCC(ret) && into_item_->file_partition_expr_ != NULL
        && OB_FAIL(visitor.visit(into_item_->file_partition_expr_, SCOPE_SELECT))) {
      LOG_WARN("failed to visit select into", K(ret));
    }
  }
  return ret;
}



ObSelectStmt::ObSelectStmt()
    : ObDMLStmt(stmt::T_SELECT)
{
  limit_count_expr_  = NULL;
  limit_offset_expr_ = NULL;
  is_distinct_ = false;
  is_set_distinct_ = false;
  set_op_ = NONE;
  is_recursive_cte_ = false;
  is_view_stmt_ = false;
  view_ref_id_ = OB_INVALID_ID;
  select_type_ = AFFECT_FOUND_ROWS;
  into_item_ = NULL;
  is_match_topk_ = false;
  children_swapped_ = false;
  check_option_ = VIEW_CHECK_OPTION_NONE;
  contain_ab_param_ = false;
  has_reverse_link_ = false;
  is_expanded_mview_ = false;
  is_select_straight_join_ = false;
  is_implicit_distinct_ = false;
  is_oracle_compat_groupby_ = false;
}

ObSelectStmt::~ObSelectStmt()
{
}


int ObSelectStmt::add_select_item(SelectItem &item)
{
  int ret = OB_SUCCESS;
  if (item.expr_ != NULL) {
    if (item.is_real_alias_) {
      item.expr_->set_alias_column_name(item.alias_name_);
    }
    if (OB_FAIL(select_items_.push_back(item))) {
      LOG_WARN("push back new item failed", K(ret));
    }
  } else {
    ret = OB_ERR_ILLEGAL_ID;
  }
  return ret;
}

/**
 * clear and reset select items for JOIN...USING
 * @param[in] sorted_select_items
 * @return OB_SUCCESS succeed, others fail
 */

int ObSelectStmt::get_child_stmt_size(int64_t &child_size) const
{
  int ret = OB_SUCCESS;
  int64_t tmp_size = 0;
  if (OB_FAIL(ObDMLStmt::get_child_stmt_size(tmp_size))) {
    LOG_WARN("failed to get child stmt size", K(ret));
  } else {
    child_size = tmp_size + set_query_.count();
  }
  return ret;
}

int ObSelectStmt::get_child_stmts(ObIArray<ObSelectStmt*> &child_stmts) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(append(child_stmts, set_query_))) {
    LOG_WARN("failed to append child query", K(ret));
  } else if (OB_FAIL(ObDMLStmt::get_child_stmts(child_stmts))) {
    LOG_WARN("failed to get child stmts", K(ret));
  }
  return ret;
}

int ObSelectStmt::set_child_stmt(const int64_t child_num, ObSelectStmt* child_stmt)
{
  int ret = OB_SUCCESS;
  if (child_num < set_query_.count()) {
    ret = set_set_query(child_num, child_stmt);
  } else if (OB_FAIL(ObDMLStmt::set_child_stmt(child_num - set_query_.count(), child_stmt))) {
    LOG_WARN("failed to set dml child stmt", K(ret));
  }
  return ret;
}

int ObSelectStmt::set_set_query(const int64_t index, ObSelectStmt *stmt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(index < 0 || index >= set_query_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to set child query", K(ret), K(index), K(set_query_.count()));
  } else {
    set_query_.at(index) = stmt;
  }
  return ret;
}

// input:: ((stmt1) union (stmt2)) union (stmt3)
// output:: stmt1
const ObSelectStmt* ObSelectStmt::get_real_stmt() const
{
  const ObSelectStmt *cur_stmt = this;
  while (OB_NOT_NULL(cur_stmt) && cur_stmt->is_set_stmt()) {
    cur_stmt = cur_stmt->get_set_query(0);
  }
  return cur_stmt;
}

int ObSelectStmt::get_from_subquery_stmts(ObIArray<ObSelectStmt*> &child_stmts,
                                          bool contain_lateral_table/* =true */) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(child_stmts.assign(set_query_))) {
    LOG_WARN("failed to assign child query", K(ret));
  } else if (OB_FAIL(ObDMLStmt::get_from_subquery_stmts(child_stmts,
                                                        contain_lateral_table))) {
    LOG_WARN("get from subquery stmts failed", K(ret));
  }
  return ret;
}

int64_t ObSelectStmt::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  UNUSED(SMART_CALL(do_to_string(buf, buf_len, pos)));
  return pos;
}

int ObSelectStmt::do_to_string(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  J_OBJ_START();
  ObArray<ObSelectStmt*> child_stmts;
  if (OB_FAIL(ObDMLStmt::get_child_stmts(child_stmts))) {
    databuff_printf(buf, buf_len, pos, "ERROR get child stmts failed");
  } else if (NONE == set_op_) {
    if (OB_ISNULL(query_ctx_)) {
      ret = OB_ERR_UNEXPECTED;
      databuff_printf(buf, buf_len, pos, "ERROR query context is null");
    } else {
      J_KV(
           N_STMT_TYPE, stmt_type_,
           N_TABLE, table_items_,
           N_JOINED_TABLE, joined_tables_,
           N_SEMI_INFO, semi_infos_,
           N_PARTITION_EXPR, part_expr_items_,
           N_COLUMN, column_items_,
           N_SELECT, select_items_,
           //muhang
           //"recursive union", is_recursive_cte_,
           N_DISTINCT, is_distinct_,
           N_FROM, from_items_,
           N_WHERE, condition_exprs_,
           N_GROUP_BY, group_exprs_,
           N_ROLLUP, rollup_exprs_,
           N_HAVING, having_exprs_,
           N_AGGR_FUNC, agg_items_,
           N_ORDER_BY, order_items_,
           N_LIMIT, limit_count_expr_,
           N_WIN_FUNC, win_func_exprs_,
           N_OFFSET, limit_offset_expr_,
           N_SHOW_STMT_CTX, show_stmt_ctx_,
           N_STMT_HINT, stmt_hint_,
           N_USER_VARS, user_var_exprs_,
           N_QUERY_CTX, *query_ctx_,
           K_(pseudo_column_like_exprs),
           //K_(win_func_exprs),
           K(child_stmts),
           K_(check_option),
           K_(dblink_id),
           K_(is_reverse_link),
           K_(is_expanded_mview),
           K_(is_implicit_distinct)
             );
    }
  } else {
    J_KV(N_SET_OP, ((int)set_op_),
         //"recursive union", is_recursive_cte_,
         N_DISTINCT, is_set_distinct_,
         K_(set_query),
         N_ORDER_BY, order_items_,
         N_LIMIT, limit_count_expr_,
         N_SELECT, select_items_,
         K(child_stmts),
         K_(dblink_id),
         K_(is_reverse_link),
         K_(is_implicit_distinct));
  }
  J_OBJ_END();
  return ret;
}

int ObSelectStmt::check_and_get_same_aggr_item(ObRawExpr *expr,
                                               ObAggFunRawExpr *&same_aggr)
{
  int ret = OB_SUCCESS;
  same_aggr = NULL;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expr is null", K(ret));
  } else {
    bool is_existed = false;
    for (int64_t i = 0; OB_SUCC(ret) && !is_existed && i < agg_items_.count(); ++i) {
      bool need_check_status = (i + 1) % 1000 == 0;
      if (OB_ISNULL(agg_items_.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("expr is null", K(ret));
      } else if (need_check_status &&
                 OB_FAIL(THIS_WORKER.check_status())) {
        LOG_WARN("failed to check status", K(ret));
      } else if (agg_items_.at(i)->same_as(*expr)) {
        is_existed = true;
        same_aggr = agg_items_.at(i);
      }
    }
  }
  return ret;
}

int ObSelectStmt::get_same_win_func_item(const ObRawExpr *expr, ObWinFunRawExpr *&win_expr)
{
  int ret = OB_SUCCESS;
  win_expr = NULL;
  if (OB_ISNULL(query_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret));
  } else {
    ObQuestionmarkEqualCtx cmp_ctx;
    bool is_existed = false;
    for (int64_t i = 0; OB_SUCC(ret) && !is_existed && i < win_func_exprs_.count(); ++i) {
      bool need_check_status = (i + 1) % 1000 == 0;
      if (win_func_exprs_.at(i) != NULL && expr != NULL &&
          expr->same_as(*win_func_exprs_.at(i), &cmp_ctx)) {
        win_expr = win_func_exprs_.at(i);
        is_existed = true;
      } else if (need_check_status &&
                 OB_FAIL(THIS_WORKER.check_status())) {
        LOG_WARN("failed to check status", K(ret));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(append(query_ctx_->all_equal_param_constraints_,
                                       cmp_ctx.equal_pairs_))) {
      LOG_WARN("failed to append equal param info", K(ret));
    }
  }
  return ret;
}

bool ObSelectStmt::has_for_update() const
{
  bool bret = false;
  for (int64_t i = 0; !bret && i < table_items_.count(); ++i) {
    const TableItem *table_item = table_items_.at(i);
    if (table_item != NULL && table_item->for_update_) {
      bret = true;
    }
  }
  return bret;
}

bool ObSelectStmt::is_skip_locked() const
{
  bool bret = false;
  for (int64_t i = 0; !bret && i < table_items_.count(); ++i) {
    const TableItem *table_item = table_items_.at(i);
    if (table_item != NULL && table_item->skip_locked_) {
      bret = true;
    }
  }
  return bret;
}

int ObSelectStmt::clear_sharable_expr_reference()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDMLStmt::clear_sharable_expr_reference())) {
    LOG_WARN("failed to clear sharable expr reference", K(ret));
  } else {
    ObRawExpr *expr = NULL;
    for (int64_t i = 0; OB_SUCC(ret) && i < agg_items_.count(); i++) {
      if (OB_ISNULL(expr = agg_items_.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret));
      } else {
        expr->clear_explicited_referece();
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < win_func_exprs_.count(); i++) {
      if (OB_ISNULL(expr = win_func_exprs_.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret));
      } else {
        expr->clear_explicited_referece();
      }
    }
  }
  return ret;
}

int ObSelectStmt::remove_useless_sharable_expr(ObRawExprFactory *expr_factory,
                                               ObSQLSessionInfo *session_info,
                                               bool explicit_for_col)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr_factory)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (OB_FAIL(ObDMLStmt::remove_useless_sharable_expr(expr_factory, session_info, explicit_for_col))) {
    LOG_WARN("failed to remove useless sharable expr", K(ret));
  } else {
    ObRawExpr *expr = NULL;
    const bool is_scala = is_scala_group_by();
    for (int64_t i = agg_items_.count() - 1; OB_SUCC(ret) && i >= 0; i--) {
      if (OB_ISNULL(expr = agg_items_.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret));
      } else if (expr->is_explicited_reference()) {
        /*do nothing*/
      } else if (OB_FAIL(agg_items_.remove(i))) {
        LOG_WARN("failed to remove agg item", K(ret));
      } else {
        LOG_TRACE("succeed to remove agg items", K(*expr));
      }
    }
    for (int64_t i = win_func_exprs_.count() - 1; OB_SUCC(ret) && i >= 0; i--) {
      if (OB_ISNULL(expr = win_func_exprs_.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret));
      } else if (expr->is_explicited_reference()) {
        /*do nothing*/
      } else if (OB_FAIL(win_func_exprs_.remove(i))) {
        LOG_WARN("failed to remove win func expr", K(ret));
      } else {
        LOG_TRACE("succeed to remove win func exprs", K(*expr));
      }
    }
    if (OB_SUCC(ret) && is_scala && agg_items_.empty()) {
      ObAggFunRawExpr *aggr_expr = NULL;
      if (OB_FAIL(ObRawExprUtils::build_dummy_count_expr(*expr_factory, session_info, aggr_expr))) {
        LOG_WARN("failed to build a dummy expr", K(ret));
      } else if (OB_FAIL(agg_items_.push_back(aggr_expr))) {
        LOG_WARN("failed to push back", K(ret));
      } else if (OB_FAIL(set_sharable_expr_reference(*aggr_expr,
                                                     ExplicitedRefType::REF_BY_NORMAL))) {
        LOG_WARN("failed to set sharable exprs reference", K(ret));
      } else {/* do nothing */}
    }
  }
  return ret;
}

bool ObSelectStmt::is_spj() const
{
  int ret = OB_SUCCESS;
  bool bret = false;
  bret = !(has_distinct()
           || has_group_by()
           || is_set_stmt()
           || has_rollup()
           || has_order_by()
           || has_limit()
           || get_aggr_item_size() != 0
           || get_from_item_size() == 0
           || is_contains_assignment()
           || has_window_function()
           || has_sequence());
  return bret;
}

bool ObSelectStmt::is_spjg() const
{
  int ret = OB_SUCCESS;
  bool bret = false;
  bret = !(has_distinct()
           || has_having()
           || is_set_stmt()
           || has_rollup()
           || has_order_by()
           || has_limit()
           || get_from_item_size() == 0
           || is_contains_assignment()
           || has_window_function()
           || has_sequence());
  return bret;
}

int ObSelectStmt::get_select_exprs(ObIArray<ObRawExpr*> &select_exprs)
{
  int ret = OB_SUCCESS;
  ObRawExpr *expr = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && i < select_items_.count(); ++i) {
    if (OB_ISNULL(expr = select_items_.at(i).expr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null select expr", K(ret));
    } else if (OB_FAIL(select_exprs.push_back(expr))) {
      LOG_WARN("failed to push back expr", K(ret));
    } else { /*do nothing*/ }
  }
  return ret;
}

int ObSelectStmt::get_select_exprs(ObIArray<ObRawExpr*> &select_exprs) const
{
  int ret = OB_SUCCESS;
  ObRawExpr *expr = NULL;
  LOG_DEBUG("before get_select_exprs", K(select_items_), K(table_items_), K(lbt()));
  for (int64_t i = 0; OB_SUCC(ret) && i < select_items_.count(); ++i) {
    if (OB_ISNULL(expr = select_items_.at(i).expr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null select expr", K(ret));
    } else if (OB_FAIL(select_exprs.push_back(expr))) {
      LOG_WARN("failed to push back expr", K(ret));
    } else { /*do nothing*/}
  }
  return ret;
}

int ObSelectStmt::get_select_exprs_without_lob(ObIArray<ObRawExpr*> &select_exprs) const
{
  int ret = OB_SUCCESS;
  ObRawExpr *expr = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && i < select_items_.count(); ++i) {
    if (OB_ISNULL(expr = select_items_.at(i).expr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null select expr", K(ret));
    } else if (ObLongTextType == expr->get_data_type()) {
      /*do nothing*/
    } else if (OB_FAIL(select_exprs.push_back(expr))) {
      LOG_WARN("failed to push back expr", K(ret));
    } else { /*do nothing*/ }
  }
  return ret;
}

int ObSelectStmt::get_equal_set_conditions(ObIArray<ObRawExpr *> &conditions,
                                           const bool is_strict,
                                           const bool check_having) const
{
  int ret = OB_SUCCESS;
  if (!(check_having && has_rollup()) &&
      OB_FAIL(ObDMLStmt::get_equal_set_conditions(conditions, is_strict, check_having))) {
    LOG_WARN("failed to get equal set cond", K(ret));
  } else if (!check_having) {
    // do nothing
  } else if (OB_FAIL(append(conditions, having_exprs_))) {
    LOG_WARN("failed to append having exprs", K(ret));
  } else { /* do nothing */ }
  return ret;
}

int ObSelectStmt::get_set_stmt_size(int64_t &size) const
{
  int ret = OB_SUCCESS;
  ObSEArray<const ObSelectStmt *, 8> set_stmts;
  if (is_set_stmt()) {
    for (int64_t i = -1; i < set_stmts.count(); ++i) {
      const ObSelectStmt *stmt = (i == -1) ? this : set_stmts.at(i);
      if (OB_ISNULL(stmt)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("stmt is null", K(ret));
      } else if (!stmt->is_set_stmt()) {
        // do nothing
      } else if (OB_FAIL(append(set_stmts, stmt->set_query_))) {
        LOG_WARN("failed to append stmts", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      size = set_stmts.count() + 1;
    }
  }
  return ret;
}

bool ObSelectStmt::check_is_select_item_expr(const ObRawExpr *expr) const
{
  bool bret = false;
  for(int64_t i = 0; !bret && i < select_items_.count(); ++i) {
    if (expr == select_items_.at(i).expr_) {
      bret = true;
    }
  }
  return bret;
}

bool ObSelectStmt::contain_nested_aggr() const
{
  bool ret = false;
  for (int64_t i = 0; !ret && i < agg_items_.count(); i++) {
    if (agg_items_.at(i)->contain_nested_aggr()) {
      ret = true;
    } else { /*do nothing.*/ }
  }
  return ret;
}



int ObSelectStmt::recursive_get_expr(ObRawExpr *expr,
                                     ObIArray<ObRawExpr *> &exprs,
                                     ObExprInfoFlag target_flag,
                                     ObExprInfoFlag search_flag) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (expr->has_flag(target_flag)) {
    ret = exprs.push_back(expr);
  } else if (expr->has_flag(search_flag)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < expr->get_param_count(); ++i) {
      if (OB_FAIL(SMART_CALL(recursive_get_expr(expr->get_param_expr(i),
                                                exprs,
                                                target_flag,
                                                search_flag)))) {
        LOG_WARN("failed to recursive get expr", K(ret));
      }
    }
  }
  return ret;
}

bool ObSelectStmt::has_hidden_rowid() const {
  bool res = false;
  for (int64_t i = 0; !res && i < get_select_item_size(); i++) {
    if (select_items_.at(i).is_hidden_rowid_) {
      res = true;
    }
  }
  return res;
}

bool ObSelectStmt::has_external_table() const {
  bool res = false;
  for (int i = 0; i < get_table_items().count(); i++) {
    if (OB_NOT_NULL(get_table_items().at(i))
        && EXTERNAL_TABLE == get_table_items().at(i)->table_type_) {
      res = true;
      break;
    }
  }
  return res;
}
int ObSelectStmt::get_pure_set_exprs(ObIArray<ObRawExpr*> &pure_set_exprs) const
{
  int ret = OB_SUCCESS;
  pure_set_exprs.reuse();
  if (is_set_stmt()) {
    ObRawExpr *select_expr = NULL;
    ObRawExpr *set_op_expr = NULL;
    for (int64_t i = 0; OB_SUCC(ret) && i < get_select_item_size(); ++i) {
      if (OB_ISNULL(select_expr = get_select_item(i).expr_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret), K(i));
      } else if (!select_expr->has_flag(CNT_SET_OP)) {
        /* do nothing, for recursive union all, exists search/cycle pseudo columns*/
      } else if (OB_ISNULL(set_op_expr = get_pure_set_expr(select_expr))
                 || OB_UNLIKELY(!set_op_expr->is_set_op_expr())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected expr", K(ret), K(i), K(*select_expr));
      } else if (OB_FAIL(pure_set_exprs.push_back(set_op_expr))) {
        LOG_WARN("failed to push back expr", K(ret));
      }
    }
  }
  return ret;
}

// cast sys functions above set op expr can be:
//   T_FUN_SYS_CAST/T_FUN_SYS_RAWTOHEX/T_FUN_SYS_TO_NCHAR/T_FUN_SYS_TO_CHAR
// use this function to get set op expr from expr child recursively
ObRawExpr* ObSelectStmt::get_pure_set_expr(ObRawExpr *expr)
{
  while (OB_NOT_NULL(expr) && !expr->is_set_op_expr() && 0 < expr->get_param_count()) {
    expr = expr->get_param_expr(0);
  }
  return expr;
}


int ObSelectStmt::check_is_simple_lock_stmt(bool &is_valid) const
{
  int ret = OB_SUCCESS;
  is_valid = false;
  if (get_from_item_size() == 0 &&
      get_subquery_expr_size() == 0 &&
      get_select_item_size() > 0 &&
      !has_distinct() &&
      !has_group_by() &&
      !is_set_stmt() &&
      !has_rollup() &&
      !has_order_by() &&
      get_aggr_item_size() == 0 &&
      !is_contains_assignment() &&
      !has_window_function() &&
      !has_sequence()) {
    bool contain_lock_expr = false;
    for (int64_t i = 0; !contain_lock_expr && i < select_items_.count(); i ++) {
      if (OB_FAIL(ObRawExprUtils::check_contain_lock_exprs(select_items_.at(i).expr_, contain_lock_expr))) {
        LOG_WARN("failed to check contain lock exprs", K(ret));
      }
    }
    is_valid = contain_lock_expr;
  }
  return ret;
}

/**
 * @brief ObSelectStmt::formalize_implicit_distinct
 *
 * 1. Check current stmt is legal for implicit distinct
 * 2. Set implicit distinct for subquery
 */
int ObSelectStmt::formalize_implicit_distinct()
{
  int ret = OB_SUCCESS;
  if (!is_implicit_distinct_allowed()) {
    reset_implicit_distinct();
  }
  if (OB_FAIL(ObDMLStmt::formalize_implicit_distinct())) {
    LOG_WARN("failed to do formalize implicit distinct", K(ret));
  }
  return ret;
}

/**
 * @brief ObSelectStmt::check_from_dup_insensitive
 *
 * Stmt contains duplicate-insensitive aggregation or DISTINCT, making it
 * insensitive to duplicate values ​​in From Scope.
 *
 * e.g.
 * SELECT DISTINCT * FROM T1, T2;
 * SELECT MAX(C1), MIN(C2) FROM T3;
 * SELECT * FROM T4 LIMIT 1;
 */
int ObSelectStmt::check_from_dup_insensitive(bool &is_from_dup_insens) const
{
  int ret = OB_SUCCESS;
  bool is_valid = true;
  bool is_dup_insens_aggr = false;
  is_from_dup_insens = false;
  // basic validity check
  if (is_set_stmt()) {
    is_valid = false;
  } else if (OB_FAIL(check_relation_exprs_deterministic(is_valid))) {
    LOG_WARN("failed to check relation exprs deterministic", K(ret));
  } else if (!is_valid) {
    // do nothing
  }

  // check whether stmt has aggregation duplicate-insensitive
  if (OB_FAIL(ret) || !is_valid) {
    // do nothing
  } else if (OB_FAIL(is_duplicate_insensitive_aggregation(is_dup_insens_aggr))) {
    LOG_WARN("failed to check has duplicate-insensitive aggregation", K(ret));
  } else if (is_dup_insens_aggr) {
    is_from_dup_insens = true;
  } else if (has_group_by() || has_window_function()) {
    is_valid = false;
  }

  // check whether stmt has distinct duplicate-insensitive
  if (OB_FAIL(ret) || !is_valid || is_from_dup_insens) {
    // do nothing
  } else if (!(is_distinct() || is_implicit_distinct())) {
    // do nothing
  } else {
    is_from_dup_insens = true;
  }

  // check whether stmt has limit 1
  if (OB_FAIL(ret) || !is_valid || is_from_dup_insens) {
    // do nothing
  } else if (NULL == limit_count_expr_
             || NULL != limit_offset_expr_
             || NULL != limit_percent_expr_) {
    // do nothing
  } else if (T_INT != limit_count_expr_->get_expr_type()
             || 1 != static_cast<const ObConstRawExpr *>(limit_count_expr_)->get_value().get_int()) {
    // do nothing
  } else {
    is_from_dup_insens = true;
  }
  return ret;
}

/**
 * @brief ObSelectStmt::is_duplicate_insensitive_aggregation
 *
 * Check whether the select stmt has duplicate-insensitive aggregation.
 * The stmt should have group by and only contain aggregate function listed: BIT_AND(),
 * BIT_OR(), MAX(), MIN() APPROX_COUNT_DISTINCT(), and other aggr func with DISTINCT.
 * 
 * @param is_dup_insens_aggr True if select stmt has duplicate-insensitive aggregation.
 */
int ObSelectStmt::is_duplicate_insensitive_aggregation(bool &is_dup_insens_aggr) const
{
  int ret = OB_SUCCESS;
  is_dup_insens_aggr = false;
  if (!has_group_by()) {
    // do nothing
  } else {
    is_dup_insens_aggr = true;
    for (int64_t i = 0; OB_SUCC(ret) && is_dup_insens_aggr && i < get_aggr_item_size(); ++i) {
      const ObAggFunRawExpr *agg_expr = get_aggr_item(i);
      if (OB_ISNULL(agg_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("agg expr is null", K(ret));
      } else if (agg_expr->get_expr_type() == T_FUN_SYS_BIT_AND
                 || agg_expr->get_expr_type() == T_FUN_SYS_BIT_OR
                 || agg_expr->get_expr_type() == T_FUN_MAX
                 || agg_expr->get_expr_type() == T_FUN_MIN
                 || agg_expr->get_expr_type() == T_FUN_APPROX_COUNT_DISTINCT
                 || agg_expr->get_expr_type() == T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS) {
        // do nothing, it is valid aggregate function
      } else if (agg_expr->is_param_distinct()) {
        // do nothing, it is valid aggregate function
      } else {
        is_dup_insens_aggr = false;
        break;
      }
    }
  }
  return ret;
}

int ObSelectStmt::is_query_deterministic(bool &is_deterministic) const
{
  int ret = OB_SUCCESS;
  ObArray<ObRawExpr *> relation_exprs;
  is_deterministic = true;
  if (OB_FAIL(get_relation_exprs(relation_exprs))) {
    LOG_WARN("failed to get relation exprs", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && is_deterministic && i < relation_exprs.count(); i++) {
      if (OB_ISNULL(relation_exprs.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("got null expr");
      } else {
        is_deterministic = relation_exprs.at(i)->is_deterministic();
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && is_deterministic && i < set_query_.count(); i++) {
      if (OB_ISNULL(set_query_.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("got null expr");
      } else if (OB_FAIL(SMART_CALL(set_query_.at(i)->is_query_deterministic(is_deterministic)))) {
        LOG_WARN("failed to check set query deterministic", K(ret));
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && is_deterministic && i < get_table_size(); ++i) {
      const TableItem *table_item = get_table_item(i);
      if (OB_ISNULL(table_item)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table_item is null", K(i));
      } else if (table_item->is_basic_table() ||
                 table_item->is_values_table() ||
                 table_item->is_json_table() ||
                 table_item->is_function_table() ||
                 table_item->is_fake_cte_table()) {
        /* do nothing */
      } else if (table_item->is_generated_table() ||
                 table_item->is_lateral_table() ||
                 table_item->is_temp_table()) {
        if (OB_FAIL(SMART_CALL(table_item->ref_query_->is_query_deterministic(is_deterministic)))) {
          LOG_WARN("failed to check table item deterministic", K(ret));
        }
      } else {
        is_deterministic = false;
      }
    }
  }
  return ret;
}

bool ObSelectStmt::is_implicit_distinct_allowed() const
{
  return !(has_for_update()
           || has_limit());
}
