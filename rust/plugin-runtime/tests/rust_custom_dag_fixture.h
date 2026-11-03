// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real Rust scheduling/loader/physical parameters, controlled child rows/spec.
#ifndef SEEKDB_TEST_RUST_CUSTOM_DAG_FIXTURE_H_
#define SEEKDB_TEST_RUST_CUSTOM_DAG_FIXTURE_H_
#include "rust_custom_multi_fixture.h"
namespace rust_custom_dag_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using rust_custom_multi_test::Input;
template <typename Provider> void run(Provider &provider, ObArenaAllocator &arena) {
  for (int test = 0; test < 6; ++test) {
    std::cerr << "custom parameter DAG case=" << test << std::endl;
    const bool fan_in = test == 1;
    const uint32_t a = 2, b = 0, c = 1; // Schedule differs from physical input order.
    const uint32_t sources[] = {a * 2 + 1, a * 2, b * 2 + 1};
    auto session = std::make_unique<ObSQLSessionInfo>();
    CHECK(session->test_init(1, 1, &arena) == OB_SUCCESS && session->load_default_sys_variable(false, false) == OB_SUCCESS);
    session->set_inner_session();
    ObPhysicalPlan physical; ObExecContext execution(arena); execution.set_my_session(session.get());
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS); execution.get_physical_plan_ctx()->set_phy_plan(&physical);
    CHECK(execution.get_physical_plan_ctx()->reserve_param_space(4) == OB_SUCCESS);
    auto &params = execution.get_physical_plan_ctx()->get_param_store_for_update();
    params.at(3).set_int(987654);
    ObSQLSessionInfo::ExecCtxSessionRegister registration(*session, &execution);
    ObRawExprFactory factory(arena); ObRawExprUniqueSet roots(false);
    std::vector<ObRawExpr *> raw_columns;
    for (int i = 0; i < 9; ++i) {
      ObColumnRefRawExpr *raw = nullptr; CHECK(factory.create_raw_expr(T_REF_COLUMN, raw) == OB_SUCCESS);
      raw->set_ref_id(950, i + 1); raw->set_data_type(i < 6 && i % 2 == 0 ? ObIntType : ObVarcharType);
      CHECK(raw->formalize(session.get()) == OB_SUCCESS && roots.append(raw) == OB_SUCCESS);
      raw_columns.push_back(raw);
    }
    for (int i = 0; i < 3; ++i) {
      ObExecParamRawExpr *param = nullptr; CHECK(factory.create_raw_expr(T_QUESTIONMARK, param) == OB_SUCCESS);
      param->set_ref_expr(raw_columns[sources[i]]); param->set_param_index(i);
      param->set_result_type(raw_columns[sources[i]]->get_result_type());
      CHECK(param->formalize(session.get()) == OB_SUCCESS && roots.append(param) == OB_SUCCESS);
    }
    ObStaticEngineExprCG generator(arena, session.get(), nullptr, 0, 0); ObExprFrameInfo frame(arena);
    CHECK(generator.generate(roots, frame) == OB_SUCCESS && execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    std::vector<ObExpr *> exprs, parameters;
    for (auto &expr : frame.rt_exprs_) {
      if (expr.type_ == T_REF_COLUMN) exprs.push_back(&expr);
      if (expr.type_ == T_QUESTIONMARK) parameters.push_back(&expr);
    }
    CHECK(exprs.size() == 9 && parameters.size() == 3);
    PluginCustomSpec spec(arena, PHY_PLUGIN_CUSTOM); spec.plan_ = &physical; spec.explicit_input_ = true;
    CHECK(spec.input_columns_.init(6) == OB_SUCCESS && spec.input_type_ids_.init(6) == OB_SUCCESS && spec.input_nullable_.init(6) == OB_SUCCESS);
    CHECK(spec.input_offsets_.init(4) == OB_SUCCESS && spec.input_offsets_.push_back(0) == OB_SUCCESS);
    std::vector<std::unique_ptr<ObOpSpec>> child_specs;
    for (uint32_t i = 0; i < 3; ++i) {
      auto child = std::make_unique<ObOpSpec>(arena, PHY_EXPR_VALUES); child->plan_ = &physical;
      CHECK(child->output_.init(2) == OB_SUCCESS);
      for (uint32_t j = 0; j < 2; ++j) {
        CHECK(child->output_.push_back(exprs[i * 2 + j]) == OB_SUCCESS && spec.input_columns_.push_back(exprs[i * 2 + j]) == OB_SUCCESS);
        CHECK(spec.input_type_ids_.push_back(ObString::make_string(j ? "core.type.bytes" : "core.type.int64")) == OB_SUCCESS);
        CHECK(spec.input_nullable_.push_back(1) == OB_SUCCESS);
      }
      CHECK(spec.input_offsets_.push_back((i + 1) * 2) == OB_SUCCESS); child_specs.push_back(std::move(child));
    }
    CHECK(spec.input_bindings_.init(3) == OB_SUCCESS && spec.binding_sources_.init(3) == OB_SUCCESS);
    CHECK(spec.binding_inputs_.init(3) == OB_SUCCESS && spec.binding_targets_.init(3) == OB_SUCCESS);
    for (int i = 0; i < 3; ++i) {
      ObDynamicParamSetter setter; setter.param_idx_ = i; setter.src_ = exprs[sources[i]]; setter.dst_ = parameters[i];
      CHECK(spec.input_bindings_.push_back(setter) == OB_SUCCESS && spec.binding_sources_.push_back(sources[i]) == OB_SUCCESS);
      CHECK(spec.binding_inputs_.push_back(i == 2 ? b : a) == OB_SUCCESS);
      CHECK(spec.binding_targets_.push_back(i == 0 && !fan_in ? b : c) == OB_SUCCESS);
    }
    if (test == 4) spec.binding_targets_.at(2) = a; // Cycle rejected before calling children.
    if (test == 5) spec.binding_inputs_.at(0) = b; // Flattened source is outside claimed input.
    CHECK(spec.columns_.init(3) == OB_SUCCESS && spec.output_.init(3) == OB_SUCCESS);
    CHECK(spec.type_ids_.init(3) == OB_SUCCESS && spec.nullable_.init(3) == OB_SUCCESS);
    for (int i = 0; i < 3; ++i) {
      CHECK(spec.columns_.push_back(exprs[6 + i]) == OB_SUCCESS && spec.output_.push_back(exprs[6 + i]) == OB_SUCCESS);
      CHECK(spec.type_ids_.push_back(ObString::make_string("core.type.bytes")) == OB_SUCCESS && spec.nullable_.push_back(1) == OB_SUCCESS);
    }
    std::string plan("SJD1");
    const auto word = [&](uint32_t value) { for (int i = 0; i < 4; ++i) plan.push_back(static_cast<char>(value >> (i * 8))); };
    word(3); word(a); word(0); word(b); word(fan_in ? 0 : 1); word(c); word(1);
    word(3); for (auto input : {a, b, c}) { word(input); word(1); }
    oceanbase::share::plugin::CustomExecutorBinding binding;
    CHECK(provider.bind_plugin_custom_executor("org.seekdb.rust-candidate.spool", 1, 0, binding) == OB_SUCCESS);
    CHECK(spec.set_binding(arena, binding, plan) == OB_SUCCESS);
    const int counts[] = {test == 2 ? 0 : 4, 2, 4};
    std::vector<std::unique_ptr<Input>> children; std::vector<ObOperator *> pointers;
    for (uint32_t i = 0; i < 3; ++i) {
      auto input = std::make_unique<Input>(execution, *child_specs[i], i, counts[i], false);
      CHECK(input->init() == OB_SUCCESS); pointers.push_back(input.get()); children.push_back(std::move(input));
    }
    PluginCustomOp op(execution, spec, nullptr);
    CHECK(op.set_children_pointer(pointers.data(), 3) == OB_SUCCESS && op.init() == OB_SUCCESS);
    const auto cleared = [&] {
      for (int i = 0; i < 3; ++i) CHECK(params.at(i).is_null());
      CHECK(params.at(3).get_int() == 987654);
    };
    const auto inspect = [&](uint32_t input) {
      for (int i = 0; i < 3; ++i) if (spec.binding_targets_.at(i) == input) {
        const auto source = spec.binding_inputs_.at(i);
        const int row = children[source]->position - 1;
        CHECK(row >= 0 && row < counts[source]);
        ObDatum *datum = nullptr; CHECK(parameters[i]->eval(op.get_eval_ctx(), datum) == OB_SUCCESS && datum);
        CHECK(datum->is_null() == (row == 1) && params.at(i).is_null() == (row == 1));
        if (row == 1) continue;
        if (i == 1) CHECK(datum->get_int() == Input::value(source, row, 0) && params.at(i).get_int() == datum->get_int());
        else {
          const auto value = datum->get_string(); const auto stored = params.at(i).get_string();
          CHECK(std::string(value.ptr() ? value.ptr() : "", value.length()) == Input::payload(source, row, 1));
          CHECK(value == stored);
        }
      }
      CHECK(params.at(3).get_int() == 987654);
    };
    children[b]->inspect_parameters = [&] { inspect(b); };
    children[c]->inspect_parameters = [&] { inspect(c); };
    const int opened = op.inner_open();
    if (test >= 4) {
      CHECK(opened == OB_INVALID_ARGUMENT);
      for (const auto &child : children) CHECK(child->reads == 0);
      op.destroy(); for (auto &child : children) child->destroy(); continue;
    }
    CHECK(opened == OB_SUCCESS);
    if (test == 3) {
      children[c]->rescan_error = OB_TIMEOUT;
      CHECK(op.get_next_row() == OB_TIMEOUT); cleared();
      CHECK(op.get_next_row() == OB_STATE_NOT_MATCH);
      children[c]->rescan_error = OB_SUCCESS;
      CHECK(op.rescan() == OB_SUCCESS); cleared();
    }
    for (int pass = 0; pass < 2; ++pass) {
      int rows = 0, ret = OB_SUCCESS;
      while ((ret = op.get_next_row()) == OB_SUCCESS) {
        CHECK(counts[b] > 0 && counts[c] > 0);
        const int logical[] = {rows / (counts[b] * counts[c]), (rows / counts[c]) % counts[b], rows % counts[c]};
        int slot = 0;
        for (const auto input : {a, b, c}) {
          const auto &datum = spec.columns_.at(slot)->locate_expr_datum(op.get_eval_ctx());
          CHECK(datum.is_null() == (logical[slot] == 1));
          if (!datum.is_null()) {
            const auto value = datum.get_string();
            CHECK(std::string(value.ptr() ? value.ptr() : "", value.length()) == Input::payload(input, logical[slot], 1));
          }
          ++slot;
        }
        ++rows;
      }
      CHECK(ret == OB_ITER_END && rows == counts[a] * counts[b] * counts[c]); cleared();
      CHECK(op.rescan() == OB_SUCCESS); cleared();
    }
    if (counts[b]) { CHECK(op.get_next_row() == OB_SUCCESS); }
    CHECK(op.inner_close() == OB_SUCCESS); cleared();
    op.destroy(); for (auto &child : children) child->destroy();
  }
  std::cerr << "Rust parameter DAG: reordered chain/fan-in, NULL/empty/70KB, zero input rows, failure/reset/close and admission passed" << std::endl;
}
}
#endif
