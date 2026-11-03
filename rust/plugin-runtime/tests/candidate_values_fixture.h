// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_CANDIDATE_VALUES_FIXTURE_H_
#define SEEKDB_TEST_CANDIDATE_VALUES_FIXTURE_H_
#include "sql/optimizer/plugin_candidate_graph.h"
namespace candidate_values_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
inline void run(ObLogPlan &plan, ObRawExprFactory &factory) {
  if (!plan.get_stmt() || plan.get_stmt()->get_column_size() == 0) return;
  auto *column = plan.get_stmt()->get_column_items().at(0).expr_;
  CHECK(column);
  ObLogFunctionTable leaf(plan); leaf.set_type(log_op_def::LOG_FUNCTION_TABLE);
  leaf.set_table_id(column->get_table_id());
  ObLogGroupBy group(plan); group.set_type(log_op_def::LOG_GROUP_BY); group.set_child(0, &leaf);
  ObLogWindowFunction window(plan); window.set_type(log_op_def::LOG_WINDOW_FUNCTION); window.set_child(0, &group);
  ObLogDistinct distinct(plan); distinct.set_type(log_op_def::LOG_DISTINCT); distinct.set_child(0, &window);
  ObLogSort sort(plan); sort.set_type(log_op_def::LOG_SORT); sort.set_child(0, &distinct);
  ObAggFunRawExpr *aggregate = nullptr;
  CHECK(factory.create_raw_expr(T_FUN_MAX, aggregate) == OB_SUCCESS);
  aggregate->set_data_type(ObIntType);
  CHECK(aggregate->add_real_param_expr(column) == OB_SUCCESS);
  CHECK(group.get_aggr_funcs().push_back(aggregate) == OB_SUCCESS);
  ObWinFunRawExpr *win = nullptr;
  CHECK(factory.create_raw_expr(T_WINDOW_FUNCTION, win) == OB_SUCCESS);
  win->set_func_type(T_WIN_FUN_ROW_NUMBER); win->set_data_type(ObIntType);
  CHECK(window.add_window_expr(win) == OB_SUCCESS);
  CHECK(distinct.get_distinct_exprs().push_back(win) == OB_SUCCESS);
  ObOpRawExpr *scalar = nullptr;
  CHECK(factory.create_raw_expr(T_OP_ADD, scalar) == OB_SUCCESS);
  CHECK(scalar->set_param_exprs(aggregate, win) == OB_SUCCESS);
  scalar->set_data_type(ObIntType);
  // Startup is only an identity registration path in this non-executed graph.
  for (auto *expr : {static_cast<ObRawExpr *>(column), static_cast<ObRawExpr *>(aggregate),
                    static_cast<ObRawExpr *>(win), static_cast<ObRawExpr *>(scalar)})
    CHECK(sort.get_startup_exprs().push_back(expr) == OB_SUCCESS);
  CandidateGraph graph; uint32_t nodes[5]{}, expressions[4]{}, unused = 0;
  ObLogicalOperator *native[] = {&leaf, &group, &window, &distinct, &sort};
  for (int i = 0; i < 5; ++i) CHECK(graph.root(native[i], &nodes[i]) == SEEKDB_PLUGIN_STATUS_OK);
  for (int i = 0; i < 4; ++i)
    CHECK(graph.expression(nodes[4], SEEKDB_PLUGIN_PLAN_STARTUP, i, &expressions[i], &unused) == SEEKDB_PLUGIN_STATUS_OK);
  const bool expected[5][4] = {{true,false,false,false}, {false,true,false,false},
      {false,true,true,false}, {false,false,true,false}, {false,false,true,false}};
  for (int i = 0; i < 5; ++i) for (int j = 0; j < 4; ++j) {
    seekdb_plugin_value_info_v1_t info{}; info.struct_size = sizeof(info);
    CHECK(graph.value_info(nodes[i], expressions[j], &info) == SEEKDB_PLUGIN_STATUS_OK);
    CHECK(info.flags == ((expected[i][j] ? SEEKDB_PLUGIN_VALUE_AVAILABLE : 0) |
        (j == 3 ? SEEKDB_PLUGIN_VALUE_SCALAR_ARGUMENTS : 0)));
    CHECK(native[i]->get_output_exprs().empty()); // No hidden allocation.
  }
  // Explicit plugin outputs are a projection boundary, not all child values.
  LogPluginCustom custom(plan); custom.set_type(log_op_def::LOG_PLUGIN_CUSTOM); custom.set_child(0, &window);
  seekdb_plugin_custom_path_request_v2_t request{};
  request.inputs = &expressions[1]; request.input_count = 1;
  request.outputs = &expressions[1]; request.output_count = 1;
  CHECK(custom.configure_layout(request, graph) == OB_SUCCESS);
  uint32_t custom_id = 0; CHECK(graph.root(&custom, &custom_id) == SEEKDB_PLUGIN_STATUS_OK);
  for (int j : {1,2}) {
    seekdb_plugin_value_info_v1_t info{}; info.struct_size = sizeof(info);
    CHECK(graph.value_info(custom_id, expressions[j], &info) == SEEKDB_PLUGIN_STATUS_OK);
    CHECK(info.flags == (j == 1 ? SEEKDB_PLUGIN_VALUE_AVAILABLE : 0));
  }
  for (int fault = 0; fault < 5; ++fault) {
    CandidateGraph invalid; uint32_t p = 0, e = 0;
    CHECK(invalid.root(&sort, &p) == SEEKDB_PLUGIN_STATUS_OK);
    CHECK(invalid.expression(p, SEEKDB_PLUGIN_PLAN_STARTUP, 0, &e, &unused) == SEEKDB_PLUGIN_STATUS_OK);
    seekdb_plugin_value_info_v1_t info{}; info.struct_size = sizeof(info) - (fault == 2);
    if (fault == 4) sort.set_child(0, &sort);
    const auto status = invalid.value_info(fault == 0 ? UINT32_MAX : p, fault == 1 ? UINT32_MAX : e,
        fault == 3 ? nullptr : &info);
    CHECK(status != SEEKDB_PLUGIN_STATUS_OK && invalid.error() != OB_SUCCESS);
    if (fault != 3) { const seekdb_plugin_value_info_v1_t empty{}; CHECK(std::memcmp(&info, &empty, sizeof(info)) == 0); }
    sort.set_child(0, &distinct);
  }
  std::cerr << "plan values: aggregate/window retention, DISTINCT/custom boundaries, no mutation and sticky errors passed" << std::endl;
}
}
#endif
