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

#ifndef OCEANBASE_STORAGE_LS_RESTORE_HANDLER_
#define OCEANBASE_STORAGE_LS_RESTORE_HANDLER_

#include "share/ob_ls_id.h"
#include "common/ob_member.h"
#include "common/ob_tablet_id.h"
#include "lib/container/ob_array.h"
#include "lib/lock/ob_spin_rwlock.h"
#include "share/ob_task_define.h"
#include "share/ob_common_rpc_proxy.h"
#include "observer/ob_rpc_processor_simple.h"
#include "share/scheduler/ob_tenant_dag_scheduler.h"
#include "lib/task/ob_timer.h"
#include "storage/ob_storage_rpc.h"
#include "ob_storage_ha_struct.h"
#include "ob_storage_restore_struct.h"
#include "ob_restore_helper.h"

namespace oceanbase
{
namespace restore
{
/*
 * Restore Handler State Machine:
 *
 *                                                              ret!=OB_SUCCESS
 *    ┌────────────────────────────────────────────────────────────────────────┐
 *    │                                                                        │
 * ┌──┴─┐  ┌────────┐  ┌─────────────┐  ┌─────▼───────┐  ┌────────────────┐  ┌──────┐
 * │INIT├─►│ BUILD  ├─►│  WAIT_BUILD ├─►│   COMPLETE  ├─►│  WAIT_COMPLETE ├─►│FINISH│
 * └────┘  └────────┘  └──┬─┬──────▲─┘  └──▲─┬──────▲─┘  └─────┬──────▲───┘  └──────┘
 *                        │ │      │       │ │      │          │      │
 *                        │ └─wait─┘       │ └──────┘          └─wait─┘
 *                        └────────────────┘  ret!=OB_SUCCESS && !is_complete_
 *         ret!=OB_SUCCESS || result_!=OB_SUCCESS
 */
enum class ObLSRestoreStatus : int8_t
{
  INIT = 0,
  BUILD = 1,
  WAIT_BUILD = 2,
  COMPLETE = 3,
  WAIT_COMPLETE = 4,
  FINISH = 5,
  MAX_STATUS,
};

struct ObLSRestoreStatusHelper
{
public:
  static bool is_valid(const ObLSRestoreStatus &status);
  static int get_next_change_status(
      const ObLSRestoreStatus &curr_status,
      const int32_t result,
      ObLSRestoreStatus &next_status);
  static const char *get_status_str(const ObLSRestoreStatus &status);
};

class ObRestoreHandler
{
public:
  ObRestoreHandler();
  virtual ~ObRestoreHandler();
  int init(ObLS *ls, common::ObInOutBandwidthThrottle *bandwidth_throttle);

  int add_ls_restore_task(const ObRestoreTask &task);
  virtual int process();
  int switch_next_stage(const int32_t result);
  int check_task_exist(const share::ObTaskId &task_id, bool &is_exist);
  void destroy();
  void stop();
  void wait(bool &wait_finished);
  bool is_cancel() const;
  bool is_complete() const;
  bool is_dag_net_cleared() const;
  void set_dag_net_cleared();
  int set_result(const int32_t result);
  int get_restore_task_and_status(
      ObRestoreTask &task,
      ObLSRestoreStatus &status);
  TO_STRING_KV(K_(is_inited), KPC_(ls), K_(start_ts), K_(finish_ts), K_(task_list),
                K_(status), K_(result), K_(is_stop), K_(is_cancel),
                K_(is_complete), K_(is_dag_net_cleared), K_(is_timer_scheduled));

private:
  class ObRestoreHandlerTimerTask : public common::ObTimerTask
  {
  public:
    explicit ObRestoreHandlerTimerTask(ObRestoreHandler &handler) : handler_(handler) {}
    virtual ~ObRestoreHandlerTimerTask() {}
    virtual void runTimerTask() override;
  private:
    ObRestoreHandler &handler_;
  };

  static constexpr int64_t RESTORE_HANDLER_SCHEDULE_INTERVAL_US = 1000L * 1000L; // 1s

  void reuse_();
  int start_schedule_();
  void stop_schedule_(bool need_wait);
  int get_status_(ObLSRestoreStatus &status);
  int check_task_list_empty_(bool &is_empty);
  int get_result_(int32_t &result);
  bool is_restore_failed_() const;
  int handle_current_task_(
      bool &need_wait,
      int32_t &task_result);
  int cancel_current_task_();
  int do_init_status_();
  int do_build_status_();
  int do_complete_status_();
  int do_finish_status_();
  int do_wait_status_();
  int generate_build_dag_net_();
  int generate_complete_dag_net_();
  int report_result_();
  int report_meta_table_();
  int switch_next_stage_with_nolock_(const int32_t result);
  int get_restore_task_(ObRestoreTask &task);
  int get_restore_task_with_nolock_(ObRestoreTask &task) const;
  int check_need_to_abort_(bool &need_to_abort);
  int check_task_exist_(bool &is_exist);
  int check_task_exist_with_nolock_(const share::ObTaskId &task_id, bool &is_exist) const;
  template<typename DagNetType>
  int schedule_dag_net_(const share::ObIDagInitParam *param, const bool check_cancel);

private:
  bool is_inited_;
  ObLS *ls_;
  ObInOutBandwidthThrottle *bandwidth_throttle_;
  int64_t start_ts_;
  int64_t finish_ts_;
  ObSEArray<ObRestoreTask, 1> task_list_;
  common::SpinRWLock lock_;
  ObLSRestoreStatus status_;
  int32_t result_;
  bool is_stop_;
  bool is_cancel_;
  bool is_complete_;
  bool is_dag_net_cleared_;
  ObRestoreHandlerTimerTask timer_task_;
  bool is_timer_scheduled_;

  DISALLOW_COPY_AND_ASSIGN(ObRestoreHandler);
};

template<typename DagNetType>
int ObRestoreHandler::schedule_dag_net_(
    const share::ObIDagInitParam *param,
    const bool check_cancel)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "restore handler not inited", K(ret));
  } else if (OB_ISNULL(param) || !param->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid dag net param", K(ret), K(status_), KP(param));
  } else {
    const int32_t cancel_result = OB_CANCELED;
    if (check_cancel && is_cancel_) {
      STORAGE_LOG(INFO, "skip schedule dag net when canceled", K(ret), K(status_), KPC(ls_),
          K(cancel_result), K(check_cancel));
      if (OB_FAIL(switch_next_stage_with_nolock_(cancel_result))) {
        STORAGE_LOG(WARN, "failed to switch next stage when canceled", K(ret), K(status_));
      }
    } else {
      ObTenantDagScheduler *scheduler = nullptr;
      if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "failed to get ObTenantDagScheduler from MTL", K(ret));
      } else if (FALSE_IT(is_dag_net_cleared_ = false)) {
      } else if (OB_FAIL(scheduler->create_and_add_dag_net<DagNetType>(param))) {
        STORAGE_LOG(WARN, "failed to create and add restore dag net", K(ret), K(status_), KPC(ls_));
        is_dag_net_cleared_ = true;
      } else {
        STORAGE_LOG(INFO, "schedule restore dag net success", K(ret), K(status_), KPC(ls_));
      }
    }
  }
  return ret;
}

} // namespace restore
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_LS_RESTORE_HANDLER_
