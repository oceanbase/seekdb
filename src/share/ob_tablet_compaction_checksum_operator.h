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
#include "share/schema/ob_table_schema.h"
#include "share/tablet/ob_tablet_info.h"
#include "share/ob_column_checksum_error_operator.h"
#include "share/scn.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"

#include "share/compaction/ob_tablet_check_info.h"
#include "share/compaction/ob_array_with_map.h"

namespace oceanbase
{
namespace common
{
class ObISQLClient;
class ObAddr;
class ObTabletID;
class ObMySQLTransaction;
class ObMySQLProxy;
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
  int assign(const ObTabletReplicaChecksumItem &other);
  int set_ckm_mem_attr();
  void set_data_checksum_type();
  common::ObTabletID get_tablet_id() const { return tablet_id_; }

  TO_STRING_KV(K_(tablet_id), K_(server), K_(row_count),
      K_(compaction_scn), K_(data_checksum), K_(column_meta), K_(data_checksum_type));

public:
  common::ObTabletID tablet_id_;
  common::ObAddr server_;
  int64_t row_count_;
  SCN compaction_scn_;
  int64_t data_checksum_;
  ObTabletReplicaReportColumnMeta column_meta_;
  ObDataChecksumType data_checksum_type_;
};
typedef ObArrayWithMap<share::ObTabletReplicaChecksumItem> ObReplicaCkmArray;

class ObTabletDataChecksumChecker
{
public:
  ObTabletDataChecksumChecker();
  ~ObTabletDataChecksumChecker();
  void reset();
  int set_data_checksum(const ObTabletReplicaChecksumItem& curr_item);
  int check_data_checksum(const ObTabletReplicaChecksumItem& curr_item);
  TO_STRING_KV(KPC_(normal_ckm_item));
private:
  const ObTabletReplicaChecksumItem *normal_ckm_item_;
};

} // share
} // oceanbase

#endif // OCEANBASE_SHARE_OB_TABLET_REPLICA_CHECKSUM_OPERATOR_H_
