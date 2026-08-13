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
#define USING_LOG_PREFIX STORAGE
#include "ob_where_optimizer.h"
#include "storage/access/ob_table_access_param.h"

namespace oceanbase
{
namespace storage
{
#define REORDER_FILTER_INTERVAL 32
ObWhereOptimizer::ObWhereOptimizer() 
  : iter_param_(nullptr)
  , filter_(nullptr)
  , batch_num_(0)
  , reorder_filter_times_(0)
  , reorder_filter_interval_(1)
  , disable_bypass_(false)
  , is_inited_(false)
{
}

int ObWhereOptimizer::init(
  const ObTableIterParam *iter_param,
  sql::ObPushdownFilterExecutor *filter)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObWhereOptimizer init twice", K(ret));
  } else if (OB_ISNULL(iter_param_ = iter_param) || OB_ISNULL(filter_ = filter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("iter param or filter is null", K(ret), K(iter_param_), K(filter_));
  } else {
    filter_conditions_.reset();
    batch_num_ = 0;
    reorder_filter_times_ = 0;
    reorder_filter_interval_ = 1;
    disable_bypass_ = false;
    judge_filter_whether_enable_reorder(filter);
    is_inited_ = true;
  }
  return ret;
}

void ObWhereOptimizer::reset()
{
  iter_param_ = nullptr;
  filter_ = nullptr;
  filter_conditions_.reset();
  batch_num_ = 0;
  reorder_filter_times_ = 0;
  reorder_filter_interval_ = 1;
  disable_bypass_ = false;
  is_inited_ = false;
}

void ObWhereOptimizer::reuse()
{
  iter_param_ = nullptr;
  filter_ = nullptr;
  filter_conditions_.reuse();
  batch_num_ = 0;
  reorder_filter_times_ = 0;
  reorder_filter_interval_ = 1;
  disable_bypass_ = false;
  is_inited_ = false;
}

int ObWhereOptimizer::analyze(bool &reordered)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ret = analyze_impl(*filter_, reordered);
  }
  return ret;
}

int ObWhereOptimizer::analyze_impl(sql::ObPushdownFilterExecutor &filter, bool &reordered)
{
  int ret = OB_SUCCESS;
  sql::ObPushdownFilterExecutor **children = filter.get_childs();
  const int64_t child_cnt = filter.get_child_count();

  if (filter.is_enable_reorder()) {
    if (OB_FAIL(filter_conditions_.prepare_allocate(child_cnt))) {
    } else {
      for (int64_t i = 0; i < child_cnt; ++i) {
        filter_conditions_.at(i).idx_ = i;
        collect_filter_info(*children[i], filter_conditions_.at(i));
      }

      lib::ob_sort(&filter_conditions_.at(0), &filter_conditions_.at(0) + child_cnt);
      bool need_reorder = false;
      for (int64_t i = 0; i < child_cnt; ++i) {
        if (i != filter_conditions_.at(i).idx_) {
          need_reorder = true;
          break;
        }
      }
      if (need_reorder) {
        for (int64_t i = 0; i < child_cnt; ++i) {
          children[i] = filter_conditions_.at(i).filter_;
        }
        reordered = true;
      }
    }
  } else if (filter.is_logic_op_node()) {
    for (int64_t i = 0; OB_SUCC(ret) && i < child_cnt; ++i) {
      if (OB_FAIL(analyze_impl(*children[i], reordered))) {
      }
    }
  }

  return ret;
}

void ObWhereOptimizer::collect_filter_info(
    sql::ObPushdownFilterExecutor &filter,
    ObFilterCondition &filter_condition)
{
  filter_condition.filter_cost_time_ = filter.get_filter_realtime_statistics().get_filter_cost_time();
  filter_condition.filtered_row_cnt_ = filter.get_filter_realtime_statistics().get_filtered_row_cnt();
  filter_condition.skip_index_skip_mb_cnt_ = filter.get_filter_realtime_statistics().get_skip_index_skip_mb_cnt();
  filter.get_filter_realtime_statistics().reset();
  filter_condition.filter_ = &filter;
}

void ObWhereOptimizer::judge_filter_whether_enable_reorder(sql::ObPushdownFilterExecutor *filter) {
  if (filter == nullptr) {
    // do nothing
  } else if (filter->is_logic_op_node()) {
    bool enable_reorder = true;
    for (int64_t i = 0; i < filter->get_child_count(); ++i) { // enable reorder of this filter if all childs are not logic op nodes
      sql::ObPushdownFilterExecutor *child = filter->get_childs()[i];
      if (child->is_logic_op_node()) {
        enable_reorder = false;
        judge_filter_whether_enable_reorder(child);
      } else if (child->is_sample_node()) {
        enable_reorder = false;
      }
    }
    filter->set_enable_reorder(enable_reorder);
  }
}

int ObWhereOptimizer::reorder_row_filter() {
  int ret = OB_SUCCESS;
  bool reordered = false;
  ++batch_num_;
  if (!filter_->is_logic_op_node()) {
    /* If there is only one node in the filter tree, do nothing. */
  } else if (reorder_filter_times_ >= reorder_filter_interval_) {
    if (OB_FAIL(analyze(reordered))) {
    } else {
      reorder_filter_times_ = 0;
      reorder_filter_interval_ = REORDER_FILTER_INTERVAL;
    }
  } else {
    ++reorder_filter_times_;
  }
  if (reordered) {
    LOG_TRACE("Reorder row filter tree", K(ret), KP(this), K(batch_num_), KP(filter_), K(filter_->get_type()),
      K(reorder_filter_times_), K(reorder_filter_interval_), K(reordered), K(filter_->get_filter_realtime_statistics()));
  }
  return ret;
}

}
}
