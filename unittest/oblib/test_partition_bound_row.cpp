/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "common/datum/partition_bound_row.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_value_flat.h"
#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Native partition bounds must not include a legacy value definition"
#endif
#include "lib/allocator/ob_allocator.h"
#include <gtest/gtest.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace oceanbase::common;
namespace {
class RejectBoundAllocator final : public ObIAllocator {
public:
  void *alloc(int64_t) override { return nullptr; }
  void *alloc(int64_t, const ObMemAttr &) override { return nullptr; }
  void free(void *) override {}
};
class CountingBoundAllocator final : public ObIAllocator {
public:
  void *alloc(int64_t size) override
  {
    ++allocations_;
    allocation_ = arena_.alloc(size);
    return allocation_;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *memory) override
  {
    ++frees_;
    EXPECT_EQ(allocation_, memory);
    arena_.free(memory);
  }
  int64_t allocations_ = 0;
  int64_t frees_ = 0;
  void *allocation_ = nullptr;
private:
  ObArenaAllocator arena_;
};
}
TEST(PartitionBoundRow, owns_payload_and_projection_after_source_reset)
{
  ObArenaAllocator source, destination;
  DatumValue cells[2];
  ASSERT_EQ(OB_SUCCESS, cells[0].set_varchar(ObString("partition"), source));
  cells[0].set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  ASSERT_EQ(OB_SUCCESS, cells[1].set_int(42, source));
  int32_t projection[] = {1, 0};
  PartitionBoundRow row(cells, 2), owned;
  row.projector_ = projection; row.projector_size_ = 2;
  ASSERT_EQ(OB_SUCCESS, row.clone(owned, destination));
  source.reset();
  projection[0] = 0;
  cells[0].reset(); cells[1].reset();
  ASSERT_EQ(42, owned.get_cell(0).get_int());
  ASSERT_EQ(ObString("partition"), owned.get_cell(1).get_string());
  ASSERT_EQ(CS_TYPE_UTF8MB4_GENERAL_CI, owned.get_cell(1).get_collation_type());
  owned.destroy(destination);
  ASSERT_EQ(0, owned.count());
}
TEST(PartitionBoundRow, nested_array_is_owned_recursively)
{
  ObArenaAllocator source, destination;
  DatumParameterArray *array = DatumParameterArray::alloc(source, 1);
  ASSERT_NE(nullptr, array);
  DatumValue element;
  ASSERT_EQ(OB_SUCCESS, element.set_varchar(ObString("leaf"), source));
  element.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  array->data_[0].datum_ = element.datum_;
  array->data_[0].set_type_meta(element.meta_);
  DatumValue value;
  ASSERT_EQ(OB_SUCCESS, value.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, source));
  PartitionBoundRow row(&value, 1), owned;
  ASSERT_EQ(OB_SUCCESS, row.clone(owned, destination));
  source.reset();
  const auto *copy = reinterpret_cast<const DatumParameterArray *>(owned.get_cell(0).get_ext());
  ASSERT_NE(nullptr, copy);
  ASSERT_EQ(1, copy->count_);
  ASSERT_EQ(ObString("leaf"), copy->data_[0].get_string());
  owned.destroy(destination);
}
TEST(PartitionBoundRow, failures_do_not_publish_output_or_cursor)
{
  ObArenaAllocator owner;
  DatumValue original, incoming;
  ASSERT_EQ(OB_SUCCESS, original.set_int(17, owner));
  ASSERT_EQ(OB_SUCCESS, incoming.set_varchar(ObString("new value"), owner));
  PartitionBoundRow output(&original, 1), source(&incoming, 1);
  RejectBoundAllocator reject;
  ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED, source.clone(output, reject));
  ASSERT_EQ(&original, output.get_cells());
  ASSERT_EQ(17, output.get_cell(0).get_int());
  char malformed[] = {static_cast<char>(0x80)};
  int64_t pos = 0;
  ASSERT_NE(OB_SUCCESS, output.deserialize(owner, malformed, sizeof(malformed), pos));
  ASSERT_EQ(0, pos);
  ASSERT_EQ(&original, output.get_cells());
  char tiny[1]; pos = 0;
  ASSERT_NE(OB_SUCCESS, output.deep_copy(source, tiny, sizeof(tiny), pos));
  ASSERT_EQ(0, pos);
  ASSERT_EQ(&original, output.get_cells());
}
TEST(PartitionBoundRow, count_only_legacy_key_accepts_omitted_null_cell)
{
  ObArenaAllocator owner;
  PartitionBoundRow decoded;
  const char wire[] = {1};
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(owner, wire, sizeof(wire), pos));
  ASSERT_EQ(sizeof(wire), pos);
  ASSERT_EQ(1, decoded.count());
  ASSERT_TRUE(decoded.get_cell(0).is_null());
  decoded.destroy(owner);
}

