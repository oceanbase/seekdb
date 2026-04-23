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

/**
 * ObOccamTimeGuard follows the Occam's razor principle and value semantics.
 * It only requires the minimum necessary information, and then things will be done.
 *
 * Occam’s razor, also spelled Ockham’s razor, also called law of economy or law of parsimony,
 * principle stated by the Scholastic philosopher William of Ockham (1285–1347/49) that
 * “plurality should not be posited without necessity.”
 * The principle gives precedence to simplicity: of two competing theories,
 * the simpler explanation of an entity is to be preferred.
 * The principle is also expressed as “Entities are not to be multiplied beyond necessity.”
 **/

#ifndef OCEANBASE_LIB_TASK_OB_EASY_TIME_GUARD_H
#define OCEANBASE_LIB_TASK_OB_EASY_TIME_GUARD_H

#include "lib/atomic/ob_atomic.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/literals/ob_literals.h"
#include "share/ob_define.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace common
{

namespace occam
{

// this happens when compile, no runtime cost, zero overhead.
template <typename T, size_t S>
inline constexpr const T *get_file_name_without_dir(const T (& str)[S], size_t i = S - 1)
{
  static_assert(S > 0, "file name char array length must greater than 0.");
  return (str[i] == '/' || str[i] == '\\') ? &str[i + 1] : (i > 0 ? get_file_name_without_dir(str, i - 1) : 0);
}

template <typename T>
inline constexpr const T *get_file_name_without_dir(T (& str)[1]) { return &str[0]; }

class ObOccamTimeGuard
{
public:
  ObOccamTimeGuard(const uint64_t warn_threshold,
                   const char *file,
                   const char *func,
                   const char *mod)
  :warn_threshold_(warn_threshold),
  idx_(0),
  last_click_ts_(common::ObTimeUtility::current_time()),
  file_(file),
  func_name_(func),
  log_mod_(mod)
  {
    static_assert(CAPACITY > 0, "CAPACITY must greater than 0");
  }
  ~ObOccamTimeGuard()
  {
    int64_t total_cost = 0;
    for (int64_t idx = 0; idx < idx_; ++idx) {
      total_cost += click_poinsts_[idx];
    }
    total_cost += common::ObTimeUtility::current_time() - last_click_ts_;
    if (OB_UNLIKELY(total_cost >= warn_threshold_)) {
      constexpr int buffer_size = 256;
      char strbuffer[buffer_size] = { 0 };
      int n = snprintf(strbuffer, buffer_size, "cost too much time:%s:%s, ",
                       file_,
                       func_name_);
      if (n >= buffer_size) {
        snprintf(&strbuffer[buffer_size - 6], 6, "..., ");
      }
      OB_MOD_LOG_RET(log_mod_, WARN, OB_SUCCESS, strbuffer, KPC(this));
    }
  }
  void reuse()
  {
    idx_ = 0;
    last_click_ts_ = common::ObTimeUtility::current_time();
  }
  bool is_timeout()
  {
    int64_t total_cost = 0;
    for (int64_t idx = 0; idx < idx_; ++idx) {
      total_cost += click_poinsts_[idx];
    }
    total_cost += common::ObTimeUtility::current_time() - last_click_ts_;
    return total_cost > warn_threshold_;
  }
  int64_t get_total_time() const
  {
    int64_t total_cost = 0;
    for (int64_t idx = 0; idx < idx_; ++idx) {
      total_cost += click_poinsts_[idx];
    }
    total_cost += common::ObTimeUtility::fast_current_time() - last_click_ts_;
    return total_cost;
  }
  bool click(const uint16_t line)
  {
    if (OB_LIKELY(idx_ < CAPACITY)) {
      int64_t now = common::ObTimeUtility::current_time();
      line_array_[idx_] = static_cast<uint16_t>(line);
      click_poinsts_[idx_] = now - last_click_ts_;
      last_click_ts_ = now;
      ++idx_;
    }
    return true;
  }
  int64_t to_string(char *buf, const int64_t buf_len) const
  {
    int64_t pos = 0;
    fmt_ts_to_meaningful_str(buf, buf_len, pos, "|threshold", warn_threshold_);
    int64_t start_click_ts = last_click_ts_;
    for (int64_t idx = 0; idx < idx_; ++idx) {
      start_click_ts -= click_poinsts_[idx];
    }
    common::databuff_printf(buf, buf_len, pos, "start at %s|", common::ObTime2Str::ob_timestamp_str_range<HOUR, MSECOND>(start_click_ts));
    int64_t total_cost = 0;
    for (int64_t idx = 0; idx < idx_; ++idx) {
      fmt_ts_to_meaningful_str(buf, buf_len, pos, line_array_[idx], click_poinsts_[idx]);
      total_cost += click_poinsts_[idx];
    }
    total_cost += common::ObTimeUtility::current_time() - last_click_ts_;
    fmt_ts_to_meaningful_str(buf, buf_len, pos, "total", total_cost);
    if (pos != 0 && pos < buf_len) {
      pos -= 1;
    }
    return pos;
  }
protected:
  void fmt_ts_to_meaningful_str(char *buf,
                                const int64_t buf_len,
                                int64_t &pos,
                                const uint16_t line,
                                const int64_t ts) const
  {
    if (line != UINT16_MAX) {
      common::databuff_printf(buf, buf_len, pos, "%d", line);
    } else {
      common::databuff_printf(buf, buf_len, pos, "end");
    }
    if (ts < 1_ms) {
      common::databuff_printf(buf, buf_len, pos, "=%ldus|", ts);
    } else if (ts < 1_s) {
      common::databuff_printf(buf, buf_len, pos, "=%.2lfms|", double(ts) / 1_ms);
    } else {
      common::databuff_printf(buf, buf_len, pos, "=%.2lfs|", double(ts) / 1_s);
    }
  }
  void fmt_ts_to_meaningful_str(char *buf,
                                const int64_t buf_len,
                                int64_t &pos,
                                const char *lvalue,
                                const int64_t ts) const
  {
    common::databuff_printf(buf, buf_len, pos, "%s", lvalue);
    if (ts < 1_ms) {
      common::databuff_printf(buf, buf_len, pos, "=%ldus|", ts);
    } else if (ts < 1_s) {
      common::databuff_printf(buf, buf_len, pos, "=%.2lfms|", double(ts) / 1_ms);
    } else {
      common::databuff_printf(buf, buf_len, pos, "=%.2lfs|", double(ts) / 1_s);
    }
  }
protected:
  static constexpr int64_t CAPACITY = 16;
protected:
  const uint64_t warn_threshold_;
  uint32_t idx_;
  int64_t last_click_ts_;
  const char * const file_;
  const char * const func_name_;
  const char * const log_mod_;
  uint16_t line_array_[CAPACITY];
  uint64_t click_poinsts_[CAPACITY];
};

class ObOccamFastTimeGuard// must used in same thread
{
public:
  ObOccamFastTimeGuard(const uint32_t warn_threshold,
                       const char *file,
                       const char *func,
                       const char *mod)
  :warn_threshold_(warn_threshold),
  idx_(0),
  last_click_ts_(common::ObTscTimestamp::get_instance().current_time()),
  file_(file),
  func_name_(func),
  log_mod_(mod)
  {
    static_assert(CAPACITY > 0, "CAPACITY must greater than 0");
  }
  ~ObOccamFastTimeGuard()
  {
    int64_t total_cost = 0;
    for (int64_t idx = 0; idx < idx_; ++idx) {
      total_cost += click_poinsts_[idx];
    }
    total_cost += common::ObTscTimestamp::get_instance().current_time() - last_click_ts_;
    if (OB_UNLIKELY(total_cost >= warn_threshold_)) {
      constexpr int buffer_size = 256;
      char strbuffer[buffer_size] = { 0 };
      int n = snprintf(strbuffer, buffer_size, "cost too much time:%s:%s, ",
                       file_,
                       func_name_);
      if (n >= buffer_size) {
        snprintf(&strbuffer[buffer_size - 6], 6, "..., ");
      }
      OB_MOD_LOG_RET(log_mod_, WARN, OB_SUCCESS, strbuffer, KPC(this));
    }
  }
  bool is_timeout()
  {
    int64_t total_cost = 0;
    for (int64_t idx = 0; idx < idx_; ++idx) {
      total_cost += click_poinsts_[idx];
    }
    total_cost += common::ObTscTimestamp::get_instance().current_time() - last_click_ts_;
    return total_cost > warn_threshold_;
  }
  bool click(const uint16_t line)
  {
    if (OB_LIKELY(idx_ < CAPACITY)) {
      int64_t now = common::ObTscTimestamp::get_instance().current_time();
      line_array_[idx_] = static_cast<uint16_t>(line);
      click_poinsts_[idx_] = static_cast<uint32_t>(now - last_click_ts_);
      last_click_ts_ = now;
      ++idx_;
    }
    return true;
  }
  int64_t to_string(char *buf, const int64_t buf_len) const
  {
    int64_t pos = 0;
    fmt_ts_to_meaningful_str(buf, buf_len, pos, "|threshold", warn_threshold_);
    int64_t start_click_ts = common::ObTimeUtility::current_time();
    for (int64_t idx = 0; idx < idx_; ++idx) {
      start_click_ts -= click_poinsts_[idx];
    }
    common::databuff_printf(buf, buf_len, pos, "start at %s|", common::ObTime2Str::ob_timestamp_str_range<HOUR, MSECOND>(start_click_ts));
    int64_t total_cost = 0;
    for (int64_t idx = 0; idx < idx_; ++idx) {
      fmt_ts_to_meaningful_str(buf, buf_len, pos, line_array_[idx], click_poinsts_[idx]);
      total_cost += click_poinsts_[idx];
    }
    total_cost += common::ObTscTimestamp::get_instance().current_time() - last_click_ts_;
    fmt_ts_to_meaningful_str(buf, buf_len, pos, "total", total_cost);
    if (pos != 0 && pos < buf_len) {
      pos -= 1;
    }
    return pos;
  }
protected:
  void fmt_ts_to_meaningful_str(char *buf,
                                const int64_t buf_len,
                                int64_t &pos,
                                const uint16_t line,
                                const int64_t ts) const
  {
    if (line != UINT16_MAX) {
      common::databuff_printf(buf, buf_len, pos, "%d", line);
    } else {
      common::databuff_printf(buf, buf_len, pos, "end");
    }
    if (ts < 1_ms) {
      common::databuff_printf(buf, buf_len, pos, "=%ldus|", ts);
    } else if (ts < 1_s) {
      common::databuff_printf(buf, buf_len, pos, "=%.2lfms|", double(ts) / 1_ms);
    } else {
      common::databuff_printf(buf, buf_len, pos, "=%.2lfs|", double(ts) / 1_s);
    }
  }
  void fmt_ts_to_meaningful_str(char *buf,
                                const int64_t buf_len,
                                int64_t &pos,
                                const char *lvalue,
                                const int64_t ts) const
  {
    common::databuff_printf(buf, buf_len, pos, "%s", lvalue);
    if (ts < 1_ms) {
      common::databuff_printf(buf, buf_len, pos, "=%ldus|", ts);
    } else if (ts < 1_s) {
      common::databuff_printf(buf, buf_len, pos, "=%.2lfms|", double(ts) / 1_ms);
    } else {
      common::databuff_printf(buf, buf_len, pos, "=%.2lfs|", double(ts) / 1_s);
    }
  }
protected:
  static constexpr int64_t CAPACITY = 16;
protected:
  const uint32_t warn_threshold_;
  uint32_t idx_;
  int64_t last_click_ts_;
  const char * const file_;
  const char * const func_name_;
  const char * const log_mod_;
  uint16_t line_array_[CAPACITY];
  uint32_t click_poinsts_[CAPACITY];
};

struct TimeGuardFactory
{
  static ObOccamTimeGuard make_guard(const int64_t threshold1,
                                     const char *file,
                                     const char *func,
                                     const int64_t line,
                                     const char *mod) {
    UNUSED(line);
    return ObOccamTimeGuard(static_cast<uint32_t>(threshold1), file, func, mod);
  }
};

#define TIMEGUARD_INIT(mod, ...) auto __time_guard__ = oceanbase::common::occam::TimeGuardFactory::\
                                                       make_guard(__VA_ARGS__,\
                                                                  oceanbase::common::occam::get_file_name_without_dir(__FILE__),\
                                                                  __PRETTY_FUNCTION__,\
                                                                  __LINE__,\
                                                                  "["#mod"] ")
#define CLICK() ({ static_assert(__LINE__ >= 0 && __LINE__ <= UINT16_MAX, "line num greater than 65535"); (__time_guard__.click(__LINE__));})
#define CLICK_FAIL(stmt) (CLICK(), OB_FAIL(stmt))
#define CLICK_TMP_FAIL(stmt) (CLICK(), OB_TMP_FAIL(stmt))

}// namespace occam
}// namespace common
}// namespace oceanbase

#endif
