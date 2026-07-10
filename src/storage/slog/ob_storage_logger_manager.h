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

#ifndef OCEANBASE_STORAGE_OB_STORAGE_LOGGER_MANAGER_H_
#define OCEANBASE_STORAGE_OB_STORAGE_LOGGER_MANAGER_H_

#include "storage/slog/ob_storage_logger.h"
#include "storage/blocksstable/ob_log_file_spec.h"
#include "common/log/ob_log_cursor.h"


namespace oceanbase
{
namespace storage
{
class ObStorageLoggerManager final
{
  friend class ObStorageLogger;
public:
  ObStorageLoggerManager();
  ~ObStorageLoggerManager();
  ObStorageLoggerManager(const ObStorageLoggerManager &) = delete;
  ObStorageLoggerManager &operator = (const ObStorageLoggerManager &) = delete;
  int init(
      const char *log_dir,
      const char *data_dir,
      const int64_t max_log_file_size,
      const blocksstable::ObLogFileSpec &log_file_spec);
  int start();
  void stop();
  void wait();
  void destroy();

  int get_server_slogger(ObStorageLogger *&slogger);
  int get_tenant_slog_dir(char (&tenant_clog_dir)[common::MAX_PATH_SIZE]);
  const char *get_root_dir() { return log_dir_; }
  int get_reserved_size(int64_t &reserved_size) const;

private:

  int get_using_disk_space(int64_t &using_space) const;

  int check_log_disk(const char *data_dir, const char *log_dir);

public:
  static constexpr int64_t RESERVED_DISK_SIZE = 128 * 1024 * 1024L; // 128M
private:

  const char *log_dir_;
  int64_t max_log_file_size_;
  bool is_inited_;
  blocksstable::ObLogFileSpec log_file_spec_;

  ObStorageLogger server_slogger_;
  bool need_reserved_;
};

}
}

#endif