TEST(PartitionBoundRow, key_and_row_wire_retain_distinct_projector_frame)
{
  ObArenaAllocator owner;
  DatumValue cells[3]; cells[0].set_null(); cells[1].set_min_value(); cells[2].set_max_value();
  PartitionBoundRow row(cells, 3), decoded;
  char key[128], record[128]; int64_t key_pos = 0, row_pos = 0, pos = 0;
  ASSERT_EQ(OB_SUCCESS, row.serialize(key, sizeof(key), key_pos));
  ASSERT_EQ(OB_SUCCESS, row.serialize_row(record, sizeof(record), row_pos));
  ASSERT_EQ(key_pos + 1, row_pos);
  ASSERT_EQ(0, MEMCMP(key, record, key_pos));
  ASSERT_EQ(0, record[key_pos]);
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize_row(owner, record, row_pos, pos));
  ASSERT_EQ(row_pos, pos);
  ASSERT_EQ(3, decoded.count());
  ASSERT_TRUE(decoded.get_cell(0).is_null());
  ASSERT_TRUE(decoded.get_cell(1).is_min_value());
  ASSERT_TRUE(decoded.get_cell(2).is_max_value());
  decoded.destroy(owner);
}

TEST(PartitionBoundRow, clone_keeps_raw_metadata_without_interpreting_collation)
{
  ObArenaAllocator source, destination;
  DatumValue value;
  ASSERT_EQ(OB_SUCCESS, value.set_varchar(ObString("raw schema text"), source));
  value.meta_.set_cs_type(0);
  value.meta_.set_cs_level(2);
  value.meta_.set_scale(-1);
  const SqlTypeMeta expected = value.meta_;
  PartitionBoundRow row(&value, 1), owned;
  ASSERT_EQ(OB_SUCCESS, row.clone(owned, destination));
  source.reset();
  ASSERT_EQ(expected, owned.get_cell(0).meta_);
  ASSERT_EQ(ObString("raw schema text"), owned.get_cell(0).get_string());
  owned.destroy(destination);
}

TEST(PartitionBoundRow, failed_reused_row_decode_keeps_old_owned_payload)
{
  ObArenaAllocator producer, owner;
  DatumValue original, incoming;
  ASSERT_EQ(OB_SUCCESS, original.set_varchar(ObString("old owned payload"), producer));
  original.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  ASSERT_EQ(OB_SUCCESS, incoming.set_int(3, producer));
  PartitionBoundRow initial(&original, 1), replacement(&incoming, 1), output;
  ASSERT_EQ(OB_SUCCESS, initial.clone(output, owner));
  char wire[128]; int64_t encoded = 0;
  ASSERT_EQ(OB_SUCCESS, replacement.serialize(wire, sizeof(wire), encoded));
  producer.reset(); original.reset(); incoming.reset();
  const DatumValue *previous = output.get_cells();
  RejectBoundAllocator reject;
  int64_t pos = 0;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, output.deserialize(reject, wire, encoded, pos));
  EXPECT_EQ(0, pos);
  EXPECT_EQ(previous, output.get_cells());
  EXPECT_EQ(ObString("old owned payload"), output.get_cell(0).get_string());
  output.destroy(owner);
}

