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

#ifndef OCEANBASE_LIB_OBLOG_OB_LOG_
#define OCEANBASE_LIB_OBLOG_OB_LOG_

#ifdef _WIN32
// Windows defines ERROR macro (as 0), which conflicts with log level ERROR
// Undefine it before including any headers that might use ERROR as a log level
#ifdef ERROR
#undef ERROR
#endif
#endif

#ifdef __APPLE__
// Define _DARWIN_C_SOURCE before including any system headers to enable BSD types (u_int, u_short, etc.)
// This must be defined before including sys/types.h
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
// Undefine _POSIX_C_SOURCE if it's defined, as it conflicts with _DARWIN_C_SOURCE
#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif
#endif

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "lib/ob_errno.h"
#include "lib/ob_abort.h"
#include "ob_log_print_kv.h"
#include "lib/coro/co_var.h"

#include "lib/oblog/ob_log_module.h"
#include "lib/oblog/ob_log_level.h"
#include "lib/utility/ob_defer.h"
#include "logger.h"
#include "lib/utility/ob_common_utility.h"
#include "lib/oblog/ob_log_dba_event.h"

// Undefine Windows min/max macros to avoid conflicts with std::min/std::max
#ifdef _WIN32
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#ifdef ERROR
#undef ERROR
#endif
#ifdef DELETE
#undef DELETE
#endif
#ifdef ERROR_SEEK
#undef ERROR_SEEK
#endif
#endif

#define OB_LOG_MAX_PAR_MOD_SIZE 64
#define OB_LOG_MAX_SUB_MOD_SIZE 64

