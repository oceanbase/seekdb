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

// The handler header uses these names from its production include context.
#include "share/ob_server_struct.h"
#include "sql/ob_query_retry_ctrl.h"
using oceanbase::share::ObGlobalContext;
using oceanbase::sql::ObQueryRetryCtrl;
#include "observer/mysql/obmp_stmt_execute.h"
#include "common/datum/datum.h"
#include "common/datum/sql_type.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/engine/expr/ob_expr.h"
#include "sql/plan_cache/ob_prepare_stmt_struct.h"
#include "sql/session/ob_piece_cache.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::observer;
using namespace oceanbase::obmysql;

namespace oceanbase
{
namespace sql
{
// Existing production evaluator; the test uses its real parameter read path.
int eval_question_mark_func(const ObExpr &, ObEvalCtx &, ObDatum &);
}
namespace observer
{
struct NativePsParameterBindingTestAccess
{
  static void configure(ObMPStmtExecute &handler, ParamStore &params, ObPsStmtId id,
                        stmt::StmtType type = stmt::T_SELECT)
  {
    handler.params_ = &params;
    handler.stmt_id_ = id;
    handler.stmt_type_ = type;
  }
  static int bind(ObMPStmtExecute &handler, ObSQLSessionInfo &session,
                  ObPsSessionInfo &info, const std::string &tail,
                  ObIAllocator &allocator, bool &handled)
  {
    const char *pos = tail.data();
    handler.analysis_checker_.init(pos, tail.size());
    const int ret = handler.request_standard_params(&session, info, pos, allocator, handled);
    handler.analysis_checker_ = ObPSAnalysisChecker();
    return ret;
  }
  static bool active(const ObMPStmtExecute &handler)
  { return handler.native_parameter_owner_ != nullptr; }
  static const Datum *values(const ObMPStmtExecute &handler)
  { return active(handler) ? handler.native_parameter_owner_->values_ : nullptr; }
  static const SqlType *type(const ObMPStmtExecute &handler)
  { return active(handler) ? handler.native_parameter_owner_->type_ : nullptr; }
  static int64_t count(const ObMPStmtExecute &handler)
  { return active(handler) ? handler.native_parameter_owner_->count_ : 0; }
  static int64_t owner_bytes(int64_t count)
  { return sizeof(ObMPStmtExecute::NativeParameterOwner) + count * sizeof(Datum); }
  static void poison_values(ObMPStmtExecute &handler)
  {
    if (active(handler)) {
      for (int64_t i = 0; i < handler.native_parameter_owner_->count_; ++i) {
        handler.native_parameter_owner_->values_[i].set_int(INT64_C(0x5a5a5a5a5a5a5a5a));
      }
    }
  }
  static void cursor(ObMPStmtExecute &handler, bool enabled)
  { handler.ps_cursor_type_ = enabled ? ObExecutePsCursorType : ObNormalType; }
  static void array(ObMPStmtExecute &handler, bool enabled)
  { handler.is_arraybinding_ = enabled; }
  static void statement(ObMPStmtExecute &handler, stmt::StmtType type)
  { handler.stmt_type_ = type; }
};
}
}

