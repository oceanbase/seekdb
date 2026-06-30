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

#include "ob_rebuild_index_task.h"
#include "rootserver/ob_rs_serial_call.h"
#include "share/ob_ddl_error_message_table_operator.h"
#include "share/ob_ddl_sim_point.h"
#include "rootserver/ddl_task/ob_sys_ddl_util.h" // for ObSysDDLSchedulerUtil
#include "rootserver/ob_root_service.h"
#include "observer/omt/ob_tenant_timezone_mgr.h"               // for OTTZ_MGR
#include "share/vector_index/ob_vector_index_util.h"
#include "src/storage/ddl/ob_ddl_lock.h"
#include "share/schema/ob_mlog_info.h"
#include "share/schema/ob_mview_info.h"
#include "sql/resolver/mv/ob_mv_dep_utils.h"

using namespace oceanbase::rootserver;
using namespace oceanbase::common;
using namespace oceanbase::common::sqlclient;
using namespace oceanbase::obcall;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::sql;

ObRebuildIndexTask::ObRebuildIndexTask()
  : ObDDLTask(DDL_REBUILD_INDEX), rebuild_index_arg_(), index_build_task_id_(-1), index_drop_task_id_(-1), new_index_id_(OB_INVALID_ID), target_object_name_()
{
}

ObRebuildIndexTask::~ObRebuildIndexTask()
{
}

int ObRebuildIndexTask::init(
    const int64_t task_id,
    const share::ObDDLType &ddl_type,
    const uint64_t data_table_id,
    const uint64_t index_table_id,  // domain index table id 
    const int64_t schema_version,
    const int64_t parent_task_id,
    const int64_t consumer_group_id,
    const int32_t sub_task_trace_id,
    const int64_t parallelism,
    const uint64_t tenant_data_version,
    const ObTableSchema &index_schema,
    const obcall::ObRebuildIndexArg &rebuild_index_arg)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  ObString tmp_table_name;
  if (OB_UNLIKELY(task_id <= 0 || OB_INVALID_ID == data_table_id
      || OB_INVALID_ID == index_table_id || schema_version <= 0 || parent_task_id < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(task_id), K(data_table_id),
                                  K(index_table_id), K(schema_version), K(parent_task_id));
  } else if (OB_ISNULL(root_service_ = GCTX.root_service_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("error sys, root service is null", KR(ret));
  } else if (OB_FAIL(deep_copy_index_arg(allocator_, rebuild_index_arg, rebuild_index_arg_))) {
  } else if (OB_FALSE_IT(tmp_table_name = index_schema.get_table_name())) {
  } else if (OB_FAIL(ob_write_string(allocator_, tmp_table_name, target_object_name_))) {
  } else {
  
    object_id_ = data_table_id;
    target_object_id_ = index_table_id;
    schema_version_ = schema_version;
    task_id_ = task_id;
    task_type_ = ddl_type;
    parent_task_id_ = parent_task_id;
    consumer_group_id_ = consumer_group_id;
    sub_task_trace_id_ = sub_task_trace_id;
    task_version_ = OB_REBUILD_INDEX_TASK_VERSION;
    
    dst_schema_version_ = schema_version_;
    data_format_version_ = tenant_data_version;
    parallelism_ = parallelism;
    is_inited_ = true;
    ddl_tracing_.open();
  }
  return ret;
}

int ObRebuildIndexTask::init(
    const ObDDLTaskRecord &task_record)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (OB_UNLIKELY(!task_record.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(task_record));
  } else if (OB_ISNULL(root_service_ = GCTX.root_service_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("error sys, root service is null", KR(ret));
  } else {
  
    object_id_ = task_record.object_id_;
    target_object_id_ = task_record.target_object_id_;
    schema_version_ = task_record.schema_version_;
    task_id_ = task_record.task_id_;
    parent_task_id_ = task_record.parent_task_id_;
    task_version_ = task_record.task_version_;
    ret_code_ = task_record.ret_code_;
    
    dst_schema_version_ = schema_version_;
    task_type_ = task_record.ddl_type_;
    if (nullptr != task_record.message_.ptr()) {
      int64_t pos = 0;
      if (OB_FAIL(deserialize_params_from_message(task_record.message_.ptr(), task_record.message_.length(), pos))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else {
      is_inited_ = true;
      // set up span during recover task
      ddl_tracing_.open_for_recovery();
    }
  }
  return ret;
}

bool ObRebuildIndexTask::is_valid() const
{
  return is_inited_ && !trace_id_.is_invalid();
}

int ObRebuildIndexTask::prepare(const ObDDLTaskStatus new_status)
{
  int ret = OB_SUCCESS;
  DEBUG_SYNC(REBUILD_VEC_INDEX_PREPARE);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObRebuildIndexTask has not been inited", KR(ret));
  } else if (ObDDLTaskStatus::PREPARE != task_status_) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("task status not match", K(ret), K(task_status_));
  } else if (OB_FAIL(switch_status(new_status, true, ret))) {
  }
  return ret;
}

