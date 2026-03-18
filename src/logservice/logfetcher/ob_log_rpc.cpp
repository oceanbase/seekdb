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

#define USING_LOG_PREFIX  OBLOG

#include "ob_log_rpc.h"
#include "ob_log_trace_id.h"

#include "lib/utility/ob_macro_utils.h"   // OB_FAIL
#include "lib/oblog/ob_log_module.h"      // LOG_ERROR

#include "ob_log_config.h"                // ObLogFetcherConfig
#include "observer/ob_srv_network_frame.h"
#include "grpc/logservice.grpc.pb.h"      // LogService gRPC
#include "grpc/ob_grpc_context.h"         // serialize_ob_to_proto, deserialize_proto_to_ob
#ifdef OB_BUILD_TDE_SECURITY
#include "share/ob_encrypt_kms.h"         // ObSSLClient
#endif

/// The rpc proxy executes the RPC function with two error codes:
/// 1. proxy function return value ret
/// 2. the result code carried by the proxy itself: proxy.get_result_code().rcode_, which indicates the error code returned by RPC processing on the target server
///
/// The two error codes above are related.
/// 1. Synchronous RPC
///   + on success of ret, the result code must be OB_SUCCESS
///   + ret failure, result code failure means that the RPC failed to process on the target machine and returned the packet, including process processing failure, tenant not present, etc.
/// result code success means local RPC delivery failed, or the remote server machine is unresponsive for a long time, no packet return, etc.

/// 2. Asynchronous RPC
///   + result code returned by proxy is meaningless because RPC is executed asynchronously and does not wait for packets to be returned to set the result code
///   + ret failure only means that the local RPC framework sent an error, excluding the case of no packet return from the target server
///
/// Based on the above analysis, for the caller sending the RPC, only the ret return value is of concern, and ret can completely replace the result code

#define SEND_RPC(RPC, tenant_id, SVR, TIMEOUT, REQ, ARG) \
    do { \
      if (IS_NOT_INIT) { \
        ret = OB_NOT_INIT; \
        LOG_ERROR("ObLogRpc not init", KR(ret), K(tenant_id)); \
      } else { \
        obrpc::ObCdcProxy proxy; \
        if (OB_FAIL(net_client_.get_proxy(proxy))) { \
          LOG_ERROR("net client get proxy fail", KR(ret)); \
        } else {\
          int64_t max_rpc_proc_time = \
                  ATOMIC_LOAD(&ObLogRpc::g_rpc_process_handler_time_upper_limit); \
          proxy.set_server((SVR)); \
          if (OB_FAIL(proxy.dst_cluster_id(cluster_id_).by(tenant_id).group_id(share::OBCG_CDCSERVICE) \
                                  .compressed(ATOMIC_LOAD(&compressor_type_)) \
                                  .trace_time(true).timeout((TIMEOUT))\
                                  .max_process_handler_time(static_cast<int32_t>(max_rpc_proc_time))\
                                  .RPC((REQ), (ARG)))) { \
            LOG_ERROR("rpc fail: " #RPC, "tenant_id", tenant_id, "svr", (SVR), "rpc_ret", ret, \
                "result_code", proxy.get_result_code().rcode_, "req", (REQ)); \
          } \
        } \
      } \
    } while(0)

using namespace oceanbase::common;
using namespace oceanbase::obrpc;

