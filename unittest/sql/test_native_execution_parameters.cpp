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

#include "common/datum/datum_copy_plan.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/charset/ob_charset.h"
#include "sql/engine/expr/ob_expr.h"
#include "sql/engine/expr/ob_expr_res_type.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/engine/table/native_point_get_input.h"
#include "sql/ob_sql_context.h"
#include "sql/ob_sql_utils.h"
#include "sql/rewrite/ob_query_range_define.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <type_traits>

namespace oceanbase
{
namespace sql
{
int eval_question_mark_func(const ObExpr &, ObEvalCtx &, Datum &);
}
}

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
TEST(NativeExpressionMetadata, resolved_scalar_preserves_normalized_execution_flags)
{
  ObArenaAllocator owner("ExprMetaTest");
  ObRawExprResType raw;
  raw.set_type(ObLongTextType);
  raw.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  raw.set_length(4096);
  ObExpr expression;
  expression.obj_meta_ = raw.get_sql_type_meta();
  expression.obj_meta_.set_has_lob_header();
  ASSERT_FALSE(raw.get_sql_type_meta().has_lob_header());
  ASSERT_EQ(OB_SUCCESS, expression.prepare_resolved_type(raw, nullptr, owner));
  EXPECT_TRUE(expression.get_sql_type().legacy_meta().has_lob_header());
  EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, expression.get_sql_type().collation());
  EXPECT_EQ(4096, expression.get_sql_type().length());

  raw.reset();
  raw.set_type(ObBitType);
  raw.set_accuracy(ObAccuracy(64, 0));
  expression.obj_meta_ = raw.get_sql_type_meta();
  expression.obj_meta_.set_scale(37); // The CG's datum width encoding.
  ASSERT_EQ(OB_SUCCESS, expression.prepare_resolved_type(raw, nullptr, owner));
  EXPECT_EQ(37, expression.get_sql_type().legacy_meta().get_scale());
  EXPECT_EQ(raw.get_accuracy(), expression.get_sql_type().accuracy());
}

TEST(NativeExpressionMetadata, rebound_inline_frame_has_no_dynamic_string_header)
{
  ObArenaAllocator owner("ExprMetaTest");
  ObExecContext exec(owner);
  ObEvalCtx eval(exec);
  alignas(8) char frame[ScalarFrameLayout::slot_size()] = {};
  char *frames[] = {frame};
  exec.set_frames(frames);
  eval.frames_ = frames;
  ObExpr expression;
  expression.frame_idx_ = 0;
  expression.datum_meta_.type_ = ObVarcharType;
  expression.dyn_buf_header_offset_ = 0;
  expression.res_buf_len_ = 0;
  Datum *slot = new (frame) Datum();
  slot->set_int(INT64_MIN);
  char *buffer = reinterpret_cast<char *>(1);
  int64_t bytes = -1;
  expression.cur_str_resvered_buf(eval, buffer, bytes);
  EXPECT_EQ(nullptr, buffer);
  EXPECT_EQ(0, bytes);
  buffer = expression.get_str_res_mem(eval, 513);
  ASSERT_NE(nullptr, buffer);
  EXPECT_EQ(INT64_MIN, slot->get_int());
}

class ExecutionAllocator final : public ObIAllocator
{
public:
  ExecutionAllocator() : arena_("ExecParamTest"), sizes_(), fail_size_(0) {}
  void *alloc(int64_t size) override
  {
    sizes_.push_back(size);
    return size == fail_size_ ? nullptr : arena_.alloc(size);
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *pointer) override { arena_.free(pointer); }
  int64_t calls_for(int64_t size) const
  {
    int64_t count = 0;
    for (int64_t actual : sizes_) { count += actual == size; }
    return count;
  }
  void clear_calls() { sizes_.clear(); }
  ObArenaAllocator arena_;
  std::vector<int64_t> sizes_;
  int64_t fail_size_;
};

void expect_metadata(const DatumParameter &expected, const DatumParameter &actual)
{
  EXPECT_EQ(expected.get_type_meta(), actual.get_type_meta());
  EXPECT_EQ(expected.get_type_meta().get_scale(), actual.get_type_meta().get_scale());
  EXPECT_EQ(expected.get_accuracy(), actual.get_accuracy());
  EXPECT_EQ(expected.get_param_meta(), actual.get_param_meta());
  EXPECT_EQ(expected.get_param_meta().get_scale(), actual.get_param_meta().get_scale());
  EXPECT_EQ(expected.get_param_flag().flag_, actual.get_param_flag().flag_);
  EXPECT_EQ(expected.get_param_flag().extend_flag_, actual.get_param_flag().extend_flag_);
  EXPECT_EQ(expected.get_result_flag(), actual.get_result_flag());
  EXPECT_EQ(expected.get_raw_text_pos(), actual.get_raw_text_pos());
  EXPECT_EQ(expected.get_raw_text_len(), actual.get_raw_text_len());
  EXPECT_EQ(expected.datum_.pack_, actual.datum_.pack_);
}

class NativeExecutionParameters : public ::testing::Test
{
protected:
  NativeExecutionParameters()
    : request_arena_("ExecParamReq"), owner_(), sql_ctx_(), exec_(owner_),
      request_type_(), request_values_(), graph_(request_arena_), node_(request_arena_),
      columns_(), start_{0, 1, 2}, end_{0, 1, 2} {}

