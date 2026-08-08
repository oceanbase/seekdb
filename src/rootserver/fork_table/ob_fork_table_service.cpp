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

#include "rootserver/ddl_task/ob_sys_ddl_util.h"
#include "rootserver/fork_table/ob_fork_table_helper.h"
#include "rootserver/ob_ddl_service.h"
#include "rootserver/fork_table/ob_fork_table_util.h"
#include "rootserver/ob_rootserver_local_runtime.h"
#include "query/vector/ob_vector_index_util.h"
#include "sql/resolver/ddl/ob_fts_index_builder_util.h"
#include "storage/ddl/ob_ddl_lock.h"
#include "storage/tablelock/ob_lock_inner_connection_util.h"

namespace oceanbase {
using namespace common;
using namespace share;
using namespace obcall;
using namespace storage;
namespace rootserver {

int ObDDLService::fork_single_table_in_trans_(const ObTableSchema &src_table_schema,
    const ObDatabaseSchema &src_db_schema, const ObDatabaseSchema &dst_db_schema,
    const ObString &dst_table_name, const int64_t fork_snapshot_version,
    const uint64_t session_id, const ObString &ddl_stmt_str,
    ObSchemaGetterGuard &schema_guard, ObDDLSQLTransaction &trans,
    ObIAllocator &allocator, ObDDLTaskRecord &task_record,
    common::hash::ObHashMap<uint64_t, uint64_t> *table_id_map,
    ObSArray<ObTableSchema> *out_table_schemas) {
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_->get_schema_service();
  ObArenaAllocator inner_allocator(ObModIds::OB_RS_PARTITION_TABLE_TEMP);
  ObSArray<ObTableSchema> table_schemas;
  ObArray<ObMockFKParentTableSchema> mock_fk_parent_table_schema_array;
  const ObTableSchema *dst_table_schema = nullptr;

  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    LOG_WARN("schema_service must not null", K(ret));
  } else {
    // Rebuild table schema with new id for fork table.
    if (OB_FAIL(rebuild_table_schema_with_new_id(
            src_table_schema, dst_db_schema, dst_table_name,
            session_id, USER_TABLE, *schema_service, table_schemas,
            inner_allocator,
            OB_INVALID_ID, // define_user_id
            false /* delete_unused_columns */))) {
    } else if (OB_FAIL(
                   generate_object_id_for_partition_schemas(table_schemas))) {
    } else if (OB_FAIL(generate_tables_tablet_id(table_schemas))) {
    } else if (table_schemas.count() > 0) {
      dst_table_schema = &table_schemas.at(0);
    }

    // Build table_id_map: src_table_id/src_index_id -> dst_table_id/dst_index_id
    if (OB_SUCC(ret) && OB_NOT_NULL(table_id_map) && table_schemas.count() > 0) {
      // Main table mapping
      if (OB_FAIL(table_id_map->set_refactored(
              src_table_schema.get_table_id(), table_schemas.at(0).get_table_id()))) {
      }
      // Index table mappings: match by index name (strip data_table_id prefix)
      if (OB_SUCC(ret)) {
        ObSEArray<ObAuxTableMetaInfo, 16> src_index_infos;
        if (OB_FAIL(src_table_schema.get_simple_index_infos(src_index_infos))) {
        }
        for (int64_t idx = 0; OB_SUCC(ret) && idx < src_index_infos.count(); ++idx) {
          const uint64_t src_index_id = src_index_infos.at(idx).table_id_;
          const ObTableSchema *src_index_schema = nullptr;
          if (OB_FAIL(schema_guard.get_table_schema( src_index_id, src_index_schema))) {
          } else if (OB_ISNULL(src_index_schema)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("src index schema is null", KR(ret), K(src_index_id));
          } else {
            ObString src_index_name;
            if (OB_FAIL(ObTableSchema::get_index_name(inner_allocator,
                    src_table_schema.get_table_id(),
                    src_index_schema->get_table_name_str(),
                    src_index_name))) {
            } else {
              bool found = false;
              for (int64_t k = 1; !found && OB_SUCC(ret) && k < table_schemas.count(); ++k) {
                ObString dst_index_name;
                if (OB_FAIL(ObTableSchema::get_index_name(inner_allocator,
                        table_schemas.at(0).get_table_id(),
                        table_schemas.at(k).get_table_name_str(),
                        dst_index_name))) {
                } else if (0 == src_index_name.compare(dst_index_name)) {
                  if (OB_FAIL(table_id_map->set_refactored(
                          src_index_id, table_schemas.at(k).get_table_id()))) {
                  }
                  found = true;
                }
              }
            }
          }
        }
      }
    }

    // Copy table_schemas to out_table_schemas if requested.
    if (OB_SUCC(ret) && OB_NOT_NULL(out_table_schemas)) {
      if (OB_FAIL(out_table_schemas->assign(table_schemas))) {
      }
    }

    // Create tables using fork logic.
    if (OB_SUCC(ret)) {
      share::ObForkTableInfo fork_table_info(src_table_schema.get_table_id(),
                                             fork_snapshot_version);

      if (OB_FAIL(create_tables_for_fork_(
              ddl_stmt_str, table_schemas,
              mock_fk_parent_table_schema_array, schema_guard, trans,
              fork_table_info))) {
      }
    }
  }

