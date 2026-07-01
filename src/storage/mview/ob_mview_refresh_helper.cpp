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

#define USING_LOG_PREFIX STORAGE

#include "storage/mview/ob_mview_refresh_helper.h"
#include "share/rc/ob_module_provider.h"
#include "sql/engine/ob_exec_context.h"
#include "storage/mview/ob_mview_transaction.h"
#include "storage/tablelock/ob_lock_inner_connection_util.h"
#include "src/share/schema/ob_mview_info.h"
#include "observer/ob_inner_sql_connection.h"
#include "share/ob_ex_rpc.h"
#include "rootserver/mview/ob_mview_maintenance_service.h"
#include "logservice/ob_log_service.h"
#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{
namespace storage
{
using namespace common;
using namespace lib;
using namespace number;
using namespace observer;
using namespace share;
using namespace sql;
using namespace transaction;
using namespace transaction::tablelock;

int ObMViewRefreshHelper::get_current_scn(SCN &current_scn)
{
  int ret = OB_SUCCESS;
  const int64_t DEFAULT_TIMEOUT = GCONF.internal_sql_execute_timeout;
  ObTransService *txs = share::g_mp->trans_service();
  if (OB_ISNULL(txs)) {
    ret = OB_ERR_SYS;
    LOG_WARN("trans service is null", KR(ret));
  } else {
    ObTimeoutCtx timeout_ctx;
    if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(timeout_ctx, DEFAULT_TIMEOUT))) {
      LOG_WARN("fail to set default timeout ctx", KR(ret));
    } else if (OB_FAIL(
                 txs->get_read_snapshot_version(timeout_ctx.get_abs_timeout(), current_scn))) {
      LOG_WARN("get read snapshot version", KR(ret));
    }
  }
  return ret;
}

int ObMViewRefreshHelper::lock_mview(ObMViewTransaction &trans,
                                     const uint64_t mview_id, const bool try_lock)
{
  int ret = OB_SUCCESS;
  ObTableLockOwnerID owner_id;
  if (OB_UNLIKELY(!trans.is_started() || false ||
                  OB_INVALID_ID == mview_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(trans.is_started()), K(mview_id));
  } else if (OB_FAIL(owner_id.convert_from_value(ObLockOwnerType::DEFAULT_OWNER_TYPE,
                                                 get_tid_cache()))) {
    LOG_WARN("failed to get owner id", K(ret), K(get_tid_cache()));
  } else {
    const int64_t DEFAULT_TIMEOUT = GCONF.internal_sql_execute_timeout;
    ObInnerSQLConnection *conn = nullptr;
    ObLockObjRequest lock_arg;
    lock_arg.obj_type_ = ObLockOBJType::OBJ_TYPE_MATERIALIZED_VIEW;
    lock_arg.obj_id_ = mview_id;
    lock_arg.owner_id_ = owner_id;
    lock_arg.lock_mode_ = EXCLUSIVE;
    lock_arg.op_type_ = ObTableLockOpType::IN_TRANS_COMMON_LOCK;
    if (OB_ISNULL(conn = static_cast<ObInnerSQLConnection *>(trans.get_connection()))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("conn_ is NULL", KR(ret));
    } else if (try_lock) {
      lock_arg.timeout_us_ = 0;
    } else {
      ObTimeoutCtx timeout_ctx;
      if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(timeout_ctx, DEFAULT_TIMEOUT))) {
        LOG_WARN("fail to set default timeout ctx", KR(ret));
      } else {
        lock_arg.timeout_us_ = timeout_ctx.get_timeout();
      }
    }
    if (OB_SUCC(ret)) {
      LOG_DEBUG("lock obj start", K(lock_arg));
      if (OB_FAIL(ObInnerConnectionLockUtil::lock_obj(lock_arg, conn))) {
        LOG_WARN("fail to lock obj", KR(ret));
      }
      LOG_DEBUG("lock obj end", KR(ret));
    }
  }
  return ret;
}

