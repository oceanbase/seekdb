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

#ifndef _OCEABASE_RPC_FRAME_OB_REQ_QHANDLER_H_
#define _OCEABASE_RPC_FRAME_OB_REQ_QHANDLER_H_

#include "rpc/frame/obi_req_qhandler.h"

namespace oceanbase
{
namespace rpc
{
class ObRequest;
namespace frame
{

class ObReqTranslator;
class ObReqQHandler
    : public ObiReqQHandler
{
public:
  explicit ObReqQHandler(ObReqTranslator &translator_);
  virtual ~ObReqQHandler();

  int init();

  // invoke when queue thread created.
  int onThreadCreated(obsys::CThread *);
  int onThreadDestroy(obsys::CThread *);

  bool handlePacketQueue(ObRequest *req, void *args);

private:
  ObReqTranslator &translator_;
}; // end of class ObReqQHandler

} // end of namespace frame
} // end of namespace observer
} // end of namespace oceanbase

#endif /* _OCEABASE_RPC_FRAME_OB_REQ_QHANDLER_H_ */