TEST(PartitionBoundRow, mixed_clone_preserves_raw_metadata_flags_and_owns_one_block)
{
  ObArenaAllocator source;
  CountingBoundAllocator destination;
  constexpr int64_t count = 11;
  DatumValue cells[count];
  ASSERT_EQ(OB_SUCCESS, cells[0].set_int(INT64_MIN, source));
  ASSERT_EQ(OB_SUCCESS, cells[1].set_uint64(UINT64_MAX, source));
  ASSERT_EQ(OB_SUCCESS, cells[2].set_float(-1.25F, source));
  ASSERT_EQ(OB_SUCCESS, cells[3].set_datetime(1234567890123456LL, source));
  cells[3].set_scale(6);
  ASSERT_EQ(OB_SUCCESS, cells[4].set_varchar(ObString(""), source));
  ASSERT_EQ(OB_SUCCESS, cells[5].set_string(ObLongTextType, ObString("outrow locator"), source));
  cells[5].meta_.set_outrow();
  cells[5].datum_.set_outrow();
  ASSERT_EQ(OB_SUCCESS, cells[6].set_string(ObLongTextType, ObString("lob header bytes"), source));
  cells[6].meta_.set_has_lob_header();
  cells[6].datum_.set_has_lob_header();
  cells[7].datum_.set_null();
  cells[7].meta_.set_decimal_int(5);
  cells[8].set_min_value();
  cells[9].set_max_value();
  ASSERT_EQ(OB_SUCCESS, cells[10].set_extend(ObActionFlag::OP_NOP, 0, source));
  SqlTypeMeta metadata[count];
  uint32_t descriptors[count];
  const char *source_payloads[count];
  std::string payloads[count];
  for (int64_t i = 0; i < count; ++i) {
    metadata[i] = cells[i].meta_;
    alignas(8) char scratch[64]; ObDatum view;
    ASSERT_EQ(OB_SUCCESS, cells[i].export_legacy(view, scratch, sizeof(scratch)));
    descriptors[i] = view.pack_;
    source_payloads[i] = NativeValueFlat::uses_reference(cells[i])
        ? static_cast<const char *>(cells[i].datum_.get_reference()) : nullptr;
    if (!cells[i].is_null() && view.len_ > 0) { payloads[i].assign(view.ptr_, view.len_); }
  }
  int32_t projection[] = {10, 7, 4, 0, 5, 6, 9, 8, 3, 2, 1};
  PartitionBoundRow row(cells, count), owned;
  row.projector_ = projection;
  row.projector_size_ = count;
  ASSERT_EQ(OB_SUCCESS, row.clone(owned, destination));
  EXPECT_EQ(1, destination.allocations_);
  EXPECT_EQ(destination.allocation_, owned.get_cells());
  EXPECT_NE(projection, owned.projector_);
  for (int64_t i = 0; i < count; ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(projection[i], owned.projector_[i]);
    if (NativeValueFlat::uses_reference(cells[i])) {
      EXPECT_NE(source_payloads[i], owned.get_cells()[i].datum_.get_reference());
      if (cells[i].datum_.length() > 0) {
        MEMSET(const_cast<void *>(cells[i].datum_.get_reference()), 0xa5, cells[i].datum_.length());
      }
    } else if (!cells[i].is_null()) {
      EXPECT_EQ(0, owned.get_cells()[i].datum_.length());
    }
    cells[i].reset();
  }
  source.reset();
  projection[0] = 0;
  for (int64_t i = 0; i < count; ++i) {
    SCOPED_TRACE(i);
    const DatumValue &copy = owned.get_cells()[i];
    EXPECT_EQ(0, MEMCMP(&metadata[i], &copy.meta_, sizeof(SqlTypeMeta)));
    alignas(8) char scratch[64]; ObDatum view;
    ASSERT_EQ(OB_SUCCESS, copy.export_legacy(view, scratch, sizeof(scratch)));
    EXPECT_EQ(descriptors[i], view.pack_);
    if (!copy.is_null()) {
      ASSERT_NE(nullptr, view.ptr_);
      EXPECT_EQ(0, MEMCMP(payloads[i].data(), view.ptr_, payloads[i].size()));
    }
  }
  EXPECT_TRUE(owned.get_cell(0).is_nop_value());
  EXPECT_TRUE(owned.get_cell(1).is_null());
  EXPECT_EQ(ObDecimalIntType, owned.get_cell(1).get_type());
  EXPECT_EQ(5, owned.get_cell(1).get_scale());
  EXPECT_FALSE(owned.get_cell(2).is_null());
  EXPECT_EQ(0, owned.get_cell(2).get_string().length());
  owned.destroy(destination);
  EXPECT_EQ(1, destination.frees_);
}

