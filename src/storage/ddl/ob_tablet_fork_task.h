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

#ifndef OCEANBASE_STORAGE_DDL_OB_TABLET_FORK_TASK_H_
#define OCEANBASE_STORAGE_DDL_OB_TABLET_FORK_TASK_H_

#define USING_LOG_PREFIX STORAGE

#include "share/ob_ddl_common.h"
#include "share/scheduler/ob_tenant_dag_scheduler.h"
#include "storage/access/ob_table_access_context.h"
#include "storage/access/ob_store_row_iterator.h"
#include "storage/access/ob_sstable_row_whole_scanner.h"
#include "storage/access/ob_table_access_param.h"
#include "storage/blocksstable/index_block/ob_index_block_builder.h"
#include "storage/ob_storage_schema.h"
#include "storage/tablet/ob_tablet_obj_load_helper.h"
#include "storage/tablet/ob_tablet_member_wrapper.h"
#include "lib/oblog/ob_log_module.h"
#include "storage/tx_storage/ob_ls_handle.h" // For ObLSHandle
#include "storage/tablet/ob_tablet_create_delete_helper.h"
#include "lib/lock/ob_mutex.h"
#include "storage/ddl/ob_tablet_rebuild_util.h"
#include "common/ob_tablet_id.h"
#include "storage/tablet/ob_tablet_table_store_iterator.h"

namespace oceanbase
{
namespace storage
{
class ObTableForkInfo;
struct ObMigrationTabletParam;

struct ObForkScanParam final
{
public:
  ObForkScanParam(
    const uint64_t table_id,
    const ObTabletHandle &tablet_handle,
    const ObDatumRange &query_range,
    const ObStorageSchema &storage_schema) :
    table_id_(table_id), tablet_handle_(tablet_handle),
    query_range_(&query_range), storage_schema_(&storage_schema)
  { }
  ~ObForkScanParam() = default;
  bool is_valid() const {
    return table_id_ > 0 && tablet_handle_.is_valid() && nullptr != query_range_
        && (nullptr != storage_schema_ && storage_schema_->is_valid());
  }
  TO_STRING_KV(K_(table_id), K_(tablet_handle), KPC_(query_range), KPC_(storage_schema));
public:
  uint64_t table_id_;
  const ObTabletHandle &tablet_handle_;
  const ObDatumRange *query_range_;
  const ObStorageSchema *storage_schema_;
};

class ObForkSnapshotRowScan : public ObIStoreRowIterator
{
public:
  ObForkSnapshotRowScan();
  virtual ~ObForkSnapshotRowScan();

  int init(
      const ObForkScanParam &param,
      ObSSTable &sstable,
      const share::ObLSID &ls_id,
      const int64_t fork_snapshot_version);

  virtual int get_next_row(const blocksstable::ObDatumRow *&tmp_row) override;

  const ObITableReadInfo *get_rowkey_read_info() const { return rowkey_read_info_; }
  storage::ObTxTableGuards &get_tx_table_guards() { return ctx_.mvcc_acc_ctx_.get_tx_table_guards(); }

