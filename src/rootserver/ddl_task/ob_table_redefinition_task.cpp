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
#include "ob_table_redefinition_task.h"
#include "share/rc/ob_server_runtime.h"
#include "rootserver/ob_local_ddl_serial_call.h"
#include "share/ob_ddl_error_message_table_operator.h"
#include "share/ob_ddl_sim_point.h"
#include "rootserver/ddl_task/ob_sys_ddl_util.h" // for ObSysDDLSchedulerUtil
#include "rootserver/ob_local_management_service.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::rootserver;

ObTableRedefinitionTask::ObTableRedefinitionTask()
  : ObDDLRedefinitionTask(ObDDLType::DDL_TABLE_REDEFINITION),
    has_rebuild_index_(false), has_rebuild_constraint_(false), has_rebuild_foreign_key_(false), 
    allocator_(lib::ObLabel("RedefTask")),
    is_copy_indexes_(true), is_copy_triggers_(true), is_copy_constraints_(true), is_copy_foreign_keys_(true), 
    is_ignore_errors_(false), is_do_finish_(false), use_heap_table_ddl_plan_(false),
    is_ddl_retryable_(true), has_rebuild_domain_indexes_(false)
{
}

ObTableRedefinitionTask::~ObTableRedefinitionTask()
{
}

int ObTableRedefinitionTask::init(const ObTableSchema* src_table_schema,
                                  const ObTableSchema* dst_table_schema,
                                  const int64_t parent_task_id,
                                  const int64_t task_id,
                                  const share::ObDDLType &ddl_type,
                                  const int64_t parallelism,
                                  const int32_t sub_task_trace_id,
                                  const ObAlterTableArg &alter_table_arg,
                                  const uint64_t data_format_version,
                                  const bool ddl_need_retry_at_executor,
                                  const int64_t task_status,
                                  const int64_t snapshot_version)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTableRedefinitionTask has already been inited", K(ret));
  } else if (OB_ISNULL(src_table_schema) || OB_ISNULL(dst_table_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(src_table_schema), KP(dst_table_schema));
  } else if (OB_UNLIKELY( !src_table_schema->is_valid()
                        || !dst_table_schema->is_valid() 
                        || task_id <= 0  || snapshot_version < 0 || data_format_version <= 0
                        || task_status < ObDDLTaskStatus::PREPARE || task_status > ObDDLTaskStatus::SUCCESS 
                        || (snapshot_version > 0 && task_status < ObDDLTaskStatus::WAIT_TRANS_END))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KPC(src_table_schema), KPC(dst_table_schema), K(task_id), 
                                  K(task_status), K(snapshot_version));
  } else if (OB_FAIL(deep_copy_table_arg(allocator_, alter_table_arg, alter_table_arg_))) {
    LOG_WARN("deep copy alter table arg failed", K(ret));
  } else if (OB_FAIL(set_ddl_stmt_str(alter_table_arg_.ddl_stmt_str_))) {
    LOG_WARN("set ddl stmt str failed", K(ret));
  } else {
    set_gmt_create(ObTimeUtility::current_time());
    sub_task_trace_id_ = sub_task_trace_id;
    task_type_ = ddl_type;
    object_id_ = src_table_schema->get_table_id();
    target_object_id_ = dst_table_schema->get_table_id();

    schema_version_ = dst_table_schema->get_schema_version();

    task_status_ = static_cast<ObDDLTaskStatus>(task_status);
    snapshot_version_ = snapshot_version;
    
    task_version_ = OB_TABLE_REDEFINITION_TASK_VERSION;
    parent_task_id_ = parent_task_id;
    task_id_ = task_id;
    parallelism_ = parallelism;
    data_format_version_ = data_format_version;
    start_time_ = ObTimeUtility::current_time();
    dst_schema_version_ = dst_table_schema->get_schema_version();
    
    alter_table_arg_.alter_table_schema_.set_schema_version(schema_version_);
    
    if (OB_FAIL(init_ddl_task_monitor_info(target_object_id_))) {
      LOG_WARN("init ddl task monitor info failed", K(ret));
    } else if (OB_FAIL(check_ddl_can_retry(ddl_need_retry_at_executor, dst_table_schema))) {
      LOG_WARN("check use heap table ddl plan failed", K(ret));
    } else {
      is_inited_ = true;
    }
  }

  LOG_INFO("init table redefinition task finished", K(ret), KPC(this));
  return ret;
}

int ObTableRedefinitionTask::init(const ObDDLTaskRecord &task_record)
{
  int ret = OB_SUCCESS;
  
  
  int64_t src_schema_version = 0;
  int64_t dst_schema_version = 0;
  const uint64_t data_table_id = task_record.object_id_;
  const uint64_t dest_table_id = task_record.target_object_id_;
  task_type_ = task_record.ddl_type_; // Needed before deserializing task-specific parameters.
  int64_t pos = 0;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTableRedefinitionTask has already been inited", K(ret));
  } else if (!task_record.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(task_record));
  } else if (OB_FAIL(deserialize_params_from_message(task_record.message_.ptr(), task_record.message_.length(), pos))) {
    LOG_WARN("deserialize params from message failed", K(ret), K(task_record.message_), K(common::lbt()));
  } else if (OB_FAIL(set_ddl_stmt_str(task_record.ddl_stmt_str_))) {
    LOG_WARN("set ddl stmt str failed", K(ret));
  } else if (FALSE_IT(src_schema_version = alter_table_arg_.alter_table_schema_.get_schema_version())) {
  } else if (FALSE_IT(dst_schema_version = task_record.schema_version_)) {
  } else if (OB_UNLIKELY(src_schema_version <= 0 
                      || dst_schema_version <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(task_record), K(src_schema_version), K(dst_schema_version));
  } else if (OB_UNLIKELY(src_schema_version != dst_schema_version)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(task_record), K(src_schema_version), K(dst_schema_version));
  } else {
    parent_task_id_ = task_record.parent_task_id_;
    task_id_ = task_record.task_id_;
    object_id_ = data_table_id;
    target_object_id_ = dest_table_id;
    schema_version_ = src_schema_version;
    task_status_ = static_cast<ObDDLTaskStatus>(task_record.task_status_);
    snapshot_version_ = task_record.snapshot_version_;
    execution_id_ = task_record.execution_id_;
    
    ret_code_ = task_record.ret_code_;
    start_time_ = ObTimeUtility::current_time();
    
    dst_schema_version_ = dst_schema_version;
    if (OB_FAIL(init_ddl_task_monitor_info(target_object_id_))) {
      LOG_WARN("init ddl task monitor info failed", K(ret));
    } else {
      is_inited_ = true;
    }
  }

  LOG_INFO("init table redefinition task finished", K(ret), KPC(this));
  return ret;
}

