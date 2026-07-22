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

#ifndef OCEANBASE_SHARE_IO_OB_BACKUP_STORAGE_INFO_H_
#define OCEANBASE_SHARE_IO_OB_BACKUP_STORAGE_INFO_H_

#include "lib/string/ob_fixed_length_string.h"
#include "lib/allocator/page_arena.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace share
{

const int64_t OB_MAX_BACKUP_PATH_LENGTH = 1024;
const int64_t OB_BACKUP_LS_DIR_NAME_LENGTH = 64;
const char *const OB_STR_LS = "logstream";
const int64_t OB_MAX_BACKUP_DEST_LENGTH = 2048;
typedef common::ObFixedLengthString<OB_MAX_BACKUP_DEST_LENGTH> ObBackupPathString;
typedef ObBackupPathString ObBackupSetPath;
typedef ObBackupPathString ObBackupPiecePath;

class ObBackupDest final
{
public:
  ObBackupDest();
  ~ObBackupDest();
  int set(const char *backup_dest);
  int set(const common::ObString &backup_dest);
  int set(const ObBackupPathString &backup_dest);
  void reset();
  bool is_valid() const;
  int get_backup_dest_str(char *buf, const int64_t buf_size) const;
  common::ObString get_root_path() const { return root_path_;}
  bool operator ==(const ObBackupDest &backup_dest) const;
  bool operator !=(const ObBackupDest &backup_dest) const;
  int deep_copy(const ObBackupDest &backup_dest);
  int64_t hash() const;
  DECLARE_TO_STRING;

private:
  int alloc_and_init();
  int parse_backup_dest_str_(const char *backup_dest);
  bool is_root_path_equal(const ObBackupDest &backup_dest) const;
  void root_path_trim_();

  char *root_path_;
  common::ObArenaAllocator allocator_;
  DISALLOW_COPY_AND_ASSIGN(ObBackupDest);
};

}  // namespace share
}  // namespace oceanbase

#endif  // OCEANBASE_SHARE_IO_OB_BACKUP_STORAGE_INFO_H_
