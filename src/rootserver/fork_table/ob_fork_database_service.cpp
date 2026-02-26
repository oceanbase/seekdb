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
#include "share/ob_fork_table_util.h"
#include "share/ob_fts_index_builder_util.h"
#include "storage/ddl/ob_ddl_lock.h"

namespace oceanbase {
using namespace common;
using namespace share;
using namespace obrpc;
using namespace storage;
namespace rootserver {

int ObDDLService::fork_database(
    const obrpc::ObForkDatabaseArg &fork_database_arg, obrpc::ObDDLRes &res) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("variable is not init", K(ret));
  } else if (!fork_database_arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(fork_database_arg));
  } else {
    LOG_INFO("fork database request accepted", "tenant_id",
             fork_database_arg.tenant_id_, "src_db",
             fork_database_arg.src_database_name_, "dst_db",
             fork_database_arg.dst_database_name_, "session_id",
             fork_database_arg.session_id_);

    ObSchemaGetterGuard schema_guard;
    const ObDatabaseSchema *src_db_schema = nullptr;
    uint64_t dst_db_id = OB_INVALID_ID;
    bool is_dst_db_exist = false;
    const uint64_t tenant_id = fork_database_arg.tenant_id_;
    int64_t refreshed_schema_version = 0;
    bool is_db_in_recyclebin = false;
    ObArray<const ObTableSchema *> src_db_table_schemas;
    ObArray<const ObTableSchema *> user_table_schemas;
    ObArenaAllocator allocator(lib::ObLabel("ForkDatabase"));

    ObDDLSQLTransaction trans(schema_service_);
    if (OB_FAIL(get_tenant_schema_guard_with_version_in_inner_table(
            tenant_id, schema_guard))) {
      LOG_WARN("get schema guard in inner table failed", K(ret));
    } else if (OB_FAIL(schema_guard.get_schema_version(
                   tenant_id, refreshed_schema_version))) {
      LOG_WARN("failed to get tenant schema version", KR(ret), K(tenant_id));
    }

    // Database existence basic check.
    if (OB_SUCC(ret)) {
      if (OB_FAIL(schema_guard.get_database_schema(
              tenant_id, fork_database_arg.src_database_name_,
              src_db_schema))) {
        LOG_WARN("failed to get src database schema", KR(ret));
      } else if (NULL == src_db_schema) {
        ret = OB_ERR_BAD_DATABASE;
        LOG_USER_ERROR(OB_ERR_BAD_DATABASE,
                       fork_database_arg.src_database_name_.length(),
                       fork_database_arg.src_database_name_.ptr());
        LOG_WARN("source database not exist", K(fork_database_arg), K(ret));
      } else if (OB_FAIL(schema_guard.check_database_in_recyclebin(
                     tenant_id, src_db_schema->get_database_id(),
                     is_db_in_recyclebin))) {
        LOG_WARN("check source database in recyclebin failed", K(ret),
                 K(tenant_id), K(*src_db_schema));
      } else if (is_db_in_recyclebin || src_db_schema->is_in_recyclebin()) {
        ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
        LOG_WARN("can not fork database from database in recyclebin", K(ret),
                 K(*src_db_schema), K(is_db_in_recyclebin));
      } else if (is_sys_database_id(src_db_schema->get_database_id())) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "fork database from system or internal database");
        LOG_WARN("fork database from system/internal database is not supported", K(ret),
                 K(*src_db_schema));
      } else if (OB_FAIL(schema_service_->check_database_exist(
                     tenant_id, fork_database_arg.dst_database_name_, dst_db_id,
                     is_dst_db_exist))) {
        LOG_WARN("check database exist failed", K(ret), K(tenant_id),
                 K(fork_database_arg.dst_database_name_));
      } else if (is_dst_db_exist) {
        // Currently, we do not support fork database to an existing database.
        ret = OB_DATABASE_EXIST;
        LOG_USER_ERROR(OB_DATABASE_EXIST,
                       fork_database_arg.dst_database_name_.length(),
                       fork_database_arg.dst_database_name_.ptr());
        LOG_WARN("destination database already exists", "database_name",
                 fork_database_arg.dst_database_name_, K(tenant_id), K(ret));
      }
    }

    // Check unsupported database objects: Routine, Package, Trigger, Sequence and Outline.
    if (OB_SUCC(ret)) {
      const uint64_t database_id = src_db_schema->get_database_id();

      // Check if database has Routine (procedures/functions)
      ObArray<uint64_t> routine_ids;
      if (OB_FAIL(schema_guard.get_routine_ids_in_database(
              tenant_id, database_id, routine_ids))) {
        LOG_WARN("failed to get routine ids in database", KR(ret),
                 K(tenant_id), K(database_id));
      } else if (routine_ids.count() > 0) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED,
                       "fork database containing routines (procedures/functions)");
        LOG_WARN("fork database with routines is not supported", K(ret),
                 K(tenant_id), K(database_id), "routine_count", routine_ids.count());
      }

      // Check if database has Package
      ObArray<const ObSimplePackageSchema *> packages;
      if (OB_SUCC(ret)) {
        if (OB_FAIL(schema_guard.get_simple_package_schemas_in_database(
                tenant_id, database_id, packages))) {
          LOG_WARN("failed to get package schemas in database", KR(ret),
                   K(tenant_id), K(database_id));
        } else if (packages.count() > 0) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "fork database containing packages");
          LOG_WARN("fork database with packages is not supported", K(ret),
                   K(tenant_id), K(database_id), "package_count", packages.count());
        }
      }

      // Check if database has Trigger
      ObArray<uint64_t> trigger_ids;
      if (OB_SUCC(ret)) {
        if (OB_FAIL(schema_guard.get_trigger_ids_in_database(
                tenant_id, database_id, trigger_ids))) {
          LOG_WARN("failed to get trigger ids in database", KR(ret),
                   K(tenant_id), K(database_id));
        } else if (trigger_ids.count() > 0) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "fork database containing triggers");
          LOG_WARN("fork database with triggers is not supported", K(ret),
                   K(tenant_id), K(database_id), "trigger_count", trigger_ids.count());
        }
      }

      // Check if database has Sequence
      ObArray<const ObSequenceSchema *> sequences;
      if (OB_SUCC(ret)) {
        if (OB_FAIL(schema_guard.get_sequence_schemas_in_database(
                tenant_id, database_id, sequences))) {
          LOG_WARN("failed to get sequence schemas in database", KR(ret),
                   K(tenant_id), K(database_id));
        } else if (sequences.count() > 0) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "fork database containing sequences");
          LOG_WARN("fork database with sequences is not supported", K(ret),
                   K(tenant_id), K(database_id), "sequence_count", sequences.count());
        }
      }

      // Check if database has Outline
      ObArray<const ObSimpleOutlineSchema *> outlines;
      if (OB_SUCC(ret)) {
        if (OB_FAIL(schema_guard.get_simple_outline_schemas_in_database(
                tenant_id, database_id, outlines))) {
          LOG_WARN("failed to get outline schemas in database", KR(ret),
                   K(tenant_id), K(database_id));
        } else if (outlines.count() > 0) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "fork database containing outlines");
          LOG_WARN("fork database with outlines is not supported", K(ret),
                   K(tenant_id), K(database_id), "outline_count", outlines.count());
        }
      }
    }

    // Get all user tables in source database
    if (OB_SUCC(ret)) {
      if (OB_FAIL(schema_guard.get_table_schemas_in_database(
              tenant_id, src_db_schema->get_database_id(),
              src_db_table_schemas))) {
        LOG_WARN("failed to get table schemas in database", KR(ret),
                 K(tenant_id), "database_id", src_db_schema->get_database_id());
      } else {
        // Filter user tables only and check for LOB aux tables
        for (int64_t i = 0; OB_SUCC(ret) && i < src_db_table_schemas.count();
             ++i) {
          const ObTableSchema *table_schema = src_db_table_schemas.at(i);
          if (OB_ISNULL(table_schema)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("table schema is null", KR(ret), K(i));
          } else if (!table_schema->is_user_table()) {
            // Skip non-user tables silently
          } else if (OB_FAIL(check_fork_table_supported(*table_schema,
                                                        schema_guard))) {
            LOG_WARN("fork table is not supported for source table", K(ret));
          } else if (OB_FAIL(user_table_schemas.push_back(table_schema))) {
            LOG_WARN("failed to push back table schema", KR(ret));
          }
        }
        if (OB_SUCC(ret)) {
          LOG_INFO("user tables in source database", K(tenant_id),
                   "database_name", src_db_schema->get_database_name(),
                   "total_table_count", src_db_table_schemas.count(),
                   "user_table_count", user_table_schemas.count());
        }
      }
    }

    // Start transaction and create destination database.
    ObDatabaseSchema dst_db_schema;
    if (OB_SUCC(ret)) {
      if (OB_FAIL(trans.start(&get_sql_proxy(), tenant_id,
                              refreshed_schema_version))) {
        LOG_WARN("start transaction failed", KR(ret), K(tenant_id),
                 K(refreshed_schema_version));
      } else if (OB_FAIL(dst_db_schema.assign(*src_db_schema))) {
        LOG_WARN("failed to assign database schema", KR(ret));
      } else {
        // Set new database name for destination database
        dst_db_schema.set_database_name(fork_database_arg.dst_database_name_);
        dst_db_schema.set_database_id(
            OB_INVALID_ID); // Will be assigned by create_database

        ObDDLOperator ddl_operator(*schema_service_, *sql_proxy_);
        if (OB_FAIL(ddl_operator.create_database(
                dst_db_schema, trans, &fork_database_arg.ddl_stmt_str_))) {
          LOG_WARN("failed to create destination database", KR(ret),
                   K(dst_db_schema));
        } else {
          LOG_INFO("destination database created", K(tenant_id), "dst_db_id",
                   dst_db_schema.get_database_id(), "dst_db_name",
                   dst_db_schema.get_database_name());
        }
      }
    }

    // Obtain snapshot for all user tables at once to ensure consistency.
    ObArray<ObDDLTaskRecord> task_records;
    int64_t fork_snapshot_version = 0;
    if (OB_SUCC(ret)) {
      if (user_table_schemas.count() == 0) {
        LOG_INFO("no user tables to fork", K(tenant_id), "src_database_name", fork_database_arg.src_database_name_);
      } else if (OB_FAIL(ObForkTableUtil::obtain_snapshot(trans, schema_guard,
                                                   tenant_id, user_table_schemas,
                                                   fork_snapshot_version))) {
        LOG_WARN("fail to obtain snapshot for all tables", K(ret),
                 "table_count", user_table_schemas.count());
      } else if (fork_snapshot_version <= 0) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid snapshot version", K(ret), K(fork_snapshot_version));
      } else {
        LOG_INFO("fork database snapshot acquired for all tables", K(tenant_id),
                 K(fork_snapshot_version), "table_count", user_table_schemas.count());
      }
    }

    // Fork each user table using the unified snapshot.
    for (int64_t i = 0; OB_SUCC(ret) && i < user_table_schemas.count(); ++i) {
      const ObTableSchema *src_table_schema = user_table_schemas.at(i);
      if (OB_ISNULL(src_table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table schema is null", KR(ret), K(i));
      } else {
        ObDDLTaskRecord task_record;
        ObString empty_ddl_stmt_str;

        // Use the unified helper function to fork the single table.
        if (OB_FAIL(fork_single_table_in_trans_(
                tenant_id, *src_table_schema, *src_db_schema, dst_db_schema,
                src_table_schema->get_table_name_str(), // Keep same table name
                fork_snapshot_version, fork_database_arg.session_id_,
                empty_ddl_stmt_str, schema_guard, trans, allocator,
                task_record))) {
          LOG_WARN("failed to fork single table in transaction", KR(ret), K(i),
                   "table_name", src_table_schema->get_table_name());
        } else if (OB_FAIL(task_records.push_back(task_record))) {
          LOG_WARN("failed to push back task record", KR(ret));
        }
      }
    }

    // End transaction.
    if (trans.is_started()) {
      bool commit = (OB_SUCCESS == ret);
      int tmp_ret = trans.end(commit);
      if (OB_SUCCESS != tmp_ret) {
        ret = (OB_SUCCESS == ret) ? tmp_ret : ret;
        LOG_WARN("trans end failed", K(ret), K(tmp_ret), K(commit));
      }
    }

    // Publish schema.
    if (OB_SUCC(ret)) {
      if (OB_FAIL(publish_schema(tenant_id))) {
        LOG_WARN("publish_schema failed", KR(ret), K(tenant_id));
      }
    }

    // Schedule all DDL tasks.
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < task_records.count(); ++i) {
        if (OB_FAIL(
                ObSysDDLSchedulerUtil::schedule_ddl_task(task_records.at(i)))) {
          LOG_WARN("fail to schedule ddl task", K(ret), K(i),
                   K(task_records.at(i)));
        }
      }
    }

    // Set response - use the first task if available, otherwise use database
    // id.
    if (OB_SUCC(ret)) {
      res.tenant_id_ = tenant_id;
      res.schema_id_ = dst_db_schema.get_database_id();
      res.task_id_ = task_records.count() > 0 ? task_records.at(0).task_id_ : 0;
      LOG_INFO("fork database completed", K(tenant_id), "dst_db_id",
               dst_db_schema.get_database_id(), "dst_db_name",
               dst_db_schema.get_database_name(), "forked_table_count",
               task_records.count(), "first_task_id", res.task_id_);
    }
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
