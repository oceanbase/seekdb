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

#include "ob_gais_rpc.h"
#include "share/ob_global_autoinc_service.h"
#include "observer/ob_ex_rpc.h"

namespace oceanbase
{
using namespace oceanbase::common;
using namespace oceanbase::obcall;
using namespace oceanbase::observer;
using namespace oceanbase::share;
using namespace oceanbase::transaction;

namespace obcall
{

OB_SERIALIZE_MEMBER(ObGAISNextValRpcResult, start_inclusive_, end_inclusive_, sync_value_);

OB_SERIALIZE_MEMBER(ObGAISCurrValRpcResult, sequence_value_, sync_value_);

OB_DEF_SERIALIZE(ObGAISNextSequenceValRpcResult)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(nextval_);
  return ret;
}

OB_DEF_DESERIALIZE(ObGAISNextSequenceValRpcResult)
{
  int ret = OB_SUCCESS;
  share::ObSequenceValue nextval;
  OB_UNIS_DECODE(nextval);
  // deep copy is needed to ensure that the memory of nextval_ will not be reclaimed
  if (OB_SUCC(ret) && OB_FAIL(nextval_.assign(nextval))) {
    LOG_WARN("fail to assign nextval", K(ret));
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObGAISNextSequenceValRpcResult)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(nextval_);
  return len;
}

int ObGAISNextValRpcResult::init(const uint64_t start_inclusive, const uint64_t end_inclusive,
                                 const uint64_t sync_value)
{
  int ret = OB_SUCCESS;
  if (start_inclusive <= 0 || end_inclusive <= 0 || start_inclusive > end_inclusive ||
      sync_value_ > end_inclusive) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(start_inclusive), K(end_inclusive), K(sync_value));
  } else {
    start_inclusive_ = start_inclusive;
    end_inclusive_ = end_inclusive;
    sync_value_ = sync_value;
  }
  return ret;
}

int ObGAISCurrValRpcResult::init(const uint64_t sequence_value, const uint64_t sync_value)
{
  int ret = OB_SUCCESS;
  if (sequence_value < sync_value) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(sequence_value), K(sync_value));
  } else {
    sequence_value_ = sequence_value;
    sync_value_ = sync_value;
  }
  return ret;
}

} // obcall

namespace share
{
int ObGAISRequestRpc::init(const ObAddr &self)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("gais request rpc inited twice", KR(ret));
  } else if (!self.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(self));
  } else {
    self_ = self;
    is_inited_ = true;
    LOG_INFO("gais request rpc inited success", KP(this), K(self));
  }
  return ret;
}

void ObGAISRequestRpc::destroy()
{
  int tmp_ret = OB_SUCCESS;
  if (is_inited_) {
    is_inited_ = false;
    self_.reset();
    LOG_INFO("gais request rpc destroy");
  }
}

int ObGAISRequestRpc::next_autoinc_val(const ObAddr &server,
                                       const ObGAISNextAutoIncValReq &msg,
                                       ObGAISNextValRpcResult &rpc_result)
{
  int ret = OB_SUCCESS;
  const uint64_t timeout = THIS_WORKER.get_timeout_remain();
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("gais request rpc not inited", KR(ret));
  } else if (!server.is_valid() || !msg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(server), K(msg));
  } else {
    // single-replica: target is always local; dispatch in-process
    ex_rpc::sync_call([&]{
    ObGlobalAutoIncService *gais = nullptr;
    const uint64_t tenant_id = msg.autoinc_key_.tenant_id_;
    MTL_SWITCH(tenant_id) {
      if (OB_ISNULL(gais = MTL(ObGlobalAutoIncService *))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("global autoinc service is null", K(ret));
      } else if (OB_FAIL(gais->handle_next_autoinc_request(msg, rpc_result))) {
        LOG_WARN("post local gais require autoinc request failed", KR(ret), K(server), K(msg));
      } else if (!rpc_result.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("post local gais require autoinc and gais_rpc_result is invalid", KR(ret), K(server),
                  K(msg), K(rpc_result));
      } else {
        LOG_TRACE("post local require autoinc request success", K(msg), K(rpc_result));
      }
    }
    return ret;
    });
  }
  return ret;
}

