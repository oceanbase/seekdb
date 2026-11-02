#include <gtest/gtest.h>
#include "sql/engine/basic/ob_expr_values_op.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "sql/engine/expr/ob_datum_cast.h"
#include "share/datum/datum_value_cast.h"
#include "sql/engine/expr/ob_obj_cast_runtime.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/ob_sql_context.h"
#include "common/udt/ob_array_nested.h"
#include "common/udt/ob_vector_type.h"
#include <array>
#include "sql/engine/expr/ob_expr_sql_udt_utils.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/session/ob_sql_session_info.h"
#include "common/datum/datum_value.h"
#include "common/udt/ob_array_fixed_size.h"
#include "common/lob/lob_types.h"
#include "share/ob_i_lob_read_service.h"
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <map>
#include <string>


namespace {
oceanbase::common::Datum read_parameter_value(
    const oceanbase::sql::ObPhysicalPlanCtx &plan, int64_t index)
{
  oceanbase::common::Datum value;
  EXPECT_EQ(oceanbase::common::OB_SUCCESS, plan.read_native_parameter(index, value));
  return value;
}

} // namespace

using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
// Every free poisons and truly releases its allocation. The final liveness
// assertion is checked before reading any result bytes, so a regression does
// not itself perform a use-after-free in the test.
class FreeingOwner final : public ObIAllocator {
public:
  ~FreeingOwner() { for (const auto &block : blocks_) { std::free(block.first); } }
  void *alloc(int64_t bytes) override {
    if (bytes < 0) { return nullptr; }
    const size_t length = bytes > 0 ? static_cast<size_t>(bytes) : 1;
    void *memory = std::malloc(length);
    if (memory != nullptr) { blocks_.emplace(memory, length); }
    return memory;
  }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *memory) override {
    if (memory == nullptr) { return; }
    const auto found = blocks_.find(memory);
    if (found == blocks_.end()) { unexpected_free_ = true; return; }
    std::memset(memory, 0xdd, found->second);
    std::free(memory); blocks_.erase(found); ++frees_;
  }
  bool owns_live(const char *bytes, int64_t length) const {
    if (bytes == nullptr || length < 0) { return false; }
    const uintptr_t address = reinterpret_cast<uintptr_t>(bytes);
    for (const auto &block : blocks_) {
      const uintptr_t begin = reinterpret_cast<uintptr_t>(block.first);
      if (address >= begin && address - begin <= block.second
          && static_cast<uint64_t>(length) <= block.second - (address - begin)) { return true; }
    }
    return false;
  }
  bool unexpected_free_ = false;
  int64_t frees_ = 0;
private:
  std::map<void *, size_t> blocks_;
};
// The input is a real inline LOB. No external LOB read is allowed by this test;
// these transport methods satisfy the actual execution-context service port.
class NoIoLobService final : public ObILobReadService {
public:
  int calls_ = 0;
  int get_outrow_lob_full_data(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *) override
    { ++calls_; return OB_NOT_SUPPORTED; }
  int get_delta_lob_full_data(ObLobTextIterCtx &, SqlValueType, ObCollationType,
      ObLobLocatorV2 &, ObIAllocator *, ObString &) override { ++calls_; return OB_NOT_SUPPORTED; }
  int get_outrow_prefix_data(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *, uint32_t) override
    { ++calls_; return OB_NOT_SUPPORTED; }
  int get_first_block(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *,
      ObString &, ObTextStringIterState &) override { ++calls_; return OB_NOT_SUPPORTED; }
  int get_next_block_inner(ObLobTextIterCtx &, ObCollationType, bool, bool,
      ObString &, ObTextStringIterState &) override { ++calls_; return OB_NOT_SUPPORTED; }
  int get_outrow_char_len(ObLobTextIterCtx &, ObCollationType, ObIAllocator *, int64_t &) override
    { ++calls_; return OB_NOT_SUPPORTED; }
  void free_lob_query_iter(ObLobTextIterCtx &) override { ++calls_; }
};
class InlineLobContext final : public ObILobAccessContext { public: ~InlineLobContext() = default; };
struct LobContextTag {
  using type = ObILobAccessContext *ObExecContext::*;
  friend type access(LobContextTag);
};
template<class Tag, typename Tag::type Member> struct TestAccess {
  friend typename Tag::type access(Tag) { return Member; }
};
template struct TestAccess<LobContextTag, &ObExecContext::lob_access_ctx_>;
class InlineContextGuard {
public:
  InlineContextGuard(ObExecContext &exec, ObILobAccessContext &context) : exec_(exec) {
    exec_.*access(LobContextTag{}) = &context;
  }
  ~InlineContextGuard() { exec_.*access(LobContextTag{}) = nullptr; }
private: ObExecContext &exec_;
};
}

