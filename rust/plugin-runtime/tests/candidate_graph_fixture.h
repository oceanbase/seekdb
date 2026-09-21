// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_CANDIDATE_GRAPH_FIXTURE_H_
#define SEEKDB_TEST_CANDIDATE_GRAPH_FIXTURE_H_
#include "sql/optimizer/plugin_candidate_graph.h"
#include "sql/optimizer/ob_log_sort.h"
#include "sql/optimizer/log_plugin_custom.h"
#include "sql/resolver/expr/ob_raw_expr_replacer.h"
#include "plugin_path_fixture.h"
#include "candidate_values_fixture.h"
namespace candidate_graph_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
inline void multi_owner(ObLogPlan &plan, ObRawExprFactory &factory) {
  ObLogSort a(plan), b(plan), c_left(plan), c_right(plan);
  ObLogJoin lower(plan), upper(plan), c(plan);
  ObShardingInfo local; local.set_location_type(OB_TBL_LOCATION_LOCAL);
  for (auto *node : {static_cast<ObLogicalOperator *>(&a), static_cast<ObLogicalOperator *>(&b),
      static_cast<ObLogicalOperator *>(&c), static_cast<ObLogicalOperator *>(&lower), static_cast<ObLogicalOperator *>(&upper),
      static_cast<ObLogicalOperator *>(&c_left), static_cast<ObLogicalOperator *>(&c_right)}) {
    node->set_strong_sharding(&local); node->set_parallel(1); node->set_available_parallel(1);
  }
  for (auto *join : {&lower, &upper, &c}) { join->set_type(log_op_def::LOG_JOIN); join->set_join_algo(NESTED_LOOP_JOIN); }
  c.set_child(0, &c_left); c.set_child(1, &c_right);
  lower.set_child(0, &a); lower.set_child(1, &b); upper.set_child(0, &lower); upper.set_child(1, &c);
  ObColumnRefRawExpr *columns[3]{};
  ObLogicalOperator *leaves[] = {&a, &b, &c};
  for (int i = 0; i < 3; ++i) {
    CHECK(factory.create_raw_expr(T_REF_COLUMN, columns[i]) == OB_SUCCESS);
    columns[i]->set_ref_id(900 + i, 1); columns[i]->set_data_type(ObIntType);
    CHECK(leaves[i]->get_startup_exprs().push_back(columns[i]) == OB_SUCCESS);
    CHECK(leaves[i]->get_output_exprs().push_back(columns[i]) == OB_SUCCESS);
  }
  ObExecParamRawExpr params[3];
  params[0].set_ref_expr(columns[0]); params[1].set_ref_expr(columns[0]); params[2].set_ref_expr(columns[1]);
  // The indexed setter also copies the referenced expression's result type.
  for (int i = 0; i < 3; ++i) params[i].set_param_index(i);
  CHECK(lower.get_nl_params().push_back(&params[0]) == OB_SUCCESS);
  CHECK(upper.get_nl_params().push_back(&params[1]) == OB_SUCCESS && upper.get_nl_params().push_back(&params[2]) == OB_SUCCESS);
  for (int bad = -1; bad < 8; ++bad) {
    CandidateGraph graph; uint32_t root = 0, inner = 0, plans[3], values[3], parameter[3], unused;
    CHECK(graph.root(&upper, &root) == SEEKDB_PLUGIN_STATUS_OK);
    CHECK(graph.child(root, 0, &inner) == SEEKDB_PLUGIN_STATUS_OK);
    // Physical order B,C,A intentionally differs from dependency order A,B,C.
    CHECK(graph.child(inner, 1, &plans[0]) == SEEKDB_PLUGIN_STATUS_OK);
    CHECK(graph.child(root, 1, &plans[1]) == SEEKDB_PLUGIN_STATUS_OK);
    CHECK(graph.child(inner, 0, &plans[2]) == SEEKDB_PLUGIN_STATUS_OK);
    for (int i = 0; i < 3; ++i) CHECK(graph.expression(plans[i], SEEKDB_PLUGIN_PLAN_STARTUP, 0, &values[i], &unused) == SEEKDB_PLUGIN_STATUS_OK);
    CHECK(graph.binding(inner, SEEKDB_PLUGIN_BIND_JOIN_NESTED_LOOP, 0, &parameter[0], &unused) == SEEKDB_PLUGIN_STATUS_OK);
    CHECK(graph.binding(root, SEEKDB_PLUGIN_BIND_JOIN_NESTED_LOOP, 0, &parameter[1], &unused) == SEEKDB_PLUGIN_STATUS_OK);
    CHECK(graph.binding(root, SEEKDB_PLUGIN_BIND_JOIN_NESTED_LOOP, 1, &parameter[2], &unused) == SEEKDB_PLUGIN_STATUS_OK);
    uint32_t offsets[] = {0,1,2,3};
    seekdb_plugin_input_binding_v1_t bindings[] = {{parameter[0],2,0,0}, {parameter[1],2,0,1}, {parameter[2],0,0,1}};
    seekdb_plugin_custom_path_request_v4_t request{};
    request.v3.v2.inputs = values; request.v3.v2.input_count = 3;
    request.v3.input_plans = plans; request.v3.plan_count = 3;
    request.v3.input_offsets = offsets; request.v3.execution = SEEKDB_PLUGIN_CUSTOM_LOCAL_SERIAL;
    request.bindings = bindings; request.binding_count = 3;
    if (bad == 0) request.binding_count = 2; // Missing a removed owner's parameter.
    if (bad == 1) bindings[0].target_input = 1; // Wrong complete consumer subtree.
    if (bad == 2) bindings[1].parameter = bindings[0].parameter;
    if (bad == 3) { values[1] = values[2]; bindings[0].source_input = 1; } // Same expression, outside owner's left scope.
    if (bad == 4) bindings[2].source_column = 1;
    if (bad == 5) CHECK(lower.get_above_pushdown_left_params().push_back(&params[0]) == OB_SUCCESS);
    ObExecParamRawExpr alias; alias.set_ref_expr(columns[1]); alias.set_param_index(params[2].get_param_index());
    if (bad == 6 || bad == 7) CHECK(c.get_nl_params().push_back(bad == 6 ? &params[0] : &alias) == OB_SUCCESS);
    LogPluginCustom custom(plan);
    CHECK(custom.configure_fragment(request.v3, graph, upper) == OB_SUCCESS);
    CHECK(custom.configure_bindings(request, graph) == (bad < 0 ? OB_SUCCESS : bad >= 5 ? OB_NOT_SUPPORTED : OB_INVALID_ARGUMENT));
    if (bad < 0) {
      CHECK(custom.input_bindings().count() == 3);
      CHECK(custom.binding_inputs().at(0) == 2 && custom.binding_targets().at(0) == 0);
      CHECK(custom.binding_sources().at(2) == 0 && custom.binding_targets().at(2) == 1);
      LogPluginCustom missing(plan);
      CHECK(missing.configure_fragment(request.v3, graph, upper) == OB_SUCCESS);
      CHECK(missing.validate_layout() == OB_NOT_SUPPORTED); // A v3 request cannot silently discard owners.
    }
    lower.get_above_pushdown_left_params().reset();
    c.get_nl_params().reset();
    CHECK(a.get_parent() == &lower && b.get_parent() == &lower && c.get_parent() == &upper && lower.get_parent() == &upper);
  }
  std::cerr << "multi-owner fragment: complete NLJ owners, reordered sources, fan-in and missing-owner rejection passed" << std::endl;
}
inline void sort_semantics(ObLogPlan &plan, ObRawExprFactory &factory) {
  class ReadOnlySort final : public ObLogSort {
  public:
    explicit ReadOnlySort(ObLogPlan &plan) : ObLogSort(plan) { set_type(log_op_def::LOG_SORT); }
    int get_op_exprs(ObIArray<ObRawExpr *> &) override { CHECK(false); return OB_ERR_UNEXPECTED; }
  } sort(plan);
  ObColumnRefRawExpr *column = nullptr;
  CHECK(factory.create_raw_expr(T_REF_COLUMN, column) == OB_SUCCESS);
  column->set_ref_id(123, 1); column->set_data_type(ObIntType);
  ObSEArray<OrderItem, 4> keys;
  for (auto direction : {NULLS_LAST_ASC, NULLS_LAST_DESC, NULLS_FIRST_ASC, NULLS_FIRST_DESC})
    CHECK(keys.push_back(OrderItem(column, direction)) == OB_SUCCESS);
  CHECK(sort.set_sort_keys(keys) == OB_SUCCESS);
  CandidateGraph graph; uint32_t root = 0, expr = UINT32_MAX, flags = 0;
  CHECK(graph.root(&sort, &root) == SEEKDB_PLUGIN_STATUS_OK);
  seekdb_plugin_sort_info_v1_t info{}; info.struct_size = sizeof(info);
  CHECK(graph.sort_info(root, &info) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(info.flags == SEEKDB_PLUGIN_SORT_PRESENT && info.key_count == 4 && info.prefix_key_count == 0 && info.partition_key_count == 0);
  CHECK(info.topn_expression == UINT32_MAX && info.topk_limit_expression == UINT32_MAX &&
      info.topk_offset_expression == UINT32_MAX && info.hash_expression == UINT32_MAX);
  uint32_t identity = UINT32_MAX;
  for (uint32_t i = 0; i < 4; ++i) {
    CHECK(graph.sort_key(root, i, &expr, &flags) == SEEKDB_PLUGIN_STATUS_OK && flags == i);
    if (!i) identity = expr; else CHECK(expr == identity);
  }
  sort.set_prefix_pos(1); sort.set_part_cnt(2); sort.set_local_merge_sort(true); sort.set_fetch_with_ties(true);
  sort.set_topn_expr(column); sort.set_topk_limit_expr(column); sort.set_topk_offset_expr(column);
  sort.set_hash_sortkey(OrderItem(column, NULLS_FIRST_ASC));
  CHECK(sort.get_encode_sortkeys().push_back(OrderItem(column, NULLS_FIRST_ASC)) == OB_SUCCESS);
  info.struct_size = sizeof(info);
  CHECK(graph.sort_info(root, &info) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(info.flags == (SEEKDB_PLUGIN_SORT_PRESENT | SEEKDB_PLUGIN_SORT_ENCODED_KEYS |
      SEEKDB_PLUGIN_SORT_LOCAL_MERGE | SEEKDB_PLUGIN_SORT_WITH_TIES));
  CHECK(info.prefix_key_count == 1 && info.partition_key_count == 2 && info.topn_expression == identity &&
      info.topk_limit_expression == identity && info.topk_offset_expression == identity && info.hash_expression == identity);
  CHECK(sort.get_output_exprs().empty() && sort.get_op_ordering().empty());
  ObLogJoin join(plan); join.set_type(log_op_def::LOG_JOIN); uint32_t non_sort = 0;
  CHECK(graph.root(&join, &non_sort) == SEEKDB_PLUGIN_STATUS_OK);
  info.struct_size = sizeof(info);
  CHECK(graph.sort_info(non_sort, &info) == SEEKDB_PLUGIN_STATUS_OK && info.flags == 0 && info.key_count == 0 &&
      info.topn_expression == UINT32_MAX && info.topk_limit_expression == UINT32_MAX &&
      info.topk_offset_expression == UINT32_MAX && info.hash_expression == UINT32_MAX);
  for (int fault = 0; fault < 10; ++fault) {
    CandidateGraph invalid; CHECK(invalid.root(&sort, &root) == SEEKDB_PLUGIN_STATUS_OK);
    info = {}; info.struct_size = sizeof(info); expr = 42; flags = 42;
    int expected = OB_INVALID_ARGUMENT;
    if (fault == 0) { info.struct_size = 0; CHECK(invalid.sort_info(root, &info) != SEEKDB_PLUGIN_STATUS_OK); }
    if (fault == 1) CHECK(invalid.sort_info(UINT32_MAX, &info) != SEEKDB_PLUGIN_STATUS_OK);
    if (fault == 2 || fault == 3) {
      sort.set_prefix_pos(fault == 2 ? -1 : 5); expected = OB_INVALID_DATA;
      CHECK(invalid.sort_info(root, &info) != SEEKDB_PLUGIN_STATUS_OK); sort.set_prefix_pos(1);
    }
    if (fault == 4) {
      sort.set_part_cnt(5); expected = OB_INVALID_DATA;
      CHECK(invalid.sort_info(root, &info) != SEEKDB_PLUGIN_STATUS_OK); sort.set_part_cnt(2);
    }
    if (fault == 5) CHECK(invalid.sort_key(root, 4, &expr, &flags) != SEEKDB_PLUGIN_STATUS_OK);
    if (fault == 6) CHECK(invalid.sort_key(root, 0, &expr, &expr) != SEEKDB_PLUGIN_STATUS_OK);
    if (fault == 7) CHECK(invalid.sort_key(root, 0, nullptr, &flags) != SEEKDB_PLUGIN_STATUS_OK);
    if (fault == 8 || fault == 9) {
      keys.at(0) = OrderItem(fault == 8 ? column : nullptr, static_cast<ObOrderDirection>(fault == 8 ? 99 : NULLS_FIRST_ASC));
      CHECK(sort.set_sort_keys(keys) == OB_SUCCESS);
      CHECK(invalid.sort_key(root, 0, &expr, &flags) != SEEKDB_PLUGIN_STATUS_OK);
      expected = fault == 8 ? OB_INVALID_DATA : OB_INVALID_ARGUMENT;
      keys.at(0) = OrderItem(column, NULLS_LAST_ASC); CHECK(sort.set_sort_keys(keys) == OB_SUCCESS);
    }
    CHECK(invalid.error() == expected);
    info.struct_size = sizeof(info);
    CHECK(invalid.sort_info(root, &info) != SEEKDB_PLUGIN_STATUS_OK && info.struct_size == 0);
  }
  std::cerr << "sort semantics: normalized directions, special expressions, non-SORT and sticky failure passed" << std::endl;
}
inline void scalar_semantics(ObLogPlan &plan, ObRawExprFactory &factory) {
  ObLogSort node(plan);
  ObColumnRefRawExpr *column = nullptr;
  CHECK(factory.create_raw_expr(T_REF_COLUMN, column) == OB_SUCCESS);
  column->set_ref_id(123, 1); column->set_data_type(ObIntType);
  CHECK(node.get_startup_exprs().push_back(column) == OB_SUCCESS);
  CandidateGraph graph; uint32_t root = 0, expr = 0, unused = 0;
  CHECK(graph.root(&node, &root) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(graph.expression(root, SEEKDB_PLUGIN_PLAN_STARTUP, 0, &expr, &unused) == SEEKDB_PLUGIN_STATUS_OK);
  for (const char *id : {"core.type.bool", "core.type.int32", "core.type.uint32",
                        "core.type.int64", "core.type.uint64", "org.example.int64"}) {
    for (int fault = 0; fault < 3; ++fault) {
      const bool unsigned_value = std::strcmp(id, "core.type.uint64") == 0;
      column->set_data_type(unsigned_value ? ObUInt64Type : ObIntType);
      PluginExprType type; type.logical_id_ = ObString::make_string(id);
      type.physical_type_ = fault == 2 ? ObVarcharType : column->get_data_type();
      type.stored_ = fault == 1;
      type.catalog_epoch_ = 1;
      if (type.stored_) {
        type.sql_name_ = ObString::make_string("test_integer");
        type.owner_ = ObString::make_string("org.example.codec");
        type.format_ = ObString::make_string("test.integer.v1");
        type.format_version_ = 1;
      }
      CHECK(column->set_plugin_type(type) == OB_SUCCESS);
      seekdb_plugin_expr_semantics_v1_t value{}; value.struct_size = sizeof(value);
      CHECK(graph.expression_semantics(expr, &value) == SEEKDB_PLUGIN_STATUS_OK);
      CHECK(value.value_kind == (fault || std::strcmp(id, "org.example.int64") == 0 ?
          SEEKDB_PLUGIN_VALUE_OTHER : unsigned_value ? SEEKDB_PLUGIN_VALUE_UNSIGNED_INTEGER :
          SEEKDB_PLUGIN_VALUE_SIGNED_INTEGER));
      CHECK(value.flags == SEEKDB_PLUGIN_EXPR_SCALAR_DETERMINISTIC);
    }
  }
  column->clear_plugin_type(); column->set_data_type(ObIntType);
  // CASE is a separate raw-expression class. Inspect every arm (including
  // its optional argument/default), not just the root's cached flags.
  ObCaseOpRawExpr *conditional = nullptr;
  CHECK(factory.create_raw_expr(T_OP_CASE, conditional) == OB_SUCCESS);
  ObColumnRefRawExpr *parts[4]{};
  for (auto &part : parts) {
    CHECK(factory.create_raw_expr(T_REF_COLUMN, part) == OB_SUCCESS);
    part->set_ref_id(123, 2); part->set_data_type(ObIntType);
  }
  conditional->set_arg_param_expr(parts[0]);
  CHECK(conditional->add_when_param_expr(parts[1]) == OB_SUCCESS);
  CHECK(conditional->add_then_param_expr(parts[2]) == OB_SUCCESS);
  conditional->set_default_param_expr(parts[3]); conditional->set_data_type(ObIntType);
  CHECK(node.get_startup_exprs().push_back(conditional) == OB_SUCCESS);
  CHECK(graph.expression(root, SEEKDB_PLUGIN_PLAN_STARTUP, 1, &expr, &unused) == SEEKDB_PLUGIN_STATUS_OK);
  for (int impure = -1; impure < 4; ++impure) {
    if (impure >= 0) parts[impure]->set_is_deterministic(false);
    seekdb_plugin_expr_semantics_v1_t value{}; value.struct_size = sizeof(value);
    CHECK(graph.expression_semantics(expr, &value) == SEEKDB_PLUGIN_STATUS_OK);
    CHECK(value.value_kind == SEEKDB_PLUGIN_VALUE_SIGNED_INTEGER);
    CHECK(value.flags == (impure < 0 ? SEEKDB_PLUGIN_EXPR_SCALAR_DETERMINISTIC : 0));
    if (impure >= 0) parts[impure]->set_is_deterministic(true);
  }
  std::cerr << "scalar semantics: core integer identities, stored/custom rejection and recursive CASE passed" << std::endl;
}
inline void run(ObLogPlan &plan, ObRawExprFactory &factory) {
  candidate_values_test::run(plan, factory);
  scalar_semantics(plan, factory);
  sort_semantics(plan, factory);
  plugin_path_test::run(plan);
  multi_owner(plan, factory);
  class ReadOnlyProbe final : public ObLogSort {
  public:
    explicit ReadOnlyProbe(ObLogPlan &plan) : ObLogSort(plan) {}
    int get_op_exprs(ObIArray<ObRawExpr *> &) override { CHECK(false); return OB_ERR_UNEXPECTED; }
  } child(plan);
  {
    ObLogSort left(plan), right(plan), foreign(plan);
    // Direct construction bypasses the operator factory, which assigns the tag.
    ObLogJoin target(plan); target.set_type(log_op_def::LOG_JOIN); target.set_join_type(INNER_JOIN);
    target.set_child(0, &left); target.set_child(1, &right);
    ObShardingInfo local; local.set_location_type(OB_TBL_LOCATION_LOCAL);
    for (auto *node : {static_cast<ObLogicalOperator *>(&left), static_cast<ObLogicalOperator *>(&right),
        static_cast<ObLogicalOperator *>(&foreign), static_cast<ObLogicalOperator *>(&target)}) {
      node->set_strong_sharding(&local); node->set_parallel(1); node->set_available_parallel(1);
    }
    left.set_card(1); right.set_card(2); target.set_card(7);
    left.set_cost(3); right.set_cost(5); target.set_cost(12);
    left.set_width(8); right.set_width(16); target.set_width(24);
    left.set_is_at_most_one_row(true); target.set_is_at_most_one_row(false);
    ObColumnRefRawExpr *a = nullptr, *b = nullptr;
    CHECK(factory.create_raw_expr(T_REF_COLUMN, a) == OB_SUCCESS);
    CHECK(factory.create_raw_expr(T_REF_COLUMN, b) == OB_SUCCESS);
    a->set_ref_id(701, 1); b->set_ref_id(702, 1);
    a->set_data_type(ObIntType); b->set_data_type(ObIntType);
    ObExecParamRawExpr parameter;
    parameter.set_ref_expr(a);
    ObRelIds left_ids, right_ids, target_ids;
    CHECK(left_ids.add_member(1) == OB_SUCCESS && right_ids.add_member(2) == OB_SUCCESS);
    CHECK(target_ids.add_members(left_ids) == OB_SUCCESS && target_ids.add_members(right_ids) == OB_SUCCESS);
    left.set_table_set(&left_ids); right.set_table_set(&right_ids); target.set_table_set(&target_ids);
    CHECK(a->get_relation_ids().add_member(1) == OB_SUCCESS && b->get_relation_ids().add_member(2) == OB_SUCCESS);
    CHECK(left.get_output_exprs().push_back(a) == OB_SUCCESS && right.get_output_exprs().push_back(b) == OB_SUCCESS);
    CHECK(left.get_startup_exprs().push_back(a) == OB_SUCCESS && right.get_startup_exprs().push_back(b) == OB_SUCCESS);
    CHECK(left.get_output_const_exprs().push_back(a) == OB_SUCCESS);
    for (int fault = -1; fault < 12; ++fault) {
      CandidateGraph graph;
      uint32_t root = 0, plans[2], inputs[3], outputs[2], offsets[] = {0, 2, 3}, unused = 0;
      CHECK(graph.root(&target, &root) == SEEKDB_PLUGIN_STATUS_OK);
      CHECK(graph.child(root, 0, &plans[0]) == SEEKDB_PLUGIN_STATUS_OK);
      CHECK(graph.child(root, 1, &plans[1]) == SEEKDB_PLUGIN_STATUS_OK);
      CHECK(graph.expression(plans[0], SEEKDB_PLUGIN_PLAN_STARTUP, 0, &inputs[0], &unused) == SEEKDB_PLUGIN_STATUS_OK);
      CHECK(graph.expression(plans[1], SEEKDB_PLUGIN_PLAN_STARTUP, 0, &inputs[2], &unused) == SEEKDB_PLUGIN_STATUS_OK);
      inputs[1] = inputs[0]; outputs[0] = inputs[0]; outputs[1] = inputs[2];
      if (fault < 0) {
        seekdb_plugin_plan_semantics_v1_t semantic{}; semantic.struct_size = sizeof(semantic);
        CHECK(graph.plan_semantics(root, &semantic) == SEEKDB_PLUGIN_STATUS_OK);
        CHECK(semantic.relation_kind == SEEKDB_PLUGIN_RELATION_INNER_JOIN && semantic.flags == SEEKDB_PLUGIN_PLAN_LOCAL_SERIAL);
        for (uint32_t role = SEEKDB_PLUGIN_BIND_JOIN_NESTED_LOOP; role <= SEEKDB_PLUGIN_BIND_JOIN_RIGHT_PUSH_DOWN; ++role) {
          uint32_t count = UINT32_MAX, p = UINT32_MAX, source = UINT32_MAX;
          CHECK(graph.binding_count(root, role, &count) == SEEKDB_PLUGIN_STATUS_OK && count == 0);
          CHECK(graph.binding_count(plans[0], role, &count) == SEEKDB_PLUGIN_STATUS_OK && count == 0);
          auto &bindings = role == SEEKDB_PLUGIN_BIND_JOIN_NESTED_LOOP ? target.get_nl_params() :
              role == SEEKDB_PLUGIN_BIND_JOIN_LEFT_PUSH_DOWN ? target.get_above_pushdown_left_params() :
              target.get_above_pushdown_right_params();
          CHECK(bindings.push_back(&parameter) == OB_SUCCESS);
          CHECK(graph.binding_count(root, role, &count) == SEEKDB_PLUGIN_STATUS_OK && count == 1);
          CHECK(graph.binding(root, role, 0, &p, &source) == SEEKDB_PLUGIN_STATUS_OK);
          CHECK(p != source && source == inputs[0]);
          ObRawExpr *resolved = nullptr;
          CHECK(graph.resolve_expression(p, resolved) == OB_SUCCESS && resolved == &parameter);
          bindings.reset();
        }
        // Invalid role/index and malformed source initialize both outputs and
        // poison only their own invocation, not the successful graph above.
        for (int broken = 0; broken < 5; ++broken) {
          CandidateGraph invalid; uint32_t id = 0, p = 0, source = 0;
          CHECK(invalid.root(&target, &id) == SEEKDB_PLUGIN_STATUS_OK);
          if (broken == 2) { parameter.set_ref_expr(nullptr); CHECK(target.get_nl_params().push_back(&parameter) == OB_SUCCESS); }
          const auto status = broken == 0 ? invalid.binding_count(id, 0, &p) :
              invalid.binding(broken == 4 ? UINT32_MAX : id, 1, 0, &p, broken == 3 ? &p : &source);
          CHECK(status == SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT && invalid.error() == OB_INVALID_ARGUMENT);
          CHECK(p == (broken == 0 ? 0 : UINT32_MAX));
          if (broken == 1 || broken == 2 || broken == 4) CHECK(source == UINT32_MAX);
          target.get_nl_params().reset(); parameter.set_ref_expr(a);
        }
        uint32_t scope = UINT32_MAX;
        CHECK(graph.scope(plan.get_stmt(), plans[0], inputs[0], &scope) == SEEKDB_PLUGIN_STATUS_OK && scope == SEEKDB_PLUGIN_SCOPE_CONTAINED);
        CHECK(graph.scope(plan.get_stmt(), plans[1], inputs[0], &scope) == SEEKDB_PLUGIN_STATUS_OK && scope == SEEKDB_PLUGIN_SCOPE_OUTSIDE);
        seekdb_plugin_expr_semantics_v1_t value{}; value.struct_size = sizeof(value);
        CHECK(graph.expression_semantics(inputs[0], &value) == SEEKDB_PLUGIN_STATUS_OK);
        CHECK(value.value_kind == SEEKDB_PLUGIN_VALUE_SIGNED_INTEGER && value.flags == SEEKDB_PLUGIN_EXPR_SCALAR_DETERMINISTIC);
        a->set_is_deterministic(false); value.struct_size = sizeof(value);
        CHECK(graph.expression_semantics(inputs[0], &value) == SEEKDB_PLUGIN_STATUS_OK && value.flags == 0);
        a->set_is_deterministic(true);
        uint32_t count = 0;
        CHECK(graph.column_count(plan.get_stmt(), &count) == SEEKDB_PLUGIN_STATUS_OK && count == plan.get_stmt()->get_column_size());
        for (uint32_t i = 0; i < count; ++i) {
          uint32_t id = UINT32_MAX;
          CHECK(graph.column(plan.get_stmt(), i, &id) == SEEKDB_PLUGIN_STATUS_OK);
          seekdb_plugin_expr_info_v1_t info{}; info.struct_size = sizeof(info);
          CHECK(graph.describe(id, &info) == SEEKDB_PLUGIN_STATUS_OK && (info.flags & SEEKDB_PLUGIN_EXPR_COLUMN));
        }
      }
      seekdb_plugin_custom_path_request_v3_t request{};
      request.v2.inputs = inputs; request.v2.input_count = 3;
      request.v2.outputs = outputs; request.v2.output_count = 2;
      request.input_plans = plans; request.plan_count = 2; request.input_offsets = offsets;
      request.execution = SEEKDB_PLUGIN_CUSTOM_LOCAL_SERIAL;
      if (fault == 0) plans[1] = plans[0];
      if (fault == 1) plans[0] = root; // Ancestor and descendant overlap.
      if (fault == 2) CHECK(graph.root(&foreign, &plans[1]) == SEEKDB_PLUGIN_STATUS_OK);
      if (fault == 3) offsets[0] = 1;
      if (fault == 4) offsets[1] = 4;
      if (fault == 5) request.plan_count = 65;
      if (fault == 6) request.input_offsets = nullptr;
      if (fault == 7) request.reserved[0] = 1;
      if (fault == 8) plans[1] = UINT32_MAX;
      if (fault == 9) right.set_parallel(2);
      if (fault == 10) request.execution = 0;
      if (fault == 11) outputs[1] = outputs[0];
      LogPluginCustom custom(plan);
      const int ret = custom.configure_fragment(request, graph, target);
      CHECK(ret == (fault < 0 ? OB_SUCCESS : fault == 9 ? OB_NOT_SUPPORTED : OB_INVALID_ARGUMENT));
      CHECK(left.get_parent() == &target && right.get_parent() == &target);
      right.set_parallel(1);
      if (fault < 0) {
        target.set_join_algo(NESTED_LOOP_JOIN);
        CHECK(target.get_nl_params().push_back(&parameter) == OB_SUCCESS);
        uint32_t parameter_id = 0, source_id = 0;
        CHECK(graph.binding(root, SEEKDB_PLUGIN_BIND_JOIN_NESTED_LOOP, 0, &parameter_id, &source_id) == SEEKDB_PLUGIN_STATUS_OK);
        for (int bad = -1; bad < 10; ++bad) {
          seekdb_plugin_input_binding_v1_t binding{parameter_id, 0, 0, 1};
          seekdb_plugin_custom_path_request_v4_t bound{}; bound.v3 = request;
          bound.bindings = &binding; bound.binding_count = 1;
          if (bad == 0) binding.parameter = source_id;
          if (bad == 1) binding.source_column = 2;
          if (bad == 2) binding.source_input = 1;
          if (bad == 3) binding.target_input = 0;
          if (bad == 4) bound.reserved_word = 1;
          if (bad == 5) bound.reserved[0] = 1;
          if (bad == 6) bound.binding_count = 0;
          if (bad == 7) target.set_join_algo(HASH_JOIN);
          if (bad == 8) CHECK(target.get_above_pushdown_left_params().push_back(&parameter) == OB_SUCCESS);
          if (bad == 9) bound.binding_count = 2;
          LogPluginCustom transferred(plan);
          CHECK(transferred.configure_fragment(bound.v3, graph, target) == OB_SUCCESS);
          CHECK(transferred.configure_bindings(bound, graph) == (bad < 0 ? OB_SUCCESS :
              bad == 7 || bad == 8 ? OB_NOT_SUPPORTED : OB_INVALID_ARGUMENT));
          if (bad < 0) {
            CHECK(transferred.input_bindings().count() == 1 && transferred.input_bindings().at(0) == &parameter);
            CHECK(transferred.binding_sources().at(0) == 0);
          }
          CHECK(left.get_parent() == &target && right.get_parent() == &target);
          target.set_join_algo(NESTED_LOOP_JOIN); target.get_above_pushdown_left_params().reset();
        }
        target.get_nl_params().reset(); target.set_join_algo(INVALID_JOIN_ALGO);
        plans[0] = inputs[0] = outputs[0] = offsets[1] = UINT32_MAX;
        CHECK(custom.fragment() && custom.get_num_of_child() == 2 && custom.get_child(1) == &right);
        CHECK(custom.input_exprs().at(0) == a && custom.input_offsets().at(1) == 2);
        CHECK(custom.get_output_exprs().push_back(a) == OB_SUCCESS && custom.get_output_exprs().push_back(b) == OB_SUCCESS);
        CHECK(custom.compute_property() == OB_SUCCESS);
        CHECK(custom.get_card() == 7 && custom.get_width() == 24 && custom.get_cost() == 8);
        CHECK(!custom.get_is_at_most_one_row() && custom.get_output_const_exprs().empty());
        CHECK(custom.validate_layout() == OB_SUCCESS);
        right.get_output_exprs().reset();
        CHECK(custom.validate_layout() == OB_INVALID_ARGUMENT); // No fallback to the other input's frame.
        CHECK(right.get_output_exprs().push_back(b) == OB_SUCCESS);
        ObLogicalOperator::PPDeps deps; CHECK(custom.check_output_dependance(left.get_output_exprs(), deps) == OB_SUCCESS);
        ObLogicalOperator::PPDeps right_deps; CHECK(custom.check_output_dependance(right.get_output_exprs(), right_deps) == OB_SUCCESS);
      }
    }
    CandidateGraph graph; uint32_t offsets[] = {0};
    seekdb_plugin_custom_path_request_v3_t empty{};
    empty.input_offsets = offsets; empty.execution = SEEKDB_PLUGIN_CUSTOM_LOCAL_SERIAL;
    LogPluginCustom source(plan);
    CHECK(source.configure_fragment(empty, graph, target) == OB_SUCCESS);
    CHECK(source.get_num_of_child() == 0 && source.input_offsets().count() == 1);
    CHECK(source.compute_property() == OB_SUCCESS && source.validate_layout() == OB_SUCCESS);
    CHECK(source.get_card() == target.get_card()); // Zero inputs do not mean an empty result relation.
  }
  child.set_type(log_op_def::LOG_SORT);
  ObLogJoin join(plan); join.set_type(log_op_def::LOG_JOIN); join.set_join_type(INNER_JOIN);
  join.set_child(0, &child); join.set_child(1, &child);
  ObColumnRefRawExpr *column = nullptr; ObOpRawExpr *add = nullptr;
  CHECK(factory.create_raw_expr(T_REF_COLUMN, column) == OB_SUCCESS && column);
  CHECK(factory.create_raw_expr(T_OP_ADD, add) == OB_SUCCESS && add);
  column->set_ref_id(101, 7); column->set_data_type(ObInt32Type);
  column->set_collation_type(CS_TYPE_BINARY);
  PluginExprType type; type.logical_id_ = ObString::make_string("org.example.number");
  type.physical_type_ = ObInt32Type; type.catalog_epoch_ = 1;
  CHECK(column->set_plugin_type(type) == OB_SUCCESS);
  add->set_data_type(ObInt32Type);
  CHECK(add->set_param_exprs(column, column) == OB_SUCCESS);
  CHECK(join.get_join_conditions().push_back(add) == OB_SUCCESS);
  CHECK(join.get_join_filters().push_back(column) == OB_SUCCESS);
  CHECK(child.get_filter_exprs().push_back(add) == OB_SUCCESS);
  CHECK(child.get_startup_exprs().push_back(column) == OB_SUCCESS);
  CHECK(child.get_op_ordering().push_back(OrderItem(column, NULLS_FIRST_ASC)) == OB_SUCCESS);
  CandidateGraph graph;
  uint32_t root = UINT32_MAX, left = UINT32_MAX, right = UINT32_MAX, duplicate = UINT32_MAX;
  CHECK(graph.root(&join, &root) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(graph.child(root, 0, &left) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(graph.child(root, 1, &right) == SEEKDB_PLUGIN_STATUS_OK && left == right);
  CHECK(graph.root(&child, &duplicate) == SEEKDB_PLUGIN_STATUS_OK && duplicate == left);
  seekdb_plugin_plan_info_v1_t info{}; info.struct_size = sizeof(info);
  CHECK(graph.plan(root, &info) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(info.child_count == 2 && info.join_type == INNER_JOIN && info.expression_counts[3] == 1 && info.expression_counts[4] == 1);
  info.struct_size = sizeof(info);
  CHECK(graph.plan(left, &info) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(info.join_type == UINT32_MAX && info.expression_counts[0] == 1 && info.expression_counts[1] == 1 && info.expression_counts[2] == 1);
  uint32_t expr = UINT32_MAX, same = UINT32_MAX, order = 0;
  CHECK(graph.expression(root, SEEKDB_PLUGIN_PLAN_JOIN_CONDITION, 0, &expr, &order) == SEEKDB_PLUGIN_STATUS_OK && order == UINT32_MAX);
  CHECK(graph.expression(left, SEEKDB_PLUGIN_PLAN_FILTER, 0, &same, &order) == SEEKDB_PLUGIN_STATUS_OK && expr == same);
  seekdb_plugin_expr_info_v1_t metadata{}; metadata.struct_size = sizeof(metadata);
  CHECK(graph.describe(expr, &metadata) == SEEKDB_PLUGIN_STATUS_OK && metadata.expression_type == T_OP_ADD && metadata.argument_count == 2);
  uint32_t arg = UINT32_MAX, other = UINT32_MAX;
  CHECK(graph.argument(expr, 0, &arg) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(graph.argument(expr, 1, &other) == SEEKDB_PLUGIN_STATUS_OK && arg == other);
  CHECK(graph.expression(left, SEEKDB_PLUGIN_PLAN_STARTUP, 0, &same, &order) == SEEKDB_PLUGIN_STATUS_OK && same == arg && order == UINT32_MAX);
  CHECK(graph.expression(root, SEEKDB_PLUGIN_PLAN_JOIN_FILTER, 0, &same, &order) == SEEKDB_PLUGIN_STATUS_OK && same == arg && order == UINT32_MAX);
  CHECK(graph.expression(left, SEEKDB_PLUGIN_PLAN_ORDERING, 0, &same, &order) == SEEKDB_PLUGIN_STATUS_OK && same == arg && order == NULLS_FIRST_ASC);
  metadata.struct_size = sizeof(metadata);
  CHECK(graph.describe(arg, &metadata) == SEEKDB_PLUGIN_STATUS_OK);
  CHECK(metadata.sql_type == ObInt32Type && metadata.table_id == 101 && metadata.column_id == 7 && metadata.collation == CS_TYPE_BINARY);
  CHECK((metadata.flags & (SEEKDB_PLUGIN_EXPR_COLUMN | SEEKDB_PLUGIN_EXPR_PLUGIN_TYPE)) == (SEEKDB_PLUGIN_EXPR_COLUMN | SEEKDB_PLUGIN_EXPR_PLUGIN_TYPE));
  CHECK(std::strcmp(metadata.type_id, "org.example.number") == 0);
  CHECK(child.get_parent() == &join && child.get_output_exprs().empty() && graph.error() == OB_SUCCESS);
  {
    ObSelectStmt statement;
    seekdb_plugin_query_info_v1_t query{}; query.struct_size = sizeof(query);
    CHECK(graph.query(&statement, &query) == SEEKDB_PLUGIN_STATUS_OK);
    CHECK(query.flags == SEEKDB_PLUGIN_QUERY_SELECT_LIST && query.target_count == 0);
    // Full list, including a target absent from every node expression role.
    ObOpRawExpr *target_only = nullptr;
    CHECK(factory.create_raw_expr(T_OP_MUL, target_only) == OB_SUCCESS && target_only);
    CHECK(target_only->set_param_exprs(column, column) == OB_SUCCESS);
    for (auto *value : {static_cast<ObRawExpr *>(add), static_cast<ObRawExpr *>(target_only), static_cast<ObRawExpr *>(add)}) {
      SelectItem item; item.expr_ = value;
      CHECK(statement.get_select_items().push_back(item) == OB_SUCCESS);
    }
    query.struct_size = sizeof(query);
    CHECK(graph.query(&statement, &query) == SEEKDB_PLUGIN_STATUS_OK);
    CHECK(query.target_count == 3 && query.statement_type == oceanbase::sql::stmt::T_SELECT);
    uint32_t first = 0, second = 0, third = 0;
    CHECK(graph.target(&statement, 0, &first) == SEEKDB_PLUGIN_STATUS_OK && first == expr);
    CHECK(graph.target(&statement, 1, &second) == SEEKDB_PLUGIN_STATUS_OK && second != first);
    CHECK(graph.target(&statement, 2, &third) == SEEKDB_PLUGIN_STATUS_OK && third == first);
    CHECK(graph.argument(second, 0, &same) == SEEKDB_PLUGIN_STATUS_OK && same == arg);
    CHECK(graph.target(&statement, 1, &same) == SEEKDB_PLUGIN_STATUS_OK && same == second);
    statement.assign_set_op(ObSelectStmt::UNION);
    query.struct_size = sizeof(query);
    CHECK(graph.query(&statement, &query) == SEEKDB_PLUGIN_STATUS_OK && query.flags == 3 && query.target_count == 3);
    ObDMLStmt mutation(oceanbase::sql::stmt::T_UPDATE);
    query.struct_size = sizeof(query);
    CHECK(graph.query(&mutation, &query) == SEEKDB_PLUGIN_STATUS_OK && query.flags == 0 && query.target_count == 0);
    for (int fault = 0; fault < 7; ++fault) {
      CandidateGraph broken; query.struct_size = sizeof(query); uint32_t out = 0;
      seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
      if (fault == 0) status = broken.query(nullptr, &query);
      if (fault == 1) { query.struct_size = 0; status = broken.query(&statement, &query); }
      if (fault == 2) status = broken.query(&statement, nullptr);
      if (fault == 3) status = broken.target(&statement, 3, &out);
      if (fault == 4) status = broken.target(&statement, 0, nullptr);
      if (fault == 5) status = broken.target(&mutation, 0, &out);
      if (fault == 6) {
        SelectItem item; CHECK(statement.get_select_items().push_back(item) == OB_SUCCESS);
        status = broken.target(&statement, 3, &out);
      }
      CHECK(status == SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT && broken.error() == OB_INVALID_ARGUMENT);
      if (fault < 2) CHECK(query.struct_size == 0 && query.target_count == 0);
      if (fault == 3 || fault == 5 || fault == 6) CHECK(out == UINT32_MAX);
      out = 0;
      CHECK(broken.target(&statement, 0, &out) == SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT && out == UINT32_MAX);
    }
    CHECK(child.get_parent() == &join && child.get_output_exprs().empty());
  }
  {
    uint32_t inputs[] = {arg, arg}, outputs[] = {expr};
    seekdb_plugin_custom_path_request_v2_t request{};
    request.inputs = inputs; request.input_count = 2; request.outputs = outputs; request.output_count = 1;
    LogPluginCustom custom(plan); custom.set_child(0, &child);
    CHECK(custom.configure_layout(request, graph) == OB_SUCCESS);
    inputs[0] = outputs[0] = UINT32_MAX; // The node owns resolved expressions, not borrowed IDs.
    CHECK(custom.input_exprs().count() == 2 && custom.input_exprs().at(0) == column && custom.result_exprs().at(0) == add);
    CHECK(custom.get_output_exprs().push_back(add) == OB_SUCCESS);
    CHECK(custom.validate_layout() == OB_INVALID_ARGUMENT); // Missing child input, not a stale frame fallback.
    CHECK(child.get_output_exprs().push_back(column) == OB_SUCCESS);
    CHECK(custom.validate_layout() == OB_SUCCESS); // Computed output need not be an input expression.
    CHECK(custom.get_output_exprs().push_back(column) == OB_SUCCESS);
    CHECK(custom.validate_layout() == OB_INVALID_ARGUMENT); // Child-only value is not emitted by the plugin.
    custom.get_output_exprs().pop_back();
    ObSysFunRawExpr *nullary = nullptr;
    CHECK(factory.create_raw_expr(T_FUN_SYS_RAND, nullary) == OB_SUCCESS && nullary);
    CHECK(custom.get_output_exprs().push_back(nullary) == OB_SUCCESS);
    CHECK(custom.validate_layout() == OB_SUCCESS); // Local zero-argument computation needs no child value.
    ObConstRawExpr *literal = nullptr; ObExecParamRawExpr *parameter = nullptr;
    CHECK(factory.create_raw_expr(T_INT, literal) == OB_SUCCESS && literal);
    CHECK(factory.create_raw_expr(T_QUESTIONMARK, parameter) == OB_SUCCESS && parameter);
    CHECK(child.get_startup_exprs().push_back(literal) == OB_SUCCESS && child.get_startup_exprs().push_back(parameter) == OB_SUCCESS);
    child.get_output_exprs().reset(); child.set_parent(&join);
    ObColumnRefRawExpr *replacement = nullptr;
    CHECK(factory.create_raw_expr(T_REF_COLUMN, replacement) == OB_SUCCESS && replacement);
    replacement->set_ref_id(101, 8); replacement->set_data_type(ObInt32Type);
    inputs[0] = inputs[1] = outputs[0] = arg;
    LogPluginCustom rewritten(plan);
    CHECK(rewritten.configure_layout(request, graph) == OB_SUCCESS);
    CHECK(rewritten.get_output_exprs().push_back(column) == OB_SUCCESS);
    ObRawExprReplacer replacer; CHECK(replacer.add_replace_expr(column, replacement) == OB_SUCCESS);
    CHECK(rewritten.replace_op_exprs(replacer) == OB_SUCCESS);
    CHECK(rewritten.input_exprs().at(0) == replacement && rewritten.input_exprs().at(1) == replacement);
    CHECK(rewritten.result_exprs().at(0) == replacement && rewritten.get_output_exprs().at(0) == replacement);
    uint32_t distinct[] = {arg, expr}; request.output_count = 2; request.outputs = distinct;
    LogPluginCustom merged(plan); CHECK(merged.configure_layout(request, graph) == OB_SUCCESS);
    CHECK(replacer.add_replace_expr(add, replacement) == OB_SUCCESS);
    CHECK(merged.replace_op_exprs(replacer) == OB_INVALID_ARGUMENT);
    for (int fault = 0; fault < 8; ++fault) {
      CandidateGraph local; uint32_t node = 0, value = 0, ignored = 0;
      CHECK(local.root(&child, &node) == SEEKDB_PLUGIN_STATUS_OK);
      CHECK(local.expression(node, SEEKDB_PLUGIN_PLAN_STARTUP, 0, &value, &ignored) == SEEKDB_PLUGIN_STATUS_OK);
      uint32_t in[] = {value}, out[] = {value, value};
      request.inputs = in; request.input_count = 1; request.outputs = out; request.output_count = 1;
      request.reserved[0] = 0;
      if (fault == 0) in[0] = UINT32_MAX;
      if (fault == 1) out[0] = UINT32_MAX;
      if (fault == 2) request.output_count = 2;
      if (fault == 3) request.inputs = nullptr;
      if (fault == 4) request.input_count = 1025;
      if (fault == 5) request.reserved[0] = 1;
      if (fault >= 6) CHECK(local.expression(node, SEEKDB_PLUGIN_PLAN_STARTUP, fault - 5, &out[0], &ignored) == SEEKDB_PLUGIN_STATUS_OK);
      LogPluginCustom broken(plan);
      CHECK(broken.configure_layout(request, local) == OB_INVALID_ARGUMENT && !broken.explicit_layout());
    }
  }
  for (int fault = 0; fault < 8; ++fault) {
    CandidateGraph broken; uint32_t id = 0, output = 0, ordering = 0;
    CHECK(broken.root(&join, &id) == SEEKDB_PLUGIN_STATUS_OK);
    seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
    if (fault == 0) status = broken.root(nullptr, &output);
    if (fault == 1) status = broken.child(id, 2, &output);
    if (fault == 2) status = broken.expression(id, 0, 0, &output, &ordering);
    if (fault == 3) status = broken.expression(id, SEEKDB_PLUGIN_PLAN_FILTER, 0, &output, &ordering);
    if (fault == 4) { metadata.struct_size = sizeof(metadata); status = broken.describe(UINT32_MAX, &metadata); CHECK(metadata.struct_size == 0); }
    if (fault == 5) { info.struct_size = 0; status = broken.plan(id, &info); CHECK(info.struct_size == 0); }
    if (fault == 6) status = broken.argument(UINT32_MAX, 0, &output);
    if (fault == 7) status = broken.root(&child, nullptr);
    CHECK(status == SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT && broken.error() == OB_INVALID_ARGUMENT);
    output = 0;
    CHECK(broken.root(&join, &output) == SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT && output == UINT32_MAX);
  }
}
}
#endif
