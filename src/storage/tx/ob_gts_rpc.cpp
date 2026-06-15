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

#include "ob_gts_rpc.h"
#include "ob_timestamp_access.h"
#include "observer/ob_ex_rpc.h"

namespace oceanbase
{

using namespace common;
using namespace transaction;
using namespace obcall;
using namespace storage;
using namespace omt;
using namespace observer;
using namespace share;

namespace obcall
{

OB_SERIALIZE_MEMBER(ObGtsRpcResult, tenant_id_, status_, srr_.mts_, gts_start_, gts_end_);

int ObGtsRpcResult::init(const uint64_t tenant_id, const int status,
    const MonotonicTs srr, const int64_t gts_start, const int64_t gts_end)
{
  int ret = OB_SUCCESS;
  if (!is_valid_tenant_id(tenant_id) ||
      (OB_SUCCESS == status && (!srr.is_valid() || 0 >= gts_start || 0 >= gts_end))) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tenant_id),
        K(status), K(srr), K(gts_start), K(gts_end));
  } else {
    tenant_id_ = tenant_id;
    status_ = status;
    srr_ = srr;
    gts_start_ = gts_start;
    gts_end_ = gts_end;
  }
  return ret;
}

void ObGtsRpcResult::reset()
{
  tenant_id_ = 0;
  status_ = OB_SUCCESS;
  srr_.reset();
  gts_start_ = 0;
  gts_end_ = 0;
}

bool ObGtsRpcResult::is_valid() const
{
  return is_valid_tenant_id(tenant_id_) &&
    (OB_SUCCESS != status_ || (srr_.is_valid() && gts_start_ > 0 && gts_end_ > 0));
}

} // obcall

namespace transaction
{
int ObGtsRequestRpc::init(const ObAddr &self,
                          ObTsMgr *ts_mgr, ObTsWorker *ts_worker)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    TRANS_LOG(WARN, "gts request rpc inited twice", KR(ret));
  } else if (!self.is_valid() || OB_ISNULL(ts_mgr) || OB_ISNULL(ts_worker)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(self), KP(ts_mgr), KP(ts_worker));
  } else if (OB_SUCCESS != (ret = gts_request_cb_.init(ts_mgr, ts_worker))) {
    TRANS_LOG(WARN, "gts request callback inited failed", KR(ret));
  } else {
    self_ = self;
    is_inited_ = true;
    ts_mgr_ = ts_mgr;
    TRANS_LOG(INFO, "gts request rpc inited success", KP(this), K(self), KP(ts_mgr));
  }
  return ret;
}

int ObGtsRequestRpc::start()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "gts request rpc not inited", KR(ret));
  } else if (is_running_) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "gts request rpc already running", KR(ret));
  } else {
    is_running_ = true;
    TRANS_LOG(INFO, "gts request rpc start success");
  }
  return ret;
}

int ObGtsRequestRpc::stop()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "gts request rpc not inited", KR(ret));
  } else {
    is_running_ = false;
    TRANS_LOG(INFO, "gts request rpc stop success");
  }
  return ret;
}

int ObGtsRequestRpc::wait()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "gts request rpc not inited", KR(ret));
  } else if (is_running_) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "gts request rpc is running", KR(ret));
  } else {
    TRANS_LOG(INFO, "gts request rpc wait success");
  }
  return ret;
}

void ObGtsRequestRpc::destroy()
{
  int tmp_ret = OB_SUCCESS;
  if (is_inited_) {
    if (is_running_) {
      if (OB_SUCCESS != (tmp_ret = stop())) {
        TRANS_LOG_RET(WARN, tmp_ret, "gts request rpc stop error", K(tmp_ret));
      } else if (OB_SUCCESS != (tmp_ret = wait())) {
        TRANS_LOG_RET(WARN, tmp_ret, "gts request rpc wait error", K(tmp_ret));
      } else {
        // do nothing
      }
    }
    is_inited_ = false;
    self_.reset();
    TRANS_LOG(INFO, "gts request rpc destroy");
  }
}

int ObGtsRequestRpc::post(const uint64_t tenant_id, const ObAddr &server,
    const ObGtsRequest &msg)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "gts request rpc not inited", KR(ret));
  } else if (!is_running_) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "gts request rpc not running", KR(ret));
  } else if (!is_valid_tenant_id(tenant_id) || !server.is_valid() || !msg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tenant_id), K(server), K(msg));
  } else {
    // single-replica: target is always local; dispatch async in-process (ex-RPC),
    // restoring the original async .post(msg, &gts_request_cb_) decoupling: the handler
    // + response callback run on a worker thread, not the caller. msg is serialized
    // (deep-copied). The cb is a plain value type now (config + tenant_id; unlike the old
    // AsyncCB it no longer stores the pending result -- that is passed to process() as an
    // argument), so a per-request value copy is the correct "clone": no heap clone(alloc)
    // is needed in the in-process model, and each async task gets its own copy (own
    // tenant_id) so they never race on the shared gts_request_cb_ member.
    (void)ex_rpc::async_call<void>(msg,
        [cb = gts_request_cb_, tenant_id, server](const ObGtsRequest &m) mutable {
      int ret = OB_SUCCESS;
      ObGtsRpcResult gts_rpc_result;
      MTL_SWITCH(tenant_id) {
        if (OB_FAIL(MTL(ObTimestampAccess*)->handle_request(m, gts_rpc_result))) {
          if (REACH_TIME_INTERVAL(100 * 1000)) {
            TRANS_LOG(WARN, "post local gts request failed", KR(ret), K(server), K(m));
          }
        } else if (!gts_rpc_result.is_valid()) {
          ret = OB_ERR_UNEXPECTED;
          TRANS_LOG(ERROR, "post local gts request and gts_rpc_result is invalid", KR(ret), K(server),
                    K(m), K(gts_rpc_result));
        } else {
          rpc::frame::ObResultCode rcode;
          rcode.rcode_ = OB_SUCCESS;
          cb.set_tenant_id(tenant_id);
          if (OB_FAIL(cb.process(gts_rpc_result, server, rcode))) {
            TRANS_LOG(WARN, "post local gts request failed", KR(ret), K(server), K(m));
          } else {
            TRANS_LOG(DEBUG, "post local gts request success", KR(ret), K(server), K(m));
          }
        }
      }
    });
  }
  return ret;
}

} // transaction

} // oceanbase
