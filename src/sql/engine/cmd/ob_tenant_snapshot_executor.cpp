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
#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/cmd/ob_tenant_snapshot_executor.h"
#include "sql/resolver/cmd/ob_tenant_snapshot_stmt.h"
#include "rootserver/tenant_snapshot/ob_tenant_snapshot_util.h"
#include "share/tenant_snapshot/ob_tenant_snapshot_table_operator.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
namespace sql
{

ERRSIM_POINT_DEF(ERRSIM_WAIT_SNAPSHOT_RESULT_ERROR);
int ObCreateTenantSnapshotExecutor::execute(ObExecContext &ctx, ObCreateTenantSnapshotStmt &stmt)
{
  int ret = OB_SUCCESS;
  const ObString &tenant_name = stmt.get_tenant_name();
  const ObString &tenant_snapshot_name = stmt.get_tenant_snapshot_name();
  uint64_t tenant_id = OB_INVALID_TENANT_ID;
  share::ObTenantSnapshotID tenant_snapshot_id;

  // TODO: support tenant snapshot in future
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("create tenant snapshot is not supported", KR(ret));
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "create tenant snapshot is");

  // if (OB_FAIL(rootserver::ObTenantSnapshotUtil::create_tenant_snapshot(tenant_name,
  //                                                                      tenant_snapshot_name,
  //                                                                      tenant_id,
  //                                                                      tenant_snapshot_id))) {
  //   LOG_WARN("create tenant snapshot failed", KR(ret), K(tenant_name), K(tenant_snapshot_name));
  //   if (OB_TENANT_SNAPSHOT_EXIST == ret) {
  //     LOG_USER_ERROR(OB_TENANT_SNAPSHOT_EXIST, tenant_snapshot_name.length(), tenant_snapshot_name.ptr());
  //   }
  // } else if (OB_UNLIKELY(ERRSIM_WAIT_SNAPSHOT_RESULT_ERROR)) {
  //   ret = ERRSIM_WAIT_SNAPSHOT_RESULT_ERROR;
  //   LOG_WARN("[ERRSIM CLONE] errsim wait snapshot creation finished", KR(ret));
  // } else if (OB_FAIL(wait_create_finish_(tenant_id, tenant_snapshot_id, ctx))) {
  //   LOG_WARN("wait create snapshot finish failed", KR(ret), K(tenant_id), K(tenant_snapshot_id));
  // }
  return ret;
}


int ObDropTenantSnapshotExecutor::execute(ObExecContext &ctx, ObDropTenantSnapshotStmt &stmt)
{
  int ret = OB_SUCCESS;
  const ObString &tenant_name = stmt.get_tenant_name();
  const ObString &tenant_snapshot_name = stmt.get_tenant_snapshot_name();

  // TODO: support tenant snapshot in future
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("drop tenant snapshot is not supported", KR(ret));
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "drop tenant snapshot is");

  // if (OB_FAIL(rootserver::ObTenantSnapshotUtil::drop_tenant_snapshot(tenant_name,
  //                                                                    tenant_snapshot_name))) {
  //   LOG_WARN("drop tenant snapshot failed", KR(ret), K(tenant_name), K(tenant_snapshot_name));
  //   if (OB_TENANT_SNAPSHOT_NOT_EXIST == ret) {
  //     LOG_USER_ERROR(OB_TENANT_SNAPSHOT_NOT_EXIST, tenant_snapshot_name.length(), tenant_snapshot_name.ptr());
  //   }
  // }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
