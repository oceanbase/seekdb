/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "share/session/ob_local_session_var.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_parameter_array.h"
#include "sql/plan_cache/ob_plan_cache_util.h"
#include "sql/session/ob_local_session_var.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/system_variable/ob_system_variable_init.h"
#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::sql;
namespace {
std::vector<std::string> columns(const std::string &line)
{
  std::vector<std::string> result;
  size_t begin = 0;
  while (true) {
    const auto end = line.find('\t', begin);
    result.push_back(line.substr(begin, end - begin));
    if (end == std::string::npos) { break; }
    begin = end + 1;
  }
  return result;
}
std::string unhex(const std::string &text)
{
  const char *digits = "0123456789abcdef";
  std::string result;
  for (size_t i = 0; i + 1 < text.size(); i += 2) {
    const char *hi = std::strchr(digits, text[i]), *lo = std::strchr(digits, text[i + 1]);
    if (hi == nullptr || lo == nullptr) { ADD_FAILURE() << "bad fixture hex"; return {}; }
    result.push_back(static_cast<char>((hi - digits) * 16 + lo - digits));
  }
  return result;
}
template<typename T> std::string wire(const T &value)
{
  std::string result(value.get_serialize_size(), '\0');
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, value.serialize(result.data(), result.size(), pos));
  EXPECT_EQ(result.size(), pos);
  return result;
}
DatumValue decoded(const std::string &hex, ObIAllocator &owner)
{
  const auto bytes = unhex(hex);
  DatumValue value;
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::decode_native_value(bytes.data(), bytes.size(), pos,
      owner, value.datum_, value.meta_));
  EXPECT_EQ(bytes.size(), pos);
  return value;
}
std::vector<std::string> fixture(const char *name)
{
  std::ifstream input("unittest/sql/data/local_session_values.golden");
  EXPECT_TRUE(input.good());
  std::string line;
  while (std::getline(input, line)) {
    const auto cols = columns(line);
    if (!cols.empty() && cols[0] == name) { return cols; }
  }
  ADD_FAILURE() << "fixture missing " << name;
  return {};
}
void decode(const std::string &bytes, ObLocalSessionVar &value)
{
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, value.deserialize(bytes.data(), bytes.size(), pos));
  ASSERT_EQ(bytes.size(), pos);
}
class BudgetOwner final : public ObIAllocator {
public:
  ObArenaAllocator arena;
  int64_t budget = INT64_MAX;
  int64_t calls = 0;
  void *alloc(int64_t size) override { ++calls; return budget-- > 0 ? arena.alloc(size) : nullptr; }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *ptr) override { arena.free(ptr); }
};
TEST(NativeLocalSessionValues, every_actual_old_valid_snapshot_roundtrips_full_wire_and_owns_input)
{
  std::ifstream input("unittest/sql/data/local_session_values.golden");
  ASSERT_TRUE(input.good());
  std::string line;
  int checked = 0;
  while (std::getline(input, line)) {
    const auto cols = columns(line);
    if (cols.size() != 4 || cols[0].find("EQUAL_") == 0 || cols[0] == "OMITTED_VALUE_REUSE") { continue; }
    SCOPED_TRACE(line);
    ASSERT_EQ(0, std::stoi(cols[1]));
    std::string bytes = unhex(cols[3]);
    ObArenaAllocator owner;
    ObLocalSessionVar value(&owner);
    decode(bytes, value);
    EXPECT_EQ(std::stoll(cols[2]), value.get_var_count());
    EXPECT_EQ(bytes, wire(value));
    std::memset(bytes.data(), 'x', bytes.size());
    EXPECT_EQ(unhex(cols[3]), wire(value));
    ++checked;
  }
  EXPECT_EQ(17, checked);
}
TEST(NativeLocalSessionValues, actual_old_binary_and_collation_exception_comparisons_are_preserved)
{
  std::ifstream input("unittest/sql/data/local_session_values.golden");
  ASSERT_TRUE(input.good());
  std::string line;
  int checked = 0;
  ObArenaAllocator owner;
  while (std::getline(input, line)) {
    const auto cols = columns(line);
    if (cols.size() != 4 || cols[0].find("EQUAL_") != 0) { continue; }
    SCOPED_TRACE(line);
    ObSessionSysVar variable;
    variable.type_ = SYS_VAR_SQL_MODE;
    ASSERT_EQ(OB_SUCCESS, variable.set_value(decoded(cols[1], owner), owner));
    const DatumValue right = decoded(cols[2], owner);
    EXPECT_EQ(std::stoi(cols[3]) != 0, variable.is_equal(right));
    ++checked;
  }
  EXPECT_EQ(4, checked);
}
TEST(NativeLocalSessionValues, legal_omitted_value_reuses_previous_value_and_container_reuse_is_unchanged)
{
  ObArenaAllocator owner;
  const auto omitted = fixture("OMITTED_VALUE_REUSE");
  ASSERT_EQ(4, omitted.size());
  ObSessionSysVar leaf;
  leaf.type_ = SYS_VAR_SQL_MODE;
  Datum initial; initial.set_uint(99);
  SqlTypeMeta initial_meta; initial_meta.set_uint64();
  ASSERT_EQ(OB_SUCCESS, leaf.set_native_value(initial, initial_meta, owner));
  const auto input = unhex(omitted[1]);
  int64_t pos = 0;
  ASSERT_EQ(std::stoi(omitted[2]), leaf.deserialize(input.data(), input.size(), pos, owner));
  EXPECT_EQ(input.size(), pos); EXPECT_EQ(unhex(omitted[3]), wire(leaf));
  const auto snapshot = fixture("four_sysvars");
  ASSERT_EQ(4, snapshot.size());
  const auto bytes = unhex(snapshot[3]);
  ObLocalSessionVar value(&owner);
  decode(bytes, value); decode(bytes, value);
  EXPECT_EQ(4, value.get_var_count());
  EXPECT_EQ(bytes, wire(value));
  ObLocalSessionVar copy(&owner);
  ASSERT_EQ(OB_SUCCESS, copy.deep_copy(value));
  EXPECT_TRUE(copy == value);
  ASSERT_EQ(OB_SUCCESS, copy.deep_copy(copy));
  EXPECT_EQ(0, copy.get_var_count()); // Preserve the actual old self-reset rule.
}
TEST(NativeLocalSessionValues, hex_body_matches_actual_old_and_rehydrates_native_values)
{
  ObArenaAllocator owner;
  const auto snapshot = fixture("four_sysvars"), old_text = fixture("HEX_BODY");
  ASSERT_EQ(4, snapshot.size()); ASSERT_EQ(3, old_text.size());
  ObLocalSessionVar value(&owner);
  decode(unhex(snapshot[3]), value);
  ObString text;
  ASSERT_EQ(std::stoi(old_text[1]), value.gen_local_session_var_str(owner, text));
  EXPECT_EQ(old_text[2], std::string(text.ptr(), text.length()));
  ObLocalSessionVar rehydrated(&owner);
  ASSERT_EQ(OB_SUCCESS, rehydrated.fill_local_session_var_from_str(text));
  EXPECT_EQ(wire(value), wire(rehydrated));
}
TEST(NativeLocalSessionValues, failed_payload_clone_does_not_publish_new_record)
{
  BudgetOwner owner;
  ObLocalSessionVar value(&owner);
  ASSERT_EQ(OB_SUCCESS, value.set_local_var_capacity(4));
  ObArenaAllocator source;
  DatumValue input;
  ASSERT_EQ(OB_SUCCESS, input.set_varchar(ObString::make_string("payload must allocate"), source));
  input.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  owner.budget = 1; // Record allocation succeeds, its native payload allocation fails.
  const auto old_failure = fixture("OOM_RECORD_CLONE");
  ASSERT_EQ(3, old_failure.size());
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, std::stoi(old_failure[1]));
  EXPECT_EQ(1, std::stoi(old_failure[2])); // Actual old push-before-clone bug.
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, value.add_local_var(SYS_VAR_TIME_ZONE, input));
  EXPECT_EQ(0, value.get_var_count()); // Native publishing is deliberately atomic.

  EXPECT_EQ(ObString::make_string("payload must allocate"), input.get_string());
  owner.budget = INT64_MAX;
  ASSERT_EQ(OB_SUCCESS, value.add_local_var(SYS_VAR_TIME_ZONE, input));
  source.reset();
  ObSessionSysVar *entry = nullptr;
  ASSERT_EQ(OB_SUCCESS, value.get_local_var(SYS_VAR_TIME_ZONE, entry));
  ASSERT_NE(nullptr, entry); EXPECT_EQ(ObString::make_string("payload must allocate"), entry->get_value_datum().get_string());
}
TEST(NativeLocalSessionValues, cache_key_equality_hash_and_text_match_actual_old)
{
  std::ifstream input("unittest/sql/data/local_session_values.golden");
  ASSERT_TRUE(input.good());
  std::string line;
  int checked = 0;
  ObArenaAllocator owner;
  while (std::getline(input, line)) {
    const auto cols = columns(line);
    if (cols.size() != 8 || cols[0].find("CACHE_") != 0) { continue; }
    SCOPED_TRACE(line);
    ObSysVarInPC left, right;
    ASSERT_EQ(OB_SUCCESS, left.push_back(decoded(cols[1], owner)));
    ASSERT_EQ(OB_SUCCESS, right.push_back(decoded(cols[2], owner)));
    EXPECT_EQ(std::stoi(cols[3]) != 0, left == right);
    EXPECT_EQ(std::stoll(cols[4]), left.hash(42));
    EXPECT_EQ(std::stoll(cols[5]), right.hash(42));
    char buffer[4096]; int64_t pos = 0;
    const int ret = left.serialize_sys_vars(buffer, sizeof(buffer), pos);
    EXPECT_EQ(std::stoi(cols[6]), ret);
    if (ret == OB_SUCCESS) { EXPECT_EQ(unhex(cols[7]), std::string(buffer, pos)); }
    ++checked;
  }
  EXPECT_EQ(8, checked);
}
TEST(NativeLocalSessionValues, recursive_native_array_clone_and_cache_buffer_own_all_descriptor_payloads)
{
  ObArenaAllocator source, destination;
  auto *array = DatumParameterArray::alloc(source, 2);
  ASSERT_NE(nullptr, array);
  ASSERT_EQ(OB_SUCCESS, array->data_[0].set_uint64(UINT64_MAX, source));
  ASSERT_EQ(OB_SUCCESS, array->data_[1].set_varchar(ObString::make_string("array leaf"), source));
  array->data_[1].set_collation_type(CS_TYPE_UTF8MB4_BIN);
  DatumValue input;
  ASSERT_EQ(OB_SUCCESS, input.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, source));
  ObLocalSessionVar local(&destination);
  ASSERT_EQ(OB_SUCCESS, local.set_local_var_capacity(1));
  ASSERT_EQ(OB_SUCCESS, local.add_local_var(SYS_VAR_SQL_MODE, input));
  char buffer[4096];
  ObSysVarInPC original, copied;
  ASSERT_EQ(OB_SUCCESS, original.push_back(input));
  copied.buf_ = buffer; copied.buf_size_ = sizeof(buffer);
  ASSERT_EQ(OB_SUCCESS, copied.deep_copy(original));
  source.reset();
  ObSessionSysVar *entry = nullptr;
  ASSERT_EQ(OB_SUCCESS, local.get_local_var(SYS_VAR_SQL_MODE, entry));
  ASSERT_NE(nullptr, entry);
  DatumValue array_view;
  alignas(8) char array_scratch[64];
  ASSERT_EQ(OB_SUCCESS, entry->export_value(array_view, array_scratch, sizeof(array_scratch)));
  const auto *owned_array = reinterpret_cast<const DatumParameterArray *>(array_view.get_ext());
  EXPECT_EQ(UINT64_MAX, owned_array->data_[0].get_uint64());
  EXPECT_EQ(ObString::make_string("array leaf"), owned_array->data_[1].get_string());
  const auto *cache_array = reinterpret_cast<const DatumParameterArray *>(copied.system_variables_.at(0).get_ext());
  EXPECT_NE(array, cache_array); EXPECT_NE(owned_array, cache_array);
  EXPECT_EQ(UINT64_MAX, cache_array->data_[0].get_uint64());
  EXPECT_EQ(ObString::make_string("array leaf"), cache_array->data_[1].get_string());
  EXPECT_GE(entry->get_deep_copy_size(), sizeof(*entry) + sizeof(DatumParameterArray)
      + 2 * sizeof(DatumParameter) + 8 + 10);
}

