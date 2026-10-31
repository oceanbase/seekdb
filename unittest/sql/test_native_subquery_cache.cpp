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

#include "common/datum/datum_parameter.h"
#include "common/datum/datum_parameter_array.h"
#include "lib/charset/ob_charset.h"
#include "sql/engine/expr/ob_expr_subquery_ref.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/subquery/ob_subplan_filter_op.h"
#include <gtest/gtest.h>
#include <array>
#include <cstring>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
class ScalarChild final : public ObOperator
{
public:
  ScalarChild(ObExecContext &ctx, const ObOpSpec &spec) : ObOperator(ctx, spec, nullptr) {}
  void destroy() override { ObOperator::destroy(); }
  int rescan() override { calls_ = 0; return OB_SUCCESS; }
  int get_next_row() override
  {
    ++calls_;
    if (calls_ > 1 && string_result_) { MEMSET(string_bytes_, 'x', sizeof(string_bytes_) - 1); }
    if (calls_ > 1 && second_result_ != OB_SUCCESS) { return second_result_; }
    ObExpr &expr = *get_spec().output_.at(0);
    if (string_result_) {
      MEMCPY(string_bytes_, "retained first row", sizeof(string_bytes_));
      if (expr.locate_expr_datum(get_eval_ctx()).set_string(string_bytes_, sizeof(string_bytes_) - 1)
          != OB_SUCCESS) { return OB_ERR_UNEXPECTED; }
    } else { expr.locate_expr_datum(get_eval_ctx()).set_int(71 + calls_); }
    expr.set_evaluated_projected(get_eval_ctx());
    return OB_SUCCESS;
  }
  int inner_get_next_row() override { return get_next_row(); }
  int calls_ = 0;
  int second_result_ = OB_ITER_END;
  bool string_result_ = false;
  char string_bytes_[sizeof("retained first row")]{};
};

class NativeSubqueryCache : public ::testing::Test
{
protected:
  NativeSubqueryCache()
    : exec_(owner_), child_spec_(owner_, PHY_EXPR_VALUES),
      parent_spec_(owner_, PHY_SUBPLAN_FILTER), child_(exec_, child_spec_),
      parent_(exec_, parent_spec_, nullptr), iterator_(child_),
      extra_info_(owner_, T_REF_QUERY) {}

  void SetUp() override
  {
    frame_.fill(0); frames_[0] = frame_.data(); exec_.set_frames(frames_);
    child_.get_eval_ctx().frames_ = frames_;
    parent_.get_eval_ctx().frames_ = frames_;
    init_expr(key_expr_, 0); init_expr(child_expr_, 64); init_expr(result_expr_, 128);
    ASSERT_EQ(OB_SUCCESS, exec_.create_physical_plan_ctx());
    ASSERT_EQ(OB_SUCCESS, exec_.get_kit_store().init(owner_, 1));
    exec_.get_operator_kit(0)->op_ = &parent_;
    ASSERT_EQ(OB_SUCCESS, child_spec_.output_.init(1));
    ASSERT_EQ(OB_SUCCESS, child_spec_.output_.push_back(&child_expr_));
    ExprFixedArray parameters(owner_);
    ASSERT_EQ(OB_SUCCESS, parameters.init(1));
    ASSERT_EQ(OB_SUCCESS, parameters.push_back(&key_expr_));
    ASSERT_EQ(OB_SUCCESS, parent_spec_.exec_param_array_.init(1));
    ASSERT_EQ(OB_SUCCESS, parent_spec_.exec_param_array_.push_back(parameters));
    auto &iterators = const_cast<ObIArray<ObSubQueryIterator *> &>(parent_.get_subplan_iters());
    ASSERT_EQ(OB_SUCCESS, iterators.push_back(&iterator_));
    iterator_.set_parent(&parent_); iterator_.set_iter_id(1);
    ASSERT_EQ(OB_SUCCESS, iterator_.init_mem_entity());
    ASSERT_EQ(OB_SUCCESS, iterator_.init_hashmap(16));
    ASSERT_EQ(OB_SUCCESS, iterator_.init_probe_row(1));
    key().set_int(17); key_expr_.set_evaluated_projected(child_.get_eval_ctx());
    ASSERT_EQ(OB_SUCCESS, iterator_.get_curr_probe_row());
    auto &extra = ObExprSubQueryRef::Extra::get_info(result_expr_);
    extra.op_id_ = 0; extra.iter_idx_ = 0; extra.is_scalar_ = true;
    result_expr_.extra_info_ = &extra_info_;
  }

  void TearDown() override
  {
    // These operators/iterator live in this fixture rather than the plan arena.
    auto &iterators = const_cast<ObIArray<ObSubQueryIterator *> &>(parent_.get_subplan_iters());
    while (!iterators.empty()) { iterators.pop_back(); }
    if (exec_.get_operator_kit(0) != nullptr) { exec_.get_operator_kit(0)->op_ = nullptr; }
  }

