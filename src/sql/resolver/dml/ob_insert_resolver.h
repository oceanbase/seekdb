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
// Copyright 2014 Alibaba Inc. All RightsReserved.
// Author:
// Normalizer:

#ifndef OCEANBASE_SQL_RESOLVER_DML_OB_INSERT_RESOLVER_H_
#define OCEANBASE_SQL_RESOLVER_DML_OB_INSERT_RESOLVER_H_
#include "lib/hash/ob_placement_hashset.h"
#include "sql/resolver/dml/ob_del_upd_resolver.h"
#include "sql/resolver/dml/ob_insert_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObSelectResolver;
class ObInsertResolver : public ObDelUpdResolver
{
public:
public:
  explicit ObInsertResolver(ObResolverParams &params);
  virtual ~ObInsertResolver();
  virtual int resolve(const peg::Node &query);
  const ObInsertStmt *get_insert_stmt() const { return static_cast<const ObInsertStmt*>(stmt_); }
  ObInsertStmt *get_insert_stmt()
  {
    return static_cast<ObInsertStmt*>(stmt_);
  }
  bool has_row_alias() const { return row_alias_source_ != nullptr; }
  bool is_row_alias_source(const peg::Node &query) const { return row_alias_source_ == &query; }
protected:
  int process_values_function(ObRawExpr *&expr) override;
  virtual int mock_values_column_ref(const ObColumnRefRawExpr *column_ref) override;
  virtual int resolve_order_item(const peg::Node &sort_node, OrderItem &order_item) override;
  virtual int resolve_column_ref_expr(const ObQualifiedName &q_name, ObRawExpr *&real_ref_expr) override;
private:
  int resolve_insert_clause(const peg::Node &query);
  int resolve_insert_field(const peg::Node &query, TableItem*& table_item);
  int resolve_insert_assign(rust::Slice<const peg::Node> assignments);
  int fill_insert_stmt_default_value();
  int check_and_fill_default_value(int64_t col_index, const ColumnItem *column);
  int check_and_build_default_expr(const ColumnItem *column, ObRawExpr* &expr);
  int fill_default_value_for_empty_brackets();
  int set_insert_columns();
  int set_insert_values();
  int replace_auto_increment_values();
  virtual int replace_column_ref(common::ObArray<ObRawExpr*> *value_row,
                                 ObRawExpr *&expr,
                                 bool in_generated_column = false);
  int set();
  int resolve_source(const peg::Node &query, TableItem *table_item);
  virtual int resolve_insert_update_assignment(rust::Slice<const peg::Node> assignments, ObInsertTableInfo& table_info) override;
  int replace_column_to_default(ObRawExpr *&origin);
  int resolve_insert_constraint();
  int check_view_insertable();
  int check_insert_select_field(ObInsertStmt &insert_stmt,
                                ObSelectStmt &select_stmt);

  int replace_column_with_mock_column(ObColumnRefRawExpr *gen_col,
                                      ObColumnRefRawExpr *value_desc);

  ObSelectResolver *&get_sub_select_resolver() {
    return sub_select_resolver_;
  }
  int add_column_conv_for_diagnosis(ObInsertStmt *insert_stmt,
                                    ObSelectStmt *select_stmt,
                                    TableItem* table_item);
  DISALLOW_COPY_AND_ASSIGN(ObInsertResolver);
private:
  int64_t row_count_;
  ObSelectResolver *sub_select_resolver_;
  bool autoinc_col_added_;
  const peg::Node *row_alias_source_;
};
}  // namespace sql
}  // namespace oceanbase
#endif /* OCEANBASE_SQL_RESOLVER_DML_OB_INSERT_RESOLVER_H_ */
