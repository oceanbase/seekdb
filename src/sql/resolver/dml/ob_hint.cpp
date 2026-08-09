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

#define USING_LOG_PREFIX SQL_RESV
#include "ob_hint.h"
#include "sql/optimizer/ob_log_plan.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

void ObPhyPlanHint::reset()
{
  read_consistency_ = INVALID_CONSISTENCY;
  query_timeout_ = -1;
  plan_cache_policy_ = OB_USE_PLAN_CACHE_INVALID;
  force_trace_log_ = false;
  log_level_.reset();
  parallel_ = -1;
}

OB_SERIALIZE_MEMBER(ObPhyPlanHint,
                    read_consistency_,
                    query_timeout_,
                    plan_cache_policy_,
                    force_trace_log_,
                    log_level_,
                    parallel_);

int ObPhyPlanHint::deep_copy(const ObPhyPlanHint &other, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  read_consistency_ = other.read_consistency_;
  query_timeout_ = other.query_timeout_;
  plan_cache_policy_ = other.plan_cache_policy_;
  force_trace_log_ = other.force_trace_log_;
  parallel_ = other.parallel_;
  if (OB_FAIL(ob_write_string(allocator, other.log_level_, log_level_))) {
  }
  return ret;
}

int ObGlobalHint::merge_alloc_op_hints(const ObIArray<ObAllocOpHint> &alloc_op_hints)
{
  int ret = OB_SUCCESS;
  bool find = false;
  for (int64_t i = 0; OB_SUCC(ret) && i < alloc_op_hints.count(); ++i) {
    find = false;
    for (int64_t j = 0; j < alloc_op_hints_.count(); ++j) {
      if ((alloc_op_hints.at(i).id_ == alloc_op_hints_.at(j).id_) &&
          (alloc_op_hints.at(i).alloc_level_ == alloc_op_hints_.at(j).alloc_level_)) {
        alloc_op_hints_.at(j).flags_ |= alloc_op_hints.at(i).flags_;
        find = true;
      }
    }
    if (!find) {
      if (OB_FAIL(alloc_op_hints_.push_back(alloc_op_hints.at(i)))) {
      }
    }
  }
  return ret;
}

int ObGlobalHint::merge_dop_hint(uint64_t dfo, uint64_t dop)
{
  int ret = OB_SUCCESS;
  bool find = false;
  for (int64_t i = 0; OB_SUCC(ret) && i < dops_.count(); ++i) {
    if (dfo == dops_.at(i).dfo_) {
      find = true;
      if (dop > dops_.at(i).dop_) {
        dops_.at(i).dop_ = dop;
      }
    }
  }
  if (!find) {
    ObDopHint hint;
    hint.dfo_ = dfo;
    hint.dop_ = dop;
    if (OB_FAIL(dops_.push_back(hint))) {
    }
  }
  return ret;
}

int ObGlobalHint::merge_dop_hint(const ObIArray<ObDopHint> &dop_hints)
{
  int ret = OB_SUCCESS;
  bool find = false;
  for (int64_t i = 0; OB_SUCC(ret) && i < dop_hints.count(); ++i) {
    if (OB_FAIL(merge_dop_hint(dop_hints.at(i).dfo_, dop_hints.at(i).dop_))) {
    }
  }
  return ret;
}

void ObGlobalHint::merge_query_timeout_hint(int64_t hint_time)
{
  if (hint_time > 0) {
    if (OB_UNLIKELY(hint_time > OB_MAX_USER_SPECIFIED_TIMEOUT)) {
      LOG_USER_WARN(OB_ERR_TIMEOUT_TRUNCATED);
      hint_time = std::min(OB_MAX_USER_SPECIFIED_TIMEOUT, hint_time);
    }
    if (query_timeout_ < 0) {
      query_timeout_ = hint_time;
    } else {
      query_timeout_ = std::min(hint_time, query_timeout_);
    }
  }
}





/* global parallel hint priority:
    1. with paralle degree: parallel(8)
    2. enable auto dop: parallel(auto)
    3. disable auto dop: parallel(manual)
 */
void ObGlobalHint::merge_parallel_hint(int64_t parallel)
{
  if (SET_ENABLE_MANUAL_DOP == parallel) {
    parallel_ = UNSET_PARALLEL == parallel_ ? SET_ENABLE_MANUAL_DOP : parallel_;
  } else if (SET_ENABLE_AUTO_DOP == parallel) {
    parallel_ = (UNSET_PARALLEL == parallel_ || SET_ENABLE_MANUAL_DOP == parallel_)
                ? SET_ENABLE_AUTO_DOP : parallel_;
  } else if (UNSET_PARALLEL < parallel) {
    if (UNSET_PARALLEL >= parallel_) {
      parallel_ = parallel;
    } else {
      parallel_ = std::min(parallel, parallel_);
    }
  }
}

void ObGlobalHint::merge_dml_parallel_hint(int64_t dml_parallel)
{
  if (UNSET_PARALLEL < dml_parallel) {
    if (UNSET_PARALLEL >= dml_parallel_) {
      dml_parallel_ = dml_parallel;
    } else {
      dml_parallel_ = std::min(dml_parallel, dml_parallel_);
    }
  }
}

void ObGlobalHint::merge_dynamic_sampling_hint(int64_t dynamic_sampling)
{
  if (dynamic_sampling != UNSET_DYNAMIC_SAMPLING) {
    if (UNSET_DYNAMIC_SAMPLING == dynamic_sampling_ || dynamic_sampling == dynamic_sampling_) {
      dynamic_sampling_ = dynamic_sampling;
    } else {//conflict will reset the original state.
      dynamic_sampling_ = UNSET_DYNAMIC_SAMPLING;
    }
  }
}

void ObGlobalHint::merge_parallel_dml_hint(ObPDMLOption pdml_option)
{
  if (ObPDMLOption::DISABLE != pdml_option_ && ObPDMLOption::ENABLE != pdml_option_) {
    if (ObPDMLOption::DISABLE == pdml_option || ObPDMLOption::ENABLE == pdml_option) {
      pdml_option_ = pdml_option;
    }
  } else if (ObPDMLOption::ENABLE == pdml_option_ || ObPDMLOption::ENABLE == pdml_option) {
    pdml_option_ = ObPDMLOption::ENABLE;
  } else {
    pdml_option_ = ObPDMLOption::DISABLE;
  }
}

void ObGlobalHint::merge_parallel_das_dml_hint(ObParallelDASOption parallel_das_option)
{
  if (ObParallelDASOption::DISABLE != parallel_das_dml_option_ && ObParallelDASOption::ENABLE != parallel_das_dml_option_) {
    if (ObParallelDASOption::DISABLE == parallel_das_option || ObParallelDASOption::ENABLE == parallel_das_option) {
      parallel_das_dml_option_ = parallel_das_option;
    }
  } else if (ObParallelDASOption::ENABLE == parallel_das_dml_option_ || ObParallelDASOption::ENABLE == parallel_das_option) {
    parallel_das_dml_option_ = ObParallelDASOption::ENABLE;
  } else {
    parallel_das_dml_option_ = ObParallelDASOption::DISABLE;
  }
}

void ObGlobalHint::merge_param_option_hint(ObParamOption opt)
{
  if (ObParamOption::FORCE != param_option_ && ObParamOption::EXACT != param_option_) {
    if (ObParamOption::FORCE == opt || ObParamOption::EXACT == opt) {
      param_option_ = opt;
    }
  } else if (ObParamOption::EXACT == param_option_ || ObParamOption::EXACT == opt) {
    param_option_ = ObParamOption::EXACT;
  } else {
    param_option_ = ObParamOption::FORCE;
  }
}

void ObGlobalHint::merge_topk_hint(int64_t precision, int64_t sharding_minimum_row_count)
{
  topk_precision_ = std::max(topk_precision_, precision);
  sharding_minimum_row_count_ = std::max(sharding_minimum_row_count_, sharding_minimum_row_count);
}

void ObGlobalHint::merge_plan_cache_hint(ObPlanCachePolicy policy)
{
  if (ObPlanCachePolicy::OB_USE_PLAN_CACHE_NONE == policy ||
      ObPlanCachePolicy::OB_USE_PLAN_CACHE_NONE == plan_cache_policy_) {
    plan_cache_policy_ = ObPlanCachePolicy::OB_USE_PLAN_CACHE_NONE;
  } else if (ObPlanCachePolicy::OB_USE_PLAN_CACHE_DEFAULT == policy ||
             ObPlanCachePolicy::OB_USE_PLAN_CACHE_DEFAULT == plan_cache_policy_) {
    plan_cache_policy_ = ObPlanCachePolicy::OB_USE_PLAN_CACHE_DEFAULT;
  }
}

// use the first log level hint now.
void ObGlobalHint::merge_log_level_hint(const ObString &log_level)
{
  int tmp_ret = OB_SUCCESS;
  if (!log_level_.empty() || log_level.empty()) {
    // do nothing
  } else if (0 == log_level.case_compare("disabled")) {
    //allowed for variables
    LOG_WARN_RET(tmp_ret, "Log level parse check error", K(tmp_ret));
  } else if (OB_UNLIKELY(tmp_ret = OB_LOGGER.parse_check(log_level.ptr(), log_level.length()))) {
    LOG_WARN_RET(tmp_ret, "Log level parse check error", K(tmp_ret));
  } else {
    log_level_.assign_ptr(log_level.ptr(), log_level.length());
  }
}

void ObGlobalHint::merge_read_consistency_hint(ObConsistencyLevel read_consistency,
                                               int64_t frozen_version)
{
  if (INVALID_CONSISTENCY == read_consistency_) {
    read_consistency_ = read_consistency;
    frozen_version_ = frozen_version;
  } else if (FROZEN == read_consistency_ || FROZEN == read_consistency) {
    read_consistency_ = FROZEN;
    frozen_version_ =  std::max(frozen_version_, frozen_version);
  } else if (WEAK == read_consistency_ || WEAK == read_consistency) {
    read_consistency_ = WEAK;
    frozen_version_ = -1;
  } else if (STRONG == read_consistency_ || STRONG == read_consistency) {
    read_consistency_ = STRONG;
    frozen_version_ = -1;
  }
}

bool ObGlobalHint::has_hint_exclude_concurrent() const
{
  return -1 != frozen_version_
         || -1 != topk_precision_
         || 0 != sharding_minimum_row_count_
         || UNSET_QUERY_TIMEOUT != query_timeout_
         || common::INVALID_CONSISTENCY != read_consistency_
         || OB_USE_PLAN_CACHE_INVALID != plan_cache_policy_
         || force_trace_log_
         || enable_lock_early_release_
         || !log_level_.empty()
         || has_parallel_hint()
         || has_dml_parallel_hint()
         || ObPDMLOption::NOT_SPECIFIED != pdml_option_
         || ObParamOption::NOT_SPECIFIED != param_option_
         || !alloc_op_hints_.empty()
         || !dops_.empty()
         || false != disable_transform_
         || false != disable_cost_based_transform_
         || !opt_params_.empty()
         || has_gather_opt_stat_hint()
         || false != has_dbms_stats_hint_
         || -1 != dynamic_sampling_
         || ObParallelDASOption::NOT_SPECIFIED != parallel_das_dml_option_;
}

void ObGlobalHint::reset()
{
  frozen_version_ = -1;
  topk_precision_ = -1;
  sharding_minimum_row_count_ = 0;
  query_timeout_ = UNSET_QUERY_TIMEOUT;
  read_consistency_ = common::INVALID_CONSISTENCY;
  plan_cache_policy_ = OB_USE_PLAN_CACHE_INVALID;
  force_trace_log_ = false;
  enable_lock_early_release_ = false;
  log_level_.reset();
  parallel_ = UNSET_PARALLEL;
  dml_parallel_ = UNSET_PARALLEL;
  pdml_option_ = ObPDMLOption::NOT_SPECIFIED;
  param_option_ = ObParamOption::NOT_SPECIFIED;
  dops_.reuse();
  disable_transform_ = false;
  disable_cost_based_transform_ = false;
  opt_params_.reset();
  osg_hint_.flags_ = 0;
  has_dbms_stats_hint_ = false;
  dynamic_sampling_ = ObGlobalHint::UNSET_DYNAMIC_SAMPLING;
  alloc_op_hints_.reuse();
  parallel_das_dml_option_ = ObParallelDASOption::NOT_SPECIFIED;
}

int ObGlobalHint::merge_global_hint(const ObGlobalHint &other)
{
  int ret = OB_SUCCESS;
  merge_read_consistency_hint(other.read_consistency_, other.frozen_version_);
  merge_topk_hint(other.topk_precision_, other.sharding_minimum_row_count_);
  merge_query_timeout_hint(other.query_timeout_);
  merge_log_level_hint(other.log_level_);
  merge_plan_cache_hint(other.plan_cache_policy_);
  merge_parallel_dml_hint(other.pdml_option_);
  force_trace_log_ |= other.force_trace_log_;
  merge_parallel_hint(other.parallel_);
  merge_dml_parallel_hint(other.dml_parallel_);
  merge_param_option_hint(other.param_option_);
  disable_transform_ |= other.disable_transform_;
  disable_cost_based_transform_ |= other.disable_cost_based_transform_;
  osg_hint_.flags_ |= other.osg_hint_.flags_;
  has_dbms_stats_hint_ |= other.has_dbms_stats_hint_;
  merge_parallel_das_dml_hint(other.parallel_das_dml_option_);
  merge_dynamic_sampling_hint(other.dynamic_sampling_);
  if (OB_FAIL(merge_alloc_op_hints(other.alloc_op_hints_))) {
  } else if (OB_FAIL(merge_dop_hint(other.dops_))) {
  } else if (OB_FAIL(opt_params_.merge_opt_param_hint(other.opt_params_))) {
  }
  return ret;
}

int ObGlobalHint::assign(const ObGlobalHint &other)
{
  reset();
  return merge_global_hint(other);
}

