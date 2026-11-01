/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "sql/session/ob_sql_session_info.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_parameter_array.h"
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
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::native_value_size(value.datum_, value.meta_, size));
  std::string bytes(size, '\0');
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::encode_native_value(value.datum_, value.meta_,
      bytes.data(), bytes.size(), pos));
  EXPECT_EQ(size, pos);
  return bytes;
}
DatumValue decoded(const std::string &text, ObIAllocator &owner)
{
  const auto bytes = unhex(text);
  DatumValue value;
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::decode_native_value(bytes.data(), bytes.size(),
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
void same_meta(const SqlTypeMeta &left, const SqlTypeMeta &right)
{
  EXPECT_EQ(left.get_type(), right.get_type());
  EXPECT_EQ(left.get_collation_type(), right.get_collation_type());
  EXPECT_EQ(left.get_collation_level(), right.get_collation_level());
  EXPECT_EQ(left.get_scale(), right.get_scale());
}
std::string provider_frame(const std::string &body)
{
  std::string bytes(1 + serialization::OB_SERIALIZE_SIZE_NEED_BYTES + body.size(), '\0');
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, serialization::encode_vi64(bytes.data(), bytes.size(), pos, 1));
  EXPECT_EQ(OB_SUCCESS, serialization::encode_fixed_bytes_i64(bytes.data(), bytes.size(), pos, body.size()));
  MEMCPY(bytes.data() + pos, body.data(), body.size());
  return bytes;
}
class RestoreGlobalSources
{
public:
  RestoreGlobalSources()
  {
    for (int i = 0; i < 2; ++i) {
      names_[i] = ObSysVariables::get_name(i);
      defaults_[i] = ObSysVariables::get_value(i);
      bases_[i] = ObSysVariables::get_base_str_value(i);
    }
  }
  ~RestoreGlobalSources()
  {
    for (int i = 0; i < 2; ++i) {
      EXPECT_EQ(OB_SUCCESS, ObSysVariables::set_value(names_[i], defaults_[i]));
      EXPECT_EQ(OB_SUCCESS, ObSysVariables::set_base_value(names_[i], bases_[i]));
    }
    EXPECT_EQ(OB_SUCCESS, ObSysVariables::init_default_values());
  }
private:
  ObString names_[2], defaults_[2], bases_[2];
};
struct BufferIndexTag { using type = int32_t ObBasicSessionInfo::*; friend type access(BufferIndexTag); };
struct ThresholdTag { using type = int64_t ObBasicSessionInfo::*; friend type access(ThresholdTag); };
struct StageTag {
  using type = int (ObBasicSessionInfo::*)(ObArray<std::pair<int64_t, std::pair<Datum, SqlTypeMeta>>> &,
      ObSysVarClassType, const Datum &, const SqlTypeMeta &, ObIAllocator &);
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
  void *alloc(int64_t size) override { ++calls_; return budget_-- > 0 ? owner_.alloc(size) : nullptr; }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *ptr) override { owner_.free(ptr); }
  int64_t calls() const { return calls_; }
private:
  ObIAllocator &owner_;
  int64_t budget_;
  int64_t calls_ = 0;
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
    alignas(8) char scratch[64];
    DatumValue exported;
    if (cols[0] == "DEFAULT") {
      ret = ObSysVariables::export_default_value(index, exported, scratch, sizeof(scratch));
      if (ret == OB_SUCCESS) { actual = wire(exported); }
    } else if (cols[0] == "BASE") {
      ret = ObSysVariables::export_base_value(index, exported, scratch, sizeof(scratch));
      if (ret == OB_SUCCESS) { actual = wire(exported); }
    } else if (cols[0] == "VALUE") {
      if (ret == OB_SUCCESS) { ret = provider->export_value(exported, scratch, sizeof(scratch)); }
      if (ret == OB_SUCCESS) { actual = wire(exported); }
    } else if (ret == OB_SUCCESS && cols[0] == "SHOW") {
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
  ObSysVarAutocommit scalar;
  BudgetOwner reject_scalars(destination_owner, 0);
  ASSERT_EQ(OB_SUCCESS, scalar.init(value, min, max, ObIntType, 0, reject_scalars));
  EXPECT_EQ(0, reject_scalars.calls());
  source_owner.reset();
  EXPECT_EQ(1, scalar.get_value_datum().get_int());
  EXPECT_EQ(9, scalar.get_max_val_datum().get_int());
  ASSERT_EQ(OB_SUCCESS, value.set_int(3, source_owner));
  ASSERT_EQ(OB_SUCCESS, scalar.set_value(value, reject_scalars));
  EXPECT_EQ(0, reject_scalars.calls());
  EXPECT_EQ(3, scalar.get_value_datum().get_int());
  const std::string scalar_wire = serialized(scalar);
  ObSysVarAutocommit scalar_decoded;
  int64_t scalar_pos = 0;
  ASSERT_EQ(OB_SUCCESS, scalar_decoded.deserialize(scalar_wire.data(), scalar_wire.size(), scalar_pos, reject_scalars));
  EXPECT_EQ(scalar_wire.size(), scalar_pos); EXPECT_EQ(0, reject_scalars.calls());
  EXPECT_EQ(3, scalar_decoded.get_value_datum().get_int());
  DatumValue unsigned_value;
  ASSERT_EQ(OB_SUCCESS, unsigned_value.set_uint64(UINT64_MAX, source_owner));
  ObSysVarLastInsertId unsigned_scalar;
  DatumValue absent;
  ASSERT_EQ(OB_SUCCESS, unsigned_scalar.init(unsigned_value, absent, absent, ObUInt64Type, 0, reject_scalars));
  ASSERT_EQ(OB_SUCCESS, unsigned_scalar.set_value(unsigned_value, reject_scalars));
  source_owner.reset();
  EXPECT_EQ(UINT64_MAX, unsigned_scalar.get_value_datum().get_uint());
  EXPECT_EQ(0, reject_scalars.calls());

  // Failure publication needs real reference allocation in this owner domain.
  ASSERT_EQ(OB_SUCCESS, value.set_varchar(ObString("base"), source_owner));
  ASSERT_EQ(OB_SUCCESS, min.set_varchar(ObString("a"), source_owner));
  ASSERT_EQ(OB_SUCCESS, max.set_varchar(ObString("z"), source_owner));
  ObSysVarVersion provider;
  BudgetOwner fail_after_base(destination_owner, 1);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, provider.init(value, min, max, ObVarcharType, 0, fail_after_base));
  EXPECT_TRUE(provider.is_base_value_empty());
  EXPECT_TRUE(provider.is_inc_value_empty());
  EXPECT_TRUE(provider.get_max_val_datum().is_null());
  EXPECT_EQ(OB_SUCCESS, provider.init(value, min, max, ObVarcharType, 0, destination_owner));
  source_owner.reset();
  EXPECT_EQ(ObString("base"), provider.get_value_datum().get_string());
  EXPECT_EQ(ObString("z"), provider.get_max_val_datum().get_string());
  const std::string before = serialized(provider);
  DatumValue update;
  ASSERT_EQ(OB_SUCCESS, update.set_varchar(ObString("next"), source_owner));
  ObWrapperAllocator reject;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, provider.set_value(update, reject));
  EXPECT_EQ(before, serialized(provider));
  EXPECT_EQ(OB_SUCCESS, provider.set_value(update, destination_owner));
  source_owner.reset();
  EXPECT_EQ(ObString("next"), provider.get_value_datum().get_string());

  number::ObNumber coefficient;
  ASSERT_EQ(OB_SUCCESS, coefficient.from("1.25", 4, source_owner));
  DatumValue number_value;
  ASSERT_EQ(OB_SUCCESS, number_value.set_number(coefficient, source_owner));
  ObSysVarLongQueryTime numeric;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, numeric.init(number_value, absent, absent, ObNumberType, 0, reject));
  EXPECT_TRUE(numeric.is_base_value_empty()); EXPECT_TRUE(numeric.is_inc_value_empty());
  ASSERT_EQ(OB_SUCCESS, numeric.init(number_value, absent, absent, ObNumberType, 0, destination_owner));
  const std::string numeric_before = serialized(numeric);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, numeric.set_value(number_value, reject));
  EXPECT_EQ(numeric_before, serialized(numeric));
  source_owner.reset();
  number::ObNumber expected;
  ASSERT_EQ(OB_SUCCESS, expected.from("1.25", 4, source_owner));
  EXPECT_EQ(0, number::ObNumber(numeric.get_value_datum().get_number()).compare(expected));
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
    alignas(8) char scratch[64];
    ASSERT_EQ(OB_SUCCESS, session.export_sys_variable(SYS_VAR_LAST_INSERT_ID, value, scratch, sizeof(scratch)));
    EXPECT_EQ(UINT64_MAX, value.get_uint64());
    ASSERT_EQ(OB_SUCCESS, session.export_sys_variable(SYS_VAR_AUTOCOMMIT, value, scratch, sizeof(scratch)));
    EXPECT_EQ(0, value.get_int());
    ASSERT_EQ(OB_SUCCESS, session.export_sys_variable(SYS_VAR__OB_ENABLE_ROLE_IDS, value, scratch, sizeof(scratch)));
    EXPECT_EQ(ObString::make_string("11,22"), value.get_string());
  }
}
TEST_F(NativeSystemVariableValues, failed_compaction_stage_keeps_active_arenas_and_values)
{
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_AUTOCOMMIT, int64_t(0)));
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_OB_QUERY_TIMEOUT, int64_t(123456)));
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_TIME_ZONE, ObString("+08:00")));
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR__OB_ENABLE_ROLE_IDS, ObString("11,22")));
  DatumValue old, pending;
  alignas(8) char scratch[64];
  ASSERT_EQ(OB_SUCCESS, session.export_sys_variable(SYS_VAR__OB_ENABLE_ROLE_IDS, old, scratch, sizeof(scratch)));
  ASSERT_EQ(OB_SUCCESS, pending.set_varchar(ObString("33,44"), owner));
  const Datum pending_native = pending.datum_;
  const std::string old_wire = wire(old);
  const void *old_payload = old.datum_.get_reference();
  const int32_t index = session.*access(BufferIndexTag{});
  ObArray<std::pair<int64_t, std::pair<Datum, SqlTypeMeta>>> staged;
  ObArenaAllocator stage_owner;
  BudgetOwner fail_after_first(stage_owner, 1);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, (session.*access(StageTag{}))(
      staged, SYS_VAR__OB_ENABLE_ROLE_IDS, pending_native, pending.meta_, fail_after_first));
  EXPECT_EQ(0, staged.count());
  EXPECT_EQ(index, session.*access(BufferIndexTag{}));
  ASSERT_EQ(OB_SUCCESS, session.export_sys_variable(SYS_VAR__OB_ENABLE_ROLE_IDS, old, scratch, sizeof(scratch)));
  EXPECT_EQ(old_wire, wire(old)); EXPECT_EQ(old_payload, old.datum_.get_reference());
  ASSERT_EQ(OB_SUCCESS, session.export_sys_variable(SYS_VAR_AUTOCOMMIT, old, scratch, sizeof(scratch)));
  EXPECT_EQ(0, old.get_int());
  ASSERT_EQ(OB_SUCCESS, session.export_sys_variable(SYS_VAR_OB_QUERY_TIMEOUT, old, scratch, sizeof(scratch)));
  EXPECT_EQ(123456, old.get_int());
}
TEST(NativeSystemVariableOwner, nop_null_and_incremental_priority_keep_exact_physical_metadata)
{
  ObArenaAllocator source, destination;
  DatumValue base, minimum, maximum, update;
  ASSERT_EQ(OB_SUCCESS, base.set_int(1, source)); base.set_scale(7);
  ASSERT_EQ(OB_SUCCESS, minimum.set_int(0, source));
  ASSERT_EQ(OB_SUCCESS, maximum.set_int(9, source));
  ObSysVarAutocommit provider;
  EXPECT_TRUE(provider.is_base_value_empty()); EXPECT_TRUE(provider.is_inc_value_empty());
  ASSERT_EQ(OB_SUCCESS, provider.init(base, minimum, maximum, ObIntType, ObSysVarFlag::NULLABLE, destination));
  ASSERT_EQ(OB_SUCCESS, update.set_int(3, source)); update.set_scale(9);
  ASSERT_EQ(OB_SUCCESS, provider.set_value(update, destination));
  EXPECT_EQ(3, provider.get_value_datum().get_int()); EXPECT_EQ(9, provider.get_value_meta().get_scale());
  provider.clean_inc_value();
  EXPECT_TRUE(provider.is_inc_value_empty());
  EXPECT_EQ(1, provider.get_value_datum().get_int()); EXPECT_EQ(7, provider.get_value_meta().get_scale());
  update.set_null(); update.set_scale(4);
  ASSERT_EQ(OB_SUCCESS, provider.set_value(update, destination));
  EXPECT_FALSE(provider.is_inc_value_empty()); EXPECT_TRUE(provider.get_value_datum().is_null());
  EXPECT_EQ(ObNullType, provider.get_value_meta().get_type()); EXPECT_EQ(4, provider.get_value_meta().get_scale());
  update.meta_ = base.meta_; update.set_scale(6); // Typed NULL keeps its physical declaration.
  ASSERT_EQ(OB_SUCCESS, provider.set_value(update, destination));
  EXPECT_FALSE(provider.is_inc_value_empty()); EXPECT_TRUE(provider.get_value_datum().is_null());
  same_meta(update.meta_, provider.get_value_meta());
  provider.clean_inc_value(); provider.clean_base_value();
  EXPECT_TRUE(provider.is_base_value_empty()); EXPECT_TRUE(provider.is_inc_value_empty());
  EXPECT_TRUE(provider.get_value_datum().is_nop());
  // Legacy set_ext() clears the Extend subtype, which shares the scale byte.
  SqlTypeMeta expected_nop_meta = base.meta_;
  expected_nop_meta.set_ext();
  EXPECT_EQ(0, expected_nop_meta.get_extend_type());
  same_meta(expected_nop_meta, provider.get_value_meta());
  provider.clean_value();
  EXPECT_TRUE(provider.get_max_val_datum().is_null());
  SqlTypeMeta default_meta;
  same_meta(default_meta, provider.get_max_val_meta());

  // A noncanonical legacy NOP wrapper still means empty, not SQL NULL.
  alignas(8) char payload[DATUM_EXTEND_PAYLOAD_SIZE];
  ObDatum marker; marker.ptr_ = payload;
  marker.set_extend(ObActionFlag::OP_NOP, 0, 17, CS_LEVEL_INVALID, CS_TYPE_INVALID);
  SqlTypeMeta marker_meta; marker_meta.set_ext(); marker_meta.set_scale(13);
  DatumValue marker_value;
  ASSERT_EQ(OB_SUCCESS, marker_value.assign_legacy_borrowed(marker, marker_meta));
  ASSERT_EQ(OB_SUCCESS, provider.set_value(marker_value, destination));
  EXPECT_TRUE(provider.is_inc_value_empty());

  // SQL-array's eight-byte address wrapper must not be read as ordinary ext.
  auto *array = DatumParameterArray::alloc(source, 1);
  ASSERT_NE(nullptr, array); array->element_.set_int();
  ASSERT_EQ(OB_SUCCESS, array->data_[0].set_int(73, source));
  DatumValue envelope;
  ASSERT_EQ(OB_SUCCESS, envelope.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, source));
  ASSERT_EQ(OB_SUCCESS, provider.set_value(envelope, destination));
  EXPECT_FALSE(provider.is_inc_value_empty());
  alignas(8) char scratch[64]; DatumValue exported;
  ASSERT_EQ(OB_SUCCESS, provider.export_value(exported, scratch, sizeof(scratch)));
  const auto *owned_array = reinterpret_cast<const DatumParameterArray *>(exported.get_ext());
  ASSERT_NE(nullptr, owned_array); EXPECT_NE(array, owned_array);
  source.reset();
  EXPECT_EQ(73, owned_array->data_[0].get_int());

  int64_t unknown_payload = 37;
  ObDatum unknown; unknown.ptr_ = reinterpret_cast<const char *>(&unknown_payload); unknown.pack_ = sizeof(unknown_payload);
  SqlTypeMeta unknown_meta; unknown_meta.set_unknown();
  DatumValue unknown_value;
  ASSERT_EQ(OB_SUCCESS, unknown_value.assign_legacy_borrowed(unknown, unknown_meta));
  ASSERT_EQ(OB_SUCCESS, provider.set_value(unknown_value, destination));
  EXPECT_EQ(8U, provider.get_value_datum().length()); // Generic domain, not Raw inline index.
  EXPECT_NE(&unknown_payload, provider.get_value_datum().get_reference());
  unknown_payload = 99;
  ASSERT_EQ(OB_SUCCESS, provider.export_value(exported, scratch, sizeof(scratch)));
  EXPECT_EQ(37, exported.get_unknown()); same_meta(unknown_meta, exported.meta_);
}

