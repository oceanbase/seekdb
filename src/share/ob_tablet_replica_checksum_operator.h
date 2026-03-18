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

#ifndef OCEANBASE_SHARE_OB_TABLET_REPLICA_CHECKSUM_OPERATOR_H_
#define OCEANBASE_SHARE_OB_TABLET_REPLICA_CHECKSUM_OPERATOR_H_

#include "lib/container/ob_se_array.h"
#include "lib/net/ob_addr.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/string/ob_sql_string.h"
#include "common/ob_tablet_id.h"
#include "share/ob_ls_id.h"
#include "share/schema/ob_table_schema.h"
#include "share/tablet/ob_tablet_info.h"
#include "share/ob_column_checksum_error_operator.h"
#include "share/scn.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"

// Forward declaration
namespace oceanbase {
namespace share {
class ObSQLiteConnection;
class ObTabletReplicaChecksumTableStorage;
}
}
#include "storage/compaction/ob_tenant_medium_checker.h"
#include "share/compaction/ob_array_with_map.h"

namespace oceanbase
{
namespace common
{
class ObISQLClient;
class ObAddr;
class ObTabletID;
class ObMySQLTransaction;
namespace sqlclient
{
class ObMySQLResult;
}
}
namespace share
{
class ObTabletReplica;

struct ObTabletReplicaReportColumnMeta
{
public:
  ObTabletReplicaReportColumnMeta();
  ~ObTabletReplicaReportColumnMeta();
  void reset();
  bool is_valid() const;
  int init(const common::ObIArray<int64_t> &column_checksums);
  int assign(const ObTabletReplicaReportColumnMeta &other);
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int64_t get_serialize_size() const;
  int deserialize(const char *buf, const int64_t buf_len, int64_t &pos);
  int check_checksum(const ObTabletReplicaReportColumnMeta &other, const int64_t pos, bool &is_equal) const;
  int check_all_checksums(const ObTabletReplicaReportColumnMeta &other, bool &is_equal) const;
  int check_equal(const ObTabletReplicaReportColumnMeta &other, bool &is_equal) const;
  int64_t get_string(char *buf, const int64_t buf_len) const;
  int64_t get_string_length() const;
  TO_STRING_KV(K_(compat_version), K_(checksum_method), K_(checksum_bytes), K_(column_checksums));

  int set_with_str(const int64_t compaction_scn_val, const ObString &str);
  int set_with_str(const ObDataChecksumType type, const ObString &str);
  int get_str_obj(
      const ObDataChecksumType type,
      common::ObIAllocator &allocator,
      ObObj &obj,
      common::ObString &str) const;
  int get_hex_str(
      common::ObIAllocator &allocator,
      common::ObString &column_meta_hex_str) const;
private:
  int set_with_hex_str(const ObString &hex_str);
  int set_with_serialize_str(const ObString &serialize_str);
  int get_serialize_str(
      common::ObIAllocator &allocator,
      common::ObString &str) const;
public:
  static const int64_t MAX_OCCUPIED_BYTES = 4000 * 8 + 11;
  static const int64_t DEFAULT_COLUMN_CNT = 64;
  static const int64_t MAGIC_NUMBER = static_cast<int64_t>(0x636865636B636F6CL); // cstirng of "checkcol"
  int8_t compat_version_;
  int8_t checksum_method_;
  int8_t checksum_bytes_;
  common::ObSEArray<int64_t, DEFAULT_COLUMN_CNT> column_checksums_;
  bool is_inited_;
};

struct ObTabletReplicaChecksumItem
{
public:
  ObTabletReplicaChecksumItem();
  virtual ~ObTabletReplicaChecksumItem() { reset(); };
  void reset();
  bool is_key_valid() const;
  bool is_valid() const;
  bool is_same_tablet(const ObTabletReplicaChecksumItem &other) const;
  int verify_column_checksum(const ObTabletReplicaChecksumItem &other) const;
  int verify_column_checksum_between_diffrent_replica(const ObTabletReplicaChecksumItem &other) const;
  int assign(const ObTabletReplicaChecksumItem &other);
  int set_tenant_id(const uint64_t tenant_id);
  int check_data_checksum_type(bool &is_cs_replica) const;
  void set_data_checksum_type(const bool is_cs_replica);
  common::ObTabletID get_tablet_id() const { return tablet_id_; }

  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(tablet_id), K_(server), K_(row_count),
      K_(compaction_scn), K_(data_checksum), K_(column_meta), K_(data_checksum_type), K_(co_base_snapshot_version));

public:
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  common::ObTabletID tablet_id_;
  common::ObAddr server_;
  int64_t row_count_;
  SCN compaction_scn_;
  int64_t data_checksum_;
  ObTabletReplicaReportColumnMeta column_meta_;
  ObDataChecksumType data_checksum_type_;
  SCN co_base_snapshot_version_;
};
typedef ObArrayWithMap<share::ObTabletReplicaChecksumItem> ObReplicaCkmArray;

// Operator for __all_tablet_replica_checksum
class ObTabletReplicaChecksumOperator
{
public:
  // Initialize SQLite storage (called once at startup)
  static int init();

