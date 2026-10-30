/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "query/vector/ob_vector_query_result.h"
#include "query/vector/ob_vector_index_util.h"
#include "common/datum/datum_parameter_codec.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::sql;
namespace {
constexpr int64_t COLUMN_COUNT = 8;
constexpr int64_t ROW_COUNT = 2;
constexpr int64_t RESULT_COLUMNS = COLUMN_COUNT + 2;
std::vector<std::string> split(const std::string &line) {
  std::vector<std::string> fields; std::istringstream input(line); std::string field;
  while (std::getline(input, field, '\t')) { fields.push_back(field); }
  return fields;
}
std::string unhex(const std::string &text) {
  std::string result;
  if (text != "-") { for (size_t i = 0; i < text.size(); i += 2) { result.push_back(std::stoi(text.substr(i, 2), nullptr, 16)); } }
  return result;
}
std::string hex(const char *ptr, int64_t size) {
  static const char *digits = "0123456789abcdef"; std::string result;
  for (int64_t i = 0; i < size; ++i) { const uint8_t c = ptr[i]; result += digits[c >> 4]; result += digits[c & 15]; }
  return result.empty() ? "-" : result;
}
std::string value_wire(const DatumValue &value) {
  int64_t size = 0, pos = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::value_size(value.datum_, value.meta_, size));
  std::string bytes(size + 64, '\0');
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::encode_value(value.datum_, value.meta_, &bytes[0], bytes.size(), pos));
  return hex(bytes.data(), pos);
}
int clone_value(const DatumValue &source, DatumValue &destination, ObIAllocator &owner) {
  DatumValue copy;
  const int ret = copy.datum_.deep_copy(source.datum_, owner);
  if (ret == OB_SUCCESS) { copy.meta_ = source.meta_; destination = copy; }
  return ret;
}
struct OldFixture {
  std::array<std::array<DatumValue, COLUMN_COUNT>, ROW_COUNT> values;
  std::array<std::array<std::string, COLUMN_COUNT>, ROW_COUNT> value_wires;
  std::array<std::string, ROW_COUNT> compact;
  std::array<std::array<std::string, RESULT_COLUMNS>, ROW_COUNT> single;
  std::array<std::array<std::string, RESULT_COLUMNS>, ROW_COUNT> batch;
  int64_t wire_size = 0;
  bool load(ObIAllocator &owner) {
    std::ifstream input("unittest/sql/data/native_vector_values.golden");
    if (!input.good()) { return false; }
    int values_seen = 0, wires_seen = 0, results_seen = 0; std::string line;
    while (std::getline(input, line)) {
      if (line.empty() || line[0] == '#') { continue; }
      const auto f = split(line);
      if (f.at(0) == "V") {
        const int row = std::stoi(f.at(1)), col = std::stoi(f.at(2));
        if (row < 0 || row >= ROW_COUNT || col < 0 || col >= COLUMN_COUNT) { return false; }
        const auto bytes = unhex(f.at(3)); int64_t pos = 0;
        DatumValue &value = values[row][col];
        if (DatumParameterCodec::decode_value(bytes.data(), bytes.size(), pos, owner, value.datum_, value.meta_) != OB_SUCCESS || pos != bytes.size()) { return false; }
        value_wires[row][col] = f.at(3); ++values_seen;
      } else if (f.at(0) == "W") {
        const int row = std::stoi(f.at(1)); const int64_t size = std::stoll(f.at(2));
        if (row < 0 || row >= ROW_COUNT || (wire_size != 0 && size != wire_size)) { return false; }
        compact[row] = unhex(f.at(3)); wire_size = size;
        if (compact[row].size() != wire_size) { return false; }
        ++wires_seen;
      } else if (f.at(0) == "R") {
        const int row = std::stoi(f.at(2)), col = std::stoi(f.at(3));
        if (row < 0 || row >= ROW_COUNT || col < 0 || col >= RESULT_COLUMNS) { return false; }
        if (f.at(1) == "single") { single[row][col] = f.at(4); }
        else if (f.at(1) == "batch") { batch[row][col] = f.at(4); }
        else { return false; }
        ++results_seen;
      } else { return false; }
    }
    return values_seen == 16 && wires_seen == 2 && results_seen == 40;
  }
  void init_expressions(ObExpr (&storage)[COLUMN_COUNT + 1], ExprFixedArray &exprs) const {
    ASSERT_EQ(OB_SUCCESS, exprs.init(COLUMN_COUNT + 1));
    storage[0].obj_meta_.set_int(); ASSERT_EQ(OB_SUCCESS, exprs.push_back(&storage[0]));
    for (int64_t i = 0; i < COLUMN_COUNT; ++i) {
      storage[i + 1].obj_meta_ = values[0][i].meta_;
      ASSERT_EQ(OB_SUCCESS, exprs.push_back(&storage[i + 1]));
    }
  }
  void init_extra_info(ObIAllocator &owner, ObVecExtraInfoPtr &extra) {
    ASSERT_EQ(OB_SUCCESS, extra.init(&owner, wire_size, ROW_COUNT));
    for (int64_t row = 0; row < ROW_COUNT; ++row) { ASSERT_EQ(OB_SUCCESS, extra.set_no_copy(row, compact[row].data())); }
  }
};
class RejectAllocator final : public ObIAllocator {
public:
  explicit RejectAllocator(int64_t allowed = 0) : allowed_(allowed) {}
  void *alloc(int64_t size) override { return calls_++ < allowed_ ? arena_.alloc(size) : nullptr; }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *ptr) override { arena_.free(ptr); }
