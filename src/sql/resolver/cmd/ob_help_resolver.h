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

#ifndef OCEANBASE_SQL_RESOLVER_CMD_OB_HELP_RESOLVER_H
#define OCEANBASE_SQL_RESOLVER_CMD_OB_HELP_RESOLVER_H
#include "sql/resolver/dml/ob_select_resolver.h"
#include "sql/resolver/cmd/ob_help_stmt.h"
namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}
namespace sql
{
class ObHelpResolver : public ObDMLResolver
{
public:
  explicit ObHelpResolver(ObResolverParams &params)
      : ObDMLResolver(params),
      sql_proxy_(params.sql_proxy_)
      {}
  virtual ~ObHelpResolver() {}
  virtual int resolve(const ParseNode &parse_tree);
private:
  int search_topic(ObHelpStmt *help_stmt,const common::ObSqlString &sql, int &topic_count,
                   const common::ObString &source_category = common::ObString::make_string("None"));
  int search_category(ObHelpStmt *help_stmt, const common::ObSqlString &sql,
                      int &category_count, common::ObString &category_name,
                      const common::ObString &source_category = common::ObString::make_string("None"));
  int create_topic_sql(const common::ObString &like_pattern, common::ObSqlString &sql);
  int create_keyword_sql(const common::ObString &like_pattern, common::ObSqlString &sql);
  int create_category_sql(const common::ObString &like_pattern, common::ObSqlString &sql);
  int create_category_topic_sql(const common::ObString &like_pattern, common::ObSqlString &sql);
  int create_category_child_sql(const common::ObString &like_pattern, common::ObSqlString &sql);
private:
  common::ObMySQLProxy *sql_proxy_;
  DISALLOW_COPY_AND_ASSIGN(ObHelpResolver);
};
}//sql
}//oceanbase
#endif
