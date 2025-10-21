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

#ifndef OCEANBASE_COMMON_OB_LOG_READER_
#define OCEANBASE_COMMON_OB_LOG_READER_

#include "lib/atomic/ob_atomic.h"
#include "common/log/ob_log_entry.h"
#include "common/log/ob_single_log_reader.h"
#include "common/log/ob_log_cursor.h"

namespace oceanbase
{
namespace common
{
/**
 * Log reading class, starting from a specified log id, when the end of the log file is encountered, open the next log file
 * There are mainly two usage methods:
 *   1. When Master starts, replay logs, replay is complete when all logs are read
 *   2. Slave's log replay thread usage: Slave uses one thread to synchronize logs, another thread to replay logs,
 *      the replay thread keeps chasing the log file, when it reads the end of the log, it should wait for a short period of time before reading the log again
 */
class ObLogReader
{
public:
  static const int64_t WAIT_TIME = 1000000; //us
  static const int FAIL_TIMES = 60;
public:
  ObLogReader();
  virtual ~ObLogReader();

  /**
   * Initialization
   * @param [in] reader Unit to read a single file
   * @param [in] log_dir Log directory
   * @param [in] log_file_id_start Starting file ID for log reading
   * @param [in] log_seq Sequence of the previous log for reading
   * @param [in] is_wait Whether to retry when opening a file or reading data fails
   */

  /**
   * @brief Read log
   * When encountering a SWITCH_LOG log, directly open the next log file
   * When opening the next log file, if the file does not exist, it may be that the place generating logs is switching files
   * Wait for 1ms and retry, retry up to 10 times, if it still does not exist then report an error
   * @return OB_SUCCESS Success
   * @return OB_READ_NOTHING No content read, possibly because the log has ended,
   *                         or possibly because the log is being generated, read intermediate state data
   *                         The caller handles differently based on their own logic
   * @return otherwise Failure
   */
  int read_log(LogCommand &cmd, uint64_t &seq, char *&log_data, int64_t &data_len);
  //Reopen a new file and locate the next bit in the current log
  inline void set_max_log_file_id(uint64_t max_log_file_id);
  inline uint64_t get_max_log_file_id() const;
  inline bool get_has_max() const;
  inline void set_has_no_max();
  inline uint64_t get_cur_log_file_id() const;
  inline uint64_t get_cur_log_file_id();
  inline uint64_t get_last_log_seq_id();
  inline uint64_t get_last_log_offset();
  inline int get_cursor(common::ObLogCursor &cursor);
  inline int get_next_cursor(common::ObLogCursor &cursor);
private:
  int open_log_(const uint64_t log_file_id, const uint64_t last_log_seq = 0);
  int read_log_(LogCommand &cmd, uint64_t &log_seq, char *&log_data, int64_t &data_len);

private:
  uint64_t cur_log_file_id_;
  uint64_t cur_log_seq_id_;
  uint64_t max_log_file_id_;
  ObSingleLogReader *log_file_reader_;
  bool is_inited_;
  bool is_wait_;
  bool has_max_;
};

inline void ObLogReader::set_max_log_file_id(uint64_t max_log_file_id)
{
  (void)ATOMIC_TAS(&max_log_file_id_, max_log_file_id);
  has_max_ = true;
}

inline uint64_t ObLogReader::get_max_log_file_id() const
{
  return max_log_file_id_;
}

inline bool ObLogReader::get_has_max() const
{
  return has_max_;
}

inline void ObLogReader::set_has_no_max()
{
  has_max_ = false;
}

inline uint64_t ObLogReader::get_cur_log_file_id() const
{
  return cur_log_file_id_;
}

inline uint64_t ObLogReader::get_cur_log_file_id()
{
  return cur_log_file_id_;
}

inline uint64_t ObLogReader::get_last_log_seq_id()
{
  return cur_log_seq_id_;
}

inline uint64_t ObLogReader::get_last_log_offset()
{
  return NULL != log_file_reader_ ? log_file_reader_->get_last_log_offset() : 0;
}

inline int ObLogReader::get_cursor(common::ObLogCursor &cursor)
{
  int ret = OB_SUCCESS;
  if (NULL == log_file_reader_) {
    ret = OB_READ_NOTHING;
  } else {
    cursor.file_id_ = cur_log_file_id_;
    cursor.log_id_ = cur_log_seq_id_;
    cursor.offset_ = log_file_reader_->get_last_log_offset();
  }
  return ret;
}

inline int ObLogReader::get_next_cursor(common::ObLogCursor &cursor)
{
  int ret = OB_SUCCESS;
  if (NULL == log_file_reader_) {
    ret = OB_READ_NOTHING;
  } else {
    cursor.file_id_ = cur_log_file_id_;
    cursor.log_id_ = cur_log_seq_id_  + 1;
    cursor.offset_ = log_file_reader_->get_last_log_offset();
  }
  return ret;
}

} // end namespace common
} // end namespace oceanbase

#endif // OCEANBASE_COMMON_OB_LOG_READER_