  void init_expr(ObExpr &expr, int64_t offset)
  {
    expr.frame_idx_ = 0; expr.datum_off_ = offset;
    expr.eval_info_off_ = offset + sizeof(Datum); expr.type_ = T_REF_COLUMN;
    expr.obj_meta_.set_int(); expr.datum_meta_.type_ = ObIntType;
    expr.obj_datum_map_ = OBJ_DATUM_8BYTE_DATA;
    new (frame_.data() + offset) Datum();
    new (frame_.data() + expr.eval_info_off_) ObEvalInfo();
    ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, ObAccuracy(20, 0), owner_));
  }

  void string_type(ObExpr &expr)
  {
    SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ObAccuracy accuracy; accuracy.set_length(128);
    expr.obj_meta_ = meta; expr.datum_meta_.type_ = ObVarcharType;
    expr.datum_meta_.cs_type_ = CS_TYPE_UTF8MB4_BIN; expr.obj_datum_map_ = OBJ_DATUM_STRING;
    ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(meta, accuracy, owner_));
  }
  Datum &key() { return key_expr_.locate_expr_datum(child_.get_eval_ctx()); }
  Datum &result() { return result_expr_.locate_expr_datum(child_.get_eval_ctx()); }
  int lookup(Datum &out) { return iterator_.get_refactored(out, result_expr_); }

  ObArenaAllocator owner_;
  ObExecContext exec_;
  ObOpSpec child_spec_;
  ObSubPlanFilterSpec parent_spec_;
  ScalarChild child_;
  ObSubPlanFilterOp parent_;
  ObSubQueryIterator iterator_;
  ObExpr key_expr_, child_expr_, result_expr_;
  ObExprSubQueryRef::ExtraInfo extra_info_;
  alignas(8) std::array<char, 512> frame_;
  char *frames_[1] = {nullptr};
};

TEST_F(NativeSubqueryCache, pending_entry_is_unpublished_and_owns_values_and_type_metadata)
{
  string_type(key_expr_); string_type(result_expr_);
  char key_bytes[] = "original string key with external bytes";
  char value_bytes[] = "original scalar subquery string result";
  ASSERT_EQ(OB_SUCCESS, key().set_string(ObString::make_string(key_bytes)));
  ASSERT_EQ(OB_SUCCESS, result().set_string(ObString::make_string(value_bytes)));
  ASSERT_EQ(OB_SUCCESS, iterator_.get_curr_probe_row());
  ObSubQueryIterator::PendingCacheEntry pending;
  ASSERT_EQ(OB_SUCCESS, iterator_.prepare_cache_result(result(), result_expr_, pending));
  ASSERT_TRUE(pending.ready_);
  EXPECT_NE(&key_expr_.get_sql_type(), pending.key_.types_[0]);
  EXPECT_NE(&result_expr_.get_sql_type(), pending.result_type_);
  EXPECT_TRUE(pending.result_type_->equals(result_expr_.get_sql_type()));
  Datum out; out.set_int(991);
  EXPECT_EQ(OB_HASH_NOT_EXIST, lookup(out)); EXPECT_EQ(991, out.get_int());
  std::memset(key_bytes, 'x', sizeof(key_bytes) - 1);
  std::memset(value_bytes, 'y', sizeof(value_bytes) - 1);
  ASSERT_EQ(OB_SUCCESS, key().set_string(ObString::make_string("original string key with external bytes")));
  ASSERT_EQ(OB_SUCCESS, iterator_.get_curr_probe_row());
  ASSERT_EQ(OB_SUCCESS, iterator_.publish_cache_result(pending));
  EXPECT_FALSE(pending.ready_);
  ASSERT_EQ(OB_SUCCESS, lookup(out));
  EXPECT_EQ(ObString::make_string("original scalar subquery string result"), out.get_string());
  // The cached key/result types do not follow a later runtime Expr rebind.
  SqlTypeMeta integer; integer.set_int();
  ASSERT_EQ(OB_SUCCESS, result_expr_.prepare_scalar_type(integer, ObAccuracy(20, 0), owner_));
  EXPECT_EQ(ObVarcharType, pending.result_type_->kind());
  EXPECT_EQ(OB_HASH_NOT_EXIST, lookup(out));
  ASSERT_EQ(OB_SUCCESS, key_expr_.prepare_scalar_type(integer, ObAccuracy(20, 0), owner_));
  EXPECT_EQ(ObVarcharType, pending.key_.types_[0]->kind());
  key().set_int(17); ASSERT_EQ(OB_SUCCESS, iterator_.get_curr_probe_row());
  EXPECT_EQ(OB_HASH_NOT_EXIST, lookup(out));
}

TEST_F(NativeSubqueryCache, failed_prepare_cannot_publish_or_replace_a_result)
{
  Datum invalid; ASSERT_EQ(OB_SUCCESS, invalid.set_string("bad", 3));
  ObSubQueryIterator::PendingCacheEntry pending;
  EXPECT_NE(OB_SUCCESS, iterator_.prepare_cache_result(invalid, result_expr_, pending));
  EXPECT_FALSE(pending.ready_);
  ASSERT_EQ(OB_SUCCESS, iterator_.publish_cache_result(pending));
  Datum out; out.set_int(993);
  EXPECT_EQ(OB_HASH_NOT_EXIST, lookup(out)); EXPECT_EQ(993, out.get_int());
  result().set_int(81);
  ASSERT_EQ(OB_SUCCESS, iterator_.cache_result(result(), result_expr_));
  EXPECT_NE(OB_SUCCESS, iterator_.prepare_cache_result(invalid, result_expr_, pending));
  ASSERT_EQ(OB_SUCCESS, lookup(out)); EXPECT_EQ(81, out.get_int());
}

