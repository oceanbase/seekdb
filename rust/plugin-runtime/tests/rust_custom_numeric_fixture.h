// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Actual SQL frames -> PluginCustomOp -> loader -> Rust spool. The source is a
// fixture; wire bytes are independently asserted before entering Rust, so an
// incorrect-width echo cannot pass merely by round-tripping the same mistake.
#ifndef SEEKDB_TEST_RUST_CUSTOM_NUMERIC_FIXTURE_H_
#define SEEKDB_TEST_RUST_CUSTOM_NUMERIC_FIXTURE_H_
#include "sql/engine/basic/plugin_custom_op.h"
#include <cmath>
#include <limits>
namespace rust_custom_numeric_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
template <typename T> std::vector<uint8_t> bytes(T value)
{
  std::vector<uint8_t> result(sizeof(value)); std::memcpy(result.data(), &value, sizeof(value)); return result;
}
struct Cell {
  int64_t signed_value = 0;
  uint64_t unsigned_value = 0;
  double floating = 0;
  bool null = false;
  std::vector<uint8_t> wire;
};
inline Cell signed_cell(int64_t value) { Cell c; c.signed_value = value; c.wire = bytes(value); return c; }
inline Cell unsigned_cell(uint64_t value) { Cell c; c.unsigned_value = value; c.wire = bytes(value); return c; }
inline Cell floating_cell(double value) { Cell c; c.floating = value; c.wire = bytes(value); return c; }
struct Case {
  ObObjType type;
  const char *id;
  std::vector<Cell> rows;
  std::vector<Cell> invalid_inputs;
  std::vector<std::vector<uint8_t>> invalid_outputs;
};
class Input final : public ObOperator {
public:
  Input(ObExecContext &ctx, const ObOpSpec &spec) : ObOperator(ctx, spec, nullptr) {}
  std::vector<Cell> rows;
  int inner_get_next_row() override {
    if (position_ == rows.size()) return OB_ITER_END;
    clear_evaluated_flag(); auto &expr = *spec_.output_.at(0);
    auto &datum = expr.locate_datum_for_write(eval_ctx_); const auto &cell = rows[position_++];
    const auto type = expr.datum_meta_.type_;
    if (cell.null) datum.set_null();
    else if (ob_is_int_tc(type)) datum.set_int(cell.signed_value);
    else if (ob_is_uint_tc(type)) datum.set_uint(cell.unsigned_value);
    else if (ob_is_float_type(type)) datum.set_float(static_cast<float>(cell.floating));
    else datum.set_double(cell.floating);
    expr.set_evaluated_projected(eval_ctx_); return OB_SUCCESS;
  }
  int inner_rescan() override { position_ = 0; return ObOperator::inner_rescan(); }
  void destroy() override { ObOperator::destroy(); }
private:
  size_t position_ = 0;
};
template <typename Provider> void run(Provider &provider, ObArenaAllocator &arena)
{
  std::vector<Case> cases;
  // Explicit SQL bounds, independently of the production type-width helper.
  for (const auto &bounds : {std::pair<ObObjType, int64_t>{ObTinyIntType, 127},
      {ObSmallIntType, 32767}, {ObMediumIntType, 8388607}, {ObInt32Type, INT32_MAX}, {ObIntType, INT64_MAX}}) {
    Case c{bounds.first, "core.type.int64", {signed_cell(-bounds.second - 1), signed_cell(0), signed_cell(bounds.second)}, {}, {}};
    if (bounds.second != INT64_MAX) {
      c.invalid_inputs = {signed_cell(bounds.second + 1), signed_cell(-bounds.second - 2)};
      c.invalid_outputs = {bytes(bounds.second + 1), bytes(-bounds.second - 2)};
    }
    cases.push_back(std::move(c));
  }
  for (const auto &bounds : {std::pair<ObObjType, uint64_t>{ObUTinyIntType, 255},
      {ObUSmallIntType, 65535}, {ObUMediumIntType, 16777215}, {ObUInt32Type, UINT32_MAX}, {ObUInt64Type, UINT64_MAX}}) {
    Case c{bounds.first, "core.type.uint64", {unsigned_cell(0), unsigned_cell(bounds.second)}, {}, {}};
    if (bounds.second != UINT64_MAX) {
      c.invalid_inputs = {unsigned_cell(bounds.second + 1)};
      c.invalid_outputs = {bytes(bounds.second + 1)};
    }
    cases.push_back(std::move(c));
  }
  for (const auto type : {ObFloatType, ObUFloatType, ObDoubleType, ObUDoubleType}) {
    const bool is_float = ob_is_float_type(type), is_unsigned = type == ObUFloatType || type == ObUDoubleType;
    const double maximum = is_float ? std::numeric_limits<float>::max() : std::numeric_limits<double>::max();
    Case c{type, "core.type.float64", {floating_cell(0.0), floating_cell(-0.0), floating_cell(1.25),
        floating_cell(maximum), floating_cell(std::numeric_limits<double>::infinity())}, {}, {}};
    if (!is_unsigned) c.rows.push_back(floating_cell(-maximum));
    else { c.invalid_inputs.push_back(floating_cell(-1.0)); c.invalid_outputs.push_back(bytes(-1.0)); }
    if (is_float) c.invalid_outputs.push_back(bytes(double(std::numeric_limits<float>::max()) * 2));
    cases.push_back(std::move(c));
  }
  Case boolean{ObIntType, "core.type.bool", {signed_cell(0), signed_cell(1)}, {signed_cell(-1), signed_cell(2)}, {}};
  for (auto &c : boolean.rows) c.wire = bytes(uint8_t(c.signed_value));
  cases.push_back(std::move(boolean));
  Case i32{ObIntType, "core.type.int32", {signed_cell(INT32_MIN), signed_cell(0), signed_cell(INT32_MAX)},
      {signed_cell(int64_t(INT32_MIN) - 1), signed_cell(int64_t(INT32_MAX) + 1)}, {}};
  for (auto &c : i32.rows) c.wire = bytes(int32_t(c.signed_value));
  cases.push_back(std::move(i32));
  Case u32{ObIntType, "core.type.uint32", {signed_cell(0), signed_cell(UINT32_MAX)},
      {signed_cell(-1), signed_cell(int64_t(UINT32_MAX) + 1)}, {}};
  for (auto &c : u32.rows) c.wire = bytes(uint32_t(c.signed_value));
  cases.push_back(std::move(u32));
  Case signed_wire_unsigned_sql{ObUInt64Type, "core.type.int32", {unsigned_cell(0), unsigned_cell(INT32_MAX)},
      {unsigned_cell(uint64_t(INT32_MAX) + 1)}, {bytes(int32_t(-1))}};
  for (auto &c : signed_wire_unsigned_sql.rows) c.wire = bytes(int32_t(c.unsigned_value));
  cases.push_back(std::move(signed_wire_unsigned_sql));
  Case unsigned_wire_signed_sql{ObTinyIntType, "core.type.uint32", {signed_cell(0), signed_cell(127)},
      {signed_cell(-1), signed_cell(128)}, {bytes(uint32_t(128)), bytes(UINT32_MAX)}};
  for (auto &c : unsigned_wire_signed_sql.rows) c.wire = bytes(uint32_t(c.signed_value));
  cases.push_back(std::move(unsigned_wire_signed_sql));
  cases.push_back(Case{ObIntType, "org.example.bool", {signed_cell(INT64_MIN), signed_cell(INT64_MAX)}, {}, {}});
  struct Alias { const char *core; const char *gis; ObObjType type; };
  for (const auto &alias : {Alias{"core.type.bool", "org.seekdb.gis.scalar.bool", ObIntType},
      {"core.type.int32", "org.seekdb.gis.scalar.int32", ObIntType},
      {"core.type.uint32", "org.seekdb.gis.scalar.uint32", ObIntType},
      {"core.type.int64", "org.seekdb.gis.scalar.int64", ObIntType},
      {"core.type.uint64", "org.seekdb.gis.scalar.uint64", ObUInt64Type},
      {"core.type.float64", "org.seekdb.gis.scalar.float64", ObDoubleType}}) {
    const auto found = std::find_if(cases.begin(), cases.end(), [&](const auto &c) {
      return c.type == alias.type && !std::strcmp(c.id, alias.core);
    });
    CHECK(found != cases.end()); Case copy = *found; copy.id = alias.gis; cases.push_back(std::move(copy));
  }
  for (auto &test : cases) {
    std::cerr << "custom numeric " << test.type << " " << test.id << std::endl;
    Cell null; null.null = true; test.rows.push_back(null);
    auto session = std::make_unique<ObSQLSessionInfo>();
    CHECK(session->test_init(1, 1, &arena) == OB_SUCCESS);
    CHECK(session->load_default_sys_variable(false, false) == OB_SUCCESS); session->set_inner_session();
    ObPhysicalPlan physical; ObExecContext execution(arena); execution.set_my_session(session.get());
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS); execution.get_physical_plan_ctx()->set_phy_plan(&physical);
    ObSQLSessionInfo::ExecCtxSessionRegister registration(*session, &execution);
    ObRawExprFactory factory(arena); ObRawExprUniqueSet roots(false); ObColumnRefRawExpr *raw = nullptr;
    CHECK(factory.create_raw_expr(T_REF_COLUMN, raw) == OB_SUCCESS && raw);
    raw->set_ref_id(100, 1); raw->set_data_type(test.type);
    CHECK(raw->formalize(session.get()) == OB_SUCCESS && roots.append(raw) == OB_SUCCESS);
    ObStaticEngineExprCG generator(arena, session.get(), nullptr, 0, 0); ObExprFrameInfo frame(arena);
    CHECK(generator.generate(roots, frame) == OB_SUCCESS && execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    ObOpSpec child_spec(arena, PHY_EXPR_VALUES); PluginCustomSpec spec(arena, PHY_PLUGIN_CUSTOM);
    child_spec.plan_ = &physical; spec.plan_ = &physical;
    CHECK(child_spec.output_.init(1) == OB_SUCCESS && spec.columns_.init(1) == OB_SUCCESS);
    CHECK(spec.output_.init(1) == OB_SUCCESS && spec.type_ids_.init(1) == OB_SUCCESS && spec.nullable_.init(1) == OB_SUCCESS);
    for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_REF_COLUMN) {
      CHECK(expr.datum_meta_.type_ == test.type && PluginCustomOp::supported_column(expr));
      CHECK(child_spec.output_.push_back(&expr) == OB_SUCCESS && spec.columns_.push_back(&expr) == OB_SUCCESS);
      CHECK(spec.output_.push_back(&expr) == OB_SUCCESS);
      CHECK(spec.type_ids_.push_back(ObString::make_string(test.id)) == OB_SUCCESS && spec.nullable_.push_back(1) == OB_SUCCESS);
    }
    CHECK(spec.columns_.count() == 1);
    oceanbase::share::plugin::CustomExecutorBinding binding;
    CHECK(provider.bind_plugin_custom_executor("org.seekdb.rust-candidate.spool", 1, 0, binding) == OB_SUCCESS);
    CHECK(spec.set_binding(arena, binding, {}) == OB_SUCCESS);
    Input child(execution, child_spec); child.rows = test.rows;
    PluginCustomOp op(execution, spec, nullptr); ObOperator *children[] = {&child};
    CHECK(op.set_children_pointer(children, 1) == OB_SUCCESS && child.init() == OB_SUCCESS && op.init() == OB_SUCCESS);
    const int opens = provider.custom_opens_, closes = provider.custom_closes_;
    // A corrupt plan cannot assign a known byte ID to a numeric Datum.
    spec.type_ids_.at(0) = ObString::make_string("core.type.bytes");
    CHECK(op.inner_open() == OB_INVALID_DATA && provider.custom_opens_ == opens);
    spec.type_ids_.at(0) = ObString::make_string(test.id);
    size_t seen = 0;
    size_t described = 0;
    provider.custom_schema_observer_ = [&](const seekdb_plugin_custom_context_v2_t &context) {
      ++described;
      CHECK(context.v1.input_count == 1 && context.inputs && context.output);
      CHECK(context.inputs[0].column_count == 1 && context.output->column_count == 1);
      const auto &column = context.inputs[0].columns[0];
      CHECK(column.sql_type == test.type && !std::strcmp(column.type_id, test.id));
      CHECK(column.flags == SEEKDB_PLUGIN_CUSTOM_COLUMN_NULLABLE);
      CHECK(!std::memcmp(&column, context.output->columns, sizeof(column)));
      const auto &wire = test.rows.front().wire;
      const uint32_t expected = wire.size() == 1 ? SEEKDB_PLUGIN_CUSTOM_ENCODING_BOOL :
          wire.size() == 4 ? (std::strstr(test.id, ".uint32") ? SEEKDB_PLUGIN_CUSTOM_ENCODING_UINT32 : SEEKDB_PLUGIN_CUSTOM_ENCODING_INT32) :
          ob_is_float_type(test.type) || ob_is_double_type(test.type) ? SEEKDB_PLUGIN_CUSTOM_ENCODING_FLOAT64 :
          ob_is_uint_tc(test.type) ? SEEKDB_PLUGIN_CUSTOM_ENCODING_UINT64 : SEEKDB_PLUGIN_CUSTOM_ENCODING_INT64;
      CHECK(column.encoding == expected);
    };
    provider.custom_input_observer_ = [&](const seekdb_plugin_custom_row_v1_t &row) {
      CHECK(row.column_count == 1 && seen < child.rows.size());
      const auto &want = child.rows[seen++]; const auto &value = row.values[0];
      CHECK(!std::strcmp(value.type_id, test.id) && value.is_null == want.null);
      if (want.null) CHECK(!value.data && !value.data_size);
      else CHECK(value.data_size == want.wire.size() && !std::memcmp(value.data, want.wire.data(), want.wire.size()));
    };
    CHECK(op.open() == OB_SUCCESS);
    const auto verify = [&] {
      for (const auto &want : child.rows) {
        CHECK(op.get_next_row() == OB_SUCCESS); ObDatum *datum = nullptr;
        CHECK(spec.columns_.at(0)->eval(op.get_eval_ctx(), datum) == OB_SUCCESS && datum && datum->is_null() == want.null);
        if (!want.null) {
          if (ob_is_int_tc(test.type)) CHECK(datum->get_int() == want.signed_value);
          else if (ob_is_uint_tc(test.type)) CHECK(datum->get_uint() == want.unsigned_value);
          else {
            const double value = ob_is_float_type(test.type) ? datum->get_float() : datum->get_double();
            CHECK(value == want.floating && std::signbit(value) == std::signbit(want.floating));
          }
        }
      }
      CHECK(op.get_next_row() == OB_ITER_END && seen == child.rows.size());
    };
    verify(); seen = 0; CHECK(op.rescan() == OB_SUCCESS); verify();
    for (const auto &invalid : test.invalid_inputs) {
      child.rows = {invalid}; seen = 0; CHECK(op.rescan() == OB_SUCCESS);
      CHECK(op.get_next_row() == OB_DATA_OUT_OF_RANGE && seen == 0);
      CHECK(op.get_next_row() == OB_STATE_NOT_MATCH);
    }
    child.rows = {test.rows.front()};
    // A valid, deliberately unaligned output must also publish correctly.
    std::vector<uint8_t> unaligned_valid(1, 0);
    unaligned_valid.insert(unaligned_valid.end(), child.rows[0].wire.begin(), child.rows[0].wire.end());
    provider.custom_output_rewriter_ = [&](auto &values) {
      values[0].data = unaligned_valid.data() + 1; values[0].data_size = unaligned_valid.size() - 1;
    };
    seen = 0; CHECK(op.rescan() == OB_SUCCESS); verify(); provider.custom_output_rewriter_ = {};
    const auto bad_output = [&](const std::vector<uint8_t> &fault, int expected) {
      // Intentionally unaligned input also exercises the host's memcpy decoder.
      std::vector<uint8_t> unaligned(1, 0); unaligned.insert(unaligned.end(), fault.begin(), fault.end());
      provider.custom_output_rewriter_ = [&](auto &values) { values[0].data = unaligned.data() + 1; values[0].data_size = fault.size(); };
      seen = 0; CHECK(op.rescan() == OB_SUCCESS);
      CHECK(op.get_next_row() == expected && op.get_next_row() == OB_STATE_NOT_MATCH);
      provider.custom_output_rewriter_ = {}; seen = 0; CHECK(op.rescan() == OB_SUCCESS); verify();
    };
    bad_output({}, OB_INVALID_DATA); // A non-NULL number cannot have zero width.
    bad_output(std::vector<uint8_t>(test.rows.front().wire.size() + 1), OB_INVALID_DATA);
    for (const auto &fault : test.invalid_outputs) bad_output(fault, OB_DATA_OUT_OF_RANGE);
    if (!std::strcmp(test.id, "core.type.bool") || !std::strcmp(test.id, "org.seekdb.gis.scalar.bool"))
      bad_output(bytes(uint8_t(2)), OB_INVALID_DATA);
    // Empty input still exposes a complete schema before the first fetch/EOF.
    child.rows.clear(); seen = 0; CHECK(op.rescan() == OB_SUCCESS);
    const auto before_described = described;
    CHECK(op.get_next_row() == OB_ITER_END && seen == 0 && described == before_described + 1);
    provider.custom_input_observer_ = {}; provider.custom_schema_observer_ = {}; CHECK(op.close() == OB_SUCCESS);
    CHECK(provider.custom_opens_ == opens + 1 && provider.custom_closes_ == closes + 1);
    op.destroy(); child.destroy();
  }
  std::cerr << "custom numeric: " << cases.size() << " SQL/logical representations passed" << std::endl;
}
}
#endif
