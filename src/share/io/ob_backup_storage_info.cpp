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
#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace share
{

ObBackupDest::ObBackupDest()
  : root_path_(NULL),
    allocator_("ObBackupDest")
{
}

ObBackupDest::~ObBackupDest()
{
  reset();
}

bool ObBackupDest::is_valid() const
{
  return NULL != root_path_;
}

void ObBackupDest::reset()
{
  allocator_.reset();
  root_path_ = NULL;
}

bool ObBackupDest::operator ==(const ObBackupDest &backup_dest) const
{
  bool is_equal = true;
  is_equal = is_root_path_equal(backup_dest);
  return is_equal;
}

bool ObBackupDest::operator !=(const ObBackupDest &backup_dest) const
{
  return !(*this == backup_dest);
}

int ObBackupDest::deep_copy(const ObBackupDest &backup_dest)
{
  reset();
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator;
  char *backup_dest_str = NULL;
  if (OB_ISNULL(backup_dest_str = reinterpret_cast<char *>(allocator.alloc(share::OB_MAX_BACKUP_DEST_LENGTH)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", KR(ret));
  } else if (OB_FAIL(backup_dest.get_backup_dest_str(backup_dest_str, share::OB_MAX_BACKUP_DEST_LENGTH))) {
    LOG_WARN("failed to get backup dest str", K(ret));
  } else if (OB_FAIL(set(backup_dest_str))) {
    LOG_WARN("failed to set backup dest", K(ret));
  }
  return ret;
}

int64_t ObBackupDest::hash() const
{
  int64_t hash_val = 0;
  if (is_valid()) {
    hash_val = murmurhash(root_path_, static_cast<int32_t>(strlen(root_path_)), hash_val);
  }
  return hash_val;
}

int ObBackupDest::alloc_and_init()
{
  int ret = OB_SUCCESS;
  if (is_valid()) {
    // do nothing
  } else if (OB_ISNULL(root_path_ = static_cast<char *>(allocator_.alloc(OB_MAX_BACKUP_PATH_LENGTH)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc root_path memory", K(ret));
  } else {
    MEMSET(root_path_, 0, OB_MAX_BACKUP_PATH_LENGTH);
  }
  return ret;
}


int ObBackupDest::parse_backup_dest_str_(const char *backup_dest)
{
  int ret = OB_SUCCESS;
  const int64_t path_len = strlen(backup_dest);
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("backup_dest not init", K(ret), K(backup_dest));
  } else if (0 != STRNCMP(backup_dest, OB_FILE_PREFIX, STRLEN(OB_FILE_PREFIX))
             || OB_NOT_NULL(strchr(backup_dest, '?'))) {
    ret = OB_INVALID_BACKUP_DEST;
    LOG_WARN("only plain file backup destination is supported", K(ret), K(backup_dest));
  } else if (path_len >= OB_MAX_BACKUP_PATH_LENGTH) {
    ret = OB_INVALID_BACKUP_DEST;
    LOG_ERROR("backup dest is too long", K(ret), K(path_len), K(backup_dest));
  } else {
    MEMCPY(root_path_, backup_dest, path_len + 1);
  }
  return ret;
}

int ObBackupDest::set(const char *backup_dest)
{
  int ret = OB_SUCCESS;
  reset();
  if (is_valid()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("cannot init twice", K(ret), K(*this));
  } else if (OB_ISNULL(backup_dest) || strlen(backup_dest) >= OB_MAX_BACKUP_DEST_LENGTH) {
    ret = OB_INVALID_BACKUP_DEST;
    LOG_WARN("invalid args", K(ret), KP(backup_dest));
  } else if (OB_FAIL(alloc_and_init())) {
    LOG_WARN("failed to alloc and init backup dest", K(ret));
  } else if (OB_FAIL(parse_backup_dest_str_(backup_dest))) {
    LOG_WARN("failed to parse backup dest str", K(ret), K(backup_dest));
  } else {
    root_path_trim_();
  }
  return ret;
}

int ObBackupDest::set(const common::ObString &backup_dest)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator;
  char *backup_dest_str = NULL;
  if (OB_ISNULL(backup_dest_str = reinterpret_cast<char *>(allocator.alloc(backup_dest.length() + 1)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", KR(ret));
  } else {
    MEMCPY(backup_dest_str, backup_dest.ptr(), backup_dest.length());
    backup_dest_str[backup_dest.length()] = '\0';
    if (OB_FAIL(set(backup_dest_str))) {
      LOG_WARN("failed to set backup dest", KR(ret), K(backup_dest));
    }
  }
  return ret;
}

int ObBackupDest::set(const ObBackupPathString &backup_dest_str)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(set(backup_dest_str.ptr()))) {
    LOG_WARN("failed to set backup dest", KR(ret), K(backup_dest_str));
  }
  return ret;
}

void ObBackupDest::root_path_trim_()
{
  int len = static_cast<int32_t>(strlen(root_path_));
  for (int i = len - 1; i >=0 ; i--) {
    if (root_path_[i] == '/') {
      root_path_[i] = '\0';
    } else {
      break;
    }
  }
}

bool ObBackupDest::is_root_path_equal(const ObBackupDest &backup_dest) const
{
  bool is_equal = true;
  if ((OB_ISNULL(root_path_) && !OB_ISNULL(backup_dest.root_path_))
      || (!OB_ISNULL(root_path_) && OB_ISNULL(backup_dest.root_path_))) {
    is_equal = false;
  } else if (!OB_ISNULL(root_path_) && !OB_ISNULL(backup_dest.root_path_)) {
    if (strlen(root_path_) != strlen(backup_dest.root_path_)) {
      is_equal = false;
    } else if (0 != STRCMP(root_path_, backup_dest.root_path_)) {
      is_equal = false;
    }
  }
  return is_equal;
}

int ObBackupDest::get_backup_dest_str(char *buf, const int64_t buf_size) const
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_NOT_INIT;
    LOG_WARN("backup dest is not init", K(ret), K(*this));
  } else if (OB_ISNULL(buf) || buf_size < share::OB_MAX_BACKUP_DEST_LENGTH) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(buf), K(buf_size));
  } else if (OB_FAIL(databuff_printf(buf, buf_size, "%s", root_path_))) {
    LOG_WARN("failed to get backup dest str", K(ret), K(root_path_));
  }

  return ret;
}


int64_t ObBackupDest::to_string(char *buf, int64_t buf_len) const
{
  int64_t pos = 0;
  if (OB_ISNULL(buf) || buf_len <= 0 || !is_valid()) {
    // do nothing
  } else {
    J_OBJ_START();
    ObString root_path(root_path_);
    J_KV(K(root_path));
    J_OBJ_END();
  }
  return pos;
}

}  // namespace share
}  // namespace oceanbase
