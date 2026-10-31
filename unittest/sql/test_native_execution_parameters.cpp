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
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/engine/table/native_point_get_input.h"
#include "sql/ob_sql_context.h"
#include "sql/rewrite/ob_query_range_define.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace oceanbase
{
namespace sql
{
int eval_question_mark_func(const ObExpr &, ObEvalCtx &, ObDatum &);
}
}

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
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
  DatumParamStore &store() { return plan().get_param_store_for_update(); }

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
      ASSERT_EQ(OB_SUCCESS, store().push_back(parameter));
    }
    plan().set_original_param_cnt(values.size());
    owner_.clear_calls();
  }

  void expect_questionmark(int64_t index, int64_t expected)
  {
    ObEvalCtx eval(exec_);
    ObExpr expr; expr.extra_ = index; expr.datum_meta_.type_ = ObIntType;
    ObDatum value;
    ASSERT_EQ(OB_SUCCESS, eval_question_mark_func(expr, eval, value));
    ASSERT_FALSE(value.is_null());
    EXPECT_EQ(expected, value.get_int());
    EXPECT_EQ(store().at(index).datum_.ptr_, value.ptr_);
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
  for (int64_t count : {1, 3, 37}) {
    SCOPED_TRACE(count);
    std::vector<int64_t> input(count);
    for (int64_t i = 0; i < count; ++i) { input[i] = i * 13 - 7; }
    input.front() = INT64_MIN; if (count > 1) { input.back() = INT64_MAX; }
    ASSERT_NO_FATAL_FAILURE(load(input));
    std::vector<DatumParameter> before;
    for (int64_t i = 0; i < count; ++i) { before.push_back(store().at(i)); }
    ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
    EXPECT_EQ(0, owner_.calls_for(8));
    EXPECT_EQ(1, owner_.calls_for(32 + count * sizeof(Datum)));
    const Datum *first = plan().get_native_bigint_parameter(0);
    ASSERT_NE(nullptr, first);
    for (int64_t i = 0; i < count; ++i) {
      const Datum *native = plan().get_native_bigint_parameter(i);
      ASSERT_NE(nullptr, native);
      EXPECT_EQ(first + i, native);
      EXPECT_EQ(input[i], native->get_int());
      EXPECT_NE(before[i].datum_.ptr_, store().at(i).datum_.ptr_);
      expect_metadata(before[i], store().at(i));
      request_values_[i].set_int(0x5a5a5a5a);
      ASSERT_NO_FATAL_FAILURE(expect_questionmark(i, input[i]));
      ASSERT_EQ(1, plan().get_param_frame_ptrs().count());
      const char *slot = plan().get_param_frame_ptrs().at(0) + i * (sizeof(ObDatum) + sizeof(ObEvalInfo));
      const ObDatum &frame = *reinterpret_cast<const ObDatum *>(slot);
      EXPECT_EQ(input[i], frame.get_int()); EXPECT_EQ(store().at(i).datum_.ptr_, frame.ptr_);
    }
    EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(-1));
    EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(count));
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
  store().at(1).datum_.set_int(-20); store().at(4).datum_.set_int(-50);
  ASSERT_NO_FATAL_FAILURE(expect_point(-20, -50, -50));
  EXPECT_EQ(20, key.values()[0]); EXPECT_EQ(50, key.values()[1]); EXPECT_EQ(50, key.values()[2]);
  ASSERT_EQ(1, key.lookup_keys().count());
  EXPECT_EQ(20, key.lookup_keys().at(0).datums_[0].get_int());
  // Repeated final-result index is distinct from repeated execution parameter.
  start_[0] = start_[1] = start_[2] = end_[0] = end_[1] = end_[2] = 0;
  ASSERT_NO_FATAL_FAILURE(expect_point(-50, -50, -50));
}

