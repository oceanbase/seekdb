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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_DML_OB_VIEW_TABLE_RESOLVER_H_
#define OCEANBASE_SRC_SQL_RESOLVER_DML_OB_VIEW_TABLE_RESOLVER_H_
#include "sql/resolver/dml/ob_select_resolver.h"
namespace oceanbase
{
namespace sql
{
struct ObViewStateGuard
{
  ObViewStateGuard(ObResolverParams &param) : params_(param)
  {
    ori_is_in_view_ = params_.is_in_view_;
    if (!params_.is_in_view_) {
      params_.is_in_view_ = true;
    }
  }
  ~ObViewStateGuard()
  {
    params_.is_in_view_ = ori_is_in_view_;
  }
  TO_STRING_KV(K_(ori_is_in_view));

  bool ori_is_in_view_;
  ObResolverParams &params_;
};

class ObViewTableResolver : public ObSelectResolver
{
public:
  ObViewTableResolver(ObResolverParams &params,
                      const ObString &view_db_name, const ObString &view_name)
    : ObSelectResolver(params),
      parent_view_resolver_(NULL),
      is_create_view_(false),
      materialized_(false),
      auto_name_id_(1),
      view_db_name_(view_db_name),
      view_name_(view_name),
      ori_is_in_sys_view_(false)
      {
        is_resolving_view_ = true;
        params_.is_from_create_view_ = params.is_from_create_view_;
        params_.is_from_create_table_ = params.is_from_create_table_;
        params_.is_specified_col_name_ = params.is_specified_col_name_;
      }
  virtual ~ObViewTableResolver()
  {
    params_.is_in_sys_view_ = ori_is_in_sys_view_;
  }

  void set_current_view_item(const TableItem &view_item)
  {
    current_view_item = view_item;
    ori_is_in_sys_view_ = params_.is_in_sys_view_;
    params_.is_in_sys_view_ = params_.is_in_sys_view_ || is_sys_view(current_view_item.ref_id_);
  }
  void set_parent_view_resolver(ObViewTableResolver *parent_view_resolver)
  { parent_view_resolver_ = parent_view_resolver; }
  int check_need_use_sys_tenant(bool &use_sys_tenant) const;
  virtual int check_in_sysview(bool &in_sysview) const override;
  void set_is_create_view(bool is_create_view) { is_create_view_ = is_create_view; }
  void set_materialized(bool materialized) { materialized_ = materialized; }
  bool get_materialized() { return materialized_; }
  void set_auto_name_id(uint64_t auto_name_id) { auto_name_id_ = auto_name_id; }
  uint64_t get_auto_name_id() const { return auto_name_id_; }

protected:
  virtual int do_resolve_set_query(const ParseNode &parse_tree,
                                   ObSelectStmt *&child_stmt,
                                   const bool is_left_child = false);
  virtual int expand_view(TableItem &view_item);
  virtual int resolve_subquery_info(const common::ObIArray<ObSubQueryInfo> &subquery_info);
  int check_view_circular_reference(const TableItem &view_item);
  virtual int resolve_generate_table(const ParseNode &table_node, const ParseNode *alias_node, TableItem *&table_item);
  virtual int set_select_item(SelectItem &select_item, bool is_auto_gen);
  virtual const ObString get_view_db_name() const override { return view_db_name_; }
  virtual const ObString get_view_name() const override { return view_name_; }

protected:
  // In the namespace resolution of the view, all subqueries must be resolved by ObViewTableResolver
  // current_view_item is used to record which view (user-created view, excluding generated table) the current namespace is expanded from
  TableItem current_view_item;
  // parent_view_resolver is used to record which view expanded the current namespace's view
  ObViewTableResolver *parent_view_resolver_;
  //ObViewTableResolver was called by create view stmt
  bool is_create_view_;
  bool materialized_;
  uint64_t auto_name_id_;
  const ObString view_db_name_;
  const ObString view_name_;
  bool ori_is_in_sys_view_;
};
}  // namespace sql
}  // namespace oceanbase
#endif /* OCEANBASE_SRC_SQL_RESOLVER_DML_OB_VIEW_TABLE_RESOLVER_H_ */