  // Create DDL task and lock for fork table.
  if (OB_SUCC(ret) && OB_NOT_NULL(dst_table_schema)) {
    ObTableLockOwnerID lock_owner;

    // Construct ObForkTableArg for this table.
    ObForkTableArg fork_table_arg;
    
    
    fork_table_arg.src_database_name_ = src_db_schema.get_database_name_str();
    fork_table_arg.src_table_name_ = src_table_schema.get_table_name_str();
    fork_table_arg.dst_database_name_ = dst_db_schema.get_database_name_str();
    fork_table_arg.dst_table_name_ = dst_table_schema->get_table_name_str();
    fork_table_arg.if_not_exist_ = false;
    fork_table_arg.session_id_ = session_id;

    ObCreateDDLTaskParam param(
        ObDDLType::DDL_FORK_TABLE, &src_table_schema,
        dst_table_schema, src_table_schema.get_table_id(),
        dst_table_schema->get_schema_version(), 0 /* parallelism */,
        &allocator, &fork_table_arg,
        0 /* parent task id*/);
    param.new_snapshot_version_ = fork_snapshot_version;

    if (OB_FAIL(
            ObSysDDLSchedulerUtil::create_ddl_task(param, trans, task_record))) {
    } else if (OB_FAIL(lock_owner.convert_from_value(
                   ObLockOwnerType::FORK_TABLE_OWNER_TYPE,
                   FORK_TABLE_LOCK_OWNER_ID))) {
    } else if (OB_FAIL(ObDDLLock::lock_for_fork_table(
                   schema_guard, src_table_schema, table_schemas, lock_owner,
                   trans))) {
    } else {
      LOG_INFO("fork single table task created", "task_id",
               task_record.task_id_, "src_table_id",
               src_table_schema.get_table_id(), "src_table_name",
               src_table_schema.get_table_name(), "dst_table_id",
               dst_table_schema->get_table_id(), "dst_table_name",
               dst_table_schema->get_table_name());
    }
  }

