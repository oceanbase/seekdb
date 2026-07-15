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
// this file was  share/ob_ddl_common.cpp created by function-level splitting from:these ObDDLUtil static methods
// implementation depends on this module,callers are all in upper layers;declaration remains in share/ob_ddl_common.h。
#define USING_LOG_PREFIX SHARE

#include "share/ob_ex_rpc.h"
#include "observer/ob_service.h"
#include "share/ob_ddl_common.h"
#include "observer/omt/ob_multi_tenant.h"  // previously hidden behind the server_struct include chain, make the dependency explicit
#include "storage/mview/ob_mview_refresh_helper.h"
#include "storage/ob_storage_rpc.h"
#include "storage/ob_storage_rpc_arg.h"
#include "share/ob_rpc_struct.h"
#include "share/ob_ddl_checksum.h"
#include "share/ob_ddl_sim_point.h"
#include "common/object/ob_object.h"
#include "share/compaction/ob_shared_storage_compaction_util.h"
#ifdef OB_BUILD_SHARED_STORAGE
#include "close_modules/shared_storage/meta_store/ob_shared_storage_obj_meta.h"
#endif
#include "share/tablet/ob_tablet_table_operator.h"
#include "share/storage/ob_tablet_replica_checksum_table_storage.h"
#include "rootserver/ddl_task/ob_index_build_task.h"
#include "rootserver/ob_root_service.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "sql/resolver/ddl/ob_ddl_resolver.h"

#include "sql/ob_sql_utils.h"
#include "sql/engine/px/ob_px_dtl_msg.h"
#include "observer/vector_index/ob_plugin_vector_index_utils.h"
#include "observer/vector_index/ob_vector_index_util.h"
#include "sql/resolver/ddl/ob_fts_index_builder_util.h"
#include "share/location_cache/ob_location_service.h"
#include "storage/tx/ob_ts_mgr.h"
#include "storage/tablet/ob_tablet_binding_helper.h"
#include "storage/ddl/ob_group_write_macro_block_task.h"
#include "rootserver/ddl_task/ob_ddl_task.h"
#include "rootserver/ob_index_builder.h"
#include "lib/worker.h"

using namespace oceanbase::share;
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using namespace oceanbase::obcall;
using namespace oceanbase::sql;

int ObDDLUtil::get_sys_log_handler_role_and_proposal_id(
    common::ObRole &role,
    int64_t &proposal_id)
{
  int ret = OB_SUCCESS;
  role = FOLLOWER;
  proposal_id = 0;
  if (OB_ISNULL(GCTX.omt_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.omt_));
  } else if (OB_UNLIKELY(!GCTX.omt_->has_tenant())) {
    ret = OB_TENANT_NOT_EXIST;
    LOG_WARN("local server does not have SYS tenant resource", KR(ret));
  } else {
    MOD_SCOPE {
      ObLSService *ls_svr = share::g_mp->ls_service();
      ObLS *ls = NULL;
      ObLSHandle handle;
      logservice::ObLogHandler *log_handler = NULL;
      if (OB_ISNULL(ls_svr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("mtl ObLSService should not be null", KR(ret), KP(ls_svr));
      } else if (OB_FAIL(ls_svr->get_ls(SYS_LS, handle, ObLSGetMod::OBSERVER_MOD))) {
        LOG_WARN("get ls failed", KR(ret));
      } else if (OB_ISNULL(ls = handle.get_ls())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls should not be null", KR(ret));
      } else if (OB_ISNULL(log_handler = ls->get_log_handler())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("log_handler is null", KR(ret), KP(log_handler));
      } else if (OB_FAIL(log_handler->get_role(role, proposal_id))) {
        LOG_WARN("fail to get role and epoch", KR(ret));
      }
    }
  }
  return ret;
}

int ObDDLUtil::hold_snapshot(
    common::ObMySQLTransaction &trans,
    rootserver::ObDDLTask* task,
    const uint64_t table_id,
    const uint64_t target_table_id,
    rootserver::ObRootService *root_service,
    const int64_t snapshot_version,
    const common::ObIArray<common::ObTabletID> *extra_mv_tablet_ids)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(task) || OB_ISNULL(root_service)) {
    ret = OB_BAD_NULL_ERROR;
    LOG_WARN("invalid argument", K(ret), KP(task), KP(root_service));
  } else if (!task->is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("args have not been inited", K(ret), K(task->get_task_type()));
  } else {
    ObSEArray<ObTabletID, 1> tablet_ids;
    SCN snapshot_scn;
    ObSchemaGetterGuard schema_guard;
    const ObTableSchema *data_table_schema = nullptr;
    const ObTableSchema *dest_table_schema = nullptr;
    
    int64_t schema_version = task->get_src_schema_version();
    ObMultiVersionSchemaService &schema_service = ObMultiVersionSchemaService::get_instance();
    if (OB_UNLIKELY(snapshot_version < 0)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid arguments", K(ret), K(snapshot_version));
    } else if (OB_FAIL(DDL_SIM(task->get_task_id(), DDL_TASK_HOLD_SNAPSHOT_FAILED))) {
      LOG_WARN("ddl sim failure", K(ret), K(task->get_task_id()));
    } else if (OB_FAIL(snapshot_scn.convert_for_tx(snapshot_version))) {
      LOG_WARN("failed to convert", K(snapshot_version), K(ret));
    } else if (OB_FAIL(schema_service.get_tenant_schema_guard(schema_guard))) {
      LOG_WARN("get tenant schema guard failed", K(ret));
    } else if (OB_FAIL(schema_guard.get_table_schema( table_id, data_table_schema))) {
      LOG_WARN("get table schema failed", K(ret), K(table_id));
    } else if (OB_FAIL(schema_guard.get_table_schema( target_table_id, dest_table_schema))) {
      LOG_WARN("get table schema failed", K(ret), K(target_table_id));
    } else if (OB_ISNULL(data_table_schema) || OB_ISNULL(dest_table_schema)) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("table not exist", K(ret), K(table_id), K(target_table_id), KP(data_table_schema), KP(dest_table_schema));
    } else if (OB_FAIL(ObDDLUtil::get_tablets(table_id, tablet_ids))) {
      LOG_WARN("failed to get data table snapshot", K(ret), K(table_id));
    } else if (OB_FAIL(ObDDLUtil::get_tablets(target_table_id, tablet_ids))) {
      LOG_WARN("failed to get dest table snapshot", K(ret), K(target_table_id));
    } else if (data_table_schema->get_aux_lob_meta_tid() != OB_INVALID_ID &&
              OB_FAIL(ObDDLUtil::get_tablets(data_table_schema->get_aux_lob_meta_tid(), tablet_ids))) {
      LOG_WARN("failed to get data lob meta table snapshot", K(ret));
    } else if (data_table_schema->get_aux_lob_piece_tid() != OB_INVALID_ID &&
              OB_FAIL(ObDDLUtil::get_tablets(data_table_schema->get_aux_lob_piece_tid(), tablet_ids))) {
      LOG_WARN("failed to get data lob piece table snapshot", K(ret));
    } else if (dest_table_schema->get_aux_lob_meta_tid() != OB_INVALID_ID &&
              OB_FAIL(ObDDLUtil::get_tablets(dest_table_schema->get_aux_lob_meta_tid(), tablet_ids))) {
      LOG_WARN("failed to get dest lob meta table snapshot", K(ret));
    } else if (dest_table_schema->get_aux_lob_piece_tid() != OB_INVALID_ID &&
              OB_FAIL(ObDDLUtil::get_tablets(dest_table_schema->get_aux_lob_piece_tid(), tablet_ids))) {
      LOG_WARN("failed to get dest lob piece table snapshot", K(ret));
    } else {
      rootserver::ObDDLService &ddl_service = root_service->get_ddl_service();
      if (OB_FAIL(ddl_service.get_snapshot_mgr().batch_acquire_snapshot(
          trans, SNAPSHOT_FOR_DDL, schema_version, snapshot_scn, nullptr, tablet_ids))) {
        LOG_WARN("batch acquire snapshot failed", K(ret), K(tablet_ids));
      } else if (OB_NOT_NULL(extra_mv_tablet_ids) &&
                 !extra_mv_tablet_ids->empty() &&
                 OB_FAIL(ddl_service.get_snapshot_mgr().batch_acquire_snapshot(
                     trans, SNAPSHOT_FOR_MAJOR_REFRESH_MV, schema_version, snapshot_scn,
                     nullptr, *extra_mv_tablet_ids))) {
        LOG_WARN("batch acquire mv snapshot failed", K(ret), K(extra_mv_tablet_ids));
      }
    }
    task->add_event_info("hold snapshot finish");
    LOG_INFO("hold snapshot finished", K(ret), K(task->get_snapshot_version()), K(table_id), K(target_table_id), K(schema_version), "ddl_event_info", ObDDLEventInfo());
  }
  return ret;
}

int ObDDLUtil::hold_snapshot(
    common::ObMySQLTransaction &trans,
    const ObTableSchema &data_table_schema,
    const ObTableSchema &index_table_schema,
    const int64_t snapshot)
{
  int ret = OB_SUCCESS;
  SCN snapshot_scn;
  
  const int64_t data_table_id = data_table_schema.get_table_id();
  const int64_t index_table_id = index_table_schema.get_table_id();
  const int64_t schema_version = index_table_schema.get_schema_version();
  if (snapshot <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("snapshot version not valid", K(ret), K(snapshot));
  } else if (OB_FAIL(snapshot_scn.convert_for_tx(snapshot))) {
    LOG_WARN("failed to convert", K(snapshot), K(ret));
  } else {
    rootserver::ObDDLService &ddl_service = GCTX.root_service_->get_ddl_service();
    ObSEArray<ObTabletID, 2> tablet_ids;
    bool need_acquire_lob = false;
    if (OB_FAIL(data_table_schema.get_tablet_ids(tablet_ids))) {
      LOG_WARN("failed to get data table snapshot", K(ret));
    } else if (OB_FAIL(index_table_schema.get_tablet_ids(tablet_ids))) {
      LOG_WARN("failed to get data table snapshot", K(ret));
    } else if (OB_FAIL(check_need_acquire_lob_snapshot(&data_table_schema, &index_table_schema, need_acquire_lob))) {
      LOG_WARN("failed to check if need to acquire lob snapshot", K(ret));
    } else if (need_acquire_lob && data_table_schema.get_aux_lob_meta_tid() != OB_INVALID_ID &&
               OB_FAIL(ObDDLUtil::get_tablets(data_table_schema.get_aux_lob_meta_tid(), tablet_ids))) {
      LOG_WARN("failed to get data lob meta table snapshot", K(ret));
    } else if (need_acquire_lob && data_table_schema.get_aux_lob_piece_tid() != OB_INVALID_ID &&
               OB_FAIL(ObDDLUtil::get_tablets(data_table_schema.get_aux_lob_piece_tid(), tablet_ids))) {
      LOG_WARN("failed to get data lob piece table snapshot", K(ret));
    } else if (OB_FAIL(ddl_service.get_snapshot_mgr().batch_acquire_snapshot(
            trans, SNAPSHOT_FOR_DDL, schema_version, snapshot_scn, nullptr, tablet_ids))) {
      LOG_WARN("batch acquire snapshot failed", K(ret), K(tablet_ids));
    }
  }
  LOG_INFO("hold snapshot finished", K(ret), K(snapshot), K(data_table_id), K(index_table_id), K(schema_version));
  return ret;
}

