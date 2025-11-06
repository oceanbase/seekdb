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
#include "storage/tx/ob_trans_define.h"

namespace oceanbase
{
namespace unittest
{

class TestObTransTlog : public ::testing::Test
{
public :
  virtual void SetUp() {}
  virtual void TearDown() {}
};

TEST_F(TestObTransTlog, analyse_tlog)
{
  TRANS_LOG(INFO, "called", "func", test_info_->name());
  int ret = common::OB_ERR_UNEXPECTED;
  int fd = -1;
  char buf[32768];
  const char *tlog_file = "/tmp/tlog";
  int nread = 0;
  oceanbase::transaction::ObTransTraceLog tlog;
  fd = open(tlog_file, O_RDONLY);
  if (fd < 0) {
    TRANS_LOG(INFO, "open tlog file failed", K(tlog_file), K(fd));
  } else {
    TRANS_LOG(INFO, "open tlog file success", K(tlog_file), K(fd));
    nread = read(fd, buf, sizeof(buf));
    TRANS_LOG(INFO, "read tlog file", K(nread));
    if (nread != sizeof(tlog)) {
      TRANS_LOG(INFO, "tlog size not match", K(nread), K(sizeof(tlog)));
    } else {
      TRANS_LOG(INFO, "read tlog file success", K(nread));
      memcpy((void *)(&tlog), buf, sizeof(tlog));
      TRANS_LOG(INFO, "tlog", K(tlog));
    }
    close(fd);
    sleep(1);
  }
}


}//end of unittest
}//end of oceanbase

using namespace oceanbase;
using namespace oceanbase::common;

int main(int argc, char **argv)
{
  int ret = 1;
  ObLogger &logger = ObLogger::get_logger();
  logger.set_file_name("test_ob_trans_tlog.log", true);
  logger.set_log_level(OB_LOG_LEVEL_INFO);

  if (OB_SUCCESS != ObClockGenerator::init()) {
    TRANS_LOG(WARN, "init ob_clock_generator error!");
  } else {
    testing::InitGoogleTest(&argc, argv);
    ret = RUN_ALL_TESTS();
  }
  return ret;
}
