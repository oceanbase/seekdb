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
