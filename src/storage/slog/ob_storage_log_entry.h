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

#ifndef OCEANBASE_STORAGE_OB_STORAGE_LOG_ENTRY_H_
#define OCEANBASE_STORAGE_OB_STORAGE_LOG_ENTRY_H_

#include "common/ob_record_header.h"

namespace oceanbase
{
namespace storage
{

struct ObStorageLogEntry
{
  int16_t magic_;
  int16_t version_;
  int16_t entry_len_;
  int16_t rez_;
  int32_t cmd_;
  int32_t data_len_;
  uint64_t seq_;
  int64_t timestamp_;
  uint64_t data_checksum_;
  uint64_t entry_checksum_;

  static const int16_t MAGIC_NUMBER = static_cast<int16_t>(0xAAAAL);
  static const int16_t ENTRY_VERSION = 1;

  ObStorageLogEntry();
  ~ObStorageLogEntry();

  TO_STRING_KV(K_(magic),
               K_(version),
               K_(entry_len),
               K_(entry_checksum),
               K_(cmd),
               K_(data_len),
               K_(seq),
               K_(data_checksum),
               K_(timestamp))

  // set fields of ObRecordHeader
  int fill_entry(
      const char *log_data,
      const int64_t data_len,
      const int32_t cmd,
      const uint64_t seq);
  void reset();
  // Calculate the checksum of data
  uint64_t calc_data_checksum(const char *log_data, const int64_t data_len) const;
  // Calculate the checksum of entry
  uint64_t calc_entry_checksum() const;

  int check_entry_integrity(const bool dump_content = true) const;

  // check the integrity of data
  int check_data_integrity(const char *log_data, const bool dump_content = true) const;

  NEED_SERIALIZE_AND_DESERIALIZE;
};

}
}

#endif
