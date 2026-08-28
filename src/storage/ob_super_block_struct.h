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

#ifndef OB_STORAGE_SUPER_BLOCK_STRUCT_H_
#define OB_STORAGE_SUPER_BLOCK_STRUCT_H_

#include "common/log/ob_log_cursor.h"
#include "storage/blocksstable/ob_macro_block_id.h"
#include "share/server_snapshot/ob_server_snapshot_id.h"
#include "share/ob_ls_id.h"
#include "common/ob_tablet_id.h"
#include "storage/meta_mem/ob_meta_obj_struct.h"

namespace oceanbase
{
namespace blocksstable {
class ObStorageObjectOpt;
}
namespace storage
{

enum GCTabletType
{
  InvalidType = -1,
  DropTablet = 0,
  CreateAbort = 1,
  DropLS = 2
};

struct ObServerSuperBlockHeader final
{
public:
  static const int32_t SERVER_SUPER_BLOCK_VERSION = 1;
  static const int64_t OB_MAX_SUPER_BLOCK_SIZE = 64 * 1024;

  ObServerSuperBlockHeader();
  ~ObServerSuperBlockHeader() = default;
  bool is_valid() const;
  void reset();
  TO_STRING_KV(K_(version), K_(magic), K_(body_size), K_(body_crc));
  NEED_SERIALIZE_AND_DESERIALIZE;

  int32_t version_;
  int32_t magic_;
  int32_t body_size_;
  int32_t body_crc_;
};

enum class ObServerRuntimeCreateStatus
{
  CREATING = 0,
  CREATED, // 1
  CREATE_ABORT, // 2
  MAX
};

struct ServerSuperBlockBody final
{
public:
  static const int64_t SUPER_BLOCK_BODY_VERSION = 1;

  int64_t create_timestamp_;  // create timestamp
  int64_t modify_timestamp_;  // last modified timestamp
  int64_t macro_block_size_;

  int64_t total_macro_block_count_;
  int64_t total_file_size_;
  // Local recovery cursor and checkpoint entry for the process-wide server runtime.
  common::ObLogCursor replay_start_point_;
  blocksstable::MacroBlockId runtime_meta_entry_;

  ServerSuperBlockBody();
  bool is_valid() const;
  void reset();

  TO_STRING_KV("Type", "ObServerSuperBlockBody",
               K_(create_timestamp),
               K_(modify_timestamp),
               K_(macro_block_size),
               K_(total_macro_block_count),
               K_(total_file_size),
               K_(replay_start_point),
               K_(runtime_meta_entry));

  OB_UNIS_VERSION(SUPER_BLOCK_BODY_VERSION);
};

struct ObServerSuperBlock final
{
public:

  ObServerSuperBlock();
  ~ObServerSuperBlock() = default;

  // represents an entry to an empty linked list, distinguished with the invalid macro block id
  static const blocksstable::MacroBlockId EMPTY_LIST_ENTRY_BLOCK;

  bool is_valid() const;
  void reset();
  NEED_SERIALIZE_AND_DESERIALIZE;
  TO_STRING_KV(K_(header), K_(body));

  OB_INLINE int64_t get_macro_block_size() const
  {
    return body_.macro_block_size_;
  }
  OB_INLINE int64_t get_total_macro_block_count() const
  {
    return body_.total_macro_block_count_;
  }
  OB_INLINE int64_t get_super_block_size() const
  {
    return header_.get_serialize_size() + body_.get_serialize_size();
  }
  int construct_header();
  int format_startup_super_block(const int64_t macro_block_size, const int64_t data_file_size);

  ObServerSuperBlockHeader header_;
  ServerSuperBlockBody body_;
};

struct ObServerSnapshotMeta final
{
public:
  ObServerSnapshotMeta()
    : ls_meta_entry_(oceanbase::storage::ObServerSuperBlock::EMPTY_LIST_ENTRY_BLOCK), snapshot_id_()
  {
  }
  bool is_valid() const;
  TO_STRING_KV(K_(ls_meta_entry), K_(snapshot_id));
  OB_UNIS_VERSION(1);
public:
  blocksstable::MacroBlockId ls_meta_entry_;
  share::ObServerSnapshotID snapshot_id_;
};

enum class ObLSItemStatus : uint8_t
{
  CREATING = 0,
  CREATED,
  CREATE_ABORT,
  DELETED,
  MAX
};

struct ObLSItem
{
public:
  ObLSItem()
    : ls_id_(),
      epoch_(0),
      status_(ObLSItemStatus::MAX),
      min_macro_seq_(UINT64_MAX),
      max_macro_seq_(UINT64_MAX)
  {
  }
  virtual ~ObLSItem() { reset(); }

