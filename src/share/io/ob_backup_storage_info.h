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

#include "lib/restore/ob_storage_info.h"
#include "lib/string/ob_fixed_length_string.h"
#include "lib/allocator/page_arena.h"

namespace oceanbase
{
namespace share
{

// ObBackupStorageInfo is the storage-info vocabulary type still used by live local-file
// IO paths (SELECT INTO OUTFILE and LOAD DATA INFILE).
const int64_t OB_MAX_BACKUP_PATH_LENGTH = 1024;
const int64_t OB_BACKUP_LS_DIR_NAME_LENGTH = 64;
const char *const OB_STR_LS = "logstream";
const char *const ENCRYPT_KEY = "encrypt_key=";
const int64_t OB_MAX_BACKUP_DEST_LENGTH = 2048;
const int64_t OB_MAX_BACKUP_AUTHORIZATION_LENGTH = 1024;
typedef common::ObFixedLengthString<OB_MAX_BACKUP_DEST_LENGTH> ObBackupPathString;
typedef ObBackupPathString ObBackupSetPath;
typedef ObBackupPathString ObBackupPiecePath;

class ObBackupStorageInfo : public common::ObObjectStorageInfo
{
public:
  using common::ObObjectStorageInfo::set;

public:
  ObBackupStorageInfo() {}
  virtual ~ObBackupStorageInfo();

  int set(
      const common::ObStorageType device_type,
      const char *endpoint,
      const char *authorization,
      const char *extension);
  int set_endpoint(const common::ObStorageType device_type, const char *storage_info);
  int get_authorization_info(char *authorization, const int64_t length) const;
  int get_unencrypted_authorization_info(char *authorization, const int64_t length) const;
};

class ObBackupDest final
{
public:
  ObBackupDest();
  ~ObBackupDest();
  int set(const char *backup_dest);
  int set(const common::ObString &backup_dest);
  int set(const ObBackupPathString &backup_dest);
  int set(
      const char *path,
      const char *endpoint,
      const char *authorization,
      const char *extension);
  int set(const char *root_path, const char *storage_info);
  int set(const char *root_path, const ObBackupStorageInfo *storage_info);
  int set_without_decryption(const common::ObString &backup_dest);
  int set_storage_path(const common::ObString &storage_path_str);
  void reset();
  int reset_access_id_and_access_key(
      const char *access_id, const char *access_key);
  bool is_valid() const;
  bool is_root_path_equal(const ObBackupDest &backup_dest) const;
  int is_backup_path_equal(const ObBackupDest &backup_dest, bool &is_equal) const;
  bool is_enable_worm() const { return OB_ISNULL(storage_info_) ? false : storage_info_->is_enable_worm(); } 
  bool is_storage_type_file(){ return OB_ISNULL(storage_info_) ? 
      false : ObStorageType::OB_STORAGE_FILE == storage_info_->get_type(); }
  ObStorageType get_storage_type() const { return OB_ISNULL(storage_info_) ? ObStorageType::OB_STORAGE_MAX_TYPE : storage_info_->get_type(); }
  int get_backup_dest_str(char *buf, const int64_t buf_size) const;
  int get_backup_path_str(char *buf, const int64_t buf_size) const;
  common::ObString get_root_path() const { return root_path_;}
  share::ObBackupStorageInfo *get_storage_info() const { return storage_info_;}
  bool operator ==(const ObBackupDest &backup_dest) const;
  bool operator !=(const ObBackupDest &backup_dest) const;
  int deep_copy(const ObBackupDest &backup_dest);
  int64_t hash() const;
  DECLARE_TO_STRING;

private:
  int alloc_and_init();
  int parse_backup_dest_str_(const char *backup_dest, const bool only_parse_for_unique_path);
  void root_path_trim_();

  char *root_path_;
  share::ObBackupStorageInfo *storage_info_;
  common::ObArenaAllocator allocator_;
  DISALLOW_COPY_AND_ASSIGN(ObBackupDest);
};

}  // namespace share
}  // namespace oceanbase

#endif  // OCEANBASE_SHARE_IO_OB_BACKUP_STORAGE_INFO_H_
