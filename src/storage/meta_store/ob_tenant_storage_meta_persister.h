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
#ifndef OCEANBASE_STORAGE_META_STORE_OB_TENANT_STORAGE_META_PERSISTER_H_
#define OCEANBASE_STORAGE_META_STORE_OB_TENANT_STORAGE_META_PERSISTER_H_

#include "share/ob_unit_getter.h"
#include "storage/slog/ob_storage_log.h"
#include "lib/hash/ob_hashmap.h"

namespace oceanbase
{
namespace storage
{
class ObLSMeta;
class ObStorageLogger;
class ObTenantMonotonicIncSeqs;
class ObTenantCheckpointSlogHandler;

class ObTenantStorageMetaPersister
{
public:
  ObTenantStorageMetaPersister()
    : is_inited_(false),
      slogger_() {}
  ObTenantStorageMetaPersister(const ObTenantStorageMetaPersister &) = delete;
  ObTenantStorageMetaPersister &operator=(const ObTenantStorageMetaPersister &) = delete;
      
  int init(ObStorageLogger &slogger,
           ObTenantCheckpointSlogHandler &ckpt_slog_handler);
  void destroy();

  int prepare_create_ls(const ObLSMeta &meta, int64_t &ls_epoch);
  int commit_create_ls(const share::ObLSID &ls_id, const int64_t ls_epoch);
  int abort_create_ls(const share::ObLSID &ls_id, const int64_t ls_epoch);
  int delete_ls(const share::ObLSID &ls_id, const int64_t ls_epoch);
  int update_ls_meta(const int64_t ls_epoch, const ObLSMeta &ls_meta);
  int update_tenant_preallocated_seqs(const ObTenantMonotonicIncSeqs &preallocated_seqs);
  int batch_update_tablet(const ObIArray<ObUpdateTabletLog> &slog_arr);
  int batch_update_tablet(const ObIArray<ObUpdateTabletLog> &slog_arr, ObIArray<ObStorageLogParam> &param_arr);
  int update_tablet(
    const share::ObLSID &ls_id, const int64_t ls_epoch,
    const common::ObTabletID &tablet_id, const ObMetaDiskAddr &disk_addr);
  int write_active_tablet_array(ObLS *ls);
  
  int write_empty_shell_tablet(ObTablet *tablet, ObMetaDiskAddr &tablet_addr);
  int remove_tablet(
      const share::ObLSID &ls_id, const int64_t ls_epoch,
      const ObTabletHandle &tablet_handle);
  int remove_tablets(
      const share::ObLSID &ls_id, const int64_t ls_epoch,
      const ObIArray<common::ObTabletID> &tablet_id_arr, const ObIArray<ObMetaDiskAddr> &tablet_addr_arr);

  int get_items_from_pending_free_tablet_array( 
      const ObLSID &ls_id, 
      const int64_t ls_epoch,
      ObIArray<ObPendingFreeTabletItem> &items);
  int delete_items_from_pending_free_tablet_array(
      const ObLSID &ls_id, 
      const int64_t ls_epoch, 
      const ObIArray<ObPendingFreeTabletItem> &items);
private:
  int write_prepare_create_ls_slog_(const ObLSMeta &ls_meta);
  int write_commit_create_ls_slog_(const share::ObLSID &ls_id);
  int write_abort_create_ls_slog_(const share::ObLSID &ls_id);
  int write_delete_ls_slog_(const share::ObLSID &ls_id);
  int write_update_ls_meta_slog_(const ObLSMeta &ls_meta);
  int write_update_tablet_slog_(
      const share::ObLSID &ls_id, const common::ObTabletID &tablet_id, const ObMetaDiskAddr &tablet_addr);
  int write_remove_tablet_slog_(const share::ObLSID &ls_id, const common::ObTabletID &tablet_id);
  int write_remove_tablets_slog_(
      const ObLSID &ls_id, const common::ObIArray<ObTabletID> &tablet_ids);
  int safe_batch_write_remove_tablets_slog_(
      const ObLSID &ls_id, const common::ObIArray<ObTabletID> &tablet_ids);

private:
  struct PendingFreeTabletArrayKey
  {
    PendingFreeTabletArrayKey() : ls_id_(), ls_epoch_(0) {}
    PendingFreeTabletArrayKey(const share::ObLSID &ls_id, const int64_t ls_epoch)
      : ls_id_(ls_id), ls_epoch_(ls_epoch) {}
    uint64_t hash() const
    {
      return ls_id_.hash() ^ ls_epoch_;
    }

    int hash(uint64_t &hash_val) const
    {
      hash_val = hash();
      return OB_SUCCESS;
    }
    bool operator ==(const PendingFreeTabletArrayKey &other) const
    {
      return other.ls_id_ == ls_id_ && other.ls_epoch_ == ls_epoch_;
    }
    bool operator !=(const PendingFreeTabletArrayKey &other) const { return !(other == *this); }
    bool operator <(const PendingFreeTabletArrayKey &other) const
    {
      bool bool_ret = false;
      if (ls_id_ < other.ls_id_) {
        bool_ret = true;
      } else if (ls_id_ == other.ls_id_) {
        bool_ret = (ls_epoch_ < other.ls_epoch_);
      }
      return bool_ret;
    }

    TO_STRING_KV(K_(ls_id), K_(ls_epoch));

    share::ObLSID ls_id_;
    int64_t ls_epoch_;
  };

  struct PendingFreeTabletArrayInfo
  {
    PendingFreeTabletArrayInfo()
      : lock_(), pending_free_tablet_arr_() {}

    lib::ObMutex lock_;
    ObLSPendingFreeTabletArray pending_free_tablet_arr_;
  };

  typedef common::hash::ObHashMap<
      PendingFreeTabletArrayKey,
      PendingFreeTabletArrayInfo*,
      common::hash::NoPthreadDefendMode> PendingFreeTabletArrayMap;

private:
  bool is_inited_;
  storage::ObStorageLogger *slogger_;
  common::ObConcurrentFIFOAllocator allocator_;
  lib::ObMutex super_block_lock_; // protect tenant super block
  ObTenantCheckpointSlogHandler *ckpt_slog_handler_;
  
  lib::ObMutex peding_free_map_lock_; // pending_free_tablet_arr_map_

  PendingFreeTabletArrayMap pending_free_tablet_arr_map_;
  
};

} // namespace storage
} // namespace oceanbase
#endif // OCEANBASE_STORAGE_BLOCKSSTALE_OB_STORAGE_META_PERSISTER_H_
