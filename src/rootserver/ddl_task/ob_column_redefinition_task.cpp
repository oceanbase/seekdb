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
#include "ob_column_redefinition_task.h"
#include "share/rc/ob_server_runtime.h"
#include "rootserver/ob_local_ddl_serial_call.h"
#include "share/ob_ddl_error_message_table_operator.h"
#include "share/ob_ddl_sim_point.h"
#include "rootserver/ddl_task/ob_sys_ddl_util.h" // for ObSysDDLSchedulerUtil
#include "rootserver/ddl_task/ob_ddl_task_util.h"
#include "rootserver/ob_local_management_service.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::rootserver;

ObColumnRedefinitionTask::ObColumnRedefinitionTask()
  : ObDDLRedefinitionTask(ObDDLType::DDL_COLUMN_REDEFINITION), has_rebuild_index_(false), has_rebuild_constraint_(false), has_rebuild_foreign_key_(false), 
    allocator_(lib::ObLabel("RedefTask"))
{
}

ObColumnRedefinitionTask::~ObColumnRedefinitionTask()
{
}

int ObColumnRedefinitionTask::init(const int64_t task_id, const share::ObDDLType &ddl_type,
    const int64_t data_table_id, const int64_t dest_table_id, const int64_t schema_version, const int64_t parallelism,
    const int32_t sub_task_trace_id, const obcall::ObAlterTableArg &alter_table_arg,
    const uint64_t data_format_version, const int64_t task_status, const int64_t snapshot_version)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObColumnRedefinitionTask has already been inited", K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == data_table_id || OB_INVALID_ID == dest_table_id || schema_version <= 0
      || data_format_version <= 0 || task_status < ObDDLTaskStatus::PREPARE
      || task_status > ObDDLTaskStatus::SUCCESS || snapshot_version < 0 || (snapshot_version > 0 && task_status < ObDDLTaskStatus::WAIT_TRANS_END))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(task_id), K(data_table_id), K(dest_table_id), K(schema_version),
      K(data_format_version), K(task_status), K(snapshot_version));
    LOG_WARN("fail to init task table operator", K(ret));
  } else if (OB_FAIL(deep_copy_table_arg(allocator_, alter_table_arg, alter_table_arg_))) {
  } else if (OB_FAIL(set_ddl_stmt_str(alter_table_arg_.ddl_stmt_str_))) {
  } else {
    set_gmt_create(ObTimeUtility::current_time());
    task_type_ = ddl_type;
    object_id_ = data_table_id;
    target_object_id_ = dest_table_id;
    schema_version_ = schema_version;
    task_status_ = static_cast<ObDDLTaskStatus>(task_status);
    snapshot_version_ = snapshot_version;

    task_version_ = OB_COLUMN_REDEFINITION_TASK_VERSION;
    task_id_ = task_id;
    parallelism_ = parallelism;
    sub_task_trace_id_ = sub_task_trace_id;
    execution_id_ = 1L;
    start_time_ = ObTimeUtility::current_time();
    if (OB_FAIL(init_ddl_task_monitor_info(target_object_id_))) {
    } else {

      dst_schema_version_ = schema_version_;

      alter_table_arg_.alter_table_schema_.set_schema_version(schema_version_);

      data_format_version_ = data_format_version;
      is_inited_ = true;
    }
  }
  return ret;
}