  TO_STRING_KV(K_(is_inited), K_(fork_snapshot_version), K_(ls_id), K_(ctx), K_(access_ctx), KPC_(rowkey_read_info), K_(access_param));
private:
  int construct_access_param(const ObForkScanParam &param);
  int construct_access_ctx(
      const share::ObLSID &ls_id,
      const common::ObTabletID &tablet_id,
      const int64_t fork_snapshot_version);
  int build_rowkey_read_info(const ObForkScanParam &param);
private:
  bool is_inited_;
  ObSSTableRowWholeScanner *row_iter_;
  ObStoreCtx ctx_;
  ObTableAccessContext access_ctx_;
  ObRowkeyReadInfo *rowkey_read_info_;
  ObTableAccessParam access_param_;
  common::ObArenaAllocator allocator_;
  share::ObLSID ls_id_;
  int64_t fork_snapshot_version_;
};

using ObForkSSTableTaskKey = ObDdlSSTableTaskKey;

struct ObTabletForkParam : public share::ObIDagInitParam
{
  OB_UNIS_VERSION(1);
public:
  ObTabletForkParam();
  ObTabletForkParam(const ObTabletForkParam &other);
  ObTabletForkParam &operator=(const ObTabletForkParam &other);
  virtual ~ObTabletForkParam();
  void reset();
  bool is_valid() const;
  int init(const ObTabletForkParam &param);
  TO_STRING_KV(K_(is_inited), K_(tenant_id), K_(ls_id), K_(table_id), K_(schema_version),
               K_(task_id), K_(source_tablet_id), K_(dest_tablet_id), K_(fork_snapshot_version),
               K_(compat_mode), K_(data_format_version), K_(consumer_group_id));
public:
  bool is_inited_;
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  uint64_t table_id_;
  int64_t schema_version_;
  int64_t task_id_;
  common::ObTabletID source_tablet_id_;
  common::ObTabletID dest_tablet_id_;
  int64_t fork_snapshot_version_;
  lib::Worker::CompatMode compat_mode_;
  int64_t data_format_version_;
  int64_t consumer_group_id_;
};

struct ObTabletForkCtx final
{
public:
  ObTabletForkCtx();
  ~ObTabletForkCtx();
  int init(const ObTabletForkParam &param);
  bool is_valid() const;
  TO_STRING_KV(K_(is_inited), K_(complement_data_ret), K_(row_inserted),
               K_(ls_rebuild_seq));
public:
  int prepare_index_builder(const ObTabletForkParam &param);
  common::ObIAllocator &get_allocator() { return allocator_; }
  // Thread-safe wrapper methods for created_sstable_handles_
  int add_created_sstable(const ObTableHandleV2 &handle);
  int get_created_sstables(common::ObIArray<ObITable *> &tables);
  int get_created_sstable(const int64_t idx, ObTableHandleV2 &table_handle);
  // Thread-safe wrapper for create_sstable with allocator protection
  template <typename T = blocksstable::ObSSTable>
  int create_sstable(const ObTabletCreateSSTableParam &param, ObTableHandleV2 &table_handle)
  {
    int ret = common::OB_SUCCESS;
    common::ObSpinLockGuard guard(allocator_lock_);
    if (OB_FAIL(ObTabletCreateDeleteHelper::create_sstable<T>(param, allocator_, table_handle))) {
      STORAGE_LOG(WARN, "failed to create sstable", K(ret), K(param));
    }
    return ret;
  }
private:
  common::ObArenaAllocator range_allocator_;
  common::ObArenaAllocator allocator_;
  common::ObSpinLock allocator_lock_;  // Protect concurrent access to allocator_
public:
  typedef common::hash::ObHashMap<ObForkSSTableTaskKey, ObSSTableIndexBuilder*> INDEX_BUILDER_MAP;
  bool is_inited_;
  int complement_data_ret_;
  ObLSHandle ls_handle_;
  ObTabletHandle src_tablet_handle_;
  ObTabletHandle dst_tablet_handle_;
  ObTabletMemberWrapper<ObTabletTableStore> snapshot_table_store_;
  ObTableStoreIterator table_store_iterator_;
  INDEX_BUILDER_MAP index_builder_map_;
  common::hash::ObHashMap<ObITable::TableKey, ObStorageSchema*> clipped_schemas_map_;
  ObTablesHandleArray created_sstable_handles_;
  lib::ObMutex created_sstable_handles_lock_;  // Protect concurrent access to created_sstable_handles_
  int64_t row_inserted_;
  int64_t ls_rebuild_seq_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletForkCtx);
};

class ObTabletForkDag final: public share::ObIDag
{
public:
  ObTabletForkDag();
  virtual ~ObTabletForkDag();
  virtual int init_by_param(const share::ObIDagInitParam *param) override;
  virtual int create_first_task() override;
  virtual uint64_t hash() const override;
  bool operator ==(const share::ObIDag &other) const;
  bool is_inited() const { return is_inited_; }
  ObTabletForkCtx &get_context() { return context_; }
  void handle_init_failed_ret_code(int ret) { context_.complement_data_ret_ = ret; }
  int fill_info_param(compaction::ObIBasicInfoParam *&out_param, ObIAllocator &allocator) const override;
  int fill_dag_key(char *buf, const int64_t buf_len) const override;
  virtual lib::Worker::CompatMode get_compat_mode() const override
  { return param_.compat_mode_; }
  virtual uint64_t get_consumer_group_id() const override
  { return static_cast<uint64_t>(consumer_group_id_); }
  virtual bool is_ha_dag() const override { return false; }
  int calc_total_row_count();
private:
  bool is_inited_;
  ObTabletForkParam param_;
  ObTabletForkCtx context_;
  int64_t consumer_group_id_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletForkDag);
};