int ObMViewRefreshHelper::generate_purge_mlog_sql(ObSchemaGetterGuard &schema_guard, const uint64_t mlog_id,
                                                  const SCN &purge_scn, const int64_t purge_log_parallel,
                                                  ObSqlString &sql_string)
{
  int ret = OB_SUCCESS;
  sql_string.reuse();
  const ObTableSchema *table_schema = nullptr;
  const ObDatabaseSchema *database_schema = nullptr;
  if (OB_UNLIKELY(false || OB_INVALID_ID == mlog_id ||
                  !purge_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(mlog_id), K(purge_scn));
  } else if (OB_FAIL(schema_guard.get_table_schema( mlog_id, table_schema))) {
    LOG_WARN("fail to get table schema", KR(ret), K(mlog_id));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table schema is nullptr", KR(ret), K(mlog_id));
  } else if (OB_UNLIKELY(!table_schema->is_mlog_table())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected table type not mlog", KR(ret), KPC(table_schema));
  } else if (OB_FAIL(schema_guard.get_database_schema( table_schema->get_database_id(),
                                                      database_schema))) {
    LOG_WARN("fail to get database schema", KR(ret));
  } else if (OB_ISNULL(database_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("database schema is nullptr", KR(ret));
  } else {
    ObArenaAllocator allocator("ObMVRefTmp");
    ObString database_name;
    ObString table_name;
    if (OB_FAIL(ObSQLUtils::generate_new_name_with_escape_character(
          allocator, database_schema->get_database_name_str(), database_name))) {
      LOG_WARN("fail to generate new name with escape character", KR(ret),
               K(database_schema->get_database_name_str()));
    } else if (OB_FAIL(ObSQLUtils::generate_new_name_with_escape_character(
                 allocator, table_schema->get_table_name_str(), table_name))) {
      LOG_WARN("fail to generate new name with escape character", KR(ret),
               K(table_schema->get_table_name_str()));
    } else {
      if (OB_FAIL(sql_string.assign_fmt("DELETE /*+ ENABLE_PARALLEL_DML PARALLEL(%d)*/ FROM `%.*s`.`%.*s` WHERE ora_rowscn <= %lu;",
                                          static_cast<int>(purge_log_parallel),
                                          static_cast<int>(database_name.length()),
                                          database_name.ptr(),
                                          static_cast<int>(table_name.length()), table_name.ptr(),
                                          purge_scn.get_val_for_sql()))) {
          LOG_WARN("fail to assign sql", KR(ret));
      }
    }
  }
  return ret;
}

int ObMViewRefreshHelper::get_table_row_num(ObMViewTransaction &trans,
                                            const uint64_t table_id, const SCN &scn,
                                            int64_t &num_rows)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *table_schema = nullptr;
  const ObDatabaseSchema *database_schema = nullptr;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("schema service is null", KR(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("fail to get tenant schema guard", KR(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema( table_id, table_schema))) {
    LOG_WARN("fail to get table schema", KR(ret), K(table_id));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table schema is nullptr", KR(ret), K(table_id));
  } else if (OB_FAIL(schema_guard.get_database_schema( table_schema->get_database_id(),
                                                      database_schema))) {
    LOG_WARN("fail to get database schema", KR(ret));
  } else if (OB_ISNULL(database_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("database schema is nullptr", KR(ret));
  } else {
    ObArenaAllocator allocator("ObMVRefTmp");
    ObString database_name;
    ObString table_name;
    if (OB_FAIL(ObSQLUtils::generate_new_name_with_escape_character(
          allocator, database_schema->get_database_name_str(), database_name))) {
      LOG_WARN("fail to generate new name with escape character", KR(ret),
               K(database_schema->get_database_name_str()));
    } else if (OB_FAIL(ObSQLUtils::generate_new_name_with_escape_character(
                 allocator, table_schema->get_table_name_str(), table_name))) {
      LOG_WARN("fail to generate new name with escape character", KR(ret),
               K(table_schema->get_table_name_str()));
    } else {
      SMART_VAR(ObMySQLProxy::MySQLResult, res)
      {
        ObSqlString sql;
        sqlclient::ObMySQLResult *sql_result = nullptr;
        int64_t count = 0;
        if (OB_FAIL(sql.assign_fmt("select count(*) as COUNT from `%.*s`.`%.*s`",
                                   static_cast<int>(database_name.length()), database_name.ptr(),
                                   static_cast<int>(table_name.length()), table_name.ptr()))) {
          LOG_WARN("fail to assign sql", KR(ret));
        } else if (scn.is_valid() &&
                   OB_FAIL(sql.append_fmt(" as of snapshot %ld", scn.get_val_for_sql()))) {
          LOG_WARN("fail to append sql", KR(ret));
        }
        OZ(trans.read(res, sql.ptr()), sql);
        CK(OB_NOT_NULL(res.get_result()));
        OX(sql_result = res.get_result());
        OZ(sql_result->next());
        EXTRACT_INT_FIELD_MYSQL(*sql_result, "COUNT", count, int64_t);
        OX(num_rows = count);
      }
    }
  }
  return ret;
}

int ObMViewRefreshHelper::get_mlog_dml_row_num(ObMViewTransaction &trans,
                                               const uint64_t table_id, const ObScnRange &scn_range,
                                               int64_t &num_rows_ins, int64_t &num_rows_upd,
                                               int64_t &num_rows_del)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *table_schema = nullptr;
  const ObDatabaseSchema *database_schema = nullptr;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_SYS;
    LOG_WARN("schema service is null", KR(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("fail to get tenant schema guard", KR(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema( table_id, table_schema))) {
    LOG_WARN("fail to get table schema", KR(ret), K(table_id));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table schema is nullptr", KR(ret), K(table_id));
  } else if (OB_UNLIKELY(!table_schema->is_mlog_table())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected not materialized view log", KR(ret), KPC(table_schema));
  } else if (OB_FAIL(schema_guard.get_database_schema( table_schema->get_database_id(),
                                                      database_schema))) {
    LOG_WARN("fail to get database schema", KR(ret));
  } else if (OB_ISNULL(database_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("database schema is nullptr", KR(ret));
  } else {
    ObArenaAllocator allocator("ObMVRefTmp");
    ObString database_name;
    ObString table_name;
    if (OB_FAIL(ObSQLUtils::generate_new_name_with_escape_character(
          allocator, database_schema->get_database_name_str(), database_name))) {
      LOG_WARN("fail to generate new name with escape character", KR(ret),
               K(database_schema->get_database_name_str()));
    } else if (OB_FAIL(ObSQLUtils::generate_new_name_with_escape_character(
                 allocator, table_schema->get_table_name_str(), table_name))) {
      LOG_WARN("fail to generate new name with escape character", KR(ret),
               K(table_schema->get_table_name_str()));
    } else {
      SMART_VAR(ObMySQLProxy::MySQLResult, res)
      {
        ObSqlString sql;
        sqlclient::ObMySQLResult *sql_result = nullptr;
        ObString dml_type;
        int64_t count = 0;
        if (scn_range.start_scn_.is_valid()) {
          OZ(sql.assign_fmt(
            "select DMLTYPE$$, count(*) as COUNT from `%.*s`.`%.*s`"
            " where ora_rowscn > %lu and ora_rowscn <= %lu"
            " group by DMLTYPE$$;",
            static_cast<int>(database_name.length()), database_name.ptr(),
            static_cast<int>(table_name.length()), table_name.ptr(),
            scn_range.start_scn_.get_val_for_sql(), scn_range.end_scn_.get_val_for_sql()));
        } else {
          OZ(
            sql.assign_fmt("select DMLTYPE$$, count(*) as COUNT from `%.*s`.`%.*s`"
                           " where ora_rowscn <= %lu"
                           " group by DMLTYPE$$;",
                           static_cast<int>(database_name.length()), database_name.ptr(),
                           static_cast<int>(table_name.length()), table_name.ptr(),
                           scn_range.end_scn_.get_val_for_sql()));
        }
        OZ(trans.read(res, sql.ptr()), sql);
        CK(OB_NOT_NULL(res.get_result()));
        OX(sql_result = res.get_result());
        while (OB_SUCC(ret)) {
          if (OB_FAIL(sql_result->next())) {
            if (OB_UNLIKELY(OB_ITER_END != ret)) {
              LOG_WARN("fail to get next", KR(ret));
            } else {
              ret = OB_SUCCESS;
              break;
            }
          } else {
            OZ(sql_result->get_varchar("DMLTYPE$$", dml_type));
            EXTRACT_INT_FIELD_MYSQL(*sql_result, "COUNT", count, int64_t);
            CK(dml_type.length() == 1);
            if (OB_SUCC(ret)) {
              switch (dml_type.ptr()[0]) {
                case 'I':
                case 'i':
                  num_rows_ins = count;
                  break;
                case 'U':
                case 'u':
                  num_rows_upd = (count / 2);
                  break;
                case 'D':
                case 'd':
                  num_rows_del = count;
                  break;
                default:
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("unexpected dmp type", KR(ret), K(table_id), K(dml_type));
                  break;
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObMViewRefreshHelper::sync_post_nested_mview_rpc(
                          obcall::ObCheckNestedMViewMdsArg &arg,
                          obcall::ObCheckNestedMViewMdsRes &res)
{
  int ret = OB_SUCCESS;
  common::ObAddr leader;
  
  ObLocationService *location_service = GCTX.location_service_;
  int64_t abs_timeout_ts = ObTimeUtility::current_time()+
                           GCONF.location_cache_refresh_sql_timeout;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("arg is invalid", K(ret), K(arg));
  } else if (OB_ISNULL(location_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("location_service is NULL", K(ret), KP(location_service));
  } else if (OB_FAIL(location_service->get_leader_with_retry_until_timeout(
             GCONF.cluster_id, share::SYS_LS, leader, abs_timeout_ts))) {
    LOG_WARN("failed to get ls leader with retry until timeout",
             K(ret), K(leader), K(abs_timeout_ts));
  } else if (OB_FAIL(ex_rpc::sync_call([&]() -> int {
      int ret = OB_SUCCESS;
      MOD_SCOPE {
        common::ObRole role, new_role;
        int64_t proposal_id = 0, new_proposal_id = 0;
        share::SCN min_target_scn;
        rootserver::ObMViewMaintenanceService *svc = share::g_mp->m_view_maintenance_service();
        if (OB_ISNULL(svc)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(share::g_mp->log_service()->get_palf_role(share::SYS_LS, role, proposal_id))) {
        } else if (common::ObRole::LEADER != role || svc->get_proposal_id() != proposal_id) {
          ret = OB_NOT_MASTER;
        } else if (arg.refresh_id_ != OB_INVALID_ID &&
                   OB_FAIL(svc->check_nested_mview_mds_exists(arg.refresh_id_, arg.target_data_sync_scn_))) {
        } else if (arg.refresh_id_ == OB_INVALID_ID &&
                   OB_FAIL(svc->get_min_target_data_sync_scn(arg.mview_id_, min_target_scn))) {
        } else if (share::g_mp->log_service()->get_palf_role(share::SYS_LS, new_role, new_proposal_id)) {
        } else if (role != new_role && proposal_id != new_proposal_id) {
          ret = OB_NOT_MASTER;
        }
        res.ret_ = ret;
        res.target_data_sync_scn_ = min_target_scn;
        ret = OB_SUCCESS;
      }
      return ret;
    }))) {
    LOG_WARN("fail to check nested mview mds", K(ret), K(arg), K(res), K(leader));
  } else if (OB_FAIL(res.ret_)) {
    LOG_WARN("check nested mview mds failed", K(ret), K(res), K(arg), K(leader));
  }
  return ret;
}

int ObMViewRefreshHelper::sync_get_min_target_data_sync_scn(const uint64_t mview_id,
                          share::SCN &min_target_scn)
{
  int ret = OB_SUCCESS;
  min_target_scn.reset();
  obcall::ObCheckNestedMViewMdsArg arg;
  
  
  arg.mview_id_ = mview_id;
  arg.refresh_id_ = OB_INVALID_ID;
  arg.target_data_sync_scn_.reset();
  obcall::ObCheckNestedMViewMdsRes res;
  int64_t start_ts = ObTimeUtility::fast_current_time();
  const int64_t timeout_ts = 30 * 1000 * 1000; // 30s
  if (mview_id == OB_INVALID_ID || false) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(mview_id));
  } else {
    do {
      if (OB_FAIL(ObMViewRefreshHelper::sync_post_nested_mview_rpc(arg, res))) {
        LOG_WARN("fail to post nested mview rpc", K(ret));
      } else {
        ret = res.ret_;
      }
      if (OB_NOT_MASTER == ret || OB_EAGAIN == ret) {
        int64_t cur_ts = ObTimeUtility::fast_current_time();
        if (cur_ts - start_ts > timeout_ts) {
          ret = OB_TIMEOUT;
          LOG_WARN("post nested mview cost too long time", K(ret));
          break;
        }
        usleep(200 * 1000); // 200ms
      }
    } while (OB_NOT_MASTER == ret ||
            OB_EAGAIN == ret);
    if (OB_SUCC(ret)) {
      min_target_scn = res.target_data_sync_scn_;
    }
  }
  LOG_INFO("get min target scn for nested refresh", K(ret), K(mview_id), K(min_target_scn));
  return ret;
}

int ObMViewRefreshHelper::get_dep_mviews_from_dep_info(const ObIArray<share::schema::ObDependencyInfo> &dependency_infos,
                          ObSchemaGetterGuard &schema_guard,
                          ObIArray<uint64_t> &dep_mview_ids)
{
  int ret = OB_SUCCESS;
  dep_mview_ids.reuse();
  ARRAY_FOREACH(dependency_infos, idx) {
    const ObDependencyInfo &dep_info = dependency_infos.at(idx);
    const ObTableSchema *table_schema = nullptr;
    if (OB_FAIL(schema_guard.get_table_schema( dep_info.get_ref_obj_id(), table_schema))) {
      LOG_WARN("fail to get table schema", K(ret));
    } else if (OB_ISNULL(table_schema)) {
      LOG_INFO("table schema is null, maybe dep container tale not exist",
                K(ret), K(dep_info.get_ref_obj_id()));
    } else if (table_schema->is_materialized_view() &&
               OB_FAIL(dep_mview_ids.push_back(dep_info.get_ref_obj_id()))) {
      LOG_WARN("fail to push back dep mview id", K(ret));
    }
  }
  return ret;
}

int ObMViewRefreshHelper::check_dep_mviews_satisfy_target_scn(const share::SCN &target_data_sync_scn,
                          const share::SCN &read_snapshot,
                          const ObIArray<uint64_t> &dep_mview_ids,
                          common::ObISQLClient &sql_proxy,
                          bool &satisfy)
{
  int ret = OB_SUCCESS;
  satisfy = true;
  if (false ||
      !target_data_sync_scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(target_data_sync_scn));
  } else {
    // check dep mview's data sync scn fit target data_sync_scn
    ObSEArray<ObMViewInfo, 2> dep_mview_infos;
    if (OB_FAIL(ret)) {
    } else if (dep_mview_ids.empty()) {
      satisfy = true;
      LOG_INFO("no dep mview");
    } else if (OB_FAIL(ObMViewInfo::bacth_fetch_mview_infos(sql_proxy,
                       read_snapshot.get_val_for_sql(), dep_mview_ids, dep_mview_infos))) {
      LOG_WARN("fail to batch fetch mview info", K(ret));
    } else {
      const uint64_t target_data_sync_ts = target_data_sync_scn.get_val_for_gts();
      satisfy = true;
      ARRAY_FOREACH(dep_mview_infos, idx) {
        ObMViewInfo &dep_mview_info = dep_mview_infos.at(idx);
        if (OB_FAIL(ObMViewInfo::check_satisfy_target_data_sync_scn(
                    dep_mview_info, target_data_sync_ts, satisfy))) {
          LOG_INFO("fail to check satisfy target data sync scn", K(ret), K(dep_mview_info));
          break;
        } else if (!satisfy) {
          break;
        }
      }
    }
    LOG_INFO("check satified", K(target_data_sync_scn), K(satisfy));
  }
  return ret;
}

int ObMViewRefreshHelper::collect_deps_and_check_satisfy(const uint64_t mview_id,
                          const uint64_t target_data_sync_ts,
                          const uint64_t snapshot_version,
                          common::ObISQLClient &sql_proxy,
                          ObSchemaGetterGuard &schema_guard)
{
  int ret = OB_SUCCESS;
  ObArray<ObDependencyInfo> dep_infos;
  ObSEArray<uint64_t, 2> dep_mview_ids;
  share::SCN target_data_sync_scn;
  share::SCN read_snapshot;
  bool satisfy = false;
  if (false ||
      target_data_sync_ts == OB_INVALID_SCN_VAL ||
      snapshot_version == OB_INVALID_SCN_VAL) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(mview_id),
             K(target_data_sync_ts), K(snapshot_version));
  } else if (OB_FAIL(target_data_sync_scn.convert_for_sql(target_data_sync_ts))) {
    LOG_WARN("failed to convert to scn", K(target_data_sync_ts));
  } else if (OB_FAIL(read_snapshot.convert_for_sql(snapshot_version))) {
    LOG_WARN("failed to convert to scn", K(snapshot_version));
  } else if (OB_FAIL(ObDependencyInfo::collect_ref_infos(mview_id, sql_proxy, dep_infos))) {
    LOG_WARN("fail to collect mview ref infos", KR(ret), K(mview_id));
  } else if (OB_FAIL(ObMViewRefreshHelper::get_dep_mviews_from_dep_info(dep_infos, schema_guard, dep_mview_ids))) {
    LOG_WARN("fail to get dep mview ids", K(ret));
  } else if (OB_FAIL(ObMViewRefreshHelper::check_dep_mviews_satisfy_target_scn(target_data_sync_scn, read_snapshot,
                     dep_mview_ids, sql_proxy, satisfy))) {
    LOG_WARN("fail to target data sync scn satisfied", K(ret));
  } else if (!satisfy) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dep mviews not satisfy target data sync scn, need retry",
             K(ret), K(target_data_sync_scn));
  }
  return ret;
}

int ObMViewRefreshHelper::replace_all_snapshot_zero(
                          const std::string &input,
                          const uint64_t snapshot_version,
                          std::string &output)
{
  int ret = OB_SUCCESS;
  output = input;
  // Oracle mode removed - always use MySQL "as of snapshot" syntax
  std::string search = "as of snapshot ";
  std::string new_value_str = std::to_string(snapshot_version);

  int64_t pos = 0;
  while ((pos = output.find(search, pos)) != std::string::npos) {
    int64_t value_pos = pos + search.size();
    if (value_pos >= output.size()) break;

    if (output[value_pos] == '0' &&
        (value_pos + 1 == output.size() || !isdigit(output[value_pos + 1]))) {
      int64_t value_end = value_pos;
      while (value_end < output.size() && isdigit(output[value_end])) {
        ++value_end;
      }
      output.replace(pos + search.size(), value_end - value_pos, new_value_str);
      pos += search.size() + new_value_str.size();
    } else {
      ++pos;
    }
  }
  // for debug
  LOG_DEBUG("print generate sql", K(input.c_str()), K(output.c_str()));
  return ret;
}
} // namespace storage
} // namespace oceanbase
