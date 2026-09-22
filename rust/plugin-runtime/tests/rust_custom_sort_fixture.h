// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real frames/loader/Rust sorting; explicit physical inputs, not SQL planning.
#ifndef SEEKDB_TEST_RUST_CUSTOM_SORT_FIXTURE_H_
#define SEEKDB_TEST_RUST_CUSTOM_SORT_FIXTURE_H_
#include "rust_custom_multi_fixture.h"
namespace rust_custom_sort_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
template <typename Provider> void run(Provider &provider, ObArenaAllocator &arena) {
  for (int test = 0; test < 9; ++test) {
    const bool zero_output = test == 6 || test == 8;
    const int rows = test == 4 ? 0 : test == 6 ? 65537 : test == 7 ? 65536 : test == 8 ? 4096 : 5;
    const uint32_t flags = test < 4 ? test : 0;
    const std::vector<ObObjType> input_types = zero_output ? std::vector<ObObjType>{ObIntType} :
        std::vector<ObObjType>{test == 5 ? ObDoubleType : ObIntType, ObIntType, ObVarcharType};
    const std::vector<uint32_t> mapping = zero_output ? std::vector<uint32_t>{} : std::vector<uint32_t>{2,1,0};
    auto session = std::make_unique<ObSQLSessionInfo>();
    CHECK(session->test_init(1, 1, &arena) == OB_SUCCESS && session->load_default_sys_variable(false, false) == OB_SUCCESS);
    session->set_inner_session();
    ObPhysicalPlan physical; ObExecContext execution(arena); execution.set_my_session(session.get());
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS); execution.get_physical_plan_ctx()->set_phy_plan(&physical);
    ObSQLSessionInfo::ExecCtxSessionRegister registration(*session, &execution);
    ObRawExprFactory factory(arena); ObRawExprUniqueSet roots(false);
    std::vector<ObObjType> types = input_types;
    for (auto slot : mapping) types.push_back(input_types[slot]);
    for (size_t i = 0; i < types.size(); ++i) {
      ObColumnRefRawExpr *raw = nullptr; CHECK(factory.create_raw_expr(T_REF_COLUMN, raw) == OB_SUCCESS);
      raw->set_ref_id(1200, i + 1); raw->set_data_type(types[i]);
      CHECK(raw->formalize(session.get()) == OB_SUCCESS && roots.append(raw) == OB_SUCCESS);
    }
    ObStaticEngineExprCG generator(arena, session.get(), nullptr, 0, 0); ObExprFrameInfo frame(arena);
    CHECK(generator.generate(roots, frame) == OB_SUCCESS && execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    std::vector<ObExpr *> expressions;
    for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_REF_COLUMN) expressions.push_back(&expr);
    CHECK(expressions.size() == types.size());
    PluginCustomSpec spec(arena, PHY_PLUGIN_CUSTOM); spec.plan_ = &physical; spec.explicit_input_ = true;
    ObOpSpec input_spec(arena, PHY_EXPR_VALUES); input_spec.plan_ = &physical;
    const uint32_t inputs = input_types.size(), outputs = mapping.size();
    CHECK(input_spec.output_.init(inputs) == OB_SUCCESS);
    CHECK(spec.input_columns_.init(inputs) == OB_SUCCESS && spec.input_type_ids_.init(inputs) == OB_SUCCESS && spec.input_nullable_.init(inputs) == OB_SUCCESS);
    CHECK(spec.input_offsets_.init(2) == OB_SUCCESS && spec.input_offsets_.push_back(0) == OB_SUCCESS && spec.input_offsets_.push_back(inputs) == OB_SUCCESS);
    const auto type_id = [](ObObjType type) { return ObString::make_string(type == ObIntType ? "core.type.int64" : type == ObDoubleType ? "core.type.float64" : "core.type.bytes"); };
    for (uint32_t i = 0; i < inputs; ++i) {
      CHECK(input_spec.output_.push_back(expressions[i]) == OB_SUCCESS && spec.input_columns_.push_back(expressions[i]) == OB_SUCCESS);
      CHECK(spec.input_type_ids_.push_back(type_id(types[i])) == OB_SUCCESS && spec.input_nullable_.push_back(1) == OB_SUCCESS);
    }
    CHECK(spec.columns_.init(outputs) == OB_SUCCESS && spec.type_ids_.init(outputs) == OB_SUCCESS && spec.nullable_.init(outputs) == OB_SUCCESS);
    CHECK(spec.output_.init(outputs) == OB_SUCCESS);
    for (uint32_t i = 0; i < outputs; ++i) {
      CHECK(spec.columns_.push_back(expressions[inputs+i]) == OB_SUCCESS && spec.output_.push_back(expressions[inputs+i]) == OB_SUCCESS);
      CHECK(spec.type_ids_.push_back(type_id(types[inputs+i])) == OB_SUCCESS && spec.nullable_.push_back(1) == OB_SUCCESS);
    }
    ObOpSpec *spec_children[] = {&input_spec};
    CHECK(spec.set_children_pointer(spec_children, 1) == OB_SUCCESS);
    std::string plan("SSO1", 4);
    const auto word = [&](uint32_t value) { for (int i = 0; i < 4; ++i) plan.push_back(static_cast<char>(value >> (i*8))); };
    word(zero_output ? 1 : 2); word(outputs); word(0); word(flags);
    if (!zero_output) { word(1); word(1); } // Independent secondary descending key.
    for (auto slot : mapping) word(slot);
    oceanbase::share::plugin::CustomExecutorBinding binding;
    CHECK(provider.bind_plugin_custom_executor("org.seekdb.rust-candidate.spool", 1, 0, binding) == OB_SUCCESS);
    CHECK(spec.set_binding(arena, binding, plan) == OB_SUCCESS);
    rust_custom_multi_test::Input child(execution, input_spec, 0, rows, false); child.repeated_keys = true;
    CHECK(child.init() == OB_SUCCESS);
    PluginCustomOp op(execution, spec, nullptr); ObOperator *children[] = {&child};
    CHECK(op.set_children_pointer(children, 1) == OB_SUCCESS && op.init() == OB_SUCCESS && op.open() == OB_SUCCESS);
    if (test == 5 || test == 6 || test == 7) {
      CHECK(op.get_next_row() == (test == 5 ? OB_INVALID_ARGUMENT : OB_ALLOCATE_MEMORY_FAILED));
      CHECK(test == 5 ? child.reads == 0 : test == 6 ? child.reads == 65537 : child.reads < rows);
      CHECK(op.close() == OB_SUCCESS); op.destroy(); child.destroy();
      std::cerr << "Rust sort physical rejection case=" << test << " reads=" << child.reads << std::endl;
      continue;
    }
    if (test == 8) {
      class CancelAfterInput final : public ObIExtraStatusCheck {
      public:
        CancelAfterInput(const int &reads, int end) : reads_(reads), end_(end) {}
        const char *name() const override { return "rust-sort-cancel-after-input"; }
        int check() const override { return reads_ >= end_ && ++polls_ == 32 ? OB_TIMEOUT : OB_SUCCESS; }
        const int &reads_; int end_; mutable int polls_ = 0;
      } cancel(child.reads, rows+1);
      { ObIExtraStatusCheck::Guard guard(execution, cancel); CHECK(op.get_next_row() == OB_TIMEOUT); }
      CHECK(child.position == rows && cancel.polls_ == 32 && op.rescan() == OB_SUCCESS);
    }
    const std::vector<int> order = test == 4 ? std::vector<int>{} : flags == 0 ? std::vector<int>{4,2,0,3,1} :
        flags == 1 ? std::vector<int>{3,4,2,0,1} : flags == 2 ? std::vector<int>{1,4,2,0,3} : std::vector<int>{1,3,4,2,0};
    for (int pass = 0; pass < 2; ++pass) {
      for (int i = 0; i < rows; ++i) {
        CHECK(op.get_next_row() == OB_SUCCESS);
        if (zero_output) continue;
        const int row = order.at(i);
        for (uint32_t output = 0; output < outputs; ++output) {
          ObDatum *datum = nullptr;
          CHECK(spec.output_.at(output)->eval(op.get_eval_ctx(), datum) == OB_SUCCESS && datum && datum->is_null() == (row == 1));
          if (row == 1) continue;
          if (output == 0) CHECK(std::string(datum->get_string().ptr(), datum->get_string().length()) == rust_custom_multi_test::Input::payload(0, row, 2));
          else CHECK(datum->get_int() == (output == 1 ? rust_custom_multi_test::Input::value(0, row, 1) : row % 2));
        }
      }
      CHECK(op.get_next_row() == OB_ITER_END);
      if (!pass) CHECK(op.rescan() == OB_SUCCESS);
    }
    CHECK(op.close() == OB_SUCCESS); op.destroy(); child.destroy();
    std::cerr << "Rust sort physical case=" << test << " null/empty/70KB ownership, rescan or comparison cancellation passed" << std::endl;
  }
}
}
#endif