private:
  int64_t allowed_; int64_t calls_ = 0; ObArenaAllocator arena_;
};
}

TEST(NativeVectorValues, immutable_old_compact_wire_and_reordered_native_decode) {
  ObArenaAllocator owner; OldFixture fixture; ASSERT_TRUE(fixture.load(owner));
  for (int64_t row = 0; row < ROW_COUNT; ++row) {
    ObSEArray<ObVecExtraInfoObj, COLUMN_COUNT> items;
    for (int64_t col = 0; col < COLUMN_COUNT; ++col) {
      ObVecExtraInfoObj item;
      ASSERT_EQ(OB_SUCCESS, item.from_value(fixture.values[row][col]));
      ASSERT_EQ(OB_SUCCESS, items.push_back(item));
    }
    EXPECT_EQ(fixture.wire_size, ObVecExtraInfo::get_encode_size(items));
    char *encoded = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObVecExtraInfo::extra_infos_to_buf(owner, &items.at(0), COLUMN_COUNT, fixture.wire_size, 1, encoded));
    EXPECT_EQ(fixture.compact[row], std::string(encoded, fixture.wire_size));
    DatumValue decoded[COLUMN_COUNT]; ObSEArray<int64_t, COLUMN_COUNT> reverse;
    for (int64_t col = 0; col < COLUMN_COUNT; ++col) {
      decoded[COLUMN_COUNT - col - 1].meta_ = fixture.values[row][col].meta_;
      ASSERT_EQ(OB_SUCCESS, reverse.push_back(COLUMN_COUNT - col - 1));
    }
    ASSERT_EQ(OB_SUCCESS, ObVecExtraInfo::extra_buf_to_values(encoded, fixture.wire_size, COLUMN_COUNT, decoded, &reverse));
    for (int64_t col = 0; col < COLUMN_COUNT; ++col) {
      DatumValue aligned; ASSERT_EQ(OB_SUCCESS, clone_value(decoded[COLUMN_COUNT - col - 1], aligned, owner));
      EXPECT_EQ(fixture.value_wires[row][col], value_wire(aligned));
    }
  }
}

TEST(NativeVectorValues, compact_decode_rejects_truncation_and_invalid_mapping) {
  ObArenaAllocator owner; OldFixture fixture; ASSERT_TRUE(fixture.load(owner));
  DatumValue decoded[COLUMN_COUNT];
  for (int64_t col = 0; col < COLUMN_COUNT; ++col) { decoded[col].meta_ = fixture.values[0][col].meta_; }
  EXPECT_EQ(OB_INVALID_DATA, ObVecExtraInfo::extra_buf_to_values(fixture.compact[0].data(), fixture.wire_size - 1, COLUMN_COUNT, decoded));
  ASSERT_EQ(OB_SUCCESS, ObVecExtraInfo::extra_buf_to_values(fixture.compact[0].data(), fixture.wire_size, COLUMN_COUNT, decoded));
  const int64_t string_offset = decoded[6].datum_.ptr_ - fixture.compact[0].data();
  ASSERT_GE(string_offset, sizeof(int32_t));
  int32_t invalid_length = -1; MEMCPY(&fixture.compact[0][string_offset - sizeof(int32_t)], &invalid_length, sizeof(invalid_length));
  EXPECT_EQ(OB_INVALID_DATA, ObVecExtraInfo::extra_buf_to_values(fixture.compact[0].data(), fixture.wire_size, COLUMN_COUNT, decoded));
  ObSEArray<int64_t, COLUMN_COUNT> invalid_mapping;
  for (int64_t col = 0; col < COLUMN_COUNT; ++col) { ASSERT_EQ(OB_SUCCESS, invalid_mapping.push_back(col == 0 ? -1 : col)); }
  EXPECT_EQ(OB_ERR_UNEXPECTED, ObVecExtraInfo::extra_buf_to_values(fixture.compact[1].data(), fixture.wire_size, COLUMN_COUNT, decoded, &invalid_mapping));
}

