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

#include "ob_partition_split_task.h"
#include "rootserver/ob_rs_serial_call.h"
#include "observer/omt/ob_tenant_timezone_mgr.h"
#include "share/ob_ddl_checksum.h"
#include "rootserver/ob_ddl_service_launcher.h" // for ObDDLServiceLauncher
#include "rootserver/ob_root_service.h"
#include "observer/ob_service.h"
#include "src/storage/tx_storage/ob_ls_map.h"
#include "share/ob_tablet_reorganize_history_table_operator.h"

using namespace oceanbase::rootserver;
using namespace oceanbase::share;

template <typename T>
ObCheckProgressKey<T>::ObCheckProgressKey(
    const T &field,
    const ObTabletID &tablet_id)
  : field_(field),
    tablet_id_(tablet_id)
{}

template <typename T>
uint64_t ObCheckProgressKey<T>::hash() const
{
  uint64_t hash_val = murmurhash(&field_, sizeof(field_), 0);
  hash_val = murmurhash(&tablet_id_, sizeof(tablet_id_), hash_val);
  return hash_val;
}

template <typename T>
bool ObCheckProgressKey<T>::operator==(const ObCheckProgressKey &other) const
{
  return field_ == other.field_ &&
         tablet_id_ == other.tablet_id_;
}


template <typename T>
int ObCheckProgressKey<T>::assign(const ObCheckProgressKey &other)
{
  int ret = OB_SUCCESS;
  if (!other.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(other));
  } else {
    field_ = other.field_;
    tablet_id_ = other.tablet_id_;
  }
  return ret;
}

ObPartitionSplitTask::ObPartitionSplitTask()
  : ObDDLTask(DDL_INVALID),
    root_service_(nullptr),
    partition_split_arg_(),
    has_synced_stats_info_(false),
    replica_build_task_submit_(false),
    replica_build_request_time_(0),
    replica_build_ret_code_(OB_SUCCESS),
    all_src_tablet_ids_(),
    data_tablet_compaction_scn_(0),
    index_tablet_compaction_scns_(),
    lob_tablet_compaction_scns_(),
    tablet_compaction_scn_map_(),
    freeze_progress_map_(),
    compaction_progress_map_(),
    send_finish_map_(),
    freeze_progress_status_inited_(false),
    compact_progress_status_inited_(false),
    write_split_log_status_inited_(false),
    replica_builder_(),
    wait_trans_ctx_(),
    tablet_size_(0),
    data_tablet_parallel_rowkey_list_(),
    index_tablet_parallel_rowkey_list_(),
    min_split_start_scn_(),
    split_start_delayed_(false)
{
  ObMemAttr attr("RSSplitRange", ObCtxIds::DEFAULT_CTX_ID);
  data_tablet_parallel_rowkey_list_.set_attr(attr);
  index_tablet_parallel_rowkey_list_.set_attr(attr);
}

ObPartitionSplitTask::~ObPartitionSplitTask()
{
  data_tablet_parallel_rowkey_list_.reset();
  index_tablet_parallel_rowkey_list_.reset();
}