namespace oceanbase
{
namespace common
{
#ifdef _WIN32
// Flag to indicate that main() has been entered and the logging system is safe to use.
// Before main(), global constructors/destructors may trigger OB_LOG macros, but the
// logging infrastructure (thread-local storage, file I/O, etc.) may not be ready.
// This flag is zero-initialized (.bss) before any dynamic initialization runs.
extern bool g_ob_log_main_entered;
#endif

extern int logdata_vprintf(char *buf, const int64_t buf_len, int64_t &pos, const char *fmt, va_list args);

#define OB_LOGGER ::oceanbase::common::ObLogger::get_logger()
#define OB_LOG_NEED_TO_PRINT(level) (OB_UNLIKELY(::oceanbase::common::need_to_print(OB_LOG_LEVEL_##level)))
#define OB_LOG_NEED_TO_PRINT_DBA(level, force) (OB_UNLIKELY(OB_LOGGER.need_to_print_dba(OB_LOG_LEVEL_##level, force)))

// Session configuration is a value object backed by a Rust-owned snapshot.
struct ObLogIdLevelMap
{
  static const uint64_t MAX_PAR_MOD_SIZE = OB_LOG_MAX_PAR_MOD_SIZE;
  static const uint64_t MAX_SUB_MOD_SIZE = OB_LOG_MAX_SUB_MOD_SIZE;
  ObLogIdLevelMap() : handle_(nullptr) {}
  ObLogIdLevelMap(const ObLogIdLevelMap &other) : handle_(logger::map_clone(other.handle_)) {}
  ObLogIdLevelMap &operator=(const ObLogIdLevelMap &other)
  {
    if (this != &other) {
      auto *next = logger::map_clone(other.handle_);
      logger::map_free(handle_);
      handle_ = next;
    }
    return *this;
  }
  ~ObLogIdLevelMap() { logger::map_free(handle_); }
  void reset_level() { set_level(OB_LOG_LEVEL_INFO); }
  void set_level(int8_t level) { (void)logger::map_set(&handle_, -1, -1, level); }
  int set_level(uint64_t parent, int8_t level)
  { return parent < MAX_PAR_MOD_SIZE ? logger::map_set(&handle_, parent, -1, level) : OB_LOG_INVALID_MOD_ID; }
  int set_level(uint64_t parent, uint64_t sub, int8_t level)
  { return parent < MAX_PAR_MOD_SIZE && sub < MAX_SUB_MOD_SIZE ? logger::map_set(&handle_, parent, sub, level) : OB_LOG_INVALID_MOD_ID; }
  int8_t get_level() const { return logger::map_level(handle_, -1, -1); }
  int8_t get_level(uint64_t parent) const
  { return parent < MAX_PAR_MOD_SIZE ? logger::map_level(handle_, parent, -1) : OB_LOG_LEVEL_NP; }
  int8_t get_level(uint64_t parent, uint64_t sub) const
  { return parent < MAX_PAR_MOD_SIZE && sub < MAX_SUB_MOD_SIZE ? logger::map_level(handle_, parent, sub) : OB_LOG_LEVEL_NP; }
private:
  friend class ObLogger;
  friend class ObThreadLogLevelUtils;
  logger::LevelMap *handle_;
};

int process_thread_log_id_level_map(const char *str, int32_t str_length);

class ObThreadLogLevelUtils
{
public:
  static void init() { logger::context_map(nullptr); }
  static void init(const ObLogIdLevelMap *map)
  {
    logger::context_map(map == nullptr ? nullptr : map->handle_);
    init(map == nullptr ? static_cast<int8_t>(OB_LOG_LEVEL_NONE) : map->get_level());
  }
  static void init(int8_t level) { logger::context_value(logger::ContextKey::Level, level, 1); }
  static void clear() { init(); init(static_cast<int8_t>(OB_LOG_LEVEL_NONE)); }
  static int8_t get_level() { return logger::context_value(logger::ContextKey::Level, 0, 0); }
};

//@class ObLogger
//@brief main class of logging facilities. Provide base function, for example log_message(),
//parse_set().
//
//This class is changed from the class 'CLogger' in tblog.h which was written by the God
//named DuoLong.
class ObLogger
{
public:
  static const uint64_t MAX_PAR_MOD_SIZE = OB_LOG_MAX_PAR_MOD_SIZE;
  static const uint64_t MAX_SUB_MOD_SIZE = OB_LOG_MAX_SUB_MOD_SIZE;
  static const int32_t MAX_LOG_FILE_COUNT = 10 * 1024;
  static const char *PERF_LEVEL;


public:
  ObLogger();
  ~ObLogger();
  int init();
  void stop();
  void wait();
  void destroy();

  bool is_inited() const { return logger::option(logger::OptionKey::Initialized, 0, 0) != 0; }
  bool has_stopped() const { return logger::option(logger::OptionKey::Stopped, 0, 0) != 0; }

public:
  bool is_async_log_used() const { return (enable_async_log() && OB_LIKELY(is_inited()) && !has_stopped()); }
  bool is_in_async_logging() const { return is_async_log_used() && !is_enable_logging(); }

  bool enable_async_log() const { return logger::option(logger::OptionKey::Async, 0, 0) != 0; }
  void set_enable_async_log(const bool flag) { logger::option(logger::OptionKey::Async, flag, 1); }
  void disable() { logger::option(logger::OptionKey::StopAppend, 1, 1); }
  bool set_disable_logging(bool flag) { return logger::context_value(logger::ContextKey::Disabled, flag, 1) != 0; }
  bool is_enable_logging() const { return logger::context_value(logger::ContextKey::Disabled, 0, 0) == 0; }
  void set_trace_mode(bool mode) { logger::context_value(logger::ContextKey::TraceMode, mode, 1); }
  bool is_trace_mode() const { return logger::context_value(logger::ContextKey::TraceMode, 0, 0) != 0; }
  void print_trace_buffer(const char *module, int32_t level, const char *file, int32_t line, const char *function)
  { (void)logger::trace_flush(module, level); }

