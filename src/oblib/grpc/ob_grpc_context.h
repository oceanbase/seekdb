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

#ifndef OCEANBASE_GRPC_CONTEXT_H_
#define OCEANBASE_GRPC_CONTEXT_H_

#include <chrono>
#include <memory>
#include <string>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#ifndef CONST
#define CONST const
#define _OB_UNDEF_CONST
#endif
#ifndef OPTIONAL
#define OPTIONAL
#define _OB_UNDEF_OPTIONAL
#endif
#include <mswsock.h>
#ifdef _OB_UNDEF_CONST
#undef CONST
#undef _OB_UNDEF_CONST
#endif
#ifdef _OB_UNDEF_OPTIONAL
#undef OPTIONAL
#undef _OB_UNDEF_OPTIONAL
#endif
#undef ERROR
#undef DELETE
#endif
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/tls_credentials_options.h>
#include "lib/net/ob_addr.h"
#include "lib/ob_define.h"

namespace oceanbase
{
namespace obgrpc
{

grpc::Status ob_error_to_grpc_status(int ob_ret);
int extract_error_from_grpc_status(const grpc::Status &status, bool *is_ob_error = nullptr);
bool ob_grpc_is_rpc_tls_enabled();

bool read_file_content(const std::string &path, std::string &content);
std::shared_ptr<grpc::ServerCredentials> create_server_credentials(
    std::shared_ptr<grpc::experimental::CertificateProviderInterface> &provider_out);
std::shared_ptr<grpc::ChannelCredentials> create_client_credentials();
int64_t get_rpc_cert_expire_time();

class ObGrpcContext {
public:
  static const int64_t MAX_RPC_TIMEOUT = 9000 * 1000;
  static const int64_t REPORT_COUNT_INTERVAL = 2000;

  ObGrpcContext();
  int init(const common::ObAddr &addr, int64_t timeout);
  void set_grpc_context(grpc::ClientContext &context);
  void set_grpc_context(grpc::ClientContext &context, const int64_t timeout);
  int translate_error(const grpc::Status &status);

  static const uint32_t VERSION = 1;

private:
  void set_grpc_context_(grpc::ClientContext &context, const int64_t timeout);

public:
  common::ObAddr dst_;
  int64_t timeout_;
  struct Statistics {
    Statistics() : send_cnt_(0), failed_cnt_(0), wait_time_(0) {}
    uint64_t send_cnt_;
    uint64_t failed_cnt_;
    uint64_t wait_time_;
  } statistics_info;
};

template <typename Service>
class ObGrpcClient {
public:
  ObGrpcClient() {}
  int init(const common::ObAddr &addr, int64_t timeout);
  int translate_error(const grpc::Status &status);

  ObGrpcContext ctx_;
  std::shared_ptr<grpc::ChannelInterface> channel_;
  std::unique_ptr<typename Service::Stub> stub_;
};

template <typename Service>
int ObGrpcClient<Service>::init(const common::ObAddr &addr, int64_t timeout)
{
  int ret = OB_SUCCESS;
  char addr_str[common::MAX_IP_PORT_LENGTH] = {0};
  if (OB_FAIL(ctx_.init(addr, timeout))) {
    RPC_LOG(WARN, "grpc ctx init failed", K(addr));
  } else if (OB_FAIL(addr.ip_port_to_string(addr_str, sizeof(addr_str)))) {
    RPC_LOG(WARN, "translate addr failed", K(addr));
  } else {
    grpc::ChannelArguments channel_args;
    channel_args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
    channel_args.SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, 1000);
    channel_args.SetInt(GRPC_ARG_MIN_RECONNECT_BACKOFF_MS, 1000);
    const int MAX_MESSAGE_SIZE = 512 * 1024 * 1024;
    channel_args.SetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, MAX_MESSAGE_SIZE);
    channel_args.SetInt(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, MAX_MESSAGE_SIZE);
    std::shared_ptr<grpc::ChannelCredentials> creds;
    if (ob_grpc_is_rpc_tls_enabled()) {
      creds = create_client_credentials();
      if (!creds) {
        ret = OB_INIT_FAIL;
        RPC_LOG(ERROR, "failed to create gRPC TLS client credentials", K(addr));
      }
    } else {
      creds = grpc::InsecureChannelCredentials();
    }
    if (OB_SUCC(ret)) {
      channel_ = grpc::CreateCustomChannel(addr_str, creds, channel_args);
      stub_ = Service::NewStub(channel_);
    }
  }
  return ret;
}

template <typename Service>
int ObGrpcClient<Service>::translate_error(const grpc::Status &status)
{
  return ctx_.translate_error(status);
}

} // end namespace obgrpc
} // end namespace oceanbase

#endif /* OCEANBASE_GRPC_CONTEXT_H_ */
