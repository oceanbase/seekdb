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

#include "common/datum/datum_parameter_codec.h"
#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Native codec tests must not include the legacy value definition"
#endif
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>
#include <gtest/gtest.h>
#include "lib/allocator/ob_allocator.h"
using namespace oceanbase::common;
namespace {
class RejectAllocator : public ObIAllocator {
public:
  int calls = 0;
  void *alloc(int64_t) override { ++calls; return nullptr; }
  void *alloc(int64_t, const ObMemAttr &) override { ++calls; return nullptr; }
  void free(void *) override {}
};
DatumParameter integer(ObIAllocator &arena, int64_t n) {
  DatumParameter p; SqlTypeMeta meta; meta.set_int(); p.set_type_meta(meta);
  EXPECT_EQ(OB_SUCCESS, p.alloc_datum_reserved_buff(meta, -1, arena)); p.datum_.set_int(n); return p;
}
std::string encode(const DatumParameter &p) {
  int64_t size = -1; EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::parameter_size(p, size));
  if (size <= 0) { return {}; }
  std::string wire(size, '\0'); int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::encode_parameter(p, &wire[0], wire.size(), pos));
  EXPECT_EQ(size, pos); return wire;
}
DatumParameter make_array(ObIAllocator &arena, int64_t count) {
  DatumParameter p; SqlTypeMeta meta; meta.set_ext(); meta.set_extend_type(T_EXT_SQL_ARRAY); p.set_type_meta(meta);
  EXPECT_EQ(OB_SUCCESS, p.alloc_datum_reserved_buff(meta, -1, arena));
  p.set_array_address(DatumParameterArray::alloc(arena, count)); return p;
}
}
TEST(DatumCodec, integer_legacy_golden) {
  ObArenaAllocator arena; DatumParameter p = integer(arena, 42);
  const unsigned char expected[] = {5, 5, 63, 255, 42};
  char wire[32]; int64_t pos = 0, size = -1;
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::value_size(p.datum_, p.get_type_meta(), size));
  EXPECT_EQ(sizeof(expected), size);
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::encode_value(p.datum_, p.get_type_meta(), wire, sizeof(wire), pos));
  ASSERT_EQ(sizeof(expected), pos); EXPECT_EQ(0, memcmp(expected, wire, pos));
  ObDatum d; SqlTypeMeta meta; int64_t read = 0;
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_value(wire, pos, read, arena, d, meta));
  EXPECT_EQ(pos, read); EXPECT_EQ(42, d.get_int()); EXPECT_EQ(ObIntType, meta.get_type());
}
TEST(DatumCodec, typed_null_keeps_accuracy_but_runtime_fields_are_not_on_wire) {
  ObArenaAllocator arena; DatumParameter p; SqlTypeMeta meta; meta.set_varchar(); meta.set_scale(6);
  p.set_type_meta(meta); p.set_param_meta(meta); p.set_raw_text_info(19, 31);
  p.get_param_flag().flag_ = 0xa5; p.get_param_flag().extend_flag_ = 0x5a;
  ObAccuracy accuracy; accuracy.set_accuracy(0x123456789abcdef0LL); p.set_accuracy(accuracy); p.set_result_flags(0xfedcba98U);
  const std::string wire = encode(p); int64_t pos = 0; DatumParameter out;
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_parameter(wire.data(), wire.size(), pos, arena, out));
  EXPECT_TRUE(out.is_null()); EXPECT_EQ(ObNullType, out.get_type());
  EXPECT_EQ(p.get_accuracy(), out.get_accuracy()); EXPECT_EQ(p.get_result_flag(), out.get_result_flag());
  EXPECT_EQ(-1, out.get_raw_text_pos()); EXPECT_EQ(-1, out.get_raw_text_len()); EXPECT_TRUE(out.get_param_meta().is_null());
  EXPECT_EQ(DatumParameter().get_param_flag().flag_, out.get_param_flag().flag_); EXPECT_EQ(0, out.get_param_flag().extend_flag_);
}
TEST(DatumCodec, decoded_string_outlives_wire_and_source_arena) {
  ObArenaAllocator destination; DatumParameter out; const std::string expected("a\0中文\xff", 9);
  {
    ObArenaAllocator source_arena; DatumParameter p; SqlTypeMeta meta; meta.set_varbinary(); p.set_type_meta(meta);
    p.datum_.set_string(expected.data(), expected.size()); std::string wire = encode(p); int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_parameter(wire.data(), wire.size(), pos, destination, out));
    memset(&wire[0], 0xa5, wire.size());
  }
  EXPECT_EQ(expected.size(), out.datum_.len_); EXPECT_EQ(0, memcmp(expected.data(), out.datum_.ptr_, expected.size()));
}
TEST(DatumCodec, decoded_number_owns_contiguous_payload) {
  ObArenaAllocator destination; DatumParameter out; std::vector<uint32_t> expected;
  {
    ObArenaAllocator source_arena; number::ObNumber n;
    ASSERT_EQ(OB_SUCCESS, n.from("12345678901234567890.000000001", source_arena));
    DatumParameter p; SqlTypeMeta meta; meta.set_number(); p.set_type_meta(meta);
    ASSERT_EQ(OB_SUCCESS, p.alloc_datum_reserved_buff(meta, 65, source_arena)); p.datum_.set_number(n); p.set_precision(65); p.set_scale(30);
    expected.assign(n.get_digits(), n.get_digits() + n.get_length()); std::string wire = encode(p); int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_parameter(wire.data(), wire.size(), pos, destination, out)); memset(&wire[0], 0xa5, wire.size());
  }
  EXPECT_EQ(sizeof(number::ObNumber::Desc) + expected.size() * sizeof(uint32_t), out.datum_.len_);
  EXPECT_EQ(expected.size(), out.datum_.get_number_desc().len_);
  EXPECT_EQ(0, memcmp(expected.data(), out.datum_.get_number_digits(), expected.size() * sizeof(uint32_t)));
  EXPECT_EQ(65, out.get_precision()); EXPECT_EQ(30, out.get_scale());
}
TEST(DatumCodec, nested_heterogeneous_arrays_own_values_and_metadata) {
  ObArenaAllocator destination; DatumParameter out;
  {
    ObArenaAllocator source_arena; DatumParameter p = make_array(source_arena, 2); auto *outer = p.get_array<DatumParameterArray>(); ASSERT_NE(nullptr, outer);
    outer->data_[0] = integer(source_arena, 123); outer->data_[0].set_precision(19); outer->data_[1] = make_array(source_arena, 1);
    auto *inner = outer->data_[1].get_array<DatumParameterArray>(); ASSERT_NE(nullptr, inner);
    SqlTypeMeta meta; meta.set_varchar(); inner->data_[0].set_type_meta(meta); inner->data_[0].set_length(80); inner->data_[0].datum_.set_string("中文", 6);
    std::string wire = encode(p); int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_parameter(wire.data(), wire.size(), pos, destination, out)); memset(&wire[0], 0xa5, wire.size());
  }
  const auto *outer = out.get_array<DatumParameterArray>(); ASSERT_NE(nullptr, outer); ASSERT_EQ(2, outer->count_);
  EXPECT_EQ(123, outer->data_[0].datum_.get_int()); EXPECT_EQ(19, outer->data_[0].get_precision());
  const auto *inner = outer->data_[1].get_array<DatumParameterArray>(); ASSERT_NE(nullptr, inner); ASSERT_EQ(1, inner->count_);
  EXPECT_EQ(ObVarcharType, inner->data_[0].get_type()); EXPECT_EQ(80, inner->data_[0].get_length()); EXPECT_EQ(ObString("中文"), inner->data_[0].datum_.get_string());
}
TEST(DatumCodec, truncation_never_changes_output_or_position) {
  ObArenaAllocator arena; DatumParameter p = make_array(arena, 2); auto *a = p.get_array<DatumParameterArray>();
  a->data_[0] = integer(arena, INT64_MIN); SqlTypeMeta meta; meta.set_varchar(); a->data_[1].set_type_meta(meta); a->data_[1].datum_.set_string("abc", 3);
  const std::string wire = encode(p);
  for (int64_t size = 0; size < wire.size(); ++size) {
    SCOPED_TRACE(size); DatumParameter out = integer(arena, 99); const char *ptr = out.datum_.ptr_; int64_t pos = 0;
    EXPECT_NE(OB_SUCCESS, DatumParameterCodec::decode_parameter(wire.data(), size, pos, arena, out));
    EXPECT_EQ(0, pos); EXPECT_EQ(ptr, out.datum_.ptr_); EXPECT_EQ(99, out.datum_.get_int());
  }
}
TEST(DatumCodec, allocation_failure_is_atomic) {
  ObArenaAllocator arena; RejectAllocator reject; DatumParameter p = integer(arena, 42), out = integer(arena, 99);
  const char *ptr = out.datum_.ptr_; const std::string wire = encode(p); int64_t pos = 0;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, DatumParameterCodec::decode_parameter(wire.data(), wire.size(), pos, reject, out));
  EXPECT_EQ(1, reject.calls); EXPECT_EQ(0, pos); EXPECT_EQ(ptr, out.datum_.ptr_); EXPECT_EQ(99, out.datum_.get_int());
}
TEST(DatumCodec, bounded_writer_preserves_guards_and_position) {
  ObArenaAllocator arena; const DatumParameter p = integer(arena, INT64_MIN); const std::string wire = encode(p);
  for (int64_t size = 0; size < wire.size(); ++size) {
    std::vector<char> bytes(size + 8, 'x'); int64_t pos = 0;
    EXPECT_NE(OB_SUCCESS, DatumParameterCodec::encode_parameter(p, bytes.data() + 4, size, pos)); EXPECT_EQ(0, pos);
    EXPECT_EQ(std::string(4, 'x'), std::string(bytes.data(), 4)); EXPECT_EQ(std::string(4, 'x'), std::string(bytes.data() + size + 4, 4));
  }
}
TEST(DatumCodec, all_twelve_collation_bits_survive) {
  ObArenaAllocator arena; ObDatum d; d.set_string("x", 1);
  for (int id = 0; id < 4096; ++id) {
    SqlTypeMeta meta; meta.set_varchar(); meta.set_cs_level(((id >> 8) << 4) | CS_LEVEL_COERCIBLE); meta.set_cs_type(id & 255);
    char wire[32]; int64_t pos = 0, read = 0; ObDatum out; SqlTypeMeta decoded;
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::encode_value(d, meta, wire, sizeof(wire), pos));
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_value(wire, pos, read, arena, out, decoded)); EXPECT_EQ(0, memcmp(&meta, &decoded, sizeof(meta))) << id;
  }
}
TEST(DatumCodec, cyclic_array_has_bounded_recursion) {
  ObArenaAllocator arena; DatumParameter p = make_array(arena, 1); p.get_array<DatumParameterArray>()->data_[0] = p; int64_t size = 123;
  EXPECT_EQ(OB_SIZE_OVERFLOW, DatumParameterCodec::parameter_size(p, size)); EXPECT_EQ(123, size);
  char wire[4096]; int64_t pos = 0;
  EXPECT_EQ(OB_SIZE_OVERFLOW, DatumParameterCodec::encode_parameter(p, wire, sizeof(wire), pos)); EXPECT_EQ(0, pos);
}
namespace {
std::string hex(const void *data, int64_t length) {
  static const char digits[] = "0123456789abcdef";
  const auto *bytes = static_cast<const unsigned char *>(data); std::string out;
  for (int64_t i = 0; i < length; ++i) { out += digits[bytes[i] >> 4]; out += digits[bytes[i] & 15]; }
  return out;
}
std::string unhex(const std::string &hex) {
  std::string result;
  for (size_t i = 0; i < hex.size(); i += 2) { result += static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16)); }
  return result;
}
std::string summary(const DatumParameter &p, bool parameter) {
  if (!p.is_null()) {
    EXPECT_EQ(p.get_type_meta().is_outrow(), p.datum_.is_outrow());
    EXPECT_EQ(p.get_type_meta().has_lob_header(), p.datum_.has_lob_header());
  }
  std::string out = hex(&p.get_type_meta(), sizeof(SqlTypeMeta)) + ":";
  if (p.is_null()) { out += "NULL"; }
  else if (p.is_ext_sql_array()) {
    out += "["; const auto *a = p.get_array<DatumParameterArray>();
    for (int64_t i = 0; i < a->count_; ++i) { out += summary(a->data_[i], true) + ";"; }
    out += "]";
  } else { out += hex(p.datum_.ptr_, p.datum_.len_); }
  if (parameter) { out += "/" + std::to_string(p.get_accuracy().get_accuracy()) + "/" + std::to_string(p.get_result_flag()); }
  return out;
}
}
TEST(DatumCodec, captured_legacy_golden_all_types_and_boundaries) {
  std::ifstream input("unittest/oblib/data/datum_parameter_codec.golden"); ASSERT_TRUE(input.good());
  int cases = 0; std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') { continue; }
    std::vector<std::string> fields; std::istringstream row(line); std::string field;
    while (std::getline(row, field, '\t')) { fields.push_back(field); }
    ASSERT_EQ(5, fields.size()); SCOPED_TRACE(fields[0]); ++cases;
    ObArenaAllocator arena;
    for (int mode = 0; mode < 2; ++mode) {
      SCOPED_TRACE(mode); const std::string wire = unhex(fields[1 + mode]); DatumParameter output; int64_t pos = 0, size = -1;
      if (mode == 0) {
        SqlTypeMeta meta;
        ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_value(wire.data(), wire.size(), pos, arena, output.datum_, meta)); output.set_type_meta(meta);
        ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::value_size(output.datum_, output.get_type_meta(), size));
      } else {
        ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_parameter(wire.data(), wire.size(), pos, arena, output));
        ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::parameter_size(output, size));
      }
      EXPECT_EQ(wire.size(), pos); EXPECT_EQ(wire.size(), size); EXPECT_EQ(fields[3 + mode], summary(output, mode == 1));
      std::string encoded(wire.size(), '\0'); pos = 0;
      if (mode == 0) { ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::encode_value(output.datum_, output.get_type_meta(), &encoded[0], encoded.size(), pos)); }
      else { ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::encode_parameter(output, &encoded[0], encoded.size(), pos)); }
      EXPECT_EQ(wire, encoded); EXPECT_EQ(wire.size(), pos);
      // Check every prefix of every captured valid record, not just an inferred size formula.
      for (int64_t prefix = 0; prefix < wire.size(); ++prefix) {
        int64_t read = 0; DatumParameter unchanged = integer(arena, 99); const char *before = unchanged.datum_.ptr_;
        if (mode == 0) {
          SqlTypeMeta meta = unchanged.get_type_meta();
          EXPECT_NE(OB_SUCCESS, DatumParameterCodec::decode_value(wire.data(), prefix, read, arena, unchanged.datum_, meta));
          EXPECT_EQ(ObIntType, meta.get_type());
        } else { EXPECT_NE(OB_SUCCESS, DatumParameterCodec::decode_parameter(wire.data(), prefix, read, arena, unchanged)); }
        EXPECT_EQ(0, read); EXPECT_EQ(before, unchanged.datum_.ptr_); EXPECT_EQ(99, unchanged.datum_.get_int());
      }
    }
  }
  EXPECT_EQ(155, cases);
}
TEST(DatumCodec, malformed_lengths_fail_before_allocating) {
  ObArenaAllocator arena; RejectAllocator reject;
  for (int64_t length : {int64_t(-1), INT64_MAX, int64_t(1) << 29}) {
    char wire[128]; int64_t pos = 0; SqlTypeMeta meta; meta.set_varchar();
    ASSERT_EQ(OB_SUCCESS, meta.serialize(wire, sizeof(wire), pos));
    ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, sizeof(wire), pos, length));
    wire[pos++] = 0; int64_t read = 0; DatumParameter output = integer(arena, 99);
    EXPECT_EQ(OB_DESERIALIZE_ERROR, DatumParameterCodec::decode_parameter(wire, pos, read, reject, output));
    EXPECT_EQ(0, read); EXPECT_EQ(99, output.datum_.get_int());
  }
  EXPECT_EQ(0, reject.calls);
}
TEST(DatumCodec, impossible_array_count_and_body_length_are_rejected_before_allocating) {
  ObArenaAllocator arena; RejectAllocator reject;
  char body[128]; int64_t body_size = 0; ObDataType element;
  ASSERT_EQ(OB_SUCCESS, element.serialize(body, sizeof(body), body_size));
  ASSERT_EQ(OB_SUCCESS, serialization::encode(body, sizeof(body), body_size, INT64_MAX));
  for (int64_t length : {int64_t(-1), INT64_MAX, body_size}) {
    char wire[256]; int64_t pos = 0; SqlTypeMeta meta; meta.set_ext(); meta.set_extend_type(T_EXT_SQL_ARRAY);
    ASSERT_EQ(OB_SUCCESS, meta.serialize(wire, sizeof(wire), pos));
    ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, sizeof(wire), pos, int64_t(0)));
    ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, sizeof(wire), pos, length));
    memcpy(wire + pos, body, body_size); pos += body_size;
    int64_t read = 0; DatumParameter output = integer(arena, 99);
    EXPECT_EQ(OB_DESERIALIZE_ERROR, DatumParameterCodec::decode_parameter(wire, pos, read, reject, output));
    EXPECT_EQ(0, read); EXPECT_EQ(99, output.datum_.get_int());
  }
  EXPECT_EQ(0, reject.calls);
}
TEST(DatumCodec, extended_metadata_and_preexisting_unsupported_pl_pointer_boundary) {
  ObArenaAllocator arena; DatumParameter p; SqlTypeMeta meta; meta.set_ext(); meta.set_extend_type(7);
  meta.set_cs_level(0xf4); meta.set_cs_type(0xff); p.set_type_meta(meta);
  ASSERT_EQ(OB_SUCCESS, p.alloc_datum_reserved_buff(meta, -1, arena));
  p.datum_.set_extend(ObActionFlag::OP_NOP, 7, 0, meta.get_cs_level(), meta.get_cs_type());
  std::string wire = encode(p); int64_t read = 0; DatumParameter out;
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_parameter(wire.data(), wire.size(), read, arena, out));
  EXPECT_EQ(int64_t(ObActionFlag::OP_NOP), out.datum_.get_ext());
  EXPECT_EQ(0xf4, out.datum_.get_extend_collation_level()); EXPECT_EQ(0xff, out.datum_.get_extend_collation());
  p.datum_.set_extend(12345, 7); int64_t size = 123, pos = 0; char bytes[64];
  EXPECT_EQ(OB_NOT_SUPPORTED, DatumParameterCodec::parameter_size(p, size)); EXPECT_EQ(123, size);
  EXPECT_EQ(OB_NOT_SUPPORTED, DatumParameterCodec::encode_parameter(p, bytes, sizeof(bytes), pos)); EXPECT_EQ(0, pos);
}
TEST(DatumCodec, store_wire_matches_legacy_count_and_records_at_nonzero_position) {
  std::ifstream input("unittest/oblib/data/datum_parameter_codec.golden"); ASSERT_TRUE(input.good());
  std::vector<std::string> records; std::string line;
  while (std::getline(input, line)) {
    if (line.rfind("type-0\t", 0) == 0 || line.rfind("type-15\t", 0) == 0 || line.rfind("array-4\t", 0) == 0) {
      std::istringstream row(line); std::string name, value, parameter; std::getline(row, name, '\t'); std::getline(row, value, '\t'); std::getline(row, parameter, '\t'); records.push_back(unhex(parameter));
    }
  }
  ASSERT_EQ(3, records.size());
  // This is the legacy Ob2DArray sequence: vi64 count followed by captured parameter records.
  char count[16]; int64_t count_size = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::encode_vi64(count, sizeof(count), count_size, records.size()));
  std::string expected = "abc" + std::string(count, count_size);
  for (const auto &record : records) { expected += record; }
  const int64_t end = expected.size(); expected += "yz";
  ObArenaAllocator arena; DatumParamStore store{ObWrapperAllocator(arena)}; int64_t pos = 3;
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_store(expected.data(), expected.size(), pos, arena, store));
  EXPECT_EQ(end, pos); EXPECT_EQ(3, store.count()); int64_t size = 0;
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::store_size(store, size)); EXPECT_EQ(end - 3, size);
  std::string output(expected.size(), '?'); output.replace(0, 3, "abc"); output.replace(end, 2, "yz"); pos = 3;
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::encode_store(store, &output[0], output.size(), pos)); EXPECT_EQ(end, pos); EXPECT_EQ(expected, output);
  for (int64_t prefix = 3; prefix < end; ++prefix) {
    ASSERT_EQ(OB_SUCCESS, store.push_back(integer(arena, 99))); pos = 3;
    EXPECT_NE(OB_SUCCESS, DatumParameterCodec::decode_store(expected.data(), prefix, pos, arena, store));
    EXPECT_EQ(3, pos); EXPECT_TRUE(store.empty());
  }
}
TEST(DatumCodec, store_codec_crosses_container_blocks_and_owns_payloads) {
  ObArenaAllocator destination; DatumParamStore decoded{ObWrapperAllocator(destination)}; int64_t expected = 0;
  {
    ObArenaAllocator source_arena; DatumParamStore source{ObWrapperAllocator(source_arena)};
    expected = source.get_block_size() / sizeof(DatumParameter) + 17;
    for (int64_t i = 0; i < expected; ++i) { ASSERT_EQ(OB_SUCCESS, source.push_back(integer(source_arena, i * 7))); }
    int64_t size = 0; ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::store_size(source, size)); std::string wire(size, '\0'); int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::encode_store(source, &wire[0], wire.size(), pos)); ASSERT_EQ(size, pos); pos = 0;
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_store(wire.data(), wire.size(), pos, destination, decoded)); EXPECT_EQ(size, pos); memset(&wire[0], 0xa5, wire.size());
  }
  ASSERT_EQ(expected, decoded.count()); EXPECT_GT(decoded.mem_used(), decoded.get_block_size());
  for (int64_t i = 0; i < expected; ++i) { EXPECT_EQ(i * 7, decoded.at(i).datum_.get_int()); EXPECT_EQ(ObIntType, decoded.at(i).get_type()); }
}
TEST(DatumCodec, failed_store_decode_clears_partial_output_and_keeps_position) {
  ObArenaAllocator arena; DatumParamStore store{ObWrapperAllocator(arena)}; RejectAllocator reject;
  for (int64_t count : {int64_t(-1), INT64_MAX}) {
    ASSERT_EQ(OB_SUCCESS, store.push_back(integer(arena, 99))); char bytes[16]; int64_t end = 0, pos = 0;
    ASSERT_EQ(OB_SUCCESS, serialization::encode_vi64(bytes, sizeof(bytes), end, count));
    EXPECT_EQ(OB_DESERIALIZE_ERROR, DatumParameterCodec::decode_store(bytes, end, pos, reject, store)); EXPECT_EQ(0, pos); EXPECT_TRUE(store.empty());
  }
  EXPECT_EQ(0, reject.calls);
  const DatumParameter p = integer(arena, 42); const std::string parameter_wire = encode(p); std::string wire(1, '\x01'); wire += parameter_wire;
  int64_t pos = 0; EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, DatumParameterCodec::decode_store(wire.data(), wire.size(), pos, reject, store)); EXPECT_EQ(0, pos); EXPECT_TRUE(store.empty());
  DatumParamStore no_blocks{ObWrapperAllocator(reject)}; pos = 0;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, DatumParameterCodec::decode_store(wire.data(), wire.size(), pos, arena, no_blocks));
  EXPECT_EQ(0, pos); EXPECT_TRUE(no_blocks.empty());
  // Empty stores require no payload or descriptor allocation.
  const char empty[] = {0}; pos = 0;
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_store(empty, 1, pos, reject, store)); EXPECT_EQ(1, pos); EXPECT_TRUE(store.empty());
  int64_t size = 0; ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::store_size(store, size)); EXPECT_EQ(1, size);
}

