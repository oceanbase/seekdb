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

#ifndef OCEANBASE_LOGSERVICE_LOG_META_HEADER_
#define OCEANBASE_LOGSERVICE_LOG_META_HEADER_

#include <stdlib.h>                           // int64_t
#include "lib/ob_define.h"                    // Serialization
#include "lib/utility/ob_print_utils.h"       // Print*

namespace oceanbase
{
namespace palf
{
class LogMetaEntry;
class LogMetaEntryHeader
{
public:
  LogMetaEntryHeader();
  ~LogMetaEntryHeader();

public:
  using ENTRYTYPE = LogMetaEntry;
  int generate(const char *buf, int32_t data_len);
  bool is_valid() const;
  void reset();

  LogMetaEntryHeader& operator=(const LogMetaEntryHeader &header);
  int32_t get_data_len() const { return data_len_; }
  bool check_integrity(const char *buf, int32_t data_len) const;
  bool check_header_integrity() const;
  bool operator==(const LogMetaEntryHeader &header) const;
  TO_STRING_KV(K_(magic), K_(version), K_(data_len), K_(data_checksum),
      K_(header_checksum));
  static const int64_t HEADER_SER_SIZE;
  NEED_SERIALIZE_AND_DESERIALIZE;
  // 0x4C4D means LM(LogMeta)
  static const int16_t MAGIC = 0x4C4D;
private:
  int64_t calc_data_checksum_(const char *buf, int32_t data_len) const;
  int64_t calc_header_checksum_() const;
  bool check_data_checksum_(const char *buf, int32_t data_len) const;
  bool check_header_checksum_() const;

private:
  static const int16_t LOG_META_ENTRY_HEADER_VERSION = 1;

private:
  int16_t magic_;
  int16_t version_;
  int32_t data_len_;
  int64_t data_checksum_;
  int64_t header_checksum_;
};
} // end namespace palf
} // end namespace namespace

#endif
