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

#ifndef STORAGE_LOG_STREAM_BACKUP_RESTORE_UTIL_H_
#define STORAGE_LOG_STREAM_BACKUP_RESTORE_UTIL_H_

#include "storage/tablet/ob_tablet_meta.h"
#include "common/ob_tablet_id.h"
#include "common/storage/ob_device_common.h"
#include "lib/string/ob_string.h"
#include "lib/allocator/ob_allocator.h"
#include "storage/blocksstable/ob_sstable.h"
#include "storage/backup/ob_backup_data_struct.h"
#include "share/backup/ob_backup_path.h"
#include "storage/backup/ob_backup_linked_item.h"
#include "storage/backup/ob_backup_linked_block_reader.h"

namespace oceanbase {
namespace backup {

class ObBackupMetaKVCache;

class ObLSBackupRestoreUtil {
public:
  static int read_tablet_meta(const common::ObString &path, const share::ObBackupStorageInfo *storage_info, const common::ObStorageIdMod &mod,
      const ObBackupMetaIndex &meta_index, ObBackupTabletMeta &tablet_meta);
  static int read_sstable_metas(const common::ObString &path, const share::ObBackupStorageInfo *storage_info, const common::ObStorageIdMod &mod,
      const ObBackupMetaIndex &meta_index, ObBackupMetaKVCache *kv_cache, common::ObIArray<ObBackupSSTableMeta> &sstable_metas);
  static int read_macro_block_id_mapping_metas(const common::ObString &path, const share::ObBackupStorageInfo *storage_info, const common::ObStorageIdMod &mod, 
      const ObBackupMetaIndex &meta_index, ObBackupMacroBlockIDMappingsMeta &id_mappings_meta);
  static int read_macro_block_data(const common::ObString &path, const share::ObBackupStorageInfo *storage_info, const common::ObStorageIdMod &mod,
      const ObBackupMacroBlockIndex &macro_index, const int64_t align_size, blocksstable::ObBufferReader &read_buffer, 
      blocksstable::ObBufferReader &data_buffer);
  static int read_macro_block_data_with_retry(const common::ObString &path, const share::ObBackupStorageInfo *storage_info, const common::ObStorageIdMod &mod,
      const ObBackupMacroBlockIndex &macro_index, const int64_t align_size, blocksstable::ObBufferReader &read_buffer, 
      blocksstable::ObBufferReader &data_buffer); // max retry count is GCONF._restore_io_max_retry_count
  static int read_ddl_sstable_other_block_id_list_in_ss_mode(
      const share::ObBackupDest &backup_set_dest, const common::ObString &path, const share::ObBackupStorageInfo *storage_info, const ObStorageIdMod &mod,
      const ObBackupMetaIndex &meta_index, const storage::ObITable::TableKey &table_key, common::ObIArray<ObBackupLinkedItem> &link_item);
  static int read_ddl_sstable_other_block_id_list_in_ss_mode_with_batch(
      const share::ObBackupDest &backup_set_dest, const common::ObString &path, const share::ObBackupStorageInfo *storage_info, const ObStorageIdMod &mod,
      const ObBackupMetaIndex &meta_index, const storage::ObITable::TableKey &table_key, const blocksstable::MacroBlockId &start_macro_id,
      const int64_t count, common::ObIArray<ObBackupLinkedItem> &link_item);
  static int pread_file(const ObString &path, const share::ObBackupStorageInfo *storage_info, const common::ObStorageIdMod &mod,
      const int64_t offset, const int64_t read_size, char *buf);

private:
  static int prepare_ddl_sstable_other_block_id_reader_(
      const share::ObBackupDest &backup_set_dest, const common::ObString &path, const share::ObBackupStorageInfo *storage_info, const ObStorageIdMod &mod,
      const ObBackupMetaIndex &meta_index, const storage::ObITable::TableKey &table_key, ObBackupLinkedBlockItemReader &reader, bool &has_any_block);
  static const int64_t READ_MACRO_BLOCK_RETRY_INTERVAL = 1 * 1000 * 1000LL; // 1s      
};

}  // namespace backup
}  // namespace oceanbase

#endif
