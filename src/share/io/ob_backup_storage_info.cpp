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

#define USING_LOG_PREFIX SHARE
#include "share/io/ob_backup_storage_info.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace share
{

static constexpr char FILE_PREFIX[] = "file://";

int ObBackupDest::set(const char *backup_dest)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_ISNULL(backup_dest)
      || 0 != STRNCMP(backup_dest, FILE_PREFIX, sizeof(FILE_PREFIX) - 1)
      || OB_NOT_NULL(STRCHR(backup_dest, '?'))
      || STRLEN(backup_dest) >= OB_MAX_BACKUP_DEST_LENGTH) {
    ret = OB_INVALID_BACKUP_DEST;
    LOG_WARN("invalid local backup destination", K(ret), KP(backup_dest));
  } else if (OB_FAIL(root_path_.assign(backup_dest))) {
    ret = OB_INVALID_BACKUP_DEST;
    LOG_WARN("backup destination is too long", K(ret), KP(backup_dest));
  } else {
    trim_trailing_slashes_();
  }
  return ret;
}

int ObBackupDest::set(const common::ObString &backup_dest)
{
  int ret = OB_SUCCESS;
  if (backup_dest.empty() || OB_NOT_NULL(backup_dest.find('\0'))) {
    ret = OB_INVALID_BACKUP_DEST;
    LOG_WARN("invalid local backup destination", K(ret), K(backup_dest));
  } else {
    common::ObFixedLengthString<OB_MAX_BACKUP_DEST_LENGTH> value;
    if (OB_FAIL(value.assign(backup_dest))) {
      ret = OB_INVALID_BACKUP_DEST;
      LOG_WARN("backup destination is too long", K(ret), K(backup_dest));
    } else if (OB_FAIL(set(value.ptr()))) {
      LOG_WARN("failed to set local backup destination", K(ret), K(backup_dest));
    }
  }
  return ret;
}

int ObBackupDest::set(const ObBackupPathString &backup_dest)
{
  return set(backup_dest.ptr());
}

void ObBackupDest::trim_trailing_slashes_()
{
  const int64_t min_len = sizeof(FILE_PREFIX);
  int64_t len = root_path_.size();
  while (len > min_len && '/' == root_path_.ptr()[len - 1]) {
    root_path_.ptr()[--len] = '\0';
  }
}

int ObBackupDest::is_backup_path_equal(const ObBackupDest &other, bool &is_equal) const
{
  int ret = OB_SUCCESS;
  if (!is_valid() || !other.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("backup destination is not initialized", K(ret), K(*this), K(other));
  } else {
    is_equal = (*this == other);
  }
  return ret;
}

int ObBackupDest::get_backup_dest_str(char *buf, const int64_t buf_size) const
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("backup destination is not initialized", K(ret));
  } else if (OB_ISNULL(buf) || buf_size <= root_path_.size()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("output buffer is invalid", K(ret), KP(buf), K(buf_size), K(root_path_));
  } else if (OB_FAIL(databuff_printf(buf, buf_size, "%s", root_path_.ptr()))) {
    LOG_WARN("failed to print backup destination", K(ret), K(root_path_));
  }
  return ret;
}

int ObBackupDest::get_backup_path_str(char *buf, const int64_t buf_size) const
{
  return get_backup_dest_str(buf, buf_size);
}

int64_t ObBackupDest::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(root_path));
  J_OBJ_END();
  return pos;
}

}  // namespace share
}  // namespace oceanbase
