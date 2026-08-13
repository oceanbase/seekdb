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
#define USING_LOG_PREFIX SHARE

#include "ob_config_helper.h"

#include "common/ob_store_format.h"
#include "lib/ob_running_mode.h"
#include "share/cache/ob_kvcache_struct.h"
#include "share/config/ob_parallel_ddl_control_mode.h"
#include "share/config/ob_server_config.h"

namespace oceanbase
{
using namespace oceanbase::share::schema;
using namespace share;
using namespace obcall;

namespace common
{

bool ObConfigIpChecker::check(const ObConfigItem &t) const
{
  struct sockaddr_in sa;
  int result = inet_pton(AF_INET, t.str(), &(sa.sin_addr));
  return result != 0;
}

ObConfigConsChecker:: ~ObConfigConsChecker()
{
  if (NULL != left_) {
    ObConfigChecker *left = const_cast<ObConfigChecker*>(left_);
    OB_DELETE(ObConfigChecker, "unused", left);
  }
  if (NULL != right_) {
    ObConfigChecker *right = const_cast<ObConfigChecker*>(right_);
    OB_DELETE(ObConfigChecker, "unused", right);
  }
}
bool ObConfigConsChecker::check(const ObConfigItem &t) const
{
  return (NULL == left_ ? true : left_->check(t))
         && (NULL == right_ ? true : right_->check(t));
}

bool ObConfigEvenIntChecker::check(const ObConfigItem &t) const
{
  bool is_valid = false;
  int64_t value = ObConfigIntParser::get(t.str(), is_valid);
  if (is_valid) {
    is_valid = value % 2 == 0;
  }
  return is_valid;
}

bool ObConfigFreezeTriggerIntChecker::check(const ObAdminSetConfigItem &t)
{
  bool is_valid = false;
  int64_t value = ObConfigIntParser::get(t.value_.ptr(), is_valid);
  int64_t write_throttle_trigger = get_write_throttle_trigger_percentage_();
  if (is_valid) {
    is_valid = value > 0 && value < 100;
  }
  if (is_valid) {
    is_valid = write_throttle_trigger != 0;
  }
  if (is_valid) {
    is_valid = value < write_throttle_trigger;
  }
  return is_valid;
}

int64_t ObConfigFreezeTriggerIntChecker::get_write_throttle_trigger_percentage_()
{
  int64_t percent = 0;

  percent = GCONF.writing_throttling_trigger_percentage;

  return percent;
}

bool ObConfigWriteThrottleTriggerIntChecker::check(const ObAdminSetConfigItem &t)
{
  bool is_valid = false;
  int64_t value = ObConfigIntParser::get(t.value_.ptr(), is_valid);
  int64_t freeze_trigger = get_freeze_trigger_percentage_();
  if (is_valid) {
    is_valid = value > 0 && value <= 100;
  }
  if (is_valid) {
    is_valid = freeze_trigger != 0;
  }
  if (is_valid) {
    is_valid = value > freeze_trigger;
  }
  return is_valid;
}

int64_t ObConfigWriteThrottleTriggerIntChecker::get_freeze_trigger_percentage_()
{
  int64_t percent = 0;

  percent = GCONF.freeze_trigger_percentage;

  return percent;
}

bool ObConfigLogDiskLimitThresholdIntChecker::check(const ObAdminSetConfigItem &t)
{
  bool is_valid = false;
  const int64_t value = ObConfigIntParser::get(t.value_.ptr(), is_valid);
  const int64_t throttling_percentage = get_log_disk_throttling_percentage_();
  if (is_valid) {
    is_valid = (throttling_percentage != 0);
  }
  if (is_valid) {
    is_valid = (throttling_percentage == 100) || (value > throttling_percentage);
  }
  return is_valid;
}

int64_t ObConfigLogDiskLimitThresholdIntChecker::get_log_disk_throttling_percentage_()
{
  int64_t percent = 0;

  percent = GCONF.log_disk_throttling_percentage;

  return percent;
}

bool ObConfigLogDiskThrottlingPercentageIntChecker::check(const obcall::ObAdminSetConfigItem &t)
{
  bool is_valid = false;
  const int64_t value = ObConfigIntParser::get(t.value_.ptr(), is_valid);
  const int64_t limit_threshold = get_log_disk_utilization_limit_threshold_();
  if (is_valid) {
    is_valid = (limit_threshold != 0);
  }
  if (is_valid) {
    is_valid = (value == 100) || (value < limit_threshold);
  }
  return is_valid;
}

int64_t ObConfigLogDiskThrottlingPercentageIntChecker::get_log_disk_utilization_limit_threshold_()
{
  int64_t threshold = 0;

  threshold = GCONF.log_disk_utilization_limit_threshold;

  return threshold;
}

bool ObConfigTabletSizeChecker::check(const ObConfigItem &t) const
{
  bool is_valid = false;
  const int64_t mask = (1 << 21) - 1;
  int64_t value = ObConfigCapacityParser::get(t.str(), is_valid, false);
  if (is_valid) {
    // value has to be a multiple of 2M
    is_valid = (value >= 0) && !(value & mask);
  }
  return is_valid;
}

bool ObConfigStaleTimeChecker::check(const ObConfigItem &t) const
{
  bool is_valid = false;
  int64_t stale_time = ObConfigTimeParser::get(t.str(), is_valid);
  if (is_valid) {
    is_valid = (stale_time >= GCONF.weak_read_version_refresh_interval);
    if (!is_valid) {
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "max_stale_time_for_weak_consistency violate"
          " weak_read_version_refresh_interval,");
    }
  }
  return is_valid;
}

bool ObConfigCompressFuncChecker::check(const ObConfigItem &t) const
{
  bool is_valid = false;
  for (int i = 0; i < ARRAYSIZEOF(common::compress_funcs); ++i) {
    if (0 == ObString::make_string(compress_funcs[i]).case_compare(t.str())) {
      is_valid = true;
      break;
    }
  }
  return is_valid;
}

bool ObConfigPerfCompressFuncChecker::check(const ObConfigItem &t) const
{
  bool is_valid = false;
  for (int i = 0; i < ARRAYSIZEOF(common::perf_compress_funcs) && !is_valid; ++i) {
    if (0 == ObString::make_string(perf_compress_funcs[i]).case_compare(t.str())) {
      is_valid = true;
    }
  }
  return is_valid;
}

bool ObConfigTempStoreFormatChecker::check(const ObConfigItem &t) const
{
  static const char *const FORMAT_OPTIONS[] = {
    "auto",
    "zstd",
    "lz4",
    "none",
  };
  bool is_valid = false;
  for (int i = 0; i < ARRAYSIZEOF(FORMAT_OPTIONS) && !is_valid; ++i) {
    if (0 == ObString::make_string(FORMAT_OPTIONS[i]).case_compare(t.str())) {
      is_valid = true;
    }
  }
  return is_valid;
}

bool ObConfigPxBFGroupSizeChecker::check(const ObConfigItem &t) const
{
  bool is_valid = false;
  ObString str("auto");
  if (0 == str.case_compare(t.str())) {
    is_valid = true;
  // max_number: 2^64 - 1
  } else if (strlen(t.str()) <= 20 && strlen(t.str()) > 0) {
    is_valid = true;
    for (int i = 0; i < strlen(t.str()); ++i) {
      if (0 == i && (t.str()[i] <= '0' || t.str()[i] > '9')) {
        is_valid = false;
        break;
      } else if (t.str()[i] < '0' || t.str()[i] > '9') {
        is_valid = false;
        break;
      }
    }
  }
  return is_valid;
}

bool ObConfigRowFormatChecker::check(const ObConfigItem &t) const
{
  bool is_valid = false;
  ObStoreFormatType type = OB_STORE_FORMAT_INVALID;
  if (OB_ISNULL(t.str()) || strlen(t.str()) == 0) {
  } else if (OB_SUCCESS != ObStoreFormat::find_store_format_type_mysql(ObString::make_string(t.str()), type)) {
  } else if (ObStoreFormat::is_store_format_mysql(type)) {
    is_valid = true;
  }
  return is_valid;
}

bool ObConfigMaxSyslogFileCountChecker::check(const ObConfigItem &t) const
{
  bool is_valid = false;
  int64_t max_count = ObConfigIntParser::get(t.str(), is_valid);
  if (is_valid) {
    int64_t uncompressed_count = GCONF.syslog_file_uncompressed_count;
    if (max_count == 0 || max_count >= uncompressed_count) {
      is_valid = true;
    } else {
      is_valid = false;
    }
  }
  return is_valid;
}

bool ObConfigSyslogCompressFuncChecker::check(const ObConfigItem &t) const
{
  bool is_valid = false;
  for (int i = 0; i < ARRAYSIZEOF(common::syslog_compress_funcs) && !is_valid; ++i) {
    if (0 == ObString::make_string(syslog_compress_funcs[i]).case_compare(t.str())) {
      is_valid = true;
    }
  }
  return is_valid;
}

bool ObConfigSyslogFileUncompressedCountChecker::check(const ObConfigItem &t) const
{
  bool is_valid = false;
  int64_t uncompressed_count = ObConfigIntParser::get(t.str(), is_valid);
  if (is_valid) {
    int64_t max_count = GCONF.max_syslog_file_count;
    if (uncompressed_count >= 0 && (max_count == 0 || uncompressed_count <= max_count)) {
      is_valid = true;
    } else {
      is_valid = false;
    }
  }
  return is_valid;
}

bool ObConfigLogLevelChecker::check(const ObConfigItem &t) const
{
  const ObString tmp_str(t.str());
  return ((0 == tmp_str.case_compare(ObLogger::PERF_LEVEL))
      || OB_SUCCESS == OB_LOGGER.parse_check(tmp_str.ptr(), tmp_str.length()));
}

bool ObConfigAuditTrailChecker::check(const ObConfigItem &t) const
{
  common::ObString tmp_string(t.str());
  return false;
}

bool ObConfigAuditLogCompressionChecker::check(const ObConfigItem &t) const
{
  common::ObString tmp_string(t.str());
  return 0 == tmp_string.case_compare("NONE")
      || 0 == tmp_string.case_compare("ZSTD");
}

bool ObConfigAuditLogPathChecker::check(const ObConfigItem &t) const
{
  static constexpr char FILE_PREFIX[] = "file://";
  const char *path = t.str();
  return '\0' == path[0]
      || (0 == STRNCMP(path, FILE_PREFIX, sizeof(FILE_PREFIX) - 1)
          && OB_ISNULL(STRCHR(path, '?'))
          && STRLEN(path) < OB_MAX_URI_LENGTH);
}

bool ObConfigAuditLogFormatChecker::check(const ObConfigItem &t) const
{
  common::ObString tmp_string(t.str());
  return 0 == tmp_string.case_compare("CSV");
}

bool ObConfigAuditLogQuerySQLChecker::check(const ObConfigItem &t) const
{
  common::ObString tmp_string(t.str());
  return 0 == tmp_string.case_compare("ALL")
      || 0 == tmp_string.case_compare("NONE");
}

bool ObConfigAuditLogStrategyChecker::check(const ObConfigItem &t) const
{
  common::ObString tmp_string(t.str());
  return 0 == tmp_string.case_compare("ASYNCHRONOUS")
      || 0 == tmp_string.case_compare("PERFORMANCE")
      || 0 == tmp_string.case_compare("SYNCHRONOUS");
}

bool ObConfigWorkAreaPolicyChecker::check(const ObConfigItem &t) const
{
  const ObString tmp_str(t.str());
  return ((0 == tmp_str.case_compare(MANUAL)) || (0 == tmp_str.case_compare(AUTO)));
}

bool ObDutyDurationUtil::extract_value(const char *ptr, uint64_t len, int32_t &value)
{
  char buffer[16] = {0};
  bool found = false;
  for (uint64_t i = 0; i < len; ++i) {
    if (' ' == ptr[i]) {
    } else if (ptr[i] >= '0' && ptr[i] <= '9') {
      found = true;
      MEMCPY(buffer, ptr + i, MIN(len - i, sizeof(buffer) - 1));
      break;
    }
  }
  value = static_cast<int32_t>(atoi(buffer));
  return found;
}

int ObDutyDurationUtil::parse_time(ObString &input, ObDutyTime &time)
{
  int ret = OB_SUCCESS;
  const char *first_split = input.find(':');
  const char *second_split = input.reverse_find(':');
  if (!input.contains(first_split) || !input.contains(second_split) || first_split >= second_split) {
    ret = OB_INVALID_CONFIG;
    LOG_WARN("invalid duty time", K(ret), K(input));
  } else if (!extract_value(input.ptr(), first_split - input.ptr(), time.hour_)
             || !extract_value(first_split + 1, second_split - first_split - 1, time.min_)
             || !extract_value(second_split + 1,
                               input.length() + input.ptr() - second_split,
                               time.sec_)) {
    ret = OB_INVALID_CONFIG;
    LOG_WARN("invalid duty time", K(ret), K(input));
  }
  return ret;
}

int ObDutyDurationUtil::parse(const char *str, ObDutyDuration &duration)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(str) || 0 == strlen(str)) {
    duration.not_set_ = true;
  } else {
    ObString input(str);
    const char *begin = input.find('[');
    const char *split = input.find(',');
    const char *end = input.reverse_find(']');
    if (OB_ISNULL(begin) || OB_ISNULL(split) || OB_ISNULL(end)) {
      ret = OB_INVALID_CONFIG;
      LOG_WARN("failed to parse duty duration", K(ret), K(input));
    } else {
      ObString begin_time;
      ObString end_time;
      begin_time.assign_ptr(begin + 1, static_cast<ObString::obstr_size_t>(split - begin - 1));
      end_time.assign_ptr(split + 1, static_cast<ObString::obstr_size_t>(end - split - 1));
      if (OB_FAIL(parse_time(begin_time, duration.begin_))
          || OB_FAIL(parse_time(end_time, duration.end_))) {
        LOG_WARN("failed to parse duty duration times", K(ret));
      } else {
        duration.not_set_ = false;
      }
    }
  }
  return ret;
}

