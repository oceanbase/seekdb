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

#include "rootserver/fork_table/ob_fork_table_task.h"
#include "rootserver/ob_ddl_service.h"
#include "rootserver/ob_root_service.h"
#include "rootserver/ob_ddl_operator.h"
#include "share/ob_ddl_common.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "rootserver/ob_rs_async_rpc_proxy.h"
#include "share/location_cache/ob_location_service.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/ob_ddl_task_executor.h"
#include "share/ob_ddl_sim_point.h"
#include "storage/ddl/ob_ddl_lock.h"
#include "storage/tablelock/ob_lock_utils.h"
#include "storage/tablelock/ob_lock_inner_connection_util.h"
#include "lib/mysqlclient/ob_mysql_transaction.h"
#include "share/ob_share_util.h"
#include "common/ob_timeout_ctx.h"
#include "share/tablet/ob_tablet_to_ls_operator.h"
#include "rootserver/ddl_task/ob_sys_ddl_util.h"
#include "storage/ddl/ob_tablet_fork_task.h"
#include "storage/compaction/ob_schedule_dag_func.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/meta_mem/ob_tablet_handle.h"
#include "storage/tablet/ob_tablet.h"
#include "storage/ls/ob_ls.h"
#include "storage/tablet/ob_tablet_fork_mds_helper.h"
#include "share/ob_autoincrement_service.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "storage/ddl/ob_ddl_redo_log_writer.h"
#include "storage/ddl/ob_ddl_clog.h"
#include "lib/allocator/ob_allocator.h"
#include "storage/tx_storage/ob_tenant_freezer.h"
#include "share/ob_debug_sync.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
using namespace obrpc;
using namespace transaction::tablelock;

namespace rootserver
{

ObForkTableTask::ObForkTableTask()
  : ObDDLTask(),
    root_service_(nullptr),
    fork_table_arg_(),
    is_data_complement_(false)
{
}

ObForkTableTask::~ObForkTableTask()
{
}

int ObForkTableTask::init(
    const uint64_t tenant_id,
    const int64_t task_id,
    const share::ObDDLType &ddl_type,
    const share::schema::ObTableSchema *src_table_schema,
    const share::schema::ObTableSchema *dst_table_schema,
    const int64_t schema_version,
    const int64_t snapshot_version,
    const obrpc::ObForkTableArg &fork_table_arg,
    const int64_t parent_task_id)
{
  int ret = OB_SUCCESS;
  uint64_t tenant_data_version = 0;
  if (OB_UNLIKELY(OB_INVALID_ID == tenant_id
                  || task_id <= 0
                  || schema_version <= 0
                  || snapshot_version <= 0
                  || nullptr == src_table_schema
                  || nullptr == dst_table_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tenant_id), K(task_id), K(schema_version),
             K(snapshot_version),
             KP(src_table_schema), KP(dst_table_schema));
  } else if (OB_ISNULL(root_service_ = GCTX.root_service_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("root_service is null", K(ret), KP(root_service_));
  } else if (OB_FAIL(GET_MIN_DATA_VERSION(tenant_id, tenant_data_version))) {
    LOG_WARN("get min data version failed", K(ret), K(tenant_id));
  } else if (OB_UNLIKELY(tenant_data_version <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected tenant_data_version", K(ret), K(tenant_id), K(tenant_data_version));
  } else {
    set_gmt_create(ObTimeUtility::current_time());
    task_type_ = ddl_type;
    tenant_id_ = tenant_id;
    object_id_ = src_table_schema->get_table_id();
    schema_version_ = schema_version;
    task_id_ = task_id;
    parent_task_id_ = parent_task_id;
    target_object_id_ = dst_table_schema->get_table_id();
    task_version_ = 1;
    execution_id_ = -1;
    task_status_ = ObDDLTaskStatus::PREPARE;
    dst_tenant_id_ = tenant_id_;
    dst_schema_version_ = schema_version_;
    snapshot_version_ = snapshot_version;
    data_format_version_ = tenant_data_version;
    compat_mode_ = lib::Worker::CompatMode::MYSQL;

    if (OB_FAIL(deep_copy_fork_table_arg(fork_table_arg))) {
      LOG_WARN("deep copy fork table arg failed", K(ret));
    } else {
      is_data_complement_ = false;
      is_inited_ = true;
    }
  }
  return ret;
}

int ObForkTableTask::init(const ObDDLTaskRecord &task_record)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObForkTableTask has already been inited", K(ret));
  } else if (OB_ISNULL(root_service_ = GCTX.root_service_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("root_service is null", K(ret), KP(root_service_));
  } else if (!task_record.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(task_record));
  } else if (OB_FAIL(deserialize_params_from_message(task_record.tenant_id_,
                                                     task_record.message_.ptr(),
                                                     task_record.message_.length(),
                                                     pos))) {
    LOG_WARN("deserialize params from message failed", K(ret), K(task_record.message_));
  } else {
    task_type_ = task_record.ddl_type_;
    tenant_id_ = task_record.tenant_id_;
    object_id_ = task_record.object_id_;
    target_object_id_ = task_record.target_object_id_;
    schema_version_ = task_record.schema_version_;
    task_id_ = task_record.task_id_;
    parent_task_id_ = task_record.parent_task_id_;
    task_status_ = static_cast<ObDDLTaskStatus>(task_record.task_status_);
    // TODO(fankun.fan)
    snapshot_version_ = task_record.snapshot_version_;
    execution_id_ = task_record.execution_id_;
    ret_code_ = task_record.ret_code_;
    start_time_ = ObTimeUtility::current_time();
    dst_tenant_id_ = task_record.tenant_id_;
    dst_schema_version_ = schema_version_;
    compat_mode_ = lib::Worker::CompatMode::MYSQL;

    if (OB_FAIL(init_ddl_task_monitor_info(target_object_id_))) {
      LOG_WARN("init ddl task monitor info failed", K(ret));
    } else {
      is_inited_ = true;
      // set up span during recover task
      ddl_tracing_.open_for_recovery();
    }
  }