int ObDDLUtil::construct_domain_index_arg(ObSchemaGetterGuard &schema_guard,
    const ObTableSchema *table_schema,
    const ObTableSchema *&index_schema,
    rootserver::ObDDLTask &task,
    ObCreateIndexArg &create_index_arg,
    ObDDLType &ddl_type)
{
  int ret = OB_SUCCESS;
  rootserver::ObRootService *root_service = GCTX.root_service_;
  if (OB_ISNULL(root_service)) {
    ret = OB_ERR_SYS;
    LOG_WARN("error sys, root service must not be nullptr", K(ret));
  } else if (OB_ISNULL(table_schema) || OB_ISNULL(index_schema)) {
    ret = OB_ERR_SYS;
    LOG_WARN("error sys, table schema must not be nullptr", K(ret));
  } else if (index_schema->is_vec_hnsw_index()) {
    ddl_type = ObDDLType::DDL_CREATE_VEC_INDEX;
  } else if (index_schema->is_vec_ivfflat_index()) {
    ddl_type = ObDDLType::DDL_CREATE_VEC_IVFFLAT_INDEX;
  } else if (index_schema->is_vec_ivfsq8_index()) {
    ddl_type = ObDDLType::DDL_CREATE_VEC_IVFSQ8_INDEX;
  } else if (index_schema->is_vec_ivfpq_index()) {
    ddl_type = ObDDLType::DDL_CREATE_VEC_IVFPQ_INDEX;
  } else if (index_schema->is_fts_index()) {
    ddl_type = ObDDLType::DDL_CREATE_FTS_INDEX;
  } else if (index_schema->is_multivalue_index()) {
    ddl_type = ObDDLType::DDL_CREATE_MULTIVALUE_INDEX;
  } else if (index_schema->is_vec_spiv_index()) {
    ddl_type = ObDDLType::DDL_CREATE_VEC_SPIV_INDEX;
  } else {
    ddl_type = get_create_index_type(task.get_data_format_version(), *index_schema);
  }

  ObSEArray<ObString, 1> col_names;
  create_index_arg.index_option_.reset();
  create_index_arg.is_offline_rebuild_ = true;
  create_index_arg.parallelism_ = task.get_parallelism();
  if (OB_FAIL(ret)) {
  } else if (index_schema->is_vec_index_snapshot_data_type()) {
    ObString domain_index_name;
    if (OB_FAIL(ObPluginVectorIndexUtils::get_vector_index_prefix(*index_schema, domain_index_name))) {
      LOG_WARN("failed to get domain index name", K(ret), KP(index_schema));
    } else if (OB_FAIL(schema_guard.get_table_schema( index_schema->get_database_id(), domain_index_name, true, index_schema, create_index_arg.is_offline_rebuild_, false))) {
      LOG_WARN("failed to get domain index schema", K(ret), K(domain_index_name));
    } else if (OB_ISNULL(index_schema)) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("get null domain table schema", K(ret), K(domain_index_name));
    } else {
      create_index_arg.index_type_ = index_schema->get_index_type();
    }
  }

  if (OB_FAIL(ret)) {
  } else if (index_schema->is_vec_index()
             && OB_FAIL(share::ObVectorIndexUtil::get_vector_index_column_name(*table_schema, *index_schema, col_names))) {
    LOG_WARN("fail to get vector index column name", K(ret), K(index_schema));
  } else if (index_schema->is_fts_index()
             && OB_FAIL(share::ObFtsIndexBuilderUtil::get_fts_index_column_name(*table_schema, *index_schema, col_names))) {
    LOG_WARN("fail to get fts index column name", K(ret), K(index_schema));
  } else if (index_schema->is_multivalue_index()
             && OB_FAIL(share::ObFtsIndexBuilderUtil::get_multivalue_index_column_name(
                 *table_schema, *index_schema, col_names))) {
    LOG_WARN("fail to get multivalue index column name", K(ret), K(index_schema));
  } else {
    FOREACH_X(it, col_names, OB_SUCC(ret)) {
      obcall::ObColumnSortItem sort_item;
      sort_item.column_name_ = (*it);
      if (OB_FAIL(create_index_arg.index_columns_.push_back(sort_item))) {
        LOG_WARN("failed to push back sort columns", K(ret), K(sort_item));
      }
    }
    if (OB_SUCC(ret) && index_schema->is_vec_delta_buffer_type()) {
      if (OB_FAIL(create_index_arg.index_schema_.assign(*index_schema))) {
        LOG_WARN("fail to assign index_schema", K(ret), KP(index_schema));
      }
    }
  }
  const ObSimpleDatabaseSchema *database_schema = nullptr;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(index_schema->get_index_name(create_index_arg.index_name_))) {
    LOG_WARN("failed to get index name", K(ret), KP(index_schema));
  } else if (OB_FAIL(schema_guard.get_database_schema( table_schema->get_database_id(), database_schema)) || OB_ISNULL(database_schema)) {
    LOG_WARN("failed to get database schema", K(ret), KP(database_schema));
  } else {
    create_index_arg.table_name_ = ObString(table_schema->get_table_name_str());
    create_index_arg.database_name_ = ObString(database_schema->get_database_name_str());
    
    
    if (ObDDLType::DDL_REBUILD_INDEX == task.get_task_type()) {
      // Only rebuild-index tasks reuse existing table ids. Offline domain-index
      // rebuild during table/column redefinition still goes through normal
      // create-index schema generation and must leave these ids invalid.
      create_index_arg.data_table_id_ = table_schema->get_table_id();
      create_index_arg.index_table_id_ = index_schema->get_table_id();
    }
    if (index_schema->is_fts_index()) {
      create_index_arg.index_option_.parser_name_ = index_schema->get_parser_name_str();
      create_index_arg.index_key_ = ObDDLResolver::INDEX_KEYNAME::FTS_KEY;
    }
  }
  return ret;
}

int ObDDLUtil::get_domain_index_share_table_snapshot(const ObTableSchema *table_schema,
    const ObTableSchema *index_schema,
    const int64_t task_id,
    const obcall::ObCreateIndexArg &create_index_arg,
    int64_t &fts_snapshot_version)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard new_schema_guard;
  rootserver::ObRootService *root_service = GCTX.root_service_;
  
  bool need_update_snapshot = false;
  if (OB_ISNULL(root_service) || OB_ISNULL(table_schema) || OB_ISNULL(index_schema)) {
    ret = OB_ERR_SYS;
    LOG_WARN("error sys, root service, table schema, index schema must not be nullptr", K(ret), K(root_service), K(table_schema), K(index_schema));
  } else if (OB_FAIL(ObDDLUtil::check_need_update_domain_index_share_table_snapshot(
                 table_schema, index_schema, task_id, create_index_arg, need_update_snapshot))) {
    LOG_WARN("fail to check need update domain index share table snapshot", K(ret));
  } else if (!need_update_snapshot) {
    // don't need update snapshot
  } else if (index_schema->is_fts_index() || index_schema->is_multivalue_index() || index_schema->is_vec_spiv_index()
             || index_schema->is_vec_hnsw_index()) {
    ObMySQLTransaction trans;
    const ObTableSchema *domain_index_share_schema = nullptr;
    uint64_t  domain_index_share_tid = 0;
    if ((index_schema->is_fts_index() || index_schema->is_multivalue_index() || index_schema->is_vec_spiv_index()) && OB_FAIL(table_schema->get_rowkey_doc_tid(domain_index_share_tid))) {
      LOG_WARN("failed to get rowkey doc table id", K(ret));
    } else if (index_schema->is_vec_hnsw_index() && OB_FAIL(table_schema->get_rowkey_vid_tid(domain_index_share_tid))) {
      LOG_WARN("failed to get rowkey vid table id", K(ret));
    } else if (OB_FAIL(root_service->get_ddl_service().get_tenant_schema_guard_with_version_in_inner_table(new_schema_guard))) {
      LOG_WARN("failed to refresh schema guard", K(ret));
    } else if (OB_FAIL(new_schema_guard.get_table_schema( domain_index_share_tid, domain_index_share_schema))) {
      LOG_WARN("get table schema failed", K(ret), K(domain_index_share_tid));
    } else if (OB_ISNULL(domain_index_share_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("error unexpected, rowkey doc/vid index schema must not be nullptr", K(ret));
    } else if (OB_FAIL(trans.start(GCTX.sql_proxy_))) {
      LOG_WARN("fail to start trans", K(ret));
    } else if (OB_FAIL(ObDDLUtil::obtain_snapshot(trans, *table_schema, *domain_index_share_schema, fts_snapshot_version))) {
      if (OB_SNAPSHOT_DISCARDED == ret) {
        LOG_INFO("snapshot discarded, need retry waiting trans", K(ret), K(fts_snapshot_version));
      } else {
        LOG_WARN("hold snapshot failed", K(ret), K(fts_snapshot_version));
      }
    }
    if (trans.is_started()) {
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_WARN("failed to commit trans", KR(ret), KR(tmp_ret));
        ret = OB_SUCC(ret) ? tmp_ret : ret;
      }
    }
  }
  return ret;
}

