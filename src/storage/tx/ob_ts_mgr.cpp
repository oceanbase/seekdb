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

#include "ob_ts_mgr.h"
#include "ob_gts_rpc.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
using namespace obcall;

namespace transaction
{
////////////////////////ObTsMgr implementation///////////////////////////////////

int ObTsMgr::init(const ObAddr &server,
                  share::schema::ObMultiVersionSchemaService &schema_service,
                  share::ObLocationService &location_service)
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    TRANS_LOG(WARN, "ObTsMgr inited twice", KR(ret));
  } else if (!server.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(server));
  } else if (OB_FAIL(location_adapter_def_.init(&schema_service, &location_service))) {
  } else if (OB_ISNULL(gts_request_rpc_ = ObGtsRequestRpcFactory::alloc())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    TRANS_LOG(WARN, "alloc gts_reqeust_rpc fail", KR(ret));
  } else if (OB_FAIL(ts_worker_.init(this, true))) {
  } else if (OB_FAIL(gts_request_rpc_->init(server, this, &ts_worker_))) {
  } else if (FALSE_IT(location_adapter_ = &location_adapter_def_)) {
  } else if (OB_FAIL(ts_source_.init(server, gts_request_rpc_, location_adapter_))) {
  } else {
    server_ = server;
    is_inited_ = true;
    TRANS_LOG(INFO, "ObTsMgr inited success", KP(this), K(server));
  }

  if (OB_FAIL(ret)) {
    if (NULL != gts_request_rpc_) {
      ObGtsRequestRpcFactory::release(gts_request_rpc_);
      gts_request_rpc_ = NULL;
    }
  }

  return ret;
}

void ObTsMgr::reset()
{
  is_inited_ = false;
  is_running_ = false;
  ts_source_.reset();
  server_.reset();
  location_adapter_ = NULL;
  gts_request_rpc_ = NULL;
}

int ObTsMgr::start()
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", KR(ret));
  } else if (is_running_) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "ObTsMgr is already running", KR(ret));
  } else if (OB_FAIL(gts_request_rpc_->start())) {
  } else if (OB_FAIL(share::ObThreadPool::start())) {
  } else {
    is_running_ = true;
    TRANS_LOG(INFO, "ObTsMgr start success");
  }
  return ret;
}

void ObTsMgr::stop()
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", KR(ret));
  } else if (OB_FAIL(gts_request_rpc_->stop())) {
  } else {
    (void)share::ObThreadPool::stop();
    (void)ts_worker_.stop();
    is_running_ = false;
    TRANS_LOG(INFO, "ObTsMgr stop success");
  }
}

void ObTsMgr::wait()
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", KR(ret));
  } else if (is_running_) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "ObTsMgr is running", KR(ret));
  } else if (OB_FAIL(gts_request_rpc_->wait())) {
  } else {
    (void)share::ObThreadPool::wait();
    (void)ts_worker_.wait();
    TRANS_LOG(INFO, "ObTsMgr wait success");
  }
}

void ObTsMgr::destroy()
{
  if (is_inited_) {
    if (is_running_) {
      stop();
      wait();
    }
    (void)share::ObThreadPool::destroy();
    (void)ts_worker_.destroy();

    location_adapter_def_.destroy();
    server_.reset();
    location_adapter_ = NULL;
    is_running_ = false;
    is_inited_ = false;
    TRANS_LOG(INFO, "ObTsMgr destroyed");
  }
  if (NULL != gts_request_rpc_) {
    ObGtsRequestRpcFactory::release(gts_request_rpc_);
    gts_request_rpc_ = NULL;
  }
}
// Execute gts task refresh, by a dedicated thread to be responsible
void ObTsMgr::run1()
{
  int ret = OB_SUCCESS;
  // cluster version less than 2.0 will not update gts
  lib::set_thread_name("TsMgr");
  while (!has_set_stop()) {
    int tmp_ret = OB_SUCCESS;
    // sleep 100 * 1000 us
    ob_usleep(REFRESH_GTS_INTERVEL_US, true/*is_idle_sleep*/);
    if (OB_SUCCESS != (tmp_ret = ts_source_.refresh_gts(false))) {
      if (EXECUTE_COUNT_PER_SEC(1)) {
        TRANS_LOG(WARN, "refresh gts failed", K(tmp_ret));
      }
    }
    if (EXECUTE_COUNT_PER_SEC(1)) {
      TRANS_LOG(INFO, "refresh gts", KR(ret));
    }
  }
}

