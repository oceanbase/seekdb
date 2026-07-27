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

#define USING_LOG_PREFIX SHARE

#include "share/tablet/ob_tablet_mapping_operator.h"
#include "common/mysqlclient/ob_isql_client.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/ob_dml_sql_splicer.h" // ObDMLSqlSplicer

namespace oceanbase
{
using namespace common;

namespace share
{
#define RANGE_GET(sql_proxy, start_tablet_id, range_size, tablets) \
    do { \
      ObSqlString sql; \
      if (OB_FAIL(ret)) { \
      } else if (OB_UNLIKELY(range_size <= 0)) { /* do not check start_tablet_id */ \
        ret = OB_INVALID_ARGUMENT; \
        LOG_WARN("invalid argument", KR(ret), K(range_size)); \
      } else if (OB_FAIL(sql.append_fmt( \
          "SELECT * FROM %s WHERE tablet_id > %lu ORDER BY tablet_id LIMIT %ld", \
          OB_ALL_TABLET_TO_TABLE_TNAME, \
          start_tablet_id.id(), \
          range_size))) { \
        LOG_WARN("fail to assign sql", KR(ret), K(sql), K(start_tablet_id)); \
      } else { \
        SMART_VAR(ObISQLClient::ReadResult, result) { \
          if (OB_FAIL(sql_proxy.read(result, sql.ptr()))) { \
            LOG_WARN("execute sql failed", KR(ret), K(sql)); \
          } else if (OB_ISNULL(result.get_result())) { \
            ret = OB_ERR_UNEXPECTED; \
            LOG_WARN("get mysql result failed", KR(ret), K(sql)); \
          } else if (OB_FAIL(construct_results_(*result.get_result(), tablets))) { \
            LOG_WARN("construct tablet info failed", KR(ret), K(sql), K(tablets)); \
          } else if (OB_UNLIKELY(tablets.count() > range_size)) { \
            ret = OB_ERR_UNEXPECTED; \
            LOG_WARN("get too much tablets", KR(ret), K(sql), \
                K(range_size), "tablets count", tablets.count()); \
          } \
        } \
      } \
    } while (0)

#define INNER_BATCH_GET(sql_proxy, tablet_ids, start_idx, end_idx, query_column_str, keep_order, results) \
    do { \
      if (OB_FAIL(ret)) { \
      } else if (OB_UNLIKELY( \
          tablet_ids.empty() \
          || start_idx < 0 \
          || start_idx >= end_idx \
          || end_idx > tablet_ids.count())) { \
        ret = OB_INVALID_ARGUMENT; \
        LOG_WARN("invalid args", KR(ret), K(tablet_ids), K(start_idx), K(end_idx)); \
      } else { \
        SMART_VAR(ObISQLClient::ReadResult, result) { \
          ObSqlString sql; \
          ObSqlString tablet_list; \
          for (int64_t idx = start_idx; OB_SUCC(ret) && (idx < end_idx); ++idx) { \
            const ObTabletID &tablet_id = tablet_ids.at(idx); \
            if (OB_UNLIKELY(!tablet_id.is_valid())) { \
              ret = OB_INVALID_ARGUMENT; \
              LOG_WARN("invalid tablet_id with runtime", KR(ret), K(tablet_id)); \
            } else if (OB_FAIL(tablet_list.append_fmt( \
                "%s%lu", \
                start_idx == idx ? "" : ",", \
                tablet_id.id()))) { \
              LOG_WARN("fail to assign sql", KR(ret), K(tablet_id)); \
            } \
          } \
          if (FAILEDx(sql.append_fmt( \
              "SELECT %s FROM %s WHERE tablet_id IN (", \
              query_column_str, \
              OB_ALL_TABLET_TO_TABLE_TNAME))) { \
            LOG_WARN("fail to assign sql", KR(ret), K(sql)); \
          } else if (OB_FAIL(sql.append(tablet_list.string()))) { \
            LOG_WARN("fail to assign sql", KR(ret), K(sql), K(tablet_list)); \
          } \
          if (OB_SUCC(ret) && keep_order) { \
            if (OB_FAIL(sql.append_fmt(") ORDER BY FIELD(tablet_id, "))) { \
              LOG_WARN("assign sql string failed", KR(ret), K(sql)); \
            } else if (OB_FAIL(sql.append(tablet_list.string()))) { \
              LOG_WARN("fail to assign sql", KR(ret), K(sql), K(tablet_list)); \
            } \
          } \
          if (FAILEDx(sql.append_fmt(")"))) { \
            LOG_WARN("fail to assign sql", KR(ret), K(sql)); \
          } else if (OB_FAIL(sql_proxy.read(result, sql.ptr()))) { \
            LOG_WARN("execute sql failed", KR(ret), K(sql)); \
          } else if (OB_ISNULL(result.get_result())) { \
            ret = OB_ERR_UNEXPECTED; \
            LOG_WARN("get mysql result failed", KR(ret)); \
          } else if (OB_FAIL(construct_results_(*result.get_result(), results))) { \
            LOG_WARN("construct tablet mapping info failed", KR(ret), K(results)); \
          } \
        } \
      } \
    } while(0)

#define BATCH_GET(sql_proxy, tablet_ids, results) \
    do { \
      results.reset(); \
      if (OB_FAIL(ret)) { \
      } else if (OB_UNLIKELY(tablet_ids.empty())) { \
        ret = OB_INVALID_ARGUMENT; \
        LOG_WARN("invalid argument", KR(ret), K(tablet_ids)); \
      } else { \
        int64_t start_idx = 0; \
        int64_t end_idx = min(MAX_BATCH_COUNT, tablet_ids.count()); \
        while (OB_SUCC(ret) && start_idx < end_idx) { \
          if (OB_FAIL(inner_batch_get_( \
              sql_proxy, \
              tablet_ids, \
              start_idx, \
              end_idx, \
              results))) { \
            LOG_WARN("fail to inner batch get by sql", \
                KR(ret), K(tablet_ids), K(start_idx), K(end_idx)); \
          } else { \
            start_idx = end_idx; \
            end_idx = min(start_idx + MAX_BATCH_COUNT, tablet_ids.count()); \
          } \
        } \
      } \
    } while(0)

int ObTabletMappingTableOperator::range_get_tablet_table_pairs(
    common::ObISQLClient &sql_proxy,
    const ObTabletID &start_tablet_id,
    const int64_t range_size,
    common::ObIArray<ObTabletTablePair> &tablets)
{
  int ret = OB_SUCCESS;
  RANGE_GET(sql_proxy, start_tablet_id, range_size, tablets);
  return ret;
}

int ObTabletMappingTableOperator::batch_update(
    common::ObISQLClient &sql_proxy,
    const ObIArray<ObTabletTablePair> &infos)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(infos.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(infos));
  } else {
    int64_t start_idx = 0;
    int64_t end_idx = min(MAX_BATCH_COUNT, infos.count());
    while (OB_SUCC(ret) && start_idx < end_idx) {
      if (OB_FAIL(inner_batch_update_by_sql_(sql_proxy, infos, start_idx, end_idx))) {
        LOG_WARN("fail to inner batch get by sql",
            KR(ret), K(infos), K(start_idx), K(end_idx));
      } else {
        start_idx = end_idx;
        end_idx = min(start_idx + MAX_BATCH_COUNT, infos.count());
      }
    }
    if (OB_SUCC(ret)) {
      LOG_TRACE("batch update tablet-table mapping success", K(infos));
    }
  }
  return ret;
}
int ObTabletMappingTableOperator::update_table_to_tablet_id_mapping(common::ObISQLClient &sql_proxy,
                                                                 const uint64_t table_id,
                                                                 const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_ID == table_id || !tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(tablet_id));
  } else {
    ObSqlString sql;
    ObDMLSqlSplicer dml_splicer;
    int64_t affected_rows = 0;
    if (OB_FAIL(dml_splicer.add_pk_column("tablet_id", tablet_id.id()))
       || OB_FAIL(dml_splicer.add_column("table_id", table_id))) {
      LOG_WARN("fail to add column", K(ret), K(tablet_id), K(table_id));
    } else if (OB_FAIL(dml_splicer.splice_update_sql(OB_ALL_TABLET_TO_TABLE_TNAME, sql))) {
      LOG_WARN("fail to splice batch insert update sql", K(ret), K(sql));
    } else if (OB_FAIL(sql_proxy.write(sql.ptr(), affected_rows))) {
      LOG_WARN("fail to write sql", K(ret), K(sql), K(affected_rows));
    } else if(!is_single_row(affected_rows)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("expect one row", K(ret), K(sql), K(affected_rows));
    } else {
      LOG_TRACE("update tablet-table mapping success", K(affected_rows));
    }
  }
  return ret;
}