int ObDDLUtil::get_data_information(const uint64_t task_id,
    uint64_t &data_format_version,
    int64_t &snapshot_version,
    share::ObDDLTaskStatus &task_status,
    uint64_t &target_object_id,
    int64_t &schema_version,
    bool &is_no_logging,
    bool &is_offline_index_rebuild)
{
  int ret = OB_SUCCESS;
  data_format_version = 0;
  snapshot_version = 0;
  task_status = share::ObDDLTaskStatus::PREPARE;
  target_object_id = 0;
  data_format_version = 0;
  is_no_logging = false;
  is_offline_index_rebuild = false;
  if (OB_UNLIKELY(task_id <= 0
      || nullptr == GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(task_id), KP(GCTX.sql_proxy_));
  } else if (OB_FAIL(DDL_SIM(task_id, GET_DATA_FORMAT_VERISON_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(task_id));
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObSqlString query_string;
      sqlclient::ObMySQLResult *result = NULL;
      if (OB_FAIL(query_string.assign_fmt(" SELECT snapshot_version, ddl_type, UNHEX(message) as message_unhex, status, schema_version, target_object_id FROM %s WHERE task_id = %lu",
          OB_ALL_DDL_TASK_STATUS_TNAME, task_id))) {
        LOG_WARN("assign sql string failed", K(ret));
      } else if (OB_FAIL(GCTX.sql_proxy_->read(res, query_string.ptr()))) {
        LOG_WARN("read record failed", K(ret), K(query_string));
      } else if (OB_UNLIKELY(nullptr == (result = res.get_result()))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to get sql result", K(ret), KP(result));
      } else if (OB_FAIL(result->next())) {
        LOG_WARN("get next row failed", K(ret));
      } else {
        int64_t pos = 0;
        int cur_task_status = 0;
        ObDDLType ddl_type = ObDDLType::DDL_INVALID;
        ObString task_message;
        EXTRACT_UINT_FIELD_MYSQL(*result, "snapshot_version", snapshot_version, uint64_t);
        EXTRACT_INT_FIELD_MYSQL(*result, "ddl_type", ddl_type, ObDDLType);
        EXTRACT_VARCHAR_FIELD_MYSQL(*result, "message_unhex", task_message);
        EXTRACT_INT_FIELD_MYSQL(*result, "status", cur_task_status, int);
        EXTRACT_INT_FIELD_MYSQL(*result, "target_object_id", target_object_id, int64_t);
        EXTRACT_INT_FIELD_MYSQL(*result, "schema_version", schema_version, int64_t);

        task_status = static_cast<share::ObDDLTaskStatus>(cur_task_status);
        if (OB_SUCC(ret)) {
          switch (ddl_type) {
            case ObDDLType::DDL_CREATE_INDEX:
            case ObDDLType::DDL_CREATE_PARTITIONED_LOCAL_INDEX:
            {
              SMART_VAR(rootserver::ObIndexBuildTask, task) {
                if (OB_FAIL(task.deserialize_params_from_message(task_message.ptr(), task_message.length(), pos))) {
                  LOG_WARN("deserialize from msg failed", K(ret));
                } else {
                  data_format_version = task.get_data_format_version();
                  is_no_logging = task.get_is_no_logging();
                  is_offline_index_rebuild = task.is_offline_rebuild();
                }
              }
              break;
            }
            default:
            {
              SMART_VAR(rootserver::ObDDLTask, task) {
                if (OB_FAIL(task.deserialize_params_from_message(task_message.ptr(), task_message.length(), pos))) {
                  LOG_WARN("deserialize from msg failed", K(ret));
                } else {
                  data_format_version = task.get_data_format_version();
                  is_no_logging = task.get_is_no_logging();
                  is_offline_index_rebuild = false;
                }
              }
              break;
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObDDLUtil::generate_mview_ddl_schema_hint_str(const uint64_t mview_table_id,
    share::schema::ObSchemaGetterGuard &schema_guard,
    const ObIArray<ObBasedSchemaObjectInfo> &based_schema_object_infos,
    ObSqlString &sql_string)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator("ObDDLTmp");
  ObString database_name;
  ObString table_name;
  for (int64_t i = 0; OB_SUCC(ret) && i < based_schema_object_infos.count(); ++i) {
    const ObBasedSchemaObjectInfo &based_info = based_schema_object_infos.at(i);
    const ObTableSchema *table_schema = nullptr;
    const ObDatabaseSchema *database_schema = nullptr;
    database_name.reset();
    table_name.reset();
    allocator.reuse();
    if (OB_FAIL(schema_guard.get_table_schema( based_info.schema_id_, table_schema))) {
      LOG_WARN("fail to get table schema", KR(ret), K(based_info));
    } else if (OB_ISNULL(table_schema)) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("table not exist", KR(ret), K(based_info));
    } else if (OB_FAIL(schema_guard.get_database_schema( table_schema->get_database_id(),
                                                        database_schema))) {
      LOG_WARN("fail to get database schema", KR(ret),
               K(table_schema->get_database_id()));
    } else if (OB_ISNULL(database_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("error unexpected, database schema must not be nullptr", KR(ret));
    } else if (OB_FAIL(sql::ObSQLUtils::generate_new_name_with_escape_character(
                 allocator, database_schema->get_database_name_str(), database_name))) {
      LOG_WARN("fail to generate new name with escape character", KR(ret),
               K(database_schema->get_database_name_str()));
    } else if (OB_FAIL(sql::ObSQLUtils::generate_new_name_with_escape_character(
                 allocator, table_schema->get_table_name_str(), table_name))) {
      LOG_WARN("fail to generate new name with escape character", KR(ret),
               K(table_schema->get_table_name_str()));
    } else {
      if (OB_FAIL(sql_string.append_fmt("ob_ddl_schema_version(`%.*s`.`%.*s`, %ld) ",
                                        static_cast<int>(database_name.length()), database_name.ptr(),
                                        static_cast<int>(table_name.length()), table_name.ptr(),
                                        based_info.schema_version_))) {
        LOG_WARN("append sql string failed", KR(ret));
      }
    }
  }
  return ret;
}

int ObDDLUtil::generate_build_replica_sql(const int64_t data_table_id,
    const int64_t dest_table_id,
    const int64_t schema_version,
    const int64_t snapshot_version,
    const int64_t execution_id,
    const int64_t task_id,
    const int64_t parallelism,
    const bool use_heap_table_ddl_plan,
    const bool use_schema_version_hint_for_src_table,
    const ObColumnNameMap *col_name_map,
    const ObString &partition_names,
    ObSqlString &sql_string)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *source_table_schema = nullptr;
  const ObTableSchema *dest_table_schema = nullptr;
  if (OB_UNLIKELY(OB_INVALID_ID == data_table_id || OB_INVALID_ID == dest_table_id
      || schema_version <= 0 || snapshot_version <= 0 || execution_id < 0 || task_id <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(data_table_id), K(dest_table_id), K(schema_version),
                                  K(snapshot_version), K(execution_id), K(task_id));
  } else if (OB_FAIL(DDL_SIM(task_id, GENERATE_BUILD_REPLICA_SQL))) {
    LOG_WARN("ddl sim failure", K(ret), K(task_id));
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_tenant_schema_guard(
      schema_guard))) {
    LOG_WARN("fail to get tenant schema guard", K(ret), K(data_table_id));
  } else if (OB_FAIL(schema_guard.check_formal_guard())) {
    LOG_WARN("fail to check formal guard", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema( data_table_id, source_table_schema))) {
    LOG_WARN("fail to get table schema", K(ret), K(data_table_id));
  } else if (OB_FAIL(schema_guard.get_table_schema( dest_table_id, dest_table_schema))) {
    LOG_WARN("fail to get table schema", K(ret), K(dest_table_id));
  } else if (OB_ISNULL(source_table_schema) || OB_ISNULL(dest_table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("fail to get table schema", K(ret), KP(source_table_schema), KP(dest_table_schema), K(data_table_id), K(dest_table_id));
  } else {
    ObArray<ObColDesc> column_ids;
    ObArray<ObColumnNameInfo> column_names;
    ObArray<ObColumnNameInfo> insert_column_names;
    ObArray<ObColumnNameInfo> rowkey_column_names;
    ObArray<int64_t> select_column_ids;
    ObArray<int64_t> order_column_ids;
    bool is_shadow_column = false;
    const int64_t real_parallelism = ObDDLUtil::get_real_parallelism(parallelism, false/*is mv refresh*/);
    const bool is_rowkey_doc_aux_table = dest_table_schema->is_rowkey_doc_id();
    uint64_t doc_id_col_id = OB_INVALID_ID;
    uint64_t ft_id_col_id = OB_INVALID_ID;
    // get dest table column names
    if (OB_FAIL(dest_table_schema->get_column_ids(column_ids))) {
      LOG_WARN("fail to get column ids", K(ret));
    } else if (is_rowkey_doc_aux_table && OB_FAIL(dest_table_schema->get_fulltext_column_ids(doc_id_col_id, ft_id_col_id))) {
      LOG_WARN("fail to get fulltext column ids", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < column_ids.count(); ++i) {
        const ObColumnSchemaV2 *column_schema = nullptr;
        ObString orig_column_name;
        is_shadow_column = common::is_shadow_column(column_ids.at(i).col_id_);
        const bool is_doc_id_column = is_rowkey_doc_aux_table && column_ids.at(i).col_id_ == doc_id_col_id;
        const int64_t col_id = is_shadow_column ? column_ids.at(i).col_id_ - OB_MIN_SHADOW_COLUMN_ID : column_ids.at(i).col_id_;
        if (OB_ISNULL(column_schema = dest_table_schema->get_column_schema(col_id))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("error unexpected, column schema must not be nullptr", K(ret));
        } else if (is_shadow_column || is_doc_id_column) {
          // do nothing
        } else if (column_schema->is_generated_column()) {
          // cannot insert to generated columns.
        } else if (nullptr == col_name_map && OB_FALSE_IT(orig_column_name.assign_ptr(column_schema->get_column_name_str().ptr(), column_schema->get_column_name_str().length()))) {
        } else if (nullptr != col_name_map && OB_FAIL(col_name_map->get_orig_column_name(column_schema->get_column_name_str(), orig_column_name))) {
          if (OB_ENTRY_NOT_EXIST == ret) {
            // newly added column cannot be selected from source table.
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("failed to get orig column name", K(ret));
          }
        } else if (OB_FAIL(column_names.push_back(ObColumnNameInfo(orig_column_name, is_shadow_column)))) {
          LOG_WARN("fail to push back column name", K(ret));
        } else if (OB_FAIL(select_column_ids.push_back(col_id))) {
          LOG_WARN("push back select column id failed", K(ret), K(col_id));
        } else if (!is_shadow_column) {
          if (OB_FAIL(insert_column_names.push_back(ObColumnNameInfo(column_schema->get_column_name_str(), is_shadow_column)))) {
            LOG_WARN("push back insert column name failed", K(ret));
          }
        }
      }
    }
    if (OB_SUCC(ret) && dest_table_schema->need_partition_key_for_build_local_index(*source_table_schema)) {
      ObArray<ObColDesc> src_column_ids;
      ObSEArray<uint64_t, 5> extra_column_ids;
      if (OB_FAIL(source_table_schema->get_column_ids(src_column_ids))) {
        LOG_WARN("fail to get column ids", K(ret));
      } else {
        // Add part keys and their cascaded columns first
        for (int64_t i = 0; OB_SUCC(ret) && i < src_column_ids.count(); ++i) {
          const ObColumnSchemaV2 *column_schema = nullptr;
          const int64_t col_id = src_column_ids.at(i).col_id_;
          if (OB_ISNULL(column_schema = source_table_schema->get_column_schema(col_id))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("error unexpected, column schema must not be nullptr", K(ret));
          } else if (!column_schema->is_tbl_part_key_column()) {
            // do nothing
          } else if (OB_FAIL(extra_column_ids.push_back(col_id))) {
            LOG_WARN("failed to push column id", K(ret), K(col_id));
          } else if (column_schema->is_generated_column()) {
            ObSEArray<uint64_t, 5> cascaded_columns;
            if (OB_FAIL(column_schema->get_cascaded_column_ids(cascaded_columns))) {
              LOG_WARN("failed to get cascaded_column_ids", K(ret));
            } else {
              for (int64_t i = 0; OB_SUCC(ret) && i < cascaded_columns.count(); ++i) {
                uint64_t cascade_col_id = cascaded_columns.at(i);
                if (is_contain(extra_column_ids, cascade_col_id)) {
                } else if (OB_FAIL(extra_column_ids.push_back(cascade_col_id))) {
                  LOG_WARN("failed to push cascade column id", K(ret), K(cascade_col_id));
                }
              }
            }
          }
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < extra_column_ids.count(); ++i) {
          const ObColumnSchemaV2 *column_schema = nullptr;
          ObString orig_column_name;
          const int64_t col_id = extra_column_ids.at(i);
          if (OB_ISNULL(column_schema = source_table_schema->get_column_schema(col_id))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("error unexpected, column schema must not be nullptr", K(ret));
          } else if (is_contain(select_column_ids, col_id)) {
            // do nothing
          } else if (nullptr == col_name_map && OB_FALSE_IT(orig_column_name.assign_ptr(column_schema->get_column_name_str().ptr(), column_schema->get_column_name_str().length()))) {
          } else if (nullptr != col_name_map && OB_FAIL(col_name_map->get_orig_column_name(column_schema->get_column_name_str(), orig_column_name))) {
            if (OB_ENTRY_NOT_EXIST == ret) {
              // newly added column cannot be selected from source table.
              ret = OB_SUCCESS;
            } else {
              LOG_WARN("failed to get orig column name", K(ret));
            }
          } else if (OB_FAIL(column_names.push_back(ObColumnNameInfo(orig_column_name, false)))) {
            LOG_WARN("fail to push back column name", K(ret));
          } else if (OB_FAIL(insert_column_names.push_back(ObColumnNameInfo(column_schema->get_column_name_str(), false)))) {
            LOG_WARN("push back insert column name failed", K(ret));
          }
        }
      }
    }

    if (OB_SUCC(ret) && dest_table_schema->is_multivalue_index_aux()
        && OB_FAIL(ObDDLUtil::append_multivalue_extra_column(*dest_table_schema, *source_table_schema, column_names, select_column_ids))) {
      LOG_WARN("fail append extra column", K(ret));
    }

    if (OB_SUCC(ret) && dest_table_schema->is_spatial_index()) {
      if (OB_FAIL(ObDDLUtil::generate_spatial_index_column_names(*dest_table_schema, *source_table_schema, insert_column_names,
                                                                 column_names, select_column_ids))) {
        LOG_WARN("generate spatial index column names failed", K(ret));
      }
    }

    // get dest table rowkey columns
    if (OB_SUCC(ret)) {
      const ObRowkeyInfo &rowkey_info = dest_table_schema->get_rowkey_info();
      const ObRowkeyColumn *rowkey_column = nullptr;
      const ObColumnSchemaV2 *column_schema = nullptr;
      int64_t col_id = 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_info.get_size(); ++i) {
        if (OB_ISNULL(rowkey_column = rowkey_info.get_column(i))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("error unexpected, rowkey column must not be nullptr", K(ret));
        } else if (FALSE_IT(is_shadow_column = common::is_shadow_column(rowkey_column->column_id_))) {
        } else if (FALSE_IT(col_id = is_shadow_column ? rowkey_column->column_id_ - OB_MIN_SHADOW_COLUMN_ID : rowkey_column->column_id_)) {
        } else if (OB_ISNULL(column_schema = dest_table_schema->get_column_schema(col_id))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("error unexpected, column schema must not be nullptr", K(ret), K(col_id));
        } else if (column_schema->is_generated_column() &&
          !dest_table_schema->is_spatial_index() &&
          !dest_table_schema->is_multivalue_index_aux() &&
          !dest_table_schema->is_vec_spiv_index_aux()) {
          // generated columns cannot be row key.
        } else if (OB_FAIL(rowkey_column_names.push_back(ObColumnNameInfo(column_schema->get_column_name_str(), is_shadow_column)))) {
          LOG_WARN("fail to push back rowkey column name", K(ret));
        } else if (OB_FAIL(order_column_ids.push_back(col_id))) {
          LOG_WARN("push back order column id failed", K(ret), K(col_id));
        }
      }
    }

    // generate build replica sql
    if (OB_SUCC(ret)) {
      ObSqlString query_column_sql_string;
      ObSqlString insert_column_sql_string;
      ObSqlString rowkey_column_sql_string;
      ObSqlString src_table_schema_version_hint_sql_string;
      const ObString &dest_table_name = dest_table_schema->get_table_name_str();
      const uint64_t dest_database_id = dest_table_schema->get_database_id();
      ObString dest_database_name;
      const ObString &source_table_name = source_table_schema->get_table_name_str();
      const uint64_t source_database_id = source_table_schema->get_database_id();
      ObString source_database_name;

      if (OB_SUCC(ret)) {
        const ObDatabaseSchema *db_schema = nullptr;
        if (OB_FAIL(schema_guard.get_database_schema( dest_database_id, db_schema))) {
          LOG_WARN("fail to get database schema", K(ret), K(dest_database_id),
                   K(dest_table_id), K(data_table_id), K(source_database_id));
        } else if (OB_ISNULL(db_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("error unexpected, database schema must not be nullptr", K(ret));
        } else {
          dest_database_name = db_schema->get_database_name_str();
        }

        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(schema_guard.get_database_schema( source_database_id, db_schema))) {
          LOG_WARN("fail to get database schema", K(ret));
        } else if (OB_ISNULL(db_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("error unexpected, database schema must not be nullptr", K(ret));
        } else {
          source_database_name = db_schema->get_database_name_str();
        }

        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(generate_column_name_str(column_names, true/*with origin name*/, true/*with alias name*/, use_heap_table_ddl_plan, query_column_sql_string))) {
          LOG_WARN("fail to generate column name str", K(ret));
        } else if (OB_FAIL(generate_column_name_str(insert_column_names, true/*with origin name*/, false/*with alias name*/, use_heap_table_ddl_plan, insert_column_sql_string))) {
          LOG_WARN("generate column name str failed", K(ret));
        } else if (!use_heap_table_ddl_plan && OB_FAIL(generate_order_by_str(select_column_ids, order_column_ids, rowkey_column_sql_string))) {
          LOG_WARN("generate order by string failed", K(ret));
        }
      }

      if (OB_SUCC(ret)) {
        ObArenaAllocator allocator("ObDDLTmp");
        ObString new_dest_database_name;
        ObString new_dest_table_name;
        ObString new_source_table_name;
        ObString new_source_database_name;

        if (OB_FAIL(sql::ObSQLUtils::generate_new_name_with_escape_character(
              allocator,
              dest_database_name,
              new_dest_database_name))) {
          LOG_WARN("fail to generate new name with escape character",
                    K(ret), K(dest_database_name));
        } else if (OB_FAIL(sql::ObSQLUtils::generate_new_name_with_escape_character(
              allocator,
              dest_table_name,
              new_dest_table_name))) {
          LOG_WARN("fail to generate new name with escape character",
                    K(ret), K(dest_table_name));
        } else if (OB_FAIL(sql::ObSQLUtils::generate_new_name_with_escape_character(
              allocator,
              source_database_name,
              new_source_database_name))) {
          LOG_WARN("fail to generate new name with escape character",
                    K(ret), K(source_database_name));
        } else if (OB_FAIL(sql::ObSQLUtils::generate_new_name_with_escape_character(
              allocator,
              source_table_name,
              new_source_table_name))) {
          LOG_WARN("fail to generate new name with escape character",
                    K(ret), K(source_table_name));
        } else if (use_schema_version_hint_for_src_table) {
          if (OB_FAIL(generate_ddl_schema_hint_str(new_source_table_name, schema_version, src_table_schema_version_hint_sql_string))) {
            LOG_WARN("failed to generated ddl schema hint", K(ret));
          }
        }
        const char *io_read_hint = " ";
        if (dest_table_schema->is_vec_vid_rowkey_type()) {
          src_table_schema_version_hint_sql_string.reset();
        }
        const bool enable_newsort_for_aux =
            dest_table_schema->is_rowkey_doc_id()
            || dest_table_schema->is_doc_id_rowkey()
            || dest_table_schema->is_fts_index_aux()
            || dest_table_schema->is_fts_doc_word_aux()
            || dest_table_schema->is_multivalue_index_aux()
            || dest_table_schema->is_vec_spiv_index_aux();
        const char *sort_hint = enable_newsort_for_aux
            ? "opt_param('enable_newsort', 'true') "
            : "opt_param('enable_newsort', 'false') ";
        if (OB_FAIL(ret)) {
        } else {
          if (OB_FAIL(sql_string.assign_fmt("INSERT /*+ monitor enable_parallel_dml parallel(%ld) opt_param('ddl_execution_id', %ld) opt_param('ddl_task_id', %ld) %s%.*s use_px */INTO `%.*s`.`%.*s` %.*s(%.*s) SELECT /*+ index(`%.*s` primary) %.*s */ %.*s from `%.*s`.`%.*s` %.*s as of snapshot %ld %.*s",
              real_parallelism, execution_id, task_id,
              sort_hint,
              static_cast<int>(strlen(io_read_hint)), io_read_hint,
              static_cast<int>(new_dest_database_name.length()), new_dest_database_name.ptr(), static_cast<int>(new_dest_table_name.length()), new_dest_table_name.ptr(),
              static_cast<int>(partition_names.length()), partition_names.ptr(),
              static_cast<int>(insert_column_sql_string.length()), insert_column_sql_string.ptr(),
              static_cast<int>(new_source_table_name.length()), new_source_table_name.ptr(),
              static_cast<int>(src_table_schema_version_hint_sql_string.length()), src_table_schema_version_hint_sql_string.ptr(),
              static_cast<int>(query_column_sql_string.length()), query_column_sql_string.ptr(),
              static_cast<int>(new_source_database_name.length()), new_source_database_name.ptr(), static_cast<int>(new_source_table_name.length()), new_source_table_name.ptr(),
              static_cast<int>(partition_names.length()), partition_names.ptr(),
              snapshot_version, static_cast<int>(rowkey_column_sql_string.length()), rowkey_column_sql_string.ptr()))) {
            LOG_WARN("fail to assign sql string", K(ret));
          }
        }
      }
    }
    LOG_INFO("execute sql", K(sql_string));
  }
  return ret;
}

int ObDDLUtil::generate_build_mview_replica_sql(const int64_t mview_table_id,
    const int64_t container_table_id,
    ObSchemaGetterGuard &schema_guard,
    const int64_t snapshot_version,
    const uint64_t mview_target_data_sync_scn,
    const int64_t execution_id,
    const int64_t task_id,
    const int64_t parallelism,
    const bool use_schema_version_hint_for_src_table,
    const ObIArray<ObBasedSchemaObjectInfo> &based_schema_object_infos,
    const ObString &mview_select_sql,
    ObSqlString &sql_string)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_ID == mview_table_id ||
                  OB_INVALID_ID == container_table_id || snapshot_version <= 0 ||
                  execution_id < 0 || task_id <= 0 || based_schema_object_infos.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(mview_table_id), K(container_table_id),
             K(snapshot_version), K(execution_id), K(task_id), K(based_schema_object_infos));
  } else {
    const ObTableSchema *mview_table_schema = nullptr;
    const ObTableSchema *container_table_schema = nullptr;
    const ObDatabaseSchema *database_schema = nullptr;
    if (OB_FAIL(schema_guard.get_table_schema( mview_table_id, mview_table_schema))) {
      LOG_WARN("fail to get table schema", KR(ret), K(mview_table_id));
    } else if (OB_ISNULL(mview_table_schema)) {
      ret = OB_ERR_MVIEW_NOT_EXIST;
      LOG_WARN("fail to get mview table schema", KR(ret), K(mview_table_id));
    } else if (OB_FAIL(schema_guard.get_table_schema( container_table_id,
                                                     container_table_schema))) {
      LOG_WARN("fail to get table schema", KR(ret), K(container_table_id));
    } else if (OB_ISNULL(container_table_schema)) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("fail to get table schema", KR(ret), K(container_table_id));
    } else if (OB_FAIL(schema_guard.get_database_schema( mview_table_schema->get_database_id(), database_schema))) {
      LOG_WARN("fail to get database schema", KR(ret),
               K(mview_table_schema->get_database_id()));
    } else if (OB_ISNULL(database_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("error unexpected, database schema must not be nullptr", KR(ret));
    } else {
      ObArenaAllocator allocator("ObDDLTmp");
      ObString database_name;
      ObString container_table_name;
      ObSqlString src_table_schema_version_hint;
      ObSqlString rowkey_column_sql_string;
      if (OB_FAIL(sql::ObSQLUtils::generate_new_name_with_escape_character(
            allocator, database_schema->get_database_name_str(), database_name))) {
        LOG_WARN("fail to generate new name with escape character", KR(ret),
                 K(database_schema->get_database_name_str()));
      } else if (OB_FAIL(sql::ObSQLUtils::generate_new_name_with_escape_character(
                   allocator, container_table_schema->get_table_name_str(), container_table_name))) {
        LOG_WARN("fail to generate new name with escape character", KR(ret),
                 K(container_table_schema->get_table_name_str()));
      } else if (use_schema_version_hint_for_src_table) {
        int64_t based_schema_version = OB_INVALID_VERSION;
        for (int64_t i = 0; OB_SUCC(ret) && i < based_schema_object_infos.count(); ++i) {
          const ObBasedSchemaObjectInfo &based_info = based_schema_object_infos.at(i);
          const ObTableSchema *based_table_schema = nullptr;
          if (OB_FAIL(schema_guard.get_table_schema( based_info.schema_id_,
                                                    based_table_schema))) {
            LOG_WARN("fail to get table schema", KR(ret), K(based_info));
          } else if (OB_ISNULL(based_table_schema)) {
            ret = OB_OLD_SCHEMA_VERSION;
            LOG_WARN("based table is not exist", KR(ret), K(based_info));
          } else if (OB_UNLIKELY(based_table_schema->get_schema_version() !=
                                 based_info.schema_version_)) {
            ret = OB_OLD_SCHEMA_VERSION;
            LOG_WARN("based table schema version is changed", KR(ret), K(based_info),
                     KPC(based_table_schema));
          }
        }
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(generate_mview_ddl_schema_hint_str(mview_table_id, schema_guard, based_schema_object_infos,
                     src_table_schema_version_hint))) {
          LOG_WARN("failed to generated mview ddl schema hint", KR(ret));
        }
      }
      const bool nested_consistent_refresh = mview_target_data_sync_scn == OB_INVALID_SCN_VAL ? false : true;
      const int64_t real_parallelism = ObDDLUtil::get_real_parallelism(parallelism, true/*is mv refresh*/);
      if (OB_FAIL(ret)) {
      } else if (!nested_consistent_refresh) {
        const ObString &select_sql_string = mview_table_schema->get_view_schema().get_view_definition_str();
        if (OB_FAIL(sql_string.assign_fmt("INSERT /*+ append monitor enable_parallel_dml parallel(%ld) opt_param('ddl_execution_id', %ld) opt_param('ddl_task_id', %ld) use_px */ INTO `%.*s`.`%.*s`"
                                          " SELECT /*+ %.*s */ * from (%.*s) as of snapshot %ld %.*s;",
            real_parallelism, execution_id, task_id,
            static_cast<int>(database_name.length()), database_name.ptr(),
            static_cast<int>(container_table_name.length()), container_table_name.ptr(),
            static_cast<int>(src_table_schema_version_hint.length()), src_table_schema_version_hint.ptr(),
            static_cast<int>(select_sql_string.length()), select_sql_string.ptr(),
            snapshot_version,
            static_cast<int>(rowkey_column_sql_string.length()), rowkey_column_sql_string.ptr()))) {
          LOG_WARN("fail to assign sql string", KR(ret));
        }
      } else if (nested_consistent_refresh) {
        std::string select_sql(mview_select_sql.ptr());
        std::string real_sql;
        if (mview_select_sql.empty()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("nested sync refresh with empty sql string", K(mview_select_sql), K(mview_table_id));
        } else if (OB_FAIL(ObMViewRefreshHelper::replace_all_snapshot_zero(
                           select_sql, snapshot_version, real_sql))) {
          LOG_WARN("fail to replace snapshot", K(ret));
        } else {
          if (OB_FAIL(sql_string.assign_fmt("INSERT /*+ append monitor enable_parallel_dml parallel(%ld) opt_param('ddl_execution_id', %ld) "
                                              " opt_param('ddl_task_id', %ld) use_px */ INTO `%.*s`.`%.*s`"
                                              " SELECT /*+ %.*s */ * from (%.*s);",
                                            real_parallelism, execution_id, task_id,
                                            static_cast<int>(database_name.length()), database_name.ptr(),
                                            static_cast<int>(container_table_name.length()), container_table_name.ptr(),
                                            static_cast<int>(src_table_schema_version_hint.length()), src_table_schema_version_hint.ptr(),
                                            static_cast<int>(real_sql.length()), real_sql.c_str()))) {
            LOG_WARN("fail to assign sql string", KR(ret));
          }
        }
      }
    LOG_INFO("execute sql", K(sql_string));
    }
  }
  return ret;
}

