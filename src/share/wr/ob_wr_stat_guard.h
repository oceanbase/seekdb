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

#ifndef OCEANBASE_WR_OB_WR_STAT_GUARD_H_
#define OCEANBASE_WR_OB_WR_STAT_GUARD_H_
#include "lib/statistic_event/ob_stat_event.h"
#include "lib/time/ob_time_utility.h"
#include "lib/stat/ob_diagnostic_info.h"

namespace oceanbase
{
namespace share
{

#define WR_STAT_GUARD(STAT_PREFIX)                                            \
  WrStatGuard<::oceanbase::common::ObStatEventIds::STAT_PREFIX##_ELAPSE_TIME, \
      ::oceanbase::common::ObStatEventIds::STAT_PREFIX##_CPU_TIME>            \
      wr_stat_guard;

template <ObStatEventIds::ObStatEventIdEnum elapse_time_id,
    ObStatEventIds::ObStatEventIdEnum cpu_time_id>
class WrStatGuard
{
public:
  WrStatGuard()
  {
    begin_ts_ = ::oceanbase::common::ObTimeUtility::current_time();
    begin_ru_cputime_ = get_ru_utime();
  }
  ~WrStatGuard()
  {
    int64_t elapse_time = ::oceanbase::common::ObTimeUtility::current_time() - begin_ts_;
    int64_t cpu_time = get_ru_utime() - begin_ru_cputime_;
    oceanbase::common::ObDiagnosticInfo *tenant_info =
        oceanbase::common::ObLocalDiagnosticInfo::get();
    if (NULL != tenant_info) {
      tenant_info->add_stat(elapse_time_id, elapse_time);
      tenant_info->add_stat(cpu_time_id, cpu_time);
    }
  }

private:
  int64_t get_ru_utime()
  {
    struct rusage ru;
    getrusage(RUSAGE_THREAD, &ru);
    int64_t ru_utime = ru.ru_utime.tv_sec * 1000000 + ru.ru_utime.tv_usec +
                       ru.ru_stime.tv_sec * 1000000 + ru.ru_stime.tv_usec;
    return ru_utime;
  }
  int64_t begin_ts_;
  int64_t begin_ru_cputime_;
};

}//end namespace share
}//end namespace oceanbase

#endif // OCEANBASE_WR_OB_WR_STAT_GUARD_H_
