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
#ifndef OCEANBASE_GRPC_SERVER_H_
#define OCEANBASE_GRPC_SERVER_H_

#include <memory>
#include <thread>
#include <vector>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/tls_credentials_options.h>
using grpc::Server;
using grpc::ServerBuilder;
using grpc::Service;

namespace oceanbase
{
namespace obgrpc
{

class ObGrpcServer
{
public:
  ObGrpcServer();
  void register_service(grpc::Service *service);
  int start(int port);
  void stop();
  void wait();
  bool is_running() const { return server_ != nullptr; }
  // Returns true only when the server is running with TLS actually active.
  // Distinguishes from the config intent (enable_rpc_tls) vs. runtime state.
  bool is_tls_enabled() const { return cert_provider_ != nullptr; }
private:
  std::thread thread_;
  std::unique_ptr<grpc::Server> server_;
  std::vector<grpc::Service *> services_;
  // Owns the certificate provider so it outlives the server.
  std::shared_ptr<grpc::experimental::CertificateProviderInterface> cert_provider_;
};


} // end of namespace obgrpc
} // end of namespace oceanbase

#endif /* OCEANBASE_GRPC_SERVER_H_ */
