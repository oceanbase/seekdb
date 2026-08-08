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

#include "sql/engine/pdml/static/ob_px_sstable_insert_op.h"
#include "share/ob_autoincrement_service.h"
#include "data_plane/blocksstable/ob_datum_row.h"
#include "sql/engine/px/ob_px_sqc_handler.h"
#include "sql/engine/basic/ob_temp_column_spill_spool.h"
#include "data_plane/ddl/ob_ddl_seq_generator.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::storage;
using namespace oceanbase::blocksstable;
using namespace oceanbase::share;
using namespace oceanbase::common::serialization;


OB_SERIALIZE_MEMBER((ObPxMultiPartSSTableInsertOpInput, ObPxMultiPartModifyOpInput));

OB_SERIALIZE_MEMBER((ObPxMultiPartSSTableInsertSpec, ObPxMultiPartInsertSpec), snapshot_query_expr_,
                     regenerate_heap_table_pk_);

int ObPxMultiPartSSTableInsertSpec::get_snapshot_version(ObEvalCtx &eval_ctx, int64_t &snapshot_version) const
{
  int ret = OB_SUCCESS;
  ObDatum *datum = nullptr;
  snapshot_version = 0;
  if (OB_FAIL(snapshot_query_expr_->eval(eval_ctx, datum))) {
    LOG_WARN("expr evaluate failed", K(ret));
  } else if (datum->is_null()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL value", K(ret));
  } else {
    snapshot_version = datum->get_int();
  }
  return ret;
}

int ObPxMultiPartSSTableInsertOp::get_tablet_info_from_row(
    const ObExprPtrIArray &row,
    common::ObTabletID &tablet_id,
    storage::ObTabletSliceParam *tablet_slice_param)
{
  int ret = OB_SUCCESS;
  tablet_id.reset();
  if (nullptr != tablet_slice_param) {
    tablet_slice_param->reset();
  }

  // 1. get tablet_id
  const int64_t part_id_idx = get_spec().row_desc_.get_part_id_index();
  if (NO_PARTITION_ID_FLAG == part_id_idx) {
    ObDASTableLoc *table_loc = ins_rtdef_.das_rtdef_.table_loc_;
    if (OB_ISNULL(table_loc) || table_loc->get_tablet_locs().size() != 1) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("insert table location is invalid", K(ret), KPC(table_loc));
    } else {
      tablet_id = table_loc->get_first_tablet_loc()->tablet_id_;
    }
  } else if (part_id_idx < 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, part_id_idx is not valid", K(ret), K(part_id_idx));
  } else if (row.count() > part_id_idx) {
    ObExpr *expr = row.at(part_id_idx);
    ObDatum &expr_datum = expr->locate_expr_datum(get_eval_ctx());
    tablet_id = expr_datum.get_int();
  }

  // 2. get slice param
  if (OB_SUCC(ret) && nullptr != tablet_slice_param) {
    bool found_slice_expr = false;
    for (int64_t i = 0; OB_SUCC(ret) && !found_slice_expr && i < row.count(); ++i) {
      if (row.at(i)->type_ == ObItemType::T_PSEUDO_DDL_SLICE_ID) {
        ObDatum &expr_datum = row.at(i)->locate_expr_datum(get_eval_ctx());
        tablet_slice_param->slice_id_ = expr_datum.get_int();
        found_slice_expr = true;
      }
    }
    if (OB_SUCC(ret) && !found_slice_expr) {
      tablet_slice_param->slice_idx_ = ctx_.get_px_task_id();
    }
    if (OB_SUCC(ret) && tablet_slice_param->slice_idx_ >= ObTabletSliceParam::MAX_TABLET_SLICE_COUNT) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid ddl_slice_id", K(ret), KPC(tablet_slice_param));
    }
  }

  return ret;
}

const ObPxMultiPartSSTableInsertSpec &ObPxMultiPartSSTableInsertOp::get_spec() const
{
  return static_cast<const ObPxMultiPartSSTableInsertSpec &>(spec_);
}

