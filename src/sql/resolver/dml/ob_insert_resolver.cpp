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

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/dml/ob_insert_resolver.h"
#include "sql/resolver/dml/ob_select_resolver.h"
#include "sql/rewrite/ob_transform_utils.h"
#include "ob_default_value_utils.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
namespace sql
{
ObInsertResolver::ObInsertResolver(ObResolverParams &params)
: ObDelUpdResolver(params),
    row_count_(0),
    sub_select_resolver_(NULL),
    autoinc_col_added_(false),
    is_mock_(false)
{
  params.contain_dml_ = true;
}

ObInsertResolver::~ObInsertResolver()
{
  if (NULL != sub_select_resolver_) {
    sub_select_resolver_->~ObSelectResolver();
  }
}

int ObInsertResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObInsertStmt *insert_stmt = NULL;
  bool has_tg = false;
  if (OB_UNLIKELY(T_INSERT != parse_tree.type_)
      || OB_UNLIKELY(4 > parse_tree.num_child_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse tree", K(parse_tree.type_), K(parse_tree.num_child_));
  } else if (OB_ISNULL(insert_stmt = create_stmt<ObInsertStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("create insert stmt failed", K(insert_stmt));
  } else if (OB_ISNULL(parse_tree.children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse tree", K(parse_tree.children_), K(ret));
  } else if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid session info", K(session_info_), K(ret));
  } else if (OB_ISNULL(parse_tree.children_[REPLACE_NODE])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid node for is_replacement", K(parse_tree.children_[1]));
  } else { // TODO:@webber.wb replace how oracle handles trigger
    insert_stmt->set_replace(parse_tree.children_[REPLACE_NODE]->type_ == T_REPLACE);
    session_info_->set_ignore_stmt(NULL != parse_tree.children_[IGNORE_NODE] ? true : false);
    insert_stmt->set_ignore(NULL != parse_tree.children_[IGNORE_NODE] ? true : false);
  }

  if (OB_SUCC(ret) && 5 <= parse_tree.num_child_) {
    if (OB_NOT_NULL(parse_tree.children_[OVERWRITE_NODE]) && 1 == parse_tree.children_[OVERWRITE_NODE]->value_) {
      insert_stmt->set_overwrite(true);
    }
  }

  // resolve outline data hints first
  if (OB_SUCC(ret)) {
    if (OB_FAIL(resolve_outline_data_hints())) {
      LOG_WARN("resolve outline data hints failed", K(ret));
    } else if (OB_FAIL(resolve_hints(parse_tree.children_[HINT_NODE]))) {
      LOG_WARN("failed to resolve hints", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    NG_TRACE(resolve_ins_tbl_begin);
    if (OB_FAIL(resolve_insert_clause(*parse_tree.children_[INSERT_NODE]))) {
      LOG_WARN("resolve single table insert failed", K(ret));
    } else {
      has_tg = insert_stmt->has_instead_of_trigger();
    }
    NG_TRACE(resolve_ins_tbl_end);
  }

  if (OB_SUCC(ret)) {
    if (insert_stmt->is_replace() && insert_stmt->is_ignore()) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "replace statement with ignore");
    } else if (insert_stmt->is_ignore() && insert_stmt->has_global_index()) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "ignore with global index");
    } else { /*do nothing*/ }
  }

  if (OB_SUCC(ret) && insert_stmt->is_overwrite()) {
    TableItem *tmp_table_item = insert_stmt->get_table_item_by_id(insert_stmt->get_insert_table_info().table_id_);
    if (OB_ISNULL(tmp_table_item)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected null", K(tmp_table_item), K(ret));
    } else if (!insert_stmt->value_from_select()) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "insert overwrite with values");
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(insert_stmt->formalize_stmt(session_info_))) {
      LOG_WARN("pull stmt all expr relation ids failed", K(ret));
    } else {
      LOG_DEBUG("check insert table info", K(insert_stmt->get_insert_table_info()));
    }
  }

  if (OB_SUCC(ret) && !has_tg) {
    if (OB_FAIL(check_view_insertable())) {
      LOG_WARN("view not insertable", K(ret));
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(check_insert_into_external_table())) {
    LOG_WARN("check insert into external table failed", K(ret));
  }

  return ret;
}

int ObInsertResolver::resolve_insert_clause(const ParseNode &node)
{
  int ret = OB_SUCCESS;
  //resolve insert table reference
  //the target table must be resolved first,
  //insert_stmt appoint that the first table in table_items_ is the target table
  const ParseNode *insert_into = NULL;
  const ParseNode *values_node = NULL;
  TableItem* table_item = NULL;
  ObInsertStmt *insert_stmt = get_insert_stmt();
  bool has_tg = false;
  if (OB_ISNULL(session_info_) || OB_ISNULL(insert_stmt) ||
      OB_ISNULL(insert_into = node.children_[INTO_NODE]) ||
      OB_ISNULL(values_node = node.children_[VALUE_NODE])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(session_info_), K(insert_stmt),
        K(insert_into), K(values_node), K(ret));
  } else if (OB_FAIL(resolve_insert_field(*insert_into, table_item))) {
    LOG_WARN("failed to resolve insert filed", K(ret));
    LOG_WARN("failed to get label se columns", K(ret));
  } else if (OB_FAIL(resolve_values(*values_node,
                                    table_item, node.children_[DUPLICATE_NODE]))) {
    LOG_WARN("failed to resolve values", K(ret));
  } else {
    has_tg = insert_stmt->has_instead_of_trigger();
    if (!insert_stmt->get_table_items().empty() &&
        NULL != insert_stmt->get_table_item(0) &&
        (insert_stmt->get_table_item(0)->is_generated_table() ||
         insert_stmt->get_table_item(0)->is_temp_table())) {
      if (OB_FAIL(add_all_column_to_updatable_view(*insert_stmt, *insert_stmt->get_table_item(0), has_tg))) {
        LOG_WARN("failed to add column to updatable view", K(ret));
      } else if (OB_FAIL(view_pullup_special_column_exprs())) {
        LOG_WARN("failed to pullup special column exprs", K(ret));
      } else if (!has_tg && OB_FAIL(view_pullup_part_exprs())) {
        LOG_WARN("pullup part exprs for view failed", K(ret));
      } else { /*do nothing*/ }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(generate_insert_table_info(*table_item, insert_stmt->get_insert_table_info()))) {
      LOG_WARN("failed to generate insert table info", K(ret));
    } else { /*do nothing*/ }
  }

  if (OB_FAIL(ret)) {
  } else if (!has_tg && OB_FAIL(generate_autoinc_params(insert_stmt->get_insert_table_info()))) {
    LOG_WARN("failed to save autoinc params", K(ret));
  } else if (OB_FAIL(generate_column_conv_function(insert_stmt->get_insert_table_info()))) {
    LOG_WARN("failed to generate column conv function", K(ret));
  } else if (OB_FAIL(replace_gen_col_dependent_col(insert_stmt->get_insert_table_info()))) {
    // In static engine we need to replace the dependent column of generate column with the
    // new insert value. e.g.:
    //   c3 as c1 + c2
    // after add_column_conv_function() the c3's new value is: column_conv(c1 + c2)
    // should be replaced to: column_conv(column_conv(__values.c1) + column_conv(__values.c2).
    //
    // The old engine no need to do this because it calculate generate column with the
    // new inserted row.
    LOG_WARN("failed to replace gen col dependent col", K(ret));
  }
  

  if (OB_SUCC(ret)) {
    if (NULL != node.children_[DUPLICATE_NODE] && // resolve assignments
               OB_FAIL(resolve_insert_update_assignment(node.children_[DUPLICATE_NODE],
                                                        insert_stmt->get_insert_table_info()))) {
      LOG_WARN("failed to resolve insert update assignment", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(resolve_insert_constraint())) {
      LOG_WARN("failed to resolve insert constraint", K(ret));
    } else { /*do nothing*/ }
  }

  return ret;
}

