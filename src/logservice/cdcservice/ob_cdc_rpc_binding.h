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

#ifndef OCEANBASE_LOGSERVICE_OB_CDC_RPC_BINDING
#define OCEANBASE_LOGSERVICE_OB_CDC_RPC_BINDING

// CDC obcall RPC has been decommissioned: the obcall ObCdcProxy send path
// (CALL_S / CALL_AP over the obcall transport) is gone and the actual log pull
// now runs over gRPC. The logfetcher (CDC log-pull client) still keeps the old
// async callback class hierarchy compiling: its CB objects derive from
// ObCdcProxy::AsyncCB<pcode>. This header used to obtain that base, the pcode
// enum, and rpc::frame::ObResultCode from the obcall TRANSPORT headers. They are now
// provided as small SELF-CONTAINED local stand-ins (no transport include) so
// cdcservice/logfetcher no longer depend on the transport, which can later be
// deleted. None of the obcall CDC RPC is reachable at runtime (dead).

#include "rpc/frame/ob_req_transport.h"   // rpc::frame::ObReqTransport::AsyncCB (frame base, NOT obcall send path)
#include "rpc/frame/ob_req_packet_code.h" // rpc::frame::ObReqPacketCode + OB_* pcode tags (self-contained, no transport)
#include "rpc/frame/ob_result_code.h" // rpc::frame::ObResultCode (backbone)
#include "lib/ob_define.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/container/ob_se_array.h"
#include "lib/oblog/ob_warning_buffer.h"
#include "ob_cdc_req.h"
#include "ob_cdc_raw_log_req.h"

namespace oceanbase
{
namespace obcall
{
using namespace oceanbase::rpc::frame; // ObReqPacketCode + OB_* pcode tags

// ---------------------------------------------------------------------------
// Local stand-in for the obcall packet-code enum. Values mirror the historical
// obcall ob_call_packet_list ids so logs/diagnostics stay stable. Only the
// (now-dead) CDC codes are kept; nothing dispatches on them.
//
// NOTE: rpc/frame/ob_req_transport.h still forward-uses rpc::frame::ObReqPacketCode
// in its AsyncCB::get_pcode(); that frame header (not part of this module) is
// the only place the transport enum is needed, and it is included above. The
// names below are the CDC subset used by logfetcher.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Local copy of rpc::frame::ObResultCode (was the obcall ob_result_code.header).
// It is a self-contained serializable struct (only lib deps) used by the CDC
// async callbacks to carry the rpc return code + warnings. Kept transport-free.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Local stand-in for the deleted obcall ObCdcProxy. Provides only the type
// binding (ObRpc<pcode>) and a transport-free AsyncCB<pcode> base that the
// logfetcher CB classes derive from. The base derives from the RPC *frame*
// transport AsyncCB (rpc/frame, not the obcall send path) so the existing
// clone()/process()/on_timeout()/on_invalid() overrides and dst_ still work.
// There is no obcall send method here; the live fetch path is gRPC.
// ---------------------------------------------------------------------------
struct ObCdcProxy
{
  template <ObReqPacketCode pcode, typename IGNORE = void>
  struct ObRpc {};

  // Self-contained async-callback base (replaces obcall::ObCallProxy::AsyncCB).
  template <class pcodeStruct>
  class ObCdcDeadAsyncCB : public rpc::frame::ObReqTransport::AsyncCB
  {
  protected:
    using Request = typename pcodeStruct::Request;
    using Response = typename pcodeStruct::Response;

  public:
    ObCdcDeadAsyncCB() : rpc::frame::ObReqTransport::AsyncCB(pcodeStruct::PCODE) { cloned_ = false; }
    virtual ~ObCdcDeadAsyncCB() { reset_rcode(); }
    // obcall wire decode is gone (dead RPC); decode is never invoked.
    virtual int decode(void *pkt) override { UNUSED(pkt); return common::OB_NOT_SUPPORTED; }
    virtual int get_rcode() override { return rcode_.rcode_; }
    virtual void reset_rcode() override { rcode_.reset(); }
    virtual void set_cloned(bool cloned) override { cloned_ = cloned; }
    virtual bool get_cloned() override { return cloned_; }
    Response &result() { return result_; }
    rpc::frame::ObResultCode &rcode() { return rcode_; }

    virtual void set_args(const Request &arg) = 0;
    virtual void destroy() {}

  protected:
    bool cloned_;
    Response result_;
    rpc::frame::ObResultCode rcode_;
  };

  // Alias kept under the historical name so derived classes need no rename.
  template <ObReqPacketCode pcode>
  using AsyncCB = ObCdcDeadAsyncCB<ObRpc<pcode> >;
};

template <typename IGNORE>
struct ObCdcProxy::ObRpc<OB_LOG_REQ_START_LSN_BY_TS, IGNORE>
{
  static constexpr ObReqPacketCode PCODE = OB_LOG_REQ_START_LSN_BY_TS;
  typedef ObCdcReqStartLSNByTsReq Request;
  typedef ObCdcReqStartLSNByTsResp Response;
};

template <typename IGNORE>
struct ObCdcProxy::ObRpc<OB_LS_FETCH_LOG2, IGNORE>
{
  static constexpr ObReqPacketCode PCODE = OB_LS_FETCH_LOG2;
  typedef ObCdcLSFetchLogReq Request;
  typedef ObCdcLSFetchLogResp Response;
};

template <typename IGNORE>
struct ObCdcProxy::ObRpc<OB_LS_FETCH_MISSING_LOG, IGNORE>
{
  static constexpr ObReqPacketCode PCODE = OB_LS_FETCH_MISSING_LOG;
  typedef ObCdcLSFetchMissLogReq Request;
  typedef ObCdcLSFetchLogResp Response;
};

template <typename IGNORE>
struct ObCdcProxy::ObRpc<OB_CDC_FETCH_RAW_LOG, IGNORE>
{
  static constexpr ObReqPacketCode PCODE = OB_CDC_FETCH_RAW_LOG;
  typedef ObCdcFetchRawLogReq Request;
  typedef ObCdcFetchRawLogResp Response;
};

} // namespace obcall
} // namespace oceanbase

#endif
