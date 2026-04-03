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
#include "share/ob_share_util.h"
#include "share/ob_global_stat_proxy.h"
#include "storage/tx/ob_trans_service.h"
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

  SCN safe_visible_scn;
  SCN current_refresh_scn;
  const int64_t DEFAULT_TIMEOUT = GCONF.internal_sql_execute_timeout;
  const int64_t SLEEP_INTERVAL_US = 100 * 1000;
  ObSQLSessionInfo *session = OB_NOT_NULL(ctx.exec_ctx_) ? ctx.exec_ctx_->get_my_session() : nullptr;
  ObMySQLProxy *mysql_proxy = GCTX.sql_proxy_;

  if (OB_ISNULL(ctx.exec_ctx_) || OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", KR(ret));
  } else if (OB_ISNULL(mysql_proxy)) {
    ret = OB_NOT_INIT;
    LOG_WARN("mysql proxy is not inited", KR(ret));
  } else {
    transaction::ObTransService *txs = MTL(transaction::ObTransService *);
    if (OB_ISNULL(txs)) {
      ret = OB_ERR_SYS;
      LOG_WARN("trans service is null", KR(ret));
    } else {
      ObTimeoutCtx timeout_ctx;
      if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(timeout_ctx, DEFAULT_TIMEOUT))) {
        LOG_WARN("fail to set default timeout ctx", KR(ret));
      } else if (OB_FAIL(txs->get_read_snapshot_version(timeout_ctx.get_abs_timeout(), safe_visible_scn))) {
        LOG_WARN("get read snapshot version failed", KR(ret));
      } else {
        bool is_satisfied = false;
        while (OB_SUCC(ret) && !is_satisfied) {
          if (OB_FAIL(THIS_WORKER.check_status())) {
            LOG_WARN("check status failed", KR(ret));
          } else if (OB_FAIL(ObGlobalStatProxy::get_change_stream_refresh_scn(
                       *mysql_proxy, MTL_ID(), false, current_refresh_scn))) {
            LOG_WARN("get change stream refresh scn failed", KR(ret));
          } else if (current_refresh_scn >= safe_visible_scn) {
            LOG_INFO("current refresh scn is already greater than or equal to safe visible scn", K(current_refresh_scn), K(safe_visible_scn));
            is_satisfied = true;
            LOG_INFO("change stream refresh completed", K(safe_visible_scn), K(current_refresh_scn));
          } else {
            LOG_INFO("current refresh scn is less than safe visible scn, sleep and retry", K(current_refresh_scn), K(safe_visible_scn));
            ob_usleep(SLEEP_INTERVAL_US);
          }
        }
      }
    }
  }
  return ret;
}
