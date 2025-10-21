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

#ifndef OCEANBASE_STORAGE_OB_STORAGE_LOG_WRITE_BUFFER_H_
#define OCEANBASE_STORAGE_OB_STORAGE_LOG_WRITE_BUFFER_H_

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace storage
{
class ObStorageLogItem;

class ObStorageLogWriteBuffer
{
public:
  ObStorageLogWriteBuffer();
  ~ObStorageLogWriteBuffer();

  int init(
      const int64_t align_size,
      const int64_t buf_size,
      const int64_t tenant_id);
  void destroy();
  const char *get_buf() const { return buf_; }
  int64_t get_write_len() const { return write_len_; }
  int64_t get_log_data_len() const { return log_data_len_; }
  int64_t get_left_space() const { return buf_size_ - log_data_len_; }
  int copy_log_item(const ObStorageLogItem *item);
  int move_buffer(int64_t &backward_size);
  void reuse();

  TO_STRING_KV(K_(is_inited), K_(buf), K_(buf_size), K_(write_len), K_(log_data_len));

private:
  bool is_inited_;
  char *buf_;
  int64_t buf_size_;
  // represents the total length (data length + nop length)
  int64_t write_len_;
  // represents the data length
  int64_t log_data_len_;
  int64_t align_size_;
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_STORAGE_LOG_WRITE_BUFFER_H_
