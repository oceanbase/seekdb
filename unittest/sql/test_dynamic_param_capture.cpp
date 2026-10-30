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
#include "query/engine/ob_operator.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/ob_spi.h"
#include "sql/engine/expr/ob_expr_udf.h"
#include "sql/pl/ob_pl_package_state.h"
#include "sql/pl/ob_pl_resolver.h"
#include "sql/pl/ob_pl_package_guard.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/session/ob_sql_session_info.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#include "common/sql_mode/ob_sql_mode.h"
#include "share/datum/typed_datum_compare.h"
#include <cstring>
#include <gtest/gtest.h>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace oceanbase { namespace sql {
} }
namespace {
struct Frame {
  ObArenaAllocator arena;
  ObExecContext exec;
  ObExpr expr;
  char **frames;
  Frame() : arena(), exec(arena), expr(), frames(nullptr) {
    EXPECT_EQ(OB_SUCCESS, exec.create_physical_plan_ctx());
    EXPECT_EQ(OB_SUCCESS, exec.get_physical_plan_ctx()->reserve_param_space(1));
    EXPECT_EQ(OB_SUCCESS, exec.get_physical_plan_ctx()->init_datum_param_store());
    frames = static_cast<char **>(arena.alloc(sizeof(char *)));
    frames[0] = static_cast<char *>(arena.alloc(2048));
    memset(frames[0], 0, 2048);
    exec.set_frames(frames);
    expr.frame_idx_ = 0;
    expr.datum_off_ = 64;
    expr.eval_info_off_ = 128;
    expr.res_buf_off_ = 256;
    expr.res_buf_len_ = 512;
    expr.type_ = T_EXEC_VAR;
    expr.obj_meta_.set_int();
    expr.datum_meta_.type_ = ObIntType;
    expr.obj_datum_map_ = OBJ_DATUM_8BYTE_DATA;
    new (frames[0] + 64) ObDatum();
  }
  DatumParameter &parameters() { return exec.get_physical_plan_ctx()->get_param_store_for_update().at(0); }
  DatumParameter &native() { return exec.get_physical_plan_ctx()->get_datum_param_store().at(0); }
  DatumParameter integer(int64_t n) {
    DatumParameter p;
    p.set_type_meta(expr.obj_meta_);
    EXPECT_EQ(OB_SUCCESS, p.alloc_datum_reserved_buff(expr.obj_meta_, -1, arena));
    p.datum_.set_int(n);
    return p;
  }
};

} // namespace
TEST(DynamicParamCapture, value_is_captured_from_sql_frame_not_old_value_payload) {
  Frame f;
  ObEvalCtx ctx(f.exec);
  ObDynamicParamSetter setter(0, nullptr, &f.expr);
  ObDatum &frame = f.expr.locate_datum_for_write(ctx);
  frame.set_int(42);
  ASSERT_EQ(OB_SUCCESS, f.parameters().set_int(999, f.arena));
  f.parameters().set_precision(19);
  f.parameters().set_result_flag(0x1234);
  f.parameters().set_raw_text_info(3, 7);
  f.parameters().set_param_meta();
  f.parameters().set_precision(9);
  DatumParameter view;
  ASSERT_EQ(OB_SUCCESS, setter.capture_dynamic_param(ctx, view));
  EXPECT_EQ(42, view.datum_.get_int());
  EXPECT_EQ(999, f.parameters().get_int());
  EXPECT_EQ(frame.ptr_, view.datum_.ptr_);
  EXPECT_EQ(9, view.get_precision());
  EXPECT_EQ(0x1234, view.get_result_flag());
  EXPECT_EQ(3, view.get_raw_text_pos());
  EXPECT_EQ(7, view.get_raw_text_len());
  EXPECT_EQ(f.parameters().get_param_meta(), view.get_param_meta());
  ObBatchRescanParams batch;
  ObBatchRescanParams::Row values(4 * sizeof(DatumParameter));
  ObSEArray<int64_t, 1> ids;
  ASSERT_EQ(OB_SUCCESS, ids.push_back(0));
  ASSERT_EQ(OB_SUCCESS, values.push_back(view));
  ASSERT_EQ(OB_SUCCESS, batch.append_batch_rescan_param(ids, values));
  frame.set_int(88);
  EXPECT_EQ(42, batch.get_one_batch_params(0).at(0).datum_.get_int());
}
TEST(DynamicParamCapture, restoring_snapshot_does_not_evaluate_source_again) {
  Frame f;
  ObEvalCtx ctx(f.exec);
  ObDynamicParamSetter setter(0, nullptr, &f.expr);
  DatumParameter source = f.integer(INT64_MIN);
  source.set_precision(19);
  source.set_raw_text_info(17, 23);
  source.set_result_flags(0xa5);
  ASSERT_EQ(OB_SUCCESS, setter.restore_dynamic_param(ctx, source));
  EXPECT_EQ(INT64_MIN, f.expr.locate_expr_datum(ctx).get_int());
  EXPECT_EQ(INT64_MIN, f.parameters().get_int());
  EXPECT_EQ(19, f.parameters().get_precision());
  EXPECT_EQ(0xa5, f.parameters().get_result_flag());
  EXPECT_EQ(17, f.parameters().get_raw_text_pos());
  EXPECT_EQ(23, f.parameters().get_raw_text_len());
  EXPECT_EQ(f.expr.get_rev_buf(ctx), f.expr.locate_expr_datum(ctx).ptr_);
}
TEST(DynamicParamCapture, typed_null_snapshot_clears_stale_param_meta_before_restore) {
  Frame f;
  ObEvalCtx ctx(f.exec);
  ObDynamicParamSetter setter(0, nullptr, &f.expr);
  ASSERT_EQ(OB_SUCCESS, f.parameters().set_int(99, f.arena));
  f.parameters().set_param_meta();
  DatumParameter source;
  ASSERT_EQ(OB_SUCCESS, setter.restore_dynamic_param(ctx, source));
  EXPECT_TRUE(f.expr.locate_expr_datum(ctx).is_null());
  EXPECT_TRUE(f.parameters().is_null());
  EXPECT_TRUE(f.parameters().get_param_meta().is_null());
  SqlTypeMeta string_type;
  string_type.set_varchar();
  source.set_param_meta(string_type);
  source.set_length(65);
  ASSERT_EQ(OB_SUCCESS, setter.restore_dynamic_param(ctx, source));
  EXPECT_EQ(string_type, f.parameters().get_param_meta());
  EXPECT_EQ(65, f.parameters().get_length());
}
TEST(DynamicParamCapture, invalid_snapshot_locations_preserve_output) {
  Frame f;
  ObEvalCtx ctx(f.exec);
  DatumParameter output = f.integer(7);
  for (int64_t idx : {int64_t(-1), int64_t(1)}) {
    ObDynamicParamSetter bad(idx, nullptr, &f.expr);
    EXPECT_EQ(OB_INVALID_ARGUMENT, bad.capture_dynamic_param(ctx, output));
    EXPECT_EQ(7, output.datum_.get_int());
    EXPECT_EQ(OB_INVALID_ARGUMENT, bad.restore_dynamic_param(ctx, output));
  }
  ObDynamicParamSetter missing(0, nullptr, nullptr);
  EXPECT_EQ(OB_INVALID_ARGUMENT, missing.capture_dynamic_param(ctx, output));
  EXPECT_EQ(7, output.datum_.get_int());
}
TEST(DynamicParamCapture, px_worker_fills_expression_from_native_batch) {
  Frame f;
  ObBatchRescanParams batch;
  ObBatchRescanParams::Row row(4 * sizeof(DatumParameter));
  ObSEArray<int64_t, 1> ids, exprs;
  ASSERT_EQ(OB_SUCCESS, ids.push_back(0));
  ASSERT_EQ(OB_SUCCESS, exprs.push_back(1));
  ASSERT_EQ(OB_SUCCESS, row.push_back(f.integer(123)));
  ASSERT_EQ(OB_SUCCESS, batch.append_batch_rescan_param(ids, row, exprs));
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
  ObDatum source;
  source.set_string(ObString("raw payload"));
  ObDatum output(source);
  EXPECT_EQ(OB_SUCCESS, ob_adjust_lob_datum(f.exec, source, plain, plain, f.arena, output));
  EXPECT_EQ(source.get_string(), output.get_string());
  EXPECT_EQ(OB_ERR_UNEXPECTED, ob_adjust_lob_datum(f.exec, source, plain, header, f.arena, output));
  ObDatum null;
  null.set_null();
  EXPECT_EQ(OB_SUCCESS, ob_adjust_lob_datum(f.exec, null, header, plain, f.arena, output));
  const int64_t prefix = offsetof(ObLobCommon, buffer_);
  char *storage = static_cast<char *>(f.arena.alloc(prefix + source.len_));
  new (storage) ObLobCommon();
  memcpy(storage + prefix, source.ptr_, source.len_);
  ObDatum with_header;
  with_header.set_string(storage, prefix + source.len_);
  output = with_header;
  ASSERT_EQ(OB_SUCCESS, ob_adjust_lob_datum(f.exec, with_header, header, plain, f.arena, output));
  EXPECT_EQ(ObString("raw payload"), output.get_string());
}

