/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "lib/charset/ob_charset.h"
#include "share/datum/number_cast_constants.h"
#include "common/wide_integer/ob_wide_integer.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#include "share/system_variable/ob_system_variable_init.h"
#include <gtest/gtest.h>
#include <iostream>
using namespace oceanbase::common;
int main(int argc, char **argv)
{
  static ObArenaAllocator constants;
  int ret = ObCharset::init_charset();
  if (ret == OB_SUCCESS) { ret = ObNumberConstValue::init(constants); }
  if (ret == OB_SUCCESS) { ret = wide::ObDecimalIntConstValue::init_const_values(constants); }
  if (ret == OB_SUCCESS) { ret = oceanbase::share::ObSysVariables::init_default_values(); }
  if (ret != OB_SUCCESS) { std::cerr << "native session test initialization failed: " << ret << "\n"; return 2; }
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
