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

#include "query/session/ob_inner_sql_connection_access.h"
#include "rootserver/ddl_task/ob_sys_ddl_util.h"
#include "rootserver/fork_table/ob_fork_table_helper.h"
#include "rootserver/ob_ddl_operator.h"
#include "rootserver/ob_tablet_drop.h"
#include "rootserver/ob_ddl_service.h"
#include "rootserver/fork_table/ob_fork_table_util.h"
#include "rootserver/ob_rootserver_local_runtime.h"
#include "sql/resolver/ddl/ob_fts_index_builder_util.h"
#include "storage/ddl/ob_ddl_lock.h"
#include "storage/tablelock/ob_lock_inner_connection_util.h"
#include "rootserver/fork_table/namespace_fork_kernel_prototype.h"
#include "share/ob_snapshot_table_proxy.h"
#include "share/ob_debug_sync.h"
#include "storage/compaction/ob_freeze_info_mgr.h"
#include "rootserver/ddl_task/ob_ddl_task_util.h"
#include "observer/namespace_worker_protocol_prototype.h"
#include "namespace/namespace.h"

namespace oceanbase {
using namespace common;
using namespace share;
using namespace obcall;
using namespace storage;
namespace rootserver {

int ObDDLService::drop_namespace_prototype_(const ObString &name)
{
  bool done = false; uint64_t id = 0;
  int ret = NamespaceForkKernelPrototype::begin_namespace_drop(name, id, done);
  if (ret != OB_SUCCESS || done) { return ret; }
  ObSchemaGetterGuard guard; int64_t version = 0;
  ObArray<const ObDatabaseSchema *> databases;
  ObArray<ObTabletID> bound;
  ObDDLSQLTransaction trans(schema_service_);
  ObDDLOperator ddl_operator(*schema_service_, *sql_proxy_);
  if (OB_FAIL(observer::namespace_worker_prototype::drain_storage_namespace_access(id))) {
  } else if (OB_FAIL(get_runtime_schema_guard_with_version_in_inner_table(guard))) {
  } else if (OB_FAIL(guard.get_schema_version(version))) {
  } else if (id == 1 && OB_FAIL(guard.get_database_schemas_in_runtime(databases))) {
  } else if (OB_FAIL(trans.start(sql_proxy_, version))) {
  } else if (OB_FAIL(NamespaceForkKernelPrototype::lock_namespace_drop(trans, id, bound))) {
  } else {
    if (id == 1) {
      NamespaceSourceDropGuard capability(trans);
      if (!capability.is_valid()) { ret = OB_EAGAIN; }
      for (int64_t i = 0; OB_SUCC(ret) && i < databases.count(); ++i) {
        const auto &db = *databases.at(i);
        if (is_inner_db(db.get_database_id()) || db.get_database_name_str().prefix_match("__fork_proto_meta")) { continue; }
        if (OB_FAIL(lock_tables_of_database_for_drop(db, trans))) {
        } else if (OB_FAIL(lock_tables_in_recyclebin(db, trans))) {
        } else { ret = ddl_operator.drop_database(db, trans); }
      }
    }
    if (OB_SUCC(ret)) { ret = NamespaceForkKernelPrototype::finish_namespace_drop(trans, id); }
    if (OB_SUCC(ret)) {
      DEBUG_SYNC(AFTER_UPDATE_TABLET_TO_LS);
      ret = THIS_WORKER.check_status();
    }
  }
  if (trans.is_started()) { const int end = trans.end(ret == OB_SUCCESS); if (ret == OB_SUCCESS) { ret = end; } }
  if (OB_SUCC(ret) && id > 1) { ns::namespace_registry().remove(id); }
  int64_t released_tables = 0;
  int64_t released_databases = 0;
  int64_t released_storage_tables = 0;
  int64_t released_storage_databases = 0;
  if (OB_SUCC(ret) && id > 1) {
    ret = NamespaceForkKernelPrototype::release_namespace_schemas(
        id, released_tables, released_databases);
  }
  if (OB_SUCC(ret) && id > 1) {
    ret = observer::namespace_worker_prototype::release_storage_namespace_schemas(
        id, released_storage_tables, released_storage_databases);
  }
  if (OB_SUCC(ret)) {
    auto *freeze = share::server_service<ObFreezeInfoMgr>();
    ret = freeze ? freeze->reload_for_test() : OB_NOT_INIT;
  }
  if (OB_SUCC(ret) && id == 1) { ret = publish_schema(); }
  // A failed attempt leaves DELETING persisted. Reissuing the same operation resumes it.
  LOG_INFO("PROTOTYPE_V7_NAMESPACE_DROP", K(ret), K(name), K(id),
      "private_tablets", bound.count(), K(released_tables), K(released_databases),
      K(released_storage_tables), K(released_storage_databases));
  return ret;
}

int ObDDLService::namespace_command(
    const obcall::NamespaceCommandArg &namespace_command_arg, obcall::ObDDLRes &res) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
  } else if (!namespace_command_arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(namespace_command_arg));
  } else if (namespace_command_arg.target_name_ == "__drop__") {
    ret = drop_namespace_prototype_(namespace_command_arg.source_name_);
  } else {
    ret = NamespaceForkKernelPrototype::control_namespace(namespace_command_arg.source_name_,
        namespace_command_arg.target_name_, res.schema_id_);
  }
  return ret;
}

int ObDDLService::rebuild_fk_in_trans_(const common::ObIArray<const share::schema::ObTableSchema *> &user_table_schemas,
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
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < intra_db_fk_infos.count(); ++i) {
      const uint64_t child_id = intra_db_fk_infos.at(i).child_table_id_;
      ObSEArray<int64_t, 4> *idx_array = child_fk_groups.get(child_id);
      if (OB_NOT_NULL(idx_array)) {
        if (OB_FAIL(idx_array->push_back(i))) {
        }
      } else {
        ObSEArray<int64_t, 4> new_array;
        if (OB_FAIL(new_array.push_back(i))) {
        } else if (OB_FAIL(child_fk_groups.set_refactored(child_id, new_array))) {
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
      } else if (FALSE_IT(fk_info.foreign_key_id_ = OB_INVALID_ID)) {
        // fetch_new_constraint_id only allocates a fresh ID when the input is OB_INVALID_ID;
        // if passed a non-OB_INVALID_ID value smaller than the current counter it returns
        // that value unchanged, causing a primary-key duplicate on __all_foreign_key.
      } else if (OB_FAIL(schema_service->fetch_new_constraint_id(fk_info.foreign_key_id_))) {
      } else if (OB_FAIL(table_id_map.get_refactored(fk_info.child_table_id_, fk_info.child_table_id_))) {
      } else {
        uint64_t dst_parent_table_id = OB_INVALID_ID;
        if (OB_FAIL(table_id_map.get_refactored(intra_db_fk_infos.at(fk_idx).parent_table_id_,
                                                 dst_parent_table_id))) {
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
            } else {
              fk_info.ref_cst_id_ = dst_ref_cst_id;
            }
          }

          if (OB_SUCC(ret)) {
            if (OB_FAIL(rebuilt_fk_infos.push_back(fk_info))) {
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
      } else if (OB_FAIL(ddl_operator.add_table_foreign_keys(
                     *dst_child_schema, inc_table_schema, trans))) {
      } else if (OB_FAIL(ddl_operator.update_table_attribute(
                     inc_table_schema, trans, OB_DDL_ALTER_TABLE))) {
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
