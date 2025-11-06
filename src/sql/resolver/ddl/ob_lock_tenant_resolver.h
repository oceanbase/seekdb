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

#ifndef _OB_LOCK_TENANT_RESOLVER_H
#define _OB_LOCK_TENANT_RESOLVER_H 1
#include "sql/resolver/ddl/ob_lock_tenant_stmt.h"
#include "sql/resolver/ddl/ob_ddl_resolver.h"
#include "lib/container/ob_se_array.h"
namespace oceanbase
{
namespace sql
{
class ObLockTenantResolver: public ObDDLResolver
{
public:
  explicit ObLockTenantResolver(ObResolverParams &params);
  virtual ~ObLockTenantResolver();

  virtual int resolve(const ParseNode &parse_tree);
private:
  // types and constants
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObLockTenantResolver);

private:
  // data members
};

} // end namespace sql
} // end namespace oceanbase

#endif /* _OB_LOCK_TENANT_RESOLVER_H */
