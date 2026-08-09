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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/dml/ob_table_delete_op.h"
#include "sql/engine/dml/ob_dml_service.h"

namespace oceanbase
{
namespace sql
{

OB_SERIALIZE_MEMBER((ObTableDeleteOpInput, ObTableModifyOpInput));

OB_DEF_SERIALIZE(ObTableDeleteSpec)
{
  int ret = OB_SUCCESS;
  int64_t tbl_cnt = del_ctdefs_.count();
  BASE_SER((ObTableDeleteSpec, ObTableModifySpec));
  OB_UNIS_ENCODE(tbl_cnt);
  for (int64_t i = 0; OB_SUCC(ret) && i < tbl_cnt; ++i) {
    int64_t idx_cnt = del_ctdefs_.at(i).count();
    OB_UNIS_ENCODE(idx_cnt);
    for (int64_t j = 0; OB_SUCC(ret) && j < idx_cnt; ++j) {
      ObDelCtDef *del_ctdef = del_ctdefs_.at(i).at(j);
      if (OB_ISNULL(del_ctdef)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("del_ctdef is nullptr", K(ret));
      }
      OB_UNIS_ENCODE(*del_ctdef);
    }
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObTableDeleteSpec)
{
  int ret = OB_SUCCESS;
  int64_t tbl_cnt = 0;
  BASE_DESER((ObTableDeleteSpec, ObTableModifySpec));
  OB_UNIS_DECODE(tbl_cnt);
  if (OB_SUCC(ret) && tbl_cnt > 0) {
    OZ(del_ctdefs_.allocate_array(alloc_, tbl_cnt));
  }
  ObDMLCtDefAllocator<ObDelCtDef> del_ctdef_allocator(alloc_);
  for (int64_t i = 0; OB_SUCC(ret) && i < tbl_cnt; ++i) {
    int64_t index_cnt = 0;
    OB_UNIS_DECODE(index_cnt);
    OZ(del_ctdefs_.at(i).allocate_array(alloc_, index_cnt));
    for (int64_t j = 0; OB_SUCC(ret) && j < index_cnt; ++j) {
      ObDelCtDef *del_ctdef = del_ctdef_allocator.alloc();
      if (OB_ISNULL(del_ctdef)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc del_ctdef failed", K(ret));
      }
      OB_UNIS_DECODE(*del_ctdef);
      del_ctdefs_.at(i).at(j) = del_ctdef;
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObTableDeleteSpec)
{
  int64_t len = 0;
  int64_t tbl_cnt = del_ctdefs_.count();
  BASE_ADD_LEN((ObTableDeleteSpec, ObTableModifySpec));
  OB_UNIS_ADD_LEN(tbl_cnt);
  for (int64_t i = 0; i < tbl_cnt; ++i) {
    int64_t index_cnt = del_ctdefs_.at(i).count();
    OB_UNIS_ADD_LEN(index_cnt);
    for (int64_t j = 0; j < index_cnt; ++j) {
      ObDelCtDef *del_ctdef = del_ctdefs_.at(i).at(j);
      if (del_ctdef != nullptr) {
        OB_UNIS_ADD_LEN(*del_ctdef);
      }
    }
  }
  return len;
}

int ObTableDeleteOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObTableModifyOp::inner_rescan())) {
  } else if (OB_UNLIKELY(iter_end_)) {
    //do nothing
  } else if (OB_FAIL(close_table_for_each())) {
  } else if (OB_FAIL(open_table_for_each())) {
  }
  return ret;
}

int ObTableDeleteOp::inner_open()
{
  int ret = OB_SUCCESS;
  //execute delete with das
  if (OB_FAIL(ObTableModifyOp::inner_open())) {
  } else if (OB_UNLIKELY(MY_SPEC.del_ctdefs_.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("del ctdef is invalid", K(ret), KP(this));
  } else if (OB_UNLIKELY(iter_end_)) {
    //do nothing
  } else if (OB_FAIL(inner_open_with_das())) {
  }
  return ret;
}

OB_INLINE int ObTableDeleteOp::inner_open_with_das()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(open_table_for_each())) {
  }
  return ret;
}

int ObTableDeleteOp::check_need_exec_single_row() 
{
  int ret = OB_SUCCESS;
  ret = ObTableModifyOp::check_need_exec_single_row();
  if (OB_SUCC(ret) && !execute_single_row_) {
    for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.del_ctdefs_.count() && !execute_single_row_; ++i) {
      const ObTableDeleteSpec::DelCtDefArray &ctdefs = MY_SPEC.del_ctdefs_.at(i);
      const ObDelCtDef &del_ctdef = *ctdefs.at(0);
      for (int64_t j = 0;
          OB_SUCC(ret) && !execute_single_row_ && j < del_ctdef.trig_ctdef_.tg_args_.count();
          ++j) {
        const ObTriggerArg &tri_arg = del_ctdef.trig_ctdef_.tg_args_.at(j);
        execute_single_row_ = tri_arg.is_execute_single_row();
      }
      const ObForeignKeyArgArray &fk_args = del_ctdef.fk_args_;
      for (int j = 0; OB_SUCC(ret) && j < fk_args.count() && !execute_single_row_; j++) {
        if (fk_args.at(j).is_self_ref_ && fk_args.at(j).ref_action_ == ACTION_CASCADE) {
          execute_single_row_ = true;
        }
      }
    }
  }
  return ret;
}