int ObPxMultiPartSSTableInsertOp::inner_open()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObPxMultiPartInsertOp::inner_open())) {
    LOG_WARN("inner open failed", K(ret));
  } else if (OB_ISNULL(ctx_.get_sqc_handler())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sqc handler is null", K(ret));
  } else {
    op_monitor_info_.otherstat_2_id_ = ObSqlMonitorStatIds::SSTABLE_INSERT_ROW_COUNT;
    op_monitor_info_.otherstat_2_value_ = 0;
    op_monitor_info_.otherstat_5_id_ = ObSqlMonitorStatIds::DDL_TASK_ID;
    op_monitor_info_.otherstat_5_value_ = MY_SPEC.plan_->get_ddl_task_id();
    LOG_INFO("update table context", K(MY_SPEC.ins_ctdef_.das_ctdef_.table_id_), K(MY_SPEC.ins_ctdef_.das_ctdef_.index_tid_));
    if (OB_SUCC(ret)) {
      direct_insert_session_ = ctx_.get_sqc_handler()->get_sub_coord()
          .get_direct_insert_session();
      if (OB_ISNULL(direct_insert_session_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("direct insert session is null", K(ret),
            KP(direct_insert_session_));
      } else if (OB_FAIL(check_need_idempotence())) {
        LOG_WARN("check need idempotence failed", K(ret));
      } else if (OB_FAIL(locate_exprs())) {
        LOG_WARN("locate exprs failed", K(ret));
      } else if (is_heap_plan() && OB_FAIL(heap_tablet_writer_map_.create(MAP_HASH_BUCKET_NUM, ObMemAttr("tblt_writer_map")))) {
        LOG_WARN("init tablet writer map failed", K(ret));
      }
    }
  }
  return ret;
}

void ObPxMultiPartSSTableInsertOp::destroy()
{
  if (heap_tablet_writer_map_.created()) {
    TabletWriterMap::iterator iter = heap_tablet_writer_map_.begin();
    for (; iter != heap_tablet_writer_map_.end(); ++iter) {
      data_plane::ObIDirectInsertWriter *slice_writer = iter->second;
      if (OB_NOT_NULL(slice_writer)) {
        data_plane::ObIDirectInsertWriterFactory::destroy(slice_writer);
      }
    }
  }
  heap_tablet_writer_map_.destroy();
  allocator_.reset();
}

int ObPxMultiPartSSTableInsertOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(direct_insert_session_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("direct insert session is null", K(ret));
  } else if (direct_insert_session_->is_final() || is_all_partition_finished_) {
    ret = OB_ITER_END;
  } else if (is_heap_plan()) {
    if (OB_FAIL(write_heap_slice_by_row())) {
      LOG_WARN("heap tablet write row failed", K(ret));
    }
  } else {
    if (OB_FAIL(write_ordered_slice_by_row())) {
      LOG_WARN("ordered tablet write row failed", K(ret));
    }
    if (OB_SUCC(ret) && need_idempotent_table_autoinc_) {
      if (OB_FAIL(sync_table_level_autoinc_value())) {
        LOG_WARN("persist table-level autoinc value failed", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(complete_direct_insert_worker())) {
      LOG_WARN("complete direct insert worker failed", K(ret));
    }
  }
  return ret;
}

int ObPxMultiPartSSTableInsertOp::complete_direct_insert_worker()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(direct_insert_session_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("direct insert session is null", K(ret));
  } else if (OB_FAIL(direct_insert_session_->complete_px_worker())) {
    LOG_WARN("complete direct insert px worker failed", K(ret));
  }
  return ret;
}

int ObPxMultiPartSSTableInsertOp::get_next_row_from_child(int64_t *inserted_row_cnt)
{
  int ret = child_->get_next_row();
  if (OB_SUCC(ret) && nullptr != inserted_row_cnt && nullptr != direct_insert_session_) {
    ++*inserted_row_cnt;
  }
  if (OB_ITER_END == ret) {
    is_all_partition_finished_ = true;
    FLOG_INFO("all partition iterate finished", KP(this));
  }
  return ret;
}