  LOG_INFO("init fork table task finished", K(ret), KPC(this));
  return ret;
}

int ObForkTableTask::process()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("fork table task not inited", K(ret));
  } else {
    LOG_DEBUG("fork table task process tick", K(task_id_), K(task_status_), "tenant_id", tenant_id_,
        "src_table_id", object_id_, "dst_table_id", target_object_id_);
    switch (task_status_) {
      case ObDDLTaskStatus::PREPARE: {
        DEBUG_SYNC(FORK_TABLE_PREPARE);
        const ObDDLTaskStatus from_status = task_status_;
        const ObDDLTaskStatus to_status = ObDDLTaskStatus::WAIT_FROZE_END;
        if (OB_FAIL(switch_status(to_status, true, ret))) {
          LOG_WARN("fail to switch task status", K(ret));
        } else {
          LOG_INFO("fork table stage enter", K(task_id_),
              "from", ddl_task_status_to_str(from_status),
              "to", ddl_task_status_to_str(to_status),
              "tenant_id", tenant_id_,
              "src_table_id", object_id_,
              "dst_table_id", target_object_id_,
              "snapshot_version", snapshot_version_);
        }
        break;
      }
      case ObDDLTaskStatus::WAIT_FROZE_END: {
        DEBUG_SYNC(FORK_TABLE_WAIT_FREEZE_END);
        const ObDDLTaskStatus from_status = task_status_;
        if (OB_FAIL(wait_freeze_end(ObDDLTaskStatus::BUILD_DATA))) {
          LOG_WARN("fail to wait freeze end", K(ret));
        } else if (task_status_ == ObDDLTaskStatus::BUILD_DATA) {
          LOG_INFO("fork table stage enter", K(task_id_),
              "from", ddl_task_status_to_str(from_status),
              "to", ddl_task_status_to_str(task_status_),
              "tenant_id", tenant_id_,
              "src_table_id", object_id_,
              "dst_table_id", target_object_id_,
              "snapshot_version", snapshot_version_);
        }
        break;
      }
      case ObDDLTaskStatus::BUILD_DATA: {
        DEBUG_SYNC(FORK_TABLE_BUILD_DATA);
        const ObDDLTaskStatus from_status = task_status_;
        if (OB_FAIL(build_data(ObDDLTaskStatus::WAIT_DATA_COMPLEMENT))) {
          LOG_WARN("fail to build data", K(ret));
        } else if (task_status_ == ObDDLTaskStatus::WAIT_DATA_COMPLEMENT) {
          LOG_INFO("fork table stage enter", K(task_id_),
              "from", ddl_task_status_to_str(from_status),
              "to", ddl_task_status_to_str(task_status_),
              "tenant_id", tenant_id_,
              "src_table_id", object_id_,
              "dst_table_id", target_object_id_,
              "snapshot_version", snapshot_version_);
        }
        break;
      }
      case ObDDLTaskStatus::WAIT_DATA_COMPLEMENT: {
        DEBUG_SYNC(FORK_TABLE_WAIT_DATA_COMPLEMENT);
        const ObDDLTaskStatus from_status = task_status_;
        if (OB_FAIL(wait_data_complement(ObDDLTaskStatus::SUCCESS))) {
          if (OB_EAGAIN != ret) {
            LOG_WARN("wait data build complete failed", K(ret));
          }
        } else if (task_status_ == ObDDLTaskStatus::SUCCESS) {
          LOG_INFO("fork table stage enter", K(task_id_),
              "from", ddl_task_status_to_str(from_status),
              "to", ddl_task_status_to_str(task_status_),
              "tenant_id", tenant_id_,
              "src_table_id", object_id_,
              "dst_table_id", target_object_id_,
              "snapshot_version", snapshot_version_);
        }
        break;
      }
      case ObDDLTaskStatus::SUCCESS: {
        DEBUG_SYNC(FORK_TABLE_SUCCESS);
        if (OB_FAIL(succ())) {
          LOG_WARN("fail to do clean up", K(ret));
        }
        break;
      }
      case ObDDLTaskStatus::FAIL: {
        if (OB_FAIL(fail())) {
          LOG_WARN("fail to do clean up", K(ret));
        }
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected task status", K(ret), K(task_status_));
        break;
      }
    }
  }
  return ret;
}

