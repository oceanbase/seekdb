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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "common/datum/datum_copy_plan.h"
#include "common/datum/datum_parameter_array.h"
#include "common/datum/native_typed_datum.h"
#include "query/engine/expr/ob_expr_res_type.h"
#include "query/plan_cache/ob_plan_cache_access_service.h"
#include "share/datum/typed_datum_compare.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/ob_result_set.h"
#include "sql/ob_sql.h"
#include "sql/plan_cache/ob_plan_cache_struct.h"
#include "sql/plan_cache/ob_plan_set.h"
#include "sql/resolver/ob_stmt.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase { namespace sql {
// Access only the actual installation/matching entry points and retained data.
// No fixture implementation of either production algorithm.
struct NativePcConstValuesTestPeer
{
  static int install(ObPlanSet &set, common::ObIArray<ObPCConstParamInfo> &values, bool possible)
  { return set.set_const_param_constraint(values, possible); }
  static int match(ObPlanSet &set, const ParamStore &values, bool &matched)
  { return set.match_constraint(values, matched); }
  static int match_cons(ObPlanSet &set, const ObPlanCacheCtx &context, bool &matched)
  { return set.match_cons(context, matched); }
  static int need_add_plan(ObSql &sql, const ObPlanCacheCtx &context,
                           ObResultSet &result, bool &needed)
  { return sql.need_add_plan(context, result, true, needed); }
  static int64_t count(const ObPlanSet &set, bool possible = false)
  { return possible ? set.all_possible_const_param_constraints_.count() : set.all_plan_const_param_constraints_.count(); }
  static const ObPCConstParamInfo &at(const ObPlanSet &set, int64_t index = 0, bool possible = false)
  { return possible ? set.all_possible_const_param_constraints_.at(index) : set.all_plan_const_param_constraints_.at(index); }
};
} }

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace {
class ConstValueAllocator final : public ObIAllocator
{
public:
  void *alloc(int64_t bytes) override
  {
    sizes_.push_back(bytes);
    return ++calls_ == fail_at_ ? nullptr : arena_.alloc(bytes);
  }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *memory) override { arena_.free(memory); }
  int64_t calls_ = 0;
  int64_t fail_at_ = -1;
  std::vector<int64_t> sizes_;
  ObArenaAllocator arena_;
};

// The real ResultSet enters/releases the epoch during construction/destruction.
// Count that lifecycle while requiring the admission decision itself to make no
// extra accesses. Other service operations are not part of this minimal fixture.
class CountingCacheAccessService final : public oceanbase::query::ObIPlanCacheAccessService
{
public:
  ~CountingCacheAccessService() override
  {
    EXPECT_EQ(1, enter_count_);
    EXPECT_EQ(enter_count_, leave_count_);
  }
  void enter_access() override { ++enter_count_; }
  void leave_access() override { ++leave_count_; }
  void check_current_thread() override { ADD_FAILURE() << "Unexpected cache epoch check"; }
  int get_global_safe_timestamp(int64_t &) const override
  { ADD_FAILURE() << "Unexpected cache epoch timestamp"; return OB_ERR_UNEXPECTED; }
  int64_t enter_count_ = 0;
  int64_t leave_count_ = 0;
};

void scalar_type(SqlType &type, SqlValueType kind, ObPrecision precision = -1, ObScale scale = -1,
                    ObCollationType collation = CS_TYPE_INVALID)
{
  SqlTypeMeta meta;
  meta.set_type(kind);
  if (kind == ObDecimalIntType) { meta.set_decimal_int(scale); }
  if (collation != CS_TYPE_INVALID) {
    meta.set_collation_type(collation);
    meta.set_collation_level(CS_LEVEL_COERCIBLE);
  }
  ObAccuracy accuracy;
  accuracy.set_precision(precision); accuracy.set_scale(scale);
  if (kind == ObVarcharType) { accuracy.set_length(64); }
  EXPECT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, type));
}

int install_one(ObPlanSet &set, const ObPCConstParamInfo &value, bool possible = false)
{
  ObSEArray<ObPCConstParamInfo, 1> values;
  int ret = values.push_back(value);
  return ret == OB_SUCCESS ? NativePcConstValuesTestPeer::install(set, values, possible) : ret;
}

void expect_match(ObPlanSet &set, const ParamStore &params, bool expected)
{
  bool matched = !expected;
  ASSERT_EQ(OB_SUCCESS, NativePcConstValuesTestPeer::match(set, params, matched));
  EXPECT_EQ(expected, matched);
}

DatumParameter decimal_parameter(int64_t coefficient, int32_t width,
    ObPrecision precision, ObScale scale, ObIAllocator &owner)
{
  DatumParameter value;
  char *bytes = width == 0 ? nullptr : static_cast<char *>(owner.alloc(width));
  EXPECT_TRUE(width == 0 || bytes != nullptr);
  if (width != 0 && bytes == nullptr) { return value; }
  if (width > 0) {
    MEMSET(bytes, coefficient < 0 ? 0xff : 0, width);
    const int64_t copied = MIN(int64_t(width), int64_t(sizeof(coefficient)));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    MEMCPY(bytes + width - copied, reinterpret_cast<const char *>(&coefficient) + sizeof(coefficient) - copied, copied);
#else
    MEMCPY(bytes, &coefficient, copied);
#endif
  }
  SqlTypeMeta meta; meta.set_decimal_int(scale);
  value.set_type_meta(meta);
  ObAccuracy accuracy; accuracy.set_precision(precision); accuracy.set_scale(scale);
  value.set_accuracy(accuracy); value.set_param_meta(meta);
  // Generic parameter values preserve the actual physical coefficient width;
  // semantic inline/normalized Decimal values are prepared at their real Type boundary.
  EXPECT_EQ(OB_SUCCESS, value.datum_.set_reference(bytes, width));
  return value;
}

int add_number(const char *text, DatumParameter &value, ObIAllocator &owner)
{
  number::ObNumber number;
  int ret = number.from(text, owner);
  if (ret == OB_SUCCESS) { ret = value.set_number(number, owner); }
  return ret;
}

DatumParameter sql_array_parameter(ObIAllocator &owner)
{
  DatumParameter result;
  DatumParameterArray *array = DatumParameterArray::alloc(owner, 1);
  EXPECT_NE(nullptr, array);
  if (array == nullptr) { return result; }
  array->element_.set_obj_type(ObIntType);
  EXPECT_EQ(OB_SUCCESS, array->data_[0].set_int(123, owner));
  EXPECT_EQ(OB_SUCCESS, result.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, owner));
  result.set_param_meta();
  return result;
}