TEST(NativeUdtResult, collection_text_remains_live_after_conversion_with_freeing_owner)
{
  // This is the same genuine collection construction used by
  // unittest/oblib/test_datum_collection_printer.cpp.
  ObArenaAllocator source_owner;
  ObCollectionBasicType element;
  element.type_id_ = OB_BASIC_TYPE;
  element.basic_meta_.set_obj_type(ObIntType);
  ObCollectionArrayType type(source_owner);
  type.type_id_ = OB_ARRAY_TYPE; type.element_type_ = &element;
  ObSqlCollectionInfo info(source_owner);
  info.collection_meta_ = &type;
  info.set_name(ObString::make_string("ARRAY(INT)"));
  ObIArrayType *array = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObArrayTypeObjFactory::construct(source_owner, type, array, false));
  ASSERT_EQ(OB_SUCCESS, array->init());
  auto *numbers = static_cast<ObArrayFixedSize<int64_t> *>(array);
  ASSERT_EQ(OB_SUCCESS, numbers->push_back(7));
  ASSERT_EQ(OB_SUCCESS, numbers->push_back(-9));
  ASSERT_EQ(OB_SUCCESS, numbers->push_null());
  ASSERT_EQ(OB_SUCCESS, array->init());
  std::string raw(array->get_raw_binary_len(), '\0');
  ASSERT_EQ(OB_SUCCESS, array->get_raw_binary(&raw[0], raw.size()));
  ObLobCommon header;
  std::string lob(sizeof(header) + raw.size(), '\0');
  std::memcpy(&lob[0], &header, sizeof(header));
  std::memcpy(&lob[sizeof(header)], raw.data(), raw.size());

  ObArenaAllocator execution_owner;
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ObExecContext exec(execution_owner);
  exec.set_my_session(&session);
  ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &exec);
  NoIoLobService service; InlineLobContext inline_context;
  exec.set_lob_read_service(&service);
  InlineContextGuard context_guard(exec, inline_context);
  ASSERT_EQ(OB_SUCCESS, exec.create_physical_plan_ctx());
  auto &subschema = exec.get_physical_plan_ctx()->get_subschema_ctx();
  ASSERT_EQ(OB_SUCCESS, subschema.init());
  uint16_t id = 0;
  ASSERT_EQ(OB_SUCCESS, subschema.get_new_subschema_id(id));
  ObSubSchemaValue metadata;
  metadata.type_ = OB_SUBSCHEMA_COLLECTION_TYPE;
  metadata.signature_ = info.get_signature(); metadata.value_ = &info;
  ASSERT_EQ(OB_SUCCESS, subschema.set_subschema(id, metadata));

  DatumValue value;
  value.meta_.set_type(ObCollectionSQLType);
  value.meta_.set_subschema_id(id);
  value.meta_.set_has_lob_header();
  value.datum_.set_string(lob.data(), lob.size());
  FreeingOwner result_owner;
  ASSERT_EQ(OB_SUCCESS, ObSqlUdtUtils::convert_result_for_client(value,
      &result_owner, &session, &exec, false));
  ASSERT_TRUE(result_owner.owns_live(value.datum_.get_string().ptr(), value.datum_.length()));
  EXPECT_EQ(std::string("[7,-9,NULL]"),
      std::string(value.get_string().ptr(), value.get_string().length()));
  EXPECT_EQ(ObCollectionSQLType, value.get_type());
  EXPECT_EQ(id, value.get_subschema_id());
  EXPECT_EQ(0, service.calls_);
  EXPECT_FALSE(result_owner.unexpected_free_);
}