TEST(DatumCodec, native_store_generic_serializer_uses_owned_native_values) {
  ObArenaAllocator arena; DatumParamStore source{ObWrapperAllocator(arena)};
  ASSERT_EQ(OB_SUCCESS, source.push_back(integer(arena, INT64_MIN)));
  DatumParameter values = make_array(arena, 1); values.get_array<DatumParameterArray>()->data_[0] = integer(arena, 42);
  ASSERT_EQ(OB_SUCCESS, source.push_back(values));
  const int64_t size = serialization::encoded_length(source); ASSERT_GT(size, 0);
  std::string bytes(size, '\0'); int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::encode(&bytes[0], bytes.size(), pos, source)); ASSERT_EQ(size, pos);
  ObArenaAllocator output_arena; DatumParamStore output{ObWrapperAllocator(output_arena)}; pos = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::decode(bytes.data(), bytes.size(), pos, output)); EXPECT_EQ(size, pos);
  memset(&bytes[0], 0xa5, bytes.size()); ASSERT_EQ(2, output.count()); EXPECT_EQ(INT64_MIN, output.at(0).datum_.get_int());
  const auto *array = output.at(1).get_array<DatumParameterArray>(); ASSERT_NE(nullptr, array); ASSERT_EQ(1, array->count_); EXPECT_EQ(42, array->data_[0].datum_.get_int());
}
TEST(DatumCodec, native_store_generic_decoder_failure_and_missing_allocator) {
  ObArenaAllocator arena; DatumParamStore source{ObWrapperAllocator(arena)}; ASSERT_EQ(OB_SUCCESS, source.push_back(integer(arena, 42)));
  std::string bytes(source.get_serialize_size(), '\0'); int64_t end = 0;
  ASSERT_EQ(OB_SUCCESS, source.serialize(&bytes[0], bytes.size(), end));
  DatumParamStore missing_allocator; int64_t pos = 0;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, missing_allocator.deserialize(bytes.data(), bytes.size(), pos)); EXPECT_EQ(0, pos); EXPECT_TRUE(missing_allocator.empty());
  DatumParamStore destination{ObWrapperAllocator(arena)}; ASSERT_EQ(OB_SUCCESS, destination.push_back(integer(arena, 99))); pos = 0;
  EXPECT_NE(OB_SUCCESS, destination.deserialize(bytes.data(), bytes.size() - 1, pos)); EXPECT_EQ(0, pos); EXPECT_TRUE(destination.empty());
}

