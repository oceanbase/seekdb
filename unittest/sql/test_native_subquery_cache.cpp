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
#include "sql/pl/ob_pl_user_type.h"
#include <functional>
#include <vector>
#include <string>
#include <csignal>
#include <unistd.h>
#include <sys/resource.h>
#include <gtest/gtest.h>
#include <array>
#include <cstring>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
// Test peer only prepares the actual store's nonspill environment. Retaining,
// writing, iteration and restoration all execute the production methods.
class NativeSubqueryCacheTestPeer
{
public:
  static int init_memory_store(ObSubQueryIterator &iterator, ObIAllocator &allocator)
  {
    iterator.store_.set_allocator(allocator);
    return iterator.store_.init(1L << 20, ObCtxIds::DEFAULT_CTX_ID, "SQRetainTest", false);
  }
};
}
}

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
    if (resource_step_) { return resource_step_(calls_); }
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
  int get_next_batch(int64_t maximum, const ObBatchRows *&rows) override
  {
    return batch_step_ ? batch_step_(maximum, rows) : ObOperator::get_next_batch(maximum, rows);
  }
  std::function<int(int)> resource_step_;
  std::function<int(int64_t, const ObBatchRows *&)> batch_step_;
  int calls_ = 0;
  int second_result_ = OB_ITER_END;
  bool string_result_ = false;
  char string_bytes_[sizeof("retained first row")]{};
};

const SqlType *scalar_record_type(ObIAllocator &metadata, uint64_t id = 801)
{
  SqlType integer; SqlTypeMeta meta; meta.set_int();
  EXPECT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(20, 0), integer));
  SqlTypeMember member;
  member.name = ObString::make_string("value"); member.type = &integer; member.nullable = true;
  SqlTypeSpec definition;
  definition.attributes.meta_.set_ext(); definition.attributes.meta_.set_extend_type(pl::PL_RECORD_TYPE);
  definition.attributes.set_udt_id(id); definition.form = SqlTypeForm::Record;
  definition.pl_declaration.source = SqlPLDeclarationSource::Local;
  definition.pl_declaration.tenant_id = OB_SERVER_RUNTIME_ID;
  definition.pl_declaration.declaration_id = id;
  definition.pl_declaration.compilation_scope.high = 0x725;
  definition.pl_declaration.compilation_scope.low = 0x339;
  definition.members = &member; definition.member_count = 1;
  const SqlType *type = nullptr;
  EXPECT_EQ(OB_SUCCESS, SqlType::create(definition, metadata, type));
  return type;
}

int scalar_record(ObIAllocator &allocator, const SqlType &type, int64_t number, Datum &output)
{
  const int64_t size = pl::ObRecordType::get_init_size(1);
  void *memory = allocator.alloc(size);
  int ret = memory == nullptr ? OB_ALLOCATE_MEMORY_FAILED : OB_SUCCESS;
  pl::ObPLRecord *record = nullptr;
  DatumParameter parameter;
  if (ret == OB_SUCCESS) {
    record = new (memory) pl::ObPLRecord(type.pl_declaration().declaration_id, 1);
    ret = record->init_data(allocator, false);
  }
  if (ret == OB_SUCCESS) {
    record->get_not_null()[0] = false;
    record->get_element_type()[0] = type.member(0).type->attributes();
    ret = record->get_element()[0].set_int(number, *record->get_allocator());
  }
  if (ret == OB_SUCCESS) {
    ret = parameter.set_extend(reinterpret_cast<int64_t>(record), pl::PL_RECORD_TYPE, allocator, size);
  }
  alignas(8) char scratch[64]; ObDatum legacy;
  if (ret == OB_SUCCESS) { ret = parameter.export_legacy(legacy, scratch, sizeof(scratch)); }
  if (ret == OB_SUCCESS) { ret = DatumCopyPlan(type).import_legacy(legacy, output); }
  if (ret == OB_SUCCESS && output.is_present() && parameter.datum_.is_inline_extended()) {
    // import_legacy validates the full declaration but borrows its input span.
    // The native producer must not return a view of this helper's local scratch.
    output = parameter.datum_;
  }
  return ret;
}