namespace
{
using Access = NativePsParameterBindingTestAccess;

class RequestAllocator final : public ObIAllocator
{
public:
  RequestAllocator() : arena_("PsReqOwner"), fail_size_(0) {}
  void *alloc(int64_t size) override
  {
    attempts_.push_back(size);
    if (size == fail_size_) { return nullptr; }
    return arena_.alloc(size);
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *ptr) override { arena_.free(ptr); }
  int64_t calls_for(int64_t size) const
  {
    int64_t count = 0;
    for (const int64_t actual : attempts_) { count += actual == size; }
    return count;
  }
  void fail_size(int64_t size) { fail_size_ = size; }
private:
  ObArenaAllocator arena_;
  int64_t fail_size_;
  std::vector<int64_t> attempts_;
};

struct WireParam
{
  EMySQLFieldType type_;
  uint8_t flags_;
  bool null_;
  std::string bytes_;
};

std::string little_endian(uint64_t value, int width)
{
  std::string result;
  for (int i = 0; i < width; ++i) { result.push_back(static_cast<char>(value >> (8 * i))); }
  return result;
}

WireParam integer(int64_t value, uint8_t flags = 0)
{ return {MYSQL_TYPE_LONGLONG, flags, false, little_endian(static_cast<uint64_t>(value), 8)}; }
WireParam integer32(int32_t value)
{ return {MYSQL_TYPE_LONG, 0, false, little_endian(static_cast<uint32_t>(value), 4)}; }
WireParam bytes(EMySQLFieldType type, const std::string &value)
{
  EXPECT_LT(value.size(), 251u);
  return {type, 0, false, std::string(1, static_cast<char>(value.size())) + value};
}
WireParam floating(double value)
{
  uint64_t bits = 0;
  MEMCPY(&bits, &value, sizeof(bits));
  return {MYSQL_TYPE_DOUBLE, 0, false, little_endian(bits, 8)};
}
std::string packet(const std::vector<WireParam> &params, bool new_bound = true)
{
  if (params.empty()) { return std::string(); }
  std::string tail((params.size() + 7) / 8, '\0');
  for (size_t i = 0; i < params.size(); ++i) {
    if (params[i].null_) { tail[i / 8] |= static_cast<char>(1u << (i % 8)); }
  }
  tail.push_back(new_bound ? 1 : 0);
  if (new_bound) {
    for (const WireParam &param : params) {
      tail.push_back(static_cast<char>(param.type_));
      tail.push_back(static_cast<char>(param.flags_));
    }
  }
  for (const WireParam &param : params) {
    if (!param.null_) { tail += param.bytes_; }
  }
  return tail;
}

void same_parameter(const DatumParameter &expected, const DatumParameter &actual)
{
  EXPECT_EQ(expected.get_type_meta(), actual.get_type_meta());
  EXPECT_EQ(expected.get_type_meta().get_scale(), actual.get_type_meta().get_scale());
  EXPECT_EQ(expected.get_accuracy(), actual.get_accuracy());
  EXPECT_EQ(expected.get_param_meta(), actual.get_param_meta());
  EXPECT_EQ(expected.get_param_meta().get_scale(), actual.get_param_meta().get_scale());
  EXPECT_EQ(expected.get_result_flag(), actual.get_result_flag());
  EXPECT_EQ(expected.get_param_flag().flag_, actual.get_param_flag().flag_);
  EXPECT_EQ(expected.get_param_flag().extend_flag_, actual.get_param_flag().extend_flag_);
  EXPECT_EQ(expected.get_raw_text_pos(), actual.get_raw_text_pos());
  EXPECT_EQ(expected.get_raw_text_len(), actual.get_raw_text_len());
  EXPECT_EQ(expected.datum_.pack_, actual.datum_.pack_);
  EXPECT_EQ(expected.get_int(), actual.get_int());
}

class NativePsParameterBinding : public testing::Test
{
protected:
  NativePsParameterBinding()
    : metadata_("PsBindMeta"), descriptors_("PsBindDesc"), request_(),
      params_(ObWrapperAllocator(descriptors_)), session_(),
      handler_(ObGlobalContext::get_instance()), stmt_id_(0), info_(nullptr), prepared_(false) {}