int ObDDLUtil::obtain_snapshot(
    const share::ObDDLTaskStatus next_task_status,
    const uint64_t table_id,
    const uint64_t target_table_id,
    int64_t &snapshot_version,
    rootserver::ObDDLTask* task,
    const common::ObIArray<common::ObTabletID> *extra_mv_tablet_ids)
{
  int ret = OB_SUCCESS;
  rootserver::ObDDLWaitTransEndCtx* wait_trans_ctx = nullptr;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
  } else if (OB_UNLIKELY(nullptr == task || snapshot_version != 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(task), K(snapshot_version));
  } else if (OB_ISNULL(wait_trans_ctx = task->get_wait_trans_ctx())) {
    ret = OB_BAD_NULL_ERROR;
    LOG_WARN("wait trans ctx is null", K(ret));
  } else if (!task->is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("args have not been inited", K(ret), K(wait_trans_ctx->is_inited()), K(task->is_inited()), K(task->get_task_type()));
  } else {
    ObDDLTaskStatus new_status = ObDDLTaskStatus::OBTAIN_SNAPSHOT;
    
    int64_t new_fetched_snapshot = 0;
    int64_t persisted_snapshot = 0;
    if (!wait_trans_ctx->is_inited()) {
      if (OB_FAIL(wait_trans_ctx->init(task->get_task_id(), static_cast<ObDDLTaskStatus>(task->get_task_status()), task->get_object_id(), rootserver::ObDDLWaitTransEndCtx::WAIT_SCHEMA_TRANS, task->get_src_schema_version()))) {
        LOG_WARN("fail to init wait trans ctx", K(ret));
      }
    } else {
      // to get snapshot version.
      bool is_trans_end = false;
      const bool need_wait_trans_end = false;
      if (OB_FAIL(wait_trans_ctx->try_wait(is_trans_end, new_fetched_snapshot, need_wait_trans_end))) {
        LOG_WARN("just to get snapshot rather than wait trans end", K(ret));
      }
      DEBUG_SYNC(DDL_REDEFINITION_HOLD_SNAPSHOT);
      // try hold snapshot
      if (OB_FAIL(ret)) {
      } else if (new_fetched_snapshot <= 0) {
        // the snapshot version obtained here must be valid.
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("snapshot version is invalid", K(ret), K(new_fetched_snapshot), KPC(wait_trans_ctx));
      } else {
        ObMySQLTransaction trans;
        if (OB_FAIL(trans.start(GCTX.sql_proxy_))) {
          LOG_WARN("fail to start trans", K(ret));
        } else if (OB_FAIL(rootserver::ObDDLTaskRecordOperator::update_snapshot_version_if_not_exist(trans,
                                                                    task->get_task_id(),
                                                                    new_fetched_snapshot,
                                                                    persisted_snapshot))) {
          LOG_WARN("update snapshot version failed", K(ret), K(task->get_task_id()), K(1UL), K(new_fetched_snapshot), K(persisted_snapshot));
        } else if (persisted_snapshot > 0) {
          // found a persisted snapshot, do not hold it again.
          FLOG_INFO("found a persisted snapshot in inner table", "task_id", task->get_task_id(), K(persisted_snapshot), K(new_fetched_snapshot));
        } else if (OB_FAIL(hold_snapshot(trans, task, table_id, target_table_id, GCTX.root_service_, new_fetched_snapshot, extra_mv_tablet_ids))) {
          if (OB_SNAPSHOT_DISCARDED == ret) {
            wait_trans_ctx->reset();
          } else {
            LOG_WARN("hold snapshot version failed", K(ret));
          }
        }
        if (trans.is_started()) {
          const bool need_commit = (ret == OB_SUCCESS);
          const int tmp_ret = trans.end(need_commit);
          if (OB_SUCCESS != tmp_ret) {
            LOG_WARN("fail to end trans", K(ret), K(tmp_ret), K(need_commit));
          } else if (need_commit) {
            // update when commit succ.
            snapshot_version = persisted_snapshot > 0 ? persisted_snapshot : new_fetched_snapshot;
          }
          ret = OB_SUCC(ret) ? tmp_ret : ret;
        }
      }

      if (OB_FAIL(ret)) {
        if (OB_SNAPSHOT_DISCARDED == ret) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("fail to obtain snapshot version", K(ret));
        }
      } else {
        new_status = next_task_status;
      }
    }
    if (new_status == next_task_status || OB_FAIL(ret)) {
      if (OB_FAIL(task->switch_status(new_status, true, ret))) {
        LOG_WARN("fail to switch task status", K(ret));
      }
    }
    task->add_event_info("obtain snapshot finish");
    LOG_INFO("obtain snapshot", K(ret), K(task->get_snapshot_version()), K(table_id), K(target_table_id), K(task->get_src_schema_version()), "ddl_event_info", ObDDLEventInfo(),
        K(persisted_snapshot), K(new_fetched_snapshot));
  }
  return ret;
}

