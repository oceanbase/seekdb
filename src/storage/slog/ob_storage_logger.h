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

#ifndef OCEANBASE_STORAGE_OB_STORAGE_LOGGER_H_
#define OCEANBASE_STORAGE_OB_STORAGE_LOGGER_H_

#include "lib/hash/ob_hashset.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/objectpool/ob_pool.h"
#include "lib/oblog/ob_log.h"
#include "lib/lock/ob_drw_lock.h"
#include "common/log/ob_log_generator.h"
#include "common/log/ob_log_data_writer.h"
#include "storage/slog/ob_storage_log_writer.h"
#include "storage/slog/ob_server_slog_writer.h"
#include "storage/slog/ob_storage_log_entry.h"
#include "common/log/ob_log_constants.h"
#include "lib/ob_define.h"

namespace oceanbase
{
namespace storage
{
class ObStorageLoggerManager;

class ObStorageLogger
{
public:
  ObStorageLogger();
  virtual ~ObStorageLogger();
public:
  //NOT thread safe.
  //Init the redo log and do recovery if there is redo logs in log_dir.
  int init(ObStorageLoggerManager &slogger_manager, const uint64_t tenant_id);
  int start();
  void stop();
  void wait();
  void destroy();
  int write_log(ObStorageLogParam &param);
  int write_log(ObIArray<ObStorageLogParam> &param_arr);
  const char *get_dir() { return tnt_slog_dir_; }
  int get_active_cursor(common::ObLogCursor &log_cursor);
  int remove_useless_log_file(const int64_t end_file_id, const uint64_t tenant_id);

  int get_using_disk_space(int64_t &using_space) const;
  int start_log(const common::ObLogCursor &start_cursor);

private:
  static const int64_t MAX_FLUSH_WAIT_TIME_MS = 60 * 1000; // 60s
  static const int64_t MAX_APPEND_WAIT_TIME_MS = 365 * 24 * 3600 * 1000000L; // 1h

private:
  int get_start_file_id(int64_t &start_file_id, const uint64_t tenant_id);

  // construct log item and fill it with single log
  int build_log_item(const ObStorageLogParam &param, ObStorageLogItem *&log_item);
  // construct log item and fill it with multiple logs
  int build_log_item(const ObIArray<ObStorageLogParam> &param_arr, ObStorageLogItem *&log_item);

private:
  bool is_inited_;
  ObStorageLogWriter *log_writer_;
  ObStorageLogWriter tenant_log_writer_;
  ObServerSlogWriter server_log_writer_;
  char tnt_slog_dir_[MAX_PATH_SIZE];
  ObStorageLoggerManager *slogger_mgr_;
  // When we write logs with multiple threads, log_writer_'s cursor may not be the newest
  // In other word, the log_writer_'s cursor is only updated when the backup thread flushes
  // So, we need maintain another variable los_seq_ in this class
  int64_t log_seq_;

  mutable lib::ObMutex build_log_mutex_;
  blocksstable::ObLogFileSpec log_file_spec_;
  bool is_start_;
};


}
}
#endif
