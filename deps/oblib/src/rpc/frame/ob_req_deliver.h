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

#ifndef _OCEABASE_RPC_FRAME_OB_REQ_DELIVER_H_
#define _OCEABASE_RPC_FRAME_OB_REQ_DELIVER_H_

#include "rpc/frame/ob_req_queue_thread.h"
#include "rpc/frame/ob_req_qhandler.h"
#include "rpc/frame/ob_req_translator.h"

namespace oceanbase
{
namespace rpc
{
class ObRequest;
namespace frame
{

// This class plays a deliver role who'll deliver each packet received
// from the upper to responding packet queue. The deliver rules is
// defined by those macros named
class ObReqDeliver
{
public:
  virtual ~ObReqDeliver() {}

  virtual int init() = 0;
  // deliver a ObPacket to a responding queue
  virtual int deliver(rpc::ObRequest &req) = 0;
  virtual void stop() = 0;

}; // end of class ObPktDeliver

class ObReqQDeliver
    : public ObReqDeliver
{
public:
  explicit ObReqQDeliver(ObiReqQHandler &qhandler);
  ObiReqQHandler &get_qhandler() { return qhandler_; }

protected:
  ObiReqQHandler &qhandler_;
}; // end of class ObReqQDeliver

} // end of namespace frame
} // end of namespace rpc
} // end of namespace oceanbase

#endif /* _OCEABASE_RPC_FRAME_OB_REQ_DELIVER_H_ */
