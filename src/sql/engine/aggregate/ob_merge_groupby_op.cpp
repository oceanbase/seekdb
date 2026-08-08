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

#include "sql/engine/aggregate/ob_merge_groupby_op.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

OB_SERIALIZE_MEMBER((ObMergeGroupBySpec, ObGroupBySpec),
                    group_exprs_,
                    rollup_exprs_,
                    is_duplicate_rollup_expr_,
                    has_rollup_,
                    distinct_exprs_,
                    est_rows_per_group_,
                    enable_hash_base_distinct_
);

DEF_TO_STRING(ObMergeGroupBySpec)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_NAME("groupby_spec");
  J_COLON();
  pos += ObGroupBySpec::to_string(buf + pos, buf_len - pos);
  J_COMMA();
  J_KV(K_(group_exprs), K_(rollup_exprs), K_(is_duplicate_rollup_expr), K_(has_rollup),
       K_(est_rows_per_group), K_(enable_hash_base_distinct));
  J_OBJ_END();
  return pos;
}

int ObMergeGroupBySpec::add_group_expr(ObExpr *expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", K(ret));
  } else if (OB_FAIL(group_exprs_.push_back(expr))) {
    LOG_ERROR("failed to push_back expr");
  }
  return ret;
}

int ObMergeGroupBySpec::add_rollup_expr(ObExpr *expr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", K(ret));
  } else if (OB_FAIL(rollup_exprs_.push_back(expr))) {
    LOG_ERROR("failed to push_back expr");
  }
  return ret;
}

void ObMergeGroupByOp::reset()
{
  is_end_ = false;
  cur_output_group_id_= OB_INVALID_INDEX;
  first_output_group_id_ = 0;
  last_child_output_.reset();
  curr_group_rowid_ = common::OB_INVALID_INDEX;
  output_queue_cnt_ = 0;
  for(auto i = 0; i < output_groupby_rows_.count(); i++) {
    if (OB_NOT_NULL(output_groupby_rows_.at(i))) {
      output_groupby_rows_.at(i)->reset();
    }
  }
  brs_holder_.reset();
  output_groupby_rows_.reset();
  cur_group_row_ = nullptr;
  has_dup_group_expr_ = false;
  is_first_calc_ = true;
  cur_group_last_row_idx_ = -1;
}

int ObMergeGroupByOp::init_group_rows()
{
  int ret = OB_SUCCESS;
  const int64_t col_count = (MY_SPEC.has_rollup_
      ? (MY_SPEC.group_exprs_.count() + MY_SPEC.rollup_exprs_.count() + 1)
      : 0);
  // for vectorization, from 0 and col_count - 2 are the rollup group row
  //                      and the col_col - 1 is the last group row
  // it's not init firstly
  all_groupby_exprs_.reset();
  if (OB_FAIL(append(all_groupby_exprs_, MY_SPEC.group_exprs_))) {
    LOG_WARN("failed to append group exprs", K(ret));
  } else if (MY_SPEC.has_rollup_ &&
      OB_FAIL(append(all_groupby_exprs_, MY_SPEC.rollup_exprs_))) {
    LOG_WARN("failed to append group exprs", K(ret));
  } else if (!is_vectorized()) {
    if (MY_SPEC.enable_hash_base_distinct_) {
      const int64_t hp_infras_cnt = col_count <= 0 ? 1 : col_count;
      const int64_t distinct_cnt = aggr_processor_.get_distinct_count();
      if (aggr_processor_.has_distinct() && distinct_cnt > 0
        && OB_FAIL(hp_infras_mgr_.reserve_hp_infras(hp_infras_cnt * distinct_cnt))) {
        LOG_WARN("failed to init hp infras group", K(ret));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(aggr_processor_.init_group_rows(col_count))) {
      LOG_WARN("failed to initialize init_group_rows", K(ret));
    }
  } else {
    if (MY_SPEC.enable_hash_base_distinct_) {
      const int64_t hp_infras_cnt = col_count - 1 <= 0 ? 1 : col_count;
      const int64_t distinct_cnt = aggr_processor_.get_distinct_count();
      if (aggr_processor_.has_distinct() && distinct_cnt > 0
        && OB_FAIL(hp_infras_mgr_.reserve_hp_infras(hp_infras_cnt * distinct_cnt))) {
        LOG_WARN("failed to init hp infras group", K(ret));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(aggr_processor_.init_group_rows(col_count - 1))) {
      LOG_WARN("failed to initialize init_group_rows", K(ret));
    } else if (MY_SPEC.has_rollup_) {
      curr_group_rowid_ = all_groupby_exprs_.count();
      cur_output_group_id_ = all_groupby_exprs_.count();
    } else {
      curr_group_rowid_ = 0;
      cur_output_group_id_ = 0;
    }
  }
  return ret;
}

// only init in inner_open
int ObMergeGroupByOp::init()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObChunkStoreUtil::alloc_dir_id(dir_id_))) {
    LOG_WARN("failed to alloc dir id", K(ret));
  } else if (FALSE_IT(aggr_processor_.set_dir_id(dir_id_))) {
  } else if (FALSE_IT(aggr_processor_.set_io_event_observer(&io_event_observer_))) {
  } else if (MY_SPEC.enable_hash_base_distinct_
    && OB_FAIL(init_hp_infras_group_mgr())) {
    LOG_WARN("failed to init hp infras group manager", K(ret));
  } else if (OB_FAIL(init_group_rows())) {
    LOG_WARN("failed to init group rows", K(ret));
  } else if (is_vectorized()) {
    if (OB_FAIL(brs_holder_.init(child_->get_spec().output_, eval_ctx_))) {
      LOG_WARN("failed to initialize brs_holder_", K(ret));
    } else {
      // prepare initial group
      if (MY_SPEC.has_rollup_) {
        for (int64_t i = 0; !has_dup_group_expr_ && i < MY_SPEC.is_duplicate_rollup_expr_.count(); ++i) {
          has_dup_group_expr_ = MY_SPEC.is_duplicate_rollup_expr_.at(i);
        }
        aggr_processor_.set_op_eval_infos(&eval_infos_);
      }
    }
  }
  if (OB_SUCC(ret)) {
    int64_t idx = -1;
    for (int64_t i = 0; i < MY_SPEC.distinct_exprs_.count() && OB_SUCC(ret); ++i) {
      if (has_exist_in_array(child_->get_spec().output_,
                             MY_SPEC.distinct_exprs_.at(i),
                             &idx)) {
        OZ(distinct_col_idx_in_output_.push_back(idx));
      } else {
        // is_const
        distinct_col_idx_in_output_.push_back(-1);
        LOG_DEBUG("distinct expr is const and is not in the output of child", K(i), K(ret));
      }
    }
    LOG_DEBUG("debug distinct exprs", K(ret), K(MY_SPEC.distinct_exprs_.count()));
  }
  if (OB_SUCC(ret) && aggr_processor_.has_extra()) {
    // set group_batch_factor_ to 1 avoid out of memory error
    group_batch_factor_ = 1;
  }
  // group by c1,c2, rollup(c3,c4)
  // then rollup will generate new group row that c3 is different
  last_child_output_.reuse_ = true;
  return ret;
}

