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

#ifndef OCEANBASE_DML_OB_TABLE_INSERT_UP_OP_H_
#define OCEANBASE_DML_OB_TABLE_INSERT_UP_OP_H_

#include "sql/engine/dml/ob_table_modify_op.h"
#include "sql/engine/dml/ob_conflict_checker.h"

namespace oceanbase
{
namespace sql
{
class ObTableInsertUpOpInput : public ObTableModifyOpInput
{
  OB_UNIS_VERSION_V(1);
public:
  ObTableInsertUpOpInput(ObExecContext &ctx, const ObOpSpec &spec)
      : ObTableModifyOpInput(ctx, spec)
  {
  }
  virtual int init(ObTaskInfo &task_info) override
  {
    return ObTableModifyOpInput::init(task_info);
  }
};

class ObTableInsertUpSpec : public ObTableModifySpec
{
  OB_UNIS_VERSION_V(1);
  typedef common::ObArrayWrap<ObInsertUpCtDef*> InsUpdCtDefArray;
public:
  ObTableInsertUpSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObTableModifySpec(alloc, type),
      insert_up_ctdefs_(),
      conflict_checker_ctdef_(alloc),
      all_saved_exprs_(alloc),
      has_global_unique_index_(false),
      ins_auto_inc_expr_(nullptr),
      upd_auto_inc_expr_(nullptr),
      alloc_(alloc)
  {
  }

  //This interface is only allowed to be used in a single-table DML operator,
  //it is invalid when multiple tables are modified in one DML operator
  virtual int get_single_dml_ctdef(const ObDMLBaseCtDef *&dml_ctdef) const override
  {
    int ret = common::OB_SUCCESS;
    if (OB_UNLIKELY(insert_up_ctdefs_.count() != 1)) {
      ret = common::OB_ERR_UNEXPECTED;
      SQL_ENG_LOG(WARN, "table ctdef is invalid", K(ret), K(insert_up_ctdefs_.count()));
    } else {
      dml_ctdef = insert_up_ctdefs_.at(0)->ins_ctdef_;
    }
    return ret;
  }

