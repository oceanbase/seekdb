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

#ifndef _OCEABASE_RPC_FRAME_OBI_REQ_QHANDLER_H_
#define _OCEABASE_RPC_FRAME_OBI_REQ_QHANDLER_H_

namespace oceanbase
{
namespace obsys
{
class CThread;
}
namespace rpc
{
class ObRequest;
namespace frame
{

class ObPacketQueueSessionHandler;
class ObiReqQHandler
{
public:
  virtual ~ObiReqQHandler() {}

  virtual int onThreadCreated(obsys::CThread *) = 0;
  virtual int onThreadDestroy(obsys::CThread *) = 0;

  virtual bool handlePacketQueue(ObRequest *req, void *args) = 0;
};

} // end of namespace frame
} // end of namespace rpc
} // end of namespace oceanbase

#endif /* _OCEABASE_RPC_FRAME_OBI_REQ_QHANDLER_H_ */
