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

#ifndef _STORAGE_BACKUP_OTHER_BLOCKS_MGR_H_ 
#define _STORAGE_BACKUP_OTHER_BLOCKS_MGR_H_

#include "storage/blocksstable/index_block/ob_sstable_meta_info.h"
#include "storage/backup/ob_backup_linked_item.h"
#include "storage/blocksstable/ob_sstable.h"
#include "storage/blocksstable/index_block/ob_sstable_meta_info.h"
#include "storage/backup/ob_backup_tmp_file.h"
#include "storage/backup/ob_backup_linked_block_writer.h"

namespace oceanbase
{
namespace backup
{

class ObBackupOtherBlockIdIterator final
{
public:
  ObBackupOtherBlockIdIterator();
  ~ObBackupOtherBlockIdIterator();
  int init(const common::ObTabletID &tablet_id, const blocksstable::ObSSTable &sstable);
  int get_next_id(blocksstable::MacroBlockId &macro_id);

private:
  int get_sstable_meta_handle_();
  int prepare_macro_id_iterator_();

private:
  bool is_inited_;
  common::ObTabletID tablet_id_;
  const blocksstable::ObSSTable *sstable_ptr_;
  blocksstable::ObSSTableMetaHandle sst_meta_hdl_;
  blocksstable::ObMacroIdIterator id_iterator_;
  DISALLOW_COPY_AND_ASSIGN(ObBackupOtherBlockIdIterator);
};

class ObLSBackupCtx;
class ObBackupOtherBlocksMgr final
{
public:
  ObBackupOtherBlocksMgr();
  ~ObBackupOtherBlocksMgr();
  int init(const uint64_t tenant_id, const common::ObTabletID &tablet_id,
      const storage::ObITable::TableKey &table_key, const blocksstable::ObSSTable &sstable);
  int wait(ObLSBackupCtx *ls_backup_ctx);
  bool is_finished() const { return list_.count() == total_other_block_count_; }
  int add_item(const ObBackupLinkedItem &link_item);
  int get_next_item(ObBackupLinkedItem &link_item);

private:
  int get_total_other_block_count_(const common::ObTabletID &tablet_id,
      const blocksstable::ObSSTable &sstable, int64_t &total_count);
  int64_t get_list_count_() const;
  
private:
  bool is_inited_;
  mutable lib::ObMutex mutex_;
  uint64_t tenant_id_;
  common::ObTabletID tablet_id_;
  storage::ObITable::TableKey table_key_;
  int64_t total_other_block_count_;
  common::ObArray<ObBackupLinkedItem> list_;
  int64_t idx_;
  DISALLOW_COPY_AND_ASSIGN(ObBackupOtherBlocksMgr);
};

}
}

#endif
