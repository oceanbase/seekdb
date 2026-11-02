/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "query/engine/expr/ob_expr_lob_utils.h"
#include "query/engine/expr/ob_expr_util.h"
#include "query/engine/ob_operator.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/ob_spi.h"
#include "sql/engine/expr/ob_expr_udf.h"
#include "sql/pl/ob_pl_package_state.h"
#include "sql/pl/ob_pl.h"
#include "sql/pl/ob_pl_user_type.h"
#include "sql/pl/ob_pl_resolver.h"
#include "sql/pl/ob_pl_package_guard.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "query/engine/basic/ob_batch_result_holder.h"
#include "sql/session/ob_sql_session_info.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#include "common/sql_mode/ob_sql_mode.h"
#include "share/datum/typed_datum_compare.h"
#include <cstring>
#include <gtest/gtest.h>

namespace {
oceanbase::common::Datum read_parameter_value(
    const oceanbase::sql::ObPhysicalPlanCtx &plan, int64_t index)
{
  oceanbase::common::Datum value;
  EXPECT_EQ(oceanbase::common::OB_SUCCESS, plan.read_native_parameter(index, value));
  return value;
}

// Only tests of a retaining generic Expr ABI explicitly request a projection.
const oceanbase::common::Datum *materialized_parameter(
    oceanbase::sql::ObPhysicalPlanCtx &plan, int64_t index)
{
  if (!plan.has_native_parameter(index)) { return nullptr; }
  oceanbase::common::Datum *value = nullptr;
  EXPECT_EQ(oceanbase::common::OB_SUCCESS, plan.materialize_parameter_for_expr(index, value));
  return value;
}
} // namespace

using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace oceanbase { namespace sql {
struct NativeExecutionParametersTestPeer {
  static int64_t input_count(const ObPhysicalPlanCtx &p) { return p.param_store_.count(); }
  static int64_t input_bytes(const ObPhysicalPlanCtx &p) { return p.parameter_input_allocator_.used(); }
};
} }
namespace {
// Explicit physical export used only by old wrapper-boundary oracles below.
// It is not an execution directory and never supplies values back by identity.
DatumParameter parameter_snapshot(const ObPhysicalPlanCtx &plan, int64_t index, ObIAllocator &owner)
{
  NativeTypedDatum value;
  DatumParameterMetadata attributes;
  DatumParameter result;
  EXPECT_EQ(OB_SUCCESS, plan.capture_native_parameter(index, value, attributes));
  if (value.type_ != nullptr) {
    alignas(8) char scratch[64];
    ObDatum legacy;
    EXPECT_EQ(OB_SUCCESS, DatumCopyPlan(*value.type_).legacy_view(value.datum_, legacy, scratch, sizeof(scratch)));
    EXPECT_EQ(OB_SUCCESS, result.assign_legacy_owned(legacy, attributes.type_meta_, owner));
    result.set_metadata(attributes);
  }
  return result;
}
struct Frame {
  ObArenaAllocator arena;
  ObExecContext exec;
  ObExpr expr;
  char **frames;
  DatumParameter physical_snapshot;
  explicit Frame(ObIAllocator *exec_allocator = nullptr)
      : arena(), exec(exec_allocator == nullptr ? arena : *exec_allocator), expr(), frames(nullptr), physical_snapshot() {
    EXPECT_EQ(OB_SUCCESS, exec.create_physical_plan_ctx());
    frames = static_cast<char **>(arena.alloc(sizeof(char *)));
    frames[0] = static_cast<char *>(arena.alloc(2048));
    memset(frames[0], 0, 2048);
    exec.set_frames(frames);
    expr.frame_idx_ = 0; expr.datum_off_ = 64; expr.eval_info_off_ = 128;
    expr.res_buf_off_ = 256; expr.res_buf_len_ = 512; expr.type_ = T_EXEC_VAR;
    expr.obj_meta_.set_int(); expr.datum_meta_.type_ = ObIntType;
    expr.obj_datum_map_ = OBJ_DATUM_8BYTE_DATA;
    EXPECT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, ObAccuracy(19, 0), arena));
    new (frames[0] + 64) Datum();
    auto &plan = *exec.get_physical_plan_ctx();
    EXPECT_EQ(OB_SUCCESS, plan.reserve_param_space(1));
    DatumParameter initial;
    initial.set_type_meta(expr.obj_meta_); initial.set_accuracy(ObAccuracy(19, 0)); initial.set_param_meta();
    EXPECT_EQ(OB_SUCCESS, plan.set_parameter_input(0, initial));
    plan.set_original_param_cnt(1);
    EXPECT_EQ(OB_SUCCESS, plan.init_datum_param_store());
  }
  const DatumParameter &parameters() {
    physical_snapshot = parameter_snapshot(*exec.get_physical_plan_ctx(), 0, arena);
    return physical_snapshot;
  }
  const DatumParameter &native() { return parameters(); }
  NativeTypedDatum typed(const DatumParameter &input) {
    NativeTypedDatum result;
    result.type_ = &expr.get_sql_type();
    const SqlTypeMeta meta = input.is_null() ? input.get_param_meta() : input.get_type_meta();
    if (!meta.is_ext() && meta.get_type() != ObCollectionSQLType
        && meta.get_type() != ObEnumType && meta.get_type() != ObSetType
        && meta.get_type() != expr.get_sql_type().kind()) {
      EXPECT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, input.get_accuracy(), arena, result.type_));
    }
    alignas(8) char scratch[64]; ObDatum legacy;
    EXPECT_EQ(OB_SUCCESS, input.export_legacy(legacy, scratch, sizeof(scratch)));
    const int imported = DatumCopyPlan(*result.type_).import_legacy(legacy, result.datum_);
    EXPECT_EQ(OB_SUCCESS, imported);
    if (imported == OB_SUCCESS && result.datum_.is_present() && input.datum_.is_inline_extended()) {
      // Validation is unchanged; this native fixture result cannot borrow its
      // helper-local legacy export scratch after typed() returns.
      result.datum_ = input.datum_;
    }
    return result;
  }
  int bind_status(int64_t index, const DatumParameter &value) {
    alignas(8) char scratch[64]; ObDatum legacy; Datum native;
    int ret = value.export_legacy(legacy, scratch, sizeof(scratch));
    if (ret == OB_SUCCESS) { ret = expr.get_datum_copy_plan().import_legacy(legacy, native); }
    return ret == OB_SUCCESS ? exec.get_physical_plan_ctx()->set_runtime_parameter(
        index, native, expr.get_sql_type(), value.get_metadata()) : ret;
  }
  void bind(const DatumParameter &value) { ASSERT_EQ(OB_SUCCESS, bind_status(0, value)); }
  int capture(const ObDynamicParamSetter &setter, ObEvalCtx &ctx, DatumParameter &out) {
    NativeTypedDatum value; DatumParameterMetadata attributes;
    int ret = setter.capture_dynamic_param(ctx, value, attributes);
    if (ret == OB_SUCCESS) {
      alignas(8) char scratch[64]; ObDatum legacy; DatumParameter candidate;
      if (OB_SUCCESS != (ret = DatumCopyPlan(*value.type_).legacy_view(value.datum_, legacy, scratch, sizeof(scratch)))) {
      } else if (OB_SUCCESS != (ret = candidate.assign_legacy_owned(legacy, attributes.type_meta_, arena))) {
      } else { candidate.set_metadata(attributes); out = candidate; }
    }
    return ret;
  }
  int restore(const ObDynamicParamSetter &setter, ObEvalCtx &ctx, const DatumParameter &input) {
    const NativeTypedDatum value = typed(input);
    return setter.restore_dynamic_param(ctx, value, input.get_metadata());
  }
  Datum native_value(const DatumParameter &value) { return typed(value).datum_; }
  DatumParameter integer(int64_t n) {
    DatumParameter p; EXPECT_EQ(OB_SUCCESS, p.set_int(n, arena)); return p;
  }
};

} // namespace
TEST(DynamicParamCapture, value_is_captured_from_sql_frame_not_old_value_payload) {
  Frame f;
  ObEvalCtx ctx(f.exec);
  ObDynamicParamSetter setter(0, nullptr, &f.expr);
  Datum &frame = f.expr.locate_datum_for_write(ctx);
  frame.set_int(42);
  DatumParameter binding1 = f.parameters();
  ASSERT_EQ(OB_SUCCESS, binding1.set_int(999, f.arena));
  binding1.set_precision(19);
  binding1.set_result_flag(0x1234);
  binding1.set_raw_text_info(3, 7);
  binding1.set_param_meta();
  binding1.set_precision(9);
  f.bind(binding1);
  DatumParameter old_binding;
  ASSERT_EQ(OB_SUCCESS, old_binding.deep_copy(f.parameters(), f.arena));
  ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, frame));
  DatumParameter view;
  ASSERT_EQ(OB_SUCCESS, f.capture(setter, ctx, view));
  EXPECT_EQ(42, view.datum_.get_int());
  EXPECT_EQ(999, old_binding.get_int());
  EXPECT_EQ(42, f.parameters().get_int());
  EXPECT_EQ(42, frame.get_int());
  EXPECT_EQ(9, view.get_precision());
  EXPECT_EQ(0x1234, view.get_result_flag());
  EXPECT_EQ(3, view.get_raw_text_pos());
  EXPECT_EQ(7, view.get_raw_text_len());
  EXPECT_EQ(f.parameters().get_param_meta(), view.get_param_meta());
  ObBatchRescanParams batch;
  ObBatchRescanParams::Row values(4 * sizeof(NativeTypedDatum));
  ObBatchRescanParams::MetadataRow attributes;
  ObSEArray<int64_t, 1> ids;
  ASSERT_EQ(OB_SUCCESS, ids.push_back(0));
  ASSERT_EQ(OB_SUCCESS, values.push_back(f.typed(view)));
  ASSERT_EQ(OB_SUCCESS, attributes.push_back(view.get_metadata()));
  ASSERT_EQ(OB_SUCCESS, batch.append_batch_rescan_param(ids, values, attributes));
  frame.set_int(88);
  EXPECT_EQ(42, batch.get_one_batch_values(0).at(0).datum_.get_int());
}
TEST(DynamicParamCapture, restoring_snapshot_does_not_evaluate_source_again) {
  Frame f;
  ObEvalCtx ctx(f.exec);
  ObDynamicParamSetter setter(0, nullptr, &f.expr);
  DatumParameter source = f.integer(INT64_MIN);
  source.set_precision(19);
  source.set_raw_text_info(17, 23);
  source.set_result_flags(0xa5);
  ASSERT_EQ(OB_SUCCESS, f.restore(setter, ctx, source));
  EXPECT_EQ(INT64_MIN, f.expr.locate_expr_datum(ctx).get_int());
  EXPECT_EQ(INT64_MIN, f.parameters().get_int());
  EXPECT_EQ(19, f.parameters().get_precision());
  EXPECT_EQ(0xa5, f.parameters().get_result_flag());
  EXPECT_EQ(17, f.parameters().get_raw_text_pos());
  EXPECT_EQ(23, f.parameters().get_raw_text_len());
  EXPECT_EQ(INT64_MIN, f.expr.locate_expr_datum(ctx).get_int());
}
TEST(DynamicParamCapture, typed_null_snapshot_clears_stale_param_meta_before_restore) {
  Frame f;
  ObEvalCtx ctx(f.exec);
  ObDynamicParamSetter setter(0, nullptr, &f.expr);
  DatumParameter binding2 = f.parameters();
  ASSERT_EQ(OB_SUCCESS, binding2.set_int(99, f.arena));
  binding2.set_param_meta();
  f.bind(binding2);
  DatumParameter source;
  ASSERT_EQ(OB_SUCCESS, f.restore(setter, ctx, source));
  EXPECT_TRUE(f.expr.locate_expr_datum(ctx).is_null());
  EXPECT_TRUE(f.parameters().is_null());
  EXPECT_TRUE(f.parameters().get_param_meta().is_null());
  SqlTypeMeta string_type;
  string_type.set_varchar();
  source.set_param_meta(string_type);
  source.set_length(65);
  ASSERT_EQ(OB_SUCCESS, f.restore(setter, ctx, source));
  EXPECT_EQ(string_type, f.parameters().get_param_meta());
  EXPECT_EQ(65, f.parameters().get_length());
}
TEST(DynamicParamCapture, invalid_snapshot_locations_preserve_output) {
  Frame f;
  ObEvalCtx ctx(f.exec);
  DatumParameter output = f.integer(7);
  for (int64_t idx : {int64_t(-1), int64_t(1)}) {
    ObDynamicParamSetter bad(idx, nullptr, &f.expr);
    EXPECT_EQ(OB_INVALID_ARGUMENT, f.capture(bad, ctx, output));
    EXPECT_EQ(7, output.datum_.get_int());
    EXPECT_EQ(OB_INVALID_ARGUMENT, f.restore(bad, ctx, output));
  }
  ObDynamicParamSetter missing(0, nullptr, nullptr);
  EXPECT_EQ(OB_INVALID_ARGUMENT, f.capture(missing, ctx, output));
  EXPECT_EQ(7, output.datum_.get_int());
}
TEST(DynamicParamCapture, px_worker_fills_expression_from_native_batch) {
  Frame f;
  ObBatchRescanParams batch;
  ObBatchRescanParams::Row row(4 * sizeof(NativeTypedDatum));
  ObBatchRescanParams::MetadataRow attributes;
  ObSEArray<int64_t, 1> ids, exprs;
  ASSERT_EQ(OB_SUCCESS, ids.push_back(0));
  ASSERT_EQ(OB_SUCCESS, exprs.push_back(1));
  ASSERT_EQ(OB_SUCCESS, row.push_back(f.typed(f.integer(123))));
  ASSERT_EQ(OB_SUCCESS, attributes.push_back(f.integer(123).get_metadata()));
  ASSERT_EQ(OB_SUCCESS, batch.append_batch_rescan_param(ids, row, attributes, exprs));
  ObSEArray<ObExpr, 1> expression_array;
  ASSERT_EQ(OB_SUCCESS, expression_array.push_back(f.expr));
  ASSERT_EQ(OB_SUCCESS, f.exec.fill_px_batch_info(batch, 0, expression_array));
  ObEvalCtx ctx(f.exec);
  EXPECT_EQ(123, f.expr.locate_expr_datum(ctx).get_int());
  EXPECT_EQ(123, f.parameters().get_int());
  EXPECT_TRUE(f.expr.get_eval_info(ctx).evaluated_);
  EXPECT_EQ(OB_ERR_UNEXPECTED, f.exec.fill_px_batch_info(batch, -1, expression_array));
}
TEST(DynamicParamCapture, native_lob_header_adjustment_keeps_mapped_cell_rules) {
  Frame f;
  SqlTypeMeta plain;
  plain.set_type(ObTextType);
  plain.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  SqlTypeMeta header = plain;
  header.set_has_lob_header();
  Datum source;
  source.set_string(ObString("raw payload"));
  Datum output(source);
  EXPECT_EQ(OB_SUCCESS, ob_adjust_lob_datum(f.exec, source, plain, plain, f.arena, output));
  EXPECT_EQ(source.get_string(), output.get_string());
  EXPECT_EQ(OB_ERR_UNEXPECTED, ob_adjust_lob_datum(f.exec, source, plain, header, f.arena, output));
  Datum null;
  null.set_null();
  EXPECT_EQ(OB_SUCCESS, ob_adjust_lob_datum(f.exec, null, header, plain, f.arena, output));
  const int64_t prefix = offsetof(ObLobCommon, buffer_);
  char *storage = static_cast<char *>(f.arena.alloc(prefix + source.length()));
  new (storage) ObLobCommon();
  memcpy(storage + prefix, source.get_reference(), source.length());
  Datum with_header;
  with_header.set_string(storage, prefix + source.length());
  output = with_header;
  ASSERT_EQ(OB_SUCCESS, ob_adjust_lob_datum(f.exec, with_header, header, plain, f.arena, output));
  EXPECT_EQ(ObString("raw payload"), output.get_string());
}

