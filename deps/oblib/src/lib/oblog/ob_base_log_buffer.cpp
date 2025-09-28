/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
