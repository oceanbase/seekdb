/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#include "ob_ts_mgr.h"
#include "ob_gts_rpc.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
using namespace obrpc;

namespace transaction
{
ObTsSourceInfo::ObTsSourceInfo() : is_inited_(false),
                                   tenant_id_(OB_INVALID_TENANT_ID),
                                   last_access_ts_(0)
{
}

int ObTsSourceInfo::init(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  if (!is_valid_tenant_id(tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tenant_id));
  } else {
    tenant_id_ = tenant_id;
    last_access_ts_ = ObClockGenerator::getClock();
    is_inited_ = true;
    TRANS_LOG(INFO, "ts source info init success", K(tenant_id));
  }
  return ret;
}

void ObTsSourceInfo::destroy()
{
  if (is_inited_) {
    const uint64_t tenant_id = tenant_id_;
    gts_source_.destroy();
    is_inited_ = false;
    TRANS_LOG(INFO, "ts source info destroyed", K(tenant_id));
  }
}

int ObTsSourceInfo::check_if_tenant_has_been_dropped(const uint64_t tenant_id, bool &has_dropped)
{
  int ret = OB_SUCCESS;
  schema::ObMultiVersionSchemaService *schema_service = GCTX.schema_service_;
  schema::ObSchemaGetterGuard guard;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", K(ret));
  } else if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id)) || OB_UNLIKELY(tenant_id_ != tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tenant_id), K(tenant_id_));
  } else if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "schema_service is null", KR(ret));
  } else if (OB_FAIL(schema_service->get_tenant_schema_guard(OB_SYS_TENANT_ID, guard))) {
    TRANS_LOG(WARN, "fail to get schema guard", KR(ret), K(tenant_id));
  } else if (OB_FAIL(guard.check_if_tenant_has_been_dropped(tenant_id, has_dropped))) {
    TRANS_LOG(WARN, "fail to check if tenant has been dropped", KR(ret), K(tenant_id));
  }
  return ret;
}

int ObTsSourceInfo::gts_callback_interrupted(const int errcode, const share::ObLSID ls_id)
{
  int ret = OB_SUCCESS;
  const int64_t task_count = gts_source_.get_task_count();
  if (0 != task_count) {
    ret = gts_source_.gts_callback_interrupted(errcode, ls_id);
  }
  return ret;
}

////////////////////////ObTsMgr implementation///////////////////////////////////

int ObTsMgr::init(const ObAddr &server,
                  share::schema::ObMultiVersionSchemaService &schema_service,
                  share::ObLocationService &location_service,
                  rpc::frame::ObReqTransport *req_transport)
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    TRANS_LOG(WARN, "ObTsMgr inited twice", KR(ret));
  } else if (!server.is_valid() || OB_ISNULL(req_transport)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(server), KP(req_transport));
  } else if (OB_FAIL(location_adapter_def_.init(&schema_service, &location_service))) {
    TRANS_LOG(ERROR, "location adapter init error", KR(ret));
  } else if (OB_ISNULL(gts_request_rpc_proxy_ = ObGtsRpcProxyFactory::alloc())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    TRANS_LOG(WARN, "alloc gts_reqeust_rpc_proxy fail", KR(ret));
  } else if (OB_ISNULL(gts_request_rpc_ = ObGtsRequestRpcFactory::alloc())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    TRANS_LOG(WARN, "alloc gts_reqeust_rpc fail", KR(ret));
  } else if (OB_FAIL(gts_request_rpc_proxy_->init(req_transport, server))) {
    TRANS_LOG(WARN, "rpc proxy init failed", KR(ret), KP(req_transport), K(server));
  } else if (OB_FAIL(ts_worker_.init(this, true))) {
    TRANS_LOG(WARN, "ts worker init failed", KR(ret));
  } else if (OB_FAIL(gts_request_rpc_->init(gts_request_rpc_proxy_, server, this, &ts_worker_))) {
    TRANS_LOG(WARN, "response rpc init failed", KR(ret), K(server));
  } else if (FALSE_IT(location_adapter_ = &location_adapter_def_)) {
  } else if (OB_FAIL(ts_source_.init(OB_SYS_TENANT_ID, server, gts_request_rpc_, location_adapter_))) {
    TRANS_LOG(WARN, "ts source init failed", KR(ret));
  } else if (OB_FAIL(lock_.init(lib::ObMemAttr(OB_SERVER_TENANT_ID, "TsMgr")))) {
    TRANS_LOG(WARN, "ObQSyncLock init failed", KR(ret), K(OB_SERVER_TENANT_ID));
  } else {
    server_ = server;
    is_inited_ = true;
    TRANS_LOG(INFO, "ObTsMgr inited success", KP(this), K(server));
  }

  if (OB_FAIL(ret)) {
    if (NULL != gts_request_rpc_proxy_) {
      ObGtsRpcProxyFactory::release(gts_request_rpc_proxy_);
      gts_request_rpc_proxy_ = NULL;
    }
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
  gts_request_rpc_proxy_ = NULL;
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
    TRANS_LOG(WARN, "gts request rpc start", KR(ret));
    // Start gts task refresh thread
  } else if (OB_FAIL(share::ObThreadPool::start())) {
    TRANS_LOG(ERROR, "GTS local cache manager refresh worker thread start error", KR(ret));
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
    TRANS_LOG(WARN, "gts request rpc stop", KR(ret));
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
    TRANS_LOG(WARN, "gts request rpc wait", KR(ret));
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

    ObSEArray<uint64_t, 1> ids;
    delete_tenant_(OB_SYS_TENANT_ID);

    location_adapter_def_.destroy();
    lock_.destroy();
    server_.reset();
    location_adapter_ = NULL;
    is_running_ = false;
    is_inited_ = false;
    TRANS_LOG(INFO, "ObTsMgr destroyed");
  }
  if (NULL != gts_request_rpc_proxy_) {
    ObGtsRpcProxyFactory::release(gts_request_rpc_proxy_);
    gts_request_rpc_proxy_ = NULL;
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
    TRANS_LOG(WARN, "handle gts err response error", KR(ret), K(msg));
  } else {
    // do nothing
  }

  return ret;
}

