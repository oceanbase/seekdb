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

#ifndef OCEANBASE_QUERY_SCHEDULER_OB_SCHEDULER_SERVICE_H_
#define OCEANBASE_QUERY_SCHEDULER_OB_SCHEDULER_SERVICE_H_

#include <cstdint>

namespace oceanbase
{
namespace common
{
class ObISQLClient;
}
namespace dbms_scheduler
{
class ObDBMSSchedJobInfo;
}
namespace query
{

constexpr int64_t OB_SCHEDULER_JOB_ID_OFFSET = (1LL << 50);

class ObISchedulerService
{
public:
  virtual ~ObISchedulerService() = default;

  virtual int allocate_job_id(int64_t &job_id) = 0;
  virtual int create_job(
      common::ObISQLClient &sql_client,
      int64_t job_id,
      const dbms_scheduler::ObDBMSSchedJobInfo &job_info) = 0;
  virtual void notify_scheduler() = 0;
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_SCHEDULER_OB_SCHEDULER_SERVICE_H_