  void SetUp() override
  {
    ASSERT_EQ(OB_SUCCESS, session_.test_init(1, 1, nullptr));
    ASSERT_EQ(OB_SUCCESS, session_.load_default_sys_variable(false, false));
  }
  void TearDown() override
  {
    if (prepared_) { EXPECT_EQ(OB_SUCCESS, session_.remove_ps_session_info(stmt_id_)); }
  }
  void prepare(int64_t count, stmt::StmtType type = stmt::T_SELECT)
  {
    if (prepared_) {
      ASSERT_EQ(OB_SUCCESS, session_.remove_ps_session_info(stmt_id_));
      prepared_ = false;
      info_ = nullptr;
    }
    ObPsStmtInfo statement(&metadata_);
    statement.set_stmt_type(type);
    ASSERT_EQ(OB_SUCCESS, statement.reserve_ps_meta_fields(count, 0));
    for (int64_t i = 0; i < count; ++i) {
      ObField field;
      field.type_meta_.set_int();
      ASSERT_EQ(OB_SUCCESS, statement.add_param_field(field));
    }
    bool already_exists = false;
    ASSERT_EQ(OB_SUCCESS, session_.prepare_ps_stmt(71, &statement, stmt_id_, already_exists, false));
    prepared_ = true;
    ASSERT_FALSE(already_exists);
    ASSERT_EQ(OB_SUCCESS, session_.get_ps_session_info(stmt_id_, info_));
    ASSERT_NE(nullptr, info_);
    Access::configure(handler_, params_, stmt_id_, type);
  }
  int bind(const std::string &tail, bool &handled)
  {
    params_.reuse();
    return Access::bind(handler_, session_, *info_, tail, request_, handled);
  }
  void successful(const std::vector<WireParam> &values, bool new_bound = true)
  {
    bool handled = false;
    ASSERT_EQ(OB_SUCCESS, bind(packet(values, new_bound), handled));
    ASSERT_TRUE(handled); // The real Rust standard parser handled this packet.
    ASSERT_EQ(values.size(), static_cast<size_t>(params_.count()));
  }
  void native_values(const std::vector<int64_t> &expected)
  {
    ASSERT_TRUE(Access::active(handler_));
    ASSERT_EQ(expected.size(), static_cast<size_t>(Access::count(handler_)));
    const Datum *values = Access::values(handler_);
    ASSERT_NE(nullptr, values);
    ASSERT_NE(nullptr, Access::type(handler_));
    EXPECT_EQ(ObIntType, Access::type(handler_)->kind());
    EXPECT_EQ(20, Access::type(handler_)->precision());
    EXPECT_EQ(0, Access::type(handler_)->scale());
    EXPECT_EQ(CS_LEVEL_COERCIBLE, Access::type(handler_)->legacy_meta().get_collation_level());
    EXPECT_EQ(0u, reinterpret_cast<uintptr_t>(values) % alignof(Datum));
    for (size_t i = 0; i < expected.size(); ++i) {
      SCOPED_TRACE(i);
      ASSERT_TRUE(values[i].is_present());
      EXPECT_EQ(0u, values[i].length());
      EXPECT_EQ(Datum::NoReferenceFlags, values[i].reference_flags());
      EXPECT_EQ(expected[i], values[i].get_int());
      EXPECT_EQ(expected[i], params_.at(i).get_int());
      EXPECT_EQ(ObIntType, params_.at(i).get_type());
      EXPECT_EQ(CS_LEVEL_COERCIBLE, params_.at(i).get_collation_level());
      EXPECT_EQ(CS_TYPE_BINARY, params_.at(i).get_collation_type());
      EXPECT_EQ(sizeof(int64_t), params_.at(i).datum_.len_);
      EXPECT_EQ(ObDatumDesc::NONE, params_.at(i).datum_.flag_);
      const uintptr_t slot = reinterpret_cast<uintptr_t>(&values[i]);
      const uintptr_t payload = reinterpret_cast<uintptr_t>(params_.at(i).datum_.ptr_);
      EXPECT_GE(payload, slot);
      EXPECT_LE(payload + sizeof(int64_t), slot + sizeof(Datum));
    }
  }

  ObArenaAllocator metadata_;
  ObArenaAllocator descriptors_;
  RequestAllocator request_;
  ParamStore params_;
  ObSQLSessionInfo session_;
  ObMPStmtExecute handler_;
  ObPsStmtId stmt_id_;
  ObPsSessionInfo *info_;
  bool prepared_;
};
}

TEST_F(NativePsParameterBinding, one_parameter_rebinds_extremes_with_cached_wire_type)
{
  ASSERT_NO_FATAL_FAILURE(prepare(1));
  ASSERT_NO_FATAL_FAILURE(successful({integer(INT64_MIN)}));
  ASSERT_NO_FATAL_FAILURE(native_values({INT64_MIN}));
  const SqlType *type = Access::type(handler_);
  const Datum *old_values = Access::values(handler_);
  ASSERT_NO_FATAL_FAILURE(successful({integer(INT64_MAX)}, false));
  ASSERT_NO_FATAL_FAILURE(native_values({INT64_MAX}));
  EXPECT_EQ(type, Access::type(handler_));
  EXPECT_NE(old_values, Access::values(handler_));
  EXPECT_EQ(INT64_MIN, old_values[0].get_int());
  ASSERT_NO_FATAL_FAILURE(successful({integer(0)}, false));
  ASSERT_NO_FATAL_FAILURE(native_values({0}));
  EXPECT_EQ(0, request_.calls_for(sizeof(int64_t)));
}

