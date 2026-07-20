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

#ifndef OCEANBASE_SHARE_OB_TABLET_CHECKSUM_OPERATOR_H_
#define OCEANBASE_SHARE_OB_TABLET_CHECKSUM_OPERATOR_H_

#include "lib/container/ob_iarray.h"
#include "common/mysqlclient/ob_isql_client.h"
#include "common/ob_zone.h"
#include "common/ob_tablet_id.h"
#include "share/ob_tablet_replica_checksum_operator.h"

namespace oceanbase
{
namespace share
{
// Memory item for __all_tablet_checksum table
//
// The data in __all_tablet_checksum table, is sync from
// __all_tablet_replica_checksum table. This table will be
// sync to standby cluster from primary cluster, for these
// two cluster checksum verifying.
struct ObTabletChecksumItem
{
public:
  ObTabletChecksumItem() 
    : tablet_id_(), data_checksum_(-1),
      row_count_(0), compaction_scn_(), replica_type_(0), column_meta_() {}
  virtual ~ObTabletChecksumItem() = default;

  void reset();
  bool is_valid() const;
  int verify_tablet_column_checksum(const ObTabletReplicaChecksumItem &replica_item) const;
  int assign(const ObTabletReplicaChecksumItem &replica_item);
  int assign(const ObTabletChecksumItem &other);
  ObTabletChecksumItem &operator =(const ObTabletChecksumItem &other);
  common::ObTabletID get_tablet_id() const { return tablet_id_; }
  TO_STRING_KV(K_(tablet_id), K_(data_checksum), K_(row_count),
    K_(compaction_scn), K_(replica_type), K_(column_meta));
  
  
  common::ObTabletID tablet_id_;
  int64_t data_checksum_;
  int64_t row_count_;
  SCN compaction_scn_;
  int replica_type_;
  ObTabletReplicaReportColumnMeta column_meta_;
};

// CRUD operation to __all_tablet_checksum table
class ObTabletChecksumOperator
{
public:
  // range get tablet checksum
  // @compaction_scn:
  //   if equals to min_scn, means get all records
  //   if greater than min_scn, means get record with this compaction_scn.
  //   else, invalid argument
  static int load_tablet_checksum_items(
      common::ObISQLClient &sql_client,
      const common::ObTabletID &start_tablet_id,
      const int64_t batch_cnt,
      const SCN &compaction_scn,
      common::ObIArray<ObTabletChecksumItem> &items);
  // multi get tablet checksum
  static int load_tablet_checksum_items(
      common::ObISQLClient &sql_client,
      const common::ObIArray<common::ObTabletID> &tablet_ids,
      const SCN &compaction_scn,
      common::ObIArray<ObTabletChecksumItem> &items);
  static int load_tablet_checksum_items(
      common::ObISQLClient &sql_client,
      const common::ObSqlString &sql,
      common::ObIArray<ObTabletChecksumItem> &items);
  static int update_tablet_checksum_items(
      common::ObISQLClient &sql_client,
      common::ObIArray<ObTabletChecksumItem> &items);
  // delete records whose compaction_scn <= @gc_compaction_scn for the special tablet
  static int delete_special_tablet_checksum_items(
      common::ObISQLClient &sql_client,
      const SCN &gc_compaction_scn);
  // delete limited records whose compaction_scn <= @gc_compaction_scn
  // while the special tablet record can't be deleted.
  static int delete_tablet_checksum_items(
      common::ObISQLClient &sql_client,
      const SCN &gc_compaction_scn,
      const int64_t limit_cnt,
      int64_t &affected_rows);
  static int load_all_compaction_scn(
      common::ObISQLClient &sql_client,
      common::ObIArray<SCN> &compaction_scn_arr);
  static int is_first_tablet_checksum_exist(
      common::ObISQLClient &sql_client, 
      const SCN &compaction_scn,
      bool &is_exist);

private:
  static int construct_load_sql_str_(const common::ObTabletID &start_tablet_id,
      const int64_t batch_cnt,
      const SCN &compaction_scn,
      common::ObSqlString &sql);
  static int construct_load_sql_str_(const common::ObIArray<common::ObTabletID> &tablet_ids,
      const int64_t start_idx,
      const int64_t end_idx,
      const SCN &compaction_scn,
      common::ObSqlString &sql);
  static int insert_or_update_tablet_checksum_items_(
      common::ObISQLClient &sql_client,
      common::ObIArray<ObTabletChecksumItem> &items,
      const bool is_update);
  static int get_tablet_cnt(
      ObISQLClient &sql_client,
      int64_t &tablet_cnt);
  static int get_estimated_timeout_us(
      ObISQLClient &sql_client,
      int64_t &estimated_timeout_us);

private:
  const static int64_t MAX_BATCH_COUNT = 99;
};

} // end namespace share
} // end namespace oceanbase

#endif // OCEANBASE_SHARE_OB_TABLET_CHECKSUM_OPERATOR_H_
