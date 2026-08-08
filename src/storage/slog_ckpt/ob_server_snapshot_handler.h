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

#ifndef OCEANBASE_STORAGE_SLOG_CKPT_OB_SERVER_SNAPSHOT_HANDLER_H_
#define OCEANBASE_STORAGE_SLOG_CKPT_OB_SERVER_SNAPSHOT_HANDLER_H_

#include "storage/slog_ckpt/ob_linked_macro_block_struct.h"
#include "storage/meta_mem/ob_tablet_map_key.h"
#include "storage/ob_super_block_struct.h"
#include "storage/ls/ob_ls_meta.h"
#include "storage/slog_ckpt/ob_local_storage_checkpoint_reader.h"
#include "storage/slog_ckpt/ob_local_storage_checkpoint_writer.h"
#include "storage/slog/ob_storage_log_struct.h"
#include "storage/slog/ob_storage_log.h"

namespace oceanbase
{
namespace share
{
class ObServerSnapshotID;
}
namespace storage
{
class ObLocalStorageCheckpointWriter;
class ObStartupAccelTaskHandler;
class ObServerSnapshotHandler
{
public:
  static const int64_t MAX_SLOG_BATCH_NUM = 30000; // almost 2MB
public:
  ObServerSnapshotHandler() {}
  ~ObServerSnapshotHandler() = default;
  DISALLOW_COPY_AND_ASSIGN(ObServerSnapshotHandler);

  // Create a server snapshot.
  static int create_server_snapshot(const ObServerSnapshotID &snapshot_id);
  // delete snapshot
  static int delete_server_snapshot(const ObServerSnapshotID &snapshot_id);
  static int create_all_tablet(ObStartupAccelTaskHandler* startup_accel_handler,
                               const blocksstable::MacroBlockId &tablet_meta_entry);
  static int get_ls_meta_entry(const ObServerSnapshotID &snapshot_id, blocksstable::MacroBlockId &ls_meta_entry);

  // recover snapshot for restart
  static int get_all_server_snapshots(ObIArray<ObServerSnapshotID> &snapshot_ids);
  // increase ref cnt for linked blocks
  static int inc_linked_block_ref(const ObIArray<blocksstable::MacroBlockId> &meta_block_list, bool &inc_success);

private:
  static int inc_all_linked_block_ref(
      ObLocalStorageCheckpointWriter &local_storage_writer,
      bool &inc_ls_blocks_ref_succ,
      bool &inc_tablet_blocks_ref_succ);
  static void rollback_ref_cnt(
      const bool inc_ls_blocks_ref_succ,
      const bool inc_tablet_blocks_ref_succ,
      ObLocalStorageCheckpointWriter &local_storage_writer);
  static void dec_meta_block_ref(const ObIArray<blocksstable::MacroBlockId> &meta_block_list);
  static int inner_delete_tablet_by_addrs(const ObIArray<ObMetaDiskAddr> &deleted_tablet_addrs);
  static int inner_delete_ls_snapshot(const blocksstable::MacroBlockId& tablet_meta_entry,
                                      ObIArray<ObMetaDiskAddr> &deleted_tablet_addrs,
                                      ObIArray<MacroBlockId> &tablet_meta_block_list);
  static int delete_ls_snapshot(
      const ObMetaDiskAddr &addr,
      const char *buf,
      const int64_t buf_len,
      ObIArray<ObMetaDiskAddr> &deleted_tablet_addrs,
      ObIArray<blocksstable::MacroBlockId> &tablet_meta_block_list);
  static int delete_tablet_snapshot(
      const ObMetaDiskAddr &addr,
      const char *buf,
      const int64_t buf_len,
      ObIArray<ObMetaDiskAddr> &deleted_tablet_addrs);
  static int batch_write_slog(
      const ObMetaDiskAddr &addr,
      const char *buf,
      const int64_t buf_len,
      ObIArray<ObUpdateTabletLog> &slog_array);
  static int do_write_slog(ObIArray<ObUpdateTabletLog> &slog_arr);
};
}
}

#endif // OCEANBASE_STORAGE_SLOG_CKPT_OB_SERVER_SNAPSHOT_HANDLER_H_