namespace oceanbase
{
namespace logfetcher
{

int64_t ObLogRpc::g_rpc_process_handler_time_upper_limit =
    ObLogFetcherConfig::default_rpc_process_handler_time_upper_limit_msec * _MSEC_;

ObLogRpc::ObLogRpc() :
    is_inited_(false),
    cluster_id_(OB_INVALID_CLUSTER_ID),
    self_tenant_id_(OB_INVALID_TENANT_ID),
    client_type_(obrpc::ObCdcClientType::CLIENT_TYPE_UNKNOWN),
    net_client_(),
    last_ssl_info_hash_(UINT64_MAX),
    ssl_key_expired_time_(0),
    client_id_(),
    cfg_(nullptr),
    external_info_val_(),
    compressor_type_(common::INVALID_COMPRESSOR),
    grpc_client_map_(),
    grpc_client_map_lock_()
{
  external_info_val_[0] = '\0';
}

ObLogRpc::~ObLogRpc()
{
  destroy();
}

// #ifdef ERRSIM
ERRSIM_POINT_DEF(ERRSIM_FETCH_LOG_TIME_OUT);
// #endif

int ObLogRpc::req_start_lsn_by_tstamp(const uint64_t tenant_id,
    const common::ObAddr &svr,
    obrpc::ObCdcReqStartLSNByTsReq &req,
    obrpc::ObCdcReqStartLSNByTsResp &resp,
    const int64_t timeout)
{
  int ret = OB_SUCCESS;
  req.set_client_id(client_id_);
  if (1 == cfg_->test_mode_force_fetch_archive) {
    req.set_flag(ObCdcRpcTestFlag::OBCDC_RPC_FETCH_ARCHIVE);
  }
  SEND_RPC(req_start_lsn_by_ts, tenant_id, svr, timeout, req, resp);
  LOG_INFO("rpc: request start LSN by tstamp", KR(ret), K(tenant_id), K(svr), K(timeout), K(req), K(resp));
  return ret;
}

int ObLogRpc::async_stream_fetch_log(const uint64_t tenant_id,
    const common::ObAddr &svr,
    obrpc::ObCdcLSFetchLogReq &req,
    obrpc::ObCdcProxy::AsyncCB<obrpc::OB_LS_FETCH_LOG2> &cb,
    const int64_t timeout)
{
  int ret = OB_SUCCESS;
  req.set_client_id(client_id_);
  req.set_tenant_id(self_tenant_id_);
  req.set_client_type(client_type_);
  if (1 == cfg_->test_mode_force_fetch_archive) {
    req.set_flag(ObCdcRpcTestFlag::OBCDC_RPC_FETCH_ARCHIVE);
  }
  if (1 == cfg_->test_mode_switch_fetch_mode) {
    req.set_flag(ObCdcRpcTestFlag::OBCDC_RPC_TEST_SWITCH_MODE);
  }
  req.set_compressor_type(ATOMIC_LOAD(&compressor_type_));

  // Use new gRPC async unary RPC
  // Serialize OB request to proto, then call gRPC layer
  // Create adapter callback on heap to ensure it lives until async callback completes
  // Note: cb's lifetime is managed by caller (e.g., LogGroupEntryRpcRequest::cb_)
  class GrpcCallbackAdapter : public ObGrpcAsyncCallback<::logservice::FetchLogRes> {
  public:
    GrpcCallbackAdapter(obrpc::ObCdcProxy::AsyncCB<obrpc::OB_LS_FETCH_LOG2> &old_cb)
      : old_cb_(old_cb) {}

    void on_success(const ::logservice::FetchLogRes &proto_resp) override {
      int ret = OB_SUCCESS;
      auto resp = std::make_unique<ObCdcLSFetchLogResp>();
      obrpc::ObRpcResultCode rcode;
      rcode.rcode_ = OB_SUCCESS;

      // Deserialize proto response to OB object
      if (OB_FAIL(deserialize_proto_to_ob(proto_resp, *resp))) {
        LOG_WARN("failed to deserialize FetchLogRes", K(ret));
        rcode.rcode_ = ret;
        old_cb_.rcode() = rcode;
        (void)old_cb_.process();
      } else {
        if (OB_FAIL(old_cb_.result().assign(*resp))) {
          LOG_WARN("failed to assign ObCdcLSFetchLogResp", K(ret));
          rcode.rcode_ = ret;
        } else {
          old_cb_.rcode() = rcode;
        }
        (void)old_cb_.process();
      }
    }

    void on_error(const grpc::Status &status) override {
      obrpc::ObRpcResultCode rcode;
      rcode.rcode_ = translate_error(status);
      old_cb_.rcode() = rcode;
      old_cb_.on_timeout();
    }

  private:
    obrpc::ObCdcProxy::AsyncCB<obrpc::OB_LS_FETCH_LOG2> &old_cb_;

    int translate_error(const grpc::Status &status) {
      if (status.ok()) {
        return OB_SUCCESS;
      } else if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
        return OB_TIMEOUT;
      } else if (status.error_code() == grpc::StatusCode::UNAVAILABLE) {
        return OB_RPC_CONNECT_ERROR;
      } else {
        return OB_ERR_SYS;
      }
    }
  };