int ObDDLUtil::release_snapshot(
    rootserver::ObDDLTask* task,
    const uint64_t table_id,
    const uint64_t target_table_id,
    const int64_t snapshot_version)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObTabletID, 2> tablet_ids;
  if (OB_ISNULL(task)) {
    ret = OB_BAD_NULL_ERROR;
    LOG_WARN("invalid argument", K(ret));
  } else if (!task->is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("args have not been inited", K(ret), K(task->get_task_type()));
  } else {
    
    int64_t schema_version = task->get_src_schema_version();
    if (OB_FAIL(DDL_SIM(task->get_task_id(), DDL_TASK_RELEASE_SNAPSHOT_FAILED))) {
      LOG_WARN("ddl sim failure", K(ret), K(task->get_task_id()));
    } else if (OB_FAIL(ObDDLUtil::get_tablet_ids(table_id, target_table_id, tablet_ids))) {
      LOG_WARN("failed to get tablet ids", K(ret), K(table_id), K(target_table_id));
    }
    if (OB_FAIL(ret)) {
    } else if (tablet_ids.count() <= 0) {
    } else if (OB_FAIL(task->batch_release_snapshot(snapshot_version, tablet_ids))) {
      LOG_WARN("failed to release snapshot", K(ret));
    }
    task->add_event_info("release snapshot finish");
    LOG_INFO("release snapshot finished", K(ret), K(snapshot_version), K(table_id), K(target_table_id), K(tablet_ids), K(schema_version), "ddl_event_info", ObDDLEventInfo());
  }
  return ret;
}