int ObPartitionSplitTask::setup_src_tablet_ids_array()
{
  int ret = OB_SUCCESS;
  if (all_src_tablet_ids_.empty()) {
    const ObTabletID &data_tablet_id = partition_split_arg_.src_tablet_id_;
    const ObSArray<ObTabletID> &index_tablet_ids = partition_split_arg_.src_local_index_tablet_ids_;
    const ObSArray<ObTabletID> &lob_tablet_ids = partition_split_arg_.src_lob_tablet_ids_;
    if (OB_FAIL(all_src_tablet_ids_.push_back(data_tablet_id))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < index_tablet_ids.count(); ++i) {
      if (OB_FAIL(all_src_tablet_ids_.push_back(index_tablet_ids.at(i)))) {
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < lob_tablet_ids.count(); ++i) {
      if (OB_FAIL(all_src_tablet_ids_.push_back(lob_tablet_ids.at(i)))) {
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (all_src_tablet_ids_.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to set up all src tablet ids", K(ret));
  }
  return ret;
}

int ObPartitionSplitTask::init_freeze_progress_map()
{
  int ret = OB_SUCCESS;
  if (!freeze_progress_status_inited_) {
    if (all_src_tablet_ids_.empty()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("all src tablet ids array is empty", K(ret));
    } else if (!freeze_progress_map_.created() &&
               OB_FAIL(freeze_progress_map_.create(all_src_tablet_ids_.count(),
                                                   lib::ObLabel("DDLSplitTask")))) {
      LOG_WARN("failed to create freeze progress map", K(ret));
    }
    TCWLockGuard guard(lock_);
    for (int64_t i = 0; OB_SUCC(ret) && i < all_src_tablet_ids_.count(); ++i) {
      const ObTabletID &tablet_id = all_src_tablet_ids_.at(i);
      if (OB_FAIL(freeze_progress_map_.set_refactored(
              tablet_id, ObCheckProgressStatus::NOT_STARTED, true /*override*/))) {
      }
    }
    if (OB_FAIL(ret)) {
      (void)freeze_progress_map_.destroy();
    }
  }
  return ret;
}

int ObPartitionSplitTask::init_send_finish_map()
{
  int ret = OB_SUCCESS;
  if (!write_split_log_status_inited_) {
    if (all_src_tablet_ids_.empty()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("all src tablet ids array is empty", K(ret));
    } else if (!send_finish_map_.created() &&
               OB_FAIL(send_finish_map_.create(all_src_tablet_ids_.count(),
                                               lib::ObLabel("DDLSplitTask")))) {
      LOG_WARN("failed to create send finish map", K(ret));
    }
    TCWLockGuard guard(lock_);
    for (int64_t i = 0; OB_SUCC(ret) && i < all_src_tablet_ids_.count(); ++i) {
      const ObTabletID &tablet_id = all_src_tablet_ids_.at(i);
      if (OB_FAIL(send_finish_map_.set_refactored(
              tablet_id, ObCheckProgressStatus::NOT_STARTED))) {
      }
    }
  }
  if (OB_FAIL(ret)) {
    (void)send_finish_map_.destroy();
  }
  return ret;
}

int ObPartitionSplitTask::init_compaction_scn_map()
{
  int ret = OB_SUCCESS;
  if (!tablet_compaction_scn_map_.created() &&
      OB_FAIL(tablet_compaction_scn_map_.create(10, lib::ObLabel("DDLSplitTask")))) {
    LOG_WARN("failed to create tablet compaction scn map", K(ret));
  }
  if (OB_FAIL(ret)) {
    (void)tablet_compaction_scn_map_.destroy();
  }
  return ret;
}

// restore from deserialization
int ObPartitionSplitTask::restore_compaction_scn_map()
{
  int ret = OB_SUCCESS;
  const ObTabletID &data_tablet_id = partition_split_arg_.src_tablet_id_;
  const ObSArray<ObTabletID> &index_tablet_ids = partition_split_arg_.src_local_index_tablet_ids_;
  const ObSArray<ObTabletID> &lob_tablet_ids = partition_split_arg_.src_lob_tablet_ids_;
  if (data_tablet_compaction_scn_ == 0 &&
      index_tablet_compaction_scns_.empty() &&
      lob_tablet_compaction_scns_.empty()) {
    // do nothing
  } else if (index_tablet_ids.count() != index_tablet_compaction_scns_.count() ||
             lob_tablet_ids.count() != lob_tablet_compaction_scns_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet ids' count != tablet compaction scns' count", K(ret),
        K(index_tablet_ids.count()), K(index_tablet_compaction_scns_.count()),
        K(lob_tablet_ids.count()), K(lob_tablet_compaction_scns_.count()));
  } else {
    if (data_tablet_compaction_scn_ != 0) {
      if (OB_FAIL(tablet_compaction_scn_map_.set_refactored(
              data_tablet_id, data_tablet_compaction_scn_))) {
        }
    }
    for (int64_t i = 0; OB_SUCC(ret) &&
        i < index_tablet_compaction_scns_.count(); ++i) {
      const ObTabletID &tablet_id = index_tablet_ids.at(i);
      int64_t compaction_scn = index_tablet_compaction_scns_.at(i);
      if (compaction_scn != 0) {
        if (OB_FAIL(tablet_compaction_scn_map_.set_refactored(
                tablet_id, compaction_scn))) {
        }
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < lob_tablet_compaction_scns_.count(); ++i) {
      const ObTabletID &tablet_id = lob_tablet_ids.at(i);
      int64_t compaction_scn = lob_tablet_compaction_scns_.at(i);
      if (compaction_scn != 0) {
        if (OB_FAIL(tablet_compaction_scn_map_.set_refactored(
                tablet_id, compaction_scn))) {
        }
      }
    }
  }
  return ret;
}

int ObPartitionSplitTask::serialize_compaction_scn_to_task_record()
{
  int ret = OB_SUCCESS;
  char *buf = nullptr;
  int64_t pos = 0;
  ObString msg;
  ObMySQLTransaction trans;
  const int64_t serialize_param_size = get_serialize_param_size();
  ObArenaAllocator allocator(lib::ObLabel("DDLTask"));
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
  } else if (OB_ISNULL(buf =
        static_cast<char *>(allocator.alloc(serialize_param_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", K(ret), K(serialize_param_size));
  } else if (OB_FAIL(serialize_params_to_message(buf, serialize_param_size, pos))) {
  } else if (FALSE_IT(msg.assign(buf, serialize_param_size))) {
  } else if (OB_FAIL(trans.start(GCTX.sql_proxy_))) {
  } else if (OB_FAIL(ObDDLTaskRecordOperator::update_message(trans, task_id_, msg))) {
  }
  bool commit = (OB_SUCCESS == ret);
  int tmp_ret = trans.end(commit);
  if (OB_SUCCESS != tmp_ret) {
    ret = (OB_SUCCESS == ret) ? tmp_ret : ret;
  }
  return ret;
}

int ObPartitionSplitTask::init_tablet_compaction_scn_array()
{
  int ret = OB_SUCCESS;
  const ObSArray<ObTabletID> &index_tablet_ids = partition_split_arg_.src_local_index_tablet_ids_;
  const ObSArray<ObTabletID> &lob_tablet_ids = partition_split_arg_.src_lob_tablet_ids_;
  if (index_tablet_compaction_scns_.empty()) {
    for (int64_t i = 0; OB_SUCC(ret) && i < index_tablet_ids.count(); ++i) {
      if (OB_FAIL(index_tablet_compaction_scns_.push_back(0))) {
      }
    }
  }
  if (lob_tablet_compaction_scns_.empty()) {
    for (int64_t i = 0; OB_SUCC(ret) && i < lob_tablet_ids.count(); ++i) {
      if (OB_FAIL(lob_tablet_compaction_scns_.push_back(0))) {
      }
    }
  }
  return ret;
}

// update compaction scn array for serialization
int ObPartitionSplitTask::update_tablet_compaction_scn_array()
{
  int ret = OB_SUCCESS;
  const ObTabletID &data_tablet_id = partition_split_arg_.src_tablet_id_;
  const ObSArray<ObTabletID> &index_tablet_ids = partition_split_arg_.src_local_index_tablet_ids_;
  const ObSArray<ObTabletID> &lob_tablet_ids = partition_split_arg_.src_lob_tablet_ids_;
  if (index_tablet_ids.count() != index_tablet_compaction_scns_.count() ||
      lob_tablet_ids.count() != lob_tablet_compaction_scns_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet ids count != tablet compaction scns count", K(ret),
        K(index_tablet_ids.count()), K(index_tablet_compaction_scns_.count()),
        K(lob_tablet_ids.count()), K(lob_tablet_compaction_scns_.count()));
  } else {
    if (OB_FAIL(tablet_compaction_scn_map_.get_refactored(
            data_tablet_id, data_tablet_compaction_scn_))) {
      if (OB_HASH_NOT_EXIST != ret) {
        LOG_WARN("failed to get compaction scn from tablet compaction scn map",
            K(ret), K(data_tablet_id));
      } 
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < index_tablet_ids.count(); ++i) {
      const ObTabletID tablet_id = index_tablet_ids.at(i);
      int64_t compaction_scn = 0l;
      if (OB_FAIL(tablet_compaction_scn_map_.get_refactored(
              tablet_id, compaction_scn))) {
        if (OB_HASH_NOT_EXIST != ret) {
          LOG_WARN("failed to get compaction scn from tablet compaction scn map",
              K(ret), K(tablet_id));
        } 
      } else { // update compaction scn
        index_tablet_compaction_scns_[i] = compaction_scn;
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < lob_tablet_ids.count(); ++i) {
      const ObTabletID tablet_id = lob_tablet_ids.at(i);
      int64_t compaction_scn = 0l;
      if (OB_FAIL(tablet_compaction_scn_map_.get_refactored(
              tablet_id, compaction_scn))) {
        if (OB_HASH_NOT_EXIST != ret) {
          LOG_WARN("failed to get compaction scn from tablet compaction scn map",
              K(ret), K(tablet_id));
        } 
      } else { // update compaction scn
        lob_tablet_compaction_scns_[i] = compaction_scn;
      }
    }
  }
  return ret;
}

int ObPartitionSplitTask::init(
    const int64_t task_id,
    const int64_t table_id,
    const int64_t schema_version,
    const int64_t parallelism,
    const obcall::ObPartitionSplitArg &partition_split_arg,
    const int64_t tablet_size,
    const uint64_t tenant_data_version,
    const int64_t parent_task_id,   /* = 0 */
    const int64_t task_status)     /* = TaskStatus::PREPARE */
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_ISNULL(root_service_ = GCTX.root_service_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("root service is null", K(ret), KP(root_service_));
  } else if (!ObDDLServiceLauncher::is_ddl_service_started()) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("ddl service not started", KR(ret));
  } else if (OB_UNLIKELY(!(schema_version > 0 &&
                           OB_INVALID_ID != table_id &&
                           (task_status >= ObDDLTaskStatus::PREPARE &&
                            task_status <= ObDDLTaskStatus::SUCCESS) &&
                           task_id > 0))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(schema_version), K(table_id),
        K(task_status), K(task_id));
  } else if (OB_UNLIKELY(
        (partition_split_arg.local_index_table_ids_.count() !=
         partition_split_arg.src_local_index_tablet_ids_.count()) &&
        (partition_split_arg.lob_table_ids_.count() !=
         partition_split_arg.src_lob_tablet_ids_.count()))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(partition_split_arg),
        K(task_status), K(task_id));
  } else if (OB_FAIL(deep_copy_table_arg(
          allocator_, partition_split_arg, partition_split_arg_))) {
  } else {
    set_gmt_create(ObTimeUtility::current_time());
    start_time_ = ObTimeUtility::current_time();
    
    
    object_id_ = table_id;
    target_object_id_ = partition_split_arg_.src_tablet_id_.id();
    schema_version_ = schema_version;
    dst_schema_version_ = schema_version_;
    parallelism_ = parallelism;
    task_type_ = partition_split_arg_.task_type_;
    task_id_ = task_id;
    tablet_size_ = tablet_size;
    parent_task_id_ = parent_task_id;
    task_version_ = OB_PARTITION_SPLIT_TASK_VERSION;
    task_status_ = static_cast<ObDDLTaskStatus>(task_status);
    execution_id_ = 1L; 
    data_format_version_ = tenant_data_version;
    split_start_delayed_ = false;
    if (OB_FAIL(init_ddl_task_monitor_info(table_id))) {
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

int ObPartitionSplitTask::init(const ObDDLTaskRecord &task_record)
{
  int ret = OB_SUCCESS;
  const uint64_t table_id = task_record.object_id_;
  const int64_t schema_version = task_record.schema_version_;
  int64_t pos = 0;
  if (!task_record.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(task_record));
  } else if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_ISNULL(root_service_ = GCTX.root_service_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("root service is null", K(ret), KP(root_service_));
  } else if (!ObDDLServiceLauncher::is_ddl_service_started()) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("ddl service not started", KR(ret));
  } else if (OB_FAIL(deserialize_params_from_message(
          task_record.message_.ptr(),
          task_record.message_.length(),
          pos))) {
  } else {
  
    
    object_id_ = table_id;
    target_object_id_ = partition_split_arg_.src_tablet_id_.id();
    schema_version_ = schema_version;
    dst_schema_version_ = schema_version_;
    task_type_ = task_record.ddl_type_;
    execution_id_ = task_record.execution_id_;
    task_id_ = task_record.task_id_;
    parent_task_id_ = task_record.parent_task_id_;
    task_status_ = static_cast<ObDDLTaskStatus>(task_record.task_status_);
    start_time_ = ObTimeUtility::current_time();
    split_start_delayed_ = false;
    if (OB_FAIL(init_ddl_task_monitor_info(object_id_))) {
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

int ObPartitionSplitTask::process()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObPartitionSplitTask has not been inited", K(ret));
  } else if (OB_FAIL(check_health())) {
  } else if (!need_retry()) {
    // bypass
  } else {
    const ObDDLTaskStatus status = static_cast<ObDDLTaskStatus>(task_status_);
    switch (status) {
    case ObDDLTaskStatus::PREPARE: {
      DEBUG_SYNC(PARTITION_SPLIT_PREPARE);
      if (OB_FAIL(prepare(WAIT_FROZE_END))) {
      }
      break;
    }
    case ObDDLTaskStatus::WAIT_FROZE_END: {
      if (OB_FAIL(wait_freeze_end(WAIT_COMPACTION_END))) {
      }
      break;
    }
    case ObDDLTaskStatus::WAIT_COMPACTION_END: {
      if (OB_FAIL(wait_compaction_end(WRITE_SPLIT_START_LOG))) {
      }
      break;
    }
    case ObDDLTaskStatus::WRITE_SPLIT_START_LOG: {
      if (OB_FAIL(write_split_start_log(WAIT_DATA_TABLE_SPLIT_END))) {
      }
      break;
    }
    case ObDDLTaskStatus::WAIT_DATA_TABLE_SPLIT_END: {
      if (OB_FAIL(wait_data_tablet_split_end(WAIT_LOCAL_INDEX_SPLIT_END))) {
      }
      break;
    }
    case ObDDLTaskStatus::WAIT_LOCAL_INDEX_SPLIT_END: {
      if (OB_FAIL(wait_local_index_tablet_split_end(WAIT_LOB_TABLE_SPLIT_END))) {
      }
      break;
    }
    case ObDDLTaskStatus::WAIT_LOB_TABLE_SPLIT_END: {
      if (OB_FAIL(wait_lob_tablet_split_end(WAIT_TRANS_END))) {
      }
      break;
    }
    case ObDDLTaskStatus::WAIT_TRANS_END: {
      if (OB_FAIL(wait_trans_end(TAKE_EFFECT))) {
      }
      break;
    }
    case ObDDLTaskStatus::TAKE_EFFECT: {
      if (OB_FAIL(take_effect(SUCCESS))) {
      }
      break;
    }
    case ObDDLTaskStatus::SUCCESS: {
      if (OB_FAIL(succ())) {
      }
      break;
    }
    case ObDDLTaskStatus::WAIT_PARTITION_SPLIT_RECOVERY_TASK_FINISH: {
      if (OB_FAIL(wait_recovery_task_finish(SUCCESS))) {
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("not expected status", K(ret), K(status), K(*this));
      break;
    }
    } // end switch
  }
  return ret;
}

int ObPartitionSplitTask::prepare(const share::ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  bool state_finished = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (ObDDLTaskStatus::PREPARE != task_status_) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("task status not match", K(ret), K(task_status_));
  } else {
    state_finished = true;
  }

  if (state_finished) {
    if (OB_FAIL(switch_status(next_task_status, true, ret))) {
    }
    LOG_INFO("prepare finished", K(ret), K(*this));
  }
  return ret;
}

int ObPartitionSplitTask::wait_freeze_end(
    const share::ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  bool freeze_finished = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (ObDDLTaskStatus::WAIT_FROZE_END != task_status_) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("task status not match", K(ret), K(task_status_));
  } else if (all_src_tablet_ids_.empty() && OB_FAIL(setup_src_tablet_ids_array())) {
    LOG_WARN("failed to setup all src tablet ids array", K(ret));
  } else if (OB_FAIL(init_freeze_progress_map())) {
  } else { // check tablet freeze progress
    if (OB_FAIL(check_freeze_progress(all_src_tablet_ids_,
                                      freeze_finished))) {
    } else {
      TCWLockGuard guard(lock_);
      freeze_progress_status_inited_ = true;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (freeze_finished) {
    DEBUG_SYNC(PARTITION_SPLIT_WAIT_FREEZE_END);
    if (OB_FAIL(switch_status(next_task_status, true, ret))) {
    }
    LOG_INFO("wait freeze end finished", K(ret), K(*this));
  }
  return ret;
}

// check freeze progess of all replicas of this tablet
int ObPartitionSplitTask::check_freeze_progress(
    const ObSArray<ObTabletID> &tablet_ids,
    bool &is_end)
{
  int ret = OB_SUCCESS;
  is_end = false;
  int64_t num_tablet_finished = 0;
  ObLocationService *location_service = nullptr;
  ObArray<ObTabletID> request_tablet_ids;
  const int64_t rpc_timeout = max(GCONF.rpc_timeout, static_cast<int64_t>(1000L * 1000L * 9L));
  for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
    const ObTabletID &tablet_id = tablet_ids.at(i);
    share::ObLSID ls_id;
    ObAddr leader_addr;
    if (OB_ISNULL(location_service = GCTX.location_service_)) {
      ret = OB_ERR_SYS;
      LOG_WARN("location_cache is null", K(ret));
    } else if (OB_FAIL(ObDDLUtil::get_tablet_leader_addr(location_service, tablet_id, rpc_timeout, ls_id, leader_addr))) {
    } else {
      ObCheckProgressStatus freeze_status;
      if (OB_FAIL(freeze_progress_map_.get_refactored(
              tablet_id, freeze_status))) {
      } else if (freeze_status == ObCheckProgressStatus::NOT_STARTED ||
          freeze_status == ObCheckProgressStatus::ONGOING) {
        // Direct call (seekdb: all tablets are local).
        UNUSED(leader_addr);
        obcall::ObCheckMemtableCntArg arg;
        obcall::ObCheckMemtableCntResult cur_result;
        
        
        arg.ls_id_ = ls_id;
        arg.tablet_id_ = tablet_id;
        int call_ret = GCTX.ob_service_->check_memtable_cnt(arg, cur_result);
        UNUSED(call_ret);
        {
          TCWLockGuard guard(lock_);
          if (OB_FAIL(freeze_progress_map_.set_refactored(tablet_id,
                  ObCheckProgressStatus::ONGOING, true/*overwrite*/))) {
          } else if (OB_FAIL(request_tablet_ids.push_back(tablet_id))) {
          } else if (cur_result.memtable_cnt_ == 0) {
            if (OB_FAIL(freeze_progress_map_.set_refactored(tablet_id,
                    ObCheckProgressStatus::DONE, true/*overwrite*/))) {
            } else {
              ++num_tablet_finished;
            }
          }
        }
      } else if (freeze_status == ObCheckProgressStatus::DONE) {
        ++num_tablet_finished;
      }
    }
  }
  is_end = num_tablet_finished == tablet_ids.count() ? true : false;
  return ret;
}

int ObPartitionSplitTask::wait_compaction_end(
    const share::ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  int64_t num_tablet_finished = 0;
  // TODO hanxuan grab mem lock
  ObLSID ls_id;
  ObAddr unused_addr;
  ObArray<ObAddr> split_replica_addrs;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (ObDDLTaskStatus::WAIT_COMPACTION_END != task_status_) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("task status not match", K(ret), K(task_status_));
  } else if (all_src_tablet_ids_.empty() && OB_FAIL(setup_src_tablet_ids_array())) {
    LOG_WARN("failed to setup all src tablet ids array", K(ret));
  } else if (OB_FAIL(init_tablet_compaction_scn_array())) {
  } else if (OB_FAIL(init_compaction_scn_map())) {
  } else { // check tablet compaction progress
    const ObTabletID &any_tablet_id = all_src_tablet_ids_.at(0);
    const int64_t rpc_timeout = ObDDLUtil::get_default_ddl_rpc_timeout();
    if (OB_FAIL(ObDDLUtil::get_tablet_leader_addr(
          GCTX.location_service_, any_tablet_id, rpc_timeout, ls_id, unused_addr))) {
    } else if (OB_FAIL(ObDDLUtil::get_split_replicas_addrs(ls_id, split_replica_addrs))) {
    } 
    for (int64_t i = 0; OB_SUCC(ret) && i < all_src_tablet_ids_.count(); ++i) {
      const ObTabletID &tablet_id = all_src_tablet_ids_.at(i);
      bool current_tablet_finished = false;
      if (OB_FAIL(check_compaction_progress(ls_id, tablet_id, split_replica_addrs, current_tablet_finished))) {
      } else if (current_tablet_finished) {
        ++num_tablet_finished;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(update_tablet_compaction_scn_array())) {
    } else {
      TCWLockGuard guard(lock_);
      compact_progress_status_inited_ = true;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (num_tablet_finished == all_src_tablet_ids_.count()) {
    DEBUG_SYNC(PARTITION_SPLIT_WAIT_COMPACTION_END);
    ObArray<ObAddr> double_check_replica_addrs;
    ObArray<ObAddr> different_replica_addrs;
    // double check.
    if (OB_FAIL(ObDDLUtil::get_split_replicas_addrs(ls_id, double_check_replica_addrs))) {
    } else if (double_check_replica_addrs.count() != split_replica_addrs.count()) {
      ret = OB_EAGAIN;
      LOG_INFO("need retry when double check failed", K(ret), K(split_replica_addrs), K(double_check_replica_addrs));
    } else if OB_FAIL(get_difference(split_replica_addrs, double_check_replica_addrs, different_replica_addrs)) {
    } else if (!different_replica_addrs.empty()) {
      ret = OB_EAGAIN;
      LOG_INFO("different replica addrs occur, need retry", K(ret), K(split_replica_addrs), K(double_check_replica_addrs));
    } else if (OB_FAIL(serialize_compaction_scn_to_task_record())) {
    } else if (OB_FAIL(switch_status(next_task_status, true, ret))) {
    }
    LOG_INFO("wait compaction end finished", K(ret), K(*this));
  }
  return ret;
}

int ObPartitionSplitTask::check_compaction_progress(
    const ObLSID &ls_id,
    const ObTabletID &tablet_id,
    const ObIArray<ObAddr> &split_replica_addrs,
    bool& is_end)
{
  int ret = OB_SUCCESS;
  is_end = false;
  const int64_t rpc_timeout = ObDDLUtil::get_default_ddl_rpc_timeout();
  if (OB_UNLIKELY(!ls_id.is_valid() || !tablet_id.is_valid() || split_replica_addrs.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(ls_id), K(tablet_id), K(split_replica_addrs));
  } else if (OB_UNLIKELY(all_src_tablet_ids_.empty())) {
    ret = OB_ERR_SYS;
    LOG_WARN("unexpected null tablet ids", K(ret), KPC(this));
  } else if (!compaction_progress_map_.created() && OB_FAIL(compaction_progress_map_.create(all_src_tablet_ids_.count(), lib::ObLabel("DDLWaitCompact")))) {
    LOG_WARN("failed to create compaction progress map", K(ret));
  } else {
    int64_t compaction_end_number = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < split_replica_addrs.count(); i++) {
      ObCheckProgressStatus compaction_status;
      const common::ObAddr &replica_addr = split_replica_addrs.at(i);
      const ObCheckProgressKey<common::ObAddr> check_progress_key(replica_addr, tablet_id);
      if (OB_FAIL(compaction_progress_map_.get_refactored(check_progress_key, compaction_status))) {
        if (OB_HASH_NOT_EXIST == ret) {
          // override ret.
          TCWLockGuard guard(lock_);
          if (OB_FAIL(compaction_progress_map_.set_refactored(check_progress_key, ObCheckProgressStatus::NOT_STARTED))) {
          } else {
            compaction_status = ObCheckProgressStatus::NOT_STARTED;
          }
        } else {
          LOG_WARN("failed to get from map", K(ret));
        }
      }

      if (OB_FAIL(ret)) {
      } else if (ObCheckProgressStatus::DONE != compaction_status) {
        // check compaction status
        obcall::ObCheckMediumCompactionInfoListArg arg;
        
        arg.ls_id_ = ls_id;
        arg.tablet_id_ = tablet_id;
        // Direct call (seekdb: all replicas are local).
        UNUSED(replica_addr);
        obcall::ObCheckMediumCompactionInfoListResult result;
        int call_ret = GCTX.ob_service_->check_medium_compaction_info_list_cnt(arg, result);
        if (OB_FAIL(call_ret)) {
        } else {
          TCWLockGuard guard(lock_);
          if (result.info_list_cnt_ > 0) {
            if (OB_FAIL(compaction_progress_map_.set_refactored(check_progress_key, ObCheckProgressStatus::ONGOING, true/*overwrite*/))) {
            }
          } else if (result.info_list_cnt_ == 0) {
            int64_t existed_compaction_scn;
            if (OB_FAIL(tablet_compaction_scn_map_.get_refactored(tablet_id, existed_compaction_scn))) {
              if (OB_HASH_NOT_EXIST == ret) {
                if (OB_FAIL(tablet_compaction_scn_map_.set_refactored(tablet_id, result.primary_compaction_scn_))) {
                }
              } else {
                LOG_WARN("get compaction scn from map failed", K(ret), K(tablet_id));
              }
            } else if (OB_UNLIKELY(existed_compaction_scn != result.primary_compaction_scn_)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unequal compaction scn between replicas", K(ret), K(tablet_id), K(replica_addr), K(existed_compaction_scn), K(result.primary_compaction_scn_));
            } else if (OB_FAIL(compaction_progress_map_.set_refactored(check_progress_key, ObCheckProgressStatus::DONE, true/*overwrite*/))) {
            } else {
              compaction_end_number++;
            }
          }
        }
      } else {
        compaction_end_number++; // compact done.
      }
    }
    is_end = OB_SUCC(ret) && (compaction_end_number == split_replica_addrs.count()) ? true : false;
  }
  return ret;
}

int ObPartitionSplitTask::write_split_start_log(const share::ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObSEArray<blocksstable::ObDatumRowkey, 8>, 8> unused_rowkey_list;
  const int64_t delay_sec = std::abs(OB_E(EventTable::EN_WRITE_SPLIT_START_LOG_DELAY_SEC) 0);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!split_start_delayed_ && delay_sec != 0) {
    set_delay_schedule_time(delay_sec * 1000000L);
    split_start_delayed_ = true;
    ret = OB_EAGAIN;
    LOG_WARN("simulate delay before write split start log", K(ret), K(task_id_));
  } else if (OB_FAIL(prepare_tablet_split_ranges(unused_rowkey_list))) {
  } else if (all_src_tablet_ids_.empty() && OB_FAIL(setup_src_tablet_ids_array())) {
    LOG_WARN("failed to setup all src tablet ids array", K(ret));
  } else if (OB_FAIL(init_send_finish_map())) {
  } else if (OB_FAIL(send_split_rpc(true/*is_split_start*/))) {
  } else {
    {
      TCWLockGuard guard(lock_);
      write_split_log_status_inited_ = true;
    }
    ObArray<ObTabletID> not_finished_tablets;
    hash::ObHashMap<ObTabletID, ObCheckProgressStatus>::const_iterator iter = send_finish_map_.begin();
    for (; OB_SUCC(ret) && iter != send_finish_map_.end(); iter++) {
      const ObTabletID &key = iter->first;
      const ObCheckProgressStatus &status = iter->second;
      if (ObCheckProgressStatus::DONE != status && OB_FAIL(not_finished_tablets.push_back(key))) {
        LOG_WARN("failed to push back", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      if (not_finished_tablets.empty()) {
        if (OB_FAIL(switch_status(next_task_status, true/*enable_flt_tracing*/, ret))) {
        }
      } else {
        LOG_INFO("dump not finished tablets", K(ret), K(not_finished_tablets));
      }
    }
  }
  return ret;
}

int ObPartitionSplitTask::wait_data_tablet_split_end(
    const share::ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  bool data_split_finished = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (ObDDLTaskStatus::WAIT_DATA_TABLE_SPLIT_END != task_status_) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("task status not match", K(ret), K(task_status_));
  } else if (!replica_build_task_submit_) {
    if (OB_FAIL(send_split_request(
          ObPartitionSplitReplicaType::DATA_TABLET_REPLICA))) {
    }
  } else {
    if (OB_FAIL(check_split_finished(data_split_finished))) {
    }
  }
  
  if (OB_FAIL(ret)) {
  } else if (data_split_finished) {
    DEBUG_SYNC(PARTITION_SPLIT_WAIT_DATA_TABLET_SPLIT_END);
    ret = replica_build_ret_code_;
    reset_replica_build_stat();
    if (OB_FAIL(switch_status(next_task_status, true, ret))) {
    }
    LOG_INFO("wait data tablet split end finished", K(ret), KPC(this));
  }
  return ret;
}

int ObPartitionSplitTask::send_split_request(
    const ObPartitionSplitReplicaType replica_type)
{
  int ret = OB_SUCCESS;
  SMART_VAR(ObDDLReplicaBuildExecutorParam, param) {
    ObSchemaGetterGuard schema_guard;
    
    
    param.ddl_type_ = task_type_;
    param.snapshot_version_ = 1L;
    param.task_id_ = task_id_;
    param.parallelism_ = parallelism_;
    param.execution_id_ = execution_id_;
    param.data_format_version_ = data_format_version_;
    param.consumer_group_id_ = partition_split_arg_.consumer_group_id_;
    param.min_split_start_scn_ = min_split_start_scn_;
    if (OB_ISNULL(root_service_)) {
      ret = OB_ERR_SYS;
      LOG_WARN("error sys", K(ret));
    } else if (OB_FAIL(root_service_->get_ddl_service().
        get_tenant_schema_guard_with_version_in_inner_table(schema_guard))) {
    } else if (replica_type == ObPartitionSplitReplicaType::DATA_TABLET_REPLICA) {
      if (OB_FAIL(param.source_table_ids_.push_back(object_id_))) {
      } else if (OB_FAIL(param.dest_table_ids_.push_back(object_id_))) {
      } else if (OB_FAIL(param.source_schema_versions_.push_back(schema_version_))) {
      } else if (OB_FAIL(param.dest_schema_versions_.push_back(schema_version_))) {
      } else if (OB_FAIL(param.source_tablet_ids_.push_back(
              partition_split_arg_.src_tablet_id_))) {
      } else if (OB_FAIL(param.dest_tablet_ids_.push_back(
          ObTabletID(ObTabletID::MIN_VALID_TABLET_ID)/*to make sure valid only*/))) {
      } else if (OB_FAIL(param.compaction_scns_.push_back(
              data_tablet_compaction_scn_))) {
      }
    } else if (replica_type == ObPartitionSplitReplicaType::LOCAL_INDEX_TABLET_REPLICA) {
      ObSArray<uint64_t> data_table_schema_versions;
      for (int64_t i = 0; OB_SUCC(ret) &&
          i < partition_split_arg_.local_index_table_ids_.count(); ++i) {
        if (OB_FAIL(data_table_schema_versions.push_back(schema_version_))) {
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(param.source_table_ids_.assign(
              partition_split_arg_.local_index_table_ids_))) {
      } else if (OB_FAIL(param.dest_table_ids_.assign(
              partition_split_arg_.local_index_table_ids_))) {
      } else if (OB_FAIL(param.source_schema_versions_.assign(
              data_table_schema_versions))) {
      } else if (OB_FAIL(param.dest_schema_versions_.assign(
              data_table_schema_versions))) {
      } else if (OB_FAIL(param.source_tablet_ids_.assign(
              partition_split_arg_.src_local_index_tablet_ids_))) {
      } else if (OB_FAIL(param.dest_tablet_ids_.assign(
              partition_split_arg_.src_local_index_tablet_ids_/*to make sure valid only*/))) {
      } else if (OB_FAIL(param.compaction_scns_.assign(
              index_tablet_compaction_scns_))) {
      }
    } else if (replica_type == ObPartitionSplitReplicaType::LOB_TABLET_REPLICA) {
      ObSArray<uint64_t> data_table_ids;
      ObSArray<uint64_t> data_table_schema_versions;
      for (int64_t i = 0; OB_SUCC(ret) &&
          i < partition_split_arg_.lob_table_ids_.count(); ++i) {
        if (OB_FAIL(data_table_ids.push_back(object_id_))) {
        } else if (OB_FAIL(data_table_schema_versions.push_back(schema_version_))) {
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(param.source_table_ids_.assign(data_table_ids))) {
      } else if (OB_FAIL(param.dest_table_ids_.assign(
              partition_split_arg_.lob_table_ids_))) {
      } else if (OB_FAIL(param.source_schema_versions_.assign(
              data_table_schema_versions))) {
      } else if (OB_FAIL(param.dest_schema_versions_.assign(
              partition_split_arg_.lob_schema_versions_))) {
      } else if (OB_FAIL(param.source_tablet_ids_.assign(
              partition_split_arg_.src_lob_tablet_ids_))) {
      } else if (OB_FAIL(param.dest_tablet_ids_.assign(
              partition_split_arg_.src_lob_tablet_ids_/*to make sure valid only*/))) {
      } else if (OB_FAIL(param.compaction_scns_.assign(
              lob_tablet_compaction_scns_))) {
      } else if (OB_FAIL(setup_lob_idxs_arr(param.lob_col_idxs_))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(prepare_tablet_split_ranges(param.parallel_datum_rowkey_list_))) {
    } else if (OB_FAIL(check_can_reuse_macro_block(schema_guard, param.source_table_ids_, param.can_reuse_macro_blocks_))) {
    } else if (OB_FAIL(replica_builder_.build(param))) {
    } else {
      LOG_INFO("start to build single replica", K(param));
      TCWLockGuard guard(lock_);
      replica_build_task_submit_ = true;
      replica_build_request_time_ = ObTimeUtility::current_time();
    }
  } // smart_var
  return ret;
}

int ObPartitionSplitTask::update_complete_sstable_job_status(
    const ObTabletID &tablet_id,
    const ObAddr &svr,
    const int64_t execution_id,
    const int ret_code,
    const ObDDLTaskInfo &addition_info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObPartitionSplitTask has not been inited", K(ret));
  } else if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id));
  } else if (!(ObDDLTaskStatus::WAIT_DATA_TABLE_SPLIT_END <= task_status_ 
            && ObDDLTaskStatus::WAIT_LOB_TABLE_SPLIT_END >= task_status_)) {
    // by pass, may be network delay
  } else if (execution_id < execution_id_) {
    LOG_INFO("receive a mismatch execution result, ignore", K(ret_code),
        K(execution_id), K(execution_id_));
  } else if (OB_FAIL(replica_builder_.update_build_progress(tablet_id, svr,
          ret_code, addition_info.row_scanned_, addition_info.row_inserted_, addition_info.physical_row_count_))) {
  }
  return ret;
}

int ObPartitionSplitTask::check_split_finished(bool &is_end)
{
  int ret = OB_SUCCESS;
  is_end = false;
  if (OB_FAIL(replica_builder_.check_build_end(false/*need checksum*/, is_end, replica_build_ret_code_))) {
  } else if (!is_end) {
    if (replica_build_request_time_ + OB_MAX_DDL_SINGLE_REPLICA_BUILD_TIMEOUT <
        ObTimeUtility::current_time()) {
      // timeout, retry
      TCWLockGuard guard(lock_);
      replica_build_task_submit_ = false;
      replica_build_request_time_ = 0;
    }
  }
  return ret;
}

int ObPartitionSplitTask::wait_local_index_tablet_split_end(
    const share::ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  bool local_index_split_finished = false;
  const uint64_t data_table_id = object_id_;
  const ObSArray<ObTabletID> &index_tablet_ids = partition_split_arg_.src_local_index_tablet_ids_;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (ObDDLTaskStatus::WAIT_LOCAL_INDEX_SPLIT_END != task_status_) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("task status not match", K(ret), K(task_status_));
  } else if (index_tablet_compaction_scns_.count() != index_tablet_ids.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("compaction scn array count mismatch index tablet array",
        K(index_tablet_compaction_scns_.count()), K(index_tablet_ids.count()));
  } else if (index_tablet_ids.count() == 0) {
    local_index_split_finished = true;
  } else if (!replica_build_task_submit_) {
    if (OB_FAIL(send_split_request(
          ObPartitionSplitReplicaType::LOCAL_INDEX_TABLET_REPLICA))) {
    }
  } else {
    if (OB_FAIL(check_split_finished(local_index_split_finished))) {
    }
  }

  if (OB_FAIL(ret)) {
  } else if (local_index_split_finished) {
    DEBUG_SYNC(PARTITION_SPLIT_WAIT_LOCAL_INDEX_SPLIT_END);
    ret = replica_build_ret_code_;
    reset_replica_build_stat();
    /* if (OB_SUCC(ret) && OB_FAIL(check_local_index_checksum())) { */
    /*   LOG_WARN("checksum failed", K(ret), K(*this)); */
    /* } */
    if (OB_FAIL(switch_status(next_task_status, true, ret))) {
    }
    LOG_INFO("wait local index split end finished", K(ret), K(*this));
  }
  return ret;
}


int ObPartitionSplitTask::setup_lob_idxs_arr(ObSArray<uint64_t> &lob_col_idxs)
{
  int ret = OB_SUCCESS;
  lob_col_idxs.reuse();
  const uint64_t table_id = object_id_;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *table_schema = nullptr;
  ObArray<ObColDesc> all_column_ids;
  ObMultiVersionSchemaService &schema_service = ObMultiVersionSchemaService::get_instance();
  if (OB_FAIL(schema_service.get_tenant_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_table_schema( table_id,
          table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("error unexpected, table schema must not be nullptr", K(ret),
        K(table_id));
  } else if (OB_FAIL(table_schema->get_store_column_ids(all_column_ids))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < all_column_ids.count(); ++i) {
    if (all_column_ids.at(i).col_type_.is_lob_storage() &&
        OB_FAIL(lob_col_idxs.push_back(i))) {
      LOG_WARN("failed to push back lob idx", K(ret));
    }
  }
  return ret;
}

int ObPartitionSplitTask::wait_lob_tablet_split_end(
    const share::ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  bool is_lob_split_finished = false;
  const uint64_t data_table_id = object_id_;
  ObSArray<ObTabletID> &lob_tablet_ids = partition_split_arg_.src_lob_tablet_ids_;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (ObDDLTaskStatus::WAIT_LOB_TABLE_SPLIT_END != task_status_) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("task status not match", K(ret), K(task_status_));
  } else if (lob_tablet_compaction_scns_.count() != lob_tablet_ids.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lob tablet compaction scn array count mismatch lob tablet ids array",
        K(ret), K(lob_tablet_compaction_scns_.count()), K(lob_tablet_ids.count()));
  } else if (lob_tablet_ids.count() == 0) {
    is_lob_split_finished = true;
  } else if (!replica_build_task_submit_) {
    if (OB_FAIL(send_split_request(
            ObPartitionSplitReplicaType::LOB_TABLET_REPLICA))) {
    }
  } else {
    if (OB_FAIL(check_split_finished(is_lob_split_finished))) {
    }
  }

  if (OB_FAIL(ret)) {
  } else if (is_lob_split_finished) {
    DEBUG_SYNC(PARTITION_SPLIT_WAIT_LOB_TABLET_SPLIT_END);
    ret = replica_build_ret_code_;
    reset_replica_build_stat();
    if (OB_FAIL(switch_status(next_task_status, true, ret))) {
    }
    LOG_INFO("wait lob split end finished", K(ret), K(*this));
  }
  return ret;
}

int ObPartitionSplitTask::wait_trans_end(const share::ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  ObDDLTaskStatus new_status = task_status_;
  int64_t new_fetched_snapshot = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObConstraintTask has not been inited", K(ret));
  } else if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
  } else if (ObDDLTaskStatus::WAIT_TRANS_END != task_status_) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("task status not match", K(ret), K(task_status_));
  } else if (snapshot_version_ > 0) {
    new_status = next_task_status;
  }

  if (OB_SUCC(ret) && snapshot_version_ <= 0 && !wait_trans_ctx_.is_inited()) {
    if (OB_FAIL(wait_trans_ctx_.init(task_id_, task_status_, object_id_, all_src_tablet_ids_, ObDDLWaitTransEndCtx::WaitTransType::WAIT_SCHEMA_TRANS, schema_version_))) {
    }
  }

  if (OB_SUCC(ret) && snapshot_version_ <= 0) {
    bool is_trans_end = false;
    if (OB_FAIL(wait_trans_ctx_.try_wait(is_trans_end, new_fetched_snapshot))) {
    }
  }

  if (OB_SUCC(ret) && snapshot_version_ <= 0 && new_fetched_snapshot > 0) {
    int64_t persisted_snapshot = 0;
    if (OB_FAIL(ObDDLTaskRecordOperator::update_snapshot_version_if_not_exist(*GCTX.sql_proxy_,
                                                                 task_id_,
                                                                 new_fetched_snapshot,
                                                                 persisted_snapshot))) {
    } else {
      snapshot_version_ = persisted_snapshot > 0 ? persisted_snapshot : new_fetched_snapshot;
      new_status = next_task_status;
    }
  }
  DEBUG_SYNC(PARTITION_SPLIT_WAIT_TRANS_END);
  if (OB_FAIL(switch_status(new_status, true, ret))) {
  }
  return ret;
}