/*
  Drop new index table on failure, drop old index table on success.
*/
ERRSIM_POINT_DEF(ERRSIM_DROP_INDEX_IMPL_ERROR);
int ObRebuildIndexTask::drop_index_impl()
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObDatabaseSchema *database_schema = nullptr;
  const ObTableSchema *data_table_schema = nullptr;
  ObSqlString drop_index_sql;
  ObString index_name;
  const ObTableSchema *index_schema = nullptr;
  obcall::ObDropIndexArg drop_index_arg;
  if (new_index_id_ == OB_INVALID_ID) {
    index_drop_task_id_ = -1; // new index table maybe not build yet, drop nothing
    LOG_INFO("new index table not exist, maybe not build yet", K(ret), K(target_object_name_), K(new_index_id_));
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().
                                                  get_tenant_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_table_schema( target_object_id_, index_schema))) {
  } else if (OB_ISNULL(index_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("index schema is null", KR(ret), K(target_object_id_));
  } else if (OB_FAIL(schema_guard.get_database_schema( index_schema->get_database_id(), database_schema))) {
  } else if (OB_FAIL(schema_guard.get_table_schema( index_schema->get_data_table_id(), data_table_schema))) {
  } else if (OB_UNLIKELY(nullptr == database_schema || nullptr == data_table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get null schema", KR(ret), KP(database_schema), KP(data_table_schema));
  } else if (OB_FAIL(prepare_drop_index_arg(schema_guard, index_schema, database_schema, data_table_schema, drop_index_arg))) {
  } else {
    int64_t ddl_rpc_timeout = 0;
    obcall::ObDropIndexRes drop_index_res;
    if (OB_FAIL(ObDDLUtil::get_ddl_rpc_timeout(index_schema->get_all_part_num() + data_table_schema->get_all_part_num(), ddl_rpc_timeout))) {
    } else if (OB_FAIL(DDL_SIM(task_id_, DROP_INDEX_RPC_FAILED))) {
    } else if (OB_UNLIKELY(ERRSIM_DROP_INDEX_IMPL_ERROR)) {
      ret = OB_EAGAIN;
      LOG_WARN("errsim ERRSIM_DROP_INDEX_IMPL_ERROR", KR(ret));
    } else if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->drop_index(drop_index_arg, drop_index_res); }))) {
    } else {
      index_drop_task_id_ = drop_index_res.task_id_;
      LOG_INFO("success to submit drop vector index task", K(ret), 
        K(new_index_id_), K(target_object_name_), K(index_drop_task_id_), K(drop_index_arg));
    }
  }
  return ret;
}

int ObRebuildIndexTask::prepare_drop_index_arg(ObSchemaGetterGuard &schema_guard,
                                               const ObTableSchema *index_schema,
                                               const ObDatabaseSchema *database_schema,
                                               const ObTableSchema *data_table_schema,
                                               obcall::ObDropIndexArg &drop_index_arg)
{
  // we set the drop_index_arg.index_name_ as index_name is following the reason:
  // 1. In the success process, the index table and the new index table have already swapped names. 
  //    At this point, the index_name of the old index that needs to be deleted should be the new index name. 
  // 2. In the failure process, the new table needs to be deleted, and the new table and the old table have not swapped names. 
  //    At this point, the index_name is also the old table name.
  int ret = OB_SUCCESS;

  drop_index_arg.is_inner_ = true; // send to rs and set is_inner_ is true to submit drop vec index ddl task。RS need get all assistant index table to drop
  
  
  
  drop_index_arg.table_id_ = new_index_id_;           // The ID of new table 3
  drop_index_arg.index_table_id_ = target_object_id_; // The ID of old table 3
  drop_index_arg.index_name_ = target_object_name_;   // The name of old table 3
  drop_index_arg.session_id_ = data_table_schema->get_session_id();
  drop_index_arg.table_name_ = data_table_schema->get_table_name();
  drop_index_arg.database_name_ = database_schema->get_database_name_str();
  drop_index_arg.is_add_to_scheduler_ = true;
  drop_index_arg.task_id_ = task_id_; // parent task id
  drop_index_arg.is_drop_in_rebuild_task_ = true;

  switch (rebuild_index_arg_.rebuild_index_type_) {
  case ObRebuildIndexArg::RebuildIndexType::REBUILD_INDEX_TYPE_VEC:
    drop_index_arg.index_action_type_ = obcall::ObIndexArg::DROP_INDEX;
    drop_index_arg.is_vec_inner_drop_ = true;
    break;
  case ObRebuildIndexArg::RebuildIndexType::REBUILD_INDEX_TYPE_MLOG:
    drop_index_arg.index_action_type_ = obcall::ObIndexArg::DROP_MLOG;
    drop_index_arg.is_vec_inner_drop_ = false;
    break;
  default:
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid rebuild index type", KR(ret), K(rebuild_index_arg_.rebuild_index_type_));
  }

  return ret;
}

int ObRebuildIndexTask::rebuild_index()
{
  int ret = OB_SUCCESS;

  switch (rebuild_index_arg_.rebuild_index_type_) {
  case ObRebuildIndexArg::RebuildIndexType::REBUILD_INDEX_TYPE_VEC:
    if (OB_FAIL(rebuild_vec_index_impl())) {
    }
    break;
  case ObRebuildIndexArg::RebuildIndexType::REBUILD_INDEX_TYPE_MLOG:
    if (OB_FAIL(rebuild_mlog_impl())) {
    }
    break;
  default:
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid rebuild index type", KR(ret), K(rebuild_index_arg_.rebuild_index_type_));
  }

  LOG_INFO("rebuild index finished", KR(ret), K(rebuild_index_arg_.rebuild_index_type_));
  return ret;
}