int ObGlobalHint::print_global_hint(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  const char* outline_indent = ObQueryHint::get_outline_indent(plan_text.is_oneline_);

  #define PRINT_GLOBAL_HINT_STR(hint_str)           \
  if (OB_FAIL(BUF_PRINTF("%s%s", outline_indent, hint_str))) {  \
    LOG_WARN("failed to print hint", K(ret), K(hint_str)); }    \

  #define PRINT_GLOBAL_HINT_NUM(hint_name, num)           \
  if (OB_FAIL(BUF_PRINTF("%s%s(%ld)", outline_indent, hint_name, num))) { \
    LOG_WARN("failed to print hint", K(ret), K(hint_name)); }             \

  //TOPK
  if (OB_SUCC(ret) && (-1 < topk_precision_ || 0 < sharding_minimum_row_count_)) {
    if (OB_FAIL(BUF_PRINTF("%sTOPK(%ld %ld)", outline_indent, topk_precision_,
                                                sharding_minimum_row_count_))) {
    }
  }

  // TRACING & STAT & BLOCKING
  if (OB_SUCC(ret) && !alloc_op_hints_.empty()) {
    if (OB_FAIL(print_alloc_op_hints(plan_text))) {
    }
  }

  //DOP
  if (OB_SUCC(ret) && !dops_.empty()) {
    for (int64_t i = 0; OB_SUCC(ret) && i < dops_.count(); ++i) {
      if (OB_FAIL(BUF_PRINTF("%sDOP(%lu, %lu)", outline_indent, dops_.at(i).dfo_, dops_.at(i).dop_))) {
      }
    }
  }

  //READ_CONSISTENCY && FROZEN_VERSION
  if (OB_SUCC(ret) && (read_consistency_ != UNSET_CONSISTENCY)) {
    if (FROZEN == read_consistency_ && frozen_version_ != -1) {
      PRINT_GLOBAL_HINT_NUM("FROZEN_VERSION", frozen_version_);
    } else if (FROZEN == read_consistency_) {
      PRINT_GLOBAL_HINT_STR("READ_CONSISTENCY( FROZEN )");
    } else if (WEAK == read_consistency_) {
      PRINT_GLOBAL_HINT_STR("READ_CONSISTENCY( WEAK )");
    } else if (STRONG == read_consistency_) {
      PRINT_GLOBAL_HINT_STR("READ_CONSISTENCY( STRONG )");
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected READ_CONSISTENCY", K(ret), K_(read_consistency));
    }
  }
  if (OB_SUCC(ret) && UNSET_QUERY_TIMEOUT != query_timeout_) { //QUERY_TIMEOUT
    PRINT_GLOBAL_HINT_NUM("QUERY_TIMEOUT", query_timeout_);
  }
  if (OB_SUCC(ret) && plan_cache_policy_ != OB_USE_PLAN_CACHE_INVALID) { //USE_PLAN_CACHE
    const char *plan_cache_policy = "INVALID";
    if (OB_USE_PLAN_CACHE_NONE == plan_cache_policy_) {
      PRINT_GLOBAL_HINT_STR("USE_PLAN_CACHE( NONE )");
    } else if (OB_USE_PLAN_CACHE_DEFAULT == plan_cache_policy_) {
      PRINT_GLOBAL_HINT_STR("USE_PLAN_CACHE( DEFAULT )");
    } else { }
  }
  if (OB_SUCC(ret) && force_trace_log_) { //TRACE_LOG
    PRINT_GLOBAL_HINT_STR("TRACE_LOG");
  }
  if (OB_SUCC(ret) && !log_level_.empty()) { //LOG_LEVEL
    if (OB_FAIL(BUF_PRINTF("%sLOG_LEVEL(\"%.*s\")", outline_indent,
                                    log_level_.length(), log_level_.ptr() ))) {
    }
  }
  if (OB_SUCC(ret) && has_parallel_hint()) { //PARALLEL
    if (has_parallel_degree()) {
      PRINT_GLOBAL_HINT_NUM("PARALLEL", parallel_);
    } else if (enable_auto_dop()) {
      PRINT_GLOBAL_HINT_STR("PARALLEL( AUTO )");
    } else if (enable_manual_dop()) {
      PRINT_GLOBAL_HINT_STR("PARALLEL( MANUAL )");
    }
  }
  if (OB_SUCC(ret) && has_dml_parallel_hint()) { //DML_PARALLEL
    PRINT_GLOBAL_HINT_NUM("DML_PARALLEL", dml_parallel_);
  }
  if (OB_SUCC(ret) && ObPDMLOption::NOT_SPECIFIED != pdml_option_) { //PDML
    if (ObPDMLOption::ENABLE == pdml_option_) {
      PRINT_GLOBAL_HINT_STR("ENABLE_PARALLEL_DML");
    } else if (ObPDMLOption::DISABLE == pdml_option_) {
      PRINT_GLOBAL_HINT_STR("DISABLE_PARALLEL_DML");
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected pdml hint value", K(ret), K_(pdml_option));
    }
  }
  if (OB_SUCC(ret) && ObParallelDASOption::NOT_SPECIFIED != parallel_das_dml_option_) { //PDML
   if (ObParallelDASOption::ENABLE == parallel_das_dml_option_) {
     PRINT_GLOBAL_HINT_STR("ENABLE_PARALLEL_DAS_DML");
   } else if (ObParallelDASOption::DISABLE == parallel_das_dml_option_) {
     PRINT_GLOBAL_HINT_STR("DISABLE_PARALLEL_DAS_DML");
   } else {
     ret = OB_ERR_UNEXPECTED;
     LOG_WARN("unexpected pdml hint value", K(ret), K_(pdml_option));
   }
  }
  if (OB_SUCC(ret) && ObParamOption::NOT_SPECIFIED != param_option_) { // PARAM
    if (OB_UNLIKELY(ObParamOption::EXACT != param_option_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected param hint value", K(ret), K_(param_option));
    } else {
      PRINT_GLOBAL_HINT_STR("CURSOR_SHARING_EXACT");
    }
  }
  if (OB_SUCC(ret) && disable_query_transform()) {
    PRINT_GLOBAL_HINT_STR("NO_QUERY_TRANSFORMATION");
  }
  if (OB_SUCC(ret) && disable_cost_based_transform()) {
    PRINT_GLOBAL_HINT_STR("NO_COST_BASED_QUERY_TRANSFORMATION");
  }
  if (OB_SUCC(ret) && UNSET_DYNAMIC_SAMPLING != dynamic_sampling_) { //DYNAMIC SAMPLING
    PRINT_GLOBAL_HINT_NUM("DYNAMIC_SAMPLING", dynamic_sampling_);
  }
  if (OB_SUCC(ret) && OB_FAIL(opt_params_.print_opt_param_hint(plan_text))) {
    LOG_WARN("failed to print opt param hint", K(ret));
  }
  if (OB_SUCC(ret) && OB_FAIL(osg_hint_.print_osg_hint(plan_text))) {
    LOG_WARN("failed to print optimizer statistics gathering hint", K(ret));
  }
  if (OB_SUCC(ret) && has_dbms_stats_hint()) {
    PRINT_GLOBAL_HINT_STR("DBMS_STATS");
  }
  return ret;
}

void ObGlobalHint::merge_osg_hint(int8_t flag) {
  osg_hint_.flags_ |= flag;
}


int ObOptimizerStatisticsGatheringHint::print_osg_hint(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  const char* outline_indent = ObQueryHint::get_outline_indent(plan_text.is_oneline_);
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  if ((flags_ & OB_NO_OPT_STATS_GATHER)) {
    PRINT_GLOBAL_HINT_STR("NO_GATHER_OPTIMIZER_STATISTICS");
  }
  if (OB_SUCC(ret) && (flags_ & OB_OPT_STATS_GATHER)) {
    PRINT_GLOBAL_HINT_STR("GATHER_OPTIMIZER_STATISTICS");
  }
  return ret;
}

int ObGlobalHint::print_alloc_op_hints(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  if (!alloc_op_hints_.empty()) {
    char *buf = plan_text.buf_;
    int64_t &buf_len = plan_text.buf_len_;
    int64_t &pos = plan_text.pos_;
    const char* outline_indent = ObQueryHint::get_outline_indent(plan_text.is_oneline_);
    ObSEArray<uint64_t, 4> tracing_ids;
    ObSEArray<uint64_t, 4> stat_ids;
    ObSEArray<uint64_t, 4> blocking_ids;
    for (int64_t i = 0; OB_SUCC(ret) && i < alloc_op_hints_.count(); ++i) {
      if (alloc_op_hints_.at(i).flags_ & ObAllocOpHint::OB_MONITOR_TRACING) {
        if (ObAllocOpHint::OB_ENUMERATE == alloc_op_hints_.at(i).alloc_level_
                   && OB_FAIL(tracing_ids.push_back(alloc_op_hints_.at(i).id_))){
          LOG_WARN("failed to push back", K(ret));
        }
      }
      if (OB_SUCC(ret) && alloc_op_hints_.at(i).flags_ & ObAllocOpHint::OB_MONITOR_STAT) {
        if (ObAllocOpHint::OB_ENUMERATE == alloc_op_hints_.at(i).alloc_level_
                   && OB_FAIL(stat_ids.push_back(alloc_op_hints_.at(i).id_))){
          LOG_WARN("failed to push back", K(ret));
        }
      }
      if (OB_SUCC(ret) && alloc_op_hints_.at(i).flags_ & ObAllocOpHint::OB_MATERIAL) {
        if (ObAllocOpHint::OB_ALL == alloc_op_hints_.at(i).alloc_level_
            && OB_FAIL(BUF_PRINTF("%sBLOCKING('ALL')", outline_indent))) {
          LOG_WARN("failed to print blocking hint", K(ret));
        } else if (ObAllocOpHint::OB_DFO == alloc_op_hints_.at(i).alloc_level_
                   && OB_FAIL(BUF_PRINTF("%sBLOCKING('DFO')", outline_indent))) {
          LOG_WARN("failed to print blocking hint", K(ret));
        } else if (ObAllocOpHint::OB_ENUMERATE == alloc_op_hints_.at(i).alloc_level_
                   && OB_FAIL(blocking_ids.push_back(alloc_op_hints_.at(i).id_))){
          LOG_WARN("failed to push back", K(ret));
        }
      }
    }
    if (OB_SUCC(ret) && !tracing_ids.empty()) {
      if (OB_FAIL(BUF_PRINTF("%sTRACING(%lu", outline_indent, tracing_ids.at(0)))) {
      }
      for (int64_t i = 1; OB_SUCC(ret) && i < tracing_ids.count(); ++i) {
        if (OB_FAIL(BUF_PRINTF(" %lu", tracing_ids.at(i)))) {
        } 
      }
      if (OB_SUCC(ret) && OB_FAIL(BUF_PRINTF(")"))) {
        LOG_WARN("failed to print tracing hint", K(ret));
      }
    }
    if (OB_SUCC(ret) && !stat_ids.empty()) {
      if (OB_FAIL(BUF_PRINTF("%sSTAT(%lu", outline_indent, stat_ids.at(0)))) {
      }
      for (int64_t i = 1; OB_SUCC(ret) && i < stat_ids.count(); ++i) {
        if (OB_FAIL(BUF_PRINTF(" %lu", stat_ids.at(i)))) {
        } 
      }
      if (OB_SUCC(ret) && OB_FAIL(BUF_PRINTF(")"))) {
        LOG_WARN("failed to print tracing hint", K(ret));
      }
    }
    if (OB_SUCC(ret) && !blocking_ids.empty()) {
      if (OB_FAIL(BUF_PRINTF("%sBLOCKING(%lu", outline_indent, blocking_ids.at(0)))) {
      }
      for (int64_t i = 1; OB_SUCC(ret) && i < blocking_ids.count(); ++i) {
        if (OB_FAIL(BUF_PRINTF(" %lu", blocking_ids.at(i)))) {
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(BUF_PRINTF(")"))) {
        LOG_WARN("failed to print blocking hint", K(ret));
      }
    }
  }
  return ret;
}

DEFINE_ENUM_FUNC(ObOptParamHint::OptParamType, opt_param, OPT_PARAM_TYPE_DEF, ObOptParamHint::);

int ObOptParamHint::print_opt_param_hint(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_types_.count() != param_vals_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected opt param hint", K(ret), K(*this));
  } else if (param_types_.empty()) {
    /* do nothing*/
  } else {
    char *buf = plan_text.buf_;
    int64_t &buf_len = plan_text.buf_len_;
    int64_t &pos = plan_text.pos_;
    const char* outline_indent = ObQueryHint::get_outline_indent(plan_text.is_oneline_);
    const char* param_name = NULL;
    for (int64_t i = 0; OB_SUCC(ret) && i < param_types_.count(); ++i) {
      if (OB_UNLIKELY(OptParamType::INVALID_OPT_PARAM_TYPE == param_types_.at(i))
          || OB_ISNULL(param_name = get_opt_param_string(param_types_.at(i)))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected param type in opt param hint", K(ret), K(param_types_.at(i)));
      } else if (OB_UNLIKELY(!param_vals_.at(i).is_int() && !param_vals_.at(i).is_varchar())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected param value type", K(ret), K(param_vals_.at(i)));
      } else if (OB_FAIL(BUF_PRINTF("%sOPT_PARAM(\'%s\'", outline_indent, param_name))) {
      } else if (param_vals_.at(i).is_int() &&
                 OB_FAIL(BUF_PRINTF(" %ld)", param_vals_.at(i).get_int()))) {
        LOG_WARN("failed to opt param hint value", K(ret), K(param_vals_.at(i)));
      } else if (param_vals_.at(i).is_varchar() &&
                 OB_FAIL(BUF_PRINTF(" \'%.*s\')", param_vals_.at(i).get_varchar().length(),
                                                  param_vals_.at(i).get_varchar().ptr()))) {
        LOG_WARN("failed to opt param hint value", K(ret), K(param_vals_.at(i)));
      }
    }
  }
  return ret;
}

int ObOptParamHint::merge_opt_param_hint(const ObOptParamHint &other)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_types_.count() != param_vals_.count()
                  || other.param_types_.count() != other.param_vals_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected opt param hint", K(ret), K(other));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < other.param_types_.count(); ++i) {
    if (OB_FAIL(add_opt_param_hint(other.param_types_.at(i), other.param_vals_.at(i)))) {
    }
  }
  return ret;
}

int ObOptParamHint::add_opt_param_hint(const OptParamType param_type, const ObObj &val)
{
  int ret = OB_SUCCESS;
  ObObj cur_value;
  if (!is_param_val_valid(param_type, val)) {
    /* do nothing */
  } else if (OB_FAIL(get_opt_param(param_type, cur_value))) {
  } else if (!cur_value.is_nop_value()) {
    /* exists opt param hint for this type, use the first opt param hint */
  } else if (OB_FAIL(param_types_.push_back(param_type))
             || OB_FAIL(param_vals_.push_back(val))) {
    LOG_WARN("failed to push back", K(ret), K(param_type), K(val));
  }
  return ret;
}

