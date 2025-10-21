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

#ifndef OCEANBASE_SQL_RESOLVER_MV_OB_OUTER_JOIN_MJV_PRINTER_H_
#define OCEANBASE_SQL_RESOLVER_MV_OB_OUTER_JOIN_MJV_PRINTER_H_
#include "sql/resolver/mv/ob_mv_printer.h"

namespace oceanbase
{
namespace sql
{

class ObOuterJoinMJVPrinter : public ObMVPrinter
{
public:
  explicit ObOuterJoinMJVPrinter(ObMVPrinterCtx &ctx,
                                 const share::schema::ObTableSchema &mv_schema,
                                 const share::schema::ObTableSchema &mv_container_schema,
                                 const ObSelectStmt &mv_def_stmt,
                                 const MlogSchemaPairIArray &mlog_tables)
    : ObMVPrinter(ctx, mv_schema, mv_container_schema, mv_def_stmt, &mlog_tables)
      {}

  ~ObOuterJoinMJVPrinter() {}

private:
  virtual int gen_refresh_dmls(ObIArray<ObDMLStmt*> &dml_stmts) override;
  virtual int gen_real_time_view(ObSelectStmt *&sel_stmt) override;
  int gen_delta_pre_table_views();
  int gen_one_delta_pre_table_view(const TableItem *ori_table,
                                   ObSelectStmt *&view_stmt,
                                   const bool is_delta_view);
  int gen_refresh_dmls_for_table(const TableItem *table,
                                 const JoinedTable *upper_table,
                                 ObSqlBitSet<> &refreshed_table_idxs,
                                 ObIArray<ObDMLStmt*> &dml_stmts);
  int update_table_idx_array(const int64_t delta_table_idx,
                             const JoinedTable *upper_table,
                             ObSqlBitSet<> &refreshed_table_idxs);
  int gen_refresh_dmls_for_inner_join(const TableItem *delta_table,
                                      const int64_t delta_table_idx,
                                      const ObSqlBitSet<> &refreshed_table_idxs,
                                      ObIArray<ObDMLStmt*> &dml_stmts);
  int gen_refresh_dmls_for_left_join(const TableItem *delta_table,
                                     const int64_t delta_table_idx,
                                     const JoinedTable *upper_table,
                                     const ObSqlBitSet<> &refreshed_table_idxs,
                                     ObIArray<ObDMLStmt*> &dml_stmts);
  int gen_delete_for_inner_join(const TableItem *delta_table,
                                ObIArray<ObDMLStmt*> &dml_stmts);
  int gen_insert_for_outer_join_mjv(const int64_t delta_table_idx,
                                    const ObSqlBitSet<> &refreshed_table_idxs,
                                    const JoinedTable *upper_table,
                                    const bool is_outer_join,
                                    ObIArray<ObDMLStmt*> &dml_stmts);
  int gen_insert_select_stmt(const int64_t delta_table_idx,
                             const ObSqlBitSet<> &refreshed_table_idxs,
                             const JoinedTable *upper_table,
                             const bool is_outer_join,
                             ObSelectStmt *&sel_stmt);
  int gen_tables_for_insert_select_stmt(const int64_t delta_table_idx,
                                        const ObSqlBitSet<> &refreshed_table_idxs,
                                        ObSelectStmt *sel_stmt);
  int gen_delete_for_left_join(const TableItem *delta_table,
                               const int64_t delta_table_idx,
                               const JoinedTable *upper_table,
                               const ObSqlBitSet<> &refreshed_table_idxs,
                               const bool is_first_delete,
                               ObIArray<ObDMLStmt*> &dml_stmts);
  int gen_select_for_left_join_first_delete(const TableItem *delta_table,
                                            const int64_t delta_table_idx,
                                            const JoinedTable *upper_table,
                                            const ObSqlBitSet<> &refreshed_table_idxs,
                                            ObSelectStmt *&cond_sel_stmt);
  int get_left_table_rowkey_exprs(const int64_t delta_table_idx,
                                  const TableItem *outer_table,
                                  const ObIArray<ObRawExpr*> &join_conditions,
                                  const ObSqlBitSet<> &except_table_idxs,
                                  ObIArray<int64_t> &other_join_table_idxs,
                                  ObIArray<ObRawExpr*> &other_join_table_rowkeys,
                                  ObIArray<ObRawExpr*> &all_other_table_rowkeys);
  int gen_mv_stat_winfunc_expr(ObRawExpr *pk_expr,
                               const ObIArray<ObRawExpr*> &part_exprs,
                               ObSelectStmt *sel_stmt);
  int gen_update_for_left_join(const TableItem *delta_table,
                               const int64_t delta_table_idx,
                               const ObSqlBitSet<> &refreshed_table_idxs,
                               ObIArray<ObDMLStmt*> &dml_stmts);
  int add_set_null_to_upd_stmt(const ObRelIds &set_null_tables,
                               const TableItem *mv_table,
                               ObIArray<ObAssignment> &assignments);
  int gen_select_for_left_join_second_delete(const TableItem *delta_table,
                                             const int64_t delta_table_idx,
                                             const JoinedTable *upper_table,
                                             const ObSqlBitSet<> &refreshed_table_idxs,
                                             ObSelectStmt *&cond_sel_stmt);

private:
  ObSEArray<ObSelectStmt*, 8, common::ModulePageAllocator, true> all_delta_table_views_;
  ObSEArray<ObSelectStmt*, 8, common::ModulePageAllocator, true> all_pre_table_views_;
  ObSEArray<ObSqlBitSet<>, 8, common::ModulePageAllocator, true> right_table_idxs_;

  DISALLOW_COPY_AND_ASSIGN(ObOuterJoinMJVPrinter);
};

}
}

#endif
