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

#ifndef OCEANBASE_SHARE_DEADLOCK_OB_DEADLOCK_DETECTOR_RPC_
#define OCEANBASE_SHARE_DEADLOCK_OB_DEADLOCK_DETECTOR_RPC_

#include "share/ob_rpc_struct.h"
#include "share/ob_server_struct.h"
#include "ob_deadlock_detector_common_define.h"
#include "ob_deadlock_parameters.h"
#include "ob_deadlock_detector_mgr.h"
#include "storage/deadlock/ob_lcl_scheme/ob_lcl_message.h"
#include "ob_deadlock_message.h"

namespace oceanbase
{
namespace share
{
namespace detector
{

// Single-replica seekdb: deadlock detector messages always target self, so
// dispatch them in-process to ObDeadLockDetectorMgr instead of obcall RPC.
class ObDeadLockDetectorRpc
{
public:
  ObDeadLockDetectorRpc() :
    is_inited_(false) {};
  ~ObDeadLockDetectorRpc() = default;
  int init(const common::ObAddr &self);
  void destroy();
public:
  virtual int post_lcl_message(const ObAddr &dest_addr, const ObLCLMessage &lcl_msg);
  virtual int post_collect_info_message(const ObAddr &dest_addr,
                                        const ObDeadLockCollectInfoMessage &lcl_msg);
  virtual int post_notify_parent_message(const ObAddr &dest_addr,
                                         const ObDeadLockNotifyParentMessage &notify_msg);
private:
  bool is_inited_;
  common::ObAddr self_;
};

}// detector
}// share
}// oceanbase
#endif