TEST(NativeLocalSessionValues, native_inline_entry_has_no_payload_allocation_and_failed_reference_keeps_pair)
{
  BudgetOwner owner;
  ObSessionSysVar entry;
  Datum value;
  value.set_int(INT64_MIN);
  SqlTypeMeta meta;
  meta.set_int(); meta.set_scale(6);
  owner.budget = 0;
  const int64_t calls = owner.calls;
  ASSERT_EQ(OB_SUCCESS, entry.set_native_value(value, meta, owner));
  EXPECT_EQ(calls, owner.calls);
  EXPECT_EQ(INT64_MIN, entry.get_value_datum().get_int());
  EXPECT_EQ(0, entry.get_value_datum().length());
  EXPECT_EQ(sizeof(entry), entry.get_deep_copy_size());
  const std::string old_wire = wire(entry);
  Datum reference;
  ASSERT_EQ(OB_SUCCESS, reference.set_string(ObString::make_string("must own the snapshot")));
  SqlTypeMeta text_meta; text_meta.set_varchar(); text_meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, entry.set_native_value(reference, text_meta, owner));
  EXPECT_EQ(old_wire, wire(entry));
  EXPECT_EQ(6, entry.get_value_meta().get_scale());

  // The container still allocates its entry and pointer array. Isolate the
  // entry allocation after reserving capacity; no scalar payload is allocated.
  owner.budget = INT64_MAX;
  ObLocalSessionVar snapshot(&owner);
  ASSERT_EQ(OB_SUCCESS, snapshot.set_local_var_capacity(1));
  const int64_t before = owner.calls;
  owner.budget = 1;
  ASSERT_EQ(OB_SUCCESS, snapshot.add_local_var_native(SYS_VAR_MAX_ALLOWED_PACKET, value, meta));
  EXPECT_EQ(before + 1, owner.calls);
  ObSessionSysVar *stored = nullptr;
  ASSERT_EQ(OB_SUCCESS, snapshot.get_local_var(SYS_VAR_MAX_ALLOWED_PACKET, stored));
  ASSERT_NE(nullptr, stored);
  EXPECT_EQ(INT64_MIN, stored->get_value_datum().get_int());
}