  InsUpdCtDefArray insert_up_ctdefs_;
  ObConflictCheckerCtdef conflict_checker_ctdef_;
  // insert_row + child_->output_
  ExprFixedArray all_saved_exprs_;
  bool has_global_unique_index_;
  ObExpr *ins_auto_inc_expr_;
  ObExpr *upd_auto_inc_expr_;
protected:
  common::ObIAllocator &alloc_;
};

class ObTableInsertUpOp : public ObTableModifyOp
{
public:
  ObTableInsertUpOp(ObExecContext &ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObTableModifyOp(ctx, spec, input),
      insert_rows_(0),
      upd_changed_rows_(0),
      found_rows_(0),
      upd_rtctx_(eval_ctx_, ctx, *this),
      conflict_checker_(ctx.get_allocator(),
                        eval_ctx_,
                        MY_SPEC.conflict_checker_ctdef_),
      insert_up_row_store_("InsertUpRow"),
      is_ignore_(false),
      gts_state_(WITHOUT_GTS_OPT_STATE),
      has_guarantee_last_insert_id_(false)
  {
  }

  virtual ~ObTableInsertUpOp() {}

  virtual int inner_open() override;
  virtual int inner_rescan() override;
  virtual int inner_close() override;
  virtual int inner_get_next_row() override;

  int calc_auto_increment(const ObUpdCtDef &upd_ctdef);

  int update_auto_increment(const ObExpr &expr,
                            const uint64_t cid);
  // Execute all attempted insert das tasks, fetch primary key of the main table for conflicting rows
  int fetch_conflict_rowkey(int64_t row_cnt);
  int get_next_conflict_rowkey(DASTaskIter &task_iter);

  virtual void destroy()
  {
    // destroy
    conflict_checker_.destroy();
    insert_up_rtdefs_.release_array();
    insert_up_row_store_.~ObChunkDatumStore();
    upd_rtctx_.cleanup();
    ObTableModifyOp::destroy();
  }

protected:
  // Materialize all rows to be replace into to replace_row_store_
  int load_batch_insert_up_rows(bool &is_iter_end, int64_t &insert_rows);

  int get_next_row_from_child();

  int do_insert_up();

  int rollback_savepoint(const transaction::ObTxSEQ &savepoint_no);
  // Check for duplicated key error occurrence
  bool check_is_duplicated();
  // Traverse all rows in replace_row_store_ and update the conflict map,
  int do_insert_up_cache();

  int set_heap_table_new_pk(const ObUpdCtDef &upd_ctdef,
                            ObUpdRtDef &upd_rtdef);
  // Traverse the hash map of the main table, determine the final submitted delete + update + insert das_tasks
  // In submission order, delete must be submitted first
  int prepare_final_insert_up_task();

  int lock_one_row_to_das(const ObUpdCtDef &upd_ctdef,
                          ObUpdRtDef &upd_rtdef,
                          const ObDASTabletLoc *tablet_loc);

  int insert_row_to_das();

  int delete_upd_old_row_to_das();

  int insert_upd_new_row_to_das();

  int delete_one_upd_old_row_das(const ObUpdCtDef &upd_ctdef,
                                 ObUpdRtDef &upd_rtdef,
                                 const ObDASTabletLoc *tablet_loc);

  int insert_one_upd_new_row_das(const ObUpdCtDef &upd_ctdef,
                                 ObUpdRtDef &upd_rtdef,
                                 const ObDASTabletLoc *tablet_loc);

  int insert_row_to_das(const ObInsCtDef &ins_ctdef,
                        ObInsRtDef &ins_rtdef,
                        const ObDASTabletLoc *tablet_loc);

  int insert_row_to_das(const ObInsCtDef &ins_ctdef,
                                         ObInsRtDef &ins_rtdef,
                                         const ObDASTabletLoc *tablet_loc,
                                         ObDMLModifyRowNode &modify_row);

  int calc_update_tablet_loc(const ObUpdCtDef &upd_ctdef,
                             ObUpdRtDef &upd_rtdef,
                             ObDASTabletLoc *&old_tablet_loc,
                             ObDASTabletLoc *&new_tablet_loc);

  int calc_update_multi_tablet_id(const ObUpdCtDef &upd_ctdef,
                                  ObExpr &part_id_expr,
                                  common::ObTabletID &tablet_id);
  // Actually here it is calculating the old_row's pkey of update
  int calc_upd_old_row_tablet_loc(const ObUpdCtDef &upd_ctdef,
                                  ObUpdRtDef &upd_rtdef,
                                  ObDASTabletLoc *&tablet_loc);

  int calc_upd_new_row_tablet_loc(const ObUpdCtDef &upd_ctdef,
                                  ObUpdRtDef &upd_rtdef,
                                  ObDASTabletLoc *&tablet_loc);
  // TODO @kaizhan.dkz This function will be deleted later
  int calc_insert_tablet_loc(const ObInsCtDef &ins_ctdef,
                             ObInsRtDef &ins_rtdef,
                             ObDASTabletLoc *&tablet_loc);

  int do_update(const ObConflictValue &constraint_value);

  int do_lock(const ObConflictValue &constraint_value);

  int do_insert(const ObConflictValue &constraint_value);

  int do_update_with_ignore();
  // Submit all current das tasks;
  int post_all_dml_das_task(ObDMLRtCtx &das_ctx);
  int post_all_try_insert_das_task(ObDMLRtCtx &dml_rtctx);
  // batch execution insert process_row and then write to das,
  int try_insert_row(bool &is_skipped);
  // batch execution insert process_row and then write to das,
  int update_row_to_das(bool need_do_trigger);

  int inner_open_with_das();

  int init_insert_up_rtdef();

  int deal_hint_part_selection(ObObjectID partition_id);
  virtual int check_need_exec_single_row() override;
  virtual ObDasParallelType check_das_parallel_type() override;

  void guarantee_session_last_insert_id() { has_guarantee_last_insert_id_ = true; }
  int record_session_last_insert_id();
  int record_stmt_last_update_id();
private:
  int check_insert_up_ctdefs_valid() const;

  const ObIArray<ObExpr *> &get_primary_table_insert_row();


  const ObIArray<ObExpr *> &get_primary_table_upd_new_row();

  const ObIArray<ObExpr *> &get_primary_table_upd_old_row();

  int reset_das_env();

  void add_need_conflict_result_flag();

  int reuse();

protected:
  const static int64_t OB_DEFAULT_INSERT_UP_MEMORY_LIMIT = 2 * 1024 * 1024L;  // 2M in default
  const static int64_t OB_DEFAULT_INSERT_UP_BATCH_ROW_COUNT = 1000L;  // 1000 in default
  int64_t insert_rows_;
  int64_t upd_changed_rows_;
  int64_t found_rows_;
  ObDMLRtCtx upd_rtctx_;
  ObConflictChecker conflict_checker_;
  common::ObArrayWrap<ObInsertUpRtDef> insert_up_rtdefs_;
  ObChunkDatumStore insert_up_row_store_; // All the rows of insert_up collection
  bool is_ignore_; // temporarily record whether it is an ignore insert_up SQL statement
  ObDmlGTSOptState gts_state_;
  bool has_guarantee_last_insert_id_;
};
} // end namespace sql
} // end namespace oceanbase
#endif // OCEANBASE_DML_OB_TABLE_INSERT_UP_OP_H_
