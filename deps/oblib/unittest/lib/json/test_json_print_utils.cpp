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

#define USING_LOG_PREFIX LIB

#include <gtest/gtest.h>
#include "lib/json/ob_json_print_utils.h"

namespace oceanbase
{
namespace json
{
using namespace common;


void check_converted_json(const char *json, const char *std_json)
{
  ObStdJsonConvertor convertor;
  const int64_t buf_len = strlen(json) * 2 + 1;
  char buf[buf_len];

  ASSERT_EQ(OB_SUCCESS, convertor.init(json, buf, buf_len));
  int64_t len = 0;
  ASSERT_EQ(OB_SUCCESS, convertor.convert(len));
  ASSERT_LT(len, buf_len);
  buf[len] = 0;
  ASSERT_STREQ(buf, std_json);
}

TEST(TestStdJsonConvertor, all)
{
  const char *json = "";
  check_converted_json(json, json);
  ASSERT_FALSE(HasFailure());

  json = "{}";
  check_converted_json(json, json);
  ASSERT_FALSE(HasFailure());

  json = "      [             ]    ";
  check_converted_json(json, json);
  ASSERT_FALSE(HasFailure());

  json = " { abc : 123,   \"abc2\"   : \"xxxxxxxxx   x\" ,  xyz: [] }  ";
  const char * std_json = " { \"abc\" : 123,   \"abc2\"   : \"xxxxxxxxx   x\" ,  \"xyz\": [] }  ";
  check_converted_json(json, std_json);
  ASSERT_FALSE(HasFailure());

  check_converted_json(std_json, std_json);
  ASSERT_FALSE(HasFailure());

  // with escaped string and json not finished
  json = "{a:123, b:[false, true,\"abc\\\", abc:456\\\\\", ]:c:\"\",d : \"abc";
  std_json = "{\"a\":123, \"b\":[false, true,\"abc\\\", abc:456\\\\\", ]:\"c\":\"\",\"d\" : \"abc";

  check_converted_json(json, std_json);
  ASSERT_FALSE(HasFailure());

  check_converted_json(std_json, std_json);
  ASSERT_FALSE(HasFailure());

  // buf not enough
  ObStdJsonConvertor convertor;
  const int64_t buf_len = 10;
  char buf[buf_len];
  ASSERT_EQ(OB_SUCCESS, convertor.init(json, buf, buf_len));
  int64_t len = 0;
  ASSERT_EQ(OB_BUF_NOT_ENOUGH, convertor.convert(len));
  ASSERT_TRUE(strncmp(std_json, buf, buf_len) == 0);
}
} // end namespace json
} // end namespace oceanase

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
