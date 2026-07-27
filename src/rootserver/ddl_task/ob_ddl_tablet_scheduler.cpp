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
#define USING_LOG_PREFIX RS

#include "ob_ddl_tablet_scheduler.h"
#include "ob_index_build_task.h"
#include "rootserver/ob_ddl_service_launcher.h" // for ObDDLServiceLauncher
#include "rootserver/ob_local_management_service.h"
#include "share/ob_ddl_checksum.h"
#include "src/observer/ob_inner_sql_connection.h"
#include "observer/vector_index/ob_vector_index_util.h"
#include "lib/utility/serialization.h"

using namespace oceanbase::rootserver;
using namespace oceanbase::common;
using namespace oceanbase::common::sqlclient;
using namespace oceanbase::obcall;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::sql;

ObDDLTabletScheduler::ObDDLTabletScheduler()
  : is_inited_(false), table_id_(OB_INVALID_ID), ref_data_table_id_(OB_INVALID_ID),
    task_id_(OB_INVALID_ID), parallelism_(0), snapshot_version_(0), trace_id_(),
    lock_(), local_management_service_(nullptr), all_tablets_(), running_tablets_(),
    running_execution_id_(-1), tablet_id_to_data_size_(), tablet_id_to_data_row_cnt_(),
    tablet_id_to_execution_id_map_()
{

}
ObDDLTabletScheduler::~ObDDLTabletScheduler()
{

}

