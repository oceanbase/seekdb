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
#include <thread>

#include "lib/trace/ob_trace.h"


using namespace oceanbase;

TEST(TestUUID, basic_test)
{
  char t[37];
  int64_t pos = 0;
  trace::UUID a;
  IGNORE_RETURN a.tostring(t, sizeof(t), pos);
  std::cout << t << std::endl;
  pos = 0;
  trace::UUID b("FFEEDDCC-BBAA-0099-8765-432101234567");
  IGNORE_RETURN b.tostring(t, sizeof(t), pos);
  std::cout << t << std::endl;
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, b.serialize(t, sizeof(t), pos));
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, a.deserialize(t, sizeof(t), pos));
  ASSERT_EQ(true, a.equal(b));
}

TEST(TestTrace, basic_test)
{
  // Framework end completes initialization
  char buffer[8 << 10];
  uint8_t level = 3;
  uint8_t auto_flush = 1;
  SET_TRACE_BUFFER(buffer, 8 << 10);
  FLT_SET_TRACE_LEVEL(level);
  FLT_SET_AUTO_FLUSH(auto_flush);
  //
  auto trace_id = FLT_BEGIN_TRACE();
  auto* proxy = FLT_BEGIN_SPAN(ObProxy);
  FLT_SET_TAG(sql_text, "select 1 from dual;");
  FLUSH_TRACE();
  auto t = std::thread([=]() {
    // RPC framework end completes initialization
    OBTRACE->init(trace_id, proxy->get_span_id(), (auto_flush << 7) + level);
    //
    FLTSpanGuard(ObSql);
    FLT_SET_TAG(sql_id, 123);
    FLT_SET_TAG(sql_text, "select 1 from dual;\\\"\r");
    auto trans = FLT_BEGIN_SPAN(ObTrans);
    FLT_SET_TAG(trans_id, 456, table_id, 789);
    IGNORE_RETURN FLT_BEGIN_SPAN(ObStorage);
    FLT_SET_TAG(table_id, 789, partition_id, 111, column_id, 1);
    FLT_END_CURRENT_SPAN();
    FLT_END_SPAN(trans);
    ObString sql_str("--only ObSql alive\n");
    FLT_SET_TAG(sql_text, sql_str);
    FLT_SET_AUTO_FLUSH(false);
    auto dummy = FLT_BEGIN_SPAN(ObTrans);
    FLT_SET_TAG(trans_id, 666);
    FLT_END_SPAN(dummy);
    FLT_RESET_SPAN();
    FLT_SET_AUTO_FLUSH(true);
    // sql end here
  });
  t.join();
  FLT_END_SPAN(proxy);
  FLT_END_TRACE();
}

int main(int argc, char **argv)
{
  OB_LOGGER.set_log_level("INFO");
  //OB_LOGGER.set_file_name("test_trace.log", true, false, nullptr, nullptr, "trace.log");
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}

