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
#include "sql/resolver/dml/ob_view_table_resolver.h"
#include "share/catalog/ob_catalog_utils.h"
namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{
int ObViewTableResolver::do_resolve_set_query(const ParseNode &parse_tree,
                                              ObSelectStmt *&child_stmt,
                                              const bool is_left_child) /*default false*/
{
  int ret = OB_SUCCESS;
  child_stmt = NULL;
  ObViewTableResolver child_resolver(params_, view_db_name_, view_name_);

  child_resolver.set_current_level(current_level_);
  child_resolver.set_current_view_level(current_view_level_);
  child_resolver.set_parent_namespace_resolver(parent_namespace_resolver_);
  child_resolver.set_current_view_item(current_view_item);
  child_resolver.set_parent_view_resolver(parent_view_resolver_);
  child_resolver.set_calc_found_rows(is_left_child && has_calc_found_rows_);
  child_resolver.set_is_top_stmt(is_top_stmt());
  
  if (OB_FAIL(add_cte_table_to_children(child_resolver))) {
    LOG_WARN("failed to add cte table to children", K(ret));
  } else if (OB_FAIL(child_resolver.resolve_child_stmt(parse_tree))) {
    LOG_WARN("failed to resolve child stmt", K(ret));
  } else if (OB_ISNULL(child_stmt = child_resolver.get_child_stmt())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get null child stmt", K(ret));
  }
  return ret;
}

int ObViewTableResolver::expand_view(TableItem &view_item)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("session info is null");
  } else if (OB_FAIL(check_view_circular_reference(view_item))) {
    LOG_WARN("check view circular reference failed", K(ret));
  } else {
    // expand view as subquery which use view name as alias
    const ObTableSchema *view_schema = NULL;
    share::schema::ObSchemaGetterGuard *schema_guard = NULL;
    uint64_t database_id = OB_INVALID_ID;
    ObString old_database_name;
    uint64_t old_database_id = session_info_->get_database_id();
    ObSwitchCatalogHelper switch_catalog_helper;
    if (OB_ISNULL(schema_checker_)
        || OB_ISNULL(schema_guard = schema_checker_->get_schema_guard())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null", K(ret));
    } else if (OB_FAIL(schema_guard->get_database_id(session_info_->get_effective_tenant_id(),
                                                     view_item.database_name_,
                                                     database_id))) {
      LOG_WARN("failed to get database id", K(ret));
    } else if (OB_FAIL(schema_checker_->get_table_schema(session_info_->get_effective_tenant_id(),
                                                  view_item.ref_id_,
                                                  view_schema))) {
      LOG_WARN("get table schema failed", K(view_item));
    } else if (OB_FAIL(ob_write_string(*allocator_,
                                       session_info_->get_database_name(),
                                       old_database_name))) {
      LOG_WARN("failed to write string", K(ret));
    } else {
      ObViewTableResolver view_resolver(params_, view_db_name_, view_name_);
      view_resolver.set_current_level(current_level_);
      view_resolver.set_current_view_level(current_view_level_ + 1);
      view_resolver.set_view_ref_id(view_item.ref_id_);
      view_resolver.set_current_view_item(view_item);
      view_resolver.set_parent_view_resolver(this);
      view_resolver.set_parent_namespace_resolver(parent_namespace_resolver_);
      if (session_info_->is_in_external_catalog()
          && OB_FAIL(session_info_->set_internal_catalog_db(&switch_catalog_helper))) {
        LOG_WARN("failed to set catalog", K(ret));
      }
      if (OB_SUCC(ret) && OB_FAIL(do_expand_view(view_item, view_resolver))) {
        LOG_WARN("do expand view failed", K(ret));
      }
      if (switch_catalog_helper.is_set()) {
        if (OB_SUCCESS != (tmp_ret = switch_catalog_helper.restore())) {
          ret = OB_SUCCESS == ret ? tmp_ret : ret;
          LOG_WARN("failed to reset catalog", K(ret), K(tmp_ret));
        }
      }
    }
  }
  return ret;
}

