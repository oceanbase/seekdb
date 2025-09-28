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

#define USING_LOG_PREFIX RPC_OBRPC
#include "rpc/obrpc/ob_rpc_proxy.h"
#include "rpc/obrpc/ob_rpc_net_handler.h"


namespace oceanbase
{
namespace obrpc
{
ObRpcProxy::NoneT None;
}; // end namespace obrpc
}; // end namespace oceanbase

using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::rpc;
using namespace oceanbase::obrpc;
using namespace oceanbase::rpc::frame;

ObAddr ObRpcProxy::myaddr_;


Handle::Handle()
    : has_more_(false),
      dst_(),
      sessid_(0L),
      opts_(),
      transport_(NULL),
      proxy_(),
      pcode_(OB_INVALID_RPC_CODE),
      first_pkt_id_(INVALID_RPC_PKT_ID),
      abs_timeout_ts_(OB_INVALID_TIMESTAMP)
{}

void Handle::reset_timeout()
{
  abs_timeout_ts_ = ObTimeUtility::current_time() + proxy_.timeout();
}

int ObRpcProxy::init(const ObReqTransport *transport,
    const oceanbase::common::ObAddr &dst)
{
  return init(transport, ObRpcNetHandler::CLUSTER_ID, dst);
}

int ObRpcProxy::init(const ObReqTransport *transport,
    const int64_t src_cluster_id,
    const oceanbase::common::ObAddr &dst)
{
  int ret = OB_SUCCESS;

  if (init_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("Rpc proxy not inited", K(ret));
  } else {
    transport_ = transport;
    src_cluster_id_ = src_cluster_id;
    dst_ = dst;
    init_ = true;
    int enable_poc_rpc = atoi(getenv("enable_poc_rpc")?:"1");
    if (enable_poc_rpc > 0) {
      transport_impl_ = 1;
    } else {
      transport_impl_ = 0;
    }
  }

  return ret;
}
const ObRpcResultCode &ObRpcProxy::get_result_code() const
{
  return rcode_;
}

int ObRpcProxy::init_pkt(
    ObRpcPacket *pkt, ObRpcPacketCode pcode, const ObRpcOpts &opts,
    const bool unneed_response) const
{
  int ret = OB_SUCCESS;
  const uint64_t* trace_id = common::ObCurTraceId::get();
  if (OB_ISNULL(trace_id)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("Trace id should not be NULL", K(ret), K(trace_id));
  } else if (OB_ISNULL(pkt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Input pkt is NULL", K(ret), K(pkt));
  } else if (0 == trace_id[0]) {
    common::ObCurTraceId::init(myaddr_);
    pkt->set_trace_id(common::ObCurTraceId::get());
    common::ObCurTraceId::reset();
  } else {
    pkt->set_trace_id(common::ObCurTraceId::get());
  }

#ifdef ERRSIM
  pkt->set_module_type(THIS_WORKER.get_module_type());
#endif

  if (OB_SUCC(ret)) {
    pkt->set_pcode(pcode);
    //Assign a channel id to this new packet
    uint32_t new_chid = ATOMIC_AAF(&ObRpcPacket::global_chid, 1);
    pkt->set_chid(new_chid);
    pkt->set_timeout(timeout_);
    pkt->set_priority(opts.pr_);
    pkt->set_session_id(0);
    int8_t log_level = common::ObThreadLogLevelUtils::get_level();
    // for log level compatibility, disable thread log level while upgrading.
    if (OB_LOGGER.is_info_as_wdiag()) {
      log_level = OB_LOG_LEVEL_NONE;
    }
    pkt->set_log_level(log_level);
    pkt->set_tenant_id(tenant_id_);
    pkt->set_priv_tenant_id(priv_tenant_id_);
    pkt->set_timestamp(ObTimeUtility::current_time());
    pkt->set_dst_cluster_id(dst_cluster_id_);
    // For request, src_cluster_id must be the cluster_id of this cluster, directly hard-coded
    pkt->set_src_cluster_id(src_cluster_id_);
    pkt->set_unis_version(0);
    const int OBCG_LQ = 100;
    if (0 != get_group_id()) {
      pkt->set_group_id(get_group_id());
    } else if (this_worker().get_group_id() == OBCG_LQ ||
               (is_resource_manager_group(this_worker().get_group_id()) && ob_get_tenant_id() != tenant_id_)) {
      pkt->set_group_id(0);
    } else {
      pkt->set_group_id(this_worker().get_group_id());
    }
    if (need_increment_request_level(pcode)) {
      if (this_worker().get_worker_level() == INT32_MAX) { // The inner sql request is not sent from the tenant thread, so the worker level is still the initial value, given
                                                   // inner sql a special nesting level
        pkt->set_request_level(5);
      } else {
        pkt->set_request_level(this_worker().get_curr_request_level() + 1);
      }
    } else {
      // When request_level <2 is still processed according to the original tenant thread, so internal requests can also be set to 0
      pkt->set_request_level(0);
    }
    pkt->calc_checksum();
    if (unneed_response) {
      pkt->set_unneed_response();
    }
  }
  return ret;
}





void ObRpcProxy::set_handle_attr(Handle* handle, const ObRpcPacketCode& pcode, const ObRpcOpts& opts, bool is_stream_next, int64_t session_id, int64_t pkt_id, int64_t send_ts) {
  if (handle) {
    handle->pcode_ = pcode;
    handle->opts_ = opts;
    handle->has_more_ = is_stream_next;
    handle->sessid_ = session_id;
    handle->dst_ = dst_;
    handle->proxy_ = *this;
    handle->do_ratelimit_ = do_ratelimit_;
    handle->is_bg_flow_ = is_bg_flow_;
    handle->transport_ = NULL;
    handle->abs_timeout_ts_ = send_ts + timeout_;
    if (is_stream_next) {
      handle->first_pkt_id_ = pkt_id;
      LOG_INFO("stream rpc register", K(pcode), K(pkt_id));
      stream_rpc_register(pkt_id, send_ts);
    }
    int64_t timeout = min(timeout_, INT64_MAX/2);
    handle->abs_timeout_ts_ = send_ts + timeout_;
  }
}