TEST(DynamicParamCapture, native_extend_array_frame_captures_and_restores_parameter_address) {
  Frame f;
  ObEvalCtx ctx(f.exec);
  DatumParameter leaf = f.integer(42);
  auto *array = DatumParameterArray::alloc(f.arena, 1);
  ASSERT_NE(nullptr, array);
  array->data_[0] = leaf;
  f.expr.obj_meta_.set_ext();
  f.expr.obj_meta_.set_extend_type(T_EXT_SQL_ARRAY);
  f.expr.obj_datum_map_ = OBJ_DATUM_EXTEND;
  ASSERT_EQ(OB_SUCCESS, f.parameters().set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, f.arena));
  ObDatum &frame = f.expr.locate_datum_for_write(ctx);
  frame.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, 0);
  ObDynamicParamSetter setter(0, nullptr, &f.expr);
  DatumParameter captured;
  ASSERT_EQ(OB_SUCCESS, setter.capture_dynamic_param(ctx, captured));
  ASSERT_EQ(sizeof(int64_t), captured.datum_.len_);
  EXPECT_EQ(array, captured.get_array<DatumParameterArray>());
  ASSERT_EQ(OB_SUCCESS, setter.restore_dynamic_param(ctx, captured));
  EXPECT_TRUE(f.expr.locate_expr_datum(ctx).is_native_extend());
  EXPECT_EQ(reinterpret_cast<int64_t>(array), f.expr.locate_expr_datum(ctx).get_ext());
  EXPECT_EQ(reinterpret_cast<int64_t>(array), f.parameters().get_ext());
  ObBatchRescanParams batch;
  ObBatchRescanParams::Row row(4 * sizeof(DatumParameter));
  ObSEArray<int64_t, 1> ids, exprs;
  ASSERT_EQ(OB_SUCCESS, ids.push_back(0));
  ASSERT_EQ(OB_SUCCESS, exprs.push_back(1));
  ASSERT_EQ(OB_SUCCESS, row.push_back(captured));
  ASSERT_EQ(OB_SUCCESS, batch.append_batch_rescan_param(ids, row, exprs));
  auto *owned = batch.get_one_batch_params(0).at(0).get_array<DatumParameterArray>();
  ASSERT_NE(array, owned);
  ObSEArray<ObExpr, 1> expressions;
  ASSERT_EQ(OB_SUCCESS, expressions.push_back(f.expr));
  ASSERT_EQ(OB_SUCCESS, f.exec.fill_px_batch_info(batch, 0, expressions));
  EXPECT_TRUE(f.expr.locate_expr_datum(ctx).is_native_extend());
  EXPECT_EQ(reinterpret_cast<int64_t>(owned), f.expr.locate_expr_datum(ctx).get_ext());
  EXPECT_EQ(42, owned->data_[0].datum_.get_int());
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
  ASSERT_EQ(OB_SUCCESS, plan->set_runtime_parameter(0, value));
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
  EXPECT_NE(OB_SUCCESS, plan->set_runtime_parameter(0, invalid));
  EXPECT_EQ(42, f.native().datum_.get_int());
  EXPECT_EQ(42, f.parameters().get_int());
  EXPECT_EQ(OB_INVALID_ARGUMENT, plan->set_runtime_parameter(-1, value));
  EXPECT_EQ(OB_INVALID_ARGUMENT, plan->set_runtime_parameter(1, value));
  // Both API names refer to the same slot. Self-binding must preserve its
  // latest value and payload address instead of restoring a former shadow.
  ASSERT_EQ(&f.parameters(), &f.native());
  ASSERT_EQ(OB_SUCCESS, f.parameters().set_int(99, f.arena));
  const char *payload = f.native().datum_.ptr_;
  ASSERT_EQ(OB_SUCCESS, plan->set_runtime_parameter(0, f.native()));
  EXPECT_EQ(payload, f.native().datum_.ptr_);
  EXPECT_EQ(99, f.native().datum_.get_int());
  EXPECT_EQ(plan->get_param_mem(), &f.parameters());
}
TEST(DynamicParamCapture, native_runtime_null_keeps_declared_type_and_flags) {
  Frame f;
  DatumParameter source = f.integer(77);
  source.set_param_meta(source.get_type_meta());
  source.set_precision(19);
  source.set_scale(3);
  source.set_result_flags(0xa5);
  source.get_param_flag().flag_ = 0x5a;
  ASSERT_EQ(OB_SUCCESS, f.exec.get_physical_plan_ctx()->set_runtime_parameter(0, source));
  DatumParameter expected = f.parameters();
  expected.set_null();
  source.set_null();
  ASSERT_EQ(OB_SUCCESS, f.exec.get_physical_plan_ctx()->set_runtime_parameter(0, source));
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
  ASSERT_EQ(OB_SUCCESS, f.exec.get_physical_plan_ctx()->set_runtime_parameter(0, initial));
  auto input = f.integer(42);
  ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, input.datum_));
  EXPECT_EQ(42, f.native().datum_.get_int());
  EXPECT_EQ(42, f.parameters().get_int());
  EXPECT_EQ(f.expr.locate_expr_datum(ctx).ptr_, f.native().datum_.ptr_);
  DatumParameter saved;
  ASSERT_EQ(OB_SUCCESS, saved.deep_copy(f.native(), f.arena));
  input.datum_.set_int(88);
  ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, input.datum_));
  EXPECT_EQ(42, saved.datum_.get_int());
  EXPECT_EQ(88, f.native().datum_.get_int());
  ASSERT_EQ(OB_SUCCESS, setter.restore_dynamic_param(ctx, saved));
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
    ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, input.datum_));
    ASSERT_EQ(OB_SUCCESS, saved.deep_copy(f.native(), f.exec.get_allocator()));
    input.datum_.set_int(88);
    ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, input.datum_));
    ASSERT_EQ(OB_SUCCESS, f.exec.get_physical_plan_ctx()->set_runtime_parameter(0, saved));
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
  ASSERT_EQ(OB_SUCCESS, f.parameters().set_int(123, f.arena));
  f.parameters().set_precision(19);
  f.parameters().set_param_meta();
  f.parameters().set_result_flag(0xa5);
  f.parameters().get_param_flag().flag_ = 0x5a;
  f.parameters().set_raw_text_info(17, 23);

  char input_storage[sizeof(int64_t)] = {};
  ObDatum input;
  input.ptr_ = input_storage;
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
}

