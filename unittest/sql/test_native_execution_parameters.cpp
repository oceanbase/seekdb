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
#include "common/datum/datum_parameter_codec.h"
#include "sql/engine/expr/native_frame_value_codec.h"
#include "sql/engine/expr/ob_expr_frame_info.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/ob_vslice_alloc.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "lib/charset/ob_charset.h"
#include "sql/engine/expr/ob_expr.h"
#include "sql/engine/expr/ob_expr_cmp_func.h"
#include "sql/engine/expr/ob_expr_substring_index.h"
#include "sql/engine/expr/ob_expr_like.h"
#include "sql/engine/expr/ob_expr_get_subprogram_var.h"
#include "sql/engine/expr/ob_expr_res_type.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/engine/ob_operator.h"
#include "sql/engine/table/native_point_get_input.h"
#include "sql/ob_sql_context.h"
#include "sql/ob_sql.h"
#include "sql/plan_cache/ob_plan_cache_value.h"
#include "sql/plan_cache/ob_plan_cache_struct.h"
#include "sql/ob_spi.h"
#include "sql/engine/expr/ob_sql_expression.h"
#include "sql/pl/ob_pl.h"
#include "sql/pl/ob_pl_package_guard.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/pl/ob_pl_stmt.h"
#include "sql/ob_sql_utils.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "share/system_variable/ob_system_variable_init.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/pl/ob_pl_user_type.h"
#include "sql/rewrite/ob_query_range_define.h"
#include <gtest/gtest.h>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <type_traits>


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

namespace oceanbase
{
namespace sql
{
int eval_question_mark_func(const ObExpr &, ObEvalCtx &, Datum &);
struct NativeExecutionParametersTestPeer {
  static int64_t input_count(const ObPhysicalPlanCtx &p) { return p.param_store_.count(); }
  static int64_t input_bytes(const ObPhysicalPlanCtx &p) { return p.parameter_input_allocator_.used(); }
};
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
  ExecutionAllocator() : arena_("ExecParamTest"), sizes_(), fail_size_(0), reject_all_(false), fail_call_(0) {}
  void *alloc(int64_t size) override
  {
    sizes_.push_back(size);
    return reject_all_ || size == fail_size_ || (fail_call_ > 0 && sizes_.size() == fail_call_)
        ? nullptr : arena_.alloc(size);
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
  bool reject_all_;
  int64_t fail_call_;
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
  alignas(8) char expected_scratch[64], actual_scratch[64];
  ObDatum expected_view, actual_view;
  ASSERT_EQ(OB_SUCCESS, expected.export_legacy(expected_view, expected_scratch, sizeof(expected_scratch)));
  ASSERT_EQ(OB_SUCCESS, actual.export_legacy(actual_view, actual_scratch, sizeof(actual_scratch)));
  EXPECT_EQ(expected_view.pack_, actual_view.pack_);
}

class NativeExecutionParameters : public ::testing::Test
{
protected:
  NativeExecutionParameters()
    : request_arena_("ExecParamReq"), snapshot_arena_("ExecParamView"), owner_(), sql_ctx_(), exec_(owner_),
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
  // Explicit test-only physical snapshot for existing metadata oracles. The
  // production binding is always the unique native frame/Type/metadata triple.
  DatumParameter parameter(int64_t index) {
    if (!plan().native_parameters_ready()) { return plan().get_parameter_inputs().at(index); }
    NativeTypedDatum value; DatumParameterMetadata metadata;
    EXPECT_EQ(OB_SUCCESS, plan().capture_native_parameter(index, value, metadata));
    alignas(8) char scratch[64]; ObDatum legacy; DatumParameter result;
    EXPECT_EQ(OB_SUCCESS, DatumCopyPlan(*value.type_).legacy_view(
        value.datum_, legacy, scratch, sizeof(scratch)));
    EXPECT_EQ(OB_SUCCESS, result.assign_legacy_owned(legacy, metadata.type_meta_, snapshot_arena_));
    result.set_metadata(metadata);
    return result;
  }
  int bind_parameter(int64_t index, const DatumParameter &input) {
    const SqlTypeMeta meta = input.is_null() && !input.get_param_meta().is_null()
        ? input.get_param_meta() : input.get_type_meta();
    const SqlType *type = nullptr;
    int ret = SqlType::from_legacy(meta, input.get_accuracy(), snapshot_arena_, type);
    alignas(8) char scratch[64]; ObDatum legacy; Datum value;
    if (ret == OB_SUCCESS) { ret = input.export_legacy(legacy, scratch, sizeof(scratch)); }
    if (ret == OB_SUCCESS) { ret = DatumCopyPlan(*type).import_legacy(legacy, value); }
    return ret == OB_SUCCESS ? plan().set_runtime_parameter(index, value, *type, input.get_metadata()) : ret;
  }
  int rebuild() {
    ParamStore inputs((ObWrapperAllocator(&snapshot_arena_)));
    const int ret = plan().build_resolution_inputs(inputs, snapshot_arena_);
    return ret == OB_SUCCESS ? plan().rebuild_runtime_parameters(inputs) : ret;
  }

  void load(const std::vector<int64_t> &values)
  {
    plan().reset_datum_param_store();
    request_values_.resize(values.size());
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
      ASSERT_EQ(OB_SUCCESS, parameter.assign_native_borrowed(
          request_values_[i], request_type_.legacy_meta()));
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
    EXPECT_EQ(expected, read_parameter_value(plan(), index).get_int());
  }

  void expect_point(int64_t first, int64_t second, int64_t third)
  {
    int64_t values[3]; bool supported = false;
    const ObQueryRangeProvider &provider = graph_;
    ASSERT_EQ(OB_SUCCESS, provider.try_get_native_bigint_point(plan(), values, supported));
    ASSERT_TRUE(supported);
    EXPECT_EQ(first, values[0]); EXPECT_EQ(second, values[1]);
    EXPECT_EQ(third, values[2]);
  }

  void expect_no_point()
  {
    int64_t values[3];
    for (int64_t i = 0; i < 3; ++i) { values[i] = 101 + i; }
    bool supported = true;
    ASSERT_EQ(OB_SUCCESS, graph_.try_get_native_bigint_point(plan(), values, supported));
    EXPECT_FALSE(supported);
    for (int64_t i = 0; i < 3; ++i) { EXPECT_EQ(101 + i, values[i]); }
  }

  ObArenaAllocator request_arena_;
  ObArenaAllocator snapshot_arena_;
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
  static_assert(std::is_same<decltype(std::declval<ObPhysicalPlanCtx &>().get_parameter_inputs()),
      const ParamStore &>::value, "construction inputs cannot escape as mutable storage");
  const int64_t per_page = MAX_FRAME_SIZE / 24;
  for (int64_t count : {int64_t(1), int64_t(3), int64_t(37), per_page, per_page + 1}) {
    SCOPED_TRACE(count);
    std::vector<int64_t> input(count);
    for (int64_t i = 0; i < count; ++i) { input[i] = i * 13 - 7; }
    input.front() = INT64_MIN;
    if (count > 1) { input.back() = INT64_MAX; }
    ASSERT_NO_FATAL_FAILURE(load(input));
    std::vector<DatumParameter> before;
    for (int64_t i = 0; i < count; ++i) { before.push_back(parameter(i)); }
    ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
    EXPECT_EQ((count + per_page - 1) / per_page, plan().get_param_frame_ptrs().count());
    for (int64_t i = 0; i < count; ++i) {
      Datum native_value;
      const auto &page = ParameterValuePage::from_handle(plan().get_param_frame_ptrs().at(i / per_page));
      ASSERT_EQ(OB_SUCCESS, page.read(i % per_page, native_value));
      const Datum *native = &native_value;
      EXPECT_EQ(nullptr, plan().locate_materialized_parameter(i));
      EXPECT_EQ(0u, reinterpret_cast<uintptr_t>(&page) % 8);
      EXPECT_EQ(8 * page.capacity(), ParameterValuePage::payload_bytes(page.capacity()));
      EXPECT_EQ(input[i], native->get_int());
      int64_t extracted = 101;
      EXPECT_TRUE(plan().read_native_bigint_parameter(i, extracted));
      EXPECT_EQ(input[i], extracted);
      const SqlType *type = plan().get_native_parameter_type(i);
      ASSERT_NE(nullptr, type);
      EXPECT_EQ(ObIntType, type->kind());
      EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_count(plan()));
      EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_bytes(plan()));
      EXPECT_EQ(0u, native->length());
      expect_metadata(before[i], parameter(i));
      request_values_[i].set_int(0x5a5a5a5a);
      ASSERT_NO_FATAL_FAILURE(expect_questionmark(i, input[i]));
    }
    EXPECT_EQ(nullptr, materialized_parameter(plan(), -1));
    EXPECT_EQ(nullptr, materialized_parameter(plan(), count));
    EXPECT_EQ(nullptr, plan().get_native_parameter_type(count));
    int64_t extracted = 101;
    EXPECT_FALSE(plan().read_native_bigint_parameter(-1, extracted));
    EXPECT_EQ(101, extracted);
    EXPECT_FALSE(plan().read_native_bigint_parameter(count, extracted));
    EXPECT_EQ(101, extracted);
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
  int64_t values[3]; bool supported = false;
  ASSERT_EQ(OB_SUCCESS, graph_.try_get_native_bigint_point(plan(), values, supported));
  ASSERT_TRUE(supported);
  NativePointGetInput key;
  ASSERT_EQ(OB_SUCCESS, key.init(values, 0));
  for (int index : {1, 4}) {
    DatumParameter replacement = parameter(index);
    ASSERT_EQ(OB_SUCCESS, replacement.set_int(index == 1 ? -20 : -50, request_arena_));
    ASSERT_EQ(OB_SUCCESS, bind_parameter(index, replacement));
  }
  ASSERT_NO_FATAL_FAILURE(expect_point(-20, -50, -50));
  EXPECT_EQ(20, key.values()[0]); EXPECT_EQ(50, key.values()[1]); EXPECT_EQ(50, key.values()[2]);
  ASSERT_EQ(1, key.lookup_keys().count());
  EXPECT_EQ(20, key.lookup_keys().at(0).datums_[0].get_int());
  start_[0] = start_[1] = start_[2] = end_[0] = end_[1] = end_[2] = 0;
  ASSERT_NO_FATAL_FAILURE(expect_point(-50, -50, -50));
  MEMSET(values, 0xA5, sizeof(values));
  plan().reset_datum_param_store();
  const int64_t expected[3] = {20, 50, 50};
  for (int64_t i = 0; i < 3; ++i) {
    EXPECT_EQ(expected[i], key.values()[i]);
    EXPECT_EQ(expected[i], key.lookup_keys().at(0).datums_[i].get_int());
    EXPECT_EQ(expected[i], key.lookup_keys().at(0).cached_rowkey_->get_cell(i).get_int());
  }
}

TEST_F(NativeExecutionParameters, runtime_rebind_updates_native_frame_type_and_compatibility_view)
{
  ASSERT_NO_FATAL_FAILURE(load({INT64_MIN, 0, INT64_MAX}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const Datum *slot = materialized_parameter(plan(), 1);
  DatumParameter replacement = parameter(1);
  ASSERT_EQ(OB_SUCCESS, replacement.set_int(-77, request_arena_));
  ASSERT_EQ(OB_SUCCESS, bind_parameter(1, replacement));
  EXPECT_EQ(slot, materialized_parameter(plan(), 1));
  EXPECT_EQ(-77, slot->get_int());
  EXPECT_EQ(-77, parameter(1).get_int());
  ASSERT_NO_FATAL_FAILURE(expect_questionmark(1, -77));
  ASSERT_EQ(OB_SUCCESS, bind_parameter(1, parameter(1))); // self-rebind
  ASSERT_NO_FATAL_FAILURE(expect_point(INT64_MIN, -77, INT64_MAX));
  ASSERT_EQ(OB_SUCCESS, replacement.set_uint64(UINT64_MAX, request_arena_));
  replacement.set_param_meta(); // This producer's new declaration is UInt, including the following typed NULL.
  ASSERT_EQ(OB_SUCCESS, bind_parameter(1, replacement));
  EXPECT_EQ(slot, materialized_parameter(plan(), 1));
  EXPECT_EQ(UINT64_MAX, slot->get_uint());
  EXPECT_EQ(ObUInt64Type, plan().get_native_parameter_type(1)->kind());
  int64_t extracted = 101;
  EXPECT_FALSE(plan().read_native_bigint_parameter(1, extracted));
  EXPECT_EQ(101, extracted);
  ASSERT_NO_FATAL_FAILURE(expect_no_point());
  replacement.datum_.set_null(); // typed NULL keeps the UInt declaration
  ASSERT_EQ(OB_SUCCESS, bind_parameter(1, replacement));
  EXPECT_TRUE(slot->is_null());
  EXPECT_EQ(ObUInt64Type, plan().get_native_parameter_type(1)->kind());
  EXPECT_TRUE(parameter(1).is_null());
  EXPECT_EQ(OB_INVALID_ARGUMENT, bind_parameter(-1, replacement));
  EXPECT_EQ(OB_INVALID_ARGUMENT, bind_parameter(3, replacement));
}

TEST_F(NativeExecutionParameters, string_type_switch_owns_bytes_and_allocation_failure_preserves_binding)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const Datum *slot = materialized_parameter(plan(), 1);
  const SqlType *type_before = plan().get_native_parameter_type(1);
  std::string text(409, 'q');
  DatumParameter replacement;
  replacement.set_varchar_view(ObString(text.size(), &text[0]));
  replacement.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  replacement.set_length(text.size());
  owner_.fail_size_ = text.size();
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, bind_parameter(1, replacement));
  EXPECT_EQ(2, slot->get_int());
  EXPECT_EQ(type_before, plan().get_native_parameter_type(1));
  EXPECT_EQ(2, parameter(1).get_int());
  owner_.fail_size_ = 0;
  ASSERT_EQ(OB_SUCCESS, bind_parameter(1, replacement));
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
  const Datum *slot = materialized_parameter(plan(), 1);
  const SqlType *type = plan().get_native_parameter_type(1);
  Datum invalid; ASSERT_EQ(OB_SUCCESS, invalid.set_reference("bad", 3, Datum::OutRow));
  EXPECT_NE(OB_SUCCESS, plan().set_runtime_parameter(1, invalid, *type,
      *plan().get_parameter_metadata(1)));
  EXPECT_EQ(2, slot->get_int());
  EXPECT_EQ(type, plan().get_native_parameter_type(1));
  EXPECT_EQ(2, parameter(1).get_int());
}

TEST_F(NativeExecutionParameters, metadata_changes_prepare_the_actual_declaration)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  DatumParameter replacement = parameter(1);
  replacement.set_precision(11);
  replacement.set_scale(1);
  replacement.get_meta().set_collation_level(CS_LEVEL_NUMERIC);
  ASSERT_EQ(OB_SUCCESS, bind_parameter(1, replacement));
  const SqlType &type = *plan().get_native_parameter_type(1);
  EXPECT_EQ(11, type.precision()); EXPECT_EQ(1, type.scale());
  EXPECT_EQ(CS_LEVEL_NUMERIC, type.legacy_meta().get_collation_level());
  expect_metadata(replacement, parameter(1));
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
  for (int64_t i = 0; i < 2; ++i) {
    const SqlType *leaf = nullptr;
    ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(array->data_[i].get_type_meta(),
        array->data_[i].get_accuracy(), request_arena_, leaf));
    ASSERT_EQ(OB_SUCCESS, array->set_element_type_borrowed(i, *leaf, request_arena_));
  }
  DatumParameter input;
  ASSERT_EQ(OB_SUCCESS, input.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, request_arena_));
  ASSERT_EQ(OB_SUCCESS, plan().append_parameter_input(input));
  plan().set_original_param_cnt(1);
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const Datum *slot = materialized_parameter(plan(), 0);
  ASSERT_NE(nullptr, slot); EXPECT_EQ(10, slot->get_int());
  EXPECT_EQ(ObIntType, plan().get_native_parameter_type(0)->kind());
  ASSERT_NE(nullptr, plan().get_group_parameter_input(0));
  ASSERT_TRUE(plan().get_group_parameter_input(0)->is_ext_sql_array());
  EXPECT_NE(array, plan().get_group_parameter_input(0)->get_array<DatumParameterArray>());
  array->data_[0].datum_.set_int(999);
  ASSERT_EQ(OB_SUCCESS, plan().replace_batch_param_datum(1, 0, 1));
  EXPECT_EQ(slot, materialized_parameter(plan(), 0));
  EXPECT_EQ(UINT64_MAX, slot->get_uint());
  EXPECT_EQ(ObUInt64Type, plan().get_native_parameter_type(0)->kind());
  ASSERT_TRUE(plan().get_group_parameter_input(0)->is_ext_sql_array());
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
      ASSERT_TRUE(plan().has_native_parameter(i));
      EXPECT_EQ(i + 1, read_parameter_value(plan(), i).get_int());
      EXPECT_EQ(nullptr, plan().locate_materialized_parameter(i));
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
  const Datum *old_slot = materialized_parameter(plan(), 1);
  const Datum borrowed = read_parameter_value(plan(), 1);
  ASSERT_EQ(OB_SUCCESS, rebuild());
  EXPECT_NE(old_slot, materialized_parameter(plan(), 1));
  DatumParameter replacement = parameter(1);
  ASSERT_EQ(OB_SUCCESS, replacement.set_int(42, request_arena_));
  ASSERT_EQ(OB_SUCCESS, bind_parameter(1, replacement));
  EXPECT_EQ(7, old_slot->get_int()); EXPECT_EQ(7, borrowed.get_int());
  ASSERT_NO_FATAL_FAILURE(expect_questionmark(1, 42));
  plan().reset_datum_param_store();
  EXPECT_EQ(nullptr, materialized_parameter(plan(), 1));
  EXPECT_EQ(7, old_slot->get_int());
  ASSERT_NO_FATAL_FAILURE(load({4, 5, 6}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ASSERT_NO_FATAL_FAILURE(expect_point(4, 5, 6));
  plan().destroy();
  EXPECT_EQ(nullptr, materialized_parameter(plan(), 1));
  EXPECT_EQ(7, old_slot->get_int());
}

TEST_F(NativeExecutionParameters, append_and_truncate_keep_original_and_beyond_pages_separate)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const int64_t per_page = MAX_FRAME_SIZE / 24;
  ASSERT_EQ(OB_SUCCESS, plan().reserve_param_space(3 + per_page + 1));
  ASSERT_EQ(3, plan().get_param_frame_ptrs().count());
  const auto &first = ParameterValuePage::from_handle(plan().get_param_frame_ptrs().at(1));
  const auto &second = ParameterValuePage::from_handle(plan().get_param_frame_ptrs().at(2));
  EXPECT_EQ(per_page, first.capacity()); EXPECT_EQ(per_page, second.capacity());
  EXPECT_EQ(&first.eval_info(0), plan().get_parameter_eval_info(3));
  EXPECT_EQ(&second.eval_info(0), plan().get_parameter_eval_info(3 + per_page));
  EXPECT_EQ(nullptr, plan().locate_materialized_parameter(3));
  EXPECT_EQ(nullptr, plan().locate_materialized_parameter(3 + per_page));
  EXPECT_TRUE(read_parameter_value(plan(), 3 + per_page).is_null());
  DatumParameter replacement;
  ASSERT_EQ(OB_SUCCESS, replacement.set_int(-91, request_arena_));
  ASSERT_EQ(OB_SUCCESS, bind_parameter(3 + per_page, replacement));
  ASSERT_NO_FATAL_FAILURE(expect_questionmark(3 + per_page, -91));
  ASSERT_EQ(OB_SUCCESS, plan().truncate_runtime_parameters(3));
  EXPECT_EQ(nullptr, materialized_parameter(plan(), 3));
  EXPECT_EQ(nullptr, plan().get_native_parameter_type(3));
  EXPECT_EQ(3, plan().get_param_frame_ptrs().count()); // allocated pages stay stable
  ASSERT_NO_FATAL_FAILURE(expect_point(1, 2, 3));
}

TEST_F(NativeExecutionParameters, construction_staging_failure_cannot_mutate_live_slots)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  // Construction editing is transactional before initialization.
  EXPECT_EQ(OB_ERR_UNEXPECTED, plan().edit_parameter_inputs([](ParamStore &inputs) {
    inputs.at(1).datum_.set_int(999); return OB_ERR_UNEXPECTED;
  }));
  EXPECT_EQ(2, plan().get_parameter_inputs().at(1).get_int());
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const Datum *slot = materialized_parameter(plan(), 1);
  bool visited = false;
  EXPECT_EQ(OB_STATE_NOT_MATCH, plan().edit_parameter_inputs([&](ParamStore &) {
    visited = true; return OB_SUCCESS;
  }));
  EXPECT_FALSE(visited);
  EXPECT_EQ(slot, materialized_parameter(plan(), 1)); EXPECT_EQ(2, slot->get_int());
  ParamStore staged((ObWrapperAllocator(&snapshot_arena_)));
  ASSERT_EQ(OB_SUCCESS, plan().build_resolution_inputs(staged, snapshot_arena_));
  ASSERT_EQ(OB_SUCCESS, staged.at(1).set_int(77, snapshot_arena_));
  ASSERT_EQ(OB_SUCCESS, plan().rebuild_runtime_parameters(staged));
  ASSERT_NO_FATAL_FAILURE(expect_questionmark(1, 77));
  EXPECT_EQ(2, slot->get_int());
}

TEST_F(NativeExecutionParameters, frame_or_type_allocation_failure_does_not_publish_a_binding)
{
  for (int64_t bytes : {ParameterValuePage::allocation_bytes(3), int64_t(sizeof(SqlType))}) {
    ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
    owner_.fail_size_ = bytes;
    ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan().init_datum_param_store());
    EXPECT_EQ(nullptr, materialized_parameter(plan(), 0));
    EXPECT_EQ(nullptr, plan().get_native_parameter_type(0));
    EXPECT_EQ(0, plan().get_param_frame_ptrs().count());
    for (int64_t i = 0; i < 3; ++i) { EXPECT_EQ(i + 1, parameter(i).get_int()); }
    owner_.fail_size_ = 0;
    ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
    ASSERT_NO_FATAL_FAILURE(expect_questionmark(1, 2));
  }
}

TEST_F(NativeExecutionParameters, failed_rebuild_revokes_current_owner_but_keeps_old_payload_borrow)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 7, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const Datum *saved = materialized_parameter(plan(), 1);
  const SqlType *saved_type = plan().get_native_parameter_type(1);
  owner_.fail_size_ = ParameterValuePage::allocation_bytes(3);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, rebuild());
  // Failed preparation preserves the published owner; no descriptor shadow is
  // needed to recover an already-ready binding.
  EXPECT_EQ(saved, materialized_parameter(plan(), 1)); EXPECT_EQ(7, saved->get_int());
  EXPECT_EQ(saved_type, plan().get_native_parameter_type(1));
  EXPECT_EQ(1, plan().get_param_frame_ptrs().count());
  owner_.fail_size_ = 0;
  ASSERT_EQ(OB_SUCCESS, rebuild());
  EXPECT_NE(saved, materialized_parameter(plan(), 1));
  EXPECT_EQ(7, saved->get_int());
}

TEST_F(NativeExecutionParameters, restored_final_pages_preserve_eval_info_and_failure_preserves_old_owner)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ObSEArray<char *, 1> pages; ObSEArray<const SqlType *, 3> types;
  ParameterValuePage *values = nullptr;
  ASSERT_EQ(OB_SUCCESS, ParameterValuePage::allocate(owner_, 3, values));
  ASSERT_NE(nullptr, values);
  char *page = reinterpret_cast<char *>(values);
  for (int64_t i = 0; i < 3; ++i) {
    Datum value; value.set_int(101 + i);
    ASSERT_EQ(OB_SUCCESS, values->assign(i, value, *plan().get_native_parameter_type(i), owner_));
    values->eval_info(i).flag_ = 3; values->eval_info(i).cnt_ = 17;
    ASSERT_EQ(OB_SUCCESS, types.push_back(plan().get_native_parameter_type(i)));
  }
  ASSERT_EQ(OB_SUCCESS, pages.push_back(page));
  const Datum *old = materialized_parameter(plan(), 0);
  SqlTypeMeta string_meta; string_meta.set_varchar();
  string_meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  SqlType string_type;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(string_meta, ObAccuracy(32), string_type));
  Datum invalid; invalid.set_string(ObString::make_string("invalid inline integer"));
  ASSERT_EQ(OB_SUCCESS, values->assign(2, invalid, string_type, owner_));
  // The page contains a reference layout, but the supplied directory still
  // declares signed BIGINT. Validation must reject before replacing the owner.
  EXPECT_NE(OB_SUCCESS, plan().restore_native_parameter_frames(pages, types, 3));
  EXPECT_EQ(old, materialized_parameter(plan(), 0)); EXPECT_EQ(1, old->get_int());
  invalid.set_null();
  ASSERT_EQ(OB_SUCCESS, values->assign(2, invalid, string_type, owner_));
  EXPECT_NE(OB_SUCCESS, plan().restore_native_parameter_frames(pages, types, 3));
  EXPECT_EQ(old, materialized_parameter(plan(), 0)); EXPECT_EQ(1, old->get_int());
  Datum repaired; repaired.set_int(103);
  ASSERT_EQ(OB_SUCCESS, values->assign(2, repaired, *types.at(2), owner_));
  values->eval_info(2).flag_ = 3; values->eval_info(2).cnt_ = 17;
  ASSERT_EQ(OB_SUCCESS, plan().restore_native_parameter_frames(pages, types, 3));
  EXPECT_EQ(page, plan().get_param_frame_ptrs().at(0));
  for (int64_t i = 0; i < 3; ++i) {
    EXPECT_EQ(101 + i, parameter(i).get_int());
    const ObEvalInfo *info = plan().get_parameter_eval_info(i);
    EXPECT_EQ(3, info->flag_); EXPECT_EQ(17, info->cnt_);
  }
  EXPECT_EQ(1, old->get_int());
}

TEST_F(NativeExecutionParameters, real_wire_roundtrip_requires_native_initialization_and_owns_decoded_payload)
{
  ASSERT_NO_FATAL_FAILURE(load({INT64_MIN, 7, INT64_MAX}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const Datum old_value = read_parameter_value(plan(), 1);
  std::string wire(plan().get_serialize_size(), '\0');
  int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, plan().serialize(&wire[0], wire.size(), position));
  ASSERT_EQ(wire.size(), position);
  position = 0;
  ASSERT_EQ(OB_SUCCESS, plan().deserialize(wire.data(), wire.size(), position));
  EXPECT_EQ(wire.size(), position);
  EXPECT_EQ(nullptr, materialized_parameter(plan(), 1));
  ASSERT_EQ(3, plan().get_parameter_count());
  EXPECT_EQ(7, parameter(1).get_int());
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
  EXPECT_NE(nullptr, materialized_parameter(plan(), 1));
  position = 0;
  EXPECT_NE(OB_SUCCESS, plan().deserialize(bad_body.data(), bad_body.size(), position));
  EXPECT_EQ(nullptr, materialized_parameter(plan(), 1));
  EXPECT_EQ(0, plan().get_parameter_count());
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
    ASSERT_EQ(OB_SUCCESS, ObSQLUtils::revise_hash_part_object(value, result_owner));
    EXPECT_EQ(ObIntType, value.get_type());
    ASSERT_FALSE(value.is_null());
    EXPECT_EQ(0u, value.datum_.length());
    EXPECT_EQ(0, result_owner.calls_for(sizeof(int64_t)));
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
    alignas(8) char before_scratch[64], after_scratch[64]; ObDatum before_view, after_view;
    ASSERT_EQ(OB_SUCCESS, value.export_legacy(before_view, before_scratch, sizeof(before_scratch)));
    const std::string bytes(before_view.ptr_, before_view.len_);
    EXPECT_EQ(OB_INVALID_ARGUMENT, ObSQLUtils::revise_hash_part_object(value, result_owner));
    ASSERT_EQ(OB_SUCCESS, value.export_legacy(after_view, after_scratch, sizeof(after_scratch)));
    EXPECT_EQ(before_view.pack_, after_view.pack_);
    EXPECT_EQ(0, MEMCMP(&original.meta_, &value.meta_, sizeof(value.meta_)));
    EXPECT_EQ(bytes, std::string(after_view.ptr_, after_view.len_));
    EXPECT_TRUE(result_owner.sizes_.empty());
  }
}

TEST(NativeHashPartitionValue, result_allocation_failure_preserves_input_including_typed_null)
{
  // The historical scalar output allocation no longer exists. A rejecting
  // allocator must not turn valid I64/U64/typed-NULL HASH results into OOM.
  for (int input = 0; input < 3; ++input) {
    SCOPED_TRACE(input);
    ObArenaAllocator source_owner("PartHashSrc");
    ExecutionAllocator result_owner; result_owner.fail_size_ = sizeof(int64_t);
    DatumValue value;
    if (input == 1) { ASSERT_EQ(OB_SUCCESS, value.set_uint(ObUInt64Type, UINT64_MAX, source_owner)); }
    else {
      ASSERT_EQ(OB_SUCCESS, value.set_int(INT64_MIN, source_owner));
      if (input == 2) { value.datum_.set_null(); }
    }
    ASSERT_EQ(OB_SUCCESS, ObSQLUtils::revise_hash_part_object(value, result_owner));
    EXPECT_EQ(input == 0 ? INT64_MAX : input == 1 ? 1 : 0, value.get_int());
    EXPECT_EQ(ObIntType, value.get_type()); EXPECT_FALSE(value.is_null());
    EXPECT_EQ(0, result_owner.calls_for(sizeof(int64_t)));
  }
}