void expect_admission(ObPCConstParamInfo &info, bool possible, bool batch, bool expected)
{
  ObArenaAllocator owner;
  ObSQLSessionInfo session;
  CountingCacheAccessService access;
  ObResultSet result(session, owner, access);
  ObSqlCtx sql_context;
  ObSEArray<ObPCConstParamInfo, 1> required, possible_values;
  ObSEArray<ObPCParamEqualInfo, 1> equal;
  ASSERT_EQ(OB_SUCCESS, (possible ? possible_values : required).push_back(info));
  sql_context.all_plan_const_param_constraints_ = &required;
  sql_context.all_possible_const_param_constraints_ = &possible_values;
  sql_context.all_equal_param_constraints_ = &equal;
  ObSEArray<ObString, 2> statements;
  if (batch) {
    ASSERT_EQ(OB_SUCCESS, statements.push_back(ObString("SELECT 1")));
    ASSERT_EQ(OB_SUCCESS, statements.push_back(ObString("SELECT 2")));
    sql_context.multi_stmt_item_.set_batched_queries(&statements);
  }
  ASSERT_EQ(batch, sql_context.is_batch_params_execute());
  ASSERT_NE(nullptr, result.get_exec_context().get_stmt_factory());
  ASSERT_NE(nullptr, result.get_exec_context().get_stmt_factory()->get_query_ctx());
  ObPlanCacheCtx cache_context(ObString("SELECT ?"), PC_TEXT_MODE, owner,
                              sql_context, result.get_exec_context());
  ObSql sql;
  EXPECT_EQ(1, access.enter_count_);
  EXPECT_EQ(0, access.leave_count_);
  const int64_t entered_before = access.enter_count_;
  const int64_t left_before = access.leave_count_;
  bool needed = true;
  EXPECT_EQ(OB_SUCCESS, NativePcConstValuesTestPeer::need_add_plan(sql, cache_context, result, needed));
  EXPECT_EQ(expected, needed);
  EXPECT_EQ(entered_before, access.enter_count_);
  EXPECT_EQ(left_before, access.leave_count_);
  // ResultSet is destroyed before access; its destructor checks the final pair.
}
}

TEST(NativePcConstValues, integers_use_native_inline_values_without_legacy_scalar_payload)
{
  static_assert(sizeof(NativeTypedDatum) == 24, "The actual cache element is Datum plus FullType pointer");
  SqlType type;
  scalar_type(type, ObIntType, 19, 0);
  ConstValueAllocator owner;
  ObPCConstParamInfo constants;
  for (int64_t value : {INT64_MIN, int64_t(0), INT64_MAX}) {
    Datum input; input.set_int(value);
    ASSERT_EQ(OB_SUCCESS, constants.add_constant(input, type, owner));
    const auto &stored = constants.const_params_.at(constants.const_params_.count() - 1);
    ASSERT_NE(nullptr, stored.type_);
    EXPECT_EQ(DatumRepresentation::Inline64, stored.type_->canonical_representation());
    EXPECT_EQ(value, stored.datum_.get_int());
    EXPECT_EQ(0, stored.datum_.length());
    EXPECT_NE(&type, stored.type_);
  }
  EXPECT_EQ(owner.sizes_.end(), std::find(owner.sizes_.begin(), owner.sizes_.end(), int64_t(8)));
}

TEST(NativePcConstValues, plan_cache_owns_string_number_and_wide_decimal_after_query_arena_reset)
{
  ObArenaAllocator query_owner, params_owner;
  SqlType text_type;
  scalar_type(text_type, ObVarcharType, -1, -1, CS_TYPE_UTF8MB4_BIN);
  SqlType number_type;
  scalar_type(number_type, ObNumberType, 65, 0);
  SqlType decimal_type;
  scalar_type(decimal_type, ObDecimalIntType, 20, 5);
  char source_text[] = "query-owned";
  ObDatum text; text.set_string(source_text, sizeof(source_text) - 1);
  DatumParameter number, decimal = decimal_parameter(123450, 16, 20, 5, query_owner);
  ASSERT_EQ(OB_SUCCESS, add_number("9007199254740993", number, query_owner));
  ObPCConstParamInfo query;
  ASSERT_EQ(OB_SUCCESS, query.add_constant(text, text_type, query_owner));
  ASSERT_EQ(OB_SUCCESS, query.add_constant(number.datum_, number_type, query_owner));
  ASSERT_EQ(OB_SUCCESS, query.add_constant(decimal.datum_, decimal_type, query_owner));
  for (int64_t i = 0; i < 3; ++i) { ASSERT_EQ(OB_SUCCESS, query.const_idx_.push_back(i)); }
  ObSqlPlanSet cache;
  ASSERT_EQ(OB_SUCCESS, install_one(cache, query));
  const auto &cached = NativePcConstValuesTestPeer::at(cache);
  for (int64_t i = 0; i < 3; ++i) {
    EXPECT_NE(query.const_params_.at(i).type_, cached.const_params_.at(i).type_);
    EXPECT_NE(query.const_params_.at(i).datum_.get_reference(), cached.const_params_.at(i).datum_.get_reference());
    MEMSET(const_cast<void *>(query.const_params_.at(i).datum_.get_reference()), 0x5a,
           query.const_params_.at(i).datum_.length());
  }
  MEMSET(source_text, 'x', sizeof(source_text));
  query_owner.reset();
  EXPECT_EQ(ObString("query-owned"), cached.const_params_.at(0).datum_.get_string());
  EXPECT_EQ(20, cached.const_params_.at(2).type_->precision());
  EXPECT_EQ(5, cached.const_params_.at(2).type_->scale());
  ParamStore params{ObWrapperAllocator(params_owner)};
  DatumParameter expected_text, expected_number;
  expected_text.set_varchar_view(ObString("query-owned"));
  expected_text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, add_number("9007199254740993", expected_number, params_owner));
  ASSERT_EQ(OB_SUCCESS, params.push_back(expected_text));
  ASSERT_EQ(OB_SUCCESS, params.push_back(expected_number));
  ASSERT_EQ(OB_SUCCESS, params.push_back(decimal_parameter(123450, 16, 20, 5, params_owner)));
  expect_match(cache, params, true);
}

