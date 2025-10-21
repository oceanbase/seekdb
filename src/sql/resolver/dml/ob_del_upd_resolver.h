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

#ifndef SRC_SQL_RESOLVER_DML_OB_DEL_UPD_RESOLVER_H_
#define SRC_SQL_RESOLVER_DML_OB_DEL_UPD_RESOLVER_H_

#include "sql/resolver/dml/ob_dml_resolver.h"
#include "sql/resolver/dml/ob_del_upd_stmt.h"

namespace oceanbase
{
namespace sql
{

// all assignments of one table
struct ObTableAssignment
{
  ObTableAssignment()
      : table_id_(OB_INVALID_ID),
        assignments_()
  {
  }
  static int expand_expr(ObRawExprFactory &expr_factory,
                         const common::ObIArray<ObAssignment> &assigns, 
                         ObRawExpr *&expr);
  uint64_t table_id_;
  common::ObSEArray<ObAssignment, 4> assignments_;
  TO_STRING_KV(K_(table_id),
               N_ASSIGN, assignments_);
};

class ObDelUpdResolver: public ObDMLResolver
{
public:
  ObDelUpdResolver(ObResolverParams &params);
  virtual ~ObDelUpdResolver();

  ObDelUpdStmt *get_del_upd_stmt() { return static_cast<ObDelUpdStmt*>(stmt_); }

  //set is json constraint type is strict or relax
  const static uint8_t IS_JSON_CONSTRAINT_RELAX = 1;
  const static uint8_t IS_JSON_CONSTRAINT_STRICT = 4;
  inline bool is_resolve_insert_update() { return is_resolve_insert_update_;}
  int recursive_search_sequence_expr(const ObRawExpr *default_expr);
protected:

  int resolve_assignments(const ParseNode &parse_node,
                          common::ObIArray<ObTableAssignment> &table_assigns,
                          ObStmtScope scope);

  int resolve_column_and_values(const ParseNode &assign_list,
                                ObIArray<ObColumnRefRawExpr *> &target_list,
                                ObIArray<ObRawExpr *> &value_list);
  // Try add remove_const expr for const expr of select item.
  // Refer to comment of ObExprRemoveConst to see why we need this expr.
  int try_add_remove_const_epxr(ObSelectStmt &stmt);

  int resolve_assign_columns(const ParseNode &assign_target,
                             ObIArray<ObColumnRefRawExpr *> &column_list);

  int resolve_additional_assignments(common::ObIArray<ObTableAssignment> &table_assigns,
                                     const ObStmtScope scope);
  int generate_wrapper_expr_for_assignemnts(common::ObIArray<ObAssignment> &assigns,
                                            bool has_before_trigger);

  virtual int add_assignment(common::ObIArray<ObTableAssignment> &table_assigns,
                             const TableItem *table_item,
                             const ColumnItem *col_item,
                             ObAssignment &assign);
  int check_need_assignment(const common::ObIArray<ObAssignment> &assigns,
                            uint64_t table_id,
                            bool before_update_row_trigger_exist,
                            const share::schema::ObColumnSchemaV2 &column,
                            bool &need_assign);

  int set_base_table_for_updatable_view(TableItem &table_item,
                                        const ObColumnRefRawExpr &col_ref,
                                        const bool log_error = true);
  int set_base_table_for_view(TableItem &table_item,
                              const bool log_error = true);
  int check_same_base_table(const TableItem &table_item,
                            const ObColumnRefRawExpr &col_ref,
                            const bool log_error = true);
  // for update view, add all columns to select item.
  int add_all_column_to_updatable_view(ObDMLStmt &stmt,
                                       const TableItem &table_item,
                                       const bool &has_need_fired_tg_on_view = false);

  virtual int mock_values_column_ref(const ObColumnRefRawExpr *column_ref)
  {
    UNUSED(column_ref);
    return common::OB_SUCCESS;
  }

  virtual int mock_values_column_ref(const ObColumnRefRawExpr *column_ref,
                                     ObInsertTableInfo &table_info)
  {
    UNUSED(column_ref);
    UNUSED(table_info);
    return common::OB_SUCCESS;
  }