bool ObPxMultiPartSSTableInsertOp::need_autoinc_by_row()
{
  return need_idempotent_table_autoinc_ || need_idempotent_doc_id_;
}

int ObPxMultiPartSSTableInsertOp::init_table_autoinc_param(
    const ObTabletID &tablet_id,
    const int64_t slice_idx,
    data_plane::ObDirectInsertAutoincParam &autoinc_param)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(direct_insert_session_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("direct insert session is null", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || slice_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(slice_idx));
  } else if (OB_FAIL(direct_insert_session_->build_autoinc_param(
                 data_plane::DIRECT_INSERT_TABLE_AUTOINC,
                 tablet_id, slice_idx, autoinc_param))) {
    LOG_WARN("initialize table-level direct insert autoinc failed", K(ret),
        K(tablet_id), K(slice_idx));
  }
  return ret;
}

int ObPxMultiPartSSTableInsertOp::init_tablet_autoinc_param(
    const ObTabletID &tablet_id,
    const int64_t slice_idx,
    data_plane::ObDirectInsertAutoincParam &autoinc_param)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(direct_insert_session_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("direct insert session is null", K(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || slice_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(slice_idx));
  } else if (OB_FAIL(direct_insert_session_->build_autoinc_param(
                 data_plane::DIRECT_INSERT_TABLET_AUTOINC,
                 tablet_id, slice_idx, autoinc_param))) {
    LOG_WARN("initialize tablet-level direct insert autoinc failed", K(ret),
        K(tablet_id), K(slice_idx));
  }
  return ret;
}

int ObPxMultiPartSSTableInsertOp::eval_current_row(const int64_t rowkey_column_count, blocksstable::ObDatumRow &current_row)
{
  int ret = OB_SUCCESS;
  const ObExprPtrIArray &exprs = get_spec().ins_ctdef_.new_row_;
  if (OB_UNLIKELY(rowkey_column_count <= 0 || !current_row.is_valid() || exprs.count() > current_row.get_capacity())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(rowkey_column_count), K(current_row.get_capacity()), K(exprs.count()));
  } else {
    clear_evaluated_flag();
    ObEvalCtx &eval_ctx = get_eval_ctx();
    const int64_t extra_rowkey_column_count = common::OB_MAX_EXTRA_ROWKEY_COLUMN_NUMBER;
    for (int64_t i = 0; OB_SUCC(ret) && i < exprs.count(); i++) {
      ObDatum *datum = nullptr;
      const ObExpr *e = exprs.at(i);
      if (OB_ISNULL(e)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("expr is NULL", K(ret), K(i));
      } else if (OB_FAIL(e->eval(eval_ctx, datum))) {
        LOG_WARN("evaluate expression failed", K(ret), K(i), KPC(e));
      } else {
        const int64_t store_position = i < rowkey_column_count ? i : i + extra_rowkey_column_count;
        current_row.storage_datums_[store_position].shallow_copy_from_datum(*datum);
      }
    }
  }
  return ret;
}

int ObPxMultiPartSSTableInsertOp::eval_current_row(ObIArray<ObDatum *> &datums)
{
  int ret = OB_SUCCESS;
  datums.reuse();
  clear_evaluated_flag();
  const ObExprPtrIArray &exprs = get_spec().ins_ctdef_.new_row_;
  ObEvalCtx &eval_ctx = get_eval_ctx();
  for (int64_t i = 0; OB_SUCC(ret) && i < exprs.count(); i++) {
    ObDatum *datum = nullptr;
    const ObExpr *e = exprs.at(i);
    if (OB_ISNULL(e)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("expr is NULL", K(ret), K(i));
    } else if (OB_FAIL(e->eval(eval_ctx, datum))) {
      LOG_WARN("evaluate expression failed", K(ret), K(i), KPC(e));
    } else if (OB_FAIL(datums.push_back(datum))) {
      LOG_WARN("push back datum pointer failed", K(ret), KPC(datum));
    }
  }
  return ret;
}

