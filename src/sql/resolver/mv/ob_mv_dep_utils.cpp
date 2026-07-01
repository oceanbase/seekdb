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

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/mv/ob_mv_dep_utils.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/schema/ob_dependency_info.h"
#include "share/schema/ob_mview_info.h"  // relocated-definition owner

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
using namespace common::sqlclient;
namespace sql
{
bool ObMVDepInfo::is_valid() const
{
  return (true)
             && (OB_INVALID_ID != mview_id_)
             && (OB_INVALID_ID != p_obj_);
}

int ObMVDepUtils::get_mview_dep_infos(
    ObISQLClient &sql_client,
    const uint64_t mview_table_id,
    ObIArray<ObMVDepInfo> &dep_infos)
{
  int ret = OB_SUCCESS;
  if ((false)
      || (OB_INVALID_ID == mview_table_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid mview_table_id", KR(ret), K(mview_table_id));
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObSqlString sql;
      ObMySQLResult *result = NULL;
      
      if (OB_FAIL(sql.assign_fmt("SELECT p_order, p_obj, p_type, qbcid, flags FROM %s.%s"
                                 " WHERE mview_id = %lu ORDER BY p_order",
                                 OB_SYS_DATABASE_NAME, OB_ALL_MVIEW_DEP_TNAME,
                                 mview_table_id))) {
        LOG_WARN("failed to assign sql", KR(ret));
      } else if (OB_FAIL(sql_client.read(res, sql.ptr()))) {
        LOG_WARN("failed to execute read", KR(ret), K(sql));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("result is null", KR(ret), KP(result));
      } else {
        while (OB_SUCC(ret)) {
          if (OB_FAIL(result->next())) {
            if (OB_UNLIKELY(OB_ITER_END != ret)) {
              LOG_WARN("failed to get next", KR(ret));
            } else {
              ret = OB_SUCCESS;
              break;
            }
          } else {
            ObMVDepInfo dep_info;
            EXTRACT_INT_FIELD_MYSQL(*result, "p_order", dep_info.p_order_, int64_t);
            EXTRACT_INT_FIELD_MYSQL(*result, "p_obj", dep_info.p_obj_, uint64_t);
            EXTRACT_INT_FIELD_MYSQL(*result, "p_type", dep_info.p_type_, int64_t);
            EXTRACT_INT_FIELD_MYSQL(*result, "qbcid", dep_info.qbcid_, int64_t);
            EXTRACT_INT_FIELD_MYSQL(*result, "flags", dep_info.flags_, int64_t);

            if (OB_SUCC(ret)) {
              
              dep_info.mview_id_ = mview_table_id;
              if (OB_FAIL(dep_infos.push_back(dep_info))) {
                LOG_WARN("failed to add dep info", KR(ret), K(dep_info));
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObMVDepUtils::get_all_mview_dep_infos(
    ObMySQLProxy *sql_proxy,
    ObIArray<ObMVDepInfo> &dep_infos)
{
  int ret = OB_SUCCESS;
  dep_infos.reuse();
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is null", KR(ret), KP(sql_proxy));
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObSqlString sql;
      ObMySQLResult *result = NULL;
      
      if (OB_FAIL(sql.assign_fmt("SELECT mview_id, p_order, p_obj FROM %s.%s"
                                 " order by mview_id, p_order",
                                 OB_SYS_DATABASE_NAME, OB_ALL_MVIEW_DEP_TNAME))) {
        LOG_WARN("failed to assign sql", KR(ret));
      } else if (OB_FAIL(sql_proxy->read(res, sql.ptr()))) {
        LOG_WARN("failed to execute read", KR(ret), K(sql));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("result is null", KR(ret), KP(result));
      } else {
        while (OB_SUCC(ret)) {
          if (OB_FAIL(result->next())) {
            if (OB_UNLIKELY(OB_ITER_END != ret)) {
              LOG_WARN("failed to get next", KR(ret));
            } else {
              ret = OB_SUCCESS;
              break;
            }
          } else {
            ObMVDepInfo dep_info;
            EXTRACT_INT_FIELD_MYSQL(*result, "mview_id", dep_info.mview_id_, int64_t);
            EXTRACT_INT_FIELD_MYSQL(*result, "p_order", dep_info.p_order_, int64_t);
            EXTRACT_INT_FIELD_MYSQL(*result, "p_obj", dep_info.p_obj_, uint64_t);
            // EXTRACT_INT_FIELD_MYSQL(*result, "p_type", dep_info.p_type_, int64_t);
            // EXTRACT_INT_FIELD_MYSQL(*result, "qbcid", dep_info.qbcid_, int64_t);
            // EXTRACT_INT_FIELD_MYSQL(*result, "flags", dep_info.flags_, int64_t);

            if (OB_SUCC(ret)) {
              
              if (OB_FAIL(dep_infos.push_back(dep_info))) {
                LOG_WARN("failed to add dep info", KR(ret), K(dep_info));
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObMVDepUtils::insert_mview_dep_infos(
    ObISQLClient &sql_client,
    const uint64_t mview_table_id,
    const ObIArray<ObMVDepInfo> &dep_infos)
{
  int ret = OB_SUCCESS;
  if ((false)
      || (OB_INVALID_ID == mview_table_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid mview_table_id", KR(ret), K(mview_table_id));
  } else {
    ObDMLSqlSplicer dml;
    
    for (int64_t i = 0; OB_SUCC(ret) && (i < dep_infos.count()); ++i) {
      const ObMVDepInfo &dep_info = dep_infos.at(i);
      if (!dep_info.is_valid()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid dep info", KR(ret), K(dep_info));
      } else if (OB_FAIL(dml.add_pk_column("mview_id", mview_table_id))
          || OB_FAIL(dml.add_column("p_order", dep_info.p_order_))
          || OB_FAIL(dml.add_column("p_obj", dep_info.p_obj_))
          || OB_FAIL(dml.add_column("p_type", dep_info.p_type_))
          || OB_FAIL(dml.add_column("qbcid", dep_info.qbcid_))
          || OB_FAIL(dml.add_column("flags", dep_info.flags_))) {
        LOG_WARN("failed to add column", KR(ret), K(dep_info));
      } else if (OB_FAIL(dml.finish_row())) {
        LOG_WARN("failed to finish dml row", KR(ret));
      }
    }
    if (OB_SUCC(ret)) {
      int64_t affected_rows = 0;
      ObSqlString sql;
      if (OB_FAIL(dml.splice_batch_insert_sql(OB_ALL_MVIEW_DEP_TNAME, sql))) {
        LOG_WARN("failed to splice batch insert sql", KR(ret));
      } else if (OB_FAIL(sql_client.write(sql.ptr(), affected_rows))) {
        LOG_WARN("failed to execute write", KR(ret));
      } else if (affected_rows != dep_infos.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("affected rows does not match the count of dep infos",
            KR(ret), K(affected_rows), K(dep_infos.count()));
      }
    }
  }

  return ret;
}

int ObMVDepUtils::delete_mview_dep_infos(
    ObISQLClient &sql_client,
    const uint64_t mview_table_id)
{
  int ret = OB_SUCCESS;
  if ((false)
      || (OB_INVALID_ID == mview_table_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid mview_table_id", KR(ret), K(mview_table_id));
  } else {
    ObSqlString sql;
    int64_t affected_rows = 0;
    
    if (OB_FAIL(sql.assign_fmt("DELETE FROM %s WHERE mview_id = %ld",
                               OB_ALL_MVIEW_DEP_TNAME,
                               mview_table_id))) {
      LOG_WARN("failed to delete from __all_mview_dep table",
          KR(ret), K(mview_table_id));
    } else if (OB_FAIL(sql_client.write(sql.ptr(), affected_rows))) {
      LOG_WARN("failed to execute write", KR(ret), K(sql));
    }
  }

  return ret;
}

int ObMVDepUtils::convert_to_mview_dep_infos(
    const ObIArray<ObDependencyInfo> &deps,
    ObIArray<ObMVDepInfo> &mv_deps)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && (i < deps.count()); ++i) {
    const ObDependencyInfo &dep_info = deps.at(i);
    ObMVDepInfo mv_dep;
    
    mv_dep.mview_id_ = dep_info.get_dep_obj_id();
    mv_dep.p_order_ = dep_info.get_order();
    mv_dep.p_obj_ = dep_info.get_ref_obj_id();
    mv_dep.p_type_ = static_cast<int64_t>(dep_info.get_ref_obj_type());
    if (OB_FAIL(mv_deps.push_back(mv_dep))) {
      LOG_WARN("failed to add mv dep to array", KR(ret), K(mv_dep));
    }
  }

  return ret;
}

int ObMVDepUtils::get_table_ids_only_referenced_by_given_mv(
    ObISQLClient &sql_client,
    const uint64_t mview_table_id,
    ObIArray<uint64_t> &ref_table_ids)
{
  int ret = OB_SUCCESS;
  if ((false)
      || (OB_INVALID_ID == mview_table_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid mview_table_id",
        KR(ret), K(mview_table_id));
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObSqlString sql;
      ObMySQLResult *result = NULL;
      
      if (OB_FAIL(sql.assign_fmt("select a.p_obj from"
                                 " (select p_obj, count(*) cnt from %s group by p_obj) a,"
                                 " (select p_obj, count(*) cnt from %s where "
                                 " mview_id = %lu group by p_obj) b"
                                 " where a.p_obj = b.p_obj and a.cnt = b.cnt",
                                 OB_ALL_MVIEW_DEP_TNAME,
                                 OB_ALL_MVIEW_DEP_TNAME,
                                 mview_table_id))) {
        LOG_WARN("failed to assign sql", KR(ret));
      } else if (OB_FAIL(sql_client.read(res, sql.ptr()))) {
        LOG_WARN("failed to execute read", KR(ret), K(sql));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("result is null", KR(ret), KP(result));
      } else {
        while (OB_SUCC(ret)) {
          if (OB_FAIL(result->next())) {
            if (OB_UNLIKELY(OB_ITER_END != ret)) {
              LOG_WARN("failed to get next", KR(ret));
            } else {
              ret = OB_SUCCESS;
              break;
            }
          } else {
            uint64_t ref_table_id = OB_INVALID_ID;
            EXTRACT_INT_FIELD_MYSQL(*result, "p_obj", ref_table_id, uint64_t);
            if (OB_SUCC(ret)) {
              if (OB_FAIL(ref_table_ids.push_back(ref_table_id))) {
                LOG_WARN("failed to add ref table id to array", KR(ret), K(ref_table_id));
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObMVDepUtils::get_table_ids_only_referenced_by_given_fast_lsm_mv(
    ObISQLClient &sql_client,
    const uint64_t mview_table_id,
    ObIArray<uint64_t> &ref_table_ids)
{
  int ret = OB_SUCCESS;
  if ((false)
      || (OB_INVALID_ID == mview_table_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid mview_table_id",
        KR(ret), K(mview_table_id));
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObSqlString sql;
      ObMySQLResult *result = NULL;
      
      if (OB_FAIL(sql.assign_fmt(
              "select a.p_obj from"
              " (select p_obj from %s dep, %s mv where dep.mview_id = mv.mview_id and "
              "mv.refresh_mode in (%ld) group by p_obj having count(*) = 1) a,"
              " (select p_obj from %s dep, %s mv where dep.mview_id = mv.mview_id and "
              "mv.refresh_mode in (%ld) and dep.mview_id = %lu) b " 
              "where a.p_obj = b.p_obj",
              OB_ALL_MVIEW_DEP_TNAME, OB_ALL_MVIEW_TNAME,
              ObMVRefreshMode::MAJOR_COMPACTION,
              OB_ALL_MVIEW_DEP_TNAME, OB_ALL_MVIEW_TNAME,
              ObMVRefreshMode::MAJOR_COMPACTION,
              mview_table_id))) {
        LOG_WARN("failed to assign sql", KR(ret));
      } else if (OB_FAIL(sql_client.read(res, sql.ptr()))) {
        LOG_WARN("failed to execute read", KR(ret), K(sql));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("result is null", KR(ret), KP(result));
      } else {
        while (OB_SUCC(ret)) {
          if (OB_FAIL(result->next())) {
            if (OB_UNLIKELY(OB_ITER_END != ret)) {
              LOG_WARN("failed to get next", KR(ret));
            } else {
              ret = OB_SUCCESS;
              break;
            }
          } else {
            uint64_t ref_table_id = OB_INVALID_ID;
            EXTRACT_INT_FIELD_MYSQL(*result, "p_obj", ref_table_id, uint64_t);
            if (OB_SUCC(ret)) {
              if (OB_FAIL(ref_table_ids.push_back(ref_table_id))) {
                LOG_WARN("failed to add ref table id to array", KR(ret), K(ref_table_id));
              }
            }
          }
        }
      }
    }
  }
  return ret;
}
int ObMVDepUtils::get_referring_mv_of_base_table(ObISQLClient &sql_client,
                                                 const uint64_t base_table_id,
                                                 ObIArray<uint64_t> &mview_ids)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;

  SMART_VAR(ObMySQLProxy::MySQLResult, res)
  {
    ObMySQLResult *result = nullptr;
    if (OB_FAIL(sql.assign_fmt("SELECT mview_id FROM %s WHERE p_obj = %ld",
                               share::OB_ALL_MVIEW_DEP_TNAME, base_table_id))) {
      LOG_WARN("fail to assign sql", KR(ret));
    } else if (OB_FAIL(sql_client.read(res, sql.ptr()))) {
      LOG_WARN("execute sql failed", KR(ret), K(sql));
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("result is null", KR(ret));
    } else {
      while (OB_SUCC(ret)) {
        if (OB_FAIL(result->next())) {
          if (OB_UNLIKELY(OB_ITER_END != ret)) {
            LOG_WARN("failed to get next", KR(ret));
          } else {
            ret = OB_SUCCESS;
            break;
          }
        } else {
          uint64_t mview_id = 0;
          EXTRACT_INT_FIELD_MYSQL(*result, "mview_id", mview_id, uint64_t);
          if (OB_SUCC(ret)) {
            if (OB_FAIL(mview_ids.push_back(mview_id))) {
              LOG_WARN("failed to add ref table id to array", KR(ret), K(mview_id));
            }
          }
        }
      }
    }
  }

  return ret;
}
int ObMVDepUtils::update_mview_data_attr(ObISQLClient &sql_client,
                                        const uint64_t refresh_scn,
                                        const uint64_t target_data_sync_scn,
                                        ObMViewInfo &mview_info)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObSEArray<ObMVDepInfo, 2> mv_dep_infos;
  ObSEArray<uint64_t, 2> dep_mview_ids;
  ObSchemaGetterGuard schema_guard;
  uint64_t data_sync_scn = OB_INVALID_SCN_VAL;
  bool is_synced = true, dep_mview = false, dep_base_table = false;
  const bool nested_consistent_refresh = target_data_sync_scn == OB_INVALID_SCN_VAL ? false : true; 
  if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("fail to get tenant schema guard", K(ret));
  } else if (OB_FAIL(ObMVDepUtils::get_mview_dep_infos(sql_client, mview_info.get_mview_id(), mv_dep_infos))) {
    LOG_WARN("fail to get mv dep infos", K(ret), K(mview_info));
  } else if (mv_dep_infos.count() <= 0) {
    ret = OB_ERR_MVIEW_MISSING_DEPENDENCE;
    const ObTableSchema *mview_table_schema = nullptr;
    const ObDatabaseSchema *db_schema = nullptr;
    uint64_t mview_table_id = mview_info.get_mview_id();
    if (OB_TMP_FAIL(schema_guard.get_table_schema(mview_table_id, mview_table_schema))) {
      LOG_WARN("fail to get table schema", KR(tmp_ret), K(mview_table_id));
    } else if (OB_ISNULL(mview_table_schema)) {
      tmp_ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table schema is null", KR(tmp_ret), K(mview_table_id));
    } else if (OB_TMP_FAIL(schema_guard.get_database_schema(
                           mview_table_schema->get_database_id(), db_schema))) {
      LOG_WARN("fail to get db schema", KR(tmp_ret),
               K(mview_table_schema->get_database_id()));
    } else if (OB_ISNULL(db_schema)) {
      tmp_ret = OB_ERR_UNEXPECTED;
      LOG_WARN("database not exist", KR(tmp_ret));
    } else {
      LOG_ERROR("This materialized view has invalid dependency info, please perform a complete refresh to recover", K(ret), K(mview_info));
      LOG_USER_ERROR(OB_ERR_MVIEW_MISSING_DEPENDENCE, db_schema->get_database_name_str().ptr(), mview_table_schema->get_table_name_str().ptr());
    }
  } else {
    ARRAY_FOREACH(mv_dep_infos, idx) {
      ObMVDepInfo &dep_info = mv_dep_infos.at(idx);
      const ObTableSchema *table_schema = nullptr;
      if (OB_FAIL(schema_guard.get_table_schema(dep_info.p_obj_, table_schema))) {
          LOG_WARN("fail to get table schema", K(ret));
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table schema is null", KR(ret), K(dep_info.p_obj_));
      } else if (table_schema->is_materialized_view()) {
        if (OB_FAIL(dep_mview_ids.push_back(dep_info.p_obj_))) {
          LOG_WARN("fail to push back dep mview id", K(ret));
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (dep_mview_ids.count () == mv_dep_infos.count()) {
        dep_mview = true,  dep_base_table = false;
      } else if (dep_mview_ids.count() == 0) {
        dep_mview = false, dep_base_table = true;
      } else {
        dep_mview = true,  dep_base_table = true;
      }
    }
  }
  // get data_sync_scn and check sync
  ObSEArray<ObMViewInfo, 2> dep_mview_infos;
  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(!dep_mview && !dep_base_table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("mview no deps", K(ret), K(mview_info));
  } else if (!dep_mview && dep_base_table) {
    // onlys dep on base table
    data_sync_scn = refresh_scn;
    if (nested_consistent_refresh) {
      data_sync_scn = min(data_sync_scn, target_data_sync_scn);
    }
    is_synced = true;
  } else if (dep_mview) {
    if (OB_FAIL(ObMViewInfo::bacth_fetch_mview_infos(sql_client,
                refresh_scn, dep_mview_ids, dep_mview_infos))) {
    LOG_WARN("fail to batch fetch mview info", K(ret));
    } else {
      is_synced = true;
      bool dep_mview_data_sync_scn_is_equal = true;
      // collect all dep mview's data_sync_scn and is_synced
      ARRAY_FOREACH(dep_mview_infos, idx) {
        const ObMViewInfo &tmp_mview_info = dep_mview_infos.at(idx);
        // check all mview data sync scn is equal
        if (dep_mview_data_sync_scn_is_equal &&
            data_sync_scn != OB_INVALID_SCN_VAL &&
            data_sync_scn != tmp_mview_info.get_data_sync_scn()) {
          dep_mview_data_sync_scn_is_equal = false;
        }
        // check all dep mview is synced
        if (is_synced && !tmp_mview_info.get_is_synced()) {
          is_synced = false;
          LOG_INFO("data not synced", K(tmp_mview_info));
        }
        // compute min_data_sync_scn
        data_sync_scn = min(data_sync_scn, tmp_mview_info.get_data_sync_scn());
      }
      if (is_synced) {
        if (!dep_mview_data_sync_scn_is_equal) {
          is_synced = false;
          LOG_INFO("data not synced", K(dep_mview_data_sync_scn_is_equal));
        } else {
          if (!nested_consistent_refresh) {
            if (dep_base_table && data_sync_scn != refresh_scn) {
              is_synced = false;
            } else if (!dep_base_table) {
              // only dep mview and all dep mview's scn is equal
              is_synced = true;
            }
          } else if (data_sync_scn != target_data_sync_scn) {
            is_synced = false;
          }
        }
      }
      LOG_DEBUG("check is synced", K(is_synced), K(dep_mview), K(dep_mview_data_sync_scn_is_equal),
               K(dep_base_table), K(data_sync_scn), K(target_data_sync_scn));
    }
  }
  if (nested_consistent_refresh && !is_synced) {
    ret = OB_ERR_MVIEW_CAN_NOT_NESTED_CONSISTENT_REFRESH;
    LOG_WARN("sync refresh failed", K(ret));
  }
  if (OB_SUCC(ret)) {
    mview_info.set_data_sync_scn(data_sync_scn);
    mview_info.set_is_synced(is_synced);
    LOG_INFO("update mview data attr", K(ret), K(mview_info), K(dep_mview), K(dep_base_table),
             K(data_sync_scn), K(target_data_sync_scn));
  }
  return ret;
}
} // end of sql
} // end of oceanbase
