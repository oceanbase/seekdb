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
#define private public
#include "common/storage/ob_sequence.h"

namespace oceanbase
{
using namespace common;
namespace unittest
{

class TestObSequence : public ::testing::Test
{
public :
  virtual void SetUp() {}
  virtual void TearDown() {}
};
//////////////////////Basic Function Test//////////////////////////////////////////

TEST_F(TestObSequence, get_max_seq_no)
{
  TRANS_LOG(INFO, "called", "func", test_info_->name());
  int64_t seq_no = ObSequence::get_max_seq_no();

  EXPECT_TRUE(seq_no <= ObTimeUtility::current_time());
  EXPECT_TRUE(seq_no > 0);
  EXPECT_EQ(ObSequence::inc_and_get_max_seq_no(), seq_no + 1);
  ObSequence::inc();
  EXPECT_EQ(ObSequence::get_max_seq_no(), seq_no + 2);
}

TEST_F(TestObSequence, update_max_seq_no)
{
  TRANS_LOG(INFO, "called", "func", test_info_->name());

  int64_t seq_no = ObSequence::get_max_seq_no();

  ObSequence::update_max_seq_no(0);
  EXPECT_TRUE(ObSequence::get_max_seq_no() >= seq_no);

  seq_no = ObSequence::get_max_seq_no();
  ObSequence::update_max_seq_no(-1);
  EXPECT_TRUE(ObSequence::get_max_seq_no() >= seq_no);

  seq_no = ObSequence::get_max_seq_no();
  ObSequence::update_max_seq_no(1);
  EXPECT_TRUE(ObSequence::get_max_seq_no() >= seq_no);

  seq_no = ObSequence::get_max_seq_no();
  ObSequence::update_max_seq_no(seq_no);
  EXPECT_TRUE(ObSequence::get_max_seq_no() > seq_no);

  seq_no = ObSequence::get_max_seq_no();
  ObSequence::update_max_seq_no(seq_no + 1000000);
  EXPECT_EQ(ObSequence::get_max_seq_no(), seq_no + 1000001);

  // just print error log but update success
  ObSequence::update_max_seq_no(seq_no + 1_day);
  EXPECT_EQ(ObSequence::get_max_seq_no(), seq_no + 1_day + 1);
  TRANS_LOG(INFO, "sequence", K(ObSequence::get_max_seq_no()));
}

TEST_F(TestObSequence, get_and_inc_max_seq_no)
{
  TRANS_LOG(INFO, "called", "func", test_info_->name());

  int64_t seq_no = 0;
  EXPECT_EQ(OB_SUCCESS, ObSequence::get_and_inc_max_seq_no(1, seq_no));
  EXPECT_EQ(ObSequence::get_max_seq_no(), seq_no + 1);

  EXPECT_EQ(OB_SUCCESS, ObSequence::get_and_inc_max_seq_no(0, seq_no));
  EXPECT_EQ(ObSequence::get_max_seq_no(), seq_no);

  EXPECT_NE(OB_SUCCESS, ObSequence::get_and_inc_max_seq_no(-1, seq_no));
  EXPECT_EQ(ObSequence::get_max_seq_no(), seq_no);

  EXPECT_NE(OB_SUCCESS, ObSequence::get_and_inc_max_seq_no(1_day + 1, seq_no));
  EXPECT_EQ(ObSequence::get_max_seq_no(), seq_no);
}

}//end of unittest
}//end of oceanbase

using namespace oceanbase;
using namespace oceanbase::common;

int main(int argc, char **argv)
{
  int ret = 1;
  ObLogger &logger = ObLogger::get_logger();
  logger.set_file_name("test_ob_sequence.log", true);
  logger.set_log_level(OB_LOG_LEVEL_INFO);
  testing::InitGoogleTest(&argc, argv);
  ret = RUN_ALL_TESTS();
  return ret;
}