  //@brief Log the message without level checking.
  void log_message_fmt(const char *mod_name,
                   const int32_t level,
                   const char *file,
                   const int32_t line,
                   const char *function,
                   const int errcode,
                   const char *fmt, ...) __attribute__((format(printf, 8, 9)));
  void log_message_va(const char *mod_name,
                      int32_t level,
                      const char *file,
                      int32_t line,
                      const char *function,
                      const int errcode,
                      const char *fmt,
                      va_list args);
  __attribute__((noinline, cold)) int fill_kv(char *buf, const int64_t buf_len, int64_t &pos,
                                              const bool with_comma)
  {
    return OB_SUCCESS;
  }

  __attribute__((noinline, cold)) int fill_kv(char *buf, const int64_t buf_len, int64_t &pos,
                                              const bool with_comma,
                                              const ObILogKV &kv)
  {
    return kv.print(buf, buf_len, pos, with_comma);
  }

  template <typename ... Args>
  __attribute__((noinline, cold)) int fill_kv(char *buf, const int64_t buf_len, int64_t &pos,
                                              const bool with_comma,
                                              const ObILogKV &kv,
                                              Args const & ... args)
  {
    int ret = OB_SUCCESS;
    ret = fill_kv(buf, buf_len, pos, with_comma, kv);
    if (OB_SUCC(ret)) {
      ret = fill_kv(buf, buf_len, pos, 1, std::forward<const Args&&>(args)...);
    }
    return ret;
  }

  __attribute__((noinline, cold)) int fill_log_buffer(char *data, const int64_t buf_len,
      int64_t &pos, const char *info)
  {
    int ret = OB_SUCCESS;
    LOG_PRINT_INFO(info);
    return ret;
  }

  template <typename ... Args>
  __attribute__((noinline, cold)) int fill_log_buffer(char *data, const int64_t buf_len,
                                                      int64_t &pos, const char *info,
                                                      const ObILogKV &kv,
                                                      Args const & ... args)
  {
    int ret = OB_SUCCESS;
    LOG_PRINT_INFO_BEGIN(info);
    if (OB_SUCC(ret)) {
      ret = fill_kv(data, buf_len, pos, 0, kv);
    }
    if (OB_SUCC(ret)) {
      ret = fill_kv(data, buf_len, pos, 1, std::forward<const Args&&>(args)...);
    }
    LOG_KV_END();
    return ret;
  }

  template <typename ... Args>
  __attribute__((noinline, cold)) void log_message_kv(const char *mod_name,
      const int32_t level, const char *file, const int32_t line, const char *function,
      const int errcode,
      const char *info, Args const && ... args)
  {
    auto &&log_data_func = [&] (char *buf, const int64_t buf_len, int64_t &pos) {
                             int ret = OB_SUCCESS;
                             ret = fill_log_buffer(buf, buf_len, pos, info,
                                                   std::forward<const Args&&>(args)...);
                             return ret;
                           };
    log_it(mod_name, nullptr, level, file, line, function, errcode, log_data_func);
  }

  // for dba log
  __attribute__((noinline, cold)) int fill_value(char *buf, const int64_t buf_len, int64_t &pos)
  {
    return OB_SUCCESS;
  }

  __attribute__((noinline, cold)) int fill_value(char *buf, const int64_t buf_len, int64_t &pos,
                                                 const ObILogValue &value)
  {
    return value.print(buf, buf_len, pos);
  }

  template <typename ... Args>
  __attribute__((noinline, cold)) int fill_value(char *buf, const int64_t buf_len, int64_t &pos,
                                                 const ObILogValue &value,
                                                 Args const & ... args)
  {
    int ret = OB_SUCCESS;
    ret = fill_value(buf, buf_len, pos, value);
    if (OB_SUCC(ret)) {
      ret = fill_value(buf, buf_len, pos, std::forward<const Args&&>(args)...);
    }
    return ret;
  }