pl::ObPLRecord *record_payload(const SqlType &type, const Datum &value)
{
  alignas(8) char scratch[64];
  ObDatum legacy;
  EXPECT_EQ(OB_SUCCESS, DatumCopyPlan(type).legacy_view(value, legacy, scratch, sizeof(scratch)));
  return reinterpret_cast<pl::ObPLRecord *>(legacy.get_ext());
}

int64_t record_number(const SqlType &type, const Datum &value)
{
  pl::ObPLRecord *record = record_payload(type, value);
  EXPECT_NE(nullptr, record);
  return record == nullptr ? 0 : record->get_element()[0].get_int();
}

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
  ASSERT_EQ(OB_SUCCESS, array->set_element_type_borrowed(0, element, source_owner));
  DatumParameter parameter;
  ASSERT_EQ(OB_SUCCESS, parameter.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, source_owner));
  Datum source;
  alignas(8) char source_scratch[64]; ObDatum source_view;
  ASSERT_EQ(OB_SUCCESS, parameter.export_legacy(source_view, source_scratch, sizeof(source_scratch)));
  ASSERT_EQ(OB_SUCCESS, DatumCopyPlan(*type).import_legacy(source_view, source));
  ObSubQueryIterator::PendingCacheEntry pending;
  ASSERT_EQ(OB_SUCCESS, iterator_.prepare_cache_result(source, result_expr_, pending));
  source_owner.reset();
  ASSERT_EQ(OB_SUCCESS, iterator_.publish_cache_result(pending));
  Datum out; ASSERT_EQ(OB_SUCCESS, lookup(out));
  ObDatum legacy;
  ASSERT_EQ(OB_SUCCESS, DatumCopyPlan(*pending.result_type_).borrow_legacy(out, legacy));
  DatumParameter retained;
  ASSERT_EQ(OB_SUCCESS, retained.assign_legacy_borrowed(legacy, type->legacy_meta()));
  const auto *owned = retained.get_array<DatumParameterArray>(); ASSERT_NE(nullptr, owned);
  ASSERT_EQ(1, owned->count_);
  ASSERT_NE(nullptr, owned->get_element_type(0));
  EXPECT_TRUE(element.equals(*owned->get_element_type(0)));
  EXPECT_NE(&element, owned->get_element_type(0));
  EXPECT_EQ(ObString::make_string("resource leaf independently owned by the pending cache"),
            owned->data_[0].get_string());
}

TEST_F(NativeSubqueryCache, actual_record_first_advance_and_cache_hit_survive_their_source_owners)
{
  const SqlType *type = scalar_record_type(owner_); ASSERT_NE(nullptr, type);
  ASSERT_EQ(OB_SUCCESS, child_expr_.bind_sql_type(*type, owner_));
  ASSERT_EQ(OB_SUCCESS, result_expr_.bind_sql_type(*type, owner_));
  ObArenaAllocator source_owner;
  child_.resource_step_ = [&](int call) {
    if (call > 1) { source_owner.reset(); return OB_ITER_END; }
    const int ret = scalar_record(source_owner, *type, INT64_MIN,
                                  child_expr_.locate_expr_datum(child_.get_eval_ctx()));
    if (ret == OB_SUCCESS) {
      const Datum &first = child_expr_.locate_expr_datum(child_.get_eval_ctx());
      EXPECT_TRUE(first.is_inline_extended()); // scalar_record's scratch has retired.
      EXPECT_EQ(INT64_MIN, record_number(*type, first));
    }
    child_expr_.set_evaluated_projected(child_.get_eval_ctx());
    return ret;
  };
  ASSERT_EQ(OB_SUCCESS, ObExprSubQueryRef::expr_eval(result_expr_, child_.get_eval_ctx(), result()));
  EXPECT_EQ(2, child_.calls_); EXPECT_EQ(INT64_MIN, record_number(*type, result()));
  ASSERT_EQ(OB_SUCCESS, ObExprSubQueryRef::expr_eval(result_expr_, child_.get_eval_ctx(), result()));
  EXPECT_EQ(0, child_.calls_); // The callback rewound, then used the cache.
  EXPECT_EQ(INT64_MIN, record_number(*type, result()));
  ASSERT_EQ(OB_SUCCESS, iterator_.reset_hash_map());
  EXPECT_EQ(INT64_MIN, record_number(*type, result())); // Execution snapshot is independent of cache clear.
  ASSERT_EQ(OB_SUCCESS, parent_.close_owned_values());
}