int ObMergeGroupByOp::inner_open()
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_FAIL(ObGroupByOp::inner_open())) {
    LOG_WARN("failed to inner_open", K(ret));
  } else if (OB_FAIL(init())) {
    LOG_WARN("failed to init", K(ret));
  }
  return ret;
}

int ObMergeGroupByOp::inner_close()
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_FAIL(ObGroupByOp::inner_close())) {
    LOG_WARN("failed to inner close", K(ret));
  }
  sql_mem_processor_.unregister_profile();
  return ret;
}

void ObMergeGroupByOp::destroy()
{
  sql_mem_processor_.unregister_profile_if_necessary();
  all_groupby_exprs_.reset();
  distinct_col_idx_in_output_.reset();
  reset();
  hp_infras_mgr_.destroy();
  ObGroupByOp::destroy();
}

int ObMergeGroupByOp::inner_switch_iterator()
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_FAIL(ObGroupByOp::inner_switch_iterator())) {
    LOG_WARN("failed to switch_iterator", K(ret));
  } else if (OB_FAIL(init_group_rows())) {
    LOG_WARN("failed to init group rows", K(ret));
  }
  return ret;
}

int ObMergeGroupByOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_FAIL(ObGroupByOp::inner_rescan())) {
    LOG_WARN("failed to rescan", K(ret));
  } else if (OB_FAIL(init_group_rows())) {
    LOG_WARN("failed to init group rows", K(ret));
  }
  return ret;
}

void ObMergeGroupByOp::set_rollup_expr_null(int64_t group_id)
{
  const bool is_dup_expr = (group_id < MY_SPEC.group_exprs_.count()
    ? true
    : MY_SPEC.is_duplicate_rollup_expr_.at(group_id - MY_SPEC.group_exprs_.count()));
  if (!is_dup_expr) {
    ObExpr *null_expr = const_cast<ObExpr *>(group_id < MY_SPEC.group_exprs_.count()
        ? MY_SPEC.group_exprs_[group_id]
        : MY_SPEC.rollup_exprs_[group_id - MY_SPEC.group_exprs_.count()]);
    null_expr->locate_expr_datum(eval_ctx_).set_null();
    null_expr->set_evaluated_projected(eval_ctx_);
  }
}

int ObMergeGroupByOp::rewrite_rollup_column(ObExpr *&diff_expr)
{
  int ret = OB_SUCCESS;
  // output rollup results here
  set_rollup_expr_null(cur_output_group_id_);
  diff_expr = const_cast<ObExpr *>(cur_output_group_id_ < MY_SPEC.group_exprs_.count()
      ? MY_SPEC.group_exprs_[cur_output_group_id_]
      : MY_SPEC.rollup_exprs_[cur_output_group_id_ - MY_SPEC.group_exprs_.count()]);
  LOG_DEBUG("debug write rollup column 1", KP(diff_expr), K(cur_output_group_id_));
  //for SELECT GROUPING(z0_test0) FROM Z0CASE GROUP BY z0_test0, ROLLUP(z0_test0);
  //issue:
  if (cur_output_group_id_ >= MY_SPEC.group_exprs_.count()) {
    for (int64_t i = 0; diff_expr != NULL && i < MY_SPEC.group_exprs_.count(); ++i) {
      if (MY_SPEC.group_exprs_[i] == diff_expr) {
        diff_expr = NULL;
      }
    }
    const bool is_dup_expr = (cur_output_group_id_ < MY_SPEC.group_exprs_.count()
                              ? true
                              : MY_SPEC.is_duplicate_rollup_expr_.at(
                              cur_output_group_id_ - MY_SPEC.group_exprs_.count()));
    if (is_dup_expr) {
      diff_expr = nullptr;
    }
  }
  LOG_DEBUG("debug write rollup column", KP(diff_expr), K(cur_output_group_id_));
  return ret;
}

int ObMergeGroupByOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  const int64_t stop_output_group_id = MY_SPEC.group_exprs_.count();
  const int64_t col_count = MY_SPEC.group_exprs_.count() + MY_SPEC.rollup_exprs_.count();
  const int64_t group_id = MY_SPEC.has_rollup_ ? col_count : 0;
  bool need_dup_data = 0 < MY_SPEC.distinct_exprs_.count() && 0 == MY_SPEC.rollup_exprs_.count();
  LOG_DEBUG("before inner_get_next_row",
            "aggr_hold_size", aggr_processor_.get_aggr_hold_size(),
            "aggr_used_size", aggr_processor_.get_aggr_used_size());

    if (MY_SPEC.has_rollup_
        && cur_output_group_id_ >= first_output_group_id_
        && cur_output_group_id_ >= stop_output_group_id) {

    ObExpr *diff_expr = NULL;
    if (OB_FAIL(rewrite_rollup_column(diff_expr))) {
      LOG_WARN("failed to rewrite_rollup_column", K(ret));
    } else if (OB_FAIL(rollup_and_calc_results(cur_output_group_id_, diff_expr))) {
      LOG_WARN("failed to rollup and calculate results", K(cur_output_group_id_), K(ret));
    } else {
      --cur_output_group_id_;
      LOG_DEBUG("finish ouput rollup row", K(cur_output_group_id_), K(first_output_group_id_),
                K(ret));
    }
  } else if (is_end_) {
    ret = OB_ITER_END;
  } else {
    // output group results here
    bool is_break = false;
    int64_t first_diff_pos = OB_INVALID_INDEX;
    ObAggregateProcessor::GroupRow *group_row = NULL;
    if (OB_FAIL(aggr_processor_.get_group_row(group_id, group_row))) {
      LOG_WARN("failed to get_group_row", K(ret));
    } else if (OB_ISNULL(group_row)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("group_row is null", K(ret));
    } else if (NULL != last_child_output_.store_row_) {
      if (OB_FAIL(last_child_output_.store_row_->to_expr(child_->get_spec().output_, eval_ctx_))) {
        LOG_WARN("Failed to get next row", K(ret));
      }
    } else {
      if (OB_FAIL(child_->get_next_row())) { // get 1st iteration
        if (ret != OB_ITER_END) {
          LOG_WARN("failed to get next row", K(ret));
        }
      } else if (0 < MY_SPEC.distinct_exprs_.count()) {
        // save the first row
        if (OB_FAIL(last_child_output_.save_store_row(child_->get_spec().output_,
                                                      eval_ctx_,
                                                      0))) {
          LOG_WARN("failed to store child output", K(ret));
        }
      }
    }
    LOG_DEBUG("finish merge prepare 1", K(child_->get_spec().output_),
              KPC(last_child_output_.store_row_));

    if (OB_SUCC(ret)) {
      clear_evaluated_flag();
      if (OB_FAIL(prepare_and_save_curr_groupby_datums(group_id, group_row,
          all_groupby_exprs_, all_groupby_exprs_.count()))) {
        LOG_WARN("failed to prepare and save groupby store row", K(ret));
      } else if (OB_FAIL(aggr_processor_.prepare(*group_row))) {
        LOG_WARN("failed to prepare", K(ret));
      }

      while (OB_SUCC(ret) && !is_break && OB_SUCC(child_->get_next_row())) {
        clear_evaluated_flag();
        if (OB_FAIL(try_check_status())) {
          LOG_WARN("check status failed", K(ret));
        } else if (0 < col_count && OB_FAIL(check_same_group(group_row, first_diff_pos))) {
          LOG_WARN("failed to check group", K(ret));
        } else if (OB_INVALID_INDEX == first_diff_pos) {
          //same group
          bool no_need_process = false;
          if (need_dup_data && check_unique_distinct_columns(group_row, no_need_process)) {
            LOG_WARN("failed to check unique distinct columns", K(ret));
          } else if (!no_need_process && OB_FAIL(aggr_processor_.process(*group_row))) {
            LOG_WARN("failed to calc aggr", K(ret));
          } else {
            LOG_DEBUG("process row", K(no_need_process),
              K(ROWEXPR2STR(eval_ctx_, child_->get_spec().output_)));
          }
        } else {
          //different group
          if (OB_FAIL(last_child_output_.save_store_row(child_->get_spec().output_,
                                                        eval_ctx_,
                                                        0))) {
            LOG_WARN("failed to store child output", K(ret));
          } else if (OB_FAIL(restore_groupby_datum(group_row, first_diff_pos))) {
            LOG_WARN("failed to restore_groupby_datum", K(ret));
          } else if (OB_FAIL(rollup_and_calc_results(group_id))) {
            LOG_WARN("failed to rollup and calculate results", K(group_id), K(ret));
          } else {
            is_break = true;
            if (MY_SPEC.has_rollup_) {
              first_output_group_id_ = first_diff_pos + 1;
              cur_output_group_id_ = group_id - 1;
            }
          }
        }
      } // end while

      if (OB_ITER_END == ret) {
        // the last group
        is_end_ = true;
        if (OB_FAIL(restore_groupby_datum(group_row, 0))) {
          LOG_WARN("failed to restore_groupby_datum", K(ret));
        } else if (OB_FAIL(rollup_and_calc_results(group_id))) {
          LOG_WARN("failed to rollup and calculate results", K(group_id), K(ret));
        } else {
          if (MY_SPEC.has_rollup_) {
            first_output_group_id_ = 0;
            cur_output_group_id_ = group_id - 1;
          }
        }
        LOG_DEBUG("finish iter end", K(first_output_group_id_), K(cur_output_group_id_),
          K(MY_SPEC.id_));
      }
    }
  }
  LOG_TRACE("after inner_get_next_row", "aggr_hold_size",
            aggr_processor_.get_aggr_hold_size(), "aggr_used_size",
            aggr_processor_.get_aggr_used_size());
  return ret;
}

