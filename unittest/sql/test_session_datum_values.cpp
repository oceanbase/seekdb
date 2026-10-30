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

#include "sql/session/ob_session_val_map.h"
#include "common/datum/datum_parameter_codec.h"
#include "share/datum/number_cast_constants.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
struct CompactTag {
  using type = int (ObSessionValMap::*)(ObString *, ObSessionVariable *, bool *);
  friend type access_map(CompactTag);
};
struct IndexTag { using type = int32_t ObSessionValMap::*; friend type access_map(IndexTag); };
struct ThresholdTag { using type = int64_t ObSessionValMap::*; friend type access_map(ThresholdTag); };
template <typename Tag, typename Tag::type Member> struct PrivateMapAccess {
  friend typename Tag::type access_map(Tag) { return Member; }
};
template struct PrivateMapAccess<CompactTag, &ObSessionValMap::free_mem>;
template struct PrivateMapAccess<IndexTag, &ObSessionValMap::current_buf_index_>;
template struct PrivateMapAccess<ThresholdTag, &ObSessionValMap::next_free_mem_point_>;
std::vector<std::string> fields(const std::string &line) {
  std::vector<std::string> result; size_t begin = 0;
  while (true) {
    size_t end = line.find('\t', begin);
    if (end == std::string::npos) { result.push_back(line.substr(begin)); break; }
    result.push_back(line.substr(begin, end - begin)); begin = end + 1;
  }
  return result;
}
std::string unhex(const std::string &hex) {
  const char *digits = "0123456789abcdef"; std::string bytes;
  if (hex.size() % 2) { ADD_FAILURE() << "invalid fixture hex"; return bytes; }
  for (size_t i = 0; i < hex.size(); i += 2) {
    const char *high = std::strchr(digits, hex[i]), *low = std::strchr(digits, hex[i+1]);
    if (high == nullptr || low == nullptr) { ADD_FAILURE() << "invalid fixture hex"; return {}; }
    bytes.push_back(static_cast<char>((high-digits)*16 + low-digits));
  }
  return bytes;
}
std::string serialized(const ObSessionValMap &map) {
  std::string result(map.get_serialize_size(), '\0'); int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, map.serialize(result.data(), result.size(), pos));
  EXPECT_EQ(result.size(), pos); return result;
}
std::string fixture(const char *id) {
  std::ifstream input("unittest/sql/data/session_values_map.golden");
  EXPECT_TRUE(input.good()); std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') { continue; }
    const auto columns = fields(line);
    if (columns.size() == 6 && columns[1] == id) { return unhex(columns[5]); }
  }
  ADD_FAILURE() << "missing fixture " << id; return {};
}
void decode(const std::string &wire, ObSessionValMap &map) {
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, map.deserialize(wire.data(), wire.size(), pos));
  ASSERT_EQ(wire.size(), pos);
}
class RejectingSessionOwner final : public ObIAllocator {
public:
  void *alloc(int64_t) override { return nullptr; }
  void *alloc(int64_t, const ObMemAttr &) override { return nullptr; }
  void free(void *) override {}
};
}
TEST(SessionDatumValues, matches_all_actual_old_valid_map_snapshots) {
  std::ifstream input("unittest/sql/data/session_values_map.golden");
  ASSERT_TRUE(input.good()); std::string line; int checked = 0;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') { continue; }
    const auto columns = fields(line); ASSERT_EQ(6, columns.size()); SCOPED_TRACE(columns[1]);
    const std::string bytes = unhex(columns[5]); ObSessionValMap map;
    decode(bytes, map);
    EXPECT_EQ(std::strtoll(columns[3].c_str(), nullptr, 10), map.size());
    EXPECT_EQ(std::strtoll(columns[4].c_str(), nullptr, 10), map.get_serialize_size());
    EXPECT_EQ(bytes, serialized(map)); ++checked;
  }
  EXPECT_EQ(35, checked);
}
TEST(SessionDatumValues, actual_temporal_setter_raw_unspecified_collation_survives_copy_and_compaction) {
  for (const char *id : {"setter_datetime", "setter_date", "setter_time", "setter_year"}) {
    SCOPED_TRACE(id);
    const std::string bytes = fixture(id); ObSessionValMap map; decode(bytes, map);
    const auto *entry = map.get(ObString::make_string("variable")); ASSERT_NE(nullptr, entry);
    EXPECT_EQ(ObVarcharType, entry->value_.get_type());
    EXPECT_EQ(CS_TYPE_INVALID, entry->value_.get_collation_type());
    EXPECT_EQ(CS_LEVEL_IMPLICIT, entry->value_.get_collation_level());
    EXPECT_EQ(CS_TYPE_UTF8MB4_GENERAL_CI, entry->meta_.get_collation_type());
    map.*access_map(ThresholdTag{}) = -1;
    ASSERT_EQ(OB_SUCCESS, (map.*access_map(CompactTag{}))(nullptr, nullptr, nullptr));
    EXPECT_EQ(bytes, serialized(map));
  }
}