TEST(NativeUdtResult, values_operator_dynamic_collection_cast_keeps_destination_declaration)
{
  ObArenaAllocator owner;
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ObExecContext exec(owner);
  exec.set_my_session(&session);
  ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &exec);
  NoIoLobService service; InlineLobContext inline_context;
  exec.set_lob_read_service(&service);
  InlineContextGuard context_guard(exec, inline_context);
  ASSERT_EQ(OB_SUCCESS, exec.create_physical_plan_ctx());
  ObSqlCtx sql_context;
  exec.set_sql_ctx(&sql_context);
  alignas(8) std::array<char, 4096> frame{};
  char *frames[] = {frame.data()};
  exec.set_frames(frames); exec.set_frame_cnt(1);
  struct FrameScope {
    ObExecContext &exec_;
    ~FrameScope() { exec_.set_frames(nullptr); exec_.set_frame_cnt(0); exec_.set_sql_ctx(nullptr); }
  } frame_scope{exec};
  ObEvalCtx eval(exec);
  const char *declarations[] = {"ARRAY(INT)", "ARRAY(ARRAY(FLOAT))", "VECTOR(3)"};
  const char *literals[] = {"[1,-2,NULL]", "[[1,2],[3,NULL]]", "[1.25,-2.5,3]"};
  for (int scenario = 0; scenario < 3; ++scenario) {
    SCOPED_TRACE(declarations[scenario]);
    uint16_t id = 0;
    ASSERT_EQ(OB_SUCCESS, exec.get_subschema_id_by_type_string(
        ObString::make_string(declarations[scenario]), id));
    ASSERT_FALSE(ob_is_reserved_subschema_id(id));
    ObSubSchemaValue definition;
    ASSERT_EQ(OB_SUCCESS, exec.get_sqludt_meta_by_subschema_id(id, definition));
    ASSERT_EQ(OB_SUBSCHEMA_COLLECTION_TYPE, definition.type_);
    const auto *collection = static_cast<const ObSqlCollectionInfo *>(definition.value_);
    ASSERT_NE(nullptr, collection); ASSERT_NE(nullptr, collection->collection_meta_);
    ObExpr source, destination;
    source.type_ = T_VARCHAR;
    source.obj_meta_.set_varchar(); source.obj_meta_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    source.obj_meta_.set_collation_level(CS_LEVEL_IMPLICIT);
    source.datum_meta_ = ObDatumMeta(ObVarcharType, CS_TYPE_UTF8MB4_BIN, -1);
    source.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(ObVarcharType);
    ObAccuracy source_accuracy; source_accuracy.set_length(64);
    ASSERT_EQ(OB_SUCCESS, source.prepare_scalar_type(source.obj_meta_, source_accuracy, owner));
    destination.type_ = T_REF_COLUMN;
    destination.obj_meta_.set_type(ObCollectionSQLType);
    destination.obj_meta_.set_subschema_id(id); destination.obj_meta_.set_has_lob_header();
    destination.datum_meta_ = ObDatumMeta(ObCollectionSQLType, static_cast<ObCollationType>(id), -1);
    destination.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(ObCollectionSQLType);
    destination.max_length_ = LENGTH_UNKNOWN_YET;
    ObAccuracy destination_accuracy; destination_accuracy.set_length(destination.max_length_);
    const SqlType *type = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObExprUtil::resolve_sql_type(exec, destination.obj_meta_,
        destination_accuracy, id, owner, type));
    ASSERT_NE(nullptr, type);
    ASSERT_EQ(OB_SUCCESS, destination.bind_sql_type(*type, owner));
    ObExpr *expressions[] = {&source, &destination};
    for (int index = 0; index < 2; ++index) {
      ObExpr &expr = *expressions[index];
      expr.frame_idx_ = 0; expr.datum_off_ = index * 1024;
      expr.eval_info_off_ = index * 1024 + 64;
      expr.res_buf_off_ = index * 1024 + 192; expr.res_buf_len_ = 512;
      new (frame.data() + expr.datum_off_) Datum;
      new (frame.data() + expr.eval_info_off_) ObEvalInfo;
    }
    ObExprValuesSpec spec(owner, PHY_EXPR_VALUES);
    ASSERT_EQ(OB_SUCCESS, spec.values_.init(1));
    ASSERT_EQ(OB_SUCCESS, spec.values_.push_back(&source));
    ASSERT_EQ(OB_SUCCESS, spec.output_.init(1));
    ASSERT_EQ(OB_SUCCESS, spec.output_.push_back(&destination));
    ASSERT_EQ(OB_SUCCESS, spec.column_names_.init(1));
    ASSERT_EQ(OB_SUCCESS, spec.column_names_.push_back(ObString::make_string("c")));
    ObExprValuesOp op(exec, spec, nullptr);
    std::string input(literals[scenario]);
    source.locate_datum_for_write(eval).set_string(input.data(), input.size());
    source.get_eval_info(eval).evaluated_ = true;
    ASSERT_EQ(OB_SUCCESS, op.inner_open());
    ASSERT_EQ(OB_SUCCESS, op.inner_get_next_row());
    EXPECT_EQ(id, destination.obj_meta_.get_subschema_id());
    Datum &result = destination.locate_expr_datum(eval);
    ASSERT_FALSE(result.is_null());
    EXPECT_NE(input.data(), result.get_reference());
    std::memset(&input[0], 0xa5, input.size());
    ObString raw = result.get_string();
    ASSERT_EQ(OB_SUCCESS, ObTextStringHelper::read_real_string_data(
        exec, &owner, ObLongTextType, CS_TYPE_BINARY, true, raw));
    ObIArrayType *array = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObArrayTypeObjFactory::construct(
        owner, *collection->collection_meta_, array, true));
    ASSERT_NE(nullptr, array); ASSERT_EQ(OB_SUCCESS, array->init(raw));
    if (scenario == 0) {
      ASSERT_EQ(3U, array->size());
      const auto *numbers = static_cast<const ObArrayFixedSize<int32_t> *>(array);
      EXPECT_EQ(1, (*numbers)[0]); EXPECT_EQ(-2, (*numbers)[1]); EXPECT_TRUE(numbers->is_null(2));
    } else if (scenario == 1) {
      ASSERT_EQ(2U, array->size());
      const auto *nested = static_cast<const ObArrayNested *>(array);
      const auto *numbers = static_cast<const ObArrayFixedSize<float> *>(nested->get_child_array());
      ASSERT_EQ(4U, numbers->size());
      EXPECT_FLOAT_EQ(1.0F, (*numbers)[0]); EXPECT_FLOAT_EQ(2.0F, (*numbers)[1]);
      EXPECT_FLOAT_EQ(3.0F, (*numbers)[2]); EXPECT_TRUE(numbers->is_null(3));
    } else {
      ASSERT_EQ(3U, array->size());
      const auto *numbers = static_cast<const ObVectorF32Data *>(array);
      EXPECT_FLOAT_EQ(1.25F, (*numbers)[0]); EXPECT_FLOAT_EQ(-2.5F, (*numbers)[1]);
      EXPECT_FLOAT_EQ(3.0F, (*numbers)[2]);
    }
    EXPECT_EQ(OB_ITER_END, op.inner_get_next_row());
    // A typed nullable target still resolves the same real declaration.
    source.locate_datum_for_write(eval).set_null();
    source.get_eval_info(eval).evaluated_ = true;
    destination.get_eval_info(eval).clear_evaluated_flag();
    ASSERT_EQ(OB_SUCCESS, op.inner_open());
    ASSERT_EQ(OB_SUCCESS, op.inner_get_next_row());
    EXPECT_TRUE(destination.locate_expr_datum(eval).is_null());
    // The fix forwards a declaration; it does not relax the reserved-ID guard.
    input.assign(literals[scenario]);
    source.locate_datum_for_write(eval).set_string(input.data(), input.size());
    source.get_eval_info(eval).evaluated_ = true;
    destination.get_eval_info(eval).clear_evaluated_flag();
    destination.obj_meta_.set_subschema_id(0);
    destination.datum_meta_.cs_type_ = static_cast<ObCollationType>(0);
    ASSERT_EQ(OB_SUCCESS, op.inner_open());
    EXPECT_EQ(OB_ERR_UNEXPECTED, op.inner_get_next_row());
    EXPECT_TRUE(destination.locate_expr_datum(eval).is_null());
    ASSERT_EQ(OB_SUCCESS, op.inner_close());
  }
  EXPECT_EQ(0, service.calls_);
}

