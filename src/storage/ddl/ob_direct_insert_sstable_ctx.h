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

#ifndef OCEANBASE_STORAGE_DDL_OB_DIRECT_INSERT_SSTABLE_CTX_H
#define OCEANBASE_STORAGE_DDL_OB_DIRECT_INSERT_SSTABLE_CTX_H

#include "storage/meta_mem/ob_tablet_handle.h"
#include "lib/lock/ob_mutex.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/lock/ob_bucket_lock.h"
#include "common/ob_tablet_id.h"
#include "common/row/ob_row_iterator.h"
#include "query/optimizer/stat/ob_opt_column_stat.h"
#include "share/scn.h"
#include "storage/ob_i_table.h"
#include "storage/ob_row_reshape.h"
#include "storage/blocksstable/ob_imacro_block_flush_callback.h"
#include "storage/blocksstable/ob_macro_block_struct.h"
#include "storage/blocksstable/ob_macro_block_writer.h"
#include "storage/ddl/ob_direct_load_struct.h"
#include "storage/meta_mem/ob_tablet_pointer.h"
#include "storage/tablet/ob_tablet_meta.h"
#include "src/share/ob_ddl_common.h"
#include "storage/ddl/ob_i_direct_load_mgr.h"
namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObDDLCtrl;
}

namespace blocksstable
{
class ObIMacroBlockFlushCallback;
class ObMacroBlockWriter;
}

namespace share
{
struct ObTabletCacheInterval;
}

namespace storage
{
class ObTablet;
class ObLobMetaRowIterator;
class ObTabletDirectLoadMgrHandle;
class ObTabletDirectLoadMgr;
class ObTabletFullDirectLoadMgr;
struct ObInsertMonitor;

class ObDirectLoadMgr final
{
public:
  ObDirectLoadMgr();
  ~ObDirectLoadMgr();
  void destroy();
  static int server_module_init(
      ObDirectLoadMgr *&direct_load_mgr);
  int init();

  int replay_create_tablet_direct_load(
      const ObTablet *tablet,
      const int64_t execution_id,
      const ObTabletDirectLoadInsertParam &param);

  int get_tablet_mgr(
      const ObTabletDirectLoadMgrKey &key,
      ObTabletDirectLoadMgrHandle &direct_load_mgr_handle);
  int get_tablet_mgr_and_check_major(
      const ObTabletID &tablet_id,
      const bool is_full_direct_load,
      ObTabletDirectLoadMgrHandle &direct_load_mgr_handle,
      bool &is_major_sstable_exist);
  int gc_tablet_direct_load();
  // Remove a legacy tablet direct-load manager after physical major generation
  // or tablet deletion.
  int remove_tablet_direct_load(const ObTabletDirectLoadMgrKey &mgr_key);
  ObIAllocator &get_allocator() { return allocator_; }
private:
  struct GetGcCandidateOp final {
  public:
    GetGcCandidateOp(ObIArray<ObTabletDirectLoadMgrKey> &candidate_mgrs)
      : candidate_mgrs_(candidate_mgrs) {}
    ~GetGcCandidateOp() {}
    int operator() (common::hash::HashMapPair<ObTabletDirectLoadMgrKey, ObBaseTabletDirectLoadMgr *> &kv);
  private:
    DISALLOW_COPY_AND_ASSIGN(GetGcCandidateOp);
    ObIArray<ObTabletDirectLoadMgrKey> &candidate_mgrs_;
  };

