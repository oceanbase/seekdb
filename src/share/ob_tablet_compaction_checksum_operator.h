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

#ifndef OCEANBASE_SHARE_OB_TABLET_COMPACTION_CHECKSUM_OPERATOR_H_
#define OCEANBASE_SHARE_OB_TABLET_COMPACTION_CHECKSUM_OPERATOR_H_

#include "lib/container/ob_se_array.h"
#include "common/ob_tablet_id.h"
#include "common/object/ob_object.h"
#include "share/scn.h"
#include "share/tablet/ob_tablet_info.h"

// Forward declaration
namespace oceanbase {
namespace share {
class ObSQLiteConnection;
class ObTabletCompactionChecksumTableStorage;
}
}
#include "storage/compaction/ob_tablet_check_info.h"  // uses only ObTabletCheckInfo, use the pure header created in batch 5(L2)
#include "share/compaction/ob_array_with_map.h"

namespace oceanbase
{
namespace share
{
enum class ObDataChecksumType : uint8_t;

struct ObTabletChecksumColumnMeta
{
public:
  ObTabletChecksumColumnMeta();
  ~ObTabletChecksumColumnMeta();
  void reset();
  bool is_valid() const;
  int init(const common::ObIArray<int64_t> &column_checksums);
  int assign(const ObTabletChecksumColumnMeta &other);
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int64_t get_serialize_size() const;
  int deserialize(const char *buf, const int64_t buf_len, int64_t &pos);
  int check_checksum(const ObTabletChecksumColumnMeta &other, const int64_t pos, bool &is_equal) const;
  int check_all_checksums(const ObTabletChecksumColumnMeta &other, bool &is_equal) const;
  int check_equal(const ObTabletChecksumColumnMeta &other, bool &is_equal) const;
  int64_t get_string(char *buf, const int64_t buf_len) const;
  int64_t get_string_length() const;
  TO_STRING_KV(K_(format_version), K_(checksum_method), K_(checksum_bytes), K_(column_checksums));

  int set_with_str(const ObString &str);
  int set_with_str(const ObDataChecksumType type, const ObString &str);
  int get_str_obj(
      const ObDataChecksumType type,
      common::ObIAllocator &allocator,
      common::ObObj &obj,
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
  static constexpr int64_t MAGIC_NUMBER = static_cast<int64_t>(0x636865636B636F6CL); // cstirng of "checkcol"
  static constexpr int8_t FORMAT_VERSION = 0;
  int8_t format_version_;
  int8_t checksum_method_;
  int8_t checksum_bytes_;
  common::ObSEArray<int64_t, DEFAULT_COLUMN_CNT> column_checksums_;
  bool is_inited_;
};

struct ObTabletCompactionChecksumItem
{
public:
  ObTabletCompactionChecksumItem();
  virtual ~ObTabletCompactionChecksumItem() { reset(); };
  void reset();
  bool is_key_valid() const;
  bool is_valid() const;
  bool is_same_tablet(const ObTabletCompactionChecksumItem &other) const;
  int verify_column_checksum(const ObTabletCompactionChecksumItem &other) const;
  int assign(const ObTabletCompactionChecksumItem &other);
  int set_ckm_mem_attr();
  void set_data_checksum_type();
  common::ObTabletID get_tablet_id() const { return tablet_id_; }

  TO_STRING_KV(K_(tablet_id), K_(row_count),
      K_(compaction_scn), K_(data_checksum), K_(column_meta), K_(data_checksum_type));

public:
  common::ObTabletID tablet_id_;
  int64_t row_count_;
  SCN compaction_scn_;
  int64_t data_checksum_;
  ObTabletChecksumColumnMeta column_meta_;
  ObDataChecksumType data_checksum_type_;
};
typedef ObArrayWithMap<share::ObTabletCompactionChecksumItem> ObTabletCompactionChecksumArray;

// Local tablet compaction checksums are persisted in the legacy
// __all_tablet_replica_checksum table format.
class ObTabletCompactionChecksumOperator
{
public:
  // Initialize SQLite storage (called once at startup)
  static int init();

  // Get a batch of checksum_items
  // Default: checksum_items' compaction_scn = @compaction_scn
  // If include_larger_than = true: checksum_items' compaction_scn >= @compaction_scn
  static int batch_get(
      const common::ObIArray<common::ObTabletID> &tablet_ids,
      const SCN &compaction_scn,
      ObTabletCompactionChecksumArray &items,
      const bool include_larger_than,
      const int32_t group_id);
  // Update checksum items within a SQLite transaction
  static int batch_update_with_trans(
      share::ObSQLiteConnection *conn,
      const common::ObIArray<ObTabletCompactionChecksumItem> &item);
  // Remove checksum items within a SQLite transaction
  static int batch_remove_with_trans(
      share::ObSQLiteConnection *conn,
      const common::ObIArray<common::ObTabletID> &tablet_ids);
  static int get_tablet_compaction_checksums(const ObIArray<compaction::ObTabletCheckInfo> &pairs,
      ObTabletCompactionChecksumArray &tablet_compaction_checksum_items);
  static int get_visible_column_meta(
      const ObTabletChecksumColumnMeta &column_meta,
      common::ObIAllocator &allocator,
      common::ObString &column_meta_visible_str);
  static int recover_mock_column_meta(ObTabletChecksumColumnMeta &column_meta);
  static int range_get(const common::ObTabletID &start_tablet_id,
      const int64_t range_size,
      const int32_t group_id,
      ObIArray<ObTabletCompactionChecksumItem> &items,
      int64_t &tablet_cnt);
  static int range_get(const common::ObTabletID &start_tablet_id,
      const common::ObTabletID &end_tablet_id,
      const int64_t compaction_scn,
      ObIArray<ObTabletCompactionChecksumItem> &items);
  static int multi_get(
    const ObIArray<ObTabletID> &tablet_id_list,
    const int64_t compaction_scn,
    ObIArray<ObTabletCompactionChecksumItem> &items);
  static int get_min_compaction_scn(SCN &min_compaction_scn);

public:
  // get column checksum from item and store result in map
  // KV of @column_ckm_map is: <column_id, column_checksum>
  static int get_tablet_compaction_checksum_items(const SCN &compaction_scn,
      const common::ObIArray<common::ObTabletID> &tablet_ids,
      ObTabletCompactionChecksumArray &items);
private:
  static ObTabletCompactionChecksumTableStorage storage_;

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
  int set_data_checksum(const ObTabletCompactionChecksumItem& curr_item);
  int check_data_checksum(const ObTabletCompactionChecksumItem& curr_item);
  TO_STRING_KV(KPC_(normal_ckm_item));
private:
  const ObTabletCompactionChecksumItem *normal_ckm_item_;
};

} // share
} // oceanbase

#endif // OCEANBASE_SHARE_OB_TABLET_COMPACTION_CHECKSUM_OPERATOR_H_