bool ObOptParamHint::is_param_val_valid(const OptParamType param_type, const ObObj &val)
{
  bool is_valid = false;
  switch (param_type) {
    case HIDDEN_COLUMN_VISIBLE:
    case ROWSETS_ENABLED:
    case ENABLE_NEWSORT:
    case USE_PART_SORT_MGB:
    case USE_DEFAULT_OPT_STAT:
    case ENABLE_IN_RANGE_OPTIMIZATION:
    case _ENABLE_STORAGE_CARDINALITY_ESTIMATION:
    case PRESERVE_ORDER_FOR_PAGINATION:
    case ENABLE_DAS_KEEP_ORDER:
    case HASH_JOIN_ENABLED:
    case OPTIMIZER_SORTMERGE_JOIN_ENABLED:
    case NESTED_LOOP_JOIN_ENABLED:
    case ENABLE_RANGE_EXTRACTION_FOR_NOT_IN:
    case OPTIMIZER_BETTER_INLIST_COSTING:
    case OPTIMIZER_GROUP_BY_PLACEMENT:
    case ENABLE_SPF_BATCH_RESCAN:
    case NLJ_BATCHING_ENABLED:
    case ENABLE_PX_ORDERED_COORD:
    case ENABLE_TOPN_RUNTIME_FILTER:
    case PRESERVE_ORDER_FOR_GROUPBY: {
      is_valid = val.is_varchar() && (0 == val.get_varchar().case_compare("true")
                                      || 0 == val.get_varchar().case_compare("false"));
      break;
    }
    case ROWSETS_MAX_ROWS: {
      is_valid = val.is_int() && (0 <= val.get_int() && 65535 >= val.get_int());
      break;
    }
    case OPTIMIZER_INDEX_COST_ADJ:
    case BLOOM_FILTER_RATIO: {
      is_valid = val.is_int() && (0 <= val.get_int() && 100 >= val.get_int());
      break;
    }
    case WITH_SUBQUERY: {
      is_valid = val.is_int() && (0 <= val.get_int() && 2 >= val.get_int());
      break;
    }
    case DDL_EXECUTION_ID: {
      is_valid = val.is_int() && (0 <= val.get_int());
      break;
    }
    case DDL_TASK_ID:
    case INLIST_REWRITE_THRESHOLD: {
      is_valid = val.is_int() && (0 < val.get_int());
      break;
    }
    case WORKAREA_SIZE_POLICY: {
      is_valid = val.is_varchar() && (0 == val.get_varchar().case_compare("MANULE"));
      break;
    }
    case SPILL_COMPRESSION_CODEC: {
      is_valid = val.is_varchar();
      if (is_valid) {
        bool exist_valid_codec = false;
        for (int i = 0; i < ARRAYSIZEOF(common::sql_temp_store_compress_funcs) && !exist_valid_codec; ++i) {
          if (0 == ObString::make_string(sql_temp_store_compress_funcs[i]).case_compare(val.get_varchar())) {
            exist_valid_codec = true;
          }
        }
        is_valid = exist_valid_codec;
      }
      break;
    }
    case RUNTIME_FILTER_TYPE: {
      is_valid = false;
      if (!val.is_varchar()) {
        // do nothing
      } else {
        ObString str_val = val.get_varchar();
        int64_t rf_type = ObConfigRuntimeFilterChecker::get_runtime_filter_type(str_val.ptr(),
                                                                                str_val.length());
        is_valid = rf_type >= 0;
      }
      break;
    }
    case IO_READ_BATCH_SIZE: {
      // Runtime parameter _io_read_batch_size, range: [0K, 16M].
      if (!val.is_varchar()) {
        is_valid = false;
      } else {
        IGNORE_RETURN ObConfigCapacityParser::get(val.get_varchar().ptr(), is_valid);
      }
      break;
    }
    case IO_READ_REDUNDANT_LIMIT_PERCENTAGE: {
      // Runtime parameter _io_read_redundant_limit_percentage, range: [0, 99].
      is_valid = val.is_int() && (0 <= val.get_int() && val.get_int() < 100);
      break;
    }
    case OPTIMIZER_COST_BASED_TRANSFORMATION: {
      is_valid = val.is_int() && (0 <= val.get_int() && val.get_int() <= 2);
      break;
    }
    case CARDINALITY_ESTIMATION_MODEL: {
      if (val.is_int()) {
        is_valid = 0 <= val.get_int() && val.get_int() < static_cast<int64_t>(ObEstCorrelationType::MAX);
      } else if (val.is_varchar()) {
        int64_t type = OB_INVALID_ID;
        ObSysVarCardinalityEstimationModel sv;
        is_valid = (OB_SUCCESS == sv.find_type(val.get_varchar(), type));
      }
      break;
    }
    case _PUSH_JOIN_PREDICATE: {
      is_valid = val.is_varchar() && (0 == val.get_varchar().case_compare("true")
                                      || 0 == val.get_varchar().case_compare("false"));
      break;
    }
    case PUSHDOWN_STORAGE_LEVEL:
    case RANGE_INDEX_DIVE_LIMIT:
    case PARTITION_INDEX_DIVE_LIMIT:
      is_valid = val.is_int();
      break;
    case PARTITION_WISE_PLAN_ENABLED: {
      is_valid = val.is_varchar() && (0 == val.get_varchar().case_compare("true")
                                      || 0 == val.get_varchar().case_compare("false"));
      break;
    }
    case USE_HASH_ROLLUP: {
      is_valid = val.is_varchar()
                 && (0 == val.get_varchar().case_compare("auto")
                     || 0 == val.get_varchar().case_compare("forced")
                     || 0 == val.get_varchar().case_compare("disabled"));
      break;
    }
    case LOB_ROWSETS_MAX_ROWS: {
      is_valid = val.is_int() && val.get_int() >= 1 && val.get_int() <= 65535;
      break;
    }
    case ENABLE_OPTIMIZER_ROWGOAL: {
      if (val.is_int()) {
        is_valid = 0 <= val.get_int() && val.get_int() < static_cast<int64_t>(ObEnableOptRowGoal::MAX);
      } else if (val.is_varchar()) {
        int64_t type = OB_INVALID_ID;
        ObSysVarEnableOptimizerRowgoal sv;
        is_valid = (OB_SUCCESS == sv.find_type(val.get_varchar(), type));
      }
      break;
    }
    case DAS_BATCH_RESCAN_FLAG: {
      is_valid = val.is_int() && 0 <= val.get_int();
      break;
    }
    case ENABLE_PDML_INSERT_UP:
    case ENABLE_CONSTANT_TYPE_DEMOTION: {
      is_valid = val.is_varchar() && (0 == val.get_varchar().case_compare("true")
                                      || 0 == val.get_varchar().case_compare("false"));
      break;
    }
    case NON_STANDARD_COMPARISON_LEVEL: {
      is_valid = val.is_varchar() && (0 == val.get_varchar().case_compare("none")
                                      || 0 == val.get_varchar().case_compare("equal")
                                      || 0 == val.get_varchar().case_compare("range"));
      break;
    }
    default:
      (void)0;
      break;
  }
  return is_valid;
}

int ObOptParamHint::get_opt_param(const OptParamType param_type, ObObj &val) const
{
  int ret = OB_SUCCESS;
  int64_t idx = OB_INVALID_INDEX;
  val.set_nop_value();
  for (int64_t i = 0; OB_INVALID_INDEX == idx && i < param_types_.count(); ++i) {
    if (param_type == param_types_.at(i)) {
      idx = i;
    }
  }
  if (OB_INVALID_INDEX == idx) {
    /* do nothing */
  } else if (OB_UNLIKELY(idx < 0 || idx >= param_vals_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected idx in opt param hint", K(ret));
  } else {
    val = param_vals_.at(idx);
  }
  return ret;
}

// check opt params with 'true'/'false' value
int ObOptParamHint::has_enable_opt_param(const OptParamType param_type, bool &enabled) const
{
  int ret = OB_SUCCESS;
  ObObj val;
  enabled = false;
  if (OB_FAIL(get_opt_param(param_type, val))) {
  } else if (val.is_varchar() && 0 == val.get_varchar().case_compare("true")) {
    enabled = true;
  }
  return ret;
}

int ObOptParamHint::get_bool_opt_param(const OptParamType param_type, bool &val, bool &is_exists) const
{
  int ret = OB_SUCCESS;
  is_exists = false;
  ObObj obj;
  if (OB_FAIL(get_opt_param(param_type, obj))) {
  } else if (obj.is_nop_value()) {
    // do nothing
  } else if (!obj.is_varchar()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("param obj is invalid", K(ret), K(obj));
  } else {
    val = (0 == obj.get_varchar().case_compare("true"));
    is_exists = true;
  }
  return ret;
}

int ObOptParamHint::get_bool_opt_param(const OptParamType param_type, bool &val) const
{
  bool is_exists = false;
  return get_bool_opt_param(param_type, val, is_exists);
}

int ObOptParamHint::get_integer_opt_param(const OptParamType param_type, int64_t &val, bool &is_exists) const
{
  int ret = OB_SUCCESS;
  is_exists = false;
  ObObj obj;
  if (OB_FAIL(get_opt_param(param_type, obj))) {
  } else if (obj.is_nop_value()) {
    // do nothing
  } else if (!obj.is_int()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("param obj is invalid", K(ret), K(obj));
  } else {
    val = obj.get_int();
    is_exists = true;
  }
  return ret;
}

int ObOptParamHint::get_integer_opt_param(const OptParamType param_type, int64_t &val) const
{
  bool is_exists = false;
  return get_integer_opt_param(param_type, val, is_exists);
}

int ObOptParamHint::get_opt_param_runtime_filter_type(int64_t &rf_type) const
{
  int ret = OB_SUCCESS;
  ObObj obj;
  if (OB_FAIL(get_opt_param(OptParamType::RUNTIME_FILTER_TYPE, obj))) {
  } else if (obj.is_nop_value()) {
    // do nothing
  } else if (!obj.is_varchar()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("runtime filter opt param obj is invalid", K(ret), K(obj));
  } else {
    ObString str_val = obj.get_varchar();
    rf_type = ObConfigRuntimeFilterChecker::get_runtime_filter_type(str_val.ptr(),
                                                                    str_val.length());
  }
  return ret;
}

int ObOptParamHint::get_hash_rollup_param(ObObj &val, bool &has_param) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(has_opt_param(USE_HASH_ROLLUP, has_param))) {
  } else if (has_param) {
    if (OB_FAIL(get_opt_param(USE_HASH_ROLLUP, val))) {
    } else {
      has_param = is_param_val_valid(USE_HASH_ROLLUP, val);
    }
  }
  return ret;
}

int ObOptParamHint::get_enum_opt_param(const OptParamType param_type, int64_t &val) const
{
  int ret = OB_SUCCESS;
  ObObj obj;
  if (OB_FAIL(get_opt_param(param_type, obj))) {
  } else if (obj.is_nop_value()) {
    // do nothing
  } else if (obj.is_int()) {
    val = obj.get_int();
  } else if (obj.is_varchar()) {
    switch (param_type) {
      case CARDINALITY_ESTIMATION_MODEL: {
        ObSysVarCardinalityEstimationModel sv;
        if (OB_FAIL(sv.find_type(obj.get_varchar(), val))) {
        }
        break;
      }
      case ENABLE_OPTIMIZER_ROWGOAL: {
        ObSysVarEnableOptimizerRowgoal sv;
        if (OB_FAIL(sv.find_type(obj.get_varchar(), val))) {
        }
        break;
      }
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("enum param is invalid", K(ret), K(obj));
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("param obj is invalid", K(ret), K(obj));
  }
  return ret;
}

int ObOptParamHint::has_opt_param(const OptParamType param_type, bool &has_hint) const
{
  int ret = OB_SUCCESS;
  has_hint = false;
  ObObj obj;
  if (OB_FAIL(get_opt_param(param_type, obj))) {
  } else {
    has_hint = !obj.is_nop_value();
  }
  return ret;
}

int ObOptParamHint::check_and_get_bool_opt_param(const OptParamType param_type, bool &has_opt_param_v,
                                                 bool &val) const
{
  int ret = OB_SUCCESS;
  has_opt_param_v = false, val = false;
  if (OB_FAIL(has_opt_param(param_type, has_opt_param_v))) {
  } else if (OB_FAIL(has_enable_opt_param(param_type, val))) {
  }
  return ret;
}

template<typename T, ObOptParamHint::GET_PARAM_FUNC<T> PARAM_FUNC>
int ObOptParamHint::inner_get_sys_var(const OptParamType param_type,
                                      const ObSQLSessionInfo *session,
                                      const share::ObSysVarClassType sys_var_id,
                                      T &val) const
{
  int ret = OB_SUCCESS;
  bool has_hint = false;
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(session));
  } else if (OB_FAIL(has_opt_param(param_type, has_hint))) {
  } else if (has_hint) {
    if (OB_FAIL((this->*PARAM_FUNC)(param_type, val))) {
    }
  } else if (OB_FAIL(session->get_sys_variable(sys_var_id, val))) {
  }
  return ret;
}

int ObOptParamHint::get_sys_var(const OptParamType param_type,
                                const ObSQLSessionInfo *session,
                                const share::ObSysVarClassType sys_var_id,
                                int64_t &val) const
{
  return inner_get_sys_var<int64_t, &ObOptParamHint::get_integer_opt_param>
            (param_type, session, sys_var_id, val);
}


int ObOptParamHint::get_enum_sys_var(const OptParamType param_type,
                                     const ObSQLSessionInfo *session,
                                     const share::ObSysVarClassType sys_var_id,
                                     int64_t &val) const
{
  return inner_get_sys_var<int64_t, &ObOptParamHint::get_enum_opt_param>
            (param_type, session, sys_var_id, val);
}

void ObOptParamHint::reset()
{
  param_types_.reuse();
  param_vals_.reuse();
}

ObItemType ObHint::get_hint_type(ObItemType type)
{
  switch(type) {
    // transform hint
    case T_NO_EXPAND:           return T_USE_CONCAT;
    case T_NO_MERGE_HINT:       return T_MERGE_HINT;
    case T_NO_UNNEST:           return T_UNNEST;
    case T_NO_PLACE_GROUP_BY:   return T_PLACE_GROUP_BY;
    case T_INLINE:              return T_MATERIALIZE;
    case T_NO_SEMI_TO_INNER:    return T_SEMI_TO_INNER;
    case T_NO_REPLACE_CONST:    return T_REPLACE_CONST;
    case T_NO_SIMPLIFY_ORDER_BY: return T_SIMPLIFY_ORDER_BY;
    case T_NO_SIMPLIFY_GROUP_BY: return T_SIMPLIFY_GROUP_BY;
    case T_NO_SIMPLIFY_DISTINCT: return T_SIMPLIFY_DISTINCT;
    case T_NO_SIMPLIFY_WINFUNC: return T_SIMPLIFY_WINFUNC;
    case T_NO_SIMPLIFY_EXPR:    return T_SIMPLIFY_EXPR;
    case T_NO_SIMPLIFY_LIMIT:   return T_SIMPLIFY_LIMIT;
    case T_NO_SIMPLIFY_SUBQUERY:  return T_SIMPLIFY_SUBQUERY;
    case T_NO_FAST_MINMAX:        return T_FAST_MINMAX;
    case T_NO_PROJECT_PRUNE:      return T_PROJECT_PRUNE;
    case T_NO_SIMPLIFY_SET:       return T_SIMPLIFY_SET;
    case T_NO_OUTER_TO_INNER:     return T_OUTER_TO_INNER;
    case T_NO_COALESCE_SQ:        return T_COALESCE_SQ;
    case T_NO_COUNT_TO_EXISTS : return T_COUNT_TO_EXISTS;
    case T_NO_LEFT_TO_ANTI :    return T_LEFT_TO_ANTI;
    case T_NO_PUSH_LIMIT :    return T_PUSH_LIMIT;
    case T_NO_ELIMINATE_JOIN :       return T_ELIMINATE_JOIN;
    case T_NO_WIN_MAGIC :    return T_WIN_MAGIC;
    case T_NO_PRED_DEDUCE:    return T_PRED_DEDUCE;
    case T_NO_PUSH_PRED_CTE:      return T_PUSH_PRED_CTE;
    case T_NO_PULLUP_EXPR :       return T_PULLUP_EXPR;
    case T_NO_AGGR_FIRST_UNNEST:           return T_AGGR_FIRST_UNNEST;
    case T_NO_JOIN_FIRST_UNNEST:           return T_JOIN_FIRST_UNNEST;
    case T_NO_DECORRELATE :       return T_DECORRELATE;
    case T_NO_COALESCE_AGGR:      return T_COALESCE_AGGR;
    case T_NO_USE_LATE_MATERIALIZATION: return T_USE_LATE_MATERIALIZATION;
    case T_NO_TRANSFORM_DISTINCT_AGG:     return T_TRANSFORM_DISTINCT_AGG;

    // optimize hint
    case T_NO_USE_DAS_HINT:     return T_USE_DAS_HINT;
    case T_ORDERED:             return T_LEADING;
    case T_NO_USE_MERGE:        return T_USE_MERGE;
    case T_NO_USE_HASH:         return T_USE_HASH;
    case T_NO_USE_NL:           return T_USE_NL;
    case T_NO_USE_NL_MATERIALIZATION:  return T_USE_NL_MATERIALIZATION;
    case T_NO_PX_JOIN_FILTER:   return T_PX_JOIN_FILTER;
    case T_NO_PX_PART_JOIN_FILTER:      return T_PX_PART_JOIN_FILTER;
    case T_NO_USE_HASH_AGGREGATE:       return T_USE_HASH_AGGREGATE;
    case T_NO_GBY_PUSHDOWN:      return T_GBY_PUSHDOWN;
    case T_NO_USE_HASH_DISTINCT: return T_USE_HASH_DISTINCT;
    case T_NO_DISTINCT_PUSHDOWN: return T_DISTINCT_PUSHDOWN;
    case T_NO_USE_HASH_SET: return T_USE_HASH_SET;
    case T_NO_USE_DISTRIBUTED_DML:    return T_USE_DISTRIBUTED_DML;
    case T_NO_PUSH_SUBQ:         return T_PUSH_SUBQ;
    default:                    return type;
  }
}

