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

#ifndef OBDEV_SRC_SQL_ENGINE_DML_OB_CONFLICT_ROW_CHECKER_H_
#define OBDEV_SRC_SQL_ENGINE_DML_OB_CONFLICT_ROW_CHECKER_H_
#include "sql/engine/basic/ob_chunk_datum_store.h"
#include "sql/das/ob_das_scan_op.h"
#include "sql/das/ob_das_attach_define.h"
#include "sql/engine/dml/ob_dml_ctx_define.h"

namespace oceanbase
{
namespace sql
{
class ObTableModifyOp;
struct ObDASScanCtDef;
struct ObDASScanRtDef;

class ObTabletSnapshotMaping
{
public:
  ObTabletSnapshotMaping()
    : snapshot_(),
      tablet_id_(),
      ls_id_()
  {}
  ~ObTabletSnapshotMaping() = default;
  int assign(const ObTabletSnapshotMaping &other);
  bool operator==(const ObTabletSnapshotMaping &other) const;
  TO_STRING_KV(K_(snapshot), K_(tablet_id), K_(ls_id));
  transaction::ObTxReadSnapshot snapshot_;
  common::ObTabletID tablet_id_;
  share::ObLSID ls_id_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTabletSnapshotMaping);
};

struct ObRowkeyCstCtdef
{
  OB_UNIS_VERSION_V(1);
public:
  ObRowkeyCstCtdef(common::ObIAllocator &alloc)
    :  constraint_name_(),
       rowkey_expr_(alloc),
       calc_exprs_(alloc),
       rowkey_accuracys_(alloc)
  {
  }
  virtual ~ObRowkeyCstCtdef() = default;
  TO_STRING_KV(K_(constraint_name),
               K_(rowkey_expr),
               K_(rowkey_accuracys),
               K_(calc_exprs));
  ObString constraint_name_;  // Print table name when conflict
  ExprFixedArray rowkey_expr_; // Primary key of the index table
  ExprFixedArray calc_exprs_; // Calculate gradually information dependent expressions
  AccuracyFixedArray rowkey_accuracys_;
};

enum ObNewRowSource
{
  //can not adjust the order of DASOpType, append OpType at the last
  FROM_SCAN = 0,
  FROM_INSERT,
  FROM_UPDATE,
  NEED_DO_LOCK
};

struct ObConflictValue
{
  ObConflictValue()
    : baseline_datum_row_(NULL),
      current_datum_row_(NULL),
      new_row_source_(ObNewRowSource::FROM_SCAN)
  {}
  // When performing check_duplicate_rowkey, it will remove duplicate rows (add_var_to_array_no_dup)
  // Need to use this operator
  bool operator==(const ObConflictValue &other) const;
  TO_STRING_KV(KPC_(baseline_datum_row), KPC_(current_datum_row), K_(new_row_source));
  const ObChunkDatumStore::StoredRow *baseline_datum_row_;
  const ObChunkDatumStore::StoredRow *current_datum_row_;
  ObNewRowSource new_row_source_;
};

typedef common::hash::ObHashMap<ObRowkey,
    ObConflictValue, common::hash::NoPthreadDefendMode> ObConflictRowMap;
class ObConflictRowMapCtx
{
public:
  ObConflictRowMapCtx()
    : conflict_map_(),
      rowkey_(NULL),
      allocator_(nullptr)
  {
  }
  ~ObConflictRowMapCtx() {};