int ObGAISRequestRpc::curr_autoinc_val(const ObAddr &server,
                                       const ObGAISAutoIncKeyArg &msg,
                                       ObGAISCurrValRpcResult &rpc_result)
{
  int ret = OB_SUCCESS;
  const uint64_t timeout = THIS_WORKER.get_timeout_remain();
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("gais request rpc not inited", KR(ret));
  } else if (!server.is_valid() || !msg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(server), K(msg));
  } else {
    // single-replica: target is always local; dispatch in-process
    ex_rpc::sync_call([&]{
    ObGlobalAutoIncService *gais = nullptr;
    const uint64_t tenant_id = msg.autoinc_key_.tenant_id_;
    MTL_SWITCH(tenant_id) {
      if (OB_ISNULL(gais = MTL(ObGlobalAutoIncService *))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("global autoinc service is null", K(ret));
      } else if (OB_FAIL(gais->handle_curr_autoinc_request(msg, rpc_result))) {
        LOG_WARN("post local gais get autoinc request failed", KR(ret), K(server), K(msg));
      } else {
        LOG_TRACE("post local get autoinc request success", K(msg), K(rpc_result));
      }
    }
    return ret;
    });
  }
  return ret;
}

int ObGAISRequestRpc::push_autoinc_val(const ObAddr &server,
                                       const ObGAISPushAutoIncValReq &msg,
                                       uint64_t &sync_value)
{
  int ret = OB_SUCCESS;
  const uint64_t timeout = THIS_WORKER.get_timeout_remain();
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("gais request rpc not inited", KR(ret));
  } else if (!server.is_valid() || !msg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(server), K(msg));
  } else {
    // single-replica: target is always local; dispatch in-process
    ex_rpc::sync_call([&]{
    ObGlobalAutoIncService *gais = nullptr;
    const uint64_t tenant_id = msg.autoinc_key_.tenant_id_;
    MTL_SWITCH(tenant_id) {
      if (OB_ISNULL(gais = MTL(ObGlobalAutoIncService *))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("global autoinc service is null", K(ret));
      } else if (OB_FAIL(gais->handle_push_autoinc_request(msg, sync_value))) {
        LOG_WARN("post local gais push global request failed", KR(ret), K(server), K(msg));
      } else {
        LOG_TRACE("post local gais push global request request success", K(msg), K(sync_value));
      }
    }
    return ret;
    });
  }
  return ret;
}

int ObGAISRequestRpc::clear_autoinc_cache(const ObAddr &server, const ObGAISAutoIncKeyArg &msg)
{
  int ret = OB_SUCCESS;
  const uint64_t timeout = THIS_WORKER.get_timeout_remain();
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("gais request rpc not inited", KR(ret));
  } else if (!server.is_valid() || !msg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(server), K(msg));
  } else {
    // single-replica: target is always local; dispatch in-process (resource RPC removed)
    ex_rpc::sync_call([&]{
    ObGlobalAutoIncService *gais = nullptr;
    const uint64_t tenant_id = msg.autoinc_key_.tenant_id_;
    MTL_SWITCH(tenant_id) {
      if (OB_ISNULL(gais = MTL(ObGlobalAutoIncService *))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("global autoinc service is null", K(ret));
      } else if (OB_FAIL(gais->handle_clear_autoinc_cache_request(msg))) {
        LOG_WARN("post local gais clear autoinc cache failed", KR(ret), K(server), K(msg));
      } else {
        LOG_TRACE("clear autoinc cache success", K(server), K(msg));
      }
    }
    return ret;
    });
  }
  return ret;
}

int ObGAISRequestRpc::broadcast_global_autoinc_cache(const ObGAISBroadcastAutoIncCacheReq &msg)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("gais request rpc not inited", KR(ret));
  } else if (!msg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(msg));
  } else {
    // single-replica: broadcast target is always self; nothing to do
  }
  return ret;
}

int ObGAISRequestRpc::next_sequence_val(const common::ObAddr &server,
                       const ObGAISNextSequenceValReq &msg,
                       ObGAISNextSequenceValRpcResult &rpc_result)
{
  int ret = OB_SUCCESS;
  const uint64_t timeout = THIS_WORKER.get_timeout_remain();
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("gais request rpc not inited", KR(ret));
  } else if (!server.is_valid() || !msg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(server), K(msg));
  } else {
    // single-replica: target is always local; dispatch in-process
    ex_rpc::sync_call([&]{
    ObGlobalAutoIncService *gais = nullptr;
    const uint64_t tenant_id = msg.schema_.get_tenant_id();
    MTL_SWITCH(tenant_id) {
      if (OB_ISNULL(gais = MTL(ObGlobalAutoIncService *))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("global autoinc service is null", K(ret));
      } else if (OB_FAIL(gais->handle_next_sequence_request(msg, rpc_result))) {
        LOG_WARN("post local gais require autoinc request failed", KR(ret), K(server), K(msg));
      } else {
        LOG_TRACE("post local require autoinc request success", K(msg), K(rpc_result));
      }
    }
    return ret;
    });
  }
  return ret;
}

} // share
} // oceanbase