int ObDDLTabletScheduler::init(const uint64_t table_id,
                               const uint64_t ref_data_table_id,
                               const int64_t  task_id,
                               const int64_t  parallelism,
                               const int64_t  snapshot_version,
                               const common::ObCurTraceId::TraceId &trace_id,
                               const ObIArray<ObTabletID> &tablets)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator arena("tblt_sched_init");
  common::ObArray<ObString> running_sql_info;
  common::ObArray<ObTabletID> ref_data_table_tablets;
  common::hash::ObHashMap<uint64_t, bool> tablet_finished_map;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_ISNULL(local_management_service_ = GCTX.local_management_service_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("local_management_service is null", K(ret), KP(local_management_service_));
  } else if (!ObDDLServiceLauncher::is_ddl_service_started()) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("ddl service not started", KR(ret));
  } else if (OB_UNLIKELY(
        !(OB_INVALID_ID != table_id
          && OB_INVALID_ID != ref_data_table_id
          && task_id > 0
          && parallelism > 0
          && snapshot_version > 0
          && trace_id.is_valid()
          && tablets.count() > 0))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_id), K(ref_data_table_id), K(task_id), K(parallelism), K(snapshot_version), K(trace_id), K(tablets.count()));
  } else if (OB_FAIL(ObDDLUtil::get_tablets(ref_data_table_id, ref_data_table_tablets))) {
    LOG_WARN("failed to get ref data table tablet ids", K(ret), K(ref_data_table_id), K(ref_data_table_tablets));
  } else if (OB_UNLIKELY(tablets.count() != ref_data_table_tablets.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("index table tablets count is not equal to data table tablets count", K(ret), K(tablets.count()), K(ref_data_table_tablets.count()));
  } else if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
  } else if (OB_FAIL(tablet_finished_map.create(tablets.count(), ObModIds::OB_SSTABLE_CREATE_INDEX))) {
    LOG_WARN("fail to create column checksum map", K(ret), K(tablets.count()));
  } else if (OB_FAIL(tablet_id_to_data_size_.create(ref_data_table_tablets.count(), ObModIds::OB_SSTABLE_CREATE_INDEX))) {
    LOG_WARN("fail to create column checksum map", K(ret), K(ref_data_table_tablets.count()));
  } else if (OB_FAIL(tablet_id_to_data_row_cnt_.create(ref_data_table_tablets.count(), ObModIds::OB_SSTABLE_CREATE_INDEX))) {
    LOG_WARN("fail to create column checksum map", K(ret), K(ref_data_table_tablets.count()));
  } else if (!tablet_id_to_execution_id_map_.created() && OB_FAIL(tablet_id_to_execution_id_map_.create(tablets.count(), ObModIds::OB_SSTABLE_CREATE_INDEX))) {
    LOG_ERROR("fail to create tablet id to execution id map", K(ret), K(tablets.count()));
  } else if (OB_FAIL(ObDDLChecksumOperator::get_local_index_tablet_finish_status(ref_data_table_id,
    table_id,
    task_id,
    tablets,
    *GCTX.sql_proxy_,
    tablet_finished_map))) {
    LOG_WARN("fail to get tablet checksum status", K(ret), K(table_id), K(task_id), K(tablets));
  } else if (OB_FAIL(ObDDLTaskRecordOperator::get_running_tasks_inner_sql(
      *GCTX.sql_proxy_, trace_id, task_id, snapshot_version, arena, running_sql_info))) {
    LOG_WARN("get running tasks inner sql fail", K(ret), K(trace_id), K(task_id), K(snapshot_version), K(running_sql_info));
  } else {
    bool is_running_status = false;
    bool is_finished_status = false;
    int64_t tablet_data_size = 0;
    int64_t tablet_data_row_cnt = 0;
    ObArray<ObTabletID> part_tablets;
    ObArray<ObString> partition_names;
    for (int64_t i = 0; i < tablets.count() && OB_SUCC(ret); i++) {
      is_running_status = false;
      is_finished_status = false;
      tablet_data_size = 0;
      tablet_data_row_cnt = 0;
      part_tablets.reuse();
      partition_names.reuse();
      if (OB_FAIL(ObDDLUtil::get_tablet_data_size(ref_data_table_tablets.at(i), tablet_data_size))) {
        LOG_WARN("fail to get tablet data size", K(ret), K(ref_data_table_tablets.at(i)), K(tablet_data_size));
      } else if (OB_FAIL(ObDDLUtil::get_tablet_data_row_cnt(ref_data_table_tablets.at(i), tablet_data_row_cnt))) {
        LOG_WARN("fail to get tablet row count", K(ret), K(ref_data_table_tablets.at(i)), K(tablet_data_row_cnt));
      } else if (OB_FAIL(tablet_id_to_data_size_.set_refactored(ref_data_table_tablets.at(i).id(), tablet_data_size, true /* overwrite */))) {
        LOG_WARN("table id to data size map set fail", K(ret), K(ref_data_table_tablets.at(i).id()), K(tablet_data_size));
      } else if (OB_FAIL(tablet_id_to_data_row_cnt_.set_refactored(ref_data_table_tablets.at(i).id(), tablet_data_row_cnt, true /* overwrite */))) {
        LOG_WARN("table id to data size map set fail", K(ret), K(ref_data_table_tablets.at(i).id()), K(tablet_data_row_cnt));
      } else if (OB_FAIL(part_tablets.push_back(tablets.at(i)))) {
        LOG_WARN("fail to push back", K(ret), K(tablets.at(i)));
      } else if (OB_FAIL(ObDDLUtil::get_index_table_batch_partition_names(ref_data_table_id, table_id, part_tablets, arena, partition_names))) {
        LOG_WARN("fail to get index table batch partition names", K(ret), K(ref_data_table_id), K(table_id), K(part_tablets), K(partition_names));
      } else {
        if (OB_FAIL(tablet_finished_map.get_refactored(tablets.at(i).id(), is_finished_status))) {
          if (OB_HASH_NOT_EXIST == ret) {
            ret = OB_SUCCESS;
          }
        }
        for (int64_t j = 0; j < running_sql_info.count() && OB_SUCC(ret); j++) {
          is_running_status = false;
          if (OB_FAIL(ObDDLUtil::check_target_partition_is_running(running_sql_info.at(j), partition_names.at(0), arena, is_running_status))) {
            LOG_WARN("fail to check target partition is running", K(ret), K(running_sql_info.at(j)), K(partition_names.at(0)), K(is_running_status));
          } else if (is_running_status) {
            break;
          }
        }
        if (OB_SUCC(ret)) {
          if (!is_running_status && is_finished_status) {
            LOG_INFO("tablet has complemented data", K(ret), K(table_id), K(ref_data_table_id), K(tablets.at(i)));
          } else {
            if (is_running_status && OB_FAIL(running_tablets_.push_back(tablets.at(i)))) {
              LOG_WARN("fail to push running tablet", K(ret), K(table_id), K(ref_data_table_id),
                       K(tablets.at(i)), K(is_finished_status));
            }
            if (OB_SUCC(ret) && OB_FAIL(all_tablets_.push_back(tablets.at(i)))) {
              LOG_WARN("fail to push back", K(ret), K(tablets.at(i)));
            }
          }
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    table_id_ = table_id;
    ref_data_table_id_ = ref_data_table_id;
    task_id_ = task_id;
    parallelism_ = parallelism;
    snapshot_version_ = snapshot_version;
    trace_id_ = trace_id;
    is_inited_ = true;
    LOG_INFO("success to init", K(ret), K(table_id), K(ref_data_table_id), K(task_id), K(parallelism), K(snapshot_version), K(trace_id), K(tablets), K(all_tablets_.count()), K(running_tablets_.count()));
  } else {
    LOG_INFO("fail to init", K(ret), K(table_id), K(ref_data_table_id), K(task_id), K(parallelism), K(snapshot_version), K(trace_id), K(tablets), K(all_tablets_.count()), K(running_tablets_.count()));
    destroy();
  }
  return ret;
}

int ObDDLTabletScheduler::get_next_batch_tablets(const bool is_ddl_retryable,
                                                 int64_t &parallelism,
                                                 int64_t &new_execution_id,
                                                 ObIArray<ObTabletID> &tablets)
{
  int ret = OB_SUCCESS;
  bool need_send_task = false;
  parallelism = 0;
  new_execution_id = 0;
  tablets.reset();
  share::ObDDLType task_type = share::DDL_CREATE_PARTITIONED_LOCAL_INDEX;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (is_all_tasks_finished()) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(determine_if_need_to_send_new_task(need_send_task))) {
    LOG_WARN("fail to get status of if need to send new task", K(ret), K(need_send_task));
  } else if (!need_send_task) {
    if (is_recovered_running_task()) {
      if (OB_FAIL(check_running_task_completion_status())) {
        LOG_WARN("fail to check running task completion status", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      ret = OB_EAGAIN;
    }
  } else if (OB_FAIL(get_next_parallelism(parallelism))) {
    LOG_WARN("fail to get next parallelism", K(ret), K(parallelism));
  } else if (OB_FAIL(get_unfinished_tablets(task_type, is_ddl_retryable, new_execution_id, tablets))) {
    LOG_WARN("failed to get unfinished tablets", K(ret), K(new_execution_id), K(tablets));
  }
  return ret;
}

int ObDDLTabletScheduler::confirm_batch_tablets_status(const int64_t execution_id, const bool finish_status, const ObIArray<ObTabletID> &tablets)
{
  int ret = OB_SUCCESS;
  TCWLockGuard guard(lock_);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(tablets.count() < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablets array is null", K(ret), K(tablets.count()));
  } else if (OB_UNLIKELY(all_tablets_.empty() || running_tablets_.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet queue is null", K(ret), K(all_tablets_.count()), K(running_tablets_.count()));
  } else {
    if (execution_id != -1) { //execution_id == -1 indicates the task before switching to rs is confirming
      if (OB_UNLIKELY(execution_id != running_execution_id_)) {
        ret = OB_TASK_EXPIRED;
        LOG_WARN("receive a mismatch execution result", K(ret), K(execution_id), K_(running_execution_id), K(tablets), K(finish_status));
      }
    }
    if (OB_SUCC(ret)) {
      running_tablets_.reset();
      running_execution_id_ = -1;
      if (finish_status && OB_FAIL(remove_finished_tablets_(tablets))) {
        LOG_WARN("fail to remove finished tablets", K(ret), K(tablets));
      }
    }
  }
  LOG_INFO("confirm batch tablets status", K(ret), K(execution_id), K(finish_status), K(tablets));
  return ret;
}

// in (idempotent_mode && ddl can not retry) case, every tablet's execution id cannot be pushed more than once
// so push_tablet_execution_id does the following:
// 1.push tablet execution id, and check if the tablet execution id is pushed more than once
// 2.push task execution id anyway, it is no need to check task execution id, we always push task execution id high to avoid different inner sql has same task execution id.
int ObDDLTabletScheduler::push_tablet_execution_id(const bool ddl_can_retry,
                                                   const common::ObIArray<common::ObTabletID> &tablets,
                                                   int64_t &new_task_execution_id)
{
  int ret = OB_SUCCESS;
  new_task_execution_id = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("scheduler not init", K(ret));
  } else if (OB_UNLIKELY(tablets.count() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tablets", K(ret), K(tablets.count()));
  } else {
    int64_t next_tablet_execution_id = DEFAULT_EXECUTION_ID;
    for (int64_t i = 0; OB_SUCC(ret) && i < tablets.count(); ++i) {
      const ObTabletID &tablet = tablets.at(i);
      int64_t tablet_execution_id = -1;
      if (OB_FAIL(tablet_id_to_execution_id_map_.get_refactored(tablet, tablet_execution_id))) {
        if (OB_HASH_NOT_EXIST != ret) {
          LOG_WARN("failed to get tablet execution id", K(ret), K(tablet));
        } else {
          tablet_execution_id = -1;
          ret = OB_SUCCESS;
        }
      } else if (OB_FAIL(ObDDLTask::calc_next_execution_id(tablet_execution_id, ddl_can_retry, next_tablet_execution_id))) {
        LOG_WARN("calc next execution id failed", K(ret), K(tablet_execution_id), K(ddl_can_retry));
      } else if (OB_FAIL(tablet_id_to_execution_id_map_.set_refactored(tablet, next_tablet_execution_id, true))) {
        LOG_WARN("set tablet execution id failed", K(ret), K(tablet), K(next_tablet_execution_id));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(push_task_execution_id(new_task_execution_id))) {
        LOG_WARN("failed to push execution id", K(ret), K(new_task_execution_id));
      }
    }
  }
  return ret;
}

// push task execution id high anyway, cause retry check is done in push_tablet_execution_id
int ObDDLTabletScheduler::push_task_execution_id(int64_t &new_task_execution_id)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  int64_t task_status = 0;
  int64_t task_execution_id = 0;
  int64_t ret_code = OB_SUCCESS;
  int64_t unused_snapshot_ver = OB_INVALID_VERSION;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("scheduler not init", K(ret));
  } else if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
  } else if (OB_FAIL(trans.start(GCTX.sql_proxy_))) {
    LOG_WARN("start transaction failed", K(ret));
  } else if (OB_FAIL(ObDDLTaskRecordOperator::select_for_update(trans, task_id_, task_status, task_execution_id, ret_code, unused_snapshot_ver))) {
    LOG_WARN("select for update failed", K(ret), K(task_id_));
  } else if (task_execution_id == -1) {
    task_execution_id = 0;
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(ObDDLTaskRecordOperator::update_execution_id(trans, task_id_, task_execution_id + 1))) {
      LOG_WARN("update execution id failed", K(ret), K(task_id_), K(task_execution_id + 1));
    } else {
      new_task_execution_id = task_execution_id + 1;
    }
  }

  bool commit = (OB_SUCCESS == ret);
  int tmp_ret = trans.end(commit);
  if (OB_SUCCESS != tmp_ret) {
    LOG_WARN("fail to end trans", K(tmp_ret));
    ret = (OB_SUCCESS == ret) ? tmp_ret : ret;
  }
  return ret;
}