TEST(NativePcConstValues, all_plan_groups_require_every_index_and_take_precedence)
{
  ObArenaAllocator owner;
  ObPCConstParamInfo required, possible;
  ASSERT_EQ(OB_SUCCESS, required.add_int_constant(7, owner));
  ASSERT_EQ(OB_SUCCESS, required.add_int_constant(9, owner));
  ASSERT_EQ(OB_SUCCESS, required.const_idx_.push_back(1));
  ASSERT_EQ(OB_SUCCESS, required.const_idx_.push_back(0));
  ASSERT_EQ(OB_SUCCESS, possible.add_int_constant(9, owner));
  ASSERT_EQ(OB_SUCCESS, possible.const_idx_.push_back(0));
  ObSqlPlanSet cache;
  ASSERT_EQ(OB_SUCCESS, install_one(cache, required));
  ASSERT_EQ(OB_SUCCESS, install_one(cache, possible, true));
  DatumParameter a, b;
  ASSERT_EQ(OB_SUCCESS, a.set_int(9, owner));
  ASSERT_EQ(OB_SUCCESS, b.set_int(7, owner));
  ParamStore params{ObWrapperAllocator(owner)}; ASSERT_EQ(OB_SUCCESS, params.push_back(a)); ASSERT_EQ(OB_SUCCESS, params.push_back(b));
  expect_match(cache, params, true); // The matching possible group is ignored here.
  ASSERT_EQ(OB_SUCCESS, params.at(1).set_int(8, owner));
  expect_match(cache, params, false);
}

TEST(NativePcConstValues, possible_groups_invert_a_complete_match_but_not_a_partial_match)
{
  ObArenaAllocator owner;
  ObPCConstParamInfo first, second;
  ASSERT_EQ(OB_SUCCESS, first.add_int_constant(7, owner));
  ASSERT_EQ(OB_SUCCESS, first.add_int_constant(9, owner));
  ASSERT_EQ(OB_SUCCESS, first.const_idx_.push_back(0)); ASSERT_EQ(OB_SUCCESS, first.const_idx_.push_back(1));
  ASSERT_EQ(OB_SUCCESS, second.add_int_constant(99, owner)); ASSERT_EQ(OB_SUCCESS, second.const_idx_.push_back(1));
  ObSEArray<ObPCConstParamInfo, 2> groups;
  ASSERT_EQ(OB_SUCCESS, groups.push_back(first)); ASSERT_EQ(OB_SUCCESS, groups.push_back(second));
  ObSqlPlanSet cache;
  ASSERT_EQ(OB_SUCCESS, NativePcConstValuesTestPeer::install(cache, groups, true));
  DatumParameter a, b; ASSERT_EQ(OB_SUCCESS, a.set_int(7, owner)); ASSERT_EQ(OB_SUCCESS, b.set_int(8, owner));
  ParamStore params{ObWrapperAllocator(owner)}; ASSERT_EQ(OB_SUCCESS, params.push_back(a)); ASSERT_EQ(OB_SUCCESS, params.push_back(b));
  expect_match(cache, params, true);
  ASSERT_EQ(OB_SUCCESS, params.at(1).set_int(9, owner)); expect_match(cache, params, false);
  ASSERT_EQ(OB_SUCCESS, params.at(0).set_int(0, owner));
  ASSERT_EQ(OB_SUCCESS, params.at(1).set_int(99, owner)); expect_match(cache, params, false);
}

TEST(NativePcConstValues, typed_null_parameter_preserves_declaration_and_matches_sql_null)
{
  ObArenaAllocator owner;
  SqlType type;
  scalar_type(type, ObDecimalIntType, 20, 7);
  DatumParameter null;
  null.set_type_meta(type.legacy_meta()); null.set_param_meta(type.legacy_meta());
  null.set_accuracy(type.accuracy()); null.set_null();
  ObExecContext context(owner);
  ASSERT_EQ(OB_SUCCESS, context.create_physical_plan_ctx());
  ObPhysicalPlanCtx *parameters = context.get_physical_plan_ctx();
  ASSERT_NE(nullptr, parameters);
  ASSERT_EQ(OB_SUCCESS, parameters->append_parameter_input(null));
  ASSERT_EQ(nullptr, parameters->get_native_parameter(0));
  ObPCConstParamInfo info;
  ASSERT_EQ(OB_SUCCESS, info.add_parameter(parameters->get_parameter_inputs().at(0), &context, owner));
  EXPECT_EQ(nullptr, parameters->get_native_parameter(0)); // Capture must not initialize frames.
  ASSERT_EQ(1, info.const_params_.count());
  EXPECT_TRUE(info.const_params_.at(0).datum_.is_null());
  ASSERT_NE(nullptr, info.const_params_.at(0).type_);
  EXPECT_EQ(ObDecimalIntType, info.const_params_.at(0).type_->kind());
  EXPECT_EQ(20, info.const_params_.at(0).type_->precision()); EXPECT_EQ(7, info.const_params_.at(0).type_->scale());
  ASSERT_EQ(OB_SUCCESS, info.const_idx_.push_back(0));
  ObSqlPlanSet cache; ASSERT_EQ(OB_SUCCESS, install_one(cache, info));
  ParamStore params{ObWrapperAllocator(owner)}; DatumParameter runtime; runtime.set_null();
  ASSERT_EQ(OB_SUCCESS, params.push_back(runtime)); expect_match(cache, params, true);
  ASSERT_EQ(OB_SUCCESS, params.at(0).set_int(0, owner)); expect_match(cache, params, false);
}

