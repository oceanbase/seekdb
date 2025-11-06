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

#ifndef _OCEANBASE_BACKUP_OB_BACKUP_LINKED_BLOCK_ITEM_H_
#define _OCEANBASE_BACKUP_OB_BACKUP_LINKED_BLOCK_ITEM_H_

#include "common/ob_tablet_id.h"
#include "storage/ob_i_table.h"
#include "storage/blocksstable/ob_logic_macro_id.h"
#include "storage/blocksstable/ob_macro_block_id.h"
#include "storage/backup/ob_backup_data_struct.h"
#include "common/storage/ob_io_device.h"
#include "storage/backup/ob_backup_file_writer_ctx.h"
#include "lib/allocator/page_arena.h"
#include "storage/ob_parallel_external_sort.h"

namespace oceanbase
{
namespace backup
{

struct ObBackupLinkedBlockHeader
{
  OB_UNIS_VERSION(1);
public:
  static const int64_t VERSION = 0;
  static const int64_t MAGIC = 1;
public:
  ObBackupLinkedBlockHeader();
  ~ObBackupLinkedBlockHeader();
  void set_previous_block_id(const ObBackupLinkedBlockAddr &physical_id) { prev_block_addr_ = physical_id; }

  TO_STRING_KV(K_(version), K_(magic), K_(item_count), K_(tablet_id), K_(table_key), K_(prev_block_addr), K_(has_prev));
  int32_t version_;
  int32_t magic_;
  int64_t item_count_;
  common::ObTabletID tablet_id_;
  storage::ObITable::TableKey table_key_;
  ObBackupLinkedBlockAddr prev_block_addr_;
  bool has_prev_;
};

struct ObBackupLinkedItem
{
  OB_UNIS_VERSION(1);
public:
  ObBackupLinkedItem();
  ~ObBackupLinkedItem();
  void reset();
  bool is_valid() const;
  bool operator==(const ObBackupLinkedItem &other) const;
  bool operator!=(const ObBackupLinkedItem &other) const;
  TO_STRING_KV(K_(macro_id), K_(backup_id));
  blocksstable::MacroBlockId macro_id_;
  ObBackupDeviceMacroBlockId backup_id_;
};

}
}

#endif
