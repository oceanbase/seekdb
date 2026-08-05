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

#ifndef OCEANBASE_STORAGE_DDL_OB_DIRECT_LOAD_COMMON_H
#define OCEANBASE_STORAGE_DDL_OB_DIRECT_LOAD_COMMON_H

#include "lib/lock/ob_mutex.h"
#include "lib/lock/ob_bucket_lock.h"
#include "common/ob_tablet_id.h"
#include "common/row/ob_row_iterator.h"
#include "share/scn.h"
#include "share/tablet/ob_tablet_info.h"
#include "share/ob_tablet_autoincrement_param.h"
#include "data_plane/scheduler/ob_dag_scheduler.h"
#include "share/ob_ddl_common.h"
#include "storage/ob_i_table.h"
#include "storage/access/ob_store_row_iterator.h"
#include "storage/blocksstable/index_block/ob_index_block_builder.h"
#include "storage/blocksstable/ob_batch_datum_rows.h"
#include "storage/blocksstable/ob_macro_block_struct.h"
#include "storage/blocksstable/ob_imacro_block_flush_callback.h"
#include "storage/ddl/ob_ddl_redo_log_writer.h"
#include "storage/lob/ob_lob_meta.h"
#include "storage/ddl/ob_ddl_seq_generator.h"
#include "storage/ddl/ob_ddl_tablet_context.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
}

namespace share
{
enum ObVectorIndexAlgorithmType : uint16_t;
}

namespace blocksstable
{
class ObMacroMetaTempStore;
}

namespace storage
{
class ObTablet;
class ObInsertMonitor;
class ObDirectLoadSliceWriter;

class ObIDDLMergeHelper;

struct ObBatchSliceWriteInfo final
{
public:
  ObBatchSliceWriteInfo()
    : data_tablet_id_(), // tablet id of the data table.
      trans_version_(0),
      direct_load_type_()
  { }
  ObBatchSliceWriteInfo(const common::ObTabletID &tablet_id, const int64_t &trans_version,
      const ObDirectLoadType &direct_load_type)
    : data_tablet_id_(tablet_id),
      trans_version_(trans_version),
      direct_load_type_(direct_load_type)

  { }
  ~ObBatchSliceWriteInfo() = default;
  TO_STRING_KV(K(data_tablet_id_), K(trans_version_), K(direct_load_type_));
public:
  common::ObTabletID data_tablet_id_;
  int64_t trans_version_;
  ObDirectLoadType direct_load_type_;
};

struct ObTabletDirectLoadMgrKey final
{
public:
  ObTabletDirectLoadMgrKey() // hash needed.
    : tablet_id_(), direct_load_type_(ObDirectLoadType::DIRECT_LOAD_INVALID), context_id_(0)
  { }
  
  ObTabletDirectLoadMgrKey(const common::ObTabletID &tablet_id, const ObDirectLoadType &type, const int64_t ctx_id) // make sure type and ctx_id is correct.
    : tablet_id_(tablet_id), direct_load_type_(DIRECT_LOAD_DDL), context_id_(0)
  { UNUSED(type); UNUSED(ctx_id); }
  ObTabletDirectLoadMgrKey(const common::ObTabletID &tablet_id, const ObDirectLoadType &type)
    : tablet_id_(tablet_id), direct_load_type_(DIRECT_LOAD_DDL), context_id_(0)
  { UNUSED(type); }
  ~ObTabletDirectLoadMgrKey() = default;
  uint64_t hash() const { 
    return tablet_id_.hash() + murmurhash(&direct_load_type_, sizeof(direct_load_type_), 0)
        + murmurhash(&context_id_, sizeof(context_id_), 0); 
  }
  int hash(uint64_t &hash_val) const {hash_val = hash(); return OB_SUCCESS;}
  bool is_valid() const { 
    return tablet_id_.is_valid() && is_valid_direct_load(direct_load_type_) && 
      context_id_ == 0; }
  bool operator == (const ObTabletDirectLoadMgrKey &other) const {
        return tablet_id_ == other.tablet_id_ && direct_load_type_ == other.direct_load_type_
            && context_id_ == other.context_id_; }
  TO_STRING_KV(K_(tablet_id), K_(direct_load_type), K_(context_id));
public:
  common::ObTabletID tablet_id_;
  ObDirectLoadType direct_load_type_;
  int64_t context_id_;
};

struct ObDirectLoadSliceInfo final
{
public:
  ObDirectLoadSliceInfo()
    : is_full_direct_load_(false), is_lob_slice_(false), data_tablet_id_(), slice_id_(-1),
      context_id_(0), is_task_finish_(false), total_slice_cnt_(-1), slice_idx_(0), merge_slice_idx_(0)
    { }
  ~ObDirectLoadSliceInfo() = default;
  bool is_valid() const { return data_tablet_id_.is_valid() && slice_id_ >= 0 && context_id_ >= 0; }
  TO_STRING_KV(K_(is_full_direct_load), K_(is_lob_slice), K_(data_tablet_id), K_(slice_id), K_(context_id), K_(is_task_finish), K_(total_slice_cnt), K_(slice_idx), K_(merge_slice_idx));
public:
  bool is_full_direct_load_;
  bool is_lob_slice_;
  common::ObTabletID data_tablet_id_;
  int64_t slice_id_;
  int64_t context_id_;
  
