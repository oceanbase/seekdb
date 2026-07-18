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

#ifndef OCEANBASE_LOGSERVICE_PALF_OPTIONS_
#define OCEANBASE_LOGSERVICE_PALF_OPTIONS_
#include "lib/compress/ob_compress_util.h"
#include "share/ob_partition_modify.h"
#include <stdint.h>
namespace oceanbase
{
namespace palf
{
// Following disk options can be set to Palf.
// 1. log_disk_usage_limit_size_, the total log disk space.
// 2. log_disk_utilization_threshold_, log_disklog disk utilization threshold before reuse log files.
// 3. log_disk_utilization_limit_threshold_, maximum of log disk usage percentage before stop submitting or receiving logs.
// 4. log_disk_throttling_percentage_, the threshold of the size of the log disk when writing_limit will be triggered.
// 5. log_writer_parallelism, the number of parallel log writer processes that can be used to write redo log entries to disk.
struct PalfDiskOptions
{
  PalfDiskOptions() : log_disk_usage_limit_size_(-1),
                      log_disk_utilization_threshold_(-1),
                      log_disk_utilization_limit_threshold_(-1),
                      log_disk_throttling_percentage_(-1),
                      log_disk_throttling_maximum_duration_(-1),
                      log_writer_parallelism_(-1)
  {}
  ~PalfDiskOptions() { reset(); }
  static constexpr int64_t MB = 1024*1024ll;
  void reset();
  bool is_valid() const;
  bool operator==(const PalfDiskOptions &rhs) const;
  bool operator!=(const PalfDiskOptions &rhs) const;
  PalfDiskOptions &operator=(const PalfDiskOptions &other);
  int64_t log_disk_usage_limit_size_;
  int log_disk_utilization_threshold_;
  int log_disk_utilization_limit_threshold_;
  int64_t log_disk_throttling_percentage_;
  int64_t log_disk_throttling_maximum_duration_;
  int log_writer_parallelism_;
  TO_STRING_KV("log_disk_size(MB)", log_disk_usage_limit_size_ / MB,
               "log_disk_utilization_threshold(%)", log_disk_utilization_threshold_,
               "log_disk_utilization_limit_threshold(%)", log_disk_utilization_limit_threshold_,
               "log_disk_throttling_percentage(%)", log_disk_throttling_percentage_,
               "log_disk_throttling_maximum_duration(s)", log_disk_throttling_maximum_duration_ / (1000 * 1000),
               "log_writer_parallelism", log_writer_parallelism_);
};


struct PalfAppendOptions
{
    // The user of Palf has two different usage methods when submitting logs:
    //
    // 1. Blocking commit (analogous to the BLOCK semantics of IO system calls). This usage is applicable when the volume of submitted logs exceeds Palf's processing capacity,
    //    Will occupy the thread, in extreme scenarios it may cause the calling thread to "block forever";
    //
    // Advantages: easy to use, no need to handle errors when exceeding processing capacity;
    //
    // Disadvantage: It will occupy the calling thread;
    //
    // Typical use case: redo log for transaction submission;
    //
    // 2. Non-blocking submission (analogous to the NONBLOCK semantics of IO system calls). This usage mode is used when the volume of submitted logs exceeds Palf's processing capacity,
    //    append call returns OB_EAGAIN error code, will not block the calling thread;
    //
    // Advantages: It does not occupy the calling thread;
    //
    // Disadvantage: The caller needs to handle the OB_EAGAIN error;
    //
    // Typical use case: prepare/commit logs for transaction submission, state is advanced by the two-phase state machine after returning OB_EAGAIN;
    //
    // Default value is NONBLOCK
    bool need_nonblock = true;
    bool need_check_proposal_id = true;
    int64_t proposal_id = 0;
    TO_STRING_KV(K(need_nonblock), K(need_check_proposal_id), K(proposal_id));
};
// Palf supports switching between three modes
//
// APPEND: In this mode, PALF assigns LSN and TS to the logs to be committed
//
// RAW_WRITE: In this mode, PALF does not have the capability to allocate LSN and TS for pending logs
//
// FLASHBACK: In this mode, PALF does not have log writing capability, and replicas do not respond to log pull requests
// PREPARE_FLASHBACK: In this mode, PALF does not have log writing capability, and logs can be synchronized among replicas
enum class AccessMode {
  INVALID_ACCESS_MODE = 0,
  APPEND = 1,
  RAW_WRITE = 2,
  FLASHBACK = 3,
  PREPARE_FLASHBACK = 4,
};

inline int access_mode_to_string(const AccessMode access_mode, char *str_buf_, const int64_t str_len)
{
  int ret = OB_SUCCESS;
  if (AccessMode::APPEND == access_mode) {
    strncpy(str_buf_, "APPEND", str_len);
  } else if (AccessMode::RAW_WRITE == access_mode) {
    strncpy(str_buf_, "RAW_WRITE", str_len);
  } else if (AccessMode::FLASHBACK == access_mode) {
    strncpy(str_buf_, "FLASHBACK", str_len);
  } else if (AccessMode::PREPARE_FLASHBACK == access_mode) {
    strncpy(str_buf_, "PREPARE_FLASHBACK", str_len);
  } else {
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}

int get_access_mode(const common::ObString &str, AccessMode &mode);

inline bool is_valid_access_mode(const AccessMode &access_mode)
{
  return AccessMode::APPEND == access_mode
    || AccessMode::RAW_WRITE == access_mode
    || AccessMode::FLASHBACK == access_mode
    || AccessMode::PREPARE_FLASHBACK == access_mode;
}

inline bool can_switch_access_mode_(const AccessMode &src_access_mode, const AccessMode &dst_access_mode)
{
  bool bool_ret = true;
  if (false == is_valid_access_mode(dst_access_mode)) {
    // can not switch to invalid AccessMode
    bool_ret = false;
  } else if (src_access_mode == dst_access_mode) {
    // can not switch to itself
    bool_ret = false;
  } else if (src_access_mode == AccessMode::APPEND &&
      (dst_access_mode == AccessMode::PREPARE_FLASHBACK || dst_access_mode == AccessMode::FLASHBACK)) {
    // can not switch from APPEND to FLASHBACK
    bool_ret = false;
  } else if ((src_access_mode == AccessMode::PREPARE_FLASHBACK || src_access_mode == AccessMode::FLASHBACK) &&
      dst_access_mode == AccessMode::RAW_WRITE) {
    // can not switch from FLASHBACK to RAW_WRITE
    bool_ret = false;
  } else if (src_access_mode == AccessMode::FLASHBACK && dst_access_mode == AccessMode::PREPARE_FLASHBACK) {
    bool_ret = false;
  }
  return bool_ret;
}

struct PalfTransportCompressOptions
{
public:
  PalfTransportCompressOptions() :
    enable_transport_compress_(false),
    transport_compress_func_(ObCompressorType::INVALID_COMPRESSOR)
  {}
  ~PalfTransportCompressOptions() { reset(); }
  void reset();
  bool is_valid() const;
  PalfTransportCompressOptions &operator=(const PalfTransportCompressOptions &other);
public:
  bool enable_transport_compress_;
  ObCompressorType transport_compress_func_;
  TO_STRING_KV(K(enable_transport_compress_),
               K(transport_compress_func_));
};

struct PalfOptions
{
  PalfOptions() : disk_options_(),
                  compress_options_(),
                  rebuild_replica_log_lag_threshold_(0),
                  enable_log_cache_(false)
  {}
  ~PalfOptions() { reset(); }
  void reset();
  bool is_valid() const;
  TO_STRING_KV(K(disk_options_),
               K(compress_options_),
               K(rebuild_replica_log_lag_threshold_),
               K(enable_log_cache_));
public:
  PalfDiskOptions disk_options_;
  PalfTransportCompressOptions compress_options_;
  int64_t rebuild_replica_log_lag_threshold_;
  bool enable_log_cache_;
};

struct PalfThrottleOptions
{
  public:
  PalfThrottleOptions() {reset();}
  ~PalfThrottleOptions() {reset();}
  void reset();
  bool is_valid() const;
  bool operator==(const PalfThrottleOptions &rhs) const;
  // size of available log disk when writing throttling triggered
  inline int64_t get_available_size_after_limit() const;
  inline int64_t get_maximum_duration() const {return maximum_duration_;}
  inline bool need_throttling() const;
  static constexpr int64_t MB = 1024*1024ll;
  TO_STRING_KV("total_disk_space", total_disk_space_ / MB,
               K_(stopping_writing_percentage), K_(trigger_percentage),
               "maximum_duration(s)", maximum_duration_/ (1000 * 1000L),
               "unrecyclable_disk_space(MB)", unrecyclable_disk_space_ / MB);
public:
  int64_t total_disk_space_;
  int64_t stopping_writing_percentage_;
  int64_t trigger_percentage_;
  int64_t maximum_duration_;
  int64_t unrecyclable_disk_space_;
};

inline int64_t PalfThrottleOptions::get_available_size_after_limit() const
{
  return (total_disk_space_ * MAX(0, stopping_writing_percentage_ - trigger_percentage_)) / 100;
}

inline bool PalfThrottleOptions::need_throttling() const
{
  return trigger_percentage_> 0  && total_disk_space_ > 0
      && (trigger_percentage_ < stopping_writing_percentage_)
      && unrecyclable_disk_space_ > (total_disk_space_ * trigger_percentage_ / 100);
}

} // end namespace palf
} // end namspace oceanbase
#endif