int ObPxMultiPartSSTableInsertOp::locate_exprs()
{
  int ret = OB_SUCCESS;
  // init tablet id expr or non_partitioned_tablet_id_
  const ObExprPtrIArray &child_output_exprs = child_->get_spec().output_;
  const int64_t part_id_idx = get_spec().row_desc_.get_part_id_index();
  if (NO_PARTITION_ID_FLAG == part_id_idx) {
    ObDASTableLoc *table_loc = ins_rtdef_.das_rtdef_.table_loc_;
    if (OB_ISNULL(table_loc) || table_loc->get_tablet_locs().size() != 1) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("insert table location is invalid", K(ret), KPC(table_loc));
    } else {
      non_partitioned_tablet_id_ = table_loc->get_first_tablet_loc()->tablet_id_;
    }
  } else if (part_id_idx < 0 || part_id_idx >= child_output_exprs.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, part_id_idx is not valid", K(ret), K(part_id_idx), K(child_output_exprs.count()));
  } else {
    tablet_id_expr_ = child_output_exprs.at(part_id_idx);
  }

  if (OB_SUCC(ret)) {
    if (is_heap_plan()) {
      // init tablet_autoinc_expr_
      for (int64_t i = 0; OB_SUCC(ret) && i < child_output_exprs.count(); ++i) {
        if (child_output_exprs.at(i)->type_ == T_TABLET_AUTOINC_NEXTVAL) {
          tablet_autoinc_expr_ = child_output_exprs.at(i);
          break;
        }
      }
      if (OB_SUCC(ret) && OB_ISNULL(tablet_autoinc_expr_) && is_vec_gen_vid_) {
        tablet_autoinc_expr_ = child_output_exprs.at(child_output_exprs.count() - 1);
      }

      const ObExprPtrIArray &exprs = get_spec().ins_ctdef_.new_row_;
      bool is_found = false;
      for (int64_t i = 0; OB_SUCC(ret) && !is_found && i < exprs.count(); ++i) {
        if (exprs.at(i) == tablet_autoinc_expr_) {
          tablet_autoinc_column_idx_ = i;
          is_found = true;
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_ISNULL(tablet_autoinc_expr_) || !is_found) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("tablet autoinc expr not found", K(ret), KP(tablet_autoinc_expr_), K(tablet_autoinc_column_idx_), K(is_found));
        }
      }
    } else {
      // for iot table and idempotent ddl, init slice_info_expr_;
      for (int64_t i = 0; OB_SUCC(ret) && i < child_output_exprs.count(); ++i) {
        if (child_output_exprs.at(i)->type_ == ObItemType::T_PSEUDO_DDL_SLICE_ID) {
          slice_info_expr_ = child_output_exprs.at(i);
          break;
        }
      }
    }
  }
  return ret;
}

