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

#ifndef OCEANBASE_LOGSERVICE_LOG_ENTRY_
#define OCEANBASE_LOGSERVICE_LOG_ENTRY_

#include "log_entry_header.h"              // LogGroupEntryHeader
#include "lib/ob_define.h"                 // Serialization
#include "lib/utility/ob_print_utils.h"    // Print*
#include "lib/utility/ob_macro_utils.h"    // DISALLOW_COPY_AND_ASSIGN
#include "share/scn.h"
#include "log_define.h"

namespace oceanbase
{
namespace palf
{
class LogEntry
{
public:
  LogEntry();
  ~LogEntry();

public:
  int shallow_copy(const LogEntry &input);
  bool is_valid() const;
  void reset();
  // TODO by runlin, need check header checsum?
  bool check_integrity() const;
  int64_t get_header_size() const { return header_.get_serialize_size(); }
  int64_t get_payload_offset() const { return header_.get_serialize_size(); }
  int64_t get_data_len() const { return header_.get_data_len(); }
  const share::SCN get_scn() const { return header_.get_scn(); }
  const char *get_data_buf() const { return buf_; }
  const LogEntryHeader &get_header() const { return header_; }

  TO_STRING_KV("LogEntryHeader", header_);
  NEED_SERIALIZE_AND_DESERIALIZE;
  static const int64_t BLOCK_SIZE = PALF_BLOCK_SIZE;
  using LogEntryHeaderType=LogEntryHeader;
private:
  LogEntryHeader header_;
  const char *buf_;
  DISALLOW_COPY_AND_ASSIGN(LogEntry);
};
} // end namespace palf
} // end namespace oceanbase

#endif
