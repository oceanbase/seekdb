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
 *
 * Change Stream plugin for asynchronous vector index maintenance.
 *
 * ObCSPluginAsyncIndex: thin plugin wrapper; delegates to ObCSAsyncIndexProcessor.
 * ObCSAsyncIndexProcessor: holds all implementation; created on stack per process() call.
 */

#ifndef OB_CS_PLUGIN_ASYNC_INDEX_H_
#define OB_CS_PLUGIN_ASYNC_INDEX_H_

#include "lib/ob_define.h"
#include "lib/container/ob_se_array.h"
#include "lib/container/ob_iarray.h"
#include "common/ob_tablet_id.h"
#include "share/scn.h"
#include "share/schema/ob_schema_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/change_stream/ob_change_stream_dispatcher.h"
#include "share/change_stream/ob_change_stream_plugin.h"
#include "lib/hash/ob_hashmap.h"

namespace oceanbase
{
namespace memtable
{
struct ObRowData;
}  // namespace memtable

namespace transaction
{
class ObTxDesc;
}  // namespace transaction

namespace sql
{
class ObDASInsertOp;
class ObDASInsCtDef;
class ObDASInsRtDef;
}  // namespace sql

namespace share
{

// ---------------------------------------------------------------------------
// Event type constants for ObASyncIndexEvent
// ---------------------------------------------------------------------------
namespace ObCSAsyncIndexEventType
{
  static const char INSERT = 'I';  // Insert event
  static const char DELETE = 'D';  // Delete event
}

// ---------------------------------------------------------------------------
// ObASyncIndexEvent: an incremental event produced by parsing one redo row.
// ---------------------------------------------------------------------------

struct ObASyncIndexEvent
{
  common::ObTabletID tablet_id_;
  uint64_t           table_id_;
  int64_t            commit_version_;
  SCN                scn_;
  int64_t            vid_;
  char               type_;
  char              *vec_data_;
  int64_t            vec_data_len_;

  ObASyncIndexEvent()
    : tablet_id_(),
      table_id_(common::OB_INVALID_ID),
      commit_version_(),
      vid_(0),
      type_(ObCSAsyncIndexEventType::INSERT),
      vec_data_(nullptr),
      vec_data_len_(0)
  {}

  void reset()
  {
    tablet_id_.reset();
    table_id_ = common::OB_INVALID_ID;
    scn_.reset();
    vid_ = 0;
    type_ = ObCSAsyncIndexEventType::INSERT;
    vec_data_ = nullptr;
    vec_data_len_ = 0;
  }

  TO_STRING_KV(K_(tablet_id), K_(table_id), K_(scn), K_(vid), K_(type),
               KP_(vec_data), K_(vec_data_len));
};

// ---------------------------------------------------------------------------
// ObCSVecIndexInfo: cached per-table vector index metadata.
// ---------------------------------------------------------------------------

struct ObCSVecIndexInfo
{
  uint64_t data_table_id_;
  uint64_t index_id_table_id_;
  uint64_t delta_buffer_table_id_;
  int64_t  vec_column_id_;
  int64_t  vec_col_idx_;
  schema::ObIndexType index_type_;
  int64_t  dim_;

  ObCSVecIndexInfo()
    : data_table_id_(common::OB_INVALID_ID),
      index_id_table_id_(common::OB_INVALID_ID),
      delta_buffer_table_id_(common::OB_INVALID_ID),
      vec_column_id_(common::OB_INVALID_ID),
      vec_col_idx_(-1),
      index_type_(schema::INDEX_TYPE_IS_NOT),
      dim_(0)
  {}

  void reset()
  {
    data_table_id_ = common::OB_INVALID_ID;
    index_id_table_id_ = common::OB_INVALID_ID;
    delta_buffer_table_id_ = common::OB_INVALID_ID;
    vec_column_id_ = common::OB_INVALID_ID;
    vec_col_idx_ = -1;
    index_type_ = schema::INDEX_TYPE_IS_NOT;
    dim_ = 0;
  }

  bool is_valid() const
  {
    return data_table_id_ != common::OB_INVALID_ID
        && index_id_table_id_ != common::OB_INVALID_ID
        && delta_buffer_table_id_ != common::OB_INVALID_ID
        && vec_column_id_ != static_cast<int64_t>(common::OB_INVALID_ID)
        && dim_ > 0;
  }

