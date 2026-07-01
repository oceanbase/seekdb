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
#pragma once

#include "share/ob_tablet_autoincrement_param.h"
#include "share/schema/ob_table_param.h"
#include "share/table/ob_table_load_define.h"
#include "storage/direct_load/ob_direct_load_i_table.h"
#include "storage/direct_load/ob_direct_load_origin_table.h"
#include "storage/direct_load/ob_direct_load_table_data_desc.h"
#include "storage/direct_load/ob_direct_load_trans_param.h"
#include "storage/direct_load/ob_direct_load_i_merge_task.h"

namespace oceanbase
{
namespace common
{
class ObOptOSGColumnStat;
class ObOptTableStat;
} // namespace common
namespace observer
{
class ObTableLoadTableCtx;
class ObTableLoadDagParallelMerger;
} // namespace observer
namespace storage
{
class ObDirectLoadInsertTableContext;
class ObDirectLoadInsertTabletContext;
class ObDirectLoadTabletMergeCtx;
class ObDirectLoadMultipleMergeRangeSplitter;
class ObDirectLoadDMLRowHandler;
class ObIDirectLoadPartitionTableBuilder;
class ObDirectLoadTmpFileManager;
class ObDirectLoadTableStore;

// NORMAL : Directly construct imported data into sstable, currently used by DELETE_INSERT_ENGINE
// NORMAL_WITH_PK_DELETE_ROW : Directly construct imported data into sstable, DELETE rows only keep PK,
// MERGE_WITH_ORIGIN_DATA : Merge imported data with existing data, scenario: full+normal write data table
// MERGE_WITH_CONFLICT_CHECK : Perform conflict detection between imported data and existing data, insert conflicting rows into sstable
// incremental or inc_replace but table has lob or index MERGE_WITH_ORIGIN_QUERY_FOR_LOB : Currently used for del_lob,
// MERGE_WITH_ORIGIN_QUERY_FOR_DATA: Used for data table, query original table by rowkey, DELETE rows only keep primary key, insert into sstable after constructing complete row
// Query original table by lob_id
struct ObDirectLoadMergeMode
{
#define OB_DIRECT_LOAD_MERGE_MODE_DEF(DEF)                 \
  DEF(INVALID_MERGE_MODE, = 0)                             \
  DEF(NORMAL, )                                            \
  DEF(NORMAL_WITH_PK_DELETE_ROW, )                         \
  DEF(MERGE_WITH_ORIGIN_DATA, )                            \
  DEF(MERGE_WITH_CONFLICT_CHECK, )                         \
  DEF(MERGE_WITH_ORIGIN_QUERY_FOR_LOB, )                   \
  DEF(MERGE_WITH_ORIGIN_QUERY_FOR_DATA, )                  \
  DEF(MAX_MERGE_MODE, )

  DECLARE_ENUM(Type, type, OB_DIRECT_LOAD_MERGE_MODE_DEF, static);