  void SetUp() override
  {
    sql_ctx_.exec_type_ = MpQuery;
    sql_ctx_.is_prepare_protocol_ = true;
    sql_ctx_.is_dynamic_sql_ = true; // The real ordinary-PS path sets this too.
    sql_ctx_.stmt_type_ = stmt::T_SELECT;
    exec_.set_sql_ctx(&sql_ctx_);
    ASSERT_EQ(OB_SUCCESS, exec_.create_physical_plan_ctx());
    SqlTypeMeta meta; meta.set_int(); meta.set_scale(0);
    meta.set_collation_level(CS_LEVEL_COERCIBLE);
    ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(20, 0), request_type_));

    ObSEArray<ColumnItem, 3> items;
    for (int64_t i = 0; i < 3; ++i) {
      columns_[i].set_ref_id(1001, 16 + i);
      columns_[i].set_data_type(ObIntType);
      ColumnItem item; item.expr_ = &columns_[i]; item.set_ref_id(1001, 16 + i);
      ASSERT_EQ(OB_SUCCESS, items.push_back(item));
    }
    ASSERT_EQ(OB_SUCCESS, graph_.fill_column_metas(items));
    ASSERT_EQ(OB_SUCCESS, graph_.get_range_map().expr_final_infos_.prepare_allocate(3));
    for (int64_t i = 0; i < 3; ++i) {
      auto &info = graph_.get_range_map().expr_final_infos_.at(i);
      info.is_param_ = true; info.param_idx_ = i;
    }
    node_.column_cnt_ = 3; node_.start_keys_ = start_; node_.end_keys_ = end_;
    node_.min_offset_ = 0; node_.max_offset_ = 2;
    node_.include_start_ = true; node_.include_end_ = true;
    graph_.set_range_head(&node_); graph_.set_node_count(1);
    graph_.set_is_precise_get(true); graph_.set_is_standard_range(true);
    graph_.set_is_equal_range(true); graph_.set_is_get(true);
  }

  ObPhysicalPlanCtx &plan() { return *exec_.get_physical_plan_ctx(); }
  const DatumParamStore &store() { return plan().get_param_store(); }

  void load(const std::vector<int64_t> &values)
  {
    plan().reset_datum_param_store();
    request_values_.resize(values.size());
    const DatumCopyPlan copy(request_type_);
    for (size_t i = 0; i < values.size(); ++i) {
      request_values_[i].set_int(values[i]);
      DatumParameter parameter;
      parameter.set_type_meta(request_type_.legacy_meta());
      parameter.set_accuracy(ObAccuracy(20, 0));
      parameter.set_param_meta();
      parameter.set_length(0); // Same scalar wire post-processing as the binder.
      parameter.set_need_to_check_type(true);
      parameter.set_need_to_check_bool_value(i % 2 == 0);
      parameter.set_result_flag(i % 2 == 0 ? NOT_NULL_FLAG : ZEROFILL_FLAG);
      parameter.set_raw_text_info(i * 2, 1);
      ASSERT_EQ(OB_SUCCESS, copy.borrow_legacy(request_values_[i], parameter.datum_));
      ASSERT_EQ(OB_SUCCESS, plan().append_parameter_input(parameter));
    }
    plan().set_original_param_cnt(values.size());
    owner_.clear_calls();
  }

  void expect_questionmark(int64_t index, int64_t expected)
  {
    ObEvalCtx eval(exec_);
    ObExpr expr; expr.extra_ = index; expr.datum_meta_.type_ = ObIntType;
    Datum value;
    ASSERT_EQ(OB_SUCCESS, eval_question_mark_func(expr, eval, value));
    ASSERT_FALSE(value.is_null());
    EXPECT_EQ(expected, value.get_int());
    EXPECT_EQ(expected, plan().get_native_parameter(index)->get_int());
  }

  void expect_point(int64_t first, int64_t second, int64_t third)
  {
    Datum values[3]; bool supported = false;
    const ObQueryRangeProvider &provider = graph_;
    ASSERT_EQ(OB_SUCCESS, provider.try_get_native_bigint_point(plan(), values, supported));
    ASSERT_TRUE(supported);
    EXPECT_EQ(first, values[0].get_int()); EXPECT_EQ(second, values[1].get_int());
    EXPECT_EQ(third, values[2].get_int());
  }

  void expect_no_point()
  {
    Datum values[3];
    for (int64_t i = 0; i < 3; ++i) { values[i].set_int(101 + i); }
    bool supported = true;
    ASSERT_EQ(OB_SUCCESS, graph_.try_get_native_bigint_point(plan(), values, supported));
    EXPECT_FALSE(supported);
    for (int64_t i = 0; i < 3; ++i) { EXPECT_EQ(101 + i, values[i].get_int()); }
  }

  ObArenaAllocator request_arena_;
  ExecutionAllocator owner_;
  ObSqlCtx sql_ctx_;
  ObExecContext exec_;
  SqlType request_type_;
  std::vector<Datum> request_values_;
  ObPreRangeGraph graph_;
  ObRangeNode node_;
  ObColumnRefRawExpr columns_[3];
  int64_t start_[3];
  int64_t end_[3];
};
} // namespace


TEST_F(NativeExecutionParameters, arbitrary_count_uses_one_owner_and_real_questionmark_frames)
{
  static_assert(sizeof(Datum) == 16 && alignof(Datum) == 8, "native value ABI");
  static_assert(std::is_same<decltype(std::declval<ObPhysicalPlanCtx &>().get_param_store()),
      const ParamStore &>::value, "execution descriptors cannot escape as mutable storage");
  const int64_t per_page = MAX_FRAME_SIZE / 24;
  for (int64_t count : {int64_t(1), int64_t(3), int64_t(37), per_page, per_page + 1}) {
    SCOPED_TRACE(count);
    std::vector<int64_t> input(count);
    for (int64_t i = 0; i < count; ++i) { input[i] = i * 13 - 7; }
    input.front() = INT64_MIN;
    if (count > 1) { input.back() = INT64_MAX; }
    ASSERT_NO_FATAL_FAILURE(load(input));
    std::vector<DatumParameter> before;
    for (int64_t i = 0; i < count; ++i) { before.push_back(store().at(i)); }
    ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
    EXPECT_EQ((count + per_page - 1) / per_page, plan().get_param_frame_ptrs().count());
    for (int64_t i = 0; i < count; ++i) {
      const Datum *native = plan().get_native_parameter(i);
      ASSERT_NE(nullptr, native);
      const char *expected_slot = plan().get_param_frame_ptrs().at(i / per_page) + (i % per_page) * 24;
      EXPECT_EQ(reinterpret_cast<const Datum *>(expected_slot), native);
      EXPECT_EQ(0u, reinterpret_cast<uintptr_t>(native) % 8);
      EXPECT_EQ(input[i], native->get_int());
      const SqlType *type = plan().get_native_parameter_type(i);
      ASSERT_NE(nullptr, type);
      EXPECT_EQ(ObIntType, type->kind());
      ObDatum borrowed;
      ASSERT_EQ(OB_SUCCESS, DatumCopyPlan(*type).borrow_legacy(*native, borrowed));
      EXPECT_EQ(borrowed.ptr_, store().at(i).datum_.ptr_);
      EXPECT_NE(before[i].datum_.ptr_, borrowed.ptr_);
      expect_metadata(before[i], store().at(i));
      request_values_[i].set_int(0x5a5a5a5a);
      ASSERT_NO_FATAL_FAILURE(expect_questionmark(i, input[i]));
    }
    EXPECT_EQ(nullptr, plan().get_native_parameter(-1));
    EXPECT_EQ(nullptr, plan().get_native_parameter(count));
    EXPECT_EQ(nullptr, plan().get_native_parameter_type(count));
  }
}

