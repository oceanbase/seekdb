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
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_service.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "logservice/ob_log_handler.h"
#include "share/ob_debug_sync.h"
#include "storage/tx/ob_ts_mgr.h"

namespace oceanbase
{
namespace share
{

ObChangeStreamMgr::ObChangeStreamMgr()
  : is_inited_(false),
    refresh_scn_(0),
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
  ATOMIC_STORE(&refresh_scn_, 0);
}

int ObChangeStreamMgr::update_refresh_scn(const int64_t refresh_scn)
{
  int ret = OB_SUCCESS;
  if (refresh_scn < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid refresh_scn", KR(ret), K(refresh_scn));
  } else {
    int64_t old_refresh_scn = ATOMIC_LOAD(&refresh_scn_);
    while (old_refresh_scn < refresh_scn
           && !ATOMIC_BCAS(&refresh_scn_, old_refresh_scn, refresh_scn)) {
      old_refresh_scn = ATOMIC_LOAD(&refresh_scn_);
    }
  }
  return ret;
}

int ObChangeStreamMgr::get_min_dep_lsn(palf::LSN &min_dep_lsn) const
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    ret = fetcher_.get_min_dep_lsn(min_dep_lsn);
  }
  return ret;
}

int ObChangeStreamMgr::wait_refresh_scn(
    common::ObISQLClient &sql_client,
    const int64_t timeout_us)
{
  UNUSED(sql_client);
  int ret = common::OB_SUCCESS;
  const int64_t abs_timeout_us = ObTimeUtility::current_time() + timeout_us;
  ObChangeStreamMgr *mgr = share::server_service<ObChangeStreamMgr>();
  bool refresh_completed = false;

  if (timeout_us <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(mgr) || !mgr->is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("change stream mgr is not inited", KR(ret), KP(mgr));
  }

  // Every retry shares abs_timeout_us.  Runtime schema changes before target
  // capture and Dispatcher recovery epoch changes both invalidate the capture,
  // but neither grants a fresh timeout.
  while (OB_SUCC(ret) && !refresh_completed) {
    int64_t schema_version_v1 = 0;
    int64_t schema_version_v2 = 0;
    ObCSAsyncSchemaState schema_state;
    SCN target_scn;
    palf::LSN fence_lsn;
    const int64_t capture_epoch = mgr->dispatcher_.get_epoch();
    const int64_t now = ObTimeUtility::current_time();

    if (now >= abs_timeout_us) {
      ret = OB_TIMEOUT;
    } else if (mgr->dispatcher_.is_recovering()) {
      // The epoch has changed, but its persisted applied_scn baseline and
      // Fetcher schema state are not ready yet.  Do not capture against the
      // previous generation's in-memory state.
      ob_usleep(1000);
      continue;
    } else if (OB_ISNULL(GCTX.schema_service_)) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(GCTX.schema_service_->get_runtime_refreshed_schema_version(
                   schema_version_v1))) {
      LOG_WARN("failed to get runtime schema version before refresh target", KR(ret));
    } else if (schema_version_v1 <= 0
               || !ObSchemaService::is_formal_version(schema_version_v1)) {
      ob_usleep(1000);
      continue;
    } else if (OB_FAIL(mgr->fetcher_.wait_async_schema_ready(
                   schema_version_v1, abs_timeout_us, schema_state))) {
      if (ret == OB_EAGAIN || ret == OB_SCHEMA_EAGAIN) {
        ret = OB_SUCCESS;
        ob_usleep(1000);
        continue;
      }
      LOG_WARN("failed to wait async schema ready", KR(ret), K(schema_version_v1));
    } else if (ObTimeUtility::current_time() >= abs_timeout_us) {
      ret = OB_TIMEOUT;
    } else if (OB_FAIL(OB_TS_MGR.get_gts_sync(
                   abs_timeout_us - ObTimeUtility::current_time(), target_scn))) {
      LOG_WARN("get GTS for change stream refresh failed", KR(ret));
    } else if (OB_FAIL(GCTX.schema_service_->get_runtime_refreshed_schema_version(
                   schema_version_v2))) {
      LOG_WARN("failed to get runtime schema version after refresh target", KR(ret));
    } else if (schema_version_v1 != schema_version_v2) {
      // The (schema state, GTS) pair was not captured from one stable schema.
      ob_usleep(1000);
      continue;
    } else if (mgr->dispatcher_.is_recovering()
               || mgr->dispatcher_.get_epoch() != capture_epoch) {
      continue;
    } else if (schema_state.has_async_index_) {
      storage::ObLS *ls = nullptr;
      if (OB_FAIL(share::server_service<storage::ObLSService>()->get_ls(ls))
          || OB_FAIL(ls->get_log_handler()->get_max_lsn(fence_lsn))) {
        LOG_WARN("failed to capture refresh fence_lsn", KR(ret), K(target_scn));
      }
    }

    if (OB_SUCC(ret)) {
      DEBUG_SYNC(CS_REFRESH_AFTER_TARGET_CAPTURE);
      if (mgr->dispatcher_.is_recovering()
          || mgr->dispatcher_.get_epoch() != capture_epoch) {
        continue;
      } else if (!schema_state.has_async_index_) {
        // wait_async_schema_ready() proves old tasks were drained for this
        // no-async schema version.  applied_scn intentionally remains unchanged.
        bool published = false;
        ret = mgr->dispatcher_.try_publish_refresh_scn(
            static_cast<int64_t>(target_scn.get_val_for_gts()),
            capture_epoch,
            published);
        refresh_completed = OB_SUCC(ret) && published;
      } else {
        bool target_completed = false;
        bool need_recapture = false;
        bool fence_processed = false;
        int64_t barrier_sn = 0;
        while (OB_SUCC(ret) && !target_completed && !need_recapture) {
          const int64_t wait_now = ObTimeUtility::current_time();
          ObCSAsyncSchemaState current_state;
          if (wait_now >= abs_timeout_us) {
            ret = OB_TIMEOUT;
          } else if (mgr->dispatcher_.is_recovering()
                     || mgr->dispatcher_.get_epoch() != capture_epoch) {
            need_recapture = true;
          } else if (OB_FAIL(mgr->fetcher_.get_async_schema_state(current_state))) {
            LOG_WARN("failed to get async schema state while waiting refresh", KR(ret));
          } else if (current_state.last_no_async_index_drained_schema_version_
                     > schema_version_v1) {
            // Historical proof remains valid even if a newer async generation
            // has already become ACTIVE.
            target_completed = true;
          } else if (!fence_processed
                     && mgr->fetcher_.get_processed_end_lsn()
                        >= static_cast<int64_t>(fence_lsn.val_)) {
            barrier_sn = mgr->dispatcher_.get_next_sn();
            fence_processed = true;
            DEBUG_SYNC(CS_REFRESH_AFTER_FENCE_PROCESSED);
          } else if (fence_processed
                     && mgr->dispatcher_.get_next_commit_sn() >= barrier_sn) {
            target_completed = true;
          } else {
            ob_usleep(1000);
          }
        }

        if (need_recapture) {
          continue;
        } else if (OB_SUCC(ret) && target_completed) {
          bool published = false;
          ret = mgr->dispatcher_.try_publish_refresh_scn(
              static_cast<int64_t>(target_scn.get_val_for_gts()),
              capture_epoch,
              published);
          refresh_completed = OB_SUCC(ret) && published;
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("change stream refresh completed",
             "refresh_scn", mgr->get_refresh_scn());
  } else {
    LOG_WARN("change stream refresh failed", KR(ret),
             "refresh_scn", OB_NOT_NULL(mgr) ? mgr->get_refresh_scn() : 0,
             K(abs_timeout_us));
  }
  return ret;
}

}  // namespace share
}  // namespace oceanbase
