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

#ifndef OCEANBASE_STORAGE_TMP_FILE_OB_TMP_FILE_THREAD_JOB_H_
#define OCEANBASE_STORAGE_TMP_FILE_OB_TMP_FILE_THREAD_JOB_H_

#include "lib/lock/ob_thread_cond.h"
#include "lib/queue/ob_link_queue.h"
#include "lib/utility/ob_print_utils.h"
#include "storage/tmp_file/ob_tmp_file_global.h"

namespace oceanbase
{
namespace tmp_file
{

class ObTmpFileSwapJob : public ObSpLinkQueue::Link
{
public:
  static const uint32_t DEFAULT_TIMEOUT_MS = 10 * 1000;
  ObTmpFileSwapJob()
    : is_inited_(false),
      ret_code_(OB_SUCCESS),
      is_finished_(false),
      timeout_ms_(DEFAULT_TIMEOUT_MS),
      create_ts_(0),
      abs_timeout_ts_(0),
      expect_swap_size_(0),
      swap_cond_() {}
  ~ObTmpFileSwapJob() { reset(); }
  int init(int64_t expect_swap_size, uint32_t timeout_ms = DEFAULT_TIMEOUT_MS);
  void reset();
  int wait_swap_complete();
  int signal_swap_complete(int ret_code);
  OB_INLINE int64_t get_create_ts() const { return create_ts_; }
  OB_INLINE int64_t get_abs_timeout_ts() const { return abs_timeout_ts_; }
  OB_INLINE int64_t get_expect_swap_size() const { return expect_swap_size_; }
  OB_INLINE bool is_valid() { return ATOMIC_LOAD(&is_inited_) && swap_cond_.is_inited(); }
  OB_INLINE bool is_finished() const { return ATOMIC_LOAD(&is_finished_); }
  OB_INLINE bool is_inited() const { return ATOMIC_LOAD(&is_inited_); }
  OB_INLINE int get_ret_code() const { return ATOMIC_LOAD(&ret_code_); }
  TO_STRING_KV(KP(this), K(is_inited_), K(is_finished_), K(create_ts_), K(timeout_ms_), K(abs_timeout_ts_), K(expect_swap_size_));
private:
  bool is_inited_;
  int ret_code_;
  bool is_finished_;
  uint32_t timeout_ms_;
  int64_t create_ts_;
  int64_t abs_timeout_ts_;
  int64_t expect_swap_size_;  // in bytes
  ObThreadCond swap_cond_;
};

// record statistics for flush tasks in flushTG thread
class ObTmpFileFlushMonitor
{
public:
  ObTmpFileFlushMonitor()
    : flush_task_cnt_(0),
      total_flushing_data_length_(0),
      max_flush_data_length_(-1),
      min_flush_data_length_(INT64_MAX),
      f1_cnt_(0),
      f2_cnt_(0),
      f3_cnt_(0),
      f4_cnt_(0),
      f5_cnt_(0) {}
  void reset();
  void print_statistics();
  void record_flush_stage(const ObTmpFileGlobal::FlushCtxState flush_stage);
  void record_flush_task(const int64_t data_length);
private:
  int64_t flush_task_cnt_;
  int64_t total_flushing_data_length_;
  int64_t max_flush_data_length_;
  int64_t min_flush_data_length_;
  int64_t f1_cnt_;
  int64_t f2_cnt_;
  int64_t f3_cnt_;
  int64_t f4_cnt_;
  int64_t f5_cnt_;
};

// record statistics for swap tasks in swapTG thread
class ObTmpFileSwapMonitor
{
public:
  ObTmpFileSwapMonitor()
    : swap_task_cnt_(0),
      swap_total_response_time_(0),
      swap_max_response_time_(-1),
      swap_min_response_time_(INT64_MAX) {}
  void reset();
  void print_statistics();
  void record_swap_response_time(const int64_t response_time);
private:
  int64_t swap_task_cnt_;
  int64_t swap_total_response_time_;
  int64_t swap_max_response_time_;
  int64_t swap_min_response_time_;
};

}  // end namespace tmp_file
}  // end namespace oceanbase
#endif // OCEANBASE_STORAGE_TMP_FILE_OB_TMP_FILE_THREAD_JOB_H_
