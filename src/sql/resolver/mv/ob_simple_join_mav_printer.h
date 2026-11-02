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

#ifndef OCEANBASE_SQL_RESOLVER_MV_OB_SIMPLE_JOIN_MAV_PRINTER_H_
#define OCEANBASE_SQL_RESOLVER_MV_OB_SIMPLE_JOIN_MAV_PRINTER_H_
#include "sql/resolver/mv/ob_simple_mav_printer.h"

namespace oceanbase
{
namespace sql
{

class ObSimpleJoinMAVPrinter : public ObSimpleMAVPrinter
{
public:
  explicit ObSimpleJoinMAVPrinter(ObMVPrinterCtx &ctx,
                                  const share::schema::ObTableSchema &mv_schema,
                                  const share::schema::ObTableSchema &mv_container_schema,
                                  const ObSelectStmt &mv_def_stmt,
                                  const MlogSchemaPairIArray &mlog_tables,
                                  const ObIArray<std::pair<ObAggFunRawExpr*, ObRawExpr*>> &expand_aggrs)
    : ObSimpleMAVPrinter(ctx, mv_schema, mv_container_schema, mv_def_stmt, mlog_tables, expand_aggrs)
    {}

  ~ObSimpleJoinMAVPrinter() {}

private:
  virtual int gen_refresh_dmls(ObIArray<ObDMLStmt*> &dml_stmts) override;
  virtual int gen_inner_delta_mav_for_mav(ObIArray<ObSelectStmt*> &inner_delta_mavs) override;
  int gen_update_insert_delete_for_simple_join_mav(ObIArray<ObDMLStmt*> &dml_stmts);
  int gen_inner_delta_mav_for_simple_join_mav(ObIArray<ObSelectStmt*> &inner_delta_mavs);
  int gen_inner_delta_mav_for_simple_join_mav(const int64_t inner_delta_no,
                                              const ObIArray<ObSelectStmt*> &all_delta_datas,
                                              const ObIArray<ObSelectStmt*> &all_pre_datas,
                                              ObSelectStmt *&inner_delta_mav);
  int construct_table_items_for_simple_join_mav_delta_data(const int64_t inner_delta_no,
                                                           const ObIArray<ObSelectStmt*> &all_delta_datas,
                                                           const ObIArray<ObSelectStmt*> &all_pre_datas,
                                                           ObSelectStmt *&stmt);
  int gen_delta_data_access_stmt(const TableItem &source_table, ObSelectStmt *&access_sel);
  int gen_pre_data_access_stmt(const TableItem &source_table, ObSelectStmt *&access_sel);
  int gen_unchanged_data_access_stmt(const TableItem &source_table, ObSelectStmt *&access_sel);
  int gen_deleted_data_access_stmt(const TableItem &source_table, ObSelectStmt *&access_sel);

private:
  DISALLOW_COPY_AND_ASSIGN(ObSimpleJoinMAVPrinter);
};

}
}

#endif
