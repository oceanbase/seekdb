/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "common/datum/partition_bound_row.h"
#include "common/datum/datum_parameter_codec.h"
#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Native partition bounds must not include a legacy value definition"
#endif
#include "lib/allocator/ob_allocator.h"
#include <gtest/gtest.h>
using namespace oceanbase::common;
namespace {
class RejectBoundAllocator final : public ObIAllocator {
public:
  void *alloc(int64_t) override { return nullptr; }
  void *alloc(int64_t, const ObMemAttr &) override { return nullptr; }
  void free(void *) override {}
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
  const auto *copy = reinterpret_cast<const DatumParameterArray *>(owned.get_cell(0).datum_.get_int());
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

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