int ObDDLUtil::obtain_snapshot(
    common::ObMySQLTransaction &trans,
    const ObTableSchema &data_table_schema,
    const ObTableSchema &index_table_schema,
    int64_t &new_fetched_snapshot)
{
  int ret = OB_SUCCESS;
  
  int64_t data_table_id = data_table_schema.get_table_id();
  new_fetched_snapshot = 0;
  if (OB_FAIL(calc_snapshot_with_gts(new_fetched_snapshot))) {
    LOG_WARN("fail to calc snapshot with gts", K(ret), K(new_fetched_snapshot));
  } else if (new_fetched_snapshot <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the snapshot is not valid", K(ret), K(new_fetched_snapshot));
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObDDLUtil::hold_snapshot(trans, data_table_schema, index_table_schema, new_fetched_snapshot))) {
    if (OB_SNAPSHOT_DISCARDED == ret) {
      LOG_INFO("snapshot discarded, need retry waiting trans", K(ret), K(new_fetched_snapshot));
    } else {
      LOG_WARN("hold snapshot failed", K(ret), K(new_fetched_snapshot));
    }
  }
  return ret;
}

int ObDDLUtil::calc_snapshot_with_gts(
    int64_t &snapshot,
    const int64_t ddl_task_id,
    const int64_t trans_end_snapshot,
    const int64_t index_snapshot_version_diff)
{
  int ret = OB_SUCCESS;
  snapshot = 0;
  SCN curr_ts;
  bool is_external_consistent = false;
  const int64_t timeout_us = ObDDLUtil::get_default_ddl_rpc_timeout();
  ObFreezeInfoProxy freeze_info_proxy{};
  ObFreezeInfo frozen_status;
  if (OB_UNLIKELY(false || ddl_task_id < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(ddl_task_id));
  } else if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.sql_proxy_));
  } else {
    {
      MAKE_TENANT_SWITCH_SCOPE_GUARD(tenant_guard);
      // ignore return, MTL is only used in get_ts_sync, which will handle switch failure.
      // for performance, everywhere calls get_ts_sync should ensure using correct tenant ctx
      tenant_guard.switch_to();
      if (OB_FAIL(OB_TS_MGR.get_ts_sync(timeout_us,
                                        curr_ts,
                                        is_external_consistent))) {
        LOG_WARN("fail to get gts sync", K(ret), K(timeout_us), K(curr_ts), K(is_external_consistent));
      }
    }
    if (OB_SUCC(ret)) {
      snapshot = max(trans_end_snapshot, curr_ts.get_val_for_tx() - index_snapshot_version_diff);
      if (OB_FAIL(freeze_info_proxy.get_freeze_info(
          *GCTX.sql_proxy_, SCN::min_scn(), frozen_status))) {
        LOG_WARN("get freeze info failed", K(ret));
      } else if (OB_FAIL(DDL_SIM(ddl_task_id, GET_FREEZE_INFO_FAILED))) {
        LOG_WARN("ddl sim failure: get freeze info failed", K(ret), K(ddl_task_id));
      } else {
        const int64_t frozen_scn_val = frozen_status.frozen_scn_.get_val_for_tx();
        snapshot = max(snapshot, frozen_scn_val);
      }
    }
  }
  return ret;
}

int ObDDLUtil::check_need_update_domain_index_share_table_snapshot(
  const ObTableSchema *table_schema,
  const ObTableSchema *index_schema,
  const int64_t task_id,
  const obcall::ObCreateIndexArg &create_index_arg,
  bool &need_update_snapshot)
{
  int ret = OB_SUCCESS;
  ObDocIDType doc_id_type = ObDocIDType::INVALID;
  uint64_t docid_col_id = OB_INVALID_ID;
  ObDocIDType vid_type = ObDocIDType::INVALID;
  uint64_t vid_col_id = OB_INVALID_ID;

  need_update_snapshot = false;
  bool is_index_with_docid = false;
  bool is_index_with_vid = false;
  uint64_t domain_index_share_tid = OB_INVALID_ID;
  if (OB_ISNULL(table_schema) || OB_ISNULL(index_schema)) {
    ret = OB_ERR_SYS;
    LOG_WARN("error sys, table schema, index schema must not be nullptr", K(ret), K(table_schema), K(index_schema));
  } else {
    is_index_with_docid = index_schema->is_fts_index() || index_schema->is_multivalue_index() || index_schema->is_vec_spiv_index();
    is_index_with_vid = index_schema->is_vec_hnsw_index();
  }

  if (!(create_index_arg.is_offline_rebuild_)) {
    // don't need update snapshot
  } else if (is_index_with_docid && OB_FAIL(ObFtsIndexBuilderUtil::determine_docid_type(*table_schema, doc_id_type))) {
    LOG_WARN("fail to get docid id type", K(ret));
  } else if (is_index_with_vid && OB_FAIL(ObVectorIndexUtil::determine_vid_type(*table_schema, vid_type))) {
    LOG_WARN("fail to get vid type", K(ret));
  } else if (doc_id_type == ObDocIDType::HIDDEN_INC_PK || vid_type == ObDocIDType::HIDDEN_INC_PK) {
    FLOG_INFO("Hidden inc pk, skip update.", K(ret));
  } else if (create_index_arg.is_offline_rebuild_ && (
              (is_index_with_docid && OB_FAIL(table_schema->get_docid_col_id(docid_col_id))) ||
              (is_index_with_vid && OB_FAIL(table_schema->get_vec_index_vid_col_id(vid_col_id))))) {
    if (ret == OB_ERR_INDEX_KEY_NOT_FOUND) {
      FLOG_INFO("There may be no docid or vid column in origin index, skip update.", K(ret), K(is_index_with_docid), K(is_index_with_vid));
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to get col id", K(ret), K(is_index_with_docid), K(is_index_with_vid));
    }
  } else if ((is_index_with_docid && OB_FAIL(table_schema->get_rowkey_doc_tid(domain_index_share_tid))) ||
             (is_index_with_vid && OB_FAIL(table_schema->get_rowkey_vid_tid(domain_index_share_tid)))) {
    if (OB_ERR_INDEX_KEY_NOT_FOUND == ret) {
      FLOG_INFO("There may be no rowkey_doc/vid table in origin index, skip update.", K(ret), K(is_index_with_docid), K(is_index_with_vid));
      ret = OB_SUCCESS;
    }
  } else {
    need_update_snapshot = true;
  }
  return ret;
}

int ObDDLUtil::write_defensive_and_obtain_snapshot(
    common::ObMySQLTransaction &trans,
    const ObTableSchema &data_table_schema,
    const ObTableSchema &index_table_schema,
    ObSchemaService *schema_service,
    int64_t &new_fetched_snapshot)
{
  int ret = OB_SUCCESS;
  if (!true || OB_ISNULL(schema_service)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("there are invalid arg", KP(schema_service));
  } else {
    HEAP_VAR(ObTableSchema, tmp_table_schema) {
      common::ObArray<ObTabletID> tablet_ids;
      const int64_t abs_timeout_us = THIS_WORKER.is_timeout_ts_valid() ? THIS_WORKER.get_timeout_ts()
                                                                  : ObTimeUtility::current_time() + GCONF.rpc_timeout;
      ObRefreshSchemaStatus schema_status;
      
      if (OB_FAIL(schema_service->get_table_schema_from_inner_table(schema_status,
                                                                    data_table_schema.get_table_id(),
                                                                    trans,
                                                                    tmp_table_schema))) {
        LOG_WARN("fail to get table schema from inner table",
            K(ret), K(data_table_schema.get_table_id()));
      } else if (OB_FAIL(data_table_schema.get_tablet_ids(tablet_ids))) {
        LOG_WARN("fail to get tablet ids", K(ret), K(data_table_schema));
      } else if (OB_FAIL(ObTabletBindingMdsHelper::modify_tablet_binding_for_write_defensive(tablet_ids,
                                                                                             tmp_table_schema.get_schema_version(),
                                                                                             abs_timeout_us,
                                                                                             trans))) {
        LOG_WARN("fail to modify tablet binding for write defensive", K(ret));
      } else if (OB_FAIL(ObDDLUtil::obtain_snapshot(trans, data_table_schema, index_table_schema, new_fetched_snapshot))) {
        LOG_WARN("fail to obtain snapshot",
            K(ret), K(data_table_schema), K(index_table_schema), K(new_fetched_snapshot));
      }
    }
  }
  return ret;
}

