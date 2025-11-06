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

#ifndef OCEANBASE_ROOTSERVER_FREEZE_OB_MAJOR_FREEZE_UTIL_H_
#define OCEANBASE_ROOTSERVER_FREEZE_OB_MAJOR_FREEZE_UTIL_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{
class ObISQLClient;
}
namespace rootserver
{
class ObPrimaryMajorFreezeService;
class ObRestoreMajorFreezeService;
class ObMajorFreezeService;

class ObMajorFreezeUtil
{
public:
  static int get_major_freeze_service(ObPrimaryMajorFreezeService *primary_major_freeze_service,
                                      ObRestoreMajorFreezeService *restore_major_freeze_service,
                                      ObMajorFreezeService *&major_freeze_service,
                                      bool &is_primary_service);
  static int check_epoch_periodically(common::ObISQLClient &sql_proxy,
                                      const uint64_t tenant_id,
                                      const int64_t expected_epoch,
                                      int64_t &last_check_us);
private:
  static const int64_t CHECK_EPOCH_INTERVAL_US = 60 * 1000L * 1000L; // 60s
};

#define FREEZE_TIME_GUARD \
  rootserver::ObFreezeTimeGuard freeze_time_guard(__FILE__, __LINE__, __FUNCTION__, "[RS_COMPACTION] ")

class ObFreezeTimeGuard
{
public:
  ObFreezeTimeGuard(const char *file,
                    const int64_t line,
                    const char *func,
                    const char *mod);
  virtual ~ObFreezeTimeGuard();

private:
  const int64_t FREEZE_WARN_THRESHOLD_US = 10 * 1000L * 1000L; // 10s

private:
  const int64_t warn_threshold_us_;
  const int64_t start_time_us_;
  const char * const file_;
  const int64_t line_;
  const char * const func_name_;
  const char * const log_mod_;
};

enum ObMajorFreezeReason : uint8_t {
  MF_DAILY_MERGE = 0,
  MF_USER_REQUEST,
  MF_MAJOR_COMPACT_TRIGGER,
  MF_REASON_MAX,
};

const char *major_freeze_reason_to_str(const int64_t freeze_reason);
bool is_valid_major_freeze_reason(const ObMajorFreezeReason &freeze_reason);

} // end namespace rootserver
} // end namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_FREEZE_OB_MAJOR_FREEZE_UTIL_H_
