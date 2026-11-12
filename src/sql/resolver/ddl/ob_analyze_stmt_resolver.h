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
public:
  ObAnalyzeStmtResolver(ObResolverParams &params);
  virtual ~ObAnalyzeStmtResolver();
  virtual int resolve(const peg::Node &parse_tree);
private:
  int resolve_analyze_table(const peg::Node &parse_node,
                            ObAnalyzeStmt &analyze_stmt);
  int resolve_table_info(const peg::Node *table_node,
                         ObAnalyzeStmt &analyze_stmt);
  int resolve_partition_info(ObAnalyzeStmt &analyze_stmt);
  int inner_resolve_partition_info(     ObAnalyzeTableInfo &table_info);
  int resolve_default_column_info(ObAnalyzeStmt &analyze_stmt);


  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObAnalyzeStmtResolver);
};

} /* namespace sql */
} /* namespace oceanbase */

#endif /* SRC_SQL_RESOLVER_DDL_OB_ANALYZE_STMT_RESOLVER_H_ */
