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
#include "lib/oblog/ob_log_module.h"
#include "share/change_stream/ob_change_stream_mgr.h"
#include "share/rc/ob_tenant_base.h"
#include "lib/thread/thread_define.h"
#include "share/ob_thread_define.h"
#include "share/ob_global_stat_proxy.h"
#include "storage/tx/ob_ts_mgr.h"
#ifdef OB_BUILD_EMBED_MODE
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tx_storage/ob_ls_handle.h"
#include "logservice/ob_log_handler.h"
#endif

namespace oceanbase
{
namespace share
{

#ifdef OB_BUILD_EMBED_MODE
// True when Fetcher has consumed to log tail and has no in-flight tx (real CS catch-up).
static int embed_fetcher_tail_caught_up(ObCSFetcher &fetcher, bool &caught_up)
{
  int ret = OB_SUCCESS;
  caught_up = false;
  if (fetcher.get_current_processing_tx_count() > 0) {
    // still draining
  } else {
    palf::LSN max_lsn;
    storage::ObLSHandle tmp_handle;
    storage::ObLS *ls = nullptr;
    logservice::ObLogHandler *log_handler = nullptr;
    const share::ObLSID ls_id(share::ObLSID::SYS_LS_ID);
    if (OB_FAIL(MTL(storage::ObLSService*)->get_ls(ls_id, tmp_handle, storage::ObLSGetMod::LOG_MOD))
        || OB_ISNULL(ls = tmp_handle.get_ls())
        || OB_ISNULL(log_handler = ls->get_log_handler())
        || OB_FAIL(log_handler->get_max_lsn(max_lsn))) {
      LOG_WARN("embed_fetcher_tail_caught_up: get_max_lsn failed", KR(ret));
    } else {
      const palf::LSN cur_lsn = fetcher.get_current_lsn();
      caught_up = cur_lsn.is_valid() && max_lsn.is_valid() && cur_lsn >= max_lsn;
    }
  }
  return ret;
}
#endif

ObChangeStreamMgr::ObChangeStreamMgr()
  : is_inited_(false),
    fetcher_(),
    dispatcher_(),
    worker_()
{
}

ObChangeStreamMgr::~ObChangeStreamMgr()
{
  destroy();
}

int ObChangeStreamMgr::mtl_init(ObChangeStreamMgr *&mgr)
{
  int ret = common::OB_SUCCESS;
  if (OB_ISNULL(mgr)) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("ObChangeStreamMgr: mgr is null", K(ret));
  } else if (OB_FAIL(mgr->init())) {
    LOG_WARN("ObChangeStreamMgr init failed", KR(ret), "tenant_id", MTL_ID());
  } else {
    LOG_INFO("ObChangeStreamMgr mtl_init success", "tenant_id", MTL_ID(), KP(MTL_CTX()));
  }
  return ret;
}

int ObChangeStreamMgr::init()
{
  int ret = common::OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(fetcher_.init(&dispatcher_))) {
    LOG_WARN("ObChangeStreamMgr: fetcher init failed", K(ret));
  } else if (OB_FAIL(dispatcher_.init())) {
    LOG_WARN("ObChangeStreamMgr: dispatcher init failed", K(ret));
  } else if (OB_FAIL(worker_.init(GET_THREAD_NUM_BY_NPROCESSORS(1)))) {
    LOG_WARN("ObChangeStreamMgr: worker init failed", K(ret));
  } else {
    is_inited_ = true;
    FLOG_INFO("ObChangeStreamMgr init success (Fetcher/Dispatcher/Worker)", K(GET_THREAD_NUM_BY_NPROCESSORS(1)));
  }
  return ret;
}

int ObChangeStreamMgr::start()
{
  int ret = common::OB_SUCCESS;
  if (!is_inited_) {
    ret = common::OB_NOT_INIT;
    LOG_WARN("ObChangeStreamMgr is not inited", K(ret));
  } else {
    if (OB_FAIL(fetcher_.start())) {
      LOG_WARN("ObChangeStreamMgr: fetcher start failed", K(ret));
    } else if (OB_FAIL(dispatcher_.start())) {
      LOG_WARN("ObChangeStreamMgr: dispatcher start failed", K(ret));
    } else if (OB_FAIL(worker_.start())) {
      LOG_WARN("ObChangeStreamMgr: worker start failed", K(ret));
    } else {
      LOG_INFO("ObChangeStreamMgr start success (Fetcher/Dispatcher/Worker threads started)");
    }
  }
  return ret;
}

