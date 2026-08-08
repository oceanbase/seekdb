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

#ifndef OCEANBASE_QUERY_PLAN_CACHE_OB_PLAN_CACHE_ACCESS_SERVICE_H_
#define OCEANBASE_QUERY_PLAN_CACHE_OB_PLAN_CACHE_ACCESS_SERVICE_H_

#include <cstdint>

namespace oceanbase
{
namespace query
{

// Query-owned seam for protecting plan-cache objects while they are being
// observed.  The composition layer owns the process-wide epoch tracker.
class ObIPlanCacheAccessService
{
public:
  virtual ~ObIPlanCacheAccessService() = default;
  virtual void enter_access() = 0;
  virtual void leave_access() = 0;
  virtual void check_current_thread() = 0;
  virtual int get_global_safe_timestamp(int64_t &safe_timestamp) const = 0;
};

class ObPlanCacheAccessGuard
{
public:
  explicit ObPlanCacheAccessGuard(ObIPlanCacheAccessService &service)
      : service_(service)
  {
    service_.enter_access();
  }
  ObPlanCacheAccessGuard(const ObPlanCacheAccessGuard &) = delete;
  ObPlanCacheAccessGuard &operator=(const ObPlanCacheAccessGuard &) = delete;

  ~ObPlanCacheAccessGuard()
  {
    service_.leave_access();
  }

private:
  ObIPlanCacheAccessService &service_;
};

inline void begin_plan_cache_access(ObIPlanCacheAccessService &service)
{
  service.enter_access();
}

inline void end_plan_cache_access(ObIPlanCacheAccessService &service)
{
  service.leave_access();
}

inline void check_plan_cache_access(ObIPlanCacheAccessService &service)
{
  service.check_current_thread();
}

inline int get_plan_cache_safe_timestamp(
    ObIPlanCacheAccessService &service,
    int64_t &safe_timestamp)
{
  return service.get_global_safe_timestamp(safe_timestamp);
}

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_PLAN_CACHE_OB_PLAN_CACHE_ACCESS_SERVICE_H_