int ObPartitionSplitTask::take_effect(
    const share::ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (ObDDLTaskStatus::TAKE_EFFECT != task_status_) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("task status not match", K(ret), K(task_status_));
  } else if (all_src_tablet_ids_.empty() && OB_FAIL(setup_src_tablet_ids_array())) {
    LOG_WARN("failed to setup all src tablet ids array", K(ret));
  } else if (OB_FAIL(init_send_finish_map())) {
  } else if (OB_FAIL(send_split_rpc(false/*is_split_start*/))) {
  } else if (OB_FAIL(sync_stats_info())) {
  } else {
    DEBUG_SYNC(PARTITION_SPLIT_TAKE_EFFECT);
    if (OB_FAIL(switch_status(next_task_status, true, ret))) {
    }
    LOG_INFO("take effect finished", K(ret), K(*this));
  }
  return ret;
}

int ObPartitionSplitTask::get_src_tablet_ids(ObIArray<ObTabletID> &src_ids)
{
  int ret = OB_SUCCESS;
  src_ids.reset();
  if (OB_FAIL(src_ids.push_back(partition_split_arg_.src_tablet_id_))) {
  }
  return ret;
}

int ObPartitionSplitTask::get_dest_tablet_ids(ObIArray<ObTabletID> &dest_ids)
{
  int ret = OB_SUCCESS;
  dest_ids.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < partition_split_arg_.dest_tablet_ids_.count(); ++i) {
    const ObTabletID &tablet_id = partition_split_arg_.dest_tablet_ids_.at(i);
    if (OB_FAIL(dest_ids.push_back(tablet_id))) {
    }
  }
  return ret;
}