TEST(DynamicParamCapture, execution_slots_grow_after_input_frames_are_initialized) {
  Frame f;
  ASSERT_EQ(OB_SUCCESS, f.parameters().set_int(42, f.arena));
  f.parameters().set_precision(19);
  f.parameters().set_param_meta();
  auto *plan = f.exec.get_physical_plan_ctx();
  ASSERT_EQ(OB_SUCCESS, plan->init_datum_param_store());
  const auto *input_bytes = f.native().datum_.ptr_;
  const int64_t input_frame_count = plan->get_param_frame_ptrs().count();
  ASSERT_EQ(OB_SUCCESS, plan->reserve_param_space(2049));
  EXPECT_EQ(2049, plan->get_param_store().count());
  EXPECT_EQ(2049, plan->get_datum_param_store().count());
  EXPECT_EQ(42, f.native().datum_.get_int());
  EXPECT_EQ(input_bytes, f.native().datum_.ptr_);
  EXPECT_EQ(input_frame_count, plan->get_param_frame_ptrs().count());
  EXPECT_TRUE(plan->get_datum_param_store().at(2048).is_null());
  ObEvalCtx ctx(f.exec);
  ObDynamicParamSetter setter(2048, nullptr, &f.expr);
  auto value = f.integer(77);
  ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(ctx, value.datum_));
  EXPECT_EQ(77, plan->get_datum_param_store().at(2048).datum_.get_int());
  EXPECT_EQ(77, plan->get_param_store().at(2048).get_int());
  ASSERT_EQ(OB_SUCCESS, plan->reserve_param_space(2));
  EXPECT_EQ(2049, plan->get_datum_param_store().count());
  EXPECT_EQ(77, plan->get_datum_param_store().at(2048).datum_.get_int());
}

