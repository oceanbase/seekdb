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

#ifndef _OB_TABLE_RPC_BINDING_H
#define _OB_TABLE_RPC_BINDING_H 1

// Table-API obcall RPC has been decommissioned: the obcall::ObTableRpcProxy
// client and the server-side dispatch (xlator registrations) are removed, so
// there is no live send/receive path. The Table-API server processors and the
// libtable helpers, however, are kept compiling. They used to obtain their
// PCODE / Request / Response type binding and their processor base class from
// the obcall TRANSPORT headers (the rpc obcall dir). This header now provides those
// pieces as small SELF-CONTAINED local stand-ins (no transport include)
// so that observer table no longer depends on the transport, which can later
// be deleted. None of this is reachable at runtime (dead RPC).

#include "lib/ob_define.h"               // UNUSED / OB_NOT_SUPPORTED / OB_SUCCESS
#include "share/table/ob_table_rpc_struct.h"
#include "rpc/frame/ob_req_processor.h"  // rpc::frame::ObReqProcessor (frame, NOT obcall transport)
#include "rpc/frame/ob_req_packet_code.h" // rpc::frame::ObReqPacketCode + OB_* pcode tags (self-contained, no transport)
#include "rpc/ob_request.h"             // rpc::ObRequest
#include "lib/compress/ob_compress_util.h"  // common::ObCompressorType / INVALID_COMPRESSOR

namespace oceanbase
{
namespace obcall
{
using namespace oceanbase::rpc::frame; // ObReqPacketCode + OB_* pcode tags

// ---------------------------------------------------------------------------
// Local stand-in for the obcall packet-code enum. Values mirror the historical
// obcall ob_call_packet_list ids so logs/diagnostics stay stable. Only the
// (now-dead) Table-API codes are kept; nothing dispatches on them.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Minimal local stand-in for obcall::ObCallPacket. The dead Table-API processor
// base holds a pointer to one of these (always NULL on the dead path) and the
// rerouting helper inspects require_rerouting()/get_timeout(). The full packet
// (de)serialization that lived on the transport is gone.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Minimal local stand-in for the deleted obcall::ObCallPacket. The dead
// Table-API processor base holds a pointer to one of these (always NULL on the
// dead path) and the rerouting helper inspects require_rerouting()/get_timeout().
// The full packet (de)serialization that lived on the transport is gone.
// ---------------------------------------------------------------------------
class ObCallPacket
{
public:
  bool require_rerouting() const { return false; }
  int64_t get_timeout() const { return 0; }
};

// ---------------------------------------------------------------------------
// pcode<->Request/Response compile-time binding (unchanged shape, transport-free).
// ---------------------------------------------------------------------------
template <ObReqPacketCode pcode>
struct ObTableRpcBinding {};

#define OB_DEFINE_TABLE_RPC_BINDING(pcode, Input, Output)   \
  template <>                                               \
  struct ObTableRpcBinding<pcode> {                         \
    static constexpr ObReqPacketCode PCODE = pcode;        \
    typedef Input Request;                                  \
    typedef Output Response;                                \
  }

OB_DEFINE_TABLE_RPC_BINDING(OB_TABLE_API_LOGIN, table::ObTableLoginRequest, table::ObTableLoginResult);
OB_DEFINE_TABLE_RPC_BINDING(OB_TABLE_API_EXECUTE, table::ObTableOperationRequest, table::ObTableOperationResult);
OB_DEFINE_TABLE_RPC_BINDING(OB_TABLE_API_BATCH_EXECUTE, table::ObTableBatchOperationRequest, table::ObTableBatchOperationResult);
OB_DEFINE_TABLE_RPC_BINDING(OB_TABLE_API_EXECUTE_QUERY, table::ObTableQueryRequest, table::ObTableQueryResult);
OB_DEFINE_TABLE_RPC_BINDING(OB_TABLE_API_QUERY_AND_MUTATE, table::ObTableQueryAndMutateRequest, table::ObTableQueryAndMutateResult);
OB_DEFINE_TABLE_RPC_BINDING(OB_TABLE_API_EXECUTE_QUERY_ASYNC, table::ObTableQueryAsyncRequest, table::ObTableQueryAsyncResult);
OB_DEFINE_TABLE_RPC_BINDING(OB_TABLE_API_DIRECT_LOAD, table::ObTableDirectLoadRequest, table::ObTableDirectLoadResult);
OB_DEFINE_TABLE_RPC_BINDING(OB_TABLE_API_LS_EXECUTE, table::ObTableLSOpRequest, table::ObTableLSOpResult);
OB_DEFINE_TABLE_RPC_BINDING(OB_REDIS_EXECUTE, table::ObTableOperationRequest, table::ObTableOperationResult);
OB_DEFINE_TABLE_RPC_BINDING(OB_REDIS_EXECUTE_V2, table::ObRedisRpcRequest, table::ObRedisResult);
OB_DEFINE_TABLE_RPC_BINDING(OB_TABLE_API_META_INFO_EXECUTE, table::ObTableMetaRequest, table::ObTableMetaResponse);

#undef OB_DEFINE_TABLE_RPC_BINDING

// ---------------------------------------------------------------------------
// Self-contained processor base for the (dead) Table-API server processors.
//
// They used to derive from obcall::ObCallProcessor<T> (the obcall RPC framework
// processor base). That framework is being deleted, so the Table-API processors
// now derive from these minimal local stand-ins instead, which depend only on
// the rpc::frame layer (ObReqProcessor) and the surviving obcall PACKET type.
// None of this is reachable at runtime: the Table-API RPC send/receive path and
// its xlator registrations were removed, so these processors are never run.
// Only enough of the old interface is reproduced to keep the Table-API feature
// code compiling.
// ---------------------------------------------------------------------------
class ObTableDeadProcessorBase : public rpc::frame::ObReqProcessor
{
public:
  ObTableDeadProcessorBase()
      : rpc_pkt_(NULL), is_stream_(false), is_stream_end_(false),
        require_rerouting_(false), preserve_recv_data_(false),
        result_compress_type_(common::INVALID_COMPRESSOR), timeout_(0)
  {}
  virtual ~ObTableDeadProcessorBase() {}