  int init_conflict_map(int64_t bucket_num, int64_t obj_cnt, common::ObIAllocator *allocator);
  int reuse();
  int destroy();

public:
  static const int64_t MAX_ROW_BATCH_SIZE = 1024 * 1024;
  ObConflictRowMap conflict_map_;
  ObRowkey *rowkey_; // temporary ObRowkey, used for map compare, reused in loop
  common::ObIAllocator *allocator_; // allocator used to create hash map
};

typedef common::ObFixedArray<ObRowkeyCstCtdef *, common::ObIAllocator> ObRowkeyCstCtdefArray;
class ObConflictCheckerCtdef
{
  OB_UNIS_VERSION_V(1);
public:
  ObConflictCheckerCtdef(common::ObIAllocator &alloc)
    : cst_ctdefs_(alloc),
      calc_part_id_expr_(NULL),
      part_id_dep_exprs_(alloc),
      das_scan_ctdef_(alloc),
      partition_cnt_(0),
      data_table_rowkey_expr_(alloc),
      table_column_exprs_(alloc),
      use_dist_das_(false),
      rowkey_count_(0),
      attach_spec_(alloc, &das_scan_ctdef_),
      alloc_(alloc)
  {}
  virtual ~ObConflictCheckerCtdef() = default;
  TO_STRING_KV(K_(cst_ctdefs), K_(das_scan_ctdef), KPC_(calc_part_id_expr), K_(attach_spec));
  static const int64_t MIN_ROW_COUNT_USE_HASHSET_DO_DISTICT = 50;
  // must constraint_infos_.count() == conflict_map_array_.count()
  // constraint_infos_ used to generate ObConflictRowMap key
  ObRowkeyCstCtdefArray cst_ctdefs_;
  ObExpr *calc_part_id_expr_; // Non-dist plan is NULL
  // calc_part_id_expr_calculate the dependent expression, used for clear_eval_flag
  ExprFixedArray part_id_dep_exprs_;
  // Back-table query, to maintain structural consistency, the main table also needs a back-table operation, the first insert returns the primary key of the main table
  ObDASScanCtDef das_scan_ctdef_;
  int64_t partition_cnt_;
  // Primary key of the main table, used for building scan_range when back-table scanning
  ExprFixedArray data_table_rowkey_expr_;
  // Main table's all_columns
  ExprFixedArray table_column_exprs_;
  bool use_dist_das_;
  int64_t rowkey_count_;
  ObDASAttachSpec attach_spec_;
  common::ObIAllocator &alloc_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConflictCheckerCtdef);
};

struct ObConflictRange {
  ObConflictRange()
    : rowkey_(),
      tablet_id_()
  {
  }
  ~ObConflictRange() {};

  void init_conflict_range(const ObRowkey &rowkey, ObTabletID &tablet_id)
  {
    rowkey_ = rowkey;
    tablet_id_ = tablet_id;
  }

  int hash(uint64_t &hash_val) const
  {
    hash_val = hash();
    return OB_SUCCESS;
  }

  inline uint64_t hash() const
  {
    uint64_t hash_val = 0;
    hash_val += rowkey_.hash();
    hash_val += tablet_id_.hash();
    return hash_val;
  }
  bool is_valid() const
  {
    return rowkey_.is_valid() && tablet_id_.is_valid();
  }
  int assign(const ObConflictRange &conflict_range);
  bool operator==(const ObConflictRange &that) const
  {
    return rowkey_ == that.rowkey_ && tablet_id_ == that.tablet_id_;
  }
  TO_STRING_KV(K_(rowkey), K_(tablet_id));
  ObRowkey rowkey_;
  ObTabletID tablet_id_;
};

typedef common::hash::ObHashSet<ObConflictRange, common::hash::NoPthreadDefendMode> ConflictRangeDistCtx;

class ObConflictChecker
{
public:
  ObConflictChecker(common::ObIAllocator &allocator,
                    ObEvalCtx &eval_ctx,
                    const ObConflictCheckerCtdef &checker_ctdef);
  ~ObConflictChecker() {};
  // Initial conflict_checker
  int init_conflict_checker(const ObExprFrameInfo *expr_frame_info,
                            ObDASTableLoc *table_loc,
                            bool use_partition_gts_opt);
  void set_local_tablet_loc(ObDASTabletLoc *tablet_loc) { local_tablet_loc_ = tablet_loc; }
  // Initial conflict_map
  int create_conflict_map(int64_t replace_row_cnt);

  int create_rowkey_check_hashset(int64_t replace_row_cnt);
  // Check if the current primary key conflicts
  int check_duplicate_rowkey(const ObChunkDatumStore::StoredRow *replace_row,
                             ObIArray<ObConflictValue> &constraint_values,
                             bool is_insert_up);
  // Remove conflicting rows from hash map
  int delete_old_row(const ObChunkDatumStore::StoredRow *replace_row,
                     ObNewRowSource from);
  // Insert new line into hash map
  int insert_new_row(const ObChunkDatumStore::StoredRow *new_row,
                     ObNewRowSource from);

  int update_row(const ObChunkDatumStore::StoredRow *new_row,
                 const ObChunkDatumStore::StoredRow *old_row);

  int lock_row(const ObChunkDatumStore::StoredRow *lock_row);

