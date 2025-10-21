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

#ifndef SRC_SQL_RESOLVER_DDL_OB_ANALYZE_STMT_RESOLVER_H_
#define SRC_SQL_RESOLVER_DDL_OB_ANALYZE_STMT_RESOLVER_H_

#include "ob_ddl_resolver.h"

namespace oceanbase
{
namespace sql
{
class ObAnalyzeStmt;
class ObAnalyzeTableInfo;

class ObAnalyzeStmtResolver: public ObDDLResolver
{
  static const int64_t DEFAULT_SAMPLE_ROWCOUNT_PER_BUCKET = 512;
public:
  ObAnalyzeStmtResolver(ObResolverParams &params);
  virtual ~ObAnalyzeStmtResolver();
  virtual int resolve(const ParseNode &parse_tree);
private:
  int resolve_oracle_analyze(const ParseNode &parse_node,
                             ObAnalyzeStmt &analyze_stmt);
  int resolve_mysql_update_histogram(const ParseNode &parse_node,
                                     ObAnalyzeStmt &analyze_stmt);
  int resolve_mysql_delete_histogram(const ParseNode &parse_node,
                                     ObAnalyzeStmt &analyze_stmt);
  int resolve_mysql_column_bucket_info(const ParseNode *column_node,
                                       const int64_t bucket_number,
                                       ObAnalyzeStmt &analyze_stmt);
  int resolve_table_info(const ParseNode *table_node,
                         ObAnalyzeStmt &analyze_stmt);
  int recursive_resolve_table_info(const ParseNode *table_list_node,
                                  ObAnalyzeStmt &analyze_stmt);
  int resolve_partition_info(const ParseNode *part_node,
                             ObAnalyzeStmt &analyze_stmt);
  int inner_resolve_partition_info(const ParseNode *part_node,
                                  const uint64_t tenant_id,
                                  ObAnalyzeTableInfo &table_info);
  int resolve_statistic_info(const ParseNode *statistic_node,
                             ObAnalyzeStmt &analyze_stmt);
  int resolve_for_clause_info(const ParseNode *for_clause_node,
                              ObAnalyzeStmt &analyze_stmt);
  int resolve_for_clause_element(const ParseNode *for_clause_node,
                                 const bool is_hist_subpart,
                                 ObAnalyzeStmt &analyze_stmt);
  int resolve_sample_clause_info(const ParseNode *sample_clause_node,
                                 ObAnalyzeStmt &analyze_stmt);


  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObAnalyzeStmtResolver);
};

} /* namespace sql */
} /* namespace oceanbase */

#endif /* SRC_SQL_RESOLVER_DDL_OB_ANALYZE_STMT_RESOLVER_H_ */