int ObRebuildIndexTask::rebuild_vec_index_impl()
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  bool is_db_in_recyclebin = false;
  const ObTableSchema *table_schema = nullptr;
  const ObTableSchema *index_schema = nullptr;
  ObArenaAllocator dbms_vector_job_info_allocator;
  dbms_scheduler::ObDBMSSchedJobInfo job_info;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
  } else if (ObRebuildIndexArg::RebuildIndexType::REBUILD_INDEX_TYPE_VEC != rebuild_index_arg_.rebuild_index_type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid rebuild index type", KR(ret), K(rebuild_index_arg_.rebuild_index_type_));
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().
                                                  get_tenant_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_table_schema( target_object_id_, index_schema))) {
  } else if (OB_ISNULL(index_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("index schema is null", KR(ret), K(target_object_id_));
  } else if ((index_schema->is_vec_delta_buffer_type() || index_schema->is_hybrid_vec_index_log_type()) &&  // only hnsw index here, because ivf not support refresh 
             OB_FAIL(ObVectorIndexUtil::get_dbms_vector_job_info(*GCTX.sql_proxy_,
                                                                 index_schema->get_table_id(), 
                                                                 dbms_vector_job_info_allocator,
                                                                 schema_guard,
                                                                 job_info))) {
    LOG_WARN("fail to get dbms_vector job info", K(ret), K(index_schema->get_table_id()));
  } else if (OB_FAIL(schema_guard.get_table_schema( index_schema->get_data_table_id(), table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get null schema", KR(ret), KP(table_schema));
  } else if (table_schema->is_in_recyclebin()) {
    ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
    LOG_WARN("can not create index of table in recyclebin.", KR(ret), KPC(table_schema));
  } else if (OB_FAIL(schema_guard.check_database_in_recyclebin(table_schema->get_database_id(),
                                                               is_db_in_recyclebin))) {
  } else if (is_db_in_recyclebin) {
    ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
    LOG_WARN("Can not truncate index of db in recyclebin", KR(ret));
  } else {
    // parameters description：
    // 1. create_index_arg.index_name_
    //    The naming convention for creating the new table: assuming new index name ='idx1', the name of the new table 3 will be __idx_{datatable_id}_idx1.
    // 2. create_index_arg.index_table_id_
    //    The ID of the old table 3 is needed to find the old table schema when creating the index, in order to assign the schema to the new table.
    SMART_VAR(obcall::ObCreateIndexArg, create_index_arg) {
      obcall::ObAlterTableRes res;
      int64_t ddl_rpc_timeout = 0;
      create_index_arg.index_type_ = index_schema->get_index_type();
      create_index_arg.index_name_ = rebuild_index_arg_.index_name_;  // new index name was generated at ddl_service of rebuild_vec_index func
      create_index_arg.index_table_id_ = target_object_id_;           // old table 3 index ID;
      create_index_arg.database_name_ = rebuild_index_arg_.database_name_; 
      create_index_arg.is_rebuild_index_ = true;
      
      
      
      create_index_arg.table_name_ = table_schema->get_table_name();
      create_index_arg.index_action_type_ = obcall::ObIndexArg::ADD_INDEX;
      create_index_arg.parallelism_ = parallelism_;
      create_index_arg.is_inner_ = true;  // is ddl task inner task
      create_index_arg.task_id_ = task_id_;
      ObColumnSortItem empty_item;
      if (index_schema->is_vec_index()) {
        ObSEArray<ObString, 1> col_names;
        if (OB_FAIL(ObVectorIndexUtil::get_vector_index_column_name(*table_schema, *index_schema, col_names))) {
        } else if (!col_names.empty()) {
          empty_item.column_name_ = col_names[0];
        }
      }
      create_index_arg.index_using_type_ = USING_BTREE;
      create_index_arg.index_columns_.push_back(empty_item);
      create_index_arg.index_option_.block_size_ = 1;
      create_index_arg.index_option_.index_status_ = INDEX_STATUS_UNAVAILABLE;
      create_index_arg.index_option_.progressive_merge_num_ = 1;
      // set refresh info
      create_index_arg.vidx_refresh_info_.exec_env_ = job_info.get_exec_env();
      create_index_arg.vidx_refresh_info_.index_params_ = rebuild_index_arg_.vidx_refresh_info_.index_params_;

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(ObDDLUtil::get_ddl_rpc_timeout_by_table(target_object_id_, ddl_rpc_timeout))) {
      } else if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->create_index(create_index_arg, res); }))) {
      } else {
        index_build_task_id_ = res.task_id_;  // create vector index task ID
        new_index_id_ = res.index_table_id_;  // new table 3 index ID
        LOG_INFO("success to create rebuild index task", K(ret), K(index_build_task_id_), K(new_index_id_), K(create_index_arg));
      }
    }
  }
  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_REBUILD_MLOG_IMPL_ERROR);
