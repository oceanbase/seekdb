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

#ifndef OB_STORAGE_CKPT_TENANT_STORAGE_CHECKPOINT_WRITER_H_
#define OB_STORAGE_CKPT_TENANT_STORAGE_CHECKPOINT_WRITER_H_

#include "storage/slog_ckpt/ob_linked_macro_block_writer.h"
#include "storage/ob_super_block_struct.h"
#include "storage/meta_mem/ob_tablet_map_key.h"
#include "storage/meta_mem/ob_tenant_meta_mem_mgr.h"
#include "storage/slog/ob_storage_log.h"

namespace oceanbase
{
namespace share
{
class ObLSID;
}

namespace storage
{
struct ObLSCkptMember;

enum class ObTenantStorageMetaType
{
  CKPT = 0,
  SNAPSHOT = 1,
  CLONE = 2,
  INVALID_TYPE
};

class ObTenantCheckpointSlogHandler;

class ObTenantStorageCheckpointWriter final
{
public:
  static bool ignore_ret(int ret);
public:
  ObTenantStorageCheckpointWriter();
  ~ObTenantStorageCheckpointWriter() = default;
  ObTenantStorageCheckpointWriter(const ObTenantStorageCheckpointWriter &) = delete;
  ObTenantStorageCheckpointWriter &operator=(const ObTenantStorageCheckpointWriter &) = delete;
  int init(const ObTenantStorageMetaType meta_type,
           ObTenantCheckpointSlogHandler *ckpt_slog_handler);

  // record meta for ckpt
  int record_meta(blocksstable::MacroBlockId &ls_meta_entry);

  // record meta for snapshot
  int record_single_ls_meta(
      const blocksstable::MacroBlockId &orig_ls_meta_entry,
      const share::ObLSID &ls_id,
      ObIArray<blocksstable::MacroBlockId> &orig_linked_block_list,
      blocksstable::MacroBlockId &ls_meta_entry,
      share::SCN &clog_max_scn);
  int delete_single_ls_meta(
      const blocksstable::MacroBlockId &orig_ls_meta_entry,
      const share::ObLSID &ls_id,
      ObIArray<blocksstable::MacroBlockId> &orig_linked_block_list,
      blocksstable::MacroBlockId &ls_meta_entry);

  int get_ls_block_list(common::ObIArray<blocksstable::MacroBlockId> *&block_list);
  int get_tablet_block_list(common::ObIArray<blocksstable::MacroBlockId> *&block_list);
  int batch_compare_and_swap_tablet();
  int rollback();

private:
  struct TabletItemAddrInfo
  {
    ObTabletMapKey tablet_key_;
    ObMetaDiskAddr old_addr_;
    ObMetaDiskAddr new_addr_;
    ObTabletPoolType tablet_pool_type_;
    bool need_rollback_;

    TabletItemAddrInfo()
      : tablet_key_(), old_addr_(), new_addr_(),
        tablet_pool_type_(ObTabletPoolType::TP_MAX),
        need_rollback_(true)
    {
    }

    TO_STRING_KV(K_(tablet_key), K_(old_addr), K_(new_addr), K_(tablet_pool_type), K_(need_rollback));
  };

  int copy_ls_meta_for_deleting(const ObMetaDiskAddr &addr, const char *buf, const int64_t buf_len,
                                const share::ObLSID &ls_id);
  int copy_ls_meta_for_creating(const ObMetaDiskAddr &addr, const char *buf, const int64_t buf_len);
  int write_item(const ObLSCkptMember &ls_ckpt_member);
  int close(blocksstable::MacroBlockId &ls_meta_entry);
  int do_record_ls_meta(ObLS &ls, share::SCN &clog_max_scn);
  int get_tablet_with_addr(
      const TabletItemAddrInfo &addr_info,
      ObTabletHandle &tablet_handle);
  int record_ls_meta(blocksstable::MacroBlockId &ls_entry_block);
  int record_tablet_meta(ObLS &ls, blocksstable::MacroBlockId &tablet_meta_entry, share::SCN &clog_max_scn);
  int persist_and_copy_tablet(
      const ObTabletMapKey &tablet_key,
      const ObMetaDiskAddr &old_addr,
      char (&slog_buf)[sizeof(ObUpdateTabletLog)]);
  int copy_tablet(
      const ObTabletMapKey &tablet_key,
      char (&slog_buf)[sizeof(ObUpdateTabletLog)],
      share::SCN &clog_max_scn);
  static int handle_old_version_tablet_for_compat(
      common::ObArenaAllocator &allocator,
      const ObTabletMapKey &tablet_key,
      const ObTablet &old_tablet,
      ObTabletHandle &new_tablet_handle);

private:
  bool is_inited_;
  ObTenantStorageMetaType meta_type_;
  ObTenantCheckpointSlogHandler *ckpt_slog_handler_;
  common::ObArray<TabletItemAddrInfo> tablet_item_addr_info_arr_;
  ObLinkedMacroBlockItemWriter ls_item_writer_;
  ObLinkedMacroBlockItemWriter tablet_item_writer_;
};

}  // end namespace storage
}  // namespace oceanbase

#endif  // OB_STORAGE_CKPT_TENANT_STORAGE_CHECKPOINT_WRITER_H_