int ObTabletMappingTableOperator::inner_batch_update_by_sql_(
    common::ObISQLClient &sql_proxy,
    const ObIArray<ObTabletTablePair> &infos,
    const int64_t start_idx,
    const int64_t end_idx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(infos.empty()
      || start_idx < 0
      || start_idx >= end_idx
      || end_idx > infos.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(infos), K(start_idx), K(end_idx));
  } else {
    ObSqlString sql;
    ObDMLSqlSplicer dml_splicer;
    int64_t affected_rows = 0;
    for (int64_t idx = start_idx; OB_SUCC(ret) && (idx < end_idx); ++idx) {
      const ObTabletTablePair &info = infos.at(idx);
      if (OB_UNLIKELY(!info.is_valid()
          || !info.get_tablet_id().is_valid())) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid tablet-table pair", KR(ret), K(info));
      } else if (OB_FAIL(dml_splicer.add_pk_column("tablet_id", info.get_tablet_id().id()))
          || OB_FAIL(dml_splicer.add_column("table_id", info.get_table_id()))) {
        LOG_WARN("fail to add column", KR(ret), K(info));
      } else if (OB_FAIL(dml_splicer.finish_row())) {
        LOG_WARN("fail to finish row", KR(ret));
      }
    }
    if (FAILEDx(dml_splicer.splice_batch_insert_update_sql(OB_ALL_TABLET_TO_TABLE_TNAME, sql))) {
      LOG_WARN("fail to splice batch insert update sql", KR(ret), K(sql));
    } else if (OB_FAIL(sql_proxy.write(sql.ptr(), affected_rows))) {
      LOG_WARN("fail to write sql", KR(ret), K(sql),
          K(affected_rows), K(infos), K(start_idx), K(end_idx));
    } else {
      LOG_TRACE("update tablet-table mapping success",
          K(affected_rows), K(start_idx), K(end_idx));
    }
  }
  return ret;
}

