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
#include "share/config/ob_server_config.h"
namespace oceanbase
{
using namespace common;
namespace share
{

int ObTabletMetaTableCompactionOperator::get_status(
    ObSQLiteConnectionPool *meta_db_pool,
    const ObTabletCompactionScnInfo &input_info,
    ObTabletCompactionScnInfo &ret_info)
{
  int ret = OB_SUCCESS;
  ret_info.reset();
  if (OB_UNLIKELY(!input_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(input_info));
  } else if (OB_ISNULL(meta_db_pool)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(meta_db_pool))) {
    } else {
      int64_t report_scn = 0;
      int64_t status = 0;
      if (OB_FAIL(storage.get_max_report_scn_and_status(common::ObTabletID(input_info.tablet_id_),
          report_scn,
          status))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("failed to get report_scn and status", KR(ret), K(input_info));
        }
      } else {
        ret_info = input_info;
        ret_info.report_scn_ = report_scn;
        ret_info.status_ = ObTabletRuntimeInfo::ScnStatus(status);
      }
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::batch_update_unequal_report_scn_tablet(
    ObSQLiteConnectionPool *meta_db_pool,
    const int64_t major_frozen_scn,
      const common::ObIArray<ObTabletID> &input_tablet_id_array)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(meta_db_pool)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    LOG_INFO("start to update unequal tablet id array", KR(ret), K(major_frozen_scn),
      "input_tablet_id_array_cnt", input_tablet_id_array.count());
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(meta_db_pool))) {
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
          if (OB_FAIL(storage.get_tablet_ids_with_report_scn_before(
              batch_tablet_ids, major_frozen_scn, unequal_tablet_id_array))) {
          } else if (unequal_tablet_id_array.count() > 0) {
            int64_t tmp_affected_rows = 0;
            if (OB_FAIL(storage.batch_update_report_scn_unequal(unequal_tablet_id_array, major_frozen_scn, tmp_affected_rows))) {
            } else {
              LOG_INFO("success to update unequal report_scn", K(ret), K(tmp_affected_rows));
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
    ObSQLiteConnectionPool *meta_db_pool,
    SCN &min_compaction_scn)
{
  int ret = OB_SUCCESS;
  const int64_t start_time_us = ObTimeUtil::current_time();
  if (OB_ISNULL(meta_db_pool)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    int64_t estimated_timeout_us = 0;
    ObTimeoutCtx timeout_ctx;
    // Set transaction and query timeouts based on the local tablet count.
    if (OB_FAIL(ObTabletMetaTableCompactionOperator::get_estimated_timeout_us(
        meta_db_pool, estimated_timeout_us))) {
    } else if (OB_FAIL(timeout_ctx.set_trx_timeout_us(estimated_timeout_us))) {
    } else if (OB_FAIL(timeout_ctx.set_timeout(estimated_timeout_us))) {
    } else {
      ObTabletMetaTableStorage storage;
      if (OB_FAIL(storage.init(meta_db_pool))) {
      } else {
        uint64_t min_compaction_scn_val = UINT64_MAX;
        if (OB_FAIL(storage.get_min_compaction_scn(min_compaction_scn_val))) {
        } else if (UINT64_MAX == min_compaction_scn_val) {
          min_compaction_scn.set_max();
        } else if (OB_FAIL(min_compaction_scn.convert_for_inner_table_field(min_compaction_scn_val))) {
        }
      }
    }
    LOG_INFO("finish to get min_compaction_scn", KR(ret), K(min_compaction_scn),
             "cost_time_us", ObTimeUtil::current_time() - start_time_us, K(estimated_timeout_us));
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::batch_update_report_scn(
    ObSQLiteConnectionPool *meta_db_pool,
    const uint64_t global_broadcast_scn_val,
    const ObTabletRuntimeInfo::ScnStatus &except_status,
    const volatile bool &stop)
{
  int ret = OB_SUCCESS;
  const int64_t start_time_us = ObTimeUtil::current_time();
  const int64_t BATCH_UPDATE_CNT = 1000;
  if (OB_ISNULL(meta_db_pool)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    LOG_INFO("start to batch update report scn", KR(ret), K(global_broadcast_scn_val));
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(meta_db_pool))) {
    } else {
      bool update_done = false;
      SMART_VAR(ObArray<ObTabletID>, tablet_ids) {
        while (OB_SUCC(ret) && !update_done && !stop) {
          int64_t affected_rows = 0;
          if (OB_FAIL(ObTabletMetaTableCompactionOperator::get_next_batch_tablet_ids(
              meta_db_pool, BATCH_UPDATE_CNT, tablet_ids))) {
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

int ObTabletMetaTableCompactionOperator::batch_update_status(
    ObSQLiteConnectionPool *meta_db_pool)
{
  int ret = OB_SUCCESS;
  const int64_t start_time_us = ObTimeUtil::current_time();
  const int64_t BATCH_UPDATE_CNT = 1000;
  if (OB_ISNULL(meta_db_pool)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    LOG_INFO("start to batch update status", KR(ret));
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(meta_db_pool))) {
    } else {
      bool update_done = false;
      SMART_VAR(ObArray<ObTabletID>, tablet_ids) {
        while (OB_SUCC(ret) && !update_done) {
          int64_t affected_rows = 0;
          if (OB_FAIL(ObTabletMetaTableCompactionOperator::get_next_batch_tablet_ids(
              meta_db_pool, BATCH_UPDATE_CNT, tablet_ids))) {
          } else if (0 == tablet_ids.count()) {
            update_done = true;
            LOG_INFO("finish all rounds of batch update status", KR(ret),
                     "cost_time_us", ObTimeUtil::current_time() - start_time_us);
          } else if (OB_FAIL(storage.batch_update_status_range(tablet_ids.at(0),
              tablet_ids.at(tablet_ids.count() - 1),
              (int64_t)ObTabletRuntimeInfo::ScnStatus::SCN_STATUS_ERROR,
              (int64_t)ObTabletRuntimeInfo::ScnStatus::SCN_STATUS_IDLE,
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

int ObTabletMetaTableCompactionOperator::get_estimated_timeout_us(
    ObSQLiteConnectionPool *meta_db_pool,
    int64_t &estimated_timeout_us)
{
  int ret = OB_SUCCESS;
  int64_t tablet_count = 0;
  if (OB_FAIL(ObTabletMetaTableCompactionOperator::get_tablet_count(
      meta_db_pool, tablet_count))) {
  } else {
    estimated_timeout_us = tablet_count * 1000L; // 1ms for each tablet
    estimated_timeout_us = MAX(estimated_timeout_us, THIS_WORKER.get_timeout_remain());
    estimated_timeout_us = MIN(estimated_timeout_us, 3 * 3600 * 1000 * 1000L);
    estimated_timeout_us = MAX(estimated_timeout_us, GCONF.rpc_timeout);
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::get_tablet_count(
    ObSQLiteConnectionPool *meta_db_pool,
    int64_t &tablet_count)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(meta_db_pool)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(meta_db_pool))) {
    } else if (OB_FAIL(storage.get_tablet_count(tablet_count))) {
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::batch_update_report_scn(
    ObSQLiteConnectionPool *meta_db_pool,
    const uint64_t global_broadcast_scn_val,
    const ObIArray<ObTabletID> &tablet_ids,
    const ObTabletRuntimeInfo::ScnStatus &except_status)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  const int64_t all_tablet_cnt = tablet_ids.count();
  if (OB_UNLIKELY(all_tablet_cnt < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(all_tablet_cnt));
  } else if (OB_ISNULL(meta_db_pool)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(meta_db_pool))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && (i < all_tablet_cnt); i += MAX_BATCH_COUNT) {
        const int64_t cur_end_idx = MIN(i + MAX_BATCH_COUNT, all_tablet_cnt);
        ObArray<ObTabletID> batch_tablet_ids;
        for (int64_t idx = i; OB_SUCC(ret) && (idx < cur_end_idx); ++idx) {
          if (OB_UNLIKELY(!tablet_ids.at(idx).is_valid())) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("invalid tablet id", KR(ret), K(tablet_ids.at(idx)));
          } else if (OB_FAIL(batch_tablet_ids.push_back(tablet_ids.at(idx)))) {
          }
        }
        if (OB_SUCC(ret)) {
          int64_t tmp_affected_rows = 0;
          if (OB_FAIL(storage.batch_update_report_scn(
              batch_tablet_ids,
              global_broadcast_scn_val,
              global_broadcast_scn_val,
              (int64_t)except_status,
              tmp_affected_rows))) {
          } else {
            affected_rows += tmp_affected_rows;
          }
        }
      }
    }
  }

  return ret;
}

int ObTabletMetaTableCompactionOperator::get_next_batch_tablet_ids(
    ObSQLiteConnectionPool *meta_db_pool,
    const int64_t batch_update_cnt,
    ObIArray<ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(batch_update_cnt < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(batch_update_cnt));
  } else if (OB_ISNULL(meta_db_pool)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletID start_tablet_id = ObTabletID(ObTabletID::INVALID_TABLET_ID);
    if (tablet_ids.count() > 0) {
      start_tablet_id = tablet_ids.at(tablet_ids.count() - 1);
    }
    tablet_ids.reuse();
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(meta_db_pool))) {
    } else if (OB_FAIL(storage.get_tablet_ids(start_tablet_id, batch_update_cnt, tablet_ids))) {
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::range_scan_for_compaction(
      ObSQLiteConnectionPool *meta_db_pool,
      const int64_t compaction_scn,
      const common::ObTabletID &start_tablet_id,
      const int64_t batch_size,
      const bool only_unreported,
      common::ObTabletID &end_tablet_id,
      ObIArray<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  tablet_infos.reset();
  if (OB_UNLIKELY(batch_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(start_tablet_id), K(batch_size));
  } else if (start_tablet_id.id() == INT64_MAX) {
    ret = OB_ITER_END;
  } else {
    ObTabletID tmp_start_tablet_id = start_tablet_id;
    ObTabletID tmp_end_tablet_id;
    while (OB_SUCC(ret) && tmp_start_tablet_id.id() < INT64_MAX) {
      if (OB_SUCC(inner_range_scan_for_compaction(
              meta_db_pool, compaction_scn, tmp_start_tablet_id, batch_size,
              only_unreported, tmp_end_tablet_id, tablet_infos))) {
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
    ObSQLiteConnectionPool *meta_db_pool,
    const int64_t compaction_scn,
    const common::ObTabletID &start_tablet_id,
    const int64_t batch_size,
    const bool only_unreported,
    common::ObTabletID &end_tablet_id,
    ObIArray<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  ObTabletID max_tablet_id;
  if (OB_ISNULL(meta_db_pool)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(meta_db_pool))) {
    } else if (OB_FAIL(inner_get_max_tablet_id_in_range(
        meta_db_pool, start_tablet_id, batch_size, max_tablet_id))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("failed to get max tablet id in range", KR(ret), K(start_tablet_id));
      } else {
        ret = OB_SUCCESS;
        max_tablet_id = ObTabletID(INT64_MAX);
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(storage.range_scan_for_compaction(
          start_tablet_id, max_tablet_id, compaction_scn,
          only_unreported, tablet_infos))) {
      } else {
        end_tablet_id = max_tablet_id;
        LOG_INFO("success to get tablet info", KR(ret), K(batch_size), K(tablet_infos), K(end_tablet_id), K(only_unreported));
      }
    }
  }
  return ret;
}

int ObTabletMetaTableCompactionOperator::inner_get_max_tablet_id_in_range(
    ObSQLiteConnectionPool *meta_db_pool,
    const common::ObTabletID &start_tablet_id,
    const int64_t batch_size,
    common::ObTabletID &end_tablet_id)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(meta_db_pool)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else {
    ObTabletMetaTableStorage storage;
    if (OB_FAIL(storage.init(meta_db_pool))) {
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
