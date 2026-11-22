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

#ifndef OCEANBASE_SHARE_LOG_TYPES_PALF_OPTIONS_
#define OCEANBASE_SHARE_LOG_TYPES_PALF_OPTIONS_
#include <stdint.h>
namespace oceanbase
{
namespace palf
{
// Following disk options can be set to Palf.
// log_disk_usage_limit_size_ is a soft target for checkpoint and recycling.
struct PalfDiskOptions
{
  PalfDiskOptions() : log_disk_usage_limit_size_(-1) {}
  ~PalfDiskOptions() { reset(); }
  static constexpr int64_t MB = 1024*1024ll;
  void reset();
  bool is_valid() const;
  bool operator==(const PalfDiskOptions &rhs) const;
  bool operator!=(const PalfDiskOptions &rhs) const;
  PalfDiskOptions &operator=(const PalfDiskOptions &other);
  int64_t log_disk_usage_limit_size_;
  TO_STRING_KV("log_disk_size(MB)", log_disk_usage_limit_size_ / MB);
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
    TO_STRING_KV(K(need_nonblock));
};
// APPEND: In this mode, PALF assigns LSN and TS to the logs to be committed
enum class AccessMode {
  INVALID_ACCESS_MODE = 0,
  APPEND = 1,
};

inline int access_mode_to_string(const AccessMode access_mode, char *str_buf_, const int64_t str_len)
{
  int ret = OB_SUCCESS;
  if (AccessMode::APPEND == access_mode) {
    strncpy(str_buf_, "APPEND", str_len);
  } else {
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}

int get_access_mode(const common::ObString &str, AccessMode &mode);

inline bool is_valid_access_mode(const AccessMode &access_mode)
{
  return AccessMode::APPEND == access_mode;
}

struct PalfOptions
{
  PalfOptions() : disk_options_()
  {}
  ~PalfOptions() { reset(); }
  void reset();
  bool is_valid() const;
  TO_STRING_KV(K(disk_options_));
public:
  PalfDiskOptions disk_options_;
};

} // end namespace palf
} // end namspace oceanbase
#endif