int ObInsertResolver::add_column_conv_for_diagnosis(ObInsertStmt *insert_stmt,
                                                    ObSelectStmt *select_stmt,
                                                    TableItem* table_item)
{
  int ret = OB_SUCCESS;
  bool is_diagnosis = false;

  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid session_info_", K(ret));
  } else {
    is_diagnosis = session_info_->is_diagnosis_enabled();
  }

  if (OB_SUCC(ret)) {
    if (is_diagnosis) {
      if (OB_ISNULL(select_stmt)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid select stmt", K(ret), K(select_stmt));
      } else {
        ObIArray<SelectItem> &select_items = select_stmt->get_select_items();
        uint64_t table_id = insert_stmt->get_insert_table_info().table_id_;

        if (insert_stmt->get_values_desc().count() != select_items.count()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected insert target column and select items",
                  K(ret), K(insert_stmt->get_values_desc()), K(select_items));
        }
        for (int64_t i = 0; i < insert_stmt->get_values_desc().count() && OB_SUCC(ret); ++i) {
          ColumnItem *column_item = NULL;
          uint64_t column_id = OB_INVALID_ID;
          const ObColumnRefRawExpr *tbl_col = NULL;
          if (OB_ISNULL(tbl_col = insert_stmt->get_values_desc().at(i))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("invalid table column", K(ret), K(i), K(insert_stmt->get_values_desc()));
          } else if (FALSE_IT(column_id = tbl_col->get_column_id())) {
          } else if (OB_ISNULL(column_item = get_del_upd_stmt()->get_column_item_by_id(table_id,
                                                                                      column_id))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected null column item", K(ret));
          }

          if (OB_SUCC(ret)) {
            if (OB_FAIL(add_additional_function_according_to_type(
                                              column_item, select_items.at(i).expr_, T_INSERT_SCOPE,
                                              ObObjMeta::is_binary(tbl_col->get_data_type(),
                                                                  tbl_col->get_collation_type())))) {
              LOG_WARN("failed to build column conv expr", K(ret));
            }
          }
        }
      }
    } else {
      // do nothing
    }
  }
  return ret;
}
// This function is used to implement values(c1) functionality;
//insert into test values(1,2) on duplicate key update c2 = values(c1);
// Replace values(c1) expression with the result of column_conv()
int ObInsertResolver::process_values_function(ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObInsertStmt *insert_stmt = get_insert_stmt();
  if (OB_ISNULL(expr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(expr));
  } else if (OB_UNLIKELY(!expr->has_flag(IS_VALUES))
             || OB_UNLIKELY(expr->get_param_count() != 1)) {
    LOG_WARN("invalid expr", K(expr), K(expr->get_param_count()));
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(expr->get_param_expr(0)->get_expr_type() != T_REF_COLUMN)) {
    LOG_WARN("invalid param expr", "type", expr->get_param_expr(0)->get_expr_type());
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(insert_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid insert stmt", K(ret), K(insert_stmt));
  } else {
    const ObIArray<ObColumnRefRawExpr*>& insert_columns = insert_stmt->get_insert_table_info().column_exprs_;
    ObColumnRefRawExpr *b_expr = static_cast<ObColumnRefRawExpr *>(expr->get_param_expr(0));
    if (OB_ISNULL(b_expr)) {
      LOG_WARN("invalid expr or insert_columns", K(b_expr));
      ret = OB_INVALID_ARGUMENT;
    } else {
      int64_t table_id = insert_stmt->get_insert_table_info().table_id_;
      uint64_t column_id = b_expr->get_column_id();
      ColumnItem *column_item = NULL;
      if (OB_ISNULL(column_item = insert_stmt->get_column_item_by_id(table_id, column_id))) {
        ret = OB_ERR_BAD_FIELD_ERROR;
        ObString scope_name = ObString::make_string(get_scope_name(current_scope_));
        LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR, b_expr->get_column_name().length(), b_expr->get_column_name().ptr(),
                       scope_name.length(), scope_name.ptr());
        LOG_WARN("fail to get column item", K(ret), K(table_id), K(column_id));
      } else {
        const int64_t N = insert_columns.count();
        int64_t index = OB_INVALID_INDEX;
        ret = OB_ENTRY_NOT_EXIST;
        for (int64_t i = 0; i < N && OB_ENTRY_NOT_EXIST == ret; i++) {
          if (OB_ISNULL(insert_columns.at(i))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_ERROR("invalid insert columns", K(i), K(insert_columns.at(i)));
          } else if (insert_columns.at(i)->get_column_id() == column_id) {
            index = i;
            ret = OB_SUCCESS;
          }
        }
        if (OB_FAIL(ret)) {
          //do nothing
        } else if (OB_UNLIKELY(index == OB_INVALID_INDEX)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("fail to find column position", K(column_id));
        } else {
          ObRawExpr *value_expr = get_insert_stmt()->get_column_conv_exprs().at(index);
          if (OB_FAIL(add_additional_function_according_to_type(column_item,
                                                                value_expr,
                                                                T_INSERT_SCOPE,
                                                                true))) {
            LOG_WARN("fail to add additional function", K(ret));
          } else if (value_expr->is_const_expr() &&
                     !ob_is_enum_or_set_type(value_expr->get_data_type())) {
            ObRawExpr* remove_const_expr = NULL;
            if (OB_FAIL(ObRawExprUtils::build_remove_const_expr(*params_.expr_factory_,
                                                                *params_.session_info_,
                                                                value_expr, remove_const_expr))) {
              LOG_WARN("fail to build remove_const expr",K(ret), K(expr), K(remove_const_expr));
            } else {
              value_expr = remove_const_expr;
            }
          }
          if (OB_FAIL(ret)) {
            //do nothing
          } else if (OB_FAIL(ObRawExprUtils::replace_ref_column(expr, b_expr, value_expr))) {
            LOG_WARN("fail to replace ref column", K(ret), K(b_expr), K(value_expr));
          } else {
            SQL_RESV_LOG(DEBUG, "replace ref column", K(expr), K(b_expr),
                         K(value_expr), K(column_id));
          }
        }
      }
    }
  }
  return ret;
}

