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

#ifndef OCEANBASE_SHARE_OB_TABLET_LOCAL_CHECKSUM_OPERATOR_H_
#define OCEANBASE_SHARE_OB_TABLET_LOCAL_CHECKSUM_OPERATOR_H_

#include "lib/container/ob_se_array.h"
#include "common/ob_tablet_id.h"
#include "share/ob_ls_id.h"
#include "share/tablet/ob_tablet_info.h"
#include "share/scn.h"

// Forward declaration
namespace oceanbase {
namespace share {
class ObSQLiteConnection;
class ObTabletLocalChecksumTableStorage;
}
}
#include "storage/compaction/ob_tablet_check_info.h"  // uses only ObTabletCheckInfo, use the pure header created in batch 5(L2)
#include "share/compaction/ob_array_with_map.h"

namespace oceanbase
{
namespace common
{
class ObTabletID;
}
namespace share
{
class ObTabletRuntimeInfo;

struct ObTabletColumnChecksumMeta
{
public:
  ObTabletColumnChecksumMeta();
  ~ObTabletColumnChecksumMeta();
  void reset();
  bool is_valid() const;
  int init(const common::ObIArray<int64_t> &column_checksums);
  int assign(const ObTabletColumnChecksumMeta &other);
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int64_t get_serialize_size() const;
  int deserialize(const char *buf, const int64_t buf_len, int64_t &pos);
  int check_checksum(const ObTabletColumnChecksumMeta &other, const int64_t pos, bool &is_equal) const;
  int check_all_checksums(const ObTabletColumnChecksumMeta &other, bool &is_equal) const;
  int check_equal(const ObTabletColumnChecksumMeta &other, bool &is_equal) const;
  int64_t get_string(char *buf, const int64_t buf_len) const;
  int64_t get_string_length() const;
  TO_STRING_KV(K_(compat_version), K_(checksum_method), K_(checksum_bytes), K_(column_checksums));

  int set_with_str(const ObString &str);
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

struct ObTabletLocalChecksumItem
{
public:
  ObTabletLocalChecksumItem();
  virtual ~ObTabletLocalChecksumItem() { reset(); };
  void reset();
  bool is_key_valid() const;
  bool is_valid() const;
  int assign(const ObTabletLocalChecksumItem &other);
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
  ObTabletColumnChecksumMeta column_meta_;
  ObDataChecksumType data_checksum_type_;
};
typedef ObArrayWithMap<share::ObTabletLocalChecksumItem> ObLocalTabletChecksumArray;

// Operator for __all_tablet_local_checksum
class ObTabletLocalChecksumOperator
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
      ObLocalTabletChecksumArray &items,
      const bool include_larger_than);
  // Update checksum items within a SQLite transaction
  static int batch_update_with_trans(
      share::ObSQLiteConnection *conn,
      const common::ObIArray<ObTabletLocalChecksumItem> &item);
  // Remove checksum items within a SQLite transaction
  static int batch_remove_with_trans(
      share::ObSQLiteConnection *conn,
      const common::ObIArray<ObTabletRuntimeInfo> &tablet_infos);
  static int get_tablet_checksums(const ObIArray<compaction::ObTabletCheckInfo> &pairs,
      ObLocalTabletChecksumArray &tablet_checksum_items);
  static int get_visible_column_meta(
      const ObTabletColumnChecksumMeta &column_meta,
      common::ObIAllocator &allocator,
      common::ObString &column_meta_visible_str);
public:
  static int get_local_tablet_checksum_items(
      const SCN &compaction_scn,
      const common::ObIArray<common::ObTabletID> &tablet_ids,
      ObLocalTabletChecksumArray &items);
  static int recover_mock_column_meta(ObTabletColumnChecksumMeta &column_meta);
private:
  static ObTabletLocalChecksumTableStorage storage_;

  const static int64_t MOCK_COLUMN_CHECKSUM = 580000000000000;
};

} // share
} // oceanbase

#endif // OCEANBASE_SHARE_OB_TABLET_LOCAL_CHECKSUM_OPERATOR_H_
