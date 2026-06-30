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
#include "share/rc/ob_module_provider.h"
#include "share/change_stream/ob_change_stream_mgr.h"
#include "share/rc/ob_tenant_base.h"
#include "lib/thread/thread_define.h"
#include "share/ob_thread_define.h"
#include "storage/tx/ob_ts_mgr.h"
#include <unistd.h>

#ifndef GET_THREAD_NUM_BY_NPROCESSORS
#define GET_THREAD_NUM_BY_NPROCESSORS(factor) \
  (sysconf(_SC_NPROCESSORS_ONLN) / (factor) > 0 ? sysconf(_SC_NPROCESSORS_ONLN) / (factor) : 1)
#endif

namespace oceanbase
{
namespace share
{

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
  } else {
    LOG_INFO("ObChangeStreamMgr mtl_init success",  KP(MTL_CTX()));
  }
  return ret;
}

int ObChangeStreamMgr::init()
{
  int ret = common::OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(fetcher_.init(&dispatcher_))) {
  } else if (OB_FAIL(dispatcher_.init())) {
  } else if (OB_FAIL(worker_.init(GET_THREAD_NUM_BY_NPROCESSORS(1)))) {
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
    } else if (OB_FAIL(dispatcher_.start())) {
    } else if (OB_FAIL(worker_.start())) {
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
    const int64_t timeout_us)
{
  UNUSED(sql_client);
  int ret = common::OB_SUCCESS;
  SCN safe_visible_scn;
  const int64_t SLEEP_INTERVAL_US = 100 * 1000; // 100ms
  const int64_t abs_timeout_us = ObTimeUtility::current_time() + timeout_us;

  if (OB_FAIL(OB_TS_MGR.get_ts_sync(abs_timeout_us - ObTimeUtility::current_time(),
                                     safe_visible_scn))) {
  } else {
    ObChangeStreamMgr *mgr = share::g_mp->change_stream_mgr();
    bool is_satisfied = false;
    while (OB_SUCC(ret) && !is_satisfied) {
      SCN current_refresh_scn;
      const int64_t now = ObTimeUtility::current_time();
      ObCSDispatcher *dispatcher = (OB_NOT_NULL(mgr) ? &mgr->dispatcher_ : nullptr);
      if (now >= abs_timeout_us) {
        ret = OB_TIMEOUT;
        LOG_WARN("wait change stream refresh scn timeout", KR(ret),
                 K(safe_visible_scn), K(current_refresh_scn));
      } else if (OB_ISNULL(mgr) || !mgr->is_inited()) {
        ret = OB_NOT_INIT;
        LOG_WARN("change stream mgr is not inited", KR(ret), KP(mgr));
      } else if (OB_FAIL(current_refresh_scn.convert_for_tx(
                     dispatcher->get_refresh_scn()))) {
      } else if (current_refresh_scn >= safe_visible_scn) {
        is_satisfied = true;
        LOG_INFO("change stream refresh scn caught up",
                 K(safe_visible_scn), K(current_refresh_scn));
      } else {
        LOG_INFO("waiting for change stream refresh scn",
                 K(safe_visible_scn), K(current_refresh_scn));
        ob_usleep(SLEEP_INTERVAL_US);
      }
    }
  }
  return ret;
}

}  // namespace share
}  // namespace oceanbase
