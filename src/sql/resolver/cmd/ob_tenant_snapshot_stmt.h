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

#ifndef OCEANBASE_SQL_OB_TENANT_SNAPSHOT_STMT_H_
#define OCEANBASE_SQL_OB_TENANT_SNAPSHOT_STMT_H_

#include "sql/resolver/cmd/ob_cmd_stmt.h"

namespace oceanbase
{
namespace sql
{

class ObCreateTenantSnapshotStmt : public ObCMDStmt
{
public:
  explicit ObCreateTenantSnapshotStmt(common::ObIAllocator *name_pool)
    : ObCMDStmt(name_pool, stmt::T_CREATE_TENANT_SNAPSHOT),
      tenant_name_(),
      tenant_snapshot_name_()
      {}
  ObCreateTenantSnapshotStmt()
    : ObCMDStmt(stmt::T_CREATE_TENANT_SNAPSHOT),
      tenant_name_(),
      tenant_snapshot_name_()
      {}
  virtual ~ObCreateTenantSnapshotStmt() {}
  const common::ObString get_tenant_name() const { return tenant_name_.str(); }
  const common::ObString get_tenant_snapshot_name() const { return tenant_snapshot_name_.str(); }

  int set_tenant_name(const common::ObString &tenant_name)
  {
    return tenant_name_.assign(tenant_name);
  }
  int set_tenant_snapshot_name(const common::ObString &tenant_snapshot_name)
  {
    return tenant_snapshot_name_.assign(tenant_snapshot_name);
  }
private:
  common::ObFixedLengthString<common::OB_MAX_TENANT_NAME_LENGTH + 1> tenant_name_;
  common::ObFixedLengthString<common::OB_MAX_TENANT_SNAPSHOT_NAME_LENGTH + 1> tenant_snapshot_name_;
  DISALLOW_COPY_AND_ASSIGN(ObCreateTenantSnapshotStmt);
};

class ObDropTenantSnapshotStmt : public ObCMDStmt
{
public:
  explicit ObDropTenantSnapshotStmt(common::ObIAllocator *name_pool)
    : ObCMDStmt(name_pool, stmt::T_DROP_TENANT_SNAPSHOT),
      tenant_name_(),
      tenant_snapshot_name_()
      {}
  ObDropTenantSnapshotStmt()
    : ObCMDStmt(stmt::T_DROP_TENANT_SNAPSHOT),
      tenant_name_(),
      tenant_snapshot_name_()
      {}
  virtual ~ObDropTenantSnapshotStmt() {}
  const common::ObString get_tenant_name() const { return tenant_name_.str(); }
  const common::ObString get_tenant_snapshot_name() const { return tenant_snapshot_name_.str(); }

  int set_tenant_name(const common::ObString &tenant_name)
  {
    return tenant_name_.assign(tenant_name);
  }
  int set_tenant_snapshot_name(const common::ObString &tenant_snapshot_name)
  {
    return tenant_snapshot_name_.assign(tenant_snapshot_name);
  }
private:
  common::ObFixedLengthString<common::OB_MAX_TENANT_NAME_LENGTH + 1> tenant_name_;
  common::ObFixedLengthString<common::OB_MAX_TENANT_SNAPSHOT_NAME_LENGTH + 1> tenant_snapshot_name_;
  DISALLOW_COPY_AND_ASSIGN(ObDropTenantSnapshotStmt);
};

} /* sql */
} /* oceanbase */

#endif //OCEANBASE_SQL_OB_TENANT_SNAPSHOT_STMT_H_