int ObPxMultiPartSSTableInsertOp::check_need_idempotence()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(direct_insert_session_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("direct insert session is null", K(ret),
        KP(direct_insert_session_));
  } else {
    ObSqlCtx *sql_ctx = nullptr;
    const ObTableSchema *ddl_table_schema = nullptr;
    const ObTableSchema *data_table_schema = nullptr;
    data_plane::ObDirectInsertPlanFacts facts;
    data_plane::ObDirectInsertWritePolicy policy;
    if (OB_ISNULL(sql_ctx = ctx_.get_sql_ctx()) || OB_ISNULL(sql_ctx->schema_guard_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("error unexpected, schema guard not be nullptr", K(ret));
    } else if (OB_FAIL(sql_ctx->schema_guard_->get_table_schema( MY_SPEC.plan_->get_ddl_table_id(), ddl_table_schema))) {
      LOG_WARN("get table schema failed", K(ret), K(MY_SPEC.plan_->get_ddl_table_id()));
    } else if (OB_ISNULL(ddl_table_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table schema is null", K(ret), K(MY_SPEC.plan_->get_ddl_table_id()));
    } else {
      facts.regenerate_heap_table_pk_ = MY_SPEC.regenerate_heap_table_pk_;
      facts.vector_rowkey_vid_ = ddl_table_schema->is_vec_rowkey_vid_type();
      facts.has_table_autoinc_ = ddl_table_schema->get_autoinc_column_id() > 0
          && ddl_table_schema->get_autoinc_column_id() != OB_INVALID_ID;
      facts.rowkey_doc_id_ = ddl_table_schema->is_rowkey_doc_id();
      if (OB_FAIL(direct_insert_session_->resolve_write_policy(facts, policy))) {
        LOG_WARN("resolve direct insert write policy failed", K(ret));
      } else if (policy.idempotent_doc_id_) {
        if (OB_FAIL(sql_ctx->schema_guard_->get_table_schema( ddl_table_schema->get_data_table_id(), data_table_schema))) {
          LOG_WARN("get table schema failed", K(ret), K(ddl_table_schema->get_data_table_id()));
        } else if (OB_ISNULL(data_table_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("table schema is null", K(ret), K(ddl_table_schema->get_data_table_id()));
        } else {
          facts.data_table_without_pk_ = data_table_schema->is_table_without_pk();
        }
      }
      if (OB_SUCC(ret) && policy.idempotent_doc_id_
          && OB_FAIL(direct_insert_session_->resolve_write_policy(facts, policy))) {
        LOG_WARN("refine direct insert write policy failed", K(ret));
      } else if (OB_SUCC(ret)) {
        is_vec_gen_vid_ = policy.vector_generated_id_;
        need_idempotent_tablet_autoinc_ = policy.idempotent_tablet_autoinc_;
        need_idempotent_table_autoinc_ = policy.idempotent_table_autoinc_;
        need_idempotent_doc_id_ = policy.idempotent_doc_id_;
      }
      LOG_TRACE("check need idempotent doc id or table autoinc", K(need_idempotent_doc_id_), K(need_idempotent_table_autoinc_), K(MY_SPEC.plan_->get_ddl_table_id()));
    }
  }
  return ret;
}

// table autoinc not support batch interface, because its eval param is not vectorized
int ObPxMultiPartSSTableInsertOp::write_heap_slice_by_row()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(direct_insert_session_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("direct insert session is null", K(ret));
  }
  ObArray<ObDatum *> datums;
  while (OB_SUCC(ret) && !is_all_partition_finished_) {
    ObTabletID tablet_id;
    data_plane::ObIDirectInsertWriter *slice_writer = nullptr;
    if (OB_FAIL(get_next_row_from_child(&op_monitor_info_.otherstat_2_value_))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("get next row failed", K(ret));
      } else {
        is_all_partition_finished_ = true;
        ret = OB_SUCCESS;
      }
    } else if (OB_FAIL(eval_current_row(datums))) {
      LOG_WARN("eval current row failed", K(ret));
    } else if (OB_FAIL(get_tablet_info_from_row(child_->get_spec().output_, tablet_id))) {
      LOG_WARN("get tablet id from row failed", K(ret), K(child_->get_spec().output_));
    } else if (OB_FAIL(get_or_create_heap_writer(tablet_id, slice_writer))) {
      LOG_WARN("get or create slice writer failed", K(ret), K(tablet_id));
    } else if (OB_FAIL(slice_writer->append_row(
                   data_plane::ObDirectInsertRowView(
                       datums.get_data(), datums.count())))) {
      LOG_WARN("append current row failed", K(ret));
    }
  }
  if (OB_SUCC(ret) && is_all_partition_finished_) {
    // close all slice writer
    TabletWriterMap::iterator iter = heap_tablet_writer_map_.begin();
    for (; OB_SUCC(ret) && iter != heap_tablet_writer_map_.end(); ++iter) {
      const ObTabletID &tablet_id = iter->first;
      data_plane::ObIDirectInsertWriter *slice_writer = iter->second;
      if (OB_ISNULL(slice_writer)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("slice writer is null", K(ret));
      } else if (OB_FAIL(slice_writer->close())) {
        LOG_WARN("close slice writer failed", K(ret));
      }
    }
  }
  return ret;
}