const char* ObHint::get_hint_name(ObItemType type, bool is_enable_hint /* default true*/ )
{
  switch(type) {
    // transform hint
    case T_NO_REWRITE:          return "NO_REWRITE";
    case T_USE_CONCAT:          return is_enable_hint ? "USE_CONCAT" : "NO_EXPAND";
    case T_MERGE_HINT:          return is_enable_hint ? "MERGE" : "NO_MERGE";
    case T_UNNEST:              return is_enable_hint ? "UNNEST" : "NO_UNNEST";
    case T_PLACE_GROUP_BY:      return is_enable_hint ? "PLACE_GROUP_BY" : "NO_PLACE_GROUP_BY";
    case T_PRED_DEDUCE:         return is_enable_hint ? "PRED_DEDUCE" : "NO_PRED_DEDUCE";
    case T_PUSH_PRED_CTE:       return is_enable_hint ? "PUSH_PRED_CTE" : "NO_PUSH_PRED_CTE";
    case T_MATERIALIZE:         return is_enable_hint ? "MATERIALIZE" : "INLINE";
    case T_SEMI_TO_INNER:       return is_enable_hint ? "SEMI_TO_INNER" : "NO_SEMI_TO_INNER";
    case T_REPLACE_CONST:       return is_enable_hint ? "REPLACE_CONST" :"NO_REPLACE_CONST";
    case T_SIMPLIFY_ORDER_BY:   return is_enable_hint ? "SIMPLIFY_ORDER_BY" : "NO_SIMPLIFY_ORDER_BY";
    case T_SIMPLIFY_GROUP_BY:   return is_enable_hint ? "SIMPLIFY_GROUP_BY" : "NO_SIMPLIFY_GROUP_BY";
    case T_SIMPLIFY_DISTINCT:   return is_enable_hint ? "SIMPLIFY_DISTINCT" : "NO_SIMPLIFY_DISTINCT";
    case T_SIMPLIFY_WINFUNC:    return is_enable_hint ? "SIMPLIFY_WINFUNC" : "NO_SIMPLIFY_WINFUNC";
    case T_SIMPLIFY_EXPR:       return is_enable_hint ? "SIMPLIFY_EXPR" : "NO_SIMPLIFY_EXPR";
    case T_SIMPLIFY_LIMIT:      return is_enable_hint ? "SIMPLIFY_LIMIT" : "NO_SIMPLIFY_LIMIT";
    case T_SIMPLIFY_SUBQUERY:   return is_enable_hint ? "SIMPLIFY_SUBQUERY" : "NO_SIMPLIFY_SUBQUERY";
    case T_FAST_MINMAX:         return is_enable_hint ? "FAST_MINMAX" : "NO_FAST_MINMAX";
    case T_PROJECT_PRUNE:       return is_enable_hint ? "PROJECT_PRUNE" : "NO_PROJECT_PRUNE";
    case T_SIMPLIFY_SET:        return is_enable_hint ? "SIMPLIFY_SET" : "NO_SIMPLIFY_SET";
    case T_OUTER_TO_INNER:      return is_enable_hint ? "OUTER_TO_INNER" : "NO_OUTER_TO_INNER";
    case T_COALESCE_SQ:         return is_enable_hint ? "COALESCE_SQ" : "NO_COALESCE_SQ";
    case T_COUNT_TO_EXISTS :    return is_enable_hint ? "COUNT_TO_EXISTS" : "NO_COUNT_TO_EXISTS";
    case T_LEFT_TO_ANTI :       return is_enable_hint ? "LEFT_TO_ANTI" : "NO_LEFT_TO_ANTI";
    case T_ELIMINATE_JOIN :     return is_enable_hint ? "ELIMINATE_JOIN" : "NO_ELIMINATE_JOIN";
    case T_WIN_MAGIC :          return is_enable_hint ? "WIN_MAGIC" : "NO_WIN_MAGIC";
    case T_PUSH_LIMIT :         return is_enable_hint ? "PUSH_LIMIT" : "NO_PUSH_LIMIT";
    case T_PULLUP_EXPR :        return is_enable_hint ? "PULLUP_EXPR" : "NO_PULLUP_EXPR";
    case T_AGGR_FIRST_UNNEST:           return is_enable_hint ? "AGGR_FIRST_UNNEST" : "NO_AGGR_FIRST_UNNEST";
    case T_JOIN_FIRST_UNNEST:           return is_enable_hint ? "JOIN_FIRST_UNNEST" : "NO_JOIN_FIRST_UNNEST";
    case T_DECORRELATE :        return is_enable_hint ? "DECORRELATE" : "NO_DECORRELATE";
    case T_COALESCE_AGGR:       return is_enable_hint ? "COALESCE_AGGR" : "NO_COALESCE_AGGR";
    case T_USE_LATE_MATERIALIZATION:
      return is_enable_hint ? "USE_LATE_MATERIALIZATION" : "NO_USE_LATE_MATERIALIZATION";
    case T_TRANSFORM_DISTINCT_AGG:
      return is_enable_hint ? "TRANSFORM_DISTINCT_AGG" : "NO_TRANSFORM_DISTINCT_AGG";
    // optimize hint
    case T_INDEX_HINT:          return "INDEX";
    case T_FULL_HINT:           return "FULL";
    case T_NO_INDEX_HINT:       return "NO_INDEX";
    case T_USE_DAS_HINT:        return is_enable_hint ? "USE_DAS" : "NO_USE_DAS";
    case T_UNION_MERGE_HINT:    return "UNION_MERGE";
    case T_LEADING:             return is_enable_hint ? "LEADING" : "ORDERED";
    case T_USE_MERGE:           return is_enable_hint ? "USE_MERGE" : "NO_USE_MERGE";
    case T_USE_HASH:            return is_enable_hint ? "USE_HASH" : "NO_USE_HASH";
    case T_USE_NL:              return is_enable_hint ? "USE_NL" : "NO_USE_NL";
    case T_USE_NL_MATERIALIZATION:     return is_enable_hint ? "USE_NL_MATERIALIZATION"
                                                             : "NO_USE_NL_MATERIALIZATION";
    case T_PX_JOIN_FILTER:      return is_enable_hint ? "PX_JOIN_FILTER" : "NO_PX_JOIN_FILTER";
    case T_PX_PART_JOIN_FILTER: return is_enable_hint ? "PX_PART_JOIN_FILTER" : "NO_PX_PART_JOIN_FILTER";
    case T_PQ_DISTRIBUTE:       return "PQ_DISTRIBUTE";
    case T_PQ_MAP:              return "PQ_MAP";
    case T_PQ_SET:              return "PQ_SET";
    case T_USE_HASH_AGGREGATE:       return is_enable_hint ? "USE_HASH_AGGREGATION"
                                                           : "NO_USE_HASH_AGGREGATION";
    case T_TABLE_PARALLEL:      return "PARALLEL";
    case T_PQ_DISTRIBUTE_WINDOW:       return "PQ_DISTRIBUTE_WINDOW";
    case T_GBY_PUSHDOWN:      return is_enable_hint ? "GBY_PUSHDOWN" : "NO_GBY_PUSHDOWN";
    case T_USE_HASH_DISTINCT: return is_enable_hint ? "USE_HASH_DISTINCT" : "NO_USE_HASH_DISTINCT";
    case T_DISTINCT_PUSHDOWN: return is_enable_hint ? "DISTINCT_PUSHDOWN" : "NO_DISTINCT_PUSHDOWN";
    case T_USE_HASH_SET: return is_enable_hint ? "USE_HASH_SET" : "NO_USE_HASH_SET";
    case T_USE_DISTRIBUTED_DML:    return is_enable_hint ? "USE_DISTRIBUTED_DML" : "NO_USE_DISTRIBUTED_DML";
    case T_TABLE_DYNAMIC_SAMPLING:    return "DYNAMIC_SAMPLING";
    case T_PQ_SUBQUERY: return "PQ_SUBQUERY";
    case T_PQ_GBY_HINT: return "PQ_GBY";
    case T_PQ_DISTINCT_HINT:  return "PQ_DISTINCT";
    case T_INDEX_ASC_HINT:    return "INDEX_ASC";
    case T_INDEX_DESC_HINT:   return "INDEX_DESC";
    case T_PUSH_SUBQ:         return is_enable_hint ? "PUSH_SUBQ" : "NO_PUSH_SUBQ";
    default:                    return NULL;
  }
}

int ObHint::print_hint(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  const ObHint *hint = plan_text.is_used_hint_ ? get_orig_hint() : this;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  int64_t old_pos1 = -1;
  int64_t old_pos2 = -1;
  if (OB_FAIL(BUF_PRINTF("%s%s(", ObQueryHint::get_outline_indent(plan_text.is_oneline_),
                                  get_hint_name()))) {
  } else if (OB_FALSE_IT(old_pos1 = pos)) {
  } else if (!hint->qb_name_.empty() &&
             OB_FAIL(BUF_PRINTF("@\"%.*s\" ", hint->qb_name_.length(), hint->qb_name_.ptr()))) {
    LOG_WARN("failed to print qb_name", K(ret));
  } else if (!hint->qb_name_.empty() && OB_FALSE_IT(old_pos2 = pos)) {
  } else if (OB_FAIL(hint->print_hint_desc(plan_text))) {
  } else if (old_pos1 == pos) {
    pos = old_pos1 - 1;  // delete "(" print before
  } else if (old_pos2 == pos && OB_FALSE_IT(pos = old_pos2 - 1)) { // delete " " after qb_name
  } else if (OB_FAIL(BUF_PRINTF(")"))) {
  } else { /* do nothing */ }
  return ret;
}

int ObHint::print_hint_desc(PlanText &plan_text) const
{
  UNUSED(plan_text);
  return OB_SUCCESS;
}

int ObHint::deep_copy_hint_contain_table(ObIAllocator *allocator, ObHint *&hint) const
{
  int ret = OB_SUCCESS;
  hint = NULL;

  #define DEEP_COPY_NORMAL_HINT(hint_class)  {           \
    hint_class *new_hint = NULL; \
    if (OB_FAIL(ObQueryHint::create_hint(allocator, hint_type_, new_hint))) { \
      LOG_WARN("failed to create hint", K(ret));  \
    } else if (OB_FAIL(new_hint->assign(*static_cast<const hint_class*>(this)))) { \
      LOG_WARN("fail to assign hint", K(ret));  \
    } else {  hint = new_hint;  }} \

  switch(hint_class_) {
    case HINT_JOIN_ORDER: {
      const ObJoinOrderHint *cur_hint = static_cast<const ObJoinOrderHint*>(this);
      ObJoinOrderHint *new_hint = NULL;
      if (OB_FAIL(ObQueryHint::create_hint(allocator, hint_type_, new_hint))) {
      } else if (OB_FAIL(new_hint->assign(*cur_hint))) {
      } else if (OB_FAIL(new_hint->get_table().deep_copy(allocator, cur_hint->get_table()))) {
      } else {
        hint = new_hint;
      }
      break;
    }
    case HINT_ACCESS_PATH:  DEEP_COPY_NORMAL_HINT(ObIndexHint); break;
    case HINT_JOIN_METHOD:  DEEP_COPY_NORMAL_HINT(ObJoinHint); break;
    case HINT_TABLE_PARALLEL: DEEP_COPY_NORMAL_HINT(ObTableParallelHint); break;
    case HINT_SEMI_TO_INNER:  DEEP_COPY_NORMAL_HINT(ObSemiToInnerHint); break;
    case HINT_LEFT_TO_ANTI: DEEP_COPY_NORMAL_HINT(ObLeftToAntiHint); break;
    case HINT_ELIMINATE_JOIN: DEEP_COPY_NORMAL_HINT(ObEliminateJoinHint); break;
    case HINT_GROUPBY_PLACEMENT: DEEP_COPY_NORMAL_HINT(ObGroupByPlacementHint); break;
    case HINT_JOIN_FILTER:  DEEP_COPY_NORMAL_HINT(ObJoinFilterHint); break;
    case HINT_WIN_MAGIC: DEEP_COPY_NORMAL_HINT(ObWinMagicHint); break;
    case HINT_COALESCE_AGGR: DEEP_COPY_NORMAL_HINT(ObCoalesceAggrHint); break;
    case HINT_UNION_MERGE: DEEP_COPY_NORMAL_HINT(ObUnionMergeHint); break;
    default:  {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected hint type to deep copy", K(ret), K(hint_class_));
    }
  }
  return ret;
}

int ObHint::create_push_down_hint(ObIAllocator *allocator,
                                  ObCollationType cs_type,
                                  const TableItem &source_table,
                                  const TableItem &target_table,
                                  ObHint *&hint)
{
  int ret = OB_SUCCESS;
  hint = NULL;
  ObSEArray<ObTableInHint*, 4> all_tables;
  if (OB_FAIL(get_all_table_in_hint(all_tables))) {
  } else {
    bool need_replace = false;
    ObHint *new_hint = NULL;
    for (int64_t i = 0; !need_replace && OB_SUCC(ret) && i < all_tables.count(); ++i) {
      if (OB_ISNULL(all_tables.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null", K(ret));
      } else {
        need_replace = all_tables.at(i)->is_match_table_item(cs_type, source_table);
      }
    }
    all_tables.reuse();
    if (!need_replace) {
      hint = this;
    } else if (OB_FAIL(deep_copy_hint_contain_table(allocator, new_hint))) {
    } else if (OB_FAIL(new_hint->get_all_table_in_hint(all_tables))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < all_tables.count(); ++i) {
        if (OB_ISNULL(all_tables.at(i))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null", K(ret));
        } else if (all_tables.at(i)->is_match_table_item(cs_type, source_table)) {
          all_tables.at(i)->set_table(target_table);
        }
      }
      if (OB_SUCC(ret)) {
        new_hint->set_orig_hint(this);
        hint = new_hint;
      }
    }
  }
  return ret;
}

int ObHint::assign(const ObHint &other)
{
  int ret = OB_SUCCESS;
  hint_type_ = other.hint_type_;
  hint_class_ = other.hint_class_;
  qb_name_ = other.qb_name_;
  orig_hint_ = other.orig_hint_;
  is_enable_hint_ = other.is_enable_hint_;
  return ret;
}

int ObHint::merge_hint(const ObHint *cur_hint,
                       const ObHint *other,
                       ObHintMergePolicy policy,
                       ObIArray<ObItemType> &conflict_hints,
                       const ObHint *&final_hint) const
{
  int ret = OB_SUCCESS;
  final_hint = cur_hint;
  if (OB_ISNULL(other) || 
      OB_LIKELY(NULL != cur_hint && cur_hint->get_hint_type() != other->get_hint_type())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected input hints", K(ret), K(cur_hint), K(other));
  } else if (NULL == cur_hint) {
    if (has_exist_in_array(conflict_hints, other->get_hint_type())) {
      if (RIGHT_HINT_DOMINATED == policy) {
        final_hint = other;
        if (OB_FAIL(ObOptimizerUtil::remove_item(conflict_hints, other->get_hint_type()))) {
        }
      }
    } else {
      final_hint = other;
    }
  } else if (RIGHT_HINT_DOMINATED == policy) {
    final_hint = other;
  } else if (LEFT_HINT_DOMINATED == policy || cur_hint->is_enable_hint() == other->is_enable_hint()) {
    /* do nothing */
  } else if (OB_FAIL(conflict_hints.push_back(other->get_hint_type()))) {
  } else {
    final_hint = NULL;
  }
  return ret;
}

