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
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/schema/ob_schema_struct.h"
#include "share/io/ob_io_manager.h"
#include "share/config/ob_server_config.h" // GCONF (get_rs_default_timeout_ctx)
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
using namespace common;
using namespace common::sqlclient;
using namespace share::schema;
namespace share
{

int ObShareUtil::get_server_ip(
    const ObAddr &self_addr,
    ObIAllocator &allocator,
    ObString &ip_string)
{
  int ret = OB_SUCCESS;
  char ip_buffer[OB_IP_STR_BUFF] = {'\0'};
  if (!self_addr.ip_to_string(ip_buffer, sizeof(ip_buffer))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("convert server IP to string failed", K(ret));
  } else if (OB_FAIL(ob_write_string(
                 allocator, ObString::make_string(ip_buffer), ip_string))) {
  } else if (ip_string.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("server IP is empty", K(ret));
  }
  return ret;
}

int ObShareUtil::set_default_timeout_ctx(ObTimeoutCtx &ctx, const int64_t default_timeout)
{
  int ret = OB_SUCCESS;
  int64_t abs_timeout_ts = OB_INVALID_TIMESTAMP;
  int64_t ctx_timeout_ts = ctx.get_abs_timeout();
  int64_t worker_timeout_ts = THIS_WORKER.get_timeout_ts();
  if (0 < ctx_timeout_ts) {
    //ctx is already been set, use it
    abs_timeout_ts = ctx_timeout_ts;
  } else if (INT64_MAX == worker_timeout_ts) {
    //if worker's timeout_ts not be set，set to default timeout
    abs_timeout_ts = ObTimeUtility::current_time() + default_timeout;
  } else if (0 < worker_timeout_ts) {
    //use worker's timeout if only it is valid
    abs_timeout_ts = worker_timeout_ts;
  } else {
    //worker's timeout_ts is invalid, set to default timeout
    abs_timeout_ts = ObTimeUtility::current_time() + default_timeout;
  }
  if (OB_FAIL(ctx.set_abs_timeout(abs_timeout_ts))) {
  } else if (ctx.is_timeouted()) {
    ret = OB_TIMEOUT;
    LOG_WARN("timeouted", KR(ret), K(abs_timeout_ts), K(ctx_timeout_ts),
        K(worker_timeout_ts), K(default_timeout));
  } else {
  }
  return ret;
}

int ObShareUtil::get_rs_default_timeout_ctx(ObTimeoutCtx &ctx)
{
  int ret = OB_SUCCESS;
  int64_t DEFAULT_TIMEOUT_US = GCONF.rpc_timeout; // default is 2s
#ifdef __APPLE__
  // On Mac, the system is significantly slower due to lack of O_DIRECT and software CRC.
  // Increase the default timeout to 10s to avoid bootstrap failure.
  DEFAULT_TIMEOUT_US = std::max(DEFAULT_TIMEOUT_US, 10000000LL);
#endif

  if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, DEFAULT_TIMEOUT_US))) {
  }
  return ret;
}

int ObShareUtil::get_abs_timeout(const int64_t default_timeout, int64_t &abs_timeout)
{
  int ret = OB_SUCCESS;
  ObTimeoutCtx ctx;
  if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, default_timeout))) {
  } else {
    abs_timeout = ctx.get_abs_timeout();
  }
  return ret;
}

int ObShareUtil::get_ctx_timeout(const int64_t default_timeout, int64_t &timeout)
{
  int ret = OB_SUCCESS;
  ObTimeoutCtx ctx;
  if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, default_timeout))) {
  } else {
    timeout = ctx.get_timeout();
  }
  return ret;
}

int ObShareUtil::fetch_current_data_version(
    common::ObISQLClient &client,
    uint64_t &data_version)
{
  int ret = OB_SUCCESS;
  data_version = DATA_CURRENT_VERSION;
  return ret;
}

