/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
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