  // add for error logging
  int resolve_err_log_table(const ParseNode *node);
  int check_err_log_table(ObString &table_name, ObString &database_name);
  int resolve_err_log_reject(const ParseNode *node);
  int check_err_log_support_type(ObObjType column_o_type);

  virtual int process_values_function(ObRawExpr *&expr);
  virtual int recursive_values_expr(ObRawExpr *&expr);

  bool need_all_columns(const share::schema::ObTableSchema &table_schema,
                        const int64_t binlog_row_image);

  int add_all_columns_to_stmt(const TableItem &table_item,
                              common::ObIArray<ObColumnRefRawExpr*> &column_exprs);
  int add_all_columns_to_stmt_for_trigger(const TableItem &table_item,
                                          common::ObIArray<ObColumnRefRawExpr*> &column_exprs);
  int add_all_rowkey_columns_to_stmt(const TableItem &table_item,
                                     common::ObIArray<ObColumnRefRawExpr*> &column_exprs);
  int add_index_related_columns_to_stmt(const TableItem &table_item,
                                        const uint64_t column_id,
                                        common::ObIArray<ObColumnRefRawExpr*> &column_exprs);
  int add_all_index_rowkey_to_stmt(const TableItem &table_item,
                                   common::ObIArray<ObColumnRefRawExpr*> &column_exprs);
  int add_all_index_rowkey_to_stmt(const TableItem &table_item,
                                   const share::schema::ObTableSchema *index_schema,
                                   common::ObIArray<ObColumnRefRawExpr*> &column_ids);

  int add_all_partition_key_columns_to_stmt(const TableItem &table_item,
                                             common::ObIArray<ObColumnRefRawExpr*> &column_ids,
                                             ObDMLStmt *stmt = NULL);
  // check the update view is key preserved

  int check_need_fired_trigger(const TableItem* table_item);

  int view_pullup_special_column_exprs();
  int view_pullup_part_exprs();
  int expand_record_to_columns(const ParseNode &record_node,
                                              ObIArray<ObRawExpr *> &value_list);
  bool is_fk_parent_table(const common::ObIArray<ObForeignKeyInfo> &foreign_key_infos, const uint64_t table_id);
  int resolve_check_constraints(const TableItem* table_item,
                                common::ObIArray<ObRawExpr*> &check_exprs);
  int resolve_view_check_exprs(uint64_t table_id,
                               const TableItem* table_item,
                               const bool cascaded,
                               common::ObIArray<ObRawExpr*> &check_exprs);
  int get_pullup_column_map(ObDMLStmt &stmt,
                            ObSelectStmt &sel_stmt,
                            uint64_t table_id,
                            ObIArray<ObRawExpr *> &view_columns,
                            ObIArray<ObRawExpr *> &base_columns);
  
  int view_pullup_column_ref_exprs_recursively(ObRawExpr *&expr,
                                               uint64_t view_table_id,
                                               uint64_t base_table_id,
                                               const ObDMLStmt *stmt);

  int generate_column_conv_function(ObInsertTableInfo &table_info);

  virtual int find_value_desc(ObInsertTableInfo &table_info, uint64_t column_id, ObRawExpr *&column_ref);

  int build_column_conv_function_with_value_desc(ObInsertTableInfo& table_info,
                                                 const int64_t idx,
                                                 ObRawExpr *column_ref);

  int build_column_conv_function_with_default_expr(ObInsertTableInfo& table_info, const int64_t idx);

  int generate_autoinc_params(ObInsertTableInfo &table_info);
  int get_value_row_size(uint64_t &count);

  int resolve_insert_columns(const ParseNode *node,
                             ObInsertTableInfo& table_info);
  int resolve_insert_values(const ParseNode *node,
                            ObInsertTableInfo& table_info);
  int check_column_value_pair(common::ObArray<ObRawExpr*> *value_row,
                              ObInsertTableInfo& table_info,
                              const int64_t row_index,
                              const uint64_t value_count,
                              bool& is_all_default);
  int build_row_for_empty_brackets(common::ObArray<ObRawExpr*> &value_row,
                                   ObInsertTableInfo& table_info);