int ObTsMgr::handle_gts_err_response(const ObGtsErrResponse &msg)
{
  int ret = OB_SUCCESS;
  ObTimeGuard timeguard("handle_gts_err_response", 100000);

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTsMgr is not running", K(ret));
  } else if (OB_UNLIKELY(!msg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(msg));
  } else if (OB_FAIL(ts_source_.handle_gts_err_response(msg))) {
  } else {
    // do nothing
  }

  return ret;
}

int ObTsMgr::refresh_gts_location()
{
  int ret = OB_SUCCESS;
  ObTimeGuard timeguard("refresh_gts_location", 100000);

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTsMgr is not running", K(ret));
  } else if (OB_FAIL(ts_source_.refresh_gts_location())) {
  } else {
    // do nothing
  }

  return ret;
}

int ObTsMgr::handle_gts_result(const int64_t queue_index, const int ts_type)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTsMgr is not running", K(ret));
  } else if (OB_FAIL(ts_source_.handle_gts_result(queue_index))) {
  } else {
    // do nothing
  }
  return ret;
}

int ObTsMgr::update_gts(const MonotonicTs srr,
                        const int64_t gts,
                        const int ts_type,
                        bool &update)
{
  int ret = OB_SUCCESS;
  const MonotonicTs receive_gts_ts = MonotonicTs::current_time();

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTsMgr is not running", K(ret));
  } else if (OB_UNLIKELY(!srr.is_valid()) || OB_UNLIKELY(0 >= gts)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(srr), K(gts));
  } else if (OB_FAIL(ts_source_.update_gts(srr, gts, receive_gts_ts, update))) {
  } else {
    // do nothing
  }

  return ret;
}


int ObTsMgr::interrupt_gts_callbacks()
{
  int ret = OB_SUCCESS;
  share::ObLSID ls_id;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else {
    const int64_t task_count = ts_source_.get_task_count();
    if (0 != task_count) {
      ret = ts_source_.gts_callback_interrupted(OB_TENANT_NOT_EXIST, ls_id);
    }
    if (OB_SUCCESS != ret) {
    } else {
      TRANS_LOG(INFO, "interrupt gts callbacks success", K(ls_id));
    }
  }
  return ret;
}

int ObTsMgr::update_gts(const int64_t gts, bool &update)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTsMgr is not running", K(ret));
  } else if (OB_UNLIKELY(0 >= gts) ||
             OB_UNLIKELY(gts > ObTimeUtility::current_time_ns() + 86400000000000L)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(gts));
  } else if (OB_FAIL(ts_source_.update_gts(gts, update))) {
  }

  return ret;
}

int ObTsMgr::get_gts(ObTsCbTask *task, SCN &scn)
{
  int ret = OB_SUCCESS;
  int64_t gts = 0;//need be invalid value for SCN
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTsMgr is not running", K(ret));
  } else if (OB_FAIL(ts_source_.get_gts(task, gts))) {
    if (OB_EAGAIN != ret) {
      TRANS_LOG(WARN, "get gts error", K(ret), KP(task));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(scn.convert_for_gts(gts))) {
    }
  }

  return ret;
}

int ObTsMgr::get_gts(const MonotonicTs stc,
                     ObTsCbTask *task,
                     SCN &scn,
                     MonotonicTs &receive_gts_ts)
{
  int ret = OB_SUCCESS;
  int64_t gts = 0;//need be invalid value for SCN

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTsMgr is not running", K(ret));
  } else if (OB_UNLIKELY(!stc.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(stc), KP(task));
  } else if (OB_FAIL(ts_source_.get_gts(stc, task, gts, receive_gts_ts))) {
    if (OB_EAGAIN != ret) {
      TRANS_LOG(WARN, "get gts error", K(ret), K(stc), KP(task));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(scn.convert_for_gts(gts))) {
    }
  }
  return ret;
}

int ObTsMgr::get_ts_sync(const int64_t timeout_us, share::SCN &scn)
{
  bool unused_is_external_consistent = false;
  return get_ts_sync(timeout_us, scn, unused_is_external_consistent);
}

