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
#include "rootserver/ob_ddl_operator.h"
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

    // Collect intra-database foreign keys from user tables.
    ObArray<ObForeignKeyInfo> intra_db_fk_infos;
    if (OB_SUCC(ret)) {
      const uint64_t src_database_id = src_db_schema->get_database_id();
      for (int64_t i = 0; OB_SUCC(ret) && i < user_table_schemas.count(); ++i) {
        const ObTableSchema *table_schema = user_table_schemas.at(i);
        if (OB_ISNULL(table_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("table schema is null", KR(ret), K(i));
        } else {
          const ObIArray<ObForeignKeyInfo> &fk_infos = table_schema->get_foreign_key_infos();
          for (int64_t j = 0; OB_SUCC(ret) && j < fk_infos.count(); ++j) {
            const ObForeignKeyInfo &fk_info = fk_infos.at(j);
            if (fk_info.child_table_id_ != table_schema->get_table_id()) {
              // Only collect from the child side to avoid duplicates.
            } else if (fk_info.is_parent_table_mock_) {
              LOG_INFO("skip mock foreign key during fork database",
                       "fk_name", fk_info.foreign_key_name_,
                       K(fk_info.child_table_id_), K(fk_info.parent_table_id_));
            } else {
              const ObTableSchema *parent_table_schema = nullptr;
              if (OB_FAIL(schema_guard.get_table_schema(
                      tenant_id, fk_info.parent_table_id_, parent_table_schema))) {
                LOG_WARN("failed to get parent table schema", KR(ret),
                         K(fk_info.parent_table_id_));
              } else if (OB_ISNULL(parent_table_schema)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("parent table schema is null", KR(ret),
                         K(fk_info.parent_table_id_));
              } else if (parent_table_schema->get_database_id() != src_database_id) {
                LOG_INFO("skip cross-database foreign key during fork database",
                         "fk_name", fk_info.foreign_key_name_,
                         K(fk_info.child_table_id_), K(fk_info.parent_table_id_),
                         "parent_db_id", parent_table_schema->get_database_id(),
                         K(src_database_id));
              } else if (OB_FAIL(intra_db_fk_infos.push_back(fk_info))) {
                LOG_WARN("failed to push back fk info", KR(ret));
              }
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        LOG_INFO("intra-database foreign keys collected", K(tenant_id),
                 "fk_count", intra_db_fk_infos.count());
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
    // Build table_id_map for FK rebuild: src_table_id/index_id -> dst_table_id/index_id
    common::hash::ObHashMap<uint64_t, uint64_t> table_id_map;
    ObArray<ObSArray<ObTableSchema>> all_dst_table_schemas;
    const bool need_fk_rebuild = (intra_db_fk_infos.count() > 0);
    if (OB_SUCC(ret) && need_fk_rebuild) {
      if (OB_FAIL(table_id_map.create(
              common::max(user_table_schemas.count() * 4, static_cast<int64_t>(64)),
              lib::ObLabel("ForkDbIdMap")))) {
        LOG_WARN("failed to create table id map", KR(ret));
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < user_table_schemas.count(); ++i) {
      const ObTableSchema *src_table_schema = user_table_schemas.at(i);
      if (OB_ISNULL(src_table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table schema is null", KR(ret), K(i));
      } else {
        ObDDLTaskRecord task_record;
        ObString empty_ddl_stmt_str;
        ObSArray<ObTableSchema> dst_table_schemas_for_table;

        if (OB_FAIL(fork_single_table_in_trans_(
                tenant_id, *src_table_schema, *src_db_schema, dst_db_schema,
                src_table_schema->get_table_name_str(),
                fork_snapshot_version, fork_database_arg.session_id_,
                empty_ddl_stmt_str, schema_guard, trans, allocator,
                task_record,
                need_fk_rebuild ? &table_id_map : nullptr,
                need_fk_rebuild ? &dst_table_schemas_for_table : nullptr))) {
          LOG_WARN("failed to fork single table in transaction", KR(ret), K(i),
                   "table_name", src_table_schema->get_table_name());
        } else if (OB_FAIL(task_records.push_back(task_record))) {
          LOG_WARN("failed to push back task record", KR(ret));
        } else if (need_fk_rebuild && OB_FAIL(all_dst_table_schemas.push_back(dst_table_schemas_for_table))) {
          LOG_WARN("failed to push back dst table schemas", KR(ret));
        }
      }
    }

    // Rebuild foreign keys before committing the transaction.
    if (OB_SUCC(ret) && need_fk_rebuild) {
      if (OB_FAIL(rebuild_fk_in_trans_(
              tenant_id,
              user_table_schemas,
              intra_db_fk_infos,
              table_id_map,
              all_dst_table_schemas,
              trans))) {
        LOG_WARN("failed to rebuild foreign keys in transaction", KR(ret));
      }
    }
    if (table_id_map.created()) {
      table_id_map.destroy();
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

int ObDDLService::rebuild_fk_in_trans_(
    const uint64_t tenant_id,
    const common::ObIArray<const share::schema::ObTableSchema *> &user_table_schemas,
    const common::ObIArray<share::schema::ObForeignKeyInfo> &intra_db_fk_infos,
    common::hash::ObHashMap<uint64_t, uint64_t> &table_id_map,
    const common::ObIArray<common::ObSArray<share::schema::ObTableSchema>> &all_dst_table_schemas,
    ObDDLSQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSchemaService *schema_service = schema_service_->get_schema_service();
  ObDDLOperator ddl_operator(*schema_service_, *sql_proxy_);
  if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    LOG_WARN("schema_service must not null", K(ret));
  }

  // Group FK infos by child_table_id for batched add_table_foreign_keys calls.
  common::hash::ObHashMap<uint64_t, ObSEArray<int64_t, 4>> child_fk_groups;
  if (OB_SUCC(ret)) {
    if (OB_FAIL(child_fk_groups.create(
            common::max(intra_db_fk_infos.count(), static_cast<int64_t>(16)),
            lib::ObLabel("ForkDbFkGrp")))) {
      LOG_WARN("failed to create child fk groups map", KR(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < intra_db_fk_infos.count(); ++i) {
      const uint64_t child_id = intra_db_fk_infos.at(i).child_table_id_;
      ObSEArray<int64_t, 4> *idx_array = child_fk_groups.get(child_id);
      if (OB_NOT_NULL(idx_array)) {
        if (OB_FAIL(idx_array->push_back(i))) {
          LOG_WARN("failed to push back fk index", KR(ret));
        }
      } else {
        ObSEArray<int64_t, 4> new_array;
        if (OB_FAIL(new_array.push_back(i))) {
          LOG_WARN("failed to push back fk index", KR(ret));
        } else if (OB_FAIL(child_fk_groups.set_refactored(child_id, new_array))) {
          LOG_WARN("failed to insert child fk group", KR(ret), K(child_id));
        }
      }
    }
  }

  // For each child table group, rebuild FKs and persist.
  for (common::hash::ObHashMap<uint64_t, ObSEArray<int64_t, 4>>::const_iterator
           group_it = child_fk_groups.begin();
       OB_SUCC(ret) && group_it != child_fk_groups.end(); ++group_it) {
    const uint64_t src_child_table_id = group_it->first;
    const ObSEArray<int64_t, 4> &fk_indices = group_it->second;
    uint64_t dst_child_table_id = OB_INVALID_ID;

    if (fk_indices.count() == 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fk indices is empty", KR(ret), K(src_child_table_id));
      break;
    }

    if (OB_FAIL(table_id_map.get_refactored(src_child_table_id, dst_child_table_id))) {
      LOG_WARN("failed to get dst child table id from map", KR(ret), K(src_child_table_id));
      break;
    }

    // Find the dst child table schema from all_dst_table_schemas.
    const ObTableSchema *dst_child_schema = nullptr;
    for (int64_t t = 0; OB_SUCC(ret) && t < all_dst_table_schemas.count() && OB_ISNULL(dst_child_schema); ++t) {
      if (all_dst_table_schemas.at(t).count() > 0
          && all_dst_table_schemas.at(t).at(0).get_table_id() == dst_child_table_id) {
        dst_child_schema = &all_dst_table_schemas.at(t).at(0);
      }
    }
    if (OB_ISNULL(dst_child_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("dst child table schema not found", KR(ret), K(dst_child_table_id));
      break;
    }

    ObTableSchema inc_table_schema;
    if (OB_FAIL(inc_table_schema.assign(*dst_child_schema))) {
      LOG_WARN("failed to assign dst child table schema", KR(ret));
      break;
    }
    inc_table_schema.reset_foreign_key_infos();

    ObSEArray<ObForeignKeyInfo, 4> rebuilt_fk_infos;
    for (int64_t fi = 0; OB_SUCC(ret) && fi < fk_indices.count(); ++fi) {
      const int64_t fk_idx = fk_indices.at(fi);
      ObForeignKeyInfo fk_info;
      if (OB_FAIL(fk_info.assign(intra_db_fk_infos.at(fk_idx)))) {
        LOG_WARN("failed to assign fk info", KR(ret));
      } else if (FALSE_IT(fk_info.foreign_key_id_ = OB_INVALID_ID)) {
        // fetch_new_constraint_id only allocates a fresh ID when the input is OB_INVALID_ID;
        // if passed a non-OB_INVALID_ID value smaller than the current counter it returns
        // that value unchanged, causing a primary-key duplicate on __all_foreign_key.
      } else if (OB_FAIL(schema_service->fetch_new_constraint_id(
                     tenant_id, fk_info.foreign_key_id_))) {
        LOG_WARN("failed to fetch new foreign key id", KR(ret), K(tenant_id));
      } else if (OB_FAIL(table_id_map.get_refactored(fk_info.child_table_id_, fk_info.child_table_id_))) {
        LOG_WARN("failed to map child table id", KR(ret), K(fk_info.child_table_id_));
      } else {
        uint64_t dst_parent_table_id = OB_INVALID_ID;
        if (OB_FAIL(table_id_map.get_refactored(intra_db_fk_infos.at(fk_idx).parent_table_id_,
                                                 dst_parent_table_id))) {
          LOG_WARN("failed to map parent table id", KR(ret),
                   "src_parent", intra_db_fk_infos.at(fk_idx).parent_table_id_);
        } else {
          fk_info.parent_table_id_ = dst_parent_table_id;
          fk_info.table_id_ = fk_info.child_table_id_;

          // Handle ref_cst_id_ mapping based on fk_ref_type_.
          const ObForeignKeyInfo &orig_fk = intra_db_fk_infos.at(fk_idx);
          if (FK_REF_TYPE_PRIMARY_KEY == fk_info.fk_ref_type_
              || (FK_REF_TYPE_NON_UNIQUE_KEY == fk_info.fk_ref_type_
                  && orig_fk.ref_cst_id_ == orig_fk.parent_table_id_)) {
            if (FK_REF_TYPE_PRIMARY_KEY == fk_info.fk_ref_type_) {
              fk_info.ref_cst_id_ = common::OB_INVALID_ID;
            } else {
              fk_info.ref_cst_id_ = dst_parent_table_id;
            }
          } else {
            // ref_cst_id_ is an index table id, map through table_id_map.
            uint64_t dst_ref_cst_id = OB_INVALID_ID;
            if (OB_FAIL(table_id_map.get_refactored(orig_fk.ref_cst_id_, dst_ref_cst_id))) {
              LOG_WARN("failed to map ref_cst_id (index table id)", KR(ret),
                       "src_ref_cst_id", orig_fk.ref_cst_id_);
            } else {
              fk_info.ref_cst_id_ = dst_ref_cst_id;
            }
          }

          if (OB_SUCC(ret)) {
            if (OB_FAIL(rebuilt_fk_infos.push_back(fk_info))) {
              LOG_WARN("failed to push back rebuilt fk info", KR(ret));
            } else if (fk_info.parent_table_id_ != fk_info.child_table_id_
                       && OB_FAIL(inc_table_schema.add_depend_table_id(fk_info.parent_table_id_))) {
              LOG_WARN("failed to add depend table id", KR(ret));
            }
          }
        }
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(inc_table_schema.set_foreign_key_infos(rebuilt_fk_infos))) {
        LOG_WARN("failed to set foreign key infos on inc table schema", KR(ret));
      } else if (OB_FAIL(ddl_operator.add_table_foreign_keys(
                     *dst_child_schema, inc_table_schema, trans))) {
        LOG_WARN("failed to add table foreign keys", KR(ret),
                 K(dst_child_table_id));
      } else if (OB_FAIL(ddl_operator.update_table_attribute(
                     inc_table_schema, trans, OB_DDL_ALTER_TABLE))) {
        LOG_WARN("failed to update table attribute after adding foreign keys",
                 KR(ret), K(dst_child_table_id));
      } else {
        LOG_INFO("foreign keys rebuilt for table", K(dst_child_table_id),
                 "fk_count", rebuilt_fk_infos.count());
      }
    }
  }

  if (child_fk_groups.created()) {
    child_fk_groups.destroy();
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
