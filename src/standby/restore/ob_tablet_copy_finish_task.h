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

#ifndef OCEABASE_STORAGE_TABLET_COPY_FINISH_TASK_
#define OCEABASE_STORAGE_TABLET_COPY_FINISH_TASK_

#include "lib/thread/ob_dynamic_thread_pool.h"
#include "standby/restore/ob_standby_restore_rpc.h"
#include "storage/blocksstable/ob_block_sstable_struct.h"
#include "storage/blocksstable/ob_macro_block_meta_mgr.h"
#include "ob_standby_restore_storage_struct.h"
#include "ob_standby_restore_macro_block_writer.h"
#include "ob_standby_restore_reader.h"
#include "storage/blocksstable/ob_sstable.h"
#include "ob_storage_restore_struct.h"

namespace oceanbase
{
namespace standby
{
struct StandbyConfig;
}
namespace storage
{

struct ObTabletCopyFinishTaskParam final
{
  ObTabletCopyFinishTaskParam();
  ~ObTabletCopyFinishTaskParam() = default;
  bool is_valid() const;

  TO_STRING_KV(KP_(ls), K_(tablet_id), K_(restore_action), K_(is_leader_restore),
      KPC_(src_tablet_meta), KP_(copy_tablet_ctx), K_(is_only_replace_major));

  ObLS *ls_;
  common::ObTabletID tablet_id_;
  ObTabletRestoreAction::ACTION restore_action_;
  bool is_leader_restore_;
  const ObMigrationTabletParam *src_tablet_meta_;
  ObICopyTabletCtx *copy_tablet_ctx_;
  bool is_only_replace_major_;
  const standby::StandbyConfig *config_;
};

struct ObICopyTabletCtx;
struct ObPhysicalCopyCtx;
class ObTabletCopyFinishTask final
{
public:
  ObTabletCopyFinishTask();
  virtual ~ObTabletCopyFinishTask();
  int init(
      const ObTabletCopyFinishTaskParam &param);
  int process();
  VIRTUAL_TO_STRING_KV(K("ObTabletCopyFinishTask"), KP(this));
  int add_sstable(ObTableHandleV2 &table_handle);
  int add_sstable(ObTableHandleV2 &table_handle, const int64_t last_meta_macro_seq);
  int get_sstable(
      const ObITable::TableKey &table_key,
      ObTableHandleV2 &table_handle);
  common::ObArenaAllocator &get_allocator() { return arena_allocator_; }
  int set_tablet_status(const ObCopyTabletStatus::STATUS &status);
  int get_tablet_status(ObCopyTabletStatus::STATUS &status);
  int get_restore_action(ObTabletRestoreAction::ACTION &restore_action);

  const ObMigrationTabletParam *get_src_tablet_meta() const { return param_.src_tablet_meta_; }
private:
  int create_new_table_store_with_major_();
  int create_new_table_store_with_minor_();
  int check_finish_copy_tablet_data_valid_();
  int get_tables_handle_ptr_(
      const ObITable::TableKey &table_key,
      ObTablesHandleArray *&table_handle_ptr);
  int check_major_valid_();
  int check_tablet_valid_();
  int deal_with_major_sstables_();
  int check_restore_major_valid_(
      const ObTablesHandleArray &major_tables_handle);
  int get_mds_sstable_max_end_scn_(share::SCN &max_escn);
  int check_log_replay_to_mds_sstable_end_scn_();
private:
  bool is_inited_;
  common::SpinRWLock lock_;
  common::ObArenaAllocator arena_allocator_;
  ObTablesHandleArray minor_tables_handle_;
  ObTablesHandleArray ddl_tables_handle_;
  ObTablesHandleArray major_tables_handle_;
  ObTablesHandleArray mds_tables_handle_;
  common::ObArray<std::pair<ObITable::TableKey, int64_t>> last_meta_seq_array_;
  ObTabletCopyFinishTaskParam param_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletCopyFinishTask);
};

}
}

#endif