TEST_F(NativeExecutionParameters, graph_reorders_repeats_execution_indices_and_storage_keeps_its_snapshot)
{
  ASSERT_NO_FATAL_FAILURE(load({10, 20, 30, 40, 50}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  start_[0] = end_[0] = 2; start_[1] = end_[1] = 0; start_[2] = end_[2] = 1;
  graph_.get_range_map().expr_final_infos_.at(0).param_idx_ = 4;
  graph_.get_range_map().expr_final_infos_.at(1).param_idx_ = 4;
  graph_.get_range_map().expr_final_infos_.at(2).param_idx_ = 1;
  ASSERT_NO_FATAL_FAILURE(expect_point(20, 50, 50));
  Datum values[3]; bool supported = false;
  ASSERT_EQ(OB_SUCCESS, graph_.try_get_native_bigint_point(plan(), values, supported));
  ASSERT_TRUE(supported);
  NativePointGetInput key;
  ASSERT_EQ(OB_SUCCESS, key.init(values, 0));
  for (int index : {1, 4}) {
    DatumParameter replacement = store().at(index);
    ASSERT_EQ(OB_SUCCESS, replacement.set_int(index == 1 ? -20 : -50, request_arena_));
    ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(index, replacement));
  }
  ASSERT_NO_FATAL_FAILURE(expect_point(-20, -50, -50));
  EXPECT_EQ(20, key.values()[0]); EXPECT_EQ(50, key.values()[1]); EXPECT_EQ(50, key.values()[2]);
  ASSERT_EQ(1, key.lookup_keys().count());
  EXPECT_EQ(20, key.lookup_keys().at(0).datums_[0].get_int());
  start_[0] = start_[1] = start_[2] = end_[0] = end_[1] = end_[2] = 0;
  ASSERT_NO_FATAL_FAILURE(expect_point(-50, -50, -50));
}

TEST_F(NativeExecutionParameters, runtime_rebind_updates_native_frame_type_and_compatibility_view)
{
  ASSERT_NO_FATAL_FAILURE(load({INT64_MIN, 0, INT64_MAX}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const Datum *slot = plan().get_native_parameter(1);
  DatumParameter replacement = store().at(1);
  ASSERT_EQ(OB_SUCCESS, replacement.set_int(-77, request_arena_));
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(1, replacement));
  EXPECT_EQ(slot, plan().get_native_parameter(1));
  EXPECT_EQ(-77, slot->get_int());
  EXPECT_EQ(-77, store().at(1).get_int());
  ASSERT_NO_FATAL_FAILURE(expect_questionmark(1, -77));
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(1, store().at(1))); // self-rebind
  ASSERT_NO_FATAL_FAILURE(expect_point(INT64_MIN, -77, INT64_MAX));
  ASSERT_EQ(OB_SUCCESS, replacement.set_uint64(UINT64_MAX, request_arena_));
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(1, replacement));
  EXPECT_EQ(slot, plan().get_native_parameter(1));
  EXPECT_EQ(UINT64_MAX, slot->get_uint());
  EXPECT_EQ(ObUInt64Type, plan().get_native_parameter_type(1)->kind());
  ASSERT_NO_FATAL_FAILURE(expect_no_point());
  replacement.datum_.set_null(); // typed NULL keeps the UInt declaration
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(1, replacement));
  EXPECT_TRUE(slot->is_null());
  EXPECT_EQ(ObUInt64Type, plan().get_native_parameter_type(1)->kind());
  EXPECT_TRUE(store().at(1).is_null());
  EXPECT_EQ(OB_INVALID_ARGUMENT, plan().set_runtime_parameter(-1, replacement));
  EXPECT_EQ(OB_INVALID_ARGUMENT, plan().set_runtime_parameter(3, replacement));
}

TEST_F(NativeExecutionParameters, string_type_switch_owns_bytes_and_allocation_failure_preserves_binding)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const Datum *slot = plan().get_native_parameter(1);
  const SqlType *type_before = plan().get_native_parameter_type(1);
  std::string text(409, 'q');
  DatumParameter replacement;
  replacement.set_varchar_view(ObString(text.size(), &text[0]));
  replacement.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  replacement.set_length(text.size());
  owner_.fail_size_ = text.size();
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan().set_runtime_parameter(1, replacement));
  EXPECT_EQ(2, slot->get_int());
  EXPECT_EQ(type_before, plan().get_native_parameter_type(1));
  EXPECT_EQ(2, store().at(1).get_int());
  owner_.fail_size_ = 0;
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(1, replacement));
  EXPECT_EQ(ObVarcharType, plan().get_native_parameter_type(1)->kind());
  EXPECT_NE(text.data(), slot->get_string().ptr());
  text.assign(text.size(), 'x');
  EXPECT_EQ(std::string(409, 'q'), std::string(slot->get_string().ptr(), slot->get_string().length()));
  ObEvalCtx eval(exec_); ObExpr expression;
  expression.extra_ = 1; expression.datum_meta_.type_ = ObVarcharType;
  Datum result;
  ASSERT_EQ(OB_SUCCESS, eval_question_mark_func(expression, eval, result));
  EXPECT_EQ(slot->get_string(), result.get_string());
}

