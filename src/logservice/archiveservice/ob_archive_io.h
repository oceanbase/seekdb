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

#include "lib/string/ob_string.h"      // ObString
#include "share/backup/ob_backup_struct.h"

#ifndef OCEANBASE_ARCHIVE_OB_ARCHIVE_IO_H_
#define OCEANBASE_ARCHIVE_OB_ARCHIVE_IO_H_

namespace oceanbase
{
namespace archive
{
using oceanbase::common::ObString;
class ObArchiveIO
{
public:
  ObArchiveIO() {}
  ~ObArchiveIO() {}

public:
  int push_log(const ObString &uri,
      const share::ObBackupStorageInfo *storage_info,
      const int64_t backup_dest_id,
      char *data,
      const int64_t data_len,
      const int64_t offset,
      const bool is_full_file,
      const bool is_can_seal);

  int mkdir(const ObString &uri,
      const share::ObBackupStorageInfo *storage_info);

private:
  int check_context_match_in_normal_file_(const ObString &uri,
      const share::ObBackupStorageInfo *storage_info,
      const common::ObStorageIdMod &storage_id_mod,
      char *data,
      const int64_t data_len,
      const int64_t offset);
};
} // namespace archive
} // namespace oceanbase

#endif /* OCEANBASE_ARCHIVE_OB_ARCHIVE_IO_H_ */
