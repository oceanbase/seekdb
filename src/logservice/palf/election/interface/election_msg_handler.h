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

#ifndef LOGSERVICE_PALF_ELECTION_INTERFACE_OB_I_ELECTION_MSG_HANDLER_H
#define LOGSERVICE_PALF_ELECTION_INTERFACE_OB_I_ELECTION_MSG_HANDLER_H

#include "lib/net/ob_addr.h"
#include "lib/container/ob_array.h"

namespace oceanbase
{
namespace palf
{
namespace election
{

class ElectionPrepareRequestMsg;
class ElectionAcceptRequestMsg;
class ElectionPrepareResponseMsg;
class ElectionAcceptResponseMsg;
class ElectionChangeLeaderMsg;

class ElectionMsgSender
{
// Need to be implemented externally message sending interface
public:
  virtual int broadcast(const ElectionPrepareRequestMsg &msg,
                        const common::ObIArray<common::ObAddr> &list) const = 0;
  virtual int broadcast(const ElectionAcceptRequestMsg &msg,
                        const common::ObIArray<common::ObAddr> &list) const = 0;
  virtual int send(const ElectionPrepareResponseMsg &msg) const = 0;
  virtual int send(const ElectionAcceptResponseMsg &msg) const = 0;
  virtual int send(const ElectionChangeLeaderMsg &msg) const = 0;
};

}
}
}

#endif
