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

#ifndef _OB_TENANT_SNAPSHOT_RESOLVER_H
#define _OB_TENANT_SNAPSHOT_RESOLVER_H

#include "sql/resolver/cmd/ob_cmd_resolver.h"

namespace oceanbase
{
namespace sql
{
class ObCreateTenantSnapshotResolver : public ObCMDResolver
{
public:
  explicit ObCreateTenantSnapshotResolver(ObResolverParams &params);
  virtual ~ObCreateTenantSnapshotResolver();

  virtual int resolve(const ParseNode &parse_tree);
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateTenantSnapshotResolver);
};

class ObDropTenantSnapshotResolver : public ObCMDResolver
{
public:
  explicit ObDropTenantSnapshotResolver(ObResolverParams &params);
  virtual ~ObDropTenantSnapshotResolver();

  virtual int resolve(const ParseNode &parse_tree);
private:
  DISALLOW_COPY_AND_ASSIGN(ObDropTenantSnapshotResolver);
};

} // end namespace sql
} // end namespace oceanbase

#endif /*_OB_TENANT_SNAPSHOT_RESOLVER_H*/