TEST(NativePcConstValues, decimal_scale_equality_is_mathematical_and_full_types_remain_distinct)
{
  ObArenaAllocator owner;
  SqlType narrow;
  scalar_type(narrow, ObDecimalIntType, 18, 2);
  SqlType wide;
  scalar_type(wide, ObDecimalIntType, 20, 3);
  SqlType unknown;
  scalar_type(unknown, ObDecimalIntType, -1, 5);
  ObPCConstParamInfo a, b, c;
  const DatumParameter va = decimal_parameter(120, 8, 18, 2, owner);
  const DatumParameter vb = decimal_parameter(1200, 16, 20, 3, owner);
  const DatumParameter vc = decimal_parameter(120000, 16, -1, 5, owner);
  // These fixtures own generic physical coefficients. The old-ABI overload
  // imports them using each actual prepared semantic declaration.
  auto capture = [&](ObPCConstParamInfo &info, const DatumParameter &value,
                      const SqlType &type) {
    alignas(8) char scratch[64];
    ObDatum physical;
    const int ret = value.export_legacy(physical, scratch, sizeof(scratch));
    return ret == OB_SUCCESS ? info.add_constant(physical, type, owner) : ret;
  };
  ASSERT_EQ(OB_SUCCESS, capture(a, va, narrow));
  ASSERT_EQ(OB_SUCCESS, capture(b, vb, wide));
  ASSERT_EQ(OB_SUCCESS, capture(c, vc, unknown));
  for (auto *info : {&a, &b, &c}) { ASSERT_EQ(OB_SUCCESS, info->const_idx_.push_back(0)); }
  EXPECT_TRUE(a == b); EXPECT_TRUE(b == c); // 1.20 == 1.200 == 1.20000.
  EXPECT_EQ(DatumRepresentation::Inline64, a.const_params_.at(0).type_->canonical_representation());
  EXPECT_EQ(20, b.const_params_.at(0).type_->precision());
  EXPECT_EQ(-1, c.const_params_.at(0).type_->precision()); EXPECT_EQ(5, c.const_params_.at(0).type_->scale());
  EXPECT_EQ(16, c.const_params_.at(0).datum_.length());
  ObSqlPlanSet cache; ASSERT_EQ(OB_SUCCESS, install_one(cache, a));
  ParamStore params{ObWrapperAllocator(owner)}; ASSERT_EQ(OB_SUCCESS, params.push_back(vc)); expect_match(cache, params, true);
  params.at(0) = decimal_parameter(120001, 16, -1, 5, owner); expect_match(cache, params, false);
}

TEST(NativePcConstValues, exact_number_and_signed_unsigned_comparisons_do_not_round_through_double)
{
  ObArenaAllocator owner;
  SqlType type;
  scalar_type(type, ObNumberType, 65, 0);
  DatumParameter number;
  ASSERT_EQ(OB_SUCCESS, add_number("9007199254740993", number, owner));
  ObPCConstParamInfo info; ASSERT_EQ(OB_SUCCESS, info.add_constant(number.datum_, type, owner));
  ASSERT_EQ(OB_SUCCESS, info.const_idx_.push_back(0));
  ObSqlPlanSet cache; ASSERT_EQ(OB_SUCCESS, install_one(cache, info));
  ParamStore params{ObWrapperAllocator(owner)}; DatumParameter runtime;
  ASSERT_EQ(OB_SUCCESS, runtime.set_int(INT64_C(9007199254740993), owner));
  ASSERT_EQ(OB_SUCCESS, params.push_back(runtime)); expect_match(cache, params, true);
  ASSERT_EQ(OB_SUCCESS, params.at(0).set_int(INT64_C(9007199254740992), owner)); expect_match(cache, params, false);
  ObPCConstParamInfo unsigned_info;
  SqlType uint_type;
  scalar_type(uint_type, ObUInt64Type, 20, 0);
  Datum max; max.set_uint(UINT64_MAX);
  ASSERT_EQ(OB_SUCCESS, unsigned_info.add_constant(max, uint_type, owner));
  ASSERT_EQ(OB_SUCCESS, unsigned_info.const_idx_.push_back(0));
  ASSERT_EQ(OB_SUCCESS, install_one(cache, unsigned_info));
  ASSERT_EQ(OB_SUCCESS, params.at(0).set_uint64(UINT64_MAX, owner)); expect_match(cache, params, true);
  ASSERT_EQ(OB_SUCCESS, params.at(0).set_int(-1, owner)); expect_match(cache, params, false);
}

TEST(NativePcConstValues, nan_matching_and_constraint_equality_keep_their_distinct_old_contracts)
{
  ObArenaAllocator owner;
  SqlType double_type;
  scalar_type(double_type, ObDoubleType);
  Datum nan; nan.set_double(std::numeric_limits<double>::quiet_NaN());
  ObPCConstParamInfo a, b, integer;
  ASSERT_EQ(OB_SUCCESS, a.add_constant(nan, double_type, owner));
  ASSERT_EQ(OB_SUCCESS, b.add_constant(nan, double_type, owner));
  ASSERT_EQ(OB_SUCCESS, integer.add_int_constant(0, owner));
  for (auto *info : {&a, &b, &integer}) { ASSERT_EQ(OB_SUCCESS, info->const_idx_.push_back(0)); }
  EXPECT_TRUE(a == b); EXPECT_FALSE(a == integer);
  ObSqlPlanSet cache; ASSERT_EQ(OB_SUCCESS, install_one(cache, a));
  DatumParameter runtime; ASSERT_EQ(OB_SUCCESS, runtime.set_double(std::numeric_limits<double>::quiet_NaN(), owner));
  ParamStore params{ObWrapperAllocator(owner)}; ASSERT_EQ(OB_SUCCESS, params.push_back(runtime)); expect_match(cache, params, true);
  ASSERT_EQ(OB_SUCCESS, params.at(0).set_double(0.0, owner)); expect_match(cache, params, false);
  ASSERT_EQ(OB_SUCCESS, params.at(0).set_int(0, owner));
  // The old cache's mixed unordered three-way comparison returns zero, while
  // datum_values_equal separately rejects mixed NaN. Pin both, not SQL '='.
  double legacy_nan = std::numeric_limits<double>::quiet_NaN();
  ObDatum legacy; legacy.ptr_ = reinterpret_cast<const char *>(&legacy_nan); legacy.set_double(legacy_nan);
  int cmp = 77;
  DatumValue legacy_value;
  ASSERT_EQ(OB_SUCCESS, legacy_value.assign_legacy_borrowed(legacy, double_type.legacy_meta()));
  ASSERT_EQ(OB_SUCCESS, compare_datum_values(legacy_value, params.at(0).value_view(), cmp));
  ASSERT_EQ(0, cmp);
  expect_match(cache, params, true);
}

