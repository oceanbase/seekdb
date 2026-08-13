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
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <gtest/gtest.h>

#include "share/datum/ob_datum_funcs.h"

namespace oceanbase
{
namespace share
{
using namespace common;
static bool is_equal_content(const char *tmp_file, const char *result_file)
{
  std::ifstream if_tmp(tmp_file);
  std::ifstream if_result(result_file);

  EXPECT_TRUE(if_tmp.is_open());
  EXPECT_TRUE(if_result.is_open());

  std::istream_iterator<std::string> if_tmp_iter(if_tmp);
  std::istream_iterator<std::string> if_result_iter(if_result);

  return std::equal(if_tmp_iter, std::istream_iterator<std::string>(), if_result_iter);
}

class ObTestDatumCmp: public ::testing::Test
{
public:
  ObTestDatumCmp() {}
  ~ObTestDatumCmp() {}
  virtual void SetUp() {}
  virtual void TearDown() {}
private:
  DISALLOW_COPY_AND_ASSIGN(ObTestDatumCmp);
};

TEST(ObTestDatumCmp, defined_nullsafe_func_by_type)
{
  const char *test_srcdir = std::getenv("TEST_SRCDIR");
  const char *test_workspace = std::getenv("TEST_WORKSPACE");
  const char *test_tmpdir = std::getenv("TEST_TMPDIR");
  ASSERT_NE(nullptr, test_srcdir);
  ASSERT_NE(nullptr, test_workspace);
  ASSERT_NE(nullptr, test_tmpdir);
  const std::string defined_func_file =
      std::string(test_srcdir) + "/" + test_workspace +
      "/unittest/share/test_defined_func_by_type.result";
  const std::string tmp_file =
      std::string(test_tmpdir) + "/test_defined_func_by_type.tmp";
  std::ofstream of_result(tmp_file);

  for (int i = 0; i < ObMaxType; i++) {
    of_result << "/**************** " << inner_obj_type_str(static_cast<ObObjType>(i))
              << " ****************/" << "\n\n";
    for (int j = 0; j < ObMaxType; j++) {
      of_result << "<"
                << inner_obj_type_str(static_cast<ObObjType>(i))
                << ", "
                << inner_obj_type_str(static_cast<ObObjType>(j))
                << ">"
                << " : ";
      if (NULL != ObDatumFuncs::get_nullsafe_cmp_func(static_cast<ObObjType>(i),
                                                      static_cast<ObObjType>(j),
                                                      NULL_FIRST,
                                                      CS_TYPE_COLLATION_FREE,
                                                      SCALE_UNKNOWN_YET,
                                                      false, false)) {
        of_result << "defined\n";
      } else {
        of_result << "not defined\n";
      }
    } // for end
    of_result << "\n";
  } // for end
  of_result.flush();
  EXPECT_TRUE(is_equal_content(tmp_file.c_str(), defined_func_file.c_str()));
}

} // end namespace share
} // end namespace oceanbase