int ObDDLUtil::load_ddl_task(
    const int64_t task_id,
    ObIAllocator &allocator,
    rootserver::ObDDLTask &task)
{
  int ret = OB_SUCCESS;
  rootserver::ObDDLTaskRecord task_record;
  if (OB_UNLIKELY(!true || task_id <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("there are invalid args", K(ret), K(task_id));
  } else if (OB_FAIL(rootserver::ObDDLTaskRecordOperator::get_ddl_task_record(
                                                                              task_id,
                                                                              *GCTX.sql_proxy_,
                                                                              allocator,
                                                                              task_record))) {
    LOG_WARN("fail to get ddl task record", K(ret), K(task_id));
  } else if (OB_FAIL(task.init(task_record))) {
    LOG_WARN("fail to initialize ddl task obj", K(ret));
  }
  LOG_INFO("finish to load ddl task obj from the disk", K(ret), K(task));
  return ret;
}

int ObDDLUtil::check_and_cancel_single_replica_dag(
    rootserver::ObDDLTask* task,
    const uint64_t table_id,
    const uint64_t target_table_id,
    common::hash::ObHashMap<common::ObTabletID, common::ObTabletID>& check_dag_exit_tablets_map,
    const uint64_t data_format_version,
    int64_t &check_dag_exit_retry_cnt,
    bool is_complement_data_dag,
    bool &all_dag_exit)
{
  int ret = OB_SUCCESS;
  all_dag_exit = false;
  const bool force_renew = true;
  bool is_cache_hit = false;
  const int64_t expire_renew_time = force_renew ? INT64_MAX : 0;
  share::ObLocationService *location_service = GCTX.location_service_;
  if (OB_ISNULL(task)) {
    ret = OB_BAD_NULL_ERROR;
    LOG_WARN("invalid argument", K(ret));
  } else if (OB_UNLIKELY(!task->is_inited())) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(location_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), KP(location_service));
  } else if (OB_UNLIKELY(!check_dag_exit_tablets_map.created())) {
    const int64_t CHECK_DAG_EXIT_BUCKET_NUM = 64;
    common::ObArray<common::ObTabletID> src_tablet_ids;
    common::ObArray<common::ObTabletID> dst_tablet_ids;
    
    
    if (OB_FAIL(ObDDLUtil::get_tablets(table_id, src_tablet_ids))) {
      LOG_WARN("fail to get tablets", K(ret), K(table_id));
    } else if (OB_FAIL(ObDDLUtil::get_tablets(target_table_id, dst_tablet_ids))) {
      LOG_WARN("fail to get tablets", K(ret), K(target_table_id));
    } else if (OB_FAIL(check_dag_exit_tablets_map.create(CHECK_DAG_EXIT_BUCKET_NUM, lib::ObLabel("DDLChkDagMap")))) {
      LOG_WARN("create hashset set failed", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < src_tablet_ids.count(); i++) {
        if (OB_FAIL(check_dag_exit_tablets_map.set_refactored(src_tablet_ids.at(i), dst_tablet_ids.at(i)))) {
          LOG_WARN("set refactored failed", K(ret));
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    int saved_ret = OB_SUCCESS;
    ObAddr unused_leader_addr;
    const int64_t timeout_us = ObDDLUtil::get_default_ddl_rpc_timeout();
    common::hash::ObHashMap<common::ObTabletID, common::ObTabletID> ::const_iterator iter =
      check_dag_exit_tablets_map.begin();
    ObArray<common::ObTabletID> dag_not_exist_tablets;
    
    
    for (; OB_SUCC(ret) && iter != check_dag_exit_tablets_map.end(); iter++) {
      ObLSID src_ls_id;
      ObLSID dst_ls_id;
      const common::ObTabletID &src_tablet_id = iter->first;
      const common::ObTabletID &dst_tablet_id = iter->second;
      int64_t paxos_member_count = 0;
      common::ObArray<ObAddr> paxos_server_list;
      if (OB_FAIL(ObDDLUtil::get_tablet_leader_addr(location_service, src_tablet_id, timeout_us, src_ls_id, unused_leader_addr))) {
        LOG_WARN("get src tablet leader addr failed", K(ret));
      } else if (OB_FAIL(ObDDLUtil::get_tablet_leader_addr(location_service, dst_tablet_id, timeout_us, dst_ls_id, unused_leader_addr))) {
        LOG_WARN("get dst tablet leader addr failed", K(ret));
      } else if (OB_FAIL(ObDDLUtil::get_tablet_paxos_member_list(dst_tablet_id, paxos_server_list, paxos_member_count))) {
        LOG_WARN("get tablet paxos member list failed", K(ret));
      } else {
        bool is_tablet_dag_exist = false;
        obcall::ObDDLBuildSingleReplicaRequestArg arg;
        arg.ls_id_ = src_ls_id;
        arg.dest_ls_id_ = dst_ls_id;
        
        
        
        arg.source_tablet_id_ = src_tablet_id;
        arg.dest_tablet_id_ = dst_tablet_id;
        arg.source_table_id_ = table_id;
        arg.dest_schema_id_ = target_table_id;
        arg.schema_version_ = task->get_src_schema_version();
        arg.dest_schema_version_ = task->get_schema_version();
        arg.snapshot_version_ = 1; // to ensure arg valid only.
        arg.ddl_type_ = task->get_task_type();
        arg.task_id_ = task->get_task_id();
        arg.parallelism_ = 1; // to ensure arg valid only.
        arg.execution_id_ = 1; // to ensure arg valid only.
        arg.data_format_version_ = data_format_version; // to ensure arg valid only.
        arg.tablet_task_id_ = 1; // to ensure arg valid only.
        arg.consumer_group_id_ = 0; // to ensure arg valid only.
        for (int64_t j = 0; OB_SUCC(ret) && j < paxos_server_list.count(); j++) {
          int tmp_ret = OB_SUCCESS;
          obcall::Bool is_replica_dag_exist(true);
          if (is_complement_data_dag && OB_TMP_FAIL(ex_rpc::sync_call(ObDDLUtil::get_default_ddl_rpc_timeout(), [&]() -> int { bool b = is_replica_dag_exist; int r = GCTX.ob_service_->check_and_cancel_ddl_complement_data_dag(arg, b); is_replica_dag_exist = b; return r; }))) {
            // consider as dag does exist in this server.
            saved_ret = OB_SUCC(saved_ret) ? tmp_ret : saved_ret;
            is_tablet_dag_exist = true;
            LOG_WARN("check and cancel ddl complement dag failed", K(ret), K(tmp_ret), K(arg));
          } else if (!is_complement_data_dag && OB_TMP_FAIL(ex_rpc::sync_call(ObDDLUtil::get_default_ddl_rpc_timeout(), [&]() -> int { bool b = is_replica_dag_exist; int r = GCTX.ob_service_->check_and_cancel_delete_lob_meta_row_dag(arg, b); is_replica_dag_exist = b; return r; }))) {
            // consider as dag does exist in this server.
            saved_ret = OB_SUCC(saved_ret) ? tmp_ret : saved_ret;
            is_tablet_dag_exist = true;
            LOG_WARN("check and cancel ddl complement dag failed", K(ret), K(tmp_ret), K(arg));
          } else if (is_replica_dag_exist) {
            is_tablet_dag_exist = true;
            if (REACH_COUNT_INTERVAL(1000L)) {
              LOG_INFO("wait dag exist", "addr", paxos_server_list.at(j), K(arg));
            }
          }
        }
        if (OB_SUCC(ret) && !is_tablet_dag_exist) {
          if (OB_FAIL(dag_not_exist_tablets.push_back(src_tablet_id))) {
            LOG_WARN("push back failed", K(ret));
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      for (int64_t j = 0; OB_SUCC(ret) && j < dag_not_exist_tablets.count(); j++) {
        if (OB_FAIL(check_dag_exit_tablets_map.erase_refactored(dag_not_exist_tablets.at(j)))) {
          LOG_WARN("erase failed", K(ret));
        }
      }
      ret = OB_SUCC(ret) ? saved_ret : ret;
    }
  }
  if (OB_SUCC(ret)) {
    all_dag_exit = check_dag_exit_tablets_map.empty() ? true : false;
    task->set_delay_schedule_time(3000L * 1000L); // 3s, to avoid sending too many rpcs to the same replica frequently if retry.
  } else if (OB_TABLE_NOT_EXIST == ret
      || OB_TENANT_NOT_EXIST == ret
      || (++check_dag_exit_retry_cnt >= 10 /*MAX RETRY COUNT IF FAILED*/)) {
    ret = OB_SUCCESS;
    all_dag_exit = true;
  }
  return ret;
}

int ObDDLUtil::generate_partition_names(const common::ObIArray<ObString> &partition_names_array, common::ObIAllocator &allocator, ObString &partition_names)
{
  int ret = OB_SUCCESS;
  const char quote = '`';
  ObArenaAllocator tmp_allocator("ObDDLTmp");
  partition_names.reset();
  ObSqlString sql_partition_names;
  if (OB_UNLIKELY(partition_names_array.count() < 1)) {
    LOG_WARN("array num is less than 1", K(ret), K(partition_names_array));
  } else {
    int64_t partition_nums = partition_names_array.count();
    if (OB_FAIL(sql_partition_names.append("PARTITION("))) {
      LOG_WARN("append partition names failed", K(ret), K(partition_names_array));
    } else {
      for (int64_t i = 0; i < partition_nums && OB_SUCC(ret); i++) {
        ObString part_name;
        tmp_allocator.reuse();
        if (OB_FAIL(sql::ObSQLUtils::generate_new_name_with_escape_character(tmp_allocator, partition_names_array.at(i), part_name))) {
          LOG_WARN("failed to generate new name", K(ret), K(partition_names_array.at(i)));
        } else if (i == partition_nums - 1) {
          if (OB_FAIL(sql_partition_names.append_fmt("%c%.*s%c)", quote, static_cast<int>(part_name.length()), part_name.ptr(), quote))) {
            LOG_WARN("append partition names failed", K(ret), K(partition_nums), K(partition_names_array), K(i), K(sql_partition_names), K(part_name));
          }
        } else {
          if (OB_FAIL(sql_partition_names.append_fmt("%c%.*s%c,", quote, static_cast<int>(part_name.length()), part_name.ptr(), quote))) {
            LOG_WARN("append partition names failed", K(ret), K(partition_nums), K(partition_names_array), K(i), K(sql_partition_names), K(part_name));
          }
        }
      }
    }
    ObString tmp_name = sql_partition_names.string();
    if (OB_SUCC(ret)) {
      if OB_FAIL(deep_copy_ob_string(allocator,
                                    tmp_name,
                                    partition_names)) {
        LOG_WARN("fail to deep copy partition names", K(ret), K(tmp_name), K(partition_names), K(partition_names_array));
      }
    }
  }
  return ret;
}

int ObDDLUtil::check_target_partition_is_running(const ObString &running_sql_info, const ObString &partition_name, common::ObIAllocator &allocator, bool &is_running_status)
{
  int ret = OB_SUCCESS;
  const char quote = '`';
  ObArenaAllocator tmp_allocator("ObDDLTmp");
  ObString escaped_partition_name;
  ObSqlString sql_partition_name;
  ObString tmp_name;
  is_running_status = false;
  if (OB_UNLIKELY(running_sql_info.empty() || partition_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(running_sql_info), K(partition_name));
  } else if (OB_FAIL(sql::ObSQLUtils::generate_new_name_with_escape_character(tmp_allocator, partition_name, escaped_partition_name))) {
    LOG_WARN("failed to generate new name", K(ret), K(partition_name));
  } else if (OB_FAIL(sql_partition_name.append_fmt("%c%.*s%c,", quote, static_cast<int>(escaped_partition_name.length()), escaped_partition_name.ptr(), quote))) {
    LOG_WARN("append partition names failed", K(ret), K(escaped_partition_name), K(sql_partition_name));
  } else {
    tmp_name = sql_partition_name.string();
    if (0 != ObCharset::instr(ObCollationType::CS_TYPE_UTF8MB4_BIN, running_sql_info.ptr(), running_sql_info.length(), tmp_name.ptr(), tmp_name.length())) {
      is_running_status = true;
    }
    if (is_running_status == false) {
      sql_partition_name.reuse();
      tmp_name.reset();
      if (OB_FAIL(sql_partition_name.append_fmt("%c%.*s%c)", quote, static_cast<int>(escaped_partition_name.length()), partition_name.ptr(), quote))) {
        LOG_WARN("append partition names failed", K(ret), K(escaped_partition_name), K(sql_partition_name));
      } else {
        tmp_name = sql_partition_name.string();
        if (0 != ObCharset::instr(ObCollationType::CS_TYPE_UTF8MB4_BIN, running_sql_info.ptr(), running_sql_info.length(), tmp_name.ptr(), tmp_name.length())) {
          is_running_status = true;
        }
      }
    }
  }
  return ret;
}

int ObDDLUtil::get_task_tablet_slice_count(const int64_t ddl_task_id, bool &is_partitioned_table, common::hash::ObHashMap<int64_t, int64_t> &tablet_slice_cnt_map)
{
  int ret = OB_SUCCESS;

  bool use_idem_mode = false;
  rootserver::ObDDLSliceInfo ddl_slice_info;
  ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
  ObArenaAllocator arena(ObMemAttr("get_slice_info"));
  bool is_use_idem_mode = false;
  is_partitioned_table = true;
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is null", K(ret));
  } else if (OB_FAIL(rootserver::ObDDLTaskRecordOperator::get_schedule_info(
                    *sql_proxy, ddl_task_id, arena, true/*is_for_update*/, ddl_slice_info, use_idem_mode))) {
    LOG_WARN("fail to get schedule info", K(ret), K(ddl_task_id));
  } else {
    for (int64_t i = 0; i < ddl_slice_info.part_ranges_.count() && OB_SUCC(ret); i++) {
      int64_t tablet_slice_cnt = 0;
      const ObPxTabletRange &cur_part_range = ddl_slice_info.part_ranges_.at(i);
      const int64_t cur_tablet_id = cur_part_range.tablet_id_;
      if (0 == cur_tablet_id && 1 == ddl_slice_info.part_ranges_.count()) {
        is_partitioned_table = false;
      }

      if (OB_FAIL(tablet_slice_cnt_map.get_refactored(cur_tablet_id, tablet_slice_cnt))) {
        if (OB_HASH_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
          if (OB_FAIL(tablet_slice_cnt_map.set_refactored(cur_tablet_id, 0))) {
            LOG_WARN("failed to set refactor", K(ret));
          }
        } else {
          LOG_WARN("failed to get  slice cnt", K(ret));
        }
      }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(tablet_slice_cnt_map.set_refactored(cur_tablet_id, tablet_slice_cnt + cur_part_range.range_cut_.count(), 1 /* over write*/))) {
        LOG_WARN("failed to set slice cnt", K(ret), K(tablet_slice_cnt), K( cur_part_range.range_cut_.count()));
      }
    }
  }
  return ret;
}

int ObDDLUtil::check_table_empty(
    const share::schema::ObSysVariableSchema &sys_var_schema,
    const ObString &database_name,
    const share::schema::ObTableSchema &table_schema,
    const ObSQLMode sql_mode,
    bool &is_table_empty)
{
  int ret = OB_SUCCESS;
  is_table_empty = false;
  uint64_t table_id = OB_INVALID_ID;
  if (!table_schema.is_valid() || database_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(database_name), K(table_schema));
  } else if (FALSE_IT(table_id = table_schema.get_table_id())) {
  } else if (OB_INVALID_ID == table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_id));
  } else {
    const ObString &table_name = table_schema.get_table_name_str();
    ObSqlString sql_string;
    ObSessionParam session_param;
    int64_t new_sql_mode = static_cast<int64_t>(sql_mode);
    session_param.sql_mode_ = &new_sql_mode;
    session_param.tz_info_wrap_ = nullptr;
    InnerDDLInfo ddl_info;
    ddl_info.set_is_ddl(true);
    ddl_info.set_retryable_ddl(true);
    ddl_info.set_source_table_hidden(table_schema.is_user_hidden_table());
    ddl_info.set_dest_table_hidden(false);
    ObTimeoutCtx timeout_ctx;
    const char* format_str = nullptr;
    ObSingleConnectionProxy single_conn_proxy;
    sqlclient::ObISQLConnection *connection = nullptr;
    const ObSysVarSchema *var_schema = nullptr;

    {
      format_str = "SELECT /*+ %.*s */ 1 FROM `%.*s`.`%.*s` WHERE NOT 1 != 1 LIMIT 1";
      if (OB_FAIL(single_conn_proxy.connect(0/*group_id*/, GCTX.sql_proxy_))) {
        LOG_WARN("failed to get mysql connect", KR(ret));
      }
    }

    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      common::sqlclient::ObMySQLResult *result = nullptr;
      ObSqlString ddl_schema_hint_str;
      ObArenaAllocator allocator("ObDDLTmp");
      ObString new_table_name;
      ObString new_database_name;
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(sys_var_schema.get_sysvar_schema(SYS_VAR_LOWER_CASE_TABLE_NAMES, var_schema))) {
        LOG_WARN("failed to get lower_case_table_names", KR(ret));
      } else if (OB_ISNULL(var_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("var_schema is null", KR(ret));
      } else if (OB_ISNULL(connection = single_conn_proxy.get_connection())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null conn", K(ret));
      } else if (OB_FAIL(connection->set_session_variable(share::OB_SV_SQL_MODE, sql_mode))) {
        LOG_WARN("update sql_mode for ddl inner sql failed", K(ret));
      } else if (OB_FAIL(connection->set_session_variable(share::OB_SV_LOWER_CASE_TABLE_NAMES, var_schema->get_value()))) {
        LOG_WARN("update lower_case_table_names for ddl inner sql failed", K(ret));
      } else if (OB_FAIL(sql::ObSQLUtils::generate_new_name_with_escape_character(
                  allocator,
                  database_name,
                  new_database_name))) {
        LOG_WARN("fail to generate new name with escape character",
                  K(ret), K(database_name));
      } else if (OB_FAIL(sql::ObSQLUtils::generate_new_name_with_escape_character(
                         allocator,
                         table_name,
                         new_table_name))) {
        LOG_WARN("fail to generate new name with escape character",
                  K(ret), K(table_name));
      } else if (OB_FAIL(session_param.ddl_info_.init(ddl_info, table_schema.get_session_id()))) {
        LOG_WARN("fail to init ddl info", KR(ret), K(ddl_info), K(table_schema.get_session_id())); 
      } else if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(timeout_ctx, GCONF.internal_sql_execute_timeout))) {
        LOG_WARN("failed to set default timeout ctx", K(ret), K(timeout_ctx));
      } else if (OB_FAIL(connection->set_ddl_info(&session_param.ddl_info_))) {
        LOG_WARN("fail to set ddl info", K(ret), K(session_param.ddl_info_));
      } else if (OB_FAIL(ObDDLUtil::generate_ddl_schema_hint_str(table_name, table_schema.get_schema_version(), ddl_schema_hint_str))) {
        LOG_WARN("failed to generate ddl schema hint str", K(ret));
      } else if (OB_FAIL(sql_string.assign_fmt(
                         format_str,
                         static_cast<int>(ddl_schema_hint_str.length()), ddl_schema_hint_str.ptr(),
                         static_cast<int>(new_database_name.length()), new_database_name.ptr(),
                         static_cast<int>(new_table_name.length()), new_table_name.ptr()))) {
        LOG_WARN("fail to assign format", K(ret));
      } else if (OB_FAIL(single_conn_proxy.read(res, sql_string.ptr()))) {
        LOG_WARN("execute sql failed", K(ret), K(sql_string.ptr()));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("execute sql failed", K(ret), K(sql_string));
      } else if (OB_FAIL(result->next())) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          is_table_empty = true;
        } else {
          LOG_WARN("iterate next result fail", K(ret), K(sql_string));
        }
      }
    }
  }
  return ret;
}