TEST_F(NativeSubqueryCache, actual_record_cardinality_or_child_failure_never_publishes_pending_cache)
{
  const SqlType *type = scalar_record_type(owner_); ASSERT_NE(nullptr, type);
  ASSERT_EQ(OB_SUCCESS, child_expr_.bind_sql_type(*type, owner_));
  ASSERT_EQ(OB_SUCCESS, result_expr_.bind_sql_type(*type, owner_));
  for (int second : {OB_SUCCESS, OB_ERR_UNEXPECTED}) {
    ASSERT_EQ(OB_SUCCESS, iterator_.reset_hash_map());
    ObArenaAllocator source_owner;
    child_.resource_step_ = [&](int call) {
      if (call > 1) { source_owner.reset(); return second; }
      Datum &first = child_expr_.locate_expr_datum(child_.get_eval_ctx());
      const int ret = scalar_record(source_owner, *type, 81, first);
      if (ret == OB_SUCCESS) {
        EXPECT_TRUE(first.is_inline_extended()); // Validate after the helper has returned.
        EXPECT_EQ(81, record_number(*type, first));
      }
      return ret;
    };
    EXPECT_EQ(second == OB_SUCCESS ? OB_SUBQUERY_TOO_MANY_ROW : second,
              ObExprSubQueryRef::expr_eval(result_expr_, child_.get_eval_ctx(), result()));
    EXPECT_EQ(2, child_.calls_);
    Datum out; EXPECT_EQ(OB_HASH_NOT_EXIST, lookup(out));
    EXPECT_EQ(81, record_number(*type, result()));
    ASSERT_EQ(OB_SUCCESS, parent_.close_owned_values());
  }
  child_.resource_step_ = nullptr; // No callback retains the last local source allocator.
}

TEST_F(NativeSubqueryCache, resource_correlation_keys_bypass_lookup_and_insertion_without_changing_values)
{
  const SqlType *type = scalar_record_type(owner_); ASSERT_NE(nullptr, type);
  ASSERT_EQ(OB_SUCCESS, key_expr_.bind_sql_type(*type, owner_));
  ObArenaAllocator source_owner;
  ASSERT_EQ(OB_SUCCESS, scalar_record(source_owner, *type, 91, key()));
  for (int i = 0; i < 2; ++i) {
    ASSERT_EQ(OB_SUCCESS, ObExprSubQueryRef::expr_eval(result_expr_, child_.get_eval_ctx(), result()));
    EXPECT_EQ(2, child_.calls_); EXPECT_EQ(72, result().get_int());
    ASSERT_EQ(OB_SUCCESS, iterator_.get_curr_probe_row());
    Datum out; out.set_int(994);
    EXPECT_EQ(OB_HASH_NOT_EXIST, lookup(out)); EXPECT_EQ(994, out.get_int());
    ObSubQueryIterator::PendingCacheEntry pending;
    ASSERT_EQ(OB_SUCCESS, iterator_.prepare_cache_result(result(), result_expr_, pending));
    EXPECT_FALSE(pending.ready_);
  }
}