  static bool is_type_valid(const Type type)
  {
    return type > INVALID_MERGE_MODE && type < MAX_MERGE_MODE;
  }
  static bool is_normal(const Type type) { return type == NORMAL; }
  static bool is_normal_with_pk_delete_row(const Type type) { return type == NORMAL_WITH_PK_DELETE_ROW; }
  static bool is_merge_with_origin_data(const Type type) { return type == MERGE_WITH_ORIGIN_DATA; }
  static bool is_merge_with_conflict_check(const Type type)
  {
    return type == MERGE_WITH_CONFLICT_CHECK;
  }
  static bool is_merge_with_origin_query_for_lob(const Type type)
  {
    return type == MERGE_WITH_ORIGIN_QUERY_FOR_LOB;
  }
  static bool is_merge_with_origin_query_for_data(const Type type)
  {
    return MERGE_WITH_ORIGIN_QUERY_FOR_DATA == type;
  }
  static bool merge_need_origin_table(const Type type)
  {
    return is_merge_with_origin_data(type) || is_merge_with_conflict_check(type) ||
           is_merge_with_origin_query_for_lob(type) || is_merge_with_origin_query_for_data(type);
  }
};

struct ObDirectLoadMergeParam
{
public:
  ObDirectLoadMergeParam();
  ~ObDirectLoadMergeParam();
  bool is_valid() const;
  TO_STRING_KV(K_(table_id), K_(rowkey_column_num), K_(column_count), KP_(col_descs),
               KP_(datum_utils), KP_(lob_column_idxs), "merge_mode",
               ObDirectLoadMergeMode::get_type_string(merge_mode_), K_(use_batch_mode),
               KP_(dml_row_handler), KP_(insert_table_ctx), K_(trans_param), KP_(file_mgr),
               KP_(ctx));

public:
  // The attributes of the table to be written in this merge, which may be a data table, index table, or lob table
  uint64_t table_id_; // origin table id
  int64_t rowkey_column_num_;
  int64_t column_count_;
  const common::ObIArray<share::schema::ObColDesc> *col_descs_;
  const blocksstable::ObStorageDatumUtils *datum_utils_;
  const common::ObArray<int64_t> *lob_column_idxs_;
  // Merge mode
  ObDirectLoadMergeMode::Type merge_mode_;
  bool use_batch_mode_;
  ObDirectLoadDMLRowHandler *dml_row_handler_; // rescan when it is nullptr
  ObDirectLoadInsertTableContext *insert_table_ctx_;
  // Task-level parameters
  ObDirectLoadTransParam trans_param_;
  ObDirectLoadTmpFileManager *file_mgr_;
  // TODO update progress information
  observer::ObTableLoadTableCtx *ctx_;
};

class ObDirectLoadMergeCtx
{
  friend class ObDirectLoadTabletMergeCtx;
  friend class ObDirectLoadMergeTaskIterator;
  friend class ObTableLoadDagParallelMerger;

public:
  ObDirectLoadMergeCtx();
  ~ObDirectLoadMergeCtx();
  void reset();
  int init(const ObDirectLoadMergeParam &param,
           const common::ObIArray<table::ObTableLoadLSIdAndPartitionId> &ls_partition_ids);
  int build_merge_task(ObDirectLoadTableStore &table_store, int64_t thread_cnt);
  int build_del_lob_task(ObDirectLoadTableStore &table_store, int64_t thread_cnt, const bool for_dag);
  int build_rescan_task(int64_t thread_cnt);
  const common::ObIArray<ObDirectLoadTabletMergeCtx *> &get_tablet_merge_ctxs() const
  {
    return tablet_merge_ctx_array_;
  }

private:
  int create_all_tablet_ctxs(
    const common::ObIArray<table::ObTableLoadLSIdAndPartitionId> &ls_partition_ids);

private:
  common::ObArenaAllocator allocator_;
  ObDirectLoadMergeParam param_;
  common::ObArray<ObDirectLoadTabletMergeCtx *> tablet_merge_ctx_array_;
  bool is_inited_;
};

class ObDirectLoadTabletMergeCtx final
{
  friend class ObDirectLoadMergeTaskIterator;

public:
  ObDirectLoadTabletMergeCtx();
  ~ObDirectLoadTabletMergeCtx();
  void reset();
  int init(ObDirectLoadMergeCtx *merge_ctx,
           const table::ObTableLoadLSIdAndPartitionId &ls_partition_id);
  // No import data merge task
  int build_empty_data_merge_task(const ObDirectLoadTableDataDesc &table_data_desc,
                                  int64_t max_parallel_degree);
  // Primary key data merge task, sort by partition
  int build_merge_task_for_sstable(const ObDirectLoadTableDataDesc &table_data_desc,
                                   const ObDirectLoadTableHandleArray &sstable_array,
                                   int64_t max_parallel_degree);
  // Primary key data merge task, partition mixed sort
  int build_merge_task_for_multiple_sstable(
    const ObDirectLoadTableDataDesc &table_data_desc,
    const ObDirectLoadTableHandleArray &multiple_sstable_array,
    ObDirectLoadMultipleMergeRangeSplitter &range_splitter, int64_t max_parallel_degree);
  // No primary key data sorting and merging task
  int build_merge_task_for_multiple_heap_table(
    const ObDirectLoadTableDataDesc &table_data_desc,
    const ObDirectLoadTableHandleArray &multiple_heap_table_array, int64_t max_parallel_degree);
  // Data aggregation and merge task without primary key, used for scenarios with many partitions, merging in parallel at the partition level
  int build_aggregate_merge_task_for_multiple_heap_table(
    const ObDirectLoadTableDataDesc &table_data_desc,
    const ObDirectLoadTableHandleArray &multiple_heap_table_array);

