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

 #ifndef OCEANBASE_SQL_RESOLVER_MV_OB_SIMPLE_MJV_PRINTER_H_
 #define OCEANBASE_SQL_RESOLVER_MV_OB_SIMPLE_MJV_PRINTER_H_
 #include "sql/resolver/mv/ob_mv_printer.h"
 
 namespace oceanbase
 {
 namespace sql
 {
 
 class ObSimpleMJVPrinter : public ObMVPrinter
 {
 public:
   explicit ObSimpleMJVPrinter(ObMVPrinterCtx &ctx,
                               const share::schema::ObTableSchema &mv_schema,
                               const share::schema::ObTableSchema &mv_container_schema,
                               const ObSelectStmt &mv_def_stmt,
                               const MlogSchemaPairIArray &mlog_tables)
    : ObMVPrinter(ctx, mv_schema, mv_container_schema, mv_def_stmt, &mlog_tables)
     {}
 
   ~ObSimpleMJVPrinter() {}

private:
  virtual int gen_refresh_dmls(ObIArray<ObDMLStmt*> &dml_stmts) override;
  virtual int gen_real_time_view(ObSelectStmt *&sel_stmt) override;
  int gen_delete_for_simple_mjv(ObIArray<ObDMLStmt*> &dml_stmts);
  int gen_insert_into_select_for_simple_mjv(ObIArray<ObDMLStmt*> &dml_stmts);
  int gen_access_mv_data_for_simple_mjv(ObSelectStmt *&sel_stmt);
  int gen_access_delta_data_for_simple_mjv(ObIArray<ObSelectStmt*> &access_delta_stmts);
  int prepare_gen_access_delta_data_for_simple_mjv(ObSelectStmt *&base_delta_stmt,
                                                   ObIArray<ObRawExpr*> &semi_filters,
                                                   ObIArray<ObRawExpr*> &anti_filters);
  int gen_one_access_delta_data_for_simple_mjv(const ObSelectStmt &base_delta_stmt,
                                               const int64_t table_idx,
                                               const ObIArray<ObRawExpr*> &semi_filters,
                                               const ObIArray<ObRawExpr*> &anti_filters,
                                               ObSelectStmt *&sel_stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObSimpleMJVPrinter);
};

}
}

#endif
