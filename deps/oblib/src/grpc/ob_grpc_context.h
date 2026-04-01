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
#include "lib/net/ob_addr.h"
#include "lib/ob_define.h"
#include <grpcpp/grpcpp.h>
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

#define GRPC_SET_CONTEXT(grpc_client, context)   \
  do {                                                  \
    grpc_client.ctx_.set_grpc_context(context);         \
  } while(0)

namespace oceanbase
{
namespace obgrpc
{

grpc::Status ob_error_to_grpc_status(int ob_ret);
int extract_error_from_grpc_status(const grpc::Status& status, bool* is_ob_error = nullptr);
bool ob_grpc_is_rpc_tls_enabled();

// Read the full content of a file into a std::string. Returns false on failure.
bool read_file_content(const std::string &path, std::string &content);
// Create gRPC server-side mTLS credentials using certificates in the wallet directory.
// provider_out receives the underlying CertificateProvider and must be kept alive
// for the duration of the server. Returns nullptr on failure.
std::shared_ptr<grpc::ServerCredentials> create_server_credentials(
    std::shared_ptr<grpc::experimental::CertificateProviderInterface> &provider_out);
// Create gRPC client-side mTLS credentials using certificates in the wallet directory.
// Returns nullptr on failure (e.g. certificate files not found).
std::shared_ptr<grpc::ChannelCredentials> create_client_credentials();
// Return the expiry time of wallet/cert.pem in microseconds (unix epoch).
// Returns 0 if TLS is disabled or the cert has not been loaded.
int64_t get_rpc_cert_expire_time();

class ObGrpcContext {
public:
  static const int64_t MAX_RPC_TIMEOUT = 9000 * 1000;
  static const int64_t REPORT_COUNT_INTERVAL = 2000;
  ObGrpcContext();
  int init(const ObAddr &addr, int64_t timeout);
  void set_grpc_context(ClientContext &context);
  void set_grpc_context(ClientContext &context, const int64_t timeout);
  int translate_error(const Status &status);
  const static uint32_t VERSION = 1;
private:
  void set_grpc_context_(ClientContext &context, const int64_t timeout);
public:
  ObAddr dst_;
  int64_t timeout_;
  struct Statistics {
    Statistics(): send_cnt_(0), failed_cnt_(0), wait_time_(0) {}
    uint64_t send_cnt_;
    uint64_t failed_cnt_;
    uint64_t wait_time_;
  } statistics_info;
};

template <typename Service>
class ObGrpcClient {
public:
  ObGrpcClient() {};
  int init(const ObAddr& addr, int64_t timeout);
  int translate_error(const Status &status);
  ObGrpcContext ctx_;
  std::shared_ptr<grpc::ChannelInterface> channel_;
  std::unique_ptr<typename Service::Stub> stub_;
};

template <typename Service>
int ObGrpcClient<Service>::init(const ObAddr& addr, int64_t timeout)
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
    // Set max message size to 2GB (same as _max_rpc_packet_size default)
    const int MAX_MESSAGE_SIZE = 512 * 1024 * 1024;
    channel_args.SetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, MAX_MESSAGE_SIZE);
    channel_args.SetInt(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, MAX_MESSAGE_SIZE);
    std::shared_ptr<grpc::ChannelCredentials> creds;
    if (ob_grpc_is_rpc_tls_enabled()) {
      creds = create_client_credentials();
      if (!creds) {
        ret = OB_INIT_FAIL;
        RPC_LOG(ERROR, "failed to create mTLS client credentials, check wallet directory", K(addr));
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
int ObGrpcClient<Service>::translate_error(const Status &status)
{
  return ctx_.translate_error(status);
}

template<typename ObType, typename ProtoType>
int serialize_ob_to_proto(const ObType& obj, ProtoType* proto_msg)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(proto_msg)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "proto_msg is null", K(ret));
  } else {
    int64_t obj_size = obj.get_serialize_size();
    if (obj_size <= 0) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "invalid serialize size", K(ret), K(obj_size));
    } else {
      std::string buf_str;
      buf_str.resize(obj_size);
      char* buf = const_cast<char*>(buf_str.data());
      int64_t pos = 0;

      if (OB_FAIL(obj.serialize(buf, obj_size, pos))) {
        STORAGE_LOG(WARN, "failed to serialize OB object", K(ret), K(obj_size));
      } else if (pos != obj_size) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "serialize incomplete", K(ret), K(pos), K(obj_size));
      } else {
        proto_msg->set_buf(buf_str);
        proto_msg->set_size(pos);
      }
    }
  }

  return ret;
}

template<typename ObType, typename ProtoType>
int deserialize_proto_to_ob(const ProtoType& proto_msg, ObType& obj)
{
  int ret = OB_SUCCESS;

  const std::string& buf = proto_msg.buf();
  const uint64_t size = proto_msg.size();

  if (buf.empty() || size == 0) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "empty buffer in proto message", K(ret), K(size));
  } else {
    int64_t pos = 0;
    if (OB_FAIL(obj.deserialize(buf.data(), size, pos))) {
      STORAGE_LOG(WARN, "failed to deserialize OB object", K(ret), K(size));
    } else if (pos != static_cast<int64_t>(size)) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "deserialize incomplete", K(ret), K(pos), K(size));
    }
  }

  return ret;
}

} // end of namespace obgrpc
} // end of namespace oceanbase

#endif /* OCEANBASE_GRPC_CONTEXT_H_ */