TEST_F(NativeExecutionParameters, escaped_assignment_and_runtime_rebind_fall_back_to_current_values)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  DatumParamStore &escaped = store();
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const DatumParameter original = escaped.at(1);
  DatumParameter replacement = original;
  ASSERT_EQ(OB_SUCCESS, replacement.set_int(71, request_arena_));
  replacement.set_type_meta(original.get_type_meta());
  escaped.at(1) = replacement;
  EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(1));
  ASSERT_NO_FATAL_FAILURE(expect_point(1, 71, 3));
  ASSERT_NO_FATAL_FAILURE(expect_questionmark(1, 71));
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(1, original));
  ASSERT_NE(nullptr, plan().get_native_bigint_parameter(1));
  ASSERT_NO_FATAL_FAILURE(expect_point(1, 2, 3));
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(1, replacement));
  EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(1));
  ASSERT_NO_FATAL_FAILURE(expect_point(1, 71, 3));
  EXPECT_EQ(OB_INVALID_ARGUMENT, plan().set_runtime_parameter(-1, replacement));
  EXPECT_EQ(OB_INVALID_ARGUMENT, plan().set_runtime_parameter(3, replacement));
}

TEST_F(NativeExecutionParameters, same_representation_write_updates_native_frame_and_questionmark)
{
  ASSERT_NO_FATAL_FAILURE(load({INT64_MIN, 0, INT64_MAX}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const Datum *native = plan().get_native_bigint_parameter(1);
  ASSERT_NE(nullptr, native);
  ObDatum *frame = reinterpret_cast<ObDatum *>(plan().get_param_frame_ptrs().at(0)
      + sizeof(ObDatum) + sizeof(ObEvalInfo));
  frame->set_int(-77);
  EXPECT_EQ(native, plan().get_native_bigint_parameter(1));
  EXPECT_EQ(-77, native->get_int());
  ASSERT_NO_FATAL_FAILURE(expect_questionmark(1, -77));
  ASSERT_NO_FATAL_FAILURE(expect_point(INT64_MIN, -77, INT64_MAX));
  store().at(1).datum_.set_int(99);
  EXPECT_EQ(99, frame->get_int()); EXPECT_EQ(99, native->get_int());
  EXPECT_EQ(0, request_values_[1].get_int());
}

TEST_F(NativeExecutionParameters, allocating_setter_retargets_without_modifying_previous_borrow)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const ObDatum previous = store().at(0).datum_;
  const SqlTypeMeta meta = store().at(0).get_type_meta();
  ASSERT_EQ(OB_SUCCESS, store().at(0).set_int(81, request_arena_));
  store().at(0).set_type_meta(meta); // Exercise the pointer guard independently.
  EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(0));
  EXPECT_EQ(1, previous.get_int());
  ASSERT_NO_FATAL_FAILURE(expect_point(81, 2, 3));
  ASSERT_NO_FATAL_FAILURE(expect_questionmark(0, 81));
}

TEST_F(NativeExecutionParameters, metadata_state_and_exact_index_guards_do_not_reuse_an_old_slot)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const DatumParameter original = store().at(1);
  store().at(1).datum_ = store().at(0).datum_; // Same owner, wrong execution index.
  EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(1));
  ASSERT_NO_FATAL_FAILURE(expect_point(1, 1, 3));
  store().at(1) = original;
  for (int change = 0; change < 6; ++change) {
    SCOPED_TRACE(change);
    if (change == 0) { store().at(1).datum_.set_null(); }
    else if (change == 1) { store().at(1).get_meta().set_type(ObUInt64Type); }
    else if (change == 2) { store().at(1).get_meta().set_scale(1); }
    else if (change == 3) { store().at(1).set_precision(11); }
    else if (change == 4) { store().at(1).datum_.set_outrow(); }
    else { store().at(1).get_meta().set_collation_level(CS_LEVEL_NUMERIC); }
    EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(1));
    if (change == 0 || change == 1 || change == 4) { ASSERT_NO_FATAL_FAILURE(expect_no_point()); }
    else { ASSERT_NO_FATAL_FAILURE(expect_point(1, 2, 3)); }
    store().at(1) = original;
    ASSERT_NE(nullptr, plan().get_native_bigint_parameter(1));
  }
}