bool ObForkTableTask::is_valid() const
{
  return is_inited_ && !trace_id_.is_invalid();
}

int ObForkTableTask::serialize_params_to_message(char *buf, const int64_t buf_size, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == buf || buf_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(buf), K(buf_size));
  } else if (OB_FAIL(ObDDLTask::serialize_params_to_message(buf, buf_size, pos))) {
    LOG_WARN("serialize ddl task failed", K(ret));
  } else if (OB_FAIL(serialization::encode_bool(buf, buf_size, pos, is_data_complement_))) {
    LOG_WARN("serialize is_data_complement failed", K(ret));
  } else if (OB_FAIL(fork_table_arg_.serialize(buf, buf_size, pos))) {
    LOG_WARN("serialize fork_table_arg failed", K(ret));
  }
  return ret;
}

int ObForkTableTask::deserialize_params_from_message(const uint64_t tenant_id, const char *buf, const int64_t buf_size, int64_t &pos)
{
  int ret = OB_SUCCESS;
  SMART_VAR(obrpc::ObForkTableArg, tmp_arg) {
    if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id) || nullptr == buf || buf_size <= 0)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid arguments", K(ret), K(tenant_id), KP(buf), K(buf_size));
    } else if (OB_FAIL(ObDDLTask::deserialize_params_from_message(tenant_id, buf, buf_size, pos))) {
      LOG_WARN("deserialize ddl task failed", K(ret));
    } else if (OB_FAIL(serialization::decode_bool(buf, buf_size, pos, &is_data_complement_))) {
      LOG_WARN("deserialize is_data_complement failed", K(ret));
    } else if (OB_FAIL(tmp_arg.deserialize(buf, buf_size, pos))) {
      LOG_WARN("deserialize fork_table_arg failed", K(ret));
    } else if (OB_FAIL(ObDDLUtil::replace_user_tenant_id(tenant_id, tmp_arg))) {
      LOG_WARN("replace user tenant id failed", K(ret), K(tenant_id), K(tmp_arg));
    } else if (OB_FAIL(deep_copy_fork_table_arg(tmp_arg))) {
      LOG_WARN("deep copy fork_table_arg failed", K(ret));
    }
  }
  return ret;
}