TEST(DynamicParamCapture, both_store_accessors_share_reset_and_reserve_state) {
  Frame f;
  auto *plan = f.exec.get_physical_plan_ctx();
  ASSERT_EQ(&plan->get_param_store_for_update(), &plan->get_datum_param_store());
  plan->get_datum_param_store().reuse();
  EXPECT_EQ(0, plan->get_param_store().count());
  ASSERT_EQ(OB_SUCCESS, plan->reserve_param_space(5));
  ASSERT_EQ(5, plan->get_datum_param_store().count());
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(plan->get_param_store().at(i).is_null());
  }
  ASSERT_EQ(OB_SUCCESS, f.parameters().set_int(42, f.arena));
  f.parameters().set_precision(19); f.parameters().set_raw_text_info(17, 23);
  ASSERT_EQ(OB_SUCCESS, plan->reserve_param_space(7));
  EXPECT_EQ(42, f.native().get_int()); EXPECT_EQ(19, f.native().get_precision());
  EXPECT_EQ(17, f.native().get_raw_text_pos()); EXPECT_EQ(23, f.native().get_raw_text_len());
  EXPECT_EQ(OB_INVALID_ARGUMENT, plan->reserve_param_space(-1));
  EXPECT_EQ(7, plan->get_param_store().count());
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
  ASSERT_EQ(OB_SUCCESS, plan.get_param_store_for_update().at(0).set_int(42, allocator));
  const char *prefix = plan.get_datum_param_store().at(0).datum_.ptr_;
  const int64_t large = plan.get_param_store().get_block_size() / sizeof(DatumParameter) * 8;
  allocator.reject_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan.reserve_param_space(large));
  EXPECT_EQ(1, plan.get_param_store().count());
  EXPECT_EQ(prefix, plan.get_datum_param_store().at(0).datum_.ptr_);
  EXPECT_EQ(42, plan.get_param_store().at(0).get_int());
  allocator.reject_ = false;
  ASSERT_EQ(OB_SUCCESS, plan.reserve_param_space(large));
  EXPECT_EQ(large, plan.get_datum_param_store().count());
  EXPECT_EQ(42, plan.get_param_store().at(0).get_int());
  EXPECT_TRUE(plan.get_param_store().at(large - 1).is_null());
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
  ASSERT_NE(source.datum_.ptr_, snapshot.datum_.ptr_);
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
  const char *original = previous.datum_.ptr_;
  RejectParameterGrowth allocator; allocator.reject_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
            ObUserDefinedType::clone_parameter(allocator, source, previous));
  EXPECT_EQ(original, previous.datum_.ptr_); EXPECT_EQ(55, previous.get_int());
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
  EXPECT_EQ(16, sizeof(state.get_vars().at(0)));
  ObArenaAllocator transient;
  DatumParameter integer, text;
  ASSERT_EQ(OB_SUCCESS, integer.set_int(42, transient));
  ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString("package payload"), transient));
  text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(0, integer));
  ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(1, text));
  EXPECT_NE(integer.datum_.ptr_, state.get_vars().at(0).datum_.ptr_);
  EXPECT_NE(text.datum_.ptr_, state.get_vars().at(1).datum_.ptr_);
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
  const char *previous = borrowed.datum_.ptr_;
  ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(0, borrowed));
  ASSERT_EQ(OB_SUCCESS, state.get_package_var_val(0, borrowed));
  EXPECT_EQ(17, borrowed.get_int());
  DatumParameter invalid;
  SqlTypeMeta array_meta; array_meta.set_ext(); array_meta.set_extend_type(T_EXT_SQL_ARRAY);
  invalid.set_type_meta(array_meta);
  ASSERT_EQ(OB_SUCCESS, invalid.alloc_datum_reserved_buff(array_meta, -1, arena));
  invalid.set_array_address<DatumParameterArray>(nullptr);
  previous = borrowed.datum_.ptr_;
  EXPECT_EQ(OB_INVALID_ARGUMENT, state.set_package_var_val(0, invalid));
  ASSERT_EQ(OB_SUCCESS, state.get_package_var_val(0, borrowed));
  EXPECT_EQ(17, borrowed.get_int()); EXPECT_EQ(previous, borrowed.datum_.ptr_);
  EXPECT_EQ(OB_ARRAY_OUT_OF_RANGE, state.set_package_var_val(-1, input));
  state.reset(nullptr);
}

