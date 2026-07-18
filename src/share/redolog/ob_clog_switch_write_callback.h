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

#ifndef OCEANBASE_COMMON_OB_CLOG_SWITCH_WRITE_CALLBACK_H_
#define OCEANBASE_COMMON_OB_CLOG_SWITCH_WRITE_CALLBACK_H_

#include <stdint.h>
#include "share/redolog/ob_log_write_callback.h"

namespace oceanbase
{
namespace common
{
class ObCLogSwitchWriteCallback : public ObLogWriteCallback
{
public:
  ObCLogSwitchWriteCallback();
  virtual ~ObCLogSwitchWriteCallback();
public:
  void destroy();
  virtual int handle(
      const char *input_buf,
      const int64_t input_size,
      char *&output_buf,
      int64_t &output_size) override;
  int64_t get_info_block_len() const { return info_block_len_; }
  const char* get_buffer() const { return buffer_; }
private:
  bool is_inited_;
  char *buffer_;
  int64_t buffer_size_;
  int64_t info_block_len_;
};
} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_COMMON_OB_CLOG_SWITCH_WRITE_CALLBACK_H_
