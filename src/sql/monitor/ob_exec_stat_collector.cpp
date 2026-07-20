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

#define USING_LOG_PREFIX SQL_MONITOR
#include "ob_exec_stat_collector.h"
using namespace oceanbase::common;
using namespace oceanbase::observer;
namespace oceanbase
{
namespace sql
{
int ObExecStatCollector::add_raw_stat(const common::ObString &str)
{
  int ret = OB_SUCCESS;
  if (length_ + str.length() >= MAX_STAT_BUF_COUNT) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_DEBUG("buffer size not enough", K(ret),K(length_), K(str.length()));
  } else {
    MEMCPY(extend_buf_ + length_, str.ptr(), str.length());
    length_ += str.length();
  }
  return ret;
}

int ObExecStatCollector::get_extend_info(ObIAllocator &allocator, ObString &str)
{
  int ret = OB_SUCCESS;
  const ObString tmp_str(length_, extend_buf_);
  if (OB_FAIL(ob_write_string(allocator, tmp_str, str))) {
    LOG_WARN("fail to write string", K(tmp_str), K(ret));
  }
  return ret;
}

}/* ns sql*/
}/* ns oceanbase */