int64_t ObDDLUtil::get_real_parallelism(const int64_t parallelism, const bool is_mv_refresh)
{
  int64_t real_parallelism = 0L;
  if (is_mv_refresh) {
    real_parallelism = std::max(static_cast<int64_t>(2), parallelism);
  } else {
    real_parallelism = std::min(oceanbase::ObMacroDataSeq::MAX_PARALLEL_IDX + 1, std::max(static_cast<int64_t>(1), parallelism));
  }
  return real_parallelism;
}

int ObCheckTabletDataComplementOp::do_check_tablets_merge_status(const int64_t snapshot_version,
  const ObIArray<ObTabletID> &tablet_ids,
  const ObLSID &ls_id,
  hash::ObHashMap<ObAddr, ObArray<ObTabletID>> &ip_tablets_map,
  hash::ObHashMap<ObTabletID, int32_t> &tablets_commited_map,
  int64_t &tablet_build_succ_count)
{
  int ret = OB_SUCCESS;
  ip_tablets_map.reuse();
  tablets_commited_map.reuse();

  tablet_build_succ_count = 0;

  if (OB_UNLIKELY(tablet_ids.count() < 0 || false || OB_INVALID_TIMESTAMP == snapshot_version)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_ids.count()), K(snapshot_version));
  } else {
    obcall::ObDDLCheckTabletMergeStatusArg arg;
    
    arg.ls_id_ = ls_id;
    arg.snapshot_version_ = snapshot_version;

    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
      const ObTabletID &tablet_id = tablet_ids.at(i);
      if (OB_FAIL(construct_tablet_ip_map(tablet_id, ip_tablets_map))) {
        LOG_WARN("fail to get tablet ip addr", K(ret), K(tablet_id));
      }
    }
    // Direct calls to local service (seekdb has no remote servers).
    for (hash::ObHashMap<ObAddr, ObArray<ObTabletID>>::const_iterator ip_iter = ip_tablets_map.begin();
      OB_SUCC(ret) && ip_iter != ip_tablets_map.end(); ++ip_iter) {
      const ObAddr & dest_ip = ip_iter->first;
      UNUSED(dest_ip);
      const ObArray<ObTabletID> &tablet_array = ip_iter->second;
      if (OB_FAIL(arg.tablet_ids_.assign(tablet_array))) {
        LOG_WARN("fail to get tablet ip addr", K(ret), K(tablet_array));
      } else {
        obcall::ObDDLCheckTabletMergeStatusResult cur_result;
        int return_ret = GCTX.ob_service_->check_ddl_tablet_merge_status(arg, cur_result);
        if (OB_SUCCESS == return_ret) {
          common::ObSArray<bool> tablet_rsp_array;
          common::ObArray<ObTabletID> tablet_req_array;
          if (FALSE_IT(tablet_rsp_array = cur_result.merge_status_)) {
          } else if (FALSE_IT(tablet_req_array = tablet_array)) {
          } else if (tablet_req_array.count() != tablet_rsp_array.count()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("tablet req count is not equal to tablet rsp count", K(ret), K(tablet_req_array), K(tablet_rsp_array));
          } else {
            for (int64_t idx = 0; OB_SUCC(ret) && idx < tablet_rsp_array.count(); ++idx) {
              const common::ObTabletID &tablet_id = tablet_req_array.at(idx);
              const bool tablet_status = tablet_rsp_array.at(idx);
              if (OB_FAIL(update_replica_merge_status(tablet_id, tablet_status, tablets_commited_map))) {
                LOG_WARN("fail to update replica merge status", K(ret), K(tablet_id), K(dest_ip));
              } else {
                LOG_INFO("succ to update replica merge status", K(dest_ip), K(tablet_id), K(tablet_status));
              }
            }
          }
        } else {
          LOG_WARN("check ddl tablet merge status failed.", K(return_ret));
        }
      }
    }
    // 3. check any commit tablet
    if (OB_SUCC(ret)) {
      int64_t build_succ_count = 0;
      if (OB_FAIL(calculate_build_finish(tablet_ids, tablets_commited_map, build_succ_count))) {
        LOG_WARN("check and commit tbalets commit log fail.", K(ret), K(tablet_ids), K(build_succ_count));
      } else {
        DEBUG_SYNC(DDL_CHECK_TABLET_MERGE_STATUS);
        tablet_build_succ_count += build_succ_count;
      }
    }
  }
  return ret;
}
