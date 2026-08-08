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

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <gtest/gtest.h>

#include "sql/engine/expr/ob_expr_cmp_func.h"
#include "unittest/sql/sql_test_paths.h"

namespace oceanbase
{
namespace sql
{
using namespace common;

namespace
{
bool is_equal_content(const char *tmp_file, const char *result_file)
{
  std::ifstream if_tmp(tmp_file);
  std::ifstream if_result(result_file);
  EXPECT_TRUE(if_tmp.is_open());
  EXPECT_TRUE(if_result.is_open());
  return std::equal(
      std::istream_iterator<std::string>(if_tmp),
      std::istream_iterator<std::string>(),
      std::istream_iterator<std::string>(if_result));
}
} // namespace

TEST(ObExprCmpFuncInventory, defined_expr_func_by_type)
{
  const std::string defined_func_file =
      sql_test_data_path("engine/expr/test_defined_expr_func_by_type.result");
  const std::string tmp_file = sql_test_tmp_path("test_defined_expr_func.tmp");
  std::ofstream of_result(tmp_file);

  for (int i = 0; i < ObMaxType; i++) {
    of_result << "/**************** " << inner_obj_type_str(static_cast<ObObjType>(i))
              << " ****************/\n\n";
    for (int j = 0; j < ObMaxType; j++) {
      of_result << "<"
                << inner_obj_type_str(static_cast<ObObjType>(i))
                << ", "
                << inner_obj_type_str(static_cast<ObObjType>(j))
                << "> : ";
      if (nullptr != ObExprCmpFuncsHelper::get_datum_expr_cmp_func(
                         static_cast<ObObjType>(i),
                         static_cast<ObObjType>(j),
                         SCALE_UNKNOWN_YET,
                         SCALE_UNKNOWN_YET,
                         PRECISION_UNKNOWN_YET,
                         PRECISION_UNKNOWN_YET,
                         CS_TYPE_COLLATION_FREE,
                         false)) {
        of_result << "defined\n";
      } else {
        of_result << "not defined\n";
      }
    }
    of_result << "\n";
  }
  of_result.flush();
  EXPECT_TRUE(is_equal_content(tmp_file.c_str(), defined_func_file.c_str()));
}

} // namespace sql
} // namespace oceanbase