  void reset()
  {
    ls_id_.reset();
    epoch_ = 0;
    status_ = ObLSItemStatus::MAX;
    min_macro_seq_ = UINT64_MAX;
    max_macro_seq_ = UINT64_MAX;
  }

  bool is_valid() const
  {
    return ls_id_.is_valid()
        && epoch_ >= 0
        && ObLSItemStatus::MAX != status_
        && min_macro_seq_ < max_macro_seq_;
  }

  TO_STRING_KV(K_(ls_id), K_(epoch), K_(status), K_(min_macro_seq), K_(max_macro_seq));
  OB_UNIS_VERSION_V(1);

  share::ObLSID ls_id_;
  int64_t epoch_;
  ObLSItemStatus status_;
  uint64_t min_macro_seq_;
  uint64_t max_macro_seq_;
};

struct ObServerRuntimeSuperBlock final
{
public:
  static const int64_t MAX_SNAPSHOT_NUM = 32;
  static const int64_t SERVER_RUNTIME_SUPER_BLOCK_VERSION = 4;
  static const int64_t MAX_LS_COUNT = 128;
  ObServerRuntimeSuperBlock();
  ObServerRuntimeSuperBlock(const bool is_hidden);
  ~ObServerRuntimeSuperBlock() = default;
  ObServerRuntimeSuperBlock(const ObServerRuntimeSuperBlock &other);
  ObServerRuntimeSuperBlock &operator==(const ObServerRuntimeSuperBlock &other) = delete;
  ObServerRuntimeSuperBlock &operator!=(const ObServerRuntimeSuperBlock &other) = delete;
  void copy_snapshots_from(const ObServerRuntimeSuperBlock &other);
  void reset();
  bool is_valid() const;
  int get_snapshot(const share::ObServerSnapshotID &snapshot_id, ObServerSnapshotMeta &snapshot) const;
  int add_snapshot(const ObServerSnapshotMeta &snapshot);
  int delete_snapshot(const share::ObServerSnapshotID &snapshot_id);
  int check_new_snapshot(const share::ObServerSnapshotID &snapshot_id) const;

  TO_STRING_KV(
               K_(replay_start_point),
               K_(ls_meta_entry),
               K_(tablet_meta_entry),
               K_(is_hidden),
               K_(version),
               K_(snapshot_cnt),
               K_(auto_inc_ls_epoch),
               K_(ls_cnt));

  OB_UNIS_VERSION(SERVER_RUNTIME_SUPER_BLOCK_VERSION);
public:
  
  // Local recovery state for LS and tablet metadata.
  common::ObLogCursor replay_start_point_;
  blocksstable::MacroBlockId ls_meta_entry_;
  blocksstable::MacroBlockId tablet_meta_entry_;

  bool is_hidden_;
  int64_t version_;
  int64_t snapshot_cnt_;
  ObServerSnapshotMeta snapshots_[MAX_SNAPSHOT_NUM];
  // Persisted LS catalog state for the current server runtime.
  int64_t auto_inc_ls_epoch_;
  int64_t ls_cnt_;
  ObLSItem ls_item_arr_[MAX_LS_COUNT];
};

#define IS_EMPTY_BLOCK_LIST(entry_block) (entry_block == oceanbase::storage::ObServerSuperBlock::EMPTY_LIST_ENTRY_BLOCK)
// Due to the design of slog, the log_id_'s initial value must be 1
#define SET_FIRST_VALID_SLOG_CURSOR(cursor) (set_cursor(cursor, 1/*file_id*/, 1/*log_id*/, 0/*offset*/))

}  // end namespace storage
}  // end namespace oceanbase

#endif  // OB_STORAGE_SUPER_BLOCK_STRUCT_H_
