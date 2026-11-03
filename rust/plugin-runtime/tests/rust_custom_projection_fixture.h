// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Independent SQL frames -> real loader/Rust projection -> independent output
// frames. Child rows are a fixture, not a planner-generated projection claim.
#ifndef SEEKDB_TEST_RUST_CUSTOM_PROJECTION_FIXTURE_H_
#define SEEKDB_TEST_RUST_CUSTOM_PROJECTION_FIXTURE_H_
#include "rust_custom_lob_fixture.h"
#include <limits>
namespace rust_custom_projection_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using rust_custom_lob_test::Row;
using rust_custom_lob_test::Cell;
inline std::string parameters(const std::vector<uint32_t> &mapping) {
  std::string result("SPJ1", 4);
  const auto append = [&](uint32_t value) { for (int b = 0; b < 4; ++b) result.push_back(static_cast<char>(value >> (b * 8))); };
  append(mapping.size()); for (auto index : mapping) append(index);
  return result;
}
inline std::string encoded(const std::string &bytes) {
  std::string result("RUT\1", 4);
  for (unsigned char b : bytes) result.push_back(static_cast<char>(~b));
  return result;
}
// Three differently typed input slots, independent output slots, and no LOB
// helpers: exercises numeric buffers sized by input rather than output count.
class NumericInput final : public ObOperator {
public:
  NumericInput(ObExecContext &ctx, const ObOpSpec &spec) : ObOperator(ctx, spec, nullptr) {}
  int position = 0, reads = 0, row_count = 3;
  static int64_t signed_value(int row) { return row == 0 ? INT64_MIN : INT64_MAX; }
  static uint64_t unsigned_value(int row) { return row == 0 ? UINT64_MAX : 0; }
  static double floating_value(int row) { return row == 0 ? -0.0 : std::numeric_limits<double>::infinity(); }
  int inner_get_next_row() override {
    ++reads;
    if (position == row_count) return OB_ITER_END;
    clear_evaluated_flag();
    for (int i = 0; i < spec_.output_.count(); ++i) {
      auto &expr = *spec_.output_.at(i); auto &datum = expr.locate_datum_for_write(eval_ctx_);
      if (position == 1) datum.set_null();
      else if (i == 0) datum.set_int(signed_value(position));
      else if (i == 1) datum.set_uint(unsigned_value(position));
      else datum.set_double(floating_value(position));
      expr.set_evaluated_projected(eval_ctx_);
    }
    ++position; return OB_SUCCESS;
  }
  int inner_rescan() override { position = 0; return ObOperator::inner_rescan(); }
  void destroy() override { ObOperator::destroy(); }
};
template <typename Provider> void numeric(Provider &provider, ObArenaAllocator &arena) {
  const std::vector<std::vector<uint32_t>> mappings{{2,0,1}, {2}, {0,1,0}, {}, {}};
  const ObObjType types[] = {ObIntType, ObUInt64Type, ObDoubleType};
  const char *ids[] = {"core.type.int64", "core.type.uint64", "core.type.float64"};
  for (size_t test = 0; test < mappings.size(); ++test) {
    const auto &mapping = mappings[test]; const int input_count = test == 4 ? 0 : 3;
    std::cerr << "custom numeric projection case=" << test << std::endl;
    auto session = std::make_unique<ObSQLSessionInfo>();
    CHECK(session->test_init(1, 1, &arena) == OB_SUCCESS);
    CHECK(session->load_default_sys_variable(false, false) == OB_SUCCESS); session->set_inner_session();
    ObPhysicalPlan physical; ObExecContext execution(arena); execution.set_my_session(session.get());
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS); execution.get_physical_plan_ctx()->set_phy_plan(&physical);
    ObSQLSessionInfo::ExecCtxSessionRegister registration(*session, &execution);
    ObRawExprFactory factory(arena); ObRawExprUniqueSet roots(false);
    // Keep one unused expression for the zero/zero case; no transport slot is
    // added for it. The same real frame setup is used in all five cases.
    for (size_t i = 0; i < std::max(size_t(1), input_count + mapping.size()); ++i) {
      ObColumnRefRawExpr *raw = nullptr; CHECK(factory.create_raw_expr(T_REF_COLUMN, raw) == OB_SUCCESS && raw);
      raw->set_ref_id(200, i + 1);
      raw->set_data_type(types[test == 4 ? 0 : i < size_t(input_count) ? i : mapping[i - input_count]]);
      CHECK(raw->formalize(session.get()) == OB_SUCCESS && roots.append(raw) == OB_SUCCESS);
    }
    ObStaticEngineExprCG generator(arena, session.get(), nullptr, 0, 0); ObExprFrameInfo frame(arena);
    CHECK(generator.generate(roots, frame) == OB_SUCCESS && execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    std::vector<ObExpr *> expressions;
    for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_REF_COLUMN) expressions.push_back(&expr);
    CHECK(expressions.size() == std::max(size_t(1), input_count + mapping.size()));
    ObOpSpec child_spec(arena, PHY_EXPR_VALUES); PluginCustomSpec spec(arena, PHY_PLUGIN_CUSTOM);
    child_spec.plan_ = &physical; spec.plan_ = &physical; spec.explicit_input_ = true;
    CHECK(child_spec.output_.init(input_count) == OB_SUCCESS && spec.input_columns_.init(input_count) == OB_SUCCESS);
    CHECK(spec.input_type_ids_.init(input_count) == OB_SUCCESS && spec.input_nullable_.init(input_count) == OB_SUCCESS);
    for (int i = 0; i < input_count; ++i) {
      CHECK(expressions[i]->datum_meta_.type_ == types[i]);
      CHECK(child_spec.output_.push_back(expressions[i]) == OB_SUCCESS && spec.input_columns_.push_back(expressions[i]) == OB_SUCCESS);
      CHECK(spec.input_type_ids_.push_back(ObString::make_string(ids[i])) == OB_SUCCESS && spec.input_nullable_.push_back(1) == OB_SUCCESS);
    }
    CHECK(spec.columns_.init(mapping.size()) == OB_SUCCESS && spec.output_.init(mapping.size()) == OB_SUCCESS);
    CHECK(spec.type_ids_.init(mapping.size()) == OB_SUCCESS && spec.nullable_.init(mapping.size()) == OB_SUCCESS);
    for (size_t i = 0; i < mapping.size(); ++i) {
      auto *expr = expressions[i + input_count]; CHECK(expr->datum_meta_.type_ == types[mapping[i]]);
      CHECK(spec.columns_.push_back(expr) == OB_SUCCESS && spec.output_.push_back(expr) == OB_SUCCESS);
      CHECK(spec.type_ids_.push_back(ObString::make_string(ids[mapping[i]])) == OB_SUCCESS && spec.nullable_.push_back(1) == OB_SUCCESS);
    }
    oceanbase::share::plugin::CustomExecutorBinding binding;
    CHECK(provider.bind_plugin_custom_executor("org.seekdb.rust-candidate.spool", 1, 0, binding) == OB_SUCCESS);
    CHECK(spec.set_binding(arena, binding, parameters(mapping)) == OB_SUCCESS);
    NumericInput child(execution, child_spec); PluginCustomOp op(execution, spec, nullptr); ObOperator *children[] = {&child};
    CHECK(op.set_children_pointer(children, 1) == OB_SUCCESS && child.init() == OB_SUCCESS && op.init() == OB_SUCCESS);
    provider.custom_schema_observer_ = [&](const seekdb_plugin_custom_context_v2_t &view) {
      CHECK(view.inputs != view.output && view.inputs[0].column_count == uint32_t(input_count));
      CHECK(view.output->column_count == mapping.size());
    };
    int observed = 0;
    provider.custom_input_observer_ = [&](const seekdb_plugin_custom_row_v1_t &row) {
      ++observed; CHECK(row.column_count == uint32_t(input_count));
      for (int i = 0; i < input_count; ++i) {
        const auto &cell = row.values[i]; CHECK(!std::strcmp(cell.type_id, ids[i]) && bool(cell.is_null) == (child.position == 2));
        if (cell.is_null) CHECK(!cell.data && !cell.data_size);
        else {
          CHECK(cell.data_size == 8); const int source = child.position - 1;
          if (i == 0) { int64_t value; std::memcpy(&value, cell.data, 8); CHECK(value == NumericInput::signed_value(source)); }
          else if (i == 1) { uint64_t value; std::memcpy(&value, cell.data, 8); CHECK(value == NumericInput::unsigned_value(source)); }
          else { double value; std::memcpy(&value, cell.data, 8); CHECK(value == NumericInput::floating_value(source) && std::signbit(value) == std::signbit(NumericInput::floating_value(source))); }
        }
      }
    };
    CHECK(op.open() == OB_SUCCESS);
    const auto verify = [&] {
      const int before = observed;
      for (int row = 0; row < 3; ++row) {
        CHECK(op.get_next_row() == OB_SUCCESS);
        for (size_t i = 0; i < mapping.size(); ++i) {
          ObDatum *datum = nullptr; CHECK(spec.columns_.at(i)->eval(op.get_eval_ctx(), datum) == OB_SUCCESS && datum && datum->is_null() == (row == 1));
          if (row != 1) {
            if (mapping[i] == 0) CHECK(datum->get_int() == NumericInput::signed_value(row));
            else if (mapping[i] == 1) CHECK(datum->get_uint() == NumericInput::unsigned_value(row));
            else CHECK(datum->get_double() == NumericInput::floating_value(row) && std::signbit(datum->get_double()) == std::signbit(NumericInput::floating_value(row)));
          }
        }
      }
      CHECK(op.get_next_row() == OB_ITER_END && observed == before + 3);
    };
    verify(); CHECK(op.rescan() == OB_SUCCESS); verify();
    if (!mapping.empty()) {
      // NULL narrowing must fail from metadata before fetching even an empty child.
      CHECK(op.inner_close() == OB_SUCCESS); spec.nullable_.at(0) = 0; child.row_count = 0;
      CHECK(op.inner_open() == OB_SUCCESS && op.rescan() == OB_SUCCESS); const int reads = child.reads;
      CHECK(op.get_next_row() == OB_INVALID_ARGUMENT && child.reads == reads);
      CHECK(op.get_next_row() == OB_STATE_NOT_MATCH && op.inner_close() == OB_SUCCESS);
      spec.nullable_.at(0) = 1; child.row_count = 3;
      CHECK(op.inner_open() == OB_SUCCESS && op.rescan() == OB_SUCCESS); verify();
    }
    provider.custom_input_observer_ = {}; provider.custom_schema_observer_ = {};
    CHECK(op.close() == OB_SUCCESS && provider.custom_opens_ == provider.custom_closes_);
    op.destroy(); child.destroy();
  }
  std::cerr << "custom projection: 5 numeric/zero-column layouts passed" << std::endl;
}
template <typename Provider> void run(Provider &provider, ObArenaAllocator &arena) {
  const std::vector<std::vector<uint32_t>> mappings{{1,0}, {0}, {0,1,0}, {}};
  for (int codec_mode = 0; codec_mode < 4; ++codec_mode) for (const auto &mapping : mappings) {
    const bool decode = codec_mode & 1, encode = codec_mode & 2;
    std::cerr << "custom projection codec=" << codec_mode << " mapping=";
    for (auto index : mapping) std::cerr << index << ',';
    std::cerr << std::endl;
    auto session = std::make_unique<ObSQLSessionInfo>();
    CHECK(session->test_init(1, 1, &arena) == OB_SUCCESS);
    CHECK(session->load_default_sys_variable(false, false) == OB_SUCCESS); session->set_inner_session();
    ObPhysicalPlan physical;
    ObExecContext execution(arena); execution.set_my_session(session.get());
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS); execution.get_physical_plan_ctx()->set_phy_plan(&physical);
    ObSQLSessionInfo::ExecCtxSessionRegister registration(*session, &execution);
    ObRawExprFactory factory(arena); ObRawExprUniqueSet roots(false);
    for (int i = 0; i < 5; ++i) {
      ObColumnRefRawExpr *raw = nullptr;
      CHECK(factory.create_raw_expr(T_REF_COLUMN, raw) == OB_SUCCESS && raw);
      raw->set_ref_id(100, i + 1); raw->set_data_type(ObLongTextType);
      raw->set_collation_type(CS_TYPE_BINARY); raw->set_collation_level(CS_LEVEL_IMPLICIT);
      ObAccuracy accuracy; accuracy.set_length(1000000); raw->set_accuracy(accuracy);
      CHECK(raw->formalize(session.get()) == OB_SUCCESS && roots.append(raw) == OB_SUCCESS);
    }
    ObStaticEngineExprCG generator(arena, session.get(), nullptr, 0, 0);
    ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS && frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    std::vector<ObExpr *> expressions;
    for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_REF_COLUMN) {
      expr.obj_meta_.set_has_lob_header(); expressions.push_back(&expr);
    }
    CHECK(expressions.size() == 5);
    ObOpSpec child_spec(arena, PHY_EXPR_VALUES); child_spec.plan_ = &physical;
    PluginCustomSpec spec(arena, PHY_PLUGIN_CUSTOM); spec.plan_ = &physical; spec.explicit_input_ = true;
    CHECK(child_spec.output_.init(2) == OB_SUCCESS && spec.input_columns_.init(2) == OB_SUCCESS);
    CHECK(spec.input_type_ids_.init(2) == OB_SUCCESS && spec.input_nullable_.init(2) == OB_SUCCESS);
    const char *ids[] = {codec_mode ? rust_stored_type_test::TYPE_ID : "core.type.bytes", "core.type.bytes"};
    PluginExprType type; type.stored_ = true; type.physical_type_ = ObLongTextType;
    type.sql_name_ = ObString::make_string("rust_stored_utf8"); type.logical_id_ = ObString::make_string(rust_stored_type_test::TYPE_ID);
    type.owner_ = ObString::make_string("org.seekdb.rust-text");
    type.format_ = ObString::make_string("org.seekdb.rust-text.stored-utf8.v1"); type.format_version_ = 1;
    for (int i = 0; i < 2; ++i) {
      CHECK(child_spec.output_.push_back(expressions[i]) == OB_SUCCESS && spec.input_columns_.push_back(expressions[i]) == OB_SUCCESS);
      CHECK(spec.input_type_ids_.push_back(ObString::make_string(ids[i])) == OB_SUCCESS && spec.input_nullable_.push_back(1) == OB_SUCCESS);
    }
    if (decode) CHECK(spec.bind_stored_input(arena, 0, type) == OB_SUCCESS);
    CHECK(spec.columns_.init(mapping.size()) == OB_SUCCESS && spec.output_.init(mapping.size()) == OB_SUCCESS);
    CHECK(spec.type_ids_.init(mapping.size()) == OB_SUCCESS && spec.nullable_.init(mapping.size()) == OB_SUCCESS);
    for (size_t i = 0; i < mapping.size(); ++i) {
      CHECK(spec.columns_.push_back(expressions[i + 2]) == OB_SUCCESS && spec.output_.push_back(expressions[i + 2]) == OB_SUCCESS);
      CHECK(spec.type_ids_.push_back(ObString::make_string(ids[mapping[i]])) == OB_SUCCESS && spec.nullable_.push_back(1) == OB_SUCCESS);
    }
    if (encode) for (size_t i = 0; i < mapping.size(); ++i)
      if (mapping[i] == 0) CHECK(spec.bind_stored_column(arena, i, type) == OB_SUCCESS);
    oceanbase::share::plugin::CustomExecutorBinding binding;
    CHECK(provider.bind_plugin_custom_executor("org.seekdb.rust-candidate.spool", 1, 0, binding) == OB_SUCCESS);
    const auto plan = parameters(mapping); CHECK(spec.set_binding(arena, binding, plan) == OB_SUCCESS);
    rust_custom_lob_test::Storage storage;
    execution.set_lob_read_service(&storage);
    rust_custom_lob_test::Input child(execution, child_spec, storage, 0);
    const std::vector<Row> rows{
      Row{Cell{std::string("a\0b",3)},Cell{"🙂"}}, Row{Cell{"",true},Cell{""}},
      Row{Cell{std::string(70000,'x')},Cell{"",true}}, Row{Cell{""},Cell{"tail"}}};
    const auto input_rows = [&] { auto result = rows; if (decode) for (auto &row : result) if (!row[0].null) row[0].bytes = encoded(row[0].bytes); return result; };
    child.rows = input_rows();
    PluginCustomOp op(execution, spec, nullptr); ObOperator *children[] = {&child};
    CHECK(op.set_children_pointer(children, 1) == OB_SUCCESS && child.init() == OB_SUCCESS && op.init() == OB_SUCCESS);
    const int opens = provider.custom_opens_;
    spec.explicit_input_ = false;
    CHECK(op.inner_open() == OB_INVALID_ARGUMENT && provider.custom_opens_ == opens); spec.explicit_input_ = true;
    const auto saved_id = spec.input_type_ids_.at(0); spec.input_type_ids_.at(0).reset();
    CHECK(op.inner_open() == OB_NOT_SUPPORTED && provider.custom_opens_ == opens); spec.input_type_ids_.at(0) = saved_id;
    int described = 0;
    provider.custom_schema_observer_ = [&](const seekdb_plugin_custom_context_v2_t &view) {
      ++described; CHECK(view.inputs && view.output && view.inputs != view.output);
      CHECK(view.inputs[0].column_count == 2 && view.output->column_count == mapping.size());
      CHECK(bool(view.inputs[0].columns[0].flags & SEEKDB_PLUGIN_CUSTOM_COLUMN_STORED) == decode);
      for (size_t i = 0; i < mapping.size(); ++i) {
        CHECK(std::strcmp(view.output->columns[i].type_id, ids[mapping[i]]) == 0);
        CHECK(bool(view.output->columns[i].flags & SEEKDB_PLUGIN_CUSTOM_COLUMN_STORED) == (encode && mapping[i] == 0));
      }
    };
    const int decodes = provider.decodes_, encodes = provider.encodes_;
    CHECK(op.open() == OB_SUCCESS);
    const auto verify = [&] {
      for (const auto &row : rows) {
        const int ret = op.get_next_row();
        if (ret != OB_SUCCESS) std::cerr << "custom projection get_next_row=" << ret << std::endl;
        CHECK(ret == OB_SUCCESS);
        for (size_t i = 0; i < mapping.size(); ++i) {
          const auto &expected = row[mapping[i]]; auto &expr = *spec.columns_.at(i); ObDatum *datum = nullptr;
          CHECK(expr.eval(op.get_eval_ctx(), datum) == OB_SUCCESS && datum && datum->is_null() == expected.null);
          if (!expected.null) {
            ObArenaAllocator temporary; ObString value;
            CHECK(ObTextStringHelper::read_real_string_data_with_copy(execution, temporary, *datum, expr.datum_meta_, true, value) == OB_SUCCESS);
            CHECK(std::string(value.ptr() ? value.ptr() : "", value.length()) == (encode && mapping[i] == 0 ? encoded(expected.bytes) : expected.bytes));
          }
        }
      }
      CHECK(op.get_next_row() == OB_ITER_END);
    };
    verify(); CHECK(op.rescan() == OB_SUCCESS); verify();
    const int copies = std::count(mapping.begin(), mapping.end(), 0u);
    CHECK(provider.decodes_ == decodes + (decode ? 6 : 0));
    CHECK(provider.encodes_ == encodes + (encode ? 6 * copies : 0));
    if (!mapping.empty()) {
      CHECK(op.inner_close() == OB_SUCCESS);
      auto bad = mapping; bad[0] = 2;
      CHECK(spec.set_binding(arena, binding, parameters(bad)) == OB_SUCCESS);
      child.rows.clear(); CHECK(op.inner_open() == OB_SUCCESS && op.rescan() == OB_SUCCESS);
      const int reads = child.reads;
      CHECK(op.get_next_row() == OB_INVALID_ARGUMENT && child.reads == reads);
      CHECK(op.get_next_row() == OB_STATE_NOT_MATCH);
      CHECK(op.inner_close() == OB_SUCCESS && spec.set_binding(arena, binding, plan) == OB_SUCCESS);
      CHECK(op.inner_open() == OB_SUCCESS && op.rescan() == OB_SUCCESS && op.get_next_row() == OB_ITER_END);
      child.rows = input_rows(); CHECK(op.rescan() == OB_SUCCESS); verify();
    }
    CHECK(described > 0); provider.custom_schema_observer_ = {};
    CHECK(storage.reads == 0 && storage.releases == 0); // Temporary LOBs perform no backing reads.
    CHECK(op.close() == OB_SUCCESS && provider.custom_opens_ == provider.custom_closes_);
    oceanbase::share::plugin::ObPluginStatusSnapshot snapshot;
    CHECK(provider.candidate_loader_->get_status("org.seekdb.rust-candidate", snapshot) == OB_SUCCESS && !snapshot.lease_count_);
  }
  std::cerr << "custom projection: 16 independent input/output LOB/codec mappings passed" << std::endl;
  numeric(provider, arena);
}
}
#endif
