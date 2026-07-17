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
#include "lib/atomic/ob_atomic_reference.h"
#include "lib/oblog/ob_log.h"

namespace oceanbase
{
namespace common
{

TEST(ObAtomicReference, normal)
{
  int ret = OB_SUCCESS;
  uint32_t ref_cnt = 0;
  ObAtomicReference atomic_ref;

  //test overflow
  atomic_ref.atomic_num_.ref = UINT32_MAX;
  ret = atomic_ref.inc_ref_cnt();
  ASSERT_NE(OB_SUCCESS, ret);
  ret = atomic_ref.check_seq_num_and_inc_ref_cnt(0);
  ASSERT_NE(OB_SUCCESS, ret);
  ret = atomic_ref.check_and_inc_ref_cnt();
  ASSERT_NE(OB_SUCCESS, ret);

  //test 0
  atomic_ref.reset();
  ret = atomic_ref.dec_ref_cnt_and_inc_seq_num(ref_cnt);
  ASSERT_NE(OB_SUCCESS, ret);
  ret = atomic_ref.check_and_inc_ref_cnt();
  ASSERT_NE(OB_SUCCESS, ret);
  ret = atomic_ref.check_seq_num_and_inc_ref_cnt(1);
  ASSERT_NE(OB_SUCCESS, ret);

  //test normal
  ret = atomic_ref.inc_ref_cnt();
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = atomic_ref.check_and_inc_ref_cnt();
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = atomic_ref.check_seq_num_and_inc_ref_cnt(0);
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = atomic_ref.dec_ref_cnt_and_inc_seq_num(ref_cnt);
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = atomic_ref.dec_ref_cnt_and_inc_seq_num(ref_cnt);
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = atomic_ref.dec_ref_cnt_and_inc_seq_num(ref_cnt);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(1U, atomic_ref.get_seq_num());
}
}
}


int main(int argc, char** argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
