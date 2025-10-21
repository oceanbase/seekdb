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

#ifndef OCEANBASE_STORAGE_OB_STORAGE_LOG_READER_H_
#define OCEANBASE_STORAGE_OB_STORAGE_LOG_READER_H_

#include "common/data_buffer.h"
#include "common/log/ob_log_cursor.h"
#include "share/redolog/ob_log_file_handler.h"
#include "storage/slog/ob_storage_log_entry.h"
#include "storage/blocksstable/ob_log_file_spec.h"
#include "storage/slog/ob_storage_log_batch_header.h"

namespace oceanbase
{

namespace storage
{
struct ObMetaDiskAddr;

class ObStorageLogReader
{
public:
  ObStorageLogReader();
  virtual ~ObStorageLogReader();
  ObStorageLogReader(const ObStorageLogReader &) = delete;
  ObStorageLogReader &operator = (const ObStorageLogReader &) = delete;

  int init(
      const char *log_dir,
      const common::ObLogCursor cursor,
      const blocksstable::ObLogFileSpec &log_file_spec,
      const uint64_t tenant_id);
  void destroy();

  // iterator read
  int read_log(ObStorageLogEntry &entry, char *&log_data, ObMetaDiskAddr &disk_addr);
  // read targeted log
  static int read_log(
      const char *log_dir,
      const ObMetaDiskAddr &disk_addr,
      const int64_t buf_len,
      void *buf,
      int64_t &pos,
      const uint64_t tenant_id);

  // when replay finishes, replayer will call this func to get the finish cursor
  int get_finish_cursor(common::ObLogCursor &cursor) const;

private:
  int open();
  int close();
  int seek(uint64_t log_seq);
  int get_next_entry(ObStorageLogEntry &entry);
  int get_next_batch_header(ObStorageLogBatchHeader &batch_header);
  int get_next_log(
      ObStorageLogEntry &entry,
      char *&log_data,
      ObMetaDiskAddr &disk_addr);
  int load_buf();

  int check_switch_file();
  int check_and_update_seq_number(const ObStorageLogEntry &entry);

  static int open(const int64_t file_id, ObLogFileHandler &handler);
  static int get_entry(void *buf, ObStorageLogEntry &entry);

private:
  static const int64_t LOG_FILE_MAX_SIZE = 256 << 20;
  bool is_inited_;
  common::ObLogCursor cursor_;
  ObDataBuffer log_buffer_;

  // the offset of the file we read
  int64_t pread_pos_;
  ObLogFileHandler file_handler_;
  int64_t batch_num_;
  int64_t batch_index_;
};

}
}
#endif
