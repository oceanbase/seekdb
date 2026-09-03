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

#ifndef OCEANBASE_SHARE_LOG_PALF_LOG_BUFFER_
#define OCEANBASE_SHARE_LOG_PALF_LOG_BUFFER_

#include "lib/ob_define.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace palf
{

// An explicitly transferable owner for one immutable PALF log payload. The
// prefix is reserved for LogEntryHeader, so PALF can prepend its header without
// copying the payload into another buffer.
//
// This ownership-only type lives in Share because both transaction log
// producers and PALF consume it. PALF-specific segment and group metadata stay
// in Logservice.
class PalfLogBuffer
{
public:
  PalfLogBuffer();
  ~PalfLogBuffer();

  int init(const int64_t capacity, const int64_t prefix_size);
  int copy_from(const char *buf, const int64_t size, const int64_t prefix_size);
  int extend_and_copy(const int64_t new_capacity, const int64_t valid_size);
  int seal(const int64_t size);
  int reuse_for_write();
  int move_from(PalfLogBuffer &other);
  void reset();

  bool is_valid() const;
  bool is_sealed() const { return is_sealed_; }
  char *get_buf() { return data_; }
  const char *get_buf() const { return data_; }
  char *get_prefix_buf(const int64_t size);
  const char *get_prefix_buf(const int64_t size) const;
  int64_t get_size() const { return size_; }
  int64_t get_capacity() const { return capacity_; }
  int64_t get_prefix_size() const { return prefix_size_; }

  TO_STRING_KV(KP_(allocation), KP_(data), K_(size), K_(capacity),
      K_(prefix_size), K_(is_sealed));

private:
  char *allocation_ = NULL;
  char *data_ = NULL;
  int64_t size_ = 0;
  int64_t capacity_ = 0;
  int64_t prefix_size_ = 0;
  bool is_sealed_ = false;
  DISALLOW_COPY_AND_ASSIGN(PalfLogBuffer);
};

} // namespace palf
} // namespace oceanbase

#endif // OCEANBASE_SHARE_LOG_PALF_LOG_BUFFER_