bool ObDutyDurationUtil::current_in_duration(const ObDutyDuration &duration)
{
  bool in_duration = false;
  if (!duration.not_set_) {
    time_t now;
    time(&now);
    struct tm local_time;
#ifdef _WIN32
    localtime_s(&local_time, &now);
#else
    localtime_r(&now, &local_time);
#endif
    const uint32_t begin = duration.begin_.sec_ + 60 * (duration.begin_.min_ + 60 * duration.begin_.hour_);
    const uint32_t end = duration.end_.sec_ + 60 * (duration.end_.min_ + 60 * duration.end_.hour_);
    const uint32_t current = local_time.tm_sec + 60 * (local_time.tm_min + 60 * local_time.tm_hour);
    in_duration = begin <= current && current <= end;
  }
  return in_duration;
}

bool ObVecIndexOptDutyTimeChecker::check(const ObConfigItem& t) const
{
  common::ObDutyDuration duty_duration;
  return OB_SUCCESS == common::ObDutyDurationUtil::parse(t.str(), duty_duration)
      && duty_duration.is_valid();
}

bool MemoryBudgetConfigChecker::check(const ObConfigItem &t) const
{
  bool is_valid = false;
  int64_t value = ObConfigCapacityParser::get(t.str(), is_valid, false);
  if (is_valid) {
    is_valid = 0 == value || value >= lib::DEFAULT_MEMORY_BUDGET;
  }
  return is_valid;
}