  // Serialize OB request to proto
  ::logservice::FetchLogReq proto_req;
  if (OB_FAIL(serialize_ob_to_proto(req, &proto_req))) {
    LOG_WARN("failed to serialize ObCdcLSFetchLogReq", K(ret), K(req));
  } else {
// #ifdef ERRSIM
    if (OB_SUCCESS != ERRSIM_FETCH_LOG_TIME_OUT &&
      OB_TIMEOUT != ERRSIM_FETCH_LOG_TIME_OUT) {
      ret = ERRSIM_FETCH_LOG_TIME_OUT;
    } else if (OB_TIMEOUT == ERRSIM_FETCH_LOG_TIME_OUT) {
      // Allocate adapter on heap - it will be deleted in async callback
      GrpcCallbackAdapter *adapter = new GrpcCallbackAdapter(cb);
      if (OB_FAIL((async_fetch_log_grpc<::logservice::FetchLogReq, ::logservice::FetchLogRes>(
          tenant_id, svr, proto_req, adapter, 1)))) {
        LOG_WARN("failed to call async_fetch_log_grpc", K(ret), K(svr), K(timeout), K(req));
        delete adapter;  // Clean up if call fails immediately
      }
    } else {
// #endif
    // Allocate adapter on heap - it will be deleted in async callback
    GrpcCallbackAdapter *adapter = new GrpcCallbackAdapter(cb);
    if (OB_FAIL((async_fetch_log_grpc<::logservice::FetchLogReq, ::logservice::FetchLogRes>(
        tenant_id, svr, proto_req, adapter, timeout)))) {
      LOG_WARN("failed to call async_fetch_log_grpc", K(ret), K(svr), K(timeout), K(req));
      delete adapter;  // Clean up if call fails immediately
    }
// #ifdef ERRSIM
    }
// #endif
  }

  LOG_TRACE("rpc: async fetch log (grpc)", KR(ret), K(svr), K(timeout), K(req));
  return ret;
}

int ObLogRpc::async_stream_fetch_missing_log(const uint64_t tenant_id,
    const common::ObAddr &svr,
    obrpc::ObCdcLSFetchMissLogReq &req,
    obrpc::ObCdcProxy::AsyncCB<obrpc::OB_LS_FETCH_MISSING_LOG> &cb,
    const int64_t timeout)
{
  int ret = OB_SUCCESS;
  req.set_client_id(client_id_);
  req.set_tenant_id(self_tenant_id_);
  if (1 == cfg_->test_mode_force_fetch_archive) {
    req.set_flag(ObCdcRpcTestFlag::OBCDC_RPC_FETCH_ARCHIVE);
  }
  req.set_compressor_type(ATOMIC_LOAD(&compressor_type_));

  // Use new gRPC async unary RPC
  // Create adapter callback to convert proto response to OB object and call old callback
  class GrpcCallbackAdapter : public ObGrpcAsyncCallback<::logservice::FetchMissingLogRes> {
  public:
    GrpcCallbackAdapter(obrpc::ObCdcProxy::AsyncCB<obrpc::OB_LS_FETCH_MISSING_LOG> &old_cb)
      : old_cb_(old_cb) {}

    void on_success(const ::logservice::FetchMissingLogRes &proto_resp) override {
      int ret = OB_SUCCESS;
      // Deserialize proto response to OB object
      auto resp = std::make_unique<ObCdcLSFetchLogResp>();
      obrpc::ObRpcResultCode rcode;
      rcode.rcode_ = OB_SUCCESS;

      if (OB_FAIL(deserialize_proto_to_ob(proto_resp, *resp))) {
        LOG_WARN("failed to deserialize FetchMissingLogRes", K(ret));
        rcode.rcode_ = ret;
        old_cb_.rcode() = rcode;
        (void)old_cb_.process();
      } else {
        // Set result in old callback and call process()
        if (OB_FAIL(old_cb_.result().assign(*resp))) {
          LOG_WARN("failed to assign ObCdcLSFetchLogResp", K(ret));
          rcode.rcode_ = ret;
        } else {
          old_cb_.rcode() = rcode;
        }
        (void)old_cb_.process();
      }
    }

    void on_error(const grpc::Status &status) override {
      obrpc::ObRpcResultCode rcode;
      rcode.rcode_ = translate_error(status);
      old_cb_.rcode() = rcode;
      old_cb_.on_timeout();
    }

  private:
    obrpc::ObCdcProxy::AsyncCB<obrpc::OB_LS_FETCH_MISSING_LOG> &old_cb_;

    int translate_error(const grpc::Status &status) {
      // Translate gRPC status to OB error code
      if (status.ok()) {
        return OB_SUCCESS;
      } else if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
        return OB_TIMEOUT;
      } else if (status.error_code() == grpc::StatusCode::UNAVAILABLE) {
        return OB_RPC_CONNECT_ERROR;
      } else {
        return OB_ERR_SYS;
      }
    }
  };

