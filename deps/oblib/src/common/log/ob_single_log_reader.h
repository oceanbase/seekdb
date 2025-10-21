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

#ifndef OCEANBASE_COMMON_OB_SINGLE_LOG_READER_
#define OCEANBASE_COMMON_OB_SINGLE_LOG_READER_

#include "lib/allocator/ob_malloc.h"
#include "lib/file/ob_file.h"
#include "lib/oblog/ob_log.h"
#include "lib/ob_define.h"
#include "common/data_buffer.h"
#include "common/log/ob_log_entry.h"

namespace oceanbase
{
namespace common
{
class ObSingleLogReader
{
public:
  static const int64_t LOG_BUFFER_MAX_LENGTH;
public:
  ObSingleLogReader();
  virtual ~ObSingleLogReader();

  /**
   * @brief ObSingleLogReader initialization
   * ObSingleLogReader must call the init function for initialization before calling open and read_log
   * During initialization, a read buffer of length LOG_BUFFER_MAX_LENGTH will be allocated
   * The read buffer will be released in the destructor
   * @param [in] log_dir log directory
   * @return OB_SUCCESS initialization successful
   * @return OB_INIT_TWICE already initialized
   * @return OB_ERROR initialization failed
   */

  /**
   * @brief Open a file
   * open function will open the log file
   * After calling the close function to close the log file, you can call the open function again to open other log files, buffer is reused
   * @param [in] file_id The id of the operation log file to read
   * @param [in] last_log_seq The sequence number of the previous log, used to determine if the logs are continuous, default value 0 indicates invalid
   */
  int open(const uint64_t file_id, const uint64_t last_log_seq = 0);

  /**
   * @brief Close the log file
   * Close the already opened log file, after which the init function can be called again to read other log files
   */
  int close();

  /**
   * @brief Reset internal state, release buffer memory
   */

  /**
   * @brief Read an update operation from the operation log
   * @param [out] cmd Log type
   * @param [out] log_seq Log sequence number
   * @param [out] log_data Log content
   * @param [out] data_len Buffer length
   * @return OB_SUCCESS: If successful;
   *         OB_READ_NOTHING: No data read from the file
   *         others: An error occurred.
   */
  virtual int read_log(LogCommand &cmd, uint64_t &log_seq, char *&log_data, int64_t &data_len) = 0;
  inline uint64_t get_cur_log_file_id() const
  {
    return file_id_;
  }
  inline uint64_t get_last_log_seq_id() const
  {
    return last_log_seq_;
  }
  inline uint64_t get_last_log_offset() const
  {
    return pos_;
  }

  /// @brief is log file opened
  inline bool is_opened() const { return file_.is_opened(); }

  ///When @brief is initialized, get the maximum log file number in the current directory
  int64_t get_cur_offset() const;

  inline void unset_dio() { dio_ = false; }

protected:
  int trim_last_zero_padding(int64_t header_size);
protected:
  /**
   * Read data from the log file into the read buffer
   * @return OB_SUCCESS: if successful;
   *         OB_READ_NOTHING: no data was read from the file
   *         others: an error occurred.
   */
  int read_log_();
protected:
  bool is_inited_;  //Initialization tag
  uint64_t file_id_;  //Log file id
  uint64_t last_log_seq_;  //Last log (Mutator) serial number
  ObDataBuffer log_buffer_;  //Read buffer
  char file_name_[OB_MAX_FILE_NAME_LENGTH];  //Log file name
  char log_dir_[OB_MAX_FILE_NAME_LENGTH];  //Log directory
  int64_t pos_;
  int64_t pread_pos_;
  ObFileReader file_;
  bool dio_;
};
} // end namespace common
} // end namespace oceanbase

#endif // OCEANBASE_COMMON_OB_SINGLE_LOG_READER_
