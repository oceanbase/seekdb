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

 #ifndef OCEANBASE_SQL_RESOLVER_MV_OB_UNION_ALL_MV_PRINTER_H_
 #define OCEANBASE_SQL_RESOLVER_MV_OB_UNION_ALL_MV_PRINTER_H_
 #include "sql/resolver/mv/ob_mv_printer.h"
 
 namespace oceanbase
 {
 namespace sql
 {
 
 class ObUnionAllMVPrinter : public ObMVPrinter
 {
 public:
   explicit ObUnionAllMVPrinter(ObMVPrinterCtx &ctx,
                               const share::schema::ObTableSchema &mv_schema,
                               const share::schema::ObTableSchema &mv_container_schema,
                               const ObSelectStmt &mv_def_stmt,
                               const int64_t marker_idx,
                               const ObIArray<ObMVRefreshableType> &child_refresh_types,
                               const MlogSchemaPairIArray &mlog_tables,
                               const ObIArray<std::pair<ObAggFunRawExpr*, ObRawExpr*>> &expand_aggrs)
    : ObMVPrinter(ctx, mv_schema, mv_container_schema, mv_def_stmt, &mlog_tables),
      marker_idx_(marker_idx),
      child_refresh_types_(child_refresh_types),
      expand_aggrs_(expand_aggrs)
     {}

   ~ObUnionAllMVPrinter() {}
private:
  virtual int gen_refresh_dmls(ObIArray<ObDMLStmt*> &dml_stmts) override;
  virtual int gen_real_time_view(ObSelectStmt *&sel_stmt) override;
  int gen_child_refresh_dmls(const ObMVRefreshableType refresh_type,
                             const ObSelectStmt &child_sel_stmt,
                             ObIArray<ObDMLStmt*> &dml_stmts);
private:
  int64_t marker_idx_;
  const ObIArray<ObMVRefreshableType> &child_refresh_types_;
  const ObIArray<std::pair<ObAggFunRawExpr*, ObRawExpr*>> &expand_aggrs_;
  DISALLOW_COPY_AND_ASSIGN(ObUnionAllMVPrinter);
};

}
}

#endif