TEST_F(NativeExecutionParameters, supplied_type_binding_reuses_exact_type_and_ignores_attribute_payload)
{
  ASSERT_NO_FATAL_FAILURE(load({11}));
  Datum value; value.set_int(INT64_MIN);
  DatumParameterMetadata attributes = parameter(0).get_metadata();
  EXPECT_EQ(OB_NOT_INIT, plan().set_runtime_parameter(0, value, request_type_, attributes));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  attributes = parameter(0).get_metadata();
  attributes.accuracy_.set_precision(9);
  attributes.res_flags_ = 0x1234;
  attributes.raw_text_pos_ = 7; attributes.raw_text_len_ = 13;
  // There is no attribute payload: the supplied native value is authoritative.
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(0, value, request_type_, attributes));
  EXPECT_EQ(INT64_MIN, read_parameter_value(plan(), 0).get_int());
  EXPECT_EQ(9, parameter(0).get_precision());
  EXPECT_EQ(0x1234, parameter(0).get_result_flag());
  EXPECT_EQ(7, parameter(0).get_raw_text_pos());
  EXPECT_EQ(13, parameter(0).get_raw_text_len());
  const Datum *slot = materialized_parameter(plan(), 0);
  const SqlType *type = plan().get_native_parameter_type(0);
  owner_.clear_calls();
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(0, *slot, *type, *plan().get_parameter_metadata(0)));
  EXPECT_EQ(slot, materialized_parameter(plan(), 0));
  EXPECT_EQ(type, plan().get_native_parameter_type(0));
  EXPECT_TRUE(owner_.sizes_.empty());
  EXPECT_EQ(0u, slot->length());
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_count(plan()));

  // Coercibility is deliberately ignored by SqlType::equals, but execution
  // metadata must not be silently reused from the old declaration.
  SqlTypeMeta changed_meta = request_type_.legacy_meta();
  changed_meta.set_collation_level(CS_LEVEL_IMPLICIT);
  changed_meta.set_scale(17); // SqlTypeMeta::operator== deliberately omits this byte.
  SqlType changed;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(changed_meta, request_type_.accuracy(), changed));
  ASSERT_TRUE(type->equals(changed));
  Datum null; null.set_null();
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(0, null, changed, *plan().get_parameter_metadata(0)));
  EXPECT_NE(type, plan().get_native_parameter_type(0));
  EXPECT_EQ(CS_LEVEL_IMPLICIT, plan().get_native_parameter_type(0)->legacy_meta().get_collation_level());
  EXPECT_EQ(17, plan().get_native_parameter_type(0)->legacy_meta().get_scale());
  EXPECT_TRUE(read_parameter_value(plan(), 0).is_null());
  EXPECT_EQ(ObNullType, parameter(0).get_type());
  EXPECT_EQ(ObIntType, parameter(0).get_param_meta().get_type());
  EXPECT_EQ(9, parameter(0).get_precision());
}

TEST_F(NativeExecutionParameters, supplied_reference_and_type_failures_preserve_the_published_binding)
{
  ASSERT_NO_FATAL_FAILURE(load({42}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const SqlType *old_type = plan().get_native_parameter_type(0);
  const DatumParameter before = parameter(0);
  auto *info = plan().get_parameter_eval_info(0);
  info->evaluated_ = true; info->projected_ = true;
  const Datum *old_slot = materialized_parameter(plan(), 0);
  // Bare states no longer require an obsolete extended-wrapper allocation.
  Datum state; state.set_min();
  owner_.clear_calls(); owner_.fail_size_ = DATUM_EXTEND_PAYLOAD_SIZE;
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(0, state, *old_type, before.get_metadata()));
  EXPECT_TRUE(read_parameter_value(plan(), 0).is_min());
  EXPECT_EQ(0, owner_.calls_for(DATUM_EXTEND_PAYLOAD_SIZE));
  Datum restored; restored.set_int(42);
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(0, restored, *old_type, before.get_metadata()));
  info->evaluated_ = true; info->projected_ = true;
  // The reference and Type failures below still exercise real allocations.
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
      EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan().set_runtime_parameter(0, input, *type, before.get_metadata()));
      EXPECT_EQ(1, owner_.calls_for(sizeof(SqlType)));
      EXPECT_EQ(rejected == int64_t(text.size()) ? 1 : 0, owner_.calls_for(text.size()));
      EXPECT_EQ(old_type, plan().get_native_parameter_type(0));
      EXPECT_EQ(42, read_parameter_value(plan(), 0).get_int());
      EXPECT_EQ(old_slot, materialized_parameter(plan(), 0));
      EXPECT_TRUE(info->evaluated_); EXPECT_TRUE(info->projected_);
      expect_metadata(before, parameter(0));
    }
    owner_.fail_size_ = 0;
    ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(0, input, *type, before.get_metadata()));
    EXPECT_NE(type, plan().get_native_parameter_type(0));
    EXPECT_NE(input.get_reference(), read_parameter_value(plan(), 0).get_reference());
    EXPECT_FALSE(info->evaluated_); EXPECT_FALSE(info->projected_);
  }
  EXPECT_EQ(ObVarcharType, plan().get_native_parameter_type(0)->kind());
  EXPECT_EQ(409, plan().get_native_parameter_type(0)->length());
  EXPECT_EQ(text, std::string(read_parameter_value(plan(), 0).get_string().ptr(), text.size()));
  const SqlType *type = plan().get_native_parameter_type(0);
  const DatumParameter stable = parameter(0);
  Datum invalid; ASSERT_EQ(OB_SUCCESS, invalid.set_string("bad", 3));
  // An inline declaration must reject a nonempty reference span.
  EXPECT_EQ(OB_INVALID_ARGUMENT, plan().set_runtime_parameter(0, invalid, request_type_, stable.get_metadata()));
  EXPECT_EQ(type, plan().get_native_parameter_type(0));
  EXPECT_EQ(409u, read_parameter_value(plan(), 0).length());
}

TEST_F(NativeExecutionParameters, supplied_binding_respects_original_and_appended_page_boundaries)
{
  const int64_t per_page = ScalarFrameLayout::slots_per_frame();
  ASSERT_NO_FATAL_FAILURE(load(std::vector<int64_t>(per_page + 1, 17)));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ASSERT_EQ(OB_SUCCESS, plan().reserve_param_space(2 * per_page + 2));
  for (int64_t index : {per_page - 1, per_page, per_page + 1, 2 * per_page + 1}) {
    Datum value; value.set_int(-index);
    ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(index, value, request_type_, *plan().get_parameter_metadata(index)));
    expect_questionmark(index, -index);
    int64_t page = 0, slot = 0;
    ScalarFrameLayout::locate_parameter(per_page + 1, index, page, slot);
    const auto &values = ParameterValuePage::from_handle(plan().get_param_frame_ptrs().at(page));
    Datum actual;
    ASSERT_EQ(OB_SUCCESS, values.read(slot, actual));
    EXPECT_EQ(-index, actual.get_int());
    EXPECT_EQ(&values.eval_info(slot), plan().get_parameter_eval_info(index));
    EXPECT_EQ(nullptr, plan().locate_materialized_parameter(index));
  }
  const int64_t count = plan().get_parameter_count();
  Datum value; value.set_int(99);
  for (int64_t index : {int64_t(-1), count}) {
    EXPECT_EQ(OB_INVALID_ARGUMENT, plan().set_runtime_parameter(index, value, request_type_, *plan().get_parameter_metadata(0)));
  }
  EXPECT_EQ(count, plan().get_parameter_count());
}


TEST_F(NativeExecutionParameters, construction_arena_is_physically_retired_after_typed_null_and_reference_bind)
{
  ASSERT_NO_FATAL_FAILURE(load({INT64_MIN, 0, 3}));
  ObArenaAllocator source_owner("ParamSource");
  DatumParameter null;
  ASSERT_EQ(OB_SUCCESS, null.set_uint64(UINT64_MAX, source_owner));
  null.set_accuracy(ObAccuracy(20, 0)); null.set_param_meta(); null.set_null();
  null.set_raw_text_info(17, 23); null.set_result_flags(0x1234);
  ASSERT_EQ(OB_SUCCESS, plan().set_parameter_input(1, null));
  const std::string expected(211, 's');
  char *bytes = static_cast<char *>(source_owner.alloc(expected.size())); ASSERT_NE(nullptr, bytes);
  MEMCPY(bytes, expected.data(), expected.size());
  DatumParameter text; text.set_varchar_view(ObString(expected.size(), bytes));
  text.set_collation_type(CS_TYPE_UTF8MB4_BIN); text.set_length(expected.size()); text.set_param_meta();
  ASSERT_EQ(OB_SUCCESS, plan().set_parameter_input(2, text));
  ASSERT_GT(NativeExecutionParametersTestPeer::input_bytes(plan()), 0);
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_count(plan()));
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_bytes(plan()));
  MEMSET(bytes, 0xa5, expected.size()); source_owner.reset();
  EXPECT_EQ(INT64_MIN, read_parameter_value(plan(), 0).get_int());
  EXPECT_TRUE(read_parameter_value(plan(), 1).is_null());
  EXPECT_EQ(ObUInt64Type, plan().get_native_parameter_type(1)->kind());
  EXPECT_EQ(ObUInt64Type, plan().get_parameter_metadata(1)->param_meta_.get_type());
  EXPECT_EQ(17, plan().get_parameter_metadata(1)->raw_text_pos_);
  EXPECT_EQ(23, plan().get_parameter_metadata(1)->raw_text_len_);
  EXPECT_EQ(0x1234u, plan().get_parameter_metadata(1)->res_flags_);
  EXPECT_EQ(expected, std::string(read_parameter_value(plan(), 2).get_string().ptr(), expected.size()));
}

TEST_F(NativeExecutionParameters, appended_full_pages_keep_live_slot_and_eval_info_addresses_stable)
{
  ASSERT_NO_FATAL_FAILURE(load({11, 22, 33}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const Datum *original = materialized_parameter(plan(), 1);
  ASSERT_EQ(OB_SUCCESS, plan().reserve_param_space(4));
  const Datum *appended = materialized_parameter(plan(), 3);
  char *first_beyond_page = plan().get_param_frame_ptrs().at(1);
  auto *info = plan().get_parameter_eval_info(3);
  Datum value; value.set_int(-91);
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(3, value, request_type_,
      *plan().get_parameter_metadata(0)));
  info->evaluated_ = true; info->projected_ = true; info->cnt_ = 17;
  const int64_t per_page = ScalarFrameLayout::slots_per_frame();
  ASSERT_EQ(OB_SUCCESS, plan().reserve_param_space(3 + per_page));
  EXPECT_EQ(original, materialized_parameter(plan(), 1));
  EXPECT_EQ(appended, materialized_parameter(plan(), 3));
  EXPECT_EQ(first_beyond_page, plan().get_param_frame_ptrs().at(1));
  owner_.clear_calls(); owner_.fail_size_ = ParameterValuePage::allocation_bytes(per_page);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan().reserve_param_space(4 + per_page));
  EXPECT_EQ(1, owner_.calls_for(ParameterValuePage::allocation_bytes(per_page)));
  EXPECT_EQ(3 + per_page, plan().get_parameter_count());
  EXPECT_EQ(appended, materialized_parameter(plan(), 3)); EXPECT_EQ(-91, appended->get_int());
  EXPECT_TRUE(info->evaluated_); EXPECT_TRUE(info->projected_); EXPECT_EQ(17, info->cnt_);
  owner_.fail_size_ = 0;
  ASSERT_EQ(OB_SUCCESS, plan().reserve_param_space(4 + per_page));
  EXPECT_EQ(original, materialized_parameter(plan(), 1)); EXPECT_EQ(appended, materialized_parameter(plan(), 3));
  EXPECT_EQ(first_beyond_page, plan().get_param_frame_ptrs().at(1));
  EXPECT_EQ(-91, appended->get_int());
  EXPECT_TRUE(info->evaluated_); EXPECT_TRUE(info->projected_); EXPECT_EQ(17, info->cnt_);
}

TEST_F(NativeExecutionParameters, typed_group_wire_owns_each_leaf_declaration_before_source_and_wire_retire)
{
  ObArenaAllocator source_owner("GroupWireSrc"), receiver_owner("GroupWireRecv");
  DatumParameterArray *source = DatumParameterArray::alloc(source_owner, 2); ASSERT_NE(nullptr, source);
  ASSERT_EQ(OB_SUCCESS, source->data_[0].set_int(INT64_MIN, source_owner));
  source->data_[0].set_accuracy(ObAccuracy(19, 0)); source->data_[0].set_param_meta();
  int64_t coefficient = 12345;
  DatumValue decimal;
  ASSERT_EQ(OB_SUCCESS, decimal.set_decimal_int(8, 5,
      reinterpret_cast<const ObDecimalInt *>(&coefficient), source_owner));
  source->data_[1].assign_value(decimal);
  source->data_[1].set_accuracy(ObAccuracy(20, 5)); source->data_[1].set_param_meta();
  source->data_[1].set_null();
  for (int64_t i = 0; i < 2; ++i) {
    const DatumParameter &cell = source->data_[i]; const SqlType *type = nullptr;
    const SqlTypeMeta meta = cell.is_null() ? cell.get_param_meta() : cell.get_type_meta();
    ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, cell.get_accuracy(), source_owner, type));
    ASSERT_EQ(OB_SUCCESS, source->set_element_type_borrowed(i, *type, source_owner));
  }
  SqlTypeMeta array_meta; array_meta.set_ext(); array_meta.set_extend_type(T_EXT_SQL_ARRAY);
  DatumParameter envelope; envelope.set_type_meta(array_meta);
  ASSERT_EQ(OB_SUCCESS, envelope.set_array_address(source, source_owner));
  envelope.set_param_meta(); envelope.set_raw_text_info(17, 23);
  envelope.set_result_flags(0x1234); envelope.get_param_flag().flag_ = 0xa5;
  envelope.get_param_flag().extend_flag_ = 0x5a;
  // The producer is a real ready PlanCtx; parameter wire carries both the
  // selected NULL slot and its sparse full group, each with seven attributes.
  plan().reset_datum_param_store();
  ASSERT_EQ(OB_SUCCESS, plan().append_parameter_input(envelope)); plan().set_original_param_cnt(1);
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  source_owner.reset();
  ASSERT_EQ(OB_SUCCESS, plan().replace_batch_param_datum(1, 0, 1));
  int64_t size = 0;
  ASSERT_EQ(OB_SUCCESS, NativeFrameCodec::parameter_size(plan(), size));
  std::string wire(size, '\0'); int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, NativeFrameCodec::serialize_parameters(plan(), &wire[0], wire.size(), pos));
  EXPECT_EQ(size, pos);
  ObExecContext receiver(receiver_owner); ASSERT_EQ(OB_SUCCESS, receiver.create_physical_plan_ctx());
  auto &received = *receiver.get_physical_plan_ctx();
  ObSEArray<char *, 1> pages; ObSEArray<const SqlType *, 2> types;
  ObSEArray<DatumParameterMetadata, 2> metadata; ObSEArray<NativeParameterGroup, 1> groups;
  int64_t original_count = 0; int32_t version = 0; pos = 0;
  ASSERT_EQ(OB_SUCCESS, NativeFrameCodec::deserialize_parameters(wire.data(), wire.size(), pos,
      receiver_owner, pages, types, original_count, &metadata, &groups, &version));
  EXPECT_EQ(size, pos); EXPECT_EQ(2, version); ASSERT_EQ(1, types.count());
  ASSERT_EQ(1, metadata.count()); ASSERT_EQ(1, groups.count());
  ASSERT_EQ(OB_SUCCESS, received.restore_native_parameter_frames(pages, types, original_count,
      &metadata, &groups));
  wire.assign(wire.size(), char(0xa5));
  EXPECT_TRUE(read_parameter_value(received, 0).is_null());
  EXPECT_EQ(ObDecimalIntType, received.get_native_parameter_type(0)->kind());
  EXPECT_EQ(20, received.get_native_parameter_type(0)->precision());
  EXPECT_EQ(5, received.get_native_parameter_type(0)->scale());
  ASSERT_NE(nullptr, received.get_group_parameter_input(0));
  const auto *owned = received.get_group_parameter_input(0)->get_array<DatumParameterArray>();
  ASSERT_NE(nullptr, owned); ASSERT_NE(nullptr, owned->get_element_type(1));
  EXPECT_EQ(20, owned->get_element_type(1)->precision()); EXPECT_EQ(5, owned->get_element_type(1)->scale());
  const auto &attributes = *received.get_parameter_metadata(0);
  EXPECT_EQ(17, attributes.raw_text_pos_); EXPECT_EQ(23, attributes.raw_text_len_);
  EXPECT_EQ(0x1234u, attributes.res_flags_); EXPECT_EQ(0xa5, attributes.flag_.flag_);
  EXPECT_EQ(0x5a, attributes.flag_.extend_flag_);
  EXPECT_EQ(ObDecimalIntType, attributes.param_meta_.get_type()); EXPECT_EQ(5, attributes.accuracy_.get_scale());
  ASSERT_EQ(OB_SUCCESS, received.replace_batch_param_datum(0, 0, 1));
  EXPECT_EQ(INT64_MIN, read_parameter_value(received, 0).get_int());
}


TEST_F(NativeExecutionParameters, resolver_inputs_use_independent_descriptors_and_statement_owned_values_then_merge_only_flags)
{
  ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
  ObArenaAllocator source_owner("ResolveSrc"), descriptors("ResolveDesc"), statement_owner("ResolveStmt");
  DatumParameter null; ASSERT_EQ(OB_SUCCESS, null.set_uint64(UINT64_MAX, source_owner));
  null.set_accuracy(ObAccuracy(20, 0)); null.set_param_meta(); null.set_null();
  null.set_result_flags(0x1234); null.set_raw_text_info(17, 23);
  ASSERT_EQ(OB_SUCCESS, plan().set_parameter_input(0, null));
  const std::string expected(211, 'r');
  DatumParameter text; ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString(expected.size(), expected.data()), source_owner));
  text.set_collation_type(CS_TYPE_UTF8MB4_BIN); text.set_length(expected.size()); text.set_param_meta();
  ASSERT_EQ(OB_SUCCESS, plan().set_parameter_input(1, text));
  DatumParameterArray *array = DatumParameterArray::alloc(source_owner, 2); ASSERT_NE(nullptr, array);
  ASSERT_EQ(OB_SUCCESS, array->data_[0].set_int(INT64_MIN, source_owner));
  array->data_[0].set_accuracy(ObAccuracy(19, 0)); array->data_[0].set_param_meta();
  int64_t coefficient = 12345; DatumValue decimal;
  ASSERT_EQ(OB_SUCCESS, decimal.set_decimal_int(8, 5,
      reinterpret_cast<const ObDecimalInt *>(&coefficient), source_owner));
  array->data_[1].assign_value(decimal); array->data_[1].set_accuracy(ObAccuracy(20, 5));
  array->data_[1].set_param_meta(); array->data_[1].set_null();
  for (int64_t i = 0; i < 2; ++i) {
    const DatumParameter &cell = array->data_[i]; const SqlType *type = nullptr;
    ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(cell.is_null() ? cell.get_param_meta() : cell.get_type_meta(),
        cell.get_accuracy(), source_owner, type));
    ASSERT_EQ(OB_SUCCESS, array->set_element_type_borrowed(i, *type, source_owner));
  }
  DatumParameter group; ASSERT_EQ(OB_SUCCESS, group.set_extend(
      reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, source_owner)); group.set_param_meta();
  ASSERT_EQ(OB_SUCCESS, plan().set_parameter_input(2, group));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ParamStore resolution((ObWrapperAllocator(&descriptors)));
  ASSERT_EQ(OB_SUCCESS, plan().build_resolution_inputs(resolution, statement_owner));
  ASSERT_EQ(3, resolution.count());
  EXPECT_TRUE(resolution.at(0).is_null()); EXPECT_EQ(ObUInt64Type, resolution.at(0).get_param_meta().get_type());
  EXPECT_EQ(20, resolution.at(0).get_precision()); EXPECT_EQ(17, resolution.at(0).get_raw_text_pos());
  EXPECT_EQ(23, resolution.at(0).get_raw_text_len()); EXPECT_EQ(0x1234u, resolution.at(0).get_result_flag());
  EXPECT_NE(read_parameter_value(plan(), 1).get_reference(), resolution.at(1).get_datum().get_reference());
  ASSERT_TRUE(resolution.at(2).is_ext_sql_array());
  const auto *retained = resolution.at(2).get_array<DatumParameterArray>();
  ASSERT_NE(nullptr, retained); ASSERT_NE(nullptr, retained->get_element_type(1));
  const auto *runtime_group = plan().get_group_parameter_input(2)->get_array<DatumParameterArray>();
  ASSERT_NE(nullptr, runtime_group);
  EXPECT_NE(runtime_group, retained);
  EXPECT_NE(runtime_group->get_element_type(1), retained->get_element_type(1));
  EXPECT_EQ(ObDecimalIntType, retained->get_element_type(1)->kind());
  EXPECT_EQ(20, retained->get_element_type(1)->precision()); EXPECT_EQ(5, retained->get_element_type(1)->scale());
  const DatumParameter saved_null = resolution.at(0), saved_text = resolution.at(1);
  ASSERT_EQ(OB_SUCCESS, resolution.at(0).set_int(999, statement_owner));
  resolution.at(0).set_accuracy(ObAccuracy(3, 2));
  resolution.at(0).get_param_flag().flag_ = 0xa5; resolution.at(0).get_param_flag().extend_flag_ = 0x5a;
  ASSERT_EQ(OB_SUCCESS, plan().merge_resolution_parameter_flags(resolution));
  EXPECT_TRUE(read_parameter_value(plan(), 0).is_null());
  EXPECT_EQ(20, plan().get_parameter_metadata(0)->accuracy_.get_precision());
  EXPECT_EQ(0xa5, plan().get_parameter_metadata(0)->flag_.flag_);
  EXPECT_EQ(0x5a, plan().get_parameter_metadata(0)->flag_.extend_flag_);
  EXPECT_EQ(expected, std::string(read_parameter_value(plan(), 1).get_string().ptr(), expected.size()));
  resolution.at(0) = saved_null; resolution.at(1) = saved_text;
  MEMSET(const_cast<void *>(read_parameter_value(plan(), 1).get_reference()), 0xa5, expected.size());
  source_owner.reset(); plan().reset_datum_param_store();
  EXPECT_TRUE(resolution.at(0).is_null()); EXPECT_EQ(ObUInt64Type, resolution.at(0).get_param_meta().get_type());
  EXPECT_EQ(expected, std::string(resolution.at(1).get_string().ptr(), expected.size()));
  EXPECT_EQ(INT64_MIN, retained->data_[0].get_int()); EXPECT_TRUE(retained->data_[1].is_null());
  EXPECT_EQ(ObDecimalIntType, retained->get_element_type(1)->kind());
  EXPECT_EQ(20, retained->get_element_type(1)->precision()); EXPECT_EQ(5, retained->get_element_type(1)->scale());
}

namespace
{
void expect_resolver_attributes(const DatumParameterMetadata &expected,
                                const DatumParameterMetadata &actual)
{
  EXPECT_EQ(expected.type_meta_, actual.type_meta_);
  EXPECT_EQ(expected.type_meta_.get_scale(), actual.type_meta_.get_scale());
  EXPECT_EQ(expected.accuracy_.get_accuracy(), actual.accuracy_.get_accuracy());
  EXPECT_EQ(expected.param_meta_, actual.param_meta_);
  EXPECT_EQ(expected.param_meta_.get_scale(), actual.param_meta_.get_scale());
  EXPECT_EQ(expected.res_flags_, actual.res_flags_);
  EXPECT_EQ(expected.raw_text_pos_, actual.raw_text_pos_);
  EXPECT_EQ(expected.raw_text_len_, actual.raw_text_len_);
  EXPECT_EQ(expected.flag_.flag_, actual.flag_.flag_);
  EXPECT_EQ(expected.flag_.extend_flag_, actual.flag_.extend_flag_);
}

int resolve_snapshot_questionmark(ObRawExprFactory &factory, ObSQLSessionInfo &session,
    const ParamStore &inputs, int64_t index, ObRawExpr *&result)
{
  ParseNode node = {};
  node.type_ = T_QUESTIONMARK;
  node.str_value_ = "?";
  node.str_len_ = 1;
  node.value_ = index;
  ObSEArray<ObQualifiedName, 1> columns;
  ObSEArray<ObVarInfo, 1> variables;
  ObSEArray<ObAggFunRawExpr *, 1> aggregates;
  ObSEArray<ObWinFunRawExpr *, 1> windows;
  ObSEArray<ObSubQueryInfo, 1> subqueries;
  ObSEArray<ObUDFInfo, 1> udfs;
  ObSEArray<ObOpRawExpr *, 1> operations;
  return ObRawExprUtils::build_raw_expr(factory, session, nullptr, nullptr,
      T_NONE_SCOPE, nullptr, &inputs, nullptr, node, result, columns, variables,
      aggregates, windows, subqueries, udfs, operations, false);
}

struct ResolverInputSessionGuard
{
  ResolverInputSessionGuard(ObSQLSessionInfo &session, ObExecContext &execution)
      : session_(session), execution_(execution)
  {
    execution_.set_my_session(&session_);
    ObSQLSessionInfo::ExecCtxSessionRegister registration(session_, &execution_);
  }
  ~ResolverInputSessionGuard()
  {
    ObSQLSessionInfo::ExecCtxSessionRegister unregister(session_, nullptr);
    execution_.set_my_session(nullptr);
  }
  ObSQLSessionInfo &session_;
  ObExecContext &execution_;
};

class RejectResolverDescriptors final : public ObIAllocator
{
public:
  void *alloc(int64_t) override { ++calls_; return nullptr; }
  void *alloc(int64_t, const ObMemAttr &) override { ++calls_; return nullptr; }
  void free(void *) override {}
  int64_t calls_ = 0;
};
} // namespace

TEST_F(NativeExecutionParameters, construction_resolver_snapshot_keeps_three_double_types_through_mid_resolution_bind)
{
  ASSERT_EQ(OB_SUCCESS, oceanbase::share::ObSysVariables::init_default_values());
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ResolverInputSessionGuard registration(session, exec_);
  ObArenaAllocator descriptors("ResolveDesc"), statement_owner("ResolveStmt"), raw_owner("ResolveRaw");
  ParamStore original((ObWrapperAllocator(&request_arena_)));
  const double keys[] = {1.0, 1.0, 7.0};
  for (int64_t i = 0; i < 3; ++i) {
    DatumParameter input;
    ASSERT_EQ(OB_SUCCESS, input.set_double(keys[i], request_arena_));
    input.set_collation_level(CS_LEVEL_COERCIBLE);
    input.set_param_meta();
    input.set_accuracy(ObAccuracy(15, 7));
    input.set_result_flags(0x1200 + i);
    input.set_raw_text_info(17 + i, 23 + i);
    input.get_param_flag().flag_ = 0x40 + i;
    input.get_param_flag().extend_flag_ = 0x80 + i;
    ASSERT_EQ(OB_SUCCESS, original.push_back(input));
  }
  ASSERT_EQ(OB_SUCCESS, plan().assign_parameter_inputs(original));
  plan().set_original_param_cnt(3);
  ASSERT_GT(NativeExecutionParametersTestPeer::input_bytes(plan()), 0);
  ParamStore resolution((ObWrapperAllocator(&descriptors)));
  ASSERT_EQ(OB_SUCCESS, plan().build_resolution_inputs(resolution, statement_owner));
  ASSERT_FALSE(plan().native_parameters_ready());
  ASSERT_EQ(3, resolution.count());
  EXPECT_EQ(0, statement_owner.used()); // Only the three scalar payload captures.
  ObRawExprFactory factory(raw_owner);
  ObRawExpr *first = nullptr;
  ASSERT_EQ(OB_SUCCESS, resolve_snapshot_questionmark(factory, session, resolution, 0, first));
  ASSERT_NE(nullptr, first);
  EXPECT_EQ(ObDoubleType, first->get_data_type());

  // The real public bind retires the construction arena while resolution is
  // still using its local inputs, as DOUBLE demotion does during formalize.
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_count(plan()));
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_bytes(plan()));
  ObEvalCtx evaluation(exec_);
  for (int64_t i : {1, 2, 0}) {
    SCOPED_TRACE(i);
    ObRawExpr *resolved = nullptr;
    ASSERT_EQ(OB_SUCCESS, resolve_snapshot_questionmark(factory, session, resolution, i, resolved));
    ASSERT_NE(nullptr, resolved);
    ASSERT_TRUE(resolved->is_const_raw_expr());
    const auto &constant = static_cast<const ObConstRawExpr &>(*resolved);
    EXPECT_EQ(ObDoubleType, constant.get_data_type());
    EXPECT_EQ(ObDoubleType, constant.get_bound_param_value_meta().get_type());
    EXPECT_DOUBLE_EQ(keys[i], constant.get_bound_param_datum().get_double());
    int64_t index = -1;
    ASSERT_EQ(OB_SUCCESS, constant.get_questionmark_index(index));
    EXPECT_EQ(i, index);
    expect_resolver_attributes(original.at(i).get_metadata(), resolution.at(i).get_metadata());
    ObExpr runtime;
    runtime.extra_ = index;
    runtime.datum_meta_.type_ = constant.get_data_type();
    Datum result;
    ASSERT_EQ(OB_SUCCESS, eval_question_mark_func(runtime, evaluation, result));
    EXPECT_DOUBLE_EQ(keys[i], result.get_double());
    EXPECT_EQ(ObDoubleType, plan().get_native_parameter_type(i)->kind());
  }
}

