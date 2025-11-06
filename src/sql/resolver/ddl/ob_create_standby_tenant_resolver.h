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

#ifndef OCEANBASE_CREATE_STANDBY_TENANT_RESOLVER_
#define OCEANBASE_CREATE_STANDBY_TENANT_RESOLVER_ 1

#include "sql/resolver/ddl/ob_create_tenant_stmt.h"
#include "sql/resolver/ddl/ob_ddl_resolver.h"

namespace oceanbase
{
namespace sql
{
class ObCreateStandbyTenantResolver: public ObDDLResolver
{
public:
  explicit ObCreateStandbyTenantResolver(ObResolverParams &params);
  virtual ~ObCreateStandbyTenantResolver();

  virtual int resolve(const ParseNode &parse_tree);
private:
  int resolve_log_restore_source_(ObCreateTenantStmt *stmt, ParseNode *log_restore_source_node) const;
  
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObCreateStandbyTenantResolver);
  // function members

private:
  // data members
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_CREATE_STANDBY_TENANT_RESOLVER_ */
