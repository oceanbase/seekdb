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

#ifndef OCEANBASE_SQL_OB_OUTLINE_RESOLVER_H_
#define OCEANBASE_SQL_OB_OUTLINE_RESOLVER_H_

#include "sql/resolver/ddl/ob_ddl_resolver.h"
namespace oceanbase
{
namespace sql
{
class ObOutlineResolver : public ObDDLResolver
{
public:
  explicit ObOutlineResolver(ObResolverParams &params) : ObDDLResolver(params) {}
  virtual ~ObOutlineResolver() {}
protected:
  int resolve_outline_name(const ParseNode *node, common::ObString &db_name, common::ObString &outline_name);
  int resolve_outline_stmt(const ParseNode *node, ObStmt *&out_stmt, common::ObString &outline_sql);
  int resolve_outline_target(const ParseNode *target_node, common::ObString &outline_target);
  static const int64_t RELATION_FACTOR_CHILD_COUNT = 2;
private:
  DISALLOW_COPY_AND_ASSIGN(ObOutlineResolver);
};
}//namespace sql
}//namespace oceanbase
#endif //OCEANBASE_SQL_OB_OUTLINE_RESOLVER_H_