int ObTableRedefinitionTask::update_complete_sstable_job_status(const common::ObTabletID &tablet_id,
                                                                const int64_t snapshot_version,
                                                                const int64_t execution_id,
                                                                const int ret_code,
                                                                const ObDDLTaskInfo &addition_info)
{
  int ret = OB_SUCCESS;
  TCWLockGuard guard(lock_);
  UNUSED(tablet_id);
  UNUSED(addition_info);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableRedefinitionTask has not been inited", K(ret));
  } else if (OB_FAIL(DDL_SIM(task_id_, UPDATE_COMPLETE_SSTABLE_FAILED))) {
    LOG_WARN("ddl sim failure", K(task_id_));
  } else if (ObDDLTaskStatus::CHECK_TABLE_EMPTY == task_status_) {
    check_table_empty_job_ret_code_ = ret_code;
  } else {
    if (OB_UNLIKELY(snapshot_version_ != snapshot_version)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("error unexpected, snapshot version is not equal", K(ret), K(snapshot_version_), K(snapshot_version));
    } else if (execution_id < execution_id_) {
      ret = OB_TASK_EXPIRED;
      LOG_WARN("receive a mismatch execution result, ignore", K(ret_code), K(execution_id), K(execution_id_));
    } else {
      complete_sstable_job_ret_code_ = ret_code;
      execution_id_ = execution_id; // update ObTableRedefinitionTask::execution_id_ from ObDDLRedefinitionSSTableBuildTask::execution_id_
      LOG_INFO("table redefinition task callback", K(complete_sstable_job_ret_code_), K(execution_id_));
    }
  }
  return ret;
}

int ObTableRedefinitionTask::send_build_replica_request()
{
  int ret = OB_SUCCESS;
  switch (task_type_) {
    default: {
      if (OB_FAIL(send_build_replica_request_by_sql())) {
        LOG_WARN("failed to send local build request", K(ret));
      }
      break;
    }
  }
  return ret;
}

int ObTableRedefinitionTask::send_build_replica_request_by_sql()
{
  int ret = OB_SUCCESS;
  bool modify_autoinc = false;
  ObLocalManagementService *local_management_service = ::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>();
  int64_t new_execution_id = 0;
  if (OB_ISNULL(local_management_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, local management service must not be nullptr", K(ret));
  } else if (OB_FAIL(DDL_SIM(task_id_, DDL_TASK_SEND_LOCAL_BUILD_REQUEST_FAILED))) {
    LOG_WARN("ddl sim failure", K(task_id_));
  } else if (OB_FAIL(check_modify_autoinc(modify_autoinc))) {
    LOG_WARN("failed to check modify autoinc", K(ret));
  } else if (OB_FAIL(ObDDLTask::push_task_execution_id(task_id_, task_type_, is_ddl_retryable_, new_execution_id))) {
    LOG_WARN("failed to fetch new execution id", K(ret));
  } else {
    execution_id_ = new_execution_id;
    ObSQLMode sql_mode = alter_table_arg_.sql_mode_;
    if (!modify_autoinc) {
      sql_mode = sql_mode | SMO_NO_AUTO_VALUE_ON_ZERO;
    }
    ObSchemaGetterGuard schema_guard;
    const ObTableSchema *orig_table_schema = nullptr;
    const ObTableSchema *hidden_table_schema = nullptr;
    ObDDLRedefinitionSSTableBuildTask task(
        task_id_,
        object_id_,
        target_object_id_,
        schema_version_,
        snapshot_version_,
        new_execution_id,
        sql_mode,
        trace_id_,
        parallelism_,
        use_heap_table_ddl_plan_,
        ::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>(),
        data_format_version_,
        is_ddl_retryable_);
    if (OB_FAIL(local_management_service->get_ddl_service().get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
      LOG_WARN("get schema guard failed", K(ret));
    } else if (OB_FAIL(schema_guard.get_table_schema(object_id_, orig_table_schema))) {
      LOG_WARN("failed to get orig table schema", K(ret));
    } else if (OB_FAIL(schema_guard.get_table_schema(target_object_id_, hidden_table_schema))) {
      LOG_WARN("fail to get table schema", K(ret), K(target_object_id_));
    } else if (OB_FAIL(task.init(*orig_table_schema, *hidden_table_schema, alter_table_arg_.alter_table_schema_, alter_table_arg_.tz_info_wrap_))) {
      LOG_WARN("fail to init table redefinition sstable build task", K(ret));
    } else if (OB_FAIL(local_management_service->submit_ddl_local_build_task(task))) {
      LOG_WARN("fail to submit ddl local build task", K(ret));
    }
  }
  return ret;
}

int ObTableRedefinitionTask::check_build_replica_end(bool &is_end)
{
  int ret = OB_SUCCESS;
  TCWLockGuard guard(lock_);
  if (INT64_MAX == complete_sstable_job_ret_code_) {
    // not complete
  } else if (OB_SUCCESS != complete_sstable_job_ret_code_) {
    ret_code_ = complete_sstable_job_ret_code_;
    is_end = true;
    LOG_WARN("complete sstable job failed", K(ret_code_), K(object_id_), K(target_object_id_));
    if (is_local_build_need_retry(ret_code_) && is_ddl_retryable_) {
      local_build_request_time_ = 0;
      complete_sstable_job_ret_code_ = INT64_MAX;
      ret_code_ = OB_SUCCESS;
      is_end = false;
      LOG_INFO("ddl need retry", K(*this));
    }
  } else {
    is_end = true;
    ret_code_ = complete_sstable_job_ret_code_;
  }
  return ret;
}

int ObTableRedefinitionTask::check_ddl_can_retry(const bool ddl_need_retry_at_executor, const ObTableSchema *table_schema)
{
  int ret = OB_SUCCESS;
  is_ddl_retryable_ = true;
  if (OB_ISNULL(table_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(table_schema));
  } else if (OB_FAIL(check_use_heap_table_ddl_plan(table_schema))) {
    LOG_WARN("check use heap table ddl plan failed", K(ret));
  } else {
    if (ObDDLUtil::use_idempotent_mode()) {
      if (use_heap_table_ddl_plan_) {
        is_ddl_retryable_ = false;
        LOG_INFO("ddl schedule will not retry for heap table", K(use_heap_table_ddl_plan_), K_(task_id));
      } else if (ddl_need_retry_at_executor) {
        is_ddl_retryable_ = false;  // do not retry at ddl scheduler when ddl need retry at executor
        LOG_INFO("ddl schedule will not retry for ddl which will retry at table executor level", K(use_heap_table_ddl_plan_), K_(task_id));
      }
    }
  }
  return ret;
}

int ObTableRedefinitionTask::check_use_heap_table_ddl_plan(const ObTableSchema *target_table_schema)
{
  int ret = OB_SUCCESS;
  use_heap_table_ddl_plan_ = false;
  if (OB_ISNULL(target_table_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(target_table_schema));
  } else if (OB_FAIL(DDL_SIM(task_id_, TABLE_REDEF_TASK_CHECK_USE_HEAP_PLAN_FAILED))) {
    LOG_WARN("ddl sim failure", K(task_id_));
  } else if (target_table_schema->is_table_with_hidden_pk_column() &&
             (DDL_ALTER_PARTITION_BY == task_type_ || DDL_DROP_PRIMARY_KEY == task_type_)) {
    use_heap_table_ddl_plan_ = true;
  }
  return ret;
}

int ObTableRedefinitionTask::table_redefinition(const ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  bool is_local_build_end = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableRedefinitionTask has not been inited", K(ret));
  } else if (OB_UNLIKELY(snapshot_version_ <= 0)) {
    is_local_build_end = true; // switch to fail.
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected snapshot", K(ret), KPC(this));
  }

  if (OB_SUCC(ret) && !is_local_build_end && 0 == get_local_build_request_time()) {
    bool need_exec_new_inner_sql = false;
    if (OB_FAIL(reap_old_local_build_task(need_exec_new_inner_sql))) {
      if (OB_EAGAIN == ret) {
        ret = OB_SUCCESS; // retry
      } else {
        LOG_WARN("failed to reap old task", K(ret));
      }
    } else if (!need_exec_new_inner_sql) {
      is_local_build_end = true;
    } else if (OB_FAIL(send_build_replica_request())) {
      if (OB_TASK_EXPIRED == ret) {
        is_local_build_end = true;
      }
      LOG_WARN("fail to send local build request", K(ret));
    } else {
      TCWLockGuard guard(lock_);
      local_build_request_time_ = ObTimeUtility::current_time();
    }
  }
  DEBUG_SYNC(TABLE_REDEFINITION_REPLICA_BUILD);
  if (OB_SUCC(ret) && !is_local_build_end) {
    if (OB_FAIL(check_build_replica_end(is_local_build_end))) {
      LOG_WARN("check local build end failed", K(ret));
    }
  }

  // overwrite ret
  if (is_local_build_end) {
    ret = OB_SUCC(ret) ? complete_sstable_job_ret_code_ : ret;
    bool need_verify_checksum = true;
#ifdef ERRSIM
    // when the major compaction is delayed, skip verify column checksum
    need_verify_checksum = 0 == GCONF.errsim_ddl_major_delay_time;
#endif
    if (OB_SUCC(ret) && need_verify_checksum) {
      if (OB_FAIL(replica_end_check(ret))) {
        LOG_WARN("fail to check", K(ret));
      }
    }
    if (OB_FAIL(switch_status(next_task_status, true, ret))) {
      // overwrite ret
      LOG_WARN("fail to switch task status", K(ret));
    }
  }
  return ret;
}

int ObTableRedefinitionTask::replica_end_check(const int ret_code)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_data_dest_tables_columns_checksum(get_execution_id()))) {
    LOG_WARN("fail to check the columns checksum of data table and destination table", K(ret));
  }
  return ret;
}

