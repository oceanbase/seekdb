// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_SQL_LOG_PLUGIN_CUSTOM_H_
#define SEEKDB_SQL_LOG_PLUGIN_CUSTOM_H_
#include "sql/optimizer/ob_logical_operator.h"
#include "share/plugin/custom_executor.h"
#include "seekdb/plugin/server_dev_planner.h"
namespace oceanbase { namespace sql {
class CandidateGraph;
class LogPluginCustom : public ObLogicalOperator {
public:
  explicit LogPluginCustom(ObLogPlan &plan) : ObLogicalOperator(plan) {}
  int configure(const seekdb_plugin_custom_path_request_v1_t &request);
  int configure_layout(const seekdb_plugin_custom_path_request_v2_t &request, CandidateGraph &graph);
  int configure_fragment(const seekdb_plugin_custom_path_request_v3_t &request,
      CandidateGraph &graph, ObLogicalOperator &target);
  int configure_bindings(const seekdb_plugin_custom_path_request_v4_t &request, CandidateGraph &graph);
  int get_op_exprs(common::ObIArray<ObRawExpr *> &exprs) override;
  int allocate_expr_pre(ObAllocExprContext &ctx) override;
  int allocate_expr_post(ObAllocExprContext &ctx) override;
  int check_output_dependance(common::ObIArray<ObRawExpr *> &child_output, PPDeps &deps) override;
  int validate_layout() const;
  int inner_replace_op_exprs(ObRawExprReplacer &replacer) override;
  bool explicit_layout() const { return explicit_layout_; }
  const common::ObIArray<ObRawExpr *> &input_exprs() const { return inputs_; }
  const common::ObIArray<ObRawExpr *> &result_exprs() const { return results_; }
  const common::ObIArray<uint32_t> &input_offsets() const { return input_offsets_; }
  const common::ObIArray<ObExecParamRawExpr *> &input_bindings() const { return input_bindings_; }
  const common::ObIArray<uint32_t> &binding_sources() const { return binding_sources_; }
  const common::ObIArray<uint32_t> &binding_inputs() const { return binding_inputs_; }
  const common::ObIArray<uint32_t> &binding_targets() const { return binding_targets_; }
  bool fragment() const { return target_ != nullptr; }
  int compute_property() override;
  int est_cost() override;
  int do_re_est_cost(EstimateCostInfo &param, double &card, double &op_cost, double &cost) override;
  int compute_op_ordering() override;
  bool is_block_op() const override { return flags_ & SEEKDB_PLUGIN_PATH_BLOCKING; }
  int get_explain_name_internal(char *buf, int64_t buf_len, int64_t &pos) override;
  uint64_t hash(uint64_t seed) const override;
  const share::plugin::CustomExecutorBinding &binding() const { return binding_; }
  const std::string &parameters() const { return parameters_; }
private:
  int validate_parameter_owners() const;
  int validate_results() const;
  int configure_layout_impl(const seekdb_plugin_custom_path_request_v2_t &request,
      CandidateGraph &graph, uint32_t max_inputs);
  share::plugin::CustomExecutorBinding binding_;
  std::string parameters_;
  double operator_cost_ = 0;
  uint32_t flags_ = 0;
  bool explicit_layout_ = false;
  common::ObSEArray<ObRawExpr *, 8> inputs_, results_;
  common::ObSEArray<uint32_t, 8> input_offsets_;
  common::ObSEArray<ObExecParamRawExpr *, 8> input_bindings_;
  common::ObSEArray<uint32_t, 8> binding_sources_, binding_inputs_, binding_targets_;
  // Planning-only semantic provenance, not an execution child or cached ID.
  ObLogicalOperator *target_ = nullptr;
};
} }
#endif
