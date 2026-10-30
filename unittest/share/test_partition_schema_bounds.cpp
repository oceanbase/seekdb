/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "share/schema/ob_schema_struct.h"
#include "lib/allocator/ob_allocator.h"
#include "share/datum/number_cast_constants.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
namespace {
class SwitchBoundAllocator final : public ObIAllocator {
public:
  SwitchBoundAllocator() : reject_(false), remaining_(-1) {}
  void *alloc(int64_t size) override { if (reject_ || remaining_ == 0) { return nullptr; } if (remaining_ > 0) { --remaining_; } return arena_.alloc(size); }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *pointer) override { arena_.free(pointer); }
  bool reject_;
  int64_t remaining_;
  ObArenaAllocator arena_;
};
}
TEST(PartitionSchemaBounds, schema_setters_own_borrowed_native_values)
{
  ObArenaAllocator producer, schema_owner;
  ObPartition schema(&schema_owner);
  DatumValue value;
  ASSERT_EQ(OB_SUCCESS, value.set_varchar(ObString("bound"), producer));
  value.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  PartitionBoundRow row(&value, 1);
  ASSERT_EQ(OB_SUCCESS, schema.set_high_bound_val(row));
  ASSERT_EQ(OB_SUCCESS, schema.set_low_bound_val(PartitionBoundRow::MIN_ROW));
  ASSERT_EQ(OB_SUCCESS, schema.add_list_row(row));
  producer.reset(); value.reset();
  ASSERT_EQ(ObString("bound"), schema.get_high_bound_val().get_cell(0).get_string());
  ASSERT_TRUE(schema.get_low_bound_val().is_min_row());
  ASSERT_EQ(1, schema.get_list_row_values().count());
  ASSERT_EQ(ObString("bound"), schema.get_list_row_values().at(0).get_cell(0).get_string());
}
TEST(PartitionSchemaBounds, failed_replacement_retains_prior_bound_and_list)
{
  ObArenaAllocator producer;
  SwitchBoundAllocator owner;
  ObPartition schema(&owner);
  DatumValue initial, incoming;
  ASSERT_EQ(OB_SUCCESS, initial.set_int(17, producer));
  ASSERT_EQ(OB_SUCCESS, incoming.set_varchar(ObString("replacement"), producer));
  PartitionBoundRow old_row(&initial, 1), new_row(&incoming, 1);
  ASSERT_EQ(OB_SUCCESS, schema.set_high_bound_val(old_row));
  ASSERT_EQ(OB_SUCCESS, schema.add_list_row(old_row));
  const DatumValue *previous = schema.get_high_bound_val().get_cells();
  owner.reject_ = true;
  ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED, schema.set_high_bound_val(new_row));
  ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED, schema.add_list_row(new_row));
  ASSERT_EQ(previous, schema.get_high_bound_val().get_cells());
  ASSERT_EQ(17, schema.get_high_bound_val().get_cell(0).get_int());
  ASSERT_EQ(1, schema.get_list_row_values().count());
  ASSERT_EQ(17, schema.get_list_row_values().at(0).get_cell(0).get_int());
  owner.reject_ = false;
}
TEST(PartitionSchemaBounds, assign_survives_source_schema_and_arena_reset)
{
  ObArenaAllocator source_owner, destination_owner;
  ObPartition source(&source_owner), destination(&destination_owner);
  DatumValue value;
  ASSERT_EQ(OB_SUCCESS, value.set_varchar(ObString("independent"), source_owner));
  value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  PartitionBoundRow row(&value, 1);
  ASSERT_EQ(OB_SUCCESS, source.set_high_bound_val(row));
  ASSERT_EQ(OB_SUCCESS, source.set_low_bound_val(PartitionBoundRow::MIN_ROW));
  ASSERT_EQ(OB_SUCCESS, source.add_list_row(row));
  ASSERT_EQ(OB_SUCCESS, destination.assign(source));
  source.reset(); source_owner.reset(); value.reset();
  ASSERT_EQ(ObString("independent"), destination.get_high_bound_val().get_cell(0).get_string());
  ASSERT_TRUE(destination.get_low_bound_val().is_min_row());
  ASSERT_EQ(ObString("independent"), destination.get_list_row_values().at(0).get_cell(0).get_string());
}

TEST(PartitionSchemaBounds, reused_list_mid_decode_oom_keeps_old_owned_payload)
{
  // Captured from the actual old row serializer: two one-column INT(3) rows.
  static const char wire[] = "\x02\x01\x05\x05\x3f\xff\x03\x00\x01\x05\x05\x3f\xff\x03\x00";
  for (int fail_at : {1, 2, 3}) {
    ObArenaAllocator producer;
    SwitchBoundAllocator owner;
    ObListRowValues list(owner);
    DatumValue initial;
    ASSERT_EQ(OB_SUCCESS, initial.set_varchar(ObString("old owned payload"), producer));
    initial.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
    ASSERT_EQ(OB_SUCCESS, list.push_back_with_deep_copy(owner, PartitionBoundRow(&initial, 1)));
    producer.reset(); initial.reset();
    const DatumValue *previous = list.at(0).get_cells();
    owner.remaining_ = fail_at - 1;
    int64_t pos = 0;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, list.deserialize(owner, wire, sizeof(wire) - 1, pos)) << fail_at;
    EXPECT_EQ(0, pos);
    ASSERT_EQ(1, list.count());
    EXPECT_EQ(previous, list.at(0).get_cells());
    EXPECT_EQ(ObString("old owned payload"), list.at(0).get_cell(0).get_string());
    owner.remaining_ = -1;
  }
}

TEST(PartitionSchemaBounds, actual_legacy_list_wire_and_reused_partial_values)
{
  std::ifstream input("unittest/share/data/partition_bounds.golden");
  ASSERT_TRUE(input.good());
  std::string line;
  int cases = 0;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') { continue; }
    std::vector<std::string> fields; std::istringstream split(line); std::string field;
    while (std::getline(split, field, '\t')) { fields.push_back(field); }
    ASSERT_EQ(5, fields.size());
    SCOPED_TRACE(fields[0]);
    std::string wire;
    for (size_t i = 0; i < fields[1].size(); i += 2) { wire.push_back(std::stoi(fields[1].substr(i, 2), nullptr, 16)); }
    ObArenaAllocator owner;
    ObListRowValues list(owner);
    int64_t pos = 0;
    const int ret = list.deserialize(owner, wire.data(), wire.size(), pos);
    ASSERT_EQ(std::stoi(fields[2]), ret);
    if (ret == OB_SUCCESS) {
      ASSERT_EQ(std::stoll(fields[3]), pos);
      std::string canonical;
      for (size_t i = 0; i < fields[4].size(); i += 2) { canonical.push_back(std::stoi(fields[4].substr(i, 2), nullptr, 16)); }
      std::string encoded(list.get_serialize_size(), '\0'); pos = 0;
      ASSERT_EQ(OB_SUCCESS, list.serialize(encoded.data(), encoded.size(), pos));
      EXPECT_EQ(canonical, encoded);
    } else {
      EXPECT_EQ(0, pos);
      EXPECT_EQ(0, list.count());
    }
    ++cases;
  }
  EXPECT_EQ(242, cases);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  static ObArenaAllocator constants;
  int ret = ObCharset::init_charset();
  if (ret == OB_SUCCESS) { ret = ObNumberConstValue::init(constants); }
  if (ret == OB_SUCCESS) { ret = wide::ObDecimalIntConstValue::init_const_values(constants); }
  if (ret != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