  // Serialize OB request to proto
  ::logservice::FetchMissingLogReq proto_req;
  if (OB_FAIL(serialize_ob_to_proto(req, &proto_req))) {
    LOG_WARN("failed to serialize ObCdcLSFetchMissLogReq", K(ret), K(req));
  } else {
    // Allocate adapter on heap - it will be deleted in async callback
    GrpcCallbackAdapter *adapter = new GrpcCallbackAdapter(cb);
    if (OB_FAIL((async_fetch_missing_log_grpc<::logservice::FetchMissingLogReq, ::logservice::FetchMissingLogRes>(
                   tenant_id, svr, proto_req, adapter, timeout)))) {
      LOG_WARN("failed to call async_fetch_missing_log_grpc", K(ret), K(svr), K(timeout), K(req));
      delete adapter;  // Clean up if call fails immediately
    }
  }

  LOG_TRACE("rpc: async fetch missing_log (grpc)", KR(ret), K(svr), K(timeout), K(req));
  return ret;
}

int ObLogRpc::async_stream_fetch_raw_log(const uint64_t tenant_id,
    const common::ObAddr &svr,
    obrpc::ObCdcFetchRawLogReq &req,
    obrpc::ObCdcProxy::AsyncCB<obrpc::OB_CDC_FETCH_RAW_LOG> &cb,
    const int64_t timeout)
{
  int ret = OB_SUCCESS;
  req.set_client_id(client_id_);
  req.set_tenant_id(self_tenant_id_);
  req.set_client_type(client_type_);
  if (1 == cfg_->test_mode_force_fetch_archive) {
    req.set_flag(ObCdcRpcTestFlag::OBCDC_RPC_FETCH_ARCHIVE);
  }
  req.set_compressor_type(ATOMIC_LOAD(&compressor_type_));

  // Use new gRPC async unary RPC
  class GrpcCallbackAdapter : public ObGrpcAsyncCallback<::logservice::FetchRawLogRes> {
  public:
    GrpcCallbackAdapter(obrpc::ObCdcProxy::AsyncCB<obrpc::OB_CDC_FETCH_RAW_LOG> &old_cb)
      : old_cb_(old_cb) {}

    void on_success(const ::logservice::FetchRawLogRes &proto_resp) override {
      int ret = OB_SUCCESS;
      auto resp = std::make_unique<ObCdcFetchRawLogResp>();
      obrpc::ObRpcResultCode rcode;
      rcode.rcode_ = OB_SUCCESS;

      if (OB_FAIL(deserialize_proto_to_ob(proto_resp, *resp))) {
        LOG_WARN("failed to deserialize FetchRawLogRes", K(ret));
        rcode.rcode_ = ret;
        old_cb_.rcode() = rcode;
        (void)old_cb_.process();
      } else {
        old_cb_.result() = *resp;
        old_cb_.rcode() = rcode;
        (void)old_cb_.process();
      }
    }

    void on_error(const grpc::Status &status) override {
      obrpc::ObRpcResultCode rcode;
      rcode.rcode_ = translate_error(status);
      old_cb_.rcode() = rcode;
      old_cb_.on_timeout();
    }

  private:
    obrpc::ObCdcProxy::AsyncCB<obrpc::OB_CDC_FETCH_RAW_LOG> &old_cb_;

    int translate_error(const grpc::Status &status) {
      if (status.ok()) {
        return OB_SUCCESS;
      } else if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
        return OB_TIMEOUT;
      } else if (status.error_code() == grpc::StatusCode::UNAVAILABLE) {
        return OB_RPC_CONNECT_ERROR;
      } else {
        return OB_ERR_SYS;
      }
    }
  };

  // Serialize OB request to proto
  ::logservice::FetchRawLogReq proto_req;
  if (OB_FAIL(serialize_ob_to_proto(req, &proto_req))) {
    LOG_WARN("failed to serialize ObCdcFetchRawLogReq", K(ret), K(req));
  } else {
// #ifdef ERRSIM
    if (OB_SUCCESS != ERRSIM_FETCH_LOG_TIME_OUT &&
      OB_TIMEOUT != ERRSIM_FETCH_LOG_TIME_OUT) {
      ret = ERRSIM_FETCH_LOG_TIME_OUT;
    } else if (OB_TIMEOUT == ERRSIM_FETCH_LOG_TIME_OUT) {
      // Allocate adapter on heap - it will be deleted in async callback
      GrpcCallbackAdapter *adapter = new GrpcCallbackAdapter(cb);
      if (OB_FAIL((async_fetch_raw_log_grpc<::logservice::FetchRawLogReq, ::logservice::FetchRawLogRes>(
          tenant_id, svr, proto_req, adapter, 1)))) {
        LOG_WARN("failed to call async_fetch_raw_log_grpc", K(ret), K(svr), K(timeout), K(req));
        delete adapter;  // Clean up if call fails immediately
      }
    } else {
// #endif
    // Allocate adapter on heap - it will be deleted in async callback
    GrpcCallbackAdapter *adapter = new GrpcCallbackAdapter(cb);
    if (OB_FAIL((async_fetch_raw_log_grpc<::logservice::FetchRawLogReq, ::logservice::FetchRawLogRes>(
        tenant_id, svr, proto_req, adapter, timeout)))) {
      LOG_WARN("failed to call async_fetch_raw_log_grpc", K(ret), K(svr), K(timeout), K(req));
      delete adapter;  // Clean up if call fails immediately
    }
// #ifdef ERRSIM
    }
// #endif
  }

  LOG_TRACE("rpc: async fetch raw log (grpc)", KR(ret), K(svr), K(timeout), K(req));
  return ret;
}