  bool is_task_finish_;
  int64_t total_slice_cnt_;
  int64_t slice_idx_;
  int64_t merge_slice_idx_;
  DISALLOW_COPY_AND_ASSIGN(ObDirectLoadSliceInfo);
};


// usued in replay replay and runtime execution
struct ObDirectInsertCommonParam final
{
public:
  ObDirectInsertCommonParam()
    : tablet_id_(), direct_load_type_(DIRECT_LOAD_INVALID), data_format_version_(0), read_snapshot_(0)

  {}
  ~ObDirectInsertCommonParam() = default;
  bool is_valid() const { return tablet_id_.is_valid()
      && data_format_version_ >= 0 && read_snapshot_ >= 0 && is_valid_direct_load(direct_load_type_);
  }
  TO_STRING_KV(K_(tablet_id), K_(direct_load_type), K_(data_format_version), K_(read_snapshot));
public:
  common::ObTabletID tablet_id_;
  ObDirectLoadType direct_load_type_;
  uint64_t data_format_version_;
  // read_snapshot_ is used to scan the source data.
  // For full direct load task, it is also the commit version of the target macro block.
  int64_t read_snapshot_;
};

// only used in runtime execution
struct ObDirectInsertRuntimeOnlyParam final
{
public:
  ObDirectInsertRuntimeOnlyParam()
    : exec_ctx_(nullptr),
      task_id_(0),
      table_id_(OB_INVALID_ID),
      schema_version_(0),
      task_cnt_(0),
      need_online_opt_stat_gather_(false),
      parallel_(1),
      max_batch_size_(0)
  {
  }
  ~ObDirectInsertRuntimeOnlyParam() = default;
  bool is_valid() const { return OB_INVALID_ID != task_id_ && OB_INVALID_ID != table_id_ && schema_version_ > 0 && task_cnt_ >= 0; }
  TO_STRING_KV(KP_(exec_ctx),
               K_(task_id),
               K_(table_id),
               K_(schema_version),
               K_(task_cnt),
               K_(need_online_opt_stat_gather),
               K_(parallel),
               K_(max_batch_size));
public:
  sql::ObExecContext *exec_ctx_;
  int64_t task_id_;
  int64_t table_id_;
  int64_t schema_version_;
  int64_t task_cnt_;
  bool need_online_opt_stat_gather_;
  int64_t parallel_; // used to decide wehter need to use compress temp data in rescan task.
  int64_t max_batch_size_;
};

// full parameters used by runtime execution
struct ObTabletDirectLoadInsertParam final
{
public:
  ObTabletDirectLoadInsertParam()
    : common_param_(), runtime_only_param_(), is_replay_(false)
  {}
  ~ObTabletDirectLoadInsertParam() = default;
  bool is_valid() const {
      return (!is_replay_ && (common_param_.is_valid() && runtime_only_param_.is_valid()))
          || (is_replay_ && common_param_.is_valid());
  }
  int assign(const ObTabletDirectLoadInsertParam &other_param);
  TO_STRING_KV(K_(common_param), K_(runtime_only_param), K_(is_replay));
public:
  ObDirectInsertCommonParam common_param_;
  ObDirectInsertRuntimeOnlyParam runtime_only_param_;
  bool is_replay_;
};

class ObLobMetaRowIterator : public ObIStoreRowIterator
{
public:
  ObLobMetaRowIterator();
  virtual ~ObLobMetaRowIterator();
  int init(ObLobMetaWriteIter *iter,
            const int64_t trans_version);
  void reset();
  void reuse();
  virtual int get_next_row(const blocksstable::ObDatumRow *&row) override;

public:
  bool is_inited_;
  ObLobMetaWriteIter *iter_;
  int64_t trans_version_;
  blocksstable::ObDatumRow tmp_row_;
  ObLobMetaWriteResult lob_meta_write_result_;
};

struct ObTabletDDLParam final
{
public:
  ObTabletDDLParam();
  ~ObTabletDDLParam();
  bool is_valid() const;
  TO_STRING_KV(K_(direct_load_type),
               K_(start_scn),
               K_(commit_scn),
               K_(data_format_version),
               K_(table_key),
               K_(snapshot_version));
public:
  ObDirectLoadType direct_load_type_;
  share::SCN start_scn_;
  share::SCN commit_scn_;
  uint64_t data_format_version_;
  ObITable::TableKey table_key_;
  int64_t snapshot_version_;
};

struct ObDDLTableMergeDagParam : public share::ObIDagInitParam 
{
public:
  ObDDLTableMergeDagParam()
    : direct_load_type_(ObDirectLoadType::DIRECT_LOAD_INVALID),
      tablet_id_(),
      rec_scn_(share::SCN::min_scn()),
      is_commit_(false),
      start_scn_(share::SCN::min_scn()),
      data_format_version_(0),
      snapshot_version_(0),
      table_key_(),
      arena_(ObMemAttr("DDL_Mrg_Par")),
      user_data_()
  { }
  bool is_valid() const
  {
    return data_format_version_ > 0 && snapshot_version_ > 0
        && is_full_direct_load(direct_load_type_)
        && tablet_id_.is_valid() && start_scn_.is_valid_and_not_min();
  }
  int assign(const ObDDLTableMergeDagParam &merge_param);
  virtual ~ObDDLTableMergeDagParam() = default;
  VIRTUAL_TO_STRING_KV(K_(direct_load_type), K_(tablet_id), K_(rec_scn), K_(is_commit), K_(start_scn), K_(data_format_version),
                       K_(snapshot_version), K_(table_key), K_(user_data));
public:
  ObDirectLoadType direct_load_type_;
  ObTabletID tablet_id_;
  share::SCN rec_scn_;
  bool is_commit_;
  share::SCN start_scn_; // start log ts at schedule, for skipping expired task
  uint64_t data_format_version_;
  int64_t snapshot_version_;
  ObITable::TableKey table_key_; // table key is only used in idem type direct load mgr