int ObPartitionSplitTask::get_all_dest_tablet_ids(
    const ObTabletID &source_tablet_id,
    ObArray<ObTabletID> &dest_ids)
{
  int ret = OB_SUCCESS;
  dest_ids.reset();
  if (source_tablet_id == partition_split_arg_.src_tablet_id_) {
    if (OB_FAIL(dest_ids.push_back(partition_split_arg_.dest_tablet_ids_))) {
    }
  }
  if (OB_SUCC(ret)) {
    const ObSArray<ObTabletID> &index_tablet_ids = partition_split_arg_.src_local_index_tablet_ids_;
    for (int64_t i = 0; OB_SUCC(ret) && dest_ids.empty() && i < index_tablet_ids.count(); ++i) {
      if (source_tablet_id == index_tablet_ids.at(i)) {
        if (OB_FAIL(dest_ids.push_back(partition_split_arg_.dest_local_index_tablet_ids_.at(i)))) {
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    const ObSArray<ObTabletID> &lob_tablet_ids = partition_split_arg_.src_lob_tablet_ids_;
    for (int64_t i = 0; OB_SUCC(ret) && dest_ids.empty() && i < lob_tablet_ids.count(); ++i) {
      if (source_tablet_id == lob_tablet_ids.at(i)) {
        if (OB_FAIL(dest_ids.push_back(partition_split_arg_.dest_lob_tablet_ids_.at(i)))) {
        }
      }
    }
  }

  if (OB_SUCC(ret) && OB_UNLIKELY(dest_ids.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err, can not find dest tablet ids", K(ret), K(source_tablet_id), K(partition_split_arg_));
  }
  return ret;
}

int ObPartitionSplitTask::setup_split_finish_items(
    ObAddr &leader_addr,
    ObIArray<ObTabletSplitArg> &split_info_array)
{
  int ret = OB_SUCCESS;
  leader_addr.reset();
  split_info_array.reset();
  ObLSID ls_id;
  ObArray<obcall::ObTabletSplitArg> tmp_split_info_array;
  ObLocationService *location_service = nullptr;
  const int64_t rpc_timeout = max(GCONF.rpc_timeout, static_cast<int64_t>(1000L * 1000L * 9L));
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(location_service = GCTX.location_service_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("location_cache is null", K(ret));
  } else if (OB_FAIL(ObDDLUtil::get_tablet_leader_addr(location_service, partition_split_arg_.src_tablet_id_, rpc_timeout, ls_id, leader_addr))) {
  } else if (OB_FAIL(prepare_tablet_split_infos(ls_id, leader_addr, tmp_split_info_array))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tmp_split_info_array.count(); ++i) {
      ObCheckProgressStatus send_status;
      const obcall::ObTabletSplitArg &each_arg = tmp_split_info_array.at(i);
      const ObTabletID &tablet_id = each_arg.source_tablet_id_;
      if (OB_FAIL(send_finish_map_.get_refactored(tablet_id, send_status))) {
      } else if (send_status == ObCheckProgressStatus::NOT_STARTED ||
                send_status == ObCheckProgressStatus::ONGOING) { // need call rpc.
        if (OB_FAIL(split_info_array.push_back(each_arg))) {
        }
      } else if (send_status == ObCheckProgressStatus::DONE) { // already call rpc.
        // do nothing.
      }
    }
  }
  return ret;
}

int ObPartitionSplitTask::send_split_rpc(
    const bool is_split_start)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObAddr leader_addr;
  const int64_t rpc_timeout = max(GCONF.rpc_timeout, static_cast<int64_t>(1000L * 1000L * 30L));
  ObTabletSplitStartArg start_arg;
  ObTabletSplitStartResult start_result;
  ObTabletSplitFinishArg finish_arg;
  ObTabletSplitFinishResult finish_result;
  ObIArray<ObTabletSplitArg> &target_split_info_array = is_split_start ? start_arg.split_info_array_ : finish_arg.split_info_array_;
  if (OB_FAIL(setup_split_finish_items(leader_addr, target_split_info_array))) {
  } else if (target_split_info_array.empty()) { // already all sent.
    LOG_TRACE("already all sent", K(ret)); // do nothing.
  } else if (is_split_start) {
    if (OB_TMP_FAIL(GCTX.ob_service_->build_split_tablet_data_start_request(start_arg, start_result))) {
    }
  } else {
    if (OB_TMP_FAIL(GCTX.ob_service_->build_split_tablet_data_finish_request(finish_arg, finish_result))) {
    }
  }
  
  // overwrite ret of rpc call function is expected.
  const ObIArray<int> &resp_ret_codes = is_split_start ? start_result.ret_codes_ : finish_result.ret_codes_;
  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(target_split_info_array.count() < resp_ret_codes.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("mismatched rpc request and response count", K(ret), K(target_split_info_array), K(resp_ret_codes));
  } else if (!resp_ret_codes.empty()) {
    if (OB_UNLIKELY(resp_ret_codes.count() > target_split_info_array.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err", K(ret), K(target_split_info_array), K(resp_ret_codes));
    } else if (is_split_start && !min_split_start_scn_.is_valid_and_not_min()) {
      if (OB_UNLIKELY(!start_result.min_split_start_scn_.is_valid_and_not_min())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected split start scn", K(ret), K(start_result));
      } else if (FALSE_IT(min_split_start_scn_ = start_result.min_split_start_scn_)) {
      } else if (OB_FAIL(update_task_message())) {
      }
    }

    if (OB_SUCC(ret)) {
      TCWLockGuard guard(lock_);
      for (int64_t i = 0; OB_SUCC(ret) && i < resp_ret_codes.count(); i++) {
        const ObTabletID &tablet_id = target_split_info_array.at(i).source_tablet_id_;
        if (OB_SUCCESS == resp_ret_codes.at(i)) {
          if (OB_FAIL(send_finish_map_.set_refactored(tablet_id, ObCheckProgressStatus::DONE, true /*overwrite*/))) {
          }
        } else {
          LOG_WARN("process split finish request failed, need retry", "resp_ret", resp_ret_codes.at(i), K(tablet_id));
        }
      }
    }
  }
  return ret;
}

bool ObPartitionSplitTask::check_need_sync_stats()
{
  return !has_synced_stats_info_;
}


int ObPartitionSplitTask::delete_stat_info(common::ObMySQLTransaction &trans,
                                           const char *table_name,
                                           const uint64_t table_id,
                                           const uint64_t src_part_id)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  ObSqlString sql_string;
  // pre-check
  if (OB_ISNULL(table_name) || OB_INVALID_ID == table_id || OB_INVALID_PARTITION_ID == src_part_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parameter invalid", K(ret), KP(table_name), K(table_id), K(src_part_id));
  } else if (OB_FAIL(sql_string.assign_fmt("DELETE FROM %s "
      " WHERE table_id = %ld and partition_id = %ld;",
      table_name, table_id, src_part_id))) {
  } else if (OB_FAIL(trans.write(sql_string.ptr(), affected_rows))) {
  } else if (OB_UNLIKELY(affected_rows < 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected affected_rows", K(ret), K(affected_rows));
  }
  return ret;
}

int ObPartitionSplitTask::copy_and_delete_src_part_stat_info(const uint64_t table_id,
                                                             const int64_t src_part_id,
                                                             const ObIArray<uint64_t> &local_index_table_ids,
                                                             const ObIArray<int64_t> &src_local_index_part_ids,
                                                             const ObSArray<int64_t> &dest_part_ids,
                                                             const ObSArray<ObSArray<int64_t>> &dest_local_index_part_ids)
{
  int ret = OB_SUCCESS;
  if (local_index_table_ids.count() != src_local_index_part_ids.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("size mismatch of local_index_table_ids and src_local_index_part_ids", K(ret), K(local_index_table_ids), K(local_index_table_ids));
  } else if (OB_INVALID_ID == table_id || OB_INVALID_ID == src_part_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parameter invalid", K(ret), K(table_id), K(src_part_id));
  } else {
    common::ObMySQLTransaction trans;
    if (OB_ISNULL(GCTX.sql_proxy_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
    } else if (OB_FAIL(trans.start(GCTX.sql_proxy_))) {
    } else if (OB_FAIL(copy_src_part_stat_info_to_dest(trans, table_id, src_part_id, partition_split_arg_.local_index_table_ids_, src_local_index_part_ids, dest_part_ids, dest_local_index_part_ids))) {
    } else if (OB_FAIL(delete_stat_info(trans, OB_ALL_TABLE_STAT_TNAME, table_id, table_id))) {
    } else if (OB_FAIL(delete_stat_info(trans, OB_ALL_COLUMN_STAT_TNAME, table_id, table_id))) {
    } else if (OB_FAIL(delete_stat_info(trans, OB_ALL_HISTOGRAM_STAT_TNAME, table_id, table_id))) {
    } else if (OB_FAIL(delete_stat_info(trans, OB_ALL_TABLE_STAT_TNAME, table_id, src_part_id))) {
    } else if (OB_FAIL(delete_stat_info(trans, OB_ALL_COLUMN_STAT_TNAME, table_id, src_part_id))) {
    } else if (OB_FAIL(delete_stat_info(trans, OB_ALL_HISTOGRAM_STAT_TNAME, table_id, src_part_id))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < src_local_index_part_ids.count(); i++) {
        if (OB_INVALID_ID == local_index_table_ids.at(i) || OB_INVALID_ID == src_local_index_part_ids.at(i)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("parameter invalid", K(ret), K(local_index_table_ids.at(i)), K(src_local_index_part_ids.at(i)));
        } else if (OB_FAIL(delete_stat_info(trans, OB_ALL_TABLE_STAT_TNAME, local_index_table_ids.at(i), src_local_index_part_ids.at(i)))) {
        }
      }
    }
    // commit the transaction
    if (trans.is_started()) {
      bool is_commit = (ret == OB_SUCCESS);
      int tmp_ret = trans.end(is_commit);
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("fail to end trans", K(ret), K(is_commit));
        if (OB_SUCC(ret)) {
          ret = tmp_ret;
        }
      } else {
        has_synced_stats_info_ = (ret == OB_SUCCESS);
      }
    }
  }

  return ret;
}

// get table schema from table, exclude partition_id
const char *ObPartitionSplitTask::get_table_schema(const char *table_name)
{
  const char *ret_table_schema = "UNKNOWN_TABLE_NAME";
  if (OB_ALL_TABLE_STAT_TNAME == table_name) {
    ret_table_schema = "gmt_create, gmt_modified, table_id, object_type, last_analyzed, sstable_row_cnt, sstable_avg_row_len, macro_blk_cnt, micro_blk_cnt, memtable_row_cnt, memtable_avg_row_len, row_cnt, avg_row_len, global_stats, user_stats, stattype_locked, stale_stats, spare1, spare2, spare3, spare4, spare5, spare6, index_type"; 
  } else if (OB_ALL_COLUMN_STAT_TNAME == table_name) {
    ret_table_schema = "gmt_create, gmt_modified, table_id, column_id, object_type, last_analyzed, distinct_cnt, null_cnt, max_value, b_max_value, min_value, b_min_value, avg_len, distinct_cnt_synopsis, distinct_cnt_synopsis_size, sample_size, density, bucket_cnt, histogram_type, global_stats, user_stats, spare1, spare2, spare3, spare4, spare5, spare6, cg_macro_blk_cnt, cg_micro_blk_cnt, cg_skip_rate"; 
  } else if (OB_ALL_HISTOGRAM_STAT_TNAME == table_name) {
    ret_table_schema = "gmt_create, gmt_modified, table_id, column_id, endpoint_num, object_type, endpoint_normalized_value, endpoint_value, b_endpoint_value, endpoint_repeat_cnt"; 
  }
  return ret_table_schema;
}

int ObPartitionSplitTask::copy_stat_info(common::ObMySQLTransaction &trans,
                                         const char *table_name,
                                         const uint64_t table_id,
                                         const uint64_t src_part_id,
                                         const uint64_t dest_part_id)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  ObSqlString sql_string;
  // pre-check
  if (OB_ISNULL(table_name) || OB_INVALID_ID == table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parameter invalid", K(ret), KP(table_name), K(table_id), K(src_part_id), K(dest_part_id));
  } else if (OB_FAIL(sql_string.assign_fmt("REPLACE INTO %s (partition_id, %s)"
                                           "SELECT %ld, %s FROM %s "
                                           " WHERE table_id = %ld and partition_id = %ld;",
                                           table_name,get_table_schema(table_name),
                                           dest_part_id, get_table_schema(table_name), table_name,
                                           table_id, src_part_id))) {
  } else if (OB_FAIL(trans.write(sql_string.ptr(), affected_rows))) {
  } else if (OB_UNLIKELY(affected_rows < 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected affected_rows", K(ret), K(affected_rows));
  }
  return ret;
}

int ObPartitionSplitTask::copy_src_part_stat_info_to_dest(common::ObMySQLTransaction &trans,
                                                          const uint64_t table_id,
                                                          const int64_t src_part_id,
                                                          const ObIArray<uint64_t> &local_index_table_ids,
                                                          const ObIArray<int64_t> &src_local_index_part_ids,
                                                          const ObSArray<int64_t> &dest_part_ids,
                                                          const ObSArray<ObSArray<int64_t>> &dest_local_index_part_ids) 
{
  int ret = OB_SUCCESS;
  if (local_index_table_ids.count() != src_local_index_part_ids.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("size mismatch of local_index_table_ids and src_local_index_part_ids", K(ret), K(local_index_table_ids), K(local_index_table_ids));
  } else if (OB_INVALID_ID == table_id || OB_INVALID_ID == src_part_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parameter invalid", K(ret), K(table_id), K(src_part_id));
  } else {
    if (!trans.is_started()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("transaction is not started", KR(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < dest_part_ids.count(); i++) {
        int64_t dest_part_id = dest_part_ids.at(i);
        if (OB_FAIL(copy_stat_info(trans, OB_ALL_TABLE_STAT_TNAME, table_id, table_id, dest_part_id))) {
        } else if (OB_FAIL(copy_stat_info(trans, OB_ALL_COLUMN_STAT_TNAME, table_id, table_id, dest_part_id))) {
        } else if (OB_FAIL(copy_stat_info(trans, OB_ALL_HISTOGRAM_STAT_TNAME, table_id, table_id, dest_part_id))) {
        } else if (OB_FAIL(copy_stat_info(trans, OB_ALL_TABLE_STAT_TNAME, table_id, src_part_id, dest_part_id))) {
        } else if (OB_FAIL(copy_stat_info(trans, OB_ALL_COLUMN_STAT_TNAME, table_id, src_part_id, dest_part_id))) {
        } else if (OB_FAIL(copy_stat_info(trans, OB_ALL_HISTOGRAM_STAT_TNAME, table_id, src_part_id, dest_part_id))) {
        } else {
          ObSArray<int64_t> dest_local_index_part_id = dest_local_index_part_ids.at(i);
          for (int64_t j = 0; OB_SUCC(ret) && j < dest_local_index_part_id.count(); j++) {
            if (OB_INVALID_ID == local_index_table_ids.at(j) || OB_INVALID_ID == dest_local_index_part_id.at(j)) {
              ret = OB_INVALID_ARGUMENT;
              LOG_WARN("parameter invalid", K(ret), K(local_index_table_ids.at(j)), K(dest_local_index_part_id.at(j)));
            } else if (OB_FAIL(copy_stat_info(trans, OB_ALL_TABLE_STAT_TNAME, local_index_table_ids.at(j),src_local_index_part_ids.at(j), dest_local_index_part_id.at(j)))) {
            }
          }
        } 
      }
    }
  }
  return ret;
}

int ObPartitionSplitTask::init_sync_stats_info(const ObTableSchema* const table_schema,
                                                ObSchemaGetterGuard &schema_guard,
                                                int64_t &src_partition_id, /* OUTPUT */
                                                ObSArray<int64_t> &src_local_index_partition_ids, /* OUTPUT */
                                                ObSArray<int64_t> &dest_partition_ids, /* OUTPUT */
                                                ObSArray<ObSArray<int64_t>> &dest_local_index_partition_ids /* OUTPUT */)
{
  // get necessary information: src_partition_id and src_local_index_partition_ids
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("table_schema is nullptr", K(ret));
  } else if (OB_FAIL(table_schema->get_hidden_part_id_by_tablet_id(partition_split_arg_.src_tablet_id_, src_partition_id))) {
  } else if (OB_INVALID_PARTITION_ID == src_partition_id) {
    /* 
      during clean_splitted_tablet or after clean_splitted_tablet, a follower become the leader, this task maybe restart.
      But all/part of the involving src tablets had been cleaned before, thus the hidden partition id maybe OB_INVALID_PARTITION_ID.
      It is not unexpected.
      If one hidden part'id is OB_INVALID_PARTITION_ID, it means we had finished the stats_info_sync job. 
      Therefore we set has_synced_stats_info_ as true.
    */
    ret = OB_ENTRY_NOT_EXIST;
    has_synced_stats_info_ = true;
    LOG_WARN("src local data partition id is invalid, due to the partition_split_task had been processed before", 
      K(ret), K(partition_split_arg_.src_tablet_id_.id()), KPC(this));
  } else if (OB_UNLIKELY(partition_split_arg_.src_local_index_tablet_ids_.count() != partition_split_arg_.local_index_table_ids_.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("src_local_index_tablet_ids_.count != local_index_table_ids_.count", K(ret),
              K(partition_split_arg_.src_local_index_tablet_ids_.count()), K(partition_split_arg_.local_index_table_ids_.count()));
  } else if (OB_FAIL(src_local_index_partition_ids.prepare_allocate(partition_split_arg_.src_local_index_tablet_ids_.count()))) {
  } else {
    // get info of the src_local_index_partition_ids
    for (int64_t i = 0; OB_SUCC(ret) && i < partition_split_arg_.local_index_table_ids_.count(); i++) {
      uint64_t index_table_id = partition_split_arg_.local_index_table_ids_.at(i);
      const ObTableSchema *index_table_schema = nullptr;
      // get the index_table_schema and get the src_local_index_part_id
      if (OB_FAIL(schema_guard.get_table_schema( index_table_id, index_table_schema))) {
      } else if (OB_ISNULL(index_table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("error unexpected, table schema must not be nullptr", K(ret), K(index_table_id));
      } else if (OB_FAIL(index_table_schema->get_hidden_part_id_by_tablet_id(partition_split_arg_.src_local_index_tablet_ids_.at(i), 
                                                       src_local_index_partition_ids.at(i)))) {
      } else if (OB_INVALID_PARTITION_ID == src_local_index_partition_ids.at(i)) {
        ret = OB_ERR_UNEXPECTED; // get the hidden_part_id of src data tablet success, but failed at local index, thus unexpected.
        LOG_WARN("unexpected: src local index partition id is invalid", 
          K(ret), K(i), K(partition_split_arg_.src_local_index_tablet_ids_.at(i).id()), KPC(this));
      }
    }
  }
  if (OB_SUCC(ret)) {
    dest_partition_ids.reset();
    dest_local_index_partition_ids.reset();
    for (int64_t i = 0; OB_SUCC(ret) && i < partition_split_arg_.dest_tablet_ids_.count(); i++) {
      const ObTabletID &dest_tablet_id = partition_split_arg_.dest_tablet_ids_.at(i);
      int64_t part_id = OB_INVALID_INDEX;
      int64_t sub_part_id = OB_INVALID_INDEX;
      if (OB_FAIL(table_schema->get_part_id_by_tablet(dest_tablet_id, part_id, sub_part_id))) {
      } else if (OB_FAIL(dest_partition_ids.push_back(part_id))) {
      } else {
        ObSArray<int64_t> dest_local_index_partition_ids_tmp;
        for (int64_t j = 0; OB_SUCC(ret) && j < partition_split_arg_.dest_local_index_tablet_ids_.count(); j++) {
          const ObTabletID &dest_local_index_tablet_id = partition_split_arg_.dest_local_index_tablet_ids_.at(j).at(i);
          uint64_t index_table_id = partition_split_arg_.local_index_table_ids_.at(j);
          int64_t local_index_part_id = OB_INVALID_INDEX;
          int64_t local_index_sub_part_id = OB_INVALID_INDEX;
          const ObTableSchema *index_table_schema = nullptr;
          // get the index_table_schema and get the dest_local_index_part_id
          if (OB_FAIL(schema_guard.get_table_schema( index_table_id, index_table_schema))) {
          } else if (OB_ISNULL(index_table_schema)) {
            ret = OB_TABLE_NOT_EXIST;
            LOG_WARN("error unexpected, index table schema not exist", K(ret), K(index_table_id));
          } else if (OB_FAIL(index_table_schema->get_part_id_by_tablet(dest_local_index_tablet_id, local_index_part_id, local_index_sub_part_id))) {
          } else if (OB_FAIL(dest_local_index_partition_ids_tmp.push_back(local_index_part_id))) {
          } 
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(dest_local_index_partition_ids.push_back(dest_local_index_partition_ids_tmp))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObPartitionSplitTask::sync_stats_info()
{
  int ret = OB_SUCCESS;
  if (check_need_sync_stats()) {
    const uint64_t data_table_id = object_id_;
    int64_t src_partition_id = 0;
    ObSArray<int64_t> src_local_index_partition_ids;
    ObSArray<int64_t> dest_partition_ids;
    ObSArray<ObSArray<int64_t>> dest_local_index_partition_ids; 
    src_local_index_partition_ids.reset();
    {
      const ObTableSchema *data_table_schema = nullptr;
      ObSchemaGetterGuard schema_guard;
      if (OB_ISNULL(GCTX.schema_service_)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid argument", KR(ret), KP(GCTX.schema_service_));
      } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
      } else if (OB_FAIL(schema_guard.get_table_schema( data_table_id, data_table_schema))) {
      } else if (OB_ISNULL(data_table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("error unexpected, table schema must not be nullptr", K(ret), K(data_table_id));
      } else if (OB_FAIL(init_sync_stats_info(data_table_schema, schema_guard, src_partition_id, src_local_index_partition_ids, dest_partition_ids, dest_local_index_partition_ids))) {
      } 
    }
    if (OB_FAIL(ret)) {
      if (has_synced_stats_info_ && OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      }
    } else if (OB_FAIL(copy_and_delete_src_part_stat_info(data_table_id, src_partition_id, partition_split_arg_.local_index_table_ids_, src_local_index_partition_ids, dest_partition_ids, dest_local_index_partition_ids))) {
    }
  }
  return ret;
}

int ObPartitionSplitTask::succ()
{
  int ret = OB_SUCCESS;
  return cleanup();
}

int ObPartitionSplitTask::wait_recovery_task_finish(const share::ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *orig_table_schema = nullptr;
  const ObTableSchema *orig_data_table_schema = nullptr;
  const ObDatabaseSchema *database_schema = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObPartitionSplitTask has not been inited", K(ret));
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_tenant_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_table_schema( object_id_, orig_table_schema))) {
  } else if (OB_ISNULL(orig_table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, orig table schema is nullptr", K(ret));
  } else if (OB_FAIL(schema_guard.get_database_schema( orig_table_schema->get_database_id(), database_schema))) {
  } else if (OB_ISNULL(database_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, database schema is nullptr", K(ret));
  } else if (orig_table_schema->is_global_index_table()) {
    if (OB_FAIL(schema_guard.get_table_schema( orig_table_schema->get_data_table_id(), orig_data_table_schema))) {
    } else if (OB_ISNULL(orig_data_table_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("error unexpected, orig data table schema is nullptr", K(ret));
    }
  } else {
    orig_data_table_schema = orig_table_schema;
  }
  if (OB_SUCC(ret)) {
    if (OB_INVALID_ID == orig_data_table_schema->get_association_table_id()) {
      SMART_VAR(obcall::ObAlterTableArg, alter_table_arg) {
        int64_t ddl_rpc_timeout = 0;
        ObSArray<uint64_t> unused_ids;
        alter_table_arg.ddl_task_type_ = share::PARTITION_SPLIT_RECOVERY_TASK;
        
        alter_table_arg.table_id_ = orig_data_table_schema->get_table_id();
        alter_table_arg.task_id_ = task_id_;
        alter_table_arg.is_inner_ = true;
        alter_table_arg.parallelism_ = parallelism_;
        alter_table_arg.consumer_group_id_ = partition_split_arg_.consumer_group_id_;
        alter_table_arg.tz_info_wrap_.set_tz_info_offset(0);
        alter_table_arg.nls_formats_[ObNLSFormatEnum::NLS_DATE] = ObTimeConverter::COMPAT_OLD_NLS_DATE_FORMAT;
        alter_table_arg.nls_formats_[ObNLSFormatEnum::NLS_TIMESTAMP] = ObTimeConverter::COMPAT_OLD_NLS_TIMESTAMP_FORMAT;
        alter_table_arg.nls_formats_[ObNLSFormatEnum::NLS_TIMESTAMP_TZ] = ObTimeConverter::COMPAT_OLD_NLS_TIMESTAMP_TZ_FORMAT;
        AlterTableSchema &alter_table_schema = alter_table_arg.alter_table_schema_;
        if (OB_FAIL(alter_table_schema.assign(*orig_data_table_schema))) {
        } else {
          
          alter_table_schema.set_origin_database_name(database_schema->get_database_name_str());
          alter_table_schema.set_origin_table_name(orig_data_table_schema->get_table_name_str());
          ObTZMapWrap tz_map_wrap;
          if (OB_FAIL(OTTZ_MGR.get_tenant_tz(tz_map_wrap))) {
          } else if (FALSE_IT(alter_table_arg.set_tz_info_map(tz_map_wrap.get_tz_map()))) {
            ret = OB_INVALID_ARGUMENT;
          } else if (OB_FAIL(ObDDLUtil::get_ddl_rpc_timeout_by_table(object_id_, ddl_rpc_timeout))) {
          } else if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->execute_ddl_task(alter_table_arg, unused_ids); }))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObPartitionSplitTask::check_health()
{
  int ret = OB_SUCCESS;
  const uint64_t table_id = object_id_;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!ObDDLServiceLauncher::is_ddl_service_started()) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("ddl service not started", KR(ret));
    need_retry_ = false;
  } else if (OB_FAIL(refresh_status())) {
  } else if (OB_FAIL(refresh_schema_version())) {
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.schema_service_));
  } else {
    ObMultiVersionSchemaService &schema_service = *GCTX.schema_service_;
    ObSchemaGetterGuard schema_guard;
    const ObTableSchema *index_schema = nullptr;
    bool is_data_table_exist = false;
    if (OB_FAIL(schema_service.get_tenant_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.check_table_exist(table_id,
            is_data_table_exist))) {
    } else if (!is_data_table_exist) {
      ret = OB_TABLE_NOT_EXIST;
      const ObDDLTaskStatus status = static_cast<ObDDLTaskStatus>(task_status_);
      if (ObDDLTaskStatus::WAIT_PARTITION_SPLIT_RECOVERY_TASK_FINISH == status) {
        ret = OB_SUCCESS;
        ObString unused_str;
        if (OB_FAIL(batch_insert_reorganize_history())) {
        } else if (OB_FAIL(report_error_code(unused_str))) {
        } else if (OB_FAIL(remove_task_record())) {
        } else {
          need_retry_ = false;   // clean succ, stop the task
        }
      }
      LOG_WARN("data table or dest table not exist", K(ret), K(is_data_table_exist), K(table_id));
    }
  }
  if (ObDDLTaskStatus::FAIL == static_cast<ObDDLTaskStatus>(task_status_)
      || ObDDLTaskStatus::SUCCESS == static_cast<ObDDLTaskStatus>(task_status_)) {
    ret = OB_SUCCESS; // allow clean up
  }
  check_ddl_task_execute_too_long();
  return ret;
}

int ObPartitionSplitTask::serialize_params_to_message(
    char *buf,
    const int64_t buf_len,
    int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == buf || buf_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(ObDDLTask::serialize_params_to_message(buf, buf_len, pos))) {
  } else {
    LST_DO_CODE(OB_UNIS_ENCODE, all_src_tablet_ids_, data_tablet_compaction_scn_,
      index_tablet_compaction_scns_, lob_tablet_compaction_scns_, partition_split_arg_, 
      tablet_size_, data_tablet_parallel_rowkey_list_, index_tablet_parallel_rowkey_list_, min_split_start_scn_);
  }
  return ret;
}

int ObPartitionSplitTask::deserialize_params_from_message(
    const char *buf,
    const int64_t data_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  ObPartitionSplitArg tmp_arg;
  int64_t split_index_tablets_cnt = 0;
  if (OB_UNLIKELY(!true || nullptr == buf || data_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(buf), K(data_len));
  } else if (OB_FAIL(ObDDLTask::deserialize_params_from_message(
          buf, data_len, pos))) {
  } else {
    LST_DO_CODE(OB_UNIS_DECODE, all_src_tablet_ids_, data_tablet_compaction_scn_,
      index_tablet_compaction_scns_, lob_tablet_compaction_scns_, tmp_arg, 
      tablet_size_);
    if (FAILEDx(ObSplitUtil::deserializ_parallel_datum_rowkey(
          allocator_, buf, data_len, pos, data_tablet_parallel_rowkey_list_))) {
    } else {
      LST_DO_CODE(OB_UNIS_DECODE, split_index_tablets_cnt);
      if (FAILEDx(index_tablet_parallel_rowkey_list_.prepare_allocate(split_index_tablets_cnt))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < split_index_tablets_cnt; i++) {
        if (OB_FAIL(ObSplitUtil::deserializ_parallel_datum_rowkey(
              allocator_, buf, data_len, pos, index_tablet_parallel_rowkey_list_.at(i)))) {
        } 
      }
      LOG_TRACE("parallel datum rowkey info", K(ret), K(data_tablet_parallel_rowkey_list_), K(index_tablet_parallel_rowkey_list_));
    }
    if (OB_SUCC(ret)) {
      LST_DO_CODE(OB_UNIS_DECODE, min_split_start_scn_);
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(deep_copy_table_arg(allocator_, tmp_arg, partition_split_arg_))) {
    } else if (OB_FAIL(init_compaction_scn_map())) {
    } else if (OB_FAIL(restore_compaction_scn_map())) {
    } 
  }
  return ret;
}

int64_t ObPartitionSplitTask::get_serialize_param_size() const
{
  int len = ObDDLTask::get_serialize_param_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN, all_src_tablet_ids_, data_tablet_compaction_scn_,
      index_tablet_compaction_scns_, lob_tablet_compaction_scns_, partition_split_arg_, 
      tablet_size_, data_tablet_parallel_rowkey_list_, index_tablet_parallel_rowkey_list_, min_split_start_scn_);
  return len;
}

int ObPartitionSplitTask::check_src_tablet_exist(const int64_t table_id,
    const ObTabletID &src_tablet_id,
    bool &is_src_tablet_exist)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *table_schema = nullptr;
  int64_t src_part_id = OB_INVALID_PARTITION_ID;
  is_src_tablet_exist = false;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.schema_service_));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_table_schema( table_id, table_schema))) {
  } else if (nullptr != table_schema) {
    if (OB_FAIL(table_schema->get_hidden_part_id_by_tablet_id(src_tablet_id, src_part_id))) {
      if (OB_TABLET_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        LOG_INFO("src tablet deleted", K(ret), K(table_id), K(src_tablet_id), K(task_id_));
      } else {
        LOG_WARN("failed to get hidden part id", K(ret));
      }
    } else {
      is_src_tablet_exist = true;
    }
  } else {
    LOG_INFO("table deleted", K(ret), K(table_id), K(task_id_));
  }
  return ret;
}

int ObPartitionSplitTask::cleanup_impl()
{
  int ret = OB_SUCCESS;
  ObString unused_str;
  bool is_src_tablet_exist = false;

  ROOTSERVICE_EVENT_ADD("ddl", "partition_split_task_cleanup");
  DEBUG_SYNC(BEFORE_PARTITION_SPLIT_TASK_CLEANUP);

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(check_src_tablet_exist(object_id_, partition_split_arg_.src_tablet_id_, is_src_tablet_exist))) {
  } else if (is_src_tablet_exist) {
    if (OB_FAIL(batch_insert_reorganize_history())) {
    } else if (OB_FAIL(clean_splitted_tablet())) {
    }
  } else {
    LOG_INFO("already clean splitted tablet, skip", K(ret), K(task_id_), K(object_id_), K(partition_split_arg_.src_tablet_id_));
  }
  DEBUG_SYNC(PARTITION_SPLIT_SUCCESS);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(report_error_code(unused_str))) {
  } else if (OB_FAIL(remove_task_record())) {
  } else {
    need_retry_ = false;   // clean succ, stop the task
  }
  LOG_INFO("clean task finished", K(ret), K(*this));
  return ret;
}

