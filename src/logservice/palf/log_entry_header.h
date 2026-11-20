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

#ifndef OCEANBASE_LOGSERVICE_LOG_ENTRY_HEADER_
#define OCEANBASE_LOGSERVICE_LOG_ENTRY_HEADER_

#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"
#include "share/scn.h"

namespace oceanbase
{
namespace palf
{
class LogEntryHeader
{
public:
  LogEntryHeader();
  ~LogEntryHeader();
public:
  LogEntryHeader& operator=(const LogEntryHeader &header);
  void reset();
  bool is_valid() const;
  bool check_integrity(const char *buf, const int64_t buf_len) const;
  int32_t get_data_len() const { return log_size_; }
  const share::SCN get_scn() const { return scn_; }
  int64_t get_data_checksum() const { return data_checksum_; }
  bool check_header_integrity() const;

  NEED_SERIALIZE_AND_DESERIALIZE;
  TO_STRING_KV("magic", magic_,
               "version", version_,
               "log_size", log_size_,
               "scn_", scn_,
               "data_checksum", data_checksum_,
               "flag", flag_);
public:
  static const int16_t MAGIC;  // 'LH' means LOG ENTRY HEADER
  static const int64_t HEADER_SER_SIZE;
private:
  uint16_t calculate_header_checksum_() const;
  bool check_header_checksum_() const;
  bool is_padding_log_() const;
private:
  static const int16_t LOG_ENTRY_HEADER_VERSION;
  static const int64_t PADDING_TYPE_MASK;
  static const int64_t CRC16_MASK;
private:
  int16_t magic_;
  int16_t version_;
  int32_t log_size_;
  share::SCN scn_;
  int64_t data_checksum_;
  // | sign bit | PADDING bit | 46 unused bit | 16 crc16 bit |
  mutable int64_t flag_;
};

int16_t xxhash_16(int16_t checksum, const uint8_t* data, const int64_t data_len);
}
}
#endif // OCEANBASE_LOGSERVICE_LOG_ENTRY_HEADER_