int ObPxMultiPartSSTableInsertOp::get_or_create_heap_writer(
    const ObTabletID &tablet_id,
    data_plane::ObIDirectInsertWriter *&slice_writer)
{
  int ret = OB_SUCCESS;
  slice_writer = nullptr;
  if (OB_FAIL(heap_tablet_writer_map_.get_refactored(tablet_id, slice_writer))) {
    if (OB_HASH_NOT_EXIST != ret) {
      LOG_WARN("get tablet writer failed", K(ret));
    } else {
      ret = OB_SUCCESS;
      const int64_t slice_idx = ctx_.get_px_task_id();
      const int64_t parallel_count = ctx_.get_sqc_handler()->get_sqc_ctx().get_task_count();
      data_plane::ObDirectInsertWriterRequest request;
      request.layout_ = data_plane::DIRECT_INSERT_HEAP_WRITER;
      request.input_format_ = data_plane::DIRECT_INSERT_ROW_INPUT;
      request.tablet_id_ = tablet_id;
      request.slice_index_ = slice_idx;
      request.parallel_count_ = parallel_count;
      request.max_batch_size_ = 0;
      request.autoinc_column_index_ = tablet_autoinc_column_idx_;
      request.idempotent_tablet_autoinc_ = need_idempotent_tablet_autoinc_;
      request.spool_factory_ = &get_temp_column_spill_spool_factory();
      if (OB_FAIL(direct_insert_session_->get_writer_factory().create(
              allocator_, request, slice_writer))) {
        LOG_WARN("create heap direct insert writer failed", K(ret), K(tablet_id),
            K(slice_idx), K(parallel_count));
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(heap_tablet_writer_map_.set_refactored(tablet_id, slice_writer))) {
          LOG_WARN("set tablet writer into map failed", K(ret), K(tablet_id),
              KP(slice_writer));
        }
      }
      if (OB_FAIL(ret)) {
        if (nullptr != slice_writer) {
          data_plane::ObIDirectInsertWriterFactory::destroy(slice_writer);
        }
      }
    }
  }
  return ret;
}

int ObPxMultiPartSSTableInsertOp::write_ordered_slice_by_row()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(direct_insert_session_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("direct insert session is null", K(ret));
  }
  ObArray<ObDatum *> datums;
  ObTabletID tablet_id;
  ObTabletSliceParam slice_param;
  data_plane::ObIDirectInsertWriter *slice_writer = nullptr;
  data_plane::ObDirectInsertAutoincParam autoinc_param;
  bool need_update_tablet_range_count = true;

  while (OB_SUCC(ret) && !is_all_partition_finished_) {
    if (OB_FAIL(get_next_row_from_child(&op_monitor_info_.otherstat_2_value_))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("get next row failed", K(ret));
      } else {
        is_all_partition_finished_ = true;
        ret = OB_SUCCESS;
      }
    } else if (OB_UNLIKELY(need_update_tablet_range_count)
               && OB_FAIL(direct_insert_session_->prepare_ordered_input())) {
      LOG_WARN("update tablet range count failed", K(ret));
    } else if (FALSE_IT(need_update_tablet_range_count = false)) {
    } else if (OB_FAIL(get_tablet_info_from_row(child_->get_spec().output_, tablet_id, &slice_param))) {
      LOG_WARN("get tablet id from row failed", K(ret), K(child_->get_spec().output_));
    } else if (OB_FAIL(switch_slice_if_need(
                   tablet_id, slice_param.slice_idx_, slice_writer,
                   &autoinc_param))) {
      LOG_WARN("get or create slice writer failed", K(ret), K(tablet_id));
    } else if (autoinc_param.enabled_ && FALSE_IT(
          get_eval_ctx().exec_ctx_.set_ddl_idempotent_autoinc_params(autoinc_param.slice_count_,
                                                                     autoinc_param.slice_index_,
                                                                     slice_writer->get_row_count(),
                                                                     autoinc_param.range_interval_))) {
    } else if (OB_FAIL(eval_current_row(datums))) {
      LOG_WARN("eval current row failed", K(ret));
    } else if (OB_FAIL(slice_writer->append_row(
                   data_plane::ObDirectInsertRowView(
                       datums.get_data(), datums.count())))) {
      LOG_WARN("append current row failed", K(ret));
    }
  }
  if (OB_SUCC(ret) && is_all_partition_finished_ && nullptr != slice_writer) {
    if (need_idempotent_doc_id_ && OB_FAIL(sync_tablet_doc_id(slice_writer))) {
      LOG_WARN("sync tablet doc id failed", K(ret), KP(slice_writer));
    } else if (OB_FAIL(slice_writer->close())) {
      LOG_WARN("close slice writer failed", K(ret));
    }
  }

  // ignore ret
  if (nullptr != slice_writer) {
    data_plane::ObIDirectInsertWriterFactory::destroy(slice_writer);
  }
  return ret;
}

