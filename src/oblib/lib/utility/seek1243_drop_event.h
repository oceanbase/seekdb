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

#pragma once
// Temporary diagnostic events. SQL/row values never belong in this helper.
#include "lib/utility/seek1243_drop_context.h"
#include "lib/ob_define.h"
#include "lib/oblog/ob_log.h"
#include "lib/time/ob_time_utility.h"
#include <exception>

namespace oceanbase { namespace common { namespace seek1243 {
inline int unwind_count()
{
#if __cplusplus >= 201703L
  return std::uncaught_exceptions();
#else
  return std::uncaught_exception() ? 1 : 0;
#endif
}
struct KeyView {
  const lib::Seek1243DropKey &k;
  int64_t to_string(char *buf, int64_t len) const
  {
    int64_t pos = 0;
    databuff_printf(buf, len, pos,
        "{request_key_available:%d,request_generation:%llu,request_recv_ts:%lld,request_trace:[%llu,%llu,%llu,%llu],"
        "trace:[%llu,%llu,%llu,%llu],session_id:%llu,inner_tx_id:%lld,observed_inner_tx_id:%lld,"
        "drop_table_id:%llu,drop_table_type:%lld,stage_table_id:%llu,stage_table_type:%lld,"
        "span:%llu,span_owner_tid:%lld,parent_span:%llu,parent_span_owner_tid:%lld,"
        "execution_id_valid:%d,execution_id:%lld,retry_cnt:%lld,ctx_abs_deadline_us:%lld,"
        "query_abs_timeout_us:%lld,query_deadline_selected:%d,worker_timeout_ts:%lld}",
        (int)k.request_key_available, (unsigned long long)k.request_generation, (long long)k.request_recv_ts,
        (unsigned long long)k.request_trace[0], (unsigned long long)k.request_trace[1],
        (unsigned long long)k.request_trace[2], (unsigned long long)k.request_trace[3],
        (unsigned long long)k.trace[0], (unsigned long long)k.trace[1],
        (unsigned long long)k.trace[2], (unsigned long long)k.trace[3],
        (unsigned long long)k.session_id, (long long)k.inner_tx_id, (long long)k.observed_inner_tx_id,
        (unsigned long long)k.drop_table_id, (long long)k.drop_table_type,
        (unsigned long long)k.stage_table_id, (long long)k.stage_table_type,
        (unsigned long long)k.span, (long long)k.span_owner_tid,
        (unsigned long long)k.parent_span, (long long)k.parent_span_owner_tid,
        (int)k.execution_id_valid, (long long)k.execution_id, (long long)k.retry_cnt,
        (long long)k.ctx_abs_deadline_us, (long long)k.query_abs_timeout_us,
        (int)k.query_deadline_selected, (long long)k.worker_timeout_ts);
    return pos;
  }
};
// Keep raw timestamps/64-bit fields independent of the host's long width.
struct RawStampView {
  int64_t value;
  int64_t to_string(char *buf, int64_t len) const
  {
    int64_t pos = 0;
    databuff_printf(buf, len, pos, "%lld", static_cast<long long>(value));
    return pos;
  }
};

inline int format_field(char *buf, int64_t len, int64_t &pos, bool comma,
                        const char *name, const int64_t &value)
{
  return logdata_printf(buf, len, pos, comma ? ", %s=%lld" : "%s=%lld", name,
                        static_cast<long long>(value));
}

inline int format_field(char *buf, int64_t len, int64_t &pos, bool comma,
                        const char *name, const uint64_t &value)
{
  return logdata_printf(buf, len, pos, comma ? ", %s=%llu" : "%s=%llu", name,
                        static_cast<unsigned long long>(value));
}

template <typename Value>
int format_field(char *buf, int64_t len, int64_t &pos, bool comma,
                 const char *name, const Value &value)
{
  return logdata_print_key_obj(buf, len, pos, name, comma, value);
}

inline int append_fields(char *, int64_t, int64_t &, bool)
{
  return OB_SUCCESS;
}

template <typename Value, typename... Rest>
int append_fields(char *buf, int64_t len, int64_t &pos, bool with_comma,
                  const char *name, const Value &value, const Rest &...rest)
{
  static_assert(sizeof...(Rest) % 2 == 0, "SEEK-1243 events require key/value pairs");
  const int ret = format_field(buf, len, pos, with_comma, name, value);
  return OB_SUCCESS == ret ? append_fields(buf, len, pos, true, rest...) : ret;
}

template <typename... Args>
inline void mark(const lib::Seek1243DropKey &key, const char *event, Args... args)
{
  static_assert(sizeof...(Args) % 2 == 0, "SEEK-1243 events require key/value pairs");
#ifdef OB_SEEK1243_DROP_DIAGNOSTIC
  if (key.active) {
#ifdef _WIN32
    const int64_t pid = static_cast<int64_t>(GetCurrentProcessId());
#else
    const int64_t pid = static_cast<int64_t>(getpid());
#endif
    const int64_t os_tid = ob_syscall_gettid();
    const RawStampView raw_us{ObTimeUtility::current_monotonic_raw_time()};
    const KeyView view{key};
    char fields_buf[4096] = {};
    int64_t fields_pos = 0;
    const int fields_ret = append_fields(fields_buf, static_cast<int64_t>(sizeof(fields_buf)), fields_pos, false, args...);
    const bool fields_incomplete = OB_SUCCESS != fields_ret || fields_pos < 0
        || fields_pos >= static_cast<int64_t>(sizeof(fields_buf)) - 1;
    fields_buf[sizeof(fields_buf) - 1] = '\0';
    const char *fields = fields_buf;
    LOG_INFO("seek1243 correlated event", K(event), K(pid), K(os_tid), K(raw_us), K(view),
             K(fields), K(fields_ret), K(fields_incomplete));
  }
#else
  (void)key; (void)event;
  const int unused[] = {0, ((void)args, 0)...};
  (void)unused;
#endif
}

class Call {
public:
  Call() : key_(), event_("") {}
  Call(const lib::Seek1243DropKey &key, const char *event) : key_(key), event_(event)
  { begin(key, event); }
  void begin(const lib::Seek1243DropKey &key, const char *event)
  {
    key_ = key;
    event_ = event;
#ifdef OB_SEEK1243_DROP_DIAGNOSTIC
    if (key_.active) {
      entry_ = ObTimeUtility::current_monotonic_raw_time();
      mark(key_, event_, "edge", "begin", "entry_raw_us", entry_);
      start_ = ObTimeUtility::current_monotonic_raw_time();
    }
#endif
  }
  void bind(const lib::Seek1243DropKey &key) { key_ = key; }
  void finish(const int ret)
  {
#ifdef OB_SEEK1243_DROP_DIAGNOSTIC
    if (key_.active && start_ != 0) {
      const int64_t end = ObTimeUtility::current_monotonic_raw_time();
      mark(key_, event_, "edge", "end", K(ret), "entry_raw_us", entry_,
           "call_start_raw_us", start_, "call_end_raw_us", end,
           "begin_log_interval_us", start_ - entry_, "call_elapsed_us", end - start_);
    }
#else
    (void)ret;
#endif
  }
  void finish_unavailable()
  {
#ifdef OB_SEEK1243_DROP_DIAGNOSTIC
    if (key_.active && start_ != 0) {
      const int64_t end = ObTimeUtility::current_monotonic_raw_time();
      mark(key_, event_, "edge", "end", "ret_available", false,
           "entry_raw_us", entry_, "call_start_raw_us", start_, "call_end_raw_us", end,
           "begin_log_interval_us", start_ - entry_, "call_elapsed_us", end - start_);
    }
#endif
  }
private:
  lib::Seek1243DropKey key_;
  const char *event_;
  int64_t entry_ = 0;
  int64_t start_ = 0;
};

// A normal function/scope exit has a completed interval. A hanging call does not.
class Scope {
public:
  Scope(const lib::Seek1243DropKey &key, const char *event, int &ret)
      : call_(key, event), ret_(ret), exceptions_(unwind_count()) {}
  ~Scope() { if (unwind_count() == exceptions_) { call_.finish(ret_); } }
  void bind(const lib::Seek1243DropKey &key) { call_.bind(key); }
private:
  Call call_;
  int &ret_;
  int exceptions_;
};

template <typename Fn>
int run(const lib::Seek1243DropKey &key, const char *event, Fn &&fn)
{
  Call call(key, event);
  const int ret = fn();
  call.finish(ret);
  return ret;
}
}}} // namespace oceanbase::common::seek1243