int ObPartitionSplitTask::clean_splitted_tablet()
{
  int ret = OB_SUCCESS;
  ObCleanSplittedTabletArg clean_arg;
  if (FALSE_IT(clean_arg.table_id_ = object_id_)) {
  } else if (FALSE_IT(clean_arg.task_id_ = task_id_)) {
  } else if (FALSE_IT(clean_arg.is_auto_split_ = is_auto_split(partition_split_arg_.task_type_))) {
  } else if (FALSE_IT(clean_arg.src_table_tablet_id_ = partition_split_arg_.src_tablet_id_)) {
  } else if (OB_FAIL(clean_arg.dest_tablet_ids_.assign(partition_split_arg_.dest_tablet_ids_))) {
  } else if (OB_FAIL(clean_arg.local_index_table_ids_.assign(partition_split_arg_.local_index_table_ids_))) {
  } else if (OB_FAIL(clean_arg.src_local_index_tablet_ids_.assign(partition_split_arg_.src_local_index_tablet_ids_))) {
  } else if (OB_FAIL(clean_arg.dest_local_index_tablet_ids_.assign(partition_split_arg_.dest_local_index_tablet_ids_))) {
  } else if (OB_FAIL(clean_arg.lob_table_ids_.assign(partition_split_arg_.lob_table_ids_))) {
  } else if (OB_FAIL(clean_arg.src_lob_tablet_ids_.assign(partition_split_arg_.src_lob_tablet_ids_))) {
  } else if (OB_FAIL(clean_arg.dest_lob_tablet_ids_.assign(partition_split_arg_.dest_lob_tablet_ids_))) {
  } else if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->clean_splitted_tablet(clean_arg); }))) {
  }

  return ret;
}