int ObRebuildIndexTask::rebuild_mlog_impl()
{
  int ret = OB_SUCCESS;
  obcall::ObCreateMLogArg &create_mlog_arg = rebuild_index_arg_.create_mlog_arg_;
  obcall::ObCreateMLogRes create_mlog_res;
  int64_t ddl_rpc_timeout = 0;

  if (ObRebuildIndexArg::RebuildIndexType::REBUILD_INDEX_TYPE_MLOG != rebuild_index_arg_.rebuild_index_type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid rebuild index type", KR(ret), K(rebuild_index_arg_.rebuild_index_type_));
  } else if (OB_FAIL(ObDDLUtil::get_ddl_rpc_timeout_by_table(target_object_id_, ddl_rpc_timeout))) {
    LOG_WARN("failed to get ddl rpc timeout", K(ret));
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(ERRSIM_REBUILD_MLOG_IMPL_ERROR)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("errsim ERRSIM_REBUILD_MLOG_IMPL_ERROR", KR(ret), K(create_mlog_arg));
  } else {
    create_mlog_arg.task_id_ = task_id_;
    create_mlog_arg.create_tmp_mlog_ = true;
  }
  
  if (OB_FAIL(ret)) {
    } else if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->create_mlog(create_mlog_arg, create_mlog_res); }))) {
  } else {
    index_build_task_id_ = create_mlog_res.task_id_;
    new_index_id_ = create_mlog_res.mlog_table_id_;
    LOG_INFO("succeeded to create rebuild mlog task", K(ret), K(index_build_task_id_),
             K(new_index_id_), K(create_mlog_arg), K(create_mlog_res));
  }

  return ret;
}

int ObRebuildIndexTask::create_and_wait_rebuild_task_finish(const ObDDLTaskStatus new_status)
{
  int ret = OB_SUCCESS;
  bool state_finished = false;
  DEBUG_SYNC(REBUILD_VEC_INDEX_WAIT_CREATE_NEW_INDEX);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (ObDDLTaskStatus::REBUILD_SCHEMA != task_status_) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("task status not match", KR(ret), K(task_status_));
  } else if (-1 == index_build_task_id_ && OB_FAIL(rebuild_index())) {
    LOG_WARN("failed to rebuild index", KR(ret));
  } else if (OB_FAIL(check_ddl_task_finish(index_build_task_id_, state_finished))) {
    if (OB_CANCELED == ret) {
      index_build_task_id_ = -1;
      new_index_id_ = -1;
      LOG_INFO("create index task is canceled, reset new_index_id_", KR(ret));
    }
    LOG_WARN("check ddl task finish failed", K(ret), K(index_build_task_id_));
  } 

#ifdef ERRSIM
  if (OB_SUCC(ret)) {
    ret = OB_E(EventTable::EN_VEC_INDEX_HNSW_REBUILD_CREATE_ERR) OB_SUCCESS;
    if (OB_FAIL(ret)) {
      LOG_WARN("errsim ddl execute building the subtask of rebuild index failed", KR(ret));
    }
  }
#endif

  if (state_finished || OB_FAIL(ret)) {
    DEBUG_SYNC(REBUILD_INDEX_WAIT_CREATE_TASK_FINISH);
    (void)switch_status(new_status, true, ret);
    LOG_INFO("rebuild_index_task wait_child_task_finish finished", KR(ret), K(*this));
  }

  return ret;
}

int ObRebuildIndexTask::update_task_message(common::ObISQLClient &proxy)
{
  int ret = OB_SUCCESS;
  char *buf = nullptr;
  int64_t pos = 0;
  ObString msg;
  common::ObArenaAllocator allocator("ObVecReBuild");
  const int64_t serialize_param_size = get_serialize_param_size();

  if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(serialize_param_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory", KR(ret), K(serialize_param_size));
  } else if (OB_FAIL(serialize_params_to_message(buf, serialize_param_size, pos))) {
  } else if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
  } else {
    msg.assign(buf, serialize_param_size);
    if (OB_FAIL(ObDDLTaskRecordOperator::update_message(*GCTX.sql_proxy_, task_id_, msg))) {
    }
  }
  return ret;
}