TEST(NativePcConstValues, collation_selects_comparison_and_different_collations_remain_nonmatches)
{
  ObArenaAllocator owner;
  for (const auto collation : {CS_TYPE_UTF8MB4_GENERAL_CI, CS_TYPE_UTF8MB4_BIN}) {
    SqlType type;
    scalar_type(type, ObVarcharType, -1, -1, collation);
    ObDatum text; text.set_string("Alpha", 5);
    ObPCConstParamInfo info; ASSERT_EQ(OB_SUCCESS, info.add_constant(text, type, owner));
    ASSERT_EQ(OB_SUCCESS, info.const_idx_.push_back(0));
    ObSqlPlanSet cache; ASSERT_EQ(OB_SUCCESS, install_one(cache, info));
    ParamStore params{ObWrapperAllocator(owner)}; DatumParameter runtime; runtime.set_varchar_view(ObString("alpha")); runtime.set_collation_type(collation);
    ASSERT_EQ(OB_SUCCESS, params.push_back(runtime));
    expect_match(cache, params, collation == CS_TYPE_UTF8MB4_GENERAL_CI);
    params.at(0).set_collation_type(collation == CS_TYPE_UTF8MB4_BIN ? CS_TYPE_UTF8MB4_GENERAL_CI : CS_TYPE_UTF8MB4_BIN);
    expect_match(cache, params, false);
  }
}

TEST(NativePcConstValues, unsupported_comparators_are_nonmatches_in_both_plan_set_branches)
{
  ObArenaAllocator owner;
  SqlType double_type;
  scalar_type(double_type, ObDoubleType);
  Datum real; real.set_double(1.0);
  ObPCConstParamInfo numeric; ASSERT_EQ(OB_SUCCESS, numeric.add_constant(real, double_type, owner));
  ASSERT_EQ(OB_SUCCESS, numeric.const_idx_.push_back(0));
  ParamStore params{ObWrapperAllocator(owner)}; DatumParameter number; ASSERT_EQ(OB_SUCCESS, add_number("1", number, owner));
  ASSERT_EQ(OB_SUCCESS, params.push_back(number));
  ObSqlPlanSet required, possible;
  ASSERT_EQ(OB_SUCCESS, install_one(required, numeric)); ASSERT_EQ(OB_SUCCESS, install_one(possible, numeric, true));
  expect_match(required, params, false); expect_match(possible, params, true);
  SqlType datetime_type;
  scalar_type(datetime_type, ObDateTimeType);
  Datum datetime; datetime.set_datetime(123);
  ObPCConstParamInfo temporal; ASSERT_EQ(OB_SUCCESS, temporal.add_constant(datetime, datetime_type, owner));
  ASSERT_EQ(OB_SUCCESS, temporal.const_idx_.push_back(0));
  ASSERT_EQ(OB_SUCCESS, install_one(required, temporal));
  ASSERT_EQ(OB_SUCCESS, params.at(0).set_timestamp(123, owner)); expect_match(required, params, false);
}

TEST(NativePcConstValues, malformed_constraint_and_invalid_indices_keep_original_errors)
{
  ObArenaAllocator owner;
  ObPCConstParamInfo info; ASSERT_EQ(OB_SUCCESS, info.add_int_constant(3, owner));
  ObSqlPlanSet cache;
  EXPECT_EQ(OB_ERR_UNEXPECTED, install_one(cache, info));
  EXPECT_EQ(0, NativePcConstValuesTestPeer::count(cache));
  ParamStore params{ObWrapperAllocator(owner)}; DatumParameter p; ASSERT_EQ(OB_SUCCESS, p.set_int(3, owner)); ASSERT_EQ(OB_SUCCESS, params.push_back(p));
  for (const int64_t index : {int64_t(-1), int64_t(1)}) {
    info.const_idx_.reuse(); ASSERT_EQ(OB_SUCCESS, info.const_idx_.push_back(index));
    for (const bool possible : {false, true}) {
      ObSqlPlanSet set;
      ASSERT_EQ(OB_SUCCESS, install_one(set, info, possible));
      bool matched = false;
      EXPECT_EQ(OB_ERR_UNEXPECTED, NativePcConstValuesTestPeer::match(set, params, matched));
    }
  }
}

TEST(NativePcConstValues, capture_and_cache_clone_failures_do_not_publish_partial_values)
{
  SqlType integer_type;
  scalar_type(integer_type, ObIntType, 19, 0);
  SqlType text_type;
  scalar_type(text_type, ObVarcharType, -1, -1, CS_TYPE_UTF8MB4_BIN);
  Datum text; ASSERT_EQ(OB_SUCCESS, text.set_reference("a reference payload", 19));
  for (const int64_t failing_step : {int64_t(1), int64_t(2)}) {
    ConstValueAllocator owner;
    ObPCConstParamInfo info; ASSERT_EQ(OB_SUCCESS, info.add_int_constant(42, owner));
    owner.fail_at_ = owner.calls_ + failing_step;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, info.add_constant(text, text_type, owner));
    ASSERT_EQ(1, info.const_params_.count()); EXPECT_EQ(42, info.const_params_.at(0).datum_.get_int());
    EXPECT_TRUE(info.cache_eligible_); // OOM is an error, never a capability downgrade.
    owner.fail_at_ = -1;
    ASSERT_EQ(OB_SUCCESS, info.add_constant(text, text_type, owner)); ASSERT_EQ(2, info.const_params_.count());
    NativeTypedDatum output; output.datum_.set_int(99); output.type_ = &integer_type;
    const NativeTypedDatum unchanged = output;
    ConstValueAllocator failed_clone;
    failed_clone.fail_at_ = failing_step;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, ObPCConstParamInfo::clone_constant(info.const_params_.at(1), failed_clone, output));
    EXPECT_EQ(0, MEMCMP(&unchanged, &output, sizeof(output)));
    failed_clone.fail_at_ = -1;
    ASSERT_EQ(OB_SUCCESS, ObPCConstParamInfo::clone_constant(info.const_params_.at(1), failed_clone, output));
    EXPECT_NE(info.const_params_.at(1).type_, output.type_);
    EXPECT_EQ(ObString("a reference payload"), output.datum_.get_string());
  }
}

TEST(NativePcConstValues, raw_constant_capture_uses_the_original_declaration)
{
  ObArenaAllocator owner;
  SqlType declared;
  scalar_type(declared, ObDecimalIntType, 20, 5);
  const DatumParameter value = decimal_parameter(123450, 16, 20, 5, owner);
  ObRawExprResType original;
  original.set_decimal_int(5); original.set_accuracy(declared.accuracy());
  ObPCConstParamInfo info;
  ASSERT_EQ(OB_SUCCESS, info.add_raw_constant(value.value_view(), original, nullptr, nullptr, owner));
  ASSERT_EQ(1, info.const_params_.count());
  const auto &stored = info.const_params_.at(0);
  ASSERT_NE(nullptr, stored.type_);
  EXPECT_EQ(20, stored.type_->precision()); EXPECT_EQ(5, stored.type_->scale());
  EXPECT_EQ(16, stored.datum_.length());
  char printed[64]; int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, ObPCConstParamInfo::print_constant(stored, printed, sizeof(printed), position));
  EXPECT_GT(position, 0);
  // Literal spelling is checked by the old decimal writer at this synchronous
  // boundary; here also ensure scale metadata was not reconstructed as zero.
  EXPECT_EQ(std::string("1.23450"), std::string(printed, position));
}