int ObTsMgr::get_gts_sync(const MonotonicTs stc,
                          const int64_t timeout_us,
                          share::SCN &scn,
                          MonotonicTs &receive_gts_ts)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTsMgr is not running", K(ret));
  } else if (OB_UNLIKELY(!stc.is_valid())
             || OB_UNLIKELY(timeout_us < 0)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), K(stc), K(timeout_us));
  } else {
    int64_t expire_ts = ObClockGenerator::getClock() + timeout_us;
    int retry_times = 0;
    const int64_t SLEEP_TIME_US = 500;
    do {
      const int64_t now = ObClockGenerator::getClock();
      int64_t gts_result = 0;
      if (now >= expire_ts) {
        ret = OB_TIMEOUT;
      } else if (OB_FAIL(ts_source_.get_gts(stc, NULL, gts_result, receive_gts_ts))) {
        if (OB_EAGAIN == ret) {
          ob_usleep(SLEEP_TIME_US);
        } else {
          TRANS_LOG(WARN, "get gts fail", K(ret), K(now));
        }
      } else {
        scn.convert_for_gts(gts_result);
      }
    } while (OB_EAGAIN == ret);
  }

  return ret;
}

int ObTsMgr::get_ts_sync(const int64_t timeout_us,
                         SCN &scn,
                         bool &is_external_consistent)
{
  int ret = OB_SUCCESS;
  const int64_t start = ObTimeUtility::current_time();
  const MonotonicTs stc = MonotonicTs::current_time();
  MonotonicTs receive_gts_ts;
  int64_t sleep_us = 100 * 1000;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTsMgr is not running", K(ret));
  } else if (OB_UNLIKELY(timeout_us < 0)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), K(timeout_us));
  } else {
    do {
      int64_t ts = 0;
      if (OB_FAIL(ts_source_.get_gts(stc, NULL, ts, receive_gts_ts))) {
        if (OB_EAGAIN != ret) {
          TRANS_LOG(WARN, "get gts error", K(ret), K(stc));
        } else {
          ob_usleep(sleep_us);
          sleep_us = sleep_us * 2;
          sleep_us = (sleep_us >= 1000000 ? 1000000 : sleep_us);
          // rewrite ret
          ret = OB_SUCCESS;
        }
      } else {
        scn.convert_for_gts(ts);
        is_external_consistent = true;
        break;
      }
    } while (OB_SUCCESS == ret);
  }

  return ret;
}

int ObTsMgr::wait_gts_elapse(const SCN &scn,
    ObTsCbTask *task, bool &need_wait)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  // } else if (OB_UNLIKELY(!is_running_)) {
  //   ret = OB_NOT_RUNNING;
  //   TRANS_LOG(WARN, "ObTsMgr not running", K(ret));
  } else if (OB_UNLIKELY(!scn.is_valid())
      || OB_ISNULL(task)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(scn), KP(task));
  } else {
    const int64_t ts = scn.get_val_for_gts();
    if (OB_FAIL(ts_source_.wait_gts_elapse(ts, task, need_wait))) {
    }
  }
  return ret;
}

int ObTsMgr::wait_gts_elapse(const SCN &scn)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTsMgr not running", K(ret));
  } else if (OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(scn));
  } else {
    const int64_t ts = scn.get_val_for_gts();
    if (OB_FAIL(ts_source_.wait_gts_elapse(ts))) {
      if (OB_EAGAIN != ret) {
        TRANS_LOG(WARN, "wait gts elapse fail", K(ret), K(ts));
      }
    }
  }

  return ret;
}

ObTsMgr *&ObTsMgr::get_instance_inner()
{
  static ObTsMgr instance;
  static ObTsMgr *instance2 = &instance;
  return instance2;
}

ObTsMgr &ObTsMgr::get_instance()
{
  return *get_instance_inner();
}

int ObTsMgr::interrupt_gts_callback_for_ls_offline(const share::ObLSID ls_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), K(ls_id));
  } else {
    const int64_t task_count = ts_source_.get_task_count();
    if (0 != task_count) {
      ret = ts_source_.gts_callback_interrupted(OB_LS_OFFLINE, ls_id);
    }

    if (OB_SUCCESS != ret) {
    } else {
      TRANS_LOG(INFO, "interrupt gts callback success", K(ls_id));
    }
  }
  return ret;
}

} // transaction
} // oceanbase