int64_t ObForkTableTask::get_serialize_param_size() const
{
  int64_t len = ObDDLTask::get_serialize_param_size();
  len += serialization::encoded_length_bool(is_data_complement_);
  len += fork_table_arg_.get_serialize_size();
  return len;
}

int ObForkTableTask::deep_copy_fork_table_arg(const obrpc::ObForkTableArg &arg)
{
  int ret = OB_SUCCESS;
  fork_table_arg_.tenant_id_ = arg.tenant_id_;
  if (OB_FAIL(ob_write_string(allocator_, arg.src_database_name_, fork_table_arg_.src_database_name_))) {
    LOG_WARN("deep copy src database name failed", K(ret));
  } else if (OB_FAIL(ob_write_string(allocator_, arg.src_table_name_, fork_table_arg_.src_table_name_))) {
    LOG_WARN("deep copy src table name failed", K(ret));
  } else if (OB_FAIL(ob_write_string(allocator_, arg.dst_database_name_, fork_table_arg_.dst_database_name_))) {
    LOG_WARN("deep copy dst database name failed", K(ret));
  } else if (OB_FAIL(ob_write_string(allocator_, arg.dst_table_name_, fork_table_arg_.dst_table_name_))) {
    LOG_WARN("deep copy dst table name failed", K(ret));
  } else {
    fork_table_arg_.if_not_exist_ = arg.if_not_exist_;
    fork_table_arg_.session_id_ = arg.session_id_;
  }
  return ret;
}