int ObTableRedefinitionTask::copy_table_indexes()
{
  int ret = OB_SUCCESS;
  ObLocalManagementService *local_management_service = ::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableRedefinitionTask has not been inited", K(ret));
  } else if (OB_ISNULL(local_management_service)) {
    ret = OB_ERR_SYS;
    LOG_WARN("error sys, local management service must not be nullptr", K(ret));
  } else if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
  } else if (OB_FAIL(DDL_SIM(task_id_, REDEF_TASK_COPY_INDEX_FAILED))) {
    LOG_WARN("ddl sim failure", K(task_id_));
  } else {
    const int64_t MAX_ACTIVE_TASK_CNT = 1;
    int64_t active_task_cnt = 0;
    // check if has rebuild index
    if (has_rebuild_index_) {
    } else if (OB_FAIL(ObDDLTaskRecordOperator::get_create_index_task_cnt(*GCTX.sql_proxy_, target_object_id_, active_task_cnt))) {
      LOG_WARN("failed to check index task cnt", K(ret));
    } else if (active_task_cnt >= MAX_ACTIVE_TASK_CNT) {
      ret = OB_EAGAIN;
    } else {
      ObSchemaGetterGuard schema_guard;
      const ObTableSchema *table_schema = nullptr;
      ObSArray<uint64_t> index_ids;
      alter_table_arg_.ddl_task_type_ = share::REBUILD_INDEX_TASK;
      alter_table_arg_.table_id_ = object_id_;
      alter_table_arg_.hidden_table_id_ = target_object_id_;
      if (OB_FAIL(local_management_service->get_ddl_service().get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
        LOG_WARN("get schema guard failed", K(ret));
      } else if (OB_FAIL(schema_guard.get_table_schema( target_object_id_, table_schema))) {
        LOG_WARN("get table schema failed", K(ret), K(target_object_id_));
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error unexpected, table schema must not be nullptr", K(ret), K(target_object_id_));
      } else {
        const common::ObIArray<ObAuxTableMetaInfo> &index_infos = table_schema->get_simple_index_infos();
        if (index_infos.count() > 0) {
          // if there is indexes in new tables, if so, the indexes is already rebuilt in new table
          for (int64_t i = 0; OB_SUCC(ret) && i < index_infos.count(); ++i) {
            if (OB_FAIL(index_ids.push_back(index_infos.at(i).table_id_))) {
              LOG_WARN("push back index id failed", K(ret));
            }
          }
          LOG_INFO("indexes schema are already built", K(index_ids));
        } else {
          int64_t ddl_rpc_timeout = 0;
          int64_t all_tablet_count = 0;
          ObSchemaGetterGuard orig_schema_guard;
          if (OB_FAIL(local_management_service->get_ddl_service().get_runtime_schema_guard_with_version_in_inner_table(orig_schema_guard))) {
            LOG_WARN("get schema guard failed", K(ret));
          } else if (OB_FAIL(generate_rebuild_index_arg_list(object_id_, orig_schema_guard, alter_table_arg_))) {
            LOG_WARN("fail to generate rebuild index arg list", K(ret), K(object_id_));
          } else if (OB_FAIL(get_orig_all_index_tablet_count(orig_schema_guard, all_tablet_count))) {
            LOG_WARN("get all tablet count failed", K(ret));
          } else if (OB_FAIL(ObDDLUtil::get_ddl_rpc_timeout(all_tablet_count, ddl_rpc_timeout))) {
            LOG_WARN("get ddl rpc timeout failed", K(ret));
            ret = OB_INVALID_ARGUMENT;
          } else if (OB_FAIL(rootserver::local_ddl_serial_call([&]{ return ::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>()->                execute_ddl_task(alter_table_arg_, index_ids); }))) {
            LOG_WARN("rebuild hidden table index failed", K(ret), K(ddl_rpc_timeout));
          }
        }
      }
      DEBUG_SYNC(TABLE_REDEFINITION_COPY_TABLE_INDEXES);
      if (OB_SUCC(ret) && index_ids.count() > 0) {
        ObSchemaGetterGuard new_schema_guard;
        if (OB_FAIL(local_management_service->get_ddl_service().get_runtime_schema_guard_with_version_in_inner_table(new_schema_guard))) {
          LOG_WARN("failed to refresh schema guard", K(ret));
        } else if (OB_FAIL(check_and_do_sync_tablet_autoinc_seq(new_schema_guard))) {
          LOG_WARN("failed to check and do sync tablet autoinc seq", K(ret), K(task_id_));
        } else if (OB_FAIL(new_schema_guard.get_table_schema( target_object_id_, table_schema))) {
          LOG_WARN("get table schema failed", K(ret), K(target_object_id_));
        } else if (OB_ISNULL(table_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("error unexpected, table schema must not be nullptr", K(ret), K(target_object_id_));
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < index_ids.count(); ++i) {
          const uint64_t index_id = index_ids.at(i);	
          const ObTableSchema *index_schema = nullptr;
          ObDDLTaskRecord task_record;
          bool need_rebuild_index = true;
          SMART_VAR(ObCreateIndexArg, create_index_arg) {
            ObTraceIdGuard trace_id_guard(get_trace_id());
            ATOMIC_INC(&sub_task_trace_id_);
            ObDDLEventInfo ddl_event_info(GCTX.self_addr(), sub_task_trace_id_);
            if (OB_FAIL(new_schema_guard.get_table_schema( index_ids.at(i), index_schema))) {
              LOG_WARN("get table schema failed", K(ret), K(index_ids.at(i)));
            } else if (OB_ISNULL(index_schema)) {
              ret = OB_ERR_SYS;
              LOG_WARN("error sys, index schema must not be nullptr", K(ret), K(index_ids.at(i)));
            } else if (is_final_index_status(index_schema->get_index_status())) {
              // index status is final
              need_rebuild_index = false;
              LOG_INFO("index status is final", K(ret), K(task_id_), K(index_id), K(need_rebuild_index));
            } else if (index_schema->is_no_need_rebuild_index()) {
              // Only domain index need rebuild, while rebuilding vector/fulltext/multivalue index.
              need_rebuild_index = false;
            } else if (active_task_cnt >= MAX_ACTIVE_TASK_CNT) {
              ret = OB_EAGAIN;
            } else {
              ObDDLType ddl_type = get_create_index_type(data_format_version_, *index_schema);
              create_index_arg.index_type_ = index_schema->get_index_type();
              if (index_schema->is_vec_index() || index_schema->is_fts_index() || index_schema->is_multivalue_index()) {
                has_rebuild_domain_indexes_ = true;
                if (OB_FAIL(ObDDLTaskUtil::construct_domain_index_arg(new_schema_guard, table_schema, index_schema, *this, create_index_arg, ddl_type))) {
                  LOG_WARN("failed to construct domain index arg", K(ret));
                }
              }
              if (OB_FAIL(ret)) {
              } else {
                ObCreateDDLTaskParam param(ddl_type,
                                           table_schema,
                                           index_schema,
                                           0/*object_id*/,
                                           index_schema->get_schema_version(),
                                           parallelism_,
                                           &allocator_,
                                           &create_index_arg,
                                           task_id_);
                param.sub_task_trace_id_ = sub_task_trace_id_;
                param.data_format_version_ = data_format_version_;
                if (OB_FAIL(ObSysDDLSchedulerUtil::create_ddl_task(param, *GCTX.sql_proxy_, task_record))) {
                  if (OB_ENTRY_EXIST == ret) {
                    ret = OB_SUCCESS;
                    active_task_cnt += 1;
                  } else {
                    LOG_WARN("submit ddl task failed", K(ret));
                  }
                } else if (FALSE_IT(active_task_cnt += 1)) {
                } else if (OB_FAIL(ObSysDDLSchedulerUtil::schedule_ddl_task(task_record))) {
                  LOG_WARN("fail to schedule ddl task", K(ret), K(task_record));
                }
              }
            }
            if (OB_FAIL(ret)) {
              add_event_info("create table_redefinition index task fail");
              LOG_WARN("add build index task failed", K(ret), K(task_record), K(ddl_event_info));
            } else if (need_rebuild_index) {
              TCWLockGuard guard(lock_);
              const uint64_t task_key = index_ids.at(i);
              DependTaskStatus status;
              status.task_id_ = task_record.task_id_;
              if (OB_FAIL(dependent_task_result_map_.get_refactored(task_key, status))) {
                if (OB_HASH_NOT_EXIST != ret) {
                  LOG_WARN("get from dependent task map failed", K(ret));
                } else if (OB_FAIL(dependent_task_result_map_.set_refactored(task_key, status))) {
                  LOG_WARN("set dependent task map failed", K(ret), K(task_key));
                }
              }
              add_event_info("create table_redefinition index task succ");
              LOG_INFO("add build index task", K(ret), K(task_key), K(status), K(ddl_event_info));
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        has_rebuild_index_ = true;
      }
    }
  }
  return ret;
}

int ObTableRedefinitionTask::copy_table_constraints()
{
  int ret = OB_SUCCESS;
  ObLocalManagementService *local_management_service = ::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>();
  const ObTableSchema *table_schema = nullptr;
  ObSchemaGetterGuard schema_guard;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableRedefinitionTask has not been inited", K(ret));
  } else if (OB_ISNULL(local_management_service)) {
    ret = OB_ERR_SYS;
    LOG_WARN("error sys, local management service must not be nullptr", K(ret));
  } else if (OB_FAIL(DDL_SIM(task_id_, REDEF_TASK_COPY_CONSTRAINT_FAILED))) {
    LOG_WARN("ddl sim failure", K(task_id_));
  } else {
    if (has_rebuild_constraint_) {
      // do nothing
    } else {
      ObSArray<uint64_t> constraint_ids;
      ObSArray<uint64_t> new_constraint_ids;
      bool need_rebuild_constraint = true;
      if (OB_FAIL(local_management_service->get_ddl_service().get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
        LOG_WARN("get schema guard failed", K(ret));
      } else if (OB_FAIL(schema_guard.get_table_schema( target_object_id_, table_schema))) {
        LOG_WARN("get table schema failed", K(ret), K(target_object_id_));
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error unexpected, table schema must not be nullptr", K(ret), K(target_object_id_));
      } else if (OB_FAIL(check_need_rebuild_constraint(*table_schema,
                                                       new_constraint_ids,
                                                       need_rebuild_constraint))) {
        LOG_WARN("failed to check need rebuild constraint", K(ret));
      } else if (need_rebuild_constraint) {
        alter_table_arg_.ddl_task_type_ = share::REBUILD_CONSTRAINT_TASK;
        alter_table_arg_.table_id_ = object_id_;
        alter_table_arg_.hidden_table_id_ = target_object_id_;
        int64_t ddl_rpc_timeout = 0;
        if (OB_FAIL(ObDDLUtil::get_ddl_rpc_timeout_by_table(
                *GCTX.schema_service_, target_object_id_, ddl_rpc_timeout))) {
          LOG_WARN("get ddl rpc timeout fail", K(ret));
          ret = OB_INVALID_ARGUMENT;
        } else if (OB_FAIL(rootserver::local_ddl_serial_call([&]{ return ::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>()->              execute_ddl_task(alter_table_arg_, constraint_ids); }))) {
          LOG_WARN("rebuild hidden table constraint failed", K(ret), K(ddl_rpc_timeout));
        }
      } else {
        LOG_INFO("constraint has already been built");
      }
      DEBUG_SYNC(TABLE_REDEFINITION_COPY_TABLE_CONSTRAINTS);
      if (OB_SUCC(ret) && constraint_ids.count() > 0) {
        for (int64_t i = 0; OB_SUCC(ret) && i < constraint_ids.count(); ++i) {
          if (OB_FAIL(add_constraint_ddl_task(constraint_ids.at(i)))) {
            LOG_WARN("add constraint ddl task failed", K(ret));
          }
        }
      }
      if (OB_SUCC(ret) && new_constraint_ids.count() > 0) {
        for (int64_t i = 0; OB_SUCC(ret) && i < new_constraint_ids.count(); ++i) {
          if (OB_FAIL(add_constraint_ddl_task(new_constraint_ids.at(i)))) {
            LOG_WARN("add constraint ddl task failed", K(ret));
          }
        }
      }
      if (OB_SUCC(ret)) {
        has_rebuild_constraint_ = true;
      }
    }
  }
  return ret;
}

int ObTableRedefinitionTask::copy_table_foreign_keys()
{
  int ret = OB_SUCCESS;
  ObLocalManagementService *local_management_service = ::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>();
  const ObSimpleTableSchemaV2 *table_schema = nullptr;
  ObSchemaGetterGuard schema_guard;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableRedefinitionTask has not been inited", K(ret));
  } else if (OB_ISNULL(local_management_service)) {
    ret = OB_ERR_SYS;
    LOG_WARN("error sys, local management service must not be nullptr", K(ret));
  } else if (OB_FAIL(DDL_SIM(task_id_, REDEF_TASK_COPY_FOREIGN_KEY_FAILED))) {
    LOG_WARN("ddl sim failure", K(task_id_));
  } else {
    if (has_rebuild_foreign_key_) {
      // do nothing
    } else {
      if (OB_FAIL(local_management_service->get_ddl_service().get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
        LOG_WARN("get schema guard failed", K(ret));
      } else if (OB_FAIL(schema_guard.get_simple_table_schema( target_object_id_, table_schema))) {
        LOG_WARN("get table schema failed", K(ret), K(target_object_id_));
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error unexpected, table schema must not be nullptr", K(ret), K(target_object_id_));
      } else {
        const ObIArray<ObSimpleForeignKeyInfo> &fk_infos = table_schema->get_simple_foreign_key_info_array();
        ObSArray<uint64_t> fk_ids;
        LOG_INFO("get current fk infos", K(fk_infos));
        if (fk_infos.count() > 0) {
          for (int64_t i = 0; OB_SUCC(ret) && i < fk_infos.count(); ++i) {
            if (OB_FAIL(fk_ids.push_back(fk_infos.at(i).foreign_key_id_))) {
              LOG_WARN("push back fk id failed", K(ret));
            }
          }
          LOG_INFO("foreign key is already built", K(fk_infos));
        } else {
          alter_table_arg_.ddl_task_type_ = share::REBUILD_FOREIGN_KEY_TASK;
          alter_table_arg_.table_id_ = object_id_;
          alter_table_arg_.hidden_table_id_ = target_object_id_;
          int64_t ddl_rpc_timeout = 0;
          if (OB_FAIL(ObDDLUtil::get_ddl_rpc_timeout_by_table(
                  *GCTX.schema_service_, target_object_id_, ddl_rpc_timeout))) {
            LOG_WARN("get ddl rpc timeout fail", K(ret));
            ret = OB_INVALID_ARGUMENT;
          } else if (OB_FAIL(rootserver::local_ddl_serial_call([&]{ return ::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>()->                execute_ddl_task(alter_table_arg_, fk_ids); }))) {
            LOG_WARN("rebuild hidden table constraint failed", K(ret), K(ddl_rpc_timeout));
          }
        }
        DEBUG_SYNC(TABLE_REDEFINITION_COPY_TABLE_FOREIGN_KEYS);
        if (OB_SUCC(ret) && fk_ids.count() > 0) {
          for (int64_t i = 0; OB_SUCC(ret) && i < fk_ids.count(); ++i) {
            if (OB_FAIL(add_fk_ddl_task(fk_ids.at(i)))) {
              LOG_WARN("add foreign key ddl task failed", K(ret));
            }
          }
        }
        if (OB_SUCC(ret)) {
          has_rebuild_foreign_key_ = true;
        }
      }
    }
  }
  return ret;
}

int ObTableRedefinitionTask::copy_table_dependent_objects(const ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  int64_t finished_task_cnt = 0;
  bool state_finish = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableRedefinitionTask has not been inited", K(ret));
  } else if (OB_FAIL(DDL_SIM(task_id_, REDEF_TASK_COPY_DEPENDENT_OBJECTS_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(task_id_));
  } else if (!dependent_task_result_map_.created() && OB_FAIL(dependent_task_result_map_.create(MAX_DEPEND_OBJECT_COUNT, lib::ObLabel("DepTasMap")))) {
    LOG_WARN("create dependent task map failed", K(ret));
  } else {
    if (get_is_copy_indexes() && OB_FAIL(copy_table_indexes())) {
      LOG_WARN("copy table indexes failed", K(ret));
    } else if (get_is_copy_constraints() && OB_FAIL(copy_table_constraints())) {
      LOG_WARN("copy table constraints failed", K(ret));
    } else if (get_is_copy_foreign_keys() && OB_FAIL(copy_table_foreign_keys())) {
      LOG_WARN("copy table foreign keys failed", K(ret));
    } else {
      // copy triggers(at current, not supported, skip it)
    }
  }

  if (OB_FAIL(ret)) {
    state_finish = true;
  } else {
    // wait copy dependent objects to be finished
    ObAddr unused_addr;
    TCRLockGuard guard(lock_);
    for (common::hash::ObHashMap<uint64_t, DependTaskStatus>::const_iterator iter = dependent_task_result_map_.begin();
        OB_SUCC(ret) && iter != dependent_task_result_map_.end(); ++iter) {
      const uint64_t task_key = iter->first;
      const int64_t target_object_id = -1;
      const int64_t child_task_id = iter->second.task_id_;
      if (iter->second.ret_code_ == INT64_MAX) {
        // maybe ddl already finish when switching rs
        HEAP_VAR(ObDDLErrorMessageTableOperator::ObBuildDDLErrorMessage, error_message) {
          int64_t unused_user_msg_len = 0;
          if (OB_FAIL(ObDDLErrorMessageTableOperator::get_ddl_error_message(child_task_id, target_object_id,
                  unused_addr, false /* is_ddl_retry_task */, *GCTX.sql_proxy_, error_message, unused_user_msg_len))) {
            if (OB_ENTRY_NOT_EXIST == ret) {
              ret = OB_SUCCESS;
              LOG_INFO("ddl task not finish", K(task_key), K(child_task_id), K(target_object_id));
            } else {
              LOG_WARN("fail to get ddl error message", K(ret), K(task_key), K(child_task_id), K(target_object_id));
            }
          } else {
            finished_task_cnt++;
            if (error_message.ret_code_ != OB_SUCCESS) {
              ret = error_message.ret_code_;
              if (get_is_ignore_errors()) {
                ret = OB_SUCCESS;
              }
            }
          }
        }
      } else {
        finished_task_cnt++;
        if (iter->second.ret_code_ != OB_SUCCESS) {
          ret = iter->second.ret_code_;
          if (get_is_ignore_errors()) {
            ret = OB_SUCCESS;
          }
        }
      }
    }
    if (finished_task_cnt == dependent_task_result_map_.size() || OB_FAIL(ret)) {
      // 1. all child tasks finish.
      // 2. the parent task exits if any child task fails.
      state_finish = true;
    }
  }
  if (state_finish) {
    if (OB_FAIL(switch_status(next_task_status, true, ret))) {
      LOG_WARN("fail to switch status", K(ret));
    }
  }
  return ret;
}

int ObTableRedefinitionTask::take_effect(const ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
#ifdef ERRSIM
  MANAGEMENT_EVENT_ADD("ddl_task", "before_table_redefinition_task_effect",
                   "object_id", object_id_,
                   "target_object_id", target_object_id_);
  DEBUG_SYNC(BEFORE_TABLE_REDEFINITION_TASK_EFFECT);
#endif
  ObSArray<uint64_t> objs;
  int64_t ddl_rpc_timeout = 0;
  alter_table_arg_.ddl_task_type_ = share::MAKE_DDL_TAKE_EFFECT_TASK;
  alter_table_arg_.table_id_ = object_id_;
  alter_table_arg_.hidden_table_id_ = target_object_id_;
  // offline ddl is allowed on table with trigger(enable/disable).
  alter_table_arg_.need_rebuild_trigger_ = true;
  alter_table_arg_.task_id_ = task_id_;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *table_schema = nullptr;
  ObDDLTaskStatus new_status = next_task_status;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableRedefinitionTask has not been inited", K(ret));
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(DDL_SIM(task_id_, DDL_TASK_TAKE_EFFECT_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(task_id_));
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("get runtime schema guard failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema( target_object_id_, table_schema))) {
    LOG_WARN("get table schema failed", K(ret));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table schema not exist", K(ret), K(target_object_id_));
  } else if (!table_schema->is_user_hidden_table()) {
    LOG_INFO("target schema took effect", K(target_object_id_));
  } else if (table_schema->is_table_with_hidden_pk_column()
      && !(DDL_ALTER_PARTITION_BY == task_type_ || DDL_DROP_PRIMARY_KEY == task_type_)
      && OB_FAIL(sync_tablet_autoinc_seq())) {
    if (OB_TIMEOUT == ret || OB_NOT_MASTER == ret) {
      ret = OB_SUCCESS;
      new_status = ObDDLTaskStatus::TAKE_EFFECT;
    } else {
      LOG_ERROR("fail to sync tablet autoinc seq", K(ret));
    }
  } else if (OB_FAIL(sync_auto_increment_position())) {
    if (OB_NOT_MASTER == ret) {
      ret = OB_SUCCESS;
      new_status = ObDDLTaskStatus::TAKE_EFFECT;
    } else {
      LOG_WARN("sync auto increment position failed", K(ret), K(object_id_), K(target_object_id_));
    }
  } else if (OB_FAIL(sync_stats_info())) {
    if (is_stats_sync_lock_conflict(ret)) {
      delay_take_effect_after_stats_sync_lock_conflict(ret);
    } else {
      LOG_WARN("fail to sync stats info", K(ret), K(object_id_), K(target_object_id_));
    }
  } else if (OB_FAIL(ObDDLUtil::get_ddl_rpc_timeout_by_table(
                 *GCTX.schema_service_, target_object_id_, ddl_rpc_timeout))) {
            LOG_WARN("get ddl rpc timeout fail", K(ret));
  } else if (OB_FAIL(rootserver::local_ddl_serial_call([&] {
               return ::oceanbase::share::server_service<
                   ::oceanbase::rootserver::ObLocalManagementService>()->execute_ddl_task(
                       alter_table_arg_, objs);
             }))) {
    int tmp_ret = OB_SUCCESS;
    bool has_took_effect_succ = false;
    if (OB_TMP_FAIL(check_take_effect_succ(has_took_effect_succ))) {
      LOG_WARN("check took effect failed", K(ret), K(tmp_ret), K(target_object_id_));
    } else if (has_took_effect_succ) {
      ret = OB_SUCCESS;
    }
    LOG_WARN("swap orig and hidden table state failed", K(ret), K(tmp_ret), K(has_took_effect_succ), K(target_object_id_));
  }
  DEBUG_SYNC(TABLE_REDEFINITION_TAKE_EFFECT);
  if (new_status == next_task_status || OB_FAIL(ret)) {
    if (OB_FAIL(switch_status(next_task_status, true, ret))) {
      LOG_WARN("fail to switch status", K(ret));
    }
  }
  char object_id_buffer[256];
  snprintf(object_id_buffer, sizeof(object_id_buffer), "object_id:%ld, target_object_id:%ld", 
            object_id_, target_object_id_);
  MANAGEMENT_EVENT_ADD("ddl scheduler", "table redefinition task take effect",
    "ret", ret,
    K_(trace_id),
    K_(task_id),
    "object_id", object_id_buffer,
    K_(schema_version),
    "info", next_task_status);
  LOG_INFO("table redefinition task take effect", K(ret), "ddl_event_info", ObDDLEventInfo(GCTX.self_addr()), K(*this));
  return ret;
}

int ObTableRedefinitionTask::check_take_effect_succ(bool &has_took_effect_succ)
{
  int ret = OB_SUCCESS;
  has_took_effect_succ = false;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *table_schema = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableRedefinitionTask has not been inited", K(ret));
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("get runtime schema guard failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema( target_object_id_, table_schema))) {
    LOG_WARN("get table schema failed", K(ret));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table schema not exist", K(ret), K(target_object_id_));
  } else if (!table_schema->is_user_hidden_table()) {
    has_took_effect_succ = true;
    LOG_INFO("target schema took effect", K(target_object_id_));
  }
  return ret;
}

int ObTableRedefinitionTask::repending(const share::ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLRedefinitionTask has not been inited", K(ret));
  } else if (OB_FAIL(DDL_SIM(task_id_, TABLE_REDEF_TASK_REPENDING_FAILED))) {
    LOG_WARN("ddl sim failure", K(task_id_));
  } else if (OB_FAIL(switch_status(next_task_status, true, ret))) {
    LOG_WARN("fail to switch status", K(ret));
  }
  return ret;
}

bool ObTableRedefinitionTask::check_task_status_is_pending(const share::ObDDLTaskStatus task_status)
{
  return task_status == ObDDLTaskStatus::REPENDING;
}

bool ObTableRedefinitionTask::is_ddl_task_can_be_cancelled() const
{
  bool can_be_cancelled = true;
  if (has_rebuild_domain_indexes_) {
    can_be_cancelled = task_status_ != ObDDLTaskStatus::COPY_TABLE_DEPENDENT_OBJECTS;
  }
  return can_be_cancelled;
}

int ObTableRedefinitionTask::process()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableRedefinitionTask has not been inited", K(ret));
  } else if (OB_FAIL(check_health())) {
    LOG_WARN("check task health failed", K(ret));
  } else {
    switch(task_status_) {
      case ObDDLTaskStatus::PREPARE:
        if (OB_FAIL(prepare(ObDDLTaskStatus::WAIT_TRANS_END))) {
          LOG_WARN("fail to prepare table redefinition task", K(ret));
        }
        break;
      case ObDDLTaskStatus::WAIT_TRANS_END:
        if (OB_FAIL(wait_trans_end(wait_trans_ctx_, ObDDLTaskStatus::OBTAIN_SNAPSHOT))) {
          LOG_WARN("fail to wait trans end", K(ret));
        }
        break;
      case ObDDLTaskStatus::OBTAIN_SNAPSHOT:
        if (OB_FAIL(obtain_snapshot(ObDDLTaskStatus::CHECK_TABLE_EMPTY))) {
          LOG_WARN("fail to lock table", K(ret));
        }
        break;
      case ObDDLTaskStatus::CHECK_TABLE_EMPTY:
        if (OB_FAIL(check_table_empty(ObDDLTaskStatus::REPENDING))) {
          LOG_WARN("fail to check table empty", K(ret));
        }
        break;
      case ObDDLTaskStatus::REPENDING:
        if (OB_FAIL(repending(ObDDLTaskStatus::REDEFINITION))) {
          LOG_WARN("fail to repending", K(ret));
        }
        break;
      case ObDDLTaskStatus::REDEFINITION:
        if (OB_FAIL(table_redefinition(ObDDLTaskStatus::COPY_TABLE_DEPENDENT_OBJECTS))) {
          LOG_WARN("fail to do table redefinition", K(ret));
        }
        break;
      case ObDDLTaskStatus::COPY_TABLE_DEPENDENT_OBJECTS:
        if (OB_FAIL(copy_table_dependent_objects(ObDDLTaskStatus::MODIFY_AUTOINC))) {
          LOG_WARN("fail to copy table dependent objects", K(ret));
        }
        break;
      case ObDDLTaskStatus::MODIFY_AUTOINC:
        if (OB_FAIL(modify_autoinc(ObDDLTaskStatus::TAKE_EFFECT))) {
          LOG_WARN("fail to modify autoinc", K(ret));
        }
        break;
      case ObDDLTaskStatus::TAKE_EFFECT:
        if (OB_FAIL(take_effect(ObDDLTaskStatus::SUCCESS))) {
          LOG_WARN("fail to take effect", K(ret));
        }
        break;
      case ObDDLTaskStatus::FAIL:
        if (OB_FAIL(fail())) {
          LOG_WARN("fail to do clean up", K(ret));
        }
        break;
      case ObDDLTaskStatus::SUCCESS:
        if (OB_FAIL(success())) {
          LOG_WARN("fail to success", K(ret));
        }
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected table redefinition task state", K(task_status_));
        break;
    }
  }
  return ret;
}

int ObTableRedefinitionTask::check_modify_autoinc(bool &modify_autoinc)
{
  int ret = OB_SUCCESS;
  modify_autoinc = false;
  AlterTableSchema &alter_table_schema = alter_table_arg_.alter_table_schema_;
  ObTableSchema::const_column_iterator iter = alter_table_schema.column_begin();
  ObTableSchema::const_column_iterator iter_end = alter_table_schema.column_end();
  AlterColumnSchema *alter_column_schema = nullptr;
  for(; OB_SUCC(ret) && iter != iter_end; iter++) {
    if (OB_ISNULL(alter_column_schema = static_cast<AlterColumnSchema *>(*iter))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("iter is NULL", K(ret));
    } else if (alter_column_schema->is_autoincrement()) {
      modify_autoinc = true;
    }
  }
  return ret;
}

int64_t ObTableRedefinitionTask::get_serialize_param_size() const
{
  int8_t copy_indexes = static_cast<int8_t>(is_copy_indexes_);
  int8_t copy_triggers = static_cast<int8_t>(is_copy_triggers_);
  int8_t copy_constraints = static_cast<int8_t>(is_copy_constraints_);
  int8_t copy_foreign_keys = static_cast<int8_t>(is_copy_foreign_keys_);
  int8_t ignore_errors = static_cast<int8_t>(is_ignore_errors_);
  int8_t do_finish = static_cast<int8_t>(is_do_finish_);
  return alter_table_arg_.get_serialize_size() + ObDDLTask::get_serialize_param_size()
         + serialization::encoded_length_i8(copy_indexes) + serialization::encoded_length_i8(copy_triggers)
         + serialization::encoded_length_i8(copy_constraints) + serialization::encoded_length_i8(copy_foreign_keys)
         + serialization::encoded_length_i8(ignore_errors) + serialization::encoded_length_i8(do_finish)
         + serialization::encoded_length_i64(complete_sstable_job_ret_code_)
         + serialization::encoded_length_i8(use_heap_table_ddl_plan_)
         + serialization::encoded_length_i8(is_ddl_retryable_)
         + serialization::encoded_length_i8(has_rebuild_domain_indexes_);
}

int ObTableRedefinitionTask::serialize_params_to_message(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  int8_t copy_indexes = static_cast<int8_t>(is_copy_indexes_);
  int8_t copy_triggers = static_cast<int8_t>(is_copy_triggers_);
  int8_t copy_constraints = static_cast<int8_t>(is_copy_constraints_);
  int8_t copy_foreign_keys = static_cast<int8_t>(is_copy_foreign_keys_);
  int8_t ignore_errors = static_cast<int8_t>(is_ignore_errors_);
  int8_t do_finish = static_cast<int8_t>(is_do_finish_);
  if (OB_UNLIKELY(nullptr == buf || buf_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(ObDDLTask::serialize_params_to_message(buf, buf_len, pos))) {
    LOG_WARN("ObDDLTask serialize failed", K(ret));
  } else if (OB_FAIL(alter_table_arg_.serialize(buf, buf_len, pos))) {
    LOG_WARN("serialize table arg failed", K(ret));
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, copy_indexes))) {
    LOG_WARN("fail to serialize is_copy_indexes", K(ret));
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, copy_triggers))) {
    LOG_WARN("fail to serialize is_copy_triggers", K(ret));
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, copy_constraints))) {
    LOG_WARN("fail to serialize is_copy_constraints", K(ret));
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, copy_foreign_keys))) {
    LOG_WARN("fail to serialize is_copy_foreign_keys", K(ret));
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, ignore_errors))) {
    LOG_WARN("fail to serialize is_ignore_errors", K(ret));
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, do_finish))) {
    LOG_WARN("fail to serialize is_do_finish", K(ret));
  } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, pos, complete_sstable_job_ret_code_))) {
    LOG_WARN("fail to serialize complete sstable job ret code", K(ret));
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, use_heap_table_ddl_plan_))) {
    LOG_WARN("fail to serialize use heap table ddl plan", K(ret));
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, is_ddl_retryable_))) {
    LOG_WARN("fail to serialize ddl can retry", K(ret));
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, has_rebuild_domain_indexes_))) {
    LOG_WARN("fail to serialize has rebuild domain indexes", K(ret));
  }
  FLOG_INFO("serialize message for table redefinition", K(ret),
      K(copy_indexes), K(copy_triggers), K(copy_constraints), K(copy_foreign_keys), K(ignore_errors), K(do_finish), K(*this));
  return ret;
}