TEST_F(NativeExecutionParameters, malformed_runtime_value_does_not_publish_type_or_value)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const Datum *slot = plan().get_native_parameter(1);
  const SqlType *type = plan().get_native_parameter_type(1);
  DatumParameter invalid = store().at(1);
  invalid.datum_.set_outrow(); // outrow is not a signed integer representation
  EXPECT_NE(OB_SUCCESS, plan().set_runtime_parameter(1, invalid));
  EXPECT_EQ(2, slot->get_int());
  EXPECT_EQ(type, plan().get_native_parameter_type(1));
  EXPECT_EQ(2, store().at(1).get_int());
}

TEST_F(NativeExecutionParameters, metadata_changes_prepare_the_actual_declaration)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  DatumParameter replacement = store().at(1);
  replacement.set_precision(11);
  replacement.set_scale(1);
  replacement.get_meta().set_collation_level(CS_LEVEL_NUMERIC);
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(1, replacement));
  const SqlType &type = *plan().get_native_parameter_type(1);
  EXPECT_EQ(11, type.precision()); EXPECT_EQ(1, type.scale());
  EXPECT_EQ(CS_LEVEL_NUMERIC, type.legacy_meta().get_collation_level());
  expect_metadata(replacement, store().at(1));
  ASSERT_NO_FATAL_FAILURE(expect_questionmark(1, 2));
}

TEST_F(NativeExecutionParameters, grouped_array_preserves_inputs_and_updates_effective_native_type)
{
  plan().reset_datum_param_store();
  DatumParameterArray *array = DatumParameterArray::alloc(request_arena_, 2);
  ASSERT_NE(nullptr, array);
  array->element_.set_obj_type(ObIntType);
  ASSERT_EQ(OB_SUCCESS, array->data_[0].set_int(10, request_arena_));
  ASSERT_EQ(OB_SUCCESS, array->data_[1].set_uint64(UINT64_MAX, request_arena_));
  DatumParameter input;
  ASSERT_EQ(OB_SUCCESS, input.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, request_arena_));
  ASSERT_EQ(OB_SUCCESS, plan().append_parameter_input(input));
  plan().set_original_param_cnt(1);
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const Datum *slot = plan().get_native_parameter(0);
  ASSERT_NE(nullptr, slot); EXPECT_EQ(10, slot->get_int());
  EXPECT_EQ(ObIntType, plan().get_native_parameter_type(0)->kind());
  ASSERT_TRUE(store().at(0).is_ext_sql_array());
  EXPECT_NE(array, store().at(0).get_array<DatumParameterArray>());
  array->data_[0].datum_.set_int(999);
  ASSERT_EQ(OB_SUCCESS, plan().replace_batch_param_datum(1, 0, 1));
  EXPECT_EQ(slot, plan().get_native_parameter(0));
  EXPECT_EQ(UINT64_MAX, slot->get_uint());
  EXPECT_EQ(ObUInt64Type, plan().get_native_parameter_type(0)->kind());
  ASSERT_TRUE(store().at(0).is_ext_sql_array());
  ASSERT_EQ(OB_SUCCESS, plan().replace_batch_param_datum(0, 0, 1));
  EXPECT_EQ(10, slot->get_int());
  EXPECT_EQ(ObIntType, plan().get_native_parameter_type(0)->kind());
  EXPECT_NE(OB_SUCCESS, plan().replace_batch_param_datum(2, 0, 1));
  EXPECT_EQ(10, slot->get_int());
}

TEST_F(NativeExecutionParameters, all_query_contexts_use_the_same_native_parameter_abi)
{
  for (int gate = 0; gate < 11; ++gate) {
    SCOPED_TRACE(gate);
    ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
    if (gate == 0) { sql_ctx_.is_prepare_protocol_ = false; }
    else if (gate == 1) { sql_ctx_.exec_type_ = PLSql; }
    else if (gate == 2) { sql_ctx_.is_cursor_ = true; }
    else if (gate == 3) { sql_ctx_.is_prepare_stage_ = true; }
    else if (gate == 4) { sql_ctx_.is_mock_prepare_ = true; }
    else if (gate == 5) { sql_ctx_.is_text_ps_mode_ = true; }
    else if (gate == 6) { sql_ctx_.is_execute_call_stmt_ = true; }
    else if (gate == 7) { sql_ctx_.stmt_type_ = stmt::T_CALL_PROCEDURE; }
    else if (gate == 8) { sql_ctx_.set_is_do_insert_batch_opt(2); }
    else if (gate == 9) { ASSERT_EQ(OB_SUCCESS, plan().get_array_param_groups().push_back(ObArrayParamGroup(2, 3, 0))); }
    else { exec_.set_sql_ctx(nullptr); }
    ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
    for (int64_t i = 0; i < 3; ++i) {
      ASSERT_NE(nullptr, plan().get_native_parameter(i));
      EXPECT_EQ(i + 1, plan().get_native_parameter(i)->get_int());
    }
    ASSERT_NO_FATAL_FAILURE(expect_point(1, 2, 3));
    sql_ctx_.is_prepare_protocol_ = true; sql_ctx_.exec_type_ = MpQuery;
    sql_ctx_.is_cursor_ = false; sql_ctx_.is_prepare_stage_ = false; sql_ctx_.is_mock_prepare_ = false;
    sql_ctx_.is_text_ps_mode_ = false; sql_ctx_.is_execute_call_stmt_ = false;
    sql_ctx_.stmt_type_ = stmt::T_SELECT; sql_ctx_.reset_do_insert_batch_opt();
    plan().get_array_param_groups().reset(); exec_.set_sql_ctx(&sql_ctx_);
  }
}

