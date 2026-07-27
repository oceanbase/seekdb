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

#ifndef OCEANBASE_SHARE_CONFIG_OB_CONFIG_HELPER_H_
#define OCEANBASE_SHARE_CONFIG_OB_CONFIG_HELPER_H_

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif
#include "lib/hash/ob_hashmap.h"
#include "lib/hash_func/murmur_hash.h"
#include "lib/hash/ob_hashutils.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace obcall
{
struct ObAdminSetConfigItem;
}

namespace common
{
class ObConfigItem;
class ObConfigIntegralItem;
class ObConfigAlwaysTrue;

class ObConfigUpdateCb
{
public:
  ObConfigUpdateCb() {}
  virtual ~ObConfigUpdateCb() {}
  virtual int64_t update_version() = 0;

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigUpdateCb);
};

class ObConfigChecker
{
public:
  ObConfigChecker() {}
  virtual ~ObConfigChecker() {}
  virtual bool check(const ObConfigItem &t) const = 0;

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigChecker);
};

class ObConfigAlwaysTrue
  : public ObConfigChecker
{
public:
  ObConfigAlwaysTrue() {}
  virtual ~ObConfigAlwaysTrue() {}
  bool check(const ObConfigItem &t) const { UNUSED(t); return true; }

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigAlwaysTrue);
};

class ObConfigIpChecker
  : public ObConfigChecker
{
public:
  ObConfigIpChecker() {}
  virtual ~ObConfigIpChecker() {}
  bool check(const ObConfigItem &t) const;

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigIpChecker);
};

class ObConfigConsChecker
  : public ObConfigChecker
{
public:
  ObConfigConsChecker(const ObConfigChecker *left, const ObConfigChecker *right)
      : left_(left), right_(right)
  {}
  virtual ~ObConfigConsChecker();
  bool check(const ObConfigItem &t) const;

private:
  const ObConfigChecker *left_;
  const ObConfigChecker *right_;
  DISALLOW_COPY_AND_ASSIGN(ObConfigConsChecker);
};

class ObConfigEvenIntChecker
  : public ObConfigChecker
{
public:
  ObConfigEvenIntChecker() {}
  virtual ~ObConfigEvenIntChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigEvenIntChecker);
};

class ObConfigFreezeTriggerIntChecker
{
public:
  static bool check(const obcall::ObAdminSetConfigItem &t);
private:
  static int64_t get_write_throttle_trigger_percentage_();
  DISALLOW_COPY_AND_ASSIGN(ObConfigFreezeTriggerIntChecker);
};
class ObConfigTxShareMemoryLimitChecker
{
public:
  static bool check(const obcall::ObAdminSetConfigItem &t);

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigTxShareMemoryLimitChecker);
};
class ObConfigMemstoreLimitChecker
{
public:
  static bool check(const obcall::ObAdminSetConfigItem &t);

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigMemstoreLimitChecker);
};

class ObConfigTxDataLimitChecker
{
public:
  static bool check(const obcall::ObAdminSetConfigItem &t);

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigTxDataLimitChecker);
};
class ObConfigMdsLimitChecker
{
public:
  static bool check(const obcall::ObAdminSetConfigItem &t);

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigMdsLimitChecker);
};

class ObConfigWriteThrottleTriggerIntChecker
{
public:
  static bool check(const obcall::ObAdminSetConfigItem &t);
private:
  static int64_t get_freeze_trigger_percentage_();
  DISALLOW_COPY_AND_ASSIGN(ObConfigWriteThrottleTriggerIntChecker);
};

//only used for RS checking
class ObConfigLogDiskLimitThresholdIntChecker
{
public:
  static bool check(const obcall::ObAdminSetConfigItem &t);
private:
  static int64_t get_log_disk_throttling_percentage_();
  DISALLOW_COPY_AND_ASSIGN(ObConfigLogDiskLimitThresholdIntChecker);
};