ERRSIM_POINT_DEF(ERRSIM_PURGE_OLD_MLOG_ERROR);
int ObRebuildIndexTask::purge_old_mlog(const ObDDLTaskStatus new_status)
{
  int ret = OB_SUCCESS;
  bool state_finished = false;
  ObMultiVersionSchemaService &schema_service = root_service_->get_schema_service();
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *old_mlog_schema = nullptr;
  const ObTableSchema *new_mlog_schema = nullptr;
  const ObDatabaseSchema *db_schema = nullptr;
  const uint64_t old_mlog_tid = target_object_id_;
  const uint64_t new_mlog_tid = new_index_id_;
  ObMLogInfo old_mlog_info;
  ObMLogInfo new_mlog_info;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(ERRSIM_PURGE_OLD_MLOG_ERROR)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("errsim ERRSIM_PURGE_OLD_MLOG_ERROR", KR(ret));
  } else if (ObDDLTaskStatus::PURGE_OLD_MLOG != task_status_) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("task status not match", KR(ret), K(task_status_));
  } else if (OB_FAIL(schema_service.get_tenant_schema_guard(schema_guard))) {
  } else if (OB_FAIL(
                 schema_guard.get_table_schema( old_mlog_tid, old_mlog_schema))) {
  } else if (OB_ISNULL(old_mlog_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("mlog schema is null", K(ret), K(old_mlog_tid));
  } else if (!old_mlog_schema->is_mlog_table()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("not a mlog table", K(ret), K(old_mlog_tid));
  } else if (OB_FAIL(schema_guard.get_table_schema( new_mlog_tid, new_mlog_schema))) {
  } else if (OB_ISNULL(new_mlog_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("base table schema is null", K(ret), K(new_mlog_tid));
  } else if (OB_FAIL(schema_guard.get_database_schema( new_mlog_schema->get_database_id(), db_schema))) {
  } else if (OB_ISNULL(db_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("database not exist", KR(ret), K(new_mlog_schema->get_database_id()));
  } else if (OB_FAIL(ObMLogInfo::fetch_mlog_info(*GCTX.sql_proxy_, old_mlog_tid,
                                                 old_mlog_info, false /*for_update*/))) {
  } else if (OB_FAIL(ObMLogInfo::fetch_mlog_info(*GCTX.sql_proxy_, new_mlog_tid,
                                                 new_mlog_info, false /*for_update*/))) {
  } else if (old_mlog_info.get_last_purge_scn() >= new_mlog_info.get_last_purge_scn()) {
    // when the old_mlog_info's last_purge_scn is greater than the new_mlog_info's last_purge_scn,
    // the data in new mlog is a superset of the old mlog. we can directly return.
    state_finished = true;
  } else {
    ObSEArray<uint64_t, 4> relevent_mv_tables;
    const uint64_t base_table_id = new_mlog_schema->get_data_table_id();
    const ObTableSchema *base_table_schema = nullptr;
    common::ObCommonSqlProxy *mview_pl_proxy = GCTX.sql_proxy_;

    // 1. get all of relevant mviews of the base table
    // 2. invoke fast refresh for the mviews which have fast-refresh or on-query-computation flags
    // 3. invoke purge for the old mlog
    if (OB_ISNULL(mview_pl_proxy)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("mview pl proxy is null", K(ret));
    } else if (OB_FAIL(schema_guard.get_table_schema( base_table_id, base_table_schema))) {
    } else if (OB_ISNULL(base_table_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("base table schema is null", K(ret), K(base_table_id));
    } else if (OB_FAIL(sql::ObMVDepUtils::get_referring_mv_of_base_table(
                   *GCTX.sql_proxy_, base_table_id, relevent_mv_tables))) {
    } else {
      for (int i = 0; OB_SUCC(ret) && i < relevent_mv_tables.count(); ++i) {
        uint64_t mv_id = relevent_mv_tables.at(i);
        const ObTableSchema *mv_schema = nullptr;
        ObMViewInfo mview_info;
        if (OB_FAIL(schema_guard.get_table_schema( mv_id, mv_schema))) {
        } else if (OB_ISNULL(mv_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("mv schema is null", K(ret), K(mv_id));
        } else if (OB_FAIL(ObMViewInfo::fetch_mview_info(*GCTX.sql_proxy_, mv_id,
                                                         mview_info, false))) {
        } else if (!mview_info.is_valid()) {
          // ignore
        } else if (ObMVRefreshMethod::FAST != mview_info.get_refresh_method() &&
                   ObMVRefreshMethod::FORCE != mview_info.get_refresh_method() &&
                   !mv_schema->mv_on_query_computation()) {
          // ignore
        } else {
          ObSqlString sql;
          int64_t affected_rows = 0;
          if (OB_FAIL(sql.assign_fmt("CALL DBMS_MVIEW.refresh('%s.%s')",
                                     db_schema->get_database_name_str().ptr(),
                                     mv_schema->get_table_name_str().ptr()))) {
          } else if (OB_FAIL(mview_pl_proxy->write(sql.ptr(), affected_rows))) {
          }
        }
      }

      if (OB_FAIL(ret)) {
      } else {
        ObString purge_table_name = base_table_schema->get_table_name_str();
        ObSqlString sql;
        int64_t affected_rows = 0;
        if (base_table_schema->mv_container_table()) {
          uint64_t mview_id = OB_INVALID_ID;
          if (OB_FAIL(ObMViewInfo::get_mview_id_from_container_id(
                  *GCTX.sql_proxy_, base_table_schema->get_table_id(), mview_id))) {
          } else {
            const ObTableSchema *mview_schema = nullptr;
            if (OB_FAIL(schema_guard.get_table_schema( mview_id, mview_schema))) {
            } else if (OB_ISNULL(mview_schema)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected null container table schema", KR(ret), KP(mview_schema));
            } else {
              purge_table_name = mview_schema->get_table_name();
            }
          }
        }
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(sql.assign_fmt("CALL DBMS_MVIEW.purge_log('%s.%s') ",
                                          db_schema->get_database_name_str().ptr(),
                                          purge_table_name.ptr()))) {
        } else if (OB_FAIL(mview_pl_proxy->write(sql.ptr(), affected_rows))) {
        }
      }
    }

    if (OB_FAIL(ret)) {
    } else {
      if (OB_FAIL(ObMLogInfo::fetch_mlog_info(*GCTX.sql_proxy_, old_mlog_tid,
                                              old_mlog_info, false /*for_update*/))) {
      } else if (old_mlog_info.get_last_purge_scn() < new_mlog_info.get_last_purge_scn()) {
        // ensure old_mlog_info's last_purge_scn is greater than the new_mlog_info's last_purge_scn,
        // so we can safely go to next state.
        ret = OB_EAGAIN;
        LOG_WARN("the old mlog has not been purged, try again later");
      } else {
        state_finished = true;
      }
    }
  }

  if (state_finished || OB_FAIL(ret)) {
    DEBUG_SYNC(REBUILD_INDEX_WAIT_PURGE_OLD_MLOG);
    (void)switch_status(new_status, true, ret);
    LOG_INFO("purge_old_mlog finished", KR(ret), K(*this));
  }

  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_SWITCH_INDEX_NAME_ERROR);
int ObRebuildIndexTask::switch_index_name(const ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  bool state_finished = false;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *index_schema = nullptr;
  const ObDatabaseSchema *database_schema = NULL;
  ObDDLTaskType ddl_task_type = INVALID_TASK;
  
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (ObDDLTaskStatus::SWITCH_INDEX_NAME != task_status_) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("task status not match", KR(ret), K(task_status_));
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().
                                                  get_tenant_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_table_schema( target_object_id_, index_schema))) {
  } else if (OB_ISNULL(index_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("index schema is null", KR(ret), K(target_object_id_));
  } else if (OB_FAIL(schema_guard.get_database_schema( index_schema->get_database_id(), database_schema))) {
  } else if (OB_ISNULL(database_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("database schema should not be null", K(ret));
  } else if (OB_FAIL(get_switch_index_name_task_type(ddl_task_type))) {
  } else {
    int64_t rpc_timeout = 0;
    ObSchemaGetterGuard schema_guard;
    const ObTableSchema *table_schema = nullptr;
    ObDDLTaskStatus new_status = next_task_status;
    ObSArray<uint64_t> unused_ids;
    const ObString origin_database_name = rebuild_index_arg_.database_name_;
    const ObString origin_table_name = index_schema->get_table_name();
    ObTZMapWrap tz_map_wrap;

    SMART_VAR(obcall::ObAlterTableArg, alter_table_arg) {
      
      alter_table_arg.alter_table_schema_.set_origin_database_name(origin_database_name);
      alter_table_arg.alter_table_schema_.set_origin_table_name(origin_table_name);
      alter_table_arg.ddl_task_type_ = ddl_task_type;
      alter_table_arg.table_id_ = target_object_id_; // Old index id, the id of the old table number 3.
      alter_table_arg.hidden_table_id_ = new_index_id_; // New index id, the id of the new table number 3, obtained after rebuilding the index.
      alter_table_arg.task_id_ = task_id_;  // rebuild index task id 
      alter_table_arg.tz_info_wrap_.set_tz_info_offset(0);
      alter_table_arg.nls_formats_[ObNLSFormatEnum::NLS_DATE] = ObTimeConverter::COMPAT_OLD_NLS_DATE_FORMAT;
      alter_table_arg.nls_formats_[ObNLSFormatEnum::NLS_TIMESTAMP] = ObTimeConverter::COMPAT_OLD_NLS_TIMESTAMP_FORMAT;
      alter_table_arg.nls_formats_[ObNLSFormatEnum::NLS_TIMESTAMP_TZ] = ObTimeConverter::COMPAT_OLD_NLS_TIMESTAMP_TZ_FORMAT;
      
      alter_table_arg.compat_mode_ = lib::Worker::CompatMode::MYSQL;
      if (OB_FAIL(DDL_SIM(task_id_, DDL_TASK_SWITCH_INDEX_NAME_FAILED))) {
      } else if (OB_FAIL(OTTZ_MGR.get_tenant_tz(tz_map_wrap))) {
      } else if (FALSE_IT(alter_table_arg.set_tz_info_map(tz_map_wrap.get_tz_map()))) {
      } else if (OB_FAIL(ObDDLUtil::get_ddl_rpc_timeout_by_table(target_object_id_, rpc_timeout))) {
      } else if (OB_UNLIKELY(ERRSIM_SWITCH_INDEX_NAME_ERROR)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("errsim ERRSIM_SWITCH_INDEX_NAME_ERROR", KR(ret));
      } else if (OB_FAIL(rootserver::serial_call([&]{ return GCTX.root_service_->execute_ddl_task(alter_table_arg, unused_ids); }))) {
      } else {
        LOG_INFO("success to switch index name", K(ret), K(origin_table_name), K(alter_table_arg));
      }
      DEBUG_SYNC(REBUILD_VEC_INDEX_SWITCH_INDEX_NAME);
      if (new_status == next_task_status || OB_FAIL(ret)) {
        if (OB_FAIL(switch_status(next_task_status, true, ret))) {
        }
      }
    }
    LOG_DEBUG("switch_index_name finish", K(ret), K(task_id_), K(target_object_id_), K(new_index_id_), K(alter_table_arg));
  }
  return ret;
}

int ObRebuildIndexTask::get_switch_index_name_task_type(ObDDLTaskType &ddl_task_type)
{
  int ret = OB_SUCCESS;
  switch (rebuild_index_arg_.rebuild_index_type_) {
  case ObRebuildIndexArg::RebuildIndexType::REBUILD_INDEX_TYPE_VEC:
    ddl_task_type = SWITCH_VEC_INDEX_NAME_TASK;
    break;
  case ObRebuildIndexArg::RebuildIndexType::REBUILD_INDEX_TYPE_MLOG:
    ddl_task_type = SWITCH_MLOG_NAME_TASK;
    break;
  default:
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid rebuild index type", KR(ret), K(rebuild_index_arg_.rebuild_index_type_));
  }

  return ret;
}

int ObRebuildIndexTask::create_and_wait_drop_task_finish(const ObDDLTaskStatus new_status)
{
  int ret = OB_SUCCESS;
  DEBUG_SYNC(REBUILD_VEC_INDEX_WAIT_DROP_OLD_INDEX);
  // Although the names of the new and old indexes have been swapped, the table ID has not changed, so the old index still needs to be dropped.
  bool state_finished = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (ObDDLTaskStatus::DROP_SCHEMA != task_status_) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("task status not match", KR(ret), K(task_status_));
  } else if (-1 == index_drop_task_id_ && OB_FAIL(drop_index_impl())) {
    LOG_WARN("fail to build drop index task", K(ret));
  } else if (-1 == index_drop_task_id_) {
    state_finished = true;
    LOG_INFO("submit drop index task return task_id is -1", K(ret), K(index_drop_task_id_));
  } else if (OB_FAIL(check_ddl_task_finish(index_drop_task_id_, state_finished))) {
  }
  if (state_finished || OB_FAIL(ret)) {
    DEBUG_SYNC(REBUILD_INDEX_WAIT_DROP_TASK_FINISH);
    (void)switch_status(new_status, true, ret);
    LOG_INFO("rebuild_index_task wait_drop_task_finish finished", KR(ret), K(*this));
  }
  return ret;
}

/*
  If the DDL task is completed, the DDL task ID will be reset to zero here.
*/
int ObRebuildIndexTask::check_ddl_task_finish(int64_t &child_task_id, bool &is_finished)
{
  int ret = OB_SUCCESS;
  is_finished = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(child_task_id == OB_INVALID_ID || false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_INFO("invalid argument", K(ret), K(child_task_id));
  } else {
    int64_t unused_user_msg_len = 0;
    const int64_t target_object_id = -1;
    const ObAddr unused_addr;
    ObDDLErrorMessageTableOperator::ObBuildDDLErrorMessage error_message;
    if (OB_FAIL(ObDDLErrorMessageTableOperator::get_ddl_error_message(child_task_id,
                                                                      target_object_id,
                                                                      unused_addr,
                                                                      false /* is_ddl_retry_task */,
                                                                      *GCTX.sql_proxy_,
                                                                      error_message,
                                                                      unused_user_msg_len))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        LOG_DEBUG("ddl task not finish", K(ret), K(child_task_id), K(task_id_));
      } else {
        LOG_WARN("fail to get ddl error message", K(ret), K(child_task_id), K(task_id_));
      }
    } else {
      ret = error_message.ret_code_;
      is_finished = true;
      LOG_INFO("succ to wait task finish", K(ret));
    }
  }
  return ret;
}

int ObRebuildIndexTask::succ()
{
  return cleanup();
}

/*
  1. If the deletion of the old table fails in the above logic, 
     then wait for the completion of the old table deletion process and no need to drop new table
  2. If it is not because the deletion of the old table failed, then here triggers the drop task of the new table.
  3. If the names of the old and new tables have already been swapped, then the old table should be deleted in case of failure, 
     otherwise the new table should be deleted.
*/
int ObRebuildIndexTask::fail()
{
  int ret = OB_SUCCESS;
  bool is_finished = false;
  if (-1 == index_drop_task_id_ && OB_FAIL(drop_index_impl())) {
    LOG_WARN("drop index impl failed", KR(ret));
  } else if (-1 == index_drop_task_id_ ) {
    is_finished = true;
    LOG_INFO("submit drop index task return task_id is -1", K(ret), K(index_drop_task_id_));
  } else if (OB_FAIL(check_ddl_task_finish(index_drop_task_id_, is_finished))) {
  }
  // we need to ensure when this rebuild task is finished, there is no duplicated indexes left 
  // on the base table, so we have to wait until the drop task succeeds.
  if (is_finished) {
    if (OB_FAIL(cleanup())) {
    }
  }
  return ret;
}

int ObRebuildIndexTask::cleanup_impl()
{
  int ret = OB_SUCCESS;
  ObString unused_str;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(GCTX.sql_proxy_) || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
  } else {
    ObMultiVersionSchemaService &schema_service = *GCTX.schema_service_;
    ObSchemaGetterGuard schema_guard;
    const ObTableSchema *data_schema = nullptr;
    ObTableLockOwnerID owner_id;
    ObMySQLTransaction trans;
    const int64_t old_index_table_id = OB_INVALID_ID;
    const int64_t new_index_table_id = OB_INVALID_ID;
    const bool is_global_vector_index = false;
    if (OB_FAIL(schema_service.get_tenant_schema_guard(schema_guard))) {
    } else if (OB_FAIL(schema_guard.get_table_schema( object_id_, data_schema))) {
    } else if (OB_ISNULL(data_schema)) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("fail to get table schema", K(ret), KPC(data_schema));
    } else if (OB_FAIL(trans.start(GCTX.sql_proxy_))) {
    } else if (OB_FAIL(owner_id.convert_from_value(ObLockOwnerType::DEFAULT_OWNER_TYPE, task_id_))) {
    } else if (OB_FAIL(ObDDLLock::unlock_for_rebuild_index(*data_schema, 
                                                old_index_table_id, 
                                                new_index_table_id, 
                                                is_global_vector_index, 
                                                owner_id, 
                                                trans))) {
    }
    if (trans.is_started()) {
      int tmp_ret = trans.end(true/*commit*/);
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("trans end failed", "is_commit", OB_SUCCESS == ret, K(tmp_ret));
        ret = (OB_SUCCESS == ret) ? tmp_ret : ret;
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(report_error_code(unused_str))) {
  } else if (OB_FAIL(ObDDLTaskRecordOperator::delete_record(*GCTX.sql_proxy_, task_id_))) {
  } else {
    need_retry_ = false;     // clean succ, stop the task
  }
  if (OB_SUCC(ret) && parent_task_id_ > 0) {
    const ObDDLTaskID parent_task_id(parent_task_id_);
    ObSysDDLSchedulerUtil::on_ddl_task_finish(parent_task_id, get_task_key(), ret_code_, trace_id_);
  }
  LOG_INFO("clean task finished", KR(ret), K(*this));
  return ret;
}

