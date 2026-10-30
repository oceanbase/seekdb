#include <gtest/gtest.h>
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
  ASSERT_TRUE(result_owner.owns_live(value.datum_.ptr_, value.datum_.len_));
  EXPECT_EQ(std::string("[7,-9,NULL]"),
      std::string(value.get_string().ptr(), value.get_string().length()));
  EXPECT_EQ(ObCollectionSQLType, value.get_type());
  EXPECT_EQ(id, value.get_subschema_id());
  EXPECT_EQ(0, service.calls_);
  EXPECT_FALSE(result_owner.unexpected_free_);
}