TEST_F(NativeExecutionParameters, construction_resolver_reference_and_typed_null_outlive_both_source_arenas)
{
  ObArenaAllocator source_owner("ResolveSrc"), descriptors("ResolveDesc"), statement_owner("ResolveStmt");
  ParamStore original((ObWrapperAllocator(&source_owner)));
  const std::string expected = std::string("r\0s", 3) + std::string(190, 'n');
  DatumParameter text;
  ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString(expected.size(), expected.data()), source_owner));
  text.set_collation_type(CS_TYPE_UTF8MB4_BIN); text.set_length(expected.size()); text.set_param_meta();
  text.set_raw_text_info(7, 11); text.set_result_flags(0x1234);
  text.get_param_flag().flag_ = 0x51; text.get_param_flag().extend_flag_ = 0x80;
  ASSERT_EQ(OB_SUCCESS, original.push_back(text));
  const int64_t coefficient = 12345;
  DatumValue decimal;
  ASSERT_EQ(OB_SUCCESS, decimal.set_decimal_int(8, 5,
      reinterpret_cast<const ObDecimalInt *>(&coefficient), source_owner));
  DatumParameter null; null.assign_value(decimal);
  null.set_accuracy(ObAccuracy(20, 5)); null.set_param_meta(); null.set_null();
  null.set_raw_text_info(17, 23); null.set_result_flags(0x4321);
  null.get_param_flag().flag_ = 0x42; null.get_param_flag().extend_flag_ = 0x81;
  ASSERT_EQ(OB_SUCCESS, original.push_back(null));
  const DatumParameterMetadata text_attributes = text.get_metadata(), null_attributes = null.get_metadata();
  ASSERT_EQ(OB_SUCCESS, plan().assign_parameter_inputs(original)); plan().set_original_param_cnt(2);
  // assign borrows producer payloads; the public edit port first detaches them
  // into the construction arena, which the real initializer will retire.
  ASSERT_EQ(OB_SUCCESS, plan().edit_parameter_inputs([](ParamStore &) { return OB_SUCCESS; }));
  const void *input_bytes = plan().get_parameter_inputs().at(0).get_datum().get_reference();
  EXPECT_NE(text.get_datum().get_reference(), input_bytes);
  ParamStore resolution((ObWrapperAllocator(&descriptors)));
  ASSERT_EQ(OB_SUCCESS, plan().build_resolution_inputs(resolution, statement_owner));
  ASSERT_EQ(2, resolution.count());
  EXPECT_NE(input_bytes, resolution.at(0).get_datum().get_reference());
  MEMSET(const_cast<void *>(text.get_datum().get_reference()), 0xa5, expected.size());
  original.destroy(); source_owner.reset();
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_bytes(plan()));
  EXPECT_EQ(expected, std::string(read_parameter_value(plan(), 0).get_string().ptr(), expected.size()));
  EXPECT_TRUE(read_parameter_value(plan(), 1).is_null());
  EXPECT_EQ(ObDecimalIntType, plan().get_native_parameter_type(1)->kind());
  EXPECT_EQ(20, plan().get_native_parameter_type(1)->precision());
  EXPECT_EQ(5, plan().get_native_parameter_type(1)->scale());
  plan().reset_datum_param_store();
  EXPECT_EQ(expected, std::string(resolution.at(0).get_string().ptr(), resolution.at(0).get_string().length()));
  EXPECT_EQ('\0', resolution.at(0).get_string().ptr()[1]);
  EXPECT_TRUE(resolution.at(1).is_null());
  expect_resolver_attributes(text_attributes, resolution.at(0).get_metadata());
  expect_resolver_attributes(null_attributes, resolution.at(1).get_metadata());
  EXPECT_EQ(ObDecimalIntType, resolution.at(1).get_param_meta().get_type());
  EXPECT_EQ(5, resolution.at(1).get_accuracy().get_scale());
}

TEST_F(NativeExecutionParameters, construction_resolver_array_keeps_real_leaf_types_and_merges_only_flags_in_both_states)
{
  ObArenaAllocator source_owner("ResolveSrc"), descriptors("ResolveDesc"), statement_owner("ResolveStmt");
  auto *array = DatumParameterArray::alloc(source_owner, 3); ASSERT_NE(nullptr, array);
  ASSERT_EQ(OB_SUCCESS, array->data_[0].set_int(INT64_MIN, source_owner));
  array->data_[0].set_accuracy(ObAccuracy(19, 0)); array->data_[0].set_param_meta();
  const std::string expected("a\0b", 3);
  ASSERT_EQ(OB_SUCCESS, array->data_[1].set_varchar(ObString(expected.size(), expected.data()), source_owner));
  array->data_[1].set_collation_type(CS_TYPE_UTF8MB4_BIN); array->data_[1].set_length(3);
  array->data_[1].set_param_meta();
  const int64_t coefficient = 12345; DatumValue decimal;
  ASSERT_EQ(OB_SUCCESS, decimal.set_decimal_int(8, 5,
      reinterpret_cast<const ObDecimalInt *>(&coefficient), source_owner));
  array->data_[2].assign_value(decimal); array->data_[2].set_accuracy(ObAccuracy(20, 5));
  array->data_[2].set_param_meta(); array->data_[2].set_null();
  for (int64_t i = 0; i < 3; ++i) {
    const DatumParameter &cell = array->data_[i]; const SqlType *type = nullptr;
    ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(cell.is_null() ? cell.get_param_meta() : cell.get_type_meta(),
        cell.get_accuracy(), source_owner, type));
    ASSERT_EQ(OB_SUCCESS, array->set_element_type_borrowed(i, *type, source_owner));
    array->data_[i].set_raw_text_info(31 + i, 43 + i);
    array->data_[i].set_result_flags(0x2100 + i);
    array->data_[i].get_param_flag().flag_ = 0x40 + i;
    array->data_[i].get_param_flag().extend_flag_ = 0x80 + i;
  }
  DatumParameterMetadata leaf_attributes[3];
  for (int64_t i = 0; i < 3; ++i) { leaf_attributes[i] = array->data_[i].get_metadata(); }
  array->element_.set_obj_type(ObIntType); array->element_.set_accuracy(ObAccuracy(19, 0));
  array->source_value_size_ = 177;
  DatumParameter envelope;
  ASSERT_EQ(OB_SUCCESS, envelope.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, source_owner));
  envelope.set_param_meta(); envelope.set_raw_text_info(17, 23); envelope.set_result_flags(0x1234);
  ParamStore original((ObWrapperAllocator(&source_owner))); ASSERT_EQ(OB_SUCCESS, original.push_back(envelope));
  ASSERT_EQ(OB_SUCCESS, plan().assign_parameter_inputs(original)); plan().set_original_param_cnt(1);
  // Preserve the borrowed-input contract before poisoning the producer arena;
  // edit clones the physical array and its actual per-leaf Type directory.
  ASSERT_EQ(OB_SUCCESS, plan().edit_parameter_inputs([](ParamStore &) { return OB_SUCCESS; }));
  const auto *input = plan().get_parameter_inputs().at(0).get_array<DatumParameterArray>();
  ASSERT_NE(nullptr, input);
  ParamStore resolution((ObWrapperAllocator(&descriptors)));
  ASSERT_EQ(OB_SUCCESS, plan().build_resolution_inputs(resolution, statement_owner));
  ASSERT_EQ(1, resolution.count());
  const auto *retained = resolution.at(0).get_array<DatumParameterArray>(); ASSERT_NE(nullptr, retained);
  EXPECT_NE(input, retained); EXPECT_EQ(177, retained->source_value_size_);
  for (int64_t i = 0; i < 3; ++i) {
    ASSERT_NE(nullptr, retained->get_element_type(i));
    EXPECT_NE(input->get_element_type(i), retained->get_element_type(i));
    EXPECT_TRUE(input->get_element_type(i)->equals(*retained->get_element_type(i)));
  }
  const DatumParameterMetadata original_attributes = plan().get_parameter_inputs().at(0).get_metadata();
  resolution.at(0).get_param_flag().flag_ = 0x51; resolution.at(0).get_param_flag().extend_flag_ = 0x80;
  resolution.at(0).set_raw_text_info(901, 902); resolution.at(0).set_result_flags(0x4321);
  ParamStore wrong_count((ObWrapperAllocator(&descriptors)));
  EXPECT_EQ(OB_STATE_NOT_MATCH, plan().merge_resolution_parameter_flags(wrong_count));
  expect_resolver_attributes(original_attributes, plan().get_parameter_inputs().at(0).get_metadata());
  ASSERT_EQ(OB_SUCCESS, plan().merge_resolution_parameter_flags(resolution));
  DatumParameterMetadata flags_only = original_attributes;
  flags_only.flag_ = resolution.at(0).get_param_flag();
  expect_resolver_attributes(flags_only, plan().get_parameter_inputs().at(0).get_metadata());
  EXPECT_EQ(INT64_MIN, input->data_[0].get_int());
  MEMSET(const_cast<void *>(array->data_[1].get_datum().get_reference()), 0xa5, expected.size());
  original.destroy(); source_owner.reset();
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_bytes(plan()));
  const auto *runtime_type = plan().get_native_parameter_type(0);
  ASSERT_NE(nullptr, runtime_type); EXPECT_EQ(ObIntType, runtime_type->kind());
  const DatumParameterMetadata before = *plan().get_parameter_metadata(0);
  EXPECT_EQ(OB_STATE_NOT_MATCH, plan().merge_resolution_parameter_flags(wrong_count));
  expect_resolver_attributes(before, *plan().get_parameter_metadata(0));
  resolution.at(0).get_param_flag().flag_ = 0x42; resolution.at(0).get_param_flag().extend_flag_ = 0x81;
  ASSERT_EQ(OB_SUCCESS, plan().merge_resolution_parameter_flags(resolution));
  flags_only = before; flags_only.flag_ = resolution.at(0).get_param_flag();
  expect_resolver_attributes(flags_only, *plan().get_parameter_metadata(0));
  EXPECT_EQ(runtime_type, plan().get_native_parameter_type(0));
  EXPECT_EQ(INT64_MIN, read_parameter_value(plan(), 0).get_int());
  EXPECT_EQ(INT64_MIN, retained->data_[0].get_int());
  EXPECT_EQ(expected, std::string(retained->data_[1].get_string().ptr(), expected.size()));
  EXPECT_TRUE(retained->data_[2].is_null());
  for (int64_t i = 0; i < 3; ++i) {
    expect_resolver_attributes(leaf_attributes[i], retained->data_[i].get_metadata());
  }
  EXPECT_EQ(ObDecimalIntType, retained->get_element_type(2)->kind());
  EXPECT_EQ(20, retained->get_element_type(2)->precision()); EXPECT_EQ(5, retained->get_element_type(2)->scale());
}

TEST_F(NativeExecutionParameters, resolver_snapshot_failures_preserve_inputs_and_pl_lvalue_identity)
{
  ObArenaAllocator source_owner("ResolveSrc"), descriptors("ResolveDesc"), statement_owner("ResolveStmt");
  ParamStore original((ObWrapperAllocator(&source_owner)));
  DatumParameter integer; ASSERT_EQ(OB_SUCCESS, integer.set_int(47, source_owner));
  integer.set_accuracy(ObAccuracy(19, 0)); integer.set_param_meta();
  ASSERT_EQ(OB_SUCCESS, original.push_back(integer));
  const std::string expected(409, 'f'); DatumParameter text;
  ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString(expected.size(), expected.data()), source_owner));
  text.set_collation_type(CS_TYPE_UTF8MB4_BIN); text.set_length(expected.size()); text.set_param_meta();
  ASSERT_EQ(OB_SUCCESS, original.push_back(text));
  ASSERT_EQ(OB_SUCCESS, plan().assign_parameter_inputs(original)); plan().set_original_param_cnt(2);
  const int64_t input_bytes = NativeExecutionParametersTestPeer::input_bytes(plan());
  RejectResolverDescriptors reject_descriptors;
  ParamStore rejected((ObWrapperAllocator(&reject_descriptors)));
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan().build_resolution_inputs(rejected, statement_owner));
  EXPECT_GT(reject_descriptors.calls_, 0); EXPECT_EQ(0, rejected.count());
  EXPECT_FALSE(plan().native_parameters_ready()); EXPECT_EQ(2, plan().get_parameter_count());
  ExecutionAllocator reject_reference; reject_reference.fail_size_ = expected.size();
  ParamStore resolution((ObWrapperAllocator(&descriptors)));
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan().build_resolution_inputs(resolution, reject_reference));
  EXPECT_EQ(1, reject_reference.calls_for(expected.size())); EXPECT_EQ(0, resolution.count());
  EXPECT_EQ(input_bytes, NativeExecutionParametersTestPeer::input_bytes(plan()));
  EXPECT_EQ(47, plan().get_parameter_inputs().at(0).get_int());
  EXPECT_EQ(expected, std::string(plan().get_parameter_inputs().at(1).get_string().ptr(), expected.size()));
  expect_resolver_attributes(integer.get_metadata(), plan().get_parameter_inputs().at(0).get_metadata());
  expect_resolver_attributes(text.get_metadata(), plan().get_parameter_inputs().at(1).get_metadata());
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  const SqlType *old_type = plan().get_native_parameter_type(1);
  const DatumParameterMetadata old_attributes = *plan().get_parameter_metadata(1);
  reject_reference.clear_calls();
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan().build_resolution_inputs(resolution, reject_reference));
  EXPECT_EQ(1, reject_reference.calls_for(expected.size())); EXPECT_EQ(0, resolution.count());
  EXPECT_TRUE(plan().native_parameters_ready()); EXPECT_EQ(old_type, plan().get_native_parameter_type(1));
  expect_resolver_attributes(old_attributes, *plan().get_parameter_metadata(1));
  EXPECT_EQ(expected, std::string(read_parameter_value(plan(), 1).get_string().ptr(), expected.size()));
  ASSERT_EQ(OB_SUCCESS, plan().build_resolution_inputs(resolution, statement_owner));
  ASSERT_EQ(2, resolution.count()); EXPECT_EQ(47, resolution.at(0).get_int());
  resolution.reuse(); plan().reset_datum_param_store();

  auto *array = DatumParameterArray::alloc(source_owner, 1); ASSERT_NE(nullptr, array);
  array->data_[0] = integer;
  const SqlType *leaf_type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(integer.get_type_meta(), integer.get_accuracy(), source_owner, leaf_type));
  ASSERT_EQ(OB_SUCCESS, array->set_element_type_borrowed(0, *leaf_type, source_owner));
  DatumParameter group;
  ASSERT_EQ(OB_SUCCESS, group.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, source_owner));
  group.set_param_meta();
  ASSERT_EQ(OB_SUCCESS, plan().append_parameter_input(group));
  ExecutionAllocator reject_type; reject_type.fail_size_ = sizeof(SqlType);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan().build_resolution_inputs(resolution, reject_type));
  EXPECT_GT(reject_type.calls_for(sizeof(SqlType)), 0); EXPECT_EQ(0, resolution.count());
  EXPECT_FALSE(plan().native_parameters_ready()); EXPECT_EQ(1, plan().get_parameter_count());
  EXPECT_EQ(array, plan().get_parameter_inputs().at(0).get_array<DatumParameterArray>());
  EXPECT_EQ(leaf_type, array->get_element_type(0)); EXPECT_EQ(47, array->data_[0].get_int());
  ASSERT_EQ(OB_SUCCESS, plan().build_resolution_inputs(resolution, statement_owner));
  ASSERT_EQ(1, resolution.count());
  const auto *retry = resolution.at(0).get_array<DatumParameterArray>(); ASSERT_NE(nullptr, retry);
  EXPECT_NE(leaf_type, retry->get_element_type(0)); EXPECT_EQ(47, retry->data_[0].get_int());
  resolution.reuse(); plan().reset_datum_param_store();

  // This is the existing physical PL lvalue domain, without a fabricated
  // FullType or a claim of pointee retention across its producer's lifetime.
  const uint64_t record_id = (uint64_t(1) << 50) | 17;
  void *record_memory = source_owner.alloc(oceanbase::pl::ObRecordType::get_init_size(1));
  ASSERT_NE(nullptr, record_memory);
  auto *record = new (record_memory) oceanbase::pl::ObPLRecord(record_id, 1);
  ASSERT_EQ(OB_SUCCESS, record->init_data(source_owner, false));
  record->get_element()[0] = integer.value_view();
  record->get_element_type()[0].set_meta_type(integer.get_type_meta());
  record->get_element_type()[0].set_accuracy(integer.get_accuracy());
  DatumParameter lvalue;
  ASSERT_EQ(OB_SUCCESS, lvalue.set_extend(reinterpret_cast<int64_t>(record),
      oceanbase::pl::PL_RECORD_TYPE, source_owner, record->get_init_size()));
  lvalue.set_udt_id(record_id); lvalue.set_param_meta();
  lvalue.set_raw_text_info(17, 23); lvalue.set_result_flags(0x1234);
  ASSERT_EQ(OB_SUCCESS, plan().append_parameter_input(lvalue));
  ExecutionAllocator wrapper_reject;
  wrapper_reject.fail_size_ = DATUM_EXTEND_PAYLOAD_SIZE;
  ASSERT_EQ(OB_SUCCESS, plan().build_resolution_inputs(resolution, wrapper_reject));
  ASSERT_EQ(1, resolution.count());
  // A carrier snapshot now copies inline bits, while this PL lvalue's pointee
  // still belongs to source_owner. No second 15-byte envelope is required.
  EXPECT_TRUE(lvalue.datum_.is_inline_extended());
  EXPECT_TRUE(resolution.at(0).datum_.is_inline_extended());
  EXPECT_EQ(0, wrapper_reject.calls_for(DATUM_EXTEND_PAYLOAD_SIZE));
  EXPECT_EQ(reinterpret_cast<int64_t>(record), resolution.at(0).get_ext());
  expect_resolver_attributes(lvalue.get_metadata(), resolution.at(0).get_metadata());
  plan().reset_datum_param_store();
  ASSERT_EQ(OB_SUCCESS, record->get_element()[0].set_int(81, source_owner));
  EXPECT_EQ(record_id, resolution.at(0).get_udt_id());
  EXPECT_EQ(81, reinterpret_cast<oceanbase::pl::ObPLRecord *>(resolution.at(0).get_ext())->get_element()[0].get_int());
}

namespace
{
namespace lib = oceanbase::lib;
using oceanbase::pl::ObPLExecCtx;
using oceanbase::pl::ObPLFunction;
using oceanbase::pl::ObPLDataType;
using oceanbase::pl::ObPLSymbolTable;

// The existing private-access pattern invokes the actual SPI implementation;
// no production API, copied selection algorithm, or fake expression is needed.
struct SpiResultTypeTag {
  using type = int (*)(ObPLExecCtx &, const ObSqlExpression &, ObExprResType &);
  friend type access(SpiResultTypeTag);
};
template <typename Tag, typename Tag::type Method> struct SpiPrivateAccess {
  friend typename Tag::type access(Tag) { return Method; }
};
template struct SpiPrivateAccess<SpiResultTypeTag, &ObSPIService::get_result_type>;

struct SpiUnitContext {
  lib::MemoryContext context_ = nullptr;
  ~SpiUnitContext() { if (context_ != nullptr) { DESTROY_CONTEXT(context_); } }
};

TEST(NativeExecutionParametersSPI, referenced_question_index_preserves_declaration_and_rejects_invalid_indices)
{
  ObArenaAllocator owner("SpiQmTest");
  SpiUnitContext scope;
  lib::ContextParam context_param;
  context_param.set_mem_attr("SpiQmTest").set_properties(lib::USE_TL_PAGE_OPTIONAL);
  ASSERT_EQ(OB_SUCCESS, CURRENT_CONTEXT->CREATE_CONTEXT(scope.context_, context_param));
  ObPLFunction function(scope.context_);
  ObPLSymbolTable symbols(owner);
  ObPLDataType declared_integer(ObIntType);
  declared_integer.get_data_type()->set_accuracy(ObAccuracy(19, 0));
  ObPLDataType declared_text(ObVarcharType);
  declared_text.get_data_type()->set_collation_type(CS_TYPE_BINARY);
  declared_text.get_data_type()->set_length(16);
  declared_text.get_data_type()->set_length_semantics(LS_BYTE);
  declared_text.get_data_type()->set_scale(0);
  ASSERT_EQ(OB_SUCCESS, symbols.add_symbol(ObString::make_string("source_id"), declared_integer));
  ASSERT_EQ(OB_SUCCESS, symbols.add_symbol(ObString::make_string("local_b"), declared_text));
  ASSERT_EQ(OB_SUCCESS, function.set_variables(symbols));

  ParamStore params((ObWrapperAllocator(&owner)));
  DatumParameter integer, null_text, fallback;
  ASSERT_EQ(OB_SUCCESS, integer.set_int(INT64_MIN, owner));
  integer.set_accuracy(ObAccuracy(19, 0)); integer.set_param_meta();
  null_text.set_null();
  ASSERT_EQ(OB_SUCCESS, fallback.set_uint64(UINT64_MAX, owner));
  fallback.set_accuracy(ObAccuracy(20, 0)); fallback.set_param_meta();
  ASSERT_EQ(OB_SUCCESS, params.push_back(integer));
  ASSERT_EQ(OB_SUCCESS, params.push_back(null_text));
  ASSERT_EQ(OB_SUCCESS, params.push_back(fallback));
  ObPLExecCtx context(&owner, nullptr, &params, nullptr, nullptr, &function);

  const int64_t indices[] = {0, 1, 2, -1, 3};
  for (int64_t index : indices) {
    SCOPED_TRACE(index);
    ObArenaAllocator source("SpiQmSource");
    DatumValue question;
    ASSERT_EQ(OB_SUCCESS, question.set_unknown(index, source));
    ASSERT_EQ(ObUnknownType, question.get_type());
    ASSERT_EQ(8, question.get_datum().length());
    ObPostExprItem item;
    ASSERT_EQ(OB_SUCCESS, item.assign(question));
    ASSERT_EQ(T_QUESTIONMARK, item.get_item_type());
    ObSqlExpression expression(owner);
    ASSERT_EQ(OB_SUCCESS, expression.set_item_count(1));
    ASSERT_EQ(OB_SUCCESS, expression.add_expr_item(item));
    const ObPostExprItem &retained = expression.get_expr_items().at(0);
    ASSERT_NE(question.get_datum().get_reference(), retained.get_obj().get_datum().get_reference());
    MEMSET(const_cast<void *>(question.get_datum().get_reference()), 0xA5, sizeof(index));
    source.reset();
    char printed[256] = {};
    const int64_t length = retained.to_string(printed, sizeof(printed));
    EXPECT_NE(std::string::npos, std::string(printed, length).find("param:" + std::to_string(index) + ","));

    ObExprResType actual;
    actual.set_type(ObDoubleType); actual.set_accuracy(ObAccuracy(22, 7));
    const ObExprResType before = actual;
    const int ret = access(SpiResultTypeTag{})(context, expression, actual);
    if (index < 0 || index >= params.count()) {
      EXPECT_EQ(OB_ERR_UNEXPECTED, ret);
      EXPECT_EQ(static_cast<const SqlTypeMeta &>(before), static_cast<const SqlTypeMeta &>(actual));
      EXPECT_EQ(before.get_accuracy(), actual.get_accuracy());
    } else {
      ASSERT_EQ(OB_SUCCESS, ret);
      const SqlTypeMeta &expected_meta = index < 2
          ? function.get_variables().at(index).get_data_type()->get_meta_type() : fallback.get_meta();
      const ObAccuracy &expected_accuracy = index < 2
          ? function.get_variables().at(index).get_data_type()->get_accuracy() : fallback.get_accuracy();
      EXPECT_EQ(expected_meta, static_cast<const SqlTypeMeta &>(actual));
      EXPECT_EQ(expected_accuracy, actual.get_accuracy());
      if (index == 1) { EXPECT_EQ(ObVarcharType, actual.get_type()); EXPECT_EQ(16, actual.get_length()); }
    }
  }

  // A malformed QM item must propagate the checked getter's type error and
  // leave the caller's output intact, rather than interpret an integer as a pointer.
  DatumValue wrong_kind;
  ASSERT_EQ(OB_SUCCESS, wrong_kind.set_int(0, owner));
  ObPostExprItem malformed_item;
  ASSERT_EQ(OB_SUCCESS, malformed_item.assign(wrong_kind));
  malformed_item.set_item_type(T_QUESTIONMARK);
  ObSqlExpression malformed(owner);
  ASSERT_EQ(OB_SUCCESS, malformed.set_item_count(1));
  ASSERT_EQ(OB_SUCCESS, malformed.add_expr_item(malformed_item));
  ObExprResType output;
  output.set_type(ObDoubleType); output.set_accuracy(ObAccuracy(22, 7));
  const ObExprResType before = output;
  EXPECT_EQ(OB_OBJ_TYPE_ERROR, access(SpiResultTypeTag{})(context, malformed, output));
  EXPECT_EQ(static_cast<const SqlTypeMeta &>(before), static_cast<const SqlTypeMeta &>(output));
  EXPECT_EQ(before.get_accuracy(), output.get_accuracy());
}
} // namespace


