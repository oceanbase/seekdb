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

int ObTabletMetaTableCompactionOperator::batch_set_info_status(const ObIArray<ObCkmErrorTabletLSInfo> &error_pairs,
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
    } else {
      ObArray<ObTabletLSPair> tablet_ls_pairs;
      ObArray<int64_t> compaction_scns;
      for (int64_t i = 0; OB_SUCC(ret) && i < error_pairs.count(); ++i) {
        if (OB_FAIL(tablet_ls_pairs.push_back(error_pairs.at(i).tablet_info_))) {
        } else if (OB_FAIL(compaction_scns.push_back(error_pairs.at(i).compaction_scn_))) {
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(storage.batch_update_status(tablet_ls_pairs,
            compaction_scns,
            (int64_t)ObTabletReplica::ScnStatus::SCN_STATUS_ERROR,
            affected_rows))) {
        } else if (affected_rows > 0) {
          LOG_INFO("success to update checksum error status", K(ret), K(affected_rows));
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
    } else {
      int64_t report_scn = 0;
      int64_t status = 0;
      if (OB_FAIL(storage.get_max_report_scn_and_status(common::ObTabletID(input_info.tablet_id_),
          ObLSID(input_info.ls_id_),
          report_scn,
          status))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("failed to get max report_scn and status", KR(ret), K(input_info));
        }
      } else {
        ret_info = input_info; // assign ls_id / tablet_id
        ret_info.report_scn_ = report_scn;
        ret_info.status_ = ObTabletReplica::ScnStatus(status);
        LOG_TRACE("success to get medium snapshot info", K(ret_info));
      }
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::batch_update_unequal_report_scn_tablet(const share::ObLSID &ls_id,
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
    } else {
      
      int64_t start_idx = 0;
      int64_t end_idx = min(MAX_BATCH_COUNT, input_tablet_id_array.count());
      common::ObSEArray<ObTabletID, 32> unequal_tablet_id_array;
      while (OB_SUCC(ret) && (start_idx < end_idx)) {
        // Get distinct tablet_ids with conditions
        ObArray<ObTabletID> batch_tablet_ids;
        for (int64_t i = start_idx; OB_SUCC(ret) && i < end_idx; ++i) {
          if (OB_FAIL(batch_tablet_ids.push_back(input_tablet_id_array.at(i)))) {
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(storage.get_distinct_tablet_ids_with_conditions(ls_id, batch_tablet_ids, major_frozen_scn, unequal_tablet_id_array))) {
          } else if (unequal_tablet_id_array.count() > 0) {
            LOG_TRACE("success to get unequal tablet_id array", K(ret), K(unequal_tablet_id_array));
            int64_t tmp_affected_rows = 0;
            if (OB_FAIL(storage.batch_update_report_scn_unequal(ls_id, unequal_tablet_id_array, major_frozen_scn, tmp_affected_rows))) {
            } else {
              LOG_INFO("success to update unequal report_scn", K(ret), K(ls_id), K(tmp_affected_rows));
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

int ObTabletMetaTableCompactionOperator::get_min_compaction_scn(SCN &min_compaction_scn)
{
  int ret = OB_SUCCESS;
  const int64_t start_time_us = ObTimeUtil::current_time();
  if (OB_UNLIKELY(!true)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    int64_t estimated_timeout_us = 0;
    ObTimeoutCtx timeout_ctx;
    // set trx_timeout and query_timeout based on tablet_replica_cnt
    if (OB_FAIL(ObTabletMetaTableCompactionOperator::get_estimated_timeout_us(estimated_timeout_us))) {
    } else if (OB_FAIL(timeout_ctx.set_trx_timeout_us(estimated_timeout_us))) {
    } else if (OB_FAIL(timeout_ctx.set_timeout(estimated_timeout_us))) {
    } else {
      ObTabletMetaTableStorage storage;
      if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
      } else {
        uint64_t min_compaction_scn_val = UINT64_MAX;
        if (OB_FAIL(storage.get_min_compaction_scn(min_compaction_scn_val))) {
        } else if (OB_FAIL(min_compaction_scn.convert_for_inner_table_field(min_compaction_scn_val))) {
        }
      }
    }
    LOG_INFO("finish to get min_compaction_scn", KR(ret), K(min_compaction_scn),
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
    } else if (OB_FAIL(tablet_id_array.push_back(ObTabletID(tablet_id)))) {
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::append_tablet_id_array(const common::ObIArray<ObTabletID> &input_tablet_id_array,
    const int64_t start_idx,
    const int64_t end_idx,
    ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  for (int64_t idx = start_idx; OB_SUCC(ret) && (idx < end_idx); ++idx) {
    const ObTabletID &tablet_id = input_tablet_id_array.at(idx);
    if (OB_UNLIKELY(!tablet_id.is_valid_with_tenant())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid tablet_id with tenant", KR(ret), K(tablet_id));
    } else if (OB_FAIL(sql.append_fmt(
        "%s %ld",
        start_idx == idx ? "" : ",",
        tablet_id.id()))) {
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::inner_batch_update_unequal_report_scn_tablet(const share::ObLSID &ls_id,
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
    } else if (OB_FAIL(storage.batch_update_report_scn_unequal(ls_id, unequal_tablet_id_array, major_frozen_scn, affected_rows))) {
    } else if (affected_rows > 0) {
      LOG_INFO("success to update unequal report_scn", K(ret), K(ls_id), K(unequal_tablet_id_array.count()), K(affected_rows));
    }
  }
  return ret;
}


int ObTabletMetaTableCompactionOperator::batch_update_report_scn(
    const uint64_t global_broadcast_scn_val,
    const ObTabletReplica::ScnStatus &except_status,
    const volatile bool &stop)
{
  int ret = OB_SUCCESS;
  const int64_t start_time_us = ObTimeUtil::current_time();
  const int64_t BATCH_UPDATE_CNT = 1000;
  if (OB_UNLIKELY(!true)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    LOG_INFO("start to batch update report scn", KR(ret), K(global_broadcast_scn_val));
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
    } else {
      bool update_done = false;
      SMART_VAR(ObArray<ObTabletID>, tablet_ids) {
        while (OB_SUCC(ret) && !update_done && !stop) {
          int64_t affected_rows = 0;
          if (OB_FAIL(ObTabletMetaTableCompactionOperator::get_next_batch_tablet_ids(BATCH_UPDATE_CNT, tablet_ids))) {
          } else if (0 == tablet_ids.count()) {
            update_done = true;
            LOG_INFO("finish all rounds of batch update report scn", KR(ret),
                     "cost_time_us", ObTimeUtil::current_time() - start_time_us);
          } else if (OB_FAIL(storage.batch_update_report_scn_range(tablet_ids.at(0),
              tablet_ids.at(tablet_ids.count() - 1),
              global_broadcast_scn_val,
              global_broadcast_scn_val,
              (int64_t)except_status,
              affected_rows))) {
          } else {
            LOG_INFO("finish one round of batch update report scn", KR(ret),
                     K(affected_rows), K(BATCH_UPDATE_CNT));
          }
        }
      }
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::batch_update_status()
{
  int ret = OB_SUCCESS;
  const int64_t start_time_us = ObTimeUtil::current_time();
  const int64_t BATCH_UPDATE_CNT = 1000;
  if (OB_UNLIKELY(!true)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    LOG_INFO("start to batch update status", KR(ret));
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
    } else {
      bool update_done = false;
      SMART_VAR(ObArray<ObTabletID>, tablet_ids) {
        while (OB_SUCC(ret) && !update_done) {
          int64_t affected_rows = 0;
          if (OB_FAIL(ObTabletMetaTableCompactionOperator::get_next_batch_tablet_ids(BATCH_UPDATE_CNT, tablet_ids))) {
          } else if (0 == tablet_ids.count()) {
            update_done = true;
            LOG_INFO("finish all rounds of batch update status", KR(ret),
                     "cost_time_us", ObTimeUtil::current_time() - start_time_us);
          } else if (OB_FAIL(storage.batch_update_status_range(tablet_ids.at(0),
              tablet_ids.at(tablet_ids.count() - 1),
              (int64_t)ObTabletReplica::ScnStatus::SCN_STATUS_ERROR,
              (int64_t)ObTabletReplica::ScnStatus::SCN_STATUS_IDLE,
              affected_rows))) {
          } else {
            LOG_INFO("finish one round of batch update status", KR(ret), K(affected_rows), K(BATCH_UPDATE_CNT));
          }
        }
      }
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::batch_get_tablet_ids(const ObSqlString &sql,
    ObIArray<ObTabletID> &tablet_ids)
{
  // This method is kept for backward compatibility but should not be used for new code
  // The SQL string is parsed and executed directly using SQLite
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
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
        }
        return ret;
      };
      if (OB_FAIL(guard->query(sql.ptr(), nullptr, row_processor))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("fail to execute sql", KR(ret), K(sql));
        } else {
          ret = OB_SUCCESS; // No rows is acceptable
        }
      } else {
        ret = tablet_ids.assign(tmp_tablet_ids);
      }
    }
    LOG_INFO("finish to batch get tablet_ids", KR(ret), K(sql));
  }
  return ret;
}


int ObTabletMetaTableCompactionOperator::get_estimated_timeout_us(
    int64_t &estimated_timeout_us)
{
  int ret = OB_SUCCESS;
  int64_t tablet_replica_cnt = 0;
  if (OB_UNLIKELY(!true)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else if (OB_FAIL(ObTabletMetaTableCompactionOperator::get_tablet_replica_cnt(tablet_replica_cnt))) {
  } else {
    estimated_timeout_us = tablet_replica_cnt * 1000L; // 1ms for each tablet replica
    estimated_timeout_us = MAX(estimated_timeout_us, THIS_WORKER.get_timeout_remain());
    estimated_timeout_us = MIN(estimated_timeout_us, 3 * 3600 * 1000 * 1000L);
    estimated_timeout_us = MAX(estimated_timeout_us, GCONF.rpc_timeout);
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::get_tablet_replica_cnt(int64_t &tablet_replica_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
    } else if (OB_FAIL(storage.get_tablet_replica_cnt(tablet_replica_cnt))) {
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::batch_update_report_scn(
    const uint64_t global_broadcast_scn_val,
    const ObIArray<ObTabletLSPair> &tablet_pairs,
    const ObTabletReplica::ScnStatus &except_status)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  const int64_t all_pair_cnt = tablet_pairs.count();
  if (OB_UNLIKELY((all_pair_cnt < 1)
      || !true)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(all_pair_cnt));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(GCTX.meta_db_pool_))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && (i < all_pair_cnt); i += MAX_BATCH_COUNT) {
        const int64_t cur_end_idx = MIN(i + MAX_BATCH_COUNT, all_pair_cnt);
        ObArray<ObTabletLSPair> batch_pairs;
        for (int64_t idx = i; OB_SUCC(ret) && (idx < cur_end_idx); ++idx) {
          if (OB_FAIL(batch_pairs.push_back(tablet_pairs.at(idx)))) {
          }
        }
        if (OB_SUCC(ret)) {
          int64_t tmp_affected_rows = 0;
          if (OB_FAIL(storage.batch_update_report_scn(
              batch_pairs,
              global_broadcast_scn_val,
              global_broadcast_scn_val,
              (int64_t)except_status,
              tmp_affected_rows))) {
          } else {
            affected_rows += tmp_affected_rows;
            LOG_TRACE("success to update report_scn", KR(ret), K(batch_pairs), K(tmp_affected_rows));
          }
        }
      }
    }
  }

  return ret;
}

int ObTabletMetaTableCompactionOperator::get_next_batch_tablet_ids(const int64_t batch_update_cnt,
    ObIArray<ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!true || batch_update_cnt < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(batch_update_cnt));
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
    } else if (OB_FAIL(storage.get_distinct_tablet_ids(start_tablet_id, batch_update_cnt, tablet_ids))) {
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::range_scan_for_compaction(const int64_t compaction_scn,
      const common::ObTabletID &start_tablet_id,
      const int64_t batch_size,
      const bool add_report_scn_filter,
      common::ObTabletID &end_tablet_id,
      ObIArray<ObTabletInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  tablet_infos.reset();
  if (OB_UNLIKELY(!true || batch_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(start_tablet_id), K(batch_size));
  } else if (start_tablet_id.id() == INT64_MAX) {
    ret = OB_ITER_END;
  } else {
    ObTabletID tmp_start_tablet_id = start_tablet_id;
    ObTabletID tmp_end_tablet_id;
    while (OB_SUCC(ret) && tmp_start_tablet_id.id() < INT64_MAX) {
      if (OB_SUCC(inner_range_scan_for_compaction(compaction_scn, tmp_start_tablet_id, batch_size,
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


int ObTabletMetaTableCompactionOperator::inner_range_scan_for_compaction(const int64_t compaction_scn,
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
    } else if (OB_FAIL(inner_get_max_tablet_id_in_range(start_tablet_id, batch_size, max_tablet_id))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("failed to get max tablet id in range", KR(ret), K(start_tablet_id));
      } else {
        ret = OB_SUCCESS;
        max_tablet_id = ObTabletID(INT64_MAX);
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(storage.range_scan_for_compaction(start_tablet_id, max_tablet_id, compaction_scn, add_report_scn_filter, tablet_infos))) {
      } else {
        end_tablet_id = max_tablet_id;
        LOG_INFO("success to get tablet info", KR(ret), K(batch_size), K(tablet_infos), K(end_tablet_id), K(add_report_scn_filter));
      }
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::inner_get_max_tablet_id_in_range(const common::ObTabletID &start_tablet_id,
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
    } else if (OB_FAIL(storage.get_max_tablet_id_in_range(start_tablet_id, batch_size, end_tablet_id))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("failed to get max tablet id in range", KR(ret), K(start_tablet_id));
      }
    }
  }
  return ret;
}

} // end namespace share
} // end namespace oceanbase
