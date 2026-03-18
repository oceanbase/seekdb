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
#include "ob_tablet_meta_table_compaction_operator.h"
#include "share/tablet/ob_tablet_table_operator.h"
#include "share/tablet/ob_tablet_meta_table_storage.h"
#include "observer/ob_server_struct.h"
namespace oceanbase
{
using namespace common;
using namespace common::sqlclient;
using namespace compaction;
namespace share
{

int ObTabletMetaTableCompactionOperator::batch_set_info_status(
    const uint64_t tenant_id,
    const ObIArray<ObCkmErrorTabletLSInfo> &error_pairs,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  affected_rows = 0;
  if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
      LOG_WARN("failed to init storage", K(ret));
    } else {
      ObArray<ObTabletLSPair> tablet_ls_pairs;
      ObArray<int64_t> compaction_scns;
      for (int64_t i = 0; OB_SUCC(ret) && i < error_pairs.count(); ++i) {
        if (OB_FAIL(tablet_ls_pairs.push_back(error_pairs.at(i).tablet_info_))) {
          LOG_WARN("failed to push back tablet_ls_pair", K(ret));
        } else if (OB_FAIL(compaction_scns.push_back(error_pairs.at(i).compaction_scn_))) {
          LOG_WARN("failed to push back compaction_scn", K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(storage.batch_update_status(
            tenant_id,
            tablet_ls_pairs,
            compaction_scns,
            (int64_t)ObTabletReplica::ScnStatus::SCN_STATUS_ERROR,
            affected_rows))) {
          LOG_WARN("failed to batch update status", K(ret), K(tenant_id));
        } else if (affected_rows > 0) {
          LOG_INFO("success to update checksum error status", K(ret), K(tenant_id), K(affected_rows));
        }
      }
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::get_status(
    const ObTabletCompactionScnInfo &input_info,
    ObTabletCompactionScnInfo &ret_info)
{
  int ret = OB_SUCCESS;
  ret_info.reset();
  if (OB_UNLIKELY(!input_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(input_info));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
      LOG_WARN("failed to init storage", K(ret));
    } else {
      int64_t report_scn = 0;
      int64_t status = 0;
      if (OB_FAIL(storage.get_max_report_scn_and_status(
          input_info.tenant_id_,
          common::ObTabletID(input_info.tablet_id_),
          ObLSID(input_info.ls_id_),
          report_scn,
          status))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("failed to get max report_scn and status", KR(ret), K(input_info));
        }
      } else {
        ret_info = input_info; // assign tenant_id / ls_id / tablet_id
        ret_info.report_scn_ = report_scn;
        ret_info.status_ = ObTabletReplica::ScnStatus(status);
        LOG_TRACE("success to get medium snapshot info", K(ret_info));
      }
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::batch_update_unequal_report_scn_tablet(
      const uint64_t tenant_id,
      const share::ObLSID &ls_id,
      const int64_t major_frozen_scn,
      const common::ObIArray<ObTabletID> &input_tablet_id_array)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    LOG_INFO("start to update unequal tablet id array", KR(ret), K(ls_id), K(major_frozen_scn),
      "input_tablet_id_array_cnt", input_tablet_id_array.count());
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
      LOG_WARN("failed to init storage", K(ret));
    } else {
      const uint64_t meta_tenant_id = gen_meta_tenant_id(tenant_id);
      int64_t start_idx = 0;
      int64_t end_idx = min(MAX_BATCH_COUNT, input_tablet_id_array.count());
      common::ObSEArray<ObTabletID, 32> unequal_tablet_id_array;
      while (OB_SUCC(ret) && (start_idx < end_idx)) {
        // Get distinct tablet_ids with conditions
        ObArray<ObTabletID> batch_tablet_ids;
        for (int64_t i = start_idx; OB_SUCC(ret) && i < end_idx; ++i) {
          if (OB_FAIL(batch_tablet_ids.push_back(input_tablet_id_array.at(i)))) {
            LOG_WARN("failed to push back tablet_id", K(ret));
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(storage.get_distinct_tablet_ids_with_conditions(
              tenant_id, ls_id, batch_tablet_ids, major_frozen_scn, unequal_tablet_id_array))) {
            LOG_WARN("failed to get distinct tablet_ids with conditions", K(ret));
          } else if (unequal_tablet_id_array.count() > 0) {
            LOG_TRACE("success to get unequal tablet_id array", K(ret), K(unequal_tablet_id_array));
            int64_t tmp_affected_rows = 0;
            if (OB_FAIL(storage.batch_update_report_scn_unequal(
                tenant_id, ls_id, unequal_tablet_id_array, major_frozen_scn, tmp_affected_rows))) {
              LOG_WARN("fail to update unequal tablet id array", KR(ret));
            } else {
              LOG_INFO("success to update unequal report_scn", K(ret), K(tenant_id), K(ls_id), K(tmp_affected_rows));
            }
            unequal_tablet_id_array.reuse();
          }
        }
        if (OB_SUCC(ret)) {
          start_idx = end_idx;
          end_idx = min(start_idx + MAX_BATCH_COUNT, input_tablet_id_array.count());
        }
      }
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::get_min_compaction_scn(
    const uint64_t tenant_id,
    SCN &min_compaction_scn)
{
  int ret = OB_SUCCESS;
  const int64_t start_time_us = ObTimeUtil::current_time();
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    int64_t estimated_timeout_us = 0;
    ObTimeoutCtx timeout_ctx;
    // set trx_timeout and query_timeout based on tablet_replica_cnt
    if (OB_FAIL(ObTabletMetaTableCompactionOperator::get_estimated_timeout_us(tenant_id,
                                                     estimated_timeout_us))) {
      LOG_WARN("fail to get estimated_timeout_us", KR(ret), K(tenant_id));
    } else if (OB_FAIL(timeout_ctx.set_trx_timeout_us(estimated_timeout_us))) {
      LOG_WARN("fail to set trx timeout", KR(ret), K(estimated_timeout_us));
    } else if (OB_FAIL(timeout_ctx.set_timeout(estimated_timeout_us))) {
      LOG_WARN("fail to set abs timeout", KR(ret), K(estimated_timeout_us));
    } else {
      ObTabletMetaTableStorage storage;
      if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
        LOG_WARN("failed to init storage", K(ret));
      } else {
        uint64_t min_compaction_scn_val = UINT64_MAX;
        if (OB_FAIL(storage.get_min_compaction_scn(tenant_id, min_compaction_scn_val))) {
          LOG_WARN("failed to get min compaction scn", KR(ret), K(tenant_id));
        } else if (OB_FAIL(min_compaction_scn.convert_for_inner_table_field(min_compaction_scn_val))) {
          LOG_WARN("fail to convert uint64_t to SCN", KR(ret), K(min_compaction_scn_val));
        }
      }
    }
    LOG_INFO("finish to get min_compaction_scn", KR(ret), K(tenant_id), K(min_compaction_scn),
             "cost_time_us", ObTimeUtil::current_time() - start_time_us, K(estimated_timeout_us));
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::construct_tablet_id_array(
    sqlclient::ObMySQLResult &result,
    ObIArray<ObTabletID> &tablet_id_array)
{
  int ret = OB_SUCCESS;
  int64_t tablet_id = 0;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(result.next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("fail to get next result", KR(ret));
      }
      break;
    } else if (OB_FAIL(result.get_int("tablet_id", tablet_id))) {
      LOG_WARN("fail to get uint", KR(ret));
    } else if (OB_FAIL(tablet_id_array.push_back(ObTabletID(tablet_id)))) {
      LOG_WARN("failed to push back tablet id", K(ret), K(tablet_id));
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::append_tablet_id_array(
    const uint64_t tenant_id,
    const common::ObIArray<ObTabletID> &input_tablet_id_array,
    const int64_t start_idx,
    const int64_t end_idx,
    ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  for (int64_t idx = start_idx; OB_SUCC(ret) && (idx < end_idx); ++idx) {
    const ObTabletID &tablet_id = input_tablet_id_array.at(idx);
    if (OB_UNLIKELY(!tablet_id.is_valid_with_tenant(tenant_id))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid tablet_id with tenant", KR(ret), K(tenant_id), K(tablet_id));
    } else if (OB_FAIL(sql.append_fmt(
        "%s %ld",
        start_idx == idx ? "" : ",",
        tablet_id.id()))) {
      LOG_WARN("fail to assign sql", KR(ret), K(idx), K(tablet_id));
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::inner_batch_update_unequal_report_scn_tablet(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const int64_t major_frozen_scn,
    const common::ObIArray<ObTabletID> &unequal_tablet_id_array)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
#ifdef ERRSIM
  ret = OB_E(EventTable::EN_COMPACTION_UPDATE_REPORT_SCN) ret;
  if (OB_FAIL(ret)) {
    LOG_INFO("ERRSIM EN_COMPACTION_UPDATE_REPORT_SCN", K(ret));
  }
#endif
  if (OB_FAIL(ret)) {
    // ERRSIM error, skip
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
      LOG_WARN("failed to init storage", K(ret));
    } else if (OB_FAIL(storage.batch_update_report_scn_unequal(
        tenant_id, ls_id, unequal_tablet_id_array, major_frozen_scn, affected_rows))) {
      LOG_WARN("fail to update unequal report_scn", KR(ret), K(tenant_id), K(ls_id));
    } else if (affected_rows > 0) {
      LOG_INFO("success to update unequal report_scn", K(ret), K(tenant_id), K(ls_id), K(unequal_tablet_id_array.count()), K(affected_rows));
    }
  }
  return ret;
}


int ObTabletMetaTableCompactionOperator::batch_update_report_scn(
    const uint64_t tenant_id,
    const uint64_t global_broadcast_scn_val,
    const ObTabletReplica::ScnStatus &except_status,
    const volatile bool &stop)
{
  int ret = OB_SUCCESS;
  const int64_t start_time_us = ObTimeUtil::current_time();
  const int64_t BATCH_UPDATE_CNT = 1000;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    LOG_INFO("start to batch update report scn", KR(ret), K(tenant_id), K(global_broadcast_scn_val));
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
      LOG_WARN("failed to init storage", K(ret));
    } else {
      bool update_done = false;
      SMART_VAR(ObArray<ObTabletID>, tablet_ids) {
        while (OB_SUCC(ret) && !update_done && !stop) {
          int64_t affected_rows = 0;
          if (OB_FAIL(ObTabletMetaTableCompactionOperator::get_next_batch_tablet_ids(tenant_id,
                      BATCH_UPDATE_CNT, tablet_ids))) {
            LOG_WARN("fail to get next batch of tablet_ids", KR(ret), K(tenant_id), K(BATCH_UPDATE_CNT));
          } else if (0 == tablet_ids.count()) {
            update_done = true;
            LOG_INFO("finish all rounds of batch update report scn", KR(ret), K(tenant_id),
                     "cost_time_us", ObTimeUtil::current_time() - start_time_us);
          } else if (OB_FAIL(storage.batch_update_report_scn_range(
              tenant_id,
              tablet_ids.at(0),
              tablet_ids.at(tablet_ids.count() - 1),
              global_broadcast_scn_val,
              global_broadcast_scn_val,
              (int64_t)except_status,
              affected_rows))) {
            LOG_WARN("fail to batch update report scn range", KR(ret), K(tenant_id),
                     K(global_broadcast_scn_val), K(except_status));
          } else {
            LOG_INFO("finish one round of batch update report scn", KR(ret), K(tenant_id),
                     K(affected_rows), K(BATCH_UPDATE_CNT));
          }
        }
      }
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::batch_update_status(
    const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  const int64_t start_time_us = ObTimeUtil::current_time();
  const int64_t BATCH_UPDATE_CNT = 1000;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    LOG_INFO("start to batch update status", KR(ret), K(tenant_id));
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
      LOG_WARN("failed to init storage", K(ret));
    } else {
      bool update_done = false;
      SMART_VAR(ObArray<ObTabletID>, tablet_ids) {
        while (OB_SUCC(ret) && !update_done) {
          int64_t affected_rows = 0;
          if (OB_FAIL(ObTabletMetaTableCompactionOperator::get_next_batch_tablet_ids(tenant_id,
                      BATCH_UPDATE_CNT, tablet_ids))) {
            LOG_WARN("fail to get next batch of tablet_ids", KR(ret), K(tenant_id), K(BATCH_UPDATE_CNT));
          } else if (0 == tablet_ids.count()) {
            update_done = true;
            LOG_INFO("finish all rounds of batch update status", KR(ret), K(tenant_id),
                     "cost_time_us", ObTimeUtil::current_time() - start_time_us);
          } else if (OB_FAIL(storage.batch_update_status_range(
              tenant_id,
              tablet_ids.at(0),
              tablet_ids.at(tablet_ids.count() - 1),
              (int64_t)ObTabletReplica::ScnStatus::SCN_STATUS_ERROR,
              (int64_t)ObTabletReplica::ScnStatus::SCN_STATUS_IDLE,
              affected_rows))) {
            LOG_WARN("fail to batch update status range", KR(ret), K(tenant_id));
          } else {
            LOG_INFO("finish one round of batch update status", KR(ret), K(tenant_id), K(affected_rows), K(BATCH_UPDATE_CNT));
          }
        }
      }
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::batch_get_tablet_ids(
    const uint64_t tenant_id,
    const ObSqlString &sql,
    ObIArray<ObTabletID> &tablet_ids)
{
  // This method is kept for backward compatibility but should not be used for new code
  // The SQL string is parsed and executed directly using SQLite
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    tablet_ids.reuse();
    ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else {
      ObArray<ObTabletID> tmp_tablet_ids;
      auto row_processor = [&](share::ObSQLiteRowReader &reader) -> int {
        int64_t tablet_id_val = reader.get_int64();
        if (OB_FAIL(tmp_tablet_ids.push_back(ObTabletID(tablet_id_val)))) {
          LOG_WARN("failed to push back tablet_id", K(ret));
        }
        return ret;
      };
      if (OB_FAIL(guard->query(sql.ptr(), nullptr, row_processor))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("fail to execute sql", KR(ret), K(tenant_id), K(sql));
        } else {
          ret = OB_SUCCESS; // No rows is acceptable
        }
      } else {
        ret = tablet_ids.assign(tmp_tablet_ids);
      }
    }
    LOG_INFO("finish to batch get tablet_ids", KR(ret), K(tenant_id), K(sql));
  }
  return ret;
}


int ObTabletMetaTableCompactionOperator::get_estimated_timeout_us(
    const uint64_t tenant_id,
    int64_t &estimated_timeout_us)
{
  int ret = OB_SUCCESS;
  int64_t tablet_replica_cnt = 0;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id));
  } else if (OB_FAIL(ObTabletMetaTableCompactionOperator::get_tablet_replica_cnt(tenant_id,
                                                          tablet_replica_cnt))) {
    LOG_WARN("fail to get tablet replica cnt", KR(ret), K(tenant_id));
  } else {
    estimated_timeout_us = tablet_replica_cnt * 1000L; // 1ms for each tablet replica
    estimated_timeout_us = MAX(estimated_timeout_us, THIS_WORKER.get_timeout_remain());
    estimated_timeout_us = MIN(estimated_timeout_us, 3 * 3600 * 1000 * 1000L);
    estimated_timeout_us = MAX(estimated_timeout_us, GCONF.rpc_timeout);
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::get_tablet_replica_cnt(
    const uint64_t tenant_id,
    int64_t &tablet_replica_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
      LOG_WARN("failed to init storage", K(ret));
    } else if (OB_FAIL(storage.get_tablet_replica_cnt(tenant_id, tablet_replica_cnt))) {
      LOG_WARN("failed to get tablet replica cnt", KR(ret), K(tenant_id));
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::batch_update_report_scn(
    const uint64_t tenant_id,
    const uint64_t global_broadcast_scn_val,
    const ObIArray<ObTabletLSPair> &tablet_pairs,
    const ObTabletReplica::ScnStatus &except_status)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  const int64_t all_pair_cnt = tablet_pairs.count();
  if (OB_UNLIKELY((all_pair_cnt < 1)
      || !is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(all_pair_cnt));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
      LOG_WARN("failed to init storage", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && (i < all_pair_cnt); i += MAX_BATCH_COUNT) {
        const int64_t cur_end_idx = MIN(i + MAX_BATCH_COUNT, all_pair_cnt);
        ObArray<ObTabletLSPair> batch_pairs;
        for (int64_t idx = i; OB_SUCC(ret) && (idx < cur_end_idx); ++idx) {
          if (OB_FAIL(batch_pairs.push_back(tablet_pairs.at(idx)))) {
            LOG_WARN("failed to push back tablet_ls_pair", K(ret));
          }
        }
        if (OB_SUCC(ret)) {
          int64_t tmp_affected_rows = 0;
          if (OB_FAIL(storage.batch_update_report_scn(
              tenant_id,
              batch_pairs,
              global_broadcast_scn_val,
              global_broadcast_scn_val,
              (int64_t)except_status,
              tmp_affected_rows))) {
            LOG_WARN("fail to batch update report_scn", KR(ret), K(tenant_id), K(global_broadcast_scn_val));
          } else {
            affected_rows += tmp_affected_rows;
            LOG_TRACE("success to update report_scn", KR(ret), K(tenant_id), K(batch_pairs), K(tmp_affected_rows));
          }
        }
      }
    }
  }

  return ret;
}

int ObTabletMetaTableCompactionOperator::get_next_batch_tablet_ids(
    const uint64_t tenant_id,
    const int64_t batch_update_cnt,
    ObIArray<ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id) || batch_update_cnt < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(batch_update_cnt));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletID start_tablet_id = ObTabletID(ObTabletID::INVALID_TABLET_ID);
    if (tablet_ids.count() > 0) {
      start_tablet_id = tablet_ids.at(tablet_ids.count() - 1);
    }
    tablet_ids.reuse();
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
      LOG_WARN("failed to init storage", K(ret));
    } else if (OB_FAIL(storage.get_distinct_tablet_ids(tenant_id, start_tablet_id, batch_update_cnt, tablet_ids))) {
      LOG_WARN("fail to get distinct tablet_ids", KR(ret), K(tenant_id), K(start_tablet_id), K(batch_update_cnt));
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::range_scan_for_compaction(
      const uint64_t tenant_id,
      const int64_t compaction_scn,
      const common::ObTabletID &start_tablet_id,
      const int64_t batch_size,
      const bool add_report_scn_filter,
      common::ObTabletID &end_tablet_id,
      ObIArray<ObTabletInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  tablet_infos.reset();
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id) || batch_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(start_tablet_id), K(batch_size));
  } else if (start_tablet_id.id() == INT64_MAX) {
    ret = OB_ITER_END;
  } else {
    ObTabletID tmp_start_tablet_id = start_tablet_id;
    ObTabletID tmp_end_tablet_id;
    while (OB_SUCC(ret) && tmp_start_tablet_id.id() < INT64_MAX) {
      if (OB_SUCC(inner_range_scan_for_compaction(
              tenant_id, compaction_scn, tmp_start_tablet_id, batch_size,
              add_report_scn_filter, tmp_end_tablet_id, tablet_infos))) {
        if (tablet_infos.empty()) {
          tmp_start_tablet_id = tmp_end_tablet_id;
          tmp_end_tablet_id.reset();
        } else {
          break;
        }
      }
    } // end of while
    if (OB_SUCC(ret)) {
      end_tablet_id = tmp_end_tablet_id;
      if (tablet_infos.empty()) {
        ret = OB_ITER_END;
      }
    }
  }
  return ret;
}


int ObTabletMetaTableCompactionOperator::inner_range_scan_for_compaction(
    const uint64_t tenant_id,
    const int64_t compaction_scn,
    const common::ObTabletID &start_tablet_id,
    const int64_t batch_size,
    const bool add_report_scn_filter,
    common::ObTabletID &end_tablet_id,
    ObIArray<ObTabletInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  ObTabletID max_tablet_id;
  if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
      LOG_WARN("failed to init storage", K(ret));
    } else if (OB_FAIL(inner_get_max_tablet_id_in_range(tenant_id, start_tablet_id, batch_size, max_tablet_id))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("failed to get max tablet id in range", KR(ret), K(start_tablet_id));
      } else {
        ret = OB_SUCCESS;
        max_tablet_id = ObTabletID(INT64_MAX);
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(storage.range_scan_for_compaction(
          tenant_id, start_tablet_id, max_tablet_id, compaction_scn, add_report_scn_filter, tablet_infos))) {
        LOG_WARN("failed to range scan for compaction", KR(ret), K(tenant_id), K(start_tablet_id), K(max_tablet_id));
      } else {
        end_tablet_id = max_tablet_id;
        LOG_INFO("success to get tablet info", KR(ret), K(batch_size), K(tablet_infos), K(end_tablet_id), K(add_report_scn_filter));
      }
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::inner_get_max_tablet_id_in_range(
    const uint64_t tenant_id,
    const common::ObTabletID &start_tablet_id,
    const int64_t batch_size,
    common:: ObTabletID &end_tablet_id)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
      LOG_WARN("failed to init storage", K(ret));
    } else if (OB_FAIL(storage.get_max_tablet_id_in_range(tenant_id, start_tablet_id, batch_size, end_tablet_id))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("failed to get max tablet id in range", KR(ret), K(tenant_id), K(start_tablet_id));
      }
    }
  }
  return ret;
}

} // end namespace share
} // end namespace oceanbase