TEST(DynamicParamCapture, native_array_binding_captures_the_owned_envelope_and_restores_its_selected_element) {
  Frame f; ObEvalCtx ctx(f.exec);
  DatumParameter leaf = f.integer(42);
  auto *array = DatumParameterArray::alloc(f.arena, 1); ASSERT_NE(nullptr, array);
  array->data_[0] = leaf;
  ASSERT_EQ(OB_SUCCESS, array->set_element_type_borrowed(0, f.expr.get_sql_type(), f.arena));
  DatumParameter input;
  SqlTypeMeta array_meta; array_meta.set_ext(); array_meta.set_extend_type(T_EXT_SQL_ARRAY);
  input.set_type_meta(array_meta);
  ASSERT_EQ(OB_SUCCESS, input.set_array_address(array, f.arena));
  auto &plan = *f.exec.get_physical_plan_ctx();
  plan.reset_datum_param_store(); ASSERT_EQ(OB_SUCCESS, plan.reserve_param_space(1));
  ASSERT_EQ(OB_SUCCESS, plan.set_parameter_input(0, input));
  ASSERT_EQ(OB_SUCCESS, plan.init_datum_param_store());
  const DatumParameter *envelope = plan.get_group_parameter_input(0);
  ASSERT_NE(nullptr, envelope);
  auto *captured_array = envelope->get_array<DatumParameterArray>(); ASSERT_NE(nullptr, captured_array);
  EXPECT_NE(array, captured_array); EXPECT_EQ(42, captured_array->data_[0].get_int());
  ObDynamicParamSetter setter(0, nullptr, &f.expr);
  NativeTypedDatum captured; DatumParameterMetadata metadata;
  ASSERT_EQ(OB_SUCCESS, setter.capture_dynamic_param(ctx, captured, metadata));
  EXPECT_EQ(42, captured.datum_.get_int()); EXPECT_EQ(ObIntType, captured.type_->kind());
  EXPECT_EQ(ObIntType, metadata.type_meta_.get_type());
  ASSERT_EQ(OB_SUCCESS, setter.restore_dynamic_param(ctx, captured, metadata));
  EXPECT_EQ(42, f.expr.locate_expr_datum(ctx).get_int());
  EXPECT_EQ(42, read_parameter_value(plan, 0).get_int());
  ObBatchRescanParams batch; ObBatchRescanParams::RowBuilder row(batch);
  ObSEArray<int64_t, 1> ids, exprs;
  ASSERT_EQ(OB_SUCCESS, ids.push_back(0)); ASSERT_EQ(OB_SUCCESS, exprs.push_back(1));
  ASSERT_EQ(OB_SUCCESS, row.append(captured, metadata)); ASSERT_EQ(OB_SUCCESS, row.finish(ids, exprs));
  EXPECT_NE(captured.type_, batch.get_one_batch_values(0).at(0).type_);
  ObSEArray<ObExpr, 1> expressions; ASSERT_EQ(OB_SUCCESS, expressions.push_back(f.expr));
  ASSERT_EQ(OB_SUCCESS, f.exec.fill_px_batch_info(batch, 0, expressions));
  EXPECT_EQ(42, f.expr.locate_expr_datum(ctx).get_int());
  EXPECT_EQ(42, batch.get_one_batch_values(0).at(0).datum_.get_int());
  EXPECT_EQ(42, captured_array->data_[0].get_int());
}

TEST(DynamicParamCapture, native_runtime_binding_preserves_metadata_in_one_aliased_store) {
  Frame f;
  DatumParameter value = f.integer(42);
  value.set_precision(19);
  value.set_raw_text_info(3, 7);
  value.set_result_flags(0xa5);
  value.get_param_flag().flag_ = 0x5a;
  value.set_param_meta(value.get_type_meta());
  auto *plan = f.exec.get_physical_plan_ctx();
  ASSERT_EQ(OB_SUCCESS, f.bind_status(0, value));
  EXPECT_EQ(42, f.native().datum_.get_int());
  EXPECT_EQ(42, f.parameters().get_int());
  EXPECT_EQ(value.get_accuracy(), f.native().get_accuracy());
  EXPECT_EQ(value.get_accuracy(), f.parameters().get_accuracy());
  EXPECT_EQ(value.get_param_flag().flag_, f.native().get_param_flag().flag_);
  EXPECT_EQ(value.get_result_flag(), f.parameters().get_result_flag());
  EXPECT_EQ(3, f.native().get_raw_text_pos());
  EXPECT_EQ(7, f.parameters().get_raw_text_len());
  DatumParameter invalid;
  SqlTypeMeta meta;
  meta.set_type(ObMaxType);
  invalid.set_type_meta(meta);
  invalid.datum_ = value.datum_;
  EXPECT_NE(OB_SUCCESS, f.bind_status(0, invalid));
  EXPECT_EQ(42, f.native().datum_.get_int());
  EXPECT_EQ(42, f.parameters().get_int());
  EXPECT_EQ(OB_INVALID_ARGUMENT, f.bind_status(-1, value));
  EXPECT_EQ(OB_INVALID_ARGUMENT, f.bind_status(1, value));
  // There is one execution slot; construction inputs are retired, not aliased.
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_count(*plan));
  DatumParameter binding4 = f.parameters(); ASSERT_EQ(OB_SUCCESS, binding4.set_int(99, f.arena)); f.bind(binding4);
  const Datum *slot = materialized_parameter(*plan, 0);
  const SqlType *type = plan->get_native_parameter_type(0);
  const DatumParameterMetadata attributes = *plan->get_parameter_metadata(0);
  ASSERT_EQ(OB_SUCCESS, plan->set_runtime_parameter(0, *slot, *type, attributes));
  EXPECT_EQ(slot, materialized_parameter(*plan, 0)); EXPECT_EQ(type, plan->get_native_parameter_type(0));
  EXPECT_EQ(99, slot->get_int()); EXPECT_EQ(0, slot->length());
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_count(*plan));
}
TEST(DynamicParamCapture, native_runtime_null_keeps_declared_type_and_flags) {
  Frame f;
  DatumParameter source = f.integer(77);
  source.set_param_meta(source.get_type_meta());
  source.set_precision(19);
  source.set_scale(3);
  source.set_result_flags(0xa5);
  source.get_param_flag().flag_ = 0x5a;
  ASSERT_EQ(OB_SUCCESS, f.bind_status(0, source));
  DatumParameter expected = f.parameters();
  expected.set_null();
  source.set_null();
  ASSERT_EQ(OB_SUCCESS, f.bind_status(0, source));
  EXPECT_TRUE(f.native().is_null());
  EXPECT_TRUE(f.parameters().is_null());
  EXPECT_EQ(expected.get_meta(), f.native().get_type_meta());
  EXPECT_EQ(expected.get_meta(), f.parameters().get_meta());
  EXPECT_EQ(expected.get_param_meta(), f.native().get_param_meta());
  EXPECT_EQ(expected.get_accuracy(), f.native().get_accuracy());
  EXPECT_EQ(expected.get_result_flag(), f.parameters().get_result_flag());
  EXPECT_EQ(expected.get_param_flag().flag_, f.native().get_param_flag().flag_);
}
TEST(DynamicParamCapture, updated_frame_and_native_store_stay_in_sync) {
  Frame f;
  ObEvalCtx ctx(f.exec);
  ObDynamicParamSetter setter(0, nullptr, &f.expr);
  DatumParameter initial = f.integer(7);
  initial.set_precision(19);
  initial.set_param_meta();
  ASSERT_EQ(OB_SUCCESS, f.bind_status(0, initial));
  auto input = f.integer(42);
  ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, f.native_value(input)));
  EXPECT_EQ(42, f.native().datum_.get_int());
  EXPECT_EQ(42, f.parameters().get_int());
  EXPECT_EQ(f.expr.locate_expr_datum(ctx).get_int(), read_parameter_value(*f.exec.get_physical_plan_ctx(), 0).get_int());
  DatumParameter saved;
  ASSERT_EQ(OB_SUCCESS, saved.deep_copy(f.native(), f.arena));
  input.datum_.set_int(88);
  ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, f.native_value(input)));
  EXPECT_EQ(42, saved.datum_.get_int());
  EXPECT_EQ(88, f.native().datum_.get_int());
  ASSERT_EQ(OB_SUCCESS, f.restore(setter, ctx, saved));
  EXPECT_EQ(42, f.native().datum_.get_int());
  EXPECT_EQ(42, f.expr.locate_expr_datum(ctx).get_int());
}
TEST(DynamicParamCapture, owned_runtime_backup_survives_rebinding_and_restore_scope) {
  Frame f;
  ObEvalCtx ctx(f.exec);
  ObDynamicParamSetter setter(0, nullptr, &f.expr);
  DatumParameter saved;
  {
    auto input = f.integer(42);
    ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, f.native_value(input)));
    ASSERT_EQ(OB_SUCCESS, saved.deep_copy(f.native(), f.exec.get_allocator()));
    input.datum_.set_int(88);
    ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, f.native_value(input)));
    ASSERT_EQ(OB_SUCCESS, f.bind_status(0, saved));
  }
  EXPECT_EQ(42, f.native().datum_.get_int());
  EXPECT_EQ(42, f.parameters().get_int());
  // A group range guard restores bindings without modifying SQL expression frames.
  EXPECT_EQ(88, f.expr.locate_expr_datum(ctx).get_int());
}
TEST(DynamicParamCapture, native_update_preserves_value_type_and_slot_metadata) {
  Frame f;
  ObEvalCtx ctx(f.exec);
  ObDynamicParamSetter setter(0, nullptr, &f.expr);
  DatumParameter binding5 = f.parameters();
  ASSERT_EQ(OB_SUCCESS, binding5.set_int(123, f.arena));
  binding5.set_precision(19);
  binding5.set_param_meta();
  binding5.set_result_flag(0xa5);
  binding5.get_param_flag().flag_ = 0x5a;
  binding5.set_raw_text_info(17, 23);
  f.bind(binding5);

  Datum input;
  input.set_int(-987654321);
  ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, input));
  EXPECT_EQ(-987654321, f.parameters().datum_.get_int());
  EXPECT_EQ(ObIntType, f.parameters().get_type_meta().get_type());
  EXPECT_EQ(ObIntType, f.parameters().get_param_meta().get_type());
  EXPECT_EQ(0xa5, f.parameters().get_result_flag());
  EXPECT_EQ(0x5a, f.parameters().get_param_flag().flag_);
  EXPECT_EQ(17, f.parameters().get_raw_text_pos());
  EXPECT_EQ(23, f.parameters().get_raw_text_len());
  EXPECT_EQ(-987654321, f.native().datum_.get_int());

  // A NULL update clears the active value type while retaining the declared
  // parameter type and slot-level result metadata.
  input.set_null();
  ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, input));
  EXPECT_TRUE(f.parameters().is_null());
  EXPECT_EQ(ObNullType, f.parameters().get_type_meta().get_type());
  EXPECT_EQ(ObIntType, f.parameters().get_param_meta().get_type());
  EXPECT_EQ(0xa5, f.parameters().get_result_flag());
  EXPECT_EQ(0x5a, f.parameters().get_param_flag().flag_);

  // A Decimal update uses its complete target accuracy rather than the old
  // integer envelope. The stack coefficient must not escape into the slot.
  SqlTypeMeta decimal_meta; decimal_meta.set_decimal_int(5);
  f.expr.obj_meta_ = decimal_meta;
  f.expr.datum_meta_.type_ = ObDecimalIntType;
  ASSERT_EQ(OB_SUCCESS, f.expr.prepare_scalar_type(decimal_meta, ObAccuracy(19, 5), f.arena));
  const int128_t coefficient = -12345;
  ASSERT_EQ(OB_SUCCESS, input.set_reference(&coefficient, sizeof(coefficient)));
  ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, input));
  EXPECT_EQ(ObDecimalIntType, f.parameters().get_param_meta().get_type());
  EXPECT_EQ(ObAccuracy(19, 5), f.parameters().get_accuracy());
  EXPECT_NE(&coefficient, read_parameter_value(*f.exec.get_physical_plan_ctx(), 0).get_reference());
  int128_t actual = 0;
  ASSERT_EQ(OB_SUCCESS, read_parameter_value(*f.exec.get_physical_plan_ctx(), 0).get_decimal_int128(actual));
  EXPECT_TRUE(coefficient == actual);
  input.set_null();
  ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, input));
  EXPECT_TRUE(f.parameters().is_null());
  EXPECT_EQ(ObDecimalIntType, f.parameters().get_param_meta().get_type());
  EXPECT_EQ(ObAccuracy(19, 5), f.parameters().get_accuracy());
}