int ObTableRedefinitionTask::deserialize_params_from_message(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int8_t copy_indexes = 0;
  int8_t copy_triggers = 0;
  int8_t copy_constraints = 0;
  int8_t copy_foreign_keys = 0;
  int8_t ignore_errors = 0;
  int8_t do_finish = 0;
  obcall::ObAlterTableArg tmp_arg;
  if (OB_UNLIKELY(nullptr == buf || data_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(buf), K(data_len));
  } else if (OB_FAIL(ObDDLTask::deserialize_params_from_message(buf, data_len, pos))) {
    LOG_WARN("ObDDLTask deserlize failed", K(ret));
  } else if (OB_FAIL(tmp_arg.deserialize(buf, data_len, pos))) {
    LOG_WARN("serialize table failed", K(ret));
  } else if (OB_FAIL(deep_copy_table_arg(allocator_, tmp_arg, alter_table_arg_))) {
    LOG_WARN("deep copy table arg failed", K(ret));
  } else if (pos < data_len) {
    if (OB_FAIL(serialization::decode_i8(buf, data_len, pos, &copy_indexes))) {
      LOG_WARN("fail to deserialize is_copy_indexes_", K(ret));
    } else if (OB_FAIL(serialization::decode_i8(buf, data_len, pos, &copy_triggers))) {
      LOG_WARN("fail to deserialize is_copy_triggers_", K(ret));
    } else if (OB_FAIL(serialization::decode_i8(buf, data_len, pos, &copy_constraints))) {
      LOG_WARN("fail to deserialize is_copy_constraints_", K(ret));
    } else if (OB_FAIL(serialization::decode_i8(buf, data_len, pos, &copy_foreign_keys))) {
      LOG_WARN("fail to deserialize is_copy_foreign_keys_", K(ret));
    } else if (OB_FAIL(serialization::decode_i8(buf, data_len, pos, &ignore_errors))) {
      LOG_WARN("fail to deserialize is_ignore_errors_", K(ret));
    } else if (OB_FAIL(serialization::decode_i8(buf, data_len, pos, &do_finish))) {
      LOG_WARN("fail to deserialize is_do_finish_", K(ret));
    } else {
      is_copy_indexes_ = static_cast<bool>(copy_indexes);
      is_copy_triggers_ = static_cast<bool>(copy_triggers);
      is_copy_constraints_ = static_cast<bool>(copy_constraints);
      is_copy_foreign_keys_ = static_cast<bool>(copy_foreign_keys);
      is_ignore_errors_ = static_cast<bool>(ignore_errors);
      is_do_finish_ = static_cast<bool>(do_finish);
    }
    if (OB_SUCC(ret) && pos < data_len) {
      if (OB_FAIL(serialization::decode_i64(buf, data_len, pos, &complete_sstable_job_ret_code_))) {
        LOG_WARN("fail to deserialize complete sstable job ret code", K(ret));
      }
    }
    if (OB_SUCC(ret) && pos < data_len) {
      int8_t use_heap_table_ddl_plan = false;
      if (OB_FAIL(serialization::decode_i8(buf, data_len, pos, &use_heap_table_ddl_plan))) {
        LOG_WARN("fail to deserialize use heap table ddl plan", K(ret));
      } else {
        use_heap_table_ddl_plan_ = use_heap_table_ddl_plan;
      }
    }
    if (OB_SUCC(ret) && pos < data_len) {
      int8_t ddl_can_retry = false;
      if (OB_FAIL(serialization::decode_i8(buf, data_len, pos, &ddl_can_retry))) {
        LOG_WARN("fail to deserialize ddl can retry", K(ret));
      } else {
        is_ddl_retryable_ = ddl_can_retry;
      }
    }
    if (OB_SUCC(ret) && pos < data_len) {
      int8_t has_rebuild_domain_indexes = false;
      if (OB_FAIL(serialization::decode_i8(buf, data_len, pos, &has_rebuild_domain_indexes))) {
        LOG_WARN("fail to deserialize has rebuild domain indexes", K(ret));
      } else {
        has_rebuild_domain_indexes_ = has_rebuild_domain_indexes;
      }
    }
  }
  FLOG_INFO("deserialize message for table redefinition", K(ret),
      K(copy_indexes), K(copy_triggers), K(copy_constraints), K(copy_foreign_keys), K(ignore_errors), K(do_finish), K(*this));
  return ret;
}