TEST_F(NativeSystemVariableValues, legacy_provider_unis_reuse_owns_wire_and_preserves_omitted_fields)
{
  std::ifstream input("unittest/sql/data/system_variable_values.golden");
  ASSERT_TRUE(input.good());
  std::string line, captured_frame, captured_value;
  while (std::getline(input, line)) {
    const auto cols = fields(line);
    if (cols.size() == 5 && std::stoi(cols[2]) == SYS_VAR_VERSION) {
      if (cols[0] == "SERIALIZE") { captured_frame = unhex(cols[4]); }
      if (cols[0] == "VALUE") { captured_value = unhex(cols[4]); }
    }
  }
  ASSERT_FALSE(captured_frame.empty()); ASSERT_FALSE(captured_value.empty());
  ObSysVarVersion provider;
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, provider.deserialize(captured_frame.data(), captured_frame.size(), pos, owner));
  EXPECT_EQ(captured_frame.size(), pos);
  const std::string expected_frame = captured_frame;
  MEMSET(captured_frame.data(), 0xa5, captured_frame.size());
  alignas(8) char scratch[64]; DatumValue selected;
  ASSERT_EQ(OB_SUCCESS, provider.export_value(selected, scratch, sizeof(scratch)));
  EXPECT_EQ(captured_value, wire(selected)); EXPECT_EQ(expected_frame, serialized(provider));
  const std::string omitted = provider_frame("");
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, provider.deserialize(omitted.data(), omitted.size(), pos, owner));
  EXPECT_EQ(omitted.size(), pos); EXPECT_EQ(expected_frame, serialized(provider));
  // The final string terminator and trailing provider fields may be omitted.
  ASSERT_EQ(0, captured_value.back());
  const std::string no_terminator = provider_frame(captured_value.substr(0, captured_value.size()-1));
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, provider.deserialize(no_terminator.data(), no_terminator.size(), pos, owner));
  EXPECT_EQ(no_terminator.size(), pos); EXPECT_EQ(expected_frame, serialized(provider));
  SqlTypeMeta invalid; invalid.set_type(ObMaxType);
  std::string bad_meta(invalid.get_serialize_size(), '\0'); pos = 0;
  ASSERT_EQ(OB_SUCCESS, invalid.serialize(bad_meta.data(), bad_meta.size(), pos));
  const std::string malformed = provider_frame(bad_meta);
  pos = 0;
  EXPECT_NE(OB_SUCCESS, provider.deserialize(malformed.data(), malformed.size(), pos, owner));
  EXPECT_EQ(malformed.size(), pos); // Outer UNIS advances to body end even on error.
  EXPECT_EQ(expected_frame, serialized(provider));
}

