/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "common/datum/datum_value_projection.h"
#include <gtest/gtest.h>
using namespace oceanbase::common;

TEST(DatumValueProjection, decimal_int_accepts_negative_scale)
{
  int32_t coefficient = 1234;
  ObDatum datum;
  datum.set_decimal_int_shallow(reinterpret_cast<ObDecimalInt *>(&coefficient), sizeof(coefficient));
  SqlTypeMeta meta;
  meta.set_decimal_int(-9);
  DatumValue output;
  ASSERT_EQ(OB_SUCCESS, project_datum_value_view(
      datum, meta, OBJ_DATUM_DECIMALINT, SqlTypeMeta(), output));
  EXPECT_EQ(-9, output.get_scale());
  EXPECT_EQ(sizeof(coefficient), output.datum_.len_);
  EXPECT_EQ(coefficient, *reinterpret_cast<const int32_t *>(output.datum_.ptr_));
}

TEST(DatumValueProjection, decimal_int_rejects_unknown_scale_without_overwriting_output)
{
  int32_t coefficient = 1234;
  ObDatum datum;
  datum.set_decimal_int_shallow(reinterpret_cast<ObDecimalInt *>(&coefficient), sizeof(coefficient));
  SqlTypeMeta meta;
  meta.set_decimal_int(SCALE_UNKNOWN_YET);
  DatumValue output(datum, SqlTypeMeta());
  SqlTypeMeta previous_meta = output.meta_;
  EXPECT_EQ(OB_ERR_UNEXPECTED, project_datum_value_view(
      datum, meta, OBJ_DATUM_DECIMALINT, SqlTypeMeta(), output));
  EXPECT_EQ(previous_meta, output.meta_);
  EXPECT_EQ(reinterpret_cast<char *>(&coefficient), output.datum_.ptr_);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
