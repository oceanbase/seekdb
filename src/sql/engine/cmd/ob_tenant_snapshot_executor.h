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

#ifndef __OB_SQL_TENANT_SNAPSHOT_EXECUTOR_H__
#define __OB_SQL_TENANT_SNAPSHOT_EXECUTOR_H__

#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace share
{
class ObTenantSnapshotID;
}

namespace sql
{
class ObCreateTenantSnapshotStmt;
class ObDropTenantSnapshotStmt;

class ObCreateTenantSnapshotExecutor
{
public:
  ObCreateTenantSnapshotExecutor() {}
  virtual ~ObCreateTenantSnapshotExecutor() {}
  int execute(ObExecContext &ctx, ObCreateTenantSnapshotStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateTenantSnapshotExecutor);
};

class ObDropTenantSnapshotExecutor
{
public:
  ObDropTenantSnapshotExecutor() {}
  virtual ~ObDropTenantSnapshotExecutor() {}
  int execute(ObExecContext &ctx, ObDropTenantSnapshotStmt &stmt);
private:
  DISALLOW_COPY_AND_ASSIGN(ObDropTenantSnapshotExecutor);
};

} //end namespace sql
} //end namespace oceanbase

#endif //__OB_SQL_TENANT_SNAPSHOT_EXECUTOR_H__
