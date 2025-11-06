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

#ifndef OCEANBASE_RPC_OBRPC_OB_RPC_PROXY_IPP_
#define OCEANBASE_RPC_OBRPC_OB_RPC_PROXY_IPP_

#include "lib/compress/ob_compressor.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/serialization.h"
#include "lib/utility/utility.h"
#include "lib/oblog/ob_trace_log.h"
#include "lib/stat/ob_diagnose_info.h"
#include "lib/statistic_event/ob_stat_event.h"
#include "lib/trace/ob_trace.h"
#include "rpc/obrpc/ob_irpc_extra_payload.h"
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "rpc/obrpc/ob_local_procedure_call.h"

namespace oceanbase
{
namespace obrpc
{

template <class pcodeStruct>
SSHandle<pcodeStruct>::~SSHandle()
{
  if (true == has_more_ && first_pkt_id_ != INVALID_RPC_PKT_ID) {
    RPC_OBRPC_LOG_RET(WARN, OB_ERROR, "stream rpc is forgotten to abort", K_(pcode), K_(first_pkt_id));
    this->abort();
  }
}

template <class pcodeStruct>
bool SSHandle<pcodeStruct>::has_more() const
{
  return has_more_;
}

template <class pcodeStruct>
int SSHandle<pcodeStruct>::get_more(typename pcodeStruct::Response &result)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  RPC_OBRPC_LOG(ERROR, "it should not be reach here, observer-lite is not supported for stream rpc", K(has_more_), K(pcode_));
  ob_abort();
  return ret;
}

template <class pcodeStruct>
int SSHandle<pcodeStruct>::abort()
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  RPC_OBRPC_LOG(ERROR, "it should not be reach here, observer-lite is not supported for stream rpc", K(has_more_), K(pcode_));
  ob_abort();
  return ret;
}

template <class pcodeStruct>
const ObRpcResultCode &SSHandle<pcodeStruct>::get_result_code() const
{
  return rcode_;
}

template <class pcodeStruct>
int ObRpcProxy::AsyncCB<pcodeStruct>::decode(void *pkt)
{
  using namespace oceanbase::common;
  using namespace rpc::frame;
  int ret   = OB_SUCCESS;

  if (OB_ISNULL(pkt)) {
    ret = OB_INVALID_ARGUMENT;
    RPC_OBRPC_LOG(WARN, "pkt should not be NULL", K(ret));
  }

  if (OB_SUCC(ret)) {
    ObRpcPacket  *rpkt = reinterpret_cast<ObRpcPacket*>(pkt);
    const char   *buf  = rpkt->get_cdata();
    int64_t      len   = rpkt->get_clen();
    int64_t      pos   = 0;
    UNIS_VERSION_GUARD(rpkt->get_unis_version());
    char *uncompressed_buf = NULL;

    if (OB_FAIL(rpkt->verify_checksum())) {
      RPC_OBRPC_LOG(ERROR, "verify checksum fail", K(*rpkt), K(ret));
    }
    if (OB_SUCC(ret)) {
      const common::ObCompressorType &compressor_type = rpkt->get_compressor_type();
      if (common::INVALID_COMPRESSOR != compressor_type) {
        // uncompress
        const int32_t original_len = rpkt->get_original_len();
        common::ObCompressor *compressor = NULL;
        int64_t dst_data_size = 0;
        if (OB_FAIL(common::ObCompressorPool::get_instance().get_compressor(compressor_type, compressor))) {
          RPC_OBRPC_LOG(WARN, "get_compressor failed", K(ret), K(compressor_type));
        } else if (NULL == (uncompressed_buf =
                                   static_cast<char *>(common::ob_malloc(original_len, common::ObModIds::OB_RPC)))) {
          ret = common::OB_ALLOCATE_MEMORY_FAILED;
          RPC_OBRPC_LOG(WARN, "Allocate memory failed", K(ret));
        } else if (OB_FAIL(compressor->decompress(buf, len, uncompressed_buf, original_len, dst_data_size))) {
          RPC_OBRPC_LOG(WARN, "decompress failed", K(ret));
        } else if (dst_data_size != original_len) {
          ret = common::OB_ERR_UNEXPECTED;
          RPC_OBRPC_LOG(ERROR, "decompress len not match", K(ret), K(dst_data_size), K(original_len));
        } else {
          RPC_OBRPC_LOG(DEBUG, "uncompress result success", K(compressor_type), K(len), K(original_len));
          // replace buf
          buf = uncompressed_buf;
          len = original_len;
        }
      }
    }
    if (OB_SUCC(ret)) {
      ACTIVE_SESSION_FLAG_SETTER_GUARD(in_rpc_decode);
      if (OB_FAIL(rcode_.deserialize(buf, len, pos))) {
        RPC_OBRPC_LOG(WARN, "decode result code fail", K(*rpkt), K(ret));
      } else if (rcode_.rcode_ != OB_SUCCESS) {
        // RPC_OBRPC_LOG(WARN, "execute rpc fail", K_(rcode));
      } else if (OB_FAIL(result_.deserialize(buf, len, pos))) {
        RPC_OBRPC_LOG(WARN, "decode packet fail", K(ret));
      } else {
        // do nothing
      }
    }
    // free the uncompress buffer
    if (NULL != uncompressed_buf) {
      common::ob_free(uncompressed_buf);
      uncompressed_buf = NULL;
    }
  }
  return ret;
}
template <class pcodeStruct>
int ObRpcProxy::AsyncCB<pcodeStruct>::get_rcode()
{
  return rcode_.rcode_;
}