//only used for RS checking
class ObConfigLogDiskThrottlingPercentageIntChecker
{
public:
  static bool check(const obcall::ObAdminSetConfigItem &t);
private:
  static int64_t get_log_disk_utilization_limit_threshold_();
  DISALLOW_COPY_AND_ASSIGN(ObConfigLogDiskThrottlingPercentageIntChecker);
};

class ObConfigTabletSizeChecker
  : public ObConfigChecker
{
public:
  ObConfigTabletSizeChecker() {}
  virtual ~ObConfigTabletSizeChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigTabletSizeChecker);
};

class ObConfigStaleTimeChecker
  : public ObConfigChecker
{
public:
  ObConfigStaleTimeChecker() {}
  virtual ~ObConfigStaleTimeChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigStaleTimeChecker);
};

class ObConfigCompressFuncChecker
  : public ObConfigChecker
{
public:
  ObConfigCompressFuncChecker() {}
  virtual ~ObConfigCompressFuncChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigCompressFuncChecker);
};

class ObConfigPerfCompressFuncChecker
  : public ObConfigChecker
{
public:
  ObConfigPerfCompressFuncChecker() {}
  virtual ~ObConfigPerfCompressFuncChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigPerfCompressFuncChecker);
};

class ObConfigTempStoreFormatChecker
  : public ObConfigChecker
{
public:
  ObConfigTempStoreFormatChecker() {}
  virtual ~ObConfigTempStoreFormatChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigTempStoreFormatChecker);
};

class ObConfigPxBFGroupSizeChecker
  : public ObConfigChecker
{
public:
  ObConfigPxBFGroupSizeChecker() {}
  virtual ~ObConfigPxBFGroupSizeChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigPxBFGroupSizeChecker);
};

class ObConfigRowFormatChecker
  : public ObConfigChecker
{
public:
  ObConfigRowFormatChecker() {}
  virtual ~ObConfigRowFormatChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigRowFormatChecker);
};

class ObConfigMaxSyslogFileCountChecker
  : public ObConfigChecker
{
public:
  ObConfigMaxSyslogFileCountChecker() {}
  virtual ~ObConfigMaxSyslogFileCountChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigMaxSyslogFileCountChecker);
};

class ObConfigSyslogCompressFuncChecker
  : public ObConfigChecker
{
public:
  ObConfigSyslogCompressFuncChecker() {}
  virtual ~ObConfigSyslogCompressFuncChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigSyslogCompressFuncChecker);
};

class ObConfigSyslogFileUncompressedCountChecker
  : public ObConfigChecker
{
public:
  ObConfigSyslogFileUncompressedCountChecker() {}
  virtual ~ObConfigSyslogFileUncompressedCountChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigSyslogFileUncompressedCountChecker);
};

class ObConfigLogLevelChecker
  : public ObConfigChecker
{
public:
  ObConfigLogLevelChecker() {}
  virtual ~ObConfigLogLevelChecker() {};
  bool check(const ObConfigItem &t) const;

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigLogLevelChecker);
};

class ObConfigAuditTrailChecker
  : public ObConfigChecker
{
public:
  ObConfigAuditTrailChecker() {}
  virtual ~ObConfigAuditTrailChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigAuditTrailChecker);
};

class ObConfigAuditLogCompressionChecker
  : public ObConfigChecker
{
public:
  ObConfigAuditLogCompressionChecker() {}
  virtual ~ObConfigAuditLogCompressionChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigAuditLogCompressionChecker);
};

class ObConfigAuditLogPathChecker
  : public ObConfigChecker
{
public:
  ObConfigAuditLogPathChecker() {}
  virtual ~ObConfigAuditLogPathChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigAuditLogPathChecker);
};

class ObConfigAuditLogFormatChecker
  : public ObConfigChecker
{
public:
  ObConfigAuditLogFormatChecker() {}
  virtual ~ObConfigAuditLogFormatChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigAuditLogFormatChecker);
};

class ObConfigAuditLogQuerySQLChecker
  : public ObConfigChecker
{
public:
  ObConfigAuditLogQuerySQLChecker() {}
  virtual ~ObConfigAuditLogQuerySQLChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigAuditLogQuerySQLChecker);
};