TEST(DatumCodec, array_body_entry_points_keep_captured_old_wire_and_reject_all_prefixes)
{
  std::ifstream input("unittest/oblib/data/datum_parameter_codec.golden"); ASSERT_TRUE(input.good());
  std::string line; int arrays = 0;
  while (std::getline(input, line)) {
    if (line.rfind("array-", 0) != 0) { continue; }
    std::istringstream record(line); std::string label, encoded;
    std::getline(record, label, '\t'); std::getline(record, encoded, '\t');
    SCOPED_TRACE(label); std::string wire = unhex(encoded);
    int64_t cursor = 4, zero = -1, length = -1;
    ASSERT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), cursor, zero)); ASSERT_EQ(0, zero);
    ASSERT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), cursor, length)); ASSERT_EQ(wire.size() - cursor, length);
    const std::string body = wire.substr(cursor);
    ObArenaAllocator arena; DatumParameterArray *array = nullptr; int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_array(body.data(), body.size(), pos, arena, array));
    ASSERT_NE(nullptr, array); EXPECT_EQ(body.size(), pos); EXPECT_EQ(body.size(), array->get_serialize_size());
    std::string repeated(body.size(), '\0'); pos = 0;
    ASSERT_EQ(OB_SUCCESS, array->serialize(&repeated[0], repeated.size(), pos)); EXPECT_EQ(body, repeated);
    for (int64_t prefix = 0; prefix < body.size(); ++prefix) {
      ObArenaAllocator rejected; DatumParameterArray *output = array; pos = 0;
      EXPECT_NE(OB_SUCCESS, DatumParameterCodec::decode_array(body.data(), prefix, pos, rejected, output));
      EXPECT_EQ(0, pos); EXPECT_EQ(array, output);
    }
    RejectAllocator reject; DatumParameterArray *output = array; pos = 0;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, DatumParameterCodec::decode_array(body.data(), body.size(), pos, reject, output));
    EXPECT_EQ(0, pos); EXPECT_EQ(array, output); ++arrays;
  }
  EXPECT_EQ(4, arrays);
}