  static int alloc_tablet_direct_load_mgr(
      ObIAllocator &allocator,
      const ObTabletDirectLoadMgrKey &mgr_key,
      ObBaseTabletDirectLoadMgr *&direct_load_mgr);
  int try_create_tablet_direct_load_mgr_nolock(
      const bool major_sstable_exist,
      ObIAllocator &allocator,
      const ObTabletDirectLoadMgrKey &mgr_key,
      ObTabletDirectLoadMgrHandle &handle);
  int get_tablet_mgr_no_lock(
      const ObTabletDirectLoadMgrKey &mgr_key,
      ObTabletDirectLoadMgrHandle &direct_load_mgr_handle);
  int remove_tablet_direct_load_nolock(
      const ObTabletDirectLoadMgrKey &mgr_key);

private:
  typedef common::hash::ObHashMap<
    ObTabletDirectLoadMgrKey,
    ObBaseTabletDirectLoadMgr*,
    common::hash::NoPthreadDefendMode> TABLET_MGR_MAP;
  bool is_inited_;
  common::ObBucketLock bucket_lock_; // to avoid concurrent execution on the TabletDirectLoadMgr.
  common::ObConcurrentFIFOAllocator allocator_;
  TABLET_MGR_MAP tablet_mgr_map_;
  volatile int64_t last_gc_time_;
DISALLOW_COPY_AND_ASSIGN(ObDirectLoadMgr);
};

struct ObTabletDirectLoadBuildCtx final
{
public:
  ObTabletDirectLoadBuildCtx();
  ~ObTabletDirectLoadBuildCtx();
  bool is_valid () const;
  static uint64_t get_slice_id_hash(const int64_t slice_id)
  {
    return common::murmurhash(&slice_id, sizeof(slice_id), 0L);
  }
  void reset_slice_ctx_on_demand();
  void cleanup_slice_writer(const int64_t context_id);
  share::SCN get_commit_scn() { return commit_scn_.atomic_load(); }
  TO_STRING_KV(K_(build_param), K_(is_task_end), K_(task_finish_count), K_(task_total_cnt), K_(commit_scn),
      KP_(index_builder), KPC(storage_schema_));
public:
  struct SliceKey
  {
  public:
    SliceKey() : context_id_(0), slice_id_(0) {}
    explicit SliceKey(const int64_t context_id, const int64_t slice_id): context_id_(context_id), slice_id_(slice_id) {}
    ~SliceKey() {}
    uint64_t hash() const { return murmurhash(&slice_id_, sizeof(slice_id_), 0); }
    int hash(uint64_t &hash_val) const { hash_val = hash(); return OB_SUCCESS;}
    bool operator == (const SliceKey &other) const { return context_id_ == other.context_id_ && slice_id_ == other.slice_id_; }
    TO_STRING_KV(K_(context_id), K_(slice_id));
  public:
    int64_t context_id_;
    int64_t slice_id_;
  };
  typedef common::hash::ObHashMap<
    SliceKey,
    ObDirectLoadSliceWriter *> SLICE_MGR_MAP;
  common::ObConcurrentFIFOAllocator allocator_;
  common::ObConcurrentFIFOAllocator slice_writer_allocator_;
  ObTabletDirectLoadInsertParam build_param_;
  SLICE_MGR_MAP slice_mgr_map_; // key is <context_id, slice_id>, decided by upper caller.
  blocksstable::ObWholeDataStoreDesc data_block_desc_;
  blocksstable::ObSSTableIndexBuilder *index_builder_;
  common::ObArray<ObOptColumnStat*> column_stat_array_; // online column stat result.
  bool is_task_end_; // to avoid write commit log/freeze in memory index sstable again
  int64_t task_finish_count_; // reach the parallel slice cnt, means the tablet data finished.
  int64_t task_total_cnt_; // parallelism of the PX.
  share::SCN commit_scn_;
  ObArenaAllocator schema_allocator_;
  ObStorageSchema *storage_schema_;
};

class ObTabletDirectLoadMgr :public ObBaseTabletDirectLoadMgr
{
public:
  ObTabletDirectLoadMgr();
  virtual ~ObTabletDirectLoadMgr();
  virtual bool is_valid();
  int update(ObBaseTabletDirectLoadMgr *lob_tablet_mgr, const ObTabletDirectLoadInsertParam &build_param) override;
  int open_sstable_slice(
      const bool is_data_tablet_process_for_lob,
      const blocksstable::ObMacroDataSeq &start_seq,
      const ObDirectLoadSliceInfo &slice_info);
  int fill_sstable_slice(
      const ObDirectLoadSliceInfo &slice_info,
      const share::SCN &start_scn,
      ObIStoreRowIterator *iter,
      int64_t &affected_rows,
      ObInsertMonitor *insert_monitor = NULL) override;
  int fill_sstable_slice(
      const ObDirectLoadSliceInfo &slice_info,
      const share::SCN &start_scn,
      const blocksstable::ObBatchDatumRows &datum_rows,
      ObInsertMonitor *insert_monitor = NULL) override;
  int fill_lob_sstable_slice(
      ObIAllocator &allocator,
      const ObDirectLoadSliceInfo &slice_info /*contains data_tablet_id, lob_slice_id, start_seq*/,
      const share::SCN &start_scn,
      share::ObTabletCacheInterval &pk_interval,
      blocksstable::ObDatumRow &datum_row) override;
  int fill_lob_sstable_slice(
      ObIAllocator &allocator,
      const ObDirectLoadSliceInfo &slice_info /*contains data_tablet_id, lob_slice_id, start_seq*/,
      const share::SCN &start_scn,
      share::ObTabletCacheInterval &pk_interval,
      blocksstable::ObBatchDatumRows &datum_rows) override;
  int close_sstable_slice(
      const bool is_data_tablet_process_for_lob,
      const ObDirectLoadSliceInfo &slice_info,
      const share::SCN &start_scn,
      const int64_t execution_id,
      ObInsertMonitor *insert_monitor,
      blocksstable::ObMacroDataSeq &next_seq) override;
  virtual int update_max_lob_id(const int64_t lob_id) { UNUSED(lob_id); return common::OB_SUCCESS; }
  virtual int set_total_slice_cnt(const int64_t slice_cnt) { UNUSED(slice_cnt); return OB_NOT_SUPPORTED;}
  int cancel();
  virtual share::SCN get_start_scn() = 0;
  virtual share::SCN get_commit_scn(const ObTabletMeta &tablet_meta) = 0;
  virtual ObTabletDirectLoadBuildCtx &get_sqc_build_ctx() { return sqc_build_ctx_; }
  ObTabletID get_lob_meta_tablet_id() {
    return lob_mgr_handle_.is_valid() ? lob_mgr_handle_.get_obj()->get_tablet_id() : ObTabletID();
  }
  ObTabletDirectLoadMgrHandle &get_lob_mgr_handle() { return lob_mgr_handle_; }
  int64_t get_ddl_task_id() const override { return sqc_build_ctx_.build_param_.runtime_only_param_.task_id_; }
  ObTabletDirectLoadInsertParam &get_build_param() override { return sqc_build_ctx_.build_param_; }
  ObWholeDataStoreDesc &get_data_block_desc() override { return sqc_build_ctx_.data_block_desc_; }
  // virtual int get_online_stat_collect_result();
  virtual int wrlock(const int64_t timeout_us, uint32_t &lock_tid);
  virtual int rdlock(const int64_t timeout_us, uint32_t &lock_tid);
  virtual void unlock(const uint32_t lock_tid);
  virtual int prepare_index_builder_if_need(const ObTableSchema &table_schema);
  const ObIArray<ObColumnSchemaItem> &get_column_info() const override { return column_items_; };
  bool is_schema_item_ready() { return is_schema_item_ready_; }
  bool get_micro_index_clustered() { return micro_index_clustered_; }
  int prepare_storage_schema(ObTabletHandle &tablet_handle);
  int64_t get_task_cnt() override { return task_cnt_; }
  VIRTUAL_TO_STRING_KV(K_(is_inited), K_(is_schema_item_ready), K_(tablet_id), K_(table_key), K_(ref_cnt),
               K_(direct_load_type), K_(sqc_build_ctx), KPC(lob_mgr_handle_.get_obj()), K_(schema_item), K_(column_items), K_(lob_column_idxs),
               K_(task_cnt), K_(micro_index_clustered));

protected:
  int prepare_schema_item_on_demand(const uint64_t table_id,
                                    const int64_t parallel);

// private:
  /* +++++ online column stat collect +++++ */
  // virtual int init_sql_statistics_if_needed();
  // int collect_obj(const blocksstable::ObDatumRow &datum_row);
  /* +++++ -------------------------- +++++ */
public:
  static const int64_t TRY_LOCK_TIMEOUT = 10 * 1000000; // 10s
  static const int64_t EACH_MACRO_MIN_ROW_CNT = 1000000; // 100w
protected:
  bool is_inited_;
  bool is_schema_item_ready_;
  // sqc_build_ctx_ is just used for the observer node who receives the requests from the SQL Layer
  // to write the start log and the data redo log. And other observer nodes can not use it.
  ObTabletDirectLoadBuildCtx sqc_build_ctx_;
  // to handle the lob meta tablet, use it before the is_valid judgement.
  ObTabletDirectLoadMgrHandle lob_mgr_handle_;
  // cache ObTableSchema for lob direct load performance
  ObArray<ObColumnSchemaItem> column_items_;
  ObArray<int64_t> lob_column_idxs_;
  ObArray<common::ObObjMeta> lob_col_types_;
  ObTableSchemaItem schema_item_;
  int64_t dir_id_;
  int64_t task_cnt_;
  bool micro_index_clustered_;
};

class ObTabletFullDirectLoadMgr final : public ObTabletDirectLoadMgr
{
public:
  ObTabletFullDirectLoadMgr();
  ~ObTabletFullDirectLoadMgr();
 int update(
      ObBaseTabletDirectLoadMgr *lob_tablet_mgr,
      const ObTabletDirectLoadInsertParam &build_param) override;
  int open(const int64_t current_execution_id, share::SCN &start_scn) override; // start
  int close(const int64_t execution_id, const share::SCN &start_scn) override; // end, including write commit log, wait major sstable generates.
  int start_nolock(
      const ObITable::TableKey &table_key,
      const share::SCN &start_scn,
      const uint64_t data_format_version,
      const int64_t execution_id,
      const share::SCN &checkpoint_scn,
      ObDDLKvMgrHandle &ddl_kv_mgr_handle,
      ObDDLKvMgrHandle &lob_kv_mgr_handle);
  int start(
      ObTablet &tablet,
      const ObITable::TableKey &table_key,
      const share::SCN &start_scn,
      const uint64_t data_format_version,
      const int64_t execution_id,
      const share::SCN &checkpoint_scn);
  int start_with_checkpoint(
      ObTablet &tablet,
      const share::SCN &start_scn,
      const uint64_t data_format_version,
      const int64_t execution_id,
      const share::SCN &checkpoint_scn);
  int commit(
      ObTablet &tablet,
      const share::SCN &start_scn,
      const share::SCN &commit_scn,
      const uint64_t table_id,
      const int64_t ddl_task_id,
      const bool is_replay); // schedule build a major sstable