class ObConfigAuditLogStrategyChecker
  : public ObConfigChecker
{
public:
  ObConfigAuditLogStrategyChecker() {}
  virtual ~ObConfigAuditLogStrategyChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigAuditLogStrategyChecker);
};

class ObConfigWorkAreaPolicyChecker
  : public ObConfigChecker
{
public:
  ObConfigWorkAreaPolicyChecker() {}
  virtual ~ObConfigWorkAreaPolicyChecker() {};
  bool check(const ObConfigItem &t) const;

private:
  static constexpr const char *MANUAL = "MANUAL";
  static constexpr const char *AUTO = "AUTO";

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigWorkAreaPolicyChecker);
};

class ObConfigMemoryLimitChecker
  : public ObConfigChecker
{
public:
  ObConfigMemoryLimitChecker() {}
  virtual ~ObConfigMemoryLimitChecker() {};
  bool check(const ObConfigItem &t) const;

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigMemoryLimitChecker);
};

class ObConfigAuditModeChecker
  : public ObConfigChecker
{
public:
  ObConfigAuditModeChecker() {}
  virtual ~ObConfigAuditModeChecker() {}

  bool check(const ObConfigItem &t) const;

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigAuditModeChecker);
};

class ObLogDiskUsagePercentageChecker
  : public ObConfigChecker
{
public:
  ObLogDiskUsagePercentageChecker() {}
  virtual ~ObLogDiskUsagePercentageChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObLogDiskUsagePercentageChecker);
};

class ObCtxMemoryLimitChecker
  : public ObConfigChecker
{
public:
  ObCtxMemoryLimitChecker() {}
  virtual ~ObCtxMemoryLimitChecker() {};
  bool check(const ObConfigItem &t) const;
  bool check(const char* str, uint64_t& ctx_id, int64_t& limit) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCtxMemoryLimitChecker);
};

class ObConfigEnableDefensiveChecker
  : public ObConfigChecker
{
public:
  ObConfigEnableDefensiveChecker() {}
  virtual ~ObConfigEnableDefensiveChecker() {};
  bool check(const ObConfigItem &t) const;

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigEnableDefensiveChecker);
};

class ObConfigRuntimeFilterChecker
  : public ObConfigChecker
{
public:
  ObConfigRuntimeFilterChecker() {}
  virtual ~ObConfigRuntimeFilterChecker() {}
  bool check(const ObConfigItem &t) const;
  static int64_t get_runtime_filter_type(const char *str, int64_t len);
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigRuntimeFilterChecker);
};

struct ObDutyTime
{
  ObDutyTime() : hour_(0), min_(0), sec_(0) {}
  bool is_valid() const
  {
    return hour_ >= 0 && hour_ <= 24
        && min_ >= 0 && min_ <= 60
        && sec_ >= 0 && sec_ <= 60;
  }

  int32_t hour_;
  int32_t min_;
  int32_t sec_;
};

struct ObDutyDuration
{
  ObDutyDuration() : begin_(), end_(), not_set_(true) {}
  bool is_valid() const { return not_set_ || (begin_.is_valid() && end_.is_valid()); }

  ObDutyTime begin_;
  ObDutyTime end_;
  bool not_set_;
};

class ObDutyDurationUtil
{
public:
  static int parse(const char *str, ObDutyDuration &duration);
  static bool current_in_duration(const ObDutyDuration &duration);

private:
  static bool extract_value(const char *ptr, uint64_t len, int32_t &value);
  static int parse_time(common::ObString &input, ObDutyTime &time);
};

class ObVecIndexOptDutyTimeChecker : public ObConfigChecker {
public:
  ObVecIndexOptDutyTimeChecker()
  {}
  virtual ~ObVecIndexOptDutyTimeChecker(){};
  bool check(const ObConfigItem& t) const;

private:
  DISALLOW_COPY_AND_ASSIGN(ObVecIndexOptDutyTimeChecker);
};

