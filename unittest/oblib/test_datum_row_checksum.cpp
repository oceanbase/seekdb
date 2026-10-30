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

// Native-only row checksum regressions against frozen legacy wire fixtures.
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_parameter_array.h"
#include "lib/allocator/ob_allocator.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Native row checksum tests must not include the old value definition"
#endif
using namespace oceanbase::common;
namespace {
std::vector<std::string> columns(const std::string &line)
{ std::vector<std::string> out; std::istringstream in(line); std::string f;
  while (std::getline(in, f, '\t')) { out.push_back(f); } return out; }
std::string unhex(const std::string &s)
{ std::string out; for (size_t i = 0; i < s.size(); i += 2)
  { out += static_cast<char>(std::stoul(s.substr(i, 2), nullptr, 16)); } return out; }
std::string hex(const char *bytes, uint32_t length)
{ if (length == 0) { return "-"; } const char *digits = "0123456789abcdef"; std::string out;
  for (uint32_t i = 0; i < length; ++i) { unsigned char c = bytes[i]; out += digits[c >> 4]; out += digits[c & 15]; } return out; }
void check_ext(const DatumValue &value, const char *label)
{
  // Templates were captured from the old common Datum conversion before old
  // test code was removed. Only address/value and extent-size slots vary.
  std::ifstream input("unittest/oblib/data/datum_common_row_checksum.golden");
  ASSERT_TRUE(input.good()); std::string line; std::vector<std::string> f;
  while (std::getline(input, line)) {
    if (line.compare(0, 2, "X\t") == 0) { auto fields = columns(line);
      if (fields.at(1) == label) { f = fields; break; } }
  }
  ASSERT_FALSE(f.empty()); auto payload = unhex(f.at(3)); auto wire = unhex(f.at(5));
  ASSERT_EQ(DATUM_EXTEND_PAYLOAD_SIZE, payload.size()); ASSERT_EQ(DATUM_LEGACY_EXTEND_SIZE, wire.size());
  const int64_t address = value.get_ext(); const int32_t extent_size = value.get_val_len();
  MEMCPY(&payload[0], &address, sizeof(address)); MEMCPY(&payload[8], &extent_size, sizeof(extent_size));
  MEMCPY(&wire[8], &address, sizeof(address)); MEMCPY(&wire[4], &extent_size, sizeof(extent_size));
  const uint32_t wire_pack = std::stoul(f.at(4));
  int64_t expected = ob_crc64_sse42(918, &wire_pack, sizeof(wire_pack));
  expected = ob_crc64_sse42(expected, wire.data(), wire.size());
  char scratch[DATUM_EXTEND_PAYLOAD_SIZE]; ObDatum row;
  ASSERT_EQ(OB_SUCCESS, value.to_row_datum(row, scratch));
  EXPECT_EQ(std::stoul(f.at(2)), row.pack_); EXPECT_EQ(payload, std::string(row.ptr_, row.len_));
  EXPECT_EQ(wire_pack, row.get_wire_pack()); char actual_wire[DATUM_LEGACY_EXTEND_SIZE];
  EXPECT_EQ(wire, std::string(row.get_wire_ext_payload(actual_wire), sizeof(actual_wire)));
  EXPECT_EQ(expected, row.checksum(918));
}
DatumParameter live_array(ObIAllocator &owner)
{
  DatumParameter value; auto *array = DatumParameterArray::alloc(owner, 1);
  EXPECT_NE(nullptr, array); if (array == nullptr) { return value; }
  EXPECT_EQ(OB_SUCCESS, array->data_[0].set_int(918, owner));
  SqlTypeMeta meta; meta.set_ext(); meta.set_extend_type(T_EXT_SQL_ARRAY); value.set_type_meta(meta);
  EXPECT_EQ(OB_SUCCESS, value.alloc_datum_reserved_buff(meta, -1, owner)); value.set_array_address(array);
  return value;
}
}
TEST(NativeRowChecksum, original_common_datum_frames_match_bytes_flags_and_crc)
{
  std::ifstream input("unittest/oblib/data/datum_common_row_checksum.golden"); ASSERT_TRUE(input.good());
  ObArenaAllocator owner; std::string line; int checked = 0;
  while (std::getline(input, line)) {
    if (line.compare(0, 2, "C\t") != 0) { continue; } const auto f = columns(line); SCOPED_TRACE(line);
    const auto bytes = unhex(f.at(2)); DatumValue value; int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_value(bytes.data(), bytes.size(), pos, owner, value.datum_, value.meta_));
    ASSERT_EQ(bytes.size(), pos); char scratch[DATUM_EXTEND_PAYLOAD_SIZE]; ObDatum row;
    const int ret = value.to_row_datum(row, scratch); ASSERT_EQ(std::stoi(f.at(3)), ret);
    if (ret == OB_SUCCESS) {
      EXPECT_EQ(std::stoul(f.at(4)), row.pack_);
      EXPECT_EQ(f.at(5), hex(row.ptr_, row.is_null() ? 0 : row.len_));
      EXPECT_EQ(std::stoul(f.at(6)), row.get_wire_pack());
      if (row.is_native_extend()) { char wire[DATUM_LEGACY_EXTEND_SIZE];
        EXPECT_EQ(f.at(7), hex(row.get_wire_ext_payload(wire), sizeof(wire))); }
      else { EXPECT_EQ(f.at(7), hex(row.ptr_, row.is_null() ? 0 : row.len_)); }
      EXPECT_EQ(std::stoll(f.at(8)), row.checksum(0));
    }
    ++checked;
  }
  EXPECT_EQ(111, checked);
}
TEST(NativeRowChecksum, current_extent_metadata_overrides_creation_snapshot)
{
  ObArenaAllocator owner;
  for (int scale : {0, 3, 6}) {
    DatumParameter parameter; ASSERT_EQ(OB_SUCCESS, parameter.set_ext(ObActionFlag::OP_DEFAULT_NOW_FLAG, owner));
    parameter.set_scale(scale); parameter.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    const std::string label = "now-scale-" + std::to_string(scale);
    check_ext(parameter.value_view(), label.c_str());
  }
  DatumValue sized; ASSERT_EQ(OB_SUCCESS, sized.set_extend(ObActionFlag::OP_NOP, 0, owner, 123));
  check_ext(sized, "nop-size");
}
TEST(NativeRowChecksum, array_live_and_decoded_sizes_are_distinct_and_survive_cloning)
{
  ObArenaAllocator source, decoded_owner, clone_owner; const DatumParameter live = live_array(source);
  ASSERT_FALSE(live.is_null()); EXPECT_EQ(0, live.get_val_len()); EXPECT_EQ(0, live.value_view().get_val_len()); check_ext(live.value_view(), "array");
  int64_t size = 0; ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::value_size(live.datum_, live.get_meta(), size));
  std::string wire(size, '\0'); int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::encode_value(live.datum_, live.get_meta(), &wire[0], wire.size(), pos));
  DatumParameter decoded; SqlTypeMeta meta; pos = 0; ObDatum datum;
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_value(wire.data(), wire.size(), pos, decoded_owner, datum, meta));
  decoded.datum_ = datum; decoded.set_type_meta(meta); ASSERT_GT(decoded.get_val_len(), 0);
  EXPECT_EQ(decoded.get_array<DatumParameterArray>()->get_serialize_size(), decoded.get_val_len());
  check_ext(decoded.value_view(), "array");
  DatumParameter clone; ASSERT_EQ(OB_SUCCESS, DatumParameterArray::clone_parameter(decoded, clone, clone_owner));
  ASSERT_NE(decoded.get_ext(), clone.get_ext()); EXPECT_EQ(decoded.get_val_len(), clone.get_val_len()); check_ext(clone.value_view(), "array");
  // The source size is provenance; do not silently replace it with a new body size.
  ASSERT_EQ(OB_SUCCESS, clone.get_array<DatumParameterArray>()->data_[0].set_varchar(ObString::make_string("longer value"), clone_owner));
  EXPECT_EQ(decoded.get_val_len(), clone.get_val_len());
  EXPECT_NE(clone.get_val_len(), clone.get_array<DatumParameterArray>()->get_serialize_size()); check_ext(clone.value_view(), "array");
}
TEST(NativeRowChecksum, typed_null_is_canonical_and_failed_conversion_preserves_output)
{
  ObArenaAllocator owner; DatumParameter typed_null; SqlTypeMeta type; type.set_enum(); typed_null.set_type_meta(type);
  ObDatum row; char scratch[DATUM_EXTEND_PAYLOAD_SIZE]; ASSERT_EQ(OB_SUCCESS, typed_null.value_view().to_row_datum(row, scratch));
  EXPECT_TRUE(row.is_null()); EXPECT_EQ(0, row.len_); EXPECT_EQ(ObDatumDesc::FlagType::NONE, row.flag_);
  DatumValue bad; ASSERT_EQ(OB_SUCCESS, bad.set_int(1, owner)); bad.datum_.len_ = 1;
  EXPECT_EQ(OB_INVALID_ARGUMENT, bad.to_row_datum(row, scratch)); EXPECT_TRUE(row.is_null());
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  if (OB_SUCCESS != ObCharset::init_charset()) { return 2; }
  return RUN_ALL_TESTS();
}