namespace oceanbase { namespace sql {
int eval_assign_question_mark_func(const ObExpr &, ObEvalCtx &, Datum &);
} }

TEST(NativeUdtResult, table_assignment_parameter_cast_uses_real_collection_declaration)
{
  ObArenaAllocator owner;
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  session.set_stmt_type(stmt::T_UPDATE);
  ObExecContext exec(owner);
  exec.set_my_session(&session);
  ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &exec);
  NoIoLobService service; InlineLobContext inline_context;
  exec.set_lob_read_service(&service);
  InlineContextGuard context_guard(exec, inline_context);
  ASSERT_EQ(OB_SUCCESS, exec.create_physical_plan_ctx());
  ObSqlCtx sql_context;
  exec.set_sql_ctx(&sql_context);
  struct SqlContextScope {
    ObExecContext &exec_;
    ~SqlContextScope() { exec_.set_sql_ctx(nullptr); }
  } sql_context_scope{exec};
  ObEvalCtx eval(exec);
  auto &plan = *exec.get_physical_plan_ctx();
  const char *declarations[] = {"ARRAY(INT)", "ARRAY(ARRAY(FLOAT))", "VECTOR(3)"};
  const char *literals[] = {"[1,-2,NULL]", "[[1,2],[3,NULL]]", "[1,7,1]"};
  for (int scenario = 0; scenario < 3; ++scenario) {
    SCOPED_TRACE(declarations[scenario]);
    uint16_t id = 0;
    ASSERT_EQ(OB_SUCCESS, exec.get_subschema_id_by_type_string(
        ObString::make_string(declarations[scenario]), id));
    ASSERT_FALSE(ob_is_reserved_subschema_id(id));
    ObSubSchemaValue definition;
    ASSERT_EQ(OB_SUCCESS, exec.get_sqludt_meta_by_subschema_id(id, definition));
    ASSERT_EQ(OB_SUBSCHEMA_COLLECTION_TYPE, definition.type_);
    const auto *collection = static_cast<const ObSqlCollectionInfo *>(definition.value_);
    ASSERT_NE(nullptr, collection); ASSERT_NE(nullptr, collection->collection_meta_);
    ObExpr assignment;
    assignment.type_ = T_QUESTIONMARK; assignment.extra_ = 0;
    assignment.obj_meta_.set_type(ObCollectionSQLType);
    assignment.obj_meta_.set_subschema_id(id); assignment.obj_meta_.set_has_lob_header();
    assignment.datum_meta_ = ObDatumMeta(ObCollectionSQLType, static_cast<ObCollationType>(id), -1);
    assignment.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(ObCollectionSQLType);
    assignment.max_length_ = LENGTH_UNKNOWN_YET;
    ObAccuracy accuracy; accuracy.set_length(assignment.max_length_);
    const SqlType *target = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObExprUtil::resolve_sql_type(exec, assignment.obj_meta_,
        accuracy, id, owner, target));
    ASSERT_NE(nullptr, target);
    ASSERT_EQ(OB_SUCCESS, assignment.bind_sql_type(*target, owner));
    plan.reset_datum_param_store();
    std::string input(literals[scenario]);
    DatumParameter parameter;
    parameter.set_varchar_view(ObString(input.size(), input.data()));
    parameter.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    parameter.set_length(64); parameter.set_param_meta();
    parameter.set_need_to_check_type(true); parameter.set_raw_text_info(9, input.size());
    ASSERT_EQ(OB_SUCCESS, plan.append_parameter_input(parameter));
    plan.set_original_param_cnt(1);
    ASSERT_EQ(OB_SUCCESS, plan.init_datum_param_store());
    ASSERT_TRUE(plan.native_parameters_ready());
    ASSERT_NE(input.data(), read_parameter_value(plan, 0).get_reference());
    std::memset(&input[0], 0xa5, input.size());
    Datum result;
    ASSERT_EQ(OB_SUCCESS, eval_assign_question_mark_func(assignment, eval, result));
    ASSERT_FALSE(result.is_null());
    EXPECT_EQ(id, assignment.obj_meta_.get_subschema_id());
    EXPECT_EQ(ObVarcharType, plan.get_native_parameter_type(0)->kind());
    EXPECT_EQ(ObString::make_string(literals[scenario]), read_parameter_value(plan, 0).get_string());
    EXPECT_TRUE(plan.get_parameter_metadata(0)->flag_.need_to_check_type_);
    EXPECT_EQ(9, plan.get_parameter_metadata(0)->raw_text_pos_);
    ObString raw = result.get_string();
    ASSERT_EQ(OB_SUCCESS, ObTextStringHelper::read_real_string_data(
        exec, &owner, ObLongTextType, CS_TYPE_BINARY, true, raw));
    ObIArrayType *array = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObArrayTypeObjFactory::construct(
        owner, *collection->collection_meta_, array, true));
    ASSERT_NE(nullptr, array); ASSERT_EQ(OB_SUCCESS, array->init(raw));
    if (scenario == 0) {
      ASSERT_EQ(3U, array->size());
      const auto *numbers = static_cast<const ObArrayFixedSize<int32_t> *>(array);
      EXPECT_EQ(1, (*numbers)[0]); EXPECT_EQ(-2, (*numbers)[1]); EXPECT_TRUE(numbers->is_null(2));
    } else if (scenario == 1) {
      ASSERT_EQ(2U, array->size());
      const auto *nested = static_cast<const ObArrayNested *>(array);
      const auto *numbers = static_cast<const ObArrayFixedSize<float> *>(nested->get_child_array());
      ASSERT_EQ(4U, numbers->size());
      EXPECT_FLOAT_EQ(1.0F, (*numbers)[0]); EXPECT_FLOAT_EQ(2.0F, (*numbers)[1]);
      EXPECT_FLOAT_EQ(3.0F, (*numbers)[2]); EXPECT_TRUE(numbers->is_null(3));
    } else {
      ASSERT_EQ(3U, array->size());
      const auto *numbers = static_cast<const ObVectorF32Data *>(array);
      EXPECT_FLOAT_EQ(1.0F, (*numbers)[0]); EXPECT_FLOAT_EQ(7.0F, (*numbers)[1]);
      EXPECT_FLOAT_EQ(1.0F, (*numbers)[2]);
      const Datum previous = result;
      const DatumParameterMetadata attributes = *plan.get_parameter_metadata(0);
      Datum bad; bad.set_string("[1,7]", 5);
      ASSERT_EQ(OB_SUCCESS, plan.set_runtime_parameter(0, bad,
          *plan.get_native_parameter_type(0), attributes));
      EXPECT_EQ(OB_ERR_INVALID_VECTOR_DIM, eval_assign_question_mark_func(assignment, eval, result));
      EXPECT_EQ(previous.get_reference(), result.get_reference());
      EXPECT_EQ(previous.length(), result.length());
      EXPECT_FLOAT_EQ(7.0F, (*numbers)[1]);
    }
    const DatumParameterMetadata attributes = *plan.get_parameter_metadata(0);
    Datum null_value;
    null_value.set_null();
    ASSERT_EQ(OB_SUCCESS, plan.set_runtime_parameter(0, null_value,
        *plan.get_native_parameter_type(0), attributes));
    ASSERT_EQ(OB_SUCCESS, eval_assign_question_mark_func(assignment, eval, result));
    EXPECT_TRUE(result.is_null());
    EXPECT_EQ(id, assignment.obj_meta_.get_subschema_id());
    EXPECT_EQ(ObVarcharType, plan.get_native_parameter_type(0)->kind());
  }
  EXPECT_EQ(0, service.calls_);
}