TEST_F(NativeExecutionParameters, rebuild_and_reset_keep_old_frame_borrows_alive_until_arena_end)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 7, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const Datum *old_slot = plan().get_native_parameter(1);
  const ObDatum borrowed = store().at(1).datum_;
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  EXPECT_NE(old_slot, plan().get_native_parameter(1));
  DatumParameter replacement = store().at(1);
  ASSERT_EQ(OB_SUCCESS, replacement.set_int(42, request_arena_));
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(1, replacement));
  EXPECT_EQ(7, old_slot->get_int()); EXPECT_EQ(7, borrowed.get_int());
  ASSERT_NO_FATAL_FAILURE(expect_questionmark(1, 42));
  plan().reset_datum_param_store();
  EXPECT_EQ(nullptr, plan().get_native_parameter(1));
  EXPECT_EQ(7, old_slot->get_int());
  ASSERT_NO_FATAL_FAILURE(load({4, 5, 6}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ASSERT_NO_FATAL_FAILURE(expect_point(4, 5, 6));
  plan().destroy();
  EXPECT_EQ(nullptr, plan().get_native_parameter(1));
  EXPECT_EQ(7, old_slot->get_int());
}

TEST_F(NativeExecutionParameters, append_and_truncate_keep_original_and_beyond_pages_separate)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const int64_t per_page = MAX_FRAME_SIZE / 24;
  ASSERT_EQ(OB_SUCCESS, plan().reserve_param_space(3 + per_page + 1));
  ASSERT_EQ(3, plan().get_param_frame_ptrs().count());
  EXPECT_EQ(reinterpret_cast<const Datum *>(plan().get_param_frame_ptrs().at(1)), plan().get_native_parameter(3));
  EXPECT_EQ(reinterpret_cast<const Datum *>(plan().get_param_frame_ptrs().at(2)), plan().get_native_parameter(3 + per_page));
  EXPECT_TRUE(plan().get_native_parameter(3 + per_page)->is_null());
  DatumParameter replacement;
  ASSERT_EQ(OB_SUCCESS, replacement.set_int(-91, request_arena_));
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(3 + per_page, replacement));
  ASSERT_NO_FATAL_FAILURE(expect_questionmark(3 + per_page, -91));
  ASSERT_EQ(OB_SUCCESS, plan().truncate_runtime_parameters(3));
  EXPECT_EQ(nullptr, plan().get_native_parameter(3));
  EXPECT_EQ(nullptr, plan().get_native_parameter_type(3));
  EXPECT_EQ(3, plan().get_param_frame_ptrs().count()); // allocated pages stay stable
  ASSERT_NO_FATAL_FAILURE(expect_point(1, 2, 3));
}

TEST_F(NativeExecutionParameters, construction_staging_failure_cannot_mutate_live_slots)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const Datum *slot = plan().get_native_parameter(1);
  EXPECT_EQ(OB_ERR_UNEXPECTED, plan().edit_parameter_inputs([](ParamStore &inputs) {
    inputs.at(1).datum_.set_int(999);
    return OB_ERR_UNEXPECTED;
  }));
  EXPECT_EQ(slot, plan().get_native_parameter(1));
  EXPECT_EQ(2, slot->get_int());
  ASSERT_EQ(OB_SUCCESS, plan().edit_parameter_inputs([](ParamStore &inputs) {
    inputs.at(1).datum_.set_int(77);
    return OB_SUCCESS;
  }));
  EXPECT_EQ(nullptr, plan().get_native_parameter(1));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ASSERT_NO_FATAL_FAILURE(expect_questionmark(1, 77));
  EXPECT_EQ(2, slot->get_int());
}

TEST_F(NativeExecutionParameters, frame_or_type_allocation_failure_does_not_publish_a_binding)
{
  for (int64_t bytes : {int64_t(3 * 24), int64_t(sizeof(SqlType))}) {
    ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
    owner_.fail_size_ = bytes;
    ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan().init_datum_param_store());
    EXPECT_EQ(nullptr, plan().get_native_parameter(0));
    EXPECT_EQ(nullptr, plan().get_native_parameter_type(0));
    EXPECT_EQ(0, plan().get_param_frame_ptrs().count());
    for (int64_t i = 0; i < 3; ++i) { EXPECT_EQ(i + 1, store().at(i).get_int()); }
    owner_.fail_size_ = 0;
    ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
    ASSERT_NO_FATAL_FAILURE(expect_questionmark(1, 2));
  }
}

TEST_F(NativeExecutionParameters, failed_rebuild_revokes_current_owner_but_keeps_old_payload_borrow)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 7, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const ObDatum saved = store().at(1).datum_;
  owner_.fail_size_ = 3 * 24;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan().init_datum_param_store());
  EXPECT_EQ(nullptr, plan().get_native_parameter(1));
  EXPECT_EQ(saved.ptr_, store().at(1).datum_.ptr_);
  EXPECT_EQ(7, saved.get_int());
  EXPECT_EQ(0, plan().get_param_frame_ptrs().count());
  owner_.fail_size_ = 0;
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  EXPECT_NE(saved.ptr_, store().at(1).datum_.ptr_);
  EXPECT_EQ(7, saved.get_int());
}

