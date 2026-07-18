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

#ifndef OCEANBASE_SQL_OB_TABLE_REPLACE_OP_
#define OCEANBASE_SQL_OB_TABLE_REPLACE_OP_

#include "sql/engine/dml/ob_table_modify_op.h"
#include "sql/engine/dml/ob_conflict_checker.h"

namespace oceanbase
{
namespace sql
{

class ObTableReplaceSpec : public ObTableModifySpec
{
  OB_UNIS_VERSION_V(1);
  typedef common::ObArrayWrap<ObReplaceCtDef*> ReplaceCtDefArray;
public:
  ObTableReplaceSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObTableModifySpec(alloc, type),
      replace_ctdefs_(),
      only_one_unique_key_(false),
      conflict_checker_ctdef_(alloc),
      has_global_unique_index_(false),
      all_saved_exprs_(alloc),
      doc_id_col_id_(OB_INVALID_ID),
      alloc_(alloc)
  {
  }

  //This interface is only allowed to be used in a single-table DML operator,
  //it is invalid when multiple tables are modified in one DML operator
  virtual int get_single_dml_ctdef(const ObDMLBaseCtDef *&dml_ctdef) const override
  {
    int ret = common::OB_SUCCESS;
    if (OB_UNLIKELY(replace_ctdefs_.count() != 1)) {
      ret = common::OB_ERR_UNEXPECTED;
      SQL_ENG_LOG(WARN, "table ctdef is invalid", K(ret), K(replace_ctdefs_.count()));
    } else {
      dml_ctdef = replace_ctdefs_.at(0)->ins_ctdef_;
    }
    return ret;
  }

  ReplaceCtDefArray replace_ctdefs_;
  bool only_one_unique_key_;
  ObConflictCheckerCtdef conflict_checker_ctdef_;
  bool has_global_unique_index_;
  // insert_row(new_row) + dependency child_output
  ExprFixedArray all_saved_exprs_;
  uint64_t doc_id_col_id_;
protected:
  common::ObIAllocator &alloc_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTableReplaceSpec);
};

class ObTableReplaceOp : public ObTableModifyOp
{
public:
  ObTableReplaceOp(ObExecContext &ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObTableModifyOp(ctx, spec, input),
      insert_rows_(0),
      delete_rows_(0),
      conflict_checker_(ctx.get_allocator(),
                        eval_ctx_,
                        MY_SPEC.conflict_checker_ctdef_),
      replace_row_store_("ReplaceRow"),
      replace_rtctx_(eval_ctx_, ctx, *this),
      gts_state_(WITHOUT_GTS_OPT_STATE)
  {}
  virtual ~ObTableReplaceOp() {}

  virtual int inner_open() override;
  virtual int inner_rescan() override;
  virtual int inner_close() override;
  virtual int inner_get_next_row() override;
  // Materialize all rows to be replace into to replace_row_store_
  int load_all_replace_row(bool &is_iter_end);
  int get_next_row_from_child();
  // Execute all attempted insert das tasks, fetch primary key of the main table for conflicting rows
  int fetch_conflict_rowkey(int64_t replace_row_cnt);
  int get_next_conflict_rowkey(DASTaskIter &task_iter);

  virtual void destroy()
  {
    replace_rtctx_.cleanup();
    conflict_checker_.destroy();
    replace_rtdefs_.release_array();
    replace_row_store_.~ObChunkDatumStore();
    ObTableModifyOp::destroy();
  }

protected:

  int do_replace_into();

  int rollback_savepoint(const transaction::ObTxSEQ &savepoint_no);
  // Check for duplicated key error occurrence
  bool check_is_duplicated();
  // Traverse all rows in replace_row_store_ and update the conflict map,
  int replace_conflict_row_cache();
  // Traverse the hash map of the main table, determine the last submitted delete + insert corresponding das task
  // In submission order, delete must be submitted first
  int prepare_final_replace_task();

  int do_delete(ObConflictRowMap *primary_map);

  int do_insert(ObConflictRowMap *primary_map);
  // Submit all the try_insert das tasks;
  int post_all_try_insert_das_task(ObDMLRtCtx &dml_rtctx);
  // Submit all insert and delete das tasks;
  int post_all_dml_das_task(ObDMLRtCtx &dml_rtctx);
  // batch execution insert process_row and then write to das,
  int insert_row_to_das(bool &is_skipped);

  int final_insert_row_to_das();
  // batch execution insert process_row and then write to das,
  int delete_row_to_das(bool need_do_trigger);

  int calc_insert_tablet_loc(const ObInsCtDef &ins_ctdef,
                             ObInsRtDef &ins_rtdef,
                             ObDASTabletLoc *&tablet_loc);
  int calc_delete_tablet_loc(const ObDelCtDef &del_ctdef,
                             ObDelRtDef &del_rtdef,
                             ObDASTabletLoc *&tablet_loc);

  int inner_open_with_das();

  int init_replace_rtdef();

  int check_values(bool &is_equal,
                   const ObChunkDatumStore::StoredRow *replace_row,
                   const ObChunkDatumStore::StoredRow *delete_row);
  virtual int check_need_exec_single_row() override;
  virtual ObDasParallelType check_das_parallel_type() override;
private:
  int check_replace_ctdefs_valid() const;

  const ObIArray<ObExpr *> &get_all_saved_exprs();

  const ObIArray<ObExpr *> &get_primary_table_new_row();

  const ObIArray<ObExpr *> &get_primary_table_old_row();

  int reset_das_env();

  void add_need_conflict_result_flag();

  int reuse();

protected:
  const static int64_t DEFAULT_REPLACE_BATCH_ROW_COUNT = 1000L;
  const static int64_t OB_DEFAULT_REPLACE_MEMORY_LIMIT = 2 * 1024 * 1024L;

  int64_t insert_rows_;
  int64_t delete_rows_;
  ObConflictChecker conflict_checker_;
  common::ObArrayWrap<ObReplaceRtDef> replace_rtdefs_;
  ObChunkDatumStore replace_row_store_; // All the rows of replace operations
  ObDMLRtCtx replace_rtctx_;
  ObDmlGTSOptState gts_state_;
};

class ObTableReplaceOpInput : public ObTableModifyOpInput
{
  OB_UNIS_VERSION_V(1);
public:
  ObTableReplaceOpInput(ObExecContext &ctx, const ObOpSpec &spec)
    : ObTableModifyOpInput(ctx, spec)
  {}
  int init(ObTaskInfo &task_info) override
  {
    return ObTableModifyOpInput::init(task_info);
  }
private:
  DISALLOW_COPY_AND_ASSIGN(ObTableReplaceOpInput);
};

} // end namespace sql
} // end namespace oceanbase

#endif
