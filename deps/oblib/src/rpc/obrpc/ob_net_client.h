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

#ifndef OCEANBASE_RPC_OBRPC_OB_NET_CLIENT_
#define OCEANBASE_RPC_OBRPC_OB_NET_CLIENT_

#include "rpc/frame/ob_net_easy.h"
#include "rpc/frame/ob_req_transport.h"

namespace oceanbase
{
namespace common
{
class ObAddr;
} // end of namespace common

namespace obrpc
{
class ObRpcProxy;

class ObNetClient
{
public:
  ObNetClient();
  virtual ~ObNetClient();

  int init();
  int init(const rpc::frame::ObNetOptions opts);
  void destroy();
  int get_proxy(ObRpcProxy &proxy);

  /*
   * Load ssl config
   * Support local file mode and BKMI mode
   * 1. For local file mode, ca_cert/public_cert/private_key is local file path
   * 2. For BKMI mode, ca_cert/public_cert/private_key is real content
   *
   * @param [in] use_bkmi    whether（or not）to use BKMI
   * @param [in] use_sm      whether（or not）to use China cryptographic algorithm,eg SM2/SM3/SM4
   * @param [in] ca_cert     CA cert
   * @param [in] public_cert public cert
   * @param [in] private_key private key
   *
   * @param OB_SUCCESS  success
   * @param other code  fail
   *
   */

private:
  int init_(const rpc::frame::ObNetOptions opts);
private:
  bool inited_;
  rpc::frame::ObReqTransport *transport_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObNetClient);
}; // end of class ObNetClient

} // end of namespace rpc
} // end of namespace oceanbase

#endif //OCEANBASE_RPC_OBRPC_OB_NET_CLIENT_