int ObMergeGroupByOp::get_child_next_batch_row(
  const int64_t max_row_cnt, const ObBatchRows *&batch_rows)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(child_->get_next_batch(max_row_cnt, batch_rows))) {
    LOG_WARN("failed to get child row", K(ret));
  } else if (aggr_processor_.get_need_advance_collect() &&
             OB_FAIL(brs_holder_.save(MY_SPEC.max_batch_size_))) {
    LOG_WARN("failed to backup child exprs", K(ret));
  }
  return ret;
}
int ObMergeGroupByOp::advance_collect_result(int64_t group_id)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(aggr_processor_.advance_collect_result(group_id))) {
    LOG_WARN("failed to calc and material distinct result", K(ret), K(group_id));
  } else if (OB_FAIL(brs_holder_.restore())) {
    LOG_WARN("failed to restore child exprs", K(ret));
  }
  clear_evaluated_flag();
  return ret;
}

int ObMergeGroupByOp::inner_get_next_batch(const int64_t max_row_cnt)
{
  // TODO qubin.qb: support rollup in next release
  int ret = OB_SUCCESS;
  int64_t output_batch_cnt = common::min(max_row_cnt, MY_SPEC.max_batch_size_);
  int64_t child_batch_cnt = common::max(max_row_cnt, MY_SPEC.max_batch_size_);
  const ObBatchRows *child_brs = nullptr;
  LOG_DEBUG("before inner_get_next_batch", "aggr_hold_size",
           aggr_processor_.get_aggr_hold_size(), "aggr_used_size",
           aggr_processor_.get_aggr_used_size(), K(output_batch_cnt), K(max_row_cnt));

  if (is_output_queue_not_empty()) {
    // consume aggr results generated in previous round
    if (OB_FAIL(calc_batch_results(is_end_, output_batch_cnt))) {
      LOG_WARN("failed to calc output results", K(ret));
    }
  } else {
    LOG_DEBUG("begin to get_next_batch rows from child", K(child_batch_cnt));
    set_output_queue_cnt(0);
    if (curr_group_rowid_ > common::OB_INVALID_INDEX &&
        OB_FAIL(brs_holder_.restore())) {
      LOG_ERROR("failed to restore previous exprs", K(ret));
    } else {
      // do nothing: 1st iteration, no previous aggregation
    }

    if (OB_SUCC(ret)) {
      brs_holder_.reset();
      while (OB_SUCC(ret) &&
             OB_SUCC(get_child_next_batch_row(child_batch_cnt, child_brs))) {
        if (child_brs->end_ && child_brs->size_ == 0) {
          LOG_DEBUG("reach iterating end with empty result, do nothing");
          break;
        }
        clear_evaluated_flag();
        if (OB_FAIL(try_check_status())) {
          LOG_WARN("check status failed", K(ret));
        } else if (OB_FAIL(groupby_datums_eval_batch(*(child_brs->skip_),
                                              child_brs->size_))) {
          LOG_WARN("failed to calc_groupby_datums", K(ret));
        } else if (OB_FAIL(process_batch(*child_brs))) {
          LOG_WARN("failed to process_batch_result", K(ret));
        } else if (stop_batch_iterating(*child_brs, output_batch_cnt)) {
          // backup child exprs for this round
          // for the vectorized merge distinct scenario, the result will be calculated and materialized
          // in advance. therefore, when a backup is performed after a batch processing is completed
          // the output expression of the child has been refilled. so, it is necessary to perform backup
          // after get next batch from the child operator, and there is no need to backup again.
          if (!aggr_processor_.get_need_advance_collect()) {
            OZ(brs_holder_.save(std::min(MY_SPEC.max_batch_size_, get_output_queue_cnt())));
          }
          LOG_DEBUG("break out of iteratation", K(child_brs->end_),
                    K(output_batch_cnt), K(output_queue_cnt_));
          break;
        } else { // do nothing
        }
      }

      if (OB_SUCC(ret) && child_brs->end_ && !OB_ISNULL(cur_group_row_)) {
        // add last unfinised grouprow into output group
        inc_output_queue_cnt();
        const int64_t advance_collect_group_id = curr_group_rowid_;
        if (MY_SPEC.has_rollup_) {
          int64_t start_rollup_id = MY_SPEC.group_exprs_.count() - 1;
          int64_t end_rollup_id = all_groupby_exprs_.count() - 1;
          int64_t max_group_idx = MY_SPEC.group_exprs_.count() - 1;
          LOG_DEBUG("debug grouping_id", K(end_rollup_id), K(start_rollup_id), K(max_group_idx));
          if (end_rollup_id >= start_rollup_id && OB_FAIL(gen_rollup_group_rows(
                start_rollup_id,
                end_rollup_id,
                max_group_idx,
                curr_group_rowid_))) {
              LOG_WARN("failed to genereate rollup group row", K(ret));
          }
        }
        if (OB_FAIL(ret)) {
        } else if (aggr_processor_.get_need_advance_collect()
          && OB_FAIL(advance_collect_result(advance_collect_group_id))) {
          LOG_WARN("failed to collect distinct result", K(ret), K(advance_collect_group_id));
        }
      }
      if (OB_SUCC(ret) &&
          OB_FAIL(calc_batch_results(child_brs->end_, output_batch_cnt))) {
        LOG_WARN("failed to calc output results", K(ret));
      }
    }
  }

  LOG_DEBUG("after inner_get_next_batch", "aggr_hold_size",
           aggr_processor_.get_aggr_hold_size(), "aggr_used_size",
           aggr_processor_.get_aggr_used_size(), K(output_batch_cnt), K(ret));
  return ret;
}