TEST_F(NativeSubqueryCache, cache_resource_clear_failure_preserves_arena_and_retries_before_reuse)
{
  const SqlType *type = scalar_record_type(owner_); ASSERT_NE(nullptr, type);
  ASSERT_EQ(OB_SUCCESS, result_expr_.bind_sql_type(*type, owner_));
  ObArenaAllocator source_owner;
  Datum source; ASSERT_EQ(OB_SUCCESS, scalar_record(source_owner, *type, 101, source));
  ObSubQueryIterator::PendingCacheEntry pending;
  ASSERT_EQ(OB_SUCCESS, iterator_.prepare_cache_result(source, result_expr_, pending));
  ASSERT_TRUE(pending.ready_);
  ASSERT_EQ(OB_SUCCESS, iterator_.publish_cache_result(pending));
  source_owner.reset();
  auto *owned = record_payload(*pending.result_type_, pending.value_);
  ASSERT_NE(nullptr, owned);
  ObIAllocator *arena = nullptr;
  ASSERT_EQ(OB_SUCCESS, iterator_.get_arena_allocator(arena));
  auto &cache_arena = static_cast<ObArenaAllocator &>(*arena);
  const int64_t used = cache_arena.used();
  owned->set_count(-1); // Safe structural error: release rejects before dereferencing fields.
  EXPECT_EQ(OB_INVALID_ARGUMENT, iterator_.reset_hash_map());
  EXPECT_EQ(used, cache_arena.used());
  owned->set_count(1);
  ASSERT_EQ(OB_SUCCESS, iterator_.reset_hash_map());
  EXPECT_LT(cache_arena.used(), used);
  ASSERT_EQ(OB_SUCCESS, parent_.close_owned_values());
}

TEST_F(NativeSubqueryCache, actual_spf_terminal_destroy_aborts_before_discarding_an_uncleared_graph)
{
  const SqlType *type = scalar_record_type(owner_); ASSERT_NE(nullptr, type);
  ObArenaAllocator source_owner;
  Datum source, retained;
  ASSERT_EQ(OB_SUCCESS, scalar_record(source_owner, *type, 111, source));
  ASSERT_EQ(OB_SUCCESS, parent_.retain_scalar_result(source, *type, retained));
  auto *owned = record_payload(*type, retained); ASSERT_NE(nullptr, owned);
  EXPECT_EXIT({
    rlimit no_core; no_core.rlim_cur = 0; no_core.rlim_max = 0;
    setrlimit(RLIMIT_CORE, &no_core);
    signal(SIGALRM, SIG_DFL); alarm(3);
    owned->set_count(-1);
    parent_.destroy();
    _exit(99);
  }, ::testing::KilledBySignal(SIGABRT), "OB_ABORT");
  // Death-test mutation was child-local; the parent graph can close normally.
  EXPECT_EQ(1, owned->get_count());
  ASSERT_EQ(OB_SUCCESS, parent_.close_owned_values());
}


