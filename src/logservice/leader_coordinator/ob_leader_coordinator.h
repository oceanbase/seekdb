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

#ifndef LOGSERVICE_COORDINATOR_INTERFACE_OB_LEADER_COORDINATOR_H
#define LOGSERVICE_COORDINATOR_INTERFACE_OB_LEADER_COORDINATOR_H
#include "failure_event.h"
#include "lib/function/ob_function.h"
#include "lib/container/ob_array.h"
#include "share/ob_delegate.h"
#include "ob_failure_detector.h"
#include "share/ob_occam_timer.h"
#include "share/ob_table_access_helper.h"

namespace oceanbase
{
namespace unittest
{
class TestTabletAccessor;
class TestElectionPriority;
}
namespace logservice
{
namespace coordinator
{
// This structure stores information that is periodically refreshed, and non-local priority information is cached in this structure.
typedef common::ObTuple<int64_t/*0. ls_id*/,
                        int64_t/*1. self zone priority*/,
                        bool/*2. is_manual_leader*/,
                        ObTuple<bool/*3.1 is_removed*/,
                                common::ObStringHolder/*3.2 removed_reason*/>,
                        bool/*4. is_zone_stopped*/,
                        bool/*5. is_server_stopped*/,
                        bool/*6. is_primary_region*/> LsElectionReferenceInfo;

class ObLeaderCoordinator
{
  friend class ObFailureDetector;
  friend class unittest::TestTabletAccessor;
  friend class unittest::TestElectionPriority;
public:
  ObLeaderCoordinator();
  ~ObLeaderCoordinator();
  void destroy();
  ObLeaderCoordinator(const ObLeaderCoordinator &rhs) = delete;
  ObLeaderCoordinator& operator=(const ObLeaderCoordinator &rhs) = delete;
  static int mtl_init(ObLeaderCoordinator *&p_coordinator);
  static int mtl_start(ObLeaderCoordinator *&p_coordinator);
  static void mtl_stop(ObLeaderCoordinator *&p_coordinator);
  static void mtl_wait(ObLeaderCoordinator *&p_coordinator);
  static void mtl_destroy(ObLeaderCoordinator *&p_coordinator);
  /**
   * @description: When the internal table is updated, this interface can be used to proactively trigger the refresh process of LeaderCoordinator so that the role switch can be completed as soon as possible
   * @param {*}
   * @return {*}
   * @Date: 2021-12-27 20:30:39
   */
  void refresh();
  int get_ls_election_reference_info(const share::ObLSID &ls_id, LsElectionReferenceInfo &reference_info) const;
  int schedule_refresh_priority_task();
private:
  common::ObArray<LsElectionReferenceInfo> *all_ls_election_reference_info_;
  // refresh priority and detect recovery from failure
  common::ObOccamTimer recovery_detect_timer_;
  // detect whether failure has occured
  common::ObOccamTimer failure_detect_timer_;
  common::ObOccamTimerTaskRAIIHandle refresh_priority_task_handle_;
  bool is_running_;
  mutable ObSpinLock lock_;
};

}
}
}

#endif