int ObTsMgr::refresh_gts_location(const uint64_t tenant_id)
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
    TRANS_LOG(WARN, "refresh gts location error", K(ret), K(tenant_id));
  } else {
    // do nothing
  }

  return ret;
}

int ObTsMgr::handle_gts_result(const uint64_t tenant_id, const int64_t queue_index, const int ts_type)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTsMgr is not running", K(ret));
  } else if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tenant_id));
  } else if (OB_FAIL(ts_source_.handle_gts_result(tenant_id, queue_index))) {
    TRANS_LOG(WARN, "handle gts result error", KR(ret), K(tenant_id));
  } else {
    // do nothing
  }
  return ret;
}

int ObTsMgr::update_gts(const uint64_t tenant_id,
                        const MonotonicTs srr,
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
  } else if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id)) ||
      OB_UNLIKELY(!srr.is_valid()) || OB_UNLIKELY(0 >= gts)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tenant_id), K(srr), K(gts));
  } else if (OB_FAIL(ts_source_.update_gts(srr, gts, receive_gts_ts, update))) {
    TRANS_LOG(WARN, "update gts cache failed", KR(ret), K(tenant_id), K(srr), K(gts));
  } else {
    // do nothing
  }

  return ret;
}


int ObTsMgr::delete_tenant_(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  TRANS_LOG(INFO, "delete tenant", KR(ret), K(tenant_id));
  // do nothing
  return ret;
}

int ObTsMgr::remove_dropped_tenant(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  share::ObLSID ls_id;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tenant_id));
  } else {
    const int64_t task_count = ts_source_.get_task_count();
    if (0 != task_count) {
      ret = ts_source_.gts_callback_interrupted(OB_TENANT_NOT_EXIST, ls_id);
    }
    if (OB_SUCCESS != ret) {
      TRANS_LOG(WARN, "remove ts resource failed", KR(ret), K(tenant_id), K(ls_id));
    } else {
      TRANS_LOG(INFO, "remove ts resource success", K(tenant_id), K(ls_id));
    }
  }
  return ret;
}

int ObTsMgr::update_gts(const uint64_t tenant_id, const int64_t gts, bool &update)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTsMgr is not running", K(ret));
  } else if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id)) ||
             OB_UNLIKELY(0 >= gts) ||
             OB_UNLIKELY(gts > ObTimeUtility::current_time_ns() + 86400000000000L)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tenant_id), K(gts));
  } else if (OB_FAIL(ts_source_.update_gts(gts, update))) {
    TRANS_LOG(WARN, "update gts cache failed", K(ret), K(tenant_id), K(gts));
  }

  return ret;
}

