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

#define USING_LOG_PREFIX LIB
#include "ob_log.h"
#include "lib/utility/ob_printf.h"
#include "lib/utility/ob_fast_convert.h"
namespace oceanbase { namespace common {
extern void update_easy_log_level();
#ifdef _WIN32
bool g_ob_log_main_entered = false;
#endif
const char *OB_WEAK_SYMBOL ob_strerror(int) { return "ob_strerror"; }
const char *ObLogger::PERF_LEVEL = "PERF";
int logdata_printf(char *buf, const int64_t buf_len, int64_t &pos, const char *fmt, ...)
{
  int ret = OB_SUCCESS;
  va_list args;
  va_start(args, fmt);
  ret = logdata_vprintf(buf, buf_len, pos, fmt, args);
  va_end(args);
  return ret;
}

int logdata_vprintf(char *buf, const int64_t buf_len, int64_t &pos, const char *fmt, va_list args)
{
  int ret = OB_SUCCESS;
  if (OB_LIKELY(NULL != buf) && OB_LIKELY(0 <= pos && pos < buf_len)) {
#ifdef _WIN32
    char local_fmt[4096];
    const char *actual_fmt = fmt;
    if (NULL != fmt) {
      const char *src = fmt;
      char *dst = local_fmt;
      char *dst_end = local_fmt + sizeof(local_fmt) - 3;
      while (*src && dst < dst_end) {
        if (*src == '%') {
          *dst++ = *src++;
          if (*src == '%') { *dst++ = *src++; continue; }
          while (*src == '-' || *src == '+' || *src == ' ' || *src == '#' || *src == '0' || *src == '\'') {
            if (*src == '\'') { src++; } else { *dst++ = *src++; }
          }
          // width: either '*' or digits
          if (*src == '*') {
            *dst++ = *src++;
          } else {
            while (*src >= '0' && *src <= '9') { *dst++ = *src++; }
          }
          // precision: '.' followed by '*' or digits
          if (*src == '.') {
            *dst++ = *src++;
            if (*src == '*') {
              *dst++ = *src++;
            } else {
              while (*src >= '0' && *src <= '9') { *dst++ = *src++; }
            }
          }
          if (*src == 'l' && *(src+1) == 'l') {
            *dst++ = *src++; *dst++ = *src++;
          } else if (*src == 'l' && (*(src+1) == 'd' || *(src+1) == 'i' || *(src+1) == 'o' ||
                     *(src+1) == 'u' || *(src+1) == 'x' || *(src+1) == 'X')) {
            *dst++ = 'l'; *dst++ = 'l'; src++;
          } else if (*src == 'l') {
            *dst++ = *src++;
          }
          if (*src) { *dst++ = *src++; }
        } else {
          *dst++ = *src++;
        }
      }
      *dst = '\0';
      actual_fmt = local_fmt;
    }
    int len = vsnprintf(buf + pos, buf_len - pos, actual_fmt, args);
#else
    int len = ob_vsnprintf(buf + pos, buf_len - pos, fmt, args);
#endif
    if (OB_UNLIKELY(len < 0)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_LIKELY(len < buf_len - pos)) {
      pos += len;
    } else {
      pos = buf_len - 1;  //skip '\0' written by vsnprintf
      ret = OB_SIZE_OVERFLOW;
    }
  } else {
    ret = OB_SIZE_OVERFLOW;
  }
  return ret;
}

int logdata_print_obj(char *buf, const int64_t buf_len, int64_t &pos, const uint64_t obj)
{
  int ret = OB_SUCCESS;
  if (OB_LIKELY(NULL != buf)
      && OB_LIKELY(0 <= pos)
      && OB_LIKELY(pos < buf_len)
      && OB_LIKELY(buf_len - pos >= ObFastFormatInt::MAX_DIGITS10_STR_SIZE)) {
    pos += ObFastFormatInt::format_unsigned(obj, buf + pos);
  } else {
    ret = OB_SIZE_OVERFLOW;
  }
  return ret;
}

int logdata_print_obj(char *buf, const int64_t buf_len, int64_t &pos, const int64_t obj)
{
  int ret = OB_SUCCESS;
  if (OB_LIKELY(NULL != buf)
      && OB_LIKELY(0 <= pos)
      && OB_LIKELY(pos < buf_len)
      && OB_LIKELY(buf_len - pos >= ObFastFormatInt::MAX_DIGITS10_STR_SIZE)) {
    pos += ObFastFormatInt::format_signed(obj, buf + pos);
  } else {
    ret = OB_SIZE_OVERFLOW;
  }
  return ret;
}

int process_thread_log_id_level_map(const char *text, int32_t len)
{
  ObLogIdLevelMap map;
  int32_t valid = 0;
  int ret = OB_LOGGER.parse_set(text, len, valid, map);
  if (OB_SUCCESS == ret) ObThreadLogLevelUtils::init(&map);
  return ret;
}
ObLogger::ObLogger() {}
ObLogger::~ObLogger() { destroy(); }
int ObLogger::init()
{
  if (is_inited()) return OB_INIT_TWICE;
  return 0 == logger::start() ? OB_SUCCESS : OB_ERR_SYS;
}
void ObLogger::stop() { set_enable_async_log(false); logger::option(logger::OptionKey::Stopped, 1, 1); }
void ObLogger::wait() { if (is_inited()) (void)logger::flush(); }
void ObLogger::destroy() { (void)logger::shutdown(); }
void ObLogger::set_new_file_info(const char *info) { (void)logger::set_new_file_info(info); }
void ObLogger::set_file_name(const char *file, bool no_redirect, bool unused)
{ (void)logger::set_file(FD_SVR_FILE, file, !no_redirect); }
void ObLogger::check_file() { (void)logger::check_files(); }
void ObLogger::set_max_file_size(int64_t size)
{ if (size >= 0) logger::file_limit(0, size, 1); }
int ObLogger::set_max_file_index(int64_t count)
{
  if (count < 0 || count > MAX_LOG_FILE_COUNT) return OB_INVALID_ARGUMENT;
  logger::file_limit(1, count, 1);
  return OB_SUCCESS;
}
int ObLogger::set_record_old_log_file() { return logger::check_files() == 0 ? OB_SUCCESS : OB_ERR_SYS; }
int ObLogger::register_mod(uint64_t parent, const char *name)
{ return parent < MAX_PAR_MOD_SIZE ? logger::register_module(parent, -1, name) : OB_LOG_INVALID_MOD_ID; }
int ObLogger::register_mod(uint64_t parent, uint64_t sub, const char *name)
{ return parent < MAX_PAR_MOD_SIZE && sub < MAX_SUB_MOD_SIZE ? logger::register_module(parent, sub, name) : OB_LOG_INVALID_MOD_ID; }
void ObLogger::set_log_level(const char *name, int64_t version)
{
  int8_t level = 0;
  if (OB_SUCCESS == level_str2int(name, level)) set_log_level(level, version);
}
void ObLogger::set_log_level(int8_t level, int64_t version)
{ (void)logger::set_instance_level(level, version); update_easy_log_level(); }
int ObLogger::level_str2int(const char *name, int8_t &level, bool alert)
{ return logger::parse_level(name, &level, alert); }
int ObLogger::parse_check(const char *text, int32_t len)
{ return logger::parse_settings(text, len, nullptr, nullptr, 0, 1); }
int ObLogger::parse_set(const char *text, int32_t len, int64_t version)
{
  int ret = logger::parse_settings(text, len, nullptr, nullptr, version, 0);
  if (OB_SUCCESS == ret) update_easy_log_level();
  return ret;
}
int ObLogger::parse_set(const char *text, int32_t len, int32_t &valid, ObLogIdLevelMap &map)
{ return logger::parse_settings(text, len, &valid, &map.handle_, 0, 0); }
void ObLogger::issue_dba_error(const int errcode, const char *file, const int line, const char *info_str)
{
  const char *base_file_name = strrchr(file, '/');
  base_file_name = (NULL != base_file_name) ? base_file_name + 1 : file;
  LOG_DBA_FORCE_PRINT(ERROR, OB_COMMON_UNEXPECT_INTERNAL_ERROR, OB_UNEXPECT_INTERNAL_ERROR,
                      "An unexpected error(", errcode, ") occurred at ", base_file_name, ":", line, ". ",
                      "Error info:\"", info_str, "\" ",
                      "[suggestion] You can seek help from official technical personnel.");
}

bool OB_WEAK_SYMBOL ObLogger::need_to_print(const int32_t level)
{
  return (level <= get_log_level());
}

bool OB_WEAK_SYMBOL ObLogger::need_to_print(const uint64_t par_mod_id, const int32_t level)
{
  return (level <= get_log_level(par_mod_id));
}

bool OB_WEAK_SYMBOL ObLogger::need_to_print(const uint64_t par_mod_id, const uint64_t sub_mod_id,
                             const int32_t level)
{
  return (level <= get_log_level(par_mod_id, sub_mod_id));
}

bool OB_WEAK_SYMBOL ObLogger::need_to_print_dba(const int32_t level, bool force)
{
  const int count = logger::dba_enabled(level, force);
  if (count < 0) {
    int ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "print too many alert logs with same trace id!", K(ret), "log_count", -count);
  }
  return count > 0;
}
} }
#include "ob_log_module.ipp"