  int check_vec_hnsw_index_vid_opt(const ObTableAssignment &ta,
                                   const ObTableSchema *table_schema,
                                   bool &is_vec_hnsw_index_vid_opt);
  int check_update_part_key(const ObTableAssignment &ta,
                            uint64_t ref_table_id,
                            bool &is_updated,
                            bool is_link = false);
  int check_heap_table_update(ObTableAssignment &tas);
  int get_part_key_ids(const int64_t table_id, common::ObIArray<uint64_t> &array);
  int build_hidden_pk_assignment(ObTableAssignment &ta,
                                 const TableItem *table_item,
                                 const ObTableSchema *table_schema);
  // replace generate column's dependent column with new inserted value (the column convert func).
  int replace_gen_col_dependent_col(ObInsertTableInfo& table_info);
  int replace_col_with_new_value(ObInsertTableInfo& table_info, ObRawExpr *&expr);
  int remove_dup_dep_cols_for_heap_table(ObIArray<ObColumnRefRawExpr*> &dep_cols,
                                         const ObIArray<ObColumnRefRawExpr*> &values_desc);
  int check_insert_column_duplicate(uint64_t column_id, bool &is_duplicate);
  common::hash::ObPlacementHashSet<uint64_t, 4229> &get_insert_column_ids()
  {
    return insert_column_ids_;
  }
  int add_select_items(ObSelectStmt &select_stmt, const ObIArray<SelectItem>& select_items);
  int add_select_list_for_set_stmt(ObSelectStmt &select_stmt);
  int add_all_lob_columns_to_stmt(const TableItem &table_item, ObIArray<ObColumnRefRawExpr*> &column_exprs);
  int check_update_vector_col_with_vector_index(const ObTableSchema *table_schema,
                                                ObSchemaGetterGuard *schema_guard,
                                                const common::ObIArray<ObAssignment> &assigns,
                                                bool &update_with_vector_index);
protected:
  int generate_insert_table_info(const TableItem &table_item,
                                 ObInsertTableInfo &table_info,
                                 bool add_column = true);
  virtual int resolve_insert_update_assignment(const ParseNode *node, ObInsertTableInfo& table_info);
  int add_relation_columns(ObIArray<ObTableAssignment> &table_assigns);
  virtual int replace_column_ref(common::ObArray<ObRawExpr*> *value_row,
                                 ObRawExpr *&expr,
                                 bool in_generated_column = false);
  int replace_column_ref_for_check_constraint(ObInsertTableInfo& table_info, ObRawExpr *&expr);
  int check_need_match_all_params(const common::ObIArray<ObColumnRefRawExpr*> &value_desc, bool &need_match);
  int build_autoinc_param(
      const uint64_t table_id,
      const ObTableSchema *table_schema,
      const ObColumnSchemaV2 *column_schema,
      const int64_t auto_increment_cache_size,
      AutoincParam &param);
  int resolve_json_partial_update_flag(ObIArray<ObTableAssignment> &table_assigns, ObStmtScope scope);
  int mark_json_partial_update_flag(const ObColumnRefRawExpr *ref_expr, ObRawExpr *expr, int depth, bool &allow_json_partial_update);
  int add_select_item_func(ObSelectStmt &select_stmt, ColumnItem &col);
  int select_items_is_pk(const ObSelectStmt& select_stmt, bool &has_pk);
  int is_external_table_partition_column(const TableItem &table_item,
                                         uint64_t column_id,
                                         bool &is_part_column);

private:
  common::hash::ObPlacementHashSet<uint64_t, 4229> insert_column_ids_;
  bool is_column_specify_;
  bool is_oracle_tmp_table_; // whether to create oracle's temporary table
  int64_t oracle_tmp_table_type_;
protected:
  bool is_resolve_insert_update_;
};

} /* namespace sql */
} /* namespace oceanbase */

#endif /* SRC_SQL_RESOLVER_DML_OB_DEL_UPD_RESOLVER_H_ */