//insert into test values(1, c1 + 2);
// This function is used to solve the value acquisition process of c1. First, find the expr 1 corresponding to C1, then convert expr 1 to column_conv(1);
// Convert c1+2 ==> column_conv(1) + 2;
int ObInsertResolver::replace_column_ref(ObArray<ObRawExpr*> *value_row,
                                         ObRawExpr *&expr,
                                         bool in_generated_column /*default false*/)
{
  int ret = OB_SUCCESS;
  ObInsertStmt *insert_stmt = get_insert_stmt();
  if (OB_ISNULL(expr) || OB_ISNULL(value_row) || OB_ISNULL(insert_stmt) || OB_ISNULL(params_.expr_factory_)) {
    LOG_WARN("invalid argument", K(expr), K(value_row), K(insert_stmt));
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(ERROR, "fail to get allocator", K(ret), K_(allocator));
  } else if (expr->get_param_count() > 0) {
    for (int i = 0; OB_SUCC(ret) && i < expr->get_param_count(); i++) {
      if (OB_FAIL(SMART_CALL(replace_column_ref(value_row, expr->get_param_expr(i),
                                                in_generated_column)))) {
        LOG_WARN("fail to postorder_spread", K(ret), K(expr->get_param_expr(i)));
      }
    }
  }
  if (OB_SUCC(ret) && expr->is_column_ref_expr()) {
    int64_t value_index = -1;
    ObIArray<ObColumnRefRawExpr*> &value_desc = insert_stmt->get_values_desc();
    if (value_desc.count() < value_row->count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid array size", K(value_row->count()), K(value_desc.count()));
    } else {
      ObColumnRefRawExpr *b_expr = static_cast<ObColumnRefRawExpr*>(expr);
      uint64_t column_id = b_expr->get_column_id();
      ret = OB_ENTRY_NOT_EXIST;
      for(int64_t i = 0; OB_ENTRY_NOT_EXIST == ret && i < value_row->count(); i++) {
        if (value_desc.at(i)->get_column_id() == column_id) {
          value_index = i;
          ret = OB_SUCCESS;
        }
      }
      ColumnItem * column_item = NULL;
      if (OB_ENTRY_NOT_EXIST == ret) {
        if (OB_FAIL(replace_column_to_default(expr))) {
          LOG_WARN("fail to replace column to default", K(ret), K(*expr));
        } else {
          SQL_RESV_LOG(DEBUG, "replace column ref to default", K(*expr));
        }
      } else if (OB_ISNULL(column_item = insert_stmt->get_column_item_by_id(
                  get_insert_stmt()->get_insert_table_info().table_id_, column_id))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to find column item", K(ret), K(column_id));
      } else {
        ObRawExpr *&value_expr = value_row->at(value_index);
        if (OB_ISNULL(value_expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("fail to get value expr", K(value_index), K(value_expr), K(value_row));
        } else if (OB_FAIL(add_additional_function_according_to_type(column_item,
                                                                     value_expr,
                                                                     T_INSERT_SCOPE,
                                                                     true,
                                                                     true/*in_insert_value_list*/))) {
          LOG_WARN("fail to build column conv expr", K(ret));
        }
        if (OB_SUCC(ret)) {
          expr = insert_stmt->get_values_desc().at(value_index);
          if (OB_FAIL(ObRawExprUtils::build_column_conv_expr(*params_.expr_factory_,
                                                             *params_.allocator_,
                                                             *column_item->get_expr(),
                                                             expr,
                                                             session_info_))) {
            LOG_WARN("fail to add column_convert expr", K(ret), K(column_item));
          } else {
            insert_stmt->set_is_all_const_values(false);
            SQL_RESV_LOG(DEBUG, "replace column ref to value", K(*expr), K(value_index));
          }
        }
      }
    }
  }
  return ret;
}

int ObInsertResolver::resolve_insert_field(const ParseNode &insert_into, TableItem*& table_item)
{
  int ret = OB_SUCCESS;
  ObInsertStmt *insert_stmt = get_insert_stmt();
  const ParseNode *table_node = NULL;
  table_item = NULL;
  ObSelectStmt *ref_stmt = NULL;
  CK(OB_NOT_NULL(insert_stmt));
  CK(OB_NOT_NULL(table_node = insert_into.children_[0]));
  //resolve insert table
  //oracle mode allow to use insert subquery... => eg:insert into (select * from t1)v values(1,2,3);
  const bool old_flag = session_info_->is_table_name_hidden();
  session_info_->set_table_name_hidden(session_info_->get_ddl_info().is_ddl()
                                        && session_info_->get_ddl_info().is_dest_table_hidden());
  OZ(resolve_basic_table(*table_node, table_item));
  session_info_->set_table_name_hidden(old_flag);
  OZ(column_namespace_checker_.add_reference_table(table_item));
  if (OB_SUCC(ret)) {
    current_scope_ = T_INSERT_SCOPE;
    OZ (check_need_fired_trigger(table_item));
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(generate_insert_table_info(*table_item,
                                           insert_stmt->get_insert_table_info(),
                                           false))) {
      LOG_WARN("failed to generate insert table info", K(ret));
    } else { /*do nothing*/ }
  }

  if (OB_SUCC(ret) && 2 == insert_into.num_child_ &&
      OB_FAIL(resolve_insert_columns(insert_into.children_[1], insert_stmt->get_insert_table_info()))) {
    LOG_WARN("failed to resolve insert columns", K(ret));
  }

  if (OB_SUCC(ret)) {
    const ObTableSchema *table_schema = NULL;
    OZ(schema_checker_->get_table_schema(session_info_->get_effective_tenant_id(),
                                         table_item->get_base_table_item().ref_id_,
                                         table_schema,
                                         table_item->is_link_table()));
  }

  OZ(remove_dup_dep_cols_for_heap_table(insert_stmt->get_insert_table_info().part_generated_col_dep_cols_,
                                        insert_stmt->get_values_desc()));

  if (OB_ISNULL(table_item) || session_info_->is_inner() ) {
  } else if (OB_UNLIKELY(table_item->is_system_table_ && table_item->table_name_.case_compare(OB_ALL_LICENSE_TNAME) == 0)) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("modify license table is not allowed", KR(ret), K(table_item->table_name_), K(table_item->is_system_table_));
  }
  return ret;
}