TEST(SessionDatumValues, declaration_metadata_and_null_payload_metadata_remain_independent) {
  ObSessionValMap map; const std::string bytes = fixture("null_distinct_declaration_meta"); decode(bytes, map);
  const auto *entry = map.get(ObString::make_string("variable")); ASSERT_NE(nullptr, entry);
  EXPECT_EQ(ObVarcharType, entry->meta_.get_type()); EXPECT_EQ(12, entry->meta_.get_scale());
  EXPECT_EQ(CS_TYPE_UTF8MB4_GENERAL_CI, entry->meta_.get_collation_type());
  EXPECT_TRUE(entry->value_.is_null()); EXPECT_EQ(ObNullType, entry->value_.get_type());
  EXPECT_EQ(7, entry->value_.get_scale()); EXPECT_EQ(CS_TYPE_BINARY, entry->value_.get_collation_type());
  EXPECT_EQ(bytes, serialized(map));
}
TEST(SessionDatumValues, payloads_survive_source_reuse_compaction_and_owning_assign) {
  std::string bytes = fixture("three_keys"); const std::string expected = bytes;
  ObSessionValMap source; decode(bytes, source); bytes.assign(bytes.size(), 'x');
  EXPECT_EQ(expected, serialized(source));
  const int32_t index = source.*access_map(IndexTag{}); source.*access_map(ThresholdTag{}) = -1;
  ASSERT_EQ(OB_SUCCESS, (source.*access_map(CompactTag{}))(nullptr, nullptr, nullptr));
  EXPECT_NE(index, source.*access_map(IndexTag{})); EXPECT_EQ(expected, serialized(source));
  ObSessionValMap copy; ASSERT_EQ(OB_SUCCESS, copy.init(128, 64, nullptr));
  ASSERT_EQ(OB_SUCCESS, copy.assign(source)); source.reuse(); EXPECT_EQ(expected, serialized(copy));
}
TEST(SessionDatumValues, borrowed_self_setter_survives_arena_switch) {
  const std::string bytes = fixture("three_keys"); ObSessionValMap map; decode(bytes, map);
  // Use the map-owned key and variable directly, not an already-owned test copy.
  auto iter = map.get_val_map().begin(); ASSERT_TRUE(iter != map.get_val_map().end());
  const int32_t index = map.*access_map(IndexTag{}); map.*access_map(ThresholdTag{}) = -1;
  ASSERT_EQ(OB_SUCCESS, map.set_refactored(iter->first, iter->second));
  EXPECT_NE(index, map.*access_map(IndexTag{})); EXPECT_EQ(bytes, serialized(map));
  ObSessionVariable borrowed; ASSERT_EQ(OB_SUCCESS, map.get_refactored(ObString::make_string("text"), borrowed));
  map.*access_map(ThresholdTag{}) = -1;
  ASSERT_EQ(OB_SUCCESS, map.set_refactored(ObString::make_string("new_text"), borrowed));
  const auto *copy = map.get(ObString::make_string("new_text")); ASSERT_NE(nullptr, copy);
  EXPECT_EQ(ObString::make_string("source_owned"), copy->value_.get_string());
}
TEST(SessionDatumValues, failed_pending_clone_does_not_publish_compaction_or_replace_entry) {
  const std::string bytes = fixture("three_keys"); ObSessionValMap map; decode(bytes, map);
  ObSessionVariable invalid; invalid.meta_.set_int(); invalid.value_.meta_.set_int();
  invalid.value_.datum_.set_string("x", 1); // Invalid fixed-width native payload.
  const int32_t index = map.*access_map(IndexTag{}); map.*access_map(ThresholdTag{}) = -1;
  EXPECT_EQ(OB_INVALID_ARGUMENT, map.set_refactored(ObString::make_string("count"), invalid));
  EXPECT_EQ(index, map.*access_map(IndexTag{})); EXPECT_EQ(bytes, serialized(map));
}
TEST(SessionDatumValues, variable_record_decode_failure_keeps_previous_value_and_position) {
  ObSessionValMap map; decode(fixture("int"), map); const auto *entry = map.get(ObString::make_string("variable")); ASSERT_NE(nullptr, entry);
  std::string record(entry->get_serialize_size(), '\0'); int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, entry->serialize(record.data(), record.size(), pos));
  ObArenaAllocator owner; ObSessionVariable existing; existing.meta_.set_varchar();
  ASSERT_EQ(OB_SUCCESS, existing.value_.set_int(99, owner)); const char *previous = existing.value_.datum_.ptr_;
  RejectingSessionOwner reject; pos = 0;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, existing.deserialize(record.data(), record.size(), pos, reject));
  EXPECT_EQ(0, pos); EXPECT_EQ(previous, existing.value_.datum_.ptr_); EXPECT_EQ(99, existing.value_.get_int());
  EXPECT_EQ(ObVarcharType, existing.meta_.get_type());
}
TEST(SessionDatumValues, real_decode_failure_is_not_overwritten_by_map_insertion) {
  const std::string valid = fixture("erase_text"); ASSERT_EQ(31, valid.size());
  // Actual old map returned SUCCESS/pos29/two entries for this incomplete vi64.
  // Native map deliberately propagates the true error and skips the failed entry.
  ObSessionValMap map; int64_t pos = 0;
  EXPECT_NE(OB_SUCCESS, map.deserialize(valid.data(), valid.size() - 1, pos));
  EXPECT_EQ(1, map.size()); EXPECT_EQ(nullptr, map.get(ObString::make_string("count")));
  EXPECT_NE(nullptr, map.get(ObString::make_string("nil")));
}