// config item container
class ObConfigStringKey
{
public:
  ObConfigStringKey() { MEMSET(str_, 0, sizeof(str_)); }
  explicit ObConfigStringKey(const char *str);
  explicit ObConfigStringKey(const ObString &string);
  virtual ~ObConfigStringKey() {}
  uint64_t hash() const;
  inline int hash(uint64_t &hash_val) const { hash_val = hash(); return OB_SUCCESS; }

  // case unsensitive
  bool operator == (const ObConfigStringKey &str) const
  {
    return 0 == STRCASECMP(str.str_, this->str_);
  }

  const char *str() const { return str_; }

private:
  char str_[OB_MAX_CONFIG_NAME_LEN];
  // ObConfigContainer container uses the object's copy constructor, cannot be prohibited
  //DISALLOW_COPY_AND_ASSIGN(ObConfigStringKey);
};
inline ObConfigStringKey::ObConfigStringKey(const char *str)
{
  int64_t pos = 0;
  (void) databuff_printf(str_, sizeof(str_), pos, "%s", str);
}

inline ObConfigStringKey::ObConfigStringKey(const ObString &string)
{
  int64_t pos = 0;
  (void) databuff_printf(str_, sizeof(str_), pos, "%.*s", string.length(), string.ptr());
}
inline uint64_t ObConfigStringKey::hash() const
{
  return 0; // murmurhash(str_, (int32_t)STRLEN(str_), 0); // murmurhash is case sensitive
}

template <class Key, class Value, int num>
class __ObConfigContainer
  : public hash::ObHashMap<Key, Value *, hash::NoPthreadDefendMode>
{
public:
  __ObConfigContainer()
  {
    this->create(num,
                 oceanbase::common::ObModIds::OB_HASH_BUCKET_CONF_CONTAINER,
                 oceanbase::common::ObModIds::OB_HASH_NODE_CONF_CONTAINER);
  }
 virtual ~__ObConfigContainer() {}

private:
  DISALLOW_COPY_AND_ASSIGN(__ObConfigContainer);
};

class ObConfigIntParser
{
public:
  ObConfigIntParser() {}
  virtual ~ObConfigIntParser() {}
  static int64_t get(const char *str, bool &valid);
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigIntParser);
};

class ObConfigCapacityParser
{
public:
  ObConfigCapacityParser() {}
  virtual ~ObConfigCapacityParser() {}
  static int64_t get(const char *str, bool &valid, bool check_unit = true, bool use_byte = false);
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigCapacityParser);
};

class ObConfigReadableIntParser
{
public:
  ObConfigReadableIntParser() {}
  virtual ~ObConfigReadableIntParser() {}
  static int64_t get(const char *str, bool &valid);

private:
  enum INT_UNIT
  {
    // Typically for a number, it can be written as 1k, 1m, respectively representing
    // 1000(kilo), 1000000(million)
    // billion not supported, avoid confusion with capacity byte's 1b
    UNIT_K = 1000,
    UNIT_M = 1000000,
  };
  DISALLOW_COPY_AND_ASSIGN(ObConfigReadableIntParser);
};

class ObConfigTimeParser
{
public:
  ObConfigTimeParser() {}
  ~ObConfigTimeParser() {}
  static int64_t get(const char *str, bool &valid);
private:
  enum TIME_UNIT : int64_t
  {
    TIME_MICROSECOND = 1LL,
    TIME_MILLISECOND = 1000LL,
    TIME_SECOND = 1000LL * 1000,
    TIME_MINUTE = 60LL * 1000 * 1000,
    TIME_HOUR = 3600LL * 1000 * 1000,
    TIME_DAY = 86400LL * 1000 * 1000,
  };
  DISALLOW_COPY_AND_ASSIGN(ObConfigTimeParser);
};

struct ObConfigBoolParser
{
  static bool get(const char *str, bool &valid);
};

class ObCallClientAuthMethodChecker
  : public ObConfigChecker
{
public:
  ObCallClientAuthMethodChecker() {}
  virtual ~ObCallClientAuthMethodChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCallClientAuthMethodChecker);
};