class NativeLocalSessionCapture : public ::testing::Test {
protected:
  static void SetUpTestCase()
  {
    ASSERT_EQ(OB_SUCCESS, ObCharset::init_charset());
    ASSERT_EQ(OB_SUCCESS, ObSysVariables::init_default_values());
  }
};

TEST_F(NativeLocalSessionCapture, actual_session_capture_assign_and_copy_keep_distinct_owner_contracts)
{
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ObArenaAllocator source("LocalSnapSrc"), target("LocalSnapDst");
  ObLocalSessionVar original(&source), borrowed(&target), owned(&target);
  ASSERT_EQ(OB_SUCCESS, ObLocalSessionVarHelper::load_session_vars(&session, original));
  ASSERT_EQ(4, original.get_var_count());
  ASSERT_EQ(OB_SUCCESS, borrowed.assign(original));
  ASSERT_EQ(OB_SUCCESS, owned.deep_copy(original));
  ObSessionSysVar *origin = nullptr, *alias = nullptr, *clone = nullptr;
  ASSERT_EQ(OB_SUCCESS, original.get_local_var(SYS_VAR_TIME_ZONE, origin));
  ASSERT_EQ(OB_SUCCESS, borrowed.get_local_var(SYS_VAR_TIME_ZONE, alias));
  ASSERT_EQ(OB_SUCCESS, owned.get_local_var(SYS_VAR_TIME_ZONE, clone));
  ASSERT_NE(nullptr, origin); ASSERT_NE(nullptr, clone);
  EXPECT_EQ(origin, alias);
  EXPECT_NE(origin, clone);
  EXPECT_NE(origin->get_value_datum().get_reference(), clone->get_value_datum().get_reference());
  const std::string saved = wire(owned);
  bool same = false;
  ASSERT_EQ(OB_SUCCESS, ObLocalSessionVarHelper::check_var_same_with_session(session, clone, same));
  EXPECT_TRUE(same);
  borrowed.reset(); original.reset(); source.reset();
  EXPECT_EQ(saved, wire(owned));
  ObString hex;
  ASSERT_EQ(OB_SUCCESS, owned.gen_local_session_var_str(target, hex));
  ObLocalSessionVar restored(&target);
  ASSERT_EQ(OB_SUCCESS, restored.fill_local_session_var_from_str(hex));
  EXPECT_TRUE(restored == owned);
}
} // namespace