bool KVCacheMemoryLimitConfigChecker::check(const ObConfigItem &t) const
{
  bool is_valid = false;
  const int64_t value = ObConfigCapacityParser::get(t.str(), is_valid, false);
  if (is_valid) {
    const int64_t initialized_capacity = GMEMCONF.get_kvcache_memory_capacity();
    const int64_t maximum_value = initialized_capacity > 0
        ? initialized_capacity
        : MAX_KVCACHE_MEMORY_SIZE;
    is_valid = 0 == value || (value > 0 && value <= maximum_value);
  }
  return is_valid;
}

bool ObLogDiskUsagePercentageChecker::check(const ObConfigItem &t) const
{
  bool is_valid = false;
  int64_t value = ObConfigIntParser::get(t.str(), is_valid);
  if (is_valid) {
    // TODO by runlun: runtime configuration item check
    const int64_t log_disk_utilization_threshold = 100;
    if (value < log_disk_utilization_threshold) {
      is_valid = false;
      LOG_USER_ERROR(OB_INVALID_CONFIG,
          "log_disk_utilization_limit_threshold "
          "should not be less than log_disk_utilization_threshold");
    }
  }
  return is_valid;
}

bool ObConfigEnableDefensiveChecker::check(const ObConfigItem &t) const
{
  bool is_valid = false;
  int64_t value = ObConfigIntParser::get(t.str(), is_valid);
  if (is_valid) {
    if (value > 2 || value < 0) {
      is_valid = false;
    }
  }
  return is_valid;
}