int ObForkTableTask::wait_freeze_end(const ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  ObSEArray<ObTabletID, 4> src_tablet_ids;
  const int64_t start_ts = ObTimeUtility::current_time();

  if (OB_FAIL(get_schema_guard(schema_guard))) {
    LOG_WARN("fail to get schema guard", K(ret));
  } else if (OB_FAIL(ObForkTableUtil::collect_tablet_ids_from_table(schema_guard, tenant_id_, object_id_, src_tablet_ids))) {
    LOG_WARN("fail to get src tablet ids", K(ret));
  } else if (OB_FAIL(storage::ObTabletForkUtil::freeze_tablets(SYS_LS, src_tablet_ids))) {
    LOG_WARN("fail to freeze tablets", K(ret), K(SYS_LS), K(src_tablet_ids));
  }

  if (OB_SUCC(ret)) {
    ObTableForkFreezeLog freeze_log;
    SCN free_log_scn;
    if (OB_FAIL(freeze_log.tablet_ids_.assign(src_tablet_ids))) {
      LOG_WARN("fail to assign source tablet ids", K(ret));
    } else if (OB_UNLIKELY(!freeze_log.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid fork freeze log", K(ret), K(freeze_log));
    } else if (OB_FAIL(ObDDLRedoLogWriter::write_auto_fork_log(SYS_LS,
                                                               ObDDLClogType::DDL_TABLE_FORK_FREEZE_LOG,
                                                               logservice::ObReplayBarrierType::NO_NEED_BARRIER,
                                                               freeze_log,
                                                               free_log_scn))) {
      LOG_WARN("fail to write table fork freeze log", K(ret), K(freeze_log));
    } else {
      LOG_INFO("fork table freeze stage done", K(task_id_), K(SYS_LS), K(free_log_scn),
          "src_table_id", object_id_, "tablet_cnt", src_tablet_ids.count(),
          "cost_us", ObTimeUtility::current_time() - start_ts);
      LOG_DEBUG("fork table freeze log detail", K(task_id_), K(freeze_log));
    }
  }

  if (OB_SUCC(ret) && OB_FAIL(switch_status(next_task_status, true, ret))) {
    LOG_WARN("fail to switch task status", K(ret));
  }

  return ret;
}

int ObForkTableTask::build_data(const ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  ObSEArray<ObTabletID, 4> src_tablet_ids;
  ObSEArray<ObTabletID, 4> dst_tablet_ids;
  storage::ObTableForkInfo fork_info;
  ObTableForkStartLog start_log;
  SCN start_log_scn;
  const int64_t start_ts = ObTimeUtility::current_time();

  if (OB_FAIL(get_schema_guard(schema_guard))) {
    LOG_WARN("fail to get schema guard", K(ret));
  } else if (OB_FAIL(ObForkTableUtil::collect_tablet_ids_from_table(schema_guard, tenant_id_, object_id_, src_tablet_ids))) {
    LOG_WARN("fail to get src tablet ids", K(ret));
  } else if (OB_FAIL(ObForkTableUtil::collect_tablet_ids_from_table(schema_guard, tenant_id_, target_object_id_, dst_tablet_ids))) {
    LOG_WARN("fail to get dst tablet ids", K(ret));
  } else if (src_tablet_ids.count() != dst_tablet_ids.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet count mismatch", K(ret),
             K(src_tablet_ids.count()), K(dst_tablet_ids.count()));
  } else if (OB_FAIL(build_fork_info(src_tablet_ids, dst_tablet_ids, fork_info))) {
    LOG_WARN("fail to build fork info", K(ret));
  } else if (OB_FAIL(start_log.fork_info_.assign(fork_info))) {
    LOG_WARN("fail to assign fork info", K(ret));
  } else if (OB_FAIL(storage::ObTabletForkUtil::try_schedule_fork_dags(fork_info))) {
    LOG_WARN("fail to try schedule fork dags", K(ret));
  } else if (OB_FAIL(ObDDLRedoLogWriter::write_auto_fork_log(SYS_LS,
                                                             ObDDLClogType::DDL_TABLE_FORK_START_LOG,
                                                             logservice::ObReplayBarrierType::PRE_BARRIER,
                                                             start_log,
                                                             start_log_scn))) {
    LOG_WARN("fail to write table fork start log", K(ret), K(start_log));
  } else {
    LOG_INFO("fork table build_data stage started", K(task_id_), K(SYS_LS), K(start_log_scn),
        "src_table_id", object_id_, "dst_table_id", target_object_id_,
        "tablet_cnt", src_tablet_ids.count(),
        "cost_us", ObTimeUtility::current_time() - start_ts);
    LOG_DEBUG("fork table start log detail", K(task_id_), K(start_log));
  }

  if (OB_SUCC(ret) && OB_FAIL(switch_status(next_task_status, true, ret))) {
    LOG_WARN("fail to switch task status", K(ret));
  }

  return ret;
}