TEST(DynamicParamCapture, package_cursor_adoption_owns_descriptor_and_preserves_handle) {
  using namespace oceanbase::pl;
  ObPLPackageState state(42, ObPackageStateVersion(1, 1)); ASSERT_EQ(OB_SUCCESS, state.init());
  DatumParameter empty, cursor;
  ASSERT_EQ(OB_SUCCESS, state.add_package_var_val(empty, PL_CURSOR_TYPE));
  ASSERT_EQ(OB_SUCCESS, ObSPIService::spi_cursor_alloc(state.get_pkg_cursor_allocator(), cursor));
  const int64_t handle = cursor.get_ext(); const char *temporary = cursor.datum_.ptr_;
  ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(0, cursor, false));
  EXPECT_NE(temporary, state.get_vars().at(0).datum_.ptr_);
  state.get_pkg_cursor_allocator().free(const_cast<char *>(temporary));
  cursor.set_null();
  DatumParameter observed; ASSERT_EQ(OB_SUCCESS, state.get_package_var_val(0, observed));
  EXPECT_EQ(handle, observed.get_ext());
  ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(0, observed));
  ASSERT_EQ(OB_SUCCESS, state.get_package_var_val(0, observed)); EXPECT_EQ(handle, observed.get_ext());
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
  value.set_precision(19); const char *previous = value.datum_.ptr_;
  ObPLDataType declaration(ObIntType);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, declaration.init_session_var(resolve, reject, exec, nullptr, false, value));
  EXPECT_EQ(previous, value.datum_.ptr_); EXPECT_EQ(51, value.get_int()); EXPECT_EQ(19, value.get_precision());
}