  // dead path: never dispatched, so run() just refuses.
  virtual int run() override { return common::OB_NOT_SUPPORTED; }

  virtual int process() = 0;
  virtual int deserialize() { return common::OB_SUCCESS; }
  virtual int before_process() { return common::OB_SUCCESS; }
  virtual int after_process(int error_code) { UNUSED(error_code); return common::OB_SUCCESS; }
  virtual int before_response(int error_code) { UNUSED(error_code); return common::OB_SUCCESS; }
  virtual int response(const int retcode) { UNUSED(retcode); return common::OB_SUCCESS; }
  int check_timeout() { return common::OB_SUCCESS; }
  virtual int flush(int64_t wait_timeout = 0, const common::ObAddr *src_addr = NULL)
  { UNUSED(wait_timeout); UNUSED(src_addr); return common::OB_SUCCESS; }
  void set_timeout(uint64_t timeout) { timeout_ = timeout; }

  void set_preserve_recv_data() { preserve_recv_data_ = true; }
  void set_result_compress_type(common::ObCompressorType t) { result_compress_type_ = t; }

protected:
  // Always NULL on the dead path (the live transport that populated it is gone).
  const ObCallPacket *rpc_pkt_;
  bool is_stream_;
  bool is_stream_end_;
  bool require_rerouting_;
  bool preserve_recv_data_;
  common::ObCompressorType result_compress_type_;
  uint64_t timeout_;
};

template <class T>
class ObTableDeadProcessor : public ObTableDeadProcessorBase
{
public:
  static constexpr ObReqPacketCode PCODE = T::PCODE;
public:
  ObTableDeadProcessor() {}
  virtual ~ObTableDeadProcessor() {}
  virtual int process() = 0;
public:
  typename T::Request arg_;
  typename T::Response result_;
};

}  // end namespace obcall
}  // end namespace oceanbase

#endif /* _OB_TABLE_RPC_BINDING_H */