int ObLogRpc::init(
    const int64_t cluster_id,
    const uint64_t self_tenant_id,
    const obrpc::ObCdcClientType client_type,
    const int64_t io_thread_num,
    const ObLogFetcherConfig &cfg)
{
  int ret = OB_SUCCESS;
  rpc::frame::ObNetOptions opt;
  opt.rpc_io_cnt_ = static_cast<int>(io_thread_num);
  opt.mysql_io_cnt_ = 0;
  // First set cfg
  cfg_ = &cfg;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_ERROR("ObLogRpc init twice", KR(ret));
  } else if (OB_UNLIKELY(io_thread_num <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid argument", KR(ret), K(io_thread_num));
  } else if (OB_FAIL(init_client_id_())) {
    LOG_ERROR("init client identity failed", KR(ret));
  } else if (OB_FAIL(net_client_.init(opt))) {
    LOG_ERROR("init net client fail", KR(ret), K(io_thread_num));
  } else if (OB_FAIL(grpc_client_map_.create(64, "LogGrpcClient"))) {
    LOG_ERROR("failed to create grpc client map", KR(ret));
  } else {
    cluster_id_ = cluster_id;
    self_tenant_id_ = self_tenant_id;
    client_type_ = client_type;
    is_inited_ = true;
    LOG_INFO("init ObLogRpc succ", K(cluster_id), K(io_thread_num));
  }

  return ret;
}