namespace
{
// Invoke the production preparation stages through the existing SPI test's
// explicit-instantiation access pattern; no production friend or public hook.
struct LookupParameterPreparationTag {
  using type = int (ObPlanCacheValue::*)(ObPlanCacheCtx &);
  friend type access(LookupParameterPreparationTag);
};
template struct SpiPrivateAccess<LookupParameterPreparationTag,
    &ObPlanCacheValue::prepare_lookup_parameters>;
struct ProtocolFirstRowTag {
  using type = int (ObSql::*)(ObIAllocator &, const ParamStore &, ParamStore *&);
  friend type access(ProtocolFirstRowTag);
};
template struct SpiPrivateAccess<ProtocolFirstRowTag, &ObSql::init_execute_params_for_ab>;
struct FixedProtocolInputsTag {
  using type = int (ObSql::*)(const ParamStore &, const ParamStore &,
      const ObIArray<int64_t> &, ParamStore &);
  friend type access(FixedProtocolInputsTag);
};
template struct SpiPrivateAccess<FixedProtocolInputsTag, &ObSql::construct_ps_param_store>;
struct DirectProtocolInputsTag {
  using type = int (ObSql::*)(const ParamStore &, ObPhysicalPlanCtx &);
  friend type access(DirectProtocolInputsTag);
};
template struct SpiPrivateAccess<DirectProtocolInputsTag, &ObSql::construct_param_store>;
struct ParserProtocolInputsTag {
  using type = int (ObSql::*)(const ObPlanCacheCtx &, ParamStore &);
  friend type access(ParserProtocolInputsTag);
};
template struct SpiPrivateAccess<ParserProtocolInputsTag,
    &ObSql::construct_param_store_from_parameterized_params>;


int make_protocol_array_inputs(ObIAllocator &payload_owner, ParamStore &groups)
{
  const char first_text[] = {'n', '\0', 'u', 'l'};
  const char later_text[] = "later-row";
  int ret = OB_SUCCESS;
  for (int64_t column = 0; ret == OB_SUCCESS && column < 3; ++column) {
    auto *array = DatumParameterArray::alloc(payload_owner, 2);
    if (array == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    for (int64_t row = 0; ret == OB_SUCCESS && row < 2; ++row) {
      DatumParameter &leaf = array->data_[row];
      if (column == 0) {
        ret = leaf.set_int(row == 0 ? INT64_MIN : 7, payload_owner);
        leaf.set_accuracy(ObAccuracy(19, 0)); leaf.set_param_meta();
      } else if (column == 1) {
        ret = leaf.set_varchar(row == 0 ? ObString(4, first_text) : ObString::make_string(later_text), payload_owner);
        leaf.set_collation_type(CS_TYPE_BINARY); leaf.set_collation_level(CS_LEVEL_COERCIBLE);
        leaf.set_length(row == 0 ? 4 : 9); leaf.set_length_semantics(LS_BYTE); leaf.set_param_meta();
      } else {
        const int64_t coefficient = row == 0 ? 12345 : 67890;
        DatumValue decimal;
        ret = decimal.set_decimal_int(8, 5, reinterpret_cast<const ObDecimalInt *>(&coefficient), payload_owner);
        if (ret == OB_SUCCESS) { leaf.assign_value(decimal); }
        leaf.set_accuracy(ObAccuracy(20, 5)); leaf.set_param_meta();
        if (row == 0) { leaf.set_null(); }
      }
      leaf.set_result_flags(0x1200 + column);
      leaf.set_raw_text_info(17 + column, 23 + row);
      leaf.get_param_flag().flag_ = 0x09;
      leaf.get_param_flag().reserved_legacy_bit_ = (column & 1) != 0;
      leaf.get_param_flag().extend_flag_ = 0x80 + row;
      const SqlType *declared = nullptr;
      if (ret == OB_SUCCESS) {
        ret = SqlType::from_legacy(leaf.is_null() ? leaf.get_param_meta() : leaf.get_type_meta(),
            leaf.get_accuracy(), payload_owner, declared);
      }
      if (ret == OB_SUCCESS) { ret = array->set_element_type_borrowed(row, *declared, payload_owner); }
    }
    if (ret != OB_SUCCESS) { return ret; }
    array->element_.set_meta_type(array->data_[0].get_param_meta());
    array->element_.set_accuracy(array->data_[0].get_accuracy());
    DatumParameter envelope;
    ret = envelope.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, payload_owner);
    envelope.set_param_meta();
    if (ret == OB_SUCCESS) { ret = groups.push_back(envelope); }
  }
  return ret;
}

TEST_F(NativeExecutionParameters, ordinary_lookup_and_miss_parser_consume_original_inputs_without_ready_shadow)
{
  ASSERT_EQ(OB_SUCCESS, oceanbase::share::ObSysVariables::init_default_values());
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ResolverInputSessionGuard registration(session, exec_);
  ObArenaAllocator descriptors("ProtoDesc"), statement_owner("ProtoStmt"), raw_owner("ProtoRaw");
  ParamStore original((ObWrapperAllocator(&descriptors)));
  const double keys[] = {1.0, 1.0, 7.0};
  for (int64_t i = 0; i < 3; ++i) {
    DatumParameter input;
    ASSERT_EQ(OB_SUCCESS, input.set_double(keys[i], request_arena_));
    input.set_accuracy(ObAccuracy(15, 7)); input.set_param_meta();
    input.set_raw_text_info(17 + i, 23 + i); input.set_result_flags(0x1200 + i);
    input.get_param_flag().flag_ = 0x09; input.get_param_flag().reserved_legacy_bit_ = (i & 1) != 0;
    input.get_param_flag().extend_flag_ = 0x80 + i;
    ASSERT_EQ(OB_SUCCESS, original.push_back(input));
  }
  plan().set_original_param_cnt(3);
  ObPlanCacheCtx lookup(ObString("SELECT ?+?+?"), PC_PS_MODE, owner_, sql_ctx_, exec_);
  lookup.fp_result_.parameter_context_ = &plan();
  ASSERT_EQ(OB_SUCCESS, ObSql::construct_parameterized_params(original, lookup));
  EXPECT_EQ(0, plan().get_parameter_count());
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_bytes(plan()));
  ObPlanCacheValue cache_value;
  ASSERT_EQ(OB_SUCCESS, (cache_value.*access(LookupParameterPreparationTag{}))(lookup));
  ASSERT_TRUE(plan().native_parameters_ready());
  for (int64_t i = 0; i < 3; ++i) {
    EXPECT_DOUBLE_EQ(keys[i], read_parameter_value(plan(), i).get_double());
    EXPECT_EQ(ObDoubleType, plan().get_native_parameter_type(i)->kind());
    expect_resolver_attributes(original.at(i).get_metadata(), *plan().get_parameter_metadata(i));
  }
  Datum converted; converted.set_double(999.0);
  const DatumParameterMetadata attributes = *plan().get_parameter_metadata(2);
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(2, converted, *plan().get_native_parameter_type(2), attributes));
  EXPECT_DOUBLE_EQ(999.0, read_parameter_value(plan(), 2).get_double());
  EXPECT_DOUBLE_EQ(7.0, original.at(2).get_double());

  // The actual miss parser resets the attempted binding and invokes this
  // production constructor from the original pointer directory, not the frame.
  plan().reset_datum_param_store();
  ObSql sql;
  ASSERT_EQ(OB_SUCCESS, plan().edit_parameter_inputs([&](ParamStore &inputs) {
    return (sql.*access(ParserProtocolInputsTag{}))(lookup, inputs);
  }));
  ASSERT_FALSE(plan().native_parameters_ready()); ASSERT_EQ(3, plan().get_parameter_count());
  ParamStore resolution((ObWrapperAllocator(&descriptors)));
  ASSERT_EQ(OB_SUCCESS, plan().build_resolution_inputs(resolution, statement_owner));
  ObRawExprFactory factory(raw_owner);
  for (int64_t i = 0; i < 3; ++i) {
    ObRawExpr *resolved = nullptr;
    ASSERT_EQ(OB_SUCCESS, resolve_snapshot_questionmark(factory, session, resolution, i, resolved));
    ASSERT_NE(nullptr, resolved); ASSERT_TRUE(resolved->is_const_raw_expr());
    const auto &constant = static_cast<const ObConstRawExpr &>(*resolved);
    EXPECT_EQ(ObDoubleType, constant.get_data_type());
    EXPECT_DOUBLE_EQ(keys[i], constant.get_bound_param_datum().get_double());
    expect_resolver_attributes(original.at(i).get_metadata(), resolution.at(i).get_metadata());
  }
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  EXPECT_DOUBLE_EQ(7.0, read_parameter_value(plan(), 2).get_double());
}

TEST_F(NativeExecutionParameters, protocol_batch_lookup_consumes_actual_first_row_and_fixed_slots_from_source)
{
  ASSERT_EQ(OB_SUCCESS, oceanbase::share::ObSysVariables::init_default_values());
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ResolverInputSessionGuard registration(session, exec_);
  ObArenaAllocator payload("ProtoABSrc"), descriptors("ProtoABDesc");
  ParamStore groups((ObWrapperAllocator(&descriptors)));
  ASSERT_EQ(OB_SUCCESS, make_protocol_array_inputs(payload, groups));
  ObSql sql;
  ParamStore *first_row = nullptr;
  ASSERT_EQ(OB_SUCCESS, (sql.*access(ProtocolFirstRowTag{}))(descriptors, groups, first_row));
  ASSERT_NE(nullptr, first_row); ASSERT_EQ(3, first_row->count());
  EXPECT_EQ(INT64_MIN, first_row->at(0).get_int());
  ASSERT_TRUE(first_row->at(2).is_null());
  EXPECT_EQ(ObDecimalIntType, first_row->at(2).get_param_meta().get_type());
  EXPECT_EQ(5, first_row->at(2).get_scale());

  ParamStore fixed((ObWrapperAllocator(&descriptors))), inputs((ObWrapperAllocator(&descriptors))),
      full_groups((ObWrapperAllocator(&descriptors)));
  DatumParameter literal;
  ASSERT_EQ(OB_SUCCESS, literal.set_int(99, payload));
  literal.set_accuracy(ObAccuracy(19, 0)); literal.set_param_meta();
  literal.set_result_flags(0x2340); literal.set_raw_text_info(71, 73);
  ASSERT_EQ(OB_SUCCESS, fixed.push_back(literal));
  ObSEArray<int64_t, 1> fixed_indices;
  ASSERT_EQ(OB_SUCCESS, fixed_indices.push_back(1));
  ASSERT_EQ(OB_SUCCESS, (sql.*access(FixedProtocolInputsTag{}))(*first_row, fixed, fixed_indices, inputs));
  ASSERT_EQ(OB_SUCCESS, (sql.*access(FixedProtocolInputsTag{}))(groups, fixed, fixed_indices, full_groups));
  ASSERT_EQ(4, inputs.count());
  DatumParameterMetadata expected[4];
  for (int64_t i = 0; i < 4; ++i) { expected[i] = inputs.at(i).get_metadata(); }

  sql_ctx_.multi_stmt_item_.set_ps_mode(true);
  sql_ctx_.multi_stmt_item_.set_ab_cnt(2);
  ASSERT_TRUE(sql_ctx_.is_batch_params_execute());
  ASSERT_TRUE(sql_ctx_.multi_stmt_item_.is_ab_batch_opt());
  plan().set_original_param_cnt(3); plan().set_orig_question_mark_cnt(3);
  plan().set_ps_fixed_array_index(fixed_indices);
  ObPlanCacheCtx lookup(ObString("SELECT ?,99,?,?"), PC_PS_MODE, owner_, sql_ctx_, exec_);
  lookup.fp_result_.parameter_context_ = &plan();
  lookup.ab_params_ = &full_groups;
  ASSERT_EQ(OB_SUCCESS, ObSql::construct_parameterized_params(inputs, lookup));
  ASSERT_FALSE(plan().native_parameters_ready());
  EXPECT_EQ(0, plan().get_parameter_count());
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_bytes(plan()));
  ObPlanCacheValue cache_value;
  ASSERT_EQ(OB_SUCCESS, (cache_value.*access(LookupParameterPreparationTag{}))(lookup));
  ASSERT_TRUE(plan().native_parameters_ready());
  ASSERT_EQ(4, plan().get_parameter_count());
  EXPECT_EQ(4, plan().get_original_param_cnt()); EXPECT_EQ(3, plan().get_orig_question_mark_cnt());
  EXPECT_EQ(&fixed_indices, plan().get_ps_fixed_array_index());
  EXPECT_EQ(&full_groups, lookup.ab_params_);
  EXPECT_EQ(INT64_MIN, read_parameter_value(plan(), 0).get_int());
  EXPECT_EQ(99, read_parameter_value(plan(), 1).get_int());
  EXPECT_EQ(std::string("n\0ul", 4), std::string(read_parameter_value(plan(), 2).get_string().ptr(), 4));
  EXPECT_TRUE(read_parameter_value(plan(), 3).is_null());
  EXPECT_EQ(ObDecimalIntType, plan().get_native_parameter_type(3)->kind());
  EXPECT_EQ(20, plan().get_native_parameter_type(3)->precision());
  EXPECT_EQ(5, plan().get_native_parameter_type(3)->scale());
  EXPECT_EQ(0, plan().get_native_group_count()); // First row, not final full-group publication.
  for (int64_t i = 0; i < 4; ++i) {
    expect_resolver_attributes(expected[i], *plan().get_parameter_metadata(i));
    EXPECT_EQ(&inputs.at(i), lookup.fp_result_.parameterized_params_.at(i));
  }
  for (int64_t i : {0, 2, 3}) { EXPECT_TRUE(plan().get_parameter_metadata(i)->flag_.is_batch_parameter_); }
  EXPECT_FALSE(plan().get_parameter_metadata(1)->flag_.is_batch_parameter_);
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_count(plan()));
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_bytes(plan()));
  const auto *original_text_group = full_groups.at(2).get_array<DatumParameterArray>();
  ASSERT_NE(nullptr, original_text_group); ASSERT_NE(nullptr, original_text_group->get_element_type(0));
  EXPECT_EQ(ObVarcharType, original_text_group->get_element_type(0)->kind());
  EXPECT_EQ(9, original_text_group->data_[1].get_string().length());
  // The real full AB source remains live for later all-group matching. The
  // first-row native text owns an independent span already at this boundary.
  EXPECT_NE(inputs.at(2).get_datum().get_reference(), read_parameter_value(plan(), 2).get_reference());
  first_row->~ParamStore();
}

TEST_F(NativeExecutionParameters, protocol_first_row_reference_failure_keeps_source_for_retry_without_ready_publication)
{
  ASSERT_EQ(OB_SUCCESS, oceanbase::share::ObSysVariables::init_default_values());
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ResolverInputSessionGuard registration(session, exec_);
  ObArenaAllocator payload("ProtoABSrc"), descriptors("ProtoABDesc");
  ParamStore groups((ObWrapperAllocator(&descriptors)));
  ASSERT_EQ(OB_SUCCESS, make_protocol_array_inputs(payload, groups));
  ObSql sql; ParamStore *first_row = nullptr;
  ASSERT_EQ(OB_SUCCESS, (sql.*access(ProtocolFirstRowTag{}))(descriptors, groups, first_row));
  ASSERT_NE(nullptr, first_row);
  sql_ctx_.multi_stmt_item_.set_ps_mode(true); sql_ctx_.multi_stmt_item_.set_ab_cnt(2);
  ObPlanCacheCtx lookup(ObString("SELECT ?,?,?"), PC_PS_MODE, owner_, sql_ctx_, exec_);
  lookup.fp_result_.parameter_context_ = &plan(); lookup.ab_params_ = &groups;
  ASSERT_EQ(OB_SUCCESS, ObSql::construct_parameterized_params(*first_row, lookup));
  DatumParameterMetadata expected[3];
  for (int64_t i = 0; i < 3; ++i) { expected[i] = first_row->at(i).get_metadata(); }
  owner_.clear_calls(); owner_.fail_size_ = 4; // Actual first-row reference clone, not a scalar 8B allocation.
  ObPlanCacheValue cache_value;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, (cache_value.*access(LookupParameterPreparationTag{}))(lookup));
  EXPECT_GT(owner_.calls_for(4), 0);
  EXPECT_FALSE(plan().native_parameters_ready()); EXPECT_EQ(3, plan().get_parameter_count());
  EXPECT_EQ(nullptr, materialized_parameter(plan(), 0));
  for (int64_t i = 0; i < 3; ++i) {
    expect_resolver_attributes(expected[i], plan().get_parameter_inputs().at(i).get_metadata());
    EXPECT_EQ(&first_row->at(i), lookup.fp_result_.parameterized_params_.at(i));
  }
  EXPECT_EQ(std::string("n\0ul", 4), std::string(first_row->at(1).get_string().ptr(), 4));
  owner_.fail_size_ = 0;
  ASSERT_EQ(OB_SUCCESS, (cache_value.*access(LookupParameterPreparationTag{}))(lookup));
  ASSERT_TRUE(plan().native_parameters_ready());
  EXPECT_EQ(INT64_MIN, read_parameter_value(plan(), 0).get_int());
  EXPECT_EQ(std::string("n\0ul", 4), std::string(read_parameter_value(plan(), 1).get_string().ptr(), 4));
  EXPECT_TRUE(read_parameter_value(plan(), 2).is_null());
  for (int64_t i = 0; i < 3; ++i) { expect_resolver_attributes(expected[i], *plan().get_parameter_metadata(i)); }
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_bytes(plan()));
  first_row->~ParamStore();
}

TEST_F(NativeExecutionParameters, direct_non_dml_inputs_reach_real_questionmark_resolver_with_declarations)
{
  ASSERT_EQ(OB_SUCCESS, oceanbase::share::ObSysVariables::init_default_values());
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  session.set_stmt_type(stmt::T_CALL_PROCEDURE);
  ResolverInputSessionGuard registration(session, exec_);
  ObArenaAllocator payload("DirectSrc"), descriptors("DirectDesc"), statement_owner("DirectStmt"), raw_owner("DirectRaw");
  ParamStore inputs((ObWrapperAllocator(&descriptors)));
  DatumParameter text, decimal_null;
  const char bytes[] = {'d', '\0', 'p', 's'};
  ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString(4, bytes), payload));
  text.set_collation_type(CS_TYPE_BINARY); text.set_collation_level(CS_LEVEL_COERCIBLE);
  text.set_length(4); text.set_length_semantics(LS_BYTE); text.set_param_meta();
  const int64_t coefficient = 12345;
  DatumValue decimal;
  ASSERT_EQ(OB_SUCCESS, decimal.set_decimal_int(8, 5,
      reinterpret_cast<const ObDecimalInt *>(&coefficient), payload));
  decimal_null.assign_value(decimal);
  decimal_null.set_accuracy(ObAccuracy(20, 5)); decimal_null.set_param_meta(); decimal_null.set_null();
  text.set_raw_text_info(17, 23); text.set_result_flags(0x1234); text.get_param_flag().flag_ = 0x19;
  decimal_null.set_raw_text_info(29, 31); decimal_null.set_result_flags(0x2340);
  ASSERT_EQ(OB_SUCCESS, inputs.push_back(text)); ASSERT_EQ(OB_SUCCESS, inputs.push_back(decimal_null));
  ObSql sql;
  plan().set_original_param_cnt(2); plan().set_orig_question_mark_cnt(2);
  // This is the actual direct non-DML construction port before generate_stmt,
  // followed by the same public resolver used by a real direct call argument.
  ASSERT_EQ(OB_SUCCESS, (sql.*access(DirectProtocolInputsTag{}))(inputs, plan()));
  ASSERT_FALSE(plan().native_parameters_ready()); ASSERT_EQ(2, plan().get_parameter_count());
  ParamStore resolution((ObWrapperAllocator(&descriptors)));
  ASSERT_EQ(OB_SUCCESS, plan().build_resolution_inputs(resolution, statement_owner));
  ObRawExprFactory factory(raw_owner);
  for (int64_t i = 0; i < 2; ++i) {
    ObRawExpr *resolved = nullptr;
    ASSERT_EQ(OB_SUCCESS, resolve_snapshot_questionmark(factory, session, resolution, i, resolved));
    ASSERT_NE(nullptr, resolved); ASSERT_TRUE(resolved->is_const_raw_expr());
    const auto &constant = static_cast<const ObConstRawExpr &>(*resolved);
    EXPECT_EQ(i, constant.get_questionmark_index());
    if (i == 0) {
      EXPECT_EQ(ObVarcharType, constant.get_data_type()); EXPECT_EQ(CS_TYPE_BINARY, constant.get_collation_type());
      EXPECT_EQ(std::string("d\0ps", 4), std::string(constant.get_bound_param_datum().get_string().ptr(), 4));
    } else {
      EXPECT_EQ(ObDecimalIntType, constant.get_data_type()); EXPECT_EQ(20, constant.get_result_type().get_precision());
      EXPECT_EQ(5, constant.get_scale()); EXPECT_TRUE(constant.get_bound_param_datum().is_null());
    }
    expect_resolver_attributes(inputs.at(i).get_metadata(), resolution.at(i).get_metadata());
  }
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  MEMSET(const_cast<void *>(text.get_datum().get_reference()), 0xa5, 4);
  inputs.destroy(); payload.reset();
  EXPECT_EQ(std::string("d\0ps", 4), std::string(resolution.at(0).get_string().ptr(), 4));
  EXPECT_EQ(std::string("d\0ps", 4), std::string(read_parameter_value(plan(), 0).get_string().ptr(), 4));
  EXPECT_TRUE(read_parameter_value(plan(), 1).is_null());
  EXPECT_EQ(ObDecimalIntType, plan().get_native_parameter_type(1)->kind());
  EXPECT_EQ(20, plan().get_native_parameter_type(1)->precision()); EXPECT_EQ(5, plan().get_native_parameter_type(1)->scale());
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}

namespace
{
struct PLQuestionMarkBindingGuard
{
  PLQuestionMarkBindingGuard(ObExecContext &execution, ObSQLSessionInfo &session,
                            oceanbase::pl::ObPLExecState &state,
                            oceanbase::pl::ObPLContext &context)
    : execution_(execution), session_(session), old_plan_(execution.get_physical_plan_ctx()),
      old_context_(session.get_pl_context()), old_frames_(execution.get_frames()),
      old_frame_count_(execution.get_frame_cnt())
  {
    execution_.set_physical_plan_ctx(&state.get_physical_plan_ctx());
    session_.set_pl_stack_ctx(&context);
  }
  ~PLQuestionMarkBindingGuard()
  {
    execution_.set_physical_plan_ctx(old_plan_);
    execution_.set_frames(old_frames_);
    execution_.set_frame_cnt(old_frame_count_);
    session_.set_pl_stack_ctx(old_context_);
  }
  ObExecContext &execution_;
  ObSQLSessionInfo &session_;
  ObPhysicalPlanCtx *old_plan_;
  oceanbase::pl::ObPLContext *old_context_;
  char **old_frames_;
  int64_t old_frame_count_;
};
} // namespace

