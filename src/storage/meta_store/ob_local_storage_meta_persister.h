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
#ifndef OCEANBASE_STORAGE_META_STORE_OB_LOCAL_STORAGE_META_PERSISTER_H_
#define OCEANBASE_STORAGE_META_STORE_OB_LOCAL_STORAGE_META_PERSISTER_H_

#include "storage/slog/ob_storage_log.h"
#include "lib/hash/ob_hashmap.h"

namespace oceanbase
{
namespace storage
{
class ObLSMeta;
class ObStorageLogger;
class ObTabletHandle;
class ObLocalStorageCheckpointSlogHandler;

class ObLocalStorageMetaPersister
{
public:
  ObLocalStorageMetaPersister()
    : is_inited_(false),
      slogger_() {}
  ObLocalStorageMetaPersister(const ObLocalStorageMetaPersister &) = delete;
  ObLocalStorageMetaPersister &operator=(const ObLocalStorageMetaPersister &) = delete;

  int init(ObStorageLogger &slogger,
           ObLocalStorageCheckpointSlogHandler &ckpt_slog_handler);
  void destroy();

  int update_ls_meta(const int64_t ls_epoch, const ObLSMeta &ls_meta);
  int batch_update_tablet(const ObIArray<ObUpdateTabletLog> &slog_arr);
  int batch_update_tablet(const ObIArray<ObUpdateTabletLog> &slog_arr, ObIArray<ObStorageLogParam> &param_arr);
  int update_tablet(
    const common::ObTabletID &tablet_id, const ObMetaDiskAddr &disk_addr);
  int write_empty_shell_tablet(ObTablet *tablet, ObMetaDiskAddr &tablet_addr);
  int remove_tablet(const ObTabletHandle &tablet_handle);
  int remove_tablets(const ObIArray<common::ObTabletID> &tablet_id_arr);

private:
  int write_update_ls_meta_slog_(const ObLSMeta &ls_meta);
  int write_update_tablet_slog_(
      const common::ObTabletID &tablet_id, const ObMetaDiskAddr &tablet_addr);
  int write_remove_tablet_slog_(const common::ObTabletID &tablet_id);
  int write_remove_tablets_slog_(const common::ObIArray<ObTabletID> &tablet_ids);
  int safe_batch_write_remove_tablets_slog_(const common::ObIArray<ObTabletID> &tablet_ids);

private:
  bool is_inited_;
  storage::ObStorageLogger *slogger_;
  ObLocalStorageCheckpointSlogHandler *ckpt_slog_handler_;

};

} // namespace storage
} // namespace oceanbase
#endif // OCEANBASE_STORAGE_META_STORE_OB_LOCAL_STORAGE_META_PERSISTER_H_