TEST_F(NativeExecutionParameters, mixed_metadata_and_non_bigint_batches_keep_the_complete_clone_path)
{
  for (int change = 0; change < 5; ++change) {
    SCOPED_TRACE(change);
    ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
    if (change == 0) { store().at(2).set_precision(9); }
    else if (change == 1) { store().at(2).get_meta().set_collation_level(CS_LEVEL_NUMERIC); }
    else if (change == 2) { store().at(2).get_meta().set_scale(-1); }
    else if (change == 3) { store().at(2).datum_.set_null(); }
    else { ASSERT_EQ(OB_SUCCESS, store().at(2).set_int32(3, request_arena_)); }
    const DatumParameter before = store().at(0);
    ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
    EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(0));
    EXPECT_EQ(0, owner_.calls_for(32 + 3 * sizeof(Datum)));
    EXPECT_EQ(change == 3 ? 2 : 3, owner_.calls_for(8));
    EXPECT_NE(before.datum_.ptr_, store().at(0).datum_.ptr_);
    expect_metadata(before, store().at(0));
    request_values_[0].set_int(100);
    ASSERT_NO_FATAL_FAILURE(expect_questionmark(0, 1));
    if (change < 3) { ASSERT_NO_FATAL_FAILURE(expect_point(1, 2, 3)); }
    else { ASSERT_NO_FATAL_FAILURE(expect_no_point()); }
  }
}

TEST_F(NativeExecutionParameters, nonordinary_ps_contexts_fall_back_without_changing_point_admission)
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
    EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(0));
    EXPECT_EQ(3, owner_.calls_for(8));
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
  ObDatum *old_frame = reinterpret_cast<ObDatum *>(plan().get_param_frame_ptrs().at(0));
  const DatumParameter old_parameter = store().at(0);
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ASSERT_NE(nullptr, plan().get_native_bigint_parameter(0));
  EXPECT_NE(old_frame->ptr_, store().at(0).datum_.ptr_);
  old_frame->set_int(42);
  EXPECT_EQ(42, old_parameter.get_int());
  ASSERT_NO_FATAL_FAILURE(expect_point(1, 7, 3));
  plan().reset_datum_param_store();
  EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(0));
  EXPECT_EQ(42, old_frame->get_int());
  ASSERT_NO_FATAL_FAILURE(load({4, 5, 6}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  store().at(0) = old_parameter;
  EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(0));
  ASSERT_NO_FATAL_FAILURE(expect_point(42, 5, 6));
  plan().destroy();
  EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(1));
  EXPECT_EQ(42, old_frame->get_int());
}

TEST_F(NativeExecutionParameters, append_count_and_replacement_are_checked_without_generation)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ASSERT_EQ(OB_SUCCESS, plan().reserve_param_space(4));
  EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(0));
  ASSERT_NO_FATAL_FAILURE(expect_point(1, 2, 3));
  store().pop_back();
  ASSERT_NE(nullptr, plan().get_native_bigint_parameter(0));
  DatumParameter replacement = store().at(2);
  ASSERT_EQ(OB_SUCCESS, replacement.set_int(90, request_arena_));
  replacement.set_type_meta(request_type_.legacy_meta());
  store().reuse();
  for (int i = 0; i < 3; ++i) { ASSERT_EQ(OB_SUCCESS, store().push_back(replacement)); }
  EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(0));
  EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(2));
  ASSERT_NO_FATAL_FAILURE(expect_point(90, 90, 90));
}

