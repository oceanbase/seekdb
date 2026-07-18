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

#ifndef OCEANBASE_LOGSERVICE_LOG_READER_UTILS_
#define OCEANBASE_LOGSERVICE_LOG_READER_UTILS_
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace palf
{
struct ReadBuf
{
  ReadBuf();
  ReadBuf(char *buf, const int64_t buf_len);
  ReadBuf(const ReadBuf &rhs);
  bool operator==(const ReadBuf &rhs) const;
  bool operator!=(const ReadBuf &rhs) const;
  
  ReadBuf &operator=(const ReadBuf &rhs);
  ~ReadBuf();
  void reset();
  bool is_valid() const;
  bool is_valid_raw_read_buf();
  TO_STRING_KV(K(buf_len_), KP(buf_));

  char *buf_;
  int64_t buf_len_;
};

struct ReadBufGuard
{
  ReadBufGuard(const char *label, const int64_t buf_len);
  ~ReadBufGuard();
  ReadBuf read_buf_;
};

int alloc_read_buf(const char *label, const int64_t buf_len, ReadBuf &read_buf);
void free_read_buf(ReadBuf &read_buf);

bool is_valid_raw_read_buf(const ReadBuf &raw_read_buf,
                           const int64_t offset,
                           const int64_t nbytes);

} // end of logservice
} // end of oceanbase

#endif