TEST(DatumCodec, clone_nested_arrays_owns_payload_and_preserves_runtime_metadata)
{
  ObArenaAllocator source_arena, output_arena;
  DatumParameter source = make_array(source_arena, 1), child = make_array(source_arena, 1);
  source.set_raw_text_info(17, 19); source.get_param_flag().extend_flag_ = 0x5a;
  auto *inner = child.get_array<DatumParameterArray>();
  char bytes[] = "owned bytes"; DatumParameter &text = inner->data_[0];
  SqlTypeMeta type; type.set_varchar(); type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  text.set_type_meta(type); text.set_param_meta(type); text.datum_.set_string(bytes, sizeof(bytes)-1);
  text.set_length(37); text.set_result_flags(0x1234); text.set_raw_text_info(23, 29); text.get_param_flag().flag_ = 0xa5;
  source.get_array<DatumParameterArray>()->data_[0] = child;
  DatumParameter output;
  ASSERT_EQ(OB_SUCCESS, DatumParameterArray::clone_parameter(source, output, output_arena));
  EXPECT_NE(source.get_array<DatumParameterArray>(), output.get_array<DatumParameterArray>());
  auto *owned = output.get_array<DatumParameterArray>()->data_[0].get_array<DatumParameterArray>();
  ASSERT_NE(inner, owned); EXPECT_NE(bytes, owned->data_[0].datum_.ptr_);
  memset(bytes, 'x', sizeof(bytes)); source_arena.reset();
  EXPECT_EQ(ObString("owned bytes"), owned->data_[0].datum_.get_string());
  EXPECT_EQ(type, owned->data_[0].get_type_meta()); EXPECT_EQ(type, owned->data_[0].get_param_meta());
  EXPECT_EQ(37, owned->data_[0].get_length()); EXPECT_EQ(0x1234, owned->data_[0].get_result_flag());
  EXPECT_EQ(23, owned->data_[0].get_raw_text_pos()); EXPECT_EQ(29, owned->data_[0].get_raw_text_len());
  EXPECT_EQ(0xa5, owned->data_[0].get_param_flag().flag_); EXPECT_EQ(17, output.get_raw_text_pos());
  EXPECT_EQ(0x5a, output.get_param_flag().extend_flag_);
}