TEST(DynamicParamCapture, execution_slots_grow_after_input_frames_are_initialized) {
  Frame f;
  DatumParameter binding6 = f.parameters();
  ASSERT_EQ(OB_SUCCESS, binding6.set_int(42, f.arena));
  binding6.set_precision(19);
  binding6.set_param_meta();
  f.bind(binding6);
  auto *plan = f.exec.get_physical_plan_ctx();
  ASSERT_EQ(OB_SUCCESS, plan->init_datum_param_store());
  const Datum *input_slot = materialized_parameter(*plan, 0);
  const int64_t input_frame_count = plan->get_param_frame_ptrs().count();
  ASSERT_EQ(OB_SUCCESS, plan->reserve_param_space(2049));
  EXPECT_EQ(2049, plan->get_parameter_count());
  EXPECT_EQ(2049, plan->get_parameter_count());
  EXPECT_EQ(42, f.native().datum_.get_int());
  EXPECT_EQ(input_slot, materialized_parameter(*plan, 0));
  EXPECT_GT(plan->get_param_frame_ptrs().count(), input_frame_count);
  EXPECT_TRUE(parameter_snapshot(*plan, 2048, f.arena).is_null());
  ObEvalCtx ctx(f.exec);
  ObDynamicParamSetter setter(2048, nullptr, &f.expr);
  auto value = f.integer(77);
  ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, f.native_value(value)));
  EXPECT_EQ(77, parameter_snapshot(*plan, 2048, f.arena).datum_.get_int());
  EXPECT_EQ(77, parameter_snapshot(*plan, 2048, f.arena).get_int());
  ASSERT_EQ(OB_SUCCESS, plan->reserve_param_space(2));
  EXPECT_EQ(2049, plan->get_parameter_count());
  EXPECT_EQ(77, parameter_snapshot(*plan, 2048, f.arena).datum_.get_int());
}

TEST(DynamicParamCapture, both_store_accessors_share_reset_and_reserve_state) {
  Frame f; auto *plan = f.exec.get_physical_plan_ctx();
  EXPECT_TRUE(plan->native_parameters_ready()); EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_count(*plan));
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_bytes(*plan));
  plan->reset_datum_param_store(); EXPECT_EQ(0, plan->get_parameter_count());
  EXPECT_FALSE(plan->native_parameters_ready());
  ASSERT_EQ(OB_SUCCESS, plan->reserve_param_space(5)); EXPECT_EQ(5, plan->get_parameter_count());
  EXPECT_GT(NativeExecutionParametersTestPeer::input_bytes(*plan), 0);
  for (int i = 0; i < 5; ++i) { EXPECT_TRUE(plan->get_parameter_inputs().at(i).is_null()); }
  ASSERT_EQ(OB_SUCCESS, plan->init_datum_param_store());
  EXPECT_TRUE(plan->native_parameters_ready()); EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_count(*plan));
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_bytes(*plan));
  for (int i = 0; i < 5; ++i) { ASSERT_NE(nullptr, materialized_parameter(*plan, i)); EXPECT_TRUE(read_parameter_value(*plan, i).is_null()); }
  DatumParameter binding7 = f.parameters(); ASSERT_EQ(OB_SUCCESS, binding7.set_int(42, f.arena));
  binding7.set_precision(19); binding7.set_raw_text_info(17, 23); f.bind(binding7);
  ASSERT_EQ(OB_SUCCESS, plan->reserve_param_space(7));
  EXPECT_EQ(42, read_parameter_value(*plan, 0).get_int());
  EXPECT_EQ(19, plan->get_parameter_metadata(0)->accuracy_.get_precision());
  EXPECT_EQ(17, plan->get_parameter_metadata(0)->raw_text_pos_); EXPECT_EQ(23, plan->get_parameter_metadata(0)->raw_text_len_);
  EXPECT_EQ(OB_INVALID_ARGUMENT, plan->reserve_param_space(-1)); EXPECT_EQ(7, plan->get_parameter_count());
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_count(*plan));
}

namespace {
class RejectParameterGrowth : public ObIAllocator {
public:
  ObArenaAllocator arena_;
  bool reject_ = false;
  void *alloc(int64_t bytes) override { return reject_ ? nullptr : arena_.alloc(bytes); }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *pointer) override { arena_.free(pointer); }
};
}
TEST(DynamicParamCapture, failed_native_store_growth_preserves_existing_bindings) {
  RejectParameterGrowth allocator;
  ObPhysicalPlanCtx plan(allocator);
  ASSERT_EQ(OB_SUCCESS, plan.reserve_param_space(1));
  DatumParameter initial; ASSERT_EQ(OB_SUCCESS, initial.set_int(42, allocator));
  ASSERT_EQ(OB_SUCCESS, plan.set_parameter_input(0, initial));
  ASSERT_EQ(OB_SUCCESS, plan.init_datum_param_store());
  const Datum *prefix = materialized_parameter(plan, 0);
  const int64_t large = 16384; // Several final scalar24 pages, independent of removed wrapper blocks.
  allocator.reject_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan.reserve_param_space(large));
  EXPECT_EQ(1, plan.get_parameter_count()); EXPECT_EQ(prefix, materialized_parameter(plan, 0));
  EXPECT_EQ(42, read_parameter_value(plan, 0).get_int());
  allocator.reject_ = false; ASSERT_EQ(OB_SUCCESS, plan.reserve_param_space(large));
  EXPECT_EQ(large, plan.get_parameter_count()); EXPECT_EQ(42, read_parameter_value(plan, 0).get_int());
  EXPECT_TRUE(read_parameter_value(plan, large - 1).is_null()); EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_count(plan));
}

TEST(DynamicParamCapture, output_destinations_are_references_without_value_objects) {
  ObSPIOutParams params;
  ASSERT_EQ(OB_SUCCESS, params.push_back(OutParameterRef()));
  EXPECT_FALSE(params.has_out_param());
  ASSERT_EQ(OB_SUCCESS, params.push_back(OutParameterRef::local(0)));
  ASSERT_EQ(OB_SUCCESS, params.push_back(OutParameterRef::field(INT64_MAX)));
  EXPECT_TRUE(params.has_out_param());
  ASSERT_EQ(3, params.get_out_params().count());
  EXPECT_TRUE(params.get_out_params().at(0).is_none());
  EXPECT_TRUE(params.get_out_params().at(1).is_local());
  EXPECT_EQ(0, params.get_out_params().at(1).index());
  EXPECT_TRUE(params.get_out_params().at(2).is_field());
  EXPECT_EQ(INT64_MAX, params.get_out_params().at(2).address());
  params.reset(); EXPECT_FALSE(params.has_out_param());
  EXPECT_EQ(0, params.get_out_params().count());
}

namespace {
oceanbase::pl::ObPLRecord *make_parameter_record(ObIAllocator &allocator, int64_t value) {
  using namespace oceanbase::pl;
  void *memory = allocator.alloc(ObRecordType::get_init_size(1));
  if (memory == nullptr) { return nullptr; }
  auto *record = new (memory) ObPLRecord(42, 1);
  if (record->init_data(allocator, true) != OB_SUCCESS) { return nullptr; }
  new (record->get_element_type()) ObDataType();
  SqlTypeMeta meta; meta.set_int();
  record->get_element_type()[0].set_meta_type(meta);
  if (record->get_element()[0].set_int(value, *record->get_allocator()) != OB_SUCCESS) { return nullptr; }
  return record;
}
}
TEST(DynamicParamCapture, pl_execution_snapshot_clones_aliased_record_and_keeps_parameter_attributes) {
  using namespace oceanbase::pl;
  ObArenaAllocator arena;
  auto *record = make_parameter_record(arena, 7);
  ASSERT_NE(nullptr, record);
  DatumParameter source;
  ASSERT_EQ(OB_SUCCESS, source.set_extend(reinterpret_cast<int64_t>(record),
                                          PL_RECORD_TYPE, arena, record->get_init_size()));
  source.set_udt_id(42); source.set_param_meta();
  source.set_result_flag(0xa5); source.set_raw_text_info(3, 17);
  source.get_param_flag().flag_ = 0x5a;
  DatumParameter snapshot = source;
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::clone_parameter(arena, source, snapshot));
  ASSERT_NE(source.datum_.get_reference(), snapshot.datum_.get_reference());
  ASSERT_NE(source.get_ext(), snapshot.get_ext());
  auto *copy = reinterpret_cast<ObPLRecord *>(snapshot.get_ext());
  ASSERT_NE(nullptr, copy);
  EXPECT_EQ(7, copy->get_element()[0].get_int());
  EXPECT_EQ(source.get_accuracy(), snapshot.get_accuracy());
  EXPECT_EQ(source.get_param_meta(), snapshot.get_param_meta());
  EXPECT_EQ(source.get_param_flag().flag_, snapshot.get_param_flag().flag_);
  EXPECT_EQ(source.get_result_flag(), snapshot.get_result_flag());
  EXPECT_EQ(3, snapshot.get_raw_text_pos()); EXPECT_EQ(17, snapshot.get_raw_text_len());
  ASSERT_EQ(OB_SUCCESS, record->get_element()[0].set_int(99, *record->get_allocator()));
  EXPECT_EQ(7, copy->get_element()[0].get_int());
  const int64_t identity = snapshot.get_ext();
  snapshot.set_raw_text_info(5, 23);
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::deep_copy_obj(arena, source, snapshot));
  EXPECT_EQ(identity, snapshot.get_ext());
  EXPECT_EQ(99, copy->get_element()[0].get_int());
  EXPECT_EQ(5, snapshot.get_raw_text_pos()); EXPECT_EQ(23, snapshot.get_raw_text_len());
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(arena, snapshot));
  EXPECT_TRUE(snapshot.is_null()); EXPECT_EQ(0xa5, snapshot.get_result_flag());
  EXPECT_EQ(0x5a, snapshot.get_param_flag().flag_);
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(arena, source));
}