int ObHint::add_tables(ObIArray<ObTableInHint> &tables, ObIArray<ObTableInHint*> &tables_ptr)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < tables.count(); ++i) {
    if (OB_FAIL(tables_ptr.push_back(&tables.at(i)))) {
    }
  }
  return ret;
}

int ObHint::get_expr_str_in_hint(ObIAllocator &allocator,
                                 const ObRawExpr &expr,
                                 ObString &str)
{
  int ret = OB_SUCCESS;
  str.reset();
  char buf[MAX_EXPR_STR_LENGTH_IN_HINT];
  int64_t buf_len = MAX_EXPR_STR_LENGTH_IN_HINT;
  int64_t pos = 0;
  if (OB_FAIL(expr.get_name(buf, buf_len, pos, EXPLAIN_HINT_FORMAT))) {
    ret = OB_SUCCESS;
    pos = (pos < 0 || pos >= buf_len) ? 0 : pos;
  }
  ObString tmp_str(pos, buf);
  if (OB_FAIL(ob_write_string(allocator, tmp_str, str))) {
  }
  return ret;
}

bool ObHint::is_expr_match_str(const ObRawExpr &expr, const ObString &str)
{
  int ret = OB_SUCCESS;
  bool bret = false;
  char buf[MAX_EXPR_STR_LENGTH_IN_HINT];
  int64_t buf_len = MAX_EXPR_STR_LENGTH_IN_HINT;
  int64_t pos = 0;
  if (OB_FAIL(expr.get_name(buf, buf_len, pos, EXPLAIN_HINT_FORMAT))) {
    ret = OB_SUCCESS;
    pos = (pos < 0 || pos >= buf_len) ? 0 : pos;
  }
  ObString tmp_str(pos, buf);
  bret = 0 == str.case_compare(tmp_str);
  return bret;
}

int ObHint::print_table_list(const ObIArray<TablesInHint> &table_list, PlanText &plan_text)
{
  int ret = OB_SUCCESS;
  if (!table_list.empty()) {
    char *buf = plan_text.buf_;
    int64_t &buf_len = plan_text.buf_len_;
    int64_t &pos = plan_text.pos_;
    for (int64_t i = 0; OB_SUCC(ret) && i < table_list.count(); ++i) {
      const TablesInHint &cur_table = table_list.at(i);
      if (cur_table.count() > 1 && OB_FAIL(BUF_PRINTF("("))) {
        LOG_WARN("failed to do BUF_PRINTF", K(ret));
      } else if (OB_FAIL(ObTableInHint::print_join_tables_in_hint(plan_text, cur_table))) {
      } else if (cur_table.count() > 1 && OB_FAIL(BUF_PRINTF(")"))) {
        LOG_WARN("failed to do BUF_PRINTF", K(ret));
      } else if (i < table_list.count() - 1 && OB_FAIL(BUF_PRINTF(" "))) {
        LOG_WARN("failed to do BUF_PRINTF", K(ret));
      }
    }
  }
  return ret;
}


int ObViewMergeHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  if (!parent_qb_name_.empty() && !is_query_push_down_ &&
      OB_FAIL(BUF_PRINTF("> \"%.*s\"", parent_qb_name_.length(), parent_qb_name_.ptr()))) {
    LOG_WARN("fail to print parent qb name", K(ret));
  } else if (!parent_qb_name_.empty() && is_query_push_down_ &&
             OB_FAIL(BUF_PRINTF("< \"%.*s\"", parent_qb_name_.length(), parent_qb_name_.ptr()))) {
    LOG_WARN("fail to print parent qb name", K(ret));
  }
  return ret;
}


int ObOrExpandHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  if (!expand_cond_.empty()) {
    char *buf = plan_text.buf_;
    int64_t &buf_len = plan_text.buf_len_;
    int64_t &pos = plan_text.pos_;
    if (OB_FAIL(BUF_PRINTF("\'%.*s\'", expand_cond_.length(), expand_cond_.ptr()))) {
    }
  }
  return ret;
}

int QbNameList::assign(const QbNameList& other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(qb_names_.assign(other.qb_names_))) {
  }
  return ret;
}

bool QbNameList::has_qb_name(const ObDMLStmt *stmt) const
{
  bool bret = false;
  ObString qb_name;
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpect null stmt", K(ret));
  } else if (OB_FAIL(stmt->get_qb_name(qb_name))) {
  } else {
    for (int i = 0; !bret && i < qb_names_.count(); ++i) {
      if (0 == qb_name.case_compare(qb_names_.at(i))) {
        bret = true;
      }
    }
  }
  return bret;
}

bool QbNameList::has_qb_name(const ObString& qb_name) const
{
  bool bret = false;
  for (int i = 0; !bret && i < qb_names_.count(); ++i) {
    if (0 == qb_name.case_compare(qb_names_.at(i))) {
      bret = true;
    }
  }
  return bret;
}

bool QbNameList::is_equal(const ObIArray<ObSelectStmt*> &stmts) const
{
  bool bret = false;
  if (qb_names_.count() == stmts.count()) {
    bool all_found = true;
    for (int i = 0; all_found && i < stmts.count(); ++i) {
      if (!has_qb_name(stmts.at(i))) {
        all_found = false;
      }
    }
    if (all_found) {
      bret = true;
    }
  }
  return bret;
}

bool QbNameList::is_equal(const ObIArray<ObString> &qb_name_list) const
{
  bool bret = false;
  if (qb_names_.count() == qb_name_list.count()) {
    bool all_found = true;
    for (int i = 0; all_found && i < qb_name_list.count(); ++i) {
      if (!has_qb_name(qb_name_list.at(i))) {
        all_found = false;
      }
    }
    if (all_found) {
      bret = true;
    }
  }
  return bret;
}

bool QbNameList::is_subset(const ObIArray<ObSelectStmt*> &stmts) const
{
  bool bret = false;
  if (qb_names_.count() <= stmts.count()) {
    bool all_found = true;
    for (int i = 0; all_found && i < qb_names_.count(); ++i) {
      bool find = false;
      ObString stmt_qb_name;
      for (int j = 0; !find && j < stmts.count(); j ++) {
        int ret = OB_SUCCESS;
        if (OB_ISNULL(stmts.at(j))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null stmt");
        } else if (OB_FAIL(stmts.at(j)->get_qb_name(stmt_qb_name))) {
        } else if (0 == stmt_qb_name.case_compare(qb_names_.at(i))) {
          find = true;
        }
      }
      if (!find) {
        all_found = false;
      }
    }
    if (all_found) {
      bret = true;
    }
  }
  return bret;
}


int QbNameList::print_qb_names(PlanText &plan_text, const bool print_quote) const
{
  int ret = OB_SUCCESS;
  if (!qb_names_.empty()) {
    char *buf = plan_text.buf_;
    int64_t &buf_len = plan_text.buf_len_;
    int64_t &pos = plan_text.pos_;
    if (print_quote && OB_FAIL(BUF_PRINTF("("))) {
      LOG_WARN("failed to do BUF_PRINTF", K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < qb_names_.count(); ++i) {
      const ObString &qb_name = qb_names_.at(i);
      if (OB_FAIL(BUF_PRINTF("\"%.*s\"", qb_name.length(), qb_name.ptr()))) {
      } else if (i != qb_names_.count() - 1 && OB_FAIL(BUF_PRINTF(" "))) {
        LOG_WARN("failed to do BUF_PRINTF", K(ret));
      }
    }
    if (OB_SUCC(ret) && print_quote && OB_FAIL(BUF_PRINTF(")"))) {
      LOG_WARN("failed to do BUF_PRINTF", K(ret));
    }
  }
  return ret;
}


int ObLeftToAntiHint::assign(const ObLeftToAntiHint &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObTransHint::assign(other))) {
  } else if (OB_FAIL(table_list_.assign(other.get_tb_name_list()))) {
  }
  return ret;
}

int ObLeftToAntiHint::get_all_table_in_hint(ObIArray<ObTableInHint*> &all_tables)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < table_list_.count(); ++i) {
    if (OB_FAIL(add_tables(table_list_.at(i), all_tables))) {
    }
  }
  return ret;
}

bool ObLeftToAntiHint::enable_left_to_anti(ObCollationType cs_type, const TableItem &table) const
{
  bool bret = false;
  if (is_enable_hint()) {
    for (int64_t i = 0; !bret && i < table_list_.count(); ++i) {
      bret = ObTableInHint::is_match_table_item(cs_type, table_list_.at(i), table);
    }
    bret |= table_list_.empty();
  }
  return bret;
}

int ObEliminateJoinHint::assign(const ObEliminateJoinHint &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObTransHint::assign(other))) {
  } else if (OB_FAIL(table_list_.assign(other.get_tb_name_list()))) {
  }
  return ret;
}

int ObEliminateJoinHint::get_all_table_in_hint(ObIArray<ObTableInHint*> &all_tables)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < table_list_.count(); ++i) {
    if (OB_FAIL(add_tables(table_list_.at(i), all_tables))) {
    }
  }
  return ret;
}

bool ObEliminateJoinHint::enable_eliminate_join(ObCollationType cs_type, const TableItem &table) const
{
  bool bret = false;
  if (is_enable_hint()) {
    for (int64_t i = 0; !bret && i < table_list_.count(); ++i) {
      bret = ObTableInHint::is_match_table_item(cs_type, table_list_.at(i), table);
    }
    bret |= table_list_.empty();
  }
  return bret;
}


int ObGroupByPlacementHint::assign(const ObGroupByPlacementHint &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObTransHint::assign(other))) {
  } else if (OB_FAIL(table_list_.assign(other.get_tb_name_list()))) {
  }
  return ret;
}

int ObGroupByPlacementHint::get_all_table_in_hint(ObIArray<ObTableInHint*> &all_tables)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < table_list_.count(); ++i) {
    if (OB_FAIL(add_tables(table_list_.at(i), all_tables))) {
    }
  }
  return ret;
}


bool ObGroupByPlacementHint::enable_groupby_placement(ObCollationType cs_type, 
                                                      const ObIArray<TableItem *> &tables) const
{
  bool bret = false;
  int ret = OB_SUCCESS;
  if (is_enable_hint()) {
    ObSEArray<TableItem *, 4> check_tables;
    if (OB_FAIL(check_tables.assign(tables))) {
    }
    for (int64_t i = 0; !bret && i < table_list_.count(); i++) {
      bret = ObTableInHint::is_match_table_items(cs_type, table_list_.at(i), check_tables);
    }
    bret |= table_list_.empty();
  }
  if (OB_FAIL(ret)) bret = false;
  return bret;
}

bool ObGroupByPlacementHint::enable_groupby_placement(ObCollationType cs_type, 
                                                      const ObIArray<TableItem *> &tables,
                                                      bool &is_all_not_match) const
{
  bool bret = false;
  int ret = OB_SUCCESS;
  if (is_enable_hint()) {
    ObSEArray<TableItem *, 4> check_tables;
    if (OB_FAIL(check_tables.assign(tables))) {
    }
    is_all_not_match = true;  // true if all tables are not matched with hint when bret == FALSE
    for (int64_t i = 0; !bret && is_all_not_match && i < table_list_.count(); i++) {
      bret = ObTableInHint::is_match_table_items(cs_type, table_list_.at(i), check_tables, is_all_not_match);
    }
    bret |= table_list_.empty();
  }
  if (OB_FAIL(ret)) {
    bret = false;
  }
  return bret;
}

int ObCoalesceAggrHint::assign(const ObCoalesceAggrHint &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObTransHint::assign(other))) {
  } else {
    enable_trans_wo_pullup_ = other.enable_trans_wo_pullup_;
    enable_trans_with_pullup_ = other.enable_trans_with_pullup_;
  }
  return ret;
}

int ObCoalesceAggrHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  if (enable_trans_wo_pullup_ && enable_trans_with_pullup_ && OB_FAIL(BUF_PRINTF("WO_PULLUP WITH_PULLUP"))) {
    LOG_WARN("failed to do BUF_PRINTF", K(ret));
  } else if (enable_trans_wo_pullup_ && !enable_trans_with_pullup_ && OB_FAIL(BUF_PRINTF("WO_PULLUP"))) {
    LOG_WARN("failed to do BUF_PRINTF", K(ret));
  } else if (!enable_trans_wo_pullup_ && enable_trans_with_pullup_ && OB_FAIL(BUF_PRINTF("WITH_PULLUP"))) {
    LOG_WARN("failed to do BUF_PRINTF", K(ret));
  }
  return ret;
}

int ObWinMagicHint::assign(const ObWinMagicHint &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObTransHint::assign(other))) {
  } else if (OB_FAIL(table_list_.assign(other.get_tb_name_list()))) {
  }
  return ret;
}

int ObWinMagicHint::get_all_table_in_hint(ObIArray<ObTableInHint*> &all_tables)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(add_tables(table_list_, all_tables))) {
  }
  return ret;
}

int ObWinMagicHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  if (!table_list_.empty()) {
    char *buf = plan_text.buf_;
    int64_t &buf_len = plan_text.buf_len_;
    int64_t &pos = plan_text.pos_;
    if (OB_UNLIKELY(table_list_.count() < 2)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table in hint count < 2", K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < table_list_.count(); ++i) {
      const ObTableInHint &cur_table = table_list_.at(i);
      if (table_list_.count() > 2 && i == 1 && OB_FAIL(BUF_PRINTF("("))) {
        LOG_WARN("failed to do BUF_PRINTF", K(ret));
      } else if (OB_FAIL(cur_table.print_table_in_hint(plan_text))) {
      } else if (table_list_.count() > 2 && i == table_list_.count() - 1 && OB_FAIL(BUF_PRINTF(")"))) {
        LOG_WARN("failed to do BUF_PRINTF", K(ret));
      } else if (i != table_list_.count() - 1 && OB_FAIL(BUF_PRINTF(" "))) {
        LOG_WARN("failed to do BUF_PRINTF", K(ret));
      } 
    }
  }
  return ret;
}

bool ObWinMagicHint::enable_win_magic(ObCollationType cs_type, const TableItem &table) const
{
  bool bret = false;
  if (is_enable_hint()) {
    for (int64_t i = 0; !bret && i < table_list_.count(); ++i) {
      bret = table_list_.at(i).is_match_table_item(cs_type, table);
    }
    bret |= table_list_.empty();
  }
  return bret;
}



int ObMaterializeHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  if (!qb_name_list_.empty()) {
    char *buf = plan_text.buf_;
    int64_t &buf_len = plan_text.buf_len_;
    int64_t &pos = plan_text.pos_;
    for (int i = 0; OB_SUCC(ret) && i < qb_name_list_.count(); ++i) {
      if (OB_FAIL(BUF_PRINTF("("))) {
      }
      const ObIArray<ObString> &qb_names = qb_name_list_.at(i).qb_names_;
      for (int j = 0; OB_SUCC(ret) && j < qb_names.count(); ++j) {
        if (j > 0) {
          if (OB_FAIL(BUF_PRINTF(", "))) {
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(BUF_PRINTF("\"%.*s\"", qb_names.at(j).length(),
                                             qb_names.at(j).ptr()))) {
          }
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(BUF_PRINTF(")"))) {
        LOG_WARN("fail to print materialize hint", K(ret));
      }
    }
    
  }
  return ret;
}

