/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "sql/engine/aggregate/ob_exec_hash_struct.h"
#include "common/datum/datum_value_buffer.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace oceanbase::common;
using namespace oceanbase::sql;
TEST(NativeHashCols, immutable_old_hashes_preserve_projector_and_column_collation)
{
  ObArenaAllocator owner;
  DatumValueBuffer values(owner);
  ASSERT_EQ(OB_SUCCESS, values.reserve(5));
  char binary[] = {'A', 'b', '\0', 'Z', ' '};
  ASSERT_EQ(OB_SUCCESS, values.set_int(0, -1837));
  ASSERT_EQ(OB_SUCCESS, values.set_uint64(1, UINT64_MAX));
  ASSERT_EQ(OB_SUCCESS, values.set_varchar_view(2, ObString(sizeof(binary), binary)));
  values.at(2).set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  ASSERT_EQ(OB_SUCCESS, values.set_double(3, -0.0));
  ASSERT_EQ(OB_SUCCESS, values.set_null(4));
  ObNewRow row(values.data(), 5);
  const ObCollationType collations[] = {CS_TYPE_UTF8MB4_GENERAL_CI, CS_TYPE_UTF8MB4_BIN, CS_TYPE_BINARY};
  int32_t projector[] = {4, 2, 0, 3, 1};
  std::ifstream input("unittest/sql/data/native_hash_cols.golden");
  ASSERT_TRUE(input.good());
  std::string line; int records = 0;
  std::vector<char> reserved(sizeof(ObRowStore::StoredRow) + DatumValueBuffer::allocation_size(5));
  auto *stored = new (reserved.data()) ObRowStore::StoredRow;
  stored->reserved_cells_count_ = 5;
  DatumValue *cells = nullptr;
  DatumValueBuffer::initialize(stored->reserved_cells_, 5, cells);
  ASSERT_EQ(stored->reserved_cells_, cells);
  for (int i = 0; i < 5; ++i) {
    ASSERT_EQ(OB_SUCCESS, DatumValueBuffer::assign_view(values.at(i), cells, 5, i));
  }
  while (std::getline(input, line)) {
    SCOPED_TRACE(line);
    std::istringstream fields(line);
    char tag; int mode, cs, count; uint64_t expected;
    ASSERT_TRUE(bool(fields >> tag >> mode >> cs >> count >> expected));
    ASSERT_EQ('H', tag); ASSERT_GE(cs, 0); ASSERT_LT(cs, 3);
    row.projector_ = mode ? projector : nullptr; row.projector_size_ = mode ? 5 : 0;
    ObSEArray<ObColumnInfo, 8> columns;
    for (int i = 0; i < count; ++i) {
      ObColumnInfo column; column.index_ = i; column.cs_type_ = collations[cs];
      ASSERT_EQ(OB_SUCCESS, columns.push_back(column));
    }
    ObHashCols hash(&row, &columns);
    EXPECT_EQ(expected, hash.inner_hash());
    EXPECT_EQ(expected, hash.hash());
    if (mode == 0) {
      // Reserved cells have the same logical order; the fallback row may be
      // projected differently and must not be consulted by this branch.
      row.projector_ = projector; row.projector_size_ = 5;
      ObHashCols reserved_hash(&row, &columns); reserved_hash.stored_row_ = stored;
      EXPECT_EQ(expected, reserved_hash.inner_hash());
    }
    ++records;
  }
  EXPECT_EQ(30, records);
}