TEST(NativeVectorValues, extra_info_producer_borrows_or_owns_and_reports_oom) {
  ObArenaAllocator source_owner, retained_owner; OldFixture fixture; ASSERT_TRUE(fixture.load(source_owner));
  for (const int col : {2, 7}) {
    const DatumValue &value = fixture.values[0][col]; ObVecExtraInfoObj borrowed, owned;
    ASSERT_EQ(OB_SUCCESS, borrowed.from_value(value)); ASSERT_EQ(OB_SUCCESS, owned.from_value(value, &retained_owner));
    EXPECT_EQ(value.datum_.ptr_, borrowed.ptr_); EXPECT_NE(value.datum_.ptr_, owned.ptr_);
    const std::string original(value.datum_.ptr_, value.datum_.len_);
    MEMSET(const_cast<char *>(value.datum_.ptr_), 0x37, value.datum_.len_);
    EXPECT_NE(original, std::string(borrowed.ptr_, borrowed.len_)); EXPECT_EQ(original, std::string(owned.ptr_, owned.len_));
    RejectAllocator reject; ObVecExtraInfoObj failed;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, failed.from_value(value, &reject));
  }
  DatumValue null_value; ObVecExtraInfoObj rejected;
  EXPECT_EQ(OB_INVALID_ARGUMENT, rejected.from_value(null_value));
}

TEST(NativeVectorValues, sql_facing_single_rows_match_old_snapshots_and_owned_values_survive_advance) {
  ObArenaAllocator owner, retained_owner; OldFixture fixture; ASSERT_TRUE(fixture.load(owner));
  ObExpr expression_storage[COLUMN_COUNT + 1]; ExprFixedArray expressions(owner); fixture.init_expressions(expression_storage, expressions);
  ObVecExtraInfoPtr extra; fixture.init_extra_info(owner, extra);
  int64_t vids[ROW_COUNT] = {101, 202}; float distances[ROW_COUNT] = {1.25f, -4.5f};
  ObVectorQueryVidIterator iter(COLUMN_COUNT, fixture.wire_size, 0, nullptr);
  ASSERT_EQ(OB_SUCCESS, iter.init(ROW_COUNT, vids, distances, extra, &owner));
  DatumValue retained[RESULT_COLUMNS];
  for (int64_t row_index = 0; row_index < ROW_COUNT; ++row_index) {
    ObVectorResultRow *row = nullptr; ASSERT_EQ(OB_SUCCESS, iter.get_next_row(row, expressions));
    ASSERT_NE(nullptr, row); ASSERT_EQ(RESULT_COLUMNS, row->get_count());
    vids[row_index] = -900; distances[row_index] = 900.0f;
    DatumValue borrowed[COLUMN_COUNT];
    for (int64_t col = 0; col < COLUMN_COUNT; ++col) { borrowed[col].meta_ = fixture.values[row_index][col].meta_; }
    ASSERT_EQ(OB_SUCCESS, ObVecExtraInfo::extra_buf_to_values(fixture.compact[row_index].data(), fixture.wire_size, COLUMN_COUNT, borrowed));
    *const_cast<char *>(borrowed[0].datum_.ptr_) ^= 1;
    for (int64_t col = 0; col < RESULT_COLUMNS; ++col) {
      EXPECT_EQ(fixture.single[row_index][col], value_wire(row->get_cell(col)));
      if (col < 8) { EXPECT_EQ(0, reinterpret_cast<uintptr_t>(row->get_cell(col).datum_.ptr_) % 8); }
      if (row_index == 0) { ASSERT_EQ(OB_SUCCESS, clone_value(row->get_cell(col), retained[col], retained_owner)); }
    }
    // Large strings keep the compact buffer's borrow contract.
    const char *long_ptr = row->get_cell(9).datum_.ptr_;
    EXPECT_GE(long_ptr, fixture.compact[row_index].data());
    EXPECT_LT(long_ptr, fixture.compact[row_index].data() + fixture.wire_size);
    std::fill(fixture.compact[row_index].begin(), fixture.compact[row_index].end(), '\x5f');
  }
  for (int64_t col = 0; col < RESULT_COLUMNS; ++col) { EXPECT_EQ(fixture.single[0][col], value_wire(retained[col])); }
  ObVectorResultRow *row = nullptr; EXPECT_EQ(OB_ITER_END, iter.get_next_row(row, expressions));
}

