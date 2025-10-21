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
#include "ob_trigger_storage_cache_executor.h"
#include "share/ob_common_rpc_proxy.h"
#include "sql/resolver/cmd/ob_trigger_storage_cache_stmt.h"


namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
namespace sql
{
class ObExecContext;
int ObTriggerStorageCacheExecutor::execute(ObExecContext &ctx, ObTriggerStorageCacheStmt &stmt)
{
  int ret = OB_SUCCESS;
#ifdef OB_BUILD_SHARED_STORAGE
  ObTaskExecutorCtx *task_exec_ctx = NULL;
  ObCommonRpcProxy *common_proxy = NULL;
  if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(ctx))) {
    ret = OB_NOT_INIT;
    LOG_WARN("get task executor failed", K(ret));
  } else if (OB_ISNULL(common_proxy = task_exec_ctx->get_common_rpc())) {
    ret = OB_NOT_INIT;
    LOG_WARN("get common rpc proxy failed", K(ret));
  } else {
    ObArray<ObAddr> server_list;
    ObArray<ObUnit> tenant_units;
    ObUnitTableOperator unit_op;
    ObTriggerStorageCacheArg &arg = stmt.get_rpc_arg();
    uint64_t tenant_id = arg.get_tenant_id();
    if (OB_FAIL(unit_op.init(*GCTX.sql_proxy_))) {
      LOG_WARN("failed to init unit op", KR(ret));
    } else if (OB_FAIL(unit_op.get_units_by_tenant(tenant_id, tenant_units))) {
      LOG_WARN("failed to get tenant units", KR(ret), K(tenant_id));
    } else if (OB_UNLIKELY(0 == tenant_units.count())) {
      ret = OB_TENANT_NOT_EXIST;
      LOG_WARN("tenant not exist", KR(ret), K(tenant_id));
    } else {
      FOREACH_X(unit, tenant_units, OB_SUCC(ret)) {
        bool is_alive = false;
        if (OB_FAIL(SVR_TRACER.check_server_alive(unit->server_, is_alive))) {
          LOG_WARN("check_server_alive failed", KR(ret), K(unit->server_));
        } else if (is_alive) {
          if (has_exist_in_array(server_list, unit->server_)) {
            // server exist
          } else if (OB_FAIL(server_list.push_back(unit->server_))) {
            LOG_WARN("push_back failed", KR(ret), K(unit->server_));
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      const int64_t rpc_timeout_us = GCONF.rpc_timeout;
      obrpc::ObSrvRpcProxy *srv_rpc_proxy = nullptr;
      if (OB_ISNULL(srv_rpc_proxy = GCTX.srv_rpc_proxy_)) {
        ret = OB_ERR_SYS;
        LOG_WARN("srv rpc proxy is null", KR(ret), KP(srv_rpc_proxy));
      } else {
        int tmp_ret = OB_SUCCESS;
        FOREACH_X(server_addr, server_list, OB_SUCC(tmp_ret)) {
          if (ObTriggerStorageCacheArg::TRIGGER == arg.get_op()) {
            if (OB_TMP_FAIL(srv_rpc_proxy->to(*server_addr).timeout(rpc_timeout_us).trigger_storage_cache(arg))) {
              // If the server is timeout, just skip it
              if (ret == OB_SUCCESS) {
                ret = tmp_ret;
              }
              LOG_WARN("fail to send trigger storage cache rpc for server", KR(ret), KR(tmp_ret), K(arg), KPC(server_addr));
              tmp_ret = OB_SUCCESS;
            }
          }
        }
      }
    }
  }
#endif
  return ret;
}
} // namespace sql
} // namespace oceanbase