TEST(DynamicParamCapture, failed_pl_snapshot_preserves_previous_binding_and_source) {
  using namespace oceanbase::pl;
  ObArenaAllocator source_arena;
  auto *record = make_parameter_record(source_arena, 7);
  ASSERT_NE(nullptr, record);
  DatumParameter source, previous;
  ASSERT_EQ(OB_SUCCESS, source.set_extend(reinterpret_cast<int64_t>(record),
                                          PL_RECORD_TYPE, source_arena, record->get_init_size()));
  ASSERT_EQ(OB_SUCCESS, previous.set_int(55, source_arena));
  previous.set_precision(19);
  const Datum original = previous.datum_;
  RejectParameterGrowth allocator; allocator.reject_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
            ObUserDefinedType::clone_parameter(allocator, source, previous));
  EXPECT_EQ(original.get_int(), previous.datum_.get_int()); EXPECT_EQ(55, previous.get_int());
  EXPECT_EQ(19, previous.get_precision()); EXPECT_EQ(7, record->get_element()[0].get_int());
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(source_arena, source));
}

TEST(DynamicParamCapture, package_values_own_sql_payloads_and_preserve_parameter_declarations) {
  using namespace oceanbase::pl;
  ObPLPackageState state(42, ObPackageStateVersion(1, 1));
  ASSERT_EQ(OB_SUCCESS, state.init());
  DatumParameter empty;
  ASSERT_EQ(OB_SUCCESS, state.add_package_var_val(empty, PL_OBJ_TYPE));
  ASSERT_EQ(OB_SUCCESS, state.add_package_var_val(empty, PL_OBJ_TYPE));
  EXPECT_EQ(sizeof(DatumValue), sizeof(state.get_vars().at(0)));
  EXPECT_EQ(24, sizeof(DatumValue));
  ObArenaAllocator transient;
  DatumParameter integer, text;
  ASSERT_EQ(OB_SUCCESS, integer.set_int(42, transient));
  ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString("package payload"), transient));
  text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(0, integer));
  ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(1, text));
  EXPECT_EQ(0, integer.datum_.length()); EXPECT_EQ(0, state.get_vars().at(0).datum_.length());
  EXPECT_NE(text.datum_.get_reference(), state.get_vars().at(1).datum_.get_reference());
  integer.datum_.set_int(99);
  transient.reset();
  DatumParameter observed;
  observed.set_precision(27); observed.set_result_flags(0x1212);
  SqlTypeMeta declared; declared.set_uint64(); observed.set_param_meta(declared);
  ASSERT_EQ(OB_SUCCESS, state.get_package_var_val(0, observed));
  EXPECT_EQ(42, observed.get_int()); EXPECT_EQ(27, observed.get_precision());
  EXPECT_EQ(0x1212, observed.get_result_flag()); EXPECT_EQ(declared, observed.get_param_meta());
  ASSERT_EQ(OB_SUCCESS, state.get_package_var_val(1, observed));
  EXPECT_EQ(ObString("package payload"), observed.get_string());
  EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, observed.get_collation_type());
  state.reset(nullptr);
}

TEST(DynamicParamCapture, package_self_assignment_and_failed_clone_do_not_leave_dangling_values) {
  using namespace oceanbase::pl;
  ObPLPackageState state(42, ObPackageStateVersion(1, 1)); ASSERT_EQ(OB_SUCCESS, state.init());
  DatumParameter empty;
  ASSERT_EQ(OB_SUCCESS, state.add_package_var_val(empty, PL_OBJ_TYPE));
  ObArenaAllocator arena;
  DatumParameter input; ASSERT_EQ(OB_SUCCESS, input.set_int(17, arena));
  ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(0, input));
  DatumParameter borrowed; ASSERT_EQ(OB_SUCCESS, state.get_package_var_val(0, borrowed));
  const Datum *previous = &state.get_vars().at(0).datum_;
  ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(0, borrowed));
  ASSERT_EQ(OB_SUCCESS, state.get_package_var_val(0, borrowed));
  EXPECT_EQ(17, borrowed.get_int());
  DatumParameter invalid;
  SqlTypeMeta array_meta; array_meta.set_ext(); array_meta.set_extend_type(T_EXT_SQL_ARRAY);
  invalid.set_type_meta(array_meta);
  ASSERT_EQ(OB_SUCCESS, invalid.set_array_address<DatumParameterArray>(nullptr, arena));
  previous = &state.get_vars().at(0).datum_;
  EXPECT_EQ(OB_INVALID_ARGUMENT, state.set_package_var_val(0, invalid));
  ASSERT_EQ(OB_SUCCESS, state.get_package_var_val(0, borrowed));
  EXPECT_EQ(17, borrowed.get_int()); EXPECT_EQ(previous, &state.get_vars().at(0).datum_);
  EXPECT_EQ(OB_ARRAY_OUT_OF_RANGE, state.set_package_var_val(-1, input));
  state.reset(nullptr);
}

TEST(DynamicParamCapture, package_cursor_adoption_owns_descriptor_and_preserves_handle) {
  using namespace oceanbase::pl;
  ObPLPackageState state(42, ObPackageStateVersion(1, 1)); ASSERT_EQ(OB_SUCCESS, state.init());
  DatumParameter empty, cursor;
  ASSERT_EQ(OB_SUCCESS, state.add_package_var_val(empty, PL_CURSOR_TYPE));
  ASSERT_EQ(OB_SUCCESS, ObSPIService::spi_cursor_alloc(state.get_pkg_cursor_allocator(), cursor));
  const int64_t handle = cursor.get_ext();
  ASSERT_TRUE(cursor.datum_.is_inline_extended());
  const SqlTypeMeta cursor_meta = cursor.get_type_meta();
  ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(0, cursor, false));
  ASSERT_TRUE(state.get_vars().at(0).datum_.is_inline_extended());
  EXPECT_EQ(handle, state.get_vars().at(0).get_ext());
  // There is no allocated wrapper to free. get_reference() would expose the
  // actual cursor handle here; its pointee remains owned by package reset().
  cursor.set_null();
  DatumParameter observed; ASSERT_EQ(OB_SUCCESS, state.get_package_var_val(0, observed));
  ASSERT_TRUE(observed.datum_.is_inline_extended());
  EXPECT_EQ(handle, observed.get_ext());
  EXPECT_EQ(cursor_meta, observed.get_type_meta());
  EXPECT_EQ(PL_CURSOR_TYPE, observed.get_extend_type());
  ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(0, observed));
  ASSERT_EQ(OB_SUCCESS, state.get_package_var_val(0, observed));
  EXPECT_TRUE(observed.datum_.is_inline_extended());
  EXPECT_EQ(handle, observed.get_ext());
  EXPECT_EQ(cursor_meta, observed.get_type_meta());
  state.reset(nullptr);
}

TEST(DynamicParamCapture, package_null_record_assignment_preserves_record_identity) {
  using namespace oceanbase::pl;
  ObPLPackageState state(42, ObPackageStateVersion(1, 1)); ASSERT_EQ(OB_SUCCESS, state.init());
  DatumParameter empty, record_value;
  ASSERT_EQ(OB_SUCCESS, state.add_package_var_val(empty, PL_RECORD_TYPE));
  auto *record = make_parameter_record(state.get_pkg_allocator(), 7); ASSERT_NE(nullptr, record);
  ASSERT_EQ(OB_SUCCESS, record_value.set_extend(reinterpret_cast<int64_t>(record), PL_RECORD_TYPE,
                                               state.get_pkg_allocator(), record->get_init_size()));
  ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(0, record_value, false));
  ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(0, empty));
  DatumParameter observed; ASSERT_EQ(OB_SUCCESS, state.get_package_var_val(0, observed));
  EXPECT_EQ(reinterpret_cast<int64_t>(record), observed.get_ext());
  EXPECT_TRUE(record->get_element()[0].is_null());
  state.reset(nullptr);
}

TEST(DynamicParamCapture, package_scalar_defaults_keep_raw_zero_and_metadata_semantics) {
  using namespace oceanbase::pl;
  ObArenaAllocator arena;
  ObSQLSessionInfo session;
  oceanbase::share::schema::ObSchemaGetterGuard schema;
  ObPLPackageGuard guard;
  ObMySQLProxy proxy;
  ObPLResolveCtx resolve(arena, session, schema, guard, proxy, false);
  ObExecContext exec(arena);
  for (SqlValueType type : {ObNullType, ObIntType, ObUInt64Type, ObFloatType, ObDoubleType,
                        ObNumberType, ObDecimalIntType, ObVarcharType, ObDateType,
                        ObMySQLDateType, ObMySQLDateTimeType}) {
    ObPLDataType declaration(type);
    DatumParameter value;
    ASSERT_EQ(OB_SUCCESS, declaration.init_session_var(resolve, arena, exec, nullptr, false, value));
    EXPECT_EQ(declaration.get_data_type()->get_meta_type(), value.get_meta());
    switch (type) {
      case ObNullType: EXPECT_TRUE(value.is_null()); break;
      case ObIntType: EXPECT_EQ(0, value.get_int()); break;
      case ObUInt64Type: EXPECT_EQ(0, value.get_uint64()); break;
      case ObFloatType: EXPECT_EQ(0.0f, value.get_float()); break;
      case ObDoubleType: EXPECT_EQ(0.0, value.get_double()); break;
      case ObNumberType: EXPECT_TRUE(value.get_number().is_zero()); break;
      case ObDecimalIntType: EXPECT_EQ(0, value.get_int_bytes()); break;
      case ObVarcharType: EXPECT_EQ(0, value.get_string_len()); break;
      case ObDateType: EXPECT_EQ(0, value.get_date()); break;
      case ObMySQLDateType: EXPECT_EQ(ObMySQLDate(0), value.get_mysql_date()); break;
      case ObMySQLDateTimeType: EXPECT_EQ(ObMySQLDateTime(0), value.get_mysql_datetime()); break;
      default: FAIL(); break;
    }
  }
}

TEST(DynamicParamCapture, package_default_allocation_failure_preserves_previous_output) {
  using namespace oceanbase::pl;
  ObArenaAllocator arena;
  ObSQLSessionInfo session;
  oceanbase::share::schema::ObSchemaGetterGuard schema;
  ObPLPackageGuard guard;
  ObMySQLProxy proxy;
  ObPLResolveCtx resolve(arena, session, schema, guard, proxy, false);
  ObExecContext exec(arena);
  RejectParameterGrowth reject; reject.reject_ = true;
  DatumParameter value; ASSERT_EQ(OB_SUCCESS, value.set_int(51, arena));
  value.set_precision(19); const Datum previous = value.datum_;
  ObPLDataType declaration(ObNumberType);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, declaration.init_session_var(resolve, reject, exec, nullptr, false, value));
  EXPECT_EQ(previous.get_int(), value.datum_.get_int()); EXPECT_EQ(51, value.get_int()); EXPECT_EQ(19, value.get_precision());
  ObPLDataType integer_declaration(ObIntType);
  DatumParameter inline_zero;
  EXPECT_EQ(OB_SUCCESS, integer_declaration.init_session_var(resolve, reject, exec, nullptr, false, inline_zero));
  EXPECT_EQ(0, inline_zero.get_int()); EXPECT_EQ(0, inline_zero.datum_.length());
}

