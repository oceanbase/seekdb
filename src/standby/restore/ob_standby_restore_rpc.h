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

#ifndef OCEANBASE_STANDBY_RESTORE_RPC_H_
#define OCEANBASE_STANDBY_RESTORE_RPC_H_

#include "lib/container/ob_array.h"
#include "lib/utility/ob_unify_serialize.h"
#include "share/ob_ls_id.h"
#include "share/ob_table_range.h"
#include "storage/blocksstable/ob_datum_rowkey.h"
#include "storage/blocksstable/ob_logic_macro_id.h"
#include "storage/blocksstable/ob_sstable_meta.h"
#include "storage/ob_i_table.h"
#include "storage/ob_storage_schema.h"
#include "storage/tablet/ob_tablet_meta.h"

namespace oceanbase
{
namespace blocksstable
{
struct ObSSTableMergeRes;
class ObSSTable;

// Wire snapshot used only by standby baseline copy.  It deliberately contains
// no distributed-HA, shared-storage, or column-store metadata.
class ObMigrationSSTableParam final
{
public:
  ObMigrationSSTableParam();
  ~ObMigrationSSTableParam();
  bool is_valid() const;
  bool is_empty_sstable() const;
  void reset();
  int assign(const ObMigrationSSTableParam &other);
  int build_from_sstable(const ObSSTable &sstable);
  int check_sstable_meta(const ObSSTableMeta &sstable_meta) const;
  int get_merge_res(ObSSTableMergeRes &res) const;
  int serialize(char *buf, const int64_t len, int64_t &pos) const;
  int deserialize(const char *buf, const int64_t len, int64_t &pos);
  int64_t get_serialize_size() const;
  TO_STRING_KV(K_(basic_meta), K_(column_checksums), K_(table_key), K_(is_small_sstable));

  common::ObArenaAllocator allocator_;
  ObSSTableBasicMeta basic_meta_;
  common::ObArray<int64_t> column_checksums_;
  storage::ObITable::TableKey table_key_;
  bool is_small_sstable_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMigrationSSTableParam);
};
} // namespace blocksstable

namespace storage
{
class ObTablet;

struct ObCopyTabletStatus final
{
  enum STATUS { TABLET_EXIST = 0, TABLET_NOT_EXIST = 1, MAX_STATUS };
  static bool is_valid(const STATUS status) { return status >= TABLET_EXIST && status < MAX_STATUS; }
};

// Current tablet metadata plus the separately persisted storage schema form the
// complete metadata side of a standby physical baseline.
class ObMigrationTabletParam final
{
public:
  ObMigrationTabletParam();
  ~ObMigrationTabletParam();
  bool is_valid() const;
  bool is_empty_shell() const;
  void reset();
  int assign(const ObMigrationTabletParam &other);
  int build_deleted_tablet_info(const share::ObLSID &ls_id, const common::ObTabletID &tablet_id);
  int build_from_tablet(const ObTablet &tablet);
  int serialize(char *buf, const int64_t len, int64_t &pos) const;
  int deserialize(const char *buf, const int64_t len, int64_t &pos);
  int64_t get_serialize_size() const;
  TO_STRING_KV(K_(tablet_id), K_(is_deleted), K_(tablet_meta), K_(storage_schema));

  common::ObTabletID tablet_id_;
  bool is_deleted_;
  ObTabletMeta tablet_meta_;
  ObStorageSchema storage_schema_;
  common::ObArenaAllocator allocator_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMigrationTabletParam);
};

struct ObCopyMacroRangeInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObCopyMacroRangeInfo();
  ~ObCopyMacroRangeInfo() = default;
  bool is_valid() const;
  void reset();
  void reuse();
  int assign(const ObCopyMacroRangeInfo &other);
  int deep_copy_start_end_key(const blocksstable::ObDatumRowkey &rowkey);
  TO_STRING_KV(K_(start_macro_block_id), K_(end_macro_block_id),
      K_(macro_block_count), K_(start_macro_block_end_key));

  blocksstable::ObLogicMacroBlockId start_macro_block_id_;
  blocksstable::ObLogicMacroBlockId end_macro_block_id_;
  int64_t macro_block_count_;
  bool is_leader_restore_;
  blocksstable::ObStorageDatum datums_[OB_INNER_MAX_ROWKEY_COLUMN_NUMBER];
  blocksstable::ObDatumRowkey start_macro_block_end_key_;
  common::ObArenaAllocator allocator_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCopyMacroRangeInfo);
};

struct ObCopySSTableMacroRangeInfo final
{
  ObCopySSTableMacroRangeInfo();
  ~ObCopySSTableMacroRangeInfo() = default;
  bool is_valid() const;
  void reset();
  int assign(const ObCopySSTableMacroRangeInfo &other);
  TO_STRING_KV(K_(copy_table_key), K_(copy_macro_range_array));

  ObITable::TableKey copy_table_key_;
  common::ObArray<ObCopyMacroRangeInfo> copy_macro_range_array_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCopySSTableMacroRangeInfo);
};
} // namespace storage

namespace obcall
{
enum ObCopyMacroBlockDataType { MACRO_DATA = 0, MACRO_META_ROW = 1, MAX };

struct ObCopyMacroBlockInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObCopyMacroBlockInfo();
  TO_STRING_KV(K_(logical_id), K_(data_type));
  blocksstable::ObLogicMacroBlockId logical_id_;
  ObCopyMacroBlockDataType data_type_;
};