TEST_F(NativeExecutionParameters, restored_final_pages_preserve_eval_info_and_failure_preserves_old_owner)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ObSEArray<char *, 1> pages; ObSEArray<const SqlType *, 3> types;
  char *page = static_cast<char *>(owner_.alloc(3 * 24));
  ASSERT_NE(nullptr, page); MEMSET(page, 0, 3 * 24);
  for (int64_t i = 0; i < 3; ++i) {
    new (page + i * 24) Datum();
    reinterpret_cast<Datum *>(page + i * 24)->set_int(101 + i);
    ObEvalInfo *info = reinterpret_cast<ObEvalInfo *>(page + i * 24 + 16);
    info->flag_ = 3; info->cnt_ = 17;
    ASSERT_EQ(OB_SUCCESS, types.push_back(plan().get_native_parameter_type(i)));
  }
  ASSERT_EQ(OB_SUCCESS, pages.push_back(page));
  const Datum *old = plan().get_native_parameter(0);
  reinterpret_cast<Datum *>(page + 2 * 24)->set_string(ObString::make_string("invalid inline integer"));
  EXPECT_NE(OB_SUCCESS, plan().restore_native_parameter_frames(pages, types, 3));
  EXPECT_EQ(old, plan().get_native_parameter(0)); EXPECT_EQ(1, old->get_int());
  reinterpret_cast<Datum *>(page + 2 * 24)->set_int(103);
  ASSERT_EQ(OB_SUCCESS, plan().restore_native_parameter_frames(pages, types, 3));
  EXPECT_EQ(reinterpret_cast<const Datum *>(page), plan().get_native_parameter(0));
  for (int64_t i = 0; i < 3; ++i) {
    EXPECT_EQ(101 + i, store().at(i).get_int());
    const ObEvalInfo *info = reinterpret_cast<const ObEvalInfo *>(page + i * 24 + 16);
    EXPECT_EQ(3, info->flag_); EXPECT_EQ(17, info->cnt_);
  }
  EXPECT_EQ(1, old->get_int());
}

TEST_F(NativeExecutionParameters, real_wire_roundtrip_requires_native_initialization_and_owns_decoded_payload)
{
  ASSERT_NO_FATAL_FAILURE(load({INT64_MIN, 7, INT64_MAX}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const ObDatum old_value = store().at(1).datum_;
  std::string wire(plan().get_serialize_size(), '\0');
  int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, plan().serialize(&wire[0], wire.size(), position));
  ASSERT_EQ(wire.size(), position);
  position = 0;
  ASSERT_EQ(OB_SUCCESS, plan().deserialize(wire.data(), wire.size(), position));
  EXPECT_EQ(wire.size(), position);
  EXPECT_EQ(nullptr, plan().get_native_parameter(1));
  ASSERT_EQ(3, store().count());
  EXPECT_NE(old_value.ptr_, store().at(1).datum_.ptr_);
  std::string bad_body = wire;
  int64_t header = 0, version = 0, body_length = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::decode(bad_body.data(), bad_body.size(), header, version));
  const int64_t length_position = header;
  ASSERT_EQ(OB_SUCCESS, serialization::decode(bad_body.data(), bad_body.size(), header, body_length));
  ASSERT_GT(body_length, 1);
  bad_body.resize(header + 1);
  int64_t patch_position = length_position;
  ASSERT_EQ(OB_SUCCESS, serialization::encode_fixed_bytes_i64(&bad_body[0], bad_body.size(), patch_position, 1));
  MEMSET(&wire[0], 0xa5, wire.size());
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ASSERT_NO_FATAL_FAILURE(expect_questionmark(1, 7));
  ASSERT_NO_FATAL_FAILURE(expect_point(INT64_MIN, 7, INT64_MAX));
  EXPECT_EQ(7, old_value.get_int());
  position = 0;
  // Rejection of the outer UNIS header never enters the class body.
  EXPECT_NE(OB_SUCCESS, plan().deserialize(wire.data(), 1, position));
  EXPECT_NE(nullptr, plan().get_native_parameter(1));
  position = 0;
  EXPECT_NE(OB_SUCCESS, plan().deserialize(bad_body.data(), bad_body.size(), position));
  EXPECT_EQ(nullptr, plan().get_native_parameter(1));
  EXPECT_EQ(0, store().count());
  EXPECT_EQ(0, plan().get_param_frame_ptrs().count());
  EXPECT_EQ(7, old_value.get_int());
}

TEST_F(NativeExecutionParameters, graph_rejections_still_preserve_output_before_reading_native_values)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  graph_.get_range_map().expr_final_infos_.at(2).null_safe_ = true;
  ASSERT_NO_FATAL_FAILURE(expect_no_point());
  graph_.get_range_map().expr_final_infos_.at(2).null_safe_ = false;
  graph_.set_contain_exec_param(true);
  ASSERT_NO_FATAL_FAILURE(expect_no_point());
  graph_.set_contain_exec_param(false);
  graph_.get_column_meta(2)->column_type_.set_type(ObUInt64Type);
  ASSERT_NO_FATAL_FAILURE(expect_no_point());
}

TEST(NativeHashPartitionValue, public_revision_imports_typed_values_and_returns_independently_owned_int64)
{
  struct Input {
    SqlValueType type;
    int64_t signed_value;
    uint64_t unsigned_value;
    int64_t expected;
    bool null;
  };
  // Independent constants, including the documented INT64_MIN saturation and
  // the unsigned all-bits-set value interpreted by MySQL HASH as signed -1.
  const Input inputs[] = {
      {ObIntType, 37, 0, 37, false},
      {ObIntType, -37, 0, 37, false},
      {ObIntType, INT64_MIN, 0, INT64_MAX, false},
      {ObIntType, INT64_MAX, 0, INT64_MAX, false},
      {ObUInt64Type, 0, UINT64_MAX, 1, false},
      {ObUInt64Type, 0, uint64_t(1) << 63, INT64_MAX, false},
      {ObTinyIntType, -128, 0, 128, false},
      {ObSmallIntType, -32768, 0, 32768, false},
      {ObUTinyIntType, 0, 255, 255, false},
      {ObBitType, 0, 255, 255, false},
      {ObBitType, 0, UINT64_MAX, 1, false},
      {ObYearType, 0, 255, 255, false},
      {ObIntType, 73, 0, 0, true},
      {ObNullType, 0, 0, 0, true},
  };
  for (const Input &input : inputs) {
    SCOPED_TRACE(static_cast<int>(input.type));
    SCOPED_TRACE(input.expected);
    ObArenaAllocator source_owner("PartHashSrc");
    ExecutionAllocator result_owner;
    DatumValue value;
    if (ob_is_int_tc(input.type)) {
      ASSERT_EQ(OB_SUCCESS, value.set_int(input.type, input.signed_value, source_owner));
    } else if (input.type == ObYearType) {
      ASSERT_EQ(OB_SUCCESS, value.set_year(static_cast<uint8_t>(input.unsigned_value), source_owner));
    } else if (input.type != ObNullType) {
      ASSERT_EQ(OB_SUCCESS, value.set_uint(input.type, input.unsigned_value, source_owner));
    }
    if (input.null) { value.datum_.set_null(); } // Preserve the declared INT type for typed NULL.
    const char *source_payload = value.datum_.ptr_;
    ASSERT_EQ(OB_SUCCESS, ObSQLUtils::revise_hash_part_object(value, result_owner));
    EXPECT_EQ(ObIntType, value.get_type());
    ASSERT_FALSE(value.is_null());
    EXPECT_EQ(sizeof(int64_t), value.datum_.len_);
    EXPECT_EQ(1, result_owner.calls_for(sizeof(int64_t)));
    EXPECT_NE(source_payload, value.datum_.ptr_);
    source_owner.reset();
    EXPECT_EQ(input.expected, value.get_int());
  }
}