TEST_F(NativeExecutionParameters, live_pl_questionmark_uses_only_matching_private_variable_domain)
{
  using oceanbase::pl::ObPLContext;
  using oceanbase::pl::ObPLExecState;
  using oceanbase::pl::ObPLPackageGuard;
  ASSERT_EQ(OB_SUCCESS, oceanbase::share::ObSysVariables::init_default_values());
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ResolverInputSessionGuard registration(session, exec_);
  ASSERT_NO_FATAL_FAILURE(load({73}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ObPhysicalPlanCtx &sql_parameters = plan();

  SpiUnitContext scope;
  lib::ContextParam context_param;
  context_param.set_mem_attr("PlQmTest").set_properties(lib::USE_TL_PAGE_OPTIONAL);
  ASSERT_EQ(OB_SUCCESS, CURRENT_CONTEXT->CREATE_CONTEXT(scope.context_, context_param));
  ObPLFunction function(scope.context_);
  DatumParameter integer;
  ASSERT_EQ(OB_SUCCESS, integer.set_int(INT64_MIN, owner_));
  integer.set_accuracy(ObAccuracy(19, 0)); integer.set_param_meta();
  const std::string payload("g\0is", 4);
  DatumValue lob_value;
  ASSERT_EQ(OB_SUCCESS, ObTextStringHelper::str_to_lob_storage_obj(
      owner_, ObString(payload.size(), payload.data()), lob_value));
  lob_value.set_type(ObTextType); lob_value.set_collation_type(CS_TYPE_BINARY);
  DatumParameter blob; blob.assign_value(lob_value);
  ObAccuracy blob_accuracy; blob_accuracy.set_full_length(65535, LS_BYTE); blob_accuracy.set_scale(0);
  blob.set_accuracy(blob_accuracy); blob.set_param_meta();
  ParamStore initial_inputs((ObWrapperAllocator(&owner_)));
  ASSERT_EQ(OB_SUCCESS, initial_inputs.push_back(integer));
  ASSERT_EQ(OB_SUCCESS, initial_inputs.push_back(blob));
  ObPLSymbolTable symbols(owner_);
  ObPLDataType declared_integer(ObIntType);
  declared_integer.get_data_type()->set_meta_type(integer.get_meta());
  declared_integer.get_data_type()->set_accuracy(integer.get_accuracy());
  ObPLDataType declared_blob(ObTextType);
  declared_blob.get_data_type()->set_meta_type(blob.get_meta());
  declared_blob.get_data_type()->set_accuracy(blob_accuracy);
  ASSERT_EQ(OB_SUCCESS, symbols.add_symbol(ObString::make_string("n"), declared_integer));
  ASSERT_EQ(OB_SUCCESS, symbols.add_symbol(ObString::make_string("b"), declared_blob));
  ASSERT_EQ(OB_SUCCESS, function.set_variables(symbols));
  function.set_arg_count(2);
  ASSERT_EQ(OB_SUCCESS, function.add_in_arg(0));
  ASSERT_EQ(OB_SUCCESS, function.add_in_arg(1));
  ObPLPackageGuard package_guard;
  ObPLContext pl_context;
  DatumParameter function_result;
  int function_status = OB_SUCCESS;
  ObPLExecState state(owner_, owner_, exec_, package_guard, function, function_result, function_status);
  ASSERT_EQ(OB_SUCCESS, pl_context.get_exec_stack().push_back(&state));
  PLQuestionMarkBindingGuard binding(exec_, session, state, pl_context);
  // The actual PL initializer creates this private, mutable variable directory.
  ASSERT_EQ(OB_SUCCESS, state.init_params(&initial_inputs, false));
  ASSERT_EQ(2, state.get_params().count());
  ASSERT_FALSE(state.get_physical_plan_ctx().native_parameters_ready());
  ASSERT_EQ(nullptr, materialized_parameter(state.get_physical_plan_ctx(), 0));
  EXPECT_EQ(ObTextType, state.get_params().at(1).get_param_meta().get_type());
  EXPECT_NE(blob.get_datum().get_reference(), state.get_params().at(1).get_datum().get_reference());
  EXPECT_TRUE(state.get_params().at(1).has_lob_header());

  alignas(8) char frame[8192] = {};
  char *frames[] = {frame};
  exec_.set_frames(frames); exec_.set_frame_cnt(1);
  new (frame) Datum(); new (frame + 32) ObEvalInfo();
  auto *dynamic = new (frame + 96) ObDynReserveBuf();
  dynamic->magic_ = ObDynReserveBuf::MAGIC_NUM;
  dynamic->len_ = 256; dynamic->mem_ = frame + 128;
  ObEvalCtx evaluation(exec_);
  ObExpr expression;
  expression.type_ = T_QUESTIONMARK; expression.extra_ = 0;
  expression.frame_idx_ = 0; expression.datum_off_ = 0; expression.eval_info_off_ = 32;
  expression.dyn_buf_header_offset_ = 96; expression.res_buf_off_ = 128; expression.res_buf_len_ = 256;
  auto prepare = [&](const SqlTypeMeta &meta, const ObAccuracy &accuracy) -> int {
    expression.obj_meta_ = meta;
    expression.datum_meta_ = ObDatumMeta(meta.get_type(), meta.get_collation_type(), meta.get_scale());
    return expression.prepare_scalar_type(meta, accuracy, owner_);
  };
  ASSERT_EQ(OB_SUCCESS, prepare(integer.get_meta(), integer.get_accuracy()));
  Datum output;
  owner_.clear_calls();
  ASSERT_EQ(OB_SUCCESS, eval_question_mark_func(expression, evaluation, output));
  EXPECT_EQ(INT64_MIN, output.get_int());
  EXPECT_EQ(INT64_MIN, expression.locate_expr_datum(evaluation).get_int());
  EXPECT_EQ(0, output.length());
  EXPECT_TRUE(owner_.sizes_.empty());
  ASSERT_EQ(OB_SUCCESS, integer.set_int(INT64_MAX, owner_));
  ASSERT_EQ(OB_SUCCESS, state.set_var(0, integer));
  ASSERT_EQ(OB_SUCCESS, eval_question_mark_func(expression, evaluation, output));
  EXPECT_EQ(INT64_MAX, output.get_int());
  EXPECT_EQ(INT64_MAX, expression.locate_expr_datum(evaluation).get_int());

  expression.extra_ = 1;
  for (bool with_header : {true, false}) {
    SCOPED_TRACE(with_header);
    SqlTypeMeta result_meta = state.get_params().at(1).get_meta();
    if (!with_header) { result_meta.set_inrow(); }
    ASSERT_EQ(OB_SUCCESS, prepare(result_meta, blob_accuracy));
    ASSERT_EQ(OB_SUCCESS, eval_question_mark_func(expression, evaluation, output));
    EXPECT_NE(state.get_params().at(1).get_datum().get_reference(), output.get_reference());
    EXPECT_EQ(expression.locate_expr_datum(evaluation).get_reference(), output.get_reference());
    const ObString actual = output.get_string();
    if (with_header) {
      const ObString stored = state.get_params().at(1).get_string();
      EXPECT_EQ(std::string(stored.ptr(), stored.length()), std::string(actual.ptr(), actual.length()));
    } else {
      EXPECT_EQ(payload, std::string(actual.ptr(), actual.length()));
    }
  }
  // A later PL assignment cannot invalidate the already published frame copy.
  const ObString retained = output.get_string();
  DatumParameter null_blob; null_blob.set_null(); null_blob.set_param_meta(blob.get_meta());
  ASSERT_EQ(OB_SUCCESS, state.set_var(1, null_blob));
  EXPECT_EQ(payload, std::string(retained.ptr(), retained.length()));
  ASSERT_EQ(OB_SUCCESS, eval_question_mark_func(expression, evaluation, output));
  EXPECT_TRUE(output.is_null());
  EXPECT_TRUE(expression.locate_expr_datum(evaluation).is_null());
  EXPECT_EQ(ObTextType, expression.get_sql_type().kind());
  EXPECT_FALSE(state.get_physical_plan_ctx().native_parameters_ready());

  // Missing/foreign PL domains are not a general construction-input fallback.
  expression.extra_ = 0;
  ASSERT_EQ(OB_SUCCESS, prepare(integer.get_meta(), integer.get_accuracy()));
  for (int64_t index : {-1, 2}) {
    expression.extra_ = index; output.set_int(515);
    EXPECT_EQ(OB_INVALID_ARGUMENT, eval_question_mark_func(expression, evaluation, output));
    EXPECT_EQ(515, output.get_int());
  }
  expression.extra_ = 0;
  ObPhysicalPlanCtx construction(owner_);
  ASSERT_EQ(OB_SUCCESS, construction.append_parameter_input(integer));
  exec_.set_physical_plan_ctx(&construction); output.set_int(515);
  EXPECT_EQ(OB_INVALID_ARGUMENT, eval_question_mark_func(expression, evaluation, output));
  EXPECT_EQ(515, output.get_int());
  exec_.set_physical_plan_ctx(&state.get_physical_plan_ctx());
  session.set_pl_stack_ctx(nullptr);
  EXPECT_EQ(OB_INVALID_ARGUMENT, eval_question_mark_func(expression, evaluation, output));
  session.set_pl_stack_ctx(&pl_context);
  ObExecContext foreign_execution(owner_);
  foreign_execution.set_my_session(&session);
  foreign_execution.set_physical_plan_ctx(&state.get_physical_plan_ctx());
  ObEvalCtx foreign_evaluation(foreign_execution);
  EXPECT_EQ(OB_INVALID_ARGUMENT, eval_question_mark_func(expression, foreign_evaluation, output));
  foreign_execution.set_physical_plan_ctx(nullptr);
  foreign_execution.set_my_session(nullptr);
  EXPECT_EQ(515, output.get_int());

  EXPECT_TRUE(expression.locate_expr_datum(evaluation).is_null());
  SqlTypeMeta wrong_kind = integer.get_meta(); wrong_kind.set_uint64();
  ASSERT_EQ(OB_SUCCESS, prepare(wrong_kind, ObAccuracy(20, 0)));
  EXPECT_EQ(OB_ERR_UNEXPECTED, eval_question_mark_func(expression, evaluation, output));
  EXPECT_EQ(515, output.get_int());
  EXPECT_TRUE(expression.locate_expr_datum(evaluation).is_null());
  ASSERT_EQ(OB_SUCCESS, prepare(integer.get_meta(), integer.get_accuracy()));

  // An active PL stack must not override a real ready SQL binding.
  exec_.set_physical_plan_ctx(&sql_parameters);
  ASSERT_EQ(OB_SUCCESS, eval_question_mark_func(expression, evaluation, output));
  EXPECT_EQ(73, output.get_int());
  EXPECT_TRUE(expression.locate_expr_datum(evaluation).is_null());
  EXPECT_EQ(1, sql_parameters.get_parameter_count());
  EXPECT_EQ(2, state.get_params().count());
}


#include "sql/das/iter/ob_das_domain_id_merge_iter.h"
#include "sql/das/ob_das_scan_op.h"
#include "sql/engine/basic/ob_chunk_datum_store.h"
#include "data_plane/access/ob_table_read_info.h"
#include "lib/container/ob_array_helper.h"

namespace
{
using DomainIdUtils = oceanbase::share::ObDomainIdUtils;
struct DomainSourceCtTag {
  using type = const ObDASScanCtDef *ObDASDomainIdMergeIter::*;
  friend type access(DomainSourceCtTag);
};
struct DomainSourceRtTag {
  using type = ObDASScanRtDef *ObDASDomainIdMergeIter::*;
  friend type access(DomainSourceRtTag);
};
struct DomainReadTag {
  using type = int (ObDASDomainIdMergeIter::*)(const ObDASScanCtDef *, ObDASScanRtDef *,
      ObIAllocator &, DomainIdUtils::DomainIds &);
  friend type access(DomainReadTag);
};
struct DomainBatchReadTag {
  using type = int (ObDASDomainIdMergeIter::*)(int64_t, const ObDASScanCtDef *, ObDASScanRtDef *,
      ObIAllocator &, ObIArray<DomainIdUtils::DomainIds> &);
  friend type access(DomainBatchReadTag);
};
template struct SpiPrivateAccess<DomainSourceCtTag, &ObDASDomainIdMergeIter::data_table_ctdef_>;
template struct SpiPrivateAccess<DomainSourceRtTag, &ObDASDomainIdMergeIter::data_table_rtdef_>;
template struct SpiPrivateAccess<DomainReadTag, &ObDASDomainIdMergeIter::get_domain_id>;
template struct SpiPrivateAccess<DomainBatchReadTag, &ObDASDomainIdMergeIter::get_domain_ids>;

class DomainIdRejectOwner : public ObIAllocator
{
public:
  void *alloc(int64_t) override { ++calls_; return nullptr; }
  void *alloc(int64_t, const ObMemAttr &) override { ++calls_; return nullptr; }
  void free(void *) override {}
  int64_t calls_ = 0;
};

class NativeDomainIdProducer : public testing::Test
{
protected:
  NativeDomainIdProducer()
    : owner_("DomainIdTest"), exec_(owner_), eval_(exec_), source_(owner_), data_(owner_), iter_(nullptr)
  {}
  void SetUp() override
  {
    frames_[0] = static_cast<char *>(owner_.alloc(4096));
    ASSERT_NE(nullptr, frames_[0]);
    MEMSET(frames_[0], 0, 4096);
    exec_.set_frames(frames_); exec_.set_frame_cnt(1);
    eval_.frames_ = frames_; eval_.max_batch_size_ = 6;
    runtime_.eval_ctx_ = &eval_;
    source_.ref_table_id_ = 100001;
    // This post-scan port only needs its real output directory/read metadata;
    // no table scan, storage service or synthetic row-reader is substituted.
    ASSERT_EQ(0, source_.table_param_.get_read_info().get_schema_rowkey_count());
    ASSERT_EQ(0, source_.semantic_index_info_.part_key_num_);
    ASSERT_EQ(OB_SUCCESS, source_.result_output_.init(1));
    ASSERT_EQ(OB_SUCCESS, source_.result_output_.push_back(&expressions_[0]));
    ASSERT_EQ(OB_SUCCESS, data_.domain_tids_.init(1));
    ASSERT_EQ(OB_SUCCESS, data_.domain_tids_.push_back(source_.ref_table_id_));
    ASSERT_EQ(OB_SUCCESS, data_.domain_types_.init(1));
    ASSERT_EQ(OB_SUCCESS, data_.domain_types_.push_back(DomainIdUtils::VID));
    ASSERT_EQ(OB_SUCCESS, data_.domain_id_idxs_.init(1));
    oceanbase::share::DomainIdxs indices;
    ASSERT_EQ(OB_SUCCESS, indices.push_back(0));
    ASSERT_EQ(OB_SUCCESS, data_.domain_id_idxs_.push_back(indices));
    void *iter_memory = owner_.alloc(sizeof(ObDASDomainIdMergeIter));
    ASSERT_NE(nullptr, iter_memory);
    iter_ = new (iter_memory) ObDASDomainIdMergeIter();
    iter_->*access(DomainSourceCtTag{}) = &data_;
    iter_->*access(DomainSourceRtTag{}) = &runtime_;
    for (int64_t col = 0; col < 2; ++col) {
      ObExpr &expr = expressions_[col];
      expr.type_ = T_REF_COLUMN; expr.frame_idx_ = 0;
      expr.datum_off_ = col * 512; expr.eval_info_off_ = col * 512 + 384;
      expr.res_buf_off_ = 1024 + col * 512; expr.res_buf_len_ = 256;
      expr.batch_result_ = true; expr.batch_idx_mask_ = UINT64_MAX;
      for (int64_t row = 0; row < 6; ++row) {
        new (frames_[0] + expr.datum_off_ + row * sizeof(Datum)) Datum();
      }
      new (frames_[0] + expr.eval_info_off_) ObEvalInfo{};
    }
    ASSERT_NO_FATAL_FAILURE(configure(DomainIdUtils::VID, ObIntType));
  }
  void TearDown() override
  {
    // Match arena-owned DAS iterators: release while the dynamic type is live.
    if (iter_ != nullptr) { EXPECT_EQ(OB_SUCCESS, iter_->release()); }
  }
  void configure(DomainIdUtils::ObDomainIDType domain, SqlValueType kind)
  {
    data_.domain_types_.at(0) = domain;
    for (auto &expr : expressions_) {
      expr.obj_meta_.set_type(kind); expr.obj_meta_.set_collation_type(CS_TYPE_BINARY);
      expr.obj_meta_.set_scale(0);
      expr.datum_meta_ = ObDatumMeta(kind, CS_TYPE_BINARY, 0, 19);
      expr.obj_datum_map_ = kind == ObIntType ? OBJ_DATUM_8BYTE_DATA : OBJ_DATUM_STRING;
      ObAccuracy accuracy;
      if (kind == ObIntType) { accuracy = ObAccuracy(19, 0); }
      else { accuracy.set_full_length(256, LS_BYTE); accuracy.set_scale(0); }
      ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, accuracy, owner_));
    }
  }
  Datum &value(int64_t column, int64_t row = 0)
  { return expressions_[column].locate_batch_datums(eval_)[row]; }
  int read(ObIAllocator &owner, DomainIdUtils::DomainIds &ids)
  { return (iter_->*access(DomainReadTag{}))(&source_, &runtime_, owner, ids); }
  int read_batch(ObIAllocator &owner, ObIArray<DomainIdUtils::DomainIds> &ids)
  { return (iter_->*access(DomainBatchReadTag{}))(6, &source_, &runtime_, owner, ids); }
  ObArenaAllocator owner_;
  ObExecContext exec_;
  ObEvalCtx eval_;
  char *frames_[1] = {};
  ObExpr expressions_[2];
  ObDASScanCtDef source_, data_;
  ObDASScanRtDef runtime_;
  ObDASDomainIdMergeIter *iter_;
};

TEST_F(NativeDomainIdProducer, scalar_and_batch_vid_survive_fill_and_stored_row_materialization)
{
  const int64_t expected[] = {0, -1, INT64_MIN, INT64_MAX, 17, 4096};
  ObArenaAllocator captured("DomainIdCopy");
  ObSEArray<DomainIdUtils::DomainIds, 6> batch_ids;
  for (int64_t i = 0; i < 6; ++i) { value(0, i).set_int(expected[i]); }
  ASSERT_EQ(OB_SUCCESS, read_batch(captured, batch_ids));
  ASSERT_EQ(6, batch_ids.count());
  for (int64_t i = 0; i < 6; ++i) {
    SCOPED_TRACE(i);
    ASSERT_EQ(1, batch_ids.at(i).count());
    ASSERT_EQ(sizeof(int64_t), batch_ids.at(i).at(0).length());
    int64_t bits = 0;
    MEMCPY(&bits, batch_ids.at(i).at(0).ptr(), sizeof(bits));
    EXPECT_EQ(expected[i], bits);
    value(0, i).set_null();
  }
  // Re-enter the real producer with other values, overwriting its stack scratch.
  // The first batch must still own every physical VID independently.
  for (int64_t i = 0; i < 6; ++i) {
    SCOPED_TRACE(i);
    value(0).set_int(expected[i]);
    DomainIdUtils::DomainIds one;
    ASSERT_EQ(OB_SUCCESS, read(captured, one));
    ASSERT_EQ(1, one.count()); ASSERT_EQ(sizeof(int64_t), one.at(0).length());
    value(0).set_null();
    ASSERT_EQ(OB_SUCCESS, DomainIdUtils::fill_domain_id_datum(DomainIdUtils::VID,
        &expressions_[1], &eval_, batch_ids.at(i).at(0)));
    ASSERT_FALSE(value(1).is_null()); EXPECT_EQ(expected[i], value(1).get_int());
    Datum row_values[2]; row_values[0] = value(1); row_values[1].set_int(101 + i);
    DatumCopyPlan plans[] = {expressions_[1].get_datum_copy_plan(), expressions_[1].get_datum_copy_plan()};
    ObArrayHelper<DatumCopyPlan> plan_array(2, plans, 2);
    char memory[sizeof(ObChunkDatumStore::StoredRow) + 2 * sizeof(StoredDatumCell) + 16];
    ObChunkDatumStore::StoredRow *stored = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObChunkDatumStore::StoredRow::build_native(stored, row_values, 2,
        plan_array, memory, sizeof(memory), 0));
    row_values[0].set_null(); row_values[1].set_null(); value(1).set_null();
    const ObDatum old_id = StoredDatumCodec::decode_legacy_view(stored->stored_cells()[0]);
    const ObDatum new_id = StoredDatumCodec::decode_legacy_view(stored->stored_cells()[1]);
    ASSERT_FALSE(old_id.is_null()); ASSERT_FALSE(new_id.is_null());
    EXPECT_EQ(expected[i], old_id.get_int()); EXPECT_EQ(101 + i, new_id.get_int());
  }
  value(0).set_null();
  DomainIdRejectOwner reject;
  DomainIdUtils::DomainIds null_id;
  ASSERT_EQ(OB_SUCCESS, read(reject, null_id)); ASSERT_EQ(1, null_id.count());
  EXPECT_TRUE(null_id.at(0).empty()); EXPECT_EQ(0, reject.calls_);
  value(1).set_int(99);
  ASSERT_EQ(OB_SUCCESS, DomainIdUtils::fill_domain_id_datum(DomainIdUtils::VID,
      &expressions_[1], &eval_, null_id.at(0)));
  EXPECT_TRUE(value(1).is_null());
  value(0).set_int(0);
  DomainIdUtils::DomainIds unchanged;
  ASSERT_EQ(OB_SUCCESS, unchanged.push_back(ObString::make_string("prefix")));
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, read(reject, unchanged));
  EXPECT_EQ(1, reject.calls_); ASSERT_EQ(1, unchanged.count());
  EXPECT_EQ(ObString::make_string("prefix"), unchanged.at(0));
}

TEST_F(NativeDomainIdProducer, reference_doc_id_retains_bytes_and_preserves_empty_and_null)
{
  ASSERT_NO_FATAL_FAILURE(configure(DomainIdUtils::DOC_ID, ObVarcharType));
  char source[] = {'d', '\0', 'o', 'c'};
  value(0).set_string(source, sizeof(source));
  ObArenaAllocator captured("DomainDocCopy");
  DomainIdUtils::DomainIds ids;
  ASSERT_EQ(OB_SUCCESS, read(captured, ids)); ASSERT_EQ(1, ids.count());
  ASSERT_EQ(sizeof(source), ids.at(0).length()); EXPECT_NE(source, ids.at(0).ptr());
  MEMSET(source, 'x', sizeof(source)); value(0).set_null();
  EXPECT_EQ(std::string("d\0oc", 4), std::string(ids.at(0).ptr(), ids.at(0).length()));
  ASSERT_EQ(OB_SUCCESS, DomainIdUtils::fill_domain_id_datum(DomainIdUtils::DOC_ID,
      &expressions_[1], &eval_, ids.at(0)));
  EXPECT_EQ(std::string("d\0oc", 4), std::string(value(1).get_string().ptr(), value(1).length()));
  for (bool is_null : {false, true}) {
    SCOPED_TRACE(is_null);
    if (is_null) { value(0).set_null(); } else { value(0).set_string(source, 0); }
    DomainIdRejectOwner reject;
    DomainIdUtils::DomainIds empty;
    ASSERT_EQ(OB_SUCCESS, read(reject, empty)); ASSERT_EQ(1, empty.count());
    EXPECT_TRUE(empty.at(0).empty()); EXPECT_EQ(0, reject.calls_);
  }
  value(0).set_string(source, sizeof(source));
  DomainIdRejectOwner reject;
  DomainIdUtils::DomainIds empty;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, read(reject, empty));
  EXPECT_TRUE(empty.empty()); EXPECT_EQ(1, reject.calls_);
}
} // namespace

namespace {
TEST_F(NativeExecutionParameters, canonical_signed_i64_init_keeps_attributes_and_owned_execution_values)
{
  ASSERT_NO_FATAL_FAILURE(load({INT64_MIN, 0, INT64_MAX}));
  DatumParameterMetadata expected[3];
  for (int64_t i = 0; i < 3; ++i) {
    DatumParameter input = plan().get_parameter_inputs().at(i);
    auto attributes = input.get_metadata();
    attributes.accuracy_ = ObAccuracy(20, 0);
    attributes.raw_text_pos_ = 31 + i;
    attributes.raw_text_len_ = 2 + i;
    attributes.res_flags_ = 0x1200 + i;
    attributes.flag_.extend_flag_ = 0x40 + i;
    input.set_metadata(attributes);
    ASSERT_EQ(OB_SUCCESS, plan().set_parameter_input(i, input));
    expected[i] = attributes;
    ASSERT_EQ(ObIntType, input.get_type());
    ASSERT_TRUE(input.datum_.is_present());
    ASSERT_EQ(0U, input.datum_.length());
    ASSERT_EQ(Datum::NoReferenceFlags, input.datum_.reference_flags());
  }
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ASSERT_TRUE(plan().native_parameters_ready());
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_count(plan()));
  EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_bytes(plan()));
  const int64_t values[] = {INT64_MIN, 0, INT64_MAX};
  for (int64_t i = 0; i < 3; ++i) {
    const Datum snapshot = read_parameter_value(plan(), i);
    const Datum *value = &snapshot;
    const SqlType *type = plan().get_native_parameter_type(i);
    ASSERT_NE(nullptr, type);
    EXPECT_EQ(nullptr, plan().locate_materialized_parameter(i));
    EXPECT_EQ(ObIntType, type->kind());
    EXPECT_EQ(DatumRepresentation::Inline64, type->canonical_representation());
    EXPECT_EQ(expected[i].type_meta_, type->legacy_meta());
    EXPECT_EQ(expected[i].accuracy_, type->accuracy());
    EXPECT_EQ(values[i], value->get_int());
    EXPECT_EQ(0U, value->length());
    EXPECT_EQ(Datum::NoReferenceFlags, value->reference_flags());
    ASSERT_NE(nullptr, plan().get_parameter_metadata(i));
    ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(
        expected[i], *plan().get_parameter_metadata(i)));
  }
  // The existing exact-declaration sharing and producer-retirement contracts
  // remain observable through the public binding, QM and range entries.
  EXPECT_EQ(plan().get_native_parameter_type(0), plan().get_native_parameter_type(1));
  EXPECT_EQ(plan().get_native_parameter_type(1), plan().get_native_parameter_type(2));
  for (Datum &input : request_values_) { input.set_int(7); }
  std::vector<Datum>().swap(request_values_);
  for (int64_t i = 0; i < 3; ++i) {
    ASSERT_NO_FATAL_FAILURE(expect_questionmark(i, values[i]));
  }
  ASSERT_NO_FATAL_FAILURE(expect_point(INT64_MIN, 0, INT64_MAX));
}

TEST_F(NativeExecutionParameters, signed_i64_init_keeps_fallbacks_first_error_and_retry_atomicity)
{
  // An unsigned word, typed NULL and generic Reference Decimal must retain
  // their original import domains even where the prepared carrier is inline.
  plan().reset_datum_param_store();
  DatumParameter unsigned_input, null_input, decimal_input;
  ASSERT_EQ(OB_SUCCESS, unsigned_input.set_uint64(UINT64_MAX, request_arena_));
  unsigned_input.set_accuracy(ObAccuracy(20, 0)); unsigned_input.set_param_meta();
  ASSERT_EQ(OB_SUCCESS, null_input.set_int(0, request_arena_));
  null_input.set_accuracy(ObAccuracy(20, 0)); null_input.set_param_meta();
  null_input.datum_.set_null();
  int64_t coefficient = 123400;
  Datum decimal;
  ASSERT_EQ(OB_SUCCESS, decimal.set_reference(&coefficient, sizeof(coefficient)));
  SqlTypeMeta decimal_meta; decimal_meta.set_decimal_int(2);
  ASSERT_EQ(OB_SUCCESS, decimal_input.assign_native_borrowed(decimal, decimal_meta));
  decimal_input.set_accuracy(ObAccuracy(18, 2)); decimal_input.set_param_meta();
  ASSERT_EQ(OB_SUCCESS, plan().append_parameter_input(unsigned_input));
  ASSERT_EQ(OB_SUCCESS, plan().append_parameter_input(null_input));
  ASSERT_EQ(OB_SUCCESS, plan().append_parameter_input(decimal_input));
  plan().set_original_param_cnt(3);
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  EXPECT_EQ(ObUInt64Type, plan().get_native_parameter_type(0)->kind());
  EXPECT_EQ(UINT64_MAX, read_parameter_value(plan(), 0).get_uint64());
  EXPECT_EQ(ObIntType, plan().get_native_parameter_type(1)->kind());
  EXPECT_TRUE(read_parameter_value(plan(), 1).is_null());
  EXPECT_EQ(ObDecimalIntType, plan().get_native_parameter_type(2)->kind());
  EXPECT_EQ(DatumRepresentation::Inline64,
      plan().get_native_parameter_type(2)->canonical_representation());
  coefficient = 0;
  EXPECT_EQ(123400, read_parameter_value(plan(), 2).get_int());
  EXPECT_EQ(2, plan().get_native_parameter_type(2)->scale());
  ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(
      decimal_input.get_metadata(), *plan().get_parameter_metadata(2)));

  for (int malformed = 0; malformed < 6; ++malformed) {
    SCOPED_TRACE(malformed);
    ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
    const DatumParameter valid = plan().get_parameter_inputs().at(1);
    DatumParameter invalid = valid;
    int64_t backing_word = 2;
    if (malformed == 0) {
      ASSERT_EQ(OB_SUCCESS, invalid.datum_.set_reference(&backing_word, 7));
    } else if (malformed == 1 || malformed == 2) {
      ASSERT_EQ(OB_SUCCESS, invalid.datum_.set_reference(&backing_word, 0,
          malformed == 1 ? Datum::OutRow : Datum::HasLobHeader));
    } else if (malformed == 3) { invalid.datum_.set_min(); }
    else if (malformed == 4) { invalid.datum_.set_max(); }
    else { invalid.datum_.set_nop(); }
    ASSERT_EQ(OB_SUCCESS, plan().set_parameter_input(1, invalid));
    // Type allocation for the first valid parameter still precedes any read
    // of the later malformed value. The fast branch must not move this error.
    owner_.fail_size_ = sizeof(SqlType);
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan().init_datum_param_store());
    EXPECT_FALSE(plan().native_parameters_ready());
    EXPECT_EQ(nullptr, materialized_parameter(plan(), 0));
    owner_.fail_size_ = 0;
    EXPECT_EQ(OB_INVALID_ARGUMENT, plan().init_datum_param_store());
    EXPECT_FALSE(plan().native_parameters_ready());
    EXPECT_EQ(nullptr, materialized_parameter(plan(), 0));
    EXPECT_EQ(nullptr, plan().get_native_parameter_type(0));
    ASSERT_EQ(3, plan().get_parameter_inputs().count());
    EXPECT_EQ(0, MEMCMP(&invalid.datum_,
        &plan().get_parameter_inputs().at(1).datum_, sizeof(Datum)));
    ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(invalid.get_metadata(),
        plan().get_parameter_inputs().at(1).get_metadata()));
    ASSERT_EQ(OB_SUCCESS, plan().set_parameter_input(1, valid));
    ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
    ASSERT_NO_FATAL_FAILURE(expect_questionmark(1, 2));
    ASSERT_NO_FATAL_FAILURE(expect_point(1, 2, 3));
  }
}
} // namespace


namespace {
TEST_F(NativeExecutionParameters, typed_bigint_null_init_rejects_point_at_every_key_position)
{
  for (int64_t null_index = 0; null_index < 3; ++null_index) {
    SCOPED_TRACE(null_index);
    for (int64_t previous_payload : {int64_t(0), int64_t(-73)}) {
      SCOPED_TRACE(previous_payload);
      ASSERT_NO_FATAL_FAILURE(load({11, 22, 33}));
      DatumParameter input = plan().get_parameter_inputs().at(null_index);
      input.datum_.set_int(previous_payload);
      input.datum_.set_null(); // Both a zero word and a retired nonzero word are SQL NULL.
      ASSERT_TRUE(input.datum_.is_null());
      ASSERT_EQ(ObIntType, input.get_param_meta().get_type());
      ASSERT_EQ(OB_SUCCESS, plan().set_parameter_input(null_index, input));
      ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
      ASSERT_TRUE(plan().native_parameters_ready());
      EXPECT_EQ(0, NativeExecutionParametersTestPeer::input_count(plan()));
      const Datum *slot = materialized_parameter(plan(), null_index);
      const SqlType *type = plan().get_native_parameter_type(null_index);
      ASSERT_NE(nullptr, slot);
      ASSERT_NE(nullptr, type);
      ASSERT_TRUE(slot->is_null());
      ASSERT_EQ(ObIntType, type->kind());
      int64_t extracted = 101 + null_index;
      EXPECT_FALSE(plan().read_native_bigint_parameter(null_index, extracted));
      EXPECT_EQ(101 + null_index, extracted);
      Datum::State state = Datum::State::Present;
      EXPECT_TRUE(plan().read_native_bigint_parameter(null_index, extracted, state));
      EXPECT_EQ(Datum::State::Null, state);
      EXPECT_EQ(101 + null_index, extracted);
      // This exercises the real bound graph and its existing independent
      // 101/102/103 sentinels, including NULL after a valid prefix.
      ASSERT_NO_FATAL_FAILURE(expect_no_point());
      EXPECT_TRUE(slot->is_null());
    }
  }
}

TEST_F(NativeExecutionParameters, ready_bigint_null_rebind_preserves_point_output_and_present_zero)
{
  for (int64_t null_index = 0; null_index < 3; ++null_index) {
    SCOPED_TRACE(null_index);
    ASSERT_NO_FATAL_FAILURE(load({0, 0, 0}));
    ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
    ASSERT_NO_FATAL_FAILURE(expect_point(0, 0, 0));
    const SqlType *type = plan().get_native_parameter_type(null_index);
    const Datum *slot = materialized_parameter(plan(), null_index);
    ASSERT_NE(nullptr, type);
    ASSERT_NE(nullptr, slot);
    const DatumParameterMetadata attributes = *plan().get_parameter_metadata(null_index);
    Datum replacement;
    replacement.set_int(97);
    replacement.set_null();
    ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(null_index, replacement, *type, attributes));
    ASSERT_TRUE(plan().native_parameters_ready());
    EXPECT_EQ(type, plan().get_native_parameter_type(null_index));
    EXPECT_EQ(slot, materialized_parameter(plan(), null_index));
    ASSERT_TRUE(slot->is_null());
    EXPECT_EQ(ObIntType, type->kind());
    EXPECT_EQ(ObNullType, plan().get_parameter_metadata(null_index)->type_meta_.get_type());
    EXPECT_EQ(ObIntType, plan().get_parameter_metadata(null_index)->param_meta_.get_type());
    int64_t extracted = 101 + null_index;
    EXPECT_FALSE(plan().read_native_bigint_parameter(null_index, extracted));
    EXPECT_EQ(101 + null_index, extracted);
    ASSERT_NO_FATAL_FAILURE(expect_no_point());
    // A real zero is still eligible. Restoring the value uses the public ready
    // binding, without changing the graph, Type or constructing a proxy store.
    replacement.set_int(0);
    ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(null_index, replacement, *type, attributes));
    EXPECT_TRUE(plan().read_native_bigint_parameter(null_index, extracted));
    EXPECT_EQ(0, extracted);
    Datum::State state = Datum::State::Null;
    extracted = 101 + null_index;
    EXPECT_TRUE(plan().read_native_bigint_parameter(null_index, extracted, state));
    EXPECT_EQ(Datum::State::Present, state);
    EXPECT_EQ(0, extracted);
    EXPECT_EQ(slot, materialized_parameter(plan(), null_index));
    ASSERT_TRUE(slot->is_present());
    ASSERT_NO_FATAL_FAILURE(expect_point(0, 0, 0));
  }
}
} // namespace

namespace {
TEST_F(NativeExecutionParameters, compact_owner_point_and_capture_never_materialize_generic_parameters)
{
  ASSERT_NO_FATAL_FAILURE(load({INT64_MIN, 0, INT64_MAX}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ASSERT_EQ(1, plan().get_param_frame_ptrs().count());
  const auto &page = ParameterValuePage::from_handle(plan().get_param_frame_ptrs().at(0));
  EXPECT_EQ(3, page.capacity());
  EXPECT_EQ(24, ParameterValuePage::payload_bytes(3));
  EXPECT_EQ(56, ParameterValuePage::allocation_bytes(3));
  EXPECT_EQ(1, owner_.calls_for(ParameterValuePage::allocation_bytes(3)));
  EXPECT_EQ(0, owner_.calls_for(sizeof(Datum)));
  ObExpr alias;
  alias.type_ = T_QUESTIONMARK; alias.native_parameter_alias_ = true; alias.extra_ = 1;
  alias.datum_off_ = ScalarFrameLayout::datum_offset(1);
  alias.eval_info_off_ = ScalarFrameLayout::eval_info_offset(1);
  ASSERT_EQ(OB_SUCCESS, alias.bind_sql_type(*plan().get_native_parameter_type(1), request_arena_));
  ObEvalCtx eval(exec_); // No generic frame directory is needed for an alias.
  owner_.clear_calls(); owner_.reject_all_ = true;
  ASSERT_NO_FATAL_FAILURE(expect_point(INT64_MIN, 0, INT64_MAX));
  NativeTypedDatum captured; DatumParameterMetadata attributes;
  ASSERT_EQ(OB_SUCCESS, capture_datum_parameter(alias, eval, captured, attributes));
  EXPECT_EQ(0, captured.datum_.get_int());
  EXPECT_EQ(plan().get_native_parameter_type(1), captured.type_);
  ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(*plan().get_parameter_metadata(1), attributes));
  alignas(8) char scratch[64]; ObDatum legacy;
  ASSERT_EQ(OB_SUCCESS, plan().export_runtime_parameter_legacy(1, legacy, scratch, sizeof(scratch)));
  EXPECT_EQ(0, legacy.get_int());
  int64_t bytes = 0;
  ASSERT_EQ(OB_SUCCESS, NativeFrameCodec::parameter_size(plan(), bytes));
  EXPECT_GT(bytes, 0);
  for (int64_t i = 0; i < 3; ++i) {
    EXPECT_TRUE(plan().has_native_parameter(i));
    EXPECT_NE(nullptr, plan().get_native_parameter_type(i));
    EXPECT_NE(nullptr, plan().get_parameter_metadata(i));
    EXPECT_EQ(nullptr, plan().locate_materialized_parameter(i));
    Datum value;
    ASSERT_EQ(OB_SUCCESS, plan().read_native_parameter(i, value));
    EXPECT_EQ(i == 0 ? INT64_MIN : i == 1 ? 0 : INT64_MAX, value.get_int());
  }
  EXPECT_TRUE(owner_.sizes_.empty());
  Datum untouched; untouched.set_int(71);
  EXPECT_EQ(OB_INVALID_ARGUMENT, plan().read_native_parameter(-1, untouched));
  EXPECT_EQ(71, untouched.get_int());
  Datum *result = &untouched;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, alias.eval(eval, result));
  EXPECT_EQ(&untouched, result); EXPECT_EQ(71, untouched.get_int());
  EXPECT_EQ(nullptr, plan().locate_materialized_parameter(1));
  EXPECT_FALSE(alias.get_eval_info(eval).evaluated_);
  // This is the real helper used by NVL/MAKETIME before they locate arguments.
  alignas(8) char skip_memory[8]; MEMSET(skip_memory, 0xff, sizeof(skip_memory));
  const ObBitVector &skip = *to_bit_vector(skip_memory);
  ObExpr parent; ObExpr *arguments[] = {&alias}; parent.args_ = arguments; parent.arg_cnt_ = 1;
  DatumVector vector; vector.datums_ = &untouched;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, parent.eval_batch_param_value(eval, skip, 3, vector));
  EXPECT_EQ(&untouched, vector.datums_);
  EXPECT_EQ(nullptr, plan().locate_materialized_parameter(1));
  char printed[128] = {};
  const int64_t printed_size = ObToStringExpr(eval, alias).to_string(printed, sizeof(printed));
  ASSERT_GT(printed_size, 0);
  EXPECT_NE(std::string::npos, std::string(printed, printed_size).find("expr_eval_error"));
  EXPECT_EQ(nullptr, plan().locate_materialized_parameter(1));
  owner_.reject_all_ = false;
}

