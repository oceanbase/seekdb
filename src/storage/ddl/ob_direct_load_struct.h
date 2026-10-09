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
constexpr int64_t OB_VEC_IDX_SNAPSHOT_KEY_LENGTH = 256;

class ObTablet;
class ObInsertMonitor;


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

}// namespace storage
}// namespace oceanbase

#endif//OCEANBASE_STORAGE_OB_DIRECT_LOAD_COMMON_H
