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

#ifndef OCEANBASE_SQL_EXECUTOR_JOB_
#define OCEANBASE_SQL_EXECUTOR_JOB_

#include "lib/queue/ob_lighty_queue.h"
#include "sql/executor/ob_job_id.h"
#include "sql/executor/ob_task_id.h"
#include "sql/executor/ob_slice_id.h"
#include "sql/executor/ob_task_info.h"
#include "sql/executor/ob_task_executor.h"
#include "sql/executor/ob_task_control.h"
#include "sql/engine/ob_physical_plan.h"

namespace oceanbase
{
namespace sql
{
enum ObJobState
{
  OB_JOB_STATE_NOT_INIT,
  OB_JOB_STATE_INITED,
  OB_JOB_STATE_RUNNING,
  OB_JOB_STATE_FINISHED,
};
class ObTaskEvent;
class ObTaskSpliter;
class ObPhysicalPlanCtx;
class ObExecContext;
class ObOpSpec;
class ObMiniJob
{
public:
  ObMiniJob()
    : phy_plan_(NULL),
      root_spec_(NULL),
      extend_spec_(NULL)
  { }

  inline const ObPhysicalPlan *get_phy_plan() const { return phy_plan_; }
  inline void set_phy_plan(const ObPhysicalPlan *phy_plan) { phy_plan_ = phy_plan; }

  inline const ObOpSpec *get_root_spec() const { return root_spec_; }
  inline void set_root_spec(const ObOpSpec *root_spec) { root_spec_ = root_spec; }
  inline const ObOpSpec *get_extend_spec() const { return extend_spec_; }
  inline void set_extend_spec(const ObOpSpec *extend_op) { extend_spec_ = extend_op; }

  DECLARE_TO_STRING;
private:
  const ObPhysicalPlan *phy_plan_;
  // For new engine use
  const ObOpSpec *root_spec_;
  const ObOpSpec *extend_spec_;
};

class ObJob
{
public:
  ObJob();
  virtual ~ObJob();
  // Different Job, Task scheduling order is different, splitting method is different
  void set_task_spliter(ObTaskSpliter *spliter) { task_spliter_ = spliter; }
  // @deprecated
  // Set dependent Job
  int add_depending_job(const ObJob *job);
  // Get the Task collection from Job
  int get_task_control(const ObExecContext &ctx, ObTaskControl *&task_control);
  // Determine the current status of the Job
  ObJobState get_state() { return state_; }
  void set_state(ObJobState state) { state_ = state; }

  int print_status(char *buf, int64_t buf_len, int64_t &pos,
                   bool ignore_normal_state = false) const;
  void reset(); // TODO 2014-11-18, need reset ds, xiaochu.yh
  inline void set_root_job() {is_root_job_ = true;task_control_.set_root_job();}
  inline bool is_root_job() const {return is_root_job_;}
  inline void set_phy_plan(ObPhysicalPlan *phy_plan) {
    phy_plan_ = phy_plan;
    if (phy_plan) {
      // Any statement that is not a select statement is considered DML, and partition-level retries are not allowed
      task_control_.set_is_select_plan(phy_plan->is_select_plan());
    }
  }
  inline const ObPhysicalPlan *get_phy_plan() const { return phy_plan_; }
  inline void set_root_spec(ObOpSpec *root_spec) { root_spec_ = root_spec; }
  inline ObOpSpec *get_root_spec() { return root_spec_; }
  inline void set_ob_job_id(const ObJobID &ob_job_id) {ob_job_id_ = ob_job_id;}
  inline const ObJobID &get_ob_job_id() { return ob_job_id_; }
  inline uint64_t get_execution_id() const { return ob_job_id_.get_execution_id(); }
  inline uint64_t get_job_id() const { return ob_job_id_.get_job_id(); }
  inline uint64_t get_root_op_id() const { return ob_job_id_.get_root_op_id(); }
  inline void set_parent_job(ObJob *parent_job);
  inline int append_child_job(ObJob *job) { return child_jobs_.push_back(job); }
  inline int get_child_job(int64_t idx, ObJob *&job) const { return child_jobs_.at(idx, job); }
  inline int64_t get_child_count() const { return child_jobs_.count(); }
  inline bool parent_can_exec() { return OB_JOB_STATE_FINISHED == state_; }
  int need_skip_empty_result(bool &skip_empty) const;
  inline void set_scan(bool has_scan) { has_scan_ = has_scan; }
  inline bool has_child_job() const { return get_child_count() > 0; }
  inline bool has_scan_op() const { return has_scan_; }

  DECLARE_TO_STRING;
private:
  // task control initialization interface
  int prepare_task_control(const ObExecContext &exec_ctx);
  int get_parallel_degree(const ObExecContext &exec_ctx, int64_t &stmt_parallel_degree);
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObJob);
private:
  static const int64_t MAX_CHILD_JOB_NUM = 16;
  ObJobID ob_job_id_;
  bool is_root_job_;
  ObPhysicalPlan *phy_plan_;
  ObJobState state_;
  // Control task splitting, as well as task status
  ObTaskSpliter *task_spliter_;
  bool task_splited_;
  ObTaskControl task_control_;

  common::ObSEArray<ObJob *, 4> child_jobs_;
  bool has_scan_;
  ObOpSpec *root_spec_; // for static engine
};

}
}
#endif /* OCEANBASE_SQL_EXECUTOR_JOB_ */
//// end of header file