int ObShareUtil::get_ora_rowscn(
    common::ObISQLClient &client,
    const ObSqlString &sql,
    SCN &ora_rowscn)
{
  int ret = OB_SUCCESS;
  uint64_t ora_rowscn_val = 0;
  ora_rowscn.set_invalid();
  SMART_VAR(ObMySQLProxy::MySQLResult, res) {
    ObMySQLResult *result = NULL;
    if (OB_FAIL(client.read(res, sql.ptr()))) {
    } else if (NULL == (result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get sql result", KR(ret));
    } else if (OB_FAIL(result->next())) {
    } else {
      EXTRACT_INT_FIELD_MYSQL(*result, "ORA_ROWSCN", ora_rowscn_val, int64_t);
      if (FAILEDx(ora_rowscn.convert_for_inner_table_field(ora_rowscn_val))) {
        LOG_WARN("fail to convert val to SCN", KR(ret), K(ora_rowscn_val));
      }
    }

    int tmp_ret = OB_SUCCESS;
    if (OB_FAIL(ret)) {
      //nothing todo
    } else if (OB_ITER_END != (tmp_ret = result->next())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get more row than one", KR(ret), KR(tmp_ret));
    }
  }
  return ret;
}

int ObShareUtil::get_server_role(ObServerRole::Role &server_role)
{
  int ret = OB_SUCCESS;
  server_role = share::server_role();
  if (OB_SUCC(ret) && OB_UNLIKELY(is_invalid_role(server_role))) {
    ret = OB_NEED_WAIT;
    LOG_WARN("server role is not ready, need wait", KR(ret), K(server_role));
  }
  return ret;
}

int ObShareUtil::check_if_server_role_is_primary(bool &is_primary)
{
  int ret = OB_SUCCESS;
  is_primary = false;
  ObServerRole::Role server_role;
  if (OB_FAIL(get_server_role(server_role))) {
  } else if (is_primary_role(server_role)) {
    is_primary = true;
  }
  return ret;
}

int ObShareUtil::check_if_server_role_is_standby(bool &is_standby)
{
  int ret = OB_SUCCESS;
  is_standby = false;
  ObServerRole::Role server_role;
  if (OB_FAIL(get_server_role(server_role))) {
  } else if (is_standby_role(server_role)) {
    is_standby = true;
  }
  return ret;
}
int ObShareUtil::get_server_role_state(ObServerRole &server_role)
{
  int ret = OB_SUCCESS;
  server_role.reset();
  server_role = share::server_role();
  return ret;
}

int ObShareUtil::check_if_server_role_state_is_primary(bool &is_primary)
{
  int ret = OB_SUCCESS;
  share::ObServerRole server_role;
  is_primary = false;
  if (OB_FAIL(get_server_role_state(server_role))) {
  } else if (server_role.is_primary()) {
    is_primary = true;
  }
  return ret;
}
int ObShareUtil::check_if_server_role_state_is_standby(bool &is_standby)
{
  int ret = OB_SUCCESS;
  share::ObServerRole server_role;
  is_standby = false;
  if (OB_FAIL(get_server_role_state(server_role))) {
  } else if (server_role.is_standby()) {
    is_standby = true;
  }
  return ret;
}

int ObShareUtil::gen_default_server_runtime_schema(
    common::ObISQLClient &sql_client,
    schema::ObServerRuntimeSchema &runtime_schema)
{
  int ret = OB_SUCCESS;
  UNUSED(sql_client);
  runtime_schema.reset();
  // The server runtime is a synthetic singleton that exists from the core
  // schema. baseline_schema_version is a lower bound for schema snapshots,
  // not the version of every schema object visible in those snapshots.
  const int64_t schema_version = OB_CORE_SCHEMA_VERSION;
  if (OB_FAIL(runtime_schema.set_runtime_name(OB_SERVER_RUNTIME_NAME))) {
  } else if (OB_FAIL(runtime_schema.set_comment("server runtime"))) {
  } else {
    runtime_schema.set_schema_version(schema_version);
    runtime_schema.set_locked(false);
    runtime_schema.set_read_only(false);
    runtime_schema.set_in_recyclebin(false);
    runtime_schema.set_status(schema::ObServerRuntimeStatus::SERVER_RUNTIME_STATUS_NORMAL);
    // The runtime schema uses the fixed MySQL charset, collation, and name-case defaults.
  }
  LOG_INFO("finish constructing server runtime schema", KR(ret), K(runtime_schema), K(schema_version));
  return ret;
}

int ObShareUtil::is_primary_server(bool &is_primary)
{
  int ret = OB_SUCCESS;
  is_primary = share::server_is_primary();
  return ret;
}

} //end namespace share
} //end namespace oceanbase
