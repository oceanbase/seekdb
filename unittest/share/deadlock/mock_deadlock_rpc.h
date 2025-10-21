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

#include "share/deadlock/ob_deadlock_detector_rpc.h"
#include "share/deadlock/ob_deadlock_detector_mgr.h"

namespace oceancase
{
namespace unittest
{

using namespace oceanbase::share::detector;

class MockDeadLockRpc : public oceanbase::share::detector::ObDeadLockDetectorRpc
{
public:
  int post_lcl_message(const ObAddr &dest_addr, const ObLCLMessage &lcl_msg) override
  {
    UNUSED(dest_addr);
    MTL(oceanbase::share::detector::ObDeadLockDetectorMgr*)->process_lcl_message(lcl_msg);
    return OB_SUCCESS;
  }
  int post_collect_info_message(const ObAddr &dest_addr,
                                const ObDeadLockCollectInfoMessage &collect_info_msg) override
  {
    UNUSED(dest_addr);
    MTL(oceanbase::share::detector::ObDeadLockDetectorMgr*)->process_collect_info_message(collect_info_msg);
    return OB_SUCCESS;
  }
  int post_notify_parent_message(const ObAddr &dest_addr,
                                 const ObDeadLockNotifyParentMessage &notify_msg) override
  {
    UNUSED(dest_addr);
    MTL(oceanbase::share::detector::ObDeadLockDetectorMgr*)->process_notify_parent_message(notify_msg);
    return OB_SUCCESS;
  }
};

}
}
