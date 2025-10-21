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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_LOCAL_JOB_EXECUTOR_
#define OCEANBASE_SQL_EXECUTOR_OB_LOCAL_JOB_EXECUTOR_
namespace oceanbase
{
namespace sql
{
class ObTaskInfo;
class ObJob;
class ObTaskExecutor;
class ObExecContext;
class ObLocalJobExecutor
{
public:
  ObLocalJobExecutor();
  virtual ~ObLocalJobExecutor();
  // Set the Job to be scheduled
  void set_job(ObJob &job) { job_ = &job; }
  void set_task_executor(ObTaskExecutor &executor) { executor_ = &executor; }
  // Schedule Job, distribute Tasks in Job for execution
  // Job exposes sufficient status interfaces, Execute can obtain the correct Task to be executed
  int execute(ObExecContext &ctx);
  inline void reset () { job_ = NULL; executor_ = NULL; }
private:
  // disallow copy
  ObLocalJobExecutor(const ObLocalJobExecutor &other);
  ObLocalJobExecutor &operator=(const ObLocalJobExecutor &ohter);

  int get_executable_task(ObExecContext &ctx, ObTaskInfo *&task);
private:
  ObJob *job_;
  ObTaskExecutor *executor_;
};
}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_LOCAL_JOB_EXECUTOR_ */
//// end of header file