TEST_F(NativePsParameterBinding, three_parameters_preserve_metadata_and_remove_scalar_allocations)
{
  ASSERT_NO_FATAL_FAILURE(prepare(3));
  const std::vector<WireParam> input = {integer(INT64_MIN, 0x40), integer(-7), integer(INT64_MAX)};
  Access::cursor(handler_, true); // Same real binder, original allocating branch.
  ASSERT_NO_FATAL_FAILURE(successful(input));
  ASSERT_FALSE(Access::active(handler_));
  EXPECT_EQ(3, request_.calls_for(sizeof(int64_t)));
  std::vector<DatumParameter> legacy;
  for (int64_t i = 0; i < params_.count(); ++i) { legacy.push_back(params_.at(i)); }
  Access::cursor(handler_, false);
  const int64_t before_owner = request_.calls_for(Access::owner_bytes(3));
  ASSERT_NO_FATAL_FAILURE(successful(input));
  ASSERT_NO_FATAL_FAILURE(native_values({INT64_MIN, -7, INT64_MAX}));
  EXPECT_EQ(3, request_.calls_for(sizeof(int64_t)));
  EXPECT_EQ(before_owner + 1, request_.calls_for(Access::owner_bytes(3)));
  for (int64_t i = 0; i < params_.count(); ++i) { same_parameter(legacy[i], params_.at(i)); }
  EXPECT_EQ(0x40, info_->get_param_type_flags().at(0));
  EXPECT_EQ(MYSQL_TYPE_LONGLONG, info_->get_param_types().at(0));
}

TEST_F(NativePsParameterBinding, many_parameters_use_one_stable_owner)
{
  ASSERT_NO_FATAL_FAILURE(prepare(65));
  std::vector<WireParam> input;
  std::vector<int64_t> expected;
  for (int64_t i = 0; i < 65; ++i) {
    expected.push_back(i * 1000000007 - 9000000011);
    input.push_back(integer(expected.back()));
  }
  ASSERT_NO_FATAL_FAILURE(successful(input));
  ASSERT_NO_FATAL_FAILURE(native_values(expected));
  EXPECT_EQ(1, request_.calls_for(Access::owner_bytes(65)));
  EXPECT_EQ(0, request_.calls_for(sizeof(int64_t)));
  EXPECT_EQ(65, info_->get_param_types().count());
}

TEST_F(NativePsParameterBinding, binder_admission_does_not_require_select_or_three_parameters)
{
  ASSERT_NO_FATAL_FAILURE(prepare(4, stmt::T_INSERT));
  ASSERT_NO_FATAL_FAILURE(successful({integer(1), integer(2), integer(3), integer(4)}));
  ASSERT_NO_FATAL_FAILURE(native_values({1, 2, 3, 4}));
}

TEST_F(NativePsParameterBinding, same_statement_changes_types_and_null_then_returns_to_native)
{
  ASSERT_NO_FATAL_FAILURE(prepare(1));
  for (int round = 0; round < 3; ++round) {
    SCOPED_TRACE(round);
    ASSERT_NO_FATAL_FAILURE(successful({integer(7)}));
    ASSERT_NO_FATAL_FAILURE(native_values({7}));
    ASSERT_NO_FATAL_FAILURE(successful({integer32(1)}));
    EXPECT_FALSE(Access::active(handler_));
    EXPECT_EQ(ObInt32Type, params_.at(0).get_type());
    EXPECT_EQ(1, params_.at(0).get_int());
    ASSERT_NO_FATAL_FAILURE(successful({integer(-1, 0x80)}));
    EXPECT_FALSE(Access::active(handler_));
    EXPECT_EQ(ObUInt64Type, params_.at(0).get_type());
    EXPECT_EQ(UINT64_MAX, params_.at(0).get_uint64());
    ASSERT_NO_FATAL_FAILURE(successful({bytes(MYSQL_TYPE_VAR_STRING, "7")}));
    EXPECT_FALSE(Access::active(handler_));
    EXPECT_EQ(ObVarcharType, params_.at(0).get_type());
    EXPECT_EQ(ObString("7"), params_.at(0).get_string());
    ASSERT_NO_FATAL_FAILURE(successful({floating(1.0)}));
    EXPECT_FALSE(Access::active(handler_));
    EXPECT_EQ(ObDoubleType, params_.at(0).get_type());
    EXPECT_EQ(1.0, params_.at(0).get_double());
    ASSERT_NO_FATAL_FAILURE(successful({bytes(MYSQL_TYPE_NEWDECIMAL, "7.00")}));
    EXPECT_FALSE(Access::active(handler_));
    EXPECT_EQ(ObNumberType, params_.at(0).get_type());
    int64_t decimal = 0;
    ASSERT_TRUE(params_.at(0).get_number().is_valid_int64(decimal));
    EXPECT_EQ(7, decimal);
    ASSERT_NO_FATAL_FAILURE(successful({{MYSQL_TYPE_LONGLONG, 0, true, std::string()}}));
    EXPECT_FALSE(Access::active(handler_));
    EXPECT_TRUE(params_.at(0).is_null());
    EXPECT_EQ(ObNullType, params_.at(0).get_type());
    EXPECT_EQ(MYSQL_TYPE_LONGLONG, info_->get_param_types().at(0));
    ASSERT_NO_FATAL_FAILURE(successful({integer(1)}, false));
    ASSERT_NO_FATAL_FAILURE(native_values({1}));
  }
}

