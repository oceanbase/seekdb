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

#ifndef OCEANBASE_ROOTSERVER_OB_DDL_TABLET_SCHEDULER_H
#define OCEANBASE_ROOTSERVER_OB_DDL_TABLET_SCHEDULER_H

#include "rootserver/ddl_task/ob_ddl_task.h"

namespace oceanbase
{
namespace rootserver
{
class ObDDLTabletScheduler final
{
  OB_UNIS_VERSION(1);
public:
  ObDDLTabletScheduler();
  ~ObDDLTabletScheduler();
  int init(const uint64_t table_id,
           const uint64_t ref_data_table_id,
           const int64_t  task_id,
           const int64_t  parallelism,
           const int64_t  snapshot_version,
           const common::ObCurTraceId::TraceId &trace_id,
           const ObIArray<ObTabletID> &tablets);
  int get_next_batch_tablets(const bool is_ddl_retryable,
                             int64_t &parallelism,
                             int64_t &new_execution_id,
                             ObIArray<ObTabletID> &tablets);
  int confirm_batch_tablets_status(const int64_t execution_id, const bool finish_status, const ObIArray<ObTabletID> &tablets);
  TO_STRING_KV(K_(is_inited), K_(table_id), K_(ref_data_table_id),
              K_(task_id), K_(parallelism), K_(snapshot_version), K_(trace_id), K_(all_tablets), K_(running_tablets));
private:
  int get_next_parallelism(int64_t &parallelism);
  int get_unfinished_tablets(const share::ObDDLType task_type, const bool ddl_can_retry, int64_t &new_execution_id, ObIArray<ObTabletID> &tablets);
  int get_to_be_scheduled_tablets(ObIArray<ObTabletID> &tablets);
  int calculate_candidate_tablets(const uint64_t left_space_size, const ObIArray<ObTabletID> &in_tablets, ObIArray<ObTabletID> &out_tablets);
  int determine_if_need_to_send_new_task(bool &status);
  int check_running_task_completion_status();
  int remove_finished_tablets_(const ObIArray<ObTabletID> &tablets);
  bool is_all_tasks_finished();
  int push_tablet_execution_id(const bool ddl_can_retry,
                               const common::ObIArray<common::ObTabletID> &tablets,
                               int64_t &new_task_execution_id);
  bool is_recovered_running_task();
  int push_task_execution_id(int64_t &new_task_execution_id);
  void destroy();
private:
  bool is_inited_;
  uint64_t table_id_;
  uint64_t ref_data_table_id_;
  int64_t task_id_;
  int64_t parallelism_;
  int64_t snapshot_version_;
  common::ObCurTraceId::TraceId trace_id_;
  common::TCRWLock lock_; // protects pending/running tablet queues against ddl builder and scheduler races.
  ObLocalManagementService *local_management_service_;
  ObArray<ObTabletID> all_tablets_;
  ObArray<ObTabletID> running_tablets_;
  int64_t running_execution_id_;
  common::hash::ObHashMap<int64_t, int64_t> tablet_id_to_data_size_;
  common::hash::ObHashMap<int64_t, int64_t> tablet_id_to_data_row_cnt_;
  common::hash::ObHashMap<common::ObTabletID, int64_t> tablet_id_to_execution_id_map_;
};
} // end namespace rootserver
} // end namespace oceanbase

#endif /* OCEANBASE_ROOTSERVER_OB_DDL_TABLET_SCHEDULER_H */