TEST_F(NativeExecutionParameters, generic_alias_directory_and_slot_oom_publish_nothing_and_retry_stably)
{
  ASSERT_NO_FATAL_FAILURE(load({INT64_MIN, 0, INT64_MAX}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ObExpr alias;
  alias.type_ = T_QUESTIONMARK; alias.native_parameter_alias_ = true; alias.extra_ = 1;
  alias.datum_off_ = ScalarFrameLayout::datum_offset(1);
  alias.eval_info_off_ = ScalarFrameLayout::eval_info_offset(1);
  ASSERT_EQ(OB_SUCCESS, alias.bind_sql_type(*plan().get_native_parameter_type(1), request_arena_));
  ObEvalCtx eval(exec_);
  const SqlType *type = plan().get_native_parameter_type(1);
  const DatumParameterMetadata attributes = *plan().get_parameter_metadata(1);
  ObEvalInfo *info = plan().get_parameter_eval_info(1);
  info->flag_ = 3; info->cnt_ = 17;
  Datum sentinel; sentinel.set_int(71);
  for (int64_t rejected : {int64_t(3 * sizeof(Datum *)), int64_t(sizeof(Datum))}) {
    owner_.clear_calls(); owner_.fail_size_ = rejected;
    Datum *result = &sentinel;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, alias.eval(eval, result));
    EXPECT_EQ(&sentinel, result);
    EXPECT_EQ(nullptr, plan().locate_materialized_parameter(1));
    EXPECT_EQ(type, plan().get_native_parameter_type(1));
    EXPECT_EQ(3, info->flag_); EXPECT_EQ(17, info->cnt_);
    EXPECT_EQ(1, owner_.calls_for(3 * sizeof(Datum *)));
    EXPECT_EQ(rejected == sizeof(Datum) ? 1 : 0, owner_.calls_for(sizeof(Datum)));
    ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(attributes, *plan().get_parameter_metadata(1)));
    EXPECT_EQ(0, read_parameter_value(plan(), 1).get_int());
  }
  owner_.fail_size_ = 0; owner_.clear_calls();
  Datum *result = nullptr;
  ASSERT_EQ(OB_SUCCESS, alias.eval(eval, result)); ASSERT_NE(nullptr, result);
  EXPECT_EQ(1, owner_.calls_for(3 * sizeof(Datum *)));
  EXPECT_EQ(1, owner_.calls_for(sizeof(Datum)));
  EXPECT_EQ(result, &alias.locate_expr_datum(eval));
  EXPECT_EQ(result, alias.locate_expr_datumvector(eval).at(3));
  EXPECT_EQ(result, alias.locate_batch_datums(eval));
  ObExpr another = alias; another.extra_ = 2;
  another.datum_off_ = ScalarFrameLayout::datum_offset(2);
  another.eval_info_off_ = ScalarFrameLayout::eval_info_offset(2);
  owner_.clear_calls(); owner_.fail_size_ = sizeof(Datum);
  Datum *failed = &sentinel;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, another.eval(eval, failed));
  EXPECT_EQ(&sentinel, failed); EXPECT_EQ(nullptr, plan().locate_materialized_parameter(2));
  EXPECT_EQ(0, owner_.calls_for(3 * sizeof(Datum *)));
  EXPECT_EQ(result, plan().locate_materialized_parameter(1));
  owner_.fail_size_ = 0; owner_.clear_calls(); owner_.reject_all_ = true;
  for (int state = 0; state < 3; ++state) {
    Datum value;
    if (state == 0) { value.set_null(); } else { value.set_int(state == 1 ? 0 : INT64_MIN); }
    ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(1, value, *type, attributes));
    Datum *again = nullptr; ASSERT_EQ(OB_SUCCESS, alias.eval(eval, again));
    EXPECT_EQ(result, again); EXPECT_EQ(value.state(), result->state());
    EXPECT_EQ(type, plan().get_native_parameter_type(1));
    int64_t raw = 93;
    EXPECT_EQ(state != 0, plan().read_native_bigint_parameter(1, raw));
    EXPECT_EQ(state == 0 ? 93 : state == 1 ? 0 : INT64_MIN, raw);
    if (state != 0) { EXPECT_EQ(raw, result->get_int()); }
  }
  EXPECT_TRUE(owner_.sizes_.empty());
  owner_.reject_all_ = false;
  const Datum *old = result;
  char *old_page = plan().get_param_frame_ptrs().at(0);
  char *copied_directory[] = {old_page};
  ObEvalCtx copied(exec_); copied.frames_ = copied_directory;
  owner_.clear_calls(); owner_.fail_size_ = 3 * sizeof(Datum *);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, rebuild());
  EXPECT_EQ(old_page, plan().get_param_frame_ptrs().at(0));
  EXPECT_EQ(old, plan().locate_materialized_parameter(1));
  EXPECT_EQ(INT64_MIN, old->get_int());
  owner_.fail_size_ = 0;
  ASSERT_EQ(OB_SUCCESS, rebuild());
  EXPECT_NE(old_page, plan().get_param_frame_ptrs().at(0));
  ASSERT_EQ(OB_SUCCESS, alias.eval(copied, result));
  EXPECT_NE(old, result); EXPECT_EQ(INT64_MIN, result->get_int());
  EXPECT_EQ(result, &alias.locate_expr_datum(copied));
  EXPECT_EQ(old_page, copied.frames_[0]);
  EXPECT_EQ(INT64_MIN, old->get_int()); // Old arena borrow remains the old snapshot.
}

TEST_F(NativeExecutionParameters, direct_alias_setter_prepares_abi_before_value_type_or_parent_publication)
{
  ASSERT_NO_FATAL_FAILURE(load({42, 7, 9}));
  ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
  ObExpr alias; alias.type_ = T_QUESTIONMARK; alias.native_parameter_alias_ = true;
  alias.extra_ = 0; alias.frame_idx_ = 1;
  alias.datum_off_ = ScalarFrameLayout::datum_offset(0);
  alias.eval_info_off_ = ScalarFrameLayout::eval_info_offset(0);
  SqlTypeMeta text_meta; text_meta.set_varchar(); text_meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, alias.prepare_scalar_type(text_meta, ObAccuracy(8), request_arena_));
  ObExpr parent; parent.type_ = T_OP_ADD;
  ASSERT_EQ(OB_SUCCESS, parent.bind_sql_type(*plan().get_native_parameter_type(0), request_arena_));
  parent.eval_info_off_ = ScalarFrameLayout::eval_info_offset(0);
  alignas(8) char parent_frame[ScalarFrameLayout::slot_size()] = {};
  new (parent_frame) Datum();
  new (parent_frame + parent.eval_info_off_) ObEvalInfo{};
  char *frames[] = {parent_frame, plan().get_param_frame_ptrs().at(0)};
  ObEvalCtx eval(exec_); eval.frames_ = frames;
  ObExpr *parents[] = {&parent}; alias.parents_ = parents; alias.parent_cnt_ = 1;
  auto *info = plan().get_parameter_eval_info(0);
  info->flag_ = 3; info->cnt_ = 17;
  parent.get_eval_info(eval).evaluated_ = true; parent.get_eval_info(eval).projected_ = true;
  const SqlType *old_type = plan().get_native_parameter_type(0);
  const DatumParameterMetadata old_attributes = *plan().get_parameter_metadata(0);
  Datum incoming; incoming.set_string(ObString::make_string("changed"));
  ObDynamicParamSetter setter(0, nullptr, &alias);
  owner_.clear_calls(); owner_.reject_all_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, setter.update_dynamic_param(eval, incoming));
  EXPECT_EQ(nullptr, plan().locate_materialized_parameter(0));
  EXPECT_EQ(42, read_parameter_value(plan(), 0).get_int());
  EXPECT_EQ(3, info->flag_); EXPECT_EQ(17, info->cnt_);
  EXPECT_TRUE(parent.get_eval_info(eval).evaluated_); EXPECT_TRUE(parent.get_eval_info(eval).projected_);
  owner_.reject_all_ = false; owner_.clear_calls(); owner_.fail_call_ = 3;
  // Directory and old-value Datum succeed; the following new Type allocation
  // fails. The cache may survive, while all SQL-visible state remains old.
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, setter.update_dynamic_param(eval, incoming));
  const Datum *old_projection = plan().locate_materialized_parameter(0);
  ASSERT_NE(nullptr, old_projection); EXPECT_EQ(42, old_projection->get_int());
  EXPECT_EQ(old_type, plan().get_native_parameter_type(0));
  ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(old_attributes, *plan().get_parameter_metadata(0)));
  EXPECT_EQ(3, info->flag_); EXPECT_EQ(17, info->cnt_);
  EXPECT_TRUE(parent.get_eval_info(eval).evaluated_); EXPECT_TRUE(parent.get_eval_info(eval).projected_);
  owner_.fail_call_ = 0; owner_.clear_calls();
  ObDynamicParamSetter wrong_index(1, nullptr, &alias);
  EXPECT_EQ(OB_INVALID_ARGUMENT, wrong_index.update_dynamic_param(eval, incoming));
  EXPECT_TRUE(owner_.sizes_.empty());
  // A cached Expr declaration differing from the current actual parameter type
  // must still project the existing value; the ordinary alias is not a PL cast.
  Datum *current = nullptr; ASSERT_EQ(OB_SUCCESS, alias.eval(eval, current));
  EXPECT_EQ(old_projection, current); EXPECT_EQ(42, current->get_int());
  ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(eval, incoming));
  EXPECT_EQ(old_projection, &alias.locate_expr_datum(eval));
  EXPECT_EQ(ObString::make_string("changed"), alias.locate_expr_datum(eval).get_string());
  EXPECT_EQ(ObVarcharType, plan().get_native_parameter_type(0)->kind());
  EXPECT_FALSE(parent.get_eval_info(eval).evaluated_); EXPECT_FALSE(parent.get_eval_info(eval).projected_);
  EXPECT_TRUE(alias.get_eval_info(eval).evaluated_);
}

// Exercise the registered comparison callback through ObExpr::eval_batch.
// Unlike eval_batch_param_value, its NULL short circuit can skip the right
// operand's evaluator before the functor constructs both DatumVectors.
class NativeParameterBatchComparison : public NativeExecutionParameters
{
protected:
  static constexpr int64_t BATCH = 3;
  static constexpr int64_t BLOCK = 128;
  NativeParameterBatchComparison()
    : frame_{}, frames_{frame_.data(), nullptr}, eval_(exec_), batch_(eval_),
      expressions_{}, args_{&expressions_[0], &expressions_[1]}, skip_memory_{}, callback_calls_(0) {}

  void configure(bool parameter_alias, bool left_batch = true, bool right_batch = false)
  {
    ASSERT_NO_FATAL_FAILURE(load({1, 2, 3}));
    DatumParameter text;
    text.set_varchar_view(ObString::make_string("value"));
    text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    text.set_length(5); text.set_param_meta();
    text.set_raw_text_info(7, 5); text.set_result_flags(0x1234);
    ASSERT_EQ(OB_SUCCESS, plan().set_parameter_input(0, text));
    ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
    frames_[1] = plan().get_param_frame_ptrs().at(0);
    exec_.set_frames(frames_); eval_.frames_ = frames_; eval_.max_batch_size_ = BATCH;
    batch_.set_batch_size(BATCH); batch_.set_batch_idx(1);
    for (int64_t col = 0; col < 3; ++col) {
      ObExpr &expr = expressions_[col];
      expr.reset();
      expr.type_ = col == 2 ? T_OP_EQ : T_REF_COLUMN;
      const SqlValueType type = col == 2 ? ObIntType : ObVarcharType;
      SqlTypeMeta meta; meta.set_type(type); meta.set_scale(0);
      meta.set_collation_type(col == 2 ? CS_TYPE_BINARY : CS_TYPE_UTF8MB4_BIN);
      expr.obj_meta_ = meta;
      expr.datum_meta_ = ObDatumMeta(type, meta.get_collation_type(), 0, col == 2 ? 20 : -1);
      ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(meta,
          col == 2 ? ObAccuracy(20, 0) : ObAccuracy(5), request_arena_));
      expr.frame_idx_ = 0; expr.datum_off_ = col * BLOCK;
      expr.eval_info_off_ = col * BLOCK + 48;
      expr.eval_flags_off_ = col * BLOCK + 56;
      expr.pvt_skip_off_ = col * BLOCK + 64;
      expr.batch_result_ = col == 2 || (col == 0 ? left_batch : right_batch);
      expr.batch_idx_mask_ = expr.is_batch_result() ? UINT64_MAX : 0;
      for (int64_t row = 0; row < BATCH; ++row) {
        new (frame_.data() + expr.datum_off_ + row * sizeof(Datum)) Datum();
      }
      new (frame_.data() + expr.eval_info_off_) ObEvalInfo{};
      expr.get_eval_info(eval_).evaluated_ = col != 2;
      expr.get_evaluated_flags(eval_).reset(BATCH);
      expr.get_pvt_skip(eval_).reset(BATCH);
    }
    if (parameter_alias) {
      ASSERT_FALSE(right_batch);
      ObExpr &right = expressions_[1];
      right.type_ = T_QUESTIONMARK; right.native_parameter_alias_ = true; right.extra_ = 0;
      right.frame_idx_ = 1;
      right.datum_off_ = ScalarFrameLayout::datum_offset(0);
      right.eval_info_off_ = ScalarFrameLayout::eval_info_offset(0);
      ASSERT_EQ(OB_SUCCESS, right.bind_sql_type(*plan().get_native_parameter_type(0), request_arena_));
    }
    result().args_ = args_; result().arg_cnt_ = 2;
    result().eval_batch_func_ = ObExprCmpFuncsHelper::get_eval_batch_expr_cmp_func(
        ObVarcharType, ObVarcharType, 0, 0, -1, -1, CO_EQ, CS_TYPE_UTF8MB4_BIN, false);
    ASSERT_NE(nullptr, result().eval_batch_func_);
    skip().reset(BATCH); callback_calls_ = 0; owner_.clear_calls();
    for (int64_t i = 0; i < 3; ++i) { ASSERT_EQ(nullptr, plan().locate_materialized_parameter(i)); }
  }

  ObExpr &result() { return expressions_[2]; }
  Datum &left(int64_t row = 0) { return expressions_[0].locate_batch_datums(eval_)[row]; }
  Datum &output(int64_t row) { return result().locate_batch_datums(eval_)[row]; }
  ObBitVector &skip() { return *to_bit_vector(skip_memory_.data()); }
  int evaluate() { return result().eval_batch(eval_, skip(), BATCH); }
  void reset_result()
  {
    result().get_eval_info(eval_) = ObEvalInfo{};
    result().get_evaluated_flags(eval_).reset(BATCH);
  }
  static int fail_scalar(const ObExpr &expr, ObEvalCtx &, Datum &)
  {
    ++*reinterpret_cast<int64_t *>(expr.extra_);
    return OB_ERR_UNEXPECTED;
  }
  static int fail_batch(const ObExpr &expr, ObEvalCtx &, const ObBitVector &, int64_t)
  {
    ++*reinterpret_cast<int64_t *>(expr.extra_);
    return OB_ERR_UNEXPECTED;
  }

  alignas(8) std::array<char, 3 * BLOCK> frame_;
  char *frames_[2];
  ObEvalCtx eval_;
  ObEvalCtx::BatchInfoScopeGuard batch_;
  ObExpr expressions_[3];
  ObExpr *args_[2];
  alignas(8) std::array<char, 8> skip_memory_;
  int64_t callback_calls_;
};

TEST_F(NativeParameterBatchComparison, null_short_circuit_projects_only_the_consumed_alias)
{
  for (bool all_skipped : {false, true}) {
    SCOPED_TRACE(all_skipped);
    ASSERT_NO_FATAL_FAILURE(configure(true));
    // Both an all-NULL left batch and a fully skipped non-NULL batch omit
    // right.eval_batch in the real relational driver.
    for (int64_t i = 0; i < BATCH; ++i) {
      if (all_skipped) { left(i).set_string(ObString::make_string("value")); skip().set(i); }
    }
    ASSERT_EQ(OB_SUCCESS, evaluate());
    const Datum *projection = plan().locate_materialized_parameter(0);
    ASSERT_NE(nullptr, projection);
    EXPECT_EQ(ObString::make_string("value"), projection->get_string());
    EXPECT_EQ(ObVarcharType, plan().get_native_parameter_type(0)->kind());
    EXPECT_EQ(1, owner_.calls_for(3 * sizeof(Datum *)));
    EXPECT_EQ(1, owner_.calls_for(sizeof(Datum)));
    EXPECT_EQ(nullptr, plan().locate_materialized_parameter(1));
    EXPECT_EQ(nullptr, plan().locate_materialized_parameter(2));
    for (int64_t i = 0; i < BATCH; ++i) {
      EXPECT_TRUE(output(i).is_null());
      EXPECT_EQ(!all_skipped, result().get_evaluated_flags(eval_).at(i));
    }
    // The next mixed batch uses the ordinary evaluator path and the same
    // stable projection. Reference values are actually compared here.
    reset_result(); skip().reset(BATCH);
    left(0).set_string(ObString::make_string("value"));
    left(1).set_string(ObString::make_string("other")); left(2).set_null();
    owner_.clear_calls(); owner_.reject_all_ = true;
    ASSERT_EQ(OB_SUCCESS, evaluate());
    EXPECT_EQ(1, output(0).get_int()); EXPECT_EQ(0, output(1).get_int());
    EXPECT_TRUE(output(2).is_null());
    EXPECT_EQ(projection, plan().locate_materialized_parameter(0));
    EXPECT_TRUE(owner_.sizes_.empty()); EXPECT_EQ(1, eval_.get_batch_idx());
    owner_.reject_all_ = false;
  }
}

TEST_F(NativeParameterBatchComparison, short_circuit_projection_oom_preserves_owner_and_retries)
{
  for (int64_t rejected : {int64_t(3 * sizeof(Datum *)), int64_t(sizeof(Datum))}) {
    SCOPED_TRACE(rejected);
    ASSERT_NO_FATAL_FAILURE(configure(true));
    const SqlType *type = plan().get_native_parameter_type(0);
    const DatumParameterMetadata attributes = *plan().get_parameter_metadata(0);
    ObEvalInfo *info = plan().get_parameter_eval_info(0);
    info->flag_ = 3; info->cnt_ = 17;
    owner_.fail_size_ = rejected;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, evaluate());
    EXPECT_EQ(nullptr, plan().locate_materialized_parameter(0));
    EXPECT_EQ(type, plan().get_native_parameter_type(0));
    EXPECT_EQ(3, info->flag_); EXPECT_EQ(17, info->cnt_);
    ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(attributes, *plan().get_parameter_metadata(0)));
    EXPECT_EQ(ObString::make_string("value"), read_parameter_value(plan(), 0).get_string());
    EXPECT_EQ(1, owner_.calls_for(3 * sizeof(Datum *)));
    EXPECT_EQ(rejected == sizeof(Datum) ? 1 : 0, owner_.calls_for(sizeof(Datum)));
    for (int64_t i = 0; i < BATCH; ++i) {
      EXPECT_TRUE(output(i).is_null());
      EXPECT_FALSE(result().get_evaluated_flags(eval_).at(i));
    }
    // Retry the same failed batch, without resetting Expr evaluation state.
    owner_.fail_size_ = 0; owner_.clear_calls();
    ASSERT_EQ(OB_SUCCESS, evaluate());
    const Datum *projection = plan().locate_materialized_parameter(0);
    ASSERT_NE(nullptr, projection);
    EXPECT_EQ(ObString::make_string("value"), projection->get_string());
    EXPECT_EQ(1, owner_.calls_for(3 * sizeof(Datum *)));
    EXPECT_EQ(1, owner_.calls_for(sizeof(Datum)));
    for (int64_t i = 0; i < BATCH; ++i) {
      EXPECT_TRUE(output(i).is_null());
      EXPECT_TRUE(result().get_evaluated_flags(eval_).at(i));
    }
    EXPECT_EQ(nullptr, plan().locate_materialized_parameter(1));
    EXPECT_EQ(nullptr, plan().locate_materialized_parameter(2));
    EXPECT_EQ(type, plan().get_native_parameter_type(0));
    ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(attributes, *plan().get_parameter_metadata(0)));
  }
}

TEST_F(NativeParameterBatchComparison, null_short_circuit_does_not_execute_ordinary_right_callbacks)
{
  for (bool left_batch : {false, true}) {
    SCOPED_TRACE(left_batch);
    ASSERT_NO_FATAL_FAILURE(configure(false, left_batch, !left_batch));
    ObExpr &right = expressions_[1];
    right.extra_ = reinterpret_cast<uint64_t>(&callback_calls_);
    right.eval_func_ = fail_scalar; right.eval_batch_func_ = fail_batch;
    right.get_eval_info(eval_).evaluated_ = false;
    ASSERT_EQ(OB_SUCCESS, evaluate());
    EXPECT_EQ(0, callback_calls_);
    for (int64_t i = 0; i < BATCH; ++i) { EXPECT_TRUE(output(i).is_null()); }
    EXPECT_TRUE(owner_.sizes_.empty());
    EXPECT_EQ(nullptr, plan().locate_materialized_parameter(0));
    // A non-NULL left value must still evaluate the same right operand and
    // return its error; the negative control is not an inert callback.
    reset_result(); left(left_batch ? 1 : 0).set_string(ObString::make_string("value"));
    EXPECT_EQ(OB_ERR_UNEXPECTED, evaluate());
    EXPECT_EQ(1, callback_calls_);
    EXPECT_TRUE(owner_.sizes_.empty());
  }
}

class NativeParameterSubstringIndex : public NativeExecutionParameters
{
protected:
  NativeParameterSubstringIndex()
    : frame_{}, frames_{frame_.data(), nullptr}, eval_(exec_), expressions_{},
      args_{&expressions_[0], &expressions_[1], &expressions_[2]}, trace_(), callbacks_{} {}

  void SetUp() override
  {
    NativeExecutionParameters::SetUp();
    ASSERT_EQ(OB_SUCCESS, exec_.init_expr_op(1));
  }

  void configure(bool string_alias = false)
  {
    ASSERT_NO_FATAL_FAILURE(load({17, 19, 0}));
    if (string_alias) {
      DatumParameter escape;
      escape.set_varchar_view(ObString::make_string("\\"));
      escape.set_collation_type(CS_TYPE_UTF8MB4_BIN);
      escape.set_length(1); escape.set_param_meta();
      escape.set_need_to_check_type(true); escape.set_raw_text_info(7, 1);
      ASSERT_EQ(OB_SUCCESS, plan().set_parameter_input(2, escape));
    }
    ASSERT_EQ(OB_SUCCESS, plan().init_datum_param_store());
    frames_[1] = plan().get_param_frame_ptrs().at(0);
    exec_.set_frames(frames_); eval_.frames_ = frames_;
    for (int64_t i = 0; i < 4; ++i) {
      ObExpr &expr = expressions_[i];
      expr.reset();
      const bool integer = i == 2 && !string_alias;
      const SqlValueType type = integer ? ObIntType : ObVarcharType;
      SqlTypeMeta meta; meta.set_type(type); meta.set_scale(0);
      meta.set_collation_type(integer ? CS_TYPE_BINARY : CS_TYPE_UTF8MB4_BIN);
      expr.obj_meta_ = meta;
      expr.datum_meta_ = ObDatumMeta(type, meta.get_collation_type(), 0, integer ? 20 : -1);
      ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(meta,
          integer ? ObAccuracy(20, 0) : ObAccuracy(32), request_arena_));
      if (i == 2) {
        expr.type_ = T_QUESTIONMARK; expr.native_parameter_alias_ = true; expr.extra_ = 2;
        expr.frame_idx_ = 1;
        expr.datum_off_ = ScalarFrameLayout::datum_offset(2);
        expr.eval_info_off_ = ScalarFrameLayout::eval_info_offset(2);
        ASSERT_EQ(OB_SUCCESS, expr.bind_sql_type(*plan().get_native_parameter_type(2), request_arena_));
      } else {
        const int64_t slot = i == 3 ? 2 : i;
        expr.type_ = i == 3 ? T_FUN_SYS_SUBSTRING_INDEX : T_REF_COLUMN;
        expr.frame_idx_ = 0;
        expr.datum_off_ = ScalarFrameLayout::datum_offset(slot);
        expr.eval_info_off_ = ScalarFrameLayout::eval_info_offset(slot);
        new (frame_.data() + expr.datum_off_) Datum();
        new (frame_.data() + expr.eval_info_off_) ObEvalInfo{};
      }
    }
    result().args_ = args_; result().arg_cnt_ = 3; result().expr_ctx_id_ = 0;
    result().eval_func_ = ObExprSubstringIndex::eval_substring_index;
    trace_.clear(); owner_.clear_calls();
    for (int64_t i = 0; i < 3; ++i) { ASSERT_EQ(nullptr, plan().locate_materialized_parameter(i)); }
  }

  struct Callback {
    std::vector<int64_t> *trace_;
    int64_t index_;
    int ret_;
  };
  static int record_null(const ObExpr &expr, ObEvalCtx &, Datum &value)
  {
    Callback &callback = *reinterpret_cast<Callback *>(expr.extra_);
    callback.trace_->push_back(callback.index_);
    if (callback.ret_ == OB_SUCCESS) { value.set_null(); }
    return callback.ret_;
  }
  void set_callbacks(int64_t failing_index)
  {
    for (int64_t i = 0; i < 2; ++i) {
      callbacks_[i] = {&trace_, i, i == failing_index ? OB_ERR_UNEXPECTED : OB_SUCCESS};
      expressions_[i].extra_ = reinterpret_cast<uint64_t>(&callbacks_[i]);
      expressions_[i].eval_func_ = record_null;
    }
  }
  ObExpr &result() { return expressions_[3]; }
  Datum &input(int64_t i) { return expressions_[i].locate_expr_datum(eval_); }
  int evaluate(Datum *&value) { return result().eval(eval_, value); }
  void reset_result() { result().get_eval_info(eval_) = ObEvalInfo{}; }

  alignas(8) std::array<char, 3 * ScalarFrameLayout::slot_size()> frame_;
  char *frames_[2];
  ObEvalCtx eval_;
  ObExpr expressions_[4];
  ObExpr *args_[3];
  std::vector<int64_t> trace_;
  Callback callbacks_[2];
};

TEST_F(NativeParameterSubstringIndex, null_strings_evaluate_unmaterialized_count_before_reading_it)
{
  ASSERT_NO_FATAL_FAILURE(configure());
  // Exact scalar callback shape of SUBSTRING_INDEX(NULL, NULL, 0): the first
  // two inputs are generic NULLs and the last is a ready, unprojected alias.
  Datum *value = nullptr;
  ASSERT_EQ(OB_SUCCESS, evaluate(value)); ASSERT_NE(nullptr, value);
  EXPECT_TRUE(value->is_null());
  const Datum *projection = plan().locate_materialized_parameter(2);
  ASSERT_NE(nullptr, projection); EXPECT_EQ(0, projection->get_int());
  EXPECT_EQ(1, owner_.calls_for(3 * sizeof(Datum *)));
  EXPECT_EQ(1, owner_.calls_for(sizeof(Datum)));
  EXPECT_EQ(nullptr, plan().locate_materialized_parameter(0));
  EXPECT_EQ(nullptr, plan().locate_materialized_parameter(1));

  input(0).set_string(ObString::make_string("a.b.c"));
  input(1).set_string(ObString::make_string("."));
  const SqlType *type = plan().get_native_parameter_type(2);
  const DatumParameterMetadata metadata = *plan().get_parameter_metadata(2);
  for (int64_t count : {int64_t(0), int64_t(2), int64_t(-1)}) {
    Datum next; next.set_int(count);
    ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(2, next, *type, metadata));
    reset_result();
    ASSERT_EQ(OB_SUCCESS, evaluate(value)); ASSERT_FALSE(value->is_null());
    const ObString expected = ObString::make_string(count == 0 ? "" : count == 2 ? "a.b" : "c");
    EXPECT_EQ(expected, value->get_string());
    EXPECT_EQ(projection, plan().locate_materialized_parameter(2));
  }
  Datum null_count; null_count.set_null();
  ASSERT_EQ(OB_SUCCESS, plan().set_runtime_parameter(2, null_count, *type, metadata));
  reset_result();
  ASSERT_EQ(OB_SUCCESS, evaluate(value)); EXPECT_TRUE(value->is_null());
  EXPECT_EQ(ObIntType, plan().get_native_parameter_type(2)->kind());
  EXPECT_EQ(projection, plan().locate_materialized_parameter(2));
}