TEST(PartitionBoundRow, clone_preserves_every_decimal_width_and_scale_after_source_overwrite)
{
  ObArenaAllocator source;
  CountingBoundAllocator destination;
  const int32_t widths[] = {0, 4, 8, 16, 32, 64};
  constexpr int64_t count = sizeof(widths) / sizeof(widths[0]);
  DatumValue cells[count];
  alignas(8) unsigned char coefficients[64];
  for (int64_t i = 0; i < static_cast<int64_t>(sizeof(coefficients)); ++i) { coefficients[i] = i + 1; }
  for (int64_t i = 0; i < count; ++i) {
    ASSERT_EQ(OB_SUCCESS, cells[i].set_decimal_int(widths[i], i + 1,
        widths[i] == 0 ? nullptr : reinterpret_cast<const ObDecimalInt *>(coefficients), source));
  }
  PartitionBoundRow row(cells, count), owned;
  ASSERT_EQ(OB_SUCCESS, row.clone(owned, destination));
  EXPECT_EQ(1, destination.allocations_);
  for (int64_t i = 0; i < count; ++i) {
    EXPECT_NE(cells[i].datum_.get_reference(), owned.get_cell(i).datum_.get_reference());
    MEMSET(const_cast<void *>(cells[i].datum_.get_reference()), 0, widths[i]);
    cells[i].reset();
  }
  source.reset();
  for (int64_t i = 0; i < count; ++i) {
    SCOPED_TRACE(widths[i]);
    const DatumValue &copy = owned.get_cell(i);
    EXPECT_EQ(ObDecimalIntType, copy.get_type());
    EXPECT_EQ(widths[i], copy.get_int_bytes());
    EXPECT_EQ(i + 1, copy.get_scale());
    EXPECT_FALSE(copy.is_null());
    EXPECT_EQ(0, MEMCMP(coefficients, copy.datum_.get_reference(), widths[i]));
  }
  owned.destroy(destination);
  EXPECT_EQ(1, destination.frees_);
}

