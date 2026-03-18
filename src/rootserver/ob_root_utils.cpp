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

#define USING_LOG_PREFIX RS

#include "ob_root_utils.h"
#include "logservice/ob_log_service.h"

using namespace oceanbase::rootserver;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::common;
using namespace oceanbase::common::hash;
using namespace oceanbase::common::sqlclient;
using namespace oceanbase::obrpc;

int ObTenantUtils::get_tenant_ids(
    ObMultiVersionSchemaService *schema_service,
    ObIArray<uint64_t> &tenant_ids)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  if (OB_ISNULL(schema_service)) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema service not init", K(ret));
  } else if (OB_FAIL(schema_service->get_tenant_schema_guard(OB_SYS_TENANT_ID, schema_guard))) {
    LOG_WARN("get_schema_guard failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_tenant_ids(tenant_ids))) {
    LOG_WARN("get_tenant_ids failed", K(ret));
  }
  return ret;
}

int ObRootUtils::get_rs_default_timeout_ctx(ObTimeoutCtx &ctx)
{
  int ret = OB_SUCCESS;
  int64_t DEFAULT_TIMEOUT_US = GCONF.rpc_timeout; // default is 2s
#ifdef __APPLE__
  // On Mac, the system is significantly slower due to lack of O_DIRECT and software CRC.
  // Increase the default timeout to 10s to avoid bootstrap failure.
  DEFAULT_TIMEOUT_US = std::max(DEFAULT_TIMEOUT_US, 10000000LL);
#endif

  if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, DEFAULT_TIMEOUT_US))) {
    LOG_WARN("fail to set default_timeout_ctx", KR(ret));
  }
  return ret;
}

///////////////////////////////

ObClusterRole ObClusterInfoGetter::get_cluster_role_v2()
{
  ObClusterRole cluster_role = PRIMARY_CLUSTER;
  return cluster_role;
}

ObClusterRole ObClusterInfoGetter::get_cluster_role()
{
  ObClusterRole cluster_role = PRIMARY_CLUSTER;
  
  return cluster_role;
}

const char *oceanbase::rootserver::resource_type_to_str(const ObResourceType &t)
{
  const char* str = "UNKNOWN";
  if (RES_CPU == t) { str = "CPU"; }
  else if (RES_MEM == t) { str = "MEMORY"; }
  else if (RES_LOG_DISK == t) { str = "LOG_DISK"; }
  else if (RES_DATA_DISK == t) { str = "DATA_DISK"; }
  else { str = "NONE"; }
  return str;
}
