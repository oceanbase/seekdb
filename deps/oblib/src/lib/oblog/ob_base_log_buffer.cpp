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

#include "ob_base_log_buffer.h"
#include <sys/mman.h>

namespace oceanbase
{
namespace common
{
ObBaseLogBufferMgr::ObBaseLogBufferMgr() : log_buf_cnt_(0)
{
}

ObBaseLogBufferMgr::~ObBaseLogBufferMgr()
{
  destroy();
}

void ObBaseLogBufferMgr::destroy()
{
  for (int64_t i = 0; i < log_buf_cnt_; ++i) {
    if (NULL != log_ctrls_[i].base_buf_) {
      munmap(log_ctrls_[i].base_buf_, SHM_BUFFER_SIZE);
      log_ctrls_[i].base_buf_ = NULL;
      log_ctrls_[i].data_buf_ = NULL;
    }
  }
  log_buf_cnt_ = 0;
}


}
}
