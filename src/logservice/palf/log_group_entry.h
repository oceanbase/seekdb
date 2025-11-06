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

#ifndef OCEANBASE_LOGSERVICE_LOG_GROUP_ENTRY_
#define OCEANBASE_LOGSERVICE_LOG_GROUP_ENTRY_

#include "log_group_entry_header.h"              // LogGroupEntryHeader
#include "lib/ob_define.h"                    // Serialization
#include "lib/utility/ob_print_utils.h"       // Print*
#include "lib/utility/ob_macro_utils.h"       // DISALLOW_COPY_AND_ASSIGN

namespace oceanbase
{
namespace palf
{
class LogGroupEntry
{
public:
  LogGroupEntry();
  ~LogGroupEntry();

public:
  // @brief generate an LogGroupEntry used to serialize or deserialize
  // @param[in] header is an object on the stack
  // @param[in] buf is a block of memory space which has allocated by heap
  int generate(const LogGroupEntryHeader &header,
               const char *buf);
  int shallow_copy(const LogGroupEntry &entry);
  bool is_valid() const;
  void reset();
  bool check_integrity() const;
  bool check_integrity(int64_t &data_checksum) const;
  int64_t get_header_size() const { return header_.get_serialize_size(); }
  int64_t get_payload_offset() const { return header_.get_serialize_size(); }
  int64_t get_data_len() const { return header_.get_data_len(); }
  // return total size of header and body, including the length of padding log
  int64_t get_group_entry_size() const { return header_.get_serialize_size() +
    header_.get_data_len(); }
  int get_log_min_scn(share::SCN &min_scn) const;
  const share::SCN get_scn() const { return header_.get_max_scn(); }
  LSN get_committed_end_lsn() const { return header_.get_committed_end_lsn(); }
  const LogGroupEntryHeader &get_header() const { return header_; }
  const char *get_data_buf() const { return buf_; }
  // @brief truncate log group entry the upper_limit_scn, only log entries with scn not bigger than which can reserve
  // param[in] upper_limit_scn the upper bound to determain which log entries can reserve
  // param[in] pre_accum_checksum, the accum_checksum of the pre log
  int truncate(const share::SCN &upper_limit_scn, const int64_t pre_accum_checksum);
  bool check_compatibility() const;

  TO_STRING_KV("LogGroupEntryHeader", header_);
  NEED_SERIALIZE_AND_DESERIALIZE;
  static const int64_t BLOCK_SIZE = PALF_BLOCK_SIZE;
  using LogEntryHeaderType=LogGroupEntryHeader;
private:
  LogGroupEntryHeader header_;
  const char *buf_;
  DISALLOW_COPY_AND_ASSIGN(LogGroupEntry);
};
} // end namespace palf
} // end namespace oceanbase

#endif
