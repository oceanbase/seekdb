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

#ifndef OCEANBASE_COMMON_OB_ASYNC_LOG_STRUCT_
#define OCEANBASE_COMMON_OB_ASYNC_LOG_STRUCT_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <deque>
#include <algorithm>
#include <string>
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/oblog/ob_base_log_writer.h"
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace common
{
enum ObPLogFDType {
  FD_SVR_FILE = 0,
  FD_TRACE_FILE,
  FD_AUDIT_FILE,
  FD_ALERT_FILE,
  MAX_FD_FILE,
};

//program log
class ObPLogItem : public ObIBaseLogItem
{
public:
  ObPLogItem();
  virtual ~ObPLogItem() {}
  virtual char *get_buf() { return buf_; }
  virtual const char *get_buf() const { return buf_; }
  virtual int64_t get_buf_size() const { return buf_size_; }
  virtual int64_t get_data_len() const { return pos_; }
  virtual int64_t get_header_len() const { return header_pos_; }

  int32_t get_tl_type() const { return tl_type_; }
  void set_tl_type(const int32_t tl_type) { tl_type_ = tl_type;}
  bool is_force_allow() const { return is_force_allow_; }
  void set_force_allow(const bool flag) { is_force_allow_ = flag;}

  bool is_size_overflow() const { return is_size_overflow_; }
  void set_size_overflow() { is_size_overflow_ = true;}
  void reset_size_overflow() { is_size_overflow_ = false;}
  void set_buf_size(const int64_t buf_size) { buf_size_ = buf_size;}
  void set_data_len(const int64_t len) { pos_ = len;}
  void set_header_len(const int64_t len) { header_pos_ = len;}
  int32_t get_log_level() const { return log_level_; }
  void set_log_level(const int32_t log_level) { log_level_ = log_level; }
  int64_t get_timestamp() const { return timestamp_; }
  void set_timestamp(const timeval &tv)
  {
    timestamp_ = static_cast<int64_t>(tv.tv_sec) * static_cast<int64_t>(1000000) + static_cast<int64_t>(tv.tv_usec);
  }
  void set_timestamp(const int64_t &value_us)
  {
    timestamp_ = value_us;
  }
  ObPLogFDType get_fd_type() const { return fd_type_; }
  void set_fd_type(const ObPLogFDType fd_type) { fd_type_ = fd_type;}
  bool is_trace_file() const { return FD_TRACE_FILE == fd_type_; }
  bool is_supported_file() const { return MAX_FD_FILE != fd_type_; }
  bool is_audit_file() const { return FD_AUDIT_FILE == fd_type_; }
  bool is_alert_file() const { return FD_ALERT_FILE == fd_type_; }

private:
  ObPLogFDType fd_type_;
  int32_t log_level_;
  int32_t tl_type_;
  bool is_force_allow_;
  bool is_size_overflow_;
  int64_t timestamp_;
  int64_t header_pos_;
  int64_t buf_size_;
  int64_t pos_;
  char buf_[0];
private:
  DISALLOW_COPY_AND_ASSIGN(ObPLogItem);
};

class ObPLogFileStruct
{
public:
  ObPLogFileStruct();
  virtual ~ObPLogFileStruct() { close_all(); }
  int open(const char *log_file, const bool redirect_flag);
  int reopen(const bool redirect_flag);
  int close_all();
  bool is_opened() { return fd_ > STDERR_FILENO; }
  int64_t get_write_size() const { return write_size_; }
public:
  static const int32_t MAX_LOG_FILE_NAME_SIZE = 256;
  static const mode_t LOG_FILE_MODE = 0644;

  char filename_[MAX_LOG_FILE_NAME_SIZE];
  int32_t fd_;//descriptor of log-file
  uint32_t write_count_;
  int64_t write_size_;
  int64_t file_size_;
  struct stat stat_;
};

} // common
} // oceanbase
#endif //OCEANBASE_COMMON_OB_ASYNC_LOG_STRUCT_