TEST(DynamicParamCapture, repeated_package_scalar_assignment_reuses_owned_payload) {
  using namespace oceanbase::pl;
  ObPLPackageState state(42, ObPackageStateVersion(1, 1)); ASSERT_EQ(OB_SUCCESS, state.init());
  DatumParameter empty;
  ASSERT_EQ(OB_SUCCESS, state.add_package_var_val(empty, PL_OBJ_TYPE));
  ObArenaAllocator arena;
  DatumParameter input; ASSERT_EQ(OB_SUCCESS, input.set_int(0, arena));
  ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(0, input));
  const Datum *owned = &state.get_vars().at(0).datum_;
  auto &allocator = static_cast<ObPLAllocator1 &>(state.get_pkg_allocator());
  const int64_t used = allocator.get_used();
  for (int64_t i = 1; i <= 1000; ++i) {
    input.datum_.set_int(i);
    ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(0, input));
    ASSERT_EQ(owned, &state.get_vars().at(0).datum_);
  }
  EXPECT_EQ(used, allocator.get_used());
  DatumParameter observed; ASSERT_EQ(OB_SUCCESS, state.get_package_var_val(0, observed));
  EXPECT_EQ(1000, observed.get_int());
  input.datum_.set_int(-1); EXPECT_EQ(1000, observed.get_int());
  state.reset(nullptr);
}

TEST(DynamicParamCapture, malformed_fixed_package_value_preserves_previous_value) {
  using namespace oceanbase::pl;
  ObPLPackageState state(42, ObPackageStateVersion(1, 1)); ASSERT_EQ(OB_SUCCESS, state.init());
  DatumParameter empty;
  ASSERT_EQ(OB_SUCCESS, state.add_package_var_val(empty, PL_OBJ_TYPE));
  ObArenaAllocator arena;
  DatumParameter input; ASSERT_EQ(OB_SUCCESS, input.set_int(25, arena));
  ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(0, input));
  const Datum *owned = &state.get_vars().at(0).datum_;
  char truncated = 3;
  input.datum_.set_string(&truncated, 1);
  EXPECT_EQ(OB_INVALID_ARGUMENT, state.set_package_var_val(0, input));
  EXPECT_EQ(owned, &state.get_vars().at(0).datum_);
  DatumParameter observed; ASSERT_EQ(OB_SUCCESS, state.get_package_var_val(0, observed));
  EXPECT_EQ(25, observed.get_int());
  state.reset(nullptr);
}

TEST(DynamicParamCapture, native_out_parameter_scalar_snapshot_keeps_caller_declaration) {
  ObArenaAllocator caller;
  DatumParameter output;
  ASSERT_EQ(OB_SUCCESS, output.set_int(7, caller));
  output.set_precision(19); output.set_param_meta(); output.set_result_flag(123);
  output.set_raw_text_info(2, 9);
  {
    ObArenaAllocator callee;
    DatumParameter source;
    ASSERT_EQ(OB_SUCCESS, source.set_varchar(ObString::make_string("owned"), callee));
    ASSERT_EQ(OB_SUCCESS, oceanbase::pl::ObUserDefinedType::copy_out_parameter(caller, source, output));
    EXPECT_NE(source.datum_.get_reference(), output.datum_.get_reference());
  }
  EXPECT_EQ(ObString::make_string("owned"), output.get_string());
  EXPECT_EQ(19, output.get_precision()); EXPECT_EQ(123, output.get_result_flag());
  EXPECT_EQ(ObIntType, output.get_param_meta().get_type());
  EXPECT_EQ(2, output.get_raw_text_pos()); EXPECT_EQ(9, output.get_raw_text_len());
}

TEST(DynamicParamCapture, native_out_parameter_shared_composite_copies_descriptor_only) {
  using namespace oceanbase::pl;
  RejectParameterGrowth caller;
  DatumParameter output;
  const int64_t handle = 0x1234; // opaque identity; this path must not dereference it
  ASSERT_EQ(OB_SUCCESS, output.set_extend(handle, PL_RECORD_TYPE, caller, 64));
  {
    ObArenaAllocator callee;
    DatumParameter source;
    ASSERT_EQ(OB_SUCCESS, source.set_extend(handle, PL_RECORD_TYPE, callee, 64));
    source.set_udt_id(456); source.set_param_meta();
    source.set_result_flags(0x72); source.set_raw_text_info(2, 9);
    source.set_need_to_check_type(true);
    ASSERT_TRUE(source.datum_.is_inline_extended());
    caller.reject_ = true; // A by-value carrier copy needs no wrapper allocation.
    ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::copy_out_parameter(caller, source, output));
    ASSERT_TRUE(output.datum_.is_inline_extended());
    EXPECT_EQ(source.get_type_meta(), output.get_type_meta());
    EXPECT_EQ(source.get_param_meta(), output.get_param_meta());
    EXPECT_EQ(source.get_accuracy(), output.get_accuracy());
    EXPECT_EQ(0x72, output.get_result_flag());
    EXPECT_EQ(2, output.get_raw_text_pos()); EXPECT_EQ(9, output.get_raw_text_len());
    EXPECT_TRUE(output.need_to_check_type());
    source.set_null(); // Retiring the callee's carrier must not alter the output.
  }
  EXPECT_EQ(handle, output.get_ext()); EXPECT_EQ(64, output.get_val_len());
  EXPECT_EQ(PL_RECORD_TYPE, output.get_extend_type());
  EXPECT_EQ(456, output.get_udt_id());
}

TEST(DynamicParamCapture, native_out_parameter_self_copy_and_oom_are_transactional) {
  RejectParameterGrowth owner;
  DatumParameter value; ASSERT_EQ(OB_SUCCESS, value.set_int(42, owner));
  owner.reject_ = true;
  ASSERT_EQ(OB_SUCCESS, oceanbase::pl::ObUserDefinedType::copy_out_parameter(owner, value, value));
  EXPECT_EQ(42, value.get_int()); EXPECT_EQ(0, value.datum_.length());
  owner.reject_ = false;
  ASSERT_EQ(OB_SUCCESS, value.set_varchar(ObString::make_string("self-copy-42"), owner));
  const void *previous = value.datum_.get_reference(); owner.reject_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
      oceanbase::pl::ObUserDefinedType::copy_out_parameter(owner, value, value));
  EXPECT_EQ(ObString("self-copy-42"), value.get_string()); EXPECT_EQ(previous, value.datum_.get_reference());
  owner.reject_ = false;
  ASSERT_EQ(OB_SUCCESS, oceanbase::pl::ObUserDefinedType::copy_out_parameter(owner, value, value));
  EXPECT_EQ(ObString("self-copy-42"), value.get_string()); EXPECT_NE(previous, value.datum_.get_reference());
}

TEST(DynamicParamCapture, compact_record_row_assignment_owns_source_bytes_and_is_self_safe) {
  using namespace oceanbase::pl;
  ObArenaAllocator owner;
  auto *record = make_parameter_record(owner, 7); ASSERT_NE(nullptr, record);
  {
    ObArenaAllocator temporary;
    DatumParameter text; ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString::make_string("record"), temporary));
    text.get_meta().set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ObSEArray<DatumValue, 1> row; ASSERT_EQ(OB_SUCCESS, row.push_back(text.value_view()));
    ASSERT_EQ(OB_SUCCESS, record->set_data(row));
    EXPECT_NE(text.datum_.get_reference(), record->get_element()[0].datum_.get_reference());
  }
  EXPECT_EQ(ObString::make_string("record"), record->get_element()[0].get_string());
  DatumValue &field = record->get_element()[0];
  const void *old = field.datum_.get_reference();
  ASSERT_EQ(OB_SUCCESS, ObPLComposite::copy_element(field, field, *record->get_allocator(),
      nullptr, nullptr, nullptr, nullptr, nullptr, false));
  EXPECT_EQ(old, field.datum_.get_reference());
  DatumValue root; ASSERT_EQ(OB_SUCCESS, root.set_extend(reinterpret_cast<int64_t>(record), PL_RECORD_TYPE, owner));
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(owner, root, nullptr));
}
TEST(DynamicParamCapture, compact_record_count_mismatch_cannot_overwrite_member_capacity) {
  using namespace oceanbase::pl;
  ObArenaAllocator owner;
  auto *record = make_parameter_record(owner, 7); ASSERT_NE(nullptr, record);
  const int64_t bytes = ObRecordType::get_init_size(2);
  auto *other = new (owner.alloc(bytes)) ObPLRecord(42, 2);
  ASSERT_EQ(OB_SUCCESS, other->init_data(owner, true));
  EXPECT_NE(OB_SUCCESS, record->assign(other, &owner));
  EXPECT_EQ(1, record->get_count()); EXPECT_EQ(7, record->get_element()[0].get_int());
  DatumValue first, second;
  ASSERT_EQ(OB_SUCCESS, first.set_extend(reinterpret_cast<int64_t>(record), PL_RECORD_TYPE, owner));
  ASSERT_EQ(OB_SUCCESS, second.set_extend(reinterpret_cast<int64_t>(other), PL_RECORD_TYPE, owner));
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(owner, first, nullptr));
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(owner, second, nullptr));
}

TEST(DynamicParamCapture, field_out_reference_carries_actual_owner_and_identity_only_ref_has_none) {
  ObArenaAllocator statement_owner, record_owner;
  DatumValue field;
  ASSERT_EQ(OB_SUCCESS, field.set_int(42, record_owner));
  const int64_t address = reinterpret_cast<int64_t>(&field);
  const OutParameterRef writable = OutParameterRef::field(address, &record_owner);
  EXPECT_EQ(&record_owner, writable.owner()); EXPECT_NE(&statement_owner, writable.owner());
  EXPECT_EQ(address, writable.address()); EXPECT_TRUE(writable.is_field());
  const OutParameterRef identity = OutParameterRef::field(address);
  EXPECT_EQ(nullptr, identity.owner()); EXPECT_EQ(writable.address(), identity.address());
}

TEST(DynamicParamCapture, udf_in_scalar_owns_bytes_after_argument_frame_retires) {
  ObArenaAllocator owner;
  ParamStore params{ObWrapperAllocator(owner)};
  ObSEArray<ObUDFParamDesc, 1> desc;
  ObSEArray<ObExprResType, 1> types;
  ASSERT_EQ(OB_SUCCESS, desc.push_back(ObUDFParamDesc()));
  ObExprResType type; type.set_int();
  ASSERT_EQ(OB_SUCCESS, types.push_back(type));
  {
    ObArenaAllocator argument_owner;
    DatumParameter input; ASSERT_EQ(OB_SUCCESS, input.set_int(42, argument_owner));
    ASSERT_EQ(OB_SUCCESS, ObExprUDF::process_in_params(&input, 1, desc, types, params, owner));
    ASSERT_EQ(1, params.count()); EXPECT_EQ(0, input.datum_.length()); EXPECT_EQ(0, params.at(0).datum_.length());
    input.datum_.set_int(99);
    EXPECT_EQ(42, params.at(0).get_int());
  }
  EXPECT_EQ(42, params.at(0).get_int());
}

TEST(DynamicParamCapture, udf_field_input_unwraps_native_member_and_clones_scalar) {
  using namespace oceanbase::pl;
  ObArenaAllocator owner, field_owner;
  ParamStore params{ObWrapperAllocator(owner)};
  ObSEArray<ObUDFParamDesc, 1> desc;
  ObSEArray<ObExprResType, 1> types;
  ASSERT_EQ(OB_SUCCESS, desc.push_back(ObUDFParamDesc(ObUDFParamDesc::OBJ_ACCESS_INOUT)));
  ObExprResType type; type.set_varchar();
  ASSERT_EQ(OB_SUCCESS, types.push_back(type));
  DatumValue field;
  {
    DatumParameter text;
    ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString::make_string("field-value"), field_owner));
    field = text.value_view();
  }
  ObPlCompiteWrite write;
  write.allocator_ = reinterpret_cast<int64_t>(&field_owner);
  write.value_addr_ = reinterpret_cast<int64_t>(&field);
  DatumParameter input;
  ASSERT_EQ(OB_SUCCESS, input.set_extend(reinterpret_cast<int64_t>(&write), PL_RECORD_TYPE, owner));
  ASSERT_EQ(OB_SUCCESS, ObExprUDF::process_in_params(&input, 1, desc, types, params, owner));
  ASSERT_EQ(1, params.count()); EXPECT_NE(field.datum_.get_reference(), params.at(0).datum_.get_reference());
  field_owner.reset();
  EXPECT_EQ(ObString::make_string("field-value"), params.at(0).get_string());
}