struct ObCopyMacroBlockRangeArg final
{
  OB_UNIS_VERSION(1);
public:
  enum { DISABLE_MACRO_BLOCK_REUSE_DATA_VERSION = -1 };
  ObCopyMacroBlockRangeArg();
  bool is_valid() const;
  TO_STRING_KV(K_(ls_id), K_(table_key), K_(data_version), K_(backfill_tx_scn),
      K_(copy_macro_range_info), K_(copy_macro_block_infos));
  share::ObLSID ls_id_;
  storage::ObITable::TableKey table_key_;
  int64_t data_version_;
  share::SCN backfill_tx_scn_;
  storage::ObCopyMacroRangeInfo copy_macro_range_info_;
  common::ObSArray<ObCopyMacroBlockInfo> copy_macro_block_infos_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCopyMacroBlockRangeArg);
};

struct ObCopyMacroBlockHeader final
{
  OB_UNIS_VERSION(1);
public:
  ObCopyMacroBlockHeader();
  void reset();
  TO_STRING_KV(K_(is_reuse_macro_block), K_(occupy_size), K_(data_type));
  bool is_reuse_macro_block_;
  int64_t occupy_size_;
  ObCopyMacroBlockDataType data_type_;
};

struct ObCopyTabletInfoArg final
{
  OB_UNIS_VERSION(1);
public:
  ObCopyTabletInfoArg();
  TO_STRING_KV(K_(ls_id), K_(tablet_id_list), K_(version));
  share::ObLSID ls_id_;
  common::ObSArray<common::ObTabletID> tablet_id_list_;
  uint64_t version_;
};

struct ObCopyTabletInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObCopyTabletInfo();
  void reset();
  bool is_valid() const;
  TO_STRING_KV(K_(tablet_id), K_(status), K_(param), K_(data_size), K_(version));
  common::ObTabletID tablet_id_;
  storage::ObCopyTabletStatus::STATUS status_;
  storage::ObMigrationTabletParam param_;
  int64_t data_size_;
  uint64_t version_;
};

struct ObCopyTabletSSTableInfoArg final
{
  OB_UNIS_VERSION(1);
public:
  ObCopyTabletSSTableInfoArg();
  void reset();
  bool is_valid() const;
  TO_STRING_KV(K_(tablet_id), K_(max_major_sstable_snapshot),
      K_(minor_sstable_scn_range), K_(ddl_sstable_scn_range));
  common::ObTabletID tablet_id_;
  int64_t max_major_sstable_snapshot_;
  share::ObScnRange minor_sstable_scn_range_;
  share::ObScnRange ddl_sstable_scn_range_;
};

struct ObCopyTabletsSSTableInfoArg final
{
  OB_UNIS_VERSION(1);
public:
  ObCopyTabletsSSTableInfoArg();
  void reset();
  int assign(const ObCopyTabletsSSTableInfoArg &other);
  TO_STRING_KV(K_(ls_id), K_(tablet_sstable_info_arg_list), K_(version));
  share::ObLSID ls_id_;
  common::ObSArray<ObCopyTabletSSTableInfoArg> tablet_sstable_info_arg_list_;
  uint64_t version_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCopyTabletsSSTableInfoArg);
};

struct ObCopyTabletSSTableInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObCopyTabletSSTableInfo();
  void reset();
  int assign(const ObCopyTabletSSTableInfo &other);
  bool is_valid() const;
  TO_STRING_KV(K_(tablet_id), K_(table_key), K_(param));
  common::ObTabletID tablet_id_;
  storage::ObITable::TableKey table_key_;
  blocksstable::ObMigrationSSTableParam param_;
};

struct ObCheckRestorePreconditionResult final
{
  OB_UNIS_VERSION(1);
public:
  ObCheckRestorePreconditionResult();
  TO_STRING_KV(K_(required_disk_size), K_(total_tablet_size), K_(data_version));
  int64_t required_disk_size_;
  int64_t total_tablet_size_;
  uint64_t data_version_;
};

struct ObCopySSTableMacroRangeInfoArg final
{
  OB_UNIS_VERSION(1);
public:
  ObCopySSTableMacroRangeInfoArg();
  bool is_valid() const;
  int assign(const ObCopySSTableMacroRangeInfoArg &other);
  TO_STRING_KV(K_(ls_id), K_(tablet_id), K_(copy_table_key_array),
      K_(macro_range_max_marco_count));
  share::ObLSID ls_id_;
  common::ObTabletID tablet_id_;
  common::ObSArray<storage::ObITable::TableKey> copy_table_key_array_;
  int64_t macro_range_max_marco_count_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCopySSTableMacroRangeInfoArg);
};

struct ObCopySSTableMacroRangeInfoHeader final
{
  OB_UNIS_VERSION(1);
public:
  ObCopySSTableMacroRangeInfoHeader();
  bool is_valid() const;
  void reset();
  TO_STRING_KV(K_(copy_table_key), K_(macro_range_count));
  storage::ObITable::TableKey copy_table_key_;
  int64_t macro_range_count_;
};

struct ObCopyTabletSSTableHeader final
{
  OB_UNIS_VERSION(1);
public:
  ObCopyTabletSSTableHeader();
  void reset();
  bool is_valid() const;
  TO_STRING_KV(K_(tablet_id), K_(status), K_(sstable_count), K_(tablet_meta), K_(version));
  common::ObTabletID tablet_id_;
  storage::ObCopyTabletStatus::STATUS status_;
  int64_t sstable_count_;
  storage::ObMigrationTabletParam tablet_meta_;
  uint64_t version_;
};
} // namespace obcall
} // namespace oceanbase

#endif // OCEANBASE_STANDBY_RESTORE_RPC_H_