TEST(DatumCodec, array_clone_bounds_cycles_and_allocation_errors_preserve_output)
{
  ObArenaAllocator arena; DatumParameter source = make_array(arena, 1), output = integer(arena, 99);
  source.get_array<DatumParameterArray>()->data_[0] = source;
  EXPECT_EQ(OB_SIZE_OVERFLOW, DatumParameterArray::clone_parameter(source, output, arena)); EXPECT_EQ(99, output.datum_.get_int());
  RejectAllocator reject;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, DatumParameterArray::clone_parameter(source, output, reject)); EXPECT_EQ(99, output.datum_.get_int());
  DatumParameterArray invalid; invalid.count_ = -1; int64_t size = 123, pos = 0; char bytes[64];
  EXPECT_EQ(OB_INVALID_ARGUMENT, DatumParameterCodec::array_size(invalid, size)); EXPECT_EQ(123, size);
  EXPECT_EQ(OB_INVALID_ARGUMENT, invalid.serialize(bytes, sizeof(bytes), pos)); EXPECT_EQ(0, pos);
}


namespace {
class ArrayBoundaryAllocator : public ObIAllocator
{
public:
  explicit ArrayBoundaryAllocator(int64_t fail_on_call = -1)
      : fail_on_call_(fail_on_call), calls_(0) {}
  ~ArrayBoundaryAllocator()
  {
    EXPECT_TRUE(owned_.empty());
    for (void *memory : owned_) { std::free(memory); }
  }
  void *alloc(int64_t size) override
  {
    const int64_t call = calls_++;
    void *memory = call == fail_on_call_ ? nullptr : std::malloc(std::max<int64_t>(size, 1));
    if (memory != nullptr) { owned_.push_back(memory); }
    return memory;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *memory) override
  {
    if (memory != nullptr) {
      auto found = std::find(owned_.begin(), owned_.end(), memory);
      EXPECT_NE(owned_.end(), found);
      if (found != owned_.end()) { owned_.erase(found); std::free(memory); }
    }
  }
  bool empty() const { return owned_.empty(); }
  int64_t calls() const { return calls_; }
private:
  int64_t fail_on_call_;
  int64_t calls_;
  std::vector<void *> owned_;
};
DatumParameter array_view(const ObDatum &address)
{
  DatumParameter value;
  SqlTypeMeta meta; meta.set_ext(); meta.set_extend_type(T_EXT_SQL_ARRAY);
  value.set_type_meta(meta); value.set_datum(address);
  return value;
}
std::string encode_value(const DatumParameter &value)
{
  int64_t size = -1;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::value_size(value.datum_, value.get_type_meta(), size));
  if (size < 0) { return {}; }
  std::string bytes(size, '\0'); int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::encode_value(value.datum_, value.get_type_meta(),
                                                        &bytes[0], bytes.size(), pos));
  EXPECT_EQ(size, pos);
  return bytes;
}
}