TEST(DynamicParamCapture, udf_default_and_invalid_argument_counts_are_distinct) {
  ObArenaAllocator owner;
  ParamStore params{ObWrapperAllocator(owner)};
  ObSEArray<ObUDFParamDesc, 1> desc;
  ObSEArray<ObExprResType, 1> types;
  ASSERT_EQ(OB_SUCCESS, desc.push_back(ObUDFParamDesc()));
  ObExprResType type; type.set_null();
  ASSERT_EQ(OB_SUCCESS, types.push_back(type));
  DatumParameter input;
  ASSERT_EQ(OB_SUCCESS, ObExprUDF::process_in_params(&input, 1, desc, types, params, owner));
  ASSERT_EQ(1, params.count()); EXPECT_TRUE(params.at(0).is_pl_mock_default_param());
  params.reuse();
  EXPECT_NE(OB_SUCCESS, ObExprUDF::process_in_params(nullptr, 1, desc, types, params, owner));
  EXPECT_NE(OB_SUCCESS, ObExprUDF::process_in_params(&input, -1, desc, types, params, owner));
  EXPECT_EQ(0, params.count());
}

TEST(DynamicParamCapture, udf_write_marker_restoration_owns_descriptor_and_preserves_on_oom) {
  using namespace oceanbase::pl;
  ObArenaAllocator owner, field_owner;
  DatumValue field; ASSERT_EQ(OB_SUCCESS, field.set_int(9, field_owner));
  ObPlCompiteWrite write;
  write.allocator_ = reinterpret_cast<int64_t>(&field_owner);
  write.value_addr_ = reinterpret_cast<int64_t>(&field);
  DatumParameter input;
  ASSERT_EQ(OB_SUCCESS, input.set_extend(reinterpret_cast<int64_t>(&write), PL_RECORD_TYPE, owner, 64));
  const Datum original = input.datum_;
  ObIAllocator *actual_owner = nullptr;
  DatumParameter restored;
  ASSERT_EQ(OB_SUCCESS, ObExprUDF::extract_allocator_and_restore_obj(input, restored, actual_owner, owner));
  EXPECT_EQ(&field_owner, actual_owner);
  ASSERT_TRUE(restored.datum_.is_inline_extended());
  EXPECT_EQ(reinterpret_cast<int64_t>(&field), restored.get_ext());
  EXPECT_EQ(PL_RECORD_TYPE, restored.get_extend_type()); EXPECT_EQ(64, restored.get_val_len());
  EXPECT_EQ(0, MEMCMP(&original, &input.datum_, sizeof(Datum)));
  RejectParameterGrowth reject; reject.reject_ = true;
  DatumParameter previous; ASSERT_EQ(OB_SUCCESS, previous.set_int(123, owner));
  actual_owner = nullptr;
  ASSERT_EQ(OB_SUCCESS,
      ObExprUDF::extract_allocator_and_restore_obj(input, previous, actual_owner, reject));
  EXPECT_TRUE(previous.datum_.is_inline_extended());
  EXPECT_EQ(reinterpret_cast<int64_t>(&field), previous.get_ext());
  EXPECT_EQ(&field_owner, actual_owner); EXPECT_EQ(64, previous.get_val_len());
  // Removed wrapper OOM is a zero-allocation positive control. Invalid marker
  // inputs still fail before publishing either the output or its owner.
  for (bool null_input : {false, true}) {
    SCOPED_TRACE(null_input);
    DatumParameter invalid;
    if (!null_input) { ASSERT_EQ(OB_SUCCESS, invalid.set_extend(0, PL_RECORD_TYPE, owner)); }
    ASSERT_EQ(OB_SUCCESS, previous.set_int(123, owner));
    previous.set_precision(19); previous.set_result_flags(0x88); previous.set_param_meta();
    const Datum old_value = previous.datum_;
    const SqlTypeMeta old_meta = previous.get_type_meta();
    const SqlTypeMeta old_param_meta = previous.get_param_meta();
    actual_owner = nullptr;
    EXPECT_EQ(OB_ERR_UNEXPECTED,
        ObExprUDF::extract_allocator_and_restore_obj(invalid, previous, actual_owner, reject));
    EXPECT_EQ(0, MEMCMP(&old_value, &previous.datum_, sizeof(Datum)));
    EXPECT_EQ(old_meta, previous.get_type_meta()); EXPECT_EQ(old_param_meta, previous.get_param_meta());
    EXPECT_EQ(19, previous.get_precision()); EXPECT_EQ(0x88, previous.get_result_flag());
    EXPECT_EQ(nullptr, actual_owner);
  }
  EXPECT_EQ(9, field.get_int());
  EXPECT_EQ(0, MEMCMP(&original, &input.datum_, sizeof(Datum)));
}

TEST(DynamicParamCapture, native_expression_capture_keeps_accuracy_scale_and_lob_metadata_separate) {
  Frame f; ObEvalCtx ctx(f.exec);
  f.expr.locate_datum_for_write(ctx).set_int(42);
  f.expr.datum_meta_.scale_ = 3;
  f.expr.datum_meta_.precision_ = 12;
  DatumParameter captured;
  ASSERT_EQ(OB_SUCCESS, f.expr.prepare_scalar_type(f.expr.obj_meta_, ObAccuracy(12, 3), f.arena));
  ASSERT_EQ(OB_SUCCESS, capture_datum_parameter(f.expr, ctx, captured));
  EXPECT_EQ(42, f.expr.locate_expr_datum(ctx).get_int());
  EXPECT_EQ(42, captured.get_int()); EXPECT_EQ(3, captured.get_meta().get_scale());
  EXPECT_EQ(3, captured.get_scale()); EXPECT_EQ(12, captured.get_precision());
  f.expr.obj_meta_.set_type(ObLongTextType); f.expr.obj_meta_.set_has_lob_header();
  f.expr.obj_meta_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  f.expr.datum_meta_.length_semantics_ = LS_CHAR;
  ASSERT_EQ(OB_SUCCESS, f.expr.prepare_scalar_type(f.expr.obj_meta_, ObAccuracy(), f.arena));
  ASSERT_EQ(OB_SUCCESS, f.expr.locate_datum_for_write(ctx).set_string(ObString("LOB payload")));
  f.expr.locate_datum_for_write(ctx).set_has_lob_header();
  ASSERT_EQ(OB_SUCCESS, capture_datum_parameter(f.expr, ctx, captured));
  EXPECT_TRUE(captured.has_lob_header());
  EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, captured.get_collation_type());
  EXPECT_EQ(LS_CHAR, captured.get_accuracy().get_length_semantics());
}
TEST(DynamicParamCapture, native_pl_return_sentinel_distinguishes_no_return_from_return_null) {
  DatumParameter result; result.set_type(ObMaxType);
  EXPECT_TRUE(result.is_invalid_type());
  DatumParameter null_value;
  result.assign_value(null_value);
  EXPECT_TRUE(result.is_null()); EXPECT_FALSE(result.is_invalid_type());
  DatumParameter typed_null; typed_null.set_type(ObExtendType);
  bool is_child = true;
  ASSERT_EQ(OB_SUCCESS, ObExprUDF::is_child_of(typed_null, typed_null, is_child));
  EXPECT_FALSE(is_child);
}

namespace {
class CaptureRecordDeclaration final : public oceanbase::pl::ObPLINS
{
public:
  explicit CaptureRecordDeclaration(const oceanbase::pl::ObRecordType &declaration)
      : declaration_(declaration) {}
  int get_user_type(uint64_t id, const oceanbase::pl::ObUserDefinedType *&out,
                    ObIAllocator *allocator = nullptr) const override
  {
    UNUSED(allocator);
    if (id != declaration_.get_user_type_id()) { return OB_ENTRY_NOT_EXIST; }
    out = &declaration_;
    return OB_SUCCESS;
  }
private:
  const oceanbase::pl::ObRecordType &declaration_;
};
} // namespace

TEST(DynamicParamCapture, pl_frame_capture_preserves_complete_declaration_id_for_present_and_typed_null) {
  using namespace oceanbase::pl;
  // Both halves matter: neither an ordinal nor the legacy numeric/length
  // projection describes these actual declaration IDs.
  for (const uint64_t declaration_id : {UINT64_C(0x1234567890abcdef), UINT64_C(0x6b7c8d9e10293847)}) {
    SCOPED_TRACE(declaration_id);
    Frame f;
    ObEvalCtx ctx(f.exec);
    ObRecordType declaration;
    declaration.set_user_type_id(declaration_id);
    ASSERT_EQ(OB_SUCCESS, declaration.record_members_init(&f.arena, 1));
    ObPLDataType member(ObIntType);
    ASSERT_EQ(OB_SUCCESS, declaration.add_record_member(ObString::make_string("value"), member));
    SqlPLDeclarationIdentity identity;
    identity.source = SqlPLDeclarationSource::Local;
    identity.tenant_id = OB_SERVER_RUNTIME_ID;
    identity.declaration_id = declaration_id;
    identity.compilation_scope.high = UINT64_C(0x1020304050607080);
    identity.compilation_scope.low = UINT64_C(0x8877665544332211);
    ASSERT_EQ(OB_SUCCESS, declaration.set_sql_declaration_identity(identity, SqlNominalType()));
    CaptureRecordDeclaration declarations(declaration);
    SqlTypeMeta meta;
    meta.set_ext(); meta.set_extend_type(PL_RECORD_TYPE);
    meta.set_collation_type(CS_TYPE_BINARY); meta.set_collation_level(CS_LEVEL_IMPLICIT);
    ObAccuracy accuracy;
    accuracy.set_accuracy(declaration_id);
    const SqlType *type = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObExprUtil::resolve_sql_type(f.exec, meta, accuracy, 0,
        f.arena, type, &declarations));
    ASSERT_NE(nullptr, type);
    f.expr.obj_meta_ = meta;
    f.expr.datum_meta_.type_ = ObExtendType;
    f.expr.obj_datum_map_ = OBJ_DATUM_EXTEND;
    f.expr.datum_meta_.scale_ = 13;
    f.expr.datum_meta_.precision_ = 17;
    f.expr.max_length_ = 19;
    ASSERT_EQ(OB_SUCCESS, f.expr.bind_sql_type(*type, f.arena));

    auto *record = make_parameter_record(f.arena, 57);
    ASSERT_NE(nullptr, record);
    record->set_id(declaration_id);
    DatumParameter source;
    ASSERT_EQ(OB_SUCCESS, source.set_extend(reinterpret_cast<int64_t>(record),
        PL_RECORD_TYPE, f.arena, record->get_init_size()));
    source.set_type_meta(meta); source.set_udt_id(declaration_id);
    struct RecordCleanup {
      ObIAllocator &owner_;
      DatumParameter &value_;
      ~RecordCleanup() {
        EXPECT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(owner_, value_));
      }
    } cleanup{f.arena, source};
    const NativeTypedDatum typed_fixture = f.typed(source);
    ASSERT_TRUE(typed_fixture.datum_.is_inline_extended()); // typed() scratch has retired.
    EXPECT_EQ(&f.expr.get_sql_type(), typed_fixture.type_);
    EXPECT_EQ(declaration_id, typed_fixture.type_->pl_declaration().declaration_id);
    EXPECT_EQ(source.get_ext(), typed_fixture.datum_.get_ext());
    EXPECT_EQ(source.get_val_len(), typed_fixture.datum_.get_extend_size());
    alignas(8) char source_scratch[64];
    ObDatum legacy_source;
    ASSERT_EQ(OB_SUCCESS, source.export_legacy(legacy_source, source_scratch, sizeof(source_scratch)));
    ASSERT_EQ(OB_SUCCESS, f.expr.get_datum_copy_plan().import_legacy(
        legacy_source, f.expr.locate_expr_datum(ctx)));
    DatumParameter captured;
    ASSERT_EQ(OB_SUCCESS, capture_datum_parameter(f.expr, ctx, captured));
    EXPECT_FALSE(captured.is_null());
    EXPECT_EQ(declaration_id, captured.get_udt_id());
    EXPECT_EQ(declaration_id, static_cast<uint64_t>(captured.get_accuracy().get_accuracy()));
    EXPECT_EQ(meta, captured.get_type_meta()); EXPECT_EQ(meta, captured.get_param_meta());
    // This frame deliberately imports a borrowed old-ABI view. Its outer
    // source_scratch is still live; capture may keep exactly that borrowed span.
    EXPECT_FALSE(captured.datum_.is_inline_extended());
    EXPECT_EQ(legacy_source.ptr_, captured.datum_.get_reference());
    EXPECT_EQ(reinterpret_cast<int64_t>(record), captured.get_ext());
    EXPECT_EQ(57, record->get_element()[0].get_int());

    f.expr.locate_expr_datum(ctx).set_null();
    ASSERT_EQ(OB_SUCCESS, capture_datum_parameter(f.expr, ctx, captured));
    EXPECT_TRUE(captured.is_null());
    EXPECT_EQ(declaration_id, captured.get_udt_id());
    EXPECT_EQ(declaration_id, static_cast<uint64_t>(captured.get_accuracy().get_accuracy()));
    EXPECT_EQ(meta, captured.get_type_meta()); EXPECT_EQ(meta, captured.get_param_meta());
    // A NULL has no payload from which the capture could recover an ID.
    EXPECT_TRUE(captured.datum_.is_null()); EXPECT_EQ(0, captured.datum_.length());

    // The real DynamicSetter consumes the prepared declaration directly.
    // No PL stack is installed: it must not rediscover this local declaration
    // in whichever caller namespace happens to be current.
    EXPECT_EQ(nullptr, f.exec.get_pl_stack_ctx());
    ObDynamicParamSetter setter(0, nullptr, &f.expr);
    ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, f.expr.locate_expr_datum(ctx)));
    auto &plan = *f.exec.get_physical_plan_ctx();
    EXPECT_TRUE(read_parameter_value(plan, 0).is_null());
    EXPECT_EQ(declaration_id, plan.get_native_parameter_type(0)->pl_declaration().declaration_id);
    EXPECT_EQ(declaration_id, static_cast<uint64_t>(f.parameters().get_accuracy().get_accuracy()));
    EXPECT_EQ(meta, f.parameters().get_param_meta());
    ASSERT_EQ(OB_SUCCESS, f.expr.get_datum_copy_plan().import_legacy(
        legacy_source, f.expr.locate_expr_datum(ctx)));
    ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, f.expr.locate_expr_datum(ctx)));
    const Datum retained = read_parameter_value(plan, 0);
    EXPECT_EQ(reinterpret_cast<int64_t>(record), retained.get_ext());
    if (!retained.is_inline_extended()) {
      EXPECT_NE(legacy_source.ptr_, retained.get_reference()); // Actual owned carrier bytes.
    }
    MEMSET(source_scratch, 0xa5, sizeof(source_scratch));
    f.expr.locate_expr_datum(ctx).set_null(); // Retire the original frame view too.
    EXPECT_EQ(reinterpret_cast<int64_t>(record), read_parameter_value(plan, 0).get_ext());
    EXPECT_EQ(declaration_id, f.parameters().get_udt_id());
    EXPECT_EQ(57, record->get_element()[0].get_int());
    record->get_element()[0].datum_.set_int(58);
    EXPECT_EQ(58, reinterpret_cast<ObPLRecord *>(read_parameter_value(plan, 0).get_ext())
        ->get_element()[0].get_int()); // Pointee/lvalue identity remains deliberately borrowed.
  }
}