TEST(NativeUdtResult, native_caster_collection_source_keeps_same_and_cross_declarations)
{
  ObArenaAllocator owner;
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ObExecContext exec(owner);
  exec.set_my_session(&session);
  ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &exec);
  NoIoLobService service; InlineLobContext inline_context;
  exec.set_lob_read_service(&service);
  InlineContextGuard context_guard(exec, inline_context);
  ASSERT_EQ(OB_SUCCESS, exec.create_physical_plan_ctx());
  ObSqlCtx sql_context;
  exec.set_sql_ctx(&sql_context);
  struct SqlContextScope {
    ObExecContext &exec_;
    ~SqlContextScope() { exec_.set_sql_ctx(nullptr); }
  } sql_context_scope{exec};

  const char *declarations[] = {"ARRAY(INT)", "ARRAY(BIGINT)"};
  uint16_t ids[2] = {};
  const ObSqlCollectionInfo *definitions[2] = {};
  for (int index = 0; index < 2; ++index) {
    ASSERT_EQ(OB_SUCCESS, exec.get_subschema_id_by_type_string(
        ObString::make_string(declarations[index]), ids[index]));
    ASSERT_FALSE(ob_is_reserved_subschema_id(ids[index]));
    ObSubSchemaValue definition;
    ASSERT_EQ(OB_SUCCESS, exec.get_sqludt_meta_by_subschema_id(ids[index], definition));
    ASSERT_EQ(OB_SUBSCHEMA_COLLECTION_TYPE, definition.type_);
    definitions[index] = static_cast<const ObSqlCollectionInfo *>(definition.value_);
    ASSERT_NE(nullptr, definitions[index]);
    ASSERT_NE(nullptr, definitions[index]->collection_meta_);
  }
  ASSERT_NE(ids[0], ids[1]);
  // Build an actual Collection input independently of the caster under test.
  ObArenaAllocator source_owner;
  ObIArrayType *array = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObArrayTypeObjFactory::construct(
      source_owner, *definitions[0]->collection_meta_, array, false));
  ASSERT_NE(nullptr, array);
  ASSERT_EQ(OB_SUCCESS, array->init());
  auto *numbers = static_cast<ObArrayFixedSize<int32_t> *>(array);
  ASSERT_EQ(OB_SUCCESS, numbers->push_back(INT32_MIN));
  ASSERT_EQ(OB_SUCCESS, numbers->push_null());
  ASSERT_EQ(OB_SUCCESS, numbers->push_back(INT32_MAX));
  ASSERT_EQ(OB_SUCCESS, array->init());
  std::string raw(array->get_raw_binary_len(), '\0');
  ASSERT_EQ(OB_SUCCESS, array->get_raw_binary(&raw[0], raw.size()));
  ObLobCommon header;
  std::string lob(sizeof(header) + raw.size(), '\0');
  std::memcpy(&lob[0], &header, sizeof(header));
  std::memcpy(&lob[sizeof(header)], raw.data(), raw.size());
  DatumParameter source;
  source.set_string_view(ObCollectionSQLType, ObString(lob.size(), lob.data()));
  source.set_subschema_id(ids[0]); source.set_has_lob_header();
  source.set_length(LENGTH_UNKNOWN_YET); source.set_param_meta();
  const DatumParameterMetadata original_attributes = source.get_metadata();
  FreeingOwner result_owner;
  DatumParameter results[3];
  const int target_indices[] = {0, 1, 0};
  {
    ObDatumCaster caster;
    ASSERT_EQ(OB_SUCCESS, caster.init(exec, false));
    for (int scenario = 0; scenario < 3; ++scenario) {
      SCOPED_TRACE(scenario);
      const int target_index = target_indices[scenario];
      ObExprResType destination;
      destination.set_type(ObCollectionSQLType);
      destination.set_subschema_id(ids[target_index]);
      destination.set_has_lob_header();
      destination.set_length(LENGTH_UNKNOWN_YET);
      ASSERT_EQ(OB_SUCCESS, caster.to_value(source, destination, CM_NONE,
          result_owner, results[scenario]));
      EXPECT_FALSE(results[scenario].is_null());
      EXPECT_EQ(ids[target_index], results[scenario].get_subschema_id());
      EXPECT_EQ(ids[target_index], results[scenario].get_param_meta().get_subschema_id());
      EXPECT_EQ(ObCollectionSQLType, results[scenario].get_param_meta().get_type());
      EXPECT_NE(source.get_datum().get_reference(), results[scenario].get_datum().get_reference());
      ASSERT_TRUE(result_owner.owns_live(static_cast<const char *>(results[scenario].get_datum().get_reference()),
          results[scenario].get_datum().length()));
      DatumParameter null_source = source;
      null_source.datum_.set_null(); // Keep the real source Collection declaration.
      DatumParameter null_result;
      ASSERT_EQ(OB_SUCCESS, caster.to_value(null_source, destination, CM_NONE,
          result_owner, null_result));
      EXPECT_TRUE(null_result.is_null());
      EXPECT_EQ(ids[target_index], null_result.get_param_meta().get_subschema_id());
    }
  }
  EXPECT_EQ(original_attributes.type_meta_, source.get_metadata().type_meta_);
  EXPECT_EQ(original_attributes.param_meta_, source.get_metadata().param_meta_);
  std::memset(&raw[0], 0xa5, raw.size());
  std::memset(&lob[0], 0xa5, lob.size());
  source_owner.reset();
  for (int scenario = 0; scenario < 3; ++scenario) {
    SCOPED_TRACE(scenario);
    const int target_index = target_indices[scenario];
    ASSERT_TRUE(result_owner.owns_live(static_cast<const char *>(results[scenario].get_datum().get_reference()),
        results[scenario].get_datum().length()));
    ObString output = results[scenario].get_string();
    ASSERT_EQ(OB_SUCCESS, ObTextStringHelper::read_real_string_data(
        exec, &owner, ObLongTextType, CS_TYPE_BINARY, true, output));
    ObIArrayType *decoded = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObArrayTypeObjFactory::construct(
        owner, *definitions[target_index]->collection_meta_, decoded, true));
    ASSERT_NE(nullptr, decoded);
    ASSERT_EQ(OB_SUCCESS, decoded->init(output));
    ASSERT_EQ(3U, decoded->size());
    if (target_index == 0) {
      const auto *values = static_cast<const ObArrayFixedSize<int32_t> *>(decoded);
      EXPECT_EQ(INT32_MIN, (*values)[0]); EXPECT_TRUE(values->is_null(1));
      EXPECT_EQ(INT32_MAX, (*values)[2]);
    } else {
      const auto *values = static_cast<const ObArrayFixedSize<int64_t> *>(decoded);
      EXPECT_EQ(static_cast<int64_t>(INT32_MIN), (*values)[0]);
      EXPECT_TRUE(values->is_null(1));
      EXPECT_EQ(static_cast<int64_t>(INT32_MAX), (*values)[2]);
    }
  }
  EXPECT_FALSE(result_owner.unexpected_free_);
  EXPECT_EQ(0, service.calls_);
}