int ObRebuildIndexTask::process()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObRebuildIndexTask has not been inited", KR(ret));
  } else if (!need_retry()) {
    // task is done
  } else {
    ddl_tracing_.restore_span_hierarchy();
    const ObDDLTaskStatus status = static_cast<ObDDLTaskStatus>(task_status_);
    switch (status) {
      case ObDDLTaskStatus::PREPARE:
        if (OB_FAIL(prepare(REBUILD_SCHEMA))) {
        }
        break;
      case ObDDLTaskStatus::REBUILD_SCHEMA:
        if (OB_FAIL(create_and_wait_rebuild_task_finish((rebuild_index_arg_.is_rebuild_mlog() ? PURGE_OLD_MLOG : SWITCH_INDEX_NAME)))) {
        }
        break;
      case ObDDLTaskStatus::PURGE_OLD_MLOG:
        if (OB_FAIL(purge_old_mlog(SWITCH_INDEX_NAME))) {
        }
        break;
      case ObDDLTaskStatus::SWITCH_INDEX_NAME:
        if (OB_FAIL(switch_index_name(DROP_SCHEMA))) {
        }
        break;
      case ObDDLTaskStatus::DROP_SCHEMA:
        if (OB_FAIL(create_and_wait_drop_task_finish(SUCCESS))) {
        }
        break;
      case ObDDLTaskStatus::SUCCESS:
        if (OB_FAIL(succ())) {
        }
        break;
      case ObDDLTaskStatus::FAIL:
        if (OB_FAIL(fail())) {
        }
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error unexpected, task status is not valid", KR(ret), K(task_status_));
    }
    ddl_tracing_.release_span_hierarchy();
    if (OB_FAIL(ret)) {
      add_event_info("rebuild index task process fail");
      LOG_INFO("rebuild index task process fail", "ddl_event_info", ObDDLEventInfo());
    }
  }
  return ret;
}