int ObForkTableTask::wait_data_complement(const ObDDLTaskStatus next_task_status)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *dst_table_schema = nullptr;
  ObSEArray<ObTabletID, 4> dst_tablet_ids;
  const int64_t start_ts = ObTimeUtility::current_time();

  if (OB_FAIL(get_schema_guard(schema_guard))) {
    LOG_WARN("fail to get schema guard", K(ret));
  } else if (is_data_complement_) {
    LOG_DEBUG("data complement already completed, skip waiting", K(task_id_), K(target_object_id_));
  } else if (OB_FAIL(schema_guard.get_table_schema(tenant_id_, target_object_id_, dst_table_schema))) {
    LOG_WARN("fail to get destination table schema", K(ret));
  } else if (OB_ISNULL(dst_table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("destination table not exist", K(ret), K_(target_object_id));
  } else if (OB_FAIL(share::ObForkTableUtil::collect_tablet_ids_from_table(schema_guard, tenant_id_, *dst_table_schema, dst_tablet_ids))) {
    LOG_WARN("fail to collect dst tablet ids", K(ret));
  } else if (dst_tablet_ids.empty()) {
    is_data_complement_ = true;
    LOG_INFO("no tablets to check, data complement considered complete", K(target_object_id_));
  } else {
    bool all_complete = true;
    for (int64_t i = 0; OB_SUCC(ret) && i < dst_tablet_ids.count(); ++i) {
      const ObTabletID &tablet_id = dst_tablet_ids.at(i);
      bool is_complete = false;
      if (OB_FAIL(storage::ObTabletForkUtil::check_fork_data_complete(tablet_id, is_complete))) {
        LOG_WARN("fail to check fork data complete", K(ret), K(tablet_id));
        all_complete = false;
        break;
      } else if (!is_complete) {
        all_complete = false;
        LOG_INFO("waiting for fork data complement", K(task_id_), K(tablet_id),
            "dst_table_id", target_object_id_, "tablet_cnt", dst_tablet_ids.count());
        // Do not block RS worker thread here. Return OB_EAGAIN to let scheduler reschedule later.
        break;
      }
    }

    if (OB_SUCC(ret)) {
      is_data_complement_ = all_complete;
      if (all_complete) {
        LOG_INFO("fork table data complement complete", K(task_id_), "dst_table_id", target_object_id_,
            "tablet_cnt", dst_tablet_ids.count(),
            "cost_us", ObTimeUtility::current_time() - start_ts);
      }
    }
  }

  if (OB_SUCC(ret)&& !is_data_complement_) {
    ret = OB_EAGAIN;
  }

  if (OB_SUCC(ret) && is_data_complement_) {
    ObSEArray<ObTabletID, 4> src_tablet_ids;
    if (OB_FAIL(ObForkTableUtil::collect_tablet_ids_from_table(schema_guard, tenant_id_, object_id_, src_tablet_ids))) {
      LOG_WARN("fail to get src tablet ids", K(ret));
    } else if (OB_UNLIKELY(src_tablet_ids.count() != dst_tablet_ids.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("src and dst tablet count mismatch", K(ret),
               K(src_tablet_ids.count()), K(dst_tablet_ids.count()));
    } else {
      storage::ObTableForkInfo fork_info;
      if (OB_FAIL(build_fork_info(src_tablet_ids, dst_tablet_ids, fork_info))) {
        LOG_WARN("fail to build fork info", K(ret));
      } else {
        ObTableForkFinishLog finish_log;
        finish_log.fork_info_ = fork_info;
        SCN scn;
        if (OB_FAIL(ObDDLRedoLogWriter::write_auto_fork_log(
            SYS_LS,
            ObDDLClogType::DDL_TABLE_FORK_FINISH_LOG,
            logservice::ObReplayBarrierType::STRICT_BARRIER,
            finish_log,
            scn))) {
          LOG_WARN("fail to write table fork finish log", K(ret), K(finish_log));
        } else {
          LOG_INFO("fork table finish log written", K(task_id_), K(SYS_LS), K(scn),
              "src_table_id", object_id_, "dst_table_id", target_object_id_,
              "tablet_cnt", dst_tablet_ids.count());
          LOG_DEBUG("fork table finish log detail", K(task_id_), K(finish_log));
        }
      }
    }
  }

  if (OB_SUCC(ret) && is_data_complement_) {
    if (OB_FAIL(switch_status(next_task_status, true, ret))) {
      LOG_WARN("fail to switch task status", K(ret));
    }
  }

  return ret;
}

int ObForkTableTask::succ()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObForkTableTask has not been inited", K(ret));
  } else if (OB_FAIL(finish())) {
    LOG_WARN("fail to finish", K(ret));
  } else if (OB_FAIL(cleanup())) {
    LOG_WARN("fail to cleanup", K(ret));
  }
  return ret;
}

