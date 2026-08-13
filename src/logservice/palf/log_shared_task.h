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

#ifndef OCEANBASE_LOGSERVICE_LOG_SHARED_TASK_
#define OCEANBASE_LOGSERVICE_LOG_SHARED_TASK_

#include "lib/utility/ob_print_utils.h"
#include "share/log/palf/lsn.h"
namespace oceanbase
{
namespace palf
{
class IPalfEnvImpl;

enum class LogSharedTaskType
{
  LogHandleSubmitType = 1,
};

inline const char *shared_type_2_str(const LogSharedTaskType type)
{
#define EXTRACT_SHARED_TYPE(type_var) case(LogSharedTaskType::type_var): return #type_var
  switch(type)
  {
    EXTRACT_SHARED_TYPE(LogHandleSubmitType);
    default:
      return "Invalid Type";
  }
#undef EXTRACT_SHARED_TYPE
}

class LogSharedTask
{
public:
  explicit LogSharedTask(const int64_t palf_epoch);
  virtual ~LogSharedTask();
  void destroy();
  void reset();
  virtual int do_task(IPalfEnvImpl *palf_env_impl) = 0;
  virtual void free_this(IPalfEnvImpl *palf_env_impl) = 0;
  virtual LogSharedTaskType get_shared_task_type() const = 0;
  VIRTUAL_TO_STRING_KV("BaseClass", "LogSharedTask",
      "palf_epoch", palf_epoch_);
protected:
  int64_t palf_epoch_;
private:
  DISALLOW_COPY_AND_ASSIGN(LogSharedTask);
};

class LogHandleSubmitTask : public LogSharedTask
{
public:
  explicit LogHandleSubmitTask(const int64_t palf_epoch);
  ~LogHandleSubmitTask() override;
  int do_task(IPalfEnvImpl *palf_env_impl) override;
  void free_this(IPalfEnvImpl *palf_env_impl) override;
  virtual LogSharedTaskType get_shared_task_type() const override { return LogSharedTaskType::LogHandleSubmitType; }
  INHERIT_TO_STRING_KV("LogSharedTask", LogSharedTask, "task type", shared_type_2_str(get_shared_task_type()));
private:
  DISALLOW_COPY_AND_ASSIGN(LogHandleSubmitTask);
};

} // end namespace palf
} // end namespace oceanbase

#endif
