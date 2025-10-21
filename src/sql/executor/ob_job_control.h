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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_JOB_CONTROL_
#define OCEANBASE_SQL_EXECUTOR_OB_JOB_CONTROL_

#include "lib/container/ob_se_array.h"
#include "sql/executor/ob_job.h"

namespace oceanbase
{
namespace sql
{
class ObTaskEvent;
class ObJobControl
{
public:
  explicit ObJobControl();
  virtual ~ObJobControl();

  virtual int sort_job_scan_part_locs(ObExecContext &ctx);
  virtual int init_job_finish_queue(ObExecContext &ctx);
//  int arrange_jobs();
  /*
   * @input ob_execution_id used to distinguish different execution results in IRM,
   * only valid for Distributed mode, others are INVALID_ID
   */
  int create_job(common::ObIAllocator &alloc,
                 const ObExecutionID &ob_execution_id,
                 uint64_t root_op_id,
                 ObJob *&job) const;
  virtual int get_ready_jobs(common::ObIArray<ObJob *> &jobs, bool serial_sched = false) const = 0;
  virtual int get_running_jobs(common::ObIArray<ObJob *> &jobs) const;
  // build op input of the current job
  int print_status(char *buf, int64_t buf_len, bool ignore_normal_state = false) const;

  inline int add_job(ObJob *job) { return jobs_.push_back(job); }
  inline int64_t get_job_count() const { return jobs_.count(); }

  DECLARE_TO_STRING;
private:
  int build_job_ctx(ObExecContext &ctx, ObJob &job);
  int build_job_op_input(ObExecContext &ctx, ObJob &job);
//  int jobs_quick_sort(common::ObIArray<ObJob *> &jobs,
//                      int64_t low,
//                      int64_t high);
  void print_job_tree(char *buf, const int64_t buf_len, int64_t &pos, ObJob *job) const;
protected:
  common::ObSEArray<ObJob *, 2> jobs_;  // remote plan has two jobs
private:
  static volatile uint64_t global_job_id_;
  DISALLOW_COPY_AND_ASSIGN(ObJobControl);
};
}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_JOB_CONTROL_ */