  // Get a batch of checksum_items
  // Default: checksum_items' compaction_scn = @compaction_scn
  // If include_larger_than = true: checksum_items' compaction_scn >= @compaction_scn
  static int batch_get(
      const uint64_t tenant_id,
      const common::ObIArray<ObTabletLSPair> &pairs,
      const SCN &compaction_scn,
      common::ObISQLClient &sql_proxy,
      ObReplicaCkmArray &items,
      const bool include_larger_than,
      const int32_t group_id);
  // Update checksum items within a SQLite transaction
  static int batch_update_with_trans(
      share::ObSQLiteConnection *conn,
      const uint64_t tenant_id,
      const common::ObIArray<ObTabletReplicaChecksumItem> &item);
  // Remove checksum items within a SQLite transaction
  static int batch_remove_with_trans(
      share::ObSQLiteConnection *conn,
      const uint64_t tenant_id,
      const common::ObIArray<ObTabletReplica> &tablet_replicas);
  static int remove_residual_checksum(
      common::ObISQLClient &sql_client,
      const uint64_t tenant_id,
      const ObAddr &server,
      const int64_t limit,
      int64_t &affected_rows);
  static int get_tablets_replica_checksum(
      const uint64_t tenant_id,
      const ObIArray<compaction::ObTabletCheckInfo> &pairs,
      ObReplicaCkmArray &tablet_replica_checksum_items);
  static int get_visible_column_meta(
      const ObTabletReplicaReportColumnMeta &column_meta,
      common::ObIAllocator &allocator,
      common::ObString &column_meta_visible_str);
  static int range_get(
      const uint64_t tenant_id,
      const common::ObTabletID &start_tablet_id,
      const int64_t range_size,
      const int32_t group_id,
      common::ObISQLClient &sql_proxy,
      ObIArray<ObTabletReplicaChecksumItem> &items,
      int64_t &tablet_cnt);
  static int range_get(
      const uint64_t tenant_id,
      const common::ObTabletID &start_tablet_id,
      const common::ObTabletID &end_tablet_id,
      const int64_t compaction_scn,
      common::ObISQLClient &sql_proxy,
      ObIArray<ObTabletReplicaChecksumItem> &items);
  static int multi_get(
    const uint64_t tenant_id,
    const ObIArray<ObTabletID> &tablet_id_list,
    const int64_t compaction_scn,
    common::ObISQLClient &sql_proxy,
    ObIArray<ObTabletReplicaChecksumItem> &items);
  static int get_min_compaction_scn(
      const uint64_t tenant_id,
      SCN &min_compaction_scn);

private:
  // ObSimpleCkmInfo and ObTabletSimpleCkmInfoMap removed - only used in OB_BUILD_SHARED_STORAGE
  // range_get_ removed - no longer used
  // inner_batch_insert_or_update_by_sql_ removed - no longer used
  // OB_BUILD_SHARED_STORAGE related functions removed
  // inner_batch_get_by_sql_ removed - no longer used, replaced by SQLite storage
  // construct_batch_get_sql_str_ removed - no longer used, replaced by SQLite storage

  static int construct_tablet_replica_checksum_items_(
      common::sqlclient::ObMySQLResult &res,
      common::ObIArray<ObTabletReplicaChecksumItem> &items,
      int64_t &tablet_items_cnt);
  static int construct_tablet_replica_checksum_items_(
    sqlclient::ObMySQLResult &res,
    ObReplicaCkmArray &items);
  static int construct_tablet_replica_checksum_item_(
      common::sqlclient::ObMySQLResult &res,
      ObTabletReplicaChecksumItem &item);

  // batch_iterate_replica_checksum_range_ removed - no longer used
  // batch_check_tablet_checksum_in_range_ removed - no longer used

public:
  // get column checksum from item and store result in map
  // KV of @column_ckm_map is: <column_id, column_checksum>
  static int get_tablet_replica_checksum_items(
      const uint64_t tenant_id,
      common::ObMySQLProxy &mysql_proxy,
      const SCN &compaction_scn,
      const common::ObIArray<ObTabletLSPair> &tablet_pairs,
      ObReplicaCkmArray &items);
  static int construct_tablet_id_list(const ObIArray<ObTabletID> &tablet_ids, ObSqlString &sql);
private:
  static ObTabletReplicaChecksumTableStorage storage_;

  const static int64_t MAX_BATCH_COUNT = 128;
  const static int64_t PRINT_LOG_INVERVAL = 2 * 60 * 1000 * 1000L; // 2m
  const static int64_t MOCK_COLUMN_CHECKSUM = 580000000000000;
};

// construct_batch_get_sql_str_ template removed - no longer used, replaced by SQLite storage

class ObTabletDataChecksumChecker
{
public:
  ObTabletDataChecksumChecker();
  ~ObTabletDataChecksumChecker();
  void reset();
  int set_data_checksum(const ObTabletReplicaChecksumItem& curr_item);
  int check_data_checksum(const ObTabletReplicaChecksumItem& curr_item);
  TO_STRING_KV(KPC_(normal_ckm_item), K_(cs_replica_ckm_items));
private:
  const ObTabletReplicaChecksumItem *normal_ckm_item_;
  ObSEArray<const ObTabletReplicaChecksumItem *, 3> cs_replica_ckm_items_; // at most support 3 cs replicas now
};

} // share
} // oceanbase

#endif // OCEANBASE_SHARE_OB_TABLET_REPLICA_CHECKSUM_OPERATOR_H_