int64_t ObConfigIntParser::get(const char *str, bool &valid)
{
  char *p_end = NULL;
  int64_t value = 0;

  if (OB_ISNULL(str) || '\0' == str[0]) {
    valid = false;
  } else {
    valid = true;
    value = strtoll(str, &p_end, 0);
    if ('\0' == *p_end) {
      valid = true;
    } else {
      valid = false;
      OB_LOG_RET(WARN, OB_ERR_UNEXPECTED, "set int error", K(str), K(valid));
    }
  }
  return value;
}

int64_t ObConfigCapacityParser::get(const char *str, bool &valid,
                                    bool check_unit /* = true */,
                                    bool use_byte /* = false*/)
{
  return parse_config_capacity(str, valid, check_unit, use_byte);
}

int64_t ObConfigReadableIntParser::get(const char *str, bool &valid)
{
  char *p_unit = NULL;
  int64_t value = 0;

  if (OB_ISNULL(str) || '\0' == str[0]) {
    valid = false;
  } else {
    valid = true;
    value = strtoll(str, &p_unit, 0);

    if (OB_ISNULL(p_unit)) {
      valid = false;
    } else if (value < 0) {
      valid = false;
    } else if ('\0' == *p_unit) {
      // 
      // without any unit, do nothing
    } else if (0 == STRCASECMP("k", p_unit)) {
      value *= UNIT_K;
    } else if (0 == STRCASECMP("m", p_unit)) {
      value *= UNIT_M;
    } else {
      valid = false;
      OB_LOG_RET(WARN, OB_ERR_UNEXPECTED, "set readable int error", K(str), K(p_unit));
    }
  }

  return value;
}

int64_t ObConfigTimeParser::get(const char *str, bool &valid)
{
  char *p_unit = NULL;
  int64_t value = 0;

  if (OB_ISNULL(str) || '\0' == str[0]) {
    valid = false;
  } else {
    valid = true;
    value = strtoll(str, &p_unit, 0);

    if (OB_ISNULL(p_unit)) {
      valid = false;
    } else if (value < 0) {
      valid = false;
    } else if (0 == STRCASECMP("us", p_unit)) {
      value = value * TIME_MICROSECOND;
    } else if (0 == STRCASECMP("ms", p_unit)) {
      value = value * TIME_MILLISECOND;
    } else if ('\0' == *p_unit || 0 == STRCASECMP("s", p_unit)) {
      value = value * TIME_SECOND;
    } else if (0 == STRCASECMP("m", p_unit)) {
      value = value * TIME_MINUTE;
    } else if (0 == STRCASECMP("h", p_unit)) {
      value = value * TIME_HOUR;
    } else if (0 == STRCASECMP("d", p_unit)) {
      value = value * TIME_DAY;
    } else {
      valid = false;
      OB_LOG_RET(WARN, OB_ERR_UNEXPECTED, "set time error", K(str), K(p_unit));
    }
  }

  return value;
}

