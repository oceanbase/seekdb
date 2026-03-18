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

#define USING_LOG_PREFIX SHARE

#include "share/ob_tenant_memstore_info_operator.h"

#include "lib/string/ob_sql_string.h"
#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "src/share/inner_table/ob_inner_table_schema_constants.h"

namespace oceanbase
{
using namespace common;
using namespace common::sqlclient;
namespace share
{


int ObTenantMemstoreInfoOperator::get(
    const uint64_t tenant_id,
    const common::ObIArray<common::ObAddr> &unit_servers,
    ObIArray<TenantServerMemInfo> &mem_infos)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  ObSqlString unit_servers_str;
  if (OB_INVALID_ID == tenant_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tenant_ids is empty", K(tenant_id), K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < unit_servers.count(); ++i) {
      char svr_ip_str[common::MAX_IP_ADDR_LENGTH] = "";
      const common::ObAddr &unit_server = unit_servers.at(i);
      if (!unit_server.ip_to_string(svr_ip_str, static_cast<int32_t>(MAX_IP_ADDR_LENGTH))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to convert ip to string", K(ret));
      } else if (OB_FAIL(unit_servers_str.append_fmt(
              "%s(1 = 1)",
              (0 != i) ? " or " : ""))) {
        LOG_WARN("fail to append fmt", K(ret));
      } else {} // no more to do
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(sql.assign_fmt("SELECT active_span, "
        "memstore_used, freeze_trigger, memstore_limit FROM %s "
        "WHERE (%s)",
        OB_ALL_VIRTUAL_TENANT_MEMSTORE_INFO_TNAME, unit_servers_str.ptr()))) {
      LOG_WARN("assign_fmt failed", K(ret));
    } else {
      SMART_VAR(ObMySQLProxy::MySQLResult, res) {
        ObMySQLResult *result = NULL;
        if (OB_FAIL(proxy_.read(res, sql.ptr()))) {
          LOG_WARN("execute sql failed", K(sql), K(ret));
        } else if (NULL == (result = res.get_result())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("execute sql failed", K(sql), K(ret));
        } else {
          TenantServerMemInfo mem_info;
          while (OB_SUCC(ret)) {
            if (OB_FAIL(result->next())) {
              if (OB_ITER_END != ret) {
                LOG_WARN("result next failed", K(ret));
              } else {
                ret = OB_SUCCESS;
                break;
              }
            } else {
              mem_info.tenant_id_ = OB_SYS_TENANT_ID;
              EXTRACT_INT_FIELD_MYSQL(*result, "active_span",
                  mem_info.active_memstore_used_, int64_t);
              EXTRACT_INT_FIELD_MYSQL(*result, "memstore_used",
                  mem_info.total_memstore_used_, int64_t);
              EXTRACT_INT_FIELD_MYSQL(*result, "freeze_trigger",
                  mem_info.major_freeze_trigger_, int64_t);
              EXTRACT_INT_FIELD_MYSQL(*result, "memstore_limit",
                  mem_info.memstore_limit_, int64_t);
              if (OB_SUCC(ret)) {
                mem_info.server_ = GCTX.self_addr();
                if (OB_FAIL(mem_infos.push_back(mem_info))) {
                  LOG_WARN("push_back failed", K(ret));
                }
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

}//end namespace share
}//end namespace oceanbase