int ObViewTableResolver::check_view_circular_reference(const TableItem &view_item)
{
  int ret = OB_SUCCESS;
  // Check logic: keep going up, layer by layer to determine if view_item is the same (db_name && tbl_name)
  // -- is_view_stmt() : determine if the current stmt is expanded from a view
  // -- get_view_item() : expand to the view (TableItem) of the current stmt
  // -- get_view_upper_scope_stmt() : view subquery's upper_scope_stmt
  // If there are mutual references, MySQL's behavior is to error on the top-level view, therefore
  // exist_circular_reference = true when it will also continue to loop, for example:
  // v3 references v2, while v1<->v2 reference each other, select * from v3 results in an error for v3
  ObViewTableResolver *cur_resolver = this;
  if (OB_UNLIKELY(! view_db_name_.empty() && ! view_name_.empty()
                  && 0 == view_db_name_.compare(view_item.database_name_)
                  && 0 == view_name_.compare(view_item.table_name_))) {
    ret = OB_ERR_VIEW_RECURSIVE;
    LOG_USER_ERROR(OB_ERR_VIEW_RECURSIVE, view_db_name_.length(), view_db_name_.ptr(),
                    view_name_.length(), view_name_.ptr());
  } else {
    // The original detection logic has a problem, for the example at the beginning, an error should not be reported when creating v3, or rather, the situation where v1 and v2 reference each other should not occur
    // but should error when create or replace v1/v2 causes v1 and v2 to reference each other.
    // Although now we have added this detection logic, checking that v does not appear after expanding the definition when creating view v can avoid mutual references,
    // But the original detection logic should also be retained. If a view with loops was created before the upgrade, selecting from this view will result in an error below.
    do {
      if (OB_UNLIKELY(view_item.ref_id_ == cur_resolver->current_view_item.ref_id_)) {
        ret = OB_ERR_VIEW_RECURSIVE;
        const ObString &db_name = cur_resolver->current_view_item.database_name_;
        const ObString &tbl_name = cur_resolver->current_view_item.table_name_;
        LOG_USER_ERROR(OB_ERR_VIEW_RECURSIVE, db_name.length(), db_name.ptr(),
                       tbl_name.length(), tbl_name.ptr());
      } else {
        cur_resolver = cur_resolver->parent_view_resolver_;
      }
    } while(OB_SUCC(ret) && cur_resolver != NULL);
  }
  return ret;
}

int ObViewTableResolver::resolve_generate_table(const ParseNode &table_node, const ParseNode *alias_node, TableItem *&table_item)
{
  int ret = OB_SUCCESS;
  ObViewTableResolver view_table_resolver(params_, view_db_name_, view_name_);
  // from subquery and current query belong to the same level, therefore current level and current remain consistent
  view_table_resolver.set_current_level(current_level_);
  view_table_resolver.set_current_view_level(current_view_level_);
  view_table_resolver.set_parent_namespace_resolver(parent_namespace_resolver_);
  view_table_resolver.set_parent_view_resolver(parent_view_resolver_);
  view_table_resolver.set_current_view_item(current_view_item);
  if (OB_FAIL(view_table_resolver.set_cte_ctx(cte_ctx_, true, true))) {
    LOG_WARN("set cte ctx to child resolver failed", K(ret));
  } else if (OB_FAIL(add_cte_table_to_children(view_table_resolver))) {
    LOG_WARN("add CTE table to children failed", K(ret));
  } else if (OB_FAIL(do_resolve_generate_table(table_node, alias_node, view_table_resolver, table_item))) {
    LOG_WARN("do resolve generate table failed", K(ret));
  }
  return ret;
}
// use_sys_tenant flag indicates whether to obtain schema as a system tenant
int ObViewTableResolver::check_need_use_sys_tenant(bool &use_sys_tenant) const
{
  int ret = OB_SUCCESS;
  // If the current tenant is already the system tenant, then ignore
  const ObTableSchema *table_schema = NULL;
  if (OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("session info is null");
  } else if (OB_SYS_TENANT_ID == session_info_->get_effective_tenant_id()) {
    use_sys_tenant = false;
  } else {
    use_sys_tenant = true;
  }
  // If the current stmt is not expanded from a system view, then ignore
  if (OB_SUCC(ret) && use_sys_tenant) {
    if (OB_FAIL(schema_checker_->get_table_schema(session_info_->get_effective_tenant_id(), current_view_item.ref_id_, table_schema))) {
      LOG_WARN("fail to get table_schema", K(ret));
    } else if (NULL == table_schema) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("table_schema should not be NULL", K(ret));
    } else if (!table_schema->is_sys_view()) {
      use_sys_tenant = false;
    }
  }

  return ret;
}

