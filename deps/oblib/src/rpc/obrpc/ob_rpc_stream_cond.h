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

#ifndef OCEANBASE_RPC_OBRPC_OB_RPC_STREAM_COND_
#define OCEANBASE_RPC_OBRPC_OB_RPC_STREAM_COND_
#include <stdint.h>
#include "lib/ob_define.h"
#include "lib/net/ob_addr.h"

namespace oceanbase
{
namespace rpc
{
class ObRequest;
} // end of namespace rp
namespace obrpc
{

class ObRpcPacket;
class ObRpcSessionHandler;
class ObRpcStreamCond
{
public:
  explicit ObRpcStreamCond(ObRpcSessionHandler &handle);
  virtual ~ObRpcStreamCond();

  virtual int prepare(const ObAddr *src_addr, const ObRpcPacket *packet);
  virtual int wait(rpc::ObRequest *&req, int64_t timeout);
  virtual int wakeup(rpc::ObRequest &req);
  virtual int destroy();
  virtual void reuse();

  int64_t sessid() const { return sessid_; }

private:
  int64_t sessid_;
  ObRpcSessionHandler &handler_;
  int64_t first_pkt_id_;
  int64_t first_send_ts_;
  ObAddr src_addr_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObRpcStreamCond);
}; // end of class ObRpcStreamCond

} // end of namespace rpc
} // end of namespace oceanbase

#endif