  return ret;
}

int ObDDLService::create_tables_for_fork_(
    const common::ObString &ddl_stmt_str,
    common::ObIArray<share::schema::ObTableSchema> &table_schemas,
    ObIArray<ObMockFKParentTableSchema> &mock_fk_parent_table_schema_array,
    share::schema::ObSchemaGetterGuard &schema_guard,
    ObDDLSQLTransaction &trans, const share::ObForkTableInfo &fork_table_info) {
  int ret = OB_SUCCESS;
  const uint64_t data_format_version = DATA_CURRENT_VERSION;
  RS_TRACE(create_tables_in_trans_begin);

  if (OB_FAIL(check_inner_stat())) {
  } else if (table_schemas.count() < 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table_schemas have no element", K(ret));
  } else if (!fork_table_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fork table info is invalid", K(ret), K(fork_table_info));
  } else {
    ObDDLOperator ddl_operator(*schema_service_, *sql_proxy_);
    
    RS_TRACE(operator_create_table_begin);

    for (int64_t i = 0; OB_SUCC(ret) && i < table_schemas.count(); i++) {
      ObTableSchema &table_schema = table_schemas.at(i);
      if (OB_FAIL(ObFtsIndexBuilderUtil::try_load_and_lock_dictionary_tables(
              table_schema, trans))) {
      } else if (OB_FAIL(ddl_operator.create_table(
                     table_schema, trans, 0 == i ? &ddl_stmt_str : NULL,
                     i == table_schemas.count() - 1))) {
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(ddl_operator.deal_with_mock_fk_parent_tables(
              trans, schema_guard, mock_fk_parent_table_schema_array))) {
      } else if (OB_FAIL(create_tablets_in_trans_(
                     table_schemas, ddl_operator, trans, schema_guard,
                     data_format_version, fork_table_info))) {
      }
    }
    RS_TRACE(operator_create_table_end);

    DEBUG_SYNC(BEFORE_CREATE_TABLE_TRANS_COMMIT);
    if (OB_SUCC(ret) && THIS_WORKER.is_timeout_ts_valid() &&
        THIS_WORKER.is_timeout()) {
      ret = OB_TIMEOUT;
      LOG_WARN("already timeout", KR(ret));
    }

    if (OB_SUCC(ret)) {
      ret = OB_E(EventTable::EN_CREATE_TABLE_TRANS_END_FAIL) OB_SUCCESS;
    }

    if (OB_SUCC(ret)) {
      ObForkTableHelper fork_table_helper(*schema_service_, *sql_proxy_, trans,
                                          schema_guard,
                                          fork_table_info);
      if (OB_FAIL(fork_table_helper.init(table_schemas))) {
      } else if (OB_FAIL(fork_table_helper.execute())) {
      }
    }
  }
  RS_TRACE(create_tables_in_trans_end);
  return ret;
}

int ObDDLService::fork_table(const obcall::ObForkTableArg &fork_table_arg,
                             obcall::ObDDLRes &res) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
  } else if (!fork_table_arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(fork_table_arg), K(ret));
  } else {
    LOG_INFO("fork table request accepted", "src_db",
             fork_table_arg.src_database_name_, "src_table",
             fork_table_arg.src_table_name_, "dst_db",
             fork_table_arg.dst_database_name_, "dst_table",
             fork_table_arg.dst_table_name_, "session_id",
             fork_table_arg.session_id_);

    ObSchemaGetterGuard schema_guard;
    ObDDLTaskRecord task_record;
    const ObTableSchema *src_table_schema = nullptr;
    const ObTableSchema *dst_table_schema = nullptr;
    const ObDatabaseSchema *src_db_schema = nullptr;
    const ObDatabaseSchema *dst_db_schema = nullptr;
    
    int64_t refreshed_schema_version = 0;
    int64_t fork_snapshot_version = 0;
    ObDDLSQLTransaction trans(schema_service_);
    ObArenaAllocator allocator(lib::ObLabel("ForkTable"));
    bool is_db_in_recyclebin = false;
    ObSEArray<const ObTableSchema*, 1> src_table_schemas;

    if (OB_FAIL(get_runtime_schema_guard_with_version_in_inner_table(
            schema_guard))) {
    } else if (OB_FAIL(schema_guard.get_schema_version(
                   refreshed_schema_version))) {
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(schema_guard.get_database_schema( fork_table_arg.src_database_name_, src_db_schema))) {
      } else if (NULL == src_db_schema) {
        ret = OB_ERR_BAD_DATABASE;
        LOG_USER_ERROR(OB_ERR_BAD_DATABASE,
                       fork_table_arg.src_database_name_.length(),
                       fork_table_arg.src_database_name_.ptr());
        LOG_WARN("source database not exist", K(fork_table_arg), K(ret));
      } else if (OB_FAIL(schema_guard.check_database_in_recyclebin(src_db_schema->get_database_id(),
                     is_db_in_recyclebin))) {
      } else if (is_db_in_recyclebin || src_db_schema->is_in_recyclebin()) {
        ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
        LOG_WARN("can not fork table from database in recyclebin", K(ret),
                 K(*src_db_schema), K(is_db_in_recyclebin));
      } else if (OB_FAIL(schema_guard.get_table_schema( fork_table_arg.src_database_name_,
                     fork_table_arg.src_table_name_, false /* is_index */,
                     src_table_schema))) {
      } else if (OB_ISNULL(src_table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("source table not exist", K(ret), K(fork_table_arg));
      } else if (OB_FAIL(check_fork_table_supported(
                     *src_table_schema, schema_guard, &fork_table_arg))) {
      }
    }

    if (OB_SUCC(ret)) {
      const share::schema::ObMockFKParentTableSchema *dst_mock_parent_schema =
          nullptr;
      if (OB_FAIL(schema_guard.get_database_schema( fork_table_arg.dst_database_name_, dst_db_schema))) {
      } else if (NULL == dst_db_schema) {
        ret = OB_ERR_BAD_DATABASE;
        LOG_USER_ERROR(OB_ERR_BAD_DATABASE,
                       fork_table_arg.dst_database_name_.length(),
                       fork_table_arg.dst_database_name_.ptr());
        LOG_WARN("destination database not exist", K(fork_table_arg), K(ret));
      } else if (dst_db_schema->is_in_recyclebin()) {
        ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
        LOG_WARN("can not create table in recyclebin", K(ret),
                 K(*dst_db_schema));
      } else if (OB_FAIL(schema_guard.get_mock_fk_parent_table_schema_with_name(dst_db_schema->get_database_id(),
                     fork_table_arg.dst_table_name_, dst_mock_parent_schema))) {
      } else if (OB_NOT_NULL(dst_mock_parent_schema)) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("fork table to mock parent table name is not supported",
                 KR(ret), "database_id",
                 dst_db_schema->get_database_id(), "table_name",
                 fork_table_arg.dst_table_name_);
        LOG_USER_ERROR(OB_NOT_SUPPORTED,
                       "fork table to mock parent table name is");
      } else if (OB_FAIL(schema_guard.get_table_schema( fork_table_arg.dst_database_name_,
                     fork_table_arg.dst_table_name_, false /* is_index */,
                     dst_table_schema))) {
      } else if (OB_NOT_NULL(dst_table_schema)) {
        ret = OB_ERR_TABLE_EXIST;
        LOG_USER_ERROR(OB_ERR_TABLE_EXIST,
                       fork_table_arg.dst_table_name_.length(),
                       fork_table_arg.dst_table_name_.ptr());
        LOG_WARN("destination table already exists", K(ret), K(fork_table_arg));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(trans.start(&get_sql_proxy(), 
                              refreshed_schema_version))) {
      } else if (FALSE_IT(src_table_schemas.push_back(src_table_schema))) {
      }

      // For tables with async vector indexes, lock the source table to block DML
      // and wait for ChangeStream to catch up before obtaining the snapshot.
      // This ensures the fork snapshot covers both main table data and async index data.
      if (OB_SUCC(ret)) {
        bool has_async_vec_index = false;
        if (OB_FAIL(check_has_async_vector_index(*src_table_schema, schema_guard,
                                                 has_async_vec_index))) {
        } else if (has_async_vec_index) {
          common::sqlclient::ObISQLConnection *iconn = trans.get_connection();
          const int64_t lock_timeout_us = GCONF.internal_sql_execute_timeout;
          if (OB_ISNULL(iconn)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("inner connection is null", KR(ret));
          } else if (OB_FAIL(transaction::tablelock::ObInnerConnectionLockUtil::lock_table(
                         src_table_schema->get_table_id(),
                         transaction::tablelock::SHARE, lock_timeout_us, iconn))) {
          } else if (OB_ISNULL(rootserver_local_runtime())) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("rootserver local runtime is null", KR(ret));
          } else if (OB_FAIL(rootserver_local_runtime()->wait_until_change_stream_refreshed(
                         get_sql_proxy(), lock_timeout_us))) {
          } else {
            LOG_INFO("async index sync completed before fork snapshot",
                     "table_id", src_table_schema->get_table_id());
          }
        }
      }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(ObForkTableUtil::obtain_snapshot(
                     trans, schema_guard, src_table_schemas,
                     fork_snapshot_version))) {
      } else if (fork_snapshot_version <= 0) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid snapshot version", K(ret), K(fork_snapshot_version));
      } else {
        LOG_INFO("fork table snapshot acquired",
                 K(fork_snapshot_version), "src_table_id",
                 src_table_schema->get_table_id(), "src_schema_version",
                 src_table_schema->get_schema_version());

        // Use the unified helper function to fork the single table.
        if (OB_FAIL(fork_single_table_in_trans_(*src_table_schema, *src_db_schema, *dst_db_schema,
                fork_table_arg.dst_table_name_, fork_snapshot_version,
                fork_table_arg.session_id_, fork_table_arg.ddl_stmt_str_,
                schema_guard, trans, allocator, task_record))) {
        }
      }

      if (trans.is_started()) {
        bool commit = (OB_SUCCESS == ret);
        int tmp_ret = trans.end(commit);
        if (OB_SUCCESS != tmp_ret) {
          ret = (OB_SUCCESS == ret) ? tmp_ret : ret;
          LOG_ERROR("trans end failed", K(ret), K(tmp_ret), K(commit));
        }
      }

      RS_TRACE(public_schema_begin);
      if (OB_SUCC(ret)) {
        if (OB_FAIL(publish_schema())) {
        } else {
          RS_TRACE(public_schema_end);
        }
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(ObSysDDLSchedulerUtil::schedule_ddl_task(task_record))) {
        } else {
        
          
          res.schema_id_ = src_table_schema->get_table_id();
          res.task_id_ = task_record.task_id_;
          LOG_INFO("fork table task scheduled", "task_id",
                   task_record.task_id_, K(fork_snapshot_version),
                   "src_table_id", src_table_schema->get_table_id());
        }
      }
    }
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