int ObViewTableResolver::check_in_sysview(bool &in_sysview) const
{
  int ret = OB_SUCCESS;
  in_sysview = is_sys_view(current_view_item.ref_id_);
  return ret;
}

// construct select item from select_expr
int ObViewTableResolver::set_select_item(SelectItem &select_item, bool is_auto_gen)
{
  int ret = OB_SUCCESS;
  ObCollationType cs_type = CS_TYPE_INVALID;
  ObSelectStmt *select_stmt = get_select_stmt();

  if (OB_ISNULL(select_stmt) || OB_ISNULL(session_info_) || OB_ISNULL(select_item.expr_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("select stmt is null", K_(session_info), K(select_stmt), K_(select_item.expr));
  } else if (is_create_view_ && !select_item.is_real_alias_) {
    if (OB_FAIL(ObSelectResolver::set_select_item(select_item, is_auto_gen))) {
      LOG_WARN("set select item failed", K(ret));
    }
  } else if (OB_FAIL(session_info_->get_collation_connection(cs_type))) {
    LOG_WARN("fail to get collation_connection", K(ret));
  } else if (select_item.is_real_alias_
             && OB_FAIL(ObSQLUtils::check_column_name(cs_type, select_item.alias_name_, true))) {
    // Only check real alias here,
    // auto generated alias will be checked in ObSelectResolver::check_auto_gen_column_names().
    LOG_WARN("fail to make field name", K(ret));
  } else if (OB_FAIL(select_stmt->add_select_item(select_item))) {
    LOG_WARN("add select item to select stmt failed", K(ret));
  }
  return ret;
}

int ObViewTableResolver::resolve_subquery_info(const ObIArray<ObSubQueryInfo> &subquery_info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("session info is null");
  } else if (current_level_ + 1 >= OB_MAX_SUBQUERY_LAYER_NUM && subquery_info.count() > 0) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "too many levels of subqueries");
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < subquery_info.count(); i++) {
    const ObSubQueryInfo &info = subquery_info.at(i);
    ObViewTableResolver subquery_resolver(params_, view_db_name_, view_name_);
    subquery_resolver.set_current_level(current_level_ + 1);
    subquery_resolver.set_current_view_level(current_view_level_);
    subquery_resolver.set_parent_namespace_resolver(this);
    subquery_resolver.set_parent_view_resolver(parent_view_resolver_);
    subquery_resolver.set_current_view_item(current_view_item);
    subquery_resolver.set_in_exists_subquery(info.parents_expr_info_.has_member(IS_EXISTS));
    set_query_ref_exec_params(info.ref_expr_ == NULL ? NULL : &info.ref_expr_->get_exec_params());
    if (OB_FAIL(add_cte_table_to_children(subquery_resolver))) {
      LOG_WARN("add CTE table to children failed", K(ret));
    } else if (is_only_full_group_by_on(session_info_->get_sql_mode())) {
      subquery_resolver.set_parent_aggr_level(info.parents_expr_info_.has_member(IS_AGG) ?
          current_level_ : parent_aggr_level_);
    }
    if (OB_SUCC(ret) && OB_FAIL(do_resolve_subquery_info(info, subquery_resolver))) {
      LOG_WARN("do resolve subquery info failed", K(ret));
    }
    set_query_ref_exec_params(NULL);
  }
  return ret;
}
}  // namespace sql
}  // namespace oceanbase
