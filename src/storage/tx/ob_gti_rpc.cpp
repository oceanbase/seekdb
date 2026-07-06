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

#include "ob_gti_rpc.h"
#include "share/rc/ob_module_provider.h"
#include "ob_trans_id_service.h"
#include "ob_trans_service.h"
#include "share/ob_ex_rpc.h"

namespace oceanbase
{
using namespace share;
using namespace obcall;
namespace transaction
{

OB_SERIALIZE_MEMBER(ObGtiRequest, range_);

int ObGtiRequest::init(const int64_t range)
{
  int ret = OB_SUCCESS;
  if (!true || 0 >= range) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(range));
  } else {
    range_ = range;
  }
  return ret;
}

bool ObGtiRequest::is_valid() const
{
  return true && range_ > 0;
}

int ObGtiRequestRpc::init(const ObAddr &self, ObGtiSource *gti_source)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    TRANS_LOG(WARN, "gti request rpc inited twice", KR(ret));
  } else if (!self.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(self));
  } else if (OB_SUCCESS != (ret = gti_request_cb_.init(gti_source))) {
    TRANS_LOG(WARN, "gti request callback inited failed", KR(ret));
  } else {
    self_ = self;
    is_inited_ = true;
    TRANS_LOG(INFO, "gti request rpc inited success", KP(this), K(self));
  }
  return ret;
}

int ObGtiRequestRpc::start()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "gti request rpc not inited", KR(ret));
  } else if (is_running_) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "gti request rpc already running", KR(ret));
  } else {
    is_running_ = true;
    TRANS_LOG(INFO, "gti request rpc start success");
  }
  return ret;
}

int ObGtiRequestRpc::stop()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "gti request rpc not inited", KR(ret));
  } else {
    is_running_ = false;
    TRANS_LOG(INFO, "gti request rpc stop success");
  }
  return ret;
}

int ObGtiRequestRpc::wait()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "gti request rpc not inited", KR(ret));
  } else if (is_running_) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "gti request rpc is running", KR(ret));
  } else {
    TRANS_LOG(INFO, "gti request rpc wait success");
  }
  return ret;
}

void ObGtiRequestRpc::destroy()
{
  int tmp_ret = OB_SUCCESS;
  if (is_inited_) {
    if (is_running_) {
      if (OB_SUCCESS != (tmp_ret = stop())) {
        TRANS_LOG_RET(WARN, tmp_ret, "gti request rpc stop error", K(tmp_ret));
      } else if (OB_SUCCESS != (tmp_ret = wait())) {
        TRANS_LOG_RET(WARN, tmp_ret, "gti request rpc wait error", K(tmp_ret));
      } else {
        // do nothing
      }
    }
    is_inited_ = false;
    self_.reset();
    TRANS_LOG(INFO, "gti request rpc destroy");
  }
}

int ObGtiRequestRpc::post(const ObGtiRequest &msg)
{
  int ret = OB_SUCCESS;
  ObAddr server;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "gti request rpc not inited", KR(ret));
  } else if (!is_running_) {
    ret = OB_NOT_RUNNING;
    TRANS_LOG(WARN, "gti request rpc not running", KR(ret));
  } else if (!msg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(msg));
  } else if (OB_FAIL(share::g_mp->trans_service()->get_location_adapter()->nonblock_get_leader(GCONF.cluster_id, GTI_LS, server))) {
    TRANS_LOG(WARN, "get leader failed", KR(ret), K(msg), K(GTI_LS));
  } else {
   // single-replica: target is always local; dispatch async in-process (ex-RPC),
   // restoring the original async .post(msg, &gti_request_cb_) decoupling. msg is
   // serialized; tenant context is restored on the worker via MTL_SWITCH. The cb is a
   // plain value type now (no heap clone needed -- the result is passed to process() as
   // an argument, not stored in the cb), so each async task captures its own value copy.
   (void)ex_rpc::async_call<void>(msg,
       [cb = gti_request_cb_, server](const ObGtiRequest &m) mutable {
     int ret = OB_SUCCESS;
     MOD_SCOPE {
       ObGtiRpcResult gti_rpc_result;
       if (OB_FAIL(share::g_mp->trans_id_service()->handle_request(m, gti_rpc_result))) {
         TRANS_LOG(WARN, "post local gti request failed", KR(ret), K(server), K(m));
       } else if (!gti_rpc_result.is_valid()) {
         ret = OB_ERR_UNEXPECTED;
         TRANS_LOG(ERROR, "post local gti request and gti_rpc_result is invalid", KR(ret), K(server),
                   K(m), K(gti_rpc_result));
       } else {
         rpc::frame::ObResultCode rcode;
         rcode.rcode_ = OB_SUCCESS;
         
         if (OB_FAIL(cb.process(gti_rpc_result, server, rcode))) {
           TRANS_LOG(WARN, "post local gti request failed", KR(ret), K(server), K(m));
         } else {
           TRANS_LOG(DEBUG, "post local gti request success", KR(ret), K(server), K(m));
         }
       }
     }
   });
  }
  return ret;
}

} //transaction

namespace obcall
{

OB_SERIALIZE_MEMBER(ObGtiRpcResult, status_, start_id_, end_id_);

int ObGtiRpcResult::init(const int status, const int64_t start_id,
    const int64_t end_id)
{
  int ret = OB_SUCCESS;
  if (!true ||
      (OB_SUCCESS == status && (0 >= start_id || 0 >= end_id))) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret),
        K(status), K(start_id), K(end_id));
  } else {
    status_ = status;
    start_id_ = start_id;
    end_id_ = end_id;
    TRANS_LOG(INFO, "ObGtiRpcResult init", KR(ret),
        K(status), K(start_id), K(end_id));
  }
  return ret;
}

void ObGtiRpcResult::reset()
{
  status_ = OB_SUCCESS;
  start_id_ = 0;
  end_id_ = 0;
}

bool ObGtiRpcResult::is_valid() const
{
  return true &&
    (OB_SUCCESS != status_ || (start_id_ > 0 && end_id_ > 0));
}

} // obcall

} // oceanbase