void ObChangeStreamMgr::stop()
{
  if (is_inited_) {
    fetcher_.stop();
    dispatcher_.stop();
    worker_.stop();
  }
}

void ObChangeStreamMgr::wait()
{
  if (is_inited_) {
    fetcher_.wait();
    dispatcher_.wait();
    worker_.wait();
  }
}

void ObChangeStreamMgr::destroy()
{
  if (is_inited_) {
    stop();
    wait();

    fetcher_.destroy();
    dispatcher_.destroy();
    worker_.destroy();
  }
  is_inited_ = false;
}

int ObChangeStreamMgr::wait_refresh_scn(
    common::ObISQLClient &sql_client,
    const uint64_t tenant_id,
    const int64_t timeout_us)
{
  int ret = common::OB_SUCCESS;
  SCN safe_visible_scn;
  SCN current_refresh_scn;
  const int64_t SLEEP_INTERVAL_US = 100 * 1000; // 100ms
  const int64_t abs_timeout_us = ObTimeUtility::current_time() + timeout_us;

  bool is_satisfied = false;
#ifdef OB_BUILD_EMBED_MODE
  // Pin target GTS at refresh start — avoid chasing a moving GTS in single-process embed.
  bool embed_target_pinned = false;
#endif
  while (OB_SUCC(ret) && !is_satisfied) {
    const int64_t now = ObTimeUtility::current_time();
    const int64_t remain_us = abs_timeout_us - now;
    if (remain_us <= 0) {
      ret = OB_TIMEOUT;
      LOG_WARN("wait change stream refresh scn timeout", KR(ret),
               K(tenant_id), K(safe_visible_scn), K(current_refresh_scn));
#ifdef OB_BUILD_EMBED_MODE
    } else if (!embed_target_pinned
               && OB_FAIL(OB_TS_MGR.get_ts_sync(tenant_id, remain_us, safe_visible_scn))) {
      LOG_WARN("get gts for safe visible scn failed", KR(ret), K(tenant_id));
    } else if (!embed_target_pinned) {
      embed_target_pinned = true;
#else
    } else if (OB_FAIL(OB_TS_MGR.get_ts_sync(tenant_id, remain_us, safe_visible_scn))) {
      LOG_WARN("get gts for safe visible scn failed", KR(ret), K(tenant_id));
#endif
    } else if (OB_FAIL(ObGlobalStatProxy::get_change_stream_refresh_scn(
                   sql_client, tenant_id, false, current_refresh_scn))) {
      LOG_WARN("get change stream refresh scn failed", KR(ret), K(tenant_id));
    } else if (current_refresh_scn >= safe_visible_scn) {
      is_satisfied = true;
      LOG_INFO("change stream refresh scn caught up",
               K(tenant_id), K(safe_visible_scn), K(current_refresh_scn));
    } else {
#ifdef OB_BUILD_EMBED_MODE
      // Embed: publish fetcher candidate or current GTS each round (CS may lag vs server).
      MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
      if (OB_SUCC(guard.switch_to(tenant_id, false))) {
        ObChangeStreamMgr *cs_mgr = MTL(ObChangeStreamMgr *);
        SCN candidate_scn;
        int64_t affected_rows = 0;
        bool tail_caught_up = false;
        if (OB_NOT_NULL(cs_mgr) && cs_mgr->is_inited()) {
          ObCSFetcher &fetcher = cs_mgr->get_fetcher();
          (void)embed_fetcher_tail_caught_up(fetcher, tail_caught_up);
          if (tail_caught_up
              && OB_SUCC(fetcher.get_refresh_scn(candidate_scn))
              && candidate_scn.is_valid()) {
            (void)ObGlobalStatProxy::advance_change_stream_refresh_scn(
                sql_client, tenant_id, candidate_scn, affected_rows);
          }
        }
      }
      if (OB_SUCC(ObGlobalStatProxy::get_change_stream_refresh_scn(
              sql_client, tenant_id, false, current_refresh_scn))
          && current_refresh_scn >= safe_visible_scn) {
        is_satisfied = true;
        LOG_INFO("change stream refresh scn caught up (embed)",
                 K(tenant_id), K(safe_visible_scn), K(current_refresh_scn));
      }
#endif
      if (!is_satisfied) {
        LOG_INFO("waiting for change stream refresh scn",
                 K(tenant_id), K(safe_visible_scn), K(current_refresh_scn));
        ob_usleep(SLEEP_INTERVAL_US);
      }
    }
  }
  return ret;
}

}  // namespace share
}  // namespace oceanbase