TEST(PartitionBoundRow, nested_array_fallback_retains_child_parameter_metadata_and_one_owner_block)
{
  ObArenaAllocator source;
  CountingBoundAllocator destination;
  DatumParameterArray *outer = DatumParameterArray::alloc(source, 1);
  DatumParameterArray *inner = DatumParameterArray::alloc(source, 2);
  ASSERT_NE(nullptr, outer);
  ASSERT_NE(nullptr, inner);
  SqlTypeMeta outer_element_type;
  outer_element_type.set_ext();
  outer_element_type.set_extend_type(T_EXT_SQL_ARRAY);
  outer->element_.set_meta_type(outer_element_type);
  outer->source_value_size_ = 73;
  inner->element_.set_obj_type(ObVarcharType);
  inner->element_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  inner->source_value_size_ = 41;
  char text[] = "nested borrowed leaf";
  DatumParameter &leaf = inner->data_[0];
  SqlTypeMeta type;
  type.set_varchar();
  type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  leaf.set_type_meta(type);
  leaf.set_param_meta(type);
  leaf.datum_.set_string(text, sizeof(text) - 1);
  leaf.set_length(37);
  leaf.set_result_flags(0x1234);
  leaf.set_raw_text_info(23, 29);
  leaf.get_param_flag().flag_ = 0xa5;
  leaf.get_param_flag().extend_flag_ = 0x5a;
  SqlTypeMeta null_type;
  null_type.set_decimal_int(5);
  DatumParameter &null_leaf = inner->data_[1];
  null_leaf.set_type_meta(null_type);
  null_leaf.set_param_meta(null_type);
  null_leaf.datum_.set_null();
  null_leaf.set_precision(20);
  null_leaf.set_scale(5);
  null_leaf.set_need_to_check_type(true);
  ASSERT_EQ(OB_SUCCESS, outer->data_[0].set_extend(reinterpret_cast<int64_t>(inner), T_EXT_SQL_ARRAY, source));
  outer->data_[0].set_raw_text_info(7, 11);
  outer->data_[0].set_result_flags(0x77);
  DatumValue value;
  ASSERT_EQ(OB_SUCCESS, value.set_extend(reinterpret_cast<int64_t>(outer), T_EXT_SQL_ARRAY, source));
  PartitionBoundRow row(&value, 1), owned;
  ASSERT_EQ(OB_SUCCESS, row.clone(owned, destination));
  EXPECT_EQ(1, destination.allocations_);
  const auto *copied_outer = reinterpret_cast<const DatumParameterArray *>(owned.get_cell(0).get_ext());
  ASSERT_NE(nullptr, copied_outer);
  ASSERT_NE(outer, copied_outer);
  ASSERT_EQ(1, copied_outer->count_);
  const auto *copied_inner = copied_outer->data_[0].get_array<DatumParameterArray>();
  ASSERT_NE(nullptr, copied_inner);
  ASSERT_NE(inner, copied_inner);
  ASSERT_EQ(2, copied_inner->count_);
  EXPECT_EQ(outer->element_.get_obj_type(), copied_outer->element_.get_obj_type());
  EXPECT_EQ(outer->element_.get_meta_type().get_extend_type(),
      copied_outer->element_.get_meta_type().get_extend_type());
  EXPECT_EQ(inner->element_.get_obj_type(), copied_inner->element_.get_obj_type());
  EXPECT_EQ(inner->element_.get_collation_type(), copied_inner->element_.get_collation_type());
  EXPECT_NE(text, copied_inner->data_[0].datum_.get_reference());
  MEMSET(text, 'x', sizeof(text));
  source.reset();
  EXPECT_EQ(73, copied_outer->source_value_size_);
  EXPECT_EQ(41, copied_inner->source_value_size_);
  EXPECT_EQ(7, copied_outer->data_[0].get_raw_text_pos());
  EXPECT_EQ(11, copied_outer->data_[0].get_raw_text_len());
  EXPECT_EQ(0x77, copied_outer->data_[0].get_result_flag());
  const DatumParameter &copy = copied_inner->data_[0];
  EXPECT_EQ(ObString("nested borrowed leaf"), copy.get_string());
  EXPECT_EQ(0, MEMCMP(&type, &copy.get_type_meta(), sizeof(SqlTypeMeta)));
  EXPECT_EQ(0, MEMCMP(&type, &copy.get_param_meta(), sizeof(SqlTypeMeta)));
  EXPECT_EQ(37, copy.get_length());
  EXPECT_EQ(0x1234, copy.get_result_flag());
  EXPECT_EQ(23, copy.get_raw_text_pos());
  EXPECT_EQ(29, copy.get_raw_text_len());
  EXPECT_EQ(0xa5, copy.get_param_flag().flag_);
  EXPECT_EQ(0x5a, copy.get_param_flag().extend_flag_);
  const DatumParameter &null_copy = copied_inner->data_[1];
  EXPECT_TRUE(null_copy.is_null());
  EXPECT_EQ(0, MEMCMP(&null_type, &null_copy.get_type_meta(), sizeof(SqlTypeMeta)));
  EXPECT_EQ(0, MEMCMP(&null_type, &null_copy.get_param_meta(), sizeof(SqlTypeMeta)));
  EXPECT_EQ(20, null_copy.get_precision());
  EXPECT_EQ(5, null_copy.get_scale());
  EXPECT_TRUE(null_copy.need_to_check_type());
  owned.destroy(destination);
  EXPECT_EQ(1, destination.frees_);
}

