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
class ObConfigChecker
{
public:
  ObConfigChecker() {}
  virtual ~ObConfigChecker() {}
  virtual bool check(const char *text) const = 0;

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigChecker);
};

class ObConfigEvenIntChecker
  : public ObConfigChecker
{
public:
  ObConfigEvenIntChecker() {}
  virtual ~ObConfigEvenIntChecker() {}
  bool check(const char *text) const;
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
  bool check(const char *text) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigTabletSizeChecker);
};

class ObConfigStaleTimeChecker
  : public ObConfigChecker
{
public:
  ObConfigStaleTimeChecker() {}
  virtual ~ObConfigStaleTimeChecker() {}
  bool check(const char *text) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigStaleTimeChecker);
};

class ObConfigCompressFuncChecker
  : public ObConfigChecker
{
public:
  ObConfigCompressFuncChecker() {}
  virtual ~ObConfigCompressFuncChecker() {}
  bool check(const char *text) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigCompressFuncChecker);
};

class ObConfigTempStoreFormatChecker
  : public ObConfigChecker
{
public:
  ObConfigTempStoreFormatChecker() {}
  virtual ~ObConfigTempStoreFormatChecker() {}
  bool check(const char *text) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigTempStoreFormatChecker);
};

class ObConfigPxBFGroupSizeChecker
  : public ObConfigChecker
{
public:
  ObConfigPxBFGroupSizeChecker() {}
  virtual ~ObConfigPxBFGroupSizeChecker() {}
  bool check(const char *text) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigPxBFGroupSizeChecker);
};

class ObConfigRowFormatChecker
  : public ObConfigChecker
{
public:
  ObConfigRowFormatChecker() {}
  virtual ~ObConfigRowFormatChecker() {}
  bool check(const char *text) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigRowFormatChecker);
};

class ObConfigMaxSyslogFileCountChecker
  : public ObConfigChecker
{
public:
  ObConfigMaxSyslogFileCountChecker() {}
  virtual ~ObConfigMaxSyslogFileCountChecker() {}
  bool check(const char *text) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigMaxSyslogFileCountChecker);
};

class ObConfigSyslogCompressFuncChecker
  : public ObConfigChecker
{
public:
  ObConfigSyslogCompressFuncChecker() {}
  virtual ~ObConfigSyslogCompressFuncChecker() {}
  bool check(const char *text) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigSyslogCompressFuncChecker);
};

class ObConfigSyslogFileUncompressedCountChecker
  : public ObConfigChecker
{
public:
  ObConfigSyslogFileUncompressedCountChecker() {}
  virtual ~ObConfigSyslogFileUncompressedCountChecker() {}
  bool check(const char *text) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigSyslogFileUncompressedCountChecker);
};

class ObConfigLogLevelChecker
  : public ObConfigChecker
{
public:
  ObConfigLogLevelChecker() {}
  virtual ~ObConfigLogLevelChecker() {};
  bool check(const char *text) const;

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigLogLevelChecker);
};

class ObConfigWorkAreaPolicyChecker
  : public ObConfigChecker
{
public:
  ObConfigWorkAreaPolicyChecker() {}
  virtual ~ObConfigWorkAreaPolicyChecker() {};
  bool check(const char *text) const;

private:
  static constexpr const char *MANUAL = "MANUAL";
  static constexpr const char *AUTO = "AUTO";

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigWorkAreaPolicyChecker);
};

class ObParallelDDLControlChecker : public ObConfigChecker
{
public:
  bool check(const char *text) const override;
};

class MemoryBudgetConfigChecker
  : public ObConfigChecker
{
public:
  MemoryBudgetConfigChecker() {}
  virtual ~MemoryBudgetConfigChecker() {};
  bool check(const char *text) const;

private:
  DISALLOW_COPY_AND_ASSIGN(MemoryBudgetConfigChecker);
};

class KVCacheMemoryLimitConfigChecker
  : public ObConfigChecker
{
public:
  KVCacheMemoryLimitConfigChecker() {}
  virtual ~KVCacheMemoryLimitConfigChecker() {}
  bool check(const char *text) const;

private:
  DISALLOW_COPY_AND_ASSIGN(KVCacheMemoryLimitConfigChecker);
};

class ObCtxMemoryLimitChecker
  : public ObConfigChecker
{
public:
  ObCtxMemoryLimitChecker() {}
  virtual ~ObCtxMemoryLimitChecker() {};
  bool check(const char *text) const;
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
  bool check(const char *text) const;

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigEnableDefensiveChecker);
};

class ObConfigRuntimeFilterChecker
  : public ObConfigChecker
{
public:
  ObConfigRuntimeFilterChecker() {}
  virtual ~ObConfigRuntimeFilterChecker() {}
  bool check(const char *text) const;
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
  bool check(const char *text) const;

private:
  DISALLOW_COPY_AND_ASSIGN(ObVecIndexOptDutyTimeChecker);
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

class ObCallClientAuthMethodChecker
  : public ObConfigChecker
{
public:
  ObCallClientAuthMethodChecker() {}
  virtual ~ObCallClientAuthMethodChecker() {}
  bool check(const char *text) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCallClientAuthMethodChecker);
};

class ObCallServerAuthMethodChecker
  : public ObConfigChecker
{
public:
  ObCallServerAuthMethodChecker() {}
  virtual ~ObCallServerAuthMethodChecker() {}
  bool check(const char *text) const;
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
  bool check(const char *text) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigSQLTlsVersionChecker);
};

class ObConfigSQLSpillCompressionCodecChecker
  : public ObConfigChecker
{
public:
  ObConfigSQLSpillCompressionCodecChecker() {}
  virtual ~ObConfigSQLSpillCompressionCodecChecker() {}
  bool check(const char *text) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigSQLSpillCompressionCodecChecker);
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
  bool check(const char *text) const;
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
  bool check(const char *text) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigNonStdCmpLevelChecker);
};

class ObHNSWIterFilterScanNumChecker
  : public ObConfigChecker
{
public:
  ObHNSWIterFilterScanNumChecker() {}
  virtual ~ObHNSWIterFilterScanNumChecker() {}
  bool check(const char *text) const;
  static constexpr int64_t MAX_HNSW_ITER_SCAN_NUMS = INT64_MAX;
  static constexpr int64_t MIN_HNSW_ITER_SCAN_NUMS = 0;
private:
  DISALLOW_COPY_AND_ASSIGN(ObHNSWIterFilterScanNumChecker);
};


} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SHARE_CONFIG_OB_CONFIG_HELPER_H_