TEST_F(NativeSubqueryCache, actual_scalar_evaluator_does_not_publish_on_second_row_or_child_error)
{
  for (int second : {OB_SUCCESS, OB_ERR_UNEXPECTED}) {
    ASSERT_EQ(OB_SUCCESS, iterator_.reset_hash_map());
    child_.second_result_ = second;
    int ret = ObExprSubQueryRef::expr_eval(result_expr_, child_.get_eval_ctx(), result());
    EXPECT_EQ(second == OB_SUCCESS ? OB_SUBQUERY_TOO_MANY_ROW : second, ret);
    EXPECT_EQ(2, child_.calls_);
    Datum out;
    EXPECT_EQ(OB_HASH_NOT_EXIST, lookup(out));
  }
}

TEST_F(NativeSubqueryCache, actual_scalar_evaluator_publishes_only_after_successful_end)
{
  child_.second_result_ = OB_ITER_END;
  ASSERT_EQ(OB_SUCCESS, ObExprSubQueryRef::expr_eval(result_expr_, child_.get_eval_ctx(), result()));
  EXPECT_EQ(2, child_.calls_);
  Datum out;
  ASSERT_EQ(OB_SUCCESS, lookup(out)); EXPECT_EQ(72, out.get_int());
  ASSERT_EQ(OB_SUCCESS, ObExprSubQueryRef::expr_eval(result_expr_, child_.get_eval_ctx(), result()));
  EXPECT_EQ(0, child_.calls_); // rewind ran, then the cache supplied the row.
  EXPECT_EQ(72, result().get_int());
}

TEST_F(NativeSubqueryCache, actual_scalar_evaluator_retains_first_payload_before_second_advance)
{
  string_type(child_expr_); string_type(result_expr_);
  child_.string_result_ = true; child_.second_result_ = OB_ITER_END;
  ASSERT_EQ(OB_SUCCESS, ObExprSubQueryRef::expr_eval(result_expr_, child_.get_eval_ctx(), result()));
  EXPECT_EQ(2, child_.calls_);
  EXPECT_NE(child_.string_bytes_, result().get_string().ptr());
  EXPECT_EQ(ObString::make_string("retained first row"), result().get_string());
  Datum out;
  ASSERT_EQ(OB_SUCCESS, lookup(out));
  EXPECT_EQ(ObString::make_string("retained first row"), out.get_string());
}

TEST_F(NativeSubqueryCache, pending_resource_clone_retains_sql_array_leaves_before_source_reset)
{
  ObArenaAllocator source_owner;
  SqlType element;
  SqlTypeMeta text; text.set_varchar(); text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObAccuracy accuracy; accuracy.set_length(128);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(text, accuracy, element));
  SqlTypeSpec declaration;
  declaration.attributes.meta_.set_ext(); declaration.attributes.meta_.set_extend_type(T_EXT_SQL_ARRAY);
  declaration.form = SqlTypeForm::Array; declaration.element = &element;
  const SqlType *type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(declaration, owner_, type));
  ASSERT_EQ(OB_SUCCESS, result_expr_.bind_sql_type(*type, owner_));
  auto *array = DatumParameterArray::alloc(source_owner, 1); ASSERT_NE(nullptr, array);
  ASSERT_EQ(OB_SUCCESS, array->data_[0].set_varchar(
      ObString::make_string("resource leaf independently owned by the pending cache"), source_owner));
  DatumParameter parameter;
  ASSERT_EQ(OB_SUCCESS, parameter.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, source_owner));
  Datum source;
  ASSERT_EQ(OB_SUCCESS, DatumCopyPlan(*type).import_legacy(parameter.datum_, source));
  ObSubQueryIterator::PendingCacheEntry pending;
  ASSERT_EQ(OB_SUCCESS, iterator_.prepare_cache_result(source, result_expr_, pending));
  source_owner.reset();
  ASSERT_EQ(OB_SUCCESS, iterator_.publish_cache_result(pending));
  Datum out; ASSERT_EQ(OB_SUCCESS, lookup(out));
  ObDatum legacy;
  ASSERT_EQ(OB_SUCCESS, DatumCopyPlan(*pending.result_type_).borrow_legacy(out, legacy));
  DatumParameter retained; retained.datum_ = legacy; retained.set_type_meta(type->legacy_meta());
  const auto *owned = retained.get_array<DatumParameterArray>(); ASSERT_NE(nullptr, owned);
  ASSERT_EQ(1, owned->count_);
  EXPECT_EQ(ObString::make_string("resource leaf independently owned by the pending cache"),
            owned->data_[0].get_string());
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
