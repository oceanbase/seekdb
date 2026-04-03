/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this software except in compliance with the License.
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

#define USING_LOG_PREFIX OBLOG

#include "logservice/logfetcher/ob_log_rpc.h"
#include "lib/oblog/ob_log_module.h"
#include "grpc/ob_grpc_context.h"
#include "grpc/logservice.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include "lib/utility/ob_macro_utils.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/time/ob_time_utility.h"

using namespace oceanbase::common;
using namespace oceanbase::obrpc;
using namespace oceanbase::obgrpc;
using namespace logservice;

namespace oceanbase
{
namespace logfetcher
{

// Template implementation for async gRPC unary RPC
// This layer only handles gRPC calls, serialization/deserialization is done in caller
// RequestType and ResponseType should be proto message types
// Reference: test_grpc.cpp async_unary test
template<typename RequestType, typename ResponseType>
int ObLogRpc::async_fetch_missing_log_grpc(
    const uint64_t tenant_id,
    const common::ObAddr &svr,
    const RequestType &proto_req,
    ObGrpcAsyncCallback<ResponseType> *cb,
    const int64_t timeout)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObLogRpc not init", KR(ret), K(tenant_id));
  } else if (OB_ISNULL(cb)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("callback is null", K(ret));
  } else {
    // Get or create gRPC client for server (reuse client from map)
    ObLogRpc::LogGrpcClient *grpc_client = nullptr;
    if (OB_FAIL(get_or_create_grpc_client_(svr, timeout, grpc_client))) {
      LOG_WARN("failed to get or create grpc client", K(ret), K(svr), K(timeout));
    } else if (OB_ISNULL(grpc_client)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("grpc client is null", K(ret), K(svr));
    } else {
      // Create context and response
      // Use shared_ptr to make lambda copy-constructible for gRPC async API
      auto context = std::make_shared<grpc::ClientContext>();
      grpc_client->ctx_.set_grpc_context(*context, timeout);
      auto response = std::make_shared<ResponseType>();

      // Send async unary request with callback
      // cb will be deleted in lambda after callback completes
      grpc_client->stub_->async()->fetch_missing_log(
          context.get(),
          &proto_req,
          response.get(),
          [cb, response, context](grpc::Status status) {
            if (status.ok()) {
              cb->on_success(*response);
            } else {
              cb->on_error(status);
            }
            delete cb;  // Delete callback after completion
          });

      LOG_TRACE("async fetch_missing_log RPC sent", K(tenant_id), K(svr));
    }
  }

  return ret;
}

template<typename RequestType, typename ResponseType>
int ObLogRpc::async_fetch_log_grpc(
    const uint64_t tenant_id,
    const common::ObAddr &svr,
    const RequestType &proto_req,
    ObGrpcAsyncCallback<ResponseType> *cb,
    const int64_t timeout)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObLogRpc not init", KR(ret), K(tenant_id));
  } else if (OB_ISNULL(cb)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("callback is null", K(ret));
  } else {
    // Get or create gRPC client for server (reuse client from map)
    ObLogRpc::LogGrpcClient *grpc_client = nullptr;
    if (OB_FAIL(get_or_create_grpc_client_(svr, timeout, grpc_client))) {
      LOG_WARN("failed to get or create grpc client", K(ret), K(svr), K(timeout));
    } else if (OB_ISNULL(grpc_client)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("grpc client is null", K(ret), K(svr));
    } else {
        // Create context and response
        // Use shared_ptr to make lambda copy-constructible for gRPC async API
        auto context = std::make_shared<grpc::ClientContext>();
        grpc_client->ctx_.set_grpc_context(*context, timeout);
        auto response = std::make_shared<ResponseType>();

        // Send async unary request with callback
        // cb will be deleted in lambda after callback completes
        grpc_client->stub_->async()->fetch_log(
            context.get(),
            &proto_req,
            response.get(),
            [cb, response, context](grpc::Status status) {
              if (status.ok()) {
                cb->on_success(*response);
              } else {
                cb->on_error(status);
              }
              delete cb;  // Delete callback after completion
            });

      LOG_TRACE("async fetch_log RPC sent", K(tenant_id), K(svr));
    }
  }

  return ret;
}

template<typename RequestType, typename ResponseType>
int ObLogRpc::async_fetch_raw_log_grpc(
    const uint64_t tenant_id,
    const common::ObAddr &svr,
    const RequestType &proto_req,
    ObGrpcAsyncCallback<ResponseType> *cb,
    const int64_t timeout)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObLogRpc not init", KR(ret), K(tenant_id));
  } else if (OB_ISNULL(cb)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("callback is null", K(ret));
  } else {
    // Get or create gRPC client for server (reuse client from map)
    ObLogRpc::LogGrpcClient *grpc_client = nullptr;
    if (OB_FAIL(get_or_create_grpc_client_(svr, timeout, grpc_client))) {
      LOG_WARN("failed to get or create grpc client", K(ret), K(svr), K(timeout));
    } else if (OB_ISNULL(grpc_client)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("grpc client is null", K(ret), K(svr));
    } else {
        // Create context and response
        // Use shared_ptr to make lambda copy-constructible for gRPC async API
        auto context = std::make_shared<grpc::ClientContext>();
        grpc_client->ctx_.set_grpc_context(*context, timeout);
        auto response = std::make_shared<ResponseType>();

        // Send async unary request with callback
        // cb will be deleted in lambda after callback completes
        grpc_client->stub_->async()->fetch_raw_log(
            context.get(),
            &proto_req,
            response.get(),
            [cb, response, context](grpc::Status status) {
              if (status.ok()) {
                cb->on_success(*response);
              } else {
                cb->on_error(status);
              }
              delete cb;  // Delete callback after completion
            });

      LOG_TRACE("async fetch_raw_log RPC sent", K(tenant_id), K(svr));
    }
  }

  return ret;
}

} // logfetcher
} // oceanbase