TEST_F(NativePsParameterBinding, last_mixed_or_null_parameter_uses_the_complete_old_branch)
{
  ASSERT_NO_FATAL_FAILURE(prepare(3));
  ASSERT_NO_FATAL_FAILURE(successful({integer(1), integer(2), integer(3)}));
  ASSERT_TRUE(Access::active(handler_));
  const int64_t before = request_.calls_for(sizeof(int64_t));
  ASSERT_NO_FATAL_FAILURE(successful({integer(7), integer(8), integer32(9)}));
  EXPECT_FALSE(Access::active(handler_));
  EXPECT_EQ(before + 3, request_.calls_for(sizeof(int64_t)));
  EXPECT_EQ(7, params_.at(0).get_int());
  EXPECT_EQ(9, params_.at(2).get_int());
  ASSERT_NO_FATAL_FAILURE(successful({integer(10), integer(11),
      {MYSQL_TYPE_LONGLONG, 0, true, std::string()}}));
  EXPECT_FALSE(Access::active(handler_));
  EXPECT_EQ(before + 5, request_.calls_for(sizeof(int64_t)));
  EXPECT_TRUE(params_.at(2).is_null());
  ASSERT_NO_FATAL_FAILURE(successful({integer(12), integer(13), integer(14)}, false));
  ASSERT_NO_FATAL_FAILURE(native_values({12, 13, 14}));
}

TEST_F(NativePsParameterBinding, cursor_pl_and_array_modes_cannot_admit_a_native_owner)
{
  ASSERT_NO_FATAL_FAILURE(prepare(1));
  const std::vector<WireParam> input = {integer(99)};
  Access::cursor(handler_, true);
  ASSERT_NO_FATAL_FAILURE(successful(input));
  EXPECT_FALSE(Access::active(handler_));
  Access::cursor(handler_, false);
  Access::array(handler_, true);
  ASSERT_NO_FATAL_FAILURE(successful(input));
  EXPECT_FALSE(Access::active(handler_));
  Access::array(handler_, false);
  for (const auto type : {stmt::T_CALL_PROCEDURE, stmt::T_ANONYMOUS_BLOCK}) {
    Access::statement(handler_, type);
    ASSERT_NO_FATAL_FAILURE(successful(input));
    EXPECT_FALSE(Access::active(handler_));
  }
  // These are entry-gate tests, not outer PL/array request-parser execution.
  Access::statement(handler_, stmt::T_SELECT);
  ASSERT_NO_FATAL_FAILURE(successful(input));
  ASSERT_NO_FATAL_FAILURE(native_values({99}));
}