TEST_F(NativeSubqueryCache, init_plan_nonspill_scalar_retains_resources_before_next_child_and_checks_type_rebind)
{
  const SqlType *type = scalar_record_type(owner_); ASSERT_NE(nullptr, type);
  ASSERT_EQ(OB_SUCCESS, child_expr_.bind_sql_type(*type, owner_));
  child_spec_.output_.reset();
  ASSERT_EQ(OB_SUCCESS, child_spec_.output_.init(2));
  ASSERT_EQ(OB_SUCCESS, child_spec_.output_.push_back(&child_expr_));
  ASSERT_EQ(OB_SUCCESS, child_spec_.output_.push_back(&key_expr_));
  ASSERT_EQ(OB_SUCCESS, NativeSubqueryCacheTestPeer::init_memory_store(iterator_, owner_));
  iterator_.set_init_plan();
  ObArenaAllocator source_owner;
  child_.resource_step_ = [&](int call) {
    source_owner.reset();
    return call > 2 ? OB_ITER_END : scalar_record(source_owner, *type, 200 + call,
        child_expr_.locate_expr_datum(child_.get_eval_ctx()));
  };
  ASSERT_EQ(OB_SUCCESS, iterator_.prepare_init_plan()); EXPECT_EQ(3, child_.calls_);
  ASSERT_EQ(OB_SUCCESS, iterator_.get_next_row());
  EXPECT_EQ(201, record_number(*type, child_expr_.locate_expr_datum(child_.get_eval_ctx())));
  EXPECT_EQ(17, key().get_int());
  const SqlType *different = scalar_record_type(owner_, 802); ASSERT_NE(nullptr, different);
  ASSERT_EQ(OB_SUCCESS, child_expr_.bind_sql_type(*different, owner_));
  EXPECT_EQ(OB_INVALID_ARGUMENT, iterator_.get_next_row());
  ASSERT_EQ(OB_SUCCESS, child_expr_.bind_sql_type(*type, owner_));
  ASSERT_EQ(OB_SUCCESS, iterator_.get_next_row()); // Rejected restoration did not advance the store.
  EXPECT_EQ(202, record_number(*type, child_expr_.locate_expr_datum(child_.get_eval_ctx())));
  EXPECT_EQ(OB_ITER_END, iterator_.get_next_row());
  ASSERT_EQ(OB_SUCCESS, parent_.close_owned_values());
  child_.resource_step_ = nullptr;
}

TEST_F(NativeSubqueryCache, init_plan_nonspill_batch_retains_only_unskipped_resource_rows)
{
  const SqlType *type = scalar_record_type(owner_); ASSERT_NE(nullptr, type);
  ASSERT_EQ(OB_SUCCESS, child_expr_.bind_sql_type(*type, owner_));
  child_spec_.output_.reset();
  ASSERT_EQ(OB_SUCCESS, child_spec_.output_.init(2));
  ASSERT_EQ(OB_SUCCESS, child_spec_.output_.push_back(&child_expr_));
  ASSERT_EQ(OB_SUCCESS, child_spec_.output_.push_back(&key_expr_));
  child_spec_.max_batch_size_ = 3;
  child_.get_eval_ctx().set_max_batch_size(3);
  child_expr_.datum_off_ = 256; child_expr_.eval_info_off_ = 384; child_expr_.eval_flags_off_ = 416;
  child_expr_.batch_result_ = true; child_expr_.batch_idx_mask_ = UINT64_MAX;
  for (int64_t i = 0; i < 3; ++i) { new (frame_.data() + 256 + i * sizeof(Datum)) Datum; }
  new (frame_.data() + 384) ObEvalInfo;
  ASSERT_EQ(OB_SUCCESS, NativeSubqueryCacheTestPeer::init_memory_store(iterator_, owner_));
  iterator_.set_init_plan();
  alignas(8) uint64_t skip_bits = 0;
  ObBitVector *skip = to_bit_vector(&skip_bits); skip->set(1);
  ObBatchRows rows(*skip, 3, false);
  ObArenaAllocator source_owner;
  int batch_calls = 0;
  child_.batch_step_ = [&](int64_t, const ObBatchRows *&output) {
    ++batch_calls;
    source_owner.reset();
    int ret = OB_SUCCESS;
    if (batch_calls == 1) {
      for (int64_t i = 0; ret == OB_SUCCESS && i < 3; ++i) {
        ret = scalar_record(source_owner, *type, 301 + i,
            child_expr_.locate_batch_datums(child_.get_eval_ctx())[i]);
      }
      child_expr_.set_evaluated_projected(child_.get_eval_ctx());
    } else { rows.size_ = 0; rows.end_ = true; }
    output = &rows;
    return ret;
  };
  ASSERT_EQ(OB_SUCCESS, iterator_.prepare_init_plan()); EXPECT_EQ(2, batch_calls);
  ObEvalCtx::BatchInfoScopeGuard output_guard(child_.get_eval_ctx());
  output_guard.set_batch_idx(0);
  for (int64_t expected : {301, 303}) {
    ASSERT_EQ(OB_SUCCESS, iterator_.get_next_row());
    EXPECT_EQ(expected, record_number(*type, child_expr_.locate_expr_datum(child_.get_eval_ctx())));
    EXPECT_EQ(17, key().get_int());
  }
  EXPECT_EQ(OB_ITER_END, iterator_.get_next_row());
  ASSERT_EQ(OB_SUCCESS, parent_.close_owned_values());
  child_.batch_step_ = nullptr;
}