bool ObConfigAuditModeChecker::check(const ObConfigItem &t) const
{
  ObString v_str(t.str());
  return 0 == v_str.case_compare("NONE") ||
         0 == v_str.case_compare("MYSQL");
}

bool ObConfigBoolParser::get(const char *str, bool &valid)
{
  bool value = true;
  valid = false;

  if (OB_ISNULL(str)) {
    valid = false;
    OB_LOG_RET(WARN, OB_ERR_UNEXPECTED, "Get bool config item fail, str is NULL!");
  } else if (0 == STRCASECMP(str, "false")) {
    valid = true;
    value = false;
  } else if (0 == STRCASECMP(str, "true")) {
    valid = true;
    value = true;
  } else if (0 == STRCASECMP(str, "off")) {
    valid = true;
    value = false;
  } else if (0 == STRCASECMP(str, "on")) {
    valid = true;
    value = true;
  } else if (0 == STRCASECMP(str, "no")) {
    valid = true;
    value = false;
  } else if (0 == STRCASECMP(str, "yes")) {
    valid = true;
    value = true;
  } else if (0 == STRCASECMP(str, "f")) {
    valid = true;
    value = false;
  } else if (0 == STRCASECMP(str, "t")) {
    valid = true;
    value = true;
  } else if (0 == STRCASECMP(str, "1")) {
    valid = true;
    value = true;
  } else if (0 == STRCASECMP(str, "0")) {
    valid = true;
    value = false;
  } else {
    OB_LOG_RET(WARN, OB_ERR_UNEXPECTED, "Get bool config item fail", K(str));
    valid = false;
  }
  return value;
}

bool ObCtxMemoryLimitChecker::check(const ObConfigItem &t) const
{
  uint64_t ctx_id = 0;
  int64_t limit = 0;
  return check(t.str(), ctx_id, limit);
}

bool ObCtxMemoryLimitChecker::check(const char* str, uint64_t& ctx_id, int64_t& limit) const
{
  bool is_valid = false;
  ctx_id = 0;
  limit = 0;
  if ('\0' == str[0]) {
    is_valid = true;
  } else {
    auto len = STRLEN(str);
    for (int64_t i = 0; i + 1 < len && !is_valid; ++i) {
      if (':' == str[i]) {
        limit = ObConfigCapacityParser::get(str + i + 1, is_valid, false);
        if (is_valid) {
          int ret = OB_SUCCESS;
          SMART_VAR(char[OB_MAX_CONFIG_VALUE_LEN], tmp_str) {
            strncpy(tmp_str, str, i);
            tmp_str[i] = '\0';
            is_valid = get_global_ctx_info().is_valid_ctx_name(tmp_str, ctx_id);
          }
        }
      }
    }
  }
  return is_valid && limit >= 0;
}

bool ObCallClientAuthMethodChecker::check(const ObConfigItem &t) const
{
  ObString v_str(t.str());
  return 0 == v_str.case_compare("NONE") ||
         0 == v_str.case_compare("SSL_NO_ENCRYPT") ||
         0 == v_str.case_compare("SSL_IO");
}

bool ObCallServerAuthMethodChecker::is_valid_server_auth_method(const ObString &str) const
{
  return 0 == str.case_compare("NONE") ||
         0 == str.case_compare("SSL_NO_ENCRYPT") ||
         0 == str.case_compare("SSL_IO") ||
         0 == str.case_compare("ALL");
}

bool ObCallServerAuthMethodChecker::check(const ObConfigItem &t) const
{
  bool bret = true;
  int MAX_METHOD_LENGTH = 256;
  char tmp_str[MAX_METHOD_LENGTH];
  size_t str_len = STRLEN(t.str());
  if (str_len >= MAX_METHOD_LENGTH) {
    bret = false;
  } else {
    MEMCPY(tmp_str, t.str(), str_len);
    tmp_str[str_len] = 0;
    ObString str(str_len, reinterpret_cast<const char *>(tmp_str));
    if (NULL == str.find(',')) {
      bret = is_valid_server_auth_method(str);
    } else {
      //split by comma
      char *token = NULL;
      char *save = NULL;
      char *str_token = tmp_str;
      int hint = 0;
      do {
        token = strtok_r(str_token, ",", &save);
        str_token = NULL;
        if (token) {
          hint = 1;
          ObString tmp(STRLEN(token), reinterpret_cast<const char *>(token));
          ObString tmp_to_check = tmp.trim();
          if (is_valid_server_auth_method(tmp_to_check)) {
          } else {
            bret = false;
            break;
          }
        } else {
          if (!hint) {
            bret = false;
          }
          break;
        }
      } while(true);
    }
  }
  return bret;
}