TEST_F(NativePsParameterBinding, truncated_packet_does_not_publish_owner_or_new_type_cache)
{
  ASSERT_NO_FATAL_FAILURE(prepare(3));
  ASSERT_NO_FATAL_FAILURE(successful({integer(1), integer(2), integer(3)}));
  std::string tail = packet({integer(7, 0x80), integer(8, 0x80), integer(9, 0x80)});
  tail.pop_back();
  bool handled = false;
  EXPECT_EQ(OB_ERR_MALFORMED_PS_PACKET, bind(tail, handled));
  EXPECT_TRUE(handled);
  EXPECT_FALSE(Access::active(handler_));
  for (int64_t i = 0; i < 3; ++i) {
    EXPECT_EQ(MYSQL_TYPE_LONGLONG, info_->get_param_types().at(i));
    EXPECT_EQ(0, info_->get_param_type_flags().at(i));
  }
  ASSERT_NO_FATAL_FAILURE(successful({integer(-1), integer(-2), integer(-3)}, false));
  ASSERT_NO_FATAL_FAILURE(native_values({-1, -2, -3}));
}

TEST_F(NativePsParameterBinding, owner_allocation_failure_leaves_cached_input_types_unchanged)
{
  ASSERT_NO_FATAL_FAILURE(prepare(3));
  ASSERT_NO_FATAL_FAILURE(successful({integer32(1), integer32(2), integer32(3)}));
  request_.fail_size(Access::owner_bytes(3));
  bool handled = false;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, bind(packet({integer(7), integer(8), integer(9)}), handled));
  EXPECT_TRUE(handled);
  EXPECT_FALSE(Access::active(handler_));
  EXPECT_EQ(1, request_.calls_for(Access::owner_bytes(3)));
  for (int64_t i = 0; i < 3; ++i) { EXPECT_EQ(MYSQL_TYPE_LONG, info_->get_param_types().at(i)); }
  request_.fail_size(0);
  ASSERT_NO_FATAL_FAILURE(successful({integer32(11), integer32(12), integer32(13)}, false));
  EXPECT_FALSE(Access::active(handler_));
  EXPECT_EQ(13, params_.at(2).get_int());
}

TEST_F(NativePsParameterBinding, cache_update_failure_does_not_publish_a_prepared_owner)
{
  ASSERT_NO_FATAL_FAILURE(prepare(3));
  ObPsSessionInfo detached(3);
  ASSERT_EQ(OB_SUCCESS, detached.fill_param_types_with_null_type());
  ASSERT_EQ(OB_SUCCESS, session_.remove_ps_session_info(stmt_id_));
  prepared_ = false;
  info_ = nullptr;
  bool handled = false;
  EXPECT_EQ(OB_EER_UNKNOWN_STMT_HANDLER, Access::bind(handler_, session_, detached,
      packet({integer(1), integer(2), integer(3)}), request_, handled));
  EXPECT_TRUE(handled);
  EXPECT_FALSE(Access::active(handler_));
  EXPECT_EQ(1, request_.calls_for(Access::owner_bytes(3)));
  EXPECT_EQ(3, params_.count()); // Binding completed before the real map update failed.
  for (int64_t i = 0; i < 3; ++i) { EXPECT_EQ(MYSQL_TYPE_NULL, detached.get_param_types().at(i)); }
}

TEST_F(NativePsParameterBinding, unsupported_extension_keeps_dispatch_to_the_old_parser)
{
  ASSERT_NO_FATAL_FAILURE(prepare(1));
  ASSERT_NO_FATAL_FAILURE(successful({integer(17)}));
  bool handled = true;
  EXPECT_EQ(OB_SUCCESS, bind(packet({{MYSQL_TYPE_COMPLEX, 0, false, std::string()}}), handled));
  EXPECT_FALSE(handled);
  EXPECT_FALSE(Access::active(handler_));
  EXPECT_EQ(MYSQL_TYPE_LONGLONG, info_->get_param_types().at(0));
}

TEST_F(NativePsParameterBinding, real_long_data_materialization_keeps_the_old_payload_path)
{
  ASSERT_NO_FATAL_FAILURE(prepare(1));
  ASSERT_NO_FATAL_FAILURE(successful({integer(17)}));
  ObPieceCache *cache = session_.get_piece_cache(true);
  ASSERT_NE(nullptr, cache);
  ObPiece *piece = nullptr;
  ASSERT_EQ(OB_SUCCESS, cache->make_piece(stmt_id_, 0, piece, session_));
  ASSERT_NE(nullptr, piece);
  ObString payload("long parameter");
  ASSERT_EQ(OB_SUCCESS, cache->add_piece_buffer(piece, ObLastPiece, &payload));
  // LongData consumes the cache, not a length/value in this execute packet.
  ASSERT_NO_FATAL_FAILURE(successful({{MYSQL_TYPE_VAR_STRING, 0, false, std::string()}}));
  EXPECT_FALSE(Access::active(handler_));
  EXPECT_EQ(ObVarcharType, params_.at(0).get_type());
  EXPECT_EQ(payload, params_.at(0).get_string());
  ASSERT_EQ(OB_SUCCESS, cache->close_all(session_));
  EXPECT_EQ(ObString("long parameter"), params_.at(0).get_string());
  ASSERT_NO_FATAL_FAILURE(successful({integer(18)}));
  ASSERT_NO_FATAL_FAILURE(native_values({18}));
}