TEST(NativeHashPartitionValue, invalid_type_preserves_original_metadata_descriptor_and_payload)
{
  for (SqlValueType type : {ObVarcharType, ObDoubleType, ObDateType}) {
    SCOPED_TRACE(static_cast<int>(type));
    ObArenaAllocator source_owner("PartHashSrc");
    ExecutionAllocator result_owner;
    DatumValue value;
    if (type == ObVarcharType) {
      ASSERT_EQ(OB_SUCCESS, value.set_varchar(ObString::make_string("not an integer"), source_owner));
    } else if (type == ObDoubleType) {
      ASSERT_EQ(OB_SUCCESS, value.set_double(1.5, source_owner));
    } else { ASSERT_EQ(OB_SUCCESS, value.set_date(17, source_owner)); }
    const DatumValue original = value;
    const std::string bytes(value.datum_.ptr_, value.datum_.len_);
    EXPECT_EQ(OB_INVALID_ARGUMENT, ObSQLUtils::revise_hash_part_object(value, result_owner));
    EXPECT_EQ(original.datum_.ptr_, value.datum_.ptr_);
    EXPECT_EQ(original.datum_.pack_, value.datum_.pack_);
    EXPECT_EQ(0, MEMCMP(&original.meta_, &value.meta_, sizeof(value.meta_)));
    EXPECT_EQ(bytes, std::string(value.datum_.ptr_, value.datum_.len_));
    EXPECT_TRUE(result_owner.sizes_.empty());
  }
}

TEST(NativeHashPartitionValue, result_allocation_failure_preserves_input_including_typed_null)
{
  for (int input = 0; input < 3; ++input) {
    SCOPED_TRACE(input);
    ObArenaAllocator source_owner("PartHashSrc");
    ExecutionAllocator result_owner;
    result_owner.fail_size_ = sizeof(int64_t);
    DatumValue value;
    if (input == 1) {
      ASSERT_EQ(OB_SUCCESS, value.set_uint(ObUInt64Type, UINT64_MAX, source_owner));
    } else {
      ASSERT_EQ(OB_SUCCESS, value.set_int(INT64_MIN, source_owner));
      if (input == 2) { value.datum_.set_null(); }
    }
    const DatumValue original = value;
    const std::string bytes(value.datum_.ptr_, sizeof(int64_t));
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, ObSQLUtils::revise_hash_part_object(value, result_owner));
    EXPECT_EQ(original.datum_.ptr_, value.datum_.ptr_);
    EXPECT_EQ(original.datum_.pack_, value.datum_.pack_);
    EXPECT_EQ(0, MEMCMP(&original.meta_, &value.meta_, sizeof(value.meta_)));
    EXPECT_EQ(bytes, std::string(value.datum_.ptr_, sizeof(int64_t)));
    EXPECT_EQ(1, result_owner.calls_for(sizeof(int64_t)));
  }
}

TEST_F(NativeExecutionParameters, supplied_type_binding_reuses_exact_type_and_ignores_attribute_payload)
{
  ASSERT_NO_FATAL_FAILURE(load({11}));
  Datum value; value.set_int(INT64_MIN);
  DatumParameter attributes = store().at(0);
  EXPECT_EQ(OB_NOT_INIT, plan().set_runtime_parameter(0, value, request_type_, attributes));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  attributes = store().at(0);
  attributes.set_precision(9);
  attributes.set_result_flags(0x1234);
  attributes.set_raw_text_info(7, 13);
  // This descriptor is deliberately unusable. The typed API consumes metadata
  // only, never imports a second legacy value.
  attributes.datum_.ptr_ = nullptr;
  attributes.datum_.pack_ = 8;
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(0, value, request_type_, attributes));
  EXPECT_EQ(INT64_MIN, plan().get_native_parameter(0)->get_int());
  EXPECT_EQ(9, store().at(0).get_precision());
  EXPECT_EQ(0x1234, store().at(0).get_result_flag());
  EXPECT_EQ(7, store().at(0).get_raw_text_pos());
  EXPECT_EQ(13, store().at(0).get_raw_text_len());
  const Datum *slot = plan().get_native_parameter(0);
  const SqlType *type = plan().get_native_parameter_type(0);
  owner_.clear_calls();
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(0, *slot, *type, store().at(0)));
  EXPECT_EQ(slot, plan().get_native_parameter(0));
  EXPECT_EQ(type, plan().get_native_parameter_type(0));
  EXPECT_TRUE(owner_.sizes_.empty());
  ObDatum view;
  ASSERT_EQ(OB_SUCCESS, DatumCopyPlan(*type).borrow_legacy(*slot, view));
  EXPECT_EQ(view.ptr_, store().at(0).datum_.ptr_);

  // Coercibility is deliberately ignored by SqlType::equals, but execution
  // metadata must not be silently reused from the old declaration.
  SqlTypeMeta changed_meta = request_type_.legacy_meta();
  changed_meta.set_collation_level(CS_LEVEL_IMPLICIT);
  changed_meta.set_scale(17); // SqlTypeMeta::operator== deliberately omits this byte.
  SqlType changed;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(changed_meta, request_type_.accuracy(), changed));
  ASSERT_TRUE(type->equals(changed));
  Datum null; null.set_null();
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(0, null, changed, store().at(0)));
  EXPECT_NE(type, plan().get_native_parameter_type(0));
  EXPECT_EQ(CS_LEVEL_IMPLICIT, plan().get_native_parameter_type(0)->legacy_meta().get_collation_level());
  EXPECT_EQ(17, plan().get_native_parameter_type(0)->legacy_meta().get_scale());
  EXPECT_TRUE(plan().get_native_parameter(0)->is_null());
  EXPECT_EQ(ObNullType, store().at(0).get_type());
  EXPECT_EQ(ObIntType, store().at(0).get_param_meta().get_type());
  EXPECT_EQ(9, store().at(0).get_precision());
}

