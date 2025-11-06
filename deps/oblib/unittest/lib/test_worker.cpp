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
#include "lib/worker.h"


TEST(TestWorker, CompatMode)
{
  using oceanbase::lib::Worker;
  set_compat_mode(oceanbase::lib::Worker::CompatMode::MYSQL);
  // MySQL Mode as default.
  EXPECT_EQ(oceanbase::lib::Worker::CompatMode::MYSQL, THIS_WORKER.get_compatibility_mode());
  EXPECT_TRUE(oceanbase::lib::is_mysql_mode());
  EXPECT_FALSE(oceanbase::lib::is_oracle_mode());

  // Change to Oracle Mode if set
  THIS_WORKER.set_compatibility_mode(oceanbase::lib::Worker::CompatMode::ORACLE);
  EXPECT_EQ(oceanbase::lib::Worker::CompatMode::ORACLE, THIS_WORKER.get_compatibility_mode());
  EXPECT_FALSE(oceanbase::lib::is_mysql_mode());
  EXPECT_TRUE(oceanbase::lib::is_oracle_mode());

  // Turn back to MySQL Mode if set back.
  THIS_WORKER.set_compatibility_mode(oceanbase::lib::Worker::CompatMode::MYSQL);
  EXPECT_EQ(oceanbase::lib::Worker::CompatMode::MYSQL, THIS_WORKER.get_compatibility_mode());
  EXPECT_TRUE(oceanbase::lib::is_mysql_mode());
  EXPECT_FALSE(oceanbase::lib::is_oracle_mode());
}

int main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