int ObMaterializeHint::add_qb_name_list(const QbNameList& qb_names)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(qb_name_list_.push_back(qb_names))) {
  }
  return ret;
}

int ObMaterializeHint::get_qb_name_list(const ObString& qb_name, QbNameList &qb_names) const
{
  int ret = OB_SUCCESS;
  bool is_valid = false;
  for (int i = 0; OB_SUCC(ret) && !is_valid && i < qb_name_list_.count(); ++i) {
    if (!qb_name_list_.at(i).has_qb_name(qb_name)) {
      //do nothing
    } else if (OB_FAIL(qb_names.assign(qb_name_list_.at(i)))) {
    } else {
      is_valid = true;
    }
  }
  return ret;
}

bool ObMaterializeHint::enable_materialize_subquery(const ObIArray<ObString> & subqueries) const
{
  bool bret = false;
  for (int i = 0; !bret && i < qb_name_list_.count(); ++i) {
    if (qb_name_list_.at(i).is_equal(subqueries)) {
      bret = true;
    }
  }
  return is_enable_hint() && bret;
}

int ObSemiToInnerHint::assign(const ObSemiToInnerHint &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(tables_.assign(other.tables_))) {
  } else if (OB_FAIL(ObHint::assign(other))) {
  }
  return ret;
}

int ObSemiToInnerHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  if (tables_.empty()) {
    //do nothing
  } else if (tables_.count() > 1 && OB_FAIL(BUF_PRINTF("("))) {
    LOG_WARN("failed to print hint", K(ret));
  } else if (OB_FAIL(ObTableInHint::print_join_tables_in_hint(plan_text, tables_, true))) {
  } else if (tables_.count() > 1 && OB_FAIL(BUF_PRINTF(")"))) {
    LOG_WARN("failed to print hint", K(ret));
  }
  return ret;
}

bool ObSemiToInnerHint::enable_semi_to_inner(ObCollationType cs_type, const TableItem &table_item) const
{
  bool bret = false;
  if (T_SEMI_TO_INNER == hint_type_) {
    for (int i = 0; !bret && i < tables_.count(); ++i) {
      bret = tables_.at(i).is_match_table_item(cs_type, table_item);
    }
  }
  return bret;
}


int ObCoalesceSqHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  if (!qb_name_list_.empty()) {
    char *buf = plan_text.buf_;
    int64_t &buf_len = plan_text.buf_len_;
    int64_t &pos = plan_text.pos_;
    for (int i = 0; i < qb_name_list_.count(); ++i) {
      if (OB_FAIL(BUF_PRINTF("("))) {
      }
      const ObIArray<ObString> &qb_names = qb_name_list_.at(i).qb_names_;
      for (int j = 0; OB_SUCC(ret) && j < qb_names.count(); ++j) {
        if (j > 0) {
          if (OB_FAIL(BUF_PRINTF(", "))) {
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(BUF_PRINTF("\"%.*s\"", qb_names.at(j).length(), 
                                             qb_names.at(j).ptr()))) {
          }
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(BUF_PRINTF(")"))) {
        LOG_WARN("fail to print coalesce sq hint", K(ret));
      }
    }
  }
  return ret;
}

int ObCoalesceSqHint::add_qb_name_list(const QbNameList& qb_names)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(qb_name_list_.push_back(qb_names))) {
  }
  return ret;
}

int ObCoalesceSqHint::get_qb_name_list(const ObString& qb_name, QbNameList &qb_names) const
{
  int ret = OB_SUCCESS;
  bool find = T_NO_COALESCE_SQ == hint_type_;
  for (int i = 0; OB_SUCC(ret) && !find && i < qb_name_list_.count(); ++i) {
    if (!qb_name_list_.at(i).has_qb_name(qb_name)) {
      //do nothing
    } else if (OB_FAIL(qb_names.assign(qb_name_list_.at(i)))) {
    } else {
      find = true;
    }
  }
  return ret;
}

bool ObCoalesceSqHint::has_qb_name_list(const ObIArray<ObString> & qb_names) const
{
  bool bret = false;
  for (int i = 0; !bret && i < qb_name_list_.count(); ++i) {
    if (qb_name_list_.at(i).is_equal(qb_names)) {
      bret = true;
    }
  }
  return bret;
}


int ObTableParallelHint::assign(const ObTableParallelHint &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(table_.assign(other.table_))) {
  } else if (OB_FAIL(ObOptHint::assign(other))) {
  } else {
    parallel_ = other.parallel_;
  }
  return ret;
}

int ObTableParallelHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  if (OB_FAIL(table_.print_table_in_hint(plan_text))) {
  } else if (OB_FAIL(BUF_PRINTF(" %ld", parallel_))) {
  }
  return ret;
}

const ObString ObIndexHint::PRIMARY_KEY = "primary";

int ObIndexHint::assign(const ObIndexHint &other)
{
  int ret = OB_SUCCESS;
  index_name_ = other.index_name_;
  index_prefix_ = other.index_prefix_;
  if (OB_FAIL(table_.assign(other.table_))) {
  } else if (OB_FAIL(ObOptHint::assign(other))) {
  }
  return ret;
}

int ObIndexHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  if (OB_FAIL(table_.print_table_in_hint(plan_text))) {
  } else if (T_FULL_HINT == hint_type_ || 
             T_USE_DAS_HINT == hint_type_) {
    /* do nothing */
  } else if (OB_FAIL(BUF_PRINTF(" \"%.*s\"", index_name_.length(), index_name_.ptr()))) {
  } else if ((T_INDEX_HINT != hint_type_ && T_INDEX_ASC_HINT != hint_type_ && T_INDEX_DESC_HINT != hint_type_)
             || index_prefix_ < 0) {
    //do nothing
  } else if (OB_FAIL(BUF_PRINTF(" %ld", index_prefix_))) {
  }
  return ret;
}

int ObUnionMergeHint::assign(const ObUnionMergeHint &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(table_.assign(other.table_))) {
  } else if (OB_FAIL(index_name_list_.assign(other.index_name_list_))) {
  } else if  (OB_FAIL(ObOptHint::assign(other))) {
  }
  return ret;
}

int ObUnionMergeHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  if (OB_FAIL(table_.print_table_in_hint(plan_text))) {
  } else {
    for (int64_t i = 0; i < index_name_list_.count(); i++) {
      if (OB_FAIL(BUF_PRINTF(" \"%.*s\"", index_name_list_.at(i).length(), index_name_list_.at(i).ptr()))) {
      }
    }
  }
  return ret;
}

int ObJoinHint::assign(const ObJoinHint &other)
{
  int ret = OB_SUCCESS;
  dist_algo_ = other.dist_algo_;
  if (OB_FAIL(tables_.assign(other.tables_))) {
  } else if (OB_FAIL(ObOptHint::assign(other))) {
  }
  return ret;
}

int ObJoinHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  const char* algo_str = get_dist_algo_str();
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  if (OB_UNLIKELY(tables_.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected join hint", K(ret));
  } else if (tables_.count() > 1 && OB_FAIL(BUF_PRINTF("("))) {
    LOG_WARN("failed to print hint", K(ret));
  } else if (OB_FAIL(ObTableInHint::print_join_tables_in_hint(plan_text, tables_))) {
  } else if (tables_.count() > 1 && OB_FAIL(BUF_PRINTF(")"))) {
    LOG_WARN("failed to print hint", K(ret));
  } else if (T_PQ_DISTRIBUTE == hint_type_ && NULL != algo_str
             && OB_FAIL(BUF_PRINTF(" %s", algo_str))) {
    LOG_WARN("failed to print dist algo", K(ret));
  }
  return ret;
}

bool ObJoinHint::is_match_local_algo(JoinAlgo join_algo) const
{
  bool bret = false;
  if (NESTED_LOOP_JOIN == join_algo) {
    bret = is_enable_hint() ? T_USE_NL == hint_type_
                            : (T_USE_HASH == hint_type_ || T_USE_MERGE == hint_type_);
  } else if (HASH_JOIN == join_algo) {
    bret = is_enable_hint() ? T_USE_HASH == hint_type_
                            : (T_USE_NL == hint_type_ || T_USE_MERGE == hint_type_);
  } else if (MERGE_JOIN == join_algo) {
    bret = is_enable_hint() ? T_USE_MERGE == hint_type_
                            : (T_USE_HASH == hint_type_ || T_USE_NL == hint_type_);
  }
  return bret;
}

const char *ObJoinHint::get_dist_algo_str(DistAlgo dist_algo)
{
  switch (dist_algo) {
    case DistAlgo::DIST_PARTITION_NONE:     return  "PARTITION NONE";
    case DistAlgo::DIST_NONE_PARTITION:     return  "NONE PARTITION";
    case DistAlgo::DIST_BC2HOST_NONE:       return  "BC2HOST NONE";
    case DistAlgo::DIST_BROADCAST_NONE:     return  "BROADCAST NONE";
    case DistAlgo::DIST_NONE_BROADCAST:     return  "NONE BROADCAST";
    case DistAlgo::DIST_HASH_HASH:          return  "HASH HASH";
    case DistAlgo::DIST_HASH_NONE:          return  "HASH NONE";
    case DistAlgo::DIST_NONE_HASH:          return  "NONE HASH";
    case DistAlgo::DIST_PULL_TO_LOCAL:      return  "LOCAL LOCAL";
    case DistAlgo::DIST_PARTITION_WISE:     return  "NONE NONE";
    case DistAlgo::DIST_EXT_PARTITION_WISE: return  "NONE NONE";
    case DistAlgo::DIST_NONE_ALL:           return  "NONE ALL";
    case DistAlgo::DIST_ALL_NONE:           return  "ALL NONE";
    case DistAlgo::DIST_RANDOM_ALL:         return  "RANDOM ALL";
    case DistAlgo::DIST_HASH_ALL:           return  "HASH ALL";
    case DistAlgo::DIST_HASH_HASH_LOCAL:    return "HASH_LOCAL HASH_LOCAL";
    case DistAlgo::DIST_PARTITION_HASH_LOCAL:    return "PARTITION HASH_LOCAL";
    case DistAlgo::DIST_HASH_LOCAL_PARTITION:    return "HASH_LOCAL PARTITION";
    case DistAlgo::DIST_BROADCAST_HASH_LOCAL:    return "BROADCAST HASH_LOCAL";
    case DistAlgo::DIST_HASH_LOCAL_BROADCAST:    return "HASH_LOCAL BROADCAST";
    default:  return NULL;
  }
  return  NULL;
};

int ObJoinFilterHint::assign(const ObJoinFilterHint &other)
{
  int ret = OB_SUCCESS;
  filter_table_ = other.filter_table_;
  pushdown_filter_table_ = other.pushdown_filter_table_;
  if (OB_FAIL(left_tables_.assign(other.left_tables_))) {
  } else if (OB_FAIL(ObOptHint::assign(other))) {
  }
  return ret;
}

int ObJoinFilterHint::get_all_table_in_hint(ObIArray<ObTableInHint*> &all_tables)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(add_tables(left_tables_, all_tables))) {
  } else if (OB_FAIL(all_tables.push_back(&filter_table_))) {
  }
  return ret;
}

int ObJoinFilterHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  if (OB_FAIL(filter_table_.print_table_in_hint(plan_text))) {
  } else if (!left_tables_.empty() && OB_FAIL(BUF_PRINTF(" "))) {
  } else if (left_tables_.count() > 1 && OB_FAIL(BUF_PRINTF("("))) {
    LOG_WARN("failed to print hint", K(ret));
  } else if (OB_FAIL(ObTableInHint::print_join_tables_in_hint(plan_text, left_tables_))) {
  } else if (left_tables_.count() > 1 && OB_FAIL(BUF_PRINTF(")"))) {
    LOG_WARN("failed to print hint", K(ret));
  } else if (has_pushdown_filter_table() && OB_FAIL(BUF_PRINTF(" "))) {
    LOG_WARN("fail to print table in hint", K(ret));
  } else if (has_pushdown_filter_table() &&
             OB_FAIL(pushdown_filter_table_.print_table_in_hint(plan_text))) {
    LOG_WARN("fail to print table in hint", K(ret));
  }
  return ret;
}


int ObPQSetHint::set_pq_set_hint(const DistAlgo dist_algo,
                                 const int64_t child_num,
                                 const int64_t random_none_idx)
{
  int ret = OB_SUCCESS;
  dist_methods_.reuse();
  if (OB_UNLIKELY(child_num < 2 || random_none_idx >= child_num)
      ||OB_UNLIKELY(random_none_idx < 0 && DistAlgo::DIST_SET_RANDOM == dist_algo)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected params", K(ret), K(child_num), K(random_none_idx), K(dist_algo));
  } else if (DistAlgo::DIST_BASIC_METHOD == dist_algo) {
    /* do nothing */
  } else if (OB_FAIL(dist_methods_.prepare_allocate(child_num))) {
  } else if (2 == child_num) {
    switch (dist_algo) {
      case DistAlgo::DIST_PULL_TO_LOCAL:  {
        dist_methods_.at(0) = T_DISTRIBUTE_LOCAL;
        dist_methods_.at(1) = T_DISTRIBUTE_LOCAL;
        break;
      } 
      case DistAlgo::DIST_PARTITION_WISE:  {
        dist_methods_.at(0) = T_DISTRIBUTE_NONE;
        dist_methods_.at(1) = T_DISTRIBUTE_NONE;
        break;
      }
      case DistAlgo::DIST_HASH_HASH_LOCAL: {
        dist_methods_.at(0) = T_DISTRIBUTE_HASH_LOCAL;
        dist_methods_.at(1) = T_DISTRIBUTE_HASH_LOCAL;
        break;
      }
      case DistAlgo::DIST_EXT_PARTITION_WISE:
      case DistAlgo::DIST_SET_PARTITION_WISE:  {
        dist_methods_.at(0) = T_DISTRIBUTE_NONE;
        dist_methods_.at(1) = T_DISTRIBUTE_NONE;
        break;
      }
      case DistAlgo::DIST_NONE_ALL:  {
        dist_methods_.at(0) = T_DISTRIBUTE_NONE;
        dist_methods_.at(1) = T_DISTRIBUTE_ALL;
        break;
      } 
      case DistAlgo::DIST_ALL_NONE:  {
        dist_methods_.at(0) = T_DISTRIBUTE_ALL;
        dist_methods_.at(1) = T_DISTRIBUTE_NONE;
        break;
      } 
      case DistAlgo::DIST_HASH_HASH:  {
        dist_methods_.at(0) = T_DISTRIBUTE_HASH;
        dist_methods_.at(1) = T_DISTRIBUTE_HASH;
        break;
      } 
      case DistAlgo::DIST_PARTITION_NONE:  {
        dist_methods_.at(0) = T_DISTRIBUTE_PARTITION;
        dist_methods_.at(1) = T_DISTRIBUTE_NONE;
        break;
      }
      case DistAlgo::DIST_PARTITION_HASH_LOCAL: {
        dist_methods_.at(0) = T_DISTRIBUTE_PARTITION;
        dist_methods_.at(1) = T_DISTRIBUTE_HASH_LOCAL;
        break;
      }
      case DistAlgo::DIST_HASH_NONE:  {
        dist_methods_.at(0) = T_DISTRIBUTE_HASH;
        dist_methods_.at(1) = T_DISTRIBUTE_NONE;
        break;
      } 
      case DistAlgo::DIST_NONE_PARTITION:  {
        dist_methods_.at(0) = T_DISTRIBUTE_NONE;
        dist_methods_.at(1) = T_DISTRIBUTE_PARTITION;
        break;
      }
      case DistAlgo::DIST_HASH_LOCAL_PARTITION: {
        dist_methods_.at(0) = T_DISTRIBUTE_HASH_LOCAL;
        dist_methods_.at(1) = T_DISTRIBUTE_PARTITION;
        break;
      }
      case DistAlgo::DIST_NONE_HASH:  {
        dist_methods_.at(0) = T_DISTRIBUTE_NONE;
        dist_methods_.at(1) = T_DISTRIBUTE_HASH;
        break;
      }
      case DistAlgo::DIST_SET_RANDOM:  {
        dist_methods_.at(0) = T_DISTRIBUTE_RANDOM;
        dist_methods_.at(1) = T_DISTRIBUTE_RANDOM;
        dist_methods_.at(random_none_idx) = T_DISTRIBUTE_NONE;
        break;
      }
      default : {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected dist algo", K(ret), K(dist_algo));
      }
    }
  } else {  // multi child union all
    ObItemType method = T_INVALID;
    if (DistAlgo::DIST_PARTITION_WISE == dist_algo) {
      method = T_DISTRIBUTE_NONE;
    } else if (DistAlgo::DIST_PULL_TO_LOCAL == dist_algo) {
      method = T_DISTRIBUTE_LOCAL;
    } else if (DistAlgo::DIST_SET_PARTITION_WISE == dist_algo) {
      method = T_DISTRIBUTE_NONE;
    } else if (DistAlgo::DIST_EXT_PARTITION_WISE == dist_algo) {
      method = T_DISTRIBUTE_NONE;
    } else if (DistAlgo::DIST_SET_RANDOM == dist_algo) {
      method = T_DISTRIBUTE_RANDOM;
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected dist algo", K(ret), K(dist_algo));
    }
    if (OB_SUCC(ret)) {
      for (int i = 0; i < child_num; ++i) {
        dist_methods_.at(i) = method;
      }
      if (DistAlgo::DIST_SET_RANDOM == dist_algo) {
        dist_methods_.at(random_none_idx) = T_DISTRIBUTE_NONE;
      }
    }
  }
  return ret;
}

int ObPQSetHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  if (OB_UNLIKELY(false == is_valid_dist_methods(dist_methods_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected pq set hint", K(ret));
  } else if (!left_branch_.empty()
             && OB_FAIL(BUF_PRINTF(" \"%.*s\"", left_branch_.length(), left_branch_.ptr()))) {
    LOG_WARN("failed to print left branch qb name", K(ret));
  }
  for (int i = 0; OB_SUCC(ret) && i < dist_methods_.count(); ++i) {
    if (OB_FAIL(BUF_PRINTF(" %s", get_dist_method_str(dist_methods_.at(i))))) {
    }
  }
  return ret;
}

bool ObPQSetHint::is_valid_dist_methods(const ObIArray<ObItemType> &dist_methods)
{
  int64_t random_none_idx = OB_INVALID_INDEX;
  return DistAlgo::DIST_INVALID_METHOD != get_dist_algo(dist_methods, 
                                                        random_none_idx);
}

// DistAlgo::DIST_BASIC_METHOD indicate 
uint64_t ObPQSetHint::get_dist_algo(const ObIArray<ObItemType> &dist_methods,
                                    int64_t &random_none_idx)
{
  uint64_t dist_algo = DistAlgo::DIST_INVALID_METHOD;
  random_none_idx = OB_INVALID_INDEX;
  if (dist_methods.empty()) {
    dist_algo = DistAlgo::DIST_BASIC_METHOD;
  } else if (dist_methods.count() < 2) {
    /* do nothing */
  } else if (2 == dist_methods.count()) {
    const ObItemType method1 = dist_methods.at(0);
    const ObItemType method2 = dist_methods.at(1);
    if (T_DISTRIBUTE_LOCAL == method1 && T_DISTRIBUTE_LOCAL == method2) {
      dist_algo = DistAlgo::DIST_PULL_TO_LOCAL;
    } else if (T_DISTRIBUTE_NONE == method1 && T_DISTRIBUTE_NONE == method2) {
      dist_algo = DistAlgo::DIST_PARTITION_WISE
                | DistAlgo::DIST_EXT_PARTITION_WISE
                | DistAlgo::DIST_SET_PARTITION_WISE;
    } else if (T_DISTRIBUTE_NONE == method1 && T_DISTRIBUTE_ALL == method2) {
      dist_algo = DistAlgo::DIST_NONE_ALL;
    } else if (T_DISTRIBUTE_ALL == method1 && T_DISTRIBUTE_NONE == method2) {
      dist_algo = DistAlgo::DIST_ALL_NONE;
    } else if (T_DISTRIBUTE_HASH == method1 && T_DISTRIBUTE_HASH == method2) {
      dist_algo = DistAlgo::DIST_HASH_HASH;
    } else if (T_DISTRIBUTE_PARTITION == method1 && T_DISTRIBUTE_NONE == method2) {
      dist_algo = DistAlgo::DIST_PARTITION_NONE;
    } else if (T_DISTRIBUTE_NONE == method1 && T_DISTRIBUTE_PARTITION == method2) {
      dist_algo = DistAlgo::DIST_NONE_PARTITION;
    } else if (T_DISTRIBUTE_HASH == method1 && T_DISTRIBUTE_NONE == method2) {
      dist_algo = DistAlgo::DIST_HASH_NONE;
    } else if (T_DISTRIBUTE_NONE == method1 && T_DISTRIBUTE_HASH == method2) {
      dist_algo = DistAlgo::DIST_NONE_HASH;
    } else if (T_DISTRIBUTE_NONE == method1 && T_DISTRIBUTE_RANDOM == method2) {
      dist_algo = DistAlgo::DIST_SET_RANDOM;
      random_none_idx = 0;
    } else if (T_DISTRIBUTE_RANDOM == method1 && T_DISTRIBUTE_NONE == method2) {
      dist_algo = DistAlgo::DIST_SET_RANDOM;
      random_none_idx = 1;
    } else if (T_DISTRIBUTE_HASH_LOCAL == method1 && T_DISTRIBUTE_HASH_LOCAL == method2) {
      dist_algo = DistAlgo::DIST_HASH_HASH_LOCAL;
    } else if (T_DISTRIBUTE_PARTITION == method1 && T_DISTRIBUTE_HASH_LOCAL == method2) {
      dist_algo = DistAlgo::DIST_PARTITION_HASH_LOCAL;
    } else if (T_DISTRIBUTE_HASH_LOCAL == method1 && T_DISTRIBUTE_PARTITION == method2) {
      dist_algo = DistAlgo::DIST_HASH_LOCAL_PARTITION;
    }
  } else { // multi child union all
    int64_t tmp = DistAlgo::DIST_PARTITION_WISE
                  | DistAlgo::DIST_EXT_PARTITION_WISE
                  | DistAlgo::DIST_SET_PARTITION_WISE
                  | DistAlgo::DIST_PULL_TO_LOCAL
                  | DistAlgo::DIST_SET_RANDOM;
    for (int i = 0; DistAlgo::DIST_INVALID_METHOD != tmp && i < dist_methods.count(); ++i) {
      const ObItemType method = dist_methods.at(i);
      if (T_DISTRIBUTE_NONE != method) {
        tmp &= ~DistAlgo::DIST_PARTITION_WISE;
        tmp &= ~DistAlgo::DIST_EXT_PARTITION_WISE;
        tmp &= ~DistAlgo::DIST_SET_PARTITION_WISE;
      }
      if (T_DISTRIBUTE_LOCAL != method) {
        tmp &= ~DistAlgo::DIST_PULL_TO_LOCAL;
      }
      if (T_DISTRIBUTE_NONE != method && T_DISTRIBUTE_RANDOM != method) {
        tmp &= ~DistAlgo::DIST_SET_RANDOM;
      } else if (T_DISTRIBUTE_NONE == method) {
        if (OB_INVALID_INDEX == random_none_idx) {
          random_none_idx = i;
        } else {
          tmp &= ~DistAlgo::DIST_SET_RANDOM;
        }
      }
    }
    dist_algo = tmp;
  }
  return dist_algo;
}

const char *ObPQSetHint::get_dist_method_str(const ObItemType dist_method)
{
  switch (dist_method) {
    case T_DISTRIBUTE_ALL:        return  "ALL";
    case T_DISTRIBUTE_NONE:       return  "NONE";
    case T_DISTRIBUTE_PARTITION:  return  "PARTITION";
    case T_DISTRIBUTE_HASH:       return  "HASH";
    case T_DISTRIBUTE_LOCAL:      return  "LOCAL";
    case T_DISTRIBUTE_RANDOM:     return  "RANDOM";
    case T_DISTRIBUTE_HASH_LOCAL: return  "HASH_LOCAL";
    default:  return NULL;
  }
  return NULL;
};


int ObPQSubqueryHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  const char *algo_str = NULL;
  if (OB_FAIL(sub_qb_names_.print_qb_names(plan_text, true))) {
  } else if (NULL != (algo_str = ObJoinHint::get_dist_algo_str(get_dist_algo()))
             && OB_FAIL(BUF_PRINTF(" %s", algo_str))) {
    LOG_WARN("failed to print dist algo", K(ret));
  }
  return ret;
}


int ObPQHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  const char *str = NULL;
  if (OB_ISNULL(str = ObPQHint::get_dist_method_str(dist_method_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null", K(ret), K(dist_method_));
  } else if (OB_FAIL(BUF_PRINTF(" %s", str))) {
  } else if (ObGlobalHint::UNSET_PARALLEL < parallel_ && OB_FAIL(BUF_PRINTF(" %ld", parallel_))) {
    LOG_WARN("fail to print parallel", K(ret));
  }
  return ret;
}

const char *ObPQHint::get_dist_method_str(ObItemType dist_method)
{
  switch (dist_method) {
    case T_DISTRIBUTE_BASIC:  return  "BASIC";
    case T_DISTRIBUTE_NONE:   return  "NONE";
    case T_DISTRIBUTE_HASH:   return  "HASH";
    case T_DISTRIBUTE_LOCAL:  return  "LOCAL";
    case T_DISTRIBUTE_HASH_LOCAL: return "HASH_LOCAL";
    default:  return NULL;
  }
  return NULL;
};

int ObJoinOrderHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  if (is_ordered_hint()) {
    /* do nothing */
  } else if (OB_FAIL(table_.print_leading_table(plan_text))) {
  }
  return ret;
}

int ObJoinOrderHint::assign(const ObJoinOrderHint &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(table_.assign(other.table_))) {
  } else if (OB_FAIL(ObOptHint::assign(other))) {
  }
  return ret;
}

int ObJoinOrderHint::merge_hint(const ObHint *cur_hint,
                                const ObHint *other,
                                ObHintMergePolicy policy,
                                ObIArray<ObItemType> &conflict_hints,
                                const ObHint *&final_hint) const
{
  int ret = OB_SUCCESS;
  final_hint = cur_hint;
  if (OB_ISNULL(other) || OB_UNLIKELY(!other->is_join_order_hint())
      || OB_UNLIKELY(NULL != cur_hint && !cur_hint->is_join_order_hint())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected hint", K(ret), K(other), K(cur_hint));
  } else if (NULL == cur_hint) {
    if (has_exist_in_array(conflict_hints, other->get_hint_type())) {
      const ObJoinOrderHint *other_join_order = static_cast<const ObJoinOrderHint*>(other);
      if (other_join_order->is_ordered_hint() || RIGHT_HINT_DOMINATED == policy) {
        final_hint = other;
        if (OB_FAIL(ObOptimizerUtil::remove_item(conflict_hints, other->get_hint_type()))) {
        }
      }
    } else {
      final_hint = other;
    }
  } else {
    const ObJoinOrderHint *cur_join_order = static_cast<const ObJoinOrderHint*>(cur_hint);
    const ObJoinOrderHint *other_join_order = static_cast<const ObJoinOrderHint*>(other);
    if (LEFT_HINT_DOMINATED == policy || cur_join_order->is_ordered_hint()) {
      /* do nothing */
    } else if (RIGHT_HINT_DOMINATED == policy || other_join_order->is_ordered_hint()) {
      final_hint = other;
    } else if (OB_FAIL(conflict_hints.push_back(other->get_hint_type()))) {
    } else {
      final_hint = NULL;
    }
  }
  return ret;
}


int ObLeadingTable::assign(const ObLeadingTable &other)
{
  int ret = OB_SUCCESS;
  table_ = other.table_;
  left_table_ = other.left_table_;
  right_table_ = other.right_table_;
  return ret;
}

DEF_TO_STRING(ObLeadingTable)
{
  int64_t pos = 0;
  J_OBJ_START();
  if (NULL != table_) {
    J_KV(K_(table));
  } else {
    J_KV(K_(left_table));
    J_COMMA();
    J_KV(K_(right_table));
  }
  J_OBJ_END();
  return pos;
}

ObTableInHint *ObLeadingTable::find_match_hint_table(ObCollationType cs_type,
                                                     const TableItem &table_item)
{
  ObTableInHint *table = NULL;
  if (NULL != table_ && table_->is_match_table_item(cs_type, table_item)) {
    table = table_;
  } else if (NULL != left_table_ &&
             NULL != (table = left_table_->find_match_hint_table(cs_type, table_item))) {
    /* do nothing */
  } else if (NULL != right_table_ &&
             NULL != (table = right_table_->find_match_hint_table(cs_type, table_item))) {
    /* do nothing */
  }
  return table;
}

int ObLeadingTable::get_all_table_in_leading_table(ObIArray<ObTableInHint*> &all_tables)
{
  int ret = OB_SUCCESS;
  if (NULL != table_ && OB_FAIL(all_tables.push_back(table_))) {
    LOG_WARN("failed to push back hint table", K(ret));
  } else if (NULL != left_table_ &&
             OB_FAIL(SMART_CALL(left_table_->get_all_table_in_leading_table(all_tables)))) {
    LOG_WARN("failed to get all table in leading table", K(ret));
  } else if (NULL != left_table_ &&
             OB_FAIL(SMART_CALL(right_table_->get_all_table_in_leading_table(all_tables)))) {
    LOG_WARN("failed to get all table in leading table", K(ret));
  }
  return ret;
}

int ObLeadingTable::print_leading_table(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected leading table", K(ret), K(table_), K(left_table_), K(right_table_));
  } else if (is_single_table()) {
    if (OB_FAIL(table_->print_table_in_hint(plan_text))) {
    }
  } else if (OB_FAIL(BUF_PRINTF("("))) {
  } else if (OB_FAIL(SMART_CALL(left_table_->print_leading_table(plan_text)))) {
  } else if (OB_FAIL(BUF_PRINTF(" "))) {
  } else if (OB_FAIL(SMART_CALL(right_table_->print_leading_table(plan_text)))) {
  } else if (OB_FAIL(BUF_PRINTF(")"))) {
  } else { /* do nothing */ }
  return ret;
}

// do not copy string buf
int ObLeadingTable::deep_copy(ObIAllocator *allocator, const ObLeadingTable &other)
{
  int ret = OB_SUCCESS;
  reset();
  if (NULL != other.table_) {
    if (OB_FAIL(ObQueryHint::create_hint_table(allocator, table_))) {
    } else if (OB_FAIL(table_->assign(*other.table_))) {
    }
  } else if (OB_FAIL(ObQueryHint::create_leading_table(allocator, left_table_))) {
  } else if (OB_FAIL(SMART_CALL(left_table_->deep_copy(allocator, *other.left_table_)))) {
  } else if (OB_FAIL(ObQueryHint::create_leading_table(allocator, right_table_))) {
  } else if (OB_FAIL(SMART_CALL(right_table_->deep_copy(allocator, *other.right_table_)))) {
  }
  return ret;
}