void ObPartitionSplitTask::reset_replica_build_stat()
{
  TCWLockGuard guard(lock_);
  replica_build_task_submit_ = false;
  replica_build_request_time_ = 0;
  replica_build_ret_code_ = OB_SUCCESS;
}

int ObPartitionSplitTask::update_message_row_progress_(const oceanbase::share::ObDDLTaskStatus status, 
                                                      const bool task_submitted,
                                                      int64_t &pos) 
{
  int ret = OB_SUCCESS;
  if (!task_submitted) {
    ObString str;
    if (ObDDLTaskStatus::WAIT_DATA_TABLE_SPLIT_END == status) {
      str = "STATUS: SUBMITTING DATA TABLET SPLIT TASK";
    } else if (ObDDLTaskStatus::WAIT_LOCAL_INDEX_SPLIT_END == status) {
      str = "STATUS: SUBMITTING LOCAL INDEX TABLET SPLIT TASK";
    } else if (ObDDLTaskStatus::WAIT_LOB_TABLE_SPLIT_END == status) {
      str = "STATUS: SUBMITTING LOB TABLE TABLET SPLIT TASK";
    }  
    if (OB_FAIL(databuff_printf(stat_info_.message_, 
                               MAX_LONG_OPS_MESSAGE_LENGTH, 
                               pos,
                               "%s",
                               str.ptr()))) {
    }
  } else {
    ObString str;
    int64_t row_inserted = 0;
    int64_t physical_row_count_ = 0;
    double percent = 0.0;
    replica_builder_.get_progress(row_inserted, physical_row_count_, percent);
    if (ObDDLTaskStatus::WAIT_DATA_TABLE_SPLIT_END == status
      && OB_FAIL(databuff_printf(stat_info_.message_, 
                         MAX_LONG_OPS_MESSAGE_LENGTH, 
                         pos,
                         "STATUS: DATA TABLET SPLITTING, TOTAL_ROWS: %ld, ROW_PROCESSED: %ld, ROW_PROGRESS: %0.1lf%%; LOCAL INDEX TABLET 0.0%%; LOB TEBLET 0.0%%;",
                         physical_row_count_, row_inserted, percent))) {
      LOG_WARN("failed to print", K(ret));
    } else if (ObDDLTaskStatus::WAIT_LOCAL_INDEX_SPLIT_END == status
            && OB_FAIL(databuff_printf(stat_info_.message_, 
                               MAX_LONG_OPS_MESSAGE_LENGTH, 
                               pos,
                               "STATUS: DATA TABLET SPLITTED 100.0%%; LOCAL INDEX TABLET SPLITTING, TOTAL_ROWS: %ld, ROW_PROCESSED: %ld, ROW_PROGRESS: %0.1lf%%; LOB TEBLET 0.0%%;",
                               physical_row_count_, row_inserted, percent))) {
      LOG_WARN("failed to print", K(ret));
    } else if (ObDDLTaskStatus::WAIT_LOB_TABLE_SPLIT_END == status
            && OB_FAIL(databuff_printf(stat_info_.message_, 
                               MAX_LONG_OPS_MESSAGE_LENGTH, 
                               pos,
                               "STATUS: DATA TABLET SPLITTED 100.0%%; LOCAL INDEX TABLET SPLITTED 100.0%%; LOB TABLET SPLITTING, TOTAL_ROWS: %ld, ROW_PROCESSED: %ld, ROW_PROGRESS: %0.1lf%%",
                               physical_row_count_, row_inserted, percent)))  {
      LOG_WARN("failed to print", K(ret));
    }
  }
  return ret;
}

