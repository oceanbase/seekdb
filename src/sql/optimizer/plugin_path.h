// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_SQL_OPTIMIZER_PLUGIN_PATH_H_
#define SEEKDB_SQL_OPTIMIZER_PLUGIN_PATH_H_
#include "sql/optimizer/ob_join_order.h"
#include "sql/optimizer/ob_log_plan.h"
#include <cmath>

namespace oceanbase { namespace sql {
// Planning-arena-owned adapter. The logical tree is owned by the same plan's
// operator factory; the path never owns a module lease or a plugin graph ID.
// This is an implementation path, not a new logical relation kind.
class PluginPath final : public Path
{
public:
  explicit PluginPath(ObJoinOrder &relation) : Path(&relation) {}
  bool is_plugin_path() const override { return true; }

  int initialize(ObLogicalOperator &root)
  {
    if (log_op_ || !parent_ || root.get_plan() != parent_->get_plan())
      return common::OB_INVALID_ARGUMENT;
    if (!root.get_table_set().equal(parent_->get_tables()))
      return common::OB_INVALID_ARGUMENT;
    if ((!root.is_local() && !root.is_match_all()) || root.get_parallel() != 1)
      return common::OB_NOT_SUPPORTED;
    if (!std::isfinite(root.get_cost()) || root.get_cost() < 0 ||
        !std::isfinite(root.get_card()) || root.get_card() < 0)
      return common::OB_INVALID_ARGUMENT;
    log_op_ = &root;
    int ret = compute_path_property_from_log_op();
    // Preserve all plan-parameter constraints when a path is reused by an
    // enclosing native JOIN. Path::assign does not copy these arrays.
    if (OB_SUCC(ret)) ret = equal_param_constraints_.assign(root.equal_param_constraints_);
    if (OB_SUCC(ret)) ret = const_param_constraints_.assign(root.const_param_constraints_);
    if (OB_SUCC(ret)) ret = expr_constraints_.assign(root.expr_constraints_);
    if (OB_SUCC(ret)) ret = ambient_card_.assign(root.get_ambient_card());
    if (OB_FAIL(ret)) log_op_ = nullptr;
    return ret;
  }

  int estimate_cost() override
  {
    if (!log_op_) return common::OB_STATE_NOT_MATCH;
    return compute_path_property_from_log_op();
  }
  int re_estimate_cost(EstimateCostInfo &info, double &card, double &cost) override
  {
    if (!log_op_) return common::OB_STATE_NOT_MATCH;
    // The logical implementation owns its cardinality/limit behavior. Do not
    // call JoinPath's built-in algorithm estimate for this implementation.
    const int ret = log_op_->re_est_cost(info, card, cost);
    if (ret == common::OB_SUCCESS && info.override_) {
      cost_ = cost;
      op_cost_ = log_op_->get_op_cost();
      if (is_inner_path_) inner_row_count_ = card;
    }
    return ret;
  }
  int compute_pipeline_info() override
  {
    if (!log_op_) return common::OB_STATE_NOT_MATCH;
    is_pipelined_path_ = log_op_->is_pipelined_plan();
    is_nl_style_pipelined_path_ = log_op_->is_nl_style_pipelined_plan();
    return common::OB_SUCCESS;
  }
  int get_name_internal(char *buf, const int64_t buf_len, int64_t &pos) const override
  {
    return BUF_PRINTF("PLUGIN PATH");
  }
};
}} // namespace oceanbase::sql
#endif
