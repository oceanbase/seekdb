/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "sql/session/ob_sql_session_info.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/sql_mode/ob_sql_mode_utils.h"
#include "sql/engine/ob_exec_context.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share;
namespace {
std::vector<std::string> fields(const std::string &line)
{
  std::vector<std::string> result;
  size_t begin = 0;
  while (true) {
    const size_t end = line.find('\t', begin);
    result.push_back(line.substr(begin, end - begin));
    if (end == std::string::npos) { break; }
    begin = end + 1;
  }
  return result;
}
std::string unhex(const std::string &text)
{
  const char *digits = "0123456789abcdef";
  std::string bytes;
  for (size_t i = 0; i + 1 < text.size(); i += 2) {
    const char *hi = std::strchr(digits, text[i]), *lo = std::strchr(digits, text[i+1]);
    if (hi == nullptr || lo == nullptr) { ADD_FAILURE() << "invalid fixture hex"; return {}; }
    bytes.push_back(static_cast<char>((hi - digits) * 16 + lo - digits));
  }
  return bytes;
}
std::string wire(const DatumValue &value)
{
  int64_t size = 0, pos = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::value_size(value.datum_, value.meta_, size));
  std::string bytes(size, '\0');
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::encode_value(value.datum_, value.meta_,
      bytes.data(), bytes.size(), pos));
  EXPECT_EQ(size, pos);
  return bytes;
}
DatumValue decoded(const std::string &text, ObIAllocator &owner)
{
  const auto bytes = unhex(text);
  DatumValue value;
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::decode_value(bytes.data(), bytes.size(),
      pos, owner, value.datum_, value.meta_));
  EXPECT_EQ(bytes.size(), pos);
  return value;
}
std::string serialized(const ObBasicSysVar &provider)
{
  std::string bytes(provider.get_serialize_size(), '\0');
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, provider.serialize(bytes.data(), bytes.size(), pos));
  EXPECT_EQ(bytes.size(), pos);
  return bytes;
}
struct BufferIndexTag { using type = int32_t ObBasicSessionInfo::*; friend type access(BufferIndexTag); };
struct ThresholdTag { using type = int64_t ObBasicSessionInfo::*; friend type access(ThresholdTag); };
struct StageTag {
  using type = int (ObBasicSessionInfo::*)(ObArray<std::pair<int64_t, DatumValue>> &,
      ObSysVarClassType, const DatumValue &, ObIAllocator &);
  friend type access(StageTag);
};
template <typename Tag, typename Tag::type Member> struct PrivateAccess {
  friend typename Tag::type access(Tag) { return Member; }
};
template struct PrivateAccess<BufferIndexTag, &ObBasicSessionInfo::current_buf_index_>;
template struct PrivateAccess<ThresholdTag, &ObBasicSessionInfo::next_frag_mem_point_>;
template struct PrivateAccess<StageTag, &ObBasicSessionInfo::defragment_sys_variable_from>;
class BudgetOwner final : public ObIAllocator {
public:
  BudgetOwner(ObIAllocator &owner, int64_t budget) : owner_(owner), budget_(budget) {}
  void *alloc(int64_t size) override { return budget_-- > 0 ? owner_.alloc(size) : nullptr; }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *ptr) override { owner_.free(ptr); }