int ObTsMgr::get_gts(const uint64_t tenant_id, ObTsCbTask *task, SCN &scn)
{
  int ret = OB_SUCCESS;
  int64_t gts = 0;//need be invalid value for SCN
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTsMgr is not running", K(ret));
  } else if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tenant_id), KP(task));
  } else if (OB_FAIL(ts_source_.get_gts(task, gts))) {
    if (OB_EAGAIN != ret) {
      TRANS_LOG(WARN, "get gts error", K(ret), K(tenant_id), KP(task));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(scn.convert_for_gts(gts))) {
      TRANS_LOG(WARN, "failed to convert_for_gts", K(ret), K(tenant_id), K(gts));
    }
  }

  return ret;
}

int ObTsMgr::get_gts(const uint64_t tenant_id,
                     const MonotonicTs stc,
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
  } else if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id)) || OB_UNLIKELY(!stc.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tenant_id), K(stc), KP(task));
  } else if (OB_FAIL(ts_source_.get_gts(stc, task, gts, receive_gts_ts))) {
    if (OB_EAGAIN != ret) {
      TRANS_LOG(WARN, "get gts error", K(ret), K(tenant_id), K(stc), KP(task));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(scn.convert_for_gts(gts))) {
      TRANS_LOG(WARN, "failed to convert_for_gts", K(ret), K(tenant_id), K(gts));
    }
  }
  return ret;
}

int ObTsMgr::get_ts_sync(const uint64_t tenant_id, const int64_t timeout_us, share::SCN &scn)
{
  bool unused_is_external_consistent = false;
  return get_ts_sync(tenant_id, timeout_us, scn, unused_is_external_consistent);
}

int ObTsMgr::get_gts_sync(const uint64_t tenant_id,
                          const MonotonicTs stc,
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
  } else if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id)) || OB_UNLIKELY(!stc.is_valid())
             || OB_UNLIKELY(timeout_us < 0)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), K(tenant_id), K(stc), K(timeout_us));
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

int ObTsMgr::get_ts_sync(const uint64_t tenant_id,
                         const int64_t timeout_us,
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
  } else if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id)) || OB_UNLIKELY(timeout_us < 0)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), K(tenant_id), K(timeout_us));
  } else {
    do {
      int64_t ts = 0;
      if (OB_FAIL(ts_source_.get_gts(stc, NULL, ts, receive_gts_ts))) {
        if (OB_EAGAIN != ret) {
          TRANS_LOG(WARN, "get gts error", K(ret), K(tenant_id), K(stc));
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

int ObTsMgr::wait_gts_elapse(const uint64_t tenant_id, const SCN &scn,
    ObTsCbTask *task, bool &need_wait)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  // } else if (OB_UNLIKELY(!is_running_)) {
  //   ret = OB_NOT_RUNNING;
  //   TRANS_LOG(WARN, "ObTsMgr not running", K(ret));
  } else if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))
      || OB_UNLIKELY(!scn.is_valid())
      || OB_ISNULL(task)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tenant_id), K(scn), KP(task));
  } else {
    const int64_t ts = scn.get_val_for_gts();
    if (OB_FAIL(ts_source_.wait_gts_elapse(ts, task, need_wait))) {
      TRANS_LOG(WARN, "wait gts elapse failed", K(ret), K(ts), KP(task));
    }
  }
  return ret;
}

int ObTsMgr::wait_gts_elapse(const uint64_t tenant_id, const SCN &scn)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!is_running_)) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "ObTsMgr not running", K(ret));
  } else if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id)) || OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tenant_id), K(scn));
  } else {
    const int64_t ts = scn.get_val_for_gts();
    if (OB_FAIL(ts_source_.wait_gts_elapse(ts))) {
      if (OB_EAGAIN != ret) {
        TRANS_LOG(WARN, "wait gts elapse fail", K(ret), K(ts), K(tenant_id));
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

int ObTsMgr::add_tenant_(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  TRANS_LOG(INFO, "add tenant success", K(tenant_id));
  // do nothing

  return ret;
}

int ObTsMgr::interrupt_gts_callback_for_ls_offline(const uint64_t tenant_id,
                                                   const share::ObLSID ls_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTsMgr is not inited", K(ret));
  } else if (OB_UNLIKELY(!ls_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), K(ls_id));
  } else if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tenant_id));
  } else {
    const int64_t task_count = ts_source_.get_task_count();
    if (0 != task_count) {
      ret = ts_source_.gts_callback_interrupted(OB_LS_OFFLINE, ls_id);
    }

    if (OB_SUCCESS != ret) {
      TRANS_LOG(WARN, "interrupt gts callback failed", KR(ret), K(tenant_id), K(ls_id));
    } else {
      TRANS_LOG(INFO, "interrupt gts callback success", K(tenant_id), K(ls_id));
    }
  }
  return ret;
}

} // transaction
} // oceanbase