int ObForkTableTask::fail()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObForkTableTask has not been inited", K(ret));
  } else if (OB_FAIL(finish())) {
    LOG_WARN("finish failed", K(ret));
  } else if (OB_FAIL(cleanup())) {
    LOG_WARN("fail to cleanup", K(ret));
  } else {
    need_retry_ = false;
  }
  return ret;
}

int ObForkTableTask::finish()
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObForkTableTask has not been inited", K(ret));
  } else if (OB_FAIL(get_schema_guard(schema_guard))) {
    LOG_WARN("fail to get schema guard", K(ret));
  } else if (snapshot_version_ > 0 && OB_FAIL(ObForkTableUtil::release_snapshot(this, schema_guard, object_id_, snapshot_version_))) {
    LOG_WARN("fail to release snapshot", K(ret), K(object_id_), K(snapshot_version_));
  }
  return ret;
}

int ObForkTableTask::cleanup_impl()
{
  int ret = OB_SUCCESS;
  ObString unused_str;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(report_error_code(unused_str))) {
    LOG_WARN("report error code failed", K(ret));
  }

  if (OB_SUCC(ret) && OB_INVALID_ID != object_id_) {
    if (OB_ISNULL(GCTX.sql_proxy_) || OB_ISNULL(GCTX.schema_service_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_), KP(GCTX.schema_service_));
    } else {
      ObSchemaGetterGuard schema_guard;
      const ObTableSchema *src_table_schema = nullptr;
      const ObTableSchema *dst_table_schema = nullptr;
      observer::ObInnerSQLConnection *conn = nullptr;
      ObTableLockOwnerID lock_owner;
      ObMySQLTransaction trans;
      ObTimeoutCtx ctx;

      if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(tenant_id_, schema_guard))) {
        LOG_WARN("get tenant schema guard failed", K(ret), K(tenant_id_));
      } else if (OB_FAIL(schema_guard.get_table_schema(tenant_id_, object_id_, src_table_schema))) {
        LOG_WARN("fail to get table schema", K(ret), K(object_id_));
      } else if (OB_ISNULL(src_table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("source table not exist", K(ret), K(object_id_));
      } else if (OB_FAIL(schema_guard.get_table_schema(tenant_id_, target_object_id_, dst_table_schema))) {
        LOG_WARN("fail to get destination table schema", K(ret));
      } else if (OB_ISNULL(dst_table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("destination table not exist", K(ret), K_(target_object_id));
      } else if (OB_FAIL(trans.start(GCTX.sql_proxy_, tenant_id_))) {
        LOG_WARN("start transaction failed", K(ret));
      } else if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, GCONF.rpc_timeout))) {
        LOG_WARN("fail to set timeout ctx", KR(ret));
      } else if (OB_ISNULL(conn = dynamic_cast<observer::ObInnerSQLConnection *>(trans.get_connection()))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("conn_ is NULL", KR(ret));
      } else {
        ObLockObjRequest lock_arg;
        lock_arg.obj_type_ = ObLockOBJType::OBJ_TYPE_TENANT;
        lock_arg.obj_id_ = tenant_id_;
        lock_arg.owner_id_.set_default();
        lock_arg.lock_mode_ = EXCLUSIVE;  // Use EXCLUSIVE lock for cleanup operation
        lock_arg.op_type_ = ObTableLockOpType::IN_TRANS_COMMON_LOCK;
        lock_arg.timeout_us_ = ctx.get_timeout();
        if (OB_FAIL(ObInnerConnectionLockUtil::lock_obj(tenant_id_, lock_arg, conn))) {
          LOG_WARN("lock tenant failed", KR(ret), K(tenant_id_));
        }
      }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(lock_owner.convert_from_value(ObLockOwnerType::FORK_TABLE_OWNER_TYPE, FORK_TABLE_LOCK_OWNER_ID))) {
        LOG_WARN("failed to convert owner id", K(ret), K(task_id_));
      } else if (OB_FAIL(storage::ObDDLLock::unlock_for_fork_table(schema_guard, *src_table_schema, *dst_table_schema, task_id_, lock_owner, trans))) {
        LOG_WARN("failed to unlock for fork table", K(ret), K(task_id_), K(object_id_));
      } else {
        ObSqlString sql_string;
        int64_t affected_rows = 0;
        if (OB_FAIL(sql_string.assign_fmt(" DELETE FROM %s WHERE task_id=%ld",
            share::OB_ALL_DDL_TASK_STATUS_TNAME, task_id_))) {
          LOG_WARN("assign sql string failed", K(ret), K(task_id_));
        } else if (OB_FAIL(DDL_SIM(tenant_id_, task_id_, TASK_STATUS_OPERATOR_SLOW))) {
          LOG_WARN("ddl sim failure: slow inner sql", K(ret), K(tenant_id_), K(task_id_));
        } else if (OB_FAIL(DDL_SIM(tenant_id_, task_id_, DELETE_TASK_RECORD_FAILED))) {
          LOG_WARN("ddl sim failure: delete task record failed", K(ret), K(tenant_id_), K(task_id_));
        } else if (OB_FAIL(trans.write(tenant_id_, sql_string.ptr(), affected_rows))) {
          LOG_WARN("delete ddl task record failed", K(ret), K(sql_string));
        } else if (OB_UNLIKELY(affected_rows < 0)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected affected_rows", K(ret), K(affected_rows));
        } else {
          need_retry_ = false;  // clean succ, stop the task
        }
      }

      if (trans.is_started()) {
        bool commit = (OB_SUCCESS == ret);
        int tmp_ret = trans.end(commit);
        if (OB_SUCCESS != tmp_ret) {
          ret = (OB_SUCCESS == ret) ? tmp_ret : ret;
          LOG_WARN("trans end failed", K(ret), K(tmp_ret), K(commit));
        }
      }
    }
  }

  if (OB_SUCC(ret) && parent_task_id_ > 0) {
    const ObDDLTaskID parent_task_id(tenant_id_, parent_task_id_);
    ObSysDDLSchedulerUtil::on_ddl_task_finish(parent_task_id, get_task_key(), ret_code_, trace_id_);
  }
  LOG_INFO("clean fork table task finished", K(ret), KPC(this));
  return ret;
}