TEST(DatumCodec, deferred_old_array_body_becomes_owned_native_value_and_preserves_source_size)
{
  // These independent pre-migration fixtures are the old value decoder's
  // exact deferred body slices, not wire generated by the new decoder.
  std::ifstream input("unittest/oblib/data/datum_parameter_codec.golden"); ASSERT_TRUE(input.good());
  std::string line; int cases = 0;
  while (std::getline(input, line)) {
    if (line.rfind("array-", 0) != 0) { continue; }
    std::istringstream record(line); std::string label, encoded;
    std::getline(record, label, '\t'); std::getline(record, encoded, '\t');
    SCOPED_TRACE(label); std::string wire = unhex(encoded); const std::string expected_wire = wire;
    int64_t cursor = 4, zero = -1, length = -1;
    ASSERT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), cursor, zero));
    ASSERT_EQ(0, zero);
    ASSERT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), cursor, length));
    ASSERT_EQ(wire.size() - cursor, length);
    std::string body = wire.substr(cursor);
    ArrayBoundaryAllocator decoded_owner, cloned_owner;
    ObDatum decoded;
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_deferred_array(
        body.data(), body.size(), decoded_owner, decoded));
    DatumParameter owned = array_view(decoded);
    EXPECT_EQ(length, owned.get_val_len());
    EXPECT_EQ(length, owned.value_view().get_val_len());
    EXPECT_EQ(expected_wire, encode_value(owned));
    DatumParameter clone;
    ASSERT_EQ(OB_SUCCESS, DatumParameterArray::clone_parameter(owned, clone, cloned_owner));
    EXPECT_EQ(length, clone.get_val_len());
    EXPECT_NE(owned.get_array<DatumParameterArray>(), clone.get_array<DatumParameterArray>());
    for (int64_t prefix = 0; prefix < body.size(); ++prefix) {
      SCOPED_TRACE(prefix); ArrayBoundaryAllocator rejected; ObDatum output = owned.datum_;
      EXPECT_NE(OB_SUCCESS, DatumParameterCodec::decode_deferred_array(body.data(), prefix, rejected, output));
      EXPECT_EQ(owned.datum_.ptr_, output.ptr_); EXPECT_TRUE(rejected.empty());
    }
    for (int64_t fail = 0; fail < decoded_owner.calls(); ++fail) {
      SCOPED_TRACE(fail); ArrayBoundaryAllocator rejected(fail); ObDatum output = owned.datum_;
      EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
                DatumParameterCodec::decode_deferred_array(body.data(), body.size(), rejected, output));
      EXPECT_EQ(owned.datum_.ptr_, output.ptr_); EXPECT_TRUE(rejected.empty());
    }
    std::fill(body.begin(), body.end(), '\x5a');
    std::fill(wire.begin(), wire.end(), '\x5a');
    EXPECT_EQ(expected_wire, encode_value(owned));
    DatumParameterArray::release_owned(owned, decoded_owner);
    EXPECT_TRUE(decoded_owner.empty());
    EXPECT_EQ(expected_wire, encode_value(clone));
    EXPECT_EQ(length, clone.get_val_len());
    DatumParameterArray::release_owned(clone, cloned_owner);
    EXPECT_TRUE(cloned_owner.empty());
    ++cases;
  }
  EXPECT_EQ(4, cases);
}