TEST(NativeVectorValues, sql_facing_batch_rows_match_old_fixed_snapshots_and_init_reports_oom) {
  ObArenaAllocator owner; OldFixture fixture; ASSERT_TRUE(fixture.load(owner));
  ObExpr expression_storage[COLUMN_COUNT + 1]; ExprFixedArray expressions(owner); fixture.init_expressions(expression_storage, expressions);
  ObVecExtraInfoPtr extra; fixture.init_extra_info(owner, extra);
  int64_t vids[ROW_COUNT] = {101, 202}; float distances[ROW_COUNT] = {1.25f, -4.5f};
  ObVectorQueryVidIterator iter(COLUMN_COUNT, fixture.wire_size, 0, nullptr); iter.set_batch_size(ROW_COUNT);
  ASSERT_EQ(OB_SUCCESS, iter.init(ROW_COUNT, vids, distances, extra, &owner));
  ObVectorResultRow *row = nullptr; int64_t size = 0; ASSERT_EQ(OB_SUCCESS, iter.get_next_rows(row, size, expressions));
  ASSERT_NE(nullptr, row); ASSERT_EQ(ROW_COUNT, size); ASSERT_EQ(ROW_COUNT * RESULT_COLUMNS, row->get_count());
  vids[0] = vids[1] = -901; distances[0] = distances[1] = 901.0f;
  for (int64_t row_index = 0; row_index < ROW_COUNT; ++row_index) {
    DatumValue borrowed[COLUMN_COUNT];
    for (int64_t col = 0; col < COLUMN_COUNT; ++col) { borrowed[col].meta_ = fixture.values[row_index][col].meta_; }
    ASSERT_EQ(OB_SUCCESS, ObVecExtraInfo::extra_buf_to_values(fixture.compact[row_index].data(), fixture.wire_size, COLUMN_COUNT, borrowed));
    *const_cast<char *>(borrowed[0].datum_.ptr_) ^= 1;
    for (int64_t col = 0; col < RESULT_COLUMNS; ++col) {
      DatumValue actual = row->get_cell(row_index * RESULT_COLUMNS + col);
      if (col < 2) {
        // The old batch carrier skipped construction. Two old-library runs
        // with different allocator fills prove only these non-Decimal scale
        // bytes are uninitialized. Compare every other metadata/payload byte.
        const auto encoded = unhex(fixture.batch[row_index][col]);
        DatumValue oracle; int64_t pos = 0;
        ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_value(encoded.data(), encoded.size(), pos, owner, oracle.datum_, oracle.meta_));
        ASSERT_EQ(encoded.size(), pos);
        ASSERT_EQ(col == 0 ? ObFloatType : ObIntType, actual.get_type());
        ASSERT_FALSE(actual.meta_.is_decimal_int());
        ASSERT_FALSE(oracle.meta_.is_decimal_int());
        actual.meta_.set_scale(oracle.meta_.get_scale());
      }
      EXPECT_EQ(fixture.batch[row_index][col], value_wire(actual));
    }
  }
  for (int64_t allowed = 0; allowed < 4; ++allowed) {
    RejectAllocator reject(allowed); ObVectorQueryVidIterator failed(COLUMN_COUNT, fixture.wire_size, 0, nullptr);
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, failed.init(ROW_COUNT, &reject)); EXPECT_EQ(OB_NOT_INIT, failed.reset_obj());
  }
}