/*
template <class pcodeStruct>
void ObRpcProxy::AsyncCB<pcodeStruct>::check_request_rt(const bool force_print)
{
  if (force_print
      || req_->client_send_time - req_->client_start_time > REQUEST_ITEM_COST_RT
      || req_->client_connect_time - req_->client_send_time > REQUEST_ITEM_COST_RT
      || req_->client_write_time - req_->client_connect_time > REQUEST_ITEM_COST_RT
      || req_->request_arrival_time - req_->client_write_time > REQUEST_ITEM_COST_RT
      || req_->arrival_push_diff > REQUEST_ITEM_COST_RT
      || req_->push_pop_diff > REQUEST_ITEM_COST_RT
      || req_->pop_process_start_diff > REQUEST_ITEM_COST_RT
      || req_->process_start_end_diff > REQUEST_ITEM_COST_RT
      || req_->process_end_response_diff > REQUEST_ITEM_COST_RT
      || (req_->client_read_time - req_->request_arrival_time - req_->arrival_push_diff - req_->push_pop_diff
        - req_->pop_process_start_diff - req_->process_start_end_diff - req_->process_end_response_diff) > REQUEST_ITEM_COST_RT
      || req_->client_end_time - req_->client_read_time > REQUEST_ITEM_COST_RT) {

    if (TC_REACH_TIME_INTERVAL(100 * 1000)) {
      _RPC_OBRPC_LOG(INFO,
          "rpc time, packet_id :%lu, client_start_time :%ld, start_send_diff :%ld, "
          "send_connect_diff: %ld, connect_write_diff: %ld, request_fly_ts: %ld, "
          "arrival_push_diff: %d, push_pop_diff: %d, pop_process_start_diff :%d, "
          "process_start_end_diff: %d, process_end_response_diff: %d, response_fly_ts: %ld, "
          "read_end_diff: %ld, client_end_time :%ld",
          req_->packet_id,
          req_->client_start_time,
          req_->client_send_time - req_->client_start_time,
          req_->client_connect_time - req_->client_send_time,
          req_->client_write_time - req_->client_connect_time,
          req_->request_arrival_time - req_->client_write_time,
          req_->arrival_push_diff,
          req_->push_pop_diff,
          req_->pop_process_start_diff,
          req_->process_start_end_diff,
          req_->process_end_response_diff,
          req_->client_read_time - req_->request_arrival_time - req_->arrival_push_diff - req_->push_pop_diff
          - req_->pop_process_start_diff - req_->process_start_end_diff - req_->process_end_response_diff,
          req_->client_end_time - req_->client_read_time,
          req_->client_end_time);
    }
  }
}
*/

template <typename Input, typename Out>
int ObRpcProxy::rpc_call(ObRpcPacketCode pcode, const Input &args,
                         Out &result, Handle *handle, const ObRpcOpts &opts)
{
  using namespace oceanbase::common;
  using namespace rpc::frame;
  int ret = OB_SUCCESS;
  UNIS_VERSION_GUARD(opts.unis_version_);

  const int64_t start_ts = ObTimeUtility::current_time();

  if (!init_) {
    ret = OB_NOT_INIT;
    RPC_OBRPC_LOG(WARN, "Rpc proxy not inited", K(ret));
  } else if (!active_) {
    ret = OB_INACTIVE_RPC_PROXY;
    RPC_OBRPC_LOG(WARN, "Rpc proxy inactive", K(ret));
  } else {
    //do nothing
  }
  if (dst_ == ObRpcProxy::myaddr_) {
    ret = oceanbase::oblpc::send(*this, pcode, args, result, handle, opts);
  } else {
    ret = OB_NOT_SUPPORTED;
    RPC_OBRPC_LOG(ERROR, "send rpc to other dst is not supported", K_(dst));
  }

  return ret;
}

template <class pcodeStruct>
int ObRpcProxy::rpc_post(const typename pcodeStruct::Request &args,
                         AsyncCB<pcodeStruct> *cb, const ObRpcOpts &opts)
{
  using namespace oceanbase::common;
  using namespace rpc::frame;
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  UNIS_VERSION_GUARD(opts.unis_version_);

  common::ObTimeGuard timeguard("rpc post", 10 * 1000);
  const int64_t start_ts = ObTimeUtility::current_time();

  if (!init_) {
    ret = OB_NOT_INIT;
    RPC_OBRPC_LOG(WARN, "rpc not inited", K(ret));
  } else if (!active_) {
    ret = OB_INACTIVE_RPC_PROXY;
    RPC_OBRPC_LOG(WARN, "rpc is inactive", K(ret));
  }
  if (dst_ == ObRpcProxy::myaddr_) {
    ret = oceanbase::oblpc::post(*this, pcodeStruct::PCODE, args, cb, opts);
  } else {
    ret = OB_NOT_SUPPORTED;
    RPC_OBRPC_LOG(ERROR, "send rpc to other dst is not supported", K_(dst));
  }
  return ret;
}




} // end of namespace rpc
} // end of namespace oceanbase


#endif //OCEANBASE_RPC_OBRPC_OB_RPC_PROXY_IPP_
