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

#ifndef OCEANBASE_SQL_OB_CREATE_OUTLINE_RESOLVER_H_
#define OCEANBASE_SQL_OB_CREATE_OUTLINE_RESOLVER_H_

#include "sql/resolver/ddl/ob_outline_resolver.h"
namespace oceanbase
{
namespace sql
{
class ObCreateOutlineStmt;
class ObCreateOutlineResolver : public ObOutlineResolver
{
public:
  explicit ObCreateOutlineResolver(ObResolverParams &params) : ObOutlineResolver(params) {}
  virtual ~ObCreateOutlineResolver() {}
  virtual int resolve(const ParseNode &parse_tree);
private:
  int resolve_sql_id(const ParseNode *node, ObCreateOutlineStmt &create_outline_stmt, bool is_format_sql);
  int resolve_hint(const ParseNode *node, ObCreateOutlineStmt &create_outline_stmt);
  static const int64_t OUTLINE_CHILD_COUNT = 6;
  DISALLOW_COPY_AND_ASSIGN(ObCreateOutlineResolver);
};
}//namespace sql
}//namespace oceanbase
#endif //OCEANBASE_SQL_OB_CREATE_OUTLINE_RESOLVER_H_