int ObMergeGroupByOp::set_null(int64_t idx, ObChunkDatumStore::StoredRow *rollup_store_row)
{
  int ret = OB_SUCCESS;
  if (0 > idx) {
  } else {
    OZ(rollup_store_row->set_null(idx));
    LOG_DEBUG("set null", K(idx), K(MY_SPEC.rollup_exprs_.count()));
    if (has_dup_group_expr_) {
      int64_t start_idx = idx - MY_SPEC.group_exprs_.count();
      ObExpr *base_expr = MY_SPEC.rollup_exprs_.at(start_idx);
      for (int i = start_idx + 1; i < MY_SPEC.rollup_exprs_.count() && OB_SUCC(ret); ++i) {
        if (base_expr == MY_SPEC.rollup_exprs_.at(i)) {
          // set null to the same expr
          OZ(rollup_store_row->set_null(i + MY_SPEC.group_exprs_.count()));
          LOG_DEBUG("set null", K(i + MY_SPEC.group_exprs_.count()), K(start_idx),
            K(MY_SPEC.rollup_exprs_.count()));
        }
      }
    }
  }
  return ret;
}

int ObMergeGroupByOp::get_rollup_row(
  int64_t prev_group_row_id,
  int64_t group_row_id,
  ObAggregateProcessor::GroupRow *&curr_group_row,
  bool &need_set_null,
  int64_t idx)
{
  int ret = OB_SUCCESS;
  curr_group_row = nullptr;
  need_set_null = false;
  ObAggregateProcessor::GroupRow *prev_group_row = nullptr;
  (void) aggr_processor_.get_group_row(prev_group_row_id, prev_group_row);
  if (OB_ISNULL(prev_group_row)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get prev group row", K(ret), K(prev_group_row_id));
  } else if (group_row_id < aggr_processor_.get_group_rows_count()) {
    // critical path: reuse grouprow directly no defensive check
    (void) aggr_processor_.get_group_row(group_row_id, curr_group_row);
  }
  if (OB_FAIL(ret)) {
  } else if (nullptr == curr_group_row) {
    if (OB_FAIL(aggr_processor_.init_one_group(group_row_id, true))) {
      LOG_WARN("failed to init_one_group", K(ret));
    } else if (OB_FAIL(aggr_processor_.get_group_row(group_row_id, curr_group_row))) {
      LOG_WARN("failed to get_group_row", K(ret));
      // performance critical: use curr_group_row directly, no defensive check
    } else {
      // deep copy from prev group row
      ObChunkDatumStore::LastStoredRow *groupby_store_row = nullptr;
      if (OB_FAIL(get_groupby_store_row(group_row_id, &groupby_store_row))) {
        LOG_WARN("failed to get_groupby_store_row", K(ret));
      } else if (OB_FAIL(groupby_store_row->save_store_row(
          *prev_group_row->groupby_store_row_, 0))) {
        LOG_WARN("failed to store group row", K(ret));
      } else {
        curr_group_row->groupby_store_row_ = groupby_store_row->store_row_;
        need_set_null = true;
        if (0 <= idx) {
          // if the expr in rollup in group by or the expr exists more than one tiem,
          // then only the first expr need to set null
          // eg: group by c1, rollup(c1,c1)
          // then don't reset null
          OZ(set_null(idx, curr_group_row->groupby_store_row_));
        }
      }
    }
  } else if (nullptr == curr_group_row->groupby_store_row_) {
    // deep copy from prev group row
    ObChunkDatumStore::LastStoredRow *groupby_store_row = nullptr;
    if (OB_FAIL(get_groupby_store_row(group_row_id, &groupby_store_row))) {
      LOG_WARN("failed to get_groupby_store_row", K(ret));
    } else if (OB_FAIL(groupby_store_row->save_store_row(
        *prev_group_row->groupby_store_row_, 0))) {
      LOG_WARN("failed to store group row", K(ret));
    } else {
      curr_group_row->groupby_store_row_ = groupby_store_row->store_row_;
      need_set_null = true;
      if (0 <= idx) {
        OZ(set_null(idx, curr_group_row->groupby_store_row_));
      }
    }
  }
  return ret;
}