int64_t ObConfigRuntimeFilterChecker::get_runtime_filter_type(const char *str, int64_t len)
{
  int64_t rf_type = -1;
  int64_t l = 0, r = len;
  if (0 == len) {
    rf_type = 0;
  } else {
    int64_t l = 0, r = len;
    bool is_valid = true;
    int flag[3] = {0, 0, 0};
    auto fill_flag = [&] (ObString &p_str) {
      bool valid = true;
      ObString trim_str = p_str.trim();
      if (0 == trim_str.case_compare("bloom_filter")) {
        flag[0]++;
      } else if (0 == trim_str.case_compare("range")) {
        flag[1]++;
      } else if (0 == trim_str.case_compare("in")) {
        flag[2]++;
      } else {
        valid = false;
      }
      if (valid) {
        if (flag[0] > 1 || flag[1] > 1 || flag[2] > 1) {
          valid = false;
        }
      }
      return valid;
    };
    for (int i = 0; i < len && is_valid; ++i) {
      if (str[i] == ',') {
        r = i;
        ObString p_str(r - l, str + l);
        is_valid = fill_flag(p_str);
        l = i + 1;
        continue;
      }
    }
    if (is_valid) {
      ObString p_str(len - l, str + l);
      is_valid = fill_flag(p_str);
    }
    if (is_valid) {
      rf_type = flag[0] << 1 |
                flag[1] << 2 |
                flag[2] << 3;
    } else {
      rf_type = -1;
    }

  }
  return rf_type;
}

bool ObConfigRuntimeFilterChecker::check(const ObConfigItem &t) const
{
  int64_t len = strlen(t.str());
  const char *p = t.str();
  int64_t rf_type = get_runtime_filter_type(t.str(), len);
  return rf_type >= 0;
}

bool ObConfigSQLTlsVersionChecker::check(const ObConfigItem &t) const
{
  const ObString tmp_str(t.str());
  return 0 == tmp_str.case_compare("NONE")    ||
         0 == tmp_str.case_compare("TLSV1")   ||
         0 == tmp_str.case_compare("TLSV1.1") ||
         0 == tmp_str.case_compare("TLSV1.2") ||
         0 == tmp_str.case_compare("TLSV1.3");
}

int ObModeConfigParserUitl::parse_item_to_kv(char *item, ObString &key, ObString &value, const char* delim)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(item)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "item is NULL", K(ret));
  } else {
    // key
    char *save_ptr = NULL;
    char *key_ptr = STRTOK_R(item, delim, &save_ptr);
    ObString tmp_key(key_ptr);
    key = tmp_key.trim();
    // value
    ObString tmp_value(save_ptr);
    value = tmp_value.trim();
    if (value.case_compare("on") != 0 && value.case_compare("off") != 0) {
      ret = OB_INVALID_CONFIG;
      OB_LOG(WARN, "item value is invalid", K(ret), K(value));
    }
  }
  return ret;
}

int ObModeConfigParserUitl::format_mode_str(const char *src, int64_t src_len, char *dst, int64_t dst_len)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(src) || OB_UNLIKELY(src_len <=0)
      || OB_ISNULL(dst) || dst_len < (3 * src_len)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid arguments", KR(ret), KP(src), KP(dst), K(src_len), K(dst_len));
  } else {
    const char *source_str = src;
    const char *locate_str = NULL;
    int64_t source_left_len = src_len;
    int32_t locate = -1;
    int64_t pos = 0;
    while (OB_SUCC(ret) && (source_left_len > 0)
           && (NULL != (locate_str = STRCHR(source_str, ',')))) {
      locate = static_cast<int32_t>(locate_str - source_str);
      if (OB_FAIL(databuff_printf(dst, dst_len, pos, "%.*s , ", locate, source_str))) {
      } else {
        source_str = locate_str + 1;
        source_left_len -= (locate + 1);
      }
    }

    if (OB_SUCC(ret) && source_left_len > 0) {
      if (OB_FAIL(databuff_printf(dst, dst_len, pos, "%s", source_str))) {
      }
    }
  }
  return ret;
}

int ObModeConfigParserUitl::get_kv_list(char *str, ObIArray<std::pair<ObString, ObString>> &kv_list, const char* delim)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(str)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "item is NULL", K(ret));
  } else {
    ObString key;
    ObString value;
    char *save_ptr = NULL;
    char *token = STRTOK_R(str, ",", &save_ptr);
    while (OB_SUCC(ret) && OB_NOT_NULL(token)) {
      // trim left space
      while (*token == ' ') token++;
      // trim right space
      uint64_t len = strlen(token);
      while (len > 0 && token[len - 1] == ' ') token[--len] = '\0';
      // check and set mode
      if (OB_FAIL(parse_item_to_kv(token, key, value, delim))) {
      } else if (OB_FAIL(kv_list.push_back(std::make_pair(key, value)))) {
      } else {
        token = STRTOK_R(NULL, ",", &save_ptr);
      }
    }
  }
  return ret;
}

