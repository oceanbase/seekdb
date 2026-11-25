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

#ifndef OCEANBASE_SQL_OB_ALTER_TABLE_RESOLVER_
#define OCEANBASE_SQL_OB_ALTER_TABLE_RESOLVER_

#include "sql/resolver/ddl/ob_ddl_resolver.h"
#include "sql/resolver/ddl/ob_alter_table_stmt.h"

namespace oceanbase
{
namespace sql
{
typedef common::hash::ObPlacementHashSet<share::schema::ObColumnNameHashWrapper, common::OB_MAX_COLUMN_NUMBER> ObColumnNameSet;
/*
#define ADD_COLUMN_NOT_NULL       (1UL << 0)
#define MODIFY_COLUMN_NOT_NULL    (1UL << 1)
#define MODIFY_COLUMN_NULLABLE    (1UL << 2)
#define DROP_COLUMN               (1UL << 3)

#define HAS_ADD_COLUMN_NOT_NULL(action)       (ADD_COLUMN_NOT_NULL & (action))
#define HAS_MODIFY_COLUMN_NOT_NULL(action)    (MODIFY_COLUMN_NOT_NULL & (action))
#define HAS_MODIFY_COLUMN_NULLABLE(action)    (MODIFY_COLUMN_NULLABLE & (action))
#define HAS_DROP_COLUMN(action)               (DROP_COLUMN & (action))
*/

class ObAlterTableResolver : public ObDDLResolver
{
public:
  explicit ObAlterTableResolver(ObResolverParams &params);
  virtual ~ObAlterTableResolver();
  virtual int resolve(const peg::Node &parse_tree);
  int resolve_action_list(rust::Slice<const peg::Node> actions);
  int resolve_column_options(const peg::Node &node,
                             bool &is_modify_column_visibility,
                             bool &is_drop_column,
                             ObColumnNameSet &add_column_names_set,
                             ObReducedVisibleColSet &reduced_visible_col_set,
                             ObReducedVisibleColSet &drop_column_names_set,
                             bool &has_add_column,
                             bool &has_drop_column);
  int resolve_index_options(rust::Slice<const peg::Node> actions, const peg::Node &node,
                            bool &is_add_index);
  int resolve_partition_options(const peg::Node &node);
  int resolve_constraint_options(const peg::Node &node, const bool is_multi_actions);
  int resolve_convert_to_character(const peg::Node &node);
  int resolve_foreign_key_options(const peg::Node &node);
  int resolve_add_column(const peg::Node &node, ObColumnNameSet &resolve_add_column);
  int resolve_alter_column(const peg::Node &node);
  int resolve_change_column(const peg::Node &node);
  int check_modify_column_allowed(const share::schema::AlterColumnSchema &alter_column_schema,
                                  const share::schema::ObColumnSchemaV2 &origin_col_schema,
                                  const ObColumnResolveStat &stat);
  int check_alter_geo_column_allowed(const share::schema::AlterColumnSchema &alter_column_schema,
                                     const share::schema::ObColumnSchemaV2 &origin_col_schema);
  int resolve_modify_column(const peg::Node &node,
                            bool &is_modify_column_visibility,
                            ObReducedVisibleColSet &reduced_visible_col_set);
  int resolve_drop_column(const peg::Node &node,
                          ObReducedVisibleColSet &reduced_visible_col_set,
                          ObReducedVisibleColSet &drop_column_names_set);
  int resolve_rename_column(const peg::Node &node);
  int fill_table_option(const share::schema::ObTableSchema *table_schema);
  //save table option to AlterTableArg
  int set_table_options();
  ObAlterTableStmt *get_alter_table_stmt() { return static_cast<ObAlterTableStmt*>(stmt_); };
  int resolve_column_index(const common::ObString &column_name);

private:
  int check_dup_foreign_keys_exist(
      share::schema::ObSchemaGetterGuard *schema_guard,
      const obcall::ObCreateForeignKeyArg &foreign_key_arg);
  int resolve_alter_table_option_list(const peg::Node &node);
  int check_drop_column_is_partition_key(const ObTableSchema &table_schema, 
                                         const ObString &column_name);

  int set_column_collation(share::schema::AlterColumnSchema &alter_column_schema);
  int resolve_index_column_list(rust::Slice<const peg::Node> columns,
                                obcall::ObCreateIndexArg &index_arg,
                                const int64_t index_name_value,
                                ObIArray<ObString> &input_index_columns_name,
                                bool &cnt_func_index);

  int add_sort_column(const obcall::ObColumnSortItem &sort_column,
                      obcall::ObCreateIndexArg &index_arg);

