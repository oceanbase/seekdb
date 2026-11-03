// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#define USING_LOG_PREFIX SQL_OPT
#include "sql/optimizer/log_plugin_custom.h"
#include "sql/optimizer/ob_log_plan.h"
#include "sql/optimizer/plugin_candidate_graph.h"
#include "sql/optimizer/ob_raw_expr_check_dep.h"
#include "sql/optimizer/ob_log_subplan_filter.h"
#include "share/rc/ob_module_provider.h"
#include "rust/plugin-runtime/include/plugin_runtime.h"
#include <cmath>
#include <unordered_set>
namespace oceanbase { namespace sql {
using namespace common;
int LogPluginCustom::configure_layout(const seekdb_plugin_custom_path_request_v2_t &r, CandidateGraph &graph)
{
  return configure_layout_impl(r, graph, SEEKDB_PLUGIN_CUSTOM_MAX_COLUMNS);
}
int LogPluginCustom::configure_layout_impl(const seekdb_plugin_custom_path_request_v2_t &r,
    CandidateGraph &graph, uint32_t max_inputs)
{
  if (explicit_layout_ || r.input_count > max_inputs ||
      r.output_count > SEEKDB_PLUGIN_CUSTOM_MAX_COLUMNS || (r.input_count && !r.inputs) ||
      (r.output_count && !r.outputs)) return OB_INVALID_ARGUMENT;
  for (auto word : r.reserved) if (word) return OB_INVALID_ARGUMENT;
  ObSEArray<ObRawExpr *, 8> inputs, outputs;
  int ret = OB_SUCCESS;
  for (uint32_t i = 0; ret == OB_SUCCESS && i < r.input_count; ++i) {
    ObRawExpr *expr = nullptr;
    if (OB_SUCC(ret = graph.resolve_expression(r.inputs[i], expr))) ret = inputs.push_back(expr);
  }
  for (uint32_t i = 0; ret == OB_SUCCESS && i < r.output_count; ++i) {
    ObRawExpr *expr = nullptr;
    if (OB_SUCC(ret = graph.resolve_expression(r.outputs[i], expr))) {
      // Constants/parameters do not have row-owned writable result slots.
      if (expr->is_const_expr() || expr->is_const_or_param_expr() ||
          ObOptimizerUtil::find_item(outputs, expr)) ret = OB_INVALID_ARGUMENT;
      else ret = outputs.push_back(expr);
    }
  }
  if (OB_SUCC(ret)) ret = inputs_.assign(inputs);
  if (OB_SUCC(ret)) ret = results_.assign(outputs);
  if (OB_SUCC(ret)) explicit_layout_ = true;
  return ret;
}
int LogPluginCustom::configure_fragment(const seekdb_plugin_custom_path_request_v3_t &r,
    CandidateGraph &graph, ObLogicalOperator &target)
try {
  if (explicit_layout_ || get_num_of_child() || target.get_plan() != get_plan() || r.plan_count > 64 ||
      (r.plan_count && !r.input_plans) || !r.input_offsets ||
      r.execution != SEEKDB_PLUGIN_CUSTOM_LOCAL_SERIAL || r.input_offsets[0] != 0 ||
      r.input_offsets[r.plan_count] != r.v2.input_count) return OB_INVALID_ARGUMENT;
  for (auto word : r.reserved) if (word) return OB_INVALID_ARGUMENT;
  if ((!target.is_local() && !target.is_match_all()) || target.get_parallel() != 1)
    return OB_NOT_SUPPORTED;
  std::vector<ObLogicalOperator *> children;
  children.reserve(r.plan_count);
  for (uint32_t i = 0; i < r.plan_count; ++i) {
    if (r.input_offsets[i] > r.input_offsets[i + 1] ||
        r.input_offsets[i + 1] - r.input_offsets[i] > SEEKDB_PLUGIN_CUSTOM_MAX_COLUMNS)
      return OB_INVALID_ARGUMENT;
    ObLogicalOperator *input = nullptr;
    int ret = graph.resolve_plan(r.input_plans[i], input);
    if (ret != OB_SUCCESS) return ret;
    if (input->get_plan() != get_plan()) return OB_INVALID_ARGUMENT;
    if ((!input->is_local() && !input->is_match_all()) || input->get_parallel() != 1)
      return OB_NOT_SUPPORTED;
    children.push_back(input);
  }
  // Verify provenance without changing any parent links. A selected input must
  // occur in the target, and two streams cannot execute the same physical tree.
  std::unordered_set<ObLogicalOperator *> reachable;
  std::vector<ObLogicalOperator *> work{&target};
  while (!work.empty()) {
    auto *node = work.back(); work.pop_back();
    if (!node) return OB_INVALID_ARGUMENT;
    if (!reachable.insert(node).second) continue;
    if (reachable.size() > 4096) return OB_SIZE_OVERFLOW;
    for (int64_t i = 0; i < node->get_num_of_child(); ++i) work.push_back(node->get_child(i));
  }
  std::unordered_set<ObLogicalOperator *> consumed;
  for (auto *input : children) {
    if (!reachable.count(input)) return OB_INVALID_ARGUMENT;
    work.push_back(input);
    while (!work.empty()) {
      auto *node = work.back(); work.pop_back();
      if (!node || !consumed.insert(node).second) return OB_INVALID_ARGUMENT;
      if (consumed.size() > 4096) return OB_SIZE_OVERFLOW;
      for (int64_t i = 0; i < node->get_num_of_child(); ++i) work.push_back(node->get_child(i));
    }
  }
  int ret = configure_layout_impl(r.v2, graph, 64 * SEEKDB_PLUGIN_CUSTOM_MAX_COLUMNS);
  if (ret != OB_SUCCESS) return ret;
  for (uint32_t i = 0; i <= r.plan_count; ++i)
    if (OB_FAIL(input_offsets_.push_back(r.input_offsets[i]))) return ret;
  // set_child normally mutates the input parent. Keep construction provisional;
  // only selection commits links, and failed/unselected alternatives stay inert.
  for (uint32_t i = 0; i < r.plan_count; ++i) {
    auto *saved = children[i]->get_parent();
    set_child(i, children[i]);
    children[i]->set_parent(saved);
    if (get_child(i) != children[i]) return OB_ALLOCATE_MEMORY_FAILED;
  }
  target_ = &target;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }
int LogPluginCustom::compute_property()
{
  if (!target_) return ObLogicalOperator::compute_property();
  int ret = validate_parameter_owners();
  if (ret != OB_SUCCESS) return ret;
  // Equivalent *result* facts are independent of the chosen implementation's
  // input arity (e.g. semi/outer join facts must not come from its first input).
  ret = get_output_const_exprs().assign(target_->get_output_const_exprs());
  if (OB_FAIL(ret)) return ret;
  set_output_equal_sets(&target_->get_output_equal_sets());
  set_fd_item_set(&target_->get_fd_item_set());
  set_table_set(&target_->get_table_set());
  set_is_at_most_one_row(target_->get_is_at_most_one_row());
  set_width(target_->get_width());
  if (OB_FAIL(get_ambient_card().assign(target_->get_ambient_card()))) return ret;
  // Only the explicit local-serial placement has been admitted. In particular
  // this does not inherit a removed exchange's distributed placement.
  set_strong_sharding(target_->get_strong_sharding());
  if (OB_FAIL(get_weak_sharding().assign(target_->get_weak_sharding()))) return ret;
  set_parallel(1); set_available_parallel(1);
  if (OB_FAIL(compute_pipeline_info())) return ret;
  if (OB_FAIL(compute_plan_type())) return ret;
  if (OB_FAIL(compute_op_other_info())) return ret;
  if (OB_FAIL(compute_op_ordering())) return ret;
  if (OB_FAIL(est_cost())) return ret;
  return check_property_valid();
}
int LogPluginCustom::validate_parameter_owners() const
try {
  if (!target_) return OB_SUCCESS;
  std::vector<ObLogicalOperator *> work{target_};
  std::unordered_set<ObLogicalOperator *> seen;
  while (!work.empty()) {
    auto *node = work.back(); work.pop_back();
    if (!node || !seen.insert(node).second) return OB_INVALID_ARGUMENT;
    if (seen.size() > 4096) return OB_SIZE_OVERFLOW;
    bool retained = false;
    for (int64_t i = 0; i < get_num_of_child(); ++i) if (node == get_child(i)) retained = true;
    if (retained) continue;
    if (node->get_type() == log_op_def::LOG_JOIN) {
      auto &join = static_cast<ObLogJoin &>(*node);
      if (!join.get_above_pushdown_left_params().empty() || !join.get_above_pushdown_right_params().empty())
        return OB_NOT_SUPPORTED;
      for (int64_t i = 0; i < join.get_nl_params().count(); ++i)
        if (!ObOptimizerUtil::find_item(input_bindings_, join.get_nl_params().at(i))) return OB_NOT_SUPPORTED;
    } else if (node->get_type() == log_op_def::LOG_SUBPLAN_FILTER) {
      auto &subplan = static_cast<ObLogSubPlanFilter &>(*node);
      if (!subplan.get_exec_params().empty() || !subplan.get_onetime_exprs().empty()) return OB_NOT_SUPPORTED;
    } else if (node->get_type() == log_op_def::LOG_PLUGIN_CUSTOM &&
        !static_cast<LogPluginCustom *>(node)->input_bindings().empty()) return OB_NOT_SUPPORTED;
    for (int64_t i = 0; i < node->get_num_of_child(); ++i) work.push_back(node->get_child(i));
  }
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }
int LogPluginCustom::configure_bindings(const seekdb_plugin_custom_path_request_v4_t &r, CandidateGraph &graph)
try
{
  if (!fragment() || !input_bindings_.empty() || r.reserved_word || !r.bindings ||
      !r.binding_count || r.binding_count > 1024)
    return OB_INVALID_ARGUMENT;
  for (auto word : r.reserved) if (word) return OB_INVALID_ARGUMENT;
  // Stop at retained input roots. Their parameter owners remain native. For
  // each removed owner retain its entire right consumer tree as ONE input;
  // the left scope may be split into several inputs (including earlier NLJs).
  // This admits chains/fan-in without guessing consumers from partial read-only
  // expression metadata or invoking get_op_exprs(), which can mutate plans.
  const auto input_index = [&](ObLogicalOperator *node) -> int64_t {
    for (int64_t i = 0; i < get_num_of_child(); ++i) if (get_child(i) == node) return i;
    return -1;
  };
  struct Owned { ObExecParamRawExpr *param; ObLogicalOperator *left; uint32_t target; };
  std::vector<Owned> owned;
  std::vector<ObLogicalOperator *> work{target_};
  std::unordered_set<ObLogicalOperator *> seen;
  while (!work.empty()) {
    auto *node = work.back(); work.pop_back();
    if (!node || node->get_plan() != get_plan() || !seen.insert(node).second) return OB_INVALID_ARGUMENT;
    if (seen.size() > 4096) return OB_SIZE_OVERFLOW;
    if (input_index(node) >= 0) continue;
    // Other owner kinds (subplan, cross-query-block, custom) need their own
    // transfer protocols. Do not silently drop them with a JOIN-only proof.
    if (node->get_type() != log_op_def::LOG_JOIN || node->get_num_of_child() != 2) return OB_NOT_SUPPORTED;
    auto &join = static_cast<ObLogJoin &>(*node);
    if (!join.get_above_pushdown_left_params().empty() || !join.get_above_pushdown_right_params().empty())
      return OB_NOT_SUPPORTED;
    if (!join.get_nl_params().empty()) {
      const auto target = input_index(join.get_child(1));
      if (join.get_join_algo() != NESTED_LOOP_JOIN || target < 0) return OB_NOT_SUPPORTED;
      for (int64_t i = 0; i < join.get_nl_params().count(); ++i) {
        auto *param = join.get_nl_params().at(i);
        if (!param || !param->get_ref_expr()) return OB_INVALID_ARGUMENT;
        for (const auto &other : owned)
          if (other.param == param || (param->get_param_index() >= 0 &&
              other.param->get_param_index() == param->get_param_index())) return OB_INVALID_ARGUMENT;
        owned.push_back({param, join.get_child(0), static_cast<uint32_t>(target)});
        if (owned.size() > 1024) return OB_SIZE_OVERFLOW;
      }
    }
    work.push_back(join.get_child(0)); work.push_back(join.get_child(1));
  }
  if (r.binding_count != owned.size()) return OB_INVALID_ARGUMENT;
  // A retained native owner must not write a transferred slot. Expression
  // copies can have different graph identities but the same ParamStore index.
  const auto conflicts = [&](const ObIArray<ObExecParamRawExpr *> &params) {
    for (int64_t i = 0; i < params.count(); ++i) {
      const auto *param = params.at(i);
      if (!param) return true;
      for (const auto &other : owned)
        if (other.param == param || (param->get_param_index() >= 0 &&
            other.param->get_param_index() == param->get_param_index())) return true;
    }
    return false;
  };
  seen.clear(); work.clear();
  for (int64_t i = 0; i < get_num_of_child(); ++i) work.push_back(get_child(i));
  while (!work.empty()) {
    auto *node = work.back(); work.pop_back();
    if (!node || !seen.insert(node).second) return OB_INVALID_ARGUMENT;
    if (seen.size() > 4096) return OB_SIZE_OVERFLOW;
    if (node->get_type() == log_op_def::LOG_JOIN) {
      auto &join = static_cast<ObLogJoin &>(*node);
      if (conflicts(join.get_nl_params()) || conflicts(join.get_above_pushdown_left_params()) ||
          conflicts(join.get_above_pushdown_right_params())) return OB_NOT_SUPPORTED;
    } else if (node->get_type() == log_op_def::LOG_SUBPLAN_FILTER) {
      auto &subplan = static_cast<ObLogSubPlanFilter &>(*node);
      if (conflicts(subplan.get_exec_params()) || conflicts(subplan.get_onetime_exprs())) return OB_NOT_SUPPORTED;
    } else if (node->get_type() == log_op_def::LOG_PLUGIN_CUSTOM &&
        conflicts(static_cast<LogPluginCustom *>(node)->input_bindings())) return OB_NOT_SUPPORTED;
    for (int64_t i = 0; i < node->get_num_of_child(); ++i) work.push_back(node->get_child(i));
  }
  ObSEArray<ObExecParamRawExpr *, 8> parameters;
  ObSEArray<uint32_t, 8> sources, source_inputs, targets;
  std::vector<seekdb_runtime_input_edge> edges;
  int ret = OB_SUCCESS;
  for (uint32_t i = 0; i < r.binding_count; ++i) {
    const auto &binding = r.bindings[i];
    ObRawExpr *raw = nullptr;
    if (binding.source_input >= get_num_of_child() || binding.target_input >= get_num_of_child() ||
        binding.source_input == binding.target_input ||
        binding.source_column >= input_offsets_.at(binding.source_input + 1) - input_offsets_.at(binding.source_input))
      return OB_INVALID_ARGUMENT;
    if (OB_FAIL(graph.resolve_expression(binding.parameter, raw))) return ret;
    if (!raw || !raw->is_exec_param_expr()) return OB_INVALID_ARGUMENT;
    auto *param = static_cast<ObExecParamRawExpr *>(raw);
    const uint32_t source = input_offsets_.at(binding.source_input) + binding.source_column;
    const Owned *owner = nullptr;
    for (const auto &entry : owned) if (entry.param == param) owner = &entry;
    if (!owner || owner->target != binding.target_input || ObOptimizerUtil::find_item(parameters, param) ||
        param->get_ref_expr() != inputs_.at(source)) return OB_INVALID_ARGUMENT;
    // Physical input order need not match the original JOIN order. Prove that
    // the chosen source is a retained subtree within this owner's left scope.
    bool contained = false;
    seen.clear(); work = {owner->left};
    while (!work.empty()) {
      auto *node = work.back(); work.pop_back();
      if (!node || !seen.insert(node).second) return OB_INVALID_ARGUMENT;
      if (seen.size() > 4096) return OB_SIZE_OVERFLOW;
      if (node == get_child(binding.source_input)) contained = true;
      for (int64_t j = 0; j < node->get_num_of_child(); ++j) work.push_back(node->get_child(j));
    }
    if (!contained) return OB_INVALID_ARGUMENT;
    if (OB_FAIL(parameters.push_back(param)) || OB_FAIL(sources.push_back(source)) ||
        OB_FAIL(source_inputs.push_back(binding.source_input)) || OB_FAIL(targets.push_back(binding.target_input))) return ret;
    edges.push_back({binding.source_input, binding.target_input});
  }
  // Share the runtime's graph rules; no second C++ cycle implementation.
  seekdb_runtime_input_state *state = nullptr;
  const auto status = seekdb_runtime_input_state_create(get_num_of_child(), edges.data(), edges.size(), &state);
  seekdb_runtime_input_state_destroy(state);
  if (status != SEEKDB_RUNTIME_OK) return status == SEEKDB_RUNTIME_NO_MEMORY ? OB_ALLOCATE_MEMORY_FAILED : OB_INVALID_ARGUMENT;
  if (OB_FAIL(input_bindings_.assign(parameters)) || OB_FAIL(binding_sources_.assign(sources)) ||
      OB_FAIL(binding_inputs_.assign(source_inputs)) || OB_FAIL(binding_targets_.assign(targets))) return ret;
  return ret;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }
int LogPluginCustom::get_op_exprs(ObIArray<ObRawExpr *> &exprs)
{
  int ret = ObLogicalOperator::get_op_exprs(exprs);
  if (OB_SUCC(ret) && explicit_layout_) ret = append_array_no_dup(exprs, inputs_);
  if (OB_SUCC(ret) && explicit_layout_) ret = append_array_no_dup(exprs, results_);
  for (auto *param : input_bindings_) if (OB_SUCC(ret)) ret = add_var_to_array_no_dup(exprs, static_cast<ObRawExpr *>(param));
  return ret;
}
int LogPluginCustom::validate_results() const
{
  for (int64_t i = 0; i < results_.count(); ++i) {
    if (!results_.at(i) || results_.at(i)->is_const_expr() || results_.at(i)->is_const_or_param_expr()) return OB_INVALID_ARGUMENT;
    for (int64_t j = 0; j < i; ++j) if (results_.at(j) == results_.at(i)) return OB_INVALID_ARGUMENT;
  }
  return OB_SUCCESS;
}
int LogPluginCustom::inner_replace_op_exprs(ObRawExprReplacer &replacer)
{
  if (!explicit_layout_) return OB_SUCCESS;
  int ret = replace_exprs_action(replacer, inputs_);
  if (OB_SUCC(ret)) ret = replace_exprs_action(replacer, results_);
  for (auto *param : input_bindings_) if (OB_SUCC(ret)) ret = replace_expr_action(replacer, param->get_ref_expr());
  // A rewrite merging two output slots cannot silently change opaque plan
  // ordinals or publish two potentially different values into the same slot.
  if (OB_SUCC(ret)) ret = validate_results();
  for (int64_t i = 0; OB_SUCC(ret) && i < input_bindings_.count(); ++i)
    if (input_bindings_.at(i)->get_ref_expr() != inputs_.at(binding_sources_.at(i))) ret = OB_INVALID_ARGUMENT;
  return ret;
}
int LogPluginCustom::allocate_expr_pre(ObAllocExprContext &ctx)
{
  int ret = ObLogicalOperator::allocate_expr_pre(ctx);
  if (OB_SUCC(ret) && explicit_layout_) {
    // Inputs must be materialized below this node, even if only the plugin
    // consumes them. Do not let base allocation place a computed input here.
    for (int64_t i = 0; i < inputs_.count(); ++i) {
      auto *expr = inputs_.at(i);
      if (expr->is_static_const_expr() || expr->has_flag(IS_DYNAMIC_USER_VARIABLE)) continue;
      int64_t child = 0;
      if (fragment()) while (child + 1 < input_offsets_.count() && i >= input_offsets_.at(child + 1)) ++child;
      uint64_t producer = OB_INVALID_ID;
      if (OB_FAIL(get_next_producer_id(get_child(child), producer))) return ret;
      ExprProducer *entry = nullptr;
      if (OB_FAIL(ctx.find(expr, entry))) return ret;
      if (!entry) return OB_ERR_UNEXPECTED;
      entry->producer_id_ = std::min(entry->producer_id_, producer);
    }
  }
  return ret;
}
int LogPluginCustom::allocate_expr_post(ObAllocExprContext &ctx)
{
  int ret = OB_SUCCESS;
  if (explicit_layout_) for (auto *expr : results_) {
    if (OB_FAIL(mark_expr_produced(expr, branch_id_, id_, ctx))) return ret;
    if (!is_plan_root() && OB_FAIL(add_var_to_array_no_dup(output_exprs_, expr))) return ret;
  }
  return ObLogicalOperator::allocate_expr_post(ctx);
}
int LogPluginCustom::check_output_dependance(ObIArray<ObRawExpr *> &child_output, PPDeps &deps)
{
  if (!explicit_layout_) return ObLogicalOperator::check_output_dependance(child_output, deps);
  // Output implementation belongs to the plugin; its SQL expression children
  // must not implicitly reintroduce undeclared input dependencies at pruning.
  ObRawExprCheckDep checker(child_output, deps, false);
  if (!fragment()) return checker.check(inputs_);
  // Core calls us with the actual child's output array during its pruning.
  for (int64_t child = 0; child < get_num_of_child(); ++child) {
    if (&child_output == &get_child(child)->get_output_exprs()) {
      ObSEArray<ObRawExpr *, 8> required;
      int ret = OB_SUCCESS;
      for (uint32_t i = input_offsets_.at(child); i < input_offsets_.at(child + 1); ++i)
        if (OB_FAIL(required.push_back(inputs_.at(i)))) return ret;
      return checker.check(required);
    }
  }
  return OB_INVALID_ARGUMENT;
}
// A child value not emitted by a buffering plugin cannot be read after next:
// it could be the final child row rather than the row being returned. Validate
// the expression closure instead of trusting flags left in the shared frame.
static int layout_covers(const ObIArray<ObRawExpr *> &required, const ObIArray<ObRawExpr *> &available)
{
  std::vector<const ObRawExpr *> work;
  std::unordered_set<const ObRawExpr *> seen;
  for (int64_t i = 0; i < required.count(); ++i) work.push_back(required.at(i));
  while (!work.empty()) {
    const auto *expr = work.back(); work.pop_back();
    if (!expr) return OB_INVALID_ARGUMENT;
    if (!seen.insert(expr).second) continue;
    if (seen.size() > 16384) return OB_SIZE_OVERFLOW;
    if (ObOptimizerUtil::find_item(available, expr) || expr->is_const_expr() || expr->is_const_or_param_expr()) continue;
    // Match core codegen's independently evaluable zero-argument functions;
    // RAND()/UUID()/a zero-argument UDF need no stale child Datum to execute.
    if ((expr->is_sys_func_expr() || expr->is_udf_expr()) && expr->get_param_count() == 0) continue;
    if (expr->get_param_count() == 0 || expr->is_column_ref_expr() || expr->is_aggr_expr() || expr->is_win_func_expr())
      return OB_INVALID_ARGUMENT;
    for (int64_t i = 0; i < expr->get_param_count(); ++i) work.push_back(expr->get_param_expr(i));
  }
  return OB_SUCCESS;
}
int LogPluginCustom::validate_layout() const
try {
  if (!explicit_layout_) return OB_SUCCESS;
  if (!fragment() && !get_child(first_child)) return OB_INVALID_ARGUMENT;
  int ret = validate_parameter_owners();
  if (OB_SUCC(ret)) ret = validate_results();
  if (OB_SUCC(ret) && !fragment()) ret = layout_covers(inputs_, get_child(first_child)->get_output_exprs());
  for (int64_t child = 0; OB_SUCC(ret) && fragment() && child < get_num_of_child(); ++child) {
    ObSEArray<ObRawExpr *, 8> required;
    for (uint32_t i = input_offsets_.at(child); OB_SUCC(ret) && i < input_offsets_.at(child + 1); ++i)
      ret = required.push_back(inputs_.at(i));
    if (OB_SUCC(ret)) ret = layout_covers(required, get_child(child)->get_output_exprs());
  }
  if (OB_SUCC(ret)) ret = layout_covers(get_output_exprs(), results_);
  if (OB_SUCC(ret)) ret = layout_covers(get_filter_exprs(), results_);
  if (OB_SUCC(ret)) ret = layout_covers(get_startup_exprs(), results_);
  return ret;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }
int LogPluginCustom::configure(const seekdb_plugin_custom_path_request_v1_t &r)
try {
  if (!share::g_mp || !r.service_id || !r.service_major ||
      r.plan_size > SEEKDB_PLUGIN_CUSTOM_MAX_PLAN_BYTES || (r.plan_size && !r.plan) ||
      (r.flags & ~(SEEKDB_PLUGIN_PATH_PRESERVES_ORDER | SEEKDB_PLUGIN_PATH_BLOCKING)) ||
      !std::isfinite(r.operator_cost) || r.operator_cost < 0) return OB_INVALID_ARGUMENT;
  for (auto word : r.reserved) if (word) return OB_INVALID_ARGUMENT;
  int ret = share::g_mp->bind_plugin_custom_executor(r.service_id, r.service_major, r.minimum_minor, binding_);
  if (ret != OB_SUCCESS) return ret;
  if (r.plan_size) parameters_.assign(reinterpret_cast<const char *>(r.plan), r.plan_size);
  operator_cost_ = r.operator_cost; flags_ = r.flags;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }
int LogPluginCustom::est_cost()
{
  if (target_) {
    double cost = operator_cost_;
    for (int64_t i = 0; i < get_num_of_child(); ++i) cost += get_child(i)->get_cost();
    if (!std::isfinite(cost)) return OB_INVALID_ARGUMENT;
    set_card(target_->get_card()); set_op_cost(operator_cost_); set_cost(cost);
    return OB_SUCCESS;
  }
  const auto *input = get_child(first_child);
  if (!input || !std::isfinite(input->get_cost() + operator_cost_)) return OB_INVALID_ARGUMENT;
  set_card(input->get_card()); set_op_cost(operator_cost_); set_cost(input->get_cost() + operator_cost_);
  return OB_SUCCESS;
}
int LogPluginCustom::do_re_est_cost(EstimateCostInfo &param, double &card, double &op_cost, double &cost)
{
  if (target_) {
    card = get_card(); op_cost = operator_cost_; cost = op_cost;
    for (int64_t i = 0; i < get_num_of_child(); ++i) {
      auto child_param = param;
      // A fragment does not imply any input/output row-count ratio.
      child_param.need_row_count_ = -1;
      double child_card = 0, child_cost = 0;
      int ret = SMART_CALL(get_child(i)->re_est_cost(child_param, child_card, child_cost));
      if (ret != OB_SUCCESS) return ret;
      cost += child_cost;
    }
    return std::isfinite(cost) ? OB_SUCCESS : OB_INVALID_ARGUMENT;
  }
  auto *input = get_child(first_child);
  if (!input) return OB_ERR_UNEXPECTED;
  auto child_param = param;
  if (is_block_op()) child_param.need_row_count_ = -1;
  double child_cost = 0;
  int ret = SMART_CALL(input->re_est_cost(child_param, card, child_cost));
  op_cost = operator_cost_; cost = child_cost + op_cost;
  return ret != OB_SUCCESS ? ret : std::isfinite(cost) ? OB_SUCCESS : OB_INVALID_ARGUMENT;
}
int LogPluginCustom::compute_op_ordering()
{
  if (target_ && (flags_ & SEEKDB_PLUGIN_PATH_PRESERVES_ORDER)) {
    int ret = set_op_ordering(target_->get_op_ordering());
    set_is_local_order(target_->get_is_local_order()); set_is_range_order(target_->get_is_range_order());
    set_interesting_order_info(target_->get_interesting_order_info());
    return ret;
  }
  if (flags_ & SEEKDB_PLUGIN_PATH_PRESERVES_ORDER) return ObLogicalOperator::compute_op_ordering();
  get_op_ordering().reset(); set_is_local_order(false); set_is_range_order(false);
  set_interesting_order_info(OrderingFlag::NOT_MATCH);
  return OB_SUCCESS;
}
int LogPluginCustom::get_explain_name_internal(char *buf, int64_t buf_len, int64_t &pos)
{
  return BUF_PRINTF("PLUGIN CUSTOM(%s)", binding_.service_id.c_str());
}
uint64_t LogPluginCustom::hash(uint64_t seed) const
{
  seed = ObLogicalOperator::hash(seed);
  for (const auto *s : {&binding_.service_id, &binding_.owner_id, &binding_.runtime_incarnation, &parameters_})
    seed = ObString(s->size(), s->data()).hash(seed);
  seed = do_hash(binding_.generation, seed); seed = do_hash(binding_.major, seed);
  seed = do_hash(binding_.minor, seed); seed = do_hash(binding_.patch, seed);
  seed = do_hash(explicit_layout_, seed);
  seed = do_hash(fragment(), seed);
  for (auto offset : input_offsets_) seed = do_hash(offset, seed);
  for (int64_t i = 0; i < input_bindings_.count(); ++i) {
    seed = do_hash(input_bindings_.at(i)->get_expr_hash(), seed);
    seed = do_hash(binding_sources_.at(i), seed);
    seed = do_hash(binding_inputs_.at(i), seed);
    seed = do_hash(binding_targets_.at(i), seed);
  }
  for (auto *expr : inputs_) seed = expr->hash(seed);
  seed = do_hash(inputs_.count(), seed);
  for (auto *expr : results_) seed = expr->hash(seed);
  seed = do_hash(results_.count(), seed);
  seed = do_hash(flags_, seed); return do_hash(operator_cost_, seed);
}
} }