  void set_commit_scn_nolock(const share::SCN &scn);
  int set_commit_scn(const share::SCN &scn);
  share::SCN get_start_scn() override;
  share::SCN get_commit_scn(const ObTabletMeta &tablet_meta) override;

  // check need schedule major compaction.
  int can_schedule_major_compaction_nolock(
      const ObTablet &tablet,
      bool &can_schedule);
  int prepare_ddl_merge_param(
      const ObTablet &tablet,
      ObDDLTableMergeDagParam &merge_param);
  int prepare_major_merge_param(ObTabletDDLParam &param);
  void cleanup_slice_writer(const int64_t context_id);
  INHERIT_TO_STRING_KV("ObTabletDirectLoadMgr", ObTabletDirectLoadMgr, K_(start_scn), K_(commit_scn), K_(execution_id));
private:
  bool is_started() { return start_scn_.is_valid_and_not_min(); }
  int schedule_merge_task(const share::SCN &start_scn, const share::SCN &commit_scn, const bool wait_major_generated, const bool is_replay); // try wait build major sstable
  int cleanup_unlock();
  int init_ddl_table_store(const share::SCN &start_scn, const int64_t snapshot_version, const share::SCN &ddl_checkpoint_scn);
  int update_major_sstable();

private:
  share::SCN start_scn_;
  share::SCN commit_scn_;
  int64_t execution_id_;
DISALLOW_COPY_AND_ASSIGN(ObTabletFullDirectLoadMgr);
};

}// namespace storage
}// namespace oceanbase

#endif//OCEANBASE_STORAGE_DDL_OB_DIRECT_INSERT_SSTABLE_CTX_H