int ObRebuildIndexTask::deep_copy_index_arg(
    common::ObIAllocator &allocator,
    const obcall::ObRebuildIndexArg &src_index_arg,
    obcall::ObRebuildIndexArg &dst_index_arg)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  char *buf = nullptr;
  const int64_t serialize_size = src_index_arg.get_serialize_size();
  if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(serialize_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc memory failed", KR(ret), K(serialize_size));
  } else if (OB_FAIL(src_index_arg.serialize(buf, serialize_size, pos))) {
  } else if (OB_FALSE_IT(pos = 0)) {
  } else if (OB_FAIL(dst_index_arg.deserialize(buf, serialize_size, pos))) {
  }
  if (OB_FAIL(ret) && nullptr != buf) {
    allocator.free(buf);
  }

  return ret;
}

int ObRebuildIndexTask::serialize_params_to_message(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == buf || buf_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(ObDDLTask::serialize_params_to_message(buf, buf_len, pos))) {
  } else if (OB_FAIL(rebuild_index_arg_.serialize(buf, buf_len, pos))) {
  } else {
    LST_DO_CODE(OB_UNIS_ENCODE, index_build_task_id_, index_drop_task_id_, new_index_id_, target_object_name_);
  }
  return ret;
}

int ObRebuildIndexTask::deserialize_params_from_message(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  obcall::ObRebuildIndexArg tmp_rebuild_index_arg;
  if (OB_UNLIKELY(!true || nullptr == buf || data_len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), KP(buf), K(data_len));
  } else if (OB_FAIL(ObDDLTask::deserialize_params_from_message(buf, data_len, pos))) {
  } else if (OB_FAIL(tmp_rebuild_index_arg.deserialize(buf, data_len, pos))) {
  } else if (OB_FAIL(deep_copy_index_arg(allocator_, tmp_rebuild_index_arg, rebuild_index_arg_))) {
  } else {
    ObString tmp_object_name;
    LST_DO_CODE(OB_UNIS_DECODE, index_build_task_id_, index_drop_task_id_, new_index_id_, tmp_object_name);
    if (OB_FAIL(ob_write_string(allocator_, tmp_object_name, target_object_name_))) {
    }
  }
  return ret;
}

int64_t ObRebuildIndexTask::get_serialize_param_size() const
{
  int ret = OB_SUCCESS;
  int len = 0;
  len += ObDDLTask::get_serialize_param_size();
  len += rebuild_index_arg_.get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN, 
              index_build_task_id_, 
              index_drop_task_id_, 
              new_index_id_,
              target_object_name_);
  return len;
}
