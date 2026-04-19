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

#define USING_LOG_PREFIX PL
#include "ob_dbms_index_manager.h"
#include "share/change_stream/ob_change_stream_mgr.h"
#include "share/rc/ob_tenant_base.h"
#include "lib/mysqlclient/ob_mysql_proxy.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::pl;
using namespace oceanbase::sql;

int ObDBMSIndexManager::refresh(
    ObPLExecCtx &ctx,
    sql::ParamStore &params,
    common::ObObj &result)
{
  LOG_INFO("call dbms_index_manager.refresh");
  int ret = OB_SUCCESS;
  UNUSED(params);
  UNUSED(result);

  ObMySQLProxy *mysql_proxy = GCTX.sql_proxy_;
  const int64_t timeout_us = GCONF.internal_sql_execute_timeout;

  if (OB_ISNULL(ctx.exec_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("exec ctx is null", KR(ret));
  } else if (OB_ISNULL(mysql_proxy)) {
    ret = OB_NOT_INIT;
    LOG_WARN("mysql proxy is not inited", KR(ret));
  } else if (OB_FAIL(ObChangeStreamMgr::wait_refresh_scn(
                 *mysql_proxy, MTL_ID(), timeout_us))) {
    LOG_WARN("wait change stream refresh failed", KR(ret));
  }
  return ret;
}