TEST(NativeUdtResult, common_value_caster_uses_registered_collection_output_declaration)
{
  ObArenaAllocator execution_owner;
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ObExecContext exec(execution_owner);
  exec.set_my_session(&session);
  ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &exec);
  NoIoLobService service; InlineLobContext inline_context;
  exec.set_lob_read_service(&service);
  InlineContextGuard context_guard(exec, inline_context);
  ASSERT_EQ(OB_SUCCESS, exec.create_physical_plan_ctx());
  ObSqlDatumCastRuntime runtime(&exec);
  const char *declarations[] = {"ARRAY(INT)", "VECTOR(3)"};
  const char *literals[] = {"[1,-2,NULL]", "[1.25,-2.5,3]"};
  for (int declaration = 0; declaration < 2; ++declaration) {
    SCOPED_TRACE(declarations[declaration]);
    uint16_t id = 0;
    ASSERT_EQ(OB_SUCCESS, exec.get_subschema_id_by_type_string(
        ObString::make_string(declarations[declaration]), id));
    ASSERT_FALSE(ob_is_reserved_subschema_id(id));
    ObSubSchemaValue definition;
    ASSERT_EQ(OB_SUCCESS, exec.get_sqludt_meta_by_subschema_id(id, definition));
    const auto *collection = static_cast<const ObSqlCollectionInfo *>(definition.value_);
    ASSERT_NE(nullptr, collection); ASSERT_NE(nullptr, collection->collection_meta_);
    for (SqlValueType source_type : {ObVarcharType, ObTinyTextType}) {
      SCOPED_TRACE(source_type);
      ObArenaAllocator source_owner, cast_owner;
      DatumValue input;
      ASSERT_EQ(OB_SUCCESS, input.set_string(source_type,
          ObString::make_string(literals[declaration]), source_owner));
      input.set_collation_type(CS_TYPE_UTF8MB4_BIN); // Real text, not raw binary-array input.
      const void *source_bytes = input.datum_.get_reference();
      ObCastCtx context(&cast_owner, nullptr, CM_NONE, CS_TYPE_INVALID);
      context.runtime_ = &runtime;
      DatumValue output;
      // This is the legacy public caller contract: metadata supplies the
      // actual target declaration, while no target payload has been published.
      output.meta_.set_type(ObCollectionSQLType);
      output.meta_.set_subschema_id(id);
      output.meta_.set_has_lob_header();
      FreeingOwner result_owner;
      ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
          ObCollectionSQLType, context, input, result_owner, output));
      ASSERT_FALSE(output.is_null());
      EXPECT_EQ(ObCollectionSQLType, output.get_type());
      EXPECT_EQ(id, output.get_subschema_id());
      EXPECT_TRUE(output.has_lob_header());
      EXPECT_NE(source_bytes, output.datum_.get_reference());
      ASSERT_TRUE(result_owner.owns_live(static_cast<const char *>(output.datum_.get_reference()),
          output.datum_.length()));
      std::memset(const_cast<void *>(source_bytes), 0xa5, input.datum_.length());
      source_owner.reset();
      cast_owner.reset(); // Runtime parser/binary scratch is genuinely retired.
      ASSERT_TRUE(result_owner.owns_live(static_cast<const char *>(output.datum_.get_reference()),
          output.datum_.length()));
      ObString raw = output.get_string();
      ASSERT_EQ(OB_SUCCESS, ObTextStringHelper::read_real_string_data(
          exec, &execution_owner, ObLongTextType, CS_TYPE_BINARY, true, raw));
      ObIArrayType *array = nullptr;
      ASSERT_EQ(OB_SUCCESS, ObArrayTypeObjFactory::construct(
          execution_owner, *collection->collection_meta_, array, true));
      ASSERT_NE(nullptr, array); ASSERT_EQ(OB_SUCCESS, array->init(raw));
      ASSERT_EQ(3U, array->size());
      if (declaration == 0) {
        const auto *values = static_cast<const ObArrayFixedSize<int32_t> *>(array);
        EXPECT_EQ(1, (*values)[0]); EXPECT_EQ(-2, (*values)[1]); EXPECT_TRUE(values->is_null(2));
      } else {
        const auto *values = static_cast<const ObVectorF32Data *>(array);
        EXPECT_FLOAT_EQ(1.25F, (*values)[0]); EXPECT_FLOAT_EQ(-2.5F, (*values)[1]);
        EXPECT_FLOAT_EQ(3.0F, (*values)[2]);
      }
      EXPECT_FALSE(result_owner.unexpected_free_);
    }
  }
  EXPECT_EQ(0, service.calls_);
}