  int process_timestamp_column(ObColumnResolveStat &stat, share::schema::AlterColumnSchema &alter_column_schema);
  int resolve_add_index(const peg::Node &node);
  int resolve_drop_index(const peg::Node &node);
  int resolve_drop_foreign_key(const peg::Node &node);
  int resolve_rename_index(const peg::Node &node);
  int resolve_index_visibility(const peg::Node &node);
  int resolve_index_parallel(const peg::Node &node);
  int check_is_drop_primary_key(rust::Slice<const peg::Node> actions, bool &is_drop_primary_key);
  int resolve_drop_primary(rust::Slice<const peg::Node> actions);
  int resolve_add_primary(const peg::Node &node);
  int resolve_alter_primary(rust::Slice<const peg::Node> actions, const peg::Node &node);
  int generate_index_arg(obcall::ObCreateIndexArg &index_arg, const bool is_unique_key);
  int resolve_alter_table_column_definition(share::schema::AlterColumnSchema &column,
                                            const peg::Node &node, ObColumnResolveStat &stat,
                                            bool &is_modify_column_visibility,
                                            ObIArray<ObColumnSchemaV2 *> &resolved_cols,
                                            const bool unused_flag = false,
                                            const bool allow_has_default = true);
  int resolve_alter_column_not_null(share::schema::AlterColumnSchema &column,
                                     const ObColumnSchemaV2 &ori_column);
  int get_table_schema_all_column_schema(ObIArray<ObColumnSchemaV2 *> &resolved_cols,
                                         ObTableSchema &table_schema);
  int resolve_add_partition(const peg::Node &node,
                            const share::schema::ObTableSchema &orig_table_schema);
  int inner_add_partition(rust::Slice<const peg::Node> elements,
                          const share::schema::ObPartitionFuncType part_type,
                          const share::schema::ObPartitionOption &part_option,
                          ObTableStmt *alter_stmt,
                          share::schema::ObTableSchema &alter_table_schema);
  int resolve_drop_partition(const peg::Node &node,
                             const share::schema::ObTableSchema &orig_table_schema);
  int resolve_add_subpartition(const peg::Node &node,
                              const share::schema::ObTableSchema &orig_table_schema);
  int resolve_drop_subpartition(const peg::Node &node,
                                const share::schema::ObTableSchema &orig_table_schema);
  int resolve_exchange_partition(const peg::Node &node,
                                 const share::schema::ObTableSchema &orig_table_schema);
  int resolve_exchange_partition_stmt(const peg::Node &node,
                                      const share::schema::ObTableSchema &orig_table_schema,
                                      const share::schema::ObTableSchema *&exchange_table_schema,
                                      common::ObString &orig_partition_name);
  int generate_exchange_partition_arg(const ObTableSchema &orig_table_schema,
                                      const ObTableSchema &exchange_table_schema,
                                      const ObString &orig_part_name,
                                      const ObPartitionLevel exchange_part_level);
  int check_subpart_name(const share::schema::ObPartition &partition,
                         const share::schema::ObSubPartition &subpartition);
  int resolve_add_constraint(const peg::Node &node);
  int resolve_drop_constraint(const peg::Node &node);
  int resolve_constraint_state(const peg::Node &node);
  int resolve_stored_partition_function(const share::schema::ObTableSchema &table_schema,
                           const bool is_sub_part,
                           common::ObIArray<ObRawExpr *> &expressions,
                           common::ObIArray<ObString> &keys);


  int resolve_pos_column(const peg::Node &definition, share::schema::AlterColumnSchema &alter_column_schema);
  int fill_column_schema_according_stat(const ObColumnResolveStat &stat,
                                        share::schema::AlterColumnSchema &alter_column_schema);
  int resolve_partitioned_partition(const peg::Node &node,
                                    const share::schema::ObTableSchema &origin_table_schema);
  virtual int get_table_schema_for_check(const share::schema::ObTableSchema *&table_schema) override;
  //int generate_new_schema(const share::schema::ObTableSchema &origin_table_schema,
  //                        share::schema::AlterTableSchema &new_table_schema);
  int is_exist_item_type(rust::Slice<const peg::Node> actions, const peg::NodeKind kind, bool &is_exist);
  int check_column_in_part_key(const share::schema::ObTableSchema &table_schema,
                               const share::schema::ObColumnSchemaV2 &src_col_schema,
                               const share::schema::ObColumnSchemaV2 &dst_col_schema);
  int alter_column_expr_in_part_expr(
      const share::schema::ObColumnSchemaV2 &src_col_schema,
      const share::schema::ObColumnSchemaV2 &dst_col_schema,
      ObRawExpr *part_expr);
  int check_alter_part_key_allowed(const share::schema::ObTableSchema &table_schema,
                                   const share::schema::ObColumnSchemaV2 &src_col_schema,
                                   const share::schema::ObColumnSchemaV2 &dst_col_schema,
                                   const bool is_part_key);
  int check_alter_column_schemas_valid(ObAlterTableStmt &stmt);

  int check_mysql_rename_column(const share::schema::AlterColumnSchema &alter_column_schema,
                                const share::schema::ObTableSchema &orign_table_schema,
                                ObAlterTableStmt &alter_table_stmt);
  int check_semistruct_encoding_type(const ObTableSchema &origin_schema, const ObTableSchema &alter_schema);

  const share::schema::ObTableSchema *table_schema_;
  const share::schema::ObTableSchema *index_schema_;

  IndexNameSet current_index_name_set_; // used for duplicate checking when allocating index names in cases where the index name is not specified
  int64_t add_or_modify_check_cst_times_;
  int64_t modify_constraint_times_;
  bool add_not_null_constraint_;
  int64_t add_column_cnt_;
  DISALLOW_COPY_AND_ASSIGN(ObAlterTableResolver);
};


} //end namespace sql
} //end namespace oceanbase

#endif // OCEANBASE_SQL_OB_ALTER_TABLE_RESOLVER_