int ObMergeGroupByOp::get_empty_rollup_row(
  int64_t group_row_id,
  ObAggregateProcessor::GroupRow *&curr_group_row)
{
  int ret = OB_SUCCESS;
  curr_group_row = nullptr;
  ObChunkDatumStore::LastStoredRow *store_row = nullptr;
  if (group_row_id < aggr_processor_.get_group_rows_count()) {
    // critical path: reuse grouprow directly no defensive check
    (void) aggr_processor_.get_group_row(group_row_id, curr_group_row);
    if (OB_ISNULL(curr_group_row)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get group row", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (nullptr == curr_group_row) {
    if (OB_FAIL(aggr_processor_.init_one_group(group_row_id))) {
      LOG_WARN("failed to init_one_group", K(ret));
    } else if (OB_FAIL(aggr_processor_.get_group_row(group_row_id, curr_group_row))) {
      LOG_WARN("failed to get_group_row", K(ret));
      // performance critical: use curr_group_row directly, no defensive check
    } else if (OB_FAIL(get_groupby_store_row(group_row_id, &store_row))) {
      LOG_WARN("failed to get groupby store row", K(ret), K(group_row_id));
    } else if (OB_ISNULL(store_row)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected status: store row is null", K(ret));
    } else if (output_groupby_rows_.count() != aggr_processor_.get_group_rows_count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected status: store row is null", K(ret));
    }
  }
  return ret;
}

int ObMergeGroupByOp::get_cur_group_row(
  int64_t group_row_id,
  ObAggregateProcessor::GroupRow *&curr_group_row,
  ObIArray<ObExpr*> &group_exprs,
  const int64_t group_count)
{
  int ret = OB_SUCCESS;
  curr_group_row = nullptr;
  if (group_row_id < aggr_processor_.get_group_rows_count()) {
    // critical path: reuse grouprow directly no defensive check
    (void) aggr_processor_.get_group_row(group_row_id, curr_group_row);
    if (OB_ISNULL(curr_group_row)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get group row", K(ret), K(group_row_id));
    } else if (OB_FAIL(prepare_and_save_curr_groupby_datums(
        curr_group_rowid_, curr_group_row, group_exprs, group_count))) {
      LOG_WARN("failed to prepare group datums", K(ret));
    }
  } else {
    if (OB_FAIL(aggr_processor_.init_one_group(group_row_id))) {
      LOG_WARN("failed to init_one_group", K(ret));
    } else if (OB_FAIL(aggr_processor_.get_group_row(group_row_id, curr_group_row))) {
      LOG_WARN("failed to get_group_row", K(ret));
      // performance critical: use curr_group_row directly, no defensive check
    } else if (OB_FAIL(prepare_and_save_curr_groupby_datums(
        curr_group_rowid_, curr_group_row, group_exprs, group_count))) {
      LOG_WARN("failed to eval_aggr_param_batch");
    }
  }
  return ret;
}

int ObMergeGroupByOp::gen_rollup_group_rows(
  int64_t start_diff_group_idx,
  int64_t end_group_idx,
  int64_t max_group_idx,
  int64_t cur_group_row_id)
{
  int ret = OB_SUCCESS;
  int64_t cur_rollup_group_id = cur_group_row_id;
  int64_t prev_group_row_id = cur_rollup_group_id;
  int64_t cur_rollup_idx = end_group_idx;
  int64_t start_rollup_idx = start_diff_group_idx;
  int64_t null_idx = all_groupby_exprs_.count();
  const int64_t group_exprs_cnt = MY_SPEC.group_exprs_.count();
  ObAggregateProcessor::GroupRow *curr_group_row = nullptr;
  bool need_set_null = false;
  for (int64_t idx = cur_rollup_idx; OB_SUCC(ret) && idx >= start_rollup_idx; --idx) {
    cur_rollup_group_id = idx;
    if (idx <= max_group_idx) {
    } else if (OB_FAIL(get_rollup_row(prev_group_row_id, cur_rollup_group_id, curr_group_row,
        need_set_null, MY_SPEC.is_duplicate_rollup_expr_.at(idx - group_exprs_cnt) ? -1 : idx))) {
      LOG_WARN("failed to get one new group row", K(ret));
    } else if (OB_FAIL(aggr_processor_.rollup_batch_process(
        prev_group_row_id, cur_rollup_group_id,
        MY_SPEC.is_duplicate_rollup_expr_.at(idx - group_exprs_cnt) ? null_idx : idx,
        all_groupby_exprs_.count()))) {
      LOG_WARN("failed to rollup process", K(ret));
    }
    if (OB_FAIL(ret)) {
    } else if (idx != cur_rollup_idx) {
      // output prev rollup group row
      ++curr_group_rowid_;
      curr_group_row = nullptr;
      inc_output_queue_cnt();
      if (aggr_processor_.get_need_advance_collect()
        && OB_FAIL(advance_collect_result(prev_group_row_id))) {
        LOG_WARN("failed to calc and material distinct result", K(ret), K(prev_group_row_id));
      } else if (OB_FAIL(get_empty_rollup_row(curr_group_rowid_, curr_group_row))) {
        LOG_WARN("failed to get one new group row", K(ret));
      } else if (OB_FAIL(aggr_processor_.swap_group_row(prev_group_row_id, curr_group_rowid_))) {
        LOG_WARN("failed to swap group row", K(ret));
      } else {
        // It must be same as swap group row
        std::swap(output_groupby_rows_[prev_group_row_id],
                      output_groupby_rows_[curr_group_rowid_]);
        LOG_DEBUG("debug gen rollup group row", K(prev_group_row_id), K(curr_group_rowid_),
          K(cur_rollup_group_id), K(output_queue_cnt_), K(start_diff_group_idx), K(idx),
          K(max_group_idx));
      }
    }
    prev_group_row_id = idx;
  }
  return ret;
}

int ObMergeGroupByOp::process_batch(const ObBatchRows &brs)
{
  int ret = OB_SUCCESS;
  uint32_t group_start_idx = 0;
  uint32_t group_end_idx = 0;
  ObDatum *prev_cells = nullptr;
  bool found_new_group = false;
  int64_t diff_group_idx = -1;
  int64_t group_count = MY_SPEC.group_exprs_.count();
  int64_t all_group_cnt = all_groupby_exprs_.count();
  bool no_need_process = false;
  bool need_dup_data = 0 < MY_SPEC.distinct_exprs_.count() && 0 == MY_SPEC.rollup_exprs_.count();
  int cmp_ret = 0;
  LOG_DEBUG("begin process_batch_results", K(brs.size_),
           K(group_start_idx), K(group_end_idx), K(curr_group_rowid_));

  if (OB_FAIL(aggr_processor_.eval_aggr_param_batch(brs))) {
    LOG_WARN("failed to eval_aggr_param_batch");
  }

  ObEvalCtx::BatchInfoScopeGuard batch_info_guard(eval_ctx_);
  batch_info_guard.set_batch_size(brs.size_);
  if (OB_FAIL(ret)) {
  } else if (all_group_cnt > 0) {
    cur_group_last_row_idx_ = -1;
    for (int idx = 0; idx < brs.size_ && OB_SUCC(ret); idx++) {
      if (brs.skip_->at(idx)) {
        continue;
      }
      batch_info_guard.set_batch_idx(idx);
      // check new group
      found_new_group = false;
      no_need_process = false;
      if (nullptr == cur_group_row_) {
        // only first group
        if (OB_FAIL(get_cur_group_row(curr_group_rowid_, cur_group_row_,
            all_groupby_exprs_, all_groupby_exprs_.count()))) {
          LOG_WARN("failed to get one new group row", K(ret));
        } else {
          is_first_calc_ = true;
          cur_group_last_row_idx_ = idx;
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(cur_group_row_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected status: cur_group_row_ is null", K(ret));
      } else {
        prev_cells = cur_group_row_->groupby_store_row_->cells();
      }
      for (int64_t i = 0; OB_SUCC(ret) && !found_new_group && i < all_group_cnt; ++i) {
        const ObDatum &last_datum = prev_cells[i];
        ObExpr *expr = all_groupby_exprs_.at(i);
        // performance critical: use expr directly NO defensive check
        ObDatum &result = expr->locate_expr_datum(eval_ctx_);
        if (OB_FAIL(expr->basic_funcs_->null_first_cmp_(
                last_datum, result, cmp_ret, datum_access_ctx_))) {
          LOG_WARN("compare failed", K(ret));
        } else if (0 != cmp_ret) {
          found_new_group = true;
          if (i < group_count) {
            diff_group_idx = std::max(i, MY_SPEC.group_exprs_.count() - 1);
          } else {
            diff_group_idx = i;
          }
        }
      }

      if (OB_FAIL(ret)) {
      } else if (!found_new_group
          && need_dup_data
          && check_unique_distinct_columns_for_batch(no_need_process, idx)) {
        LOG_WARN("failed to check unique distinct columns", K(ret));
      } else if (no_need_process) {
        // set the currunt row should not be processed
        brs.skip_->set(idx);
      } else if (found_new_group) {
        // calc last group result
        group_end_idx = idx;
        cur_group_last_row_idx_ = idx;
        LOG_DEBUG("new group found, calc ast group result", K(brs.size_),
                  K(group_start_idx), K(group_end_idx), K(idx),
                  K(curr_group_rowid_), K(output_queue_cnt_));
        inc_output_queue_cnt();
        if (OB_FAIL(aggregate_group_rows(curr_group_rowid_, brs, group_start_idx,
                                        group_end_idx))) {
          LOG_WARN("failed to aggregate_group_rows", K(curr_group_rowid_), K(ret),
                  K(group_start_idx), K(group_end_idx));
        } else {
          const int64_t advance_collect_group_id = curr_group_rowid_;
          if (MY_SPEC.has_rollup_) {
            int64_t start_rollup_id = diff_group_idx;
            int64_t end_rollup_id = all_groupby_exprs_.count() - 1;
            int64_t max_group_idx = MY_SPEC.group_exprs_.count() - 1;
            LOG_DEBUG("debug grouping_id", K(end_rollup_id), K(start_rollup_id), K(max_group_idx));
            if (end_rollup_id >= start_rollup_id && OB_FAIL(gen_rollup_group_rows(
                  start_rollup_id,
                  end_rollup_id,
                  max_group_idx,
                  curr_group_rowid_))) {
              LOG_WARN("failed to genereate rollup group row", K(ret));
            }
          }
          if (OB_FAIL(ret)) {
          } else if (aggr_processor_.get_need_advance_collect()
            && OB_FAIL(advance_collect_result(advance_collect_group_id))) {
            LOG_WARN("failed to collect distinct result", K(ret), K(advance_collect_group_id));
          } else {
            ++curr_group_rowid_;
            // create new group
            if (OB_FAIL(get_cur_group_row(curr_group_rowid_, cur_group_row_,
                all_groupby_exprs_, all_groupby_exprs_.count()))) {
              LOG_WARN("failed to get one new group row", K(ret));
            } else {
              group_start_idx = idx; // record new start idx in next round
            }
          }
        }
      }
    }
  } else { // no groupby column, equals to scalar group by
    if (nullptr == cur_group_row_) {
      if (OB_FAIL(get_cur_group_row(curr_group_rowid_, cur_group_row_, all_groupby_exprs_,
          all_groupby_exprs_.count()))) {
        LOG_WARN("failed to get new group row", K(ret));
      }
    }
  }

  group_end_idx = brs.size_;
  LOG_DEBUG("calc last unfinished group row", K(brs.size_),
            K(found_new_group), K(curr_group_rowid_), K(group_start_idx),
            K(group_end_idx));
  // curr_group_rowid_ is common::OB_INVALID_INDEX means all rows are skipped
  // therefore, do nothing when all rows are skipped
  if (OB_SUCC(ret) && curr_group_rowid_ != common::OB_INVALID_INDEX &&
      OB_FAIL(aggregate_group_rows(curr_group_rowid_, brs, group_start_idx,
                                   group_end_idx))) {
    LOG_WARN("failed to aggregate_group_rows", K(ret), K(curr_group_rowid_),
             K(group_start_idx), K(group_end_idx));
  }
  if (OB_SUCC(ret) && 0 < MY_SPEC.distinct_exprs_.count() && -1 != cur_group_last_row_idx_) {
    // the current row is not same as before row, then save the current row
    ObEvalCtx::BatchInfoScopeGuard batch_info_guard(eval_ctx_);
    batch_info_guard.set_batch_size(brs.size_);
    batch_info_guard.set_batch_idx(cur_group_last_row_idx_);
    if (OB_FAIL(last_child_output_.save_store_row(child_->get_spec().output_,
                                                  eval_ctx_,
                                                  0))) {
      LOG_WARN("failed to store child output", K(ret));
    }
  }

  return ret;
}

int ObMergeGroupByOp::groupby_datums_eval_batch(const ObBitVector &skip,
                                          const int64_t size)
{
  int ret = OB_SUCCESS;
  int64_t all_count = all_groupby_exprs_.count();

  for (int64_t i = 0; OB_SUCC(ret) && i < all_count; i++) {
    ObExpr *expr = all_groupby_exprs_.at(i);
    if (OB_FAIL(expr->eval_batch(eval_ctx_, skip, size))) {
      LOG_WARN("eval failed", K(ret));
    }
  }

  return ret;
}

int ObMergeGroupByOp::prepare_and_save_curr_groupby_datums(
    int64_t curr_group_rowid,
    ObAggregateProcessor::GroupRow *group_row,
    ObIArray<ObExpr*> &group_exprs,
    const int64_t group_count)
{
  int ret = OB_SUCCESS;
  UNUSED(group_count);
  // save current groupby row
  ObChunkDatumStore::LastStoredRow *groupby_store_row = nullptr;
  if (OB_FAIL(get_groupby_store_row(curr_group_rowid, &groupby_store_row))) {
    LOG_WARN("failed to get_groupby_store_row", K(ret));
  } else if (OB_FAIL(groupby_store_row->save_store_row(group_exprs, eval_ctx_, 0))) {
    LOG_WARN("failed to store group row", K(ret));
  } else {
    group_row->groupby_store_row_ = groupby_store_row->store_row_;
  }
  LOG_DEBUG("finish prepare and save groupby store row", K(group_exprs), K(ret),
    K(ROWEXPR2STR(eval_ctx_, group_exprs)));
  return ret;
}

int ObMergeGroupByOp::check_same_group(
  ObAggregateProcessor::GroupRow *cur_group_row, int64_t &diff_pos)
{
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  diff_pos = OB_INVALID_INDEX;
  int64_t all_group_cnt = all_groupby_exprs_.count();
  if (0 >= all_group_cnt) {
  } else if (OB_ISNULL(cur_group_row->groupby_store_row_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected status: groupby store row is NULL", K(ret));
  } else {
    ObDatum *prev_cells = nullptr;
    int64_t group_count = MY_SPEC.group_exprs_.count();
    bool found_new_group = false;
    ObDatum *result = nullptr;
    prev_cells = cur_group_row->groupby_store_row_->cells();
    for (int64_t i = 0; OB_SUCC(ret) && !found_new_group && i < all_group_cnt; ++i) {
      const ObDatum &last_datum = prev_cells[i];
      ObExpr *expr = all_groupby_exprs_.at(i);
      // performance critical: use expr directly NO defensive check
      if (OB_FAIL(expr->eval(eval_ctx_, result))) {
        LOG_WARN("eval failed", K(ret));
      } else {
        if (OB_FAIL(expr->basic_funcs_->null_first_cmp_(
                last_datum, *result, cmp_ret, datum_access_ctx_))) {
          LOG_WARN("compare failed", K(ret));
        } else if (0 != cmp_ret) {
          found_new_group = true;
          diff_pos = i;
        } // end if
      } // end if
    } // end for
    LOG_DEBUG("finish check same group", K(diff_pos), K(ret), K(group_count), K(all_group_cnt),
      "row" , ROWEXPR2STR(eval_ctx_, all_groupby_exprs_), K(found_new_group));
  }
  return ret;
}

int ObMergeGroupByOp::check_unique_distinct_columns(
  ObAggregateProcessor::GroupRow *cur_group_row, bool &is_same_before_row)
{
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  ObDatum *prev_cells = nullptr;
  ObDatum *cur_datum = nullptr;
  is_same_before_row = true;
  if (OB_ISNULL(last_child_output_.store_row_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected status: last child output is null", K(ret));
  } else if (OB_INVALID_INDEX_INT64 == MY_SPEC.aggr_code_idx_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected status: invalid aggr_code_idx", K(ret), K(MY_SPEC.aggr_code_idx_));
  } else {
    prev_cells = last_child_output_.store_row_->cells();
    ObDatum &aggr_code_datum = cur_group_row->groupby_store_row_->cells()[MY_SPEC.aggr_code_idx_];
    if (aggr_code_datum.get_int() >= MY_SPEC.dist_aggr_group_idxes_.count()) {
      // non-distinct aggregate function
      is_same_before_row = false;
      LOG_DEBUG("debug non-distinct aggregate function", K(ret),
        K(aggr_code_datum.get_int()), K(MY_SPEC.dist_aggr_group_idxes_.count()));
    } else {
      for (int64_t i = 0; is_same_before_row && i < distinct_col_idx_in_output_.count() && OB_SUCC(ret); ++i) {
        if (-1 == distinct_col_idx_in_output_.at(i)) {
          continue;
        }
        const ObDatum &last_datum = prev_cells[distinct_col_idx_in_output_.at(i)];
        ObExpr *expr = MY_SPEC.distinct_exprs_.at(i);
        // performance critical: use expr directly NO defensive check
        if (OB_FAIL(expr->eval(eval_ctx_, cur_datum))) {
          LOG_WARN("eval failed", K(ret));
        } else {
          if (OB_FAIL(expr->basic_funcs_->null_first_cmp_(
                  last_datum, *cur_datum, cmp_ret, datum_access_ctx_))) {
            LOG_WARN("compare failed", K(ret));
          } else if (0 != cmp_ret) {
            is_same_before_row = false;
          } // end if
        } // end if
      } // end for

      if (OB_SUCC(ret) && !is_same_before_row) {
        // it's not same as prev row, then save the new distinct row
        if (OB_FAIL(last_child_output_.save_store_row(child_->get_spec().output_,
                                                      eval_ctx_,
                                                      0))) {
          LOG_WARN("failed to store child output", K(ret));
        }
      }
    }
    LOG_DEBUG("finish check unique distinct columns", K(ret),
      "row" , ROWEXPR2STR(eval_ctx_, MY_SPEC.distinct_exprs_), K(is_same_before_row),
      K(is_first_calc_), K(aggr_code_datum.get_int()), K(MY_SPEC.dist_aggr_group_idxes_.count()));
  }
  LOG_DEBUG("finish check unique distinct columns", K(ret),
        "row" , ROWEXPR2STR(eval_ctx_, MY_SPEC.distinct_exprs_), K(is_same_before_row));
  return ret;
}

int ObMergeGroupByOp::check_unique_distinct_columns_for_batch(
  bool &is_same_before_row, int64_t cur_row_idx)
{
  int ret = OB_SUCCESS;
  int cmp_ret = 0;
  if (is_first_calc_) {
    is_same_before_row = false;
    LOG_DEBUG("debug is_first_calc", K(ret), K(is_same_before_row));
  } else {
    is_same_before_row = true;
    ObDatum &aggr_code_datum = cur_group_row_->groupby_store_row_->cells()[MY_SPEC.aggr_code_idx_];
    if (aggr_code_datum.get_int() >= MY_SPEC.dist_aggr_group_idxes_.count()) {
      // non-distinct aggregate function
      is_same_before_row = false;
      LOG_DEBUG("debug non-distinct aggregate function", K(ret),
        K(aggr_code_datum.get_int()), K(MY_SPEC.dist_aggr_group_idxes_.count()));
    } else if (-1 != cur_group_last_row_idx_) {
      // non first batch
      for (int64_t i = 0; is_same_before_row && i < distinct_col_idx_in_output_.count() && OB_SUCC(ret); ++i) {
        if (-1 == distinct_col_idx_in_output_.at(i)) {
          continue;
        }
        ObExpr *expr = MY_SPEC.distinct_exprs_.at(i);
        ObDatumVector datums = expr->locate_expr_datumvector(eval_ctx_);
        if (OB_FAIL(expr->basic_funcs_->null_first_cmp_(*datums.at(cur_group_last_row_idx_),
                                                        *datums.at(cur_row_idx),
                                                        cmp_ret,
                                                        datum_access_ctx_))) {
          LOG_WARN("compare failed", K(ret));
        } else if (0 != cmp_ret) {
          is_same_before_row = false;
        } // end if
      } // end for
      LOG_DEBUG("debug non-distinct aggregate function", K(ret), K(aggr_code_datum.get_int()),
        K(MY_SPEC.dist_aggr_group_idxes_.count()));
    } else if (nullptr != last_child_output_.store_row_) {
      ObDatum *prev_cells = nullptr;
      prev_cells = last_child_output_.store_row_->cells();
      for (int64_t i = 0; is_same_before_row && i < distinct_col_idx_in_output_.count() && OB_SUCC(ret); ++i) {
        if (-1 == distinct_col_idx_in_output_.at(i)) {
          continue;
        }
        const ObDatum &last_datum = prev_cells[distinct_col_idx_in_output_.at(i)];
        ObExpr *expr = MY_SPEC.distinct_exprs_.at(i);
        ObDatum &result = expr->locate_expr_datum(eval_ctx_);
        if (OB_FAIL(expr->basic_funcs_->null_first_cmp_(
                last_datum, result, cmp_ret, datum_access_ctx_))) {
          LOG_WARN("compare failed", K(ret));
        } else if (0 != cmp_ret) {
          is_same_before_row = false;
        } // end if
      } // end for
      LOG_DEBUG("finish check unique distinct columns", K(ret), K(aggr_code_datum.get_int()),
        "row" , ROWEXPR2STR(eval_ctx_, MY_SPEC.distinct_exprs_), K(is_same_before_row));
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected status: invalid last group row", K(ret));
    }
  }
  LOG_DEBUG("finish check unique distinct columns", K(ret),
    "row" , ROWEXPR2STR(eval_ctx_, MY_SPEC.distinct_exprs_), K(is_same_before_row));
  cur_group_last_row_idx_ = cur_row_idx;
  is_first_calc_ = false;
  return ret;
}

int ObMergeGroupByOp::restore_groupby_datum(
  ObAggregateProcessor::GroupRow *cur_group_row, const int64_t diff_pos)
{
  int ret = OB_SUCCESS;
  UNUSED(diff_pos);
  ObDatum *groupby_cells = cur_group_row->groupby_store_row_->cells();
  // it must use the groupby datum
  // bug#32738630, if the column is case insensitive
  //  then the different position is not the case insensitive column,
  //  and we set the previous groupby value from the case insensitive value of different groupby,
  // so the case insensitive is not same if the current group use group values of the current row
  // case: merge_gby_return_prev_gby_col_bug.test
  for (int64_t i = 0; OB_SUCC(ret) && i < all_groupby_exprs_.count(); ++i) {
    ObDatum &last_datum = groupby_cells[i];
    ObExpr *expr = all_groupby_exprs_.at(i);
    ObDatum &result = expr->locate_expr_datum(eval_ctx_);
    result.set_datum(last_datum);
    expr->set_evaluated_projected(eval_ctx_);
    LOG_DEBUG("succ to restore", K(i), KPC(expr), K(result), K(last_datum));
  }
  LOG_DEBUG("finish restore groupby datum", K(diff_pos), K(ret));
  return ret;
}

int ObMergeGroupByOp::rollup_and_calc_results(const int64_t group_id,
    const ObExpr *diff_expr /*= NULL*/)
{
  int ret = OB_SUCCESS;
  const int64_t col_count = MY_SPEC.group_exprs_.count() + MY_SPEC.rollup_exprs_.count();
  if (OB_UNLIKELY(group_id < 0 || group_id > col_count)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(group_id), K(col_count), K(ret));
  } else if (MY_SPEC.has_rollup_ && group_id > 0) {
    const int64_t rollup_group_id = group_id - 1;
    if (OB_FAIL(aggr_processor_.rollup_process(group_id, rollup_group_id,
        MY_SPEC.group_exprs_.count(), diff_expr))) {
      LOG_WARN("failed to rollup aggregation results", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    clear_evaluated_flag();
    if (OB_FAIL(aggr_processor_.collect(group_id, diff_expr, MY_SPEC.group_exprs_.count()))) {
      LOG_WARN("failed to collect aggr result", K(group_id), K(ret));
    } else if (OB_FAIL(aggr_processor_.reuse_group(group_id))) {
      LOG_WARN("failed to reuse group", K(group_id), K(ret));
    } else {
      LOG_DEBUG("finish rollup and calc results", K(group_id), K(is_end_),
                "row", ROWEXPR2STR(eval_ctx_, MY_SPEC.output_));
    }
  }
  return ret;
}

int ObMergeGroupByOp::calc_batch_results(const bool is_iter_end,
                                         const int64_t max_output_size)
{
  // TODO: support rollup logic
  int ret = OB_SUCCESS;
  if (nullptr == cur_group_row_) {
    // get empty result from child, just return empty
    brs_.size_ = 0;
    brs_.end_ = is_iter_end;
    LOG_DEBUG("debug cur_group_row_ is null", K(ret));
  } else {
    if (is_iter_end) {
      is_end_ = true;
    }

    // make sure output size is less than aggr group_rows
    int64_t output_size = common::min(get_output_queue_cnt(), max_output_size);
    clear_evaluated_flag();
    // note: brs_.size_ is set in collect_result_batch
    if (OB_FAIL(aggr_processor_.collect_result_batch(
            all_groupby_exprs_, output_size, brs_, cur_output_group_id_))) {
      LOG_WARN("failed to collect batch result", K(ret));
    } else {
      LOG_DEBUG("collect result done", K(cur_output_group_id_),
                K(get_output_queue_cnt()), K(is_iter_end), K(output_size),
                K(aggr_processor_.get_group_rows_count()));
      set_output_queue_cnt(get_output_queue_cnt() - brs_.size_);
      if (!is_output_queue_not_empty()) {
        LOG_DEBUG("all output row are consumed, do the cleanup",
                  K(cur_output_group_id_), K(get_output_queue_cnt()),
                  K(is_iter_end), K(output_size),
                  K(aggr_processor_.get_group_rows_count()),
                  K(curr_group_rowid_));

        // aggregation rows cleanup and reuse
        int64_t start_pos = MY_SPEC.has_rollup_ ? all_groupby_exprs_.count(): 0;
        for (auto i = start_pos; OB_SUCC(ret) && i < curr_group_rowid_; i++) {
          if (OB_FAIL(aggr_processor_.reuse_group(i, false))) {
            LOG_WARN("failed to collect result ", K(ret), K(i),
                     K(eval_ctx_.get_batch_idx()));
          }
        }
        if (is_end_) {// all output rows are consumed, child operator eached end. mark batch end
          brs_.end_ = true;
        } else {
          // move last unfinished grouprow
          // curr_group_rowid(last group row) calculation is NOT done, move it
          // to group 0
          ObAggregateProcessor::GroupRow *group_row = nullptr;
          if (OB_FAIL(aggr_processor_.swap_group_row(start_pos, curr_group_rowid_))) {
            LOG_WARN("failed to swap aggregation group rows", K(ret),
                     K(curr_group_rowid_));
          } else if (OB_FAIL(aggr_processor_.get_group_row(start_pos, group_row))) {
            LOG_WARN("failed to get group row", K(ret));
          } else if (OB_ISNULL(group_row) ||
                     OB_ISNULL(group_row->groupby_store_row_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("group row is empty", K(ret), KP(group_row),
                     K(curr_group_rowid_));
          } else {
            std::swap(output_groupby_rows_[start_pos],
                      output_groupby_rows_[curr_group_rowid_]);
          }
          curr_group_rowid_ = start_pos;
          cur_output_group_id_ = start_pos;
        }
      }
    }
  }
  return ret;
}

inline int ObMergeGroupByOp::get_groupby_store_row(
    int i, ObChunkDatumStore::LastStoredRow **store_row)
{
  int ret = OB_SUCCESS;
  if (i < output_groupby_rows_.count()) {
    *store_row = output_groupby_rows_.at(i);
  } else if (i == output_groupby_rows_.count()) {
    if (OB_FAIL(create_groupby_store_row(store_row))) {
      LOG_WARN("failed create groupby store row", K(ret));
    }
  } else {
    for (int64_t cur = output_groupby_rows_.count(); OB_SUCC(ret) && cur <= i; ++cur) {
      if (OB_FAIL(create_groupby_store_row(store_row))) {
        LOG_WARN("failed create groupby store row", K(ret));
      }
    }
  }
  return ret;
}

inline int ObMergeGroupByOp::create_groupby_store_row(
           ObChunkDatumStore::LastStoredRow **store_row)
{
  int ret = OB_SUCCESS;
  void *buf = aggr_processor_.get_aggr_alloc().alloc(
      sizeof(ObChunkDatumStore::LastStoredRow));
  if (OB_ISNULL(buf)) {
    LOG_WARN("failed alloc memory", K(ret));
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    *store_row = new (buf)
        ObChunkDatumStore::LastStoredRow(aggr_processor_.get_aggr_alloc());
    if (OB_FAIL(output_groupby_rows_.push_back(*store_row))) {
      LOG_WARN("failed push back", K(ret));
    }
    (*store_row)->reuse_ = true;
  }
  return ret;
}

int ObMergeGroupByOp::init_hp_infras_group_mgr()
{
  int ret = OB_SUCCESS;
  if (aggr_processor_.has_distinct() &&
      !(MY_SPEC.has_rollup_ && aggr_processor_.has_listagg_non_const_separator())) {
    int64_t est_rows = MY_SPEC.est_rows_per_group_;
    aggr_processor_.set_io_event_observer(&io_event_observer_);
    if (OB_FAIL(ObPxEstimateSizeUtil::get_px_size(
        &ctx_, MY_SPEC.px_est_size_factor_, est_rows, est_rows))) {
      LOG_WARN("failed to get px size", K(ret));
    } else if (OB_FAIL(sql_mem_processor_.init(
                    &ctx_.get_allocator(),
                    est_rows * MY_SPEC.width_,
                    MY_SPEC.type_,
                    MY_SPEC.id_,
                    &ctx_))) {
      LOG_WARN("failed to init sql mem processor", K(ret));
    } else if (OB_FAIL(hp_infras_mgr_.init(
      GCONF.is_sql_operator_dump_enabled(), est_rows, MY_SPEC.width_, true/*unique*/, 1/*ways*/,
      &eval_ctx_, &sql_mem_processor_, &io_event_observer_))) {
      LOG_WARN("failed to init hash infras group", K(ret));
    } else {
      aggr_processor_.set_hp_infras_mgr(&hp_infras_mgr_);
      aggr_processor_.set_enable_hash_distinct();
    }
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