bool ObConfigSQLSpillCompressionCodecChecker::check(const ObConfigItem &t) const
{
  bool is_valid = false;
  for (int i = 0; i < ARRAYSIZEOF(common::sql_temp_store_compress_funcs) && !is_valid; ++i) {
    if (0 == ObString::make_string(sql_temp_store_compress_funcs[i]).case_compare(t.str())) {
      is_valid = true;
    }
  }
  return is_valid;
}

bool ObParallelDDLControlParser::parse(const char *str, uint8_t *arr, int64_t len)
{
  bool bret = true;
  ObParallelDDLControlMode ddl_mode;
  if (OB_ISNULL(str) || OB_ISNULL(arr)) {
    bret = false;
    OB_LOG_RET(WARN, OB_ERR_UNEXPECTED, "Get config item failed", KP(str), KP(arr));
  } else if (strlen(str) == 0) {
    // do nothing
  } else {
    int tmp_ret = OB_SUCCESS;
    ObSEArray<std::pair<ObString, ObString>, 1> kv_list;
    int64_t str_len = strlen(str);
    const int64_t buf_len = 3 * str_len; // need replace ',' to ' , '
    char buf[buf_len];
    MEMSET(buf, 0, sizeof(buf));
    MEMCPY(buf, str, str_len);
    if (OB_TMP_FAIL(ObModeConfigParserUitl::format_mode_str(str, str_len, buf, buf_len))) {
      bret = false;
      OB_LOG_RET(WARN, tmp_ret, "fail to format mode str", K(str));
    } else if (OB_TMP_FAIL(ObModeConfigParserUitl::get_kv_list(buf, kv_list, ":"))) {
      bret = false;
      OB_LOG_RET(WARN, tmp_ret, "fail to get kv list", K(str));
    } else {
      for (int64_t i = 0; bret && i < kv_list.count(); ++i) {
        uint8_t mode = MODE_DEFAULT;
        if (kv_list.at(i).second.case_compare("on") == 0) {
          mode = MODE_ON;
        } else if (kv_list.at(i).second.case_compare("off") == 0) {
          mode = MODE_OFF;
        } else {
          bret = false;
          OB_LOG_RET(WARN, OB_INVALID_CONFIG, "unknown mode type", K(kv_list.at(i).second));
        }
        ObParallelDDLControlMode::ObParallelDDLType ddl_type = ObParallelDDLControlMode::MAX_TYPE;
        if (!bret) {
          // do nothing
        } else if (OB_TMP_FAIL(ObParallelDDLControlMode::string_to_ddl_type(kv_list.at(i).first, ddl_type))) {
          bret = false;
          OB_LOG_RET(WARN, tmp_ret, "fail to trans string ddl_type", K(kv_list.at(i).first));
        } else if (OB_TMP_FAIL(ddl_mode.set_parallel_ddl_mode(ddl_type, mode))) {
          bret = false;
          OB_LOG_RET(WARN, tmp_ret, "fail to set parallel ddl mode", K(ddl_type), K(mode));
        }
      }
    }
  }
  if (bret) {
    for (uint64_t i = 0; i < 8; ++i) {
      arr[i] = static_cast<uint8_t>((ddl_mode.get_value() >> (i * 8)) & 0xFF);
    }
  }
  return bret;
}

bool ObConfigDefaultTableOrganizationChecker::check(const obcall::ObAdminSetConfigItem &t)
{
  const ObString tmp_str(t.value_.size(), t.value_.ptr());
  return 0 == tmp_str.case_compare("INDEX")
      || 0 == tmp_str.case_compare("HEAP");
}


bool ObConfigEnableHashRollupChecker::check(const ObConfigItem &t) const
{
  int bret = false;
  common::ObString tmp_str(t.str());
  bret = (0 == tmp_str.case_compare("auto")
          || 0 == tmp_str.case_compare("forced")
          || 0 == tmp_str.case_compare("disabled"));
  return bret;
}

bool ObConfigNonStdCmpLevelChecker::check(const ObConfigItem &t) const
{
  int bret = false;
  common::ObString tmp_str(t.str());
  bret = (0 == tmp_str.case_compare("none")
          || 0 == tmp_str.case_compare("equal")
          || 0 == tmp_str.case_compare("range"));
  return bret;
}

bool ObHNSWIterFilterScanNumChecker::check(const ObConfigItem &t) const
{
  bool is_valid = false;
  int64_t iter_max_scan_num = ObConfigIntParser::get(t.str(), is_valid);
  if (is_valid) {
    is_valid = (iter_max_scan_num >= MIN_HNSW_ITER_SCAN_NUMS &&
                iter_max_scan_num <= MAX_HNSW_ITER_SCAN_NUMS);
  }
  return is_valid;
}

} // end of namepace common

