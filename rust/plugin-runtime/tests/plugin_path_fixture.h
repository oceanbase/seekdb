// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_PLUGIN_PATH_FIXTURE_H_
#define SEEKDB_TEST_PLUGIN_PATH_FIXTURE_H_
#include "sql/optimizer/plugin_path.h"
#include "sql/optimizer/ob_log_sort.h"
namespace plugin_path_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
inline void run(ObLogPlan &plan)
{
  ObJoinOrder relation(&plan.get_allocator(), &plan, JOIN);
  CHECK(relation.get_tables().add_member(1) == OB_SUCCESS);
  relation.set_output_rows(7);
  ObShardingInfo local; local.set_location_type(OB_TBL_LOCATION_LOCAL);
  class CostProbe final : public ObLogSort {
  public:
    explicit CostProbe(ObLogPlan &plan) : ObLogSort(plan) {}
    int calls = 0; bool fail = false;
    int do_re_est_cost(EstimateCostInfo &, double &rows, double &op, double &cost) override
    {
      ++calls;
      if (fail) return OB_TIMEOUT;
      rows = 4; op = 2; cost = 5;
      return OB_SUCCESS;
    }
  } root(plan);
  root.set_type(log_op_def::LOG_SORT);
  root.set_strong_sharding(&local); root.set_parallel(1); root.set_available_parallel(1);
  root.set_table_set(&relation.get_tables()); root.set_card(7); root.set_cost(8); root.set_op_cost(3);
  CHECK(root.get_ambient_card().push_back(7) == OB_SUCCESS);
  ObPCParamEqualInfo equal{};
  equal.first_param_idx_ = 1; equal.second_param_idx_ = 2;
  CHECK(root.equal_param_constraints_.push_back(equal) == OB_SUCCESS);
  ObPCConstParamInfo constant;
  CHECK(root.const_param_constraints_.push_back(constant) == OB_SUCCESS);
  ObExprConstraint expression;
  CHECK(root.expr_constraints_.push_back(expression) == OB_SUCCESS);
  PluginPath path(relation);
  EstimateCostInfo estimate; double rows = -1, cost = -1;
  CHECK(path.estimate_cost() == OB_STATE_NOT_MATCH);
  CHECK(path.re_estimate_cost(estimate, rows, cost) == OB_STATE_NOT_MATCH);
  CHECK(path.initialize(root) == OB_SUCCESS);
  CHECK(path.initialize(root) == OB_INVALID_ARGUMENT);
  CHECK(path.log_op_ == &root && path.cost_ == 8 && path.op_cost_ == 3);
  CHECK(path.parallel_ == 1 && path.strong_sharding_ == &local && path.get_path_output_rows() == 7);
  CHECK(path.equal_param_constraints_.count() == 1 && path.const_param_constraints_.count() == 1 &&
      path.expr_constraints_.count() == 1 && path.ambient_card_.count() == 1);
  CHECK(path.re_estimate_cost(estimate, rows, cost) == OB_SUCCESS && rows == 7 && cost == 8);
  estimate.override_ = true;
  CHECK(path.re_estimate_cost(estimate, rows, cost) == OB_SUCCESS && rows == 4 && cost == 5);
  CHECK(root.calls == 1 && path.cost_ == 5 && path.op_cost_ == 2);
  root.fail = true;
  CHECK(path.re_estimate_cost(estimate, rows, cost) == OB_TIMEOUT && root.calls == 2);
  CHECK(path.cost_ == 5 && path.op_cost_ == 2);
  root.fail = false; root.set_card(7); root.set_cost(8); root.set_op_cost(3);
  CHECK(path.estimate_cost() == OB_SUCCESS && path.cost_ == 8 && path.op_cost_ == 3);
  CHECK(path.compute_pipeline_info() == OB_SUCCESS &&
      path.is_pipelined_path() == root.is_pipelined_plan() &&
      path.is_nl_style_pipelined_path() == root.is_nl_style_pipelined_plan());
  for (auto kind : {ACCESS, JOIN, SUBQUERY, FUNCTION_TABLE_ACCESS, JSON_TABLE_ACCESS,
      TEMP_TABLE_ACCESS, VALUES_TABLE_ACCESS, FAKE_CTE_TABLE_ACCESS}) {
    relation.set_type(kind);
    CHECK(path.is_plugin_path() && !path.is_access_path() && !path.is_join_path() &&
        !path.is_subquery_path() && !path.is_function_table_path() && !path.is_json_table_path() &&
        !path.is_temp_table_path() && !path.is_values_table_path() && !path.is_cte_path());
  }
  relation.set_type(JOIN);
  JoinPath native;
  native.parent_ = &relation; native.cost_ = 100; native.op_cost_ = 90;
  native.parallel_ = 1; native.strong_sharding_ = &local;
  CHECK(native.is_join_path() && !native.is_plugin_path());
  CHECK(relation.add_path(&native) == OB_SUCCESS);
  Path *published = nullptr;
  CHECK(relation.add_plugin_path(root, published) == OB_SUCCESS && published);
  CHECK(relation.get_interesting_paths().count() == 2 && published->is_plugin_path());
  ObLogicalOperator *tree = nullptr;
  CHECK(plan.create_plan_tree_from_path(published, tree) == OB_SUCCESS && tree == &root);
  CHECK(root.get_parent() == nullptr);
  char name[256]{}; int64_t length = 0;
  CHECK(published->get_name(name, sizeof(name), length) == OB_SUCCESS &&
      std::strstr(name, "PLUGIN PATH") != nullptr);
  CHECK(relation.add_recycled_paths(published) == OB_SUCCESS);
  const int64_t recycled = plan.get_recycled_join_paths().count();
  native.log_op_ = &root;
  CHECK(relation.add_recycled_paths(&native) == OB_SUCCESS);
  CHECK(native.log_op_ == &root && native.parent_ == &relation && native.cost_ == 100);
  CHECK(plan.get_recycled_join_paths().count() == recycled);
  // Invalid alternatives never enter the relation or change the cached tree.
  for (int fault = 0; fault < 3; ++fault) {
    ObLogSort invalid(plan);
    invalid.set_table_set(&relation.get_tables()); invalid.set_strong_sharding(&local);
    invalid.set_parallel(fault == 0 ? 2 : 1); invalid.set_card(7); invalid.set_cost(fault == 1 ? -1 : 8);
    if (fault == 2) invalid.set_table_set(nullptr);
    Path *rejected = published;
    CHECK(relation.add_plugin_path(invalid, rejected) ==
        (fault == 0 ? OB_NOT_SUPPORTED : OB_INVALID_ARGUMENT));
    CHECK(!rejected && relation.get_interesting_paths().count() == 2 && published->log_op_ == &root);
  }
  relation.get_interesting_paths().reset();
  static_cast<PluginPath *>(published)->~PluginPath();
  plan.get_allocator().free(published);
  std::cerr << "plugin path: concrete identity, native coexistence, properties and logical tree reuse passed" << std::endl;
}
template <typename Provider>
inline void upper(ObLogPlan &plan, Provider &provider)
{
  auto *root = plan.get_plan_root(); auto *parent = root->get_parent();
  ObSEArray<CandidatePlan, 1> input; CHECK(input.push_back(CandidatePlan(root)) == OB_SUCCESS);
  for (auto phase : {SEEKDB_PLUGIN_PHASE_GROUP, SEEKDB_PLUGIN_PHASE_WINDOW,
      SEEKDB_PLUGIN_PHASE_DISTINCT, SEEKDB_PLUGIN_PHASE_ORDERED}) {
    for (int mode = 1; mode <= (phase == SEEKDB_PLUGIN_PHASE_ORDERED ? 6 : 5); ++mode) {
      provider.upper_probe_mode_ = mode;
      ObSEArray<CandidatePlan, 4> output;
      CHECK(output.push_back(CandidatePlan(root)) == OB_SUCCESS);
      CandidatePlan unused;
      const int expected = mode <= 2 ? OB_SUCCESS : mode == 3 ? OB_TIMEOUT :
          mode == 6 ? OB_NOT_SUPPORTED : OB_INVALID_ARGUMENT;
      CHECK(plan.run_plugin_candidate_phase(input, unused, &output, phase) == expected);
      CHECK(root->get_parent() == parent && input.count() == 1 && input.at(0).plan_tree_ == root);
      CHECK(output.count() == (mode <= 2 ? mode == 1 ? 1 : 3 : 0));
      CHECK(!unused.plan_tree_);
    }
  }
  provider.upper_probe_mode_ = 0;
  std::cerr << "upper paths: independent stage routing, atomic contributions, forbidden selection and ordering guard passed" << std::endl;
}
template <typename Provider>
inline void subproblem(ObLogPlan &plan, Provider &provider)
{
  auto *root = plan.get_plan_root();
  auto *parent = root->get_parent();
  const bool saved_enabled = provider.candidate_subproblem_enabled_;
  provider.candidate_subproblem_enabled_ = false;
  for (int mode = 0; mode <= 5; ++mode) {
    provider.join_probe_mode_ = mode;
    CHECK(plan.refresh_plugin_join_hooks() == OB_SUCCESS);
    ObJoinOrder relation(&plan.get_allocator(), &plan, JOIN);
    CHECK(relation.get_tables().add_members(root->get_table_set()) == OB_SUCCESS);
    relation.set_output_rows(root->get_card());
    JoinPath native;
    native.parent_ = &relation; native.log_op_ = root;
    native.parallel_ = 1; native.strong_sharding_ = root->get_strong_sharding();
    CHECK(relation.get_interesting_paths().push_back(&native) == OB_SUCCESS);
    const int before = provider.subproblem_calls_;
    const int expected = mode <= 2 ? OB_SUCCESS : mode == 3 ? OB_TIMEOUT : OB_INVALID_ARGUMENT;
    CHECK(relation.contribute_plugin_join_paths() == expected);
    CHECK(provider.subproblem_calls_ == before + (mode != 0));
    CHECK(root->get_parent() == parent && native.log_op_ == root);
    CHECK(relation.get_interesting_paths().count() == (mode == 2 ? 3 : 1));
    JoinPath later;
    if (mode <= 2) {
      const int once = provider.subproblem_calls_;
      CHECK(relation.contribute_plugin_join_paths() == OB_SUCCESS);
      CHECK(provider.subproblem_calls_ == once); // No duplicate or self-contribution.
      if (mode != 0) {
        later.parent_ = &relation; later.log_op_ = root;
        later.parallel_ = 1; later.strong_sharding_ = root->get_strong_sharding();
        CHECK(relation.get_interesting_paths().push_back(&later) == OB_SUCCESS);
        CHECK(relation.contribute_plugin_join_paths() == OB_SUCCESS);
        CHECK(provider.subproblem_calls_ == once + 1); // New path identity is not lost.
        CHECK(relation.get_interesting_paths().count() == (mode == 2 ? 6 : 2));
        CHECK(relation.contribute_plugin_join_paths() == OB_SUCCESS);
        CHECK(provider.subproblem_calls_ == once + 1);
      }
    } else {
      provider.join_probe_mode_ = 1;
      CHECK(relation.contribute_plugin_join_paths() == OB_SUCCESS);
      CHECK(provider.subproblem_calls_ == before + 2); // Failed input was not marked seen.
    }
    for (int64_t i = 1; i < relation.get_interesting_paths().count(); ++i) {
      if (!relation.get_interesting_paths().at(i)->is_plugin_path()) continue;
      auto *path = static_cast<PluginPath *>(relation.get_interesting_paths().at(i));
      CHECK(path->is_plugin_path()); path->~PluginPath(); plan.get_allocator().free(path);
    }
    relation.get_interesting_paths().reset();
  }
  provider.join_probe_mode_ = 0;
  provider.candidate_subproblem_enabled_ = saved_enabled;
  CHECK(plan.refresh_plugin_join_hooks() == OB_SUCCESS);
  std::cerr << "JOIN subproblem: disabled fast path, independent route, atomic publication and one-time inputs passed" << std::endl;
}
}
#endif
