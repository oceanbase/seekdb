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

#ifndef OCEANBASE_SQL_RESOLVER_PREPARE_OB_EXECUTE_RESOLVER_
#define OCEANBASE_SQL_RESOLVER_PREPARE_OB_EXECUTE_RESOLVER_

#include "sql/resolver/ob_stmt_resolver.h"
#include "sql/resolver/prepare/ob_execute_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObExecuteResolver : public ObStmtResolver
{
public:
  explicit ObExecuteResolver(ObResolverParams &params) : ObStmtResolver(params) {}
  virtual ~ObExecuteResolver() {}

  virtual int resolve(const ParseNode &parse_tree);
  ObExecuteStmt *get_execute_stmt() { return static_cast<ObExecuteStmt*>(stmt_); }

private:

};

}
}
#endif /*OCEANBASE_SQL_RESOLVER_DML_OB_EXECUTE_RESOLVER_H_*/