TEST(NativePcConstValues, missing_full_type_cannot_publish_a_cache_clone)
{
  SqlType integer_type;
  scalar_type(integer_type, ObIntType, 19, 0);
  NativeTypedDatum source;
  source.datum_.set_int(42); // A carrier alone does not make a typed cache value.
  NativeTypedDatum output;
  output.datum_.set_int(99); output.type_ = &integer_type;
  const NativeTypedDatum unchanged = output;
  ConstValueAllocator owner;
  EXPECT_EQ(OB_ERR_UNEXPECTED, ObPCConstParamInfo::clone_constant(source, owner, output));
  EXPECT_EQ(0, owner.calls_);
  EXPECT_EQ(0, MEMCMP(&unchanged, &output, sizeof(output)));
}

TEST(NativePcConstValues, resource_and_missing_declaration_capture_preserve_sql_without_fake_values)
{
  ObArenaAllocator owner;
  ObExecContext context(owner);
  DatumParameter array = sql_array_parameter(owner);
  ASSERT_FALSE(array.is_null());
  ObPCConstParamInfo present;
  ASSERT_EQ(OB_SUCCESS, present.add_int_constant(7, owner));
  ASSERT_EQ(OB_SUCCESS, present.add_parameter(array, &context, owner));
  EXPECT_FALSE(present.cache_eligible_);
  ASSERT_EQ(1, present.const_params_.count()); // Preserve the valid prefix only.
  EXPECT_EQ(7, present.const_params_.at(0).datum_.get_int());
  ASSERT_NE(nullptr, array.get_array<DatumParameterArray>());
  EXPECT_EQ(123, array.get_array<DatumParameterArray>()->data_[0].datum_.get_int());

  DatumParameter null_array = array;
  null_array.set_null();
  ObPCConstParamInfo known_null;
  ASSERT_EQ(OB_SUCCESS, known_null.add_parameter(null_array, &context, owner));
  EXPECT_TRUE(known_null.cache_eligible_);
  ASSERT_EQ(1, known_null.const_params_.count());
  EXPECT_TRUE(known_null.const_params_.at(0).datum_.is_null());
  ASSERT_NE(nullptr, known_null.const_params_.at(0).type_);
  EXPECT_TRUE(known_null.const_params_.at(0).type_->legacy_meta().is_ext_sql_array());
  EXPECT_TRUE(known_null.const_params_.at(0).type_->heterogeneous_array());

  ObPCConstParamInfo missing_declaration;
  ASSERT_EQ(OB_SUCCESS, missing_declaration.add_parameter(null_array, nullptr, owner));
  EXPECT_FALSE(missing_declaration.cache_eligible_);
  EXPECT_EQ(0, missing_declaration.const_params_.count());
  EXPECT_FALSE(missing_declaration == missing_declaration);
}

TEST(NativePcConstValues, ineligible_groups_propagate_and_never_match_either_plan_set_branch)
{
  ObArenaAllocator owner;
  ObExecContext context(owner);
  DatumParameter resource = sql_array_parameter(owner);
  ObPCConstParamInfo blocked, seven, other;
  ASSERT_EQ(OB_SUCCESS, blocked.add_parameter(resource, &context, owner));
  ASSERT_FALSE(blocked.cache_eligible_); ASSERT_EQ(0, blocked.const_params_.count());
  ASSERT_EQ(OB_SUCCESS, blocked.const_idx_.push_back(0));
  ASSERT_EQ(OB_SUCCESS, seven.add_int_constant(7, owner)); ASSERT_EQ(OB_SUCCESS, seven.const_idx_.push_back(0));
  ASSERT_EQ(OB_SUCCESS, other.add_int_constant(99, owner)); ASSERT_EQ(OB_SUCCESS, other.const_idx_.push_back(0));
  ObSEArray<ObPCConstParamInfo, 2> required, possible;
  ObSEArray<ObPCParamEqualInfo, 1> equal;
  ObSqlCtx sql_context;
  sql_context.all_plan_const_param_constraints_ = &required;
  sql_context.all_possible_const_param_constraints_ = &possible;
  sql_context.all_equal_param_constraints_ = &equal;
  ObPlanCacheCtx cache_context(ObString("SELECT ?"), PC_PS_MODE, owner, sql_context, context);
  ParamStore params{ObWrapperAllocator(owner)};
  DatumParameter value; ASSERT_EQ(OB_SUCCESS, value.set_int(7, owner)); ASSERT_EQ(OB_SUCCESS, params.push_back(value));
  for (const bool in_required : {false, true}) {
    required.reuse(); possible.reuse();
    if (in_required) {
      ASSERT_EQ(OB_SUCCESS, required.push_back(blocked));
      ASSERT_EQ(OB_SUCCESS, possible.push_back(seven));
    } else {
      ASSERT_EQ(OB_SUCCESS, possible.push_back(other)); // Does not match; blocked group must still force nonmatch.
      ASSERT_EQ(OB_SUCCESS, possible.push_back(blocked));
    }
    ObSqlPlanSet cache;
    ASSERT_EQ(OB_SUCCESS, NativePcConstValuesTestPeer::install(cache, required, false));
    ASSERT_EQ(OB_SUCCESS, NativePcConstValuesTestPeer::install(cache, possible, true));
    const auto &stored = NativePcConstValuesTestPeer::at(cache, in_required ? 0 : 1, !in_required);
    EXPECT_FALSE(stored.cache_eligible_); EXPECT_EQ(0, stored.const_params_.count());
    expect_match(cache, params, false);
    bool matched = true;
    EXPECT_EQ(OB_SUCCESS, NativePcConstValuesTestPeer::match_cons(cache, cache_context, matched));
    EXPECT_FALSE(matched);
  }
  // The all-plan precedence rule must not hide an ineligible possible group.
  required.reuse(); possible.reuse();
  ASSERT_EQ(OB_SUCCESS, required.push_back(seven)); ASSERT_EQ(OB_SUCCESS, possible.push_back(blocked));
  ObSqlPlanSet cache;
  ASSERT_EQ(OB_SUCCESS, NativePcConstValuesTestPeer::install(cache, required, false));
  ASSERT_EQ(OB_SUCCESS, NativePcConstValuesTestPeer::install(cache, possible, true));
  expect_match(cache, params, false);
}