TEST_F(NativeExecutionParameters, owner_or_frame_allocation_failure_does_not_publish_a_binding)
{
  for (int stage = 0; stage < 2; ++stage) {
    SCOPED_TRACE(stage);
    ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
    std::vector<const char *> before;
    for (int i = 0; i < 3; ++i) { before.push_back(store().at(i).datum_.ptr_); }
    owner_.fail_size_ = stage == 0 ? 32 + 3 * sizeof(Datum) : 3 * (sizeof(ObDatum) + sizeof(ObEvalInfo));
    ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan().init_datum_param_store());
    EXPECT_EQ(1, owner_.calls_for(owner_.fail_size_));
    EXPECT_EQ(0, plan().get_param_frame_ptrs().count());
    for (int i = 0; i < 3; ++i) {
      EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(i));
      EXPECT_EQ(before[i], store().at(i).datum_.ptr_);
      EXPECT_EQ(i + 1, store().at(i).get_int());
    }
    owner_.fail_size_ = 0;
    ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
    ASSERT_NE(nullptr, plan().get_native_bigint_parameter(0));
  }
}

TEST_F(NativeExecutionParameters, failed_rebuild_revokes_current_owner_but_keeps_old_payload_borrow)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 7, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const ObDatum saved = store().at(1).datum_;
  owner_.fail_size_ = 32 + 3 * sizeof(Datum);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan().init_datum_param_store());
  EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(1));
  EXPECT_EQ(saved.ptr_, store().at(1).datum_.ptr_);
  EXPECT_EQ(7, saved.get_int());
  EXPECT_EQ(0, plan().get_param_frame_ptrs().count());
  owner_.fail_size_ = 0;
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ASSERT_NE(nullptr, plan().get_native_bigint_parameter(1));
  EXPECT_NE(saved.ptr_, store().at(1).datum_.ptr_);
  EXPECT_EQ(7, saved.get_int());
}

TEST_F(NativeExecutionParameters, real_wire_roundtrip_clears_owner_and_decoded_payload_is_independent)
{
  ASSERT_NO_FATAL_FAILURE(load({INT64_MIN, 7, INT64_MAX}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const ObDatum old_value = store().at(1).datum_;
  std::string wire(plan().get_serialize_size(), '\0');
  int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, plan().serialize(&wire[0], wire.size(), position));
  ASSERT_EQ(wire.size(), position);
  std::string bad_body = wire;
  int64_t header = 0, version = 0, body_length = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::decode(bad_body.data(), bad_body.size(), header, version));
  const int64_t length_position = header;
  ASSERT_EQ(OB_SUCCESS, serialization::decode(bad_body.data(), bad_body.size(), header, body_length));
  ASSERT_GT(body_length, 1);
  bad_body.resize(header + 1); // A complete outer header, but no mandatory parameter store.
  int64_t patch_position = length_position;
  ASSERT_EQ(OB_SUCCESS, serialization::encode_fixed_bytes_i64(
      &bad_body[0], bad_body.size(), patch_position, 1));
  position = 0;
  ASSERT_EQ(OB_SUCCESS, plan().deserialize(wire.data(), wire.size(), position));
  EXPECT_EQ(wire.size(), position);
  EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(1));
  ASSERT_EQ(3, store().count());
  EXPECT_NE(old_value.ptr_, store().at(1).datum_.ptr_);
  MEMSET(&wire[0], 0xa5, wire.size());
  ASSERT_NO_FATAL_FAILURE(expect_questionmark(1, 7));
  ASSERT_NO_FATAL_FAILURE(expect_point(INT64_MIN, 7, INT64_MAX));
  EXPECT_EQ(7, old_value.get_int());
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ASSERT_NE(nullptr, plan().get_native_bigint_parameter(1));
  position = 0;
  // UNIS header rejection happens before the class body; it preserves the old
  // object just as before. Once its body runs, reset/error must revoke owner.
  EXPECT_NE(OB_SUCCESS, plan().deserialize(wire.data(), 1, position));
  EXPECT_NE(nullptr, plan().get_native_bigint_parameter(1));
  position = 0;
  EXPECT_NE(OB_SUCCESS, plan().deserialize(bad_body.data(), bad_body.size(), position));
  EXPECT_EQ(nullptr, plan().get_native_bigint_parameter(1));
  EXPECT_EQ(0, store().count()); EXPECT_EQ(0, plan().get_param_frame_ptrs().count());
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

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