void ObLogRpc::destroy()
{
  is_inited_ = false;
  cluster_id_ = OB_INVALID_CLUSTER_ID;
  self_tenant_id_ = OB_INVALID_TENANT_ID;
  client_type_ = obrpc::ObCdcClientType::CLIENT_TYPE_UNKNOWN;
  net_client_.destroy();
  last_ssl_info_hash_ = UINT64_MAX;
  ssl_key_expired_time_ = 0;
  client_id_.reset();
  cfg_ = nullptr;
  external_info_val_[0] = '\0';
  compressor_type_ = common::INVALID_COMPRESSOR;

  // Destroy gRPC clients
  common::SpinWLockGuard guard(grpc_client_map_lock_);
  for (GrpcClientMap::iterator it = grpc_client_map_.begin(); it != grpc_client_map_.end(); ++it) {
    if (OB_NOT_NULL(it->second)) {
      ob_free(it->second);
      it->second = nullptr;
    }
  }
  grpc_client_map_.destroy();
}

void ObLogRpc::configure(const ObLogFetcherConfig &cfg)
{
  int64_t rpc_process_handler_time_upper_limit_msec = cfg.rpc_process_handler_time_upper_limit_msec;

  ATOMIC_STORE(&g_rpc_process_handler_time_upper_limit,
      rpc_process_handler_time_upper_limit_msec * _MSEC_);
  LOG_INFO("[CONFIG]", K(rpc_process_handler_time_upper_limit_msec));
}

int ObLogRpc::update_compressor_type(const common::ObCompressorType &compressor_type)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObLogRpc is not inited", KR(ret), K(cluster_id_));
  } else {
    ATOMIC_SET(&compressor_type_, compressor_type);

    if (REACH_TIME_INTERVAL_THREAD_LOCAL(10 * _SEC_)) {
      const char *compressor_type_name = nullptr;

      if (compressor_type_ < common::MAX_COMPRESSOR) {
        compressor_type_name = common::all_compressor_name[compressor_type];
      }
      LOG_INFO("update compressor type success", K_(compressor_type), K(compressor_type_name));
    }
  }

  return ret;
}

int ObLogRpc::init_client_id_()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(client_id_.init(getpid(), get_self_addr()))) {
    LOG_ERROR("init client id failed", KR(ret));
  }

  return ret;
}

int ObLogRpc::get_or_create_grpc_client_(const common::ObAddr &svr, int64_t timeout, LogGrpcClient *&grpc_client)
{
  int ret = OB_SUCCESS;
  grpc_client = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLogRpc not inited", K(ret));
  } else if (!svr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server address", K(ret), K(svr));
  } else {
    // Try to get existing client
    {
      common::SpinRLockGuard guard(grpc_client_map_lock_);
      if (OB_FAIL(grpc_client_map_.get_refactored(svr, grpc_client))) {
        if (OB_HASH_NOT_EXIST != ret) {
          LOG_WARN("failed to get grpc client from map", K(ret), K(svr));
        } else {
          ret = OB_SUCCESS;  // Not found, will create new one
        }
      }
    }

    // Create new client if not found
    if (OB_SUCC(ret) && OB_ISNULL(grpc_client)) {
      common::SpinWLockGuard guard(grpc_client_map_lock_);
      // Double check after acquiring write lock
      if (OB_FAIL(grpc_client_map_.get_refactored(svr, grpc_client))) {
        if (OB_HASH_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
          // Allocate new client
          void *buf = ob_malloc(sizeof(LogGrpcClient), "LogGrpcClient");
          if (OB_ISNULL(buf)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("failed to allocate grpc client", K(ret), K(svr));
          } else {
            grpc_client = new(buf) LogGrpcClient();
            if (OB_FAIL(grpc_client->init(svr, timeout))) {
              LOG_WARN("failed to init grpc client", K(ret), K(svr), K(timeout));
              grpc_client->~LogGrpcClient();
              ob_free(buf);
              grpc_client = nullptr;
            } else if (OB_FAIL(grpc_client_map_.set_refactored(svr, grpc_client))) {
              LOG_WARN("failed to insert grpc client to map", K(ret), K(svr));
              grpc_client->~LogGrpcClient();
              ob_free(buf);
              grpc_client = nullptr;
            } else {
              LOG_INFO("created new grpc client", K(svr), K(timeout));
            }
          }
        } else {
          LOG_WARN("failed to get grpc client from map", K(ret), K(svr));
        }
      }
    }
  }

  return ret;
}

}
}
