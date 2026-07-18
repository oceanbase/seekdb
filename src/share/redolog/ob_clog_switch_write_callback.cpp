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

#define USING_LOG_PREFIX COMMON
#include "share/redolog/ob_clog_switch_write_callback.h"
#include "lib/allocator/ob_malloc.h"

namespace oceanbase
{
namespace common
{
ObCLogSwitchWriteCallback::ObCLogSwitchWriteCallback()
  : is_inited_(false),  buffer_(nullptr), buffer_size_(0),
    info_block_len_(0)
{
}

ObCLogSwitchWriteCallback::~ObCLogSwitchWriteCallback()
{
  destroy();
}


void ObCLogSwitchWriteCallback::destroy()
{
  if (buffer_ != nullptr) {
    ob_free(buffer_);
    buffer_ = nullptr;
    buffer_size_ = 0;
  }
  is_inited_ = false;
}

int ObCLogSwitchWriteCallback::handle(
    const char *input_buf,
    const int64_t input_size,
    char *&output_buf,
    int64_t &output_size)
{
  return OB_NOT_SUPPORTED;
}

} // namespace common
} // namespace oceanbase