TEST(DynamicParamCapture, repeated_package_scalar_assignment_reuses_owned_payload) {
  using namespace oceanbase::pl;
  ObPLPackageState state(42, ObPackageStateVersion(1, 1)); ASSERT_EQ(OB_SUCCESS, state.init());
  DatumParameter empty;
  ASSERT_EQ(OB_SUCCESS, state.add_package_var_val(empty, PL_OBJ_TYPE));
  ObArenaAllocator arena;
  DatumParameter input; ASSERT_EQ(OB_SUCCESS, input.set_int(0, arena));
  ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(0, input));
  const char *owned = state.get_vars().at(0).datum_.ptr_;
  auto &allocator = static_cast<ObPLAllocator1 &>(state.get_pkg_allocator());
  const int64_t used = allocator.get_used();
  for (int64_t i = 1; i <= 1000; ++i) {
    input.datum_.set_int(i);
    ASSERT_EQ(OB_SUCCESS, state.set_package_var_val(0, input));
    ASSERT_EQ(owned, state.get_vars().at(0).datum_.ptr_);
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
  const char *owned = state.get_vars().at(0).datum_.ptr_;
  char truncated = 3;
  input.datum_.set_string(&truncated, 1);
  EXPECT_EQ(OB_INVALID_ARGUMENT, state.set_package_var_val(0, input));
  EXPECT_EQ(owned, state.get_vars().at(0).datum_.ptr_);
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
    EXPECT_NE(source.datum_.ptr_, output.datum_.ptr_);
  }
  EXPECT_EQ(ObString::make_string("owned"), output.get_string());
  EXPECT_EQ(19, output.get_precision()); EXPECT_EQ(123, output.get_result_flag());
  EXPECT_EQ(ObIntType, output.get_param_meta().get_type());
  EXPECT_EQ(2, output.get_raw_text_pos()); EXPECT_EQ(9, output.get_raw_text_len());
}