class ObTabletForkPrepareTask final : public share::ObITask
{
public:
  ObTabletForkPrepareTask()
    : ObITask(TASK_TYPE_DDL_FORK_PREPARE), is_inited_(false), param_(nullptr), context_(nullptr)
    { }
  virtual ~ObTabletForkPrepareTask() = default;
  int init(ObTabletForkParam &param, ObTabletForkCtx &ctx);
  virtual int process() override;
private:
  int prepare_context();
private:
  bool is_inited_;
  ObTabletForkParam *param_;
  ObTabletForkCtx *context_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletForkPrepareTask);
};

class ObTabletForkReuseTask final : public share::ObITask
{
public:
  ObTabletForkReuseTask();
  virtual ~ObTabletForkReuseTask();
  int init(ObTabletForkParam &param, ObTabletForkCtx &ctx, storage::ObITable *sstable);
  virtual int process() override;
private:
  int process_reuse_sstable();
private:
  bool is_inited_;
  ObTabletForkParam *param_;
  ObTabletForkCtx *context_;
  ObSSTable *sstable_;
  common::ObArenaAllocator allocator_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletForkReuseTask);
};

class ObTabletForkRewriteTask final : public share::ObITask
{
public:
  ObTabletForkRewriteTask();
  virtual ~ObTabletForkRewriteTask();
  int init(ObTabletForkParam &param, ObTabletForkCtx &ctx, storage::ObITable *sstable);
  virtual int process() override;
private:
  int prepare_context(
      const ObStorageSchema *&clipped_storage_schema);
  int prepare_macro_block_writer(
      const ObStorageSchema &clipped_storage_schema,
      ObWholeDataStoreDesc &data_desc,
      ObMacroBlockWriter *&macro_block_writer);
  int process_rewrite_sstable_task(
      ObMacroBlockWriter *macro_block_writer,
      const ObStorageSchema &clipped_storage_schema);
private:
  bool is_inited_;
  ObTabletForkParam *param_;
  ObTabletForkCtx *context_;
  ObSSTable *sstable_;
  common::ObArenaAllocator allocator_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletForkRewriteTask);
};

class ObTabletForkMergeTask final : public share::ObITask
{
public:
  ObTabletForkMergeTask()
    : ObITask(TASK_TYPE_DDL_FORK_MERGE), is_inited_(false), param_(nullptr), context_(nullptr)
    { }
  virtual ~ObTabletForkMergeTask() = default;
  int init(ObTabletForkParam &param, ObTabletForkCtx &ctx);
  virtual int process() override;
private:
  int create_sstables();
  int update_table_store_with_batch_tables(
      const int64_t ls_rebuild_seq,
      const ObLSHandle &ls_handle,
      const ObTabletHandle &src_tablet_handle,
      const ObTabletHandle &dst_tablet_handle,
      const common::ObTabletID &dst_tablet_id,
      const ObTablesHandleArray &tables_handle,
      const compaction::ObMergeType &merge_type);
  int build_create_sstable_param(
      const ObSSTable &src_table,
      ObSSTableIndexBuilder *index_builder,
      ObTabletCreateSSTableParam &create_sstable_param);
private:
  bool is_inited_;
  ObTabletForkParam *param_;
  ObTabletForkCtx *context_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletForkMergeTask);
};

struct ObTabletForkUtil final
{
public:
  static int check_satisfy_fork_condition(const ObTabletForkParam &param, bool &is_satisfied);
  static int check_fork_data_complete(const common::ObTabletID &dest_tablet_id, bool &is_complete);
  static int get_participants(
      const ObTableStoreIterator &table_store_iter,
      const int64_t fork_snapshot_version,
      ObIArray<ObITable *> &participants);
  static int try_schedule_fork_dags(const ObTableForkInfo &fork_info);
  static int freeze_tablet(const share::ObLSID &ls_id, const common::ObTabletID &tablet_id);
  static int freeze_tablets(const share::ObLSID &ls_id, const ObIArray<common::ObTabletID> &tablet_ids);
};

}  // end namespace storage
}  // end namespace oceanbase

#endif  // OCEANBASE_STORAGE_DDL_OB_TABLET_FORK_TASK_H_