TEST_F(NativeParameterSubstringIndex, count_projection_oom_preserves_owner_and_retries)
{
  for (int64_t rejected : {int64_t(3 * sizeof(Datum *)), int64_t(sizeof(Datum))}) {
    SCOPED_TRACE(rejected);
    ASSERT_NO_FATAL_FAILURE(configure());
    const SqlType *type = plan().get_native_parameter_type(2);
    const DatumParameterMetadata metadata = *plan().get_parameter_metadata(2);
    ObEvalInfo *info = plan().get_parameter_eval_info(2);
    info->flag_ = 3; info->cnt_ = 17;
    owner_.fail_size_ = rejected;
    result().locate_expr_datum(eval_).set_int(71);
    Datum *value = nullptr;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, evaluate(value));
    ASSERT_NE(nullptr, value); EXPECT_TRUE(value->is_null()); // Existing eval error cleanup.
    EXPECT_FALSE(result().get_eval_info(eval_).evaluated_);
    EXPECT_EQ(nullptr, plan().locate_materialized_parameter(2));
    EXPECT_EQ(type, plan().get_native_parameter_type(2));
    EXPECT_EQ(3, info->flag_); EXPECT_EQ(17, info->cnt_);
    ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(metadata, *plan().get_parameter_metadata(2)));
    EXPECT_EQ(0, read_parameter_value(plan(), 2).get_int());
    EXPECT_EQ(1, owner_.calls_for(3 * sizeof(Datum *)));
    EXPECT_EQ(rejected == sizeof(Datum) ? 1 : 0, owner_.calls_for(sizeof(Datum)));
    owner_.fail_size_ = 0; owner_.clear_calls();
    // Retry the same failed expression without resetting evaluation state.
    ASSERT_EQ(OB_SUCCESS, evaluate(value)); EXPECT_TRUE(value->is_null());
    ASSERT_NE(nullptr, plan().locate_materialized_parameter(2));
    EXPECT_EQ(0, plan().locate_materialized_parameter(2)->get_int());
    EXPECT_EQ(1, owner_.calls_for(3 * sizeof(Datum *)));
    EXPECT_EQ(1, owner_.calls_for(sizeof(Datum)));
    EXPECT_EQ(type, plan().get_native_parameter_type(2));
    ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(metadata, *plan().get_parameter_metadata(2)));
  }
}

TEST_F(NativeParameterSubstringIndex, preceding_argument_error_does_not_read_or_evaluate_later_alias)
{
  for (int64_t failing_index : {int64_t(0), int64_t(1)}) {
    SCOPED_TRACE(failing_index);
    ASSERT_NO_FATAL_FAILURE(configure());
    set_callbacks(failing_index);
    owner_.reject_all_ = true;
    Datum *value = nullptr;
    EXPECT_EQ(OB_ERR_UNEXPECTED, evaluate(value));
    ASSERT_NE(nullptr, value); EXPECT_TRUE(value->is_null());
    ASSERT_EQ(failing_index + 1, trace_.size());
    for (int64_t i = 0; i <= failing_index; ++i) { EXPECT_EQ(i, trace_[i]); }
    EXPECT_TRUE(owner_.sizes_.empty());
    EXPECT_EQ(nullptr, plan().locate_materialized_parameter(2));
    owner_.reject_all_ = false;
    trace_.clear(); set_callbacks(-1);
    for (int64_t i = 0; i < 2; ++i) { expressions_[i].get_eval_info(eval_).evaluated_ = false; }
    ASSERT_EQ(OB_SUCCESS, evaluate(value)); EXPECT_TRUE(value->is_null());
    ASSERT_EQ(2, trace_.size()); EXPECT_EQ(0, trace_[0]); EXPECT_EQ(1, trace_[1]);
    ASSERT_NE(nullptr, plan().locate_materialized_parameter(2));
    EXPECT_EQ(0, plan().locate_materialized_parameter(2)->get_int());
  }
}

class NativeParameterLike : public NativeParameterSubstringIndex
{
protected:
  void configure_like()
  {
    ASSERT_NO_FATAL_FAILURE(configure(true));
    SqlTypeMeta meta; meta.set_int(); meta.set_scale(0); meta.set_collation_type(CS_TYPE_BINARY);
    result().obj_meta_ = meta;
    result().datum_meta_ = ObDatumMeta(ObIntType, CS_TYPE_BINARY, 0, 1);
    ASSERT_EQ(OB_SUCCESS, result().prepare_scalar_type(meta, ObAccuracy(1, 0), request_arena_));
    result().type_ = T_OP_LIKE; result().extra_ = 0;
    result().eval_func_ = ObExprLike::like_varchar;
    ASSERT_EQ(ObVarcharType, plan().get_native_parameter_type(2)->kind());
    EXPECT_EQ(ObString::make_string("\\"), read_parameter_value(plan(), 2).get_string());
    EXPECT_EQ(nullptr, plan().locate_materialized_parameter(2));
  }
};

TEST_F(NativeParameterLike, preceding_argument_error_leaves_unmaterialized_escape_untouched)
{
  for (int64_t failing_index : {int64_t(0), int64_t(1)}) {
    SCOPED_TRACE(failing_index);
    ASSERT_NO_FATAL_FAILURE(configure_like());
    set_callbacks(failing_index);
    owner_.reject_all_ = true;
    Datum *value = nullptr;
    EXPECT_EQ(OB_ERR_UNEXPECTED, evaluate(value));
    ASSERT_NE(nullptr, value); EXPECT_TRUE(value->is_null());
    ASSERT_EQ(failing_index + 1, trace_.size());
    for (int64_t i = 0; i <= failing_index; ++i) { EXPECT_EQ(i, trace_[i]); }
    EXPECT_TRUE(owner_.sizes_.empty());
    EXPECT_EQ(nullptr, plan().locate_materialized_parameter(2));
    owner_.reject_all_ = false;
    trace_.clear(); set_callbacks(-1);
    for (int64_t i = 0; i < 2; ++i) { expressions_[i].get_eval_info(eval_).evaluated_ = false; }
    ASSERT_EQ(OB_SUCCESS, evaluate(value)); EXPECT_TRUE(value->is_null());
    ASSERT_EQ(2, trace_.size()); EXPECT_EQ(0, trace_[0]); EXPECT_EQ(1, trace_[1]);
    ASSERT_NE(nullptr, plan().locate_materialized_parameter(2));
    EXPECT_EQ(ObString::make_string("\\"), plan().locate_materialized_parameter(2)->get_string());
  }
}

TEST_F(NativeParameterLike, escape_projection_oom_returns_error_before_locate_and_retries)
{
  for (int64_t rejected : {int64_t(3 * sizeof(Datum *)), int64_t(sizeof(Datum))}) {
    SCOPED_TRACE(rejected);
    ASSERT_NO_FATAL_FAILURE(configure_like());
    const SqlType *type = plan().get_native_parameter_type(2);
    const DatumParameterMetadata metadata = *plan().get_parameter_metadata(2);
    ObEvalInfo *info = plan().get_parameter_eval_info(2);
    info->flag_ = 3; info->cnt_ = 17;
    owner_.fail_size_ = rejected;
    result().locate_expr_datum(eval_).set_int(71);
    Datum *value = nullptr;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, evaluate(value));
    ASSERT_NE(nullptr, value); EXPECT_TRUE(value->is_null());
    EXPECT_FALSE(result().get_eval_info(eval_).evaluated_);
    EXPECT_EQ(nullptr, plan().locate_materialized_parameter(2));
    EXPECT_EQ(type, plan().get_native_parameter_type(2));
    EXPECT_EQ(3, info->flag_); EXPECT_EQ(17, info->cnt_);
    ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(metadata, *plan().get_parameter_metadata(2)));
    EXPECT_EQ(ObString::make_string("\\"), read_parameter_value(plan(), 2).get_string());
    EXPECT_EQ(1, owner_.calls_for(3 * sizeof(Datum *)));
    EXPECT_EQ(rejected == sizeof(Datum) ? 1 : 0, owner_.calls_for(sizeof(Datum)));
    owner_.fail_size_ = 0; owner_.clear_calls();
    ASSERT_EQ(OB_SUCCESS, evaluate(value)); EXPECT_TRUE(value->is_null());
    const Datum *projection = plan().locate_materialized_parameter(2);
    ASSERT_NE(nullptr, projection);
    EXPECT_EQ(ObString::make_string("\\"), projection->get_string());
    EXPECT_EQ(1, owner_.calls_for(3 * sizeof(Datum *)));
    EXPECT_EQ(1, owner_.calls_for(sizeof(Datum)));
    EXPECT_EQ(type, plan().get_native_parameter_type(2));
    ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(metadata, *plan().get_parameter_metadata(2)));
    input(0).set_string(ObString::make_string("abc"));
    input(1).set_string(ObString::make_string("a%"));
    reset_result(); ASSERT_EQ(OB_SUCCESS, evaluate(value)); EXPECT_EQ(1, value->get_int());
    input(0).set_string(ObString::make_string("xyz"));
    reset_result(); ASSERT_EQ(OB_SUCCESS, evaluate(value)); EXPECT_EQ(0, value->get_int());
    EXPECT_EQ(projection, plan().locate_materialized_parameter(2));
  }
}
} // namespace

namespace
{
struct PLDescriptorAllocationException {};

// The production slice allocator supplies the charge and releases the block;
// this wrapper only records exact owned pointers and injects allocation faults.
class PLDescriptorAllocator final : public ObIAllocator
{
public:
  struct Allocation { void *pointer_; int64_t bytes_; int64_t slice_bytes_; bool live_; };
  PLDescriptorAllocator()
    : blocks_(), slices_(ObMemAttr("PlDescTest"), 2 * 1024 - 256, blocks_) {}
  ~PLDescriptorAllocator()
  {
    // Keep a failed/red test from leaking its test allocations. All ownership
    // and hold() oracles below run before this teardown cleanup.
    for (auto &allocation : allocations_) {
      if (allocation.live_) { slices_.free(allocation.pointer_); allocation.live_ = false; }
    }
  }
  void *alloc(int64_t size) override
  {
    const bool descriptor = size == DATUM_EXTEND_PAYLOAD_SIZE;
    if (descriptor) { ++descriptor_calls_; } else { ++other_calls_; }
    const bool record = size == oceanbase::pl::ObRecordType::get_init_size(1);
    if (record) { ++record_calls_; }
    if (record && record_calls_ == throw_record_call_) { throw PLDescriptorAllocationException(); }
    if (record && record_calls_ == fail_record_call_) { return nullptr; }
    if ((descriptor && descriptor_calls_ == throw_descriptor_call_)
        || (!descriptor && throw_other_)) { throw PLDescriptorAllocationException(); }
    if ((descriptor && descriptor_calls_ == fail_descriptor_call_)
        || (!descriptor && reject_other_)) { return nullptr; }
    void *pointer = slices_.alloc(size);
    if (pointer != nullptr) {
      const int64_t charge = (static_cast<ObBlockVSlicer::Item *>(pointer) - 1)->size_;
      allocations_.push_back({pointer, size, charge, true});
      live_charge_ += charge;
      if (live_charge_ > peak_charge_) { peak_charge_ = live_charge_; }
    }
    return pointer;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *pointer) override
  {
    if (pointer == nullptr) { return; }
    for (auto entry = allocations_.rbegin(); entry != allocations_.rend(); ++entry) {
      if (entry->pointer_ == pointer && entry->live_) {
        if (entry->bytes_ == DATUM_EXTEND_PAYLOAD_SIZE) { ++descriptor_frees_; }
        live_charge_ -= entry->slice_bytes_;
        entry->live_ = false;
        slices_.free(pointer);
        return;
      }
    }
    ++foreign_or_duplicate_frees_;
  }
  int64_t live_descriptors() const
  {
    int64_t count = 0;
    for (const auto &entry : allocations_) {
      count += entry.live_ && entry.bytes_ == DATUM_EXTEND_PAYLOAD_SIZE;
    }
    return count;
  }
  int64_t live_records() const
  {
    int64_t count = 0;
    for (const auto &entry : allocations_) {
      count += entry.live_ && entry.bytes_ == oceanbase::pl::ObRecordType::get_init_size(1);
    }
    return count;
  }
  int64_t hold() const { return blocks_.hold(); }
  ObBlockAllocMgr blocks_;
  ObVSliceAlloc slices_;
  std::vector<Allocation> allocations_;
  int64_t descriptor_calls_ = 0;
  int64_t descriptor_frees_ = 0;
  int64_t record_calls_ = 0;
  int64_t fail_record_call_ = 0;
  int64_t throw_record_call_ = 0;
  int64_t other_calls_ = 0;
  int64_t fail_descriptor_call_ = 0;
  int64_t throw_descriptor_call_ = 0;
  int64_t foreign_or_duplicate_frees_ = 0;
  int64_t live_charge_ = 0;
  int64_t peak_charge_ = 0;
  bool reject_other_ = false;
  bool throw_other_ = false;
};

class PLInitialDescriptors : public NativeExecutionParameters
{
protected:
  static constexpr uint64_t RECORD_ID = (uint64_t(1) << 50) | 83;
  PLInitialDescriptors() : inputs_(ObWrapperAllocator(&owner_)) {}
  void SetUp() override
  {
    NativeExecutionParameters::SetUp();
    ASSERT_EQ(OB_SUCCESS, oceanbase::share::ObSysVariables::init_default_values());
    ASSERT_EQ(OB_SUCCESS, session_.test_init(1, 1, nullptr));
    ASSERT_EQ(OB_SUCCESS, session_.load_default_sys_variable(false, false));
    exec_.set_my_session(&session_);
    ObSQLSessionInfo::ExecCtxSessionRegister registration(session_, &exec_);
    lib::ContextParam context_param;
    context_param.set_mem_attr("PlDescUnit").set_properties(lib::USE_TL_PAGE_OPTIONAL);
    ASSERT_EQ(OB_SUCCESS, CURRENT_CONTEXT->CREATE_CONTEXT(scope_.context_, context_param));
    sql_ctx_.schema_guard_ = &schema_guard_;
    exec_.set_sql_proxy(&sql_proxy_);
    exec_.set_package_guard(&package_guard_);
  }
  void TearDown() override
  {
    function_.reset();
    exec_.set_package_guard(nullptr);
    exec_.set_sql_proxy(nullptr);
    sql_ctx_.schema_guard_ = nullptr;
    ObSQLSessionInfo::ExecCtxSessionRegister unregister(session_, nullptr);
    exec_.set_my_session(nullptr);
  }
  void declare_records(int64_t arguments, int64_t locals = 0, bool in_arguments = true,
                       bool second_not_null = false)
  {
    function_.reset(new ObPLFunction(scope_.context_));
    inputs_.reuse();
    ObPLDataType declared(oceanbase::pl::PL_RECORD_TYPE);
    declared.set_user_type_id(oceanbase::pl::PL_RECORD_TYPE, RECORD_ID);
    ObPLSymbolTable symbols(owner_);
    for (int64_t i = 0; i < arguments + locals; ++i) {
      const std::string name = "record_" + std::to_string(i);
      ObString owned_name;
      ASSERT_EQ(OB_SUCCESS, ob_write_string(owner_, ObString(name.size(), name.data()), owned_name));
      ObPLDataType symbol_type = declared;
      if (second_not_null && i == 1) { symbol_type.set_not_null(true); }
      ASSERT_EQ(OB_SUCCESS, symbols.add_symbol(owned_name, symbol_type));
    }
    ASSERT_EQ(OB_SUCCESS, function_->set_variables(symbols));
    function_->set_arg_count(arguments);
    for (int64_t i = 0; i < arguments; ++i) {
      if (in_arguments) { ASSERT_EQ(OB_SUCCESS, function_->add_in_arg(i)); }
      ASSERT_EQ(OB_SUCCESS, function_->add_out_arg(i));
      void *memory = owner_.alloc(oceanbase::pl::ObRecordType::get_init_size(1));
      ASSERT_NE(nullptr, memory);
      auto *record = new (memory) oceanbase::pl::ObPLRecord(RECORD_ID, 1);
      ASSERT_EQ(OB_SUCCESS, record->init_data(owner_, false));
      ASSERT_EQ(OB_SUCCESS, record->get_element()[0].set_int(100 + i, owner_));
      record->get_element_type()[0].set_meta_type(record->get_element()[0].get_meta());
      record->get_element_type()[0].set_accuracy(ObAccuracy(19, 0));
      DatumParameter input;
      ASSERT_EQ(OB_SUCCESS, input.set_extend(reinterpret_cast<int64_t>(record),
          oceanbase::pl::PL_RECORD_TYPE, owner_, record->get_init_size()));
      input.set_udt_id(RECORD_ID); input.set_param_meta();
      input.set_raw_text_info(11 + i, 23 + i); input.set_result_flags(0x100 + i);
      input.set_need_to_check_type(false); input.set_need_to_check_extend_type(true);
      input.set_ignore_scale_check((i & 1) != 0);
      ASSERT_EQ(OB_SUCCESS, inputs_.push_back(input));
    }
  }
  void install_record_type()
  {
    oceanbase::pl::ObRecordType record_type;
    record_type.set_user_type_id(RECORD_ID);
    record_type.set_name(ObString::make_string("test_record"));
    ASSERT_EQ(OB_SUCCESS, record_type.record_members_init(&owner_, 1));
    ObPLDataType member(ObIntType);
    member.get_data_type()->set_accuracy(ObAccuracy(19, 0));
    ASSERT_EQ(OB_SUCCESS, record_type.add_record_member(ObString::make_string("v"), member));
    oceanbase::pl::ObPLUserTypeTable types;
    ASSERT_EQ(OB_SUCCESS, types.add_type(&record_type));
    ASSERT_EQ(OB_SUCCESS, function_->set_types(types));
  }
  void expect_borrowed_rows(const oceanbase::pl::ObPLExecState &state, int64_t count)
  {
    for (int64_t i = 0; i < count; ++i) {
      ASSERT_NO_FATAL_FAILURE(expect_metadata(inputs_.at(i), state.get_params().at(i)));
      EXPECT_EQ(inputs_.at(i).get_datum().is_inline_extended(),
          state.get_params().at(i).get_datum().is_inline_extended());
      if (!inputs_.at(i).get_datum().is_inline_extended()) {
        EXPECT_EQ(inputs_.at(i).get_datum().get_reference(),
            state.get_params().at(i).get_datum().get_reference());
      }
      EXPECT_EQ(inputs_.at(i).get_ext(), state.get_params().at(i).get_ext());
      auto *record = reinterpret_cast<oceanbase::pl::ObPLRecord *>(inputs_.at(i).get_ext());
      ASSERT_NE(nullptr, record);
      EXPECT_EQ(100 + i, record->get_element()[0].get_int());
    }
  }
  ObSQLSessionInfo session_;
  SpiUnitContext scope_;
  std::unique_ptr<ObPLFunction> function_;
  ParamStore inputs_;
  oceanbase::share::schema::ObSchemaGetterGuard schema_guard_;
  ObMySQLProxy sql_proxy_;
  oceanbase::pl::ObPLPackageGuard package_guard_;
};
constexpr uint64_t PLInitialDescriptors::RECORD_ID;

TEST_F(PLInitialDescriptors, trigger_row_binding_releases_only_initial_wrappers)
{
  ASSERT_NO_FATAL_FAILURE(declare_records(2));
  PLDescriptorAllocator callee;
  DatumParameter result; int status = OB_SUCCESS;
  oceanbase::pl::ObPLExecState state(owner_, callee, exec_, package_guard_, *function_, result, status);
  ASSERT_EQ(OB_SUCCESS, state.init_params(&inputs_, false));
  ASSERT_EQ(2, state.get_params().count());
  // Native OLD/NEW carriers no longer allocate either initial 15-byte wrapper.
  EXPECT_EQ(0, callee.descriptor_calls_); EXPECT_EQ(0, callee.descriptor_frees_);
  EXPECT_EQ(0, callee.peak_charge_);
  EXPECT_EQ(0, callee.live_charge_); EXPECT_EQ(0, callee.hold());
  EXPECT_EQ(0, callee.other_calls_); EXPECT_EQ(0, callee.foreign_or_duplicate_frees_);
  ASSERT_NO_FATAL_FAILURE(expect_borrowed_rows(state, 2));
  EXPECT_FALSE(state.need_free_arg(0)); EXPECT_FALSE(state.need_free_arg(1));
  EXPECT_EQ(OB_SUCCESS, state.final(OB_ERR_UNEXPECTED));
  ASSERT_NO_FATAL_FAILURE(expect_borrowed_rows(state, 2));
  EXPECT_EQ(0, callee.hold()); EXPECT_EQ(0, callee.foreign_or_duplicate_frees_);
}

TEST_F(PLInitialDescriptors, failed_initialization_and_tracking_growth_release_exact_owned_pointers)
{
  // The three removed 15-byte wrapper fault sites are negative controls now:
  // rejecting or throwing on such a request must not affect native binding.
  // The tracking directory still has a real growth/OOM boundary at slot9.
  enum Fault { FIRST_DESCRIPTOR, SECOND_DESCRIPTOR, THROW_SECOND, TRACKING_GROWTH,
               THROW_TRACKING_GROWTH, WRONG_ARGUMENT_COUNT };
  for (Fault fault : {FIRST_DESCRIPTOR, SECOND_DESCRIPTOR, THROW_SECOND,
                     TRACKING_GROWTH, THROW_TRACKING_GROWTH, WRONG_ARGUMENT_COUNT}) {
    SCOPED_TRACE(static_cast<int>(fault));
    const bool tracking = fault == TRACKING_GROWTH || fault == THROW_TRACKING_GROWTH;
    const bool failing = tracking || fault == WRONG_ARGUMENT_COUNT;
    ASSERT_NO_FATAL_FAILURE(declare_records(tracking ? 9 : 2));
    PLDescriptorAllocator callee;
    if (fault == FIRST_DESCRIPTOR) { callee.fail_descriptor_call_ = 1; }
    if (fault == SECOND_DESCRIPTOR) { callee.fail_descriptor_call_ = 2; }
    if (fault == THROW_SECOND) { callee.throw_descriptor_call_ = 2; }
    callee.reject_other_ = fault == TRACKING_GROWTH;
    callee.throw_other_ = fault == THROW_TRACKING_GROWTH;
    DatumParameter result; int status = OB_SUCCESS;
    oceanbase::pl::ObPLExecState state(owner_, callee, exec_, package_guard_, *function_, result, status);
    if (fault == THROW_TRACKING_GROWTH) {
      EXPECT_THROW(state.init_params(&inputs_, false), PLDescriptorAllocationException);
    } else {
      const int expected = tracking ? OB_ALLOCATE_MEMORY_FAILED
          : fault == WRONG_ARGUMENT_COUNT ? OB_ERR_SP_WRONG_ARG_NUM : OB_SUCCESS;
      EXPECT_EQ(expected, state.init_params(fault == WRONG_ARGUMENT_COUNT ? nullptr : &inputs_, false));
    }
    EXPECT_EQ(0, callee.descriptor_calls_); EXPECT_EQ(0, callee.descriptor_frees_);
    EXPECT_EQ(tracking ? 1 : 0, callee.other_calls_);
    EXPECT_EQ(0, callee.record_calls_); EXPECT_EQ(0, callee.live_descriptors());
    EXPECT_EQ(0, callee.live_charge_); EXPECT_EQ(0, callee.hold());
    EXPECT_EQ(0, callee.foreign_or_duplicate_frees_);
    if (failing) {
      // Keep the old observable failure contract, even though there is no
      // wrapper span to free: untouched initial values become SQL NULL.
      for (int64_t i = 0; i < state.get_params().count(); ++i) {
        EXPECT_TRUE(state.get_params().at(i).is_null());
        EXPECT_EQ(nullptr, state.get_params().at(i).get_datum().get_reference());
        EXPECT_EQ(RECORD_ID, state.get_params().at(i).get_udt_id());
        EXPECT_EQ(ObExtendType, state.get_params().at(i).get_param_meta().get_type());
      }
    } else {
      ASSERT_NO_FATAL_FAILURE(expect_borrowed_rows(state, inputs_.count()));
    }
    EXPECT_EQ(OB_SUCCESS, state.final(OB_ERR_UNEXPECTED));
    EXPECT_EQ(0, callee.hold()); EXPECT_EQ(0, callee.foreign_or_duplicate_frees_);
    // A fresh execution still binds the unchanged caller rows after every
    // actual growth/error path, without changing input lifetime or metadata.
    callee.fail_descriptor_call_ = 0; callee.throw_descriptor_call_ = 0;
    callee.reject_other_ = false; callee.throw_other_ = false;
    oceanbase::pl::ObPLExecState retry(owner_, callee, exec_, package_guard_, *function_, result, status);
    ASSERT_EQ(OB_SUCCESS, retry.init_params(&inputs_, false));
    ASSERT_NO_FATAL_FAILURE(expect_borrowed_rows(retry, inputs_.count()));
    EXPECT_EQ(0, callee.hold()); EXPECT_EQ(0, callee.foreign_or_duplicate_frees_);
    EXPECT_EQ(0, callee.descriptor_calls_); EXPECT_EQ(0, callee.descriptor_frees_);
    EXPECT_EQ(OB_SUCCESS, retry.final(OB_ERR_UNEXPECTED));
  }
}

TEST_F(PLInitialDescriptors, borrowed_zero_descriptor_and_unbound_local_keep_their_owners)
{
  {
    // A successful binding may have exactly the same inline carrier bits as the
    // initial zero placeholder. A later error must use origin/disarm, not byte
    // equality, to decide which slots are still untouched initial values.
    ASSERT_NO_FATAL_FAILURE(declare_records(2, 1, true, true));
    ASSERT_TRUE(function_->get_variables().at(1).is_not_null());
    ASSERT_EQ(OB_SUCCESS, function_->set_params_info(inputs_, true));
    ASSERT_FALSE(function_->get_params_info().at(0).flag_.need_to_check_type_);
    ASSERT_FALSE(function_->get_params_info().at(1).flag_.need_to_check_type_);
    ASSERT_EQ(OB_SUCCESS, inputs_.at(0).set_extend(0, oceanbase::pl::PL_RECORD_TYPE, owner_));
    inputs_.at(0).set_udt_id(RECORD_ID); inputs_.at(0).set_param_meta();
    const DatumParameter inline_borrowed = inputs_.at(0);
    ASSERT_TRUE(inline_borrowed.datum_.is_inline_extended());
    DatumParameter initial_zero;
    ASSERT_EQ(OB_SUCCESS, initial_zero.set_extend(0, oceanbase::pl::PL_RECORD_TYPE, owner_));
    EXPECT_EQ(0, MEMCMP(&initial_zero.datum_, &inline_borrowed.datum_, sizeof(Datum)));
    inputs_.at(1).set_null();
    PLDescriptorAllocator partial_owner;
    DatumParameter result; int status = OB_SUCCESS;
    oceanbase::pl::ObPLExecState partial(owner_, partial_owner, exec_, package_guard_, *function_, result, status);
    EXPECT_EQ(OB_INVALID_ARGUMENT, partial.init_params(&inputs_, true));
    ASSERT_EQ(3, partial.get_params().count());
    EXPECT_FALSE(partial.get_params().at(0).is_null());
    EXPECT_TRUE(partial.get_params().at(0).datum_.is_inline_extended());
    EXPECT_EQ(0, partial.get_params().at(0).get_ext());
    ASSERT_NO_FATAL_FAILURE(expect_metadata(inline_borrowed, partial.get_params().at(0)));
    EXPECT_TRUE(partial.get_params().at(1).is_null());
    EXPECT_TRUE(partial.get_params().at(2).is_null());
    EXPECT_EQ(RECORD_ID, partial.get_params().at(2).get_udt_id());
    EXPECT_EQ(ObExtendType, partial.get_params().at(2).get_param_meta().get_type());
    EXPECT_EQ(0, partial_owner.descriptor_calls_); EXPECT_EQ(0, partial_owner.other_calls_);
    EXPECT_EQ(0, partial_owner.hold()); EXPECT_EQ(0, partial_owner.foreign_or_duplicate_frees_);
    EXPECT_EQ(OB_SUCCESS, partial.final(OB_INVALID_ARGUMENT));
    EXPECT_FALSE(partial.get_params().at(0).is_null());
    EXPECT_TRUE(partial.get_params().at(0).datum_.is_inline_extended());
    ASSERT_NO_FATAL_FAILURE(expect_metadata(inline_borrowed, inputs_.at(0)));
    EXPECT_EQ(0, partial_owner.hold()); EXPECT_EQ(0, partial_owner.foreign_or_duplicate_frees_);
  }

  ASSERT_NO_FATAL_FAILURE(declare_records(2, 1));
  // Produce the anonymous cache metadata while its record addresses are valid,
  // then replace only the caller's descriptor with its legitimate zero value.
  ASSERT_EQ(OB_SUCCESS, function_->set_params_info(inputs_, true));
  ASSERT_FALSE(function_->get_params_info().at(0).flag_.need_to_check_type_);
  char borrowed_payload[DATUM_EXTEND_PAYLOAD_SIZE];
  ASSERT_EQ(OB_SUCCESS, inputs_.at(0).datum_.set_extend(0,
      oceanbase::pl::PL_RECORD_TYPE, 0, borrowed_payload, sizeof(borrowed_payload)));
  ASSERT_FALSE(inputs_.at(0).datum_.is_inline_extended());
  inputs_.at(0).set_udt_id(RECORD_ID); inputs_.at(0).set_param_meta();
  const DatumParameter borrowed_zero = inputs_.at(0);
  PLDescriptorAllocator callee;
  DatumParameter result; int status = OB_SUCCESS;
  oceanbase::pl::ObPLExecState state(owner_, callee, exec_, package_guard_, *function_, result, status);
  ASSERT_EQ(OB_SUCCESS, state.init_params(&inputs_, true));
  EXPECT_EQ(0, callee.descriptor_calls_); EXPECT_EQ(0, callee.descriptor_frees_);
  EXPECT_EQ(0, callee.live_descriptors()); EXPECT_EQ(0, callee.hold());
  EXPECT_EQ(borrowed_zero.get_datum().get_reference(), state.get_params().at(0).get_datum().get_reference());
  EXPECT_EQ(0, state.get_params().at(0).get_ext());
  ASSERT_NO_FATAL_FAILURE(expect_metadata(borrowed_zero, state.get_params().at(0)));
  ASSERT_TRUE(state.get_params().at(2).get_datum().has_ext_value());
  EXPECT_EQ(0, state.get_params().at(2).get_ext());
  EXPECT_EQ(RECORD_ID, state.get_params().at(2).get_udt_id());
  EXPECT_TRUE(state.get_params().at(2).datum_.is_inline_extended());
  // The unbound local is an inline zero carrier. final() must not free it or
  // the explicit foreign 15-byte OLD/NEW envelope.
  EXPECT_EQ(OB_SUCCESS, state.final(OB_ERR_UNEXPECTED));
  EXPECT_EQ(0, callee.descriptor_frees_); EXPECT_EQ(0, callee.hold());
  EXPECT_EQ(0, callee.foreign_or_duplicate_frees_);
  EXPECT_EQ(0, borrowed_zero.get_ext());
  ASSERT_NO_FATAL_FAILURE(expect_metadata(borrowed_zero, state.get_params().at(0)));
  EXPECT_EQ(borrowed_zero.get_datum().get_reference(), state.get_params().at(0).get_datum().get_reference());


}

TEST_F(PLInitialDescriptors, null_and_out_construction_release_replaced_placeholders_on_success_and_failure)
{
  // set_sql_proxy() is a no-op; the PL constructor reads GCTX through get_sql_proxy().
  // Restore the actual global even if a fatal assertion leaves this test early.
  struct SqlProxyGuard
  {
    explicit SqlProxyGuard(ObMySQLProxy &proxy) : saved_(GCTX.sql_proxy_)
    { GCTX.sql_proxy_ = &proxy; }
    ~SqlProxyGuard() { GCTX.sql_proxy_ = saved_; }
    ObMySQLProxy *saved_;
  };
  SqlProxyGuard proxy_guard(sql_proxy_);
  ASSERT_EQ(&sql_proxy_, exec_.get_sql_proxy());
  for (bool pure_out : {false, true}) {
    for (bool reject_replacement : {false, true}) {
      SCOPED_TRACE(pure_out);
      SCOPED_TRACE(reject_replacement);
      ASSERT_NO_FATAL_FAILURE(declare_records(2, 0, !pure_out));
      ASSERT_NO_FATAL_FAILURE(install_record_type());
      // NULL asks the actual PL constructor for a new record. For the IN case,
      // argument 0 is already borrowed when constructing argument 1 fails.
      if (pure_out) { inputs_.at(0).set_null(); }
      inputs_.at(1).set_null();
      PLDescriptorAllocator callee;
      // Fail the real pointee allocation, after argument0 if pureOUT. This
      // cannot accidentally become a no-op failure of a removed wrapper.
      if (reject_replacement) { callee.fail_record_call_ = pure_out ? 2 : 1; }
      DatumParameter result; int status = OB_SUCCESS;
      oceanbase::pl::ObPLExecState state(owner_, callee, exec_, package_guard_, *function_, result, status);
      oceanbase::pl::ObPLContext pl_context;
      ASSERT_EQ(OB_SUCCESS, pl_context.get_exec_stack().push_back(&state));
      PLQuestionMarkBindingGuard binding(exec_, session_, state, pl_context);
      const int ret = state.init_params(&inputs_, false);
      if (reject_replacement) {
        EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, ret);
        EXPECT_EQ(pure_out ? 2 : 1, callee.record_calls_);
        EXPECT_EQ(pure_out ? 1 : 0, callee.live_records());
        if (!pure_out) { EXPECT_EQ(0, callee.hold()); }
        if (!pure_out) { ASSERT_NO_FATAL_FAILURE(expect_borrowed_rows(state, 1)); }
      } else {
        ASSERT_EQ(OB_SUCCESS, ret);
        EXPECT_EQ(pure_out ? 2 : 1, callee.record_calls_);
        EXPECT_EQ(pure_out ? 2 : 1, callee.live_records());
        EXPECT_TRUE(state.get_params().at(1).datum_.is_inline_extended());
        EXPECT_TRUE(state.need_free_arg(1));
        auto *record = reinterpret_cast<oceanbase::pl::ObPLRecord *>(state.get_params().at(1).get_ext());
        ASSERT_NE(nullptr, record); EXPECT_EQ(1, record->get_count());
        EXPECT_TRUE(record->get_element()[0].is_null());
        EXPECT_EQ(RECORD_ID, state.get_params().at(1).get_udt_id());
        EXPECT_EQ(ObExtendType, state.get_params().at(1).get_param_meta().get_type());
      }
      EXPECT_EQ(0, callee.descriptor_calls_); EXPECT_EQ(0, callee.descriptor_frees_);
      EXPECT_EQ(0, callee.live_descriptors());
      EXPECT_EQ(OB_SUCCESS, state.final(OB_ERR_UNEXPECTED));
      EXPECT_EQ(0, callee.hold()); EXPECT_EQ(0, callee.live_descriptors());
      EXPECT_EQ(0, callee.foreign_or_duplicate_frees_);
      EXPECT_TRUE(inputs_.at(1).is_null());
      if (!pure_out) { ASSERT_NO_FATAL_FAILURE(expect_borrowed_rows(state, 1)); }
    }
  }
}
} // namespace

