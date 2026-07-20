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

#ifndef OCEANBASE_LOGSERVICE_LOG_IO_CONTEXT_
#define OCEANBASE_LOGSERVICE_LOG_IO_CONTEXT_
#include <cstdint>
#include "lib/utility/ob_print_utils.h"
#include "log_iterator_info.h"

namespace oceanbase
{
namespace palf
{
enum class LogIOUser {
  DEFAULT = 0,
  REPLAY = 1,
  FETCHLOG = 2,
  ARCHIVE = 3,
  RESTORE = 4,
  CHANGE_STREAM = 5,
  STANDBY = 6,
  SHARED_UPLOAD = 7,
  META_INFO = 8,
  RESTART = 9,
  OTHER = 10,
};

inline const char *log_io_user_str(const LogIOUser user_type)
{
  #define USER_TYPE_STR(x) case(LogIOUser::x): return #x
  switch (user_type)
  {
    USER_TYPE_STR(DEFAULT);
    USER_TYPE_STR(REPLAY);
    USER_TYPE_STR(FETCHLOG);
    USER_TYPE_STR(ARCHIVE);
    USER_TYPE_STR(RESTORE);
    USER_TYPE_STR(CHANGE_STREAM);
    USER_TYPE_STR(STANDBY);
    USER_TYPE_STR(SHARED_UPLOAD);
    USER_TYPE_STR(META_INFO);
    USER_TYPE_STR(RESTART);
    USER_TYPE_STR(OTHER);
    default:
      return "Invalid";
  }
  #undef USER_TYPE_STR
}

class LogIOContext
{
public:
  LogIOContext();
  // do not get group_id
  LogIOContext(const LogIOUser &user);
  ~LogIOContext() { destroy(); }
  bool is_valid() const
  {
    return true;
  }
  void destroy()
  {
    user_ = LogIOUser::DEFAULT;
    iterator_info_.reset();
  }
  LogIOContext &operator=(const LogIOContext &io_ctx)
  {
    if (&io_ctx != this) {
      this->user_ = io_ctx.user_;
      this->iterator_info_ = io_ctx.iterator_info_;
    }
    return *this;
  }
  void set_start_lsn(const LSN &start_lsn) { iterator_info_.set_start_lsn(start_lsn); }
  LogIteratorInfo *get_iterator_info() { return &iterator_info_; }
  void inc_read_io_cnt() 
  { 
    iterator_info_.inc_read_io_cnt();
  }
  void inc_read_io_size(const int64_t read_size) 
  {
    iterator_info_.inc_read_io_size(read_size);
  }
  void inc_read_disk_cost_ts(const int64_t cost_ts)
  {
    iterator_info_.inc_read_disk_cost_ts(cost_ts);
  }
  void inc_cache_hit_cnt()
  {
    iterator_info_.inc_cache_hit_cnt();
  }
  void inc_cache_miss_cnt()
  {
    iterator_info_.inc_cache_miss_cnt();
  }
  void inc_cache_read_size(const int64_t read_size)
  {
    iterator_info_.inc_cache_read_size(read_size);
  }
  TO_STRING_KV("user", log_io_user_str(user_), K_(iterator_info));
private:
  LogIOUser user_;
  LogIteratorInfo iterator_info_;
};
}
}
#endif