  template <typename ... Args>
  __attribute__((noinline, cold)) int fill_log_buffer_value(char *data, const int64_t buf_len,
                                                            int64_t &pos, bool with_quotes,
                                                            const ObILogValue &value,
                                                            Args const & ... args)
  {
    int ret = OB_SUCCESS;
    if (with_quotes && OB_FAIL(logdata_printf(data, buf_len, pos, "\""))) {
    } else if (OB_FAIL(fill_value(data, buf_len, pos, value))) {
    } else if (OB_FAIL(fill_value(data, buf_len, pos, std::forward<const Args&&>(args)...))) {
    } else if (with_quotes) {
      ret = logdata_printf(data, buf_len, pos, "\"");
    }
    return ret;
  }

  template <typename ... Args>
  __attribute__((noinline, cold)) void log_message_value(const char *mod_name, const char *dba_event,
      const int32_t level, const char *file, const int32_t line, const char *function,
      const int errcode, bool with_quotes,
      Args const && ... args)
  {
    auto &&log_data_func = [&] (char *buf, const int64_t buf_len, int64_t &pos) {
                             int ret = OB_SUCCESS;
                             ret = fill_log_buffer_value(buf, buf_len, pos, with_quotes,
                                                       std::forward<const Args&&>(args)...);
                             return ret;
                           };
    log_it(mod_name, dba_event, level, file, line, function, errcode, log_data_func);
  }

  //@brief Check whether the level to print.
  bool __attribute__((noinline, cold)) need_to_print(const int32_t level);
  bool __attribute__((noinline, cold)) need_to_print_dba(const int32_t level, bool force = false);
  //@brief Check whether the level of the par-module to print.
  bool __attribute__((noinline, cold)) need_to_print(const uint64_t par_mod_id, const int32_t level);
  bool __attribute__((noinline, cold)) need_to_print(const uint64_t par_mod_id, const uint64_t sub_mod_id,
                            const int32_t level);

  //@brief Set the log-file's name.
  //@param[in] filename The log-file's name.
  //@param[in] flag Whether redirect the stdout and stderr to the descriptor of the log-file.
  //FALSE:redirect TRUE:no redirect.
  void set_file_name(const char *filename,
                     const bool no_redirect_flag = false,
                     const bool unused = false);

  //@brief Check the log-file's status.
  void check_file();
  //@brief Check the log-file's status.


  //@brief Set the max log-file size. The unit is byte. Default value and max value are 1GB.
  void set_max_file_size(int64_t max_file_size);
  //@brief Set the max number of log-files. If max_file_index = 0, no limit.
  int set_max_file_index(int64_t max_file_index = 0x0F);
  //@brief Set whether record old log file. If this flag and max_file_index set,
  //will record log files in the directory for log file
  int set_record_old_log_file();

  //@brief Get the process-only ObLogger.
  static ObLogger &get_logger();

  //@brief Regist par-module name with par_mod_id.
  int register_mod(const uint64_t par_mod_id, const char *par_mod_name);
  //@brief Regist sub-module name with par_mod_id and sub_mod_id.
  int register_mod(const uint64_t par_mod_id, const uint64_t sub_mod_id, const char *sub_mod_name);

  //@brief Set global log-file's level and warning log-file's level.
  //@param[in] level The log-file's level.
  //@param[in] version The time(us) change the log level.
  void set_log_level(const char *level, int64_t version = 0);
  //@brief Set global level.
  void set_log_level(const int32_t level, int64_t version = 0)
  {set_log_level(static_cast<int8_t>(level), version);}
  void set_log_level(const int8_t level, int64_t version = 0);
  int set_mod_log_levels(const char* level_str, int64_t version = 0);

  void down_log_level(int64_t version = 0) {set_log_level(get_level() + 1, version);}
  void up_log_level(int64_t version = 0) {set_log_level(get_level() - 1, version);}

  int32_t get_level() const {return logger::get_level(-1, -1, 0);}
  //@brief current log level, with session log level considered.
  int32_t get_log_level() const;
  int32_t get_log_level(const uint64_t par_mod_id) const;
  int32_t get_log_level(const uint64_t par_mod_id, const uint64_t sub_mod_id) const;
  inline const char *get_level_str() const { return logger::level_name(get_level()); }
  static const char *get_level_str(const int8_t level) { return logger::level_name(level); }