int ObTableRedefinitionTask::collect_longops_stat(ObLongopsValue &value)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  const ObDDLTaskStatus status = static_cast<ObDDLTaskStatus>(task_status_);
  databuff_printf(stat_info_.message_, MAX_LONG_OPS_MESSAGE_LENGTH, pos, "TASK_ID: %ld, ", task_id_);
  switch (status) {
    case ObDDLTaskStatus::PREPARE: {
      if (OB_FAIL(databuff_printf(stat_info_.message_,
                                  MAX_LONG_OPS_MESSAGE_LENGTH,
                                  pos,
                                  "STATUS: PREPARE"))) {
        LOG_WARN("failed to print", K(ret));
      }
      break;
    }
    case ObDDLTaskStatus::WAIT_TRANS_END: {
      if (snapshot_version_ > 0) {
        if (OB_FAIL(databuff_printf(stat_info_.message_,
                                    MAX_LONG_OPS_MESSAGE_LENGTH,
                                    pos,
                                    "STATUS: ACQUIRE SNAPSHOT, SNAPSHOT_VERSION: %ld",
                                    snapshot_version_))) {
          LOG_WARN("failed to print", K(ret));
        }
      } else {
        if (OB_FAIL(databuff_printf(stat_info_.message_,
                                    MAX_LONG_OPS_MESSAGE_LENGTH,
                                    pos,
                                    "STATUS: WAIT TRANS END, PENDING_TX_ID: %ld",
                                    wait_trans_ctx_.get_pending_tx_id().get_id()))) {
          LOG_WARN("failed to print", K(ret));
        }
      }
      break;
    }
    case ObDDLTaskStatus::OBTAIN_SNAPSHOT: {
      if (OB_FAIL(databuff_printf(stat_info_.message_,
                                  MAX_LONG_OPS_MESSAGE_LENGTH,
                                  pos,
                                  "STATUS: OBTAIN SNAPSHOT"))) {
        LOG_WARN("failed to print", K(ret));
      }
      break;
    }
    case ObDDLTaskStatus::CHECK_TABLE_EMPTY: {
      if (OB_FAIL(databuff_printf(stat_info_.message_,
                                  MAX_LONG_OPS_MESSAGE_LENGTH,
                                  pos,
                                  "STATUS: CHECK TABLE EMPTY"))) {
        LOG_WARN("failed to print", K(ret));
      }
      break;
    }
    case ObDDLTaskStatus::REDEFINITION: {
      if (OB_FAIL(databuff_printf(stat_info_.message_,
                                  MAX_LONG_OPS_MESSAGE_LENGTH,
                                  pos,
                                  "STATUS: REDEFINITION"))) {
        LOG_WARN("failed to print", K(ret));
      }
      break;
    }
    case ObDDLTaskStatus::COPY_TABLE_DEPENDENT_OBJECTS: {
      char child_task_ids[MAX_LONG_OPS_MESSAGE_LENGTH];
      if (OB_FAIL(get_child_task_ids(child_task_ids, MAX_LONG_OPS_MESSAGE_LENGTH))) {
        if (ret == OB_SIZE_OVERFLOW) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("failed to get all child task ids", K(ret));
        }
      } else if (OB_FAIL(databuff_printf(stat_info_.message_,
                                         MAX_LONG_OPS_MESSAGE_LENGTH,
                                         pos,
                                         "STATUS: COPY DEPENDENT OBJECTS, CHILD TASK IDS: %s",
                                         child_task_ids))) {
        if (ret == OB_SIZE_OVERFLOW) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("failed to print", K(ret));
        }
      }
      break;
    }
    case ObDDLTaskStatus::MODIFY_AUTOINC: {
      if (OB_FAIL(databuff_printf(stat_info_.message_,
                                  MAX_LONG_OPS_MESSAGE_LENGTH,
                                  pos,
                                  "STATUS: MODIFY AUTOINC"))) {
        LOG_WARN("failed to print", K(ret));
      }
      break;
    }
    case ObDDLTaskStatus::TAKE_EFFECT: {
      if (OB_FAIL(databuff_printf(stat_info_.message_,
                                  MAX_LONG_OPS_MESSAGE_LENGTH,
                                  pos,
                                  "STATUS: TAKE EFFECT"))) {
        LOG_WARN("failed to print", K(ret));
      }
      break;
    }
    case ObDDLTaskStatus::REPENDING: {
      if (OB_FAIL(databuff_printf(stat_info_.message_,
                                  MAX_LONG_OPS_MESSAGE_LENGTH,
                                  pos,
                                  "STATUS: REPENDING"))) {
        LOG_WARN("failed to print", K(ret));
      }
      break;
    }
    case ObDDLTaskStatus::FAIL: {
      if (OB_FAIL(databuff_printf(stat_info_.message_,
                                  MAX_LONG_OPS_MESSAGE_LENGTH,
                                  pos,
                                  "STATUS: CLEAN ON FAIL"))) {
        LOG_WARN("failed to print", K(ret));
      }
      break;
    }
    case ObDDLTaskStatus::SUCCESS: {
      if (OB_FAIL(databuff_printf(stat_info_.message_,
                                  MAX_LONG_OPS_MESSAGE_LENGTH,
                                  pos,
                                  "STATUS: CLEAN ON SUCCESS"))) {
        LOG_WARN("failed to print", K(ret));
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("not expected status", K(ret), K(status), K(*this));
      break;
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(DDL_SIM(task_id_, DDL_TASK_COLLECT_LONGOPS_STAT_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(task_id_));
  } else if (OB_FAIL(copy_longops_stat(value))) {
    LOG_WARN("failed to collect common longops stat", K(ret));
  }
  return ret;
}
