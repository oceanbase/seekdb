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

#ifndef OCEANBASE_STORAGE_LS_RESTORE_HELPER_
#define OCEANBASE_STORAGE_LS_RESTORE_HELPER_

#include "share/ob_task_define.h"
#include "ob_storage_restore_struct.h"
#include "standby/restore/ob_standby_restore_rpc.h"
#include "ob_standby_restore_storage_struct.h"
#include "lib/allocator/page_arena.h"
#include "storage/tablet/ob_tablet_meta.h"            // ObMigrationTabletParam
#include "storage/tablet/ob_tablet_create_sstable_param.h" // blocksstable::ObMigrationSSTableParam
#include "ob_standby_restore_reader.h"
#include "standby/restore/ob_restore_helper_ctx.h"

namespace oceanbase
{
namespace restore
{
class ObStandbyRestoreHelper
{
public:
  ObStandbyRestoreHelper();
  ~ObStandbyRestoreHelper();
  bool is_valid() const;
  void destroy();
  int copy_for_task(common::ObIAllocator &allocator, ObStandbyRestoreHelper *&helper) const;
  int init(
      const common::ObAddr &src,
      const share::ObTaskId &task_id,
      common::ObInOutBandwidthThrottle *bandwidth_throttle);
  int check_restore_precondition();
  int init_for_ls_view();
  int fetch_next_tablet_info(obcall::ObCopyTabletInfo &tablet_info);
  int fetch_ls_meta(ObLSMeta &ls_meta, share::SCN &physical_checkpoint_scn);
  int init_for_build_tablets_sstable_info(const common::ObIArray<ObTabletHandle> &tablet_handle_array);
  int fetch_next_tablet_sstable_header(obcall::ObCopyTabletSSTableHeader &copy_header);
  int fetch_next_sstable_meta(obcall::ObCopyTabletSSTableInfo &sstable_info);
  // Build sstable macro range info for copy chain. The helper is responsible for iterating
  // and returning range info for each sstable key in the input list.
  int init_for_sstable_macro_range(const common::ObIArray<storage::ObITable::TableKey> &copy_table_key_array);
  int fetch_next_sstable_macro_range_info(storage::ObCopySSTableMacroRangeInfo &sstable_macro_range_info);
  // Macro block copy iteration for a single sstable range.
  int init_for_macro_block_copy(
      const storage::ObITable::TableKey &copy_table_key,
      const storage::ObCopyMacroRangeInfo &macro_range_info,
      const share::SCN &backfill_tx_scn,
      const int64_t data_version);
  int fetch_next_macro_block(storage::ObICopyMacroBlockReader::CopyMacroBlockReadData &read_data);
  int init_for_fetch_tablet_meta(const common::ObIArray<common::ObTabletID> &tablet_id_array);
  int fetch_tablet_meta(obcall::ObCopyTabletInfo &tablet_info);
  TO_STRING_KV(K_(is_inited), K_(task_id), K_(src));
private:
  int create_ctx_(const ObRestoreHelperCtxType ctx_type);
  int get_ls_view_rpc_timeout_(int64_t &rpc_timeout_us);
  int build_copy_tablet_sstable_info_arg_for_restore_(
      const ObTabletHandle &tablet_handle,
      obcall::ObCopyTabletSSTableInfoArg &arg);
  int get_major_sstable_max_snapshot_for_restore_(
      const ObSSTableArray &major_sstable_array,
      int64_t &max_snapshot_version);
  int get_need_copy_ddl_sstable_range_for_restore_(
      const ObTablet *tablet,
      const ObSSTableArray &ddl_sstable_array,
      share::ObScnRange &need_copy_scn_range);
  int fetch_macro_block_header_(
      ObRestoreHelperMacroBlockCtx *macro_block_ctx,
      obcall::ObCopyMacroBlockHeader &header);
  int fetch_macro_block_data_(
      ObRestoreHelperMacroBlockCtx *macro_block_ctx,
      const obcall::ObCopyMacroBlockHeader &header,
      blocksstable::ObBufferReader &data_reader);
private:
  bool is_inited_;
  share::ObTaskId task_id_;
  common::ObAddr src_;
  common::ObInOutBandwidthThrottle *bandwidth_throttle_;
  ObIRestoreHelperCtx *ctx_;
  common::ObArenaAllocator ctx_allocator_;
  DISALLOW_COPY_AND_ASSIGN(ObStandbyRestoreHelper);
};

} // namespace restore
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_LS_RESTORE_HELPER_