int ObInsertResolver::resolve_insert_assign(const ParseNode &assign_list)
{
  int ret = OB_SUCCESS;
  //insert into t1 set c1=1, c2=1;
  ObSEArray<ObTableAssignment, 2> tables_assign;
  ObInsertStmt *insert_stmt = get_insert_stmt();
  if (OB_ISNULL(insert_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid insert stmt", K(ret), K(insert_stmt));
  } else if (OB_FAIL(resolve_assignments(assign_list, tables_assign, current_scope_))) {
    LOG_WARN("resolve insert set assignment list failed", K(ret));
  } else {
    ObArray<ObRawExpr*> value_row;
    int64_t table_count = tables_assign.count();
    for (int64_t i = 0 ; OB_SUCC(ret) && i < table_count; i++) {
      ObTableAssignment &ts = tables_assign.at(i);
      int64_t assign_count = ts.assignments_.count();
      for (int64_t j = 0; OB_SUCC(ret) && j < assign_count; j++) {
        ObAssignment &assign = ts.assignments_.at(j);
        //add column_item
        if (OB_ISNULL(assign.column_expr_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid assignment variable", K(i), K(j), K(assign));
        } else if (assign.is_duplicated_) {
          ret = OB_ERR_FIELD_SPECIFIED_TWICE;
          ObCStringHelper helper;
          LOG_USER_ERROR(OB_ERR_FIELD_SPECIFIED_TWICE,
              helper.convert(assign.column_expr_->get_column_name()));
        } else if (OB_FAIL(replace_column_to_default(assign.expr_))) {
          LOG_WARN("replace values column to default failed", K(ret));
        } else if (OB_FAIL(assign.expr_->formalize(session_info_))) {
          LOG_WARN("formalize expr failed", K(ret));
        } else if (OB_FAIL(value_row.push_back(assign.expr_))) {
          LOG_WARN("Can not add expr_id to ObArray", K(ret));
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(append(insert_stmt->get_insert_table_info().values_vector_, value_row))) {
        LOG_WARN("failed to append value row", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(check_need_match_all_params(insert_stmt->get_insert_table_info().values_desc_,
                                            insert_stmt->get_query_ctx()->need_match_all_params_))) {
      LOG_WARN("check need match all params failed", K(ret));
    }
  }
  const ObIArray<ObColumnRefRawExpr*> &dep_cols = insert_stmt->get_insert_table_info().part_generated_col_dep_cols_;
  if (OB_SUCC(ret) && 0 != dep_cols.count()) {
    ColumnItem *col_item = NULL;
    ObDefaultValueUtils utils(insert_stmt, &params_, this);
    for (int64_t i = 0; OB_SUCC(ret) && i < dep_cols.count(); ++i) {
      const ObColumnRefRawExpr *col_ref = dep_cols.at(i);
      CK(OB_NOT_NULL(col_ref));
      CK(OB_NOT_NULL(col_item = insert_stmt->get_column_item_by_id(
                                col_ref->get_table_id(),
                                col_ref->get_column_id())));
      if (OB_SUCC(ret)) {
        if (NULL == col_item->default_value_expr_) {
          OZ(utils.resolve_default_expr(*col_item, col_item->default_value_expr_,
                                        T_INSERT_SCOPE));
        }
      }
    }
  }

  return ret;
}

int ObInsertResolver::resolve_values(const ParseNode &value_node,
                                     TableItem* table_item,
                                     const ParseNode *duplicate_node)
{
  int ret = OB_SUCCESS;
  ObInsertStmt *insert_stmt = get_insert_stmt();
  void *select_buffer = NULL;
  if (OB_ISNULL(insert_stmt) || OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid stat", K(insert_stmt), K_(allocator));
  } else if (T_VALUE_LIST == value_node.type_) {
    // value list
    if (OB_FAIL(resolve_insert_values(&value_node,
                                      insert_stmt->get_insert_table_info()))) {
      LOG_WARN("resolve insert values failed", K(ret));
    }
  } else if (T_ASSIGN_LIST == value_node.type_) {
    if (OB_FAIL(resolve_insert_assign(value_node))) {
      LOG_WARN("resolve insert assign list failed", K(ret));
    }
  } else if (OB_ISNULL(select_buffer = allocator_->alloc(sizeof(ObSelectResolver)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate select buffer failed", K(ret), "size", sizeof(ObSelectResolver));
  } else {
    // value from sub-query(insert into table select ..)
    is_mock_ = lib::is_mysql_mode() && value_node.reserved_;
    ObSelectStmt *select_stmt = NULL;
    sub_select_resolver_ = new(select_buffer) ObSelectResolver(params_);
    //insert clause and select clause in insert into select belong to the same namespace level
    //so select resolver current level equal to insert resolver
    sub_select_resolver_->set_current_level(current_level_);
    sub_select_resolver_->set_current_view_level(current_view_level_);
    // select layer should not see the insert stmt's attributes from the upper layer, so upper scope stmt should be empty
    sub_select_resolver_->set_parent_namespace_resolver(NULL);
    //for values stmt: insert into table_name values row()...
    sub_select_resolver_->set_upper_insert_resolver(this);
    TableItem *sub_select_table = NULL;
    ObString view_name;
    ObSEArray<ColumnItem, 4> column_items;
    ParseNode *alias_node = NULL;
    ParseNode *table_alias_node = NULL;
    if (is_mock_ &&
        value_node.children_[PARSE_SELECT_FROM] != NULL &&
        value_node.children_[PARSE_SELECT_FROM]->num_child_ == 1 &&
        value_node.children_[PARSE_SELECT_FROM]->children_[0]->type_ == T_ALIAS &&
        value_node.children_[PARSE_SELECT_FROM]->children_[0]->num_child_ == 2) {
        alias_node = value_node.children_[PARSE_SELECT_FROM]->children_[0]->children_[1];
        if (T_LINK_NODE == alias_node->type_ && alias_node->num_child_ == 2) {
          table_alias_node = alias_node->children_[0];
        } else {
          table_alias_node = alias_node;
        }
        if (NULL != table_alias_node) {
          view_name.assign_ptr(const_cast<char*>(table_alias_node->str_value_),
                          static_cast<int32_t>(table_alias_node->str_len_));
        }
    }
    if (OB_UNLIKELY(T_SELECT != value_node.type_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid value node", K(value_node.type_));
    } else if (OB_FAIL(sub_select_resolver_->resolve(value_node))) {
      LOG_WARN("failed to resolve select stmt in INSERT stmt", K(ret));
    } else if (OB_ISNULL(select_stmt = sub_select_resolver_->get_select_stmt())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid select stmt", K(ret), K(select_stmt));
    } else if (!session_info_->get_ddl_info().is_ddl() &&
               !session_info_->get_ddl_info().is_dummy_ddl_for_inner_visibility() &&
                OB_FAIL(check_insert_select_field(*insert_stmt, *select_stmt, is_mock_))) {
      LOG_WARN("check insert select field failed", K(ret), KPC(insert_stmt), KPC(select_stmt));
    } else if (OB_FAIL(add_column_conv_for_diagnosis(insert_stmt, select_stmt, table_item))) {
      LOG_WARN("failed to add column conv for diagnosis", K(ret));
    } else if (OB_FAIL(resolve_generate_table_item(select_stmt, view_name, sub_select_table))) {
      LOG_WARN("failed to resolve generate table item", K(ret));
    }

    if (OB_SUCC(ret) && is_mock_) {
    ObString ori_table_name = table_item->table_name_;
    ObSEArray<ObString, 4> ori_column_names;
    ObString row_alias_table_name = sub_select_table->get_table_name();
    ObSEArray<ObString, 4> row_alias_column_names;
    //1. check_table_and_column_name
    if (OB_FAIL(check_table_and_column_name(insert_stmt->get_values_desc(),
                                            select_stmt,
                                            ori_table_name,
                                            ori_column_names,
                                            row_alias_table_name,
                                            row_alias_column_names))) {
      LOG_WARN("fail to get table and column name", K(ret));
    }
    //2.check_validity_of_duplicate_node
    if (OB_FAIL(ret)) {
      //do nothing
    } else if (duplicate_node != NULL) {
        if (OB_FAIL(check_validity_of_duplicate_node(duplicate_node,
                                                    ori_table_name,
                                                    ori_column_names,
                                                    row_alias_table_name,
                                                    row_alias_column_names))) {
            LOG_WARN("fail to check validity of duplicate node", K(ret));
        }
      }
    }
    if (OB_FAIL(ret)) {
      //do nothing
    } else if (OB_FAIL(resolve_all_generated_table_columns(*sub_select_table,
                                                                   column_items))) {
      LOG_WARN("failed to resolve all generated table columns", K(ret));
    } else {
      insert_stmt->add_from_item(sub_select_table->table_id_);
    }
  }
  return ret;
}

int ObInsertResolver::check_table_and_column_name(const ObIArray<ObColumnRefRawExpr*> & value_desc,
                                                  ObSelectStmt *select_stmt,
                                                  ObString &ori_table_name,
                                                  ObIArray<ObString> &ori_column_names,
                                                  ObString &row_alias_table_name,
                                                  ObIArray<ObString> &row_alias_column_names)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(select_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", K(ret));
  }
  //get original table name and column name,row alias table name and column name
  for (int64_t i = 0; OB_SUCC(ret) && i < value_desc.count(); i++) {
    //column name comparsion is case insensitive
    const ObColumnRefRawExpr* column = value_desc.at(i);
    if (OB_ISNULL(column)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("column is NULL", K(ret));
    } else if (OB_FAIL(ori_column_names.push_back(column->get_column_name()))) {
      LOG_WARN("fail to push column name");
    } else if (OB_FAIL(row_alias_column_names.push_back(select_stmt->get_select_item(i).alias_name_))){
      LOG_WARN("fail to push column name");
    }
  }
  if (OB_SUCC(ret)) {
  //check validity
    ObNameCaseMode mode = OB_NAME_CASE_INVALID;
    bool perserve_lettercase = true;
    ObCollationType cs_type = CS_TYPE_INVALID;
    if (OB_FAIL(session_info_->get_name_case_mode(mode))) {
      LOG_WARN("fail to get name case mode", K(mode), K(ret));
    } else if (OB_FAIL(session_info_->get_collation_connection(cs_type))) {
      LOG_WARN("fail to get collation_connection", K(ret));
    } else {
      perserve_lettercase = (mode != OB_LOWERCASE_AND_INSENSITIVE);
    }
    if (OB_FAIL(ret)) {
      //do nothing
    } else if (!perserve_lettercase) {
      ObCharset::casedn(cs_type, ori_table_name);
      ObCharset::casedn(cs_type, row_alias_table_name);
    }
    if (0 == ori_table_name.compare(row_alias_table_name)) {
      //case: insert into t1(a,b) values (4,5) as t1(a,b) on duplicate key update a = t1.a;
      ret = OB_ERR_NONUNIQ_TABLE;
      LOG_USER_ERROR(OB_ERR_NONUNIQ_TABLE, row_alias_table_name.length(), row_alias_table_name.ptr());
    } else if (row_alias_column_names.count() == 0 ||
              ori_column_names.count() == 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected column column", K(ret));
    } else if (row_alias_column_names.count() != ori_column_names.count()) {
      //case: insert into t1(a,b) values (4,5) as new(a,b,c) on duplicate key update a = t1.a;
      ret = OB_ERR_VIEW_WRONG_LIST;
      LOG_WARN("unexpect different count between row_alias_column_names and ori_column_names", K(ret));
    }
  }
  return ret;
}

int ObInsertResolver::check_validity_of_duplicate_node(const ParseNode* node,
                                                       ObString &ori_table_name,
                                                       ObIArray<ObString> &ori_column_names,
                                                       ObString &row_alias_table_name,
                                                       ObIArray<ObString> &row_alias_column_names)
{
  int ret = OB_SUCCESS;
  bool is_valid = true;
  if (OB_ISNULL(node)) {
    is_valid = false;
  } else if (T_ASSIGN_ITEM == node->type_) {
    ObString table_name;
    ObString column_name;
    ParseNode* table_name_node = NULL;
    ParseNode* column_name_node = NULL;
    ParseNode* col_ref_node = node->children_[0];
    if (col_ref_node->num_child_ != 3) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error", K(ret));
    } else if (OB_ISNULL(table_name_node = col_ref_node->children_[1])) {
      //do nothing
    } else if (OB_ISNULL(column_name_node = col_ref_node->children_[2])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error", K(ret));
    } else {
      table_name.assign_ptr(table_name_node->str_value_,
                                static_cast<int32_t>(table_name_node->str_len_));
      if (0 == (row_alias_table_name.compare(ori_table_name)) ) {
        column_name.assign_ptr(column_name_node->str_value_,
                               static_cast<int32_t>(column_name_node->str_len_));
        /*
        * INSERT INTO t1 VALUES (4,5,7) AS t1(m,n,p) ON DUPLICATE KEY UPDATE NEW.m = m+n+1;
        * NEW.m is not allowed to set
        */
        ret = OB_ERR_BAD_FIELD_ERROR;
        ObString scope_name = ObString::make_string(get_scope_name(current_scope_));
        LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR, column_name.length(), column_name.ptr(),
                                              scope_name.length(), scope_name.ptr());
      } else if (OB_FAIL(check_ambiguous_column(column_name, ori_column_names, row_alias_column_names))) {
        LOG_WARN("fail to check ambiguous columns", K(ret));
      }
    }
  } else if (T_FUN_SYS == node->type_) {
    if (node->num_child_ != 2) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error", K(node->num_child_), K(ret));
    } else {
      ObString node_name;
      ObString node_column_name;
      node_name.assign_ptr(node->children_[0]->str_value_,
                           static_cast<int32_t>(node->children_[0]->str_len_));
      if (0 == node_name.case_compare("values")) {
        //do nothing, do not replace column under values expr
        if (node->children_[1]->type_ == T_EXPR_LIST) {
          ParseNode* expr_list_node = node->children_[1];
          for (int64_t i = 0; OB_SUCC(ret) && i < expr_list_node->num_child_; ++i) {
            ObString node_table_name;
            if (OB_ISNULL(expr_list_node->children_[i])) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected error", K(ret));
            } else {
              //case: insert into t1(a,b) values (4,5) as new(a,b) on duplicate key update a = value(new.a)+new.a;
              //new.a under values is not allowed
              ParseNode* col_ref_node = expr_list_node->children_[i];
              if (col_ref_node->num_child_ != 3 || OB_ISNULL(col_ref_node->children_[2])) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("unexpected error", K(col_ref_node->num_child_), K(col_ref_node->children_[2]), K(ret));
              } else if (col_ref_node->children_[1] == NULL) {
                //table node is null
                //insert into t1(a,b) values (4,5) as new(a,b) on duplicate key update a = value(a)+a
                //a in valus expr must be column of target table t1
                node_column_name.assign_ptr(col_ref_node->children_[2]->str_value_,
                                            static_cast<int32_t>(col_ref_node->children_[2]->str_len_));
                if (!find_in_column(node_column_name, ori_column_names)) {
                  ret = OB_ERR_BAD_FIELD_ERROR;
                  ObString scope_name = ObString::make_string(get_scope_name(current_scope_));
                  LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR, node_column_name.length(), node_column_name.ptr(),
                                                        scope_name.length(), scope_name.ptr());
                }
              } else {
                //table node is not null
                node_table_name.assign_ptr(col_ref_node->children_[1]->str_value_,
                                          static_cast<int32_t>(col_ref_node->children_[1]->str_len_));
                if (0 == (node_table_name.compare(row_alias_table_name))) {
                  node_column_name.assign_ptr(col_ref_node->children_[2]->str_value_,
                            static_cast<int32_t>(col_ref_node->children_[2]->str_len_));
                  ret = OB_ERR_BAD_FIELD_ERROR;
                  ObString scope_name = ObString::make_string(get_scope_name(current_scope_));
                  LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR, node_column_name.length(), node_column_name.ptr(),
                                                        scope_name.length(), scope_name.ptr());
                }
              }
            }
          }
        }
        is_valid = false;
      }
    }
  } else if (T_COLUMN_REF == node->type_) {
    ObString table_name;
    ObString column_name;
    if (node->num_child_ != 3) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpect column ref child node", K(node->num_child_), K(ret));
    } else {
      if ((node->children_[1]) != NULL) {
        table_name.assign_ptr(node->children_[1]->str_value_,
                                   static_cast<int32_t>(node->children_[1]->str_len_));
      }
      if ((node->children_[2]) != NULL) {
        column_name.assign_ptr(node->children_[2]->str_value_,
                                    static_cast<int32_t>(node->children_[2]->str_len_));
      }
      if (node->children_[1] == NULL ||
         (0 == (table_name.compare(ori_table_name)) &&
          0 == (table_name.compare(row_alias_table_name)))) {
          //case: insert into t1(a,b) values (4,5) as new(a,b) on duplicate key update a = value(a)+a;
          //update a[0] = value(a[1])+a[2], a[2] is ambiguous
          if (OB_FAIL(check_ambiguous_column(column_name, ori_column_names, row_alias_column_names))) {
            LOG_WARN("fail to check ambiguous columns", K(ret));
          }
      }
    }
    is_valid = false;
  }
  for (int64_t i = 0; OB_SUCC(ret) && is_valid && i < node->num_child_; i++) {
    if (T_ASSIGN_ITEM == node->type_ && 0 == i) {
      //ON DUPLICATE KEY UPDATE c = new.a+values(b), only deal with value list [new.a+values(b)]
    } else if (OB_FAIL(SMART_CALL(check_validity_of_duplicate_node(node->children_[i],
                                                            ori_table_name,
                                                            ori_column_names,
                                                            row_alias_table_name,
                                                            row_alias_column_names)))) {
      LOG_WARN("refine_insert_update_assignment fail", K(ret));
    }
  }
  return ret;
}