  // del lob task
  int build_del_lob_task(const ObDirectLoadTableDataDesc &table_data_desc,
                         const ObDirectLoadTableHandleArray &multiple_sstable_array,
                         ObDirectLoadMultipleMergeRangeSplitter &range_splitter,
                         const int64_t max_parallel_degree);
  int build_del_lob_task_for_dag(const ObDirectLoadTableDataDesc &table_data_desc,
                                 const ObDirectLoadTableHandleArray &multiple_sstable_array,
                                 ObDirectLoadMultipleMergeRangeSplitter &range_splitter,
                                 const int64_t max_parallel_degree);
  // rescan task
  int build_rescan_task(int64_t thread_count);

  int inc_finish_count(int ret_code, bool &is_ready);

  bool merge_with_origin_data() const
  {
    return (nullptr != param_ &&
            ObDirectLoadMergeMode::is_merge_with_origin_data(param_->merge_mode_));
  }
  bool merge_with_conflict_check() const
  {
    return (nullptr != param_ &&
            ObDirectLoadMergeMode::is_merge_with_conflict_check(param_->merge_mode_));
  }
  bool merge_with_origin_query_for_data() const
  {
    return (nullptr != param_ &&
            ObDirectLoadMergeMode::is_merge_with_origin_query_for_data(param_->merge_mode_));
  }
  bool is_valid() const { return is_inited_; }
  const ObDirectLoadMergeParam *get_param() const { return param_; }
  const common::ObTabletID &get_tablet_id() const { return tablet_id_; }
  const common::ObIArray<ObDirectLoadIMergeTask *> &get_merge_task_array() const
  {
    return merge_task_array_;
  }
  ObDirectLoadInsertTabletContext *get_insert_tablet_ctx() const { return insert_tablet_ctx_; }
  TO_STRING_KV(KP_(merge_ctx), KPC_(param), K_(tablet_id), KP_(insert_tablet_ctx), K_(range_array),
               K_(merge_task_array), K_(parallel_idx), K_(task_finish_cnt), K_(task_ret_code));

private:
  int build_empty_merge_task();
  int build_origin_data_merge_task(const ObDirectLoadTableDataDesc &table_data_desc,
                                   const int64_t max_parallel_degree);
  int get_autoincrement_value(uint64_t count, share::ObTabletCacheInterval &interval);

private:
  common::ObArenaAllocator allocator_;
  ObDirectLoadMergeCtx *merge_ctx_;
  ObDirectLoadMergeParam *param_;
  common::ObTabletID tablet_id_;
  ObDirectLoadInsertTabletContext *insert_tablet_ctx_;
  ObDirectLoadOriginTable origin_table_;
  common::ObArray<blocksstable::ObDatumRange> range_array_;
  common::ObArray<ObDirectLoadIMergeTask *> merge_task_array_;
  int64_t parallel_idx_;
  mutable lib::ObMutex mutex_;
  int64_t task_finish_cnt_ CACHE_ALIGNED;
  int task_ret_code_;
  bool is_inited_;
};

class ObDirectLoadMergeTaskIterator
{
public:
  ObDirectLoadMergeTaskIterator();
  ~ObDirectLoadMergeTaskIterator();
  int init(ObDirectLoadMergeCtx *merge_ctx);
  int get_next_task(ObDirectLoadIMergeTask *&task);
  TO_STRING_KV(KP_(merge_ctx), K(tablet_pos_), K(task_pos_), K(is_inited_));

private:
  ObDirectLoadMergeCtx *merge_ctx_;
  ObDirectLoadTabletMergeCtx *tablet_merge_ctx_;
  int64_t tablet_pos_;
  int64_t task_pos_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
