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

#ifndef OB_ADMIN_SLOG_EXECUTOR_H_
#define OB_ADMIN_SLOG_EXECUTOR_H_

#include "../ob_admin_executor.h"
#include "storage/blocksstable/ob_log_file_spec.h"
#include <dirent.h>
#include "common/storage/ob_device_common.h"
#include "lib/ob_define.h"

namespace oceanbase
{
namespace tools
{

class ObLogDirEntryOperation : public ObBaseDirEntryOperator
{
public:
  ObLogDirEntryOperation() {};
  virtual ~ObLogDirEntryOperation();
  virtual int func(const dirent *entry) override;
  void reset();

public:
  static const int64_t MAX_TENANT_NUM = 64;
  static const int64_t MAX_D_NAME_LEN = 256;

public:
  int64_t size_;
  char d_names_[MAX_TENANT_NUM][MAX_D_NAME_LEN];
};

class ObAdminSlogExecutor : public ObAdminExecutor
{
public:
  ObAdminSlogExecutor();
  virtual ~ObAdminSlogExecutor() = default;
  virtual int execute(int argc, char *argv[]);

private:
  int parse_args(int argc, char *argv[]);
  void print_usage();
  int scan_periodically();
  int concat_dir(const char *root_dir, const char *dir);
  int parse_log(const char *slog_dir, const int64_t file_id);

private:
  static const int64_t FILE_MAX_SIZE = 256 << 20;
  uint64_t tenant_id_;
  int64_t log_file_id_;
  bool period_scan_;
  ObLogDirEntryOperation dir_op_;
  int64_t offset_;
  int64_t parse_count_;
  char slog_dir_[common::MAX_PATH_SIZE];
};

}
}

#endif
