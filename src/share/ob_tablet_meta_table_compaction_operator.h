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
 
#ifndef OCEANBASE_SHARE_OB_TABLET_META_TABLE_COMPACTION_OPERATOR_
#define OCEANBASE_SHARE_OB_TABLET_META_TABLE_COMPACTION_OPERATOR_

#include "lib/container/ob_iarray.h"
#include "share/tablet/ob_tablet_info.h"
namespace oceanbase
{
namespace share
{
class ObSQLiteConnectionPool;
class SCN;

// part compaction related member from __all_tablet_meta_table
struct ObTabletCompactionScnInfo
{
public:
  ObTabletCompactionScnInfo()
   : tablet_id_(0),
     compaction_scn_(0),
     report_scn_(0),
     status_(ObTabletRuntimeInfo::SCN_STATUS_MAX)
   {}
  ObTabletCompactionScnInfo(
      const ObTabletID &tablet_id,
      const ObTabletRuntimeInfo::ScnStatus status)
   : tablet_id_(tablet_id.id()),
     compaction_scn_(0),
     report_scn_(0),
     status_(status)
   {}
  bool is_valid() const
  {
    return tablet_id_ > 0 && report_scn_ >= 0;
  }
  // only check when last compaction type is major
  bool could_schedule_next_round(const int64_t major_frozen_scn)
  {
    return ObTabletRuntimeInfo::SCN_STATUS_IDLE == status_ && major_frozen_scn <= report_scn_;
  }
  void reset()
  {
    tablet_id_ = 0;
    compaction_scn_ = 0;
    report_scn_ = 0;
    status_ = ObTabletRuntimeInfo::SCN_STATUS_MAX;
  }
  TO_STRING_KV(K_(tablet_id), K_(compaction_scn), K_(report_scn), K_(status));
public:
  int64_t tablet_id_;
  int64_t compaction_scn_;
  int64_t report_scn_;
  ObTabletRuntimeInfo::ScnStatus status_;
};

// CRUD operation to __all_tablet_meta_table
class ObTabletMetaTableCompactionOperator
{
public:
  static int get_status(
      ObSQLiteConnectionPool *meta_db_pool,
      const ObTabletCompactionScnInfo &input_info,
      ObTabletCompactionScnInfo &ret_info);
  // update report_scn of all tablets in @tablet_ids
  static int batch_update_report_scn(
      ObSQLiteConnectionPool *meta_db_pool,
      const uint64_t global_broadcast_scn_val,
      const common::ObIArray<ObTabletID> &tablet_ids,
      const ObTabletRuntimeInfo::ScnStatus &except_status);
  // after major_freeze, update all tablets' report_scn to global_broadcast_scn_val
  static int batch_update_report_scn(
      ObSQLiteConnectionPool *meta_db_pool,
      const uint64_t global_broadcast_scn_val,
      const ObTabletRuntimeInfo::ScnStatus &except_status,
      const volatile bool &stop);
  // designed for 'clear merge error'. it updates all tablets' status to SCN_STATUS_IDLE
  static int batch_update_status(ObSQLiteConnectionPool *meta_db_pool);
  static int batch_update_unequal_report_scn_tablet(ObSQLiteConnectionPool *meta_db_pool,
      const int64_t major_frozen_scn,
      const common::ObIArray<ObTabletID> &input_tablet_id_array);
  static int get_min_compaction_scn(ObSQLiteConnectionPool *meta_db_pool,
                                    SCN &min_compaction_scn);
  static int range_scan_for_compaction(ObSQLiteConnectionPool *meta_db_pool,
      const int64_t compaction_scn,
      const common::ObTabletID &start_tablet_id,
      const int64_t batch_size,
      const bool only_unreported,
      common::ObTabletID &end_tablet_id,
      ObIArray<ObTabletRuntimeInfo> &tablet_infos);
private:
  static int inner_range_scan_for_compaction(ObSQLiteConnectionPool *meta_db_pool,
      const int64_t compaction_scn,
      const common::ObTabletID &start_tablet_id,
      const int64_t batch_size,
      const bool only_unreported,
      common::ObTabletID &end_tablet_id,
      ObIArray<ObTabletRuntimeInfo> &tablet_infos);
  static int inner_get_max_tablet_id_in_range(ObSQLiteConnectionPool *meta_db_pool,
      const common::ObTabletID &start_tablet_id,
      const int64_t batch_size,
      common::ObTabletID &max_tablet_id);
  static int get_estimated_timeout_us(ObSQLiteConnectionPool *meta_db_pool,
                                      int64_t &estimated_timeout_us);
  static int get_tablet_count(ObSQLiteConnectionPool *meta_db_pool,
                              int64_t &tablet_count);
  static int get_next_batch_tablet_ids(ObSQLiteConnectionPool *meta_db_pool,
      const int64_t batch_update_cnt,
      common::ObIArray<ObTabletID> &tablet_ids);
private:
  const static int64_t MAX_BATCH_COUNT = 500;
};

} // end namespace share
} // end namespace oceanbase

#endif  // OCEANBASE_SHARE_OB_TABLET_META_TABLE_COMPACTION_OPERATOR_
