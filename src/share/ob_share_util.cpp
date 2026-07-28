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
#include "share/rc/ob_module_provider.h" // for share::g_mp
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/ob_global_stat_proxy.h" // for ObGlobalStatProxy
#include "share/schema/ob_schema_struct.h" // for ObServerRuntimeSchema
#include "share/ob_server_struct.h"
#include "share/io/ob_io_manager.h"  // OB_IO_MANAGER, previously hidden behind a removed include chain, make the dependency explicit
#include "share/config/ob_server_config.h" // GCONF (get_rs_default_timeout_ctx)

namespace oceanbase
{
using namespace common;
using namespace common::sqlclient;
using namespace share::schema;
namespace share
{

void ObIDGenerator::reset()
{
  inited_ = false;
  step_ = 0;
  start_id_ = common::OB_INVALID_ID;
  end_id_ = common::OB_INVALID_ID;
  current_id_ = common::OB_INVALID_ID;
}

int ObIDGenerator::init(
    const uint64_t step,
    const uint64_t start_id,
    const uint64_t end_id)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_UNLIKELY(start_id > end_id || 0 == step)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid start_id/end_id", KR(ret), K(start_id), K(end_id), K(step));
  } else {
    step_ = step;
    start_id_ = start_id;
    end_id_ = end_id;
    current_id_ = start_id - step_;
    inited_ = true;
  }
  return ret;
}

int ObIDGenerator::next(uint64_t &current_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("generator is not inited", KR(ret), KPC(this));
  } else if (current_id_ >= end_id_) {
    ret = OB_ITER_END;
  } else {
    current_id_ += step_;
    current_id = current_id_;
  }
  return ret;
}

int ObIDGenerator::get_start_id(uint64_t &start_id) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("generator is not inited", KR(ret), KPC(this));
  } else {
    start_id = start_id_;
  }
  return ret;
}

int ObIDGenerator::get_current_id(uint64_t &current_id) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("generator is not inited", KR(ret), KPC(this));
  } else {
    current_id = current_id_;
  }
  return ret;
}

int ObIDGenerator::get_end_id(uint64_t &end_id) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("generator is not inited", KR(ret), KPC(this));
  } else {
    end_id = end_id_;
  }
  return ret;
}

int ObIDGenerator::get_id_cnt(uint64_t &cnt) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("generator is not inited", KR(ret), KPC(this));
  } else if (OB_UNLIKELY(end_id_ < start_id_
             || step_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid start_id/end_id/step", KR(ret), KPC(this));
  } else {
    cnt = (end_id_ - start_id_) / step_ + 1;
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
    LOG_WARN("set timeout failed", KR(ret), K(abs_timeout_ts), K(ctx_timeout_ts),
        K(worker_timeout_ts), K(default_timeout));
  } else if (ctx.is_timeouted()) {
    ret = OB_TIMEOUT;
    LOG_WARN("timeouted", KR(ret), K(abs_timeout_ts), K(ctx_timeout_ts),
        K(worker_timeout_ts), K(default_timeout));
  } else {
    LOG_TRACE("set_default_timeout_ctx success", K(abs_timeout_ts),
        K(ctx_timeout_ts), K(worker_timeout_ts), K(default_timeout));
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
    LOG_WARN("fail to set default_timeout_ctx", KR(ret));
  }
  return ret;
}

int ObShareUtil::get_abs_timeout(const int64_t default_timeout, int64_t &abs_timeout)
{
  int ret = OB_SUCCESS;
  ObTimeoutCtx ctx;
  if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, default_timeout))) {
    LOG_WARN("fail to set default timeout ctx", KR(ret), K(default_timeout));
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
    LOG_WARN("fail to set default timeout ctx", KR(ret), K(default_timeout));
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
      LOG_WARN("execute sql failed", KR(ret), K(sql));
    } else if (NULL == (result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get sql result", KR(ret));
    } else if (OB_FAIL(result->next())) {
      LOG_WARN("fail to get next row", KR(ret));
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
  server_role = GCTX.server_role_;
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
    LOG_WARN("fail to execute get_server_role", KR(ret));
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
    LOG_WARN("fail to execute get_server_role", KR(ret));
  } else if (is_standby_role(server_role)) {
    is_standby = true;
  }
  return ret;
}
int ObShareUtil::get_server_role_state(ObServerRole &server_role)
{
  int ret = OB_SUCCESS;
  server_role.reset();
  server_role = GCTX.server_role_;
  return ret;
}

int ObShareUtil::check_if_server_role_state_is_primary(bool &is_primary)
{
  int ret = OB_SUCCESS;
  share::ObServerRole server_role;
  is_primary = false;
  if (OB_FAIL(get_server_role_state(server_role))) {
    LOG_WARN("fail to execute get_server_role_state", KR(ret));
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
    LOG_WARN("fail to execute get_server_role_state", KR(ret));
  } else if (server_role.is_standby()) {
    is_standby = true;
  }
  return ret;
}
int ObShareUtil::gen_default_server_runtime_schema(schema::ObServerRuntimeSchema &runtime_schema)
{
  int ret = OB_SUCCESS;
  runtime_schema.reset();
  int64_t schema_version = 0;
  if (OB_ISNULL(GCTX.config_) || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.config_), KP(GCTX.schema_service_));
  } else if (OB_FAIL(runtime_schema.set_runtime_name(OB_SERVER_RUNTIME_NAME))) {
    LOG_WARN("set_runtime_name failed", "runtime_name", OB_SERVER_RUNTIME_NAME, KR(ret));
  } else if (OB_FAIL(runtime_schema.set_comment("server runtime"))) {
    LOG_WARN("set_comment failed", "comment", "server runtime", KR(ret));
  } else {
    if (OB_ISNULL(GCTX.sql_proxy_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
    } else {
      ObGlobalStatProxy proxy(*GCTX.sql_proxy_);
      if (OB_FAIL(proxy.get_baseline_schema_version(schema_version))) {
        LOG_WARN("get_baseline_schema_version failed", KR(ret));
      } else if (-1 == schema_version) {
        // Bootstrap starts with schema version 1 before the global stat row is visible.
        LOG_INFO("use bootstrap schema version", KR(ret));
        schema_version = 1;
      }
    }
    if (OB_SUCC(ret)) {
      runtime_schema.set_schema_version(schema_version);
      runtime_schema.set_locked(false);
      runtime_schema.set_read_only(false);
      runtime_schema.set_in_recyclebin(false);
      runtime_schema.set_status(schema::ObServerRuntimeStatus::SERVER_RUNTIME_STATUS_NORMAL);
      // The runtime schema uses the fixed MySQL charset, collation, and name-case defaults.
    }
  }
  LOG_INFO("finish constructing server runtime schema", KR(ret), K(runtime_schema), K(schema_version));
  return ret;
}

int ObShareUtil::is_primary_server(bool &is_primary)
{
  int ret = OB_SUCCESS;
  is_primary = ObServerRole::PRIMARY_ROLE == GCTX.server_role_;
  return ret;
}

} //end namespace share
} //end namespace oceanbase
