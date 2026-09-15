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

#ifndef OCEANBASE_LOGSERVICE_LOG_GROUP_ENTRY_HEADER_
#define OCEANBASE_LOGSERVICE_LOG_GROUP_ENTRY_HEADER_

#include "lib/ob_define.h"                      // Serialization
#include "lib/utility/ob_print_utils.h"         // Print*
#include "share/scn.h"                          // SCN
#include "share/log/palf/lsn.h"                                // LSN

namespace oceanbase
{
namespace palf
{
class LogGroupEntry;
class LogGroupEntryHeader
{
public:
  LogGroupEntryHeader();
  ~LogGroupEntryHeader();
public:
  using ENTRYTYPE = LogGroupEntry;
  // Generate a group header when the caller has already calculated the
  // checksum by walking immutable segments in LSN order.
  int generate(const bool is_padding_log,
               const int64_t data_len,
               const share::SCN &max_scn,
               const int64_t log_id,
               const LSN &committed_end_lsn,
               const int64_t data_checksum);
  bool is_valid() const;
  void reset();
  LogGroupEntryHeader& operator=(const LogGroupEntryHeader &header);
  int32_t get_data_len() const { return group_size_; }
  int64_t get_accum_checksum() const { return accumulated_checksum_; }
  const share::SCN &get_max_scn() const { return max_scn_; }
  int64_t get_log_id() const { return log_id_; }
  const LSN &get_committed_end_lsn() const { return committed_end_lsn_; }
  bool is_padding_log() const;

  bool operator==(const LogGroupEntryHeader &header) const;
  // This function used to check the checksum of buf is as same as
  // the data_checksum_
  bool check_header_integrity() const;
  bool check_integrity(const char *buf, int64_t data_len) const;
  bool check_integrity(const char *buf, int64_t data_len, int64_t &group_log_checksum) const;
  // The follow function used to update a few fields.
  //
  // NB: The data integrity is the responsibility of
  // the caller.
  //
  // Used to update header checksum
  void update_header_checksum();
  void update_accumulated_checksum(int64_t accumulated_checksum);

  NEED_SERIALIZE_AND_DESERIALIZE;

  TO_STRING_KV("magic", magic_,
               "version", version_,
               "group_size", group_size_,
               "committed_lsn", committed_end_lsn_,
               "max_scn", max_scn_,
               "accumulated_checksum", accumulated_checksum_,
               "log_id", log_id_,
               "flag", flag_);

private:
  void update_header_checksum_();
  bool check_header_checksum_() const;
  bool check_log_checksum_(const char *buf, const int64_t data_len, int64_t &group_log_checksum) const;
  uint16_t calculate_header_checksum_() const;
public:
  // Update this variable when modifying header's member
  static const int64_t HEADER_SER_SIZE;
  //GR means record
  static const int16_t MAGIC;
private:
  static const int16_t LOG_GROUP_ENTRY_HEADER_VERSION;
  static const int64_t PADDING_TYPE_MASK;
  static const int64_t PADDING_LOG_DATA_CHECKSUM;

  static const int64_t CRC16_MASK;
private:
  // Binary visualization, for LogGroupEntryHeader, its' magic number
  // is 0x4752, means GR(group header)
  int16_t magic_;
  int16_t version_;
  // The length of data, not including the header
  int32_t group_size_;
  // The max committed log offset before this log
  LSN committed_end_lsn_;
  // The max scn of this log
  share::SCN max_scn_;
  // The accumulated checksum before this log, including this log,
  // not including log header
  int64_t accumulated_checksum_;
  // The log id of this log, this field just only used for
  // sliding window
  int64_t log_id_;

  // | sign bit | PADDING bit | 46 unused bits | 16 crc16 bits |
  mutable int64_t flag_;
};

} // end namespace palf
} // end namespace oceanbase

#endif