TEST_F(NativePsParameterBinding, execution_clone_and_questionmark_read_survive_request_poison)
{
  ASSERT_NO_FATAL_FAILURE(prepare(3));
  const int64_t expected[] = {INT64_MIN, 7, INT64_MAX};
  ASSERT_NO_FATAL_FAILURE(successful({integer(expected[0]), integer(expected[1]), integer(expected[2])}));
  ASSERT_TRUE(Access::active(handler_));
  ObArenaAllocator execution_arena("PsExecOwner");
  ObExecContext exec(execution_arena);
  exec.set_my_session(&session_);
  ASSERT_EQ(OB_SUCCESS, exec.create_physical_plan_ctx());
  ObPhysicalPlanCtx &plan = *exec.get_physical_plan_ctx();
  ASSERT_EQ(OB_SUCCESS, plan.get_param_store_for_update().assign(params_));
  plan.set_original_param_cnt(3);
  ASSERT_EQ(OB_SUCCESS, plan.init_datum_param_store());
  for (int64_t i = 0; i < 3; ++i) {
    EXPECT_NE(params_.at(i).datum_.ptr_, plan.get_param_store().at(i).datum_.ptr_);
  }
  Access::poison_values(handler_);
  EXPECT_EQ(INT64_C(0x5a5a5a5a5a5a5a5a), params_.at(0).get_int());
  ObEvalCtx eval(exec);
  for (int64_t i = 0; i < 3; ++i) {
    ObExpr expr;
    expr.extra_ = i;
    expr.datum_meta_.type_ = ObIntType;
    ObDatum result;
    ASSERT_EQ(OB_SUCCESS, eval_question_mark_func(expr, eval, result));
    EXPECT_EQ(expected[i], result.get_int());
    ASSERT_EQ(1, plan.get_param_frame_ptrs().count());
    const char *slot = plan.get_param_frame_ptrs().at(0) + i * (sizeof(ObDatum) + sizeof(ObEvalInfo));
    EXPECT_EQ(expected[i], reinterpret_cast<const ObDatum *>(slot)->get_int());
  }
}

TEST_F(NativePsParameterBinding, rebuilding_execution_context_does_not_modify_request_values)
{
  ASSERT_NO_FATAL_FAILURE(prepare(3));
  ASSERT_NO_FATAL_FAILURE(successful({integer(1), integer(7), integer(-9)}));
  const Datum *request_values = Access::values(handler_);
  for (int attempt = 0; attempt < 2; ++attempt) {
    ObArenaAllocator execution_arena("PsRetryOwner");
    ObPhysicalPlanCtx plan(execution_arena);
    ASSERT_EQ(OB_SUCCESS, plan.get_param_store_for_update().assign(params_));
    plan.set_original_param_cnt(3);
    ASSERT_EQ(OB_SUCCESS, plan.init_datum_param_store());
    EXPECT_EQ(1, plan.get_param_store().at(0).get_int());
    EXPECT_EQ(7, plan.get_param_store().at(1).get_int());
    plan.get_param_store_for_update().at(0).datum_.set_int(1000 + attempt);
    EXPECT_EQ(1, request_values[0].get_int());
  }
  // This exercises real context cloning/destruction, not a forced SQL retry.
  ASSERT_NO_FATAL_FAILURE(native_values({1, 7, -9}));
}

TEST_F(NativePsParameterBinding, zero_parameter_execute_preserves_the_existing_empty_path)
{
  ASSERT_NO_FATAL_FAILURE(prepare(0));
  ASSERT_NO_FATAL_FAILURE(successful({}));
  EXPECT_FALSE(Access::active(handler_));
  EXPECT_EQ(0, params_.count());
}