  int convert_exprs_to_stored_row(const ObExprPtrIArray &exprs,
                                  ObChunkDatumStore::StoredRow *&new_row);
  // todo @kaizhan.dkz Here we can replace char *buf and int64_t buf_len with ObString
  int extract_rowkey_info(const ObRowkeyCstCtdef *constraint_info,
                          char *buf,
                          int64_t buf_len);
  // This function is similar to shuffle_final_data, only here it just returns a pointer to the hash map of the main table, the outer function iterates over the map, inserting rows into the corresponding das task
  int get_primary_table_map(ObConflictRowMap *&primary_map);
  // Use the data pulled back from the table to build the map
  int build_base_conflict_map(
      int64_t replace_row_cnt,
      const ObChunkDatumStore::StoredRow *conflict_row);
  // Back to the main table for lookup, query all corresponding conflict rows in the main table based on the primary key of the conflicting row, and build the conflict map
  int do_lookup_and_build_base_map(int64_t replace_row_cnt);

  int post_all_das_scan_tasks();
  // todo @kaizhan.dkz build the das scan task for back-table lookup
  int build_primary_table_lookup_das_task();

  int add_lookup_range_no_dup(storage::ObTableScanParam &scan_param,
                              ObNewRange &lookup_range,
                              common::ObTabletID &tablet_id);
  // will be called by the operator's inner_close function
  int close();

  int reuse();

  int collect_all_snapshot(transaction::ObTxReadSnapshot &snapshot, const ObDASTabletLoc *tablet_loc);
  int get_snapshot_by_ids(ObTabletID tablet_id, share::ObLSID ls_id, bool &founded, transaction::ObTxReadSnapshot &snapshot);

  int set_partition_snapshot_for_das_task(ObDASRef &das_ref);

  int destroy();

private:
  int to_expr(const ObChunkDatumStore::StoredRow *replace_row);
  int calc_lookup_tablet_loc(ObDASTabletLoc *&tablet_loc);
  // get current row corresponding scan_op
  int get_das_scan_op(ObDASTabletLoc *tablet_loc, ObDASScanOp *&das_scan_op);
  // Build the range information for back-table lookup
  int build_data_table_range(ObNewRange &lookup_range, ObRowkey &table_rowkey);

  // --------------------------
  int get_next_row_from_data_table(DASOpResultIter &result_iter,
                                   ObChunkDatumStore::StoredRow *&conflict_row);
  // Build ob_rowkey
  int build_rowkey(ObRowkey *&rowkey, ObRowkeyCstCtdef *rowkey_cst_ctdef);
  // Build ob_rowkey
  int build_tmp_rowkey(ObRowkey *rowkey, ObRowkeyCstCtdef *rowkey_info);

  int init_das_scan_rtdef();
  int init_attach_scan_rtdef(const ObDASBaseCtDef *attach_ctdef, ObDASBaseRtDef *&attach_rtdef);
  int attach_related_taskinfo(ObDASScanOp &target_op, ObDASBaseRtDef *attach_rtdef);

  int get_tmp_string_buffer(common::ObIAllocator *&allocator);
public:
  static const int64_t MAX_ROWKEY_CHECKER_DISTINCT_BUCKET_NUM = 1 * 1024 * 1024;
  common::ObArrayWrap<ObConflictRowMapCtx> conflict_map_array_;
  ObEvalCtx &eval_ctx_; // used for expression evaluation
  const ObConflictCheckerCtdef &checker_ctdef_;
  ObDASScanRtDef das_scan_rtdef_;
  ObDASAttachRtInfo *attach_rtinfo_;
  // allocator is used to create hash map, this is the allocator inside ObExecContext and cannot be reused
  common::ObIAllocator &allocator_;
  // das_scan back table use
  // This das_ref must be careful with reuse and reset,
  // because its internal allocator is used in many places
  ObDASRef das_ref_;
  ObDASTabletLoc *local_tablet_loc_;
  ObDASTableLoc *table_loc_;
  lib::MemoryContext tmp_mem_ctx_;
  ObSEArray<ObTabletSnapshotMaping, 16> snapshot_maping_;
  ConflictRangeDistCtx *conflict_range_dist_ctx_;
};
}  // namespace sql
}  // namespace oceanbase
#endif /* OBDEV_SRC_SQL_ENGINE_DML_OB_CONFLICT_ROW_CHECKER_H_ */