  TO_STRING_KV(K_(data_table_id), K_(index_id_table_id), K_(delta_buffer_table_id),
               K_(vec_column_id), K_(vec_col_idx), K_(index_type), K_(dim));
};

// ---------------------------------------------------------------------------
// TabletEventGroup, GroupKey
// ---------------------------------------------------------------------------

struct TabletEventGroup
{
  common::ObTabletID tablet_id_;
  uint64_t table_id_;
  ObCSVecIndexInfo vec_info_;
  common::ObSEArray<ObASyncIndexEvent, 64> events_;
  TabletEventGroup()
    : tablet_id_(),
      table_id_(common::OB_INVALID_ID),
      vec_info_(),
      events_()
  {}
  TO_STRING_KV(K_(tablet_id), K_(table_id), K_(vec_info), K_(events));
};

struct GroupKey
{
  common::ObTabletID tablet_id_;
  uint64_t index_id_table_id_;
  GroupKey() : tablet_id_(), index_id_table_id_(common::OB_INVALID_ID) {}
  GroupKey(const common::ObTabletID &tablet_id, const uint64_t index_id_table_id)
    : tablet_id_(tablet_id), index_id_table_id_(index_id_table_id)
  {}
  uint64_t hash() const
  {
    uint64_t hash_val = tablet_id_.hash();
    hash_val += index_id_table_id_;
    return hash_val;
  }
  int hash(uint64_t &hash_val) const
  {
    hash_val = hash();
    return common::OB_SUCCESS;
  }
  bool operator==(const GroupKey &other) const
  {
    return tablet_id_ == other.tablet_id_ && index_id_table_id_ == other.index_id_table_id_;
  }
  TO_STRING_KV(K_(tablet_id), K_(index_id_table_id));
};

// ---------------------------------------------------------------------------
// ObCSAsyncIndexProcessor: per-thread processor; holds all implementation.
// Created on stack for each process() call; no shared mutable state.
// ---------------------------------------------------------------------------
class ObCSAsyncIndexProcessor
{
public:
  explicit ObCSAsyncIndexProcessor(ObCSExecCtx &ctx);
  ~ObCSAsyncIndexProcessor();

  int process(common::ObIArray<ObCSRow> &rows);

private:
  int resolve_table_id_from_tablet_id_(const common::ObTabletID &tablet_id, uint64_t &table_id);
  int resolve_vector_index_info_(uint64_t table_id,
      schema::ObSchemaGetterGuard &schema_guard,
      common::ObIArray<ObCSVecIndexInfo> &vec_infos);
  int get_or_cache_vec_index_info_(uint64_t table_id,
      schema::ObSchemaGetterGuard &schema_guard,
      const common::ObIArray<ObCSVecIndexInfo> *&out_vec_infos_ptr);
  int build_event_from_row_(const ObCSRow &row, const ObCSVecIndexInfo &vec_info,
      ObASyncIndexEvent &event, bool &skip);
  int extract_vector_data_(const memtable::ObRowData &new_row,
      const ObCSVecIndexInfo &vec_info, char *&vec_data, int64_t &vec_len);
  int insert_vector_index_log_batch_(const common::ObIArray<ObASyncIndexEvent> &events,
      const ObCSVecIndexInfo &vec_info);
  int get_tx_desc_from_ctx_(transaction::ObTxDesc *&tx_desc);
  int build_das_ins_ctdef_(common::ObArenaAllocator &allocator,
      const ObCSVecIndexInfo &vec_info,
      sql::ObDASInsertOp *insert_op,
      sql::ObDASInsCtDef *&ins_ctdef);
  int build_das_ins_rtdef_(common::ObArenaAllocator &allocator,
      sql::ObDASInsertOp *insert_op,
      sql::ObDASInsRtDef *&ins_rtdef);
  int build_insert_buffer_from_events_(common::ObArenaAllocator &allocator,
      const common::ObIArray<ObASyncIndexEvent> &events,
      const ObCSVecIndexInfo &vec_info,
      sql::ObDASInsertOp *insert_op,
      sql::ObDASInsCtDef *ins_ctdef);
  int set_das_insert_context_(const common::ObIArray<ObASyncIndexEvent> &events,
      const ObCSVecIndexInfo &vec_info,
      transaction::ObTxDesc *tx_desc,
      sql::ObDASInsertOp *insert_op,
      common::ObArenaAllocator &allocator);
  void handle_insert_or_write_error_(int &ret, const TabletEventGroup &g,
      bool is_insert);
  int write_to_vsag_(const common::ObIArray<ObASyncIndexEvent> &events,
      const ObCSVecIndexInfo &vec_info);

  int init_schema_guard_();

  ObCSExecCtx &ctx_;
  schema::ObSchemaGetterGuard schema_guard_;  // Per-thread; created in process().
  common::hash::ObHashMap<uint64_t, common::ObSEArray<ObCSVecIndexInfo, 4>> vec_index_cache_;
  common::hash::ObHashMap<uint64_t, uint64_t> tablet_to_table_;

  DISALLOW_COPY_AND_ASSIGN(ObCSAsyncIndexProcessor);
};

// ---------------------------------------------------------------------------
// ObCSPluginAsyncIndex: thin plugin; only implements base class interface.
// ---------------------------------------------------------------------------

class ObCSPluginAsyncIndex : public ObCSPlugin
{
public:
  ObCSPluginAsyncIndex();
  virtual ~ObCSPluginAsyncIndex();

  virtual int init() override;
  virtual void destroy() override;
  virtual int process(common::ObIArray<ObCSRow> &rows, ObCSExecCtx &ctx) override;
  virtual int commit() override;

private:
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObCSPluginAsyncIndex);
};

ObCSPlugin *create_async_index_plugin();

}  // namespace share
}  // namespace oceanbase

#endif  // OB_CS_PLUGIN_ASYNC_INDEX_H_