OB_INLINE int ObTableDeleteOp::open_table_for_each()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(del_rtdefs_.allocate_array(ctx_.get_allocator(), MY_SPEC.del_ctdefs_.count()))) {
  }
  fk_checkers_.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < del_rtdefs_.count(); ++i) {
    DelRtDefArray &rtdefs = del_rtdefs_.at(i);
    const ObTableDeleteSpec::DelCtDefArray &ctdefs = MY_SPEC.del_ctdefs_.at(i);
    if (OB_FAIL(rtdefs.allocate_array(ctx_.get_allocator(), MY_SPEC.del_ctdefs_.at(i).count()))) {
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < del_rtdefs_.at(i).count(); ++j) {
      ObDelRtDef &del_rtdef = rtdefs.at(j);
      const ObDelCtDef &del_ctdef = *ctdefs.at(j);
      if (OB_FAIL(ObDMLService::init_del_rtdef(dml_rtctx_, del_rtdef, del_ctdef))) {
      }
    }
    if (OB_SUCC(ret) && !rtdefs.empty()) {
      ObDelRtDef &primary_del_rtdef = rtdefs.at(0);
      if (OB_SUCC(ret)) {
        //this table is being accessed by dml operator, mark its table location as writing
        primary_del_rtdef.das_rtdef_.table_loc_->is_writing_ = true;
      }
    }
  }
  return ret;
}

OB_INLINE int ObTableDeleteOp::close_table_for_each()
{
  int ret = OB_SUCCESS;
  if (OB_SUCCESS == ctx_.get_errcode()) {
    for (int64_t i = 0; OB_SUCC(ret) && i < del_rtdefs_.count(); ++i) {
      if (!del_rtdefs_.at(i).empty()) {
        ObDelRtDef &primary_del_rtdef = del_rtdefs_.at(i).at(0);
        if (OB_NOT_NULL(primary_del_rtdef.das_rtdef_.table_loc_)) {
          primary_del_rtdef.das_rtdef_.table_loc_->is_writing_ = false;
        }
      }
    }
  }
  //whether it is successful or not, needs to release rtdef
  for (int64_t i = 0; i < del_rtdefs_.count(); ++i) {
    del_rtdefs_.at(i).release_array();
  }
  return ret;
}

OB_INLINE int ObTableDeleteOp::calc_tablet_loc(const ObDelCtDef &del_ctdef,
                                               ObDelRtDef &del_rtdef,
                                               ObDASTabletLoc *&tablet_loc)
{
  int ret = OB_SUCCESS;
  if (MY_SPEC.use_dist_das_) {
    if (del_ctdef.multi_ctdef_ != nullptr) {
      ObExpr *calc_part_id_expr = del_ctdef.multi_ctdef_->calc_part_id_expr_;
      ObDASTableLoc &table_loc = *del_rtdef.das_rtdef_.table_loc_;
      ObObjectID partition_id = OB_INVALID_ID;
      ObTabletID tablet_id;
      if (OB_FAIL(ObExprCalcPartitionBase::calc_part_and_tablet_id(calc_part_id_expr, eval_ctx_, partition_id, tablet_id))) {
      } else if (OB_FAIL(DAS_CTX(ctx_).extended_tablet_loc(table_loc, tablet_id, tablet_loc))) {
      }
    }
  } else {
    //direct write delete row to storage
    tablet_loc = MY_INPUT.get_tablet_loc();
  }
  return ret;
}

int ObTableDeleteOp::write_row_to_das_buffer()
{
  int ret = OB_SUCCESS;
  ret = delete_row_to_das();
  return ret;
}