int ObPxMultiPartSSTableInsertOp::switch_slice_if_need(
    const ObTabletID &tablet_id, const int64_t slice_idx,
    data_plane::ObIDirectInsertWriter *&slice_writer,
    data_plane::ObDirectInsertAutoincParam *autoinc_param/* = nullptr */)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == direct_insert_session_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("direct insert session is null", K(ret),
        KP(direct_insert_session_));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || slice_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(slice_idx));
  } else if (OB_LIKELY(OB_NOT_NULL(slice_writer)
             && slice_writer->get_tablet_id() == tablet_id
             && slice_writer->get_slice_index() == slice_idx)) {
    // do nothing
  } else {
    if (nullptr != autoinc_param) {
      autoinc_param->reset();
    }
    if (OB_NOT_NULL(slice_writer)) {
      if (need_idempotent_doc_id_ && OB_FAIL(sync_tablet_doc_id(slice_writer))) {
        LOG_WARN("sync tablet doc id failed", K(ret), KP(slice_writer));
      } else if (OB_FAIL(slice_writer->close())) {
        LOG_WARN("close slice writer failed", K(ret));
      } else {
        data_plane::ObIDirectInsertWriterFactory::destroy(slice_writer);
      }
    }

    if (OB_FAIL(ret)) {
    } else {
      data_plane::ObDirectInsertWriterRequest request;
      request.layout_ = data_plane::DIRECT_INSERT_ORDERED_WRITER;
      request.input_format_ = data_plane::DIRECT_INSERT_ROW_INPUT;
      request.tablet_id_ = tablet_id;
      request.slice_index_ = slice_idx;
      request.max_batch_size_ = 0;
      request.append_batch_ = false;
      request.spool_factory_ = &get_temp_column_spill_spool_factory();
      if (OB_FAIL(direct_insert_session_->get_writer_factory().create(
              allocator_, request, slice_writer))) {
        LOG_WARN("create ordered direct insert writer failed", K(ret),
            K(tablet_id), K(slice_idx));
      }
    }
    if (OB_SUCC(ret) && nullptr != autoinc_param) {
      if (need_idempotent_doc_id_) {
        if (OB_FAIL(init_tablet_autoinc_param(tablet_id, slice_idx, *autoinc_param))) {
          LOG_WARN("init tablet autoinc param failed", K(ret));
        }
      } else if (need_idempotent_table_autoinc_) {
        if (OB_FAIL(init_table_autoinc_param(tablet_id, slice_idx, *autoinc_param))) {
          LOG_WARN("init table autoinc param failed", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObPxMultiPartSSTableInsertOp::get_data_tablet_id(const ObTabletID &tablet_id, ObTabletID &data_tablet_id)
{
  int ret = OB_SUCCESS;
  ObSqlCtx *sql_ctx = nullptr;
  const ObTableSchema *ddl_table_schema = nullptr;
  const ObTableSchema *data_table_schema = nullptr;
  data_tablet_id.reset();
  if (OB_ISNULL(sql_ctx = ctx_.get_sql_ctx()) || OB_ISNULL(sql_ctx->schema_guard_) || OB_ISNULL(MY_SPEC.plan_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema guard, sql_ctx or plan is null", K(ret));
  } else if (OB_FAIL(sql_ctx->schema_guard_->get_table_schema( MY_SPEC.plan_->get_ddl_table_id(), ddl_table_schema))) {
    LOG_WARN("fail to get ddl table schema", K(ret), K(MY_SPEC.plan_->get_ddl_table_id()));
  } else if (OB_ISNULL(ddl_table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ddl table schema is null", K(ret), K(MY_SPEC.plan_->get_ddl_table_id()));
  } else if (OB_FAIL(sql_ctx->schema_guard_->get_table_schema( ddl_table_schema->get_data_table_id(), data_table_schema))) {
    LOG_WARN("fail to get data table schema", K(ret), K(ddl_table_schema->get_data_table_id()));
  } else if (OB_ISNULL(data_table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("data table schema is null", K(ret), K(ddl_table_schema->get_data_table_id()));
  } else if (!data_table_schema->is_partitioned_table()) {
    data_tablet_id = data_table_schema->get_tablet_id();
  } else {
    int64_t part_idx = OB_INVALID_INDEX;
    int64_t subpart_idx = OB_INVALID_INDEX;
    ObObjectID object_id;
    ObObjectID first_level_part_id;
    if (OB_FAIL(ddl_table_schema->get_part_idx_by_tablet(tablet_id, part_idx, subpart_idx))) {
      LOG_WARN("fail to get part idx by tablet", K(ret), K(tablet_id));
    } else if (OB_FAIL(data_table_schema->get_tablet_and_object_id_by_index(part_idx,
                                                                            subpart_idx,
                                                                            data_tablet_id,
                                                                            object_id,
                                                                            first_level_part_id))) {
      LOG_WARN("fail to get data tablet id", K(ret), K(part_idx), K(subpart_idx));
    }
  }
  return ret;
}

int ObPxMultiPartSSTableInsertOp::sync_tablet_doc_id(
    data_plane::ObIDirectInsertWriter *slice_writer)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == direct_insert_session_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("direct insert session is null", K(ret),
        KP(direct_insert_session_));
  } else if (OB_UNLIKELY(nullptr == slice_writer)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(slice_writer));
  } else {
    const ObTabletID tablet_id = slice_writer->get_tablet_id();
    const int64_t slice_idx = slice_writer->get_slice_index();
    ObTabletID data_tablet_id;
    if (OB_FAIL(get_data_tablet_id(tablet_id, data_tablet_id))) {
      LOG_WARN("fail to get data tablet id", K(ret), K(tablet_id));
    } else if (OB_FAIL(direct_insert_session_->sync_tablet_autoinc(
                   tablet_id, data_tablet_id, slice_idx,
                   slice_writer->get_row_count()))) {
      LOG_WARN("sync tablet autoinc sequence failed", K(ret), K(tablet_id),
          K(data_tablet_id), K(slice_idx), K(slice_writer->get_row_count()));
    }
  }
  return ret;
}

int ObPxMultiPartSSTableInsertOp::sync_table_level_autoinc_value()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(direct_insert_session_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("direct insert session is null", K(ret));
  } else {
    ObAutoincrementService &auto_service = ObAutoincrementService::get_instance();
    ObEvalCtx &eval_ctx = get_eval_ctx();
    ObPhysicalPlanCtx *plan_ctx = eval_ctx.exec_ctx_.get_physical_plan_ctx();
    if (OB_ISNULL(plan_ctx)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("physical plan context is null", K(ret), K(plan_ctx));
    } else {
      ObIArray<AutoincParam> &autoinc_params = plan_ctx->get_autoinc_params();
      if (OB_FAIL(plan_ctx->sync_last_value_local())) {
        LOG_WARN("fail to sync last value local", K(ret));
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < autoinc_params.count(); ++i) {
        AutoincParam &autoinc_param = autoinc_params.at(i);
        autoinc_param.auto_increment_cache_size_ = 0; // set cache size to 0 to disable prefetch
        if (OB_FAIL(auto_service.sync_insert_value(autoinc_param))) {
          LOG_WARN("sync value failed", K(ret));
        }
      }
    }
  }
  return ret;
}
