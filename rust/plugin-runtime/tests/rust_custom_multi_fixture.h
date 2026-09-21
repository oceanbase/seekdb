// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Actual SQL frames/operator/loader/Rust algorithm. Children and physical spec
// are explicit fixtures, not evidence of planner-generated joins or real scans.
#ifndef SEEKDB_TEST_RUST_CUSTOM_MULTI_FIXTURE_H_
#define SEEKDB_TEST_RUST_CUSTOM_MULTI_FIXTURE_H_
#include "rust_custom_projection_fixture.h"
namespace rust_custom_multi_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
inline std::string parameters(const std::vector<std::vector<uint32_t>> &mappings) {
  std::string result("SMP1", 4);
  const auto append = [&](uint32_t value) { for (int b = 0; b < 4; ++b) result.push_back(static_cast<char>(value >> (b * 8))); };
  append(mappings.size());
  for (const auto &mapping : mappings) { append(mapping.size()); for (auto index : mapping) append(index); }
  return result;
}
class Input final : public ObOperator {
public:
  Input(ObExecContext &ctx, const ObOpSpec &spec, uint32_t number, int rows, bool encoded)
      : ObOperator(ctx, spec, nullptr), number(number), rows(rows), encoded(encoded), bytes(spec.output_.count()) {}
  uint32_t number;
  int rows, position = 0, reads = 0, rescans = 0;
  int rescan_error = OB_SUCCESS, fail_at = -1;
  bool encoded;
  bool repeated_keys = false;
  std::function<void()> inspect_parameters;
  std::vector<std::string> bytes;
  static int64_t value(int input, int row, int column) { return input * 100000 + row * 1000 + column; }
  static std::string payload(int input, int row, int column) {
    if (row == 2) return "";
    if (row == 3) return std::string(70000, 'a' + input);
    return std::to_string(value(input, row, column)) + std::string("\0🙂", 5);
  }
  int inner_get_next_row() override {
    ++reads;
    if (position == fail_at) return OB_TIMEOUT;
    if (inspect_parameters) inspect_parameters();
    if (position == rows) return OB_ITER_END;
    clear_evaluated_flag();
    for (int64_t i = 0; i < spec_.output_.count(); ++i) {
      auto &expr = *spec_.output_.at(i); auto &datum = expr.locate_datum_for_write(eval_ctx_);
      if (position == 1) datum.set_null();
      else if (expr.datum_meta_.type_ == ObIntType) datum.set_int(repeated_keys && i == 0 ? position % 2 : value(number, position, i));
      else if (expr.datum_meta_.type_ == ObDoubleType) datum.set_double(value(number, position, i) + 0.25);
      else {
        bytes[i] = payload(number, position, i);
        if (encoded) bytes[i] = rust_custom_projection_test::encoded(bytes[i]);
        CHECK(ObTextStringHelper::string_to_templob_result(expr, eval_ctx_, datum,
            ObString(bytes[i].size(), bytes[i].data())) == OB_SUCCESS);
      }
      expr.set_evaluated_projected(eval_ctx_);
    }
    ++position; return OB_SUCCESS;
  }
  int inner_rescan() override {
    ++rescans;
    if (rescan_error) return rescan_error;
    position = 0; return ObOperator::inner_rescan();
  }
  void destroy() override { ObOperator::destroy(); }
};
template <typename Provider> void run(Provider &provider, ObArenaAllocator &arena) {
  for (int test = 0; test < 18; ++test) {
    std::cerr << "custom multi input case=" << test << std::endl;
    const bool lob = test == 7 || test == 8, encode = test == 8;
    std::vector<std::vector<ObObjType>> types{{ObIntType}, {ObDoubleType, ObIntType, ObVarcharType}, {ObVarcharType, ObIntType}};
    std::vector<std::vector<uint32_t>> mapping{{0}, {1}, {1}};
    std::vector<int> row_counts{2,4,1};
    if (test == 1) mapping = {{0,0}, {1,1}, {1,1}};
    if (test == 2) mapping = {{},{},{}};
    if (test == 3) { types = {{},{},{}}; mapping = {{},{},{}}; row_counts = {2,0,3}; }
    if (test == 4) { types.clear(); mapping.clear(); row_counts.clear(); }
    if (test == 5 || test == 9) {
      const int count = test == 5 ? 64 : 65;
      types.assign(count, {ObIntType}); mapping.assign(count, {0}); row_counts.resize(count);
      for (int i = 0; i < count; ++i) row_counts[i] = i % 3;
    }
    if (test == 6) { types = {std::vector<ObObjType>(1024, ObIntType), {ObIntType}}; mapping = {{1023}, {0}}; row_counts = {1,2}; }
    if (lob) { types = {{ObLongTextType, ObLongTextType}, {ObLongTextType}, {ObLongTextType, ObLongTextType}}; mapping = {{1},{0},{1}}; }
    const bool join = test == 10 || test == 11;
    const bool streaming = test >= 12;
    const bool bound = test >= 16, bound_lob = test == 17;
    if (join) { types = {{ObIntType}, {ObIntType}}; mapping = {{}, {}}; row_counts = test == 10 ? std::vector<int>{0, 3} : std::vector<int>{3, 0}; }
    if (streaming) {
      types = {{ObIntType, ObVarcharType}, {ObIntType, ObVarcharType}};
      mapping = {{1}, {1}}; row_counts = {5,4};
      if (test == 14) row_counts[1] = 0;
      if (test == 15) row_counts[0] = 0;
      if (bound_lob) types = {{ObIntType, ObLongTextType}, {ObIntType, ObLongTextType}};
    }
    const uint32_t outputs = streaming ? (test == 13 ? 0 : 2) : mapping.empty() ? 0 : mapping[0].size();
    uint32_t total = 0; for (const auto &input : types) total += input.size();
    auto session = std::make_unique<ObSQLSessionInfo>();
    CHECK(session->test_init(1, 1, &arena) == OB_SUCCESS && session->load_default_sys_variable(false, false) == OB_SUCCESS);
    session->set_inner_session();
    ObPhysicalPlan physical; ObExecContext execution(arena); execution.set_my_session(session.get());
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS); execution.get_physical_plan_ctx()->set_phy_plan(&physical);
    ObSQLSessionInfo::ExecCtxSessionRegister registration(*session, &execution);
    plugin_projection_test::InrowOnlyLobService lob_service; execution.set_lob_read_service(&lob_service);
    ObRawExprFactory factory(arena); ObRawExprUniqueSet roots(false);
    std::vector<ObObjType> flat;
    for (const auto &input : types) flat.insert(flat.end(), input.begin(), input.end());
    flat.insert(flat.end(), outputs, bound_lob ? ObLongTextType : streaming ? ObVarcharType : lob ? ObLongTextType : ObIntType);
    if (flat.empty()) flat.push_back(ObIntType); // Unused slot for zero/zero frames.
    std::vector<ObRawExpr *> raw_columns;
    for (size_t i = 0; i < flat.size(); ++i) {
      ObColumnRefRawExpr *raw = nullptr; CHECK(factory.create_raw_expr(T_REF_COLUMN, raw) == OB_SUCCESS && raw);
      raw->set_ref_id(700, i + 1); raw->set_data_type(flat[i]);
      CHECK(raw->formalize(session.get()) == OB_SUCCESS && roots.append(raw) == OB_SUCCESS);
      raw_columns.push_back(raw);
    }
    if (bound) {
      for (int i = 0; i < 2; ++i) {
        ObExecParamRawExpr *parameter = nullptr;
        CHECK(factory.create_raw_expr(T_QUESTIONMARK, parameter) == OB_SUCCESS && parameter);
        parameter->set_ref_expr(raw_columns[i]); parameter->set_param_index(i);
        parameter->set_result_type(raw_columns[i]->get_result_type());
        CHECK(parameter->formalize(session.get()) == OB_SUCCESS && roots.append(parameter) == OB_SUCCESS);
      }
      CHECK(execution.get_physical_plan_ctx()->reserve_param_space(3) == OB_SUCCESS);
      execution.get_physical_plan_ctx()->get_param_store_for_update().at(2).set_int(987654);
    }
    ObStaticEngineExprCG generator(arena, session.get(), nullptr, 0, 0); ObExprFrameInfo frame(arena);
    CHECK(generator.generate(roots, frame) == OB_SUCCESS && execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    std::vector<ObExpr *> expressions;
    for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_REF_COLUMN) {
      if (is_lob_storage(expr.datum_meta_.type_)) expr.obj_meta_.set_has_lob_header();
      expressions.push_back(&expr);
    }
    CHECK(expressions.size() == flat.size());
    PluginCustomSpec spec(arena, PHY_PLUGIN_CUSTOM); spec.plan_ = &physical; spec.explicit_input_ = true;
    if (bound) {
      CHECK(spec.input_bindings_.init(2) == OB_SUCCESS && spec.binding_sources_.init(2) == OB_SUCCESS);
      CHECK(spec.binding_inputs_.init(2) == OB_SUCCESS && spec.binding_targets_.init(2) == OB_SUCCESS);
      uint32_t index = 0;
      for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_QUESTIONMARK) {
        CHECK(index < 2);
        if (is_lob_storage(expr.datum_meta_.type_)) expr.obj_meta_.set_has_lob_header();
        ObDynamicParamSetter setter; setter.param_idx_ = index; setter.src_ = expressions[index]; setter.dst_ = &expr;
        CHECK(spec.input_bindings_.push_back(setter) == OB_SUCCESS && spec.binding_sources_.push_back(index) == OB_SUCCESS);
        CHECK(spec.binding_inputs_.push_back(0) == OB_SUCCESS && spec.binding_targets_.push_back(1) == OB_SUCCESS);
        ++index;
      }
      CHECK(index == 2);
    }
    CHECK(spec.input_columns_.init(total) == OB_SUCCESS && spec.input_type_ids_.init(total) == OB_SUCCESS && spec.input_nullable_.init(total) == OB_SUCCESS);
    CHECK(spec.input_offsets_.init(types.size() + 1) == OB_SUCCESS && spec.input_offsets_.push_back(0) == OB_SUCCESS);
    PluginExprType stored; stored.stored_ = true; stored.physical_type_ = ObLongTextType;
    stored.sql_name_ = ObString::make_string("rust_stored_utf8"); stored.logical_id_ = ObString::make_string(rust_stored_type_test::TYPE_ID);
    stored.owner_ = ObString::make_string("org.seekdb.rust-text");
    stored.format_ = ObString::make_string("org.seekdb.rust-text.stored-utf8.v1"); stored.format_version_ = 1;
    std::vector<std::unique_ptr<ObOpSpec>> child_specs;
    uint32_t slot = 0;
    for (size_t i = 0; i < types.size(); ++i) {
      auto child = std::make_unique<ObOpSpec>(arena, PHY_EXPR_VALUES); child->plan_ = &physical;
      CHECK(child->output_.init(types[i].size()) == OB_SUCCESS);
      for (auto type : types[i]) {
        const char *id = lob ? rust_stored_type_test::TYPE_ID : type == ObIntType ? "core.type.int64" : type == ObDoubleType ? "core.type.float64" : "core.type.bytes";
        CHECK(child->output_.push_back(expressions[slot]) == OB_SUCCESS && spec.input_columns_.push_back(expressions[slot]) == OB_SUCCESS);
        CHECK(spec.input_type_ids_.push_back(ObString::make_string(id)) == OB_SUCCESS && spec.input_nullable_.push_back(1) == OB_SUCCESS);
        ++slot;
      }
      CHECK(spec.input_offsets_.push_back(slot) == OB_SUCCESS); child_specs.push_back(std::move(child));
    }
    if (lob) for (size_t i = 0; i < types.size(); i += 2)
      for (uint32_t j = spec.input_offsets_.at(i); j < spec.input_offsets_.at(i + 1); ++j) CHECK(spec.bind_stored_input(arena, j, stored) == OB_SUCCESS);
    CHECK(spec.columns_.init(outputs) == OB_SUCCESS && spec.output_.init(outputs) == OB_SUCCESS);
    CHECK(spec.type_ids_.init(outputs) == OB_SUCCESS && spec.nullable_.init(outputs) == OB_SUCCESS);
    for (uint32_t i = 0; i < outputs; ++i) {
      CHECK(spec.columns_.push_back(expressions[total + i]) == OB_SUCCESS && spec.output_.push_back(expressions[total + i]) == OB_SUCCESS);
      CHECK(spec.type_ids_.push_back(ObString::make_string(streaming ? "core.type.bytes" : lob ? rust_stored_type_test::TYPE_ID : "core.type.int64")) == OB_SUCCESS);
      CHECK(spec.nullable_.push_back(1) == OB_SUCCESS);
    }
    if (encode) for (uint32_t i = 0; i < outputs; ++i) CHECK(spec.bind_stored_column(arena, i, stored) == OB_SUCCESS);
    oceanbase::share::plugin::CustomExecutorBinding binding;
    CHECK(provider.bind_plugin_custom_executor("org.seekdb.rust-candidate.spool", 1, 0, binding) == OB_SUCCESS);
    auto plan = join ? std::string("SJE1", 4) + std::string(12, '\0') : parameters(mapping);
    if (streaming) {
      plan = std::string(bound ? "SJC1" : "SJR1", 4) + std::string(12, '\0'); plan[12] = outputs;
      const auto word = [&](uint32_t value) { for (int b = 0; b < 4; ++b) plan.push_back(static_cast<char>(value >> (b * 8))); };
      for (uint32_t input = 0; input < outputs; ++input) { word(input); word(1); }
    }
    CHECK(spec.set_binding(arena, binding, plan) == OB_SUCCESS);
    std::vector<std::unique_ptr<Input>> children; std::vector<ObOperator *> pointers;
    for (size_t i = 0; i < types.size(); ++i) {
      auto child = std::make_unique<Input>(execution, *child_specs[i], i, row_counts[i], lob && i % 2 == 0);
      child->repeated_keys = streaming && !bound;
      CHECK(child->init() == OB_SUCCESS); pointers.push_back(child.get()); children.push_back(std::move(child));
    }
    PluginCustomOp op(execution, spec, nullptr);
    CHECK(op.set_children_pointer(pointers.data(), pointers.size()) == OB_SUCCESS && op.init() == OB_SUCCESS);
    const auto verify_cleared = [&] {
      const auto &params = execution.get_physical_plan_ctx()->get_param_store();
      CHECK(params.at(0).is_null() && params.at(1).is_null() && params.at(2).get_int() == 987654);
    };
    if (bound) {
      children[1]->inspect_parameters = [&] {
        const int left = children[0]->position - 1;
        CHECK(left >= 0 && left < row_counts[0]);
        const auto &params = execution.get_physical_plan_ctx()->get_param_store();
        CHECK(params.at(2).get_int() == 987654); // An unowned parameter must never be cleared or rebound.
        for (int i = 0; i < 2; ++i) {
          CHECK(params.at(i).is_null() == (left == 1));
          const auto &expr = *spec.input_bindings_.at(i).dst_;
          ObDatum *datum = nullptr;
          CHECK(expr.eval(op.get_eval_ctx(), datum) == OB_SUCCESS && datum && datum->is_null() == (left == 1));
          if (left == 1) continue;
          if (i == 0) CHECK(datum->get_int() == Input::value(0, left, 0) && params.at(i).get_int() == datum->get_int());
          else {
            ObArenaAllocator temporary; ObString bytes;
            CHECK(ObTextStringHelper::read_real_string_data_with_copy(execution, temporary, *datum,
                expr.datum_meta_, expr.obj_meta_.has_lob_header(), bytes) == OB_SUCCESS);
            CHECK(std::string(bytes.ptr() ? bytes.ptr() : "", bytes.length()) == Input::payload(0, left, 1));
          }
        }
      };
      provider.custom_before_binding_ = [&](const seekdb_plugin_custom_context_v4_t &) {
        auto &eval = op.get_eval_ctx();
        expressions[0]->locate_expr_datum(eval).set_int(-777);
        auto &bytes = expressions[1]->locate_expr_datum(eval);
        if (!bytes.is_null() && bytes.len_) std::memset(const_cast<char *>(bytes.ptr_), '!', bytes.len_);
        bytes.set_null(); // Both Rust's row and the host snapshot must already be owned.
        return OB_SUCCESS;
      };
    }
    const int opened = provider.custom_opens_;
    if (test == 9) {
      CHECK(op.inner_open() == OB_INVALID_ARGUMENT && provider.custom_opens_ == opened);
      op.destroy(); for (auto &child : children) child->destroy(); continue;
    }
    if (test == 0) {
      const uint32_t saved[] = {0,1,4,6};
      for (int fault = 0; fault < 6; ++fault) {
        if (fault == 0) spec.input_offsets_.at(0) = 1;
        if (fault == 1) spec.input_offsets_.at(3) = 5;
        if (fault == 2) spec.input_offsets_.at(1) = 1025;
        if (fault == 3) spec.input_offsets_.at(2) = 0;
        if (fault == 4) spec.explicit_input_ = false;
        if (fault == 5) spec.input_nullable_.at(5) = 2;
        CHECK(op.inner_open() != OB_SUCCESS && provider.custom_opens_ == opened);
        for (int i = 0; i < 4; ++i) spec.input_offsets_.at(i) = saved[i];
        spec.explicit_input_ = true; spec.input_nullable_.at(5) = 1;
      }
    }
    provider.custom_schema_observer_ = [&](const seekdb_plugin_custom_context_v2_t &view) {
      CHECK(view.v1.input_count == types.size() && view.output->column_count == outputs);
      for (size_t i = 0; i < types.size(); ++i) {
        CHECK(view.inputs[i].column_count == types[i].size());
        for (size_t j = 0; j < types[i].size(); ++j) {
          const auto &column = view.inputs[i].columns[j];
          CHECK(column.sql_type == uint32_t(types[i][j]));
          CHECK(bool(column.flags & SEEKDB_PLUGIN_CUSTOM_COLUMN_STORED) == (lob && i % 2 == 0));
        }
      }
    };
    CHECK(op.open() == OB_SUCCESS);
    const auto verify = [&] {
      std::vector<int> before; for (const auto &child : children) before.push_back(child->reads);
      if (streaming) {
        const int rewinds = children[1]->rescans;
        int expected_rewinds = 0;
        for (int left = 0; left < row_counts[0]; ++left) {
          if (!bound && left == 1) continue;
          ++expected_rewinds;
          for (int right = 0; right < row_counts[1]; ++right) {
            if (!bound && (right == 1 || left % 2 != right % 2)) continue;
            CHECK(op.get_next_row() == OB_SUCCESS);
            // The first result must arrive without consuming either input.
            if (left == 0 && right == 0) {
              CHECK(children[0]->reads == before[0] + 1 && children[1]->reads == before[1] + 1);
            }
            for (uint32_t output = 0; output < outputs; ++output) {
              ObDatum *datum = nullptr;
              const auto &expr = *spec.columns_.at(output);
              const bool null = bound && (output ? right : left) == 1;
              CHECK(expr.eval(op.get_eval_ctx(), datum) == OB_SUCCESS && datum && datum->is_null() == null);
              if (null) continue;
              ObArenaAllocator temporary; ObString bytes;
              CHECK(ObTextStringHelper::read_real_string_data_with_copy(execution, temporary, *datum,
                  expr.datum_meta_, expr.obj_meta_.has_lob_header(), bytes) == OB_SUCCESS);
              CHECK(std::string(bytes.ptr() ? bytes.ptr() : "", bytes.length()) == Input::payload(output, output ? right : left, 1));
            }
          }
        }
        CHECK(op.get_next_row() == OB_ITER_END && op.get_next_row() == OB_ITER_END);
        CHECK(children[0]->reads == before[0] + row_counts[0] + 1);
        CHECK(children[1]->reads == before[1] + expected_rewinds * (row_counts[1] + 1));
        CHECK(children[1]->rescans == rewinds + expected_rewinds);
        if (bound) verify_cleared();
        return;
      }
      const int largest = join || row_counts.empty() ? 0 : *std::max_element(row_counts.begin(), row_counts.end());
      for (int row = 0; row < largest; ++row) for (size_t input = 0; input < types.size(); ++input) {
        if (row >= row_counts[input]) continue;
        const int ret = op.get_next_row();
        if (ret != OB_SUCCESS) std::cerr << "multi next=" << ret << " case=" << test << std::endl;
        CHECK(ret == OB_SUCCESS);
        for (uint32_t i = 0; i < outputs; ++i) {
          auto &expr = *spec.columns_.at(i); ObDatum *datum = nullptr;
          CHECK(expr.eval(op.get_eval_ctx(), datum) == OB_SUCCESS && datum && datum->is_null() == (row == 1));
          if (row == 1) continue;
          if (!lob) CHECK(datum->get_int() == Input::value(input, row, mapping[input][i]));
          else {
            ObArenaAllocator temporary; ObString bytes;
            CHECK(ObTextStringHelper::read_real_string_data_with_copy(execution, temporary, *datum, expr.datum_meta_, true, bytes) == OB_SUCCESS);
            const auto expected = Input::payload(input, row, mapping[input][i]);
            CHECK(std::string(bytes.ptr() ? bytes.ptr() : "", bytes.length()) == (encode ? rust_custom_projection_test::encoded(expected) : expected));
          }
        }
      }
      CHECK(op.get_next_row() == OB_ITER_END);
      for (size_t i = 0; i < children.size(); ++i) CHECK(children[i]->reads == before[i] + row_counts[i] + 1);
    };
    verify(); CHECK(op.rescan() == OB_SUCCESS); verify();
    if (bound) {
      const auto poison_source = provider.custom_before_binding_;
      for (int reset_case = 0; reset_case < 5; ++reset_case) {
        CHECK(op.rescan() == OB_SUCCESS);
        bool called = false;
        const int source_rescans = children[0]->rescans, target_reads = children[1]->reads;
        provider.custom_before_binding_ = [&](const seekdb_plugin_custom_context_v4_t &view) -> int {
          CHECK(!called); called = true;
          const auto &v1 = view.v3.v2.v1;
          int32_t error = OB_SUCCESS;
          // Establish an environment first, so clearing is not tested only
          // against an already-empty parameter store. No right row is read.
          CHECK(view.bind_rescan_input(v1.host_context, 1, &error) == SEEKDB_PLUGIN_STATUS_OK && !error);
          CHECK(!execution.get_physical_plan_ctx()->get_param_store().at(0).is_null());
          if (reset_case == 1) {
            seekdb_plugin_custom_row_v1_t row = {};
            seekdb_plugin_status_t status = SEEKDB_PLUGIN_STATUS_OK;
            while ((status = v1.next_input(v1.host_context, 0, &row, &error)) == SEEKDB_PLUGIN_STATUS_OK) CHECK(!error);
            CHECK(status == SEEKDB_PLUGIN_STATUS_END_OF_STREAM && !error && children[0]->position == row_counts[0]);
          }
          if (reset_case == 2) children[0]->rescan_error = OB_TIMEOUT;
          const auto status = view.v3.rescan_input(v1.host_context, 0, &error);
          verify_cleared();
          if (reset_case == 2) {
            CHECK(status == SEEKDB_PLUGIN_STATUS_INTERNAL && error == OB_TIMEOUT);
            return error; // Preserve the real host failure through loader/SDK.
          }
          CHECK(status == SEEKDB_PLUGIN_STATUS_OK && !error);
          if (reset_case >= 3) {
            seekdb_plugin_custom_row_v1_t row = {};
            const auto invalid = reset_case == 3 ? v1.next_input(v1.host_context, 1, &row, &error)
                                                : view.bind_rescan_input(v1.host_context, 1, &error);
            CHECK(invalid == SEEKDB_PLUGIN_STATUS_INTERNAL && error == OB_STATE_NOT_MATCH);
            CHECK(children[1]->reads == target_reads);
            return error; // No stale target access or binding without a fresh source row.
          }
          CHECK(view.v3.rescan_input(v1.host_context, 0, &error) == SEEKDB_PLUGIN_STATUS_OK && !error);
          verify_cleared();
          seekdb_plugin_custom_row_v1_t row = {};
          CHECK(v1.next_input(v1.host_context, 0, &row, &error) == SEEKDB_PLUGIN_STATUS_OK && !error);
          CHECK(row.column_count == 2 && children[0]->position == 1 && children[1]->reads == target_reads);
          return OB_SUCCESS; // Actual Rust binding now consumes the fresh row-zero snapshot.
        };
        CHECK(op.get_next_row() == (reset_case >= 3 ? OB_STATE_NOT_MATCH : reset_case == 2 ? OB_TIMEOUT : OB_SUCCESS) && called);
        CHECK(children[0]->rescans == source_rescans + (reset_case >= 2 ? 1 : 2));
        if (reset_case >= 2) {
          CHECK(op.get_next_row() == OB_STATE_NOT_MATCH); verify_cleared();
          children[0]->rescan_error = OB_SUCCESS;
        } else {
          CHECK(children[1]->reads == target_reads + 1);
          children[1]->inspect_parameters();
        }
        provider.custom_before_binding_ = poison_source;
        CHECK(op.rescan() == OB_SUCCESS); verify();
      }
      // A failed target rescan occurs AFTER both parameters have been set.
      CHECK(op.rescan() == OB_SUCCESS); children[1]->rescan_error = OB_TIMEOUT;
      CHECK(op.get_next_row() == OB_TIMEOUT && op.get_next_row() == OB_STATE_NOT_MATCH);
      verify_cleared();
      const ObBatchRows *batch = nullptr;
      CHECK(op.get_next_batch(3, batch) == OB_STATE_NOT_MATCH && !batch);
      children[1]->rescan_error = OB_SUCCESS;
      CHECK(op.rescan() == OB_SUCCESS); verify();
      // A later sibling failure during whole-operator rescan also clears owned parameters.
      CHECK(op.rescan() == OB_SUCCESS && op.get_next_row() == OB_SUCCESS);
      children[1]->rescan_error = OB_TIMEOUT;
      CHECK(op.rescan() == OB_TIMEOUT && op.get_next_row() == OB_STATE_NOT_MATCH);
      verify_cleared(); children[1]->rescan_error = OB_SUCCESS;
      CHECK(op.rescan() == OB_SUCCESS); verify();
      CHECK(op.rescan() == OB_SUCCESS && op.get_next_row() == OB_SUCCESS);
      CHECK(op.inner_close() == OB_SUCCESS); verify_cleared();
      CHECK(op.inner_open() == OB_SUCCESS && op.rescan() == OB_SUCCESS); verify();
      provider.custom_before_binding_ = {};
    }
    if (test == 12) {
      CHECK(op.rescan() == OB_SUCCESS); children[1]->rescan_error = OB_TIMEOUT;
      CHECK(op.get_next_row() == OB_TIMEOUT && op.get_next_row() == OB_STATE_NOT_MATCH);
      const ObBatchRows *batch = nullptr;
      CHECK(op.get_next_batch(3, batch) == OB_STATE_NOT_MATCH && !batch);
      children[1]->rescan_error = OB_SUCCESS;
      CHECK(op.rescan() == OB_SUCCESS); verify();
      CHECK(op.rescan() == OB_SUCCESS);
      class CancelAfterRewind final : public ObIExtraStatusCheck {
      public:
        explicit CancelAfterRewind(const int &count) : count(count), before(count) {}
        const char *name() const override { return "plugin-input-rewind-cancel"; }
        int check() const override { return count == before ? OB_SUCCESS : OB_TIMEOUT; }
        const int &count; const int before;
      } cancel(children[1]->rescans);
      { ObIExtraStatusCheck::Guard guard(execution, cancel); CHECK(op.get_next_row() == OB_TIMEOUT); }
      CHECK(op.get_next_row() == OB_STATE_NOT_MATCH && op.rescan() == OB_SUCCESS); verify();
      CHECK(op.inner_close() == OB_SUCCESS);
      auto invalid = plan; invalid[8] = 2;
      CHECK(spec.set_binding(arena, binding, invalid) == OB_SUCCESS && op.inner_open() == OB_SUCCESS && op.rescan() == OB_SUCCESS);
      const int reads = children[0]->reads + children[1]->reads, rewinds = children[1]->rescans;
      CHECK(op.get_next_row() == OB_INVALID_ARGUMENT && op.get_next_row() == OB_STATE_NOT_MATCH);
      CHECK(reads == children[0]->reads + children[1]->reads && rewinds == children[1]->rescans);
    }
    if (join) {
      CHECK(op.rescan() == OB_SUCCESS); children[1]->fail_at = 0;
      CHECK(op.get_next_row() == OB_TIMEOUT && op.get_next_row() == OB_STATE_NOT_MATCH);
      children[1]->fail_at = -1; CHECK(op.rescan() == OB_SUCCESS); verify();
      CHECK(op.inner_close() == OB_SUCCESS);
      auto invalid = plan; invalid[8] = 1; // Right key past its one-column schema.
      CHECK(spec.set_binding(arena, binding, invalid) == OB_SUCCESS && op.inner_open() == OB_SUCCESS && op.rescan() == OB_SUCCESS);
      const int reads = children[0]->reads + children[1]->reads;
      CHECK(op.get_next_row() == OB_INVALID_ARGUMENT && op.get_next_row() == OB_STATE_NOT_MATCH);
      CHECK(reads == children[0]->reads + children[1]->reads);
    }
    if (test == 0) {
      children[1]->rescan_error = OB_TIMEOUT;
      CHECK(op.rescan() == OB_TIMEOUT && op.get_next_row() == OB_STATE_NOT_MATCH);
      const ObBatchRows *batch = nullptr;
      CHECK(op.get_next_batch(3, batch) == OB_STATE_NOT_MATCH && !batch);
      children[1]->rescan_error = OB_SUCCESS;
      CHECK(op.rescan() == OB_SUCCESS); verify();
      CHECK(op.rescan() == OB_SUCCESS); children[1]->fail_at = 0;
      CHECK(op.get_next_row() == OB_TIMEOUT && op.get_next_row() == OB_STATE_NOT_MATCH);
      children[1]->fail_at = -1; CHECK(op.rescan() == OB_SUCCESS); verify();
      CHECK(op.rescan() == OB_SUCCESS);
      class CancelAfterRead final : public ObIExtraStatusCheck {
      public:
        explicit CancelAfterRead(const int &reads) : reads(reads), before(reads) {}
        const char *name() const override { return "plugin-multi-cancel"; }
        int check() const override { return reads == before ? OB_SUCCESS : OB_TIMEOUT; }
        const int &reads; const int before;
      } cancel(children[0]->reads);
      { ObIExtraStatusCheck::Guard guard(execution, cancel); CHECK(op.get_next_row() == OB_TIMEOUT); }
      CHECK(op.get_next_row() == OB_STATE_NOT_MATCH && op.rescan() == OB_SUCCESS); verify();
      // A bad mapping in the LAST branch must fail before reading even the first.
      CHECK(op.inner_close() == OB_SUCCESS);
      auto invalid = mapping; invalid.back()[0] = 2;
      CHECK(spec.set_binding(arena, binding, parameters(invalid)) == OB_SUCCESS && op.inner_open() == OB_SUCCESS && op.rescan() == OB_SUCCESS);
      std::vector<int> reads; for (const auto &child : children) reads.push_back(child->reads);
      CHECK(op.get_next_row() == OB_INVALID_ARGUMENT && op.get_next_row() == OB_STATE_NOT_MATCH);
      for (size_t i = 0; i < children.size(); ++i) CHECK(children[i]->reads == reads[i]);
      CHECK(op.inner_close() == OB_SUCCESS && spec.set_binding(arena, binding, plan) == OB_SUCCESS && op.inner_open() == OB_SUCCESS);
      CHECK(op.rescan() == OB_SUCCESS); verify();
    }
    provider.custom_schema_observer_ = {};
    CHECK(op.close() == OB_SUCCESS && provider.custom_opens_ == provider.custom_closes_);
    op.destroy(); for (auto &child : children) child->destroy();
  }
  std::cerr << "custom multi input: independent layouts, round-robin Rust projection, zero/64 inputs, limits and failures passed" << std::endl;
}
}
#endif