OB_INLINE int ObTableDeleteOp::delete_row_to_das()
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.del_ctdefs_.count(); ++i) {
    const ObTableDeleteSpec::DelCtDefArray &ctdefs = MY_SPEC.del_ctdefs_.at(i);
    DelRtDefArray &rtdefs = del_rtdefs_.at(i);
    bool is_skipped = false;
    for (int64_t j = 0; OB_SUCC(ret) && j < ctdefs.count(); ++j) {
      //delete each table with fetched row
      const ObDelCtDef &del_ctdef = *ctdefs.at(j);
      ObDelRtDef &del_rtdef = rtdefs.at(j);
      ObDASTabletLoc *tablet_loc = nullptr;
      ObDMLModifyRowNode modify_row(this, &del_ctdef, &del_rtdef, ObDmlEventType::DE_DELETING);
      bool is_skipped = false;
      if (OB_FAIL(ObDMLService::process_delete_row(del_ctdef, del_rtdef, is_skipped, *this))) {
      } else if (OB_UNLIKELY(is_skipped)) {
        //this row has been skipped, so can not write to DAS buffer(include its global index)
        //so need to break this loop
        break;
      } else if (OB_FAIL(calc_tablet_loc(del_ctdef, del_rtdef, tablet_loc))) {
      } else if (OB_FAIL(ObDMLService::delete_row(del_ctdef, del_rtdef, tablet_loc, dml_rtctx_, modify_row.old_row_))) {
      } else if (need_after_row_process(del_ctdef) && OB_FAIL(dml_modify_rows_.push_back(modify_row))) {
        LOG_WARN("failed to push dml modify row to modified row list", K(ret));
      } else {
        ++del_rtdef.cur_row_num_;
      }
    }
    if (OB_SUCC(ret)) {
      int64_t delete_rows = is_skipped ? 0 : 1;
      if (OB_FAIL(merge_implict_cursor(delete_rows, 0, 0, 0))) {
      }
    }
  }

  return ret;
}

int ObTableDeleteOp::write_rows_post_proc(int last_errno)
{
  int ret = last_errno;
  if (iter_end_) {
    ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(ctx_);
    for (int64_t i = 0; OB_SUCC(ret) && i < del_rtdefs_.count(); ++i) {
      plan_ctx->add_affected_rows(del_rtdefs_.at(i).at(0).das_rtdef_.affected_rows_);
      LOG_DEBUG("del rows post proc", K(plan_ctx->get_affected_rows()), K(del_rtdefs_.at(i).at(0)));
    }
    if (OB_SUCC(ret) && GCONF.enable_defensive_check()) {
      if (OB_FAIL(check_delete_affected_row())) {
      }
    }
  }
  return ret;
}

int ObTableDeleteOp::check_delete_affected_row()
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < del_rtdefs_.count(); ++i) {
    DelRtDefArray &del_rtdef_array = del_rtdefs_.at(i);
    const ObDelCtDef *primary_del_ctdef = MY_SPEC.del_ctdefs_.at(i).at(0);
    ObDelRtDef &primary_del_rtdef = del_rtdef_array.at(0);
    int64_t primary_write_rows = primary_del_rtdef.das_rtdef_.affected_rows_;
    for (int64_t j = 1; OB_SUCC(ret) && j < del_rtdef_array.count(); ++j) {
      const ObDelCtDef *index_del_ctdef = MY_SPEC.del_ctdefs_.at(i).at(j);
      ObDelRtDef &index_del_rtdef = del_rtdef_array.at(j);
      int64_t index_write_rows = index_del_rtdef.das_rtdef_.affected_rows_;
      if (primary_write_rows != index_write_rows) {
        ret = OB_ERR_DEFENSIVE_CHECK;
        ObString func_name = ObString::make_string("check_delete_affected_row");
        LOG_USER_ERROR(OB_ERR_DEFENSIVE_CHECK, func_name.length(), func_name.ptr());
        LOG_ERROR_RET(OB_ERR_DEFENSIVE_CHECK, "Fatal Error!!! data table delete affected row is not match with index table",
                  K(ret), K(primary_write_rows), K(index_write_rows), 
                  KPC(primary_del_ctdef), K(primary_del_rtdef),
                  KPC(index_del_ctdef), K(index_del_rtdef));
        LOG_DBA_ERROR_V2(OB_SQL_DELETE_AFFECTED_ROW_FAIL, ret, "Attention!!!", "data table delete affected row is not match with index table"
                  ", data table delete affected_rows is: ", primary_write_rows, ", index table delete affected_rows is: ", index_write_rows);
      }
    }
    if (OB_SUCC(ret)) {
      if (primary_del_rtdef.cur_row_num_ != primary_write_rows) {
        ret = OB_ERR_DEFENSIVE_CHECK;
        ObString func_name = ObString::make_string("check_delete_affected_row");
        LOG_USER_ERROR(OB_ERR_DEFENSIVE_CHECK, func_name.length(), func_name.ptr());
        LOG_ERROR_RET(OB_ERR_DEFENSIVE_CHECK, "Fatal Error!!! data table delete affected row is not match with found rows",
                  K(ret), K(primary_write_rows), K(primary_del_rtdef.cur_row_num_),
                  KPC(primary_del_ctdef), K(primary_del_rtdef));
        LOG_DBA_ERROR_V2(OB_SQL_DELETE_AFFECTED_ROW_FAIL, ret, "Attention!!!", "data table delete affected row is not match with index table"
                  ", data table delete affected row is: ", primary_write_rows,
                  ", data table found_rows is: ", primary_del_rtdef.cur_row_num_);
      }
    }
  }
  return ret;
}

int ObTableDeleteOp::inner_close()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(close_table_for_each())) {
  }
  int close_ret = ObTableModifyOp::inner_close();
  return (OB_SUCCESS == ret) ? close_ret : ret;
}
} // namespace sql
} // namespace oceanbase
