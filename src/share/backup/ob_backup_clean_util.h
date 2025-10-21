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

#ifndef OCEANBASE_SHARE_OB_BACKUP_CEALN_UTIL_H_
#define OCEANBASE_SHARE_OB_BACKUP_CEALN_UTIL_H_

#include "share/backup/ob_backup_struct.h"
#include "share/backup/ob_backup_path.h"
#include "common/storage/ob_device_common.h"

namespace oceanbase
{
namespace share
{

class ObBackupCleanFileOp : public ObBaseDirEntryOperator
{
public:
  enum ObBackUpFile
  {
    BACKUP_NORMAL_FILE = 1,
    BACKUP_CLOG = 2
  };
  ObBackupCleanFileOp(
      const ObBackupPath& path, 
      const share::ObBackupStorageInfo *storage_info,
      ObBackUpFile file_type)
        : path_(path),
          file_type_(file_type),
          storage_info_(storage_info),
          total_file_num_(0),
          handled_file_num_(0)
  {}
  virtual ~ObBackupCleanFileOp() {}
  int func(const dirent *entry) override;
  int64_t get_total_file_num() {return total_file_num_;}
  int64_t get_handled_file_num() {return handled_file_num_;}

public:
  ObBackupPath path_;
  ObBackUpFile file_type_;
  const share::ObBackupStorageInfo *storage_info_;
  int64_t total_file_num_;
  int64_t handled_file_num_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObBackupCleanFileOp);
};

class ObBackupPrefixDeleteFileOp : public ObBaseDirEntryOperator
{
public:
  ObBackupPrefixDeleteFileOp();
  virtual ~ObBackupPrefixDeleteFileOp() {}
  int func(const dirent *entry) override;
  int init(
      const char *filter_str,
      const int32_t filter_str_len,
      const ObBackupPath& path,
      const share::ObBackupStorageInfo *storage_info);

public:
  bool is_inited_;
  ObBackupPath path_;
  char filter_str_[common::MAX_PATH_SIZE];
  const share::ObBackupStorageInfo *storage_info_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObBackupPrefixDeleteFileOp); 
};

class ObBackupCleanUtil
{
public:
  static int delete_backup_dir_files(
      const ObBackupPath &path,
      const share::ObBackupStorageInfo *storage_info);
  static int delete_clog_dir_files(
      const ObBackupPath &path,
      const share::ObBackupStorageInfo *storage_info);
  static int delete_backup_dir(
      const ObBackupPath &path,
      const share::ObBackupStorageInfo *storage_info);
  static int delete_backup_file(
      const ObBackupPath &path,
      const share::ObBackupStorageInfo *storage_info);
private:
  static int delete_backup_dir_(
      const ObBackupPath &path,
      const share::ObBackupStorageInfo *storage_info);
  static void check_need_retry(
    const int64_t result,
    const int64_t start_ts,
    int64_t &retry_count,
    int64_t &io_limit_retry_count,
    bool &need_retry);
};
}//share
}//oceanbase

#endif /* OCEANBASE_SHARE_OB_BACKUP_CEALN_UTIL_H_ */
