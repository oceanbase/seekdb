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

#ifndef LOGSERVICE_COORDINATOR_FAILURE_DETECTOR_H
#define LOGSERVICE_COORDINATOR_FAILURE_DETECTOR_H

#include "failure_event.h"
#include "lib/function/ob_function.h"
#include "lib/container/ob_array.h"
#include "lib/lock/ob_spin_lock.h"
#include "share/ob_occam_timer.h"
#include "logservice/palf/palf_handle.h"        // PalfHandle

namespace oceanbase
{
namespace logservice
{
namespace coordinator
{

class ObLeaderCoordinator;

/**
 * @description: ObFailureDetector detects failures in each module, records them, and affects the election priority
 * [Value semantics of FailureEvent]
 * FailureEvent consists of three enumeration fields and one info_ field. The type of the info_ field is ObStringHolder, which allocates memory space on the heap and uses RAII to manage the lifecycle of the string it holds.
 * It is agreed that each failure registered by each module is different. Users should not register the same FailureEvent multiple times. ObFailureDetector will check the registered failure events and reject a user's attempt to register an already existing FailureEvent. Users need to make use of the content in the FailureEvent.info_ field to distinguish between two different failures registered by the same module.
 * [Different levels of Failure]
 * Unless specified by the user, FailureEvent has a default level of SERIOUS, which triggers a leader switch.
 * In addition, users can specify NOTICE and FATAL levels explicitly. The former does not affect the election priority and is only displayed in internal tables, while the latter has a higher priority for leader switch compared to general Failure events.
 * [Recovering from failures]
 * Users can choose to register a recover_detect_operation, which probes in the background at a frequency of 1 time/s whether the failure has recovered.
 * It is agreed that returning true from recover_detect_operation indicates that the anomaly has been restored. Users should ensure that the call time of recover_detect_operation is sufficiently short (in the ms range) to avoid blocking the background thread pool.
 * Users can also choose to call the remove_failure_event(event) interface to proactively cancel the failure event. When event == failure, the corresponding failure record is canceled.
 * @Date: 2022-01-10 10:21:28
 */
class ObFailureDetector
{
  friend class ObLeaderCoordinator;
public:
  ObFailureDetector();
  ~ObFailureDetector();
  void destroy();
  static int mtl_init(ObFailureDetector *&p_failure_detector);
  static int mtl_start(ObFailureDetector *&p_failure_detector);
  static void mtl_stop(ObFailureDetector *&p_failure_detector);
  static void mtl_wait(ObFailureDetector *&p_failure_detector);
  /**
   * @description: Set an irrecoverable failure that needs to be manually restored by the registered module through the remove_failure_event() interface; otherwise, it will persist.
   * @param {FailureEvent} event Failure event, defined in failure_event.h
   * @return {*}
   * @Date: 2021-12-29 11:13:25
   */
  int add_failure_event(const FailureEvent &event);
  /**
   * @description: The failure event set by external and the corresponding recovery detection logic
   * @param {FailureEvent} event failure event, defined in failure_event.h
   * @param {ObFunction<bool()>} recover_detect_operation The operation to detect failure recovery, which will be periodically called
   * @return {*}
   * @Date: 2021-12-29 10:27:54
   */
  /**
   * @description: User selects to unregister a previously registered failure event
   * @param {FailureEvent} event A previously registered failure event
   * @return {*}
   * @Date: 2022-01-09 15:08:22
   */
  int remove_failure_event(const FailureEvent &event);
  int get_specified_level_event(FailureLevel level, ObIArray<FailureEvent> &results);
public:
  /**
   * @description: Timed task for periodically detecting if anomalies have recovered
   * @param {*}
   * @return {*}
   * @Date: 2022-01-04 21:12:00
   */
  void detect_recover();
  /**
   * @description: detect whether failure has occured
   * @param {*}
   * @return {*}
   */
  void detect_failure();
  bool is_clog_disk_has_fatal_error();
  bool is_clog_disk_has_hang_error();
  bool is_clog_disk_has_full_error();
  bool is_data_disk_has_fatal_error();
  bool is_data_disk_full() const
  {
    return has_add_disk_full_event_;
  }
private:
  bool check_is_running_() const { return is_running_; }
  int insert_event_to_table_(const FailureEvent &event, const ObFunction<bool()> &recover_operation, ObString info);
  void detect_palf_hang_failure_();
  void detect_data_disk_io_failure_();
  void detect_palf_disk_full_();
  void detect_schema_not_refreshed_();
  void detect_data_disk_full_();
#ifdef OB_BUILD_ARBITRATION
  void detect_election_silent_();
#endif
private:
  struct FailureEventWithRecoverOp {
    int init(const FailureEvent &event, const ObFunction<bool()> &recover_detect_operation);
    int assign(const FailureEventWithRecoverOp &);
    FailureEvent event_;
    ObFunction<bool()> recover_detect_operation_;
    TO_STRING_KV(K_(event));
  };

#ifdef OB_BUILD_ARBITRATION
  class GetElectionSilentFunctor 
  {
  public:
    GetElectionSilentFunctor(bool &is_election_silent) : is_election_silent_(is_election_silent) {}
    int operator()(const palf::PalfHandle &palf_handle) {
      if (true == palf_handle.is_election_silent()) {
        is_election_silent_ = true;
      }
      return OB_SUCCESS;
    }
  private:
    bool &is_election_silent_;
  };
#endif
  bool is_running_;
  common::ObArray<FailureEventWithRecoverOp> events_with_ops_;
  common::ObArray<common::ObAddr> tenant_server_list_;
  common::ObOccamTimerTaskRAIIHandle failure_task_handle_;
  common::ObOccamTimerTaskRAIIHandle recovery_task_handle_;
  ObLeaderCoordinator *coordinator_;
  bool has_add_clog_hang_event_;
  bool has_add_data_disk_hang_event_;
  bool has_add_clog_full_event_;
  bool has_schema_error_;
  bool has_add_disk_full_event_;
  bool has_election_silent_event_;
  ObSpinLock lock_;
};

}
}
}

#endif