int ObForkTableTask::get_schema_guard(share::schema::ObSchemaGetterGuard &schema_guard)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(root_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("root service is null", K(ret));
  } else if (OB_FAIL(root_service_->get_ddl_service().get_tenant_schema_guard_with_version_in_inner_table(
      tenant_id_, schema_guard))) {
    LOG_WARN("fail to get tenant schema guard", K(ret));
  }
  return ret;
}

int ObForkTableTask::build_fork_info(
    const ObSEArray<ObTabletID, 4> &src_tablet_ids,
    const ObSEArray<ObTabletID, 4> &dst_tablet_ids,
    storage::ObTableForkInfo &fork_info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(src_tablet_ids.count() != dst_tablet_ids.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("src and dst tablet count mismatch", K(ret), K(src_tablet_ids.count()), K(dst_tablet_ids.count()));
  } else {
    fork_info = storage::ObTableForkInfo(
        tenant_id_,
        SYS_LS,
        object_id_,
        schema_version_,
        task_id_,
        snapshot_version_,
        compat_mode_,
        data_format_version_,
        consumer_group_id_,
        src_tablet_ids,
        dst_tablet_ids);

    if (OB_UNLIKELY(!fork_info.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid fork info", K(ret), K(fork_info));
    }
  }
  return ret;
}

}  // namespace rootserver
}  // namespace oceanbase
