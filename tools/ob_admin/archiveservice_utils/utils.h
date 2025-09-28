/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
  static constexpr int64_t DEFAULT_ARCHIVE_UNIT_SIZE = 16 * 1024L;   // 归档压缩加密单元大小;
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