int ObPartitionSplitTask::get_waiting_tablet_ids_(
      const hash::ObHashMap<ObCheckProgressKey<common::ObAddr>, ObCheckProgressStatus> &tablet_hash,
      ObIArray<uint64_t> &waiting_tablets /* OUT */)
{
  int ret = OB_SUCCESS;
  waiting_tablets.reset();
  for (hash::ObHashMap<ObCheckProgressKey<common::ObAddr>, ObCheckProgressStatus>::const_iterator it = tablet_hash.begin(); OB_SUCC(ret) && it != tablet_hash.end(); it++) {
    if (ObCheckProgressStatus::DONE != it->second && OB_FAIL(waiting_tablets.push_back(it->first.tablet_id_.id()))) {
      LOG_WARN("failed to push back", K(ret));
    }
  }
  return ret;
}

int ObPartitionSplitTask::get_waiting_tablet_ids_(
      const hash::ObHashMap<ObTabletID, ObCheckProgressStatus> &tablet_hash,
      ObIArray<uint64_t> &waiting_tablets /* OUT */)
{
  int ret = OB_SUCCESS;
  waiting_tablets.reset();
  for (hash::ObHashMap<ObTabletID, ObCheckProgressStatus>::const_iterator it = tablet_hash.begin(); OB_SUCC(ret) && it != tablet_hash.end(); it++) {
    if (ObCheckProgressStatus::DONE != it->second && OB_FAIL(waiting_tablets.push_back(it->first.id()))) {
      LOG_WARN("failed to push back", K(ret));
    }
  }
  return ret;
}

int ObPartitionSplitTask::update_message_tablet_progress_(const oceanbase::share::ObDDLTaskStatus status,
                                                         int64_t &pos)
{
  int ret = OB_SUCCESS;
  ObSArray<uint64_t> waiting_tablets;
  {
    TCRLockGuard guard(lock_);
    if (ObDDLTaskStatus::WAIT_FROZE_END == status) {
      if (!freeze_progress_status_inited_) {
        if (OB_FAIL(databuff_printf(stat_info_.message_, MAX_LONG_OPS_MESSAGE_LENGTH, pos, "STATUS: WAIT FROZE at INITIALIZING"))) {
        }
      } else if (OB_FAIL(get_waiting_tablet_ids_(freeze_progress_map_, waiting_tablets))) {
      } else if (OB_FAIL(databuff_printf(stat_info_.message_, MAX_LONG_OPS_MESSAGE_LENGTH, pos, "STATUS: WAIT FROZE %ld/%ld", 
          freeze_progress_map_.size() - waiting_tablets.size(), freeze_progress_map_.size()))) {
      }
    } else if (ObDDLTaskStatus::WAIT_COMPACTION_END == status) {
      if (!compact_progress_status_inited_) {
        if (OB_FAIL(databuff_printf(stat_info_.message_, MAX_LONG_OPS_MESSAGE_LENGTH, pos, "STATUS: WAIT COMPACTION at INITIALIZING"))) {
        }
      } else if (OB_FAIL(get_waiting_tablet_ids_(compaction_progress_map_, waiting_tablets))) {
      } else if (OB_FAIL(databuff_printf(stat_info_.message_, MAX_LONG_OPS_MESSAGE_LENGTH, pos, "STATUS: WAIT COMPACTION %ld/%ld", 
          compaction_progress_map_.size() - waiting_tablets.size(), compaction_progress_map_.size()))) {
      }
    } else if (ObDDLTaskStatus::WRITE_SPLIT_START_LOG == status) {
      if (!write_split_log_status_inited_) {
        if (OB_FAIL(databuff_printf(stat_info_.message_, MAX_LONG_OPS_MESSAGE_LENGTH, pos, "STATUS: WRITE START LOG at INITIALIZING"))) {
        }
      } else if (OB_FAIL(get_waiting_tablet_ids_(send_finish_map_, waiting_tablets))) {
      } else if (OB_FAIL(databuff_printf(stat_info_.message_, MAX_LONG_OPS_MESSAGE_LENGTH, pos, "STATUS: WRITE START LOG %ld/%ld", 
          send_finish_map_.size() - waiting_tablets.size(), send_finish_map_.size()))) {
      }
    }
  }
  
  if (OB_SUCC(ret) && waiting_tablets.count() != 0) { // show the processing tablet_ids;
    int64_t tmp_pos = 0;
    if (OB_FAIL(databuff_printf(stat_info_.message_,
                                MAX_LONG_OPS_MESSAGE_LENGTH - 2, // "]\0" at the end
                                pos,
                                ", WAITING TABLET: ["))) {
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < waiting_tablets.count(); i++) {
      if (OB_FAIL(databuff_printf(stat_info_.message_,
                                  MAX_LONG_OPS_MESSAGE_LENGTH - 2, // "]\0" at the end
                                  pos,
                                  "%ld",
                                  waiting_tablets.at(i)))) {
      } else if (OB_FALSE_IT(tmp_pos = pos)) { 
        // [123
        //     ^
        //     |
        //  tmp_pos, re-orientate pos when OB_SIZE_OVERFLOW
        LOG_WARN("failed to assign tmp_pos", K(ret));
      } else if (i != waiting_tablets.count() - 1 
                 && OB_FAIL(databuff_printf(stat_info_.message_,
                                            MAX_LONG_OPS_MESSAGE_LENGTH - 2, // "]\0" at the end
                                            pos,
                                            ", "))) {
        LOG_WARN("failed to print", K(ret));
      } else if (i == waiting_tablets.count() - 1 
                 && OB_FAIL(databuff_printf(stat_info_.message_,
                                            MAX_LONG_OPS_MESSAGE_LENGTH,
                                            pos,
                                            "]"))) {
        LOG_WARN("failed to print", K(ret));
      }
    }
    if (ret == OB_SIZE_OVERFLOW) { // cut down when length of tablet_ids longer than MAX_LONG_OPS_MESSAGE_LENGTH.
      ret = OB_SUCCESS;
      stat_info_.message_[tmp_pos] = ']';
      stat_info_.message_[tmp_pos + 1] = '\0';
    } else {
      LOG_WARN("failed to print", K(ret));
    }
  }
  return ret;
}

int ObPartitionSplitTask::collect_longops_stat(ObLongopsValue &value)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  const ObDDLTaskStatus status = static_cast<ObDDLTaskStatus>(task_status_);
  bool replica_build_task_submit = false;
  {
    TCRLockGuard guard(lock_);
    replica_build_task_submit = replica_build_task_submit_;
  }
  if (OB_FAIL(databuff_printf(stat_info_.message_, 
                              MAX_LONG_OPS_MESSAGE_LENGTH, 
                              pos, 
                              "TASK_ID: %ld, ", 
                              task_id_))) {
  }
  else {
    switch (status) {
      case ObDDLTaskStatus::PREPARE: {
        if (OB_FAIL(databuff_printf(stat_info_.message_,
                                    MAX_LONG_OPS_MESSAGE_LENGTH,
                                    pos,
                                    "STATUS: PREPARE"))) {
        }
        break;
      }
      case ObDDLTaskStatus::WAIT_FROZE_END: {
        if (OB_FAIL(update_message_tablet_progress_(status, pos))) {
        }
        break;
      }
      case ObDDLTaskStatus::WAIT_COMPACTION_END: {
        if (OB_FAIL(update_message_tablet_progress_(status, pos))) {
        }
        break;
      }
      case ObDDLTaskStatus::WRITE_SPLIT_START_LOG: {
        if (OB_FAIL(update_message_tablet_progress_(status, pos))) {
        }
        break;
      }
      case ObDDLTaskStatus::WAIT_DATA_TABLE_SPLIT_END: {
        if (OB_FAIL(update_message_row_progress_(status,
                                                replica_build_task_submit,
                                                pos))) {
        }
        break;
      }
      case ObDDLTaskStatus::WAIT_LOCAL_INDEX_SPLIT_END: {
        if (OB_FAIL(update_message_row_progress_(status,
                                                replica_build_task_submit,
                                                pos))) {
        }
        break;
      }
      case ObDDLTaskStatus::WAIT_LOB_TABLE_SPLIT_END: {
        if (OB_FAIL(update_message_row_progress_(status,
                                                replica_build_task_submit,
                                                pos))) {
        }        
        break;
      }
      case ObDDLTaskStatus::WAIT_TRANS_END: {
        if (snapshot_version_ > 0) {
          if (OB_FAIL(databuff_printf(stat_info_.message_,
                                      MAX_LONG_OPS_MESSAGE_LENGTH,
                                      pos,
                                      "STATUS: WAIT TRANS END: SNAPSHOT_VERSION: %ld",
                                      snapshot_version_))) {
          }
        } else {
          if (OB_FAIL(databuff_printf(stat_info_.message_,
                                      MAX_LONG_OPS_MESSAGE_LENGTH,
                                      pos,
                                      "STATUS: WAIT TRANS END, PENDING_TX_ID: %ld",
                                      wait_trans_ctx_.get_pending_tx_id().get_id()))) {
          }
        }
        break;
      }
      case ObDDLTaskStatus::TAKE_EFFECT: {
        if (OB_FAIL(databuff_printf(stat_info_.message_,
                                    MAX_LONG_OPS_MESSAGE_LENGTH,
                                    pos,
                                    "STATUS: TAKE EFFECT"))) {
        }
        break;
      }
      case ObDDLTaskStatus::SUCCESS: {
        if (OB_FAIL(databuff_printf(stat_info_.message_,
                                    MAX_LONG_OPS_MESSAGE_LENGTH, 
                                    pos,
                                    "STATUS: PARTITION SPLIT SUCCESS"))) {
        }
        break;
      }
      case ObDDLTaskStatus::WAIT_PARTITION_SPLIT_RECOVERY_TASK_FINISH: {
        if (OB_FAIL(databuff_printf(stat_info_.message_,
                                    MAX_LONG_OPS_MESSAGE_LENGTH, 
                                    pos,
                                    "STATUS: WAIT RECOVERY SPLIT TASK CREATED"))) {
        }
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("not expected status", K(ret), K(status), K(*this));
        break;
      }
    } // end switch
  }
  
  if (OB_FAIL(ret)) {
    // error occur
  } else if (OB_FAIL(copy_longops_stat(value))) {
  }
  return ret;
}
int ObPartitionSplitTask::batch_insert_reorganize_history()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
  } else if (OB_FAIL(ObTabletReorganizeHistoryTableOperator::batch_insert(
      *GCTX.sql_proxy_, partition_split_arg_, stat_info_.start_time_, ObTimeUtility::current_time()))) {
  }
  return ret;
}