namespace
{
struct PLSetVarSqlProxyGuard
{
  explicit PLSetVarSqlProxyGuard(ObMySQLProxy &proxy) : saved_(GCTX.sql_proxy_)
  { GCTX.sql_proxy_ = &proxy; }
  ~PLSetVarSqlProxyGuard() { GCTX.sql_proxy_ = saved_; }
  ObMySQLProxy *saved_;
};

std::vector<size_t> pl_descriptor_indexes_since(const PLDescriptorAllocator &allocator, size_t begin)
{
  std::vector<size_t> indexes;
  for (size_t i = begin; i < allocator.allocations_.size(); ++i) {
    if (allocator.allocations_[i].bytes_ == DATUM_EXTEND_PAYLOAD_SIZE) { indexes.push_back(i); }
  }
  return indexes;
}

TEST_F(PLInitialDescriptors, set_var_null_reclaims_only_its_intermediate_descriptors)
{
  PLSetVarSqlProxyGuard proxy_guard(sql_proxy_);
  ASSERT_EQ(&sql_proxy_, exec_.get_sql_proxy());
  ASSERT_NO_FATAL_FAILURE(declare_records(1));
  ASSERT_NO_FATAL_FAILURE(install_record_type());
  inputs_.at(0).set_null();
  PLDescriptorAllocator callee;
  DatumParameter result; int status = OB_SUCCESS;
  oceanbase::pl::ObPLExecState state(owner_, callee, exec_, package_guard_, *function_, result, status);
  oceanbase::pl::ObPLContext pl_context;
  ASSERT_EQ(OB_SUCCESS, pl_context.get_exec_stack().push_back(&state));
  PLQuestionMarkBindingGuard binding(exec_, session_, state, pl_context);
  ASSERT_EQ(OB_SUCCESS, state.init_params(&inputs_, false));
  ASSERT_TRUE(state.need_free_arg(0)); ASSERT_EQ(1, callee.live_records());
  ASSERT_EQ(0, callee.live_descriptors());
  auto &slot = state.get_params().at(0);
  slot.set_raw_text_info(31, 47); slot.set_result_flags(0x527);
  slot.set_need_to_check_type(true); slot.set_need_to_check_extend_type(true);
  slot.set_ignore_scale_check(true);
  const DatumParameterMetadata metadata = slot.get_metadata();
  const int32_t value_length = slot.get_val_len();
  DatumParameter null_value;
  const DatumParameterMetadata null_metadata = null_value.get_metadata();
  for (int64_t round = 0; round < 2; ++round) {
    SCOPED_TRACE(round);
    auto *old_record = reinterpret_cast<oceanbase::pl::ObPLRecord *>(slot.get_ext());
    ASSERT_NE(nullptr, old_record); ASSERT_NE(nullptr, old_record->get_allocator());
    ASSERT_EQ(OB_SUCCESS, old_record->get_element()[0].set_int(900 + round, *old_record->get_allocator()));
    const size_t begin = callee.allocations_.size();
    const int64_t calls = callee.record_calls_;
    ASSERT_EQ(OB_SUCCESS, state.set_var(0, null_value));
    // B and C no longer have 15-byte heap envelopes. Only the newly constructed
    // record and its real allocator own memory; the old record is retired.
    EXPECT_TRUE(pl_descriptor_indexes_since(callee, begin).empty());
    EXPECT_EQ(calls + 1, callee.record_calls_);
    EXPECT_EQ(0, callee.descriptor_calls_); EXPECT_EQ(0, callee.descriptor_frees_);
    EXPECT_EQ(0, callee.live_descriptors()); EXPECT_EQ(1, callee.live_records());
    EXPECT_TRUE(slot.datum_.is_inline_extended());
    EXPECT_EQ(0, callee.foreign_or_duplicate_frees_);
    ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(metadata, slot.get_metadata()));
    EXPECT_EQ(value_length, slot.get_val_len());
    auto *record = reinterpret_cast<oceanbase::pl::ObPLRecord *>(slot.get_ext());
    ASSERT_NE(nullptr, record); ASSERT_EQ(1, record->get_count());
    EXPECT_TRUE(record->get_element()[0].is_null());
    EXPECT_TRUE(null_value.is_null());
    ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(null_metadata, null_value.get_metadata()));
  }
  EXPECT_EQ(OB_SUCCESS, state.final(OB_ERR_UNEXPECTED));
  EXPECT_EQ(0, callee.live_records()); EXPECT_EQ(0, callee.live_descriptors());
  EXPECT_EQ(0, callee.live_charge_); EXPECT_EQ(0, callee.hold());
  EXPECT_EQ(0, callee.foreign_or_duplicate_frees_); EXPECT_TRUE(inputs_.at(0).is_null());
}

TEST_F(PLInitialDescriptors, set_var_null_allocation_failures_preserve_original_publication_and_cleanup)
{
  PLSetVarSqlProxyGuard proxy_guard(sql_proxy_);
  ASSERT_EQ(&sql_proxy_, exec_.get_sql_proxy());
  // Removed B/C carrier allocations cannot fail. Keep the actual remaining
  // record allocation's OOM/exception boundary and an earlier first-error control.
  enum Fault { BAD_INDEX, POINTEE_OOM, POINTEE_THROW };
  for (Fault fault : {BAD_INDEX, POINTEE_OOM, POINTEE_THROW}) {
    SCOPED_TRACE(static_cast<int>(fault));
    ASSERT_NO_FATAL_FAILURE(declare_records(1));
    ASSERT_NO_FATAL_FAILURE(install_record_type());
    inputs_.at(0).set_null();
    PLDescriptorAllocator callee;
    DatumParameter result; int status = OB_SUCCESS;
    oceanbase::pl::ObPLExecState state(owner_, callee, exec_, package_guard_, *function_, result, status);
    oceanbase::pl::ObPLContext pl_context;
    ASSERT_EQ(OB_SUCCESS, pl_context.get_exec_stack().push_back(&state));
    PLQuestionMarkBindingGuard binding(exec_, session_, state, pl_context);
    ASSERT_EQ(OB_SUCCESS, state.init_params(&inputs_, false));
    ASSERT_TRUE(state.need_free_arg(0));
    auto &slot = state.get_params().at(0);
    slot.set_raw_text_info(53, 79); slot.set_result_flags(0x735);
    slot.set_need_to_check_type(true); slot.set_need_to_check_extend_type(true);
    slot.set_ignore_scale_check(true);
    const DatumParameterMetadata metadata = slot.get_metadata();
    const int32_t value_length = slot.get_val_len();
    const Datum old_value = slot.datum_;
    const int64_t old_address = slot.get_ext();
    auto *old_record = reinterpret_cast<oceanbase::pl::ObPLRecord *>(old_address);
    ASSERT_NE(nullptr, old_record); ASSERT_NE(nullptr, old_record->get_allocator());
    ASSERT_EQ(OB_SUCCESS, old_record->get_element()[0].set_int(931, *old_record->get_allocator()));
    const int64_t charge = callee.live_charge_, calls = callee.record_calls_;
    if (fault == POINTEE_THROW) { callee.throw_record_call_ = calls + 1; }
    if (fault == POINTEE_OOM) { callee.fail_record_call_ = calls + 1; }
    DatumParameter null_value;
    if (fault == POINTEE_THROW) {
      EXPECT_THROW(state.set_var(0, null_value), PLDescriptorAllocationException);
    } else if (fault == POINTEE_OOM) {
      EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, state.set_var(0, null_value));
    } else {
      EXPECT_EQ(OB_ERR_UNEXPECTED, state.set_var(1, null_value));
    }
    ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(metadata, slot.get_metadata()));
    EXPECT_EQ(value_length, slot.get_val_len()); EXPECT_TRUE(state.need_free_arg(0));
    EXPECT_EQ(0, callee.descriptor_calls_); EXPECT_EQ(0, callee.descriptor_frees_);
    EXPECT_EQ(0, callee.live_descriptors());
    if (fault == BAD_INDEX) {
      EXPECT_EQ(calls, callee.record_calls_); EXPECT_EQ(charge, callee.live_charge_);
      EXPECT_EQ(0, MEMCMP(&old_value, &slot.datum_, sizeof(old_value)));
      EXPECT_EQ(old_address, slot.get_ext()); EXPECT_EQ(931, old_record->get_element()[0].get_int());
      EXPECT_EQ(1, callee.live_records());
    } else {
      EXPECT_EQ(calls + 1, callee.record_calls_); // Real ObPLRecord allocation was reached.
      EXPECT_TRUE(slot.datum_.is_inline_extended()); EXPECT_EQ(0, slot.get_ext());
      EXPECT_EQ(0, callee.live_records()); EXPECT_EQ(0, callee.live_charge_); EXPECT_EQ(0, callee.hold());
    }
    EXPECT_TRUE(null_value.is_null()); EXPECT_EQ(0, callee.foreign_or_duplicate_frees_);
    // Retry the same state, including the published zero B after a failed C.
    // This used to retain an owned zero wrapper from the prior invocation.
    callee.fail_record_call_ = 0; callee.throw_record_call_ = 0;
    ASSERT_EQ(OB_SUCCESS, state.set_var(0, null_value));
    EXPECT_TRUE(slot.datum_.is_inline_extended()); EXPECT_NE(0, slot.get_ext());
    EXPECT_EQ(1, callee.live_records()); EXPECT_EQ(0, callee.live_descriptors());
    ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(metadata, slot.get_metadata()));
    EXPECT_EQ(OB_SUCCESS, state.final(OB_ERR_UNEXPECTED));
    EXPECT_EQ(0, callee.live_records()); EXPECT_EQ(0, callee.live_charge_);
    EXPECT_EQ(0, callee.hold()); EXPECT_EQ(0, callee.foreign_or_duplicate_frees_);
  }
}

TEST_F(PLInitialDescriptors, set_var_null_never_claims_a_borrowed_zero_descriptor)
{
  PLSetVarSqlProxyGuard proxy_guard(sql_proxy_);
  ASSERT_EQ(&sql_proxy_, exec_.get_sql_proxy());
  ASSERT_NO_FATAL_FAILURE(declare_records(1));
  ASSERT_NO_FATAL_FAILURE(install_record_type());
  ASSERT_EQ(OB_SUCCESS, function_->set_params_info(inputs_, true));
  ASSERT_FALSE(function_->get_params_info().at(0).flag_.need_to_check_type_);
  // Explicit old Reference carrier: a native set_extend() would now be inline
  // and would no longer exercise protection of a foreign 15-byte envelope.
  char borrowed_payload[DATUM_EXTEND_PAYLOAD_SIZE];
  ASSERT_EQ(OB_SUCCESS, inputs_.at(0).datum_.set_extend(0,
      oceanbase::pl::PL_RECORD_TYPE, 0, borrowed_payload, sizeof(borrowed_payload)));
  inputs_.at(0).set_udt_id(RECORD_ID); inputs_.at(0).set_param_meta();
  const DatumParameter borrowed = inputs_.at(0);
  ASSERT_FALSE(borrowed.datum_.is_inline_extended());
  PLDescriptorAllocator callee;
  DatumParameter result; int status = OB_SUCCESS;
  oceanbase::pl::ObPLExecState state(owner_, callee, exec_, package_guard_, *function_, result, status);
  oceanbase::pl::ObPLContext pl_context;
  ASSERT_EQ(OB_SUCCESS, pl_context.get_exec_stack().push_back(&state));
  PLQuestionMarkBindingGuard binding(exec_, session_, state, pl_context);
  ASSERT_EQ(OB_SUCCESS, state.init_params(&inputs_, true));
  EXPECT_FALSE(state.need_free_arg(0));
  EXPECT_EQ(0, callee.live_descriptors()); EXPECT_EQ(0, callee.hold());
  auto &slot = state.get_params().at(0);
  ASSERT_EQ(borrowed_payload, slot.datum_.get_reference());
  ASSERT_FALSE(slot.datum_.is_inline_extended());
  DatumParameter null_value;
  const int64_t calls = callee.record_calls_;
  callee.fail_record_call_ = calls + 1;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, state.set_var(0, null_value));
  EXPECT_EQ(calls + 1, callee.record_calls_);
  EXPECT_EQ(borrowed_payload, slot.datum_.get_reference()); EXPECT_FALSE(slot.datum_.is_inline_extended());
  EXPECT_EQ(0, slot.get_ext()); ASSERT_NO_FATAL_FAILURE(expect_metadata(borrowed, slot));
  EXPECT_EQ(0, callee.live_records()); EXPECT_EQ(0, callee.live_descriptors()); EXPECT_EQ(0, callee.hold());
  EXPECT_EQ(0, callee.foreign_or_duplicate_frees_);
  callee.fail_record_call_ = 0;
  ASSERT_EQ(OB_SUCCESS, state.set_var(0, null_value));
  EXPECT_TRUE(slot.datum_.is_inline_extended()); EXPECT_NE(0, slot.get_ext());
  EXPECT_EQ(1, callee.live_records()); EXPECT_EQ(0, callee.live_descriptors());
  EXPECT_EQ(0, callee.descriptor_calls_); EXPECT_EQ(0, callee.descriptor_frees_);
  EXPECT_FALSE(state.need_free_arg(0)); // Existing borrowed-formal policy is unchanged.
  ASSERT_NO_FATAL_FAILURE(expect_resolver_attributes(borrowed.get_metadata(), slot.get_metadata()));
  EXPECT_EQ(borrowed_payload, inputs_.at(0).datum_.get_reference());
  EXPECT_FALSE(inputs_.at(0).datum_.is_inline_extended()); EXPECT_EQ(0, inputs_.at(0).get_ext());
  ASSERT_NO_FATAL_FAILURE(expect_metadata(borrowed, inputs_.at(0)));
  EXPECT_EQ(0, callee.foreign_or_duplicate_frees_);
  // Only C's known new pointee is owned here; never free the borrowed envelope.
  ASSERT_EQ(OB_SUCCESS, oceanbase::pl::ObUserDefinedType::destruct_objparam(callee, slot, &session_));
  EXPECT_EQ(OB_SUCCESS, state.final(OB_ERR_UNEXPECTED));
  EXPECT_EQ(0, callee.live_records()); EXPECT_EQ(0, callee.live_charge_);
  EXPECT_EQ(0, callee.hold()); EXPECT_EQ(0, callee.foreign_or_duplicate_frees_);
  EXPECT_EQ(0, inputs_.at(0).get_ext());
  ASSERT_NO_FATAL_FAILURE(expect_metadata(borrowed, inputs_.at(0)));
}

} // namespace


TEST_F(PLInitialDescriptors, subprogram_evaluator_keeps_inline_record_carrier_after_return_and_rejects_invalid_slot)
{
  ASSERT_NO_FATAL_FAILURE(declare_records(1));
  constexpr uint64_t package_id = 1807;
  constexpr uint64_t routine_id = 1809;
  function_->set_package_id(package_id);
  function_->set_routine_id(routine_id);
  // Resolve the actual record declaration through the live PL namespace;
  // neither the expression Type nor its carrier layout is fabricated.
  oceanbase::pl::ObRecordType record_type;
  record_type.set_user_type_id(RECORD_ID);
  record_type.set_name(ObString::make_string("subprogram_record"));
  ASSERT_EQ(OB_SUCCESS, record_type.record_members_init(&owner_, 1));
  ObPLDataType member(ObIntType);
  member.get_data_type()->set_accuracy(ObAccuracy(19, 0));
  ASSERT_EQ(OB_SUCCESS, record_type.add_record_member(ObString::make_string("v"), member));
  SqlPLDeclarationIdentity declaration;
  declaration.source = SqlPLDeclarationSource::Local;
  declaration.tenant_id = OB_SERVER_RUNTIME_ID;
  declaration.declaration_id = RECORD_ID;
  declaration.compilation_scope.low = 0x18091807;
  ASSERT_EQ(OB_SUCCESS, record_type.set_sql_declaration_identity(declaration, SqlNominalType()));
  oceanbase::pl::ObPLUserTypeTable types;
  ASSERT_EQ(OB_SUCCESS, types.add_type(&record_type));
  ASSERT_EQ(OB_SUCCESS, function_->set_types(types));

  PLDescriptorAllocator callee;
  DatumParameter function_result; int function_status = OB_SUCCESS;
  oceanbase::pl::ObPLExecState state(owner_, callee, exec_, package_guard_, *function_,
      function_result, function_status);
  oceanbase::pl::ObPLContext pl_context;
  ASSERT_EQ(OB_SUCCESS, pl_context.get_exec_stack().push_back(&state));
  ObSQLSessionInfo::ExecCtxSessionRegister registration(session_, &exec_);
  PLQuestionMarkBindingGuard binding(exec_, session_, state, pl_context);
  ASSERT_EQ(OB_SUCCESS, state.init_params(&inputs_, false));
  ASSERT_EQ(1, state.get_params().count());
  ASSERT_TRUE(state.get_params().at(0).datum_.is_inline_extended());
  ASSERT_NO_FATAL_FAILURE(expect_borrowed_rows(state, 1));
  auto *source = reinterpret_cast<oceanbase::pl::ObPLRecord *>(inputs_.at(0).get_ext());
  ASSERT_NE(nullptr, source);
  const DatumParameter original_binding = state.get_params().at(0);

  constexpr int64_t block = 256;
  alignas(8) std::array<char, 5 * block> frame = {};
  char *frames[] = {frame.data()};
  exec_.set_frames(frames); exec_.set_frame_cnt(1);
  ObEvalCtx evaluation(exec_);
  ObExpr arguments[4];
  ObExpr *argument_ptrs[] = {&arguments[0], &arguments[1], &arguments[2], &arguments[3]};
  for (int64_t i = 0; i < 4; ++i) {
    ObExpr &argument = arguments[i];
    argument.type_ = T_INT;
    argument.frame_idx_ = 0; argument.datum_off_ = i * block;
    argument.eval_info_off_ = i * block + 32;
    new (frame.data() + argument.datum_off_) Datum();
    new (frame.data() + argument.eval_info_off_) ObEvalInfo{};
    SqlTypeMeta integer_meta; integer_meta.set_int();
    argument.obj_meta_ = integer_meta;
    argument.datum_meta_ = ObDatumMeta(ObIntType, CS_TYPE_BINARY, 0, 20);
    ASSERT_EQ(OB_SUCCESS, argument.prepare_scalar_type(integer_meta, ObAccuracy(20, 0), owner_));
    argument.get_eval_info(evaluation).evaluated_ = true;
  }
  arguments[0].locate_expr_datum(evaluation).set_int(package_id);
  arguments[1].locate_expr_datum(evaluation).set_int(routine_id);
  arguments[2].locate_expr_datum(evaluation).set_int(0);
  arguments[3].locate_expr_datum(evaluation).set_int(0); // Runtime placeholder, never evaluated.
  ObExpr expression;
  expression.type_ = T_OP_GET_SUBPROGRAM_VAR;
  expression.args_ = argument_ptrs; expression.arg_cnt_ = 4;
  expression.frame_idx_ = 0; expression.datum_off_ = 4 * block;
  expression.eval_info_off_ = 4 * block + 32;
  expression.dyn_buf_header_offset_ = 4 * block + 96;
  expression.res_buf_off_ = 4 * block + 128; expression.res_buf_len_ = 128;
  new (frame.data() + expression.datum_off_) Datum();
  new (frame.data() + expression.eval_info_off_) ObEvalInfo{};
  auto *dynamic = new (frame.data() + expression.dyn_buf_header_offset_) ObDynReserveBuf();
  dynamic->magic_ = ObDynReserveBuf::MAGIC_NUM;
  dynamic->len_ = 128; dynamic->mem_ = frame.data() + expression.res_buf_off_;
  ObRawExprResType resolved;
  resolved.set_type(ObExtendType);
  resolved.set_extend_type(oceanbase::pl::PL_RECORD_TYPE);
  resolved.set_udt_id(RECORD_ID);
  expression.obj_meta_ = resolved.get_sql_type_meta();
  expression.datum_meta_ = ObDatumMeta(ObExtendType, CS_TYPE_BINARY, 0);
  ASSERT_EQ(OB_SUCCESS, expression.prepare_resolved_type(resolved, &session_, owner_,
      &schema_guard_, &state.get_exec_ctx()));
  const SqlType *prepared_type = &expression.get_sql_type();
  ASSERT_EQ(SqlTypeForm::Record, prepared_type->form());
  ASSERT_EQ(1, prepared_type->member_count());
  const SqlTypeMeta expression_meta = expression.obj_meta_;

  Datum output;
  owner_.clear_calls();
  ASSERT_EQ(OB_SUCCESS,
      ObExprGetSubprogramVar::calc_get_subprogram_var(expression, evaluation, output));
  // Inspect after the real evaluator returned: no result may point into its
  // export scratch, while the PL resource itself still belongs to the caller.
  ASSERT_TRUE(output.is_inline_extended());
  ASSERT_TRUE(expression.locate_expr_datum(evaluation).is_inline_extended());
  EXPECT_EQ(reinterpret_cast<int64_t>(source), output.get_ext());
  EXPECT_EQ(output.get_ext(), expression.locate_expr_datum(evaluation).get_ext());
  EXPECT_EQ(source->get_init_size(), output.get_extend_size());
  EXPECT_EQ(oceanbase::pl::PL_RECORD_TYPE, output.get_extend_type());
  EXPECT_EQ(0, owner_.calls_for(DATUM_EXTEND_PAYLOAD_SIZE));
  EXPECT_EQ(0, callee.descriptor_calls_);
  ASSERT_EQ(OB_SUCCESS, source->get_element()[0].set_int(INT64_MIN, owner_));
  EXPECT_EQ(INT64_MIN, reinterpret_cast<oceanbase::pl::ObPLRecord *>(output.get_ext())
      ->get_element()[0].get_int());
  EXPECT_EQ(INT64_MIN, reinterpret_cast<oceanbase::pl::ObPLRecord *>(
      expression.locate_expr_datum(evaluation).get_ext())->get_element()[0].get_int());
  for (int64_t invalid_index : {-1, 1}) {
    SCOPED_TRACE(invalid_index);
    arguments[2].locate_expr_datum(evaluation).set_int(invalid_index);
    output.set_int(951);
    const Datum old_slot = expression.locate_expr_datum(evaluation);
    EXPECT_EQ(OB_ERR_UNEXPECTED,
        ObExprGetSubprogramVar::calc_get_subprogram_var(expression, evaluation, output));
    EXPECT_EQ(951, output.get_int());
    EXPECT_EQ(0, MEMCMP(&old_slot, &expression.locate_expr_datum(evaluation), sizeof(Datum)));
    EXPECT_EQ(expression_meta, expression.obj_meta_);
    EXPECT_EQ(prepared_type, &expression.get_sql_type());
    ASSERT_NO_FATAL_FAILURE(expect_metadata(original_binding, state.get_params().at(0)));
  }
  EXPECT_FALSE(state.need_free_arg(0));
  EXPECT_EQ(OB_SUCCESS, state.final(OB_ERR_UNEXPECTED));
  EXPECT_EQ(INT64_MIN, source->get_element()[0].get_int());
  EXPECT_EQ(0, callee.hold());
  EXPECT_EQ(0, callee.foreign_or_duplicate_frees_);
}