TEST(PartitionBoundRow, every_short_buffer_preserves_previous_output_and_nonzero_cursor)
{
  ObArenaAllocator source;
  DatumValue cells[3], original;
  ASSERT_EQ(OB_SUCCESS, cells[0].set_int(42, source));
  ASSERT_EQ(OB_SUCCESS, cells[1].set_varchar(ObString("copied before the final payload"), source));
  ASSERT_EQ(OB_SUCCESS, cells[2].set_datetime(123456789, source));
  ASSERT_EQ(OB_SUCCESS, original.set_int(17, source));
  int32_t projection[] = {2, 0, 1};
  PartitionBoundRow incoming(cells, 3);
  incoming.projector_ = projection;
  incoming.projector_size_ = 3;
  constexpr int64_t start = 3;
  constexpr char guard = 0x5a;
  const int64_t capacity = start + incoming.get_deep_copy_size();
  std::vector<char> buffer(capacity + 16, guard);
  PartitionBoundRow complete;
  int64_t used = start;
  ASSERT_EQ(OB_SUCCESS, complete.deep_copy(incoming, buffer.data(), capacity, used));
  ASSERT_GT(used, start);
  ASSERT_LE(used, capacity);
  EXPECT_EQ(123456789, complete.get_cell(0).get_datetime());
  EXPECT_EQ(42, complete.get_cell(1).get_int());
  EXPECT_EQ(ObString("copied before the final payload"), complete.get_cell(2).get_string());
  for (int64_t length = start; length < used; ++length) {
    SCOPED_TRACE(length);
    std::vector<char> short_buffer(capacity + 16, guard);
    PartitionBoundRow output(&original, 1);
    int64_t pos = start;
    ASSERT_NE(OB_SUCCESS, output.deep_copy(incoming, short_buffer.data(), length, pos));
    EXPECT_EQ(start, pos);
    EXPECT_EQ(&original, output.get_cells());
    EXPECT_EQ(1, output.count());
    EXPECT_EQ(nullptr, output.projector_);
    EXPECT_EQ(17, output.get_cell(0).get_int());
    for (int64_t i = 0; i < start; ++i) { EXPECT_EQ(guard, short_buffer[i]); }
    for (int64_t i = length; i < static_cast<int64_t>(short_buffer.size()); ++i) {
      EXPECT_EQ(guard, short_buffer[i]);
    }
  }
}

// Opt in explicitly; ordinary correctness runs must not include this benchmark.
// Compile the identical test file against each production implementation, then
// alternate pinned-CPU runs. Setup, heap allocation and assertions are untimed.
TEST(PartitionBoundRow, DISABLED_clone_three_bigint_benchmark)
{
  int64_t iterations = 10000000;
  if (const char *configured = std::getenv("DATUM_CLONE_BENCH_ITERATIONS")) {
    char *end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(configured, &end, 10);
    ASSERT_EQ(0, errno);
    ASSERT_NE(configured, end);
    ASSERT_EQ('\0', *end);
    ASSERT_GT(parsed, 0);
    ASSERT_LE(parsed, 1000000000LL);
    iterations = parsed;
  }
  ObArenaAllocator source;
  const int64_t values[] = {1024, 2048, 3000000001LL};
  DatumValue cells[3];
  for (int64_t i = 0; i < 3; ++i) {
    ASSERT_EQ(OB_SUCCESS, cells[i].set_int(values[i], source));
  }
  const PartitionBoundRow row(cells, 3);
  const int64_t capacity = row.get_deep_copy_size();
  ASSERT_GT(capacity, 0);
  std::vector<char> buffer(capacity);
  PartitionBoundRow output;
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, output.deep_copy(row, buffer.data(), capacity, pos));
  const int64_t expected_cursor = pos;
  int ret = OB_SUCCESS;
  int64_t completed = 0;
  const auto begin = std::chrono::steady_clock::now();
  for (; completed < iterations; ++completed) {
    pos = 0;
    ret = output.deep_copy(row, buffer.data(), capacity, pos);
    if (ret != OB_SUCCESS) { break; }
    // A compiler barrier makes each copy observable without an extra checksum
    // load/store per iteration or a hardware synchronization instruction.
    std::atomic_signal_fence(std::memory_order_seq_cst);
  }
  const auto end = std::chrono::steady_clock::now();
  const int64_t elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(iterations, completed);
  ASSERT_EQ(expected_cursor, pos);
  ASSERT_EQ(3, output.count());
  uint64_t checksum = 1469598103934665603ULL;
  uint64_t expected_checksum = checksum;
  for (int64_t i = 0; i < 3; ++i) {
    EXPECT_EQ(values[i], output.get_cell(i).get_int());
    checksum = (checksum ^ static_cast<uint64_t>(output.get_cell(i).get_int())) * 1099511628211ULL;
    expected_checksum = (expected_checksum ^ static_cast<uint64_t>(values[i])) * 1099511628211ULL;
  }
  checksum ^= static_cast<uint64_t>(pos);
  expected_checksum ^= static_cast<uint64_t>(expected_cursor);
  EXPECT_EQ(expected_checksum, checksum);
  std::printf("datum_clone_benchmark iterations=%lld elapsed_ns=%lld ns_per_clone=%.3f checksum=%llu\n",
      static_cast<long long>(completed), static_cast<long long>(elapsed_ns),
      static_cast<double>(elapsed_ns) / static_cast<double>(completed),
      static_cast<unsigned long long>(checksum));
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
