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

#ifndef STORAGE_LOG_STREAM_BACKUP_FACTORY_H_
#define STORAGE_LOG_STREAM_BACKUP_FACTORY_H_

#include "storage/backup/ob_backup_index_merger.h"
#include "storage/backup/ob_backup_iterator.h"
#include "storage/backup/ob_backup_reader.h"
#include "storage/backup/ob_backup_utils.h"
#include "storage/backup/ob_backup_sstable_sec_meta_iterator.h"
#include "storage/backup/ob_backup_device_wrapper.h"

namespace oceanbase {
namespace backup {

class ObLSBackupFactory {
public:
  static ObILSTabletIdReader *get_ls_tablet_id_reader(const ObLSTabletIdReaderType &type, const uint64_t tenant_id);
  static ObITabletLogicMacroIdReader *get_tablet_logic_macro_id_reader(const ObTabletLogicIdReaderType &type, const uint64_t tenant_id);
  static ObIMacroBlockBackupReader *get_macro_block_backup_reader(const ObMacroBlockReaderType &type, const uint64_t tenant_id);
  static ObMultiMacroBlockBackupReader *get_multi_macro_block_backup_reader(const uint64_t tenant_id);
  static ObITabletMetaBackupReader *get_tablet_meta_backup_reader(const ObTabletMetaReaderType &type, const uint64_t tenant_id);
  static ObIBackupIndexIterator *get_backup_index_iterator(const ObBackupIndexIteratorType &type, const uint64_t tenant_id);
  static ObIBackupTabletProvider *get_backup_tablet_provider(const ObBackupTabletProviderType &type, const uint64_t tenant_id);
  static ObIBackupMacroBlockIndexFuser *get_backup_macro_index_fuser(const ObBackupMacroIndexFuserType &type, const uint64_t tenant_id);
  static ObBackupTabletCtx *get_backup_tablet_ctx(const uint64_t tenant_id);
  static ObBackupSSTableSecMetaIterator *get_backup_sstable_sec_meta_iterator(const uint64_t tenant_id);
  static ObBackupWrapperIODevice *get_backup_wrapper_io_device(const uint64_t tenant_id);
  static ObExternBackupTabletMetaIterator *get_extern_backup_tablet_meta_iterator(const uint64_t tenant_id);
  static ObBackupTabletMetaIndexIterator *get_backup_tablet_meta_index_iterator(const uint64_t tenant_id);

  static void free(ObILSTabletIdReader *&reader);
  static void free(ObITabletLogicMacroIdReader *&reader);
  static void free(ObIMacroBlockBackupReader *&reader);
  static void free(ObMultiMacroBlockBackupReader *&reader);
  static void free(ObITabletMetaBackupReader *&reader);
  static void free(ObBackupMetaIndexIterator *&iterator);
  static void free(ObIMacroBlockIndexIterator *&iterator);
  static void free(ObBackupMacroBlockIndexIterator *&iterator);
  static void free(ObBackupMacroRangeIndexIterator *&iterator);
  static void free(ObIBackupTabletProvider *&provider);
  static void free(ObBackupTabletProvider *&provider);
  static void free(ObIBackupMacroBlockIndexFuser *&fuser);
  static void free(ObBackupTabletCtx *&ctx);
  static void free(ObBackupSSTableSecMetaIterator *&iterator);
  static void free(ObBackupWrapperIODevice *device);
  static void free(ObIBackupTabletMetaIterator *device);
  static void free(ObBackupUnorderedMacroBlockIndexIterator *&iterator);
  static void free(ObBackupOrderedMacroBlockIndexIterator *&iterator);
private:
  template <class IT>
  static void component_free(IT *component)
  {
    if (OB_LIKELY(NULL != component)) {
      op_free(component);
      component = NULL;
    }
  }
};

}  // namespace backup
}  // namespace oceanbase

#endif