  /* optional val */
  ObArenaAllocator arena_; // for user_data_
  ObTabletDDLCompleteMdsUserData user_data_;
};


/* merge param for ob ddl merge_task_v2 */
struct ObDDLTabletMergeDagParamV2
{
public:
  ObDDLTabletMergeDagParamV2():
    for_major_(false), for_lob_(false), for_replay_(false), merge_all_slice_(false), direct_load_type_(ObDirectLoadType::DIRECT_LOAD_INVALID), start_scn_(share::SCN::min_scn()),
    rec_scn_(share::SCN::min_scn()),  ddl_task_param_(), tablet_ctx_(nullptr), is_inited_(false) {}
  int init(const bool for_major,
           const bool for_lob,
           const bool for_replay,
           const share::SCN start_scn,
           const ObDirectLoadType &direct_load_type,
           const ObDDLTaskParam &task_param,
           ObDDLTabletContext *tablet_ctx_);
  bool is_valid() const;
  int assign(const ObDDLTabletMergeDagParamV2 &merge_param);
  int init_slice_sstable_array(hash::ObHashSet<int64_t> &slice_idxes);
  int set_slice_sstable(const int64_t slice_idx, const ObTableHandleV2 &sstable_handle);
  int get_tablet_param(ObTabletID &tablet_id, ObWriteTabletParam *&tablet_param) const;
  int get_merge_ctx(ObDDLTabletContext::MergeCtx *&merge_ctx);
  int get_storage_schema(ObStorageSchema *stroage_schema);
  void set_merge_all_slice() { merge_all_slice_ = true; }
  bool need_merge_all_slice() const { return for_major_ || merge_all_slice_; }
  ObDDLTabletContext *get_tablet_ctx() { return tablet_ctx_; }
  ObDDLTabletContext *get_tablet_ctx() const { return tablet_ctx_; }
  int get_merge_helper(ObIDDLMergeHelper *&merge_helper);
  VIRTUAL_TO_STRING_KV(K(for_major_), K(for_replay_), K(for_lob_), K(merge_all_slice_), K(direct_load_type_), K(start_scn_), K(rec_scn_), K(table_key_), K(ddl_task_param_), KPC(tablet_ctx_));
public:
  bool for_major_;
  bool for_lob_;
  bool for_replay_;
  bool merge_all_slice_;
  ObDirectLoadType direct_load_type_;
  share::SCN start_scn_;
  share::SCN rec_scn_;
  ObDDLTaskParam ddl_task_param_;
  ObITable::TableKey table_key_;
private:
  ObDDLTabletContext *tablet_ctx_;
  bool is_inited_;
};

// Column organization used by direct-load slice stores:
// <rowkey_columns, multi_version_columns, other_columns_if_exist>
// This organization matches the row stored in a macro block.
class ObTabletSliceStore
{
public:
  ObTabletSliceStore() {}
  virtual ~ObTabletSliceStore() {}
  virtual int append_row(const blocksstable::ObDatumRow &datum_row) = 0;
  virtual int append_batch(const blocksstable::ObBatchDatumRows &datum_rows) = 0;
  virtual int close() = 0;
  virtual void cancel() = 0;
  virtual int64_t get_row_count() const { return 0; } // dummy one
  virtual int64_t get_next_block_start_seq() const { return -1; } // invalid block start seq.
  virtual ObDatumRowkey get_compare_key() const { return ObDatumRowkey(); }
  DECLARE_PURE_VIRTUAL_TO_STRING;
};

class ObVectorIndexBaseSliceStore;
class ObVectorIndexSliceStore;
class ObIvfSliceStore;

class ObMacroBlockSliceStore: public ObTabletSliceStore
{
public:
  ObMacroBlockSliceStore()
   : is_inited_(false), ddl_redo_callback_(nullptr), macro_block_writer_(true /* use buffer */) {}
  virtual ~ObMacroBlockSliceStore() {
    if (ddl_redo_callback_ != nullptr) {
      common::ob_delete(ddl_redo_callback_);
    }
  }
  int init(
      ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr,
      const blocksstable::ObMacroDataSeq &data_seq,
      const share::SCN &start_scn);
  virtual int append_row(const blocksstable::ObDatumRow &datum_row) override;
  virtual int append_batch(const blocksstable::ObBatchDatumRows &datum_rows) override;
  virtual int close() override;
  virtual void cancel() override {}
  virtual int64_t get_next_block_start_seq() const override { return macro_block_writer_.get_last_macro_seq(); }
  TO_STRING_KV(K(is_inited_), K(macro_block_writer_));
private:
  bool is_inited_;
  blocksstable::ObIMacroBlockFlushCallback *ddl_redo_callback_;
  blocksstable::ObMacroBlockWriter macro_block_writer_;
};

class ObTabletDirectLoadMgr;

class ObDirectLoadSliceWriter final
{
public:
  ObDirectLoadSliceWriter();
  ~ObDirectLoadSliceWriter();
  void reset();
  inline bool is_inited() { return is_inited_; }
  int init(
      ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr,
      const blocksstable::ObMacroDataSeq &start_seq,
      const int64_t slice_idx,
      const int64_t merge_slice_idx);
  int fill_sstable_slice(
      const share::SCN &start_scn,
      const uint64_t table_id,
      const ObTabletID &curr_tablet_id,
      const ObStorageSchema *storage_schema,
      ObIStoreRowIterator *row_iter,
      const ObTableSchemaItem &schema_item,
      const ObDirectLoadType &direct_load_type,
      const ObArray<ObColumnSchemaItem> &column_items,
      const int64_t dir_id,
      const int64_t parallelism,
      const int64_t context_id,
      int64_t &affected_rows,
      ObInsertMonitor *insert_monitor = NULL);
  int fill_sstable_slice(
      const share::SCN &start_scn,
      const uint64_t table_id,
      const ObTabletID &curr_tablet_id,
      const ObStorageSchema *storage_schema,
      const blocksstable::ObBatchDatumRows &datum_rows,
      const ObTableSchemaItem &schema_item,
      const ObDirectLoadType &direct_load_type,
      const ObArray<ObColumnSchemaItem> &column_items,
      const int64_t dir_id,
      const int64_t parallelism,
      const int64_t context_id,
      ObInsertMonitor *insert_monitor = NULL);
  int fill_lob_sstable_slice(
      const uint64_t table_id,
      ObIAllocator &allocator,
      ObIAllocator &iter_allocator,
      const share::SCN &start_scn,
      const ObBatchSliceWriteInfo &info,
      share::ObTabletCacheInterval &pk_interval,
      const ObArray<int64_t> &lob_column_idxs,
      const ObArray<common::ObObjMeta> &col_types,
      const ObTableSchemaItem &schema_item,
      blocksstable::ObDatumRow &datum_row);
  int fill_lob_sstable_slice(
      const uint64_t table_id,
      ObIAllocator &allocator,
      ObIAllocator &iter_allocator,
      const share::SCN &start_scn,
      const ObBatchSliceWriteInfo &info,
      share::ObTabletCacheInterval &pk_interval,
      const ObArray<int64_t> &lob_column_idxs,
      const ObArray<common::ObObjMeta> &col_types,
      const ObTableSchemaItem &schema_item,
      blocksstable::ObBatchDatumRows &datum_rows);
  int close();
  int fill_vector_index_data(
    const int64_t snapshot_version,
    const ObStorageSchema *storage_schema,
    const SCN &start_scn,
    const ObTableSchemaItem &schema_item,
    ObInsertMonitor* insert_monitor,
    const int64_t context_id);
  int64_t get_row_count() const { return nullptr == slice_store_ ? 0 : slice_store_->get_row_count(); }
  blocksstable::ObMacroDataSeq &get_start_seq() { return start_seq_; }
  bool is_empty() const { return 0 == get_row_count(); }
  ObTabletSliceStore *get_slice_store() const { return slice_store_; }
  void cancel();
  int64_t get_next_block_start_seq() const { return nullptr == slice_store_ ? start_seq_.get_data_seq() /*slice empty*/ : slice_store_->get_next_block_start_seq(); }
  TO_STRING_KV(K(is_inited_), K(is_canceled_), K(start_seq_), K(slice_idx_), K(merge_slice_idx_), KPC(slice_store_));
private:
  int fill_lob_into_macro_block(
      ObIAllocator &allocator,
      ObIAllocator &iter_allocator,
      const share::SCN &start_scn,
      const ObBatchSliceWriteInfo &info,
      share::ObTabletCacheInterval &pk_interval,
      const common::ObObjMeta &col_type,
      const ObLobStorageParam &lob_storage_param,
      blocksstable::ObStorageDatum &datum);
  int check_null_and_length(
      const bool is_index_table,
      const bool has_lob_rowkey,
      const int64_t rowkey_column_cnt,
      const blocksstable::ObDatumRow &row_val) const;
  int check_null_and_length(
      const bool is_index_table,
      const bool has_lob_rowkey,
      const int64_t rowkey_column_cnt,
      const blocksstable::ObBatchDatumRows &datum_rows);
  int prepare_slice_store_if_need(
      const ObStorageSchema *storage_schema, 
      const share::SCN &start_scn,
      const ObString vec_idx_param,
      const int64_t vec_dim,
      const int64_t context_id);
  int prepare_vector_slice_store(
      const ObStorageSchema *storage_schema,
      const ObString vec_idx_param,
      const int64_t vec_dim,
      const int64_t context_id);
public:
  static int report_unique_key_dumplicated(
      const int ret_code,
      const uint64_t table_id,
      const blocksstable::ObDatumRow &datum_row,
      const common::ObTabletID &tablet_id,
      int &report_ret_code);
  static int report_unique_key_dumplicated(
      const int ret_code,
      const uint64_t table_id,
      const blocksstable::ObBatchDatumRows &datum_rows,
      const common::ObTabletID &tablet_id,
      int &report_ret_code);
private:
  int prepare_iters(
      ObIAllocator &allocator,
      ObIAllocator &iter_allocator,
      blocksstable::ObStorageDatum &datum,
      const ObTabletID &tablet_id,
      const int64_t trans_version,
      const ObObjType &obj_type,
      const ObCollationType &cs_type,
      const int64_t timeout_ts,
      const ObLobStorageParam &lob_storage_param,
      share::ObTabletCacheInterval &pk_interval,
      ObLobMetaRowIterator *&row_iter);
  int inner_fill_vector_index_data(
      ObMacroBlockSliceStore *&macro_block_slice_store,
      ObVectorIndexBaseSliceStore *vec_idx_slice_store,
      const int64_t snapshot_version,
      const ObStorageSchema *storage_schema,
      const SCN &start_scn,
      share::ObVectorIndexAlgorithmType index_type,
      ObInsertMonitor* insert_monitor);
  int inner_fill_hnsw_vector_index_data(
      ObVectorIndexSliceStore &vec_idx_slice_store,
      const int64_t snapshot_version,
      const ObStorageSchema *storage_schema,
      const SCN &start_scn,
      const int64_t lob_inrow_threshold,
      ObInsertMonitor* insert_monitor);
  int inner_fill_ivf_vector_index_data(
      ObIvfSliceStore &vec_idx_slice_store,
      const int64_t snapshot_version,
      const ObStorageSchema *storage_schema,
      const SCN &start_scn,
      const int64_t lob_inrow_threshold,
      ObInsertMonitor* insert_monitor);
private:
  bool is_inited_;
  bool is_canceled_;
  blocksstable::ObMacroDataSeq start_seq_;
  int64_t slice_idx_;
  int64_t merge_slice_idx_;
  ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr_;
  ObTabletSliceStore *slice_store_;
  ObLobMetaWriteIter *meta_write_iter_;
  ObLobMetaRowIterator *row_iterator_;
  common::ObArenaAllocator allocator_;
  common::ObIAllocator *lob_allocator_;
  ObSEArray<int64_t, 256> rowkey_lengths_;
};


struct ObTabletDirectLoadBatchSliceKey final
{
public:
  ObTabletDirectLoadBatchSliceKey()
    : tablet_id_(), tid_(GETTID())
  {}
  explicit ObTabletDirectLoadBatchSliceKey(const ObTabletID &tablet_id)
    : tablet_id_(tablet_id), tid_(GETTID())
  {}
  ObTabletDirectLoadBatchSliceKey(const ObTabletDirectLoadBatchSliceKey &other) {
    tablet_id_ = other.tablet_id_;
    tid_ = other.tid_;
  }
  ObTabletDirectLoadBatchSliceKey &operator=(const ObTabletDirectLoadBatchSliceKey &other) {
    tablet_id_ = other.tablet_id_;
    tid_ = other.tid_;
    return *this;
  }
  ~ObTabletDirectLoadBatchSliceKey() = default;
  uint64_t hash() const {
    return tablet_id_.hash() + murmurhash(&tid_, sizeof(tid_), 0);
  }
  int hash(uint64_t &hash_val) const {hash_val = hash(); return OB_SUCCESS;}
  bool operator==(const ObTabletDirectLoadBatchSliceKey &other) const {
        return tablet_id_ == other.tablet_id_ && tid_ == other.tid_; }
  TO_STRING_KV(K_(tablet_id), K_(tid));
public:
  common::ObTabletID tablet_id_;
  int64_t tid_;
};

struct ObTabletDirectLoadSliceGroup final
{
public:
  ObTabletDirectLoadSliceGroup()
    : is_inited_(false), bucket_lock_(), batch_slice_map_(), allocator_()
  {
  }
  ~ObTabletDirectLoadSliceGroup()
  {
    reset();
  }
  int init(const int64_t task_cnt);
  void reset();
  int record_slice_id(const ObTabletDirectLoadBatchSliceKey &key, const int64_t slice_id);
  int get_slice_array(const ObTabletDirectLoadBatchSliceKey &key, ObArray<int64_t> &slice_array);
  int remove_slice_array(const ObTabletDirectLoadBatchSliceKey &key);
  TO_STRING_KV(K_(is_inited));
public:
  bool is_inited_;
  ObBucketLock bucket_lock_;
  hash::ObHashMap<ObTabletDirectLoadBatchSliceKey, ObArray<int64_t/*slices_array_idx*/> *> batch_slice_map_;
  ObConcurrentFIFOAllocator allocator_;
};

}// namespace storage
}// namespace oceanbase

#endif//OCEANBASE_STORAGE_OB_DIRECT_LOAD_COMMON_H