TEST(DynamicParamCapture, typed_update_failure_keeps_destination_parent_and_parameter_flags) {
  Frame f;
  ObEvalCtx ctx(f.exec);
  DatumParameter previous = f.integer(42);
  previous.set_param_meta(); f.bind(previous);
  f.expr.locate_expr_datum(ctx).set_int(77);
  f.expr.get_eval_info(ctx).evaluated_ = true;
  f.expr.get_eval_info(ctx).projected_ = true;
  ObExpr parent;
  parent.frame_idx_ = 0; parent.eval_info_off_ = 512;
  parent.type_ = T_OP_ADD;
  ASSERT_EQ(OB_SUCCESS, parent.bind_sql_type(f.expr.get_sql_type(), f.arena));
  ObExpr *parents[] = {&parent};
  f.expr.parents_ = parents; f.expr.parent_cnt_ = 1;
  parent.get_eval_info(ctx).evaluated_ = true;
  auto &plan = *f.exec.get_physical_plan_ctx();
  const Datum *old_slot = materialized_parameter(plan, 0);
  const SqlType *old_type = plan.get_native_parameter_type(0);
  const DatumParameter old_view = f.parameters();
  auto *info = plan.get_parameter_eval_info(0);
  info->evaluated_ = true; info->projected_ = true;
  Datum invalid; ASSERT_EQ(OB_SUCCESS, invalid.set_string("bad", 3));
  ObDynamicParamSetter setter(0, nullptr, &f.expr);
  EXPECT_EQ(OB_INVALID_ARGUMENT, setter.update_dynamic_param(ctx, invalid));
  EXPECT_EQ(old_type, plan.get_native_parameter_type(0));
  EXPECT_EQ(old_slot, materialized_parameter(plan, 0)); EXPECT_EQ(42, old_slot->get_int());
  EXPECT_EQ(old_view.get_int(), f.parameters().get_int());
  EXPECT_EQ(old_slot, materialized_parameter(plan, 0));
  EXPECT_TRUE(info->evaluated_); EXPECT_TRUE(info->projected_);
  EXPECT_EQ(77, f.expr.locate_expr_datum(ctx).get_int());
  EXPECT_TRUE(f.expr.get_eval_info(ctx).evaluated_); EXPECT_TRUE(f.expr.get_eval_info(ctx).projected_);
  EXPECT_TRUE(parent.get_eval_info(ctx).evaluated_);
  Datum valid; valid.set_int(-8);
  ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, valid));
  EXPECT_EQ(-8, read_parameter_value(plan, 0).get_int());
  EXPECT_EQ(-8, f.expr.locate_expr_datum(ctx).get_int());
  EXPECT_TRUE(f.expr.get_eval_info(ctx).evaluated_);
  EXPECT_FALSE(parent.get_eval_info(ctx).evaluated_);
}

TEST(DynamicParamCapture, typed_capture_batch_restore_retires_source_and_batch_owners_without_a_shadow_value) {
  ObBatchRescanParams batch;
  ObAccuracy text_accuracy; text_accuracy.set_full_length(65, LS_BYTE); text_accuracy.set_scale(0);
  {
    Frame source; ObEvalCtx ctx(source.exec);
    DatumParameter binding = source.integer(9);
    binding.set_raw_text_info(17, 23); binding.set_result_flags(0xa5); source.bind(binding);
    source.expr.obj_meta_.set_varchar(); source.expr.obj_meta_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS, source.expr.prepare_scalar_type(source.expr.obj_meta_, text_accuracy, source.arena));
    char bytes[] = {'a', 0, 'b'}; Datum input; ASSERT_EQ(OB_SUCCESS, input.set_string(bytes, sizeof(bytes)));
    ObDynamicParamSetter setter(0, nullptr, &source.expr);
    ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, input));
    NativeTypedDatum captured; DatumParameterMetadata metadata;
    ASSERT_EQ(OB_SUCCESS, setter.capture_dynamic_param(ctx, captured, metadata));
    EXPECT_EQ(source.exec.get_physical_plan_ctx()->get_native_parameter_type(0), captured.type_);
    EXPECT_EQ(read_parameter_value(*source.exec.get_physical_plan_ctx(), 0).get_reference(), captured.datum_.get_reference());
    ObBatchRescanParams::RowBuilder row(batch);
    ASSERT_EQ(OB_SUCCESS, row.append(captured, metadata));
    memset(bytes, 'x', sizeof(bytes));
    ASSERT_EQ(OB_SUCCESS, input.set_string("later", 5));
    ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, input));
    ObSEArray<int64_t, 1> ids, expressions;
    ASSERT_EQ(OB_SUCCESS, ids.push_back(0)); ASSERT_EQ(OB_SUCCESS, expressions.push_back(1));
    ASSERT_EQ(OB_SUCCESS, row.finish(ids, expressions));
  }
  ASSERT_TRUE(batch.native_ready());
  EXPECT_EQ(ObString(3, "a\0b"), batch.get_one_batch_values(0).at(0).datum_.get_string());
  EXPECT_EQ(ObVarcharType, batch.get_one_batch_values(0).at(0).type_->kind());
  Frame destination; ObEvalCtx ctx(destination.exec);
  destination.expr.obj_meta_.set_varchar(); destination.expr.obj_meta_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, destination.expr.prepare_scalar_type(destination.expr.obj_meta_, text_accuracy, destination.arena));
  ObDynamicParamSetter setter(0, nullptr, &destination.expr);
  ASSERT_EQ(OB_SUCCESS, setter.restore_dynamic_param(ctx, batch.get_one_batch_values(0).at(0), batch.get_one_batch_metadata(0).at(0)));
  const Datum *slot = materialized_parameter(*destination.exec.get_physical_plan_ctx(), 0);
  const SqlType *type = destination.exec.get_physical_plan_ctx()->get_native_parameter_type(0);
  EXPECT_NE(batch.get_one_batch_values(0).at(0).datum_.get_reference(), slot->get_reference());
  EXPECT_NE(batch.get_one_batch_values(0).at(0).type_, type);
  batch.reset();
  EXPECT_EQ(ObString(3, "a\0b"), slot->get_string()); EXPECT_EQ(ObVarcharType, type->kind());
  EXPECT_EQ(ObString(3, "a\0b"), destination.expr.locate_expr_datum(ctx).get_string());
  const auto *metadata = destination.exec.get_physical_plan_ctx()->get_parameter_metadata(0);
  ASSERT_NE(nullptr, metadata); EXPECT_EQ(17, metadata->raw_text_pos_); EXPECT_EQ(23, metadata->raw_text_len_);
  EXPECT_EQ(0xa5, metadata->res_flags_);
}


namespace {
class HolderOwner final : public ObIAllocator {
public:
  HolderOwner() : arena_("HolderOwner"), calls_(0), fail_call_(0), reject_all_(false) {}
  void *alloc(int64_t size) override {
    ++calls_;
    return reject_all_ || calls_ == fail_call_ ? nullptr : arena_.alloc(size);
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *value) override { arena_.free(value); }
  ObArenaAllocator arena_;
  int64_t calls_;
  int64_t fail_call_;
  bool reject_all_;
};
int holder_callback_calls = 0;
int holder_forbidden_callback(const ObExpr &, ObEvalCtx &, Datum &) {
  ++holder_callback_calls;
  return OB_ERR_UNEXPECTED;
}
ObExpr holder_alias(const Frame &f) {
  ObExpr alias = f.expr;
  alias.type_ = T_QUESTIONMARK;
  alias.native_parameter_alias_ = true;
  alias.extra_ = 0;
  alias.frame_idx_ = 0;
  alias.datum_off_ = ScalarFrameLayout::datum_offset(0);
  alias.eval_info_off_ = ScalarFrameLayout::eval_info_offset(0);
  alias.eval_func_ = nullptr;
  return alias;
}
const Datum *holder_projection(const ObPhysicalPlanCtx &plan) {
  int64_t page = 0, slot = 0;
  ScalarFrameLayout::locate_parameter(plan.get_original_param_cnt(), 0, page, slot);
  return ParameterValuePage::from_handle(plan.get_param_frame_ptrs().at(page)).find_materialized(slot);
}
void holder_expect_attributes(const DatumParameterMetadata &expected,
                              const DatumParameterMetadata &actual) {
  EXPECT_EQ(expected.type_meta_, actual.type_meta_);
  EXPECT_EQ(expected.type_meta_.get_scale(), actual.type_meta_.get_scale());
  EXPECT_EQ(expected.accuracy_, actual.accuracy_);
  EXPECT_EQ(expected.param_meta_, actual.param_meta_);
  EXPECT_EQ(expected.param_meta_.get_scale(), actual.param_meta_.get_scale());
  EXPECT_EQ(expected.res_flags_, actual.res_flags_);
  EXPECT_EQ(expected.raw_text_pos_, actual.raw_text_pos_);
  EXPECT_EQ(expected.raw_text_len_, actual.raw_text_len_);
  EXPECT_EQ(expected.flag_.flag_, actual.flag_.flag_);
  EXPECT_EQ(expected.flag_.extend_flag_, actual.flag_.extend_flag_);
}
} // namespace