namespace {
class RejectCollectionResultOwner final : public ObIAllocator {
public:
  void *alloc(int64_t) override { ++calls_; return nullptr; }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *) override {}
  int64_t calls_ = 0;
};
}

TEST(NativeUdtResult, common_collection_cast_failures_preserve_previous_output_and_null_contract)
{
  ObArenaAllocator owner;
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ObExecContext exec(owner);
  exec.set_my_session(&session);
  ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &exec);
  ASSERT_EQ(OB_SUCCESS, exec.create_physical_plan_ctx());
  uint16_t vector_id = 0;
  ASSERT_EQ(OB_SUCCESS, exec.get_subschema_id_by_type_string(
      ObString::make_string("VECTOR(3)"), vector_id));
  ASSERT_FALSE(ob_is_reserved_subschema_id(vector_id));
  ObSqlDatumCastRuntime runtime(&exec);
  ObCastCtx context(&owner, nullptr, CM_NONE, CS_TYPE_INVALID);
  context.runtime_ = &runtime;
  DatumValue input;
  ASSERT_EQ(OB_SUCCESS, input.set_string(ObVarcharType, ObString::make_string("[1,2,3]"), owner));
  input.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  DatumValue output;
  output.meta_.set_type(ObCollectionSQLType); output.meta_.set_subschema_id(vector_id);
  output.meta_.set_has_lob_header();
  FreeingOwner result_owner;
  ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
      ObCollectionSQLType, context, input, result_owner, output));
  ASSERT_TRUE(result_owner.owns_live(static_cast<const char *>(output.datum_.get_reference()),
      output.datum_.length()));
  const DatumValue before = output;
  const std::string before_bytes(output.get_string().ptr(), output.get_string().length());
  auto expect_unchanged = [&]() {
    EXPECT_EQ(before.get_meta(), output.get_meta());
    EXPECT_EQ(before.datum_.get_reference(), output.datum_.get_reference());
    EXPECT_EQ(before.datum_.length(), output.datum_.length());
    ASSERT_TRUE(result_owner.owns_live(static_cast<const char *>(output.datum_.get_reference()),
        output.datum_.length()));
    EXPECT_EQ(before_bytes, std::string(output.get_string().ptr(), output.get_string().length()));
  };
  DatumValue wrong_dimension;
  ASSERT_EQ(OB_SUCCESS, wrong_dimension.set_string(ObVarcharType, ObString::make_string("[1,2]"), owner));
  wrong_dimension.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  EXPECT_EQ(OB_ERR_INVALID_VECTOR_DIM, DatumValueCaster::to_type(
      ObCollectionSQLType, context, wrong_dimension, result_owner, output));
  ASSERT_NO_FATAL_FAILURE(expect_unchanged());
  RejectCollectionResultOwner rejected;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, DatumValueCaster::to_type(
      ObCollectionSQLType, context, input, rejected, output));
  EXPECT_GT(rejected.calls_, 0);
  ASSERT_NO_FATAL_FAILURE(expect_unchanged());
  context.runtime_ = nullptr;
  EXPECT_EQ(OB_ERR_UNEXPECTED, DatumValueCaster::to_type(
      ObCollectionSQLType, context, input, result_owner, output));
  ASSERT_NO_FATAL_FAILURE(expect_unchanged());
  context.runtime_ = &runtime;

  // A real non-reserved but unregistered ID reaches the actual ExecCtx lookup.
  uint16_t unregistered_id = 0;
  ASSERT_EQ(OB_SUCCESS, exec.get_physical_plan_ctx()->get_subschema_ctx().get_new_subschema_id(unregistered_id));
  ASSERT_FALSE(ob_is_reserved_subschema_id(unregistered_id));
  DatumValue unregistered = before;
  unregistered.meta_.set_subschema_id(unregistered_id);
  EXPECT_EQ(OB_HASH_NOT_EXIST, DatumValueCaster::to_type(
      ObCollectionSQLType, context, input, result_owner, unregistered));
  EXPECT_EQ(unregistered_id, unregistered.get_subschema_id());
  EXPECT_EQ(before.datum_.get_reference(), unregistered.datum_.get_reference());
  DatumValue missing_declaration;
  EXPECT_EQ(OB_ERR_UNEXPECTED, DatumValueCaster::to_type(
      ObCollectionSQLType, context, input, result_owner, missing_declaration));
  EXPECT_TRUE(missing_declaration.is_null());

  // SQL NULL follows the original identity branch, preserving its actual
  // current metadata rather than manufacturing a Collection declaration.
  DatumValue null_input = input;
  null_input.datum_.set_null();
  DatumValue null_output;
  null_output.meta_ = before.get_meta();
  ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
      ObCollectionSQLType, context, null_input, result_owner, null_output));
  EXPECT_TRUE(null_output.is_null());
  EXPECT_EQ(null_input.get_meta(), null_output.get_meta());
  ASSERT_NO_FATAL_FAILURE(expect_unchanged());
  EXPECT_FALSE(result_owner.unexpected_free_);
}
