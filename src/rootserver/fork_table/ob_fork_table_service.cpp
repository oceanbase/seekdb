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

#include "rootserver/ob_ddl_service.h"
#include "rootserver/fork_table/ob_fork_table_helper.h"
#include "rootserver/ddl_task/ob_sys_ddl_util.h"
#include "storage/ddl/ob_ddl_lock.h"
#include "share/ob_fts_index_builder_util.h"
#include "share/ob_fork_table_util.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace obrpc;
using namespace storage;
namespace rootserver
{

int ObDDLService::create_tables_for_fork_(
    const common::ObString &ddl_stmt_str,
    common::ObIArray<share::schema::ObTableSchema> &table_schemas,
    const obrpc::ObSequenceDDLArg &sequence_ddl_arg,
    ObIArray<ObMockFKParentTableSchema> &mock_fk_parent_table_schema_array,
    share::schema::ObSchemaGetterGuard &schema_guard,
    ObDDLSQLTransaction &trans,
    const share::ObForkTableInfo &fork_table_info)
{
  int ret = OB_SUCCESS;
  uint64_t tenant_data_version = 0;
  RS_TRACE(create_tables_in_trans_begin);

  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("variable is not init");
  } else if (table_schemas.count() < 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table_schemas have no element", K(ret));
  } else if (!fork_table_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fork table info is invalid", K(ret), K(fork_table_info));
  } else {
    ObDDLOperator ddl_operator(*schema_service_, *sql_proxy_);
    uint64_t tenant_id = table_schemas.at(0).get_tenant_id();
    if (OB_FAIL(GET_MIN_DATA_VERSION(tenant_id, tenant_data_version))) {
      LOG_WARN("get min data version failed", K(ret), K(tenant_id));
    } else {
      RS_TRACE(operator_create_table_begin);

      for (int64_t i = 0; OB_SUCC(ret) && i < table_schemas.count(); i++) {
        ObTableSchema &table_schema = table_schemas.at(i);
        if (OB_FAIL(ObFtsIndexBuilderUtil::try_load_and_lock_dictionary_tables(table_schema, trans))) {
          LOG_WARN("fail to try load and lock dictionary tables", K(ret));
        } else if (OB_FAIL(ddl_operator.create_sequence_in_create_table(table_schema,
                                                                 trans,
                                                                 schema_guard,
                                                                 &sequence_ddl_arg))) {
          LOG_WARN("create sequence in create table fail", K(ret));
        } else if (OB_FAIL(ddl_operator.create_table(table_schema,
                                                     trans,
                                                     0 == i ? &ddl_stmt_str : NULL,
                                                     i == table_schemas.count() - 1))) {
          LOG_WARN("failed to create table schema, ", K(ret));
        } else if (OB_FAIL(ddl_operator.insert_temp_table_info(trans, table_schema))) {
          LOG_WARN("failed to insert_temp_table_info!", K(ret));
        }
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(ddl_operator.deal_with_mock_fk_parent_tables(trans, schema_guard, mock_fk_parent_table_schema_array))) {
          LOG_WARN("fail to deal_with_mock_fk_parent_tables", K(ret), K(tenant_id));
        } else if (OB_FAIL(create_tablets_in_trans_(table_schemas, ddl_operator, trans, schema_guard, tenant_data_version, fork_table_info))) {
          LOG_WARN("fail to create tablets in trans", KR(ret));
        }
      }
      RS_TRACE(operator_create_table_end);
    }

    DEBUG_SYNC(BEFORE_CREATE_TABLE_TRANS_COMMIT);
    if (OB_SUCC(ret) && THIS_WORKER.is_timeout_ts_valid() && THIS_WORKER.is_timeout()) {
      ret = OB_TIMEOUT;
      LOG_WARN("already timeout", KR(ret));
    }

    if (OB_SUCC(ret)) {
      ret = OB_E(EventTable::EN_CREATE_TABLE_TRANS_END_FAIL) OB_SUCCESS;
    }