TEST(NativePcConstValues, sql_cache_admission_skips_both_ineligible_arrays_without_batch_rollback)
{
  ObArenaAllocator owner;
  ObExecContext context(owner);
  ObPCConstParamInfo eligible, blocked;
  ASSERT_EQ(OB_SUCCESS, eligible.add_int_constant(7, owner)); ASSERT_EQ(OB_SUCCESS, eligible.const_idx_.push_back(0));
  const DatumParameter resource = sql_array_parameter(owner);
  ASSERT_EQ(OB_SUCCESS, blocked.add_parameter(resource, &context, owner));
  ASSERT_FALSE(blocked.cache_eligible_);
  ASSERT_EQ(OB_SUCCESS, blocked.const_idx_.push_back(0));
  for (const bool batch : {false, true}) {
    for (const bool possible : {false, true}) {
      expect_admission(eligible, possible, batch, true);
      expect_admission(blocked, possible, batch, false);
    }
  }
}

TEST(NativePcConstValues, sql_udt_without_full_type_is_cache_ineligible_even_with_actual_context)
{
  ObArenaAllocator execution_owner;
  ObExecContext execution(execution_owner);
  ObSQLSessionInfo session;
  ObSQLSessionInfo::ExecCtxSessionRegister register_context(session, &execution);
  ASSERT_EQ(&execution, session.get_cur_exec_ctx());
  SqlTypeMeta declaration;
  declaration.set_type(ObUserDefinedSQLType);
  DatumParameter parameter;
  parameter.set_type_meta(declaration); parameter.set_param_meta(declaration);
  parameter.set_null(); // No SQL UDT resource address or invented FullType.
  ObRawExprResType original;
  original.set_type(ObUserDefinedSQLType); original.set_accuracy(parameter.get_accuracy());
  ConstValueAllocator owner;
  ObPCConstParamInfo from_parameter, from_raw;
  EXPECT_EQ(OB_SUCCESS, from_parameter.add_parameter(parameter, &execution, owner));
  EXPECT_FALSE(from_parameter.cache_eligible_);
  EXPECT_EQ(0, from_parameter.const_params_.count());
  EXPECT_EQ(OB_SUCCESS, from_raw.add_raw_constant(parameter.value_view(), original,
      &session, nullptr, owner));
  EXPECT_FALSE(from_raw.cache_eligible_);
  EXPECT_EQ(0, from_raw.const_params_.count());
  EXPECT_EQ(0, owner.calls_); // No fabricated declaration/value allocation.

  // The checked physical boundary rejects the former null-pointer/7-byte
  // image before it can become a native wrapper. Invalid metadata still tests
  // the actual capture preflight rather than becoming capability-ineligible.
  ObDatum malformed;
  malformed.ptr_ = nullptr; malformed.pack_ = 7;
  DatumParameter invalid = parameter;
  EXPECT_EQ(OB_INVALID_ARGUMENT, invalid.assign_legacy_borrowed(malformed, declaration));
  SqlTypeMeta malformed_meta; malformed_meta.set_type(ObMaxType);
  invalid.set_type_meta(malformed_meta);
  ObPCConstParamInfo invalid_parameter, invalid_raw;
  EXPECT_EQ(OB_INVALID_ARGUMENT, invalid_parameter.add_parameter(invalid, &execution, owner));
  EXPECT_TRUE(invalid_parameter.cache_eligible_);
  EXPECT_EQ(0, invalid_parameter.const_params_.count());
  EXPECT_EQ(OB_INVALID_ARGUMENT, invalid_raw.add_raw_constant(invalid.value_view(),
      original, &session, nullptr, owner));
  EXPECT_TRUE(invalid_raw.cache_eligible_);
  EXPECT_EQ(0, invalid_raw.const_params_.count());
  ObSQLSessionInfo::ExecCtxSessionRegister unregister_context(session, nullptr);
}

