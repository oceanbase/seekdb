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

#ifndef OCEANBASE_LOGSERVICE_LOG_DEFINE_
#define OCEANBASE_LOGSERVICE_LOG_DEFINE_
#include <cstdint>                                       // UINT64_MAX
#include <string.h>                                      // strncmp...
#include <dirent.h>                                      // dirent
#include <fcntl.h>                                       // O_RDONLY, O_RDWR, O_SYNC
#ifdef __APPLE__
// macOS doesn't support O_DIRECT, define it as 0 (no-op)
#ifndef O_DIRECT
#define O_DIRECT 0
#endif
#endif
#ifdef _WIN32
// Windows doesn't support O_DIRECT and O_SYNC
#ifndef O_DIRECT
#define O_DIRECT 0
#endif
#ifndef O_SYNC
#define O_SYNC 0
#endif
#endif
#include "lib/ob_errno.h"                                // errno
#include "lib/utility/ob_print_utils.h"                  // databuff_printf
#include "lib/container/ob_fixed_array.h"                // ObFixedArray
#include "share/ob_force_print_log.h"                    // force_print
#include "lib/time/ob_clock_generator.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}

namespace share
{
class SCN;
}
namespace palf
{
#define TMP_SUFFIX ".tmp"

#define PALF_EVENT(info_string, args...) FLOG_INFO("[PALF_EVENT] " info_string, args)

#define PALF_REPORT_INFO_KV(args...) \
const int64_t MAX_INFO_LENGTH = 512; \
char EXTRA_INFOS[MAX_INFO_LENGTH]; \
int64_t pos = 0; \
::oceanbase::common::databuff_print_kv(EXTRA_INFOS, MAX_INFO_LENGTH, pos, ##args); \

typedef int FileDesc;
typedef uint64_t block_id_t ;
typedef uint64_t offset_t;
class LSN;
class LogWriteBuf;
class ILogBlockPool;

// ==================== palf env start =============================
const int64_t MIN_DISK_SIZE_PER_PALF_INSTANCE = 512 * 1024 * 1024ul;
// =====================palf env end ===============================

// ==================== block and log start ========================
constexpr offset_t MAX_LOG_HEADER_SIZE = 4 * 1024;
constexpr offset_t MAX_INFO_BLOCK_SIZE = 4 * 1024;
constexpr offset_t MAX_META_ENTRY_SIZE = 4 * 1024;
constexpr offset_t MAX_LOG_BODY_SIZE = 3 * 1024 * 1024 + 512 * 1024;                 // The max size of one log body is 3.5MB.

constexpr offset_t MAX_NORMAL_LOG_BODY_SIZE = 2 * 1024 * 1024 + 16 * 1024;
const int64_t PALF_PHY_BLOCK_SIZE = 1 << 26;                                        // 64MB
const int64_t PALF_BLOCK_SIZE = PALF_PHY_BLOCK_SIZE - MAX_INFO_BLOCK_SIZE;          // log block size is 64M-MAX_INFO_BLOCK_SIZE by default.
const int64_t PALF_META_BLOCK_SIZE = PALF_PHY_BLOCK_SIZE - MAX_INFO_BLOCK_SIZE;     // meta block size is 64M-MAX_INFO_BLOCK_SIZE by default.
const int64_t DEFAULT_LOG_UTL_THRESHOLD = 80;

constexpr int64_t CLOG_FILE_TAIL_PADDING_TRIGGER = 4096;     // Threshold for padding the remaining space at the end of the file
// The valid group_entry (not padding entry) size range is:
//    (0, (MAX_LOG_BODY_SIZE + MAX_LOG_HEADER_SIZE) ).
// The padding group_entry size range is:
//    [4KB, (max_valid_group_entry_size + CLOG_FILE_TAIL_PADDING_TRIGGER) ).
// So the MAX_LOG_BUFFER_SIZE is defined as below:
constexpr offset_t MAX_LOG_BUFFER_SIZE = MAX_LOG_BODY_SIZE + MAX_LOG_HEADER_SIZE + CLOG_FILE_TAIL_PADDING_TRIGGER;        // max size of the log buffer is (3.5MB + 4KB + 4KB)

constexpr offset_t LOG_DIO_ALIGN_SIZE = 4 * 1024;
constexpr offset_t LOG_DIO_ALIGNED_BUF_SIZE_REDO = MAX_LOG_BUFFER_SIZE + LOG_DIO_ALIGN_SIZE;
constexpr offset_t LOG_DIO_ALIGNED_BUF_SIZE_META = MAX_META_ENTRY_SIZE + LOG_DIO_ALIGN_SIZE;
const block_id_t LOG_INITIAL_BLOCK_ID = 0;
constexpr block_id_t LOG_MAX_BLOCK_ID = UINT64_MAX/PALF_BLOCK_SIZE - 1;
constexpr block_id_t LOG_INVALID_BLOCK_ID = LOG_MAX_BLOCK_ID + 1;
typedef common::ObFixedArray<share::SCN, ObIAllocator> SCNArray;
typedef common::ObFixedArray<LSN, ObIAllocator> LSNArray;
typedef common::ObFixedArray<LogWriteBuf *, ObIAllocator> LogWriteBufArray;
// ==================== block and log end ===========================

// ====================== Local log state begin =====================
constexpr int64_t DEFAULT_GROUP_BUFFER_SIZE = 1 << 22;
const int64_t PALF_STAT_PRINT_INTERVAL_US = 1 * 1000 * 1000L;
const int64_t PALF_IO_STAT_PRINT_INTERVAL_US = 10 * 1000 * 1000L;
const int64_t MATCH_LSN_ADVANCE_DELAY_THRESHOLD_US = 1 * 1000 * 1000L;
const int32_t PALF_MAX_REPLAY_TIMEOUT = 500 * 1000;
const int32_t DEFAULT_LOG_LOOP_INTERVAL_US = 100 * 1000;                            // 100ms
const int32_t LOG_LOOP_INTERVAL_FOR_PERIOD_FREEZE_US = 1 * 1000;                       // 1ms
const int64_t PALF_SLIDING_WINDOW_SIZE = 1 << 11;                                   // must be 2^n(n>0), default 2^11 = 2048
const int64_t PALF_MAX_SUBMIT_LOG_COUNT = PALF_SLIDING_WINDOW_SIZE / 2;
const int64_t FIRST_VALID_LOG_ID = 1;  // The first valid log_id is 1.
const int64_t PALF_DUMP_DEBUG_INFO_INTERVAL_US = 10 * 1000 * 1000;                  // 10s
constexpr char PADDING_LOG_CONTENT_CHAR = '\0';
const int64_t MIN_WRITING_THTOTTLING_TRIGGER_PERCENTAGE = 40;
constexpr int64_t PALF_IO_WAIT_EVENT_TIMEOUT_MS = 100;

// ====================== Local log state end ========================

// =========== LSN begin ==============
const uint64_t LOG_INVALID_LSN_VAL = UINT64_MAX;
const uint64_t LOG_MAX_LSN_VAL = LOG_INVALID_LSN_VAL - 1;
const uint64_t PALF_INITIAL_LSN_VAL = 0;
// =========== LSN end ==============

// =========== Disk io start ==================
constexpr int LOG_READ_FLAG = O_RDONLY | O_DIRECT | O_SYNC;
constexpr int LOG_WRITE_FLAG = O_RDWR | O_DIRECT | O_SYNC;
constexpr mode_t FILE_OPEN_MODE = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
// =========== Disk io end ====================


// =========== BatchRPC start ==================
// NOTE: ORDER AND VALUE ARE VITAL, DO NOT CHANGE
// =========== BatchRPC end  ==================

// ========== LogCache start =================
constexpr offset_t LOG_CACHE_ALIGN_SIZE = 64 * 1024;
// ========== LogCache end =================

enum LogState {
  INVALID_STATE = 0,
  INIT = 1,
  ACTIVE = 2,
  RECOVERING = 3,
};
inline const char *log_state_to_string(const LogState state)
{
  #define CHECK_LOG_STATE(x) case(LogState::x): return #x
  switch (state)
  {
    CHECK_LOG_STATE(INIT);
    CHECK_LOG_STATE(ACTIVE);
    CHECK_LOG_STATE(RECOVERING);
    default:
      return "INVALID_STATE";
  }
  #undef CHECK_LOG_STATE
}

enum LogType
{
  LOG_UNKNOWN = 0,
  LOG_SUBMIT = 201,
  LOG_PADDING = 301,
  // max value of log_type
  LOG_TYPE_MAX  = 1000
};

inline bool is_valid_log_id(const int64_t log_id)
{
  return (log_id > 0);
}

inline bool is_valid_block_id(block_id_t  block_id)
{
  return block_id >= 0 && block_id < LOG_MAX_BLOCK_ID;
}

inline bool is_tmp_block(const char *block_name)
{
  bool bool_ret = false;
  if (NULL != block_name && NULL != strstr(block_name, TMP_SUFFIX)) {
    bool_ret = true;
  }
  return bool_ret;
}

inline int convert_to_tmp_block(const char *log_dir,
                               const block_id_t  block_id,
                               char *buf,
                               const int64_t buf_len)
{
  int64_t pos = 0;
  return databuff_printf(buf, buf_len, pos, "%s/%lu%s", log_dir,
          block_id, TMP_SUFFIX);
}

inline int convert_to_normal_block(const char *log_dir,
                                   const block_id_t  block_id,
                                   char *buf,
                                   const int64_t buf_len)
{
  int64_t pos = 0;
  return databuff_printf(buf, buf_len, pos, "%s/%lu", log_dir, block_id);
}

struct TimeoutChecker
{
  explicit TimeoutChecker(const int64_t timeout_us)
      : begin_time_us_(common::ObTimeUtility::current_time()), timeout_us_(timeout_us) { }
  ~TimeoutChecker() { }
  void reset()
  {
    begin_time_us_ = common::ObTimeUtility::current_time();
  }

  int operator()()
  {
    int ret = OB_SUCCESS;
    if ((common::ObTimeUtility::current_time() - begin_time_us_ >= timeout_us_)) {
      ret = OB_TIMEOUT;
    }
    return ret;
  }

  int64_t begin_time_us_;
  int64_t timeout_us_;
};

inline bool palf_reach_time_interval(const int64_t interval, int64_t &warn_time)
{
  bool bool_ret = false;
  if ((ObClockGenerator::getClock() - warn_time >= interval) ||
      common::OB_INVALID_TIMESTAMP == warn_time) {
    warn_time = ObClockGenerator::getClock();
    bool_ret = true;
  }
  return bool_ret;
}

inline bool is_valid_file_desc(const FileDesc &fd)
{
  return 0 <= fd;
}

int block_id_to_string(const block_id_t block_id,
                       char *str,
                       const int64_t str_len);
int block_id_to_tmp_string(const block_id_t block_id,
                           char *str,
                           const int64_t str_len);

int construct_absolute_block_path(const char *dir_path, const block_id_t block_id, const int64_t buf_len, char *absolute_block_path);
int construct_absolute_tmp_block_path(const char *dir_path, const block_id_t block_id, const int64_t buf_len, char *absolute_tmp_block_path);
int convert_sys_errno();

bool is_number(const char *);

enum PurgeThrottlingType
{
  INVALID_PURGE_TYPE = 0,
  PURGE_BY_CHECK_BARRIER_CONDITION = 1,
  MAX_PURGE_TYPE
};

inline const char *purge_throttling_type_2_str(const PurgeThrottlingType type)
{
#define EXTRACT_PURGE_TYPE(type_var) case(type_var): return #type_var
  switch(type)
  {
    EXTRACT_PURGE_TYPE(INVALID_PURGE_TYPE);
    EXTRACT_PURGE_TYPE(PURGE_BY_CHECK_BARRIER_CONDITION);

    default:
      return "Invalid Type";
  }
#undef EXTRACT_PURGE_TYPE
}

} // end namespace palf
} // end namespace oceanbase

#endif
