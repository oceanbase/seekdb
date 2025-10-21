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

#include <gtest/gtest.h>
#include "lib/json/ob_json.h"

using namespace oceanbase::common;
using namespace oceanbase::json;
//using namespace oceanbase::sql;

class TestJsonFormat : public ::testing::Test
{
public:
  TestJsonFormat() {};
  virtual ~TestJsonFormat() {};
  virtual void SetUp() {};
  virtual void TearDown() {};
private:
  // disallow copy and assign
  TestJsonFormat(const TestJsonFormat &other);
  TestJsonFormat& operator=(const TestJsonFormat &ohter);
};

TEST_F(TestJsonFormat, basic)
{
  char json[] = "{\
  \"main_query\":  [{\
    \"PHY_PROJECT\":  { },\
    \"PHY_INDEX_SELECTOR\":  {\
      \"USE_INDEX\": \"test\" },\
            \"PHY_TABLE_RPC_SCAN_IMPL\":  {\
              \"PHY_RPC_SCAN\":  {\
                \"scan_plan\":  {\
                  \"plan\":  {\
                    \"plan\":  {\
                      \"main_query\":  {\
                        \"PHY_PROJECT\":  { },\
                        \"PHY_TABLET_SCAN_V2\":  { } } } } } } } }] }";
  int32_t length = static_cast<int32_t>(strlen(json));

  ObArenaAllocator allocator(ObModIds::OB_SQL_PARSER);
  Parser parser;
  parser.init(&allocator, NULL);
  Value *root = NULL;
  parser.parse(json, length, root);
  Tidy tidy(root);
  char output_buf[OB_MAX_LOG_BUFFER_SIZE];
  output_buf[0] = '\n';
  int64_t pos = tidy.to_string(output_buf + 1, sizeof(output_buf));
  output_buf[pos + 1] = '\n';
  _OB_LOG(INFO, "%.*s", static_cast<int32_t>(pos + 2), output_buf);
}

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("DEBUG");
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
