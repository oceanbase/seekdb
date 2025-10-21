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

#ifndef OB_ADMIN_ARCHIVESERVICE_UTILS_H
#define OB_ADMIN_ARCHIVESERVICE_UTILS_H

#include "logservice/palf/lsn.h"
#include "share/backup/ob_backup_struct.h"

namespace oceanbase
{
namespace tools
{

// file header for common archive log file
struct ObArchiveFileHeader
{
  int16_t magic_;                    // FH
  int16_t version_;
  int32_t flag_;//for compression and encrytion and so on
  int64_t unit_size_;
  int64_t start_lsn_;
  int64_t checksum_;

  bool is_valid() const;
  int generate_header(const palf::LSN &lsn);
  NEED_SERIALIZE_AND_DESERIALIZE;
  TO_STRING_KV(K_(magic),
               K_(version),
               K_(flag),
               K_(unit_size),
               K_(start_lsn),
               K_(checksum));

public:
  static const int16_t ARCHIVE_FILE_HEADER_MAGIC = 0x4648; // FH means archive file header
  static constexpr int64_t DEFAULT_ARCHIVE_UNIT_SIZE = 16 * 1024L;   // archive compression encryption unit size;
};

class ObArchiveFileUtils
{
public:
  static int get_file_range(const ObString &prifix,
      const share::ObBackupStorageInfo *storage_info,
      int64_t &min_file_id,
      int64_t &max_file_id);
  static int extract_file_id(const ObString &file_name, int64_t &file_id, bool &match);
};

} // end namespace tools
} // end namespace oceanbase

#endif