class ObCallServerAuthMethodChecker
  : public ObConfigChecker
{
public:
  ObCallServerAuthMethodChecker() {}
  virtual ~ObCallServerAuthMethodChecker() {}
  bool check(const ObConfigItem &t) const;
  bool is_valid_server_auth_method(const ObString &str) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCallServerAuthMethodChecker);
};

class ObConfigSQLTlsVersionChecker
  : public ObConfigChecker
{
public:
  ObConfigSQLTlsVersionChecker() {}
  virtual ~ObConfigSQLTlsVersionChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigSQLTlsVersionChecker);
};

class ObConfigSQLSpillCompressionCodecChecker
  : public ObConfigChecker
{
public:
  ObConfigSQLSpillCompressionCodecChecker() {}
  virtual ~ObConfigSQLSpillCompressionCodecChecker() {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigSQLSpillCompressionCodecChecker);
};

class ObModeConfigParserUitl
{
public:
  // parse config item like: "xxx=yyy", "xxx:yyy"
  static int parse_item_to_kv(char *item, ObString &key, ObString &value, const char* delim = "=");
  static int get_kv_list(char *str, ObIArray<std::pair<ObString, ObString>> &kv_list, const char* delim = "=");
  // format str for split config item
  static int format_mode_str(const char *src, int64_t src_len, char *dst, int64_t dst_len);
};

class ObConfigParser
{
public:
  ObConfigParser() {}
  virtual ~ObConfigParser() {}
  virtual bool parse(const char *str, uint8_t *arr, int64_t len) = 0;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigParser);
};

class ObParallelDDLControlParser : public ObConfigParser
{
public:
  ObParallelDDLControlParser() {}
  virtual ~ObParallelDDLControlParser() {}
  virtual bool parse(const char *str, uint8_t *arr, int64_t len) override;
public:
  static const uint8_t MODE_DEFAULT = 0b00;
  static const uint8_t MODE_OFF = 0b01;
  static const uint8_t MODE_ON = 0b10;
private:
  DISALLOW_COPY_AND_ASSIGN(ObParallelDDLControlParser);
};

typedef __ObConfigContainer<ObConfigStringKey,
                            ObConfigItem, OB_MAX_CONFIG_NUMBER> ObConfigContainer;

class ObConfigVectorMemoryChecker
{
public:
  static bool check(const obcall::ObAdminSetConfigItem &t);

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigVectorMemoryChecker);
};

class ObConfigDefaultTableOrganizationChecker : public ObConfigChecker
{
public:
  ObConfigDefaultTableOrganizationChecker() {}
  virtual ~ObConfigDefaultTableOrganizationChecker() {}
  static bool check(const obcall::ObAdminSetConfigItem &t);
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigDefaultTableOrganizationChecker);
};

class ObConfigEnableHashRollupChecker: public ObConfigChecker
{
public:
  ObConfigEnableHashRollupChecker()
  {}
  virtual ~ObConfigEnableHashRollupChecker()
  {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigEnableHashRollupChecker);
};

class ObConfigNonStdCmpLevelChecker: public ObConfigChecker
{
public:
  ObConfigNonStdCmpLevelChecker()
  {}
  virtual ~ObConfigNonStdCmpLevelChecker()
  {}
  bool check(const ObConfigItem &t) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigNonStdCmpLevelChecker);
};

class ObHNSWIterFilterScanNumChecker
  : public ObConfigChecker
{
public:
  ObHNSWIterFilterScanNumChecker() {}
  virtual ~ObHNSWIterFilterScanNumChecker() {}
  bool check(const ObConfigItem &t) const;
  static constexpr int64_t MAX_HNSW_ITER_SCAN_NUMS = INT64_MAX;
  static constexpr int64_t MIN_HNSW_ITER_SCAN_NUMS = 0;
private:
  DISALLOW_COPY_AND_ASSIGN(ObHNSWIterFilterScanNumChecker);
};


} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SHARE_CONFIG_OB_CONFIG_HELPER_H_