TEST(DatumCodec, deferred_array_decode_rolls_back_every_prefix_and_partial_allocation)
{
  ObArenaAllocator source;
  DatumParameter original = make_array(source, 40);
  auto *array = original.get_array<DatumParameterArray>(); ASSERT_NE(nullptr, array);
  for (int64_t i = 0; i < array->count_; ++i) { array->data_[i] = integer(source, i * 12345); }
  int64_t size = 0;
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::array_size(*array, size));
  std::string body(size, '\0'); int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::encode_array(*array, &body[0], body.size(), pos));
  DatumParameter sentinel = integer(source, 99);
  for (int64_t prefix = 0; prefix < body.size(); ++prefix) {
    SCOPED_TRACE(prefix);
    ArrayBoundaryAllocator owner;
    ObDatum output = sentinel.datum_;
    EXPECT_NE(OB_SUCCESS, DatumParameterCodec::decode_deferred_array(body.data(), prefix, owner, output));
    EXPECT_EQ(sentinel.datum_.ptr_, output.ptr_); EXPECT_EQ(99, output.get_int());
    EXPECT_TRUE(owner.empty());
  }
  int64_t allocations = 0;
  {
    ArrayBoundaryAllocator owner; ObDatum output;
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_deferred_array(body.data(), body.size(), owner, output));
    allocations = owner.calls(); EXPECT_GT(allocations, 16);
    DatumParameter owned = array_view(output);
    DatumParameterArray::release_owned(owned, owner);
    EXPECT_TRUE(owner.empty());
  }
  for (int64_t fail = 0; fail < allocations; ++fail) {
    SCOPED_TRACE(fail);
    ArrayBoundaryAllocator owner(fail); ObDatum output = sentinel.datum_;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
              DatumParameterCodec::decode_deferred_array(body.data(), body.size(), owner, output));
    EXPECT_EQ(sentinel.datum_.ptr_, output.ptr_); EXPECT_EQ(99, output.get_int());
    EXPECT_TRUE(owner.empty());
  }
  {
    ArrayBoundaryAllocator owner; ObDatum output = sentinel.datum_;
    std::string trailing = body + "unused";
    EXPECT_EQ(OB_DESERIALIZE_ERROR,
              DatumParameterCodec::decode_deferred_array(trailing.data(), trailing.size(), owner, output));
    EXPECT_EQ(sentinel.datum_.ptr_, output.ptr_); EXPECT_EQ(99, output.get_int());
    EXPECT_TRUE(owner.empty());
    EXPECT_EQ(OB_INVALID_ARGUMENT,
              DatumParameterCodec::decode_deferred_array(nullptr, body.size(), owner, output));
    EXPECT_EQ(OB_INVALID_ARGUMENT,
              DatumParameterCodec::decode_deferred_array(body.data(), INT64_MAX, owner, output));
    EXPECT_TRUE(owner.empty());
  }
}