int ObTabletMappingTableOperator::batch_remove(
    common::ObISQLClient &sql_proxy,
    const ObIArray<common::ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(tablet_ids.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_ids));
  } else {
    int64_t start_idx = 0;
    int64_t end_idx = min(MAX_BATCH_COUNT, tablet_ids.count());
    while (OB_SUCC(ret) && start_idx < end_idx) {
      if (OB_FAIL(inner_batch_remove_by_sql_(
          sql_proxy,
          tablet_ids,
          start_idx,
          end_idx))) {
        LOG_WARN("fail to inner batch remove by sql",
            KR(ret), K(tablet_ids), K(start_idx), K(end_idx));
      } else {
        start_idx = end_idx;
        end_idx = min(start_idx + MAX_BATCH_COUNT, tablet_ids.count());
      }
    }
  }
  return ret;
}

int ObTabletMappingTableOperator::inner_batch_remove_by_sql_(
    common::ObISQLClient &sql_proxy,
    const ObIArray<common::ObTabletID> &tablet_ids,
    const int64_t start_idx,
    const int64_t end_idx)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  if (OB_UNLIKELY(
      false
      || tablet_ids.empty()
      || start_idx < 0
      || start_idx >= end_idx
      || end_idx > tablet_ids.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret),
        K(tablet_ids), K(start_idx), K(end_idx));
  } else if (OB_FAIL(sql.append_fmt(
      "DELETE FROM %s WHERE tablet_id IN (",
      OB_ALL_TABLET_TO_TABLE_TNAME))) {
    LOG_WARN("fail to assign sql", KR(ret));
  } else {
    int64_t affected_rows = 0;
    for (int64_t idx = start_idx; OB_SUCC(ret) && (idx < end_idx); ++idx) {
      const ObTabletID &tablet_id = tablet_ids.at(idx);
      if (OB_UNLIKELY(!tablet_id.is_valid())) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid tablet_id with runtime", KR(ret), K(tablet_id));
      } else if (OB_FAIL(sql.append_fmt("%s %lu", start_idx == idx ? "" : ",", tablet_id.id()))) {
        LOG_WARN("fail to assign sql", KR(ret), K(tablet_id));
      }
    }
    if (FAILEDx(sql.append_fmt(")"))) {
      LOG_WARN("fail to assign sql", KR(ret));
    } else if (OB_FAIL(sql_proxy.write(sql.ptr(), affected_rows))) {
      LOG_WARN("fail to write sql", KR(ret), K(sql), K(affected_rows));
    }
  }
  return ret;
}