TEST_F(NativeExecutionParameters, supplied_reference_and_type_failures_preserve_the_published_binding)
{
  ASSERT_NO_FATAL_FAILURE(load({42}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const SqlType *old_type = plan().get_native_parameter_type(0);
  const DatumParameter before = store().at(0);
  auto *info = reinterpret_cast<ObEvalInfo *>(plan().get_param_frame_ptrs().at(0)
      + ScalarFrameLayout::eval_info_offset(0));
  info->evaluated_ = true; info->projected_ = true;
  // State compatibility requires an owned legacy wrapper. Its allocation is
  // also staged before any native value/type/info publication.
  Datum state; state.set_min();
  int64_t wrapper_size = 0;
  ASSERT_EQ(OB_SUCCESS, DatumCopyPlan(*old_type).legacy_payload_size(state, wrapper_size));
  ASSERT_GT(wrapper_size, 0);
  owner_.clear_calls();
  owner_.fail_size_ = wrapper_size;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan().set_runtime_parameter(0, state, *old_type, before));
  EXPECT_EQ(1, owner_.calls_for(wrapper_size));
  EXPECT_EQ(0, owner_.calls_for(sizeof(SqlType)));
  EXPECT_EQ(42, plan().get_native_parameter(0)->get_int());
  EXPECT_EQ(old_type, plan().get_native_parameter_type(0));
  EXPECT_EQ(before.datum_.ptr_, store().at(0).datum_.ptr_);
  EXPECT_TRUE(info->evaluated_); EXPECT_TRUE(info->projected_);
  owner_.fail_size_ = 0;
  const std::string text(409, 'x');
  {
    ObArenaAllocator source_owner;
    SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    const SqlType *type = nullptr;
    ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(409), source_owner, type));
    Datum input; ASSERT_EQ(OB_SUCCESS, input.set_string(text.data(), text.size()));
    for (int64_t rejected : {int64_t(sizeof(SqlType)), int64_t(text.size())}) {
      owner_.clear_calls();
      owner_.fail_size_ = rejected;
      EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan().set_runtime_parameter(0, input, *type, before));
      EXPECT_EQ(1, owner_.calls_for(sizeof(SqlType)));
      EXPECT_EQ(rejected == int64_t(text.size()) ? 1 : 0, owner_.calls_for(text.size()));
      EXPECT_EQ(old_type, plan().get_native_parameter_type(0));
      EXPECT_EQ(42, plan().get_native_parameter(0)->get_int());
      EXPECT_EQ(before.datum_.ptr_, store().at(0).datum_.ptr_);
      EXPECT_TRUE(info->evaluated_); EXPECT_TRUE(info->projected_);
      expect_metadata(before, store().at(0));
    }
    owner_.fail_size_ = 0;
    ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(0, input, *type, before));
    EXPECT_NE(type, plan().get_native_parameter_type(0));
    EXPECT_NE(input.get_reference(), plan().get_native_parameter(0)->get_reference());
    EXPECT_FALSE(info->evaluated_); EXPECT_FALSE(info->projected_);
  }
  EXPECT_EQ(ObVarcharType, plan().get_native_parameter_type(0)->kind());
  EXPECT_EQ(409, plan().get_native_parameter_type(0)->length());
  EXPECT_EQ(text, std::string(plan().get_native_parameter(0)->get_string().ptr(), text.size()));
  const SqlType *type = plan().get_native_parameter_type(0);
  const DatumParameter stable = store().at(0);
  Datum invalid; ASSERT_EQ(OB_SUCCESS, invalid.set_string("bad", 3));
  // An inline declaration must reject a nonempty reference span.
  EXPECT_EQ(OB_INVALID_ARGUMENT, plan().set_runtime_parameter(0, invalid, request_type_, stable));
  EXPECT_EQ(type, plan().get_native_parameter_type(0));
  EXPECT_EQ(stable.datum_.ptr_, store().at(0).datum_.ptr_);
}

TEST_F(NativeExecutionParameters, supplied_binding_respects_original_and_appended_page_boundaries)
{
  const int64_t per_page = ScalarFrameLayout::slots_per_frame();
  ASSERT_NO_FATAL_FAILURE(load(std::vector<int64_t>(per_page + 1, 17)));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ASSERT_EQ(OB_SUCCESS, plan().reserve_param_space(2 * per_page + 2));
  for (int64_t index : {per_page - 1, per_page, per_page + 1, 2 * per_page + 1}) {
    Datum value; value.set_int(-index);
    ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(index, value, request_type_, store().at(index)));
    expect_questionmark(index, -index);
    int64_t page = 0, slot = 0;
    ScalarFrameLayout::locate_parameter(per_page + 1, index, page, slot);
    EXPECT_EQ(reinterpret_cast<const Datum *>(plan().get_param_frame_ptrs().at(page)
        + ScalarFrameLayout::datum_offset(slot)), plan().get_native_parameter(index));
  }
  const int64_t count = store().count();
  Datum value; value.set_int(99);
  for (int64_t index : {int64_t(-1), count}) {
    EXPECT_EQ(OB_INVALID_ARGUMENT, plan().set_runtime_parameter(index, value, request_type_, store().at(0)));
  }
  EXPECT_EQ(count, store().count());
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