namespace {
std::string encode_unis_regression_value(const ObDatum &value, const SqlTypeMeta &meta)
{
  int64_t size = 0, pos = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::value_size(value, meta, size));
  if (size <= 0) { return {}; }
  std::string encoded(size, '\0');
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::encode_value(value, meta, &encoded[0], size, pos));
  EXPECT_EQ(size, pos);
  return hex(encoded.data(), encoded.size());
}
}

TEST(DatumCodec, unis_omitted_metadata_fields_reuse_existing_value_metadata)
{
  // Actual reused old ValueItemExpr/ParamValuePair/node captures preserve
  // numeric coercibility and binary collation when these fields are omitted.
  for (const char *encoded : {"05", "0505", "05053f", "05053fff"}) {
    SCOPED_TRACE(encoded);
    ObArenaAllocator owner;
    DatumParameter initial = integer(owner, 999);
    ObDatum value = initial.datum_;
    SqlTypeMeta meta = initial.get_type_meta();
    const std::string wire = unhex(encoded);
    int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_unis_value(
        wire.data(), wire.size(), pos, owner, value, meta));
    EXPECT_EQ(wire.size(), pos);
    EXPECT_EQ(0, value.get_int());
    EXPECT_EQ("05053fff00", encode_unis_regression_value(value, meta));
  }
}

TEST(DatumCodec, unis_present_metadata_fields_override_reused_value_metadata)
{
  // Complete NULL and VARCHAR frames come from the actual old producer and
  // reused-record decoder; each must replace the previous Int metadata.
  for (const char *encoded : {"00063f02", "16022eff016200", "05053fff03"}) {
    SCOPED_TRACE(encoded);
    ObArenaAllocator owner;
    DatumParameter initial = integer(owner, 999);
    ObDatum value = initial.datum_;
    SqlTypeMeta meta = initial.get_type_meta();
    const std::string wire = unhex(encoded);
    int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_unis_value(
        wire.data(), wire.size(), pos, owner, value, meta));
    EXPECT_EQ(wire.size(), pos);
    EXPECT_EQ(encoded, encode_unis_regression_value(value, meta));
  }
}

TEST(DatumCodec, unis_metadata_reuse_does_not_change_strict_or_schema_decoding)
{
  ObArenaAllocator owner;
  for (const char *encoded : {"05", "0505"}) {
    const std::string wire = unhex(encoded);
    DatumParameter initial = integer(owner, 999);
    ObDatum value = initial.datum_;
    SqlTypeMeta meta = initial.get_type_meta();
    int64_t pos = 0;
    EXPECT_NE(OB_SUCCESS, DatumParameterCodec::decode_value(
        wire.data(), wire.size(), pos, owner, value, meta));
    EXPECT_EQ(0, pos);
    EXPECT_EQ(initial.datum_.ptr_, value.ptr_);
    EXPECT_EQ(999, value.get_int());
    EXPECT_EQ("05053fffe707", encode_unis_regression_value(value, meta));
  }
  // Preserve the existing schema decoder's independent default metadata seed.
  const std::string wire = unhex("05");
  DatumParameter initial = integer(owner, 999);
  ObDatum value = initial.datum_;
  SqlTypeMeta meta = initial.get_type_meta();
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_schema_value(
      wire.data(), wire.size(), pos, owner, value, meta));
  EXPECT_EQ(1, pos);
  EXPECT_EQ("050700ff00", encode_unis_regression_value(value, meta));
}

int main(int argc, char **argv) { ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS(); }