int ObInsertResolver::check_ambiguous_column(ObString &column_name,
                                             ObIArray<ObString> &ori_column_names,
                                             ObIArray<ObString> &row_alias_column_names)
{
  int ret = OB_SUCCESS;
  bool find_in_ori = false;
  bool find_in_row_alias = false;
  if (row_alias_column_names.count() != ori_column_names.count()) {
    ret = OB_ERR_VIEW_WRONG_LIST;
    LOG_WARN("unexpect different count between row_alias_column_names and ori_column_names", K(ret));
  }
  for (int i = 0; OB_SUCC(ret) && (!find_in_row_alias || !find_in_ori)
                              && i < row_alias_column_names.count(); i++) {
    ObString row_alias_col_name = row_alias_column_names.at(i);
    ObString ori_col_name = ori_column_names.at(i);
    if (0 == (column_name.case_compare(row_alias_col_name))) {
      find_in_row_alias = true;
    }
    if (0 == (column_name.case_compare(ori_col_name))) {
      find_in_ori = true;
    }
  }
  if (OB_FAIL(ret)) {
    //do nothing
  } else if (find_in_row_alias && find_in_ori) {
    ret = OB_NON_UNIQ_ERROR;
    ObString scope_name = ObString::make_string(get_scope_name(current_scope_));
    LOG_USER_ERROR(OB_NON_UNIQ_ERROR, column_name.length(), column_name.ptr(),
                                          scope_name.length(), scope_name.ptr());
  }
  return ret;
}