TEST_F(NativeSystemVariableValues, global_default_base_failure_keeps_pair_and_published_prefix)
{
  ASSERT_GE(ObSysVariables::get_amount(), 3);
  ASSERT_EQ(ObUInt64Type, ObSysVariables::get_type(0));
  ASSERT_EQ(ObUInt64Type, ObSysVariables::get_type(1));
  std::vector<std::string> defaults, bases;
  alignas(8) char scratch[64]; DatumValue value;
  for (int64_t i = 0; i < ObSysVariables::get_amount(); ++i) {
    ASSERT_EQ(OB_SUCCESS, ObSysVariables::export_default_value(i, value, scratch, sizeof(scratch)));
    defaults.push_back(wire(value));
    ASSERT_EQ(OB_SUCCESS, ObSysVariables::export_base_value(i, value, scratch, sizeof(scratch)));
    bases.push_back(wire(value));
  }
  {
    RestoreGlobalSources restore;
    ASSERT_EQ(OB_SUCCESS, ObSysVariables::set_value(ObSysVariables::get_name(0), ObString("2")));
    ASSERT_EQ(OB_SUCCESS, ObSysVariables::set_base_value(ObSysVariables::get_name(0), ObString("3")));
    ASSERT_EQ(OB_SUCCESS, ObSysVariables::set_value(ObSysVariables::get_name(1), ObString("5")));
    ASSERT_EQ(OB_SUCCESS, ObSysVariables::set_base_value(ObSysVariables::get_name(1), ObString("not-a-number")));
    EXPECT_NE(OB_SUCCESS, ObSysVariables::init_default_values());
    ASSERT_EQ(OB_SUCCESS, ObSysVariables::export_default_value(0, value, scratch, sizeof(scratch)));
    EXPECT_EQ(2U, value.get_uint64());
    ASSERT_EQ(OB_SUCCESS, ObSysVariables::export_base_value(0, value, scratch, sizeof(scratch)));
    EXPECT_EQ(3U, value.get_uint64());
    for (int64_t i = 1; i < ObSysVariables::get_amount(); ++i) {
      ASSERT_EQ(OB_SUCCESS, ObSysVariables::export_default_value(i, value, scratch, sizeof(scratch)));
      EXPECT_EQ(defaults[i], wire(value));
      ASSERT_EQ(OB_SUCCESS, ObSysVariables::export_base_value(i, value, scratch, sizeof(scratch)));
      EXPECT_EQ(bases[i], wire(value));
    }
  } // Always restore both metadata sources and perform real initialization.
  for (int64_t i = 0; i < ObSysVariables::get_amount(); ++i) {
    ASSERT_EQ(OB_SUCCESS, ObSysVariables::export_default_value(i, value, scratch, sizeof(scratch)));
    EXPECT_EQ(defaults[i], wire(value));
    ASSERT_EQ(OB_SUCCESS, ObSysVariables::export_base_value(i, value, scratch, sizeof(scratch)));
    EXPECT_EQ(bases[i], wire(value));
  }
}
} // namespace