TEST_F(NativeSubqueryCache, cached_record_children_are_accounted_in_the_arena_soft_limit)
{
  constexpr int64_t columns = 18;
  constexpr int64_t text_bytes = 60000; // Each VARCHAR fits its actual declared length.
  SqlType text;
  SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObAccuracy accuracy; accuracy.set_length(text_bytes);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, text));
  std::vector<SqlTypeMember> members(columns);
  std::vector<std::string> names(columns);
  for (int64_t i = 0; i < columns; ++i) {
    names[i] = "v" + std::to_string(i);
    members[i].name = ObString(names[i].size(), names[i].data()); members[i].type = &text;
  }
  SqlTypeSpec definition;
  definition.attributes.meta_.set_ext(); definition.attributes.meta_.set_extend_type(pl::PL_RECORD_TYPE);
  definition.attributes.set_udt_id(803); definition.form = SqlTypeForm::Record;
  definition.pl_declaration.source = SqlPLDeclarationSource::Local;
  definition.pl_declaration.tenant_id = OB_SERVER_RUNTIME_ID;
  definition.pl_declaration.declaration_id = 803;
  definition.pl_declaration.compilation_scope.low = 0x4405;
  definition.members = members.data(); definition.member_count = columns;
  const SqlType *type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(definition, owner_, type));
  ASSERT_EQ(OB_SUCCESS, result_expr_.bind_sql_type(*type, owner_));
  ObArenaAllocator source_owner;
  void *memory = source_owner.alloc(pl::ObRecordType::get_init_size(columns)); ASSERT_NE(nullptr, memory);
  auto *record = new (memory) pl::ObPLRecord(803, columns);
  ASSERT_EQ(OB_SUCCESS, record->init_data(source_owner, false));
  const std::string bytes(text_bytes, 'v');
  for (int64_t i = 0; i < columns; ++i) {
    record->get_not_null()[i] = false;
    record->get_element_type()[i] = type->member(i).type->attributes();
    ASSERT_EQ(OB_SUCCESS, record->get_element()[i].set_varchar(
        ObString(bytes.size(), bytes.data()), *record->get_allocator()));
  }
  DatumParameter parameter;
  ASSERT_EQ(OB_SUCCESS, parameter.set_extend(reinterpret_cast<int64_t>(record), pl::PL_RECORD_TYPE,
      source_owner, pl::ObRecordType::get_init_size(columns)));
  Datum source;
  alignas(8) char source_scratch[64]; ObDatum source_view;
  ASSERT_EQ(OB_SUCCESS, parameter.export_legacy(source_view, source_scratch, sizeof(source_scratch)));
  ASSERT_EQ(OB_SUCCESS, DatumCopyPlan(*type).import_legacy(source_view, source));
  ObSubQueryIterator::PendingCacheEntry first;
  ASSERT_EQ(OB_SUCCESS, iterator_.prepare_cache_result(source, result_expr_, first));
  ASSERT_TRUE(first.ready_); // The historical insertion estimate only sees the root carrier span.
  EXPECT_FALSE(iterator_.check_can_insert(1)); // Recursive children counted by actual cache-arena used().
  ObSubQueryIterator::PendingCacheEntry second;
  ASSERT_EQ(OB_SUCCESS, iterator_.prepare_cache_result(source, result_expr_, second));
  EXPECT_FALSE(second.ready_);
  ASSERT_EQ(OB_SUCCESS, iterator_.reset_hash_map());
  EXPECT_TRUE(iterator_.check_can_insert(1));
}

} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