TEST(NativePcConstValues, cache_parameter_info_reads_final_metadata_and_sparse_group_flags)
{
  ObArenaAllocator source_owner, runtime_owner, statement_owner, descriptors;
  ParamStore inputs{ObWrapperAllocator(source_owner)};
  DatumParameter integer, typed_null, text, group;
  ASSERT_EQ(OB_SUCCESS, integer.set_int(INT64_MIN, source_owner));
  integer.set_accuracy(ObAccuracy(20, 0)); integer.set_scale(0);
  integer.set_param_meta(integer.get_type_meta());
  ParamFlag original_flags; original_flags.flag_ = 0x59; original_flags.extend_flag_ = 0xa4;
  integer.set_param_flag(original_flags);
  SqlTypeMeta decimal_meta; decimal_meta.set_decimal_int(7);
  SqlTypeMeta null_meta = decimal_meta; null_meta.set_null();
  typed_null.set_type_meta(null_meta); typed_null.datum_.set_null();
  typed_null.set_param_meta(decimal_meta); typed_null.set_accuracy(ObAccuracy(27, 7));
  char source_text[] = "cache metadata reference";
  ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString(sizeof(source_text) - 1, source_text), source_owner));
  text.set_collation_type(CS_TYPE_UTF8MB4_BIN); text.set_collation_level(CS_LEVEL_COERCIBLE);
  // String precision shares its field with length semantics. Keep that
  // attribute unspecified; 64 belongs to the independent length field.
  text.set_length(64); text.set_scale(0); text.set_param_meta(text.get_type_meta());
  auto *array = DatumParameterArray::alloc(source_owner, 1); ASSERT_NE(nullptr, array);
  ASSERT_EQ(OB_SUCCESS, array->data_[0].set_int(41, source_owner));
  array->data_[0].set_accuracy(ObAccuracy(20, 0)); array->data_[0].set_scale(0);
  array->data_[0].set_param_meta(array->data_[0].get_type_meta());
  SqlType leaf_type;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(array->data_[0].get_type_meta(),
      array->data_[0].get_accuracy(), leaf_type));
  ASSERT_EQ(OB_SUCCESS, array->set_element_type_borrowed(0, leaf_type, source_owner));
  array->element_.set_obj_type(ObIntType); array->element_.set_accuracy(ObAccuracy(20, 0));
  ASSERT_EQ(OB_SUCCESS, group.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, source_owner));
  group.set_param_meta(group.get_type_meta()); group.set_accuracy(ObAccuracy(20, 0));
  group.set_param_flag(original_flags);
  ASSERT_EQ(OB_SUCCESS, inputs.push_back(integer)); ASSERT_EQ(OB_SUCCESS, inputs.push_back(typed_null));
  ASSERT_EQ(OB_SUCCESS, inputs.push_back(text)); ASSERT_EQ(OB_SUCCESS, inputs.push_back(group));
  ObPhysicalPlan construction_plan;
  ASSERT_EQ(OB_SUCCESS, construction_plan.set_params_info(inputs));
  const auto &before = construction_plan.get_params_info(); ASSERT_EQ(4, before.count());
  EXPECT_EQ(ObIntType, before.at(0).type_); EXPECT_EQ(20, before.at(0).precision_);
  EXPECT_EQ(0, before.at(0).scale_); EXPECT_FALSE(before.at(0).is_typed_null_value_);
  EXPECT_EQ(ObDecimalIntType, before.at(1).type_); EXPECT_EQ(27, before.at(1).precision_);
  EXPECT_EQ(7, before.at(1).scale_); EXPECT_TRUE(before.at(1).is_typed_null_value_);
  EXPECT_EQ(CS_TYPE_BINARY, before.at(1).col_type_);
  EXPECT_EQ(ObVarcharType, before.at(2).type_); EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, before.at(2).col_type_);
  EXPECT_EQ(PRECISION_UNKNOWN_YET, before.at(2).precision_); EXPECT_EQ(0, before.at(2).scale_);
  EXPECT_EQ(ObExtendType, before.at(3).type_); EXPECT_EQ(ObIntType, before.at(3).ext_real_type_);
  EXPECT_EQ(20, before.at(3).precision_); EXPECT_EQ(0, before.at(3).scale_);

  ObExecContext execution(runtime_owner); ASSERT_EQ(OB_SUCCESS, execution.create_physical_plan_ctx());
  ObPhysicalPlanCtx &parameters = *execution.get_physical_plan_ctx(); parameters.set_original_param_cnt(4);
  ASSERT_EQ(OB_SUCCESS, parameters.assign_parameter_inputs(inputs));
  ASSERT_EQ(OB_SUCCESS, parameters.init_datum_param_store()); ASSERT_TRUE(parameters.native_parameters_ready());
  source_owner.reuse(); MEMSET(source_text, 'x', sizeof(source_text));
  ASSERT_NE(nullptr, parameters.get_native_parameter(1)); EXPECT_TRUE(parameters.get_native_parameter(1)->is_null());
  ASSERT_NE(nullptr, parameters.get_parameter_metadata(1));
  EXPECT_EQ(ObNullType, parameters.get_parameter_metadata(1)->type_meta_.get_type());
  EXPECT_EQ(ObDecimalIntType, parameters.get_parameter_metadata(1)->param_meta_.get_type());
  EXPECT_EQ(7, parameters.get_parameter_metadata(1)->accuracy_.get_scale());
  EXPECT_EQ(27, parameters.get_parameter_metadata(1)->accuracy_.get_precision());
  EXPECT_EQ(ObString::make_string("cache metadata reference"), parameters.get_native_parameter(2)->get_string());
  ObPhysicalPlan ready_plan;
  ASSERT_EQ(OB_SUCCESS, ready_plan.set_params_info(parameters));
  const auto &after = ready_plan.get_params_info(); ASSERT_EQ(4, after.count());
  for (int64_t i = 0; i < 4; ++i) {
    EXPECT_EQ(before.at(i).type_, after.at(i).type_); EXPECT_EQ(before.at(i).col_type_, after.at(i).col_type_);
    EXPECT_EQ(before.at(i).ext_real_type_, after.at(i).ext_real_type_);
    EXPECT_EQ(before.at(i).precision_, after.at(i).precision_); EXPECT_EQ(before.at(i).scale_, after.at(i).scale_);
    EXPECT_EQ(before.at(i).is_typed_null_value_, after.at(i).is_typed_null_value_);
    EXPECT_EQ(before.at(i).flag_.flag_, after.at(i).flag_.flag_);
    EXPECT_EQ(before.at(i).flag_.extend_flag_, after.at(i).flag_.extend_flag_);
  }
  ParamStore resolution{ObWrapperAllocator(descriptors)};
  ASSERT_EQ(OB_SUCCESS, parameters.build_resolution_inputs(resolution, statement_owner));
  ASSERT_EQ(4, resolution.count()); ASSERT_TRUE(resolution.at(3).is_ext_sql_array());
  ParamFlag changed_flags; changed_flags.flag_ = 0xad; changed_flags.extend_flag_ = 0x73;
  resolution.at(3).set_param_flag(changed_flags);
  ASSERT_EQ(OB_SUCCESS, parameters.merge_resolution_parameter_flags(resolution));
  ASSERT_NE(nullptr, parameters.get_group_parameter_input(3));
  EXPECT_EQ(original_flags.flag_, parameters.get_group_parameter_input(3)->get_param_flag().flag_);
  EXPECT_EQ(changed_flags.flag_, parameters.get_parameter_metadata(3)->flag_.flag_);
  EXPECT_EQ(41, parameters.get_native_parameter(3)->get_int());
  ObPhysicalPlan merged_plan; ASSERT_EQ(OB_SUCCESS, merged_plan.set_params_info(parameters));
  const auto &merged = merged_plan.get_params_info(); ASSERT_EQ(4, merged.count());
  EXPECT_EQ(changed_flags.flag_, merged.at(3).flag_.flag_);
  EXPECT_EQ(changed_flags.extend_flag_, merged.at(3).flag_.extend_flag_);
  EXPECT_EQ(ObExtendType, merged.at(3).type_); EXPECT_EQ(ObIntType, merged.at(3).ext_real_type_);
  EXPECT_EQ(20, merged.at(3).precision_); EXPECT_EQ(0, merged.at(3).scale_);
  EXPECT_TRUE(merged.at(1).is_typed_null_value_); EXPECT_EQ(7, merged.at(1).scale_);
}