int ObTabletMappingTableOperator::batch_get(
    common::ObISQLClient &sql_proxy,
    const ObIArray<common::ObTabletID> &tablet_ids,
    ObIArray<ObTabletTablePair> &infos)
{
  int ret = OB_SUCCESS;
  BATCH_GET(sql_proxy, tablet_ids, infos);
  if (OB_SUCC(ret) && OB_UNLIKELY(infos.count() != tablet_ids.count())) {
    ret = OB_ITEM_NOT_MATCH;
    LOG_WARN("count of infos and tablet_ids do not match,"
        " there may be duplicates or nonexistent values in tablet_ids",
        KR(ret), "tablet_ids count", tablet_ids.count(), "infos count", infos.count(),
        K(tablet_ids), K(infos));
  }
  return ret;
}

int ObTabletMappingTableOperator::inner_batch_get_(
    common::ObISQLClient &sql_proxy,
    const ObIArray<common::ObTabletID> &tablet_ids,
    const int64_t start_idx,
    const int64_t end_idx,
    ObIArray<ObTabletTablePair> &infos)
{
  int ret = OB_SUCCESS;
  const char *query_column_str = "*";
  const bool keep_order = false;
  INNER_BATCH_GET(sql_proxy, tablet_ids, start_idx, end_idx,
      query_column_str, keep_order, infos);
  return ret;
}

int ObTabletMappingTableOperator::construct_results_(
    common::sqlclient::ObMySQLResult &res,
    ObIArray<ObTabletTablePair> &infos)
{
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret) && OB_SUCC(res.next())) {
    int64_t tablet_id = ObTabletID::INVALID_TABLET_ID;
    uint64_t table_id = OB_INVALID_ID;
    ObTabletTablePair info;

    EXTRACT_INT_FIELD_MYSQL(res, "tablet_id", tablet_id, int64_t);
    EXTRACT_INT_FIELD_MYSQL(res, "table_id", table_id, uint64_t);

    if (OB_UNLIKELY(!ObTabletID(tablet_id).is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid runtime tablet id in mapping table", KR(ret), K(tablet_id));
    } else if (FAILEDx(info.init(ObTabletID(tablet_id), table_id))) {
      LOG_WARN("init failed", KR(ret), K(tablet_id), K(table_id));
    } else if (OB_FAIL(infos.push_back(info))) {
      LOG_WARN("fail to push back", KR(ret), K(info));
    }
  }
  if (OB_ITER_END == ret) {
    ret = OB_SUCCESS;
  } else {
    if (OB_SUCC(ret)) {
      ret = OB_ERR_UNEXPECTED;
    }
    LOG_WARN("construct_results failed", KR(ret), K(infos));
  }
  return ret;
}

} // end namespace share
} // end namespace oceanbase