int ObPartitionSplitTask::check_can_reuse_macro_block(
    ObSchemaGetterGuard &schema_guard,
    const ObIArray<uint64_t> &table_ids,
    ObSArray<bool> &can_reuse_macro_blocks)
{
  int ret = OB_SUCCESS;
  can_reuse_macro_blocks.reset();
  if (OB_UNLIKELY(false || table_ids.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init twice", K(ret), K(table_ids));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < table_ids.count(); i++) {
      const ObTableSchema *table_schema = nullptr;
      const uint64_t table_id = table_ids.at(i);
      const ObTableSchema *fetch_part_key_schema = nullptr; // used to fetch partition key info.
      if (OB_FAIL(schema_guard.get_table_schema( table_id, table_schema))) {
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("table not exist", K(ret), K(table_id));
      } else if (is_aux_lob_table(table_schema->get_table_type())) {
        if (OB_FAIL(can_reuse_macro_blocks.push_back(false))) {
        }
      } else if (!table_schema->is_index_local_storage() &&
          FALSE_IT(fetch_part_key_schema = table_schema)) {
        // fetch partition key info from itself for data table and global index table.
        // fetch partition key info from data table for local index table.
      } else if (table_schema->is_index_local_storage() 
          && OB_FAIL(schema_guard.get_table_schema( table_schema->get_data_table_id(), fetch_part_key_schema))) {
        LOG_WARN("get data table schema failed", K(ret), "data_table_id", table_schema->get_data_table_id());
      } else if (OB_ISNULL(fetch_part_key_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("table does not exist", K(ret), KPC(table_schema));
      } else {
        ObArray<uint64_t> rowkey_cols;
        ObArray<uint64_t> part_key_cols;
        const common::ObRowkeyInfo &rowkey_info = table_schema->get_rowkey_info();
        const common::ObPartitionKeyInfo &part_key_info = fetch_part_key_schema->get_partition_key_info();
        if (OB_FAIL(rowkey_info.get_column_ids(rowkey_cols))) {
        } else if (OB_FAIL(part_key_info.get_column_ids(part_key_cols))) {
        } else if (OB_UNLIKELY(rowkey_cols.count() < part_key_cols.count())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected err", K(ret), K(rowkey_cols), K(part_key_cols), KPC(fetch_part_key_schema));
        } else {
          bool can_reuse = true;
          for (int64_t col_idx = 0; OB_SUCC(ret) && can_reuse && col_idx < rowkey_cols.count() && col_idx < part_key_cols.count(); col_idx++) {
            const ObColumnSchemaV2 *part_col_chema = fetch_part_key_schema->get_column_schema(part_key_cols.at(col_idx));
            const ObColumnSchemaV2 *rowkey_col_schema = table_schema->get_column_schema(rowkey_cols.at(col_idx));
            if (OB_UNLIKELY(nullptr == part_col_chema || nullptr == rowkey_col_schema)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected err", K(ret), K(col_idx), KPC(fetch_part_key_schema));
            } else if (rowkey_col_schema->is_shadow_column()) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected error", K(ret), K(col_idx), K(rowkey_cols), K(part_key_cols), KPC(fetch_part_key_schema));
            } else {
              can_reuse &= rowkey_cols.at(col_idx) == part_key_cols.at(col_idx);
            }
          }
          if (FAILEDx(can_reuse_macro_blocks.push_back(can_reuse))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObPartitionSplitTask::prepare_tablet_split_ranges(
    ObSEArray<ObSEArray<blocksstable::ObDatumRowkey, 8>, 8> &parallel_datum_rowkey_list)
{
  int ret = OB_SUCCESS;
  parallel_datum_rowkey_list.reset();
  ObLSID ls_id;
  ObAddr leader_addr;
  const int64_t rpc_timeout = ObDDLUtil::get_default_ddl_rpc_timeout();
  const int64_t source_index_tablets_cnt = partition_split_arg_.src_local_index_tablet_ids_.count();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!data_tablet_parallel_rowkey_list_.empty()) {
    if (OB_UNLIKELY(source_index_tablets_cnt != index_tablet_parallel_rowkey_list_.count())) {
      ret = OB_ERR_UNEXPECTED; // defensive check.
      LOG_WARN("unexpected err, mismatched parallel rowkey list count", 
        K(ret), K(task_status_), K(source_index_tablets_cnt), K(index_tablet_parallel_rowkey_list_));
    }
  } else if (OB_UNLIKELY(!index_tablet_parallel_rowkey_list_.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err, empty parallel rowkey list for the data tablet, but non-empty ones for the index tablets", 
      K(ret), K(task_status_), K(data_tablet_parallel_rowkey_list_), K(index_tablet_parallel_rowkey_list_));
  } else if (WRITE_SPLIT_START_LOG != task_status_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err, empty parallel rowkey list under a mismatched status", K(ret), K(task_status_));
  } else if (OB_FAIL(index_tablet_parallel_rowkey_list_.prepare_allocate(source_index_tablets_cnt))) {
  } else if (OB_FAIL(ObDDLUtil::get_tablet_leader_addr(GCTX.location_service_, partition_split_arg_.src_tablet_id_, rpc_timeout, ls_id, leader_addr))) {
  } else {
    for (int64_t tablet_idx = 0; OB_SUCC(ret) && tablet_idx < 1 /*data_tablet*/ + source_index_tablets_cnt; tablet_idx++) {
      const ObTabletID &src_tablet_id = tablet_idx == 0 ? partition_split_arg_.src_tablet_id_ : partition_split_arg_.src_local_index_tablet_ids_.at(tablet_idx - 1);
      ObIArray<blocksstable::ObDatumRowkey> &target_parallel_rowkey_list = tablet_idx == 0 ? 
            data_tablet_parallel_rowkey_list_ : index_tablet_parallel_rowkey_list_.at(tablet_idx - 1);
      obcall::ObPrepareSplitRangesArg arg;
      obcall::ObPrepareSplitRangesRes result;
      arg.ls_id_              = ls_id;
      arg.tablet_id_          = src_tablet_id;
      arg.user_parallelism_   = parallelism_; // parallelism_;
      arg.schema_tablet_size_ = std::max(tablet_size_, static_cast<int64_t>(128 * 1024 * 1024L)/*128MB*/);
      arg.ddl_type_ = task_type_;
      if (OB_FAIL(GCTX.ob_service_->prepare_tablet_split_task_ranges(arg, result))) {
      } else if (OB_UNLIKELY(result.parallel_datum_rowkey_list_.empty())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected parallel info", K(ret), K(result));
      } else if (OB_FAIL(target_parallel_rowkey_list.prepare_allocate(result.parallel_datum_rowkey_list_.count()))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < result.parallel_datum_rowkey_list_.count(); i++) {
          if (OB_FAIL(result.parallel_datum_rowkey_list_.at(i).deep_copy(target_parallel_rowkey_list.at(i), allocator_))) {
          }
        }
      }
    }
    if (OB_FAIL(ret)) {
      data_tablet_parallel_rowkey_list_.reset(); // reset.
      index_tablet_parallel_rowkey_list_.reset(); // reset.
    } else if (OB_FAIL(update_task_message())) {
    }
  }
  if (OB_SUCC(ret)) {
    if (ObDDLTaskStatus::WRITE_SPLIT_START_LOG == task_status_) {
      // do nothing.
    } else if (ObDDLTaskStatus::WAIT_DATA_TABLE_SPLIT_END == task_status_) {
      if (OB_FAIL(parallel_datum_rowkey_list.push_back(data_tablet_parallel_rowkey_list_))) {
      }
    } else if (ObDDLTaskStatus::WAIT_LOB_TABLE_SPLIT_END == task_status_) {
      if (OB_FAIL(parallel_datum_rowkey_list.push_back(data_tablet_parallel_rowkey_list_))) {
      } else if (OB_FAIL(parallel_datum_rowkey_list.push_back(data_tablet_parallel_rowkey_list_))) {
      }
    } else if (ObDDLTaskStatus::WAIT_LOCAL_INDEX_SPLIT_END == task_status_) {
      if (OB_UNLIKELY(source_index_tablets_cnt != index_tablet_parallel_rowkey_list_.count())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("mismatched datum rowkey list", K(ret), K(source_index_tablets_cnt), K(index_tablet_parallel_rowkey_list_));
      } else if (OB_FAIL(parallel_datum_rowkey_list.assign(index_tablet_parallel_rowkey_list_))) {
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected status", K(ret), K(task_status_), K(data_tablet_parallel_rowkey_list_), K(index_tablet_parallel_rowkey_list_));
    }
  }
  return ret;
}

int ObPartitionSplitTask::prepare_tablet_split_infos(
    const share::ObLSID &ls_id,
    const ObAddr &leader_addr, 
    ObIArray<ObTabletSplitArg> &split_info_array)
{
  int ret = OB_SUCCESS;
  split_info_array.reset();
  ObSArray<uint64_t> lob_col_idxs;
  ObArray<uint64_t> table_ids;
  ObSArray<bool> can_reuse_macro_blocks;
  ObSchemaGetterGuard schema_guard;
  ObMultiVersionSchemaService &schema_service = ObMultiVersionSchemaService::get_instance();
  const int64_t rpc_timeout = max(GCONF.rpc_timeout, static_cast<int64_t>(1000L * 1000L * 9L));
  if (all_src_tablet_ids_.empty() && OB_FAIL(setup_src_tablet_ids_array())) {
    LOG_WARN("failed to setup all src tablet ids", K(ret));
  } else if (OB_FAIL(setup_lob_idxs_arr(lob_col_idxs))) {
  } else if (OB_FAIL(schema_service.get_tenant_schema_guard(schema_guard))) {
  } else if (OB_FAIL(table_ids.push_back(object_id_))) {
  } else if (OB_FAIL(table_ids.push_back(partition_split_arg_.local_index_table_ids_))) {
  } else if (OB_FAIL(table_ids.push_back(partition_split_arg_.lob_table_ids_))) {
  } else if (OB_FAIL(check_can_reuse_macro_block(schema_guard, table_ids, can_reuse_macro_blocks))) {
  } else if (OB_UNLIKELY(all_src_tablet_ids_.count() != table_ids.count() || all_src_tablet_ids_.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(all_src_tablet_ids_), K(table_ids), K(partition_split_arg_));
  }
  const int64_t lob_tablet_start_idx = 1 /*data_tablet*/ + partition_split_arg_.src_local_index_tablet_ids_.count();
  const ObSArray<ObTabletID> &lob_tablet_ids = partition_split_arg_.src_lob_tablet_ids_;
  for (int64_t i = 0; OB_SUCC(ret) && i < all_src_tablet_ids_.count(); ++i) {
    const ObTabletID &tablet_id = all_src_tablet_ids_.at(i);
    int64_t primary_compaction_scn = 0;
    if (OB_FAIL(tablet_compaction_scn_map_.get_refactored(tablet_id, primary_compaction_scn))) {
    } else {
      ObTabletSplitArg split_info;
      split_info.ls_id_               = ls_id;
      split_info.table_id_            = i < lob_tablet_start_idx ? table_ids.at(i) : object_id_;
      split_info.lob_table_id_        = i < lob_tablet_start_idx ? OB_INVALID_ID : table_ids.at(i);
      split_info.schema_version_      = schema_version_;
      split_info.task_id_             = task_id_;
      split_info.source_tablet_id_    = tablet_id;
      split_info.compaction_scn_      = primary_compaction_scn;
      split_info.data_format_version_ = data_format_version_;
      split_info.consumer_group_id_   = partition_split_arg_.consumer_group_id_;
      split_info.can_reuse_macro_block_ = can_reuse_macro_blocks.at(i);
      split_info.split_sstable_type_  = share::ObSplitSSTableType::SPLIT_BOTH;
      split_info.min_split_start_scn_ = min_split_start_scn_;
      const ObIArray<blocksstable::ObDatumRowkey> &parallel_datum_rowkey_list = i > 0 && i < lob_tablet_start_idx ?
          index_tablet_parallel_rowkey_list_.at(i - 1) : data_tablet_parallel_rowkey_list_;
      int64_t index = 0;
      ObArray<ObTabletID> dst_tablet_ids;
      if (OB_FAIL(get_all_dest_tablet_ids(tablet_id, dst_tablet_ids))) {
      } else if (OB_FAIL(split_info.dest_tablets_id_.assign(dst_tablet_ids))) {
      } else if (i >= lob_tablet_start_idx && OB_FAIL(split_info.lob_col_idxs_.assign(lob_col_idxs))) {
        LOG_WARN("assign failed", K(ret));
      } else if (OB_FAIL(split_info.parallel_datum_rowkey_list_.assign(parallel_datum_rowkey_list))) {
      } else if (OB_FAIL(split_info_array.push_back(split_info))) {
      }
    }
  }
  return ret;
}

int ObPartitionSplitTask::update_task_message()
{
  int ret = OB_SUCCESS;
  char *buf = nullptr;
  int64_t pos = 0;
  ObString msg;
  common::ObArenaAllocator allocator("SplitUpdMsg");
  const int64_t serialize_param_size = get_serialize_param_size();
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
  } else if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(serialize_param_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory", K(ret), K(serialize_param_size));
  } else if (OB_FAIL(serialize_params_to_message(buf, serialize_param_size, pos))) {
  } else {
    msg.assign(buf, serialize_param_size);
    if (OB_FAIL(ObDDLTaskRecordOperator::update_message(*GCTX.sql_proxy_, task_id_, msg))) {
    }
  }
  return ret;
}

void ObPartitionSplitTask::clear_old_status_context()
{
  TCWLockGuard guard(lock_);
  wait_trans_ctx_.reset();
  (void)send_finish_map_.destroy();
  write_split_log_status_inited_ = false;
}