namespace share
{
namespace schema
{

using namespace common;

static const char *const DDL_TYPES[] = {
  "TRUNCATE_TABLE",
  "SET_COMMENT",
  "CREATE_INDEX",
  "CREATE_VIEW",
  "DROP_TABLE"
};

static const char *const UNSUPPORTED_DDL_TYPES[] = {
  "CREATE_VIEW"
};

int ObParallelDDLControlMode::string_to_ddl_type(const ObString &ddl_string, ObParallelDDLType &ddl_type)
{
  int ret = OB_SUCCESS;
  ddl_type = MAX_TYPE;
  STATIC_ASSERT(ARRAYSIZEOF(DDL_TYPES) == MAX_TYPE, "size count not match");
  for (uint64_t i = 0; MAX_TYPE == ddl_type && i < ARRAYSIZEOF(DDL_TYPES); ++i) {
    if (0 == ddl_string.case_compare(DDL_TYPES[i])) {
      ddl_type = static_cast<ObParallelDDLType>(i);
    }
  }
  if (OB_UNLIKELY(MAX_TYPE == ddl_type)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "unknown ddl_type", KR(ret), K(ddl_string));
  }
  return ret;
}

int ObParallelDDLControlMode::set_value(const ObConfigModeItem &mode_item)
{
  int ret = OB_SUCCESS;
  const uint8_t *values = mode_item.get_value();
  if (OB_ISNULL(values)) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "mode item's value_ is null ptr", KR(ret));
  } else {
    STATIC_ASSERT(sizeof(value_) / sizeof(uint8_t) <= ObConfigModeItem::MAX_MODE_BYTES,
                  "value_ size overflow");
    STATIC_ASSERT(MAX_TYPE * 2 <= sizeof(value_) * 8, "type size overflow");
    value_ = 0;
    for (uint64_t i = 0; i < sizeof(value_); ++i) {
      value_ |= static_cast<uint64_t>(values[i]) << (8 * i);
    }
  }
  return ret;
}

int ObParallelDDLControlMode::set_parallel_ddl_mode(const ObParallelDDLType type, const uint8_t mode)
{
  int ret = OB_SUCCESS;
  if (TRUNCATE_TABLE <= type && type < MAX_TYPE) {
    const uint64_t shift = static_cast<uint64_t>(type);
    if (!check_mode_valid_(mode)) {
      ret = OB_INVALID_ARGUMENT;
      OB_LOG(WARN, "mode invalid", KR(ret), K(mode));
    } else {
      const uint64_t mask = MASK << (shift * MASK_SIZE);
      value_ = (value_ & ~mask) | (static_cast<uint64_t>(mode) << (shift * MASK_SIZE));
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "type invalid", KR(ret), K(type));
  }
  return ret;
}

int ObParallelDDLControlMode::is_parallel_ddl(const ObParallelDDLType type, bool &is_parallel)
{
  int ret = OB_SUCCESS;
  is_parallel = true;
  if (TRUNCATE_TABLE <= type && type < MAX_TYPE) {
    const uint64_t shift = static_cast<uint64_t>(type);
    const uint8_t value = static_cast<uint8_t>((value_ >> (shift * MASK_SIZE)) & MASK);
    if (ObParallelDDLControlParser::MODE_OFF == value) {
      is_parallel = false;
    } else if (ObParallelDDLControlParser::MODE_ON == value
               || ObParallelDDLControlParser::MODE_DEFAULT == value) {
      is_parallel = true;
    } else {
      ret = OB_ERR_UNEXPECTED;
      OB_LOG(WARN, "invalid value unexpected", KR(ret), K(value));
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "type invalid", KR(ret), K(type));
  }
  return ret;
}

int ObParallelDDLControlMode::is_parallel_ddl_enable(const ObParallelDDLType ddl_type, bool &is_parallel)
{
  int ret = OB_SUCCESS;
  is_parallel = true;
  ObParallelDDLControlMode cfg;
  if (OB_FAIL(GCONF._parallel_ddl_control.init_mode(cfg))) {
  } else if (OB_FAIL(cfg.is_parallel_ddl(ddl_type, is_parallel))) {
  }
  return ret;
}

int ObParallelDDLControlMode::generate_parallel_ddl_control_config_for_create_tenant(ObSqlString &config_value)
{
  int ret = OB_SUCCESS;
  config_value.reset();
  for (int i = 0; OB_SUCC(ret) && i < ARRAYSIZEOF(DDL_TYPES); ++i) {
    const ObString ddl_type = DDL_TYPES[i];
    bool unsupported = false;
    for (int j = 0; !unsupported && j < ARRAYSIZEOF(UNSUPPORTED_DDL_TYPES); ++j) {
      unsupported = 0 == ddl_type.case_compare(UNSUPPORTED_DDL_TYPES[j]);
    }
    if (unsupported) {
      // skip
    } else if (OB_FAIL(config_value.append_fmt("%s:ON, ", DDL_TYPES[i]))) {
    }
  }
  if (config_value.is_valid()) {
    config_value.set_length(config_value.length() - 2);
  }
  return ret;
}

} // namespace schema
} // namespace share
} // end of namespace oceanbase