OB_DEF_SERIALIZE(ObDDLTabletScheduler)
{
  int ret = OB_SUCCESS;
  common::ObSArray<ObTabletExecutionIdPair> pairs;
  if (tablet_id_to_execution_id_map_.created()) {
    for (hash::ObHashMap<common::ObTabletID, int64_t>::const_iterator iter = tablet_id_to_execution_id_map_.begin();
          OB_SUCC(ret) && iter != tablet_id_to_execution_id_map_.end();
          ++iter) {
      ObTabletExecutionIdPair pair(iter->first, iter->second);
      if (OB_FAIL(pairs.push_back(pair))) {
        LOG_WARN("failed to push tablet execution id pair", K(ret), K(pair));
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(pairs.serialize(buf, buf_len, pos))) {
      LOG_WARN("failed to serialize tablet execution id pairs", K(ret));
    }
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObDDLTabletScheduler)
{
  int ret = OB_SUCCESS;
  common::ObSArray<ObTabletExecutionIdPair> pairs;
  if (OB_FAIL(pairs.deserialize(buf, data_len, pos))) {
    LOG_WARN("failed to deserialize tablet execution id pairs", K(ret));
  } else if (pairs.count() > 0) {
    if (!tablet_id_to_execution_id_map_.created()) {
      int64_t bucket_num = pairs.count();
      if (OB_FAIL(tablet_id_to_execution_id_map_.create(bucket_num, ObModIds::OB_SSTABLE_CREATE_INDEX))) {
        LOG_WARN("failed to create tablet execution id map", K(ret), K(pairs.count()));
      }
    } else if (OB_FAIL(tablet_id_to_execution_id_map_.reuse())) {
      LOG_WARN("failed to reuse tablet execution id map", K(ret));
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < pairs.count(); ++i) {
      const ObTabletExecutionIdPair &pair = pairs.at(i);
      if (OB_FAIL(tablet_id_to_execution_id_map_.set_refactored(pair.tablet_id_, pair.execution_id_, true))) {
        LOG_WARN("failed to set tablet execution id pair", K(ret), K(pair));
      }
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObDDLTabletScheduler)
{
  int64_t len = 0;
  if (tablet_id_to_execution_id_map_.created()) {
    const int64_t count = tablet_id_to_execution_id_map_.size();
    len = serialization::encoded_length_vi64(count);
    for (hash::ObHashMap<common::ObTabletID, int64_t>::const_iterator iter = tablet_id_to_execution_id_map_.begin();
          iter != tablet_id_to_execution_id_map_.end();
          ++iter) {
      ObTabletExecutionIdPair pair(iter->first, iter->second);
      len += pair.get_serialize_size();
    }
  } else {
    len = serialization::encoded_length_vi64(0);
  }
  return len;
}

int ObDDLTabletScheduler::get_next_parallelism(int64_t &parallelism)
{
  int ret = OB_SUCCESS;
  parallelism = 0;
  TCRLockGuard guard(lock_);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else {
    if (all_tablets_.count() > 0) {
      parallelism = parallelism_;
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet queue is empty", K(ret), K(all_tablets_.count()), K(parallelism));
    }
  }
  return ret;
}


int ObDDLTabletScheduler::get_unfinished_tablets(const share::ObDDLType task_type, const bool ddl_can_retry,
                                                 int64_t &new_execution_id, ObIArray<ObTabletID> &tablets)
{
  int ret = OB_SUCCESS;
  new_execution_id = 0;
  tablets.reset();
  ObArray<ObTabletID> tablet_queue;
  uint64_t left_space_size = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_FAIL(get_to_be_scheduled_tablets(tablet_queue))) {
    LOG_WARN("fail to get to be scheduled tablets", K(ret), K(tablet_queue));
  } else if (OB_UNLIKELY(tablet_queue.count() < 1)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet queue is null", K(ret), K(tablet_queue.count()));
  } else if (OB_FAIL(ObDDLUtil::get_ls_host_left_disk_space(left_space_size))) {
    LOG_WARN("fail to get local disk free space", K(ret), K(left_space_size));
  } else if (OB_FAIL(calculate_candidate_tablets(left_space_size, tablet_queue, tablets))) {
    LOG_WARN("fail to use strategy to get tablets", K(ret), K(left_space_size), K(tablet_queue), K(tablets));
  } else if (OB_FAIL(push_tablet_execution_id(ddl_can_retry, tablets, new_execution_id))) {
    LOG_WARN("failed to push tablet execution id", K(ret), K(task_type), K(ddl_can_retry), K(tablets), K(new_execution_id));
  } else {
    TCWLockGuard guard(lock_);
    if (OB_FAIL(running_tablets_.assign(tablets))) {
      LOG_WARN("ObArray assign failed", K(ret), K(tablets));
    } else {
      running_execution_id_ = new_execution_id;
    }
  }
  return ret;
}

int ObDDLTabletScheduler::get_to_be_scheduled_tablets(ObIArray<ObTabletID> &tablets)
{
  int ret = OB_SUCCESS;
  tablets.reset();
  TCWLockGuard guard(lock_);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(all_tablets_.empty() || !running_tablets_.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet queue state is invalid", K(ret), K(all_tablets_.count()), K(running_tablets_.count()));
  } else if (OB_FAIL(tablets.assign(all_tablets_))) {
    LOG_WARN("ObArray assign failed", K(ret), K_(all_tablets));
  }
  return ret;
}

int ObDDLTabletScheduler::calculate_candidate_tablets(const uint64_t left_space_size, const ObIArray<ObTabletID> &in_tablets, ObIArray<ObTabletID> &out_tablets)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *data_table_schema = nullptr;
  const ObTableSchema *index_schema = nullptr;
  out_tablets.reset();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("fail to get schema guard", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema( ref_data_table_id_, data_table_schema))) {
    LOG_WARN("get table schema failed", K(ret), K(ref_data_table_id_));
  } else if (OB_ISNULL(data_table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("error unexpected, data table schema is null", K(ret), K(ref_data_table_id_));
  } else if (OB_FAIL(schema_guard.get_table_schema( table_id_, index_schema))) {
    LOG_WARN("get table schema failed", K(ret), K(table_id_));
  } else if (OB_ISNULL(index_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("error unexpected, index table schema is null", K(ret), K(table_id_));
  } else {
    ObPartition **data_partitions = data_table_schema->get_part_array();
    const ObPartitionLevel part_level = data_table_schema->get_part_level();
    if (OB_ISNULL(data_partitions)) {
      ret = OB_PARTITION_NOT_EXIST;
      LOG_WARN("data table part array is null", K(ret), KPC(this));
    } else {
      int64_t part_index = -1;
      int64_t subpart_index = -1;
      int64_t pre_data_size = 0;
      int64_t pre_data_row_cnt = 0;
      int64_t tablet_data_size = 0;
      int64_t tablet_data_row_cnt = 0;
      uint64_t task_max_data_size = 0;
      const int64_t task_max_data_row_cnt = 50000000;
      if (left_space_size > 0) {
        task_max_data_size = left_space_size / 30; // according to the estimated maximum temporary space amplification factor 30, ensure that the current remaining disk space can complete index construction
      } else {
        task_max_data_size = 5368709120; // 5GB
      }
      for (int64_t i = 0; i < in_tablets.count() && OB_SUCC(ret); i++) {
        tablet_data_size = 0;
        tablet_data_row_cnt = 0;
        if (OB_FAIL(index_schema->get_part_idx_by_tablet(in_tablets.at(i), part_index, subpart_index))) {
          LOG_WARN("failed to get part idx by tablet", K(ret), K(in_tablets.at(i)), K(part_index), K(subpart_index));
        } else {
          if (PARTITION_LEVEL_ONE == part_level) {
            if (OB_FAIL(tablet_id_to_data_size_.get_refactored(data_partitions[part_index]->get_tablet_id().id(), tablet_data_size))) {
              LOG_WARN("fail to get tablet data size", K(ret), K(data_partitions[part_index]->get_tablet_id()), K(tablet_data_size));
            } else if (OB_FAIL(tablet_id_to_data_row_cnt_.get_refactored(data_partitions[part_index]->get_tablet_id().id(), tablet_data_row_cnt))) {
              LOG_WARN("fail to get tablet data size", K(ret), K(data_partitions[part_index]->get_tablet_id()), K(tablet_data_row_cnt));
            }
          } else if (PARTITION_LEVEL_TWO == part_level) {
            ObSubPartition **data_subpart_array = data_partitions[part_index]->get_subpart_array();
            if (OB_ISNULL(data_subpart_array)) {
              ret = OB_PARTITION_NOT_EXIST;
              LOG_WARN("part array is null", K(ret), KPC(this));
            } else if (OB_FAIL(tablet_id_to_data_size_.get_refactored(data_subpart_array[subpart_index]->get_tablet_id().id(), tablet_data_size))) {
              LOG_WARN("fail to get tablet data size", K(ret), K(data_subpart_array[subpart_index]->get_tablet_id()), K(tablet_data_size));
            } else if (OB_FAIL(tablet_id_to_data_row_cnt_.get_refactored(data_subpart_array[subpart_index]->get_tablet_id().id(), tablet_data_row_cnt))) {
              LOG_WARN("fail to get tablet data size", K(ret), K(data_subpart_array[subpart_index]->get_tablet_id()), K(tablet_data_row_cnt));
            }
          }
          if (OB_SUCC(ret)) {
            bool satisfied_built_vec_index_if_need = true;
            if (index_schema->is_vec_hnsw_index() && !ObVectorIndexUtil::check_vector_index_memory(schema_guard, *index_schema, tablet_data_row_cnt + pre_data_row_cnt)) {
              satisfied_built_vec_index_if_need = false;
            } else if (index_schema->is_vec_ivf_index() && !ObVectorIndexUtil::check_ivf_vector_index_memory(schema_guard, *index_schema, tablet_data_row_cnt + pre_data_row_cnt)) {
              satisfied_built_vec_index_if_need = false;
            }
            if (pre_data_size == 0 || ((tablet_data_row_cnt + pre_data_row_cnt) <= task_max_data_row_cnt && (tablet_data_size + pre_data_size) <= task_max_data_size && satisfied_built_vec_index_if_need)) {
              if (OB_FAIL(out_tablets.push_back(in_tablets.at(i)))) {
                LOG_WARN("fail to push back", K(ret), K(in_tablets.at(i)));
              } else {
                pre_data_size = pre_data_size + tablet_data_size;
                pre_data_row_cnt = pre_data_row_cnt + tablet_data_row_cnt;
              }
            } else {
              break;
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObDDLTabletScheduler::remove_finished_tablets_(const ObIArray<ObTabletID> &tablets)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < tablets.count(); ++i) {
    bool found = false;
    for (int64_t j = 0; OB_SUCC(ret) && j < all_tablets_.count(); ++j) {
      if (tablets.at(i) == all_tablets_.at(j)) {
        if (OB_FAIL(all_tablets_.remove(j))) {
          LOG_WARN("failed to remove tablet id", K(ret), K(i), K(j), K_(all_tablets), K(tablets));
        } else {
          found = true;
        }
        break;
      }
    }
    if (OB_SUCC(ret) && !found) {
      LOG_INFO("finished tablet is not in pending queue", K(tablets.at(i)), K_(all_tablets));
    }
  }
  LOG_INFO("remove finished tablets from pending queue", K(ret), K(tablets), K_(all_tablets));
  return ret;
}

int ObDDLTabletScheduler::check_running_task_completion_status()
{
  int ret = OB_SUCCESS;
  ObArray<ObTabletID> running_tablet_queue;
  common::hash::ObHashMap<uint64_t, bool> tablet_finished_map;
  ObArenaAllocator arena("tblt_sched_get");
  common::ObArray<ObString> running_sql_info;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
  } else {
    {
      TCRLockGuard guard(lock_);
      if (OB_FAIL(running_tablet_queue.assign(running_tablets_))) {
        LOG_WARN("ObArray assign failed", K(ret), K_(running_tablets));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (running_tablet_queue.empty()) {
      // do nothing, the running task has finished and reported
    } else if (OB_FAIL(ObDDLTaskRecordOperator::get_running_tasks_inner_sql(
          *GCTX.sql_proxy_, trace_id_, task_id_, snapshot_version_, arena, running_sql_info))) {
      LOG_WARN("get running tasks inner sql fail", K(ret), K(trace_id_), K(task_id_), K(snapshot_version_), K(running_sql_info));
    } else {
      ObArray<ObString> partition_names;
      bool is_running_status = false;
      if (OB_FAIL(ObDDLUtil::get_index_table_batch_partition_names(
            ref_data_table_id_, table_id_, running_tablet_queue, arena, partition_names))) {
        LOG_WARN("fail to get index table batch partition names", K(ret), K(ref_data_table_id_), K(table_id_), K(running_tablet_queue), K(partition_names));
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < partition_names.count(); i++) {
        is_running_status = false;
        for (int64_t j = 0; OB_SUCC(ret) && j < running_sql_info.count(); j++) {
          if (OB_FAIL(ObDDLUtil::check_target_partition_is_running(
                running_sql_info.at(j), partition_names.at(i), arena, is_running_status))) {
            LOG_WARN("fail to check target partition is running", K(ret), K(running_sql_info.at(j)), K(partition_names.at(i)), K(is_running_status));
          } else if (is_running_status) {
            break;
          }
        }
        if (is_running_status) {
          break;
        }
      }
      if (OB_SUCC(ret) && !is_running_status) {
        if (OB_FAIL(tablet_finished_map.create(running_tablet_queue.count(), ObModIds::OB_SSTABLE_CREATE_INDEX))) {
          LOG_WARN("fail to create tablet checksum status map", K(ret), K(running_tablet_queue.count()));
        } else if (OB_FAIL(ObDDLChecksumOperator::get_local_index_tablet_finish_status(ref_data_table_id_,
          table_id_,
          task_id_,
          running_tablet_queue,
          *GCTX.sql_proxy_,
          tablet_finished_map))) {
          LOG_WARN("fail to get tablet checksum status", K(ret), K(table_id_), K(task_id_), K(running_tablet_queue));
        } else {
          bool is_finished_status = true;
          for (int64_t i = 0; i < running_tablet_queue.count() && OB_SUCC(ret); i++) {
            if (OB_FAIL(tablet_finished_map.get_refactored(running_tablet_queue.at(i).id(), is_finished_status))) {
              if (OB_HASH_NOT_EXIST == ret) {
                LOG_ERROR("tablet checksum is not exist", K(ret), K(running_tablet_queue.at(i)), K(is_finished_status));
                is_finished_status = false;
                ret = OB_SUCCESS;
                break;
              } else {
                LOG_WARN("fail to get refactored", K(ret), K(running_tablet_queue.at(i)), K(is_finished_status));
              }
            }
          }
          if (OB_SUCC(ret)) {
            if (OB_FAIL(confirm_batch_tablets_status(-1, is_finished_status, running_tablet_queue))) {
              LOG_WARN("fail to confirm batch tablets status", K(ret), K(is_finished_status), K(running_tablet_queue));
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObDDLTabletScheduler::determine_if_need_to_send_new_task(bool &status)
{
  int ret = OB_SUCCESS;
  status = false;
  TCRLockGuard guard(lock_);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    status = running_tablets_.empty() && !all_tablets_.empty();
  }
  return ret;
}

bool ObDDLTabletScheduler::is_all_tasks_finished()
{
  TCRLockGuard guard(lock_);
  return all_tablets_.empty();
}

bool ObDDLTabletScheduler::is_recovered_running_task()
{
  TCRLockGuard guard(lock_);
  return !running_tablets_.empty() && -1 == running_execution_id_;
}

void ObDDLTabletScheduler::destroy()
{
  is_inited_ = false;
  table_id_ = 0;
  ref_data_table_id_ = 0;
  task_id_ = 0;
  parallelism_ = 0;
  snapshot_version_ = 0;
  trace_id_.reset();
  all_tablets_.reset();
  running_tablets_.reset();
  running_execution_id_ = -1;
  tablet_id_to_data_size_.destroy();
  tablet_id_to_data_row_cnt_.destroy();
}
