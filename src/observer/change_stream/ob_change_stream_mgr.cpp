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
#include "lib/utility/utility.h"
#include "share/rc/ob_server_runtime.h"
#include "observer/change_stream/ob_change_stream_mgr.h"
#include "storage/tx/ob_ts_mgr.h"

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

int ObChangeStreamMgr::server_module_init(
    ObChangeStreamMgr *&mgr,
    logservice::ObILogStorage &log_storage,
    schema::ObSchemaPublishSignal &schema_publish_signal,
    lib::IRunWrapper *run_wrapper)
{
  int ret = common::OB_SUCCESS;
  if (OB_ISNULL(mgr)) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("ObChangeStreamMgr: mgr is null", K(ret));
  } else if (OB_FAIL(
      mgr->init(log_storage, schema_publish_signal, run_wrapper))) {
  } else {
    LOG_INFO("ObChangeStreamMgr server_module_init success");
  }
  return ret;
}

int ObChangeStreamMgr::init(
    logservice::ObILogStorage &log_storage,
    schema::ObSchemaPublishSignal &schema_publish_signal,
    lib::IRunWrapper *run_wrapper)
{
  int ret = common::OB_SUCCESS;
  const int64_t worker_thread_count = common::max(common::get_cpu_num(), int64_t{1});
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(fetcher_.init(
      &dispatcher_, log_storage, schema_publish_signal, run_wrapper))) {
  } else if (OB_FAIL(dispatcher_.init())) {
  } else if (OB_FAIL(worker_.init(worker_thread_count))) {
  } else {
    is_inited_ = true;
    FLOG_INFO("ObChangeStreamMgr init success (Fetcher/Dispatcher/Worker)", K(worker_thread_count));
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

  if (OB_FAIL(OB_TS_MGR.get_gts_sync(abs_timeout_us - ObTimeUtility::current_time(),
                                    safe_visible_scn))) {
  } else {
    ObChangeStreamMgr *mgr = ::oceanbase::share::server_service<::oceanbase::share::ObChangeStreamMgr>();
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