private:
  ObIAllocator &owner_;
  int64_t budget_;
};
class NativeSystemVariableValues : public testing::Test {
protected:
  ObSQLSessionInfo session;
  ObArenaAllocator owner;
  std::string previous_version;
  void SetUp() override
  {
    previous_version = ObSpecialSysVarValues::version_;
    std::strcpy(ObSpecialSysVarValues::version_, "5.7.25-native-system-variable-oracle");
    ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
    ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  }
  void TearDown() override { std::strcpy(ObSpecialSysVarValues::version_, previous_version.c_str()); }
};
TEST_F(NativeSystemVariableValues, all_generated_defaults_provider_outputs_and_wire_match_actual_old)
{
  std::ifstream input("unittest/sql/data/system_variable_values.golden");
  ASSERT_TRUE(input.good());
  std::string line;
  int checked = 0;
  while (std::getline(input, line)) {
    const auto cols = fields(line);
    if (cols.size() != 5 || cols[0] == "MODE") { continue; }
    SCOPED_TRACE(line);
    const int64_t index = std::stoll(cols[1]);
    const auto id = static_cast<ObSysVarClassType>(std::stoi(cols[2]));
    ASSERT_EQ(id, ObSysVariables::get_sys_var_id(index));
    ObBasicSysVar *provider = nullptr;
    int ret = session.get_sys_variable(id, provider);
    std::string actual;
    if (cols[0] == "DEFAULT") { ret = OB_SUCCESS; actual = wire(ObSysVariables::get_default_value(index)); }
    else if (cols[0] == "BASE") { ret = OB_SUCCESS; actual = wire(ObSysVariables::get_base_value(index)); }
    else if (cols[0] == "VALUE") { if (ret == OB_SUCCESS) { actual = wire(provider->get_value()); } }
    else if (ret == OB_SUCCESS && cols[0] == "SHOW") {
      ObString text;
      ret = provider->to_show_str(owner, session, text);
      if (ret == OB_SUCCESS) { actual.assign(text.ptr() == nullptr ? "" : text.ptr(), text.length()); }
    } else if (ret == OB_SUCCESS && cols[0] == "SELECT") {
      DatumValue value;
      ret = provider->to_select_obj(owner, session, value);
      if (ret == OB_SUCCESS) { actual = wire(value); }
    } else if (ret == OB_SUCCESS && cols[0] == "SERIALIZE") { actual = serialized(*provider); }
    else { FAIL() << "unknown fixture row " << line; }
    ASSERT_EQ(std::stoi(cols[3]), ret);
    if (ret == OB_SUCCESS) { EXPECT_EQ(unhex(cols[4]), actual); }
    ++checked;
  }
  EXPECT_EQ(6 * ObSysVariables::get_amount(), checked);
}
TEST_F(NativeSystemVariableValues, sql_mode_renderer_matches_actual_old_masks_metadata_and_error_order)
{
  std::ifstream input("unittest/sql/data/system_variable_values.golden");
  ASSERT_TRUE(input.good());
  std::string line;
  int checked = 0;
  while (std::getline(input, line)) {
    const auto cols = fields(line);
    if (cols.size() != 6 || cols[0] != "MODE") { continue; }
    SCOPED_TRACE(line);
    DatumValue source = decoded(cols[2], owner), output = decoded(cols[3], owner);
    const int index = std::stoi(cols[1]);
    ObWrapperAllocator reject;
    ObIAllocator *render_owner = index == 69 ? nullptr : index == 70 ? static_cast<ObIAllocator *>(&reject) : &owner;
    const int ret = ob_sql_mode_to_str(source, output, render_owner);
    EXPECT_EQ(std::stoi(cols[4]), ret);
    EXPECT_EQ(unhex(cols[5]), wire(output));
    ++checked;
  }
  EXPECT_EQ(71, checked);
}
TEST_F(NativeSystemVariableValues, real_type_range_and_custom_checkers_match_actual_old_strict_modes)
{
  std::ifstream input("unittest/sql/data/system_variable_values.golden");
  ASSERT_TRUE(input.good());
  ObExecContext context(owner);
  context.set_my_session(&session);
  ASSERT_EQ(OB_SUCCESS, context.create_physical_plan_ctx());
  ObMySQLProxy sql_proxy;
  std::string line;
  int checked = 0;
  while (std::getline(input, line)) {
    const auto cols = fields(line);
    if (cols.size() != 7 || cols[0] != "CHECK") { continue; }
    SCOPED_TRACE(line);
    const auto id = static_cast<ObSysVarClassType>(std::stoi(cols[2]));
    session.set_sql_mode(std::stoi(cols[3]) == 0 ? 0 : SMO_STRICT_ALL_TABLES);
    ObBasicSysVar *provider = nullptr;
    ASSERT_EQ(OB_SUCCESS, session.get_sys_variable(id, provider));
    const DatumValue source = decoded(cols[4], owner);
    DatumValue output;
    ASSERT_EQ(OB_SUCCESS, output.set_int(73, owner));
    ObSetVar set_var(provider->get_name(), ObSetVar::SET_SCOPE_SESSION, false, owner, sql_proxy);
    int ret = provider->check_update_type(set_var, source);
    if (ret == OB_SUCCESS) { ret = provider->check_and_convert(context, set_var, source, output); }
    EXPECT_EQ(std::stoi(cols[5]), ret);
    EXPECT_EQ(unhex(cols[6]), wire(output));
    ++checked;
  }
  EXPECT_EQ(140, checked);
}
TEST(NativeSystemVariableOwner, init_and_setter_failure_never_publish_half_owned_fields)
{
  ObArenaAllocator source_owner, destination_owner;
  DatumValue value, min, max;
  ASSERT_EQ(OB_SUCCESS, value.set_int(1, source_owner));
  ASSERT_EQ(OB_SUCCESS, min.set_int(0, source_owner));
  ASSERT_EQ(OB_SUCCESS, max.set_int(9, source_owner));
  ObSysVarAutocommit provider;
  BudgetOwner fail_after_base(destination_owner, 1);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, provider.init(value, min, max, ObIntType, 0, fail_after_base));
  EXPECT_TRUE(provider.is_base_value_empty());
  EXPECT_TRUE(provider.is_inc_value_empty());
  EXPECT_EQ(OB_SUCCESS, provider.init(value, min, max, ObIntType, 0, destination_owner));
  source_owner.reset();
  EXPECT_EQ(1, provider.get_value().get_int());
  EXPECT_EQ(9, provider.get_max_val().get_int());
  const std::string before = serialized(provider);
  DatumValue update;
  ASSERT_EQ(OB_SUCCESS, update.set_int(3, source_owner));
  ObWrapperAllocator reject;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, provider.set_value(update, reject));
  EXPECT_EQ(before, serialized(provider));
  EXPECT_EQ(OB_SUCCESS, provider.set_value(update, destination_owner));
  source_owner.reset();
  EXPECT_EQ(3, provider.get_value().get_int());
}
TEST_F(NativeSystemVariableValues, scalar_and_string_incremental_values_survive_two_compactions)
{
  DatumValue value;
  ASSERT_EQ(OB_SUCCESS, value.set_uint64(UINT64_MAX, owner));
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_LAST_INSERT_ID, value));
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_AUTOCOMMIT, int64_t(0)));
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR__OB_ENABLE_ROLE_IDS, ObString::make_string("11,22")));
  owner.reset();
  for (int i = 0; i < 2; ++i) {
    const int32_t previous = session.*access(BufferIndexTag{});
    session.*access(ThresholdTag{}) = -1;
    ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_OB_QUERY_TIMEOUT, int64_t(123456 + i)));
    EXPECT_NE(previous, session.*access(BufferIndexTag{}));
    ASSERT_EQ(OB_SUCCESS, session.get_sys_variable(SYS_VAR_LAST_INSERT_ID, value));
    EXPECT_EQ(UINT64_MAX, value.get_uint64());
    ASSERT_EQ(OB_SUCCESS, session.get_sys_variable(SYS_VAR_AUTOCOMMIT, value));
    EXPECT_EQ(0, value.get_int());
    ASSERT_EQ(OB_SUCCESS, session.get_sys_variable(SYS_VAR__OB_ENABLE_ROLE_IDS, value));
    EXPECT_EQ(ObString::make_string("11,22"), value.get_string());
  }
}
TEST_F(NativeSystemVariableValues, failed_compaction_stage_keeps_active_arenas_and_values)
{
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_AUTOCOMMIT, int64_t(0)));
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_OB_QUERY_TIMEOUT, int64_t(123456)));
  DatumValue old, pending;
  ASSERT_EQ(OB_SUCCESS, session.get_sys_variable(SYS_VAR_AUTOCOMMIT, old));
  ASSERT_EQ(OB_SUCCESS, pending.set_int(1, owner));
  const std::string old_wire = wire(old);
  const char *old_payload = old.datum_.ptr_;
  const int32_t index = session.*access(BufferIndexTag{});
  ObArray<std::pair<int64_t, DatumValue>> staged;
  ObArenaAllocator stage_owner;
  BudgetOwner fail_after_first(stage_owner, 1);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, (session.*access(StageTag{}))(
      staged, SYS_VAR_AUTOCOMMIT, pending, fail_after_first));
  EXPECT_EQ(0, staged.count());
  EXPECT_EQ(index, session.*access(BufferIndexTag{}));
  ASSERT_EQ(OB_SUCCESS, session.get_sys_variable(SYS_VAR_AUTOCOMMIT, old));
  EXPECT_EQ(old_wire, wire(old)); EXPECT_EQ(old_payload, old.datum_.ptr_);
  ASSERT_EQ(OB_SUCCESS, session.get_sys_variable(SYS_VAR_OB_QUERY_TIMEOUT, old));
  EXPECT_EQ(123456, old.get_int());
}
} // namespace
