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

#ifndef OCEANBASE_LOGSERVICE_LOG_LOG_BUFFER_
#define OCEANBASE_LOGSERVICE_LOG_LOG_BUFFER_

#include "lib/atomic/atomic128.h"
#include "lib/utility/ob_macro_utils.h"
#include "log_define.h"
#include "lsn.h"

namespace oceanbase
{
namespace palf
{
class LogWriteBuf;
class LogGroupBuffer
{
public:
  LogGroupBuffer();
  ~LogGroupBuffer();
public:
  int init(const LSN &start_lsn);
  void reset();
  void destroy();
  //
  // Function: Fill the log body into the aggregation buffer
  //
  // @param [in] lsn, starting offset for filling in the aggregation buffer
  // @param [in] data, data content pointer
  // @param [in] data_len, data length
  // @param [in] cb, callback object pointer
  //
  // return code:
  //      OB_SUCCESS
  int fill(const LSN &lsn,
           const char *data,
           const int64_t data_len);
  int fill_padding_body(const LSN &lsn,
                        const char *data,
                        const int64_t data_len,
                        const int64_t log_body_size);
  int get_log_buf(const LSN &lsn, const int64_t total_len, LogWriteBuf &log_buf);
  bool can_handle_new_log(const LSN &lsn,
                          const int64_t total_len) const;
  bool can_handle_new_log(const LSN &lsn,
                          const int64_t total_len,
                          const LSN &ref_reuse_lsn) const;
  int check_log_buf_wrapped(const LSN &lsn, const int64_t log_len, bool &is_buf_wrapped) const;
  int64_t get_available_buffer_size() const;
  int64_t get_reserved_buffer_size() const;
  int to_leader();
  int to_follower();
  // inc update readable_begin_lsn, used by append_disk_log().
  int inc_update_readable_begin_lsn(const LSN &new_lsn);
  // inc update reuse_lsn, used for flush log cb case.
  int inc_update_reuse_lsn(const LSN &new_reuse_lsn);
  void get_reuse_lsn(LSN &reuse_lsn) const { return get_reuse_lsn_(reuse_lsn); }
  // Used for truncating log / truncating for rebuild.
  int truncate(const LSN &new_lsn);
  //
  // read log data from group buffer
  //
  // @param [in] read_begin_lsn, the read begin lsn
  // @param [in] in_read_size, the expected read size
  // @param [in] buf, the data buf for read
  // @param [out] out_read_size, the successful read size of data
  //
  // return code:
  //    - OB_INVALID_ARGUMENT, the lsn is invalid or unexpected
  //    - OB_ERR_OUT_OF_LOWER_BOUND, read_begin_lsn < readable_begin_lsn_
  //    - OB_SUCCESS, read successfully
  int read_data(const LSN &read_begin_lsn,
                const int64_t in_read_size,
                char *buf,
                int64_t &out_read_size) const;
  TO_STRING_KV("log_group_buffer: start_lsn", start_lsn_, "reuse_lsn", reuse_lsn_, "reserved_buffer_size",
      reserved_buffer_size_, "available_buffer_size", available_buffer_size_, "readable_begin_lsn", readable_begin_lsn_);
private:
  int get_buffer_pos_(const LSN &lsn, int64_t &start_pos) const;
  void get_buffer_start_lsn_(LSN &start_lsn) const;
  void get_reuse_lsn_(LSN &reuse_lsn) const;
  void get_start_lsn_(LSN &lsn) const;
  void gen_readable_begin_lsn_for_filling_(const LSN &lsn,
                                           LSN &new_readable_begin_lsn) const;
  void inc_update_readable_begin_lsn_(const LSN &new_readable_begin_lsn);
  void get_readable_begin_lsn_(LSN &readable_begin_lsn) const;
  int fill_(const LSN &lsn,
            const int64_t start_pos,
            const char *data,
            const int64_t data_len);
private:
  // buffer start position corresponding lsn
  LSN start_lsn_;
  // buffer reusable start point corresponding lsn, which is expected to be equal to max_flushed_end_lsn eventually.
  // All logic that updates max_flushed_end_lsn should also consider updating this value.
  LSN reuse_lsn_;
  // lock for truncate operation.
  mutable common::ObSpinLock truncate_lock_;
  // This field is used for recording the readable begin lsn.
  // It won't fallback.
  LSN readable_begin_lsn_;
  // allocated buffer size
  int64_t reserved_buffer_size_;
  // current available buffer size
  int64_t available_buffer_size_;
  // buffer pointer
  char *data_buf_;
  bool is_inited_;
private:
  DISALLOW_COPY_AND_ASSIGN(LogGroupBuffer);
};
}
}
#endif // OCEANBASE_LOGSERVICE_LOG_LOG_BUFFER_