int ObColumnRedefinitionTask::init(const ObDDLTaskRecord &task_record)
{
  int ret = OB_SUCCESS;
  const uint64_t data_table_id = task_record.object_id_;
  const uint64_t dest_table_id = task_record.target_object_id_;
  const int64_t schema_version = task_record.schema_version_;
  task_type_ = task_record.ddl_type_;
  int64_t pos = 0;
  const ObTableSchema *data_schema = nullptr;
  ObSchemaGetterGuard schema_guard;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObColumnRedefinitionTask has already been inited", K(ret));
  } else if (!task_record.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret));
  } else if (OB_FAIL(deserialize_params_from_message(task_record.message_.ptr(), task_record.message_.length(), pos))) {
  } else if (OB_FAIL(set_ddl_stmt_str(task_record.ddl_stmt_str_))) {
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(
          schema_guard, schema_version))) {
  } else if (OB_FAIL(schema_guard.check_formal_guard())) {
  } else if (OB_FAIL(schema_guard.get_table_schema( data_table_id, data_schema))) {
  } else if (OB_ISNULL(data_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("fail to get table schema", K(ret), K(data_schema));
  } else {
    task_id_ = task_record.task_id_;
    object_id_ = data_table_id;
    target_object_id_ = dest_table_id;
    schema_version_ = schema_version;
    task_status_ = static_cast<ObDDLTaskStatus>(task_record.task_status_);
    snapshot_version_ = task_record.snapshot_version_;
    execution_id_ = task_record.execution_id_;

    ret_code_ = task_record.ret_code_;
    start_time_ = ObTimeUtility::current_time();

    dst_schema_version_ = schema_version_;

    alter_table_arg_.alter_table_schema_.set_schema_version(schema_version_);

    if (OB_FAIL(init_ddl_task_monitor_info(target_object_id_))) {
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}


// Update the local SSTable complement status.
int ObColumnRedefinitionTask::update_complete_sstable_job_status(const common::ObTabletID &tablet_id,
                                                                 const int64_t snapshot_version,
                                                                 const int64_t execution_id,
                                                                 const int ret_code,
                                                                 const ObDDLTaskInfo &addition_info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObColumnRedefinitionTask has not been inited", K(ret));
  } else if (OB_FAIL(DDL_SIM(task_id_, UPDATE_COMPLETE_SSTABLE_FAILED))) {
  } else if (ObDDLTaskStatus::REDEFINITION != task_status_) {
    // by pass, may be network delay
  } else if (snapshot_version != snapshot_version_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("snapshot version not match", K(ret), K(snapshot_version), K(snapshot_version_));
  } else if (execution_id < execution_id_) {
    LOG_INFO("receive a mismatch execution result, ignore", K(ret_code), K(execution_id), K(execution_id_));
  } else if (OB_FAIL(local_builder_.update_build_progress(tablet_id,
                                                            ret_code,
                                                            addition_info.row_scanned_,
                                                            addition_info.row_inserted_,
                                                            addition_info.physical_row_count_))) {
  }
  return ret;
}

// Now, rebuild index table in schema and tablet.
// Next, we only rebuild index in schema and remap new index schema to old tablet by sending RPC(REMAP_INDEXES_AND_TAKE_EFFECT_TASK) to RS.
int ObColumnRedefinitionTask::copy_table_indexes()
{
  int ret = OB_SUCCESS;
  ObLocalManagementService *local_management_service = ::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObColumnRedefinitionTask has not been inited", K(ret));
  } else if (OB_ISNULL(local_management_service)) {
    ret = OB_ERR_SYS;
    LOG_WARN("error sys, local management service must not be nullptr", K(ret));
  } else if (OB_FAIL(DDL_SIM(task_id_, REDEF_TASK_COPY_INDEX_FAILED))) {
  } else {
    const int64_t MAX_ACTIVE_TASK_CNT = 1;
    int64_t active_task_cnt = 0;
    // check if has rebuild index
    if (has_rebuild_index_) {
    } else if (OB_ISNULL(GCTX.sql_proxy_) ) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(ObDDLTaskRecordOperator::get_create_index_task_cnt(*GCTX.sql_proxy_, target_object_id_, active_task_cnt))) {
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
      } else if (OB_FAIL(schema_guard.get_table_schema( target_object_id_, table_schema))) {
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error unexpected, table schema must not be nullptr", K(ret), K(target_object_id_));
      } else {
        const common::ObIArray<ObAuxTableMetaInfo> &index_infos = table_schema->get_simple_index_infos();
        if (index_infos.count() > 0) {
          // if there is indexes in new tables, if so, the indexes is already rebuilt in new table
          for (int64_t i = 0; OB_SUCC(ret) && i < index_infos.count(); ++i) {
            if (OB_FAIL(index_ids.push_back(index_infos.at(i).table_id_))) {
            }
          }
          LOG_INFO("indexes schema are already built", K(index_ids));
        } else {
          int64_t rpc_timeout = 0;
          int64_t all_tablet_count = 0;
          if (OB_FAIL(generate_rebuild_index_arg_list(object_id_, schema_guard, alter_table_arg_))) {
          } else if (OB_FAIL(get_orig_all_index_tablet_count(schema_guard, all_tablet_count))) {
          } else if (OB_FAIL(ObDDLUtil::get_ddl_rpc_timeout(all_tablet_count, rpc_timeout))) {
          } else if (OB_FAIL(rootserver::local_ddl_serial_call([&]{ return ::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>()->                execute_ddl_task(alter_table_arg_, index_ids); }))) {
          }
        }
      }
      DEBUG_SYNC(COLUMN_REDEFINITION_COPY_TABLE_INDEXES);
      if (OB_SUCC(ret) && index_ids.count() > 0) {
        ObSchemaGetterGuard new_schema_guard;
        bool has_fts_index = false;
        if (OB_FAIL(local_management_service->get_ddl_service().get_runtime_schema_guard_with_version_in_inner_table(new_schema_guard))) {
        }  else if (OB_FAIL(check_and_do_sync_tablet_autoinc_seq(new_schema_guard))) {
        } else if (OB_FAIL(new_schema_guard.get_table_schema( target_object_id_, table_schema))) {
        } else if (OB_ISNULL(table_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("error unexpected, table schema must not be nullptr", K(ret), K(target_object_id_));
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < index_ids.count(); ++i) {
          const uint64_t index_id = index_ids.at(i);	
          const ObTableSchema *index_schema = nullptr;
          ObDDLTaskRecord task_record;
          bool need_rebuild_index = true;
          SMART_VAR(obcall::ObCreateIndexArg, create_index_arg) {
            ObTraceIdGuard trace_id_guard(get_trace_id());
            ATOMIC_INC(&sub_task_trace_id_);
            ObDDLEventInfo ddl_event_info(GCTX.self_addr(), sub_task_trace_id_);
            if (OB_FAIL(new_schema_guard.get_table_schema( index_ids.at(i), index_schema))) {
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
              if ((index_schema->is_vec_index() || index_schema->is_fts_index() || index_schema->is_multivalue_index())
                  && OB_FAIL(ObDDLTaskUtil::construct_domain_index_arg(new_schema_guard, table_schema, index_schema, *this, create_index_arg, ddl_type))) {
                LOG_WARN("failed to construct domain index arg", K(ret));
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
                if (OB_FAIL(ObSysDDLSchedulerUtil::create_ddl_task(param,
                                                                   *GCTX.sql_proxy_,
                                                                   task_record))) {
                  if (OB_ENTRY_EXIST == ret) {
                    ret = OB_SUCCESS;
                    active_task_cnt += 1;
                  } else {
                    LOG_WARN("submit ddl task failed", K(ret));
                  }
                } else if (FALSE_IT(active_task_cnt += 1)) {
                } else if (OB_FAIL(ObSysDDLSchedulerUtil::schedule_ddl_task(task_record))) {
                }
              }
            }
            if (OB_FAIL(ret)) {
              add_event_info("create column redefinition index fail");
              LOG_WARN("add build index task failed", K(ret), K(ddl_event_info), K(task_record));
            } else if (need_rebuild_index) {
              TCWLockGuard guard(lock_);
              const uint64_t task_key = index_ids.at(i);
              DependTaskStatus status;
              status.task_id_ = task_record.task_id_; // child task id is used to judge whether child task finish.
              if (OB_FAIL(dependent_task_result_map_.get_refactored(task_key, status))) {
                if (OB_HASH_NOT_EXIST != ret) {
                  LOG_WARN("get from dependent task map failed", K(ret));
                } else if (OB_FAIL(dependent_task_result_map_.set_refactored(task_key, status))) {
                }
              }
              add_event_info("create column redefinition index succ");
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

// we only need to rebuild constraints in schema without pushing it into ddl scheduler.
int ObColumnRedefinitionTask::copy_table_constraints()
{
  int ret = OB_SUCCESS;
  int64_t rpc_timeout = 0;
  ObLocalManagementService *local_management_service = ::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>();
  const ObTableSchema *table_schema = nullptr;
  ObSchemaGetterGuard schema_guard;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObColumnRedefinitionTask has not been inited", K(ret));
  } else if (OB_ISNULL(local_management_service)) {
    ret = OB_ERR_SYS;
    LOG_WARN("error sys, local management service must not be nullptr", K(ret));
  } else if (OB_FAIL(DDL_SIM(task_id_, REDEF_TASK_COPY_CONSTRAINT_FAILED))) {
    LOG_WARN("ddl sim failure", K(task_id_));
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (has_rebuild_constraint_) {
      // do nothing
    } else {
      ObSArray<uint64_t> constraint_ids;
      bool need_rebuild_constraint = false;
      if (OB_FAIL(local_management_service->get_ddl_service().get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
      } else if (OB_FAIL(schema_guard.get_table_schema( target_object_id_, table_schema))) {
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error unexpected, table schema must not be nullptr", K(ret), K(target_object_id_));
      } else if (OB_FAIL(check_need_rebuild_constraint(*table_schema,
                                                       constraint_ids,
                                                       need_rebuild_constraint))) {
      } else if (need_rebuild_constraint) {
        alter_table_arg_.ddl_task_type_ = share::REBUILD_CONSTRAINT_TASK;
        alter_table_arg_.table_id_ = object_id_;
        alter_table_arg_.hidden_table_id_ = target_object_id_;
        if (OB_FAIL(ObDDLUtil::get_ddl_rpc_timeout_by_table(
                *GCTX.schema_service_, target_object_id_, rpc_timeout))) {
        } else if (OB_FAIL(rootserver::local_ddl_serial_call([&]{ return ::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>()->              execute_ddl_task(alter_table_arg_, constraint_ids); }))) {
        }
      } else {
        LOG_INFO("constraint has already been built");
      }
      DEBUG_SYNC(COLUMN_REDEFINITION_COPY_TABLE_CONSTRAINTS);
      if (OB_SUCC(ret)) {
        has_rebuild_constraint_ = true;
      }
    }
  }
  return ret;
}

int ObColumnRedefinitionTask::copy_table_foreign_keys()
{
  int ret = OB_SUCCESS;
  int64_t rpc_timeout = 0;
  ObLocalManagementService *local_management_service = ::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>();
  const ObSimpleTableSchemaV2 *table_schema = nullptr;
  ObSchemaGetterGuard schema_guard;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObColumnRedefinitionTask has not been inited", K(ret));
  } else if (OB_ISNULL(local_management_service)) {
    ret = OB_ERR_SYS;
    LOG_WARN("error sys, local management service must not be nullptr", K(ret));
  } else if (OB_FAIL(DDL_SIM(task_id_, REDEF_TASK_COPY_FOREIGN_KEY_FAILED))) {
  } else {
    if (has_rebuild_foreign_key_) {
      // do nothing
    } else {
      if (OB_FAIL(local_management_service->get_ddl_service().get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
      } else if (OB_FAIL(schema_guard.get_simple_table_schema( target_object_id_, table_schema))) {
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error unexpected, table schema must not be nullptr", K(ret), K(target_object_id_));
      } else if (table_schema->get_simple_foreign_key_info_array().empty()) {
        ObSArray<uint64_t> fk_ids;
        alter_table_arg_.ddl_task_type_ = share::REBUILD_FOREIGN_KEY_TASK;
        alter_table_arg_.table_id_ = object_id_;
        alter_table_arg_.hidden_table_id_ = target_object_id_;
        if (OB_FAIL(ObDDLUtil::get_ddl_rpc_timeout_by_table(
                *GCTX.schema_service_, target_object_id_, rpc_timeout))) {
          LOG_WARN("get ddl rpc timeout failed", K(ret));
          ret = OB_INVALID_ARGUMENT;
        } else if (OB_FAIL(rootserver::local_ddl_serial_call([&]{ return ::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>()->              execute_ddl_task(alter_table_arg_, fk_ids); }))) {
        }
        DEBUG_SYNC(COLUMN_REDEFINITION_COPY_TABLE_FOREIGN_KEYS);
        if (OB_SUCC(ret)) {
          has_rebuild_foreign_key_ = true;
        }
      } else {
        has_rebuild_foreign_key_ = true;
      }
    }
  }
  return ret;
}

int ObColumnRedefinitionTask::serialize_params_to_message(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == buf || buf_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(ObDDLTask::serialize_params_to_message(buf, buf_len, pos))) {
  } else if (OB_FAIL(alter_table_arg_.serialize(buf, buf_len, pos))) {
  }
  return ret;
}

int ObColumnRedefinitionTask::deserialize_params_from_message(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  obcall::ObAlterTableArg tmp_arg;
  if (OB_UNLIKELY(nullptr == buf || data_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(buf), K(data_len));
  } else if (OB_FAIL(ObDDLTask::deserialize_params_from_message(buf, data_len, pos))) {
  } else if (OB_FAIL(tmp_arg.deserialize(buf, data_len, pos))) {
  } else if (OB_FAIL(deep_copy_table_arg(allocator_, tmp_arg, alter_table_arg_))) {
  }
  return ret;
}

int64_t ObColumnRedefinitionTask::get_serialize_param_size() const
{
  return alter_table_arg_.get_serialize_size() + ObDDLTask::get_serialize_param_size();
}

int ObColumnRedefinitionTask::copy_table_dependent_objects(const ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  int64_t finished_task_cnt = 0;
  bool state_finish = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObColumnRedefinitionTask has not been inited", K(ret));
  } else if (OB_FAIL(DDL_SIM(task_id_, REDEF_TASK_COPY_DEPENDENT_OBJECTS_FAILED))) {
  } else if (!dependent_task_result_map_.created() && OB_FAIL(dependent_task_result_map_.create(MAX_DEPEND_OBJECT_COUNT, lib::ObLabel("DepTasMap")))) {
    LOG_WARN("create dependent task map failed", K(ret));
  } else {
    if (OB_FAIL(copy_table_indexes())) {
    } else if (OB_FAIL(copy_table_constraints())) {
    } else if (OB_FAIL(copy_table_foreign_keys())) {
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
            }
          }
        }
      } else {
        finished_task_cnt++;
        if (iter->second.ret_code_ != OB_SUCCESS) {
          ret = iter->second.ret_code_;
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
    }
  }
  return ret;
}

int ObColumnRedefinitionTask::take_effect(const ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  int64_t rpc_timeout = 0;
  ObSArray<uint64_t> objs;
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
    LOG_WARN("ObColumnRedefinitionTask has not been inited", K(ret));
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(DDL_SIM(task_id_, DDL_TASK_TAKE_EFFECT_FAILED))) {
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_table_schema( target_object_id_, table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table schema not exist", K(ret), K(target_object_id_));
  } else if (!table_schema->is_user_hidden_table()) {
    LOG_INFO("target schema took effect", K(target_object_id_));
  } else if (table_schema->is_table_with_hidden_pk_column() && OB_FAIL(sync_tablet_autoinc_seq())) {
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
                 *GCTX.schema_service_, target_object_id_, rpc_timeout))) {
  } else if (OB_FAIL(rootserver::local_ddl_serial_call([&] {
               return ::oceanbase::share::server_service<
                   ::oceanbase::rootserver::ObLocalManagementService>()->execute_ddl_task(
                       alter_table_arg_, objs);
             }))) {
    LOG_WARN("fail to swap original and hidden table state", K(ret));
    if (OB_TIMEOUT == ret) {
      ret = OB_SUCCESS;
      new_status = ObDDLTaskStatus::TAKE_EFFECT;
    }
  }
  DEBUG_SYNC(COLUMN_REDEFINITION_TAKE_EFFECT);
  if (new_status == next_task_status || OB_FAIL(ret)) {
    if (OB_FAIL(switch_status(next_task_status, true, ret))) {
    }
  }
  return ret;
}