    if (OB_SUCC(ret)) {
      ObForkTableHelper fork_table_helper(*schema_service_, *sql_proxy_, trans, schema_guard, tenant_id, fork_table_info);
      if (OB_FAIL(fork_table_helper.init(table_schemas))) {
        LOG_WARN("failed to init fork table helper", KR(ret), K(fork_table_info));
      } else if (OB_FAIL(fork_table_helper.execute())) {
        LOG_WARN("failed to execute fork table helper", KR(ret), K(fork_table_info));
      }
    }
  }
  RS_TRACE(create_tables_in_trans_end);
  return ret;
}

int ObDDLService::fork_table(const obrpc::ObForkTableArg &fork_table_arg, obrpc::ObDDLRes &res)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("variable is not init");
  } else if (!fork_table_arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(fork_table_arg), K(ret));
  } else {
    LOG_INFO("fork table request accepted",
        "tenant_id", fork_table_arg.tenant_id_,
        "src_db", fork_table_arg.src_database_name_,
        "src_table", fork_table_arg.src_table_name_,
        "dst_db", fork_table_arg.dst_database_name_,
        "dst_table", fork_table_arg.dst_table_name_,
        "session_id", fork_table_arg.session_id_);

    ObSchemaGetterGuard schema_guard;
    ObDDLTaskRecord task_record;
    const ObTableSchema *src_table_schema = nullptr;
    const ObTableSchema *dst_table_schema = nullptr;
    const ObDatabaseSchema *src_db_schema = nullptr;
    const ObDatabaseSchema *dst_db_schema = nullptr;
    const uint64_t tenant_id = fork_table_arg.tenant_id_;
    int64_t refreshed_schema_version = 0;
    int64_t fork_snapshot_version = 0;
    ObDDLSQLTransaction trans(schema_service_);
    ObArenaAllocator allocator(lib::ObLabel("ForkTable"));
    bool is_db_in_recyclebin = false;
    ObSArray<ObTableSchema> table_schemas;

    if (OB_FAIL(get_tenant_schema_guard_with_version_in_inner_table(tenant_id, schema_guard))) {
      LOG_WARN("get schema guard in inner table failed", K(ret));
    } else if (OB_FAIL(schema_guard.get_schema_version(tenant_id, refreshed_schema_version))) {
      LOG_WARN("failed to get tenant schema version", KR(ret), K(tenant_id));
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(schema_guard.get_database_schema(tenant_id, fork_table_arg.src_database_name_, src_db_schema))) {
        LOG_WARN("fail to get source database schema", K(ret));
      } else if (NULL == src_db_schema) {
        ret = OB_ERR_BAD_DATABASE;
        LOG_USER_ERROR(OB_ERR_BAD_DATABASE, fork_table_arg.src_database_name_.length(), fork_table_arg.src_database_name_.ptr());
        LOG_WARN("source database not exist", K(fork_table_arg), K(ret));
      } else if (OB_FAIL(schema_guard.check_database_in_recyclebin(
                 tenant_id, src_db_schema->get_database_id(), is_db_in_recyclebin))) {
        LOG_WARN("check source database in recyclebin failed", K(ret), K(tenant_id), K(*src_db_schema));
      } else if (is_db_in_recyclebin || src_db_schema->is_in_recyclebin()) {
        ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
        LOG_WARN("can not fork table from database in recyclebin", K(ret),
                 K(*src_db_schema), K(is_db_in_recyclebin));
      } else if (OB_FAIL(schema_guard.get_table_schema(tenant_id, fork_table_arg.src_database_name_,
            fork_table_arg.src_table_name_, false /* is_index */,
            src_table_schema))) {
        LOG_WARN("fail to get source table schema", K(ret));
      } else if (OB_ISNULL(src_table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("source table not exist", K(ret), K(fork_table_arg));
      } else if (src_table_schema->is_tmp_table() || src_table_schema->is_ctas_tmp_table()) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("fork table on temporary table is not supported", KR(ret), K(*src_table_schema));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "fork table on temporary table is");
      } else if (!src_table_schema->is_user_table()) {
        ret = OB_ERR_WRONG_OBJECT;
        LOG_USER_ERROR(OB_ERR_WRONG_OBJECT, to_cstring(fork_table_arg.src_database_name_), to_cstring(fork_table_arg.src_table_name_), "BASE TABLE");
      } else if (src_table_schema->is_in_recyclebin()) {
        ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
        LOG_WARN("can fork table from table in recyclebin", K(ret), K(*src_table_schema));
      } else if (src_table_schema->has_mlog_table()) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("fork table on table with materialized view log is not supported", KR(ret));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "fork table on table with materialized view log is");
      } else if (src_table_schema->table_referenced_by_fast_lsm_mv()) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("fork table on table required by materialized view is not supported", KR(ret));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "fork table on table required by materialized view is");
      }
    }

    if (OB_SUCC(ret)) {
      const share::schema::ObMockFKParentTableSchema *dst_mock_parent_schema = nullptr;
      if (OB_FAIL(schema_guard.get_database_schema(tenant_id, fork_table_arg.dst_database_name_, dst_db_schema))) {
        LOG_WARN("fail to get destination database schema", K(ret));
      } else if (NULL == dst_db_schema) {
        ret = OB_ERR_BAD_DATABASE;
        LOG_USER_ERROR(OB_ERR_BAD_DATABASE, fork_table_arg.dst_database_name_.length(), fork_table_arg.dst_database_name_.ptr());
        LOG_WARN("destination database not exist", K(fork_table_arg), K(ret));
      } else if (dst_db_schema->is_in_recyclebin()) {
        ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
        LOG_WARN("can not create table in recyclebin", K(ret), K(*dst_db_schema));
      } else if (OB_FAIL(schema_guard.get_mock_fk_parent_table_schema_with_name(
                     tenant_id,
                     dst_db_schema->get_database_id(),
                     fork_table_arg.dst_table_name_,
                     dst_mock_parent_schema))) {
        LOG_WARN("failed to get mock fk parent table schema for destination table name", K(ret), K(tenant_id),
                 K(dst_db_schema->get_database_id()), K(fork_table_arg.dst_table_name_));
      } else if (OB_NOT_NULL(dst_mock_parent_schema)) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("fork table to mock parent table name is not supported", KR(ret),
                 K(tenant_id), "database_id", dst_db_schema->get_database_id(),
                 "table_name", fork_table_arg.dst_table_name_);
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "fork table to mock parent table name is");
      } else if (OB_FAIL(schema_guard.get_table_schema(tenant_id, fork_table_arg.dst_database_name_,
            fork_table_arg.dst_table_name_, false /* is_index */,
            dst_table_schema))) {
        LOG_WARN("fail to get destination table schema", K(ret));
      } else if (OB_NOT_NULL(dst_table_schema)) {
        ret = OB_ERR_TABLE_EXIST;
        LOG_USER_ERROR(OB_ERR_TABLE_EXIST, fork_table_arg.dst_table_name_.length(), fork_table_arg.dst_table_name_.ptr());
        LOG_WARN("destination table already exists", K(ret), K(fork_table_arg));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(trans.start(&get_sql_proxy(), tenant_id, refreshed_schema_version))) {
        LOG_WARN("start transaction failed", KR(ret), K(tenant_id), K(refreshed_schema_version));
      } else if (OB_FAIL(ObForkTableUtil::obtain_snapshot(trans, schema_guard, *src_table_schema, fork_snapshot_version))) {
        LOG_WARN("fail to obtain snapshot", K(ret), K(fork_snapshot_version));
      } else if (fork_snapshot_version <= 0) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid snapshot version", K(ret), K(fork_snapshot_version));
      } else {
        LOG_INFO("fork table snapshot acquired", K(tenant_id), K(fork_snapshot_version),
            "src_table_id", src_table_schema->get_table_id(),
            "src_schema_version", src_table_schema->get_schema_version());
        share::ObForkTableInfo fork_table_info(src_table_schema->get_table_id(), fork_snapshot_version);
        ObSchemaService *schema_service = schema_service_->get_schema_service();
        ObArenaAllocator inner_allocator(ObModIds::OB_RS_PARTITION_TABLE_TEMP);
        ObArray<ObMockFKParentTableSchema> mock_fk_parent_table_schema_array;
        int64_t ddl_task_id = 0;

        if (OB_ISNULL(schema_service)) {
          ret = OB_ERR_SYS;
          LOG_WARN("schema_service must not null", K(ret));
        } else {
          share::schema::ObErrorInfo error_info;
          ObString empty_ddl_stmt_str;
          obrpc::ObSequenceDDLArg empty_sequence_ddl_arg;

          // Rebuild table schema with new id for fork table
          if (OB_FAIL(rebuild_table_schema_with_new_id(*src_table_schema,
                                                       *dst_db_schema,
                                                       fork_table_arg.dst_table_name_,
                                                       ObString(), // create_host
                                                       fork_table_arg.session_id_,
                                                       USER_TABLE,
                                                       *schema_service,
                                                       table_schemas,
                                                       inner_allocator,
                                                       OB_INVALID_ID))) { // define_user_id
            LOG_WARN("failed to rebuild table schema with new id", KR(ret));
          } else if (OB_FAIL(generate_object_id_for_partition_schemas(table_schemas))) {
            LOG_WARN("fail to generate object_id for partition schema", KR(ret), K(table_schemas));
          } else if (OB_FAIL(generate_tables_tablet_id(table_schemas))) {
            LOG_WARN("failed to generate_tables_id", KR(ret), K(table_schemas));
          } else if (table_schemas.count() > 0) {
            dst_table_schema = &table_schemas.at(0);
          }

          if (OB_SUCC(ret) && OB_FAIL(create_tables_for_fork_(fork_table_arg.ddl_stmt_str_,
                                                              table_schemas,
                                                              empty_sequence_ddl_arg,
                                                              mock_fk_parent_table_schema_array,
                                                              schema_guard,
                                                              trans,
                                                              fork_table_info))) {
            LOG_WARN("failed to create tables in transaction", K(ret));
          }
        }
      }

      if (OB_SUCC(ret)) {
        ObTableLockOwnerID lock_owner;
        ObCreateDDLTaskParam param(tenant_id,
                                   ObDDLType::DDL_FORK_TABLE,
                                   src_table_schema,
                                   dst_table_schema,
                                   src_table_schema->get_table_id(),
                                   dst_table_schema->get_schema_version(),
                                   0 /* parallelism */,
                                   0 /* consumer_group_id */,
                                   &allocator,
                                   &fork_table_arg,
                                   0 /* parent task id*/);
        param.new_snapshot_version_ = fork_snapshot_version;
        if (OB_FAIL(ObSysDDLSchedulerUtil::create_ddl_task(param, trans, task_record))) {
          LOG_WARN("submit ddl task failed", K(ret), K(fork_table_arg));
        } else if (OB_FAIL(lock_owner.convert_from_value(ObLockOwnerType::FORK_TABLE_OWNER_TYPE, FORK_TABLE_LOCK_OWNER_ID))) {
          LOG_WARN("failed to convert owner id", K(ret), K(task_record.task_id_));
        } else if (OB_FAIL(storage::ObDDLLock::lock_for_fork_table(schema_guard, *src_table_schema, table_schemas, lock_owner, trans))) {
          LOG_WARN("failed to lock for fork table", K(ret), K(task_record.task_id_));
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

      RS_TRACE(public_schema_begin);
      if (OB_SUCC(ret)) {
        if (OB_FAIL(publish_schema(tenant_id))) {
          LOG_WARN("publish_schema failed", KR(ret), K(tenant_id));
        } else {
          RS_TRACE(public_schema_end);
        }
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(ObSysDDLSchedulerUtil::schedule_ddl_task(task_record))) {
          LOG_WARN("fail to schedule ddl task", K(ret), K(task_record));
        } else {
          res.tenant_id_ = tenant_id;
          res.schema_id_ = src_table_schema->get_table_id();
          res.task_id_ = task_record.task_id_;
          LOG_INFO("fork table task scheduled", K(tenant_id), "task_id", task_record.task_id_,
              K(fork_snapshot_version),
              "src_table_id", src_table_schema->get_table_id(),
              "dst_table_id", dst_table_schema->get_table_id());
        }
      }
    }
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