bool ObInsertResolver::find_in_column(ObString &column_name,
                                      ObIArray<ObString> &column_names)
{
  bool find_column = false;
  for (int i = 0; !find_column && i < column_names.count(); i++) {
    ObString tmp_col_name = column_names.at(i);
    if (0 == (column_name.case_compare(tmp_col_name))) {
      find_column = true;
    }
  }
  return find_column;
}

int ObInsertResolver::check_insert_select_field(ObInsertStmt &insert_stmt,
                                                ObSelectStmt &select_stmt,
                                                bool is_mock)
{
  int ret = OB_SUCCESS;
  bool is_generated_column = false;
  const ObIArray<ObColumnRefRawExpr*> &values_desc = insert_stmt.get_values_desc();
  ObSelectStmt *ref_stmt = NULL;
  TableItem *insert_table = NULL;
  if (OB_ISNULL(insert_table = insert_stmt.get_table_item_by_id(insert_stmt.get_insert_table_info().table_id_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("insert target table is unexpected null", K(ret));
  } else if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid session_info_", K(ret));
  } else if (values_desc.count() != select_stmt.get_select_item_size()) {
    ret = OB_ERR_COULUMN_VALUE_NOT_MATCH;
    LOG_WARN("column count mismatch", K(values_desc.count()), K(select_stmt.get_select_item_size()));
    LOG_USER_ERROR(OB_ERR_COULUMN_VALUE_NOT_MATCH, 1l);
  } else if (is_mock && select_stmt.is_single_table_stmt()) {
    const TableItem *table_item = select_stmt.get_table_item(0);
    if (table_item->is_generated_table()) {
      ref_stmt = table_item->ref_query_;
      if (ref_stmt->get_select_item_size() != select_stmt.get_select_item_size()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("item size is unexpected", K(ret), K(ref_stmt->get_select_item_size()), K(select_stmt.get_select_item_size()));
      }
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < values_desc.count(); ++i) {
    const ObColumnRefRawExpr *value_desc = values_desc.at(i);
    if (OB_ISNULL(value_desc)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("value desc is null", K(ret));
    } else if (OB_FAIL(ObDMLResolver::check_basic_column_generated(value_desc,
                                                                   &insert_stmt,
                                                                   is_generated_column))) {
          LOG_WARN("check basic column generated failed", K(ret));
    } else if (is_generated_column && schema::EXTERNAL_TABLE != insert_table->table_type_) {
      if (select_stmt.get_table_size() == 1 &&
          select_stmt.get_table_item(0) != NULL &&
          select_stmt.get_table_item(0)->is_values_table()) {
        //do nothing, already checked in advance.
      } else {
        ret = OB_NON_DEFAULT_VALUE_FOR_GENERATED_COLUMN;
        ColumnItem *orig_col_item = NULL;
        if (NULL != (orig_col_item = insert_stmt.get_column_item_by_id(
            insert_stmt.get_insert_table_info().table_id_, value_desc->get_column_id()))
            && orig_col_item->expr_ != NULL) {
          const ObString &column_name = orig_col_item->expr_->get_column_name();
          const ObString &table_name = orig_col_item->expr_->get_table_name();
          LOG_USER_ERROR(OB_NON_DEFAULT_VALUE_FOR_GENERATED_COLUMN,
                        column_name.length(), column_name.ptr(),
                        table_name.length(), table_name.ptr());
        }
      }
    } else if (!session_info_->is_in_user_scope() && value_desc->is_always_identity_column()) {
      // create table as select not need check here
      ret = OB_ERR_INSERT_INTO_GENERATED_ALWAYS_IDENTITY_COLUMN;
      LOG_USER_ERROR(OB_ERR_INSERT_INTO_GENERATED_ALWAYS_IDENTITY_COLUMN);
    }
    if (OB_FAIL(ret)) {
      //do nothing
    } else if (is_mock &&
               ref_stmt != NULL) {
      if (!ref_stmt->get_select_item(i).is_real_alias_) {
        ref_stmt->get_select_item(i).alias_name_ = value_desc->get_column_name();
        ref_stmt->get_select_item(i).is_real_alias_ = true;
      }
      if (!select_stmt.get_select_item(i).is_real_alias_) {
        select_stmt.get_select_item(i).alias_name_ = ref_stmt->get_select_item(i).alias_name_;
        select_stmt.get_select_item(i).is_real_alias_ = true;
      }
    }
  }
  return ret;
}

int ObInsertResolver::mock_values_column_ref(const ObColumnRefRawExpr *column_ref)
{
  int ret = OB_SUCCESS;
  ObInsertStmt *stmt = get_insert_stmt();
  ObColumnRefRawExpr *value_desc = NULL;
  ObColumnRefRawExpr *base_column_ref = const_cast<ObColumnRefRawExpr*>(column_ref);
  if (OB_ISNULL(column_ref) || OB_ISNULL(stmt) || OB_ISNULL(params_.expr_factory_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(column_ref), K(stmt), KP_(params_.expr_factory));
  } else {
    bool found_column = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < stmt->get_values_desc().count(); ++i) {
      if (OB_ISNULL(value_desc = stmt->get_values_desc().at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("value desc is null");
      } else if (column_ref->get_column_id() == value_desc->get_column_id()) {
        found_column = true;
        break;
      }
    }
    if (found_column) {
      //ignore generating new column
    } else if (OB_FAIL(params_.expr_factory_->create_raw_expr(T_REF_COLUMN, value_desc))) {
      LOG_WARN("create column ref raw expr failed", K(ret));
    } else if (OB_ISNULL(value_desc)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN(("value desc is null"));
    } else {
      if (OB_FAIL(ObTransformUtils::get_base_column(stmt, base_column_ref))) {
        // may be invalid updatable view, will be handled later.
        ret = OB_SUCCESS;
        base_column_ref = const_cast<ObColumnRefRawExpr*>(column_ref);
        LOG_WARN("failed to get base column", K(ret));
      }
      value_desc->set_result_type(column_ref->get_result_type());
      value_desc->set_result_flag(column_ref->get_result_flag());
      value_desc->set_column_flags(column_ref->get_column_flags());
      if (base_column_ref != column_ref) {
        if (base_column_ref->is_table_part_key_column()) {
          value_desc->set_table_part_key_column();
        }
        if (base_column_ref->is_table_part_key_org_column()) {
          value_desc->set_table_part_key_org_column();
        }
      }
      value_desc->set_dependant_expr(const_cast<ObRawExpr *>(column_ref->get_dependant_expr()));
      value_desc->set_ref_id(stmt->get_insert_table_info().table_id_, column_ref->get_column_id());
      value_desc->set_column_attr(ObString::make_string(OB_VALUES), column_ref->get_column_name());
      value_desc->set_udt_set_id(column_ref->get_udt_set_id());
      if (OB_SUCC(ret)) {
        if (OB_FAIL(value_desc->add_flag(IS_COLUMN))) {
          LOG_WARN("failed to add flag IS_COLUMN", K(ret));
        } else if (OB_FAIL(stmt->get_values_desc().push_back(value_desc))) {
          LOG_WARN("failed to push back values desc", K(ret), K(*value_desc));
        } else {/*do nothing*/}
      }
    }
  }
  return ret;
}


int ObInsertResolver::replace_column_to_default(ObRawExpr *&origin)
{
  int ret = OB_SUCCESS;
  ObInsertStmt *insert_stmt = get_insert_stmt();
  if (OB_ISNULL(origin)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("null pointer passed in", K(ret));
  } else {
    if (T_REF_COLUMN == origin->get_expr_type()) {
      ObColumnRefRawExpr *b_expr = static_cast<ObColumnRefRawExpr*>(origin);
      ColumnItem *column_item = NULL;
      ObDefaultValueUtils utils(insert_stmt, &params_, static_cast<ObDMLResolver*>(this));
      if (OB_ISNULL(column_item = insert_stmt->get_column_item_by_id(
                  insert_stmt->get_insert_table_info().table_id_, b_expr->get_column_id()))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to get column item", K(ret));
      } else if (OB_FAIL(insert_stmt->get_insert_table_info().column_in_values_vector_.push_back(column_item->expr_))) {
        LOG_WARN("fail to push back column expr", K(ret));
      } else if (OB_FAIL(utils.resolve_column_ref_in_insert(column_item, origin))) {
        LOG_WARN("fail to resolve column ref in insert", K(ret));
      }
    } else {
      int64_t N = origin->get_param_count();
      for (int64_t i = 0; OB_SUCC(ret) && i < N; ++i) {
        ObRawExpr *& cur_child = origin->get_param_expr(i);
        if (OB_FAIL(replace_column_to_default(cur_child))) {
          LOG_WARN("failed to replace child column_ref expr to default value", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObInsertResolver::check_insert_into_external_table()
{
  int ret = OB_SUCCESS;
  ObInsertStmt *insert_stmt = get_insert_stmt();
  TableItem *table = NULL;
  if (OB_ISNULL(insert_stmt) || insert_stmt->get_table_items().empty()
      || OB_ISNULL(table = insert_stmt->get_table_item(0))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid stmt", K(ret), K(insert_stmt));
  } else if (schema::EXTERNAL_TABLE != table->table_type_) {
    // do nothing
  } else if (!insert_stmt->value_from_select() || insert_stmt->is_replace()
             || insert_stmt->is_ignore() || insert_stmt->is_returning()
             || insert_stmt->is_insert_up()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not support insert into external table with values, replace, ignore, returning, update", K(ret));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "insert into external table with values, replace, ignore, returning, update");
  }
  return ret;
}

int ObInsertResolver::resolve_column_ref_expr(const ObQualifiedName &q_name, ObRawExpr *&real_ref_expr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(resolve_basic_column_ref(q_name, real_ref_expr))) {
    if ((OB_ERR_BAD_FIELD_ERROR == ret || OB_ERR_COLUMN_NOT_FOUND == ret) && sub_select_resolver_ != NULL) {
      //In insert into select stmt, insert clause and select clause belong to the same namespace
      //the on duplicate key update clause will see the column in select clause,
      //so need to add table reference to insert resolve column namespace checker
      //eg:
      int64_t idx = -1;
      ObString dummy_name;
      TableItem *view = NULL;
      ColumnItem *col_item = NULL;
      ObSelectStmt *sel_stmt = sub_select_resolver_->get_select_stmt();
      ObInsertStmt *insert_stmt = get_insert_stmt();
      if (OB_ISNULL(sel_stmt) || OB_ISNULL(insert_stmt)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("stmt is null", K(ret), K(sel_stmt), K(insert_stmt));
      } else if (OB_UNLIKELY(insert_stmt->get_from_item_size() != 1) ||
                 OB_ISNULL(view = insert_stmt->get_table_item_by_id(
                             insert_stmt->get_from_item(0).table_id_))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("does not find generated table item", K(ret), K(insert_stmt->get_from_items()));
      } else if (OB_FAIL(sub_select_resolver_->resolve_column_ref_expr(q_name, real_ref_expr))) {
        LOG_WARN("resolve column ref expr in sub select resolver failed", K(ret), K(q_name));
      } else if (!real_ref_expr->is_column_ref_expr()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("expr is expected to be column", K(ret));
      } else {
        for (idx = 0; OB_SUCC(ret) && idx < sel_stmt->get_select_item_size(); ++idx) {
          if (OB_ISNULL(sel_stmt->get_select_item(idx).expr_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("select expr is null", K(ret));
          } else if (sel_stmt->get_select_item(idx).expr_ == real_ref_expr) {
            break;
          }
        }
        if (idx == sel_stmt->get_select_item_size()) {
          SelectItem sel_item;
          sel_item.expr_ = real_ref_expr;
          sel_item.expr_name_ = static_cast<ObColumnRefRawExpr*>(real_ref_expr)->get_column_name();
          sel_item.alias_name_ = sel_item.expr_name_;
          if (OB_FAIL(sel_stmt->add_select_item(sel_item))) {
            LOG_WARN("failed to add select item", K(ret));
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(resolve_generated_table_column_item(
                        *view, dummy_name, col_item, insert_stmt, idx + OB_APP_MIN_COLUMN_ID))) {
            LOG_WARN("failed to resolve generated table column item", K(ret));
          } else if (OB_ISNULL(col_item)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("column item is null", K(ret), K(col_item));
          } else {
            real_ref_expr = col_item->expr_;
          }
        }
      }
    } else {
      // don't print this log as WARN,
      // as it takes lots of CPU cycles when inserting many rows
      // related issue: 
      LOG_TRACE("resolve basic column ref failed", K(ret), K(q_name));
    }
  }
  return ret;
}

int ObInsertResolver::resolve_order_item(const ParseNode &sort_node, OrderItem &order_item)
{
  UNUSED(sort_node);
  UNUSED(order_item);
  return OB_NOT_SUPPORTED;
}

int ObInsertResolver::resolve_insert_update_assignment(const ParseNode *node, ObInsertTableInfo& table_info)
{
  int ret = OB_SUCCESS;
  ObInsertStmt *insert_stmt = get_insert_stmt();
  if (OB_ISNULL(insert_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(insert_stmt), K(ret));
  } else if (insert_stmt->is_replace()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("REPLACE statement does not support ON DUPLICATE KEY UPDATE clause");
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "REPLACE statement ON DUPLICATE KEY UPDATE clause");
  } else if (FALSE_IT(is_resolve_insert_update_ = true)) {
    //do nothing
  } else if (OB_FAIL(ObDelUpdResolver::resolve_insert_update_assignment(node, table_info))) {
    LOG_WARN("resolve assignment error", K(ret));
  }
  return ret;
}

int ObInsertResolver::resolve_insert_constraint()
{
  int ret = OB_SUCCESS;
  ObInsertStmt *insert_stmt = NULL;
  TableItem *table_item = NULL;
  if (OB_ISNULL(insert_stmt = get_insert_stmt()) || OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(insert_stmt), K(session_info_), K(ret));
  } else if (session_info_->get_ddl_info().is_ddl() ||
             session_info_->get_ddl_info().is_dummy_ddl_for_inner_visibility() ||
             insert_stmt->has_instead_of_trigger()) {
    /*do nothing*/
  } else if (OB_ISNULL(table_item = insert_stmt->get_table_item_by_id(
                       insert_stmt->get_insert_table_info().table_id_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else {
    ObInsertTableInfo &table_info = insert_stmt->get_insert_table_info();
    if (OB_FAIL(resolve_view_check_exprs(table_item->table_id_, table_item, false, table_info.view_check_exprs_))) {
      LOG_WARN("resolve view check exprs failed", K(ret));
    } else if (OB_FAIL(resolve_check_constraints(table_item, table_info.check_constraint_exprs_))) {
      LOG_WARN("failed to resolve check constraints", K(ret));
    } else {
      // TODO @yibo remove view check exprs in log_del_upd
      for (uint64_t i = 0; OB_SUCC(ret) && i < table_info.view_check_exprs_.count(); ++i) {
        if (OB_FAIL(replace_column_ref_for_check_constraint(
                      table_info, table_info.view_check_exprs_.at(i)))) {
          LOG_WARN("fail to replace column ref for check constraint", K(ret),
                    K(i), K(table_info.view_check_exprs_.at(i)));
        }
      }
      const ObIArray<ObColumnRefRawExpr *> &table_columns = table_info.column_exprs_;
      for (uint64_t i = 0; OB_SUCC(ret) && i < table_columns.count(); ++i) {
        ObColumnRefRawExpr *table_column = table_columns.at(i);
        if (OB_ISNULL(table_column)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to resolve table desc as tbl column expr is null", K(ret));
        } else if (table_column->is_strict_json_column() > 0) {   // 0 not json  1 relax json  4 strict json
          for (uint64_t j = 0; OB_SUCC(ret) && j < table_info.values_desc_.count(); ++j) {
            ObColumnRefRawExpr *table_column_desc = table_info.values_desc_.at(j);
            if (table_column_desc->get_column_id() == table_column->get_column_id()) {
              table_column_desc->set_strict_json_column(table_column->is_strict_json_column());
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObInsertResolver::check_view_insertable()
{
  int ret = OB_SUCCESS;
  TableItem *table = NULL;
  ObInsertStmt *stmt = NULL;
  if (OB_ISNULL(stmt = get_insert_stmt())
      || stmt->get_table_items().empty()
      || OB_ISNULL(table = stmt->get_table_item(0))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("stmt is NULL or table item is NULL", K(ret));
  }
  // uv_check_basic already checked
  if (OB_SUCC(ret) && is_mysql_mode() &&
      (table->is_generated_table() || table->is_temp_table())) {
    // check duplicate base column and non column reference column.
    if (OB_SUCC(ret)) {
      bool has_dup_col = false;
      bool has_non_col_ref = false;
      if (OB_FAIL(ObResolverUtils::uv_check_dup_base_col(*table, has_dup_col, has_non_col_ref))) {
        LOG_WARN("check update view hash duplicate column failed", K(ret));
      } else {
        LOG_DEBUG("update view check duplicate column", K(has_dup_col), K(has_non_col_ref));
        if (has_dup_col || has_non_col_ref) {
          ret = OB_ERR_NON_INSERTABLE_TABLE;
        }
      }
    }

    // check all column belong to the insert base table, some columns already checked in
    // resolving assignment, but some columns are missing. e.g.:
    //     create table t1 (c1 int primary key, c2 int);
    //     create table t2 (c3 int primary key, c4 int);
    //     create view v as select * from t1, t2 where c1 = c3;
    //     insert into v (c2) values (1) on duplicate update c2 = c3 + c4;
    if (OB_SUCC(ret)) {
      bool log_error = true;
      ObSEArray<ObRawExpr *, 4> col_exprs;
      ObIArray<ObAssignment>& assignments = stmt->get_insert_table_info().assignments_;
      for (int64_t i = 0; OB_SUCC(ret) && i < assignments.count(); ++i) {
        col_exprs.reuse();
        if (OB_FAIL(ObRawExprUtils::extract_column_exprs(assignments.at(i).expr_, col_exprs))) {
          LOG_WARN("extract column failed", K(ret));
        } else {
          FOREACH_CNT_X(col, col_exprs, OB_SUCC(ret)) {
            if (OB_FAIL(check_same_base_table(*table,
                *static_cast<ObColumnRefRawExpr *>(*col), log_error))) {
              LOG_TRACE("check same base table fail", K(ret));
            }
          }
        }
      }
    }

    // subquery in select list is checked in uv_check_dup_base_col

    // check subquery in where
    if (OB_SUCC(ret)) {
      bool ref_update_table = false;
      if (OB_FAIL(ObResolverUtils::uv_check_where_subquery(*table, ref_update_table))) {
        LOG_WARN("update view check where condition failed", K(ret));
      } else {
        LOG_DEBUG("update view check", K(ref_update_table));
        ret = ref_update_table ? OB_ERR_NON_INSERTABLE_TABLE : OB_SUCCESS;
      }
    }

    // check join
    if (OB_SUCC(ret)) {
      bool insertable = true;
      if (OB_FAIL(ObResolverUtils::uv_mysql_insertable_join(*table,
          table->get_base_table_item().ref_id_, insertable))) {
        LOG_WARN("check insertable join failed", K(ret));
      } else {
        LOG_DEBUG("update view check join", K(insertable));
        ret = insertable ? ret : OB_ERR_NON_INSERTABLE_TABLE;
      }
    }

    if (ret == OB_ERR_NON_INSERTABLE_TABLE) {
      LOG_USER_ERROR(OB_ERR_NON_INSERTABLE_TABLE,
          table->get_table_name().length(), table->get_table_name().ptr());
    }
  }
  return ret;
}

/**
 *
 * Why do we do this cast?
 * The handling here is similar to that of set operations (e.g., union).
 * The main reason is to align the type of the select item in insert into select with the type of the columns in insert, mainly to be able to
 * form a PK plan for pdml. Different types cannot be used for PKEY.
 *
 * What impact does it have on the old plan? In the old plan scenario, there may be some performance degradation.
 *
 */


}  // namespace sql
}  // namespace oceanbase