  //@brief Get the int8_t type representing the level in string.
  int level_str2int(const char *level_name, int8_t &level_int, bool is_alert_log = false);

  int parse_check(const char *str, const int32_t str_length);
  //@brief Parse the string like "ALL.*:INFO, SQL.ENG:DEBUG, COMMON.*:ERROR",
  //and set the global level map.
  int parse_set(const char *str, const int32_t str_length, int64_t version = 0);
  //@brief Parse the string like "ALL.*:INFO, SQL.ENG:DEBUG, COMMON.*:ERROR",
  //and set the level map.
  int parse_set(const char *str,
                const int32_t str_length,
                int32_t &valid_length,
                ObLogIdLevelMap &id_level_map);

  bool get_guard() const { return logger::context_value(logger::ContextKey::Guard, 0, 0) != 0; }
  void set_guard(bool guard) { logger::context_value(logger::ContextKey::Guard, guard, 1); }

  void set_new_file_info(const char *info);
  void set_info_as_wdiag(const bool v) { logger::option(logger::OptionKey::InfoAsWdiag, v, 1); }
  bool is_info_as_wdiag() const { return logger::option(logger::OptionKey::InfoAsWdiag, 0, 0) != 0; }

  // issue a ERROR message for each EDIAG log right now
  void issue_dba_error(const int errcode, const char *file, const int line, const char *info_str);


private:
  template<typename Function>
  void log_it(const char *module, const char *event, int32_t level, const char *file,
      int32_t line, const char *function, int error, Function &&format);
};

inline ObLogger& ObLogger::get_logger()
{
  static ObLogger logger;
  return logger;
}

template<typename Function>
void ObLogger::log_it(const char *module, const char *event, int32_t level,
    const char *file, int32_t line, const char *function, int error, Function &&format)
{
#ifdef _WIN32
  if (OB_UNLIKELY(!g_ob_log_main_entered)) return;
#endif
  logger::RecordMetadata metadata = {module, file, function, event, GETTNAME_V2(),
      0, GETTID(), line, level, error, 1};
  logger::RecordBuffer buffer = {};
  const int result = logger::begin(&metadata, &buffer);
  if (0 != result) return;
  DEFER(logger::cancel(&buffer));
  int64_t pos = 0;
  int ret = format(buffer.data, buffer.capacity, pos);
  if (OB_SIZE_OVERFLOW == ret && 0 == logger::grow(&buffer)) {
    pos = 0;
    ret = format(buffer.data, buffer.capacity, pos);
  }
  if (OB_SUCCESS == ret || OB_SIZE_OVERFLOW == ret) {
    const char *backtrace = (buffer.flags & static_cast<uint32_t>(logger::RecordFlag::Backtrace)) != 0 ? lbt() : nullptr;
    (void)logger::commit(&buffer, std::max<int64_t>(pos, 0), backtrace);
  }
}

__attribute__((noinline, cold))
inline void ObLogger::log_message_fmt(const char *mod_name,
                 const int32_t level,
                 const char *file,
                 const int32_t line,
                 const char *function,
                 const int errcode,
                 const char *fmt, ...)
{
  if (OB_NOT_NULL(fmt)) {
    va_list args;
    va_start(args, fmt);
    DEFER(va_end(args));
    log_message_va(mod_name, level, file, line, function, errcode, fmt, args);
  }
}

inline void ObLogger::log_message_va(const char *mod_name,
                 const int32_t level,
                 const char *file,
                 const int32_t line,
                 const char *function,
                 const int errcode,
                 const char *fmt,
                 va_list args)
{
  if (OB_NOT_NULL(fmt)) {
    auto &&log_data_func = [&](char *buf, const int64_t buf_len, int64_t &pos) {
      va_list args_cpy;
      va_copy(args_cpy, args);
      DEFER(va_end(args_cpy));
      return logdata_vprintf(buf, buf_len, pos, fmt, args_cpy);
    };
    log_it(mod_name, nullptr, level, file, line, function, errcode, log_data_func);
  }
}

inline int32_t ObLogger::get_log_level() const { return logger::get_level(-1, -1, 1); }
inline int32_t ObLogger::get_log_level(uint64_t parent) const
{ return parent < MAX_PAR_MOD_SIZE ? logger::get_level(parent, -1, 1) : OB_LOG_LEVEL_NP; }
inline int32_t ObLogger::get_log_level(uint64_t parent, uint64_t sub) const
{ return parent < MAX_PAR_MOD_SIZE && sub < MAX_SUB_MOD_SIZE ? logger::get_level(parent, sub, 1) : OB_LOG_LEVEL_NP; }
inline int ObLogger::set_mod_log_levels(const char *levels, int64_t version)
{ return levels == nullptr ? OB_INVALID_ARGUMENT : parse_set(levels, strlen(levels), version); }

template <typename ... Args>
#ifdef _WIN32
__attribute__((noinline)) void OB_PRINT(const char *mod_name, const int32_t level, const char *file, const int32_t line,
#else
__attribute__((noinline, cold)) void OB_PRINT(const char *mod_name, const int32_t level, const char *file, const int32_t line,
#endif
              const char *function,
              const int errcode, const char *info_string,
              const char *, /* placeholder */
              Args const && ... args)
{
  int ret = OB_SUCCESS;
#ifndef _WIN32
  if (OB_LOG_LEVEL_ERROR == level) {
    OB_LOGGER.issue_dba_error(errcode, file, line, info_string);
  }
#endif
  if (OB_LIKELY(!OB_LOGGER.get_guard())) {
    OB_LOGGER.set_guard(true);
    DEFER(OB_LOGGER.set_guard(false));
    OB_LOGGER.log_message_kv(mod_name, level, file, line, function, errcode,
                             info_string, std::forward<const Args&&>(args)...);
    UNUSED(ret);
  }
}

template <typename ... Args>
void OB_PRINT_DBA(const char *mod_name, const char *mod_name_bracket,
                  const char *dba_event, bool print_rd_log, const int32_t level,
                  const char *file, const int32_t line,
                  const char *function,
                  const int errcode,
                  const char *, /* placeholder */
                  Args const && ... args)
{
#ifdef _WIN32
  if (OB_UNLIKELY(!g_ob_log_main_entered)) return;
#endif
  if (OB_LIKELY(!OB_LOGGER.get_guard())) {
    OB_LOGGER.set_guard(true);
    DEFER(OB_LOGGER.set_guard(false));
    OB_LOGGER.log_message_value(mod_name_bracket, nullptr, level, file, line, function, errcode, false,
                                std::forward<const Args&&>(args)...);
  }
}

__attribute__((noinline, cold))
inline bool need_to_print(const int32_t level)
{
#ifdef _WIN32
  if (OB_UNLIKELY(!g_ob_log_main_entered)) return false;
#endif
  return OB_LOGGER.need_to_print(level);
}

__attribute__((noinline, cold))
inline bool need_to_print(const uint64_t par_mod_id, const int32_t level)
{
#ifdef _WIN32
  if (OB_UNLIKELY(!g_ob_log_main_entered)) return false;
#endif
  return OB_LOGGER.need_to_print(par_mod_id, level);
}

__attribute__((noinline, cold))
inline bool need_to_print(const uint64_t par_mod_id, const uint64_t sub_mod_id,
                          const int32_t level)
{
#ifdef _WIN32
  if (OB_UNLIKELY(!g_ob_log_main_entered)) return false;
#endif
  return OB_LOGGER.need_to_print(par_mod_id, sub_mod_id, level);
}

} // common
} // oceanbase
#endif