TEST(DynamicParamCapture, native_out_parameter_shared_composite_copies_descriptor_only) {
  using namespace oceanbase::pl;
  ObArenaAllocator caller;
  DatumParameter output;
  const int64_t handle = 0x1234; // opaque identity; this path must not dereference it
  ASSERT_EQ(OB_SUCCESS, output.set_extend(handle, PL_RECORD_TYPE, caller, 64));
  {
    ObArenaAllocator callee;
    DatumParameter source;
    ASSERT_EQ(OB_SUCCESS, source.set_extend(handle, PL_RECORD_TYPE, callee, 64));
    source.set_udt_id(456);
    ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::copy_out_parameter(caller, source, output));
    EXPECT_NE(source.datum_.ptr_, output.datum_.ptr_);
  }
  EXPECT_EQ(handle, output.get_ext()); EXPECT_EQ(64, output.get_val_len());
  EXPECT_EQ(456, output.get_udt_id());
}

TEST(DynamicParamCapture, native_out_parameter_self_copy_and_oom_are_transactional) {
  RejectParameterGrowth owner;
  DatumParameter value;
  ASSERT_EQ(OB_SUCCESS, value.set_int(42, owner));
  const char *previous = value.datum_.ptr_;
  owner.reject_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
      oceanbase::pl::ObUserDefinedType::copy_out_parameter(owner, value, value));
  EXPECT_EQ(42, value.get_int()); EXPECT_EQ(previous, value.datum_.ptr_);
  owner.reject_ = false;
  ASSERT_EQ(OB_SUCCESS, oceanbase::pl::ObUserDefinedType::copy_out_parameter(owner, value, value));
  EXPECT_EQ(42, value.get_int()); EXPECT_NE(previous, value.datum_.ptr_);
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
    EXPECT_NE(text.datum_.ptr_, record->get_element()[0].datum_.ptr_);
  }
  EXPECT_EQ(ObString::make_string("record"), record->get_element()[0].get_string());
  DatumValue &field = record->get_element()[0];
  const char *old = field.datum_.ptr_;
  ASSERT_EQ(OB_SUCCESS, ObPLComposite::copy_element(field, field, *record->get_allocator(),
      nullptr, nullptr, nullptr, nullptr, nullptr, false));
  EXPECT_EQ(old, field.datum_.ptr_);
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
    ASSERT_EQ(1, params.count()); EXPECT_NE(input.datum_.ptr_, params.at(0).datum_.ptr_);
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
  ASSERT_EQ(1, params.count()); EXPECT_NE(field.datum_.ptr_, params.at(0).datum_.ptr_);
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
  ASSERT_EQ(OB_SUCCESS, input.set_extend(reinterpret_cast<int64_t>(&write), PL_RECORD_TYPE, owner));
  ObIAllocator *actual_owner = nullptr;
  DatumParameter restored;
  ASSERT_EQ(OB_SUCCESS, ObExprUDF::extract_allocator_and_restore_obj(input, restored, actual_owner, owner));
  EXPECT_EQ(&field_owner, actual_owner);
  EXPECT_EQ(reinterpret_cast<int64_t>(&field), restored.get_ext());
  EXPECT_NE(input.datum_.ptr_, restored.datum_.ptr_);
  RejectParameterGrowth reject; reject.reject_ = true;
  DatumParameter previous; ASSERT_EQ(OB_SUCCESS, previous.set_int(123, owner));
  actual_owner = nullptr;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
      ObExprUDF::extract_allocator_and_restore_obj(input, previous, actual_owner, reject));
  EXPECT_EQ(123, previous.get_int()); EXPECT_EQ(nullptr, actual_owner);
}

TEST(DynamicParamCapture, native_expression_capture_keeps_accuracy_scale_and_lob_metadata_separate) {
  Frame f; ObEvalCtx ctx(f.exec);
  f.expr.locate_datum_for_write(ctx).set_int(42);
  f.expr.datum_meta_.scale_ = 3;
  f.expr.datum_meta_.precision_ = 12;
  DatumParameter captured;
  capture_datum_parameter(f.expr, ctx, captured);
  EXPECT_EQ(f.expr.locate_expr_datum(ctx).ptr_, captured.datum_.ptr_);
  EXPECT_EQ(42, captured.get_int()); EXPECT_EQ(3, captured.get_meta().get_scale());
  EXPECT_EQ(3, captured.get_scale()); EXPECT_EQ(12, captured.get_precision());
  f.expr.obj_meta_.set_type(ObLongTextType); f.expr.obj_meta_.set_has_lob_header();
  f.expr.obj_meta_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  f.expr.datum_meta_.length_semantics_ = LS_CHAR;
  capture_datum_parameter(f.expr, ctx, captured);
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