int ObTableInHint::assign(const ObTableInHint &other)
{
  int ret = OB_SUCCESS;
  qb_name_ = other.qb_name_;
  db_name_ = other.db_name_;
  table_name_ = other.table_name_;
  return ret;
}

bool ObTableInHint::equal(const ObTableInHint& other) const
{
  return qb_name_.case_compare(other.qb_name_) == 0 &&
         db_name_.case_compare(other.db_name_) == 0 &&
         table_name_.case_compare(other.table_name_) == 0;
}

DEF_TO_STRING(ObTableInHint)
{
  int64_t pos = 0;
  J_OBJ_START();
  if (!qb_name_.empty()) {
    J_KV(K_(qb_name));
  }
  if (!db_name_.empty()) {
    J_KV(K_(db_name));
    J_COMMA();
  }
  J_KV(K_(table_name));
  J_OBJ_END();
  return pos;
}

int ObTableInHint::print_join_tables_in_hint(PlanText &plan_text,
                                             const ObIArray<ObTableInHint> &tables,
                                             bool ignore_qb_name /* default false */)
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  for (int64_t i = 0; OB_SUCC(ret) && i < tables.count(); ++i) {
    if (OB_FAIL(tables.at(i).print_table_in_hint(plan_text, ignore_qb_name))) {
    } else if (i < tables.count() - 1 && OB_FAIL(BUF_PRINTF(" "))) {
    } else { /* do nothing */ }
  }
  return ret;
}

int ObTableInHint::print_table_in_hint(PlanText &plan_text,
                                       bool ignore_qb_name /* default false */) const
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  if (OB_UNLIKELY(table_name_.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get empty table name for table in hint", K(ret), K(table_name_));
  } else if (!db_name_.empty() &&
             OB_FAIL(BUF_PRINTF("\"%.*s\".", db_name_.length(), db_name_.ptr()))) {
    LOG_WARN("fail to print db_name", K(ret), K(db_name_), K(buf), K(buf_len), K(pos));
  } else if (OB_FAIL(BUF_PRINTF("\"%.*s\"", table_name_.length(), table_name_.ptr()))) {
  } else if (!ignore_qb_name && !qb_name_.empty() &&
             OB_FAIL(BUF_PRINTF("@\"%.*s\"", qb_name_.length(), qb_name_.ptr()))) {
    LOG_WARN("fail to print qb_name", K(ret), K(qb_name_), K(buf), K(buf_len), K(pos));
  }
  return ret;
}

// check table match table_item ignore qb_name_
bool ObTableInHint::is_match_table_item(ObCollationType cs_type, const TableItem &table_item) const
{
  return 0 == ObCharset::strcmp(cs_type, table_name_, table_item.get_object_name()) &&
         (db_name_.empty() || 0 == ObCharset::strcmp(cs_type, db_name_, table_item.get_object_db_name()));
}

bool ObTableInHint::is_match_physical_table_item(ObCollationType cs_type, const TableItem &table_item) const
{
  return 0 == ObCharset::strcmp(cs_type, table_name_, table_item.table_name_) &&
         (db_name_.empty() || 0 == ObCharset::strcmp(cs_type, db_name_, table_item.database_name_));
}

bool ObTableInHint::is_match_table_item(ObCollationType cs_type,
                                        const ObIArray<ObTableInHint> &tables,
                                        const TableItem &table_item)
{
  bool bret = false;
  if (!table_item.is_joined_table()) {
    bret = 1 == tables.count() && tables.at(0).is_match_table_item(cs_type, table_item);
  } else if (tables.count() != static_cast<const JoinedTable&>(table_item).single_table_ids_.count()) {
    bret = false;
  } else {
    int ret = OB_SUCCESS;
    const TableItem *cur_table = NULL;
    ObSEArray<const TableItem*, 4> table_items;
    if (OB_FAIL(table_items.push_back(&table_item))) {
    } else {
      bret = true;
      for (int64_t i = 0; bret && OB_SUCC(ret) && i < table_items.count();) {
        if (OB_ISNULL(cur_table = table_items.at(i))) {
          bret = false;
        } else if (cur_table->is_joined_table()) {
          const JoinedTable *joined_table = static_cast<const JoinedTable*>(cur_table);
          if (OB_FAIL(table_items.push_back(joined_table->right_table_))) {
          } else {
            table_items.at(i) = joined_table->left_table_;
          }
        } else {
          bret = false;
          for (int64_t j = 0; !bret && j < tables.count(); ++j) {
            bret = tables.at(j).is_match_table_item(cs_type, *cur_table);
          }
          ++i;
        }
      }
      if (OB_FAIL(ret)) {
        bret = false;
      }
    }
  }
  return bret;
}

bool ObTableInHint::is_match_table_items(ObCollationType cs_type,
                                        const ObIArray<ObTableInHint> &tables,
                                        ObIArray<TableItem *> &table_items)
{
  int ret = OB_SUCCESS;
  TableItem *cur_table = NULL;
  bool bret = true;
  for (int64_t i = 0; bret && OB_SUCC(ret) && i < table_items.count();) {
    if (OB_ISNULL(cur_table = table_items.at(i))) {
      bret = false;
    } else if (cur_table->is_joined_table()) {
      JoinedTable *joined_table = static_cast<JoinedTable*>(cur_table);
      if (OB_FAIL(table_items.push_back(joined_table->right_table_))) {
      } else {
        table_items.at(i) = joined_table->left_table_;
      }
    } else {
      bret = false;
      for (int64_t j = 0; !bret && j < tables.count(); ++j) {
        bret = tables.at(j).is_match_table_item(cs_type, *cur_table);
      }
      ++i;
    }
  }
  if (OB_FAIL(ret)) {
    bret = false;
  }
  return bret;
}

bool ObTableInHint::is_match_table_items(ObCollationType cs_type,
                                         const ObIArray<ObTableInHint> &tables,
                                         ObIArray<TableItem *> &table_items,
                                         bool &is_all_not_match)
{
  int ret = OB_SUCCESS;
  TableItem *cur_table = NULL;
  bool bret = true;
  uint64_t unmatched_num = 0;
  is_all_not_match = false;
  for (int64_t i = 0; OB_SUCC(ret) && i < table_items.count();) {
    bool tmp_bret = false;
    if (OB_ISNULL(cur_table = table_items.at(i))) {
      bret = false;
    } else if (cur_table->is_joined_table()) {
      JoinedTable *joined_table = static_cast<JoinedTable*>(cur_table);
      if (OB_FAIL(table_items.push_back(joined_table->right_table_))) {
      } else {
        table_items.at(i) = joined_table->left_table_;
      }
    } else {
      for (int64_t j = 0; !tmp_bret && j < tables.count(); ++j) {
        tmp_bret = tables.at(j).is_match_table_item(cs_type, *cur_table);
      }
      ++i;
    }
    if (OB_SUCC(ret) && false == tmp_bret) {
      bret = false;
      ++unmatched_num;
    }
  }
  if (OB_SUCC(ret)) {
    is_all_not_match = unmatched_num == table_items.count();
  } else {
    bret = false;
  }
  return bret;
}

void ObTableInHint::set_table(const TableItem& table)
{
  qb_name_.assign_ptr(table.qb_name_.ptr(), table.qb_name_.length());
  db_name_.reset(); // for alias table or generated table, db_name_ should be empty
  if (!table.alias_name_.empty()) {
    table_name_.assign_ptr(table.alias_name_.ptr(), table.alias_name_.length());
  } else {
    table_name_.assign_ptr(table.table_name_.ptr(), table.table_name_.length());
    if (table.is_basic_table()) {
      db_name_.assign_ptr(table.database_name_.ptr(), table.database_name_.length());
    }
  }
}

const char *ObWindowDistHint::get_dist_algo_str(WinDistAlgo dist_algo)
{
  switch (dist_algo) {
    case WinDistAlgo::WIN_DIST_NONE:   return  "NONE";
    case WinDistAlgo::WIN_DIST_HASH:   return  "HASH";
    case WinDistAlgo::WIN_DIST_HASH_LOCAL:   return  "HASH_LOCAL";
    case WinDistAlgo::WIN_DIST_RANGE:  return  "RANGE";
    case WinDistAlgo::WIN_DIST_LIST:   return  "LIST";
    default:  return NULL;
  }
  return NULL;
};

int ObWindowDistHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < win_dist_options_.count(); ++i) {
    if (OB_FAIL(win_dist_options_.at(i).print_win_dist_option(plan_text))) {
    }
  }
  return ret;
}

int ObWindowDistHint::add_win_dist_option(const ObIArray<ObWinFunRawExpr*> &all_win_funcs,
                                          const ObIArray<ObWinFunRawExpr*> &cur_win_funcs,
                                          const WinDistAlgo algo,
                                          const bool is_push_down,
                                          const bool use_hash_sort,
                                          const bool use_topn_sort)
{
  int ret = OB_SUCCESS;
  ObSEArray<int64_t, 4> win_func_idxs;
  int64_t idx = OB_INVALID_INDEX;
  for (int64_t i = 0; OB_SUCC(ret) && i < cur_win_funcs.count(); ++i) {
    if (OB_UNLIKELY(!ObOptimizerUtil::find_item(all_win_funcs, cur_win_funcs.at(i), &idx))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to find item", K(ret), K(all_win_funcs), K(cur_win_funcs));
    } else if (OB_FAIL(win_func_idxs.push_back(idx))) {
    }
  }
  if (OB_SUCC(ret) && add_win_dist_option(win_func_idxs, algo, is_push_down, use_hash_sort, use_topn_sort)) {
    LOG_WARN("failed to add win dist option", K(ret));
  }
  return ret;
}

int ObWindowDistHint::add_win_dist_option(const ObIArray<int64_t> &win_func_idxs,
                                          const WinDistAlgo algo,
                                          const bool is_push_down,
                                          const bool use_hash_sort,
                                          const bool use_topn_sort)
{
  int ret = OB_SUCCESS;
  int64_t idx = win_dist_options_.count();
  if (OB_FAIL(win_dist_options_.prepare_allocate(win_dist_options_.count() + 1))) {
  } else {
    WinDistOption &win_dist_option = win_dist_options_.at(idx);
    win_dist_option.algo_ = algo;
    win_dist_option.is_push_down_ = is_push_down;
    win_dist_option.use_hash_sort_ = use_hash_sort;
    win_dist_option.use_topn_sort_ = use_topn_sort;
    if (OB_FAIL(win_dist_option.win_func_idxs_.assign(win_func_idxs))) {
    }
  }
  return ret;
}

int ObWindowDistHint::WinDistOption::print_win_dist_option(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid WinDistOption", K(ret), K(*this));
  } else if (win_func_idxs_.empty()) {
    /* do nothing */
  } else if (OB_FAIL(BUF_PRINTF(" (%ld", win_func_idxs_.at(0)))) {
  } else {
    for (int64_t i = 1; OB_SUCC(ret) && i < win_func_idxs_.count(); ++i) {
      if (OB_FAIL(BUF_PRINTF(",%ld", win_func_idxs_.at(i)))) {
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(BUF_PRINTF(")"))) {
      LOG_WARN("failed to print win func idx", K(ret));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(BUF_PRINTF(" %s", ObWindowDistHint::get_dist_algo_str(algo_)))) {
  } else if (use_hash_sort_ && OB_FAIL(BUF_PRINTF(" PARTITION_SORT"))) {
    LOG_WARN("failed to print win func sort", K(ret));
  } else if (is_push_down_ && OB_FAIL(BUF_PRINTF(" PUSHDOWN"))) {
    LOG_WARN("failed to print win func push down", K(ret));
  } else if (use_topn_sort_ && OB_FAIL(BUF_PRINTF(" WF_TOPN"))) {
    LOG_WARN("failed to print win func sort", K(ret));
  }
  return ret;
}

bool ObWindowDistHint::WinDistOption::is_valid() const
{
  bool bret = true;
  if (WinDistAlgo::WIN_DIST_INVALID == algo_) {
    bret = false;
  } else if (WinDistAlgo::WIN_DIST_HASH != algo_ && is_push_down_) {
    bret = false;
  } else if (WinDistAlgo::WIN_DIST_HASH != algo_ && 
             WinDistAlgo::WIN_DIST_NONE != algo_ &&
             WinDistAlgo::WIN_DIST_HASH_LOCAL != algo_
            && (use_hash_sort_ || use_topn_sort_)) {
    bret = false;
  } else {
    for (int64_t i = 0; bret && i < win_func_idxs_.count(); ++i) {
      bret = win_func_idxs_.at(i) >= 0;
    }
  }
  return bret;
}

int ObWindowDistHint::WinDistOption::assign(const WinDistOption& other)
{
  int ret = OB_SUCCESS;
  algo_ = other.algo_;
  use_hash_sort_ = other.use_hash_sort_;
  use_topn_sort_ = other.use_topn_sort_;
  is_push_down_ = other.is_push_down_;
  if (OB_FAIL(win_func_idxs_.assign(other.win_func_idxs_))) {
  }
  return ret;
}

void ObWindowDistHint::WinDistOption::reset()
{
  algo_ = WinDistAlgo::WIN_DIST_INVALID;
  use_hash_sort_ = false;
  use_topn_sort_ = false;
  is_push_down_ = false;
  win_func_idxs_.reuse();
}


int ObAggHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  if (sort_method_valid_) {
    char *buf = plan_text.buf_;
    int64_t &buf_len = plan_text.buf_len_;
    int64_t &pos = plan_text.pos_;
    if (use_partition_sort_ && OB_FAIL(BUF_PRINTF("PARTITION_SORT"))) {
      LOG_WARN("print failed", K(ret));
    } else if (!use_partition_sort_ && OB_FAIL(BUF_PRINTF("NO_PARTITION_SORT"))) {
      LOG_WARN("print failed", K(ret));
    }  
  }
  return ret;
}


int ObTableDynamicSamplingHint::print_hint_desc(PlanText &plan_text) const
{
  int ret = OB_SUCCESS;
  char *buf = plan_text.buf_;
  int64_t &buf_len = plan_text.buf_len_;
  int64_t &pos = plan_text.pos_;
  if (OB_FAIL(table_.print_table_in_hint(plan_text))) {
  } else if (OB_FAIL(BUF_PRINTF(" %ld", dynamic_sampling_))) {
  } else if (sample_block_cnt_ > 0 &&
             OB_FAIL(BUF_PRINTF(" %ld", sample_block_cnt_))) {
    LOG_WARN("fail to print dynamic sampling sample percent", K(ret));
  }
  return ret;
}


int ObAllocOpHint::assign(const ObAllocOpHint& other) {
  int ret = OB_SUCCESS;
  id_ = other.id_;
  flags_ = other.flags_;
  alloc_level_ = other.alloc_level_;
  return ret;
}

bool ObIndexHint::is_match_index(const ObCollationType cs_type,
                                 const TableItem &ref_table,
                                 const ObTableSchema &index_schema) const
{
  bool match = false;
  int ret = OB_SUCCESS;
  ObString table_name;
  ObString index_name;
  if (OB_UNLIKELY(!index_schema.is_index_table())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("got unexpected param", K(ret));
  } else if (!table_.is_match_table_item(cs_type, ref_table)) {
    /* do nothing */
  } else if (OB_FAIL(index_schema.get_index_name(index_name))) {
  } else {
    match = 0 == ObCharset::strcmp(cs_type, index_name_, index_name) ? true : false;
  }
  if (OB_FAIL(ret)) {
    match = false;
  }
  return match;
}

}//end of namespace sql
}//end of namespace oceanbase
