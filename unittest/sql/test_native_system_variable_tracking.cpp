/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "sql/session/ob_sql_session_info.h"
#include "common/datum/datum_parameter_codec.h"
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
std::string decode_hex(const std::string &text)
{
  std::string bytes;
  const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
  for (size_t i = 0; i + 1 < text.size(); i += 2) {
    bytes.push_back(static_cast<char>((digit(text[i]) << 4) | digit(text[i + 1])));
  }
  return bytes;
}
std::vector<std::string> columns(const std::string &line)
{
  std::vector<std::string> result;
  std::istringstream input(line);
  std::string value;
  while (std::getline(input, value, '\t')) { result.push_back(value); }
  return result;
}
std::string wire(const DatumValue &value)
{
  int64_t size = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::value_size(value.datum_, value.meta_, size));
  std::string bytes(size, '\0');
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::encode_value(
      value.datum_, value.meta_, bytes.data(), bytes.size(), pos));
  EXPECT_EQ(size, pos);
  return bytes;
}
class NativeSystemVariableTracking : public testing::Test {
protected:
  ObSQLSessionInfo session;
  ObArenaAllocator owner;
  void SetUp() override
  {
    ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
    ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
    session.reset_session_changed_info();
  }
};

TEST_F(NativeSystemVariableTracking, compare_matches_actual_immutable_old_function)
{
  std::ifstream input("unittest/sql/data/system_variable_tracking.golden");
  ASSERT_TRUE(input.good());
  std::string line;
  int compared = 0;
  bool roles_changed = false;
  while (std::getline(input, line)) {
    const auto fields = columns(line);
    if (fields.size() != 6) { continue; }
    SCOPED_TRACE(line);
    if (!roles_changed && fields[0].find("roles_current_1122_") == 0) {
      ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(
          SYS_VAR__OB_ENABLE_ROLE_IDS, ObString::make_string("11,22")));
      roles_changed = true;
    }
    const auto id = static_cast<ObSysVarClassType>(std::stoi(fields[1]));
    const std::string input_wire = decode_hex(fields[2]);
    DatumValue old, current;
    int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_value(input_wire.data(), input_wire.size(),
        pos, owner, old.datum_, old.meta_));
    ASSERT_EQ(input_wire.size(), pos);
    bool changed = false;
    const int ret = session.is_sys_var_actully_changed(id, old, current, changed, owner);
    EXPECT_EQ(std::stoi(fields[3]), ret);
    EXPECT_EQ(std::stoi(fields[4]) != 0, changed);
    if (ret == OB_SUCCESS) { EXPECT_EQ(decode_hex(fields[5]), wire(current)); }
    ++compared;
  }
  EXPECT_EQ(72, compared);
}

TEST_F(NativeSystemVariableTracking, first_snapshot_owns_bytes_and_survives_later_setters)
{
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(
      SYS_VAR__OB_ENABLE_ROLE_IDS, ObString::make_string("11,22")));
  ObBasicSysVar *provider = nullptr;
  ASSERT_EQ(OB_SUCCESS, session.get_sys_variable(SYS_VAR__OB_ENABLE_ROLE_IDS, provider));
  ASSERT_NE(nullptr, provider);
  ASSERT_EQ(OB_SUCCESS, session.track_sys_var(SYS_VAR__OB_ENABLE_ROLE_IDS, provider->get_value()));
  ASSERT_EQ(1, session.get_changed_sys_var().count());
  const DatumValue &snapshot = session.get_changed_sys_var().at(0).old_val_;
  EXPECT_EQ(ObString::make_string("11,22"), snapshot.get_string());
  const ObString source = provider->get_value().get_string();
  EXPECT_NE(source.ptr(), snapshot.datum_.ptr_);
  // The old provider's value is a genuine producer; overwrite its bytes only
  // after tracking, to verify that the tracker owns its payload separately.
  std::memset(const_cast<char *>(source.ptr()), '3', source.length());
  EXPECT_EQ(ObString::make_string("11,22"), snapshot.get_string());
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(
      SYS_VAR__OB_ENABLE_ROLE_IDS, ObString::make_string("33,44")));
  ASSERT_EQ(OB_SUCCESS, session.track_sys_var(SYS_VAR__OB_ENABLE_ROLE_IDS, provider->get_value()));
  ASSERT_EQ(1, session.get_changed_sys_var().count());
  EXPECT_EQ(ObString::make_string("11,22"), snapshot.get_string());
  session.reset_session_changed_info();
  EXPECT_EQ(0, session.get_changed_sys_var().count());
  EXPECT_FALSE(session.is_session_info_changed());
}

TEST_F(NativeSystemVariableTracking, actual_setter_tracks_before_change_and_keeps_first_value)
{
  oceanbase::obmysql::ObMySQLCapabilityFlags capability;
  capability.cap_flags_.OB_CLIENT_SESSION_TRACK = 1;
  session.set_capability(capability);
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_AUTOCOMMIT, int64_t(0)));
  ASSERT_EQ(1, session.get_changed_sys_var().count());
  const auto &entry = session.get_changed_sys_var().at(0);
  EXPECT_EQ(SYS_VAR_AUTOCOMMIT, entry.id_);
  EXPECT_EQ(1, entry.old_val_.get_int());
  DatumValue current;
  bool changed = false;
  ASSERT_EQ(OB_SUCCESS, session.is_sys_var_actully_changed(
      entry.id_, entry.old_val_, current, changed, owner));
  EXPECT_TRUE(changed); EXPECT_EQ(0, current.get_int());
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_AUTOCOMMIT, int64_t(1)));
  EXPECT_EQ(1, session.get_changed_sys_var().count());
  ASSERT_EQ(OB_SUCCESS, session.is_sys_var_actully_changed(
      entry.id_, entry.old_val_, current, changed, owner));
  EXPECT_FALSE(changed); EXPECT_EQ(1, current.get_int());
}

TEST_F(NativeSystemVariableTracking, failed_current_snapshot_does_not_publish_output)
{
  ObBasicSysVar *provider = nullptr;
  ASSERT_EQ(OB_SUCCESS, session.get_sys_variable(SYS_VAR_AUTOCOMMIT, provider));
  ASSERT_EQ(OB_SUCCESS, session.track_sys_var(SYS_VAR_AUTOCOMMIT, provider->get_value()));
  DatumValue output;
  ASSERT_EQ(OB_SUCCESS, output.set_int(99, owner));
  const char *previous = output.datum_.ptr_;
  const SqlTypeMeta previous_meta = output.meta_;
  ObWrapperAllocator reject;
  bool changed = false;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, session.is_sys_var_actully_changed(SYS_VAR_AUTOCOMMIT,
      session.get_changed_sys_var().at(0).old_val_, output, changed, reject));
  EXPECT_TRUE(changed); EXPECT_EQ(previous, output.datum_.ptr_);
  EXPECT_EQ(previous_meta, output.meta_); EXPECT_EQ(99, output.get_int());
  EXPECT_EQ(1, session.get_changed_sys_var().count());
  EXPECT_EQ(OB_INVALID_ARGUMENT, session.is_sys_var_actully_changed(SYS_VAR_INVALID,
      session.get_changed_sys_var().at(0).old_val_, output, changed, reject));
  EXPECT_EQ(previous, output.datum_.ptr_); EXPECT_EQ(99, output.get_int());
}
} // namespace