int ObColumnRedefinitionTask::process()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObColumnRedefinitionTask has not been inited", K(ret));
  } else if (OB_FAIL(check_health())) {
  } else {
    switch(task_status_) {
      case ObDDLTaskStatus::PREPARE:
        if (OB_FAIL(prepare(ObDDLTaskStatus::WAIT_TRANS_END))) {
        }
        break;
      case ObDDLTaskStatus::WAIT_TRANS_END:
        if (OB_FAIL(wait_trans_end(wait_trans_ctx_, ObDDLTaskStatus::OBTAIN_SNAPSHOT))) {
        }
        break;
      case ObDDLTaskStatus::OBTAIN_SNAPSHOT:
        if (OB_FAIL(obtain_snapshot(ObDDLTaskStatus::REDEFINITION))) {
        }
        break;
      case ObDDLTaskStatus::REDEFINITION:
        if (OB_FAIL(wait_data_complement(ObDDLTaskStatus::COPY_TABLE_DEPENDENT_OBJECTS))) {
        }
        break;
      case ObDDLTaskStatus::COPY_TABLE_DEPENDENT_OBJECTS:
        if (OB_FAIL(copy_table_dependent_objects(ObDDLTaskStatus::TAKE_EFFECT))) {
        }
        break;
      case ObDDLTaskStatus::TAKE_EFFECT:
        if (OB_FAIL(take_effect(ObDDLTaskStatus::SUCCESS))) {
        }
        break;
      case ObDDLTaskStatus::FAIL:
        if (OB_FAIL(fail())) {
        }
        break;
      case ObDDLTaskStatus::SUCCESS:
        if (OB_FAIL(success())) {
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

int ObColumnRedefinitionTask::collect_longops_stat(ObLongopsValue &value)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  const ObDDLTaskStatus status = static_cast<ObDDLTaskStatus>(task_status_);
  databuff_printf(stat_info_.message_, MAX_LONG_OPS_MESSAGE_LENGTH, pos, "TASK_ID: %ld, ", task_id_);
  switch(status) {
    case ObDDLTaskStatus::PREPARE: {
      if (OB_FAIL(databuff_printf(stat_info_.message_,
                                  MAX_LONG_OPS_MESSAGE_LENGTH,
                                  pos,
                                  "STATUS: PREPARE"))) {
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
    case ObDDLTaskStatus::OBTAIN_SNAPSHOT: {
      if (OB_FAIL(databuff_printf(stat_info_.message_,
                                  MAX_LONG_OPS_MESSAGE_LENGTH,
                                  pos,
                                  "STATUS: OBTAIN SNAPSHOT"))) {
      }
      break;
    }
    case ObDDLTaskStatus::REDEFINITION: {
      int64_t row_inserted = 0;
      int64_t physical_row_count_ = 0;
      double percent = 0.0;
      bool initializing = false;
      {
        TCRLockGuard guard(lock_);
        initializing = !is_sstable_complete_task_submitted_;
      }
      if (initializing) {
        if (OB_FAIL(databuff_printf(stat_info_.message_,
                                    MAX_LONG_OPS_MESSAGE_LENGTH,
                                    pos,
                                    "STATUS: REPLICA BUILD, PARALLELISM: %ld, INITIALIZING",
                                    ObDDLTaskUtil::get_real_parallelism(parallelism_)))) {
        }
      } else if (OB_FAIL(local_builder_.get_progress(row_inserted, physical_row_count_, percent))) {
      } else if (OB_FAIL(databuff_printf(stat_info_.message_,
                                  MAX_LONG_OPS_MESSAGE_LENGTH,
                                  pos,
                                  "STATUS: REPLICA BUILD, PARALLELISM: %ld, ESTIMATED_TOTAL_ROWS: %ld, ROW_PROCESSED: %ld, PROGRESS: %0.2lf%%",
                                  ObDDLTaskUtil::get_real_parallelism(parallelism_),
                                  physical_row_count_,
                                  row_inserted,
                                  percent))) {
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
    case ObDDLTaskStatus::TAKE_EFFECT: {
      if (OB_FAIL(databuff_printf(stat_info_.message_,
                                  MAX_LONG_OPS_MESSAGE_LENGTH,
                                  pos,
                                  "STATUS: ENABLE INDEX"))) {
      }
      break;
    }
    case ObDDLTaskStatus::FAIL: {
      if (OB_FAIL(databuff_printf(stat_info_.message_,
                                  MAX_LONG_OPS_MESSAGE_LENGTH,
                                  pos,
                                  "STATUS: CLEAN ON FAIL"))) {
      }
      break;
    }
    case ObDDLTaskStatus::SUCCESS: {
      if (OB_FAIL(databuff_printf(stat_info_.message_,
                                  MAX_LONG_OPS_MESSAGE_LENGTH,
                                  pos,
                                  "STATUS: CLEAN ON SUCCESS"))) {
      }
      break;
    }
    default:
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("not expected status", K(ret), K(status), K(*this));
      break;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(DDL_SIM(task_id_, DDL_TASK_COLLECT_LONGOPS_STAT_FAILED))) {
  } else if (OB_FAIL(copy_longops_stat(value))) {
  }
    
  return ret;
}