TEST(DynamicParamCapture, batch_holder_saves_fresh_alias_and_restores_canonical_without_eval) {
  HolderOwner owner;
  Frame f(&owner);
  ObEvalCtx ctx(f.exec); ctx.max_batch_size_ = 3;
  auto &plan = *f.exec.get_physical_plan_ctx();
  DatumParameter initial = f.integer(INT64_MIN);
  initial.set_precision(19); initial.set_scale(3); initial.set_result_flags(0xa5);
  initial.set_raw_text_info(17, 23); initial.set_param_meta();
  initial.get_param_flag().flag_ = 0x5a;
  ASSERT_EQ(OB_SUCCESS, f.bind_status(0, initial));
  const SqlType *saved_type = plan.get_native_parameter_type(0);
  const DatumParameterMetadata saved_attributes = *plan.get_parameter_metadata(0);
  ObExpr alias = holder_alias(f);
  // Real CG aliases follow earlier frames: their frame_idx is an absolute
  // directory position, not the owner-relative parameter page number.
  char **prefixed = static_cast<char **>(f.arena.alloc(2 * sizeof(char *)));
  ASSERT_NE(nullptr, prefixed);
  prefixed[0] = f.frames[0]; prefixed[1] = plan.get_param_frame_ptrs().at(0);
  f.exec.set_frames(prefixed); ctx.frames_ = prefixed; alias.frame_idx_ = 1;
  ObExpr generic = f.expr; generic.batch_result_ = true;
  generic.eval_func_ = holder_forbidden_callback;
  Datum *rows = generic.locate_batch_datums(ctx);
  for (int i = 0; i < 3; ++i) { new (rows + i) Datum(); rows[i].set_int(11 + i); }
  ObSEArray<ObExpr *, 3> exprs;
  ASSERT_EQ(OB_SUCCESS, exprs.push_back(&generic));
  ASSERT_EQ(OB_SUCCESS, exprs.push_back(&alias));
  ASSERT_EQ(OB_SUCCESS, exprs.push_back(&alias)); // repeated slot preserves holder order
  ObBatchResultHolder holder;
  ASSERT_EQ(OB_SUCCESS, holder.init(exprs, ctx));
  holder_callback_calls = 0;
  ASSERT_EQ(nullptr, holder_projection(plan)); // no eval/materialization prewarm
  int64_t calls = owner.calls_;
  owner.reject_all_ = true;
  ASSERT_EQ(OB_SUCCESS, holder.save(3)); // old 725 aborts in locate_native_parameter here
  EXPECT_EQ(calls, owner.calls_); EXPECT_EQ(nullptr, holder_projection(plan));
  EXPECT_EQ(0, holder_callback_calls);
  owner.reject_all_ = false;
  Datum changed; changed.set_int(99);
  ASSERT_EQ(OB_SUCCESS, plan.set_runtime_parameter(0, changed, *saved_type, saved_attributes));
  for (int i = 0; i < 3; ++i) { rows[i].set_int(90 + i); }
  auto *info = plan.get_parameter_eval_info(0);
  info->flag_ = 0xb; info->cnt_ = 7;
  calls = owner.calls_; owner.reject_all_ = true;
  ASSERT_EQ(OB_SUCCESS, holder.restore());
  EXPECT_EQ(calls, owner.calls_); EXPECT_EQ(nullptr, holder_projection(plan));
  EXPECT_EQ(INT64_MIN, read_parameter_value(plan, 0).get_int());
  EXPECT_EQ(saved_type, plan.get_native_parameter_type(0));
  holder_expect_attributes(saved_attributes, *plan.get_parameter_metadata(0));
  EXPECT_EQ(0xb, info->flag_); EXPECT_EQ(7, info->cnt_);
  for (int i = 0; i < 3; ++i) { EXPECT_EQ(11 + i, rows[i].get_int()); }
  ASSERT_EQ(OB_SUCCESS, holder.check_datum_modified());
  EXPECT_FALSE(holder.is_saved()); EXPECT_EQ(0, holder_callback_calls);
  owner.reject_all_ = false;
  holder.destroy();
}

TEST(DynamicParamCapture, batch_holder_restores_reference_type_metadata_and_existing_projection_noalloc) {
  HolderOwner owner;
  Frame f(&owner); ObEvalCtx ctx(f.exec); ctx.max_batch_size_ = 2;
  auto &plan = *f.exec.get_physical_plan_ctx();
  SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObAccuracy accuracy; accuracy.set_full_length(64, LS_BYTE);
  const SqlType *text_type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, f.arena, text_type));
  DatumParameter attributes_source;
  attributes_source.set_type_meta(meta); attributes_source.set_param_meta(meta);
  attributes_source.set_accuracy(accuracy); attributes_source.set_result_flags(0x3456);
  attributes_source.set_raw_text_info(31, 47); attributes_source.get_param_flag().flag_ = 0xa5;
  const DatumParameterMetadata attributes = attributes_source.get_metadata();
  Datum text; ASSERT_EQ(OB_SUCCESS, text.set_string("a\0b", 3));
  ASSERT_EQ(OB_SUCCESS, plan.set_runtime_parameter(0, text, *text_type, attributes));
  const SqlType *saved_type = plan.get_native_parameter_type(0);
  const Datum saved = read_parameter_value(plan, 0);
  const DatumParameterMetadata saved_attributes = *plan.get_parameter_metadata(0);
  ObExpr alias = holder_alias(f); ObSEArray<ObExpr *, 1> exprs;
  ASSERT_EQ(OB_SUCCESS, exprs.push_back(&alias));
  ObBatchResultHolder holder; ASSERT_EQ(OB_SUCCESS, holder.init(exprs, ctx));
  ASSERT_EQ(nullptr, holder_projection(plan));
  ASSERT_EQ(OB_SUCCESS, holder.save(2));
  const Datum *projection = materialized_parameter(plan, 0); // created after save by a real ABI consumer
  ASSERT_NE(nullptr, projection);
  Datum newer; ASSERT_EQ(OB_SUCCESS, newer.set_string("overwritten", 11));
  ASSERT_EQ(OB_SUCCESS, plan.set_runtime_parameter(0, newer, *saved_type, saved_attributes));
  // Generic backing cell is overwritten above, then displaced by NULL and I64.
  Datum null_value;
  ASSERT_EQ(OB_SUCCESS, plan.set_runtime_parameter(0, null_value, *saved_type, saved_attributes));
  Datum number; number.set_int(77);
  DatumParameterMetadata int_attributes = f.integer(77).get_metadata();
  ASSERT_EQ(OB_SUCCESS, plan.set_runtime_parameter(0, number, f.expr.get_sql_type(), int_attributes));
  auto *info = plan.get_parameter_eval_info(0); info->flag_ = 0xd; info->cnt_ = 9;
  int64_t calls = owner.calls_; owner.reject_all_ = true; holder_callback_calls = 0;
  ASSERT_EQ(OB_SUCCESS, holder.restore());
  EXPECT_EQ(calls, owner.calls_); EXPECT_EQ(0, holder_callback_calls);
  const Datum restored = read_parameter_value(plan, 0);
  EXPECT_EQ(ObString(3, "a\0b"), restored.get_string());
  EXPECT_EQ(saved.get_reference(), restored.get_reference()); // same statement owner, shallow holder contract
  EXPECT_EQ(saved_type, plan.get_native_parameter_type(0));
  holder_expect_attributes(saved_attributes, *plan.get_parameter_metadata(0));
  EXPECT_EQ(projection, holder_projection(plan)); EXPECT_EQ(restored.get_string(), projection->get_string());
  EXPECT_EQ(0xd, info->flag_); EXPECT_EQ(9, info->cnt_);
  ASSERT_EQ(OB_SUCCESS, holder.check_datum_modified());
  // Save a typed NULL and restore it after a present value/type change as well.
  owner.reject_all_ = false;
  ASSERT_EQ(OB_SUCCESS, plan.set_runtime_parameter(0, null_value, *saved_type, saved_attributes));
  const DatumParameterMetadata null_metadata = *plan.get_parameter_metadata(0);
  EXPECT_TRUE(null_metadata.type_meta_.is_null());
  EXPECT_EQ(ObVarcharType, null_metadata.param_meta_.get_type());
  EXPECT_EQ(saved_type, plan.get_native_parameter_type(0));
  EXPECT_EQ(ObVarcharType, plan.get_native_parameter_type(0)->kind());
  // The NULL setter normalizes only the current physical kind. Preserve all
  // other attributes independently of the actual snapshot restored below.
  DatumParameterMetadata expected_null_metadata = saved_attributes;
  expected_null_metadata.type_meta_.set_null();
  holder_expect_attributes(expected_null_metadata, null_metadata);
  ASSERT_EQ(OB_SUCCESS, holder.save(1));
  ASSERT_EQ(OB_SUCCESS, plan.set_runtime_parameter(0, number, f.expr.get_sql_type(), int_attributes));
  calls = owner.calls_; owner.reject_all_ = true;
  ASSERT_EQ(OB_SUCCESS, holder.restore());
  EXPECT_TRUE(read_parameter_value(plan, 0).is_null());
  EXPECT_EQ(saved_type, plan.get_native_parameter_type(0));
  holder_expect_attributes(null_metadata, *plan.get_parameter_metadata(0));
  EXPECT_EQ(calls, owner.calls_); EXPECT_TRUE(projection->is_null());
  owner.reject_all_ = false; holder.destroy();
}

TEST(DynamicParamCapture, batch_holder_init_oom_is_unpublished_and_retryable) {
  for (int64_t fail_allocation : {int64_t(1), int64_t(2)}) {
    Frame f; ObEvalCtx ctx(f.exec); ctx.max_batch_size_ = 2;
    ObExpr alias = holder_alias(f); ObSEArray<ObExpr *, 1> exprs;
    ASSERT_EQ(OB_SUCCESS, exprs.push_back(&alias));
    HolderOwner buffers; buffers.fail_call_ = fail_allocation;
    ObBatchResultHolder holder;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, holder.init(exprs, ctx, &buffers));
    EXPECT_FALSE(holder.is_saved());
    EXPECT_EQ(OB_NOT_INIT, holder.save(1));
    buffers.fail_call_ = 0;
    ASSERT_EQ(OB_SUCCESS, holder.init(exprs, ctx, &buffers));
    ASSERT_EQ(OB_SUCCESS, holder.save(1));
    EXPECT_TRUE(holder.is_saved());
    EXPECT_EQ(nullptr, holder_projection(*f.exec.get_physical_plan_ctx()));
    holder.destroy();
  }
}

TEST(DynamicParamCapture, batch_holder_preflights_all_aliases_before_restore_or_replacing_saved_values) {
  Frame f; ObEvalCtx ctx(f.exec); ctx.max_batch_size_ = 2;
  auto &plan = *f.exec.get_physical_plan_ctx();
  DatumParameter initial = f.integer(7); f.bind(initial);
  ObExpr alias = holder_alias(f); ObExpr last = holder_alias(f);
  ObExpr generic = f.expr; generic.eval_func_ = holder_forbidden_callback;
  generic.locate_expr_datum(ctx).set_int(11);
  ObSEArray<ObExpr *, 3> exprs;
  ASSERT_EQ(OB_SUCCESS, exprs.push_back(&generic));
  ASSERT_EQ(OB_SUCCESS, exprs.push_back(&alias));
  ASSERT_EQ(OB_SUCCESS, exprs.push_back(&last));
  ObBatchResultHolder holder; ASSERT_EQ(OB_SUCCESS, holder.init(exprs, ctx));
  ASSERT_EQ(OB_SUCCESS, holder.save(1));
  DatumParameter changed = f.integer(99); f.bind(changed);
  generic.locate_expr_datum(ctx).set_int(88);
  last.extra_ = 123; // a later alias fails before an earlier generic/owner write
  EXPECT_NE(OB_SUCCESS, holder.restore());
  EXPECT_EQ(88, generic.locate_expr_datum(ctx).get_int());
  EXPECT_EQ(99, read_parameter_value(plan, 0).get_int());
  EXPECT_TRUE(holder.is_saved());
  EXPECT_NE(OB_SUCCESS, holder.save(1));
  last.extra_ = 0;
  ASSERT_EQ(OB_SUCCESS, holder.restore()); // failed save did not replace the first backup
  EXPECT_EQ(11, generic.locate_expr_datum(ctx).get_int());
  EXPECT_EQ(7, read_parameter_value(plan, 0).get_int());
  ASSERT_EQ(OB_SUCCESS, holder.check_datum_modified());
  ASSERT_EQ(OB_SUCCESS, holder.save(1));
  f.bind(changed);
  EXPECT_NE(OB_SUCCESS, holder.check_datum_modified()); // canonical value, not an old projection
  EXPECT_FALSE(holder.is_saved());
  EXPECT_EQ(nullptr, holder_projection(plan));
  holder.destroy();
}
