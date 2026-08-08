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

#ifndef SRC_OBSERVER_OB_DBMS_JOB_MASTER_H_
#define SRC_OBSERVER_OB_DBMS_JOB_MASTER_H_

#include "ob_dbms_job_utils.h"

#include "lib/ob_define.h"
#include "lib/allocator/page_arena.h"
#include "common/mysqlclient/ob_isql_client.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/thread/ob_simple_thread_pool.h"
#include "lib/task/ob_timer.h"
#include "lib/container/ob_iarray.h"

#include "share/schema/ob_schema_service.h"
#include "share/schema/ob_multi_version_schema_service.h"

#include "rootserver/ob_ddl_service.h"


namespace oceanbase
{

namespace dbms_job
{
class ObDBMSJobThread : public ObSimpleThreadPool
{
  virtual void handle(void *task);
};


class ObDBMSJobKey : public common::ObLink
{
public:
  ObDBMSJobKey(
    uint64_t job_id,
    uint64_t execute_at, uint64_t delay,
    bool check_job, bool check_new,
    uint64_t generation)
  : job_id_(job_id),
    execute_at_(execute_at),
    delay_(delay),
    check_job_(check_job),
    check_new_(check_new),
    generation_(generation) {}

  virtual ~ObDBMSJobKey() {}

  
  OB_INLINE uint64_t get_job_id() const { return job_id_; }
  OB_INLINE uint64_t get_execute_at() const { return execute_at_;}
  OB_INLINE uint64_t get_delay() const { return delay_; }
  OB_INLINE uint64_t get_generation() const { return generation_; }

  OB_INLINE bool is_check() { return check_job_ || check_new_; }
  OB_INLINE bool is_check_new() { return check_new_; }

  
  OB_INLINE void set_job_id(uint64_t job_id) { job_id_ = job_id; }

  OB_INLINE void set_execute_at(uint64_t execute_at) { execute_at_ = execute_at; }
  OB_INLINE void set_delay(uint64_t delay) { delay_ = delay; }

  OB_INLINE void set_check_job(bool check_job) { check_job_ = check_job; }
  OB_INLINE void set_check_new(bool check_new) { check_new_ = check_new; }
  OB_INLINE void set_generation(uint64_t generation) { generation_ = generation; }

  OB_INLINE uint64_t get_adjust_delay() const
  {
    uint64_t now = ObTimeUtility::current_time();
    return (execute_at_ < now) ? 0 : (execute_at_ - now);
  }

  OB_INLINE bool is_valid()
  {
    return job_id_ != OB_INVALID_ID;
  }

  TO_STRING_KV(
    K_(check_job), K_(check_new),
    K_(execute_at), K_(delay), K_(job_id), K_(generation));

private:
  uint64_t job_id_; // for check_new, job_id is the highest registered job id
  uint64_t execute_at_;
  uint64_t delay_;

  bool check_job_; // for check job update ...
  bool check_new_; // for check new job coming ...
  uint64_t generation_; // invalidates keys already published to ready_queue_
};

class ObDBMSJobTask : public ObTimerTask
{
public:
  typedef common::ObSortedVector<ObDBMSJobKey *> WaitVector;
  typedef WaitVector::iterator WaitVectorIterator;

  ObDBMSJobTask()
    : inited_(false),
      job_key_(NULL),
      ready_queue_(NULL),
      needs_reconcile_(NULL),
      wait_vector_(0, NULL, ObModIds::VECTOR),
      reconfiguring_(false),
      lock_(common::ObLatchIds::DBMS_JOB_TASK_LOCK) {}

  virtual ~ObDBMSJobTask() {}

  int init(ObDBMSJobQueue *ready_queue, bool *needs_reconcile);
  int start();
  int stop();
  int destroy();

  void runTimerTask();

  int scheduler(ObDBMSJobKey *job_key, ObDBMSJobKey *&replaced_job_key);
  int add_new_job(ObDBMSJobKey *job_key, ObDBMSJobKey *&replaced_job_key);
  int immediately(ObDBMSJobKey *job_key);

  // pause_and_wait() prevents a dispatched timer callback from observing a
  // replacement job_key_. The caller may then pop and free all timer-owned
  // keys before resume(). Keys already in ready_queue_ are not returned and
  // must be invalidated by ObDBMSJobKey::generation_.
  int pause_and_wait();
  int pop_waiting_job(ObDBMSJobKey *&job_key);
  void resume();

  inline static bool compare_job_key(
    const ObDBMSJobKey *lhs, const ObDBMSJobKey *rhs);
  inline static bool equal_job_key(
    const ObDBMSJobKey *lhs, const ObDBMSJobKey *rhs);

private:
  // Called with lock_ held after the previous timer token is known to be gone.
  // If scheduling still fails, hand the head to ready_queue_ so the master can
  // rebuild instead of leaving an unreachable key behind.
  int recover_unscheduled_head_();

  const static int64_t RECOVERY_INTERVAL = 1 * 1000 * 1000;

  bool inited_;
  ObDBMSJobKey *job_key_;
  ObDBMSJobQueue *ready_queue_;
  bool *needs_reconcile_;
  WaitVector wait_vector_;
  bool reconfiguring_;

  ObSpinLock lock_;
  ObTimer timer_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObDBMSJobTask);
};

class ObDBMSJobMaster
{
public:
  ObDBMSJobMaster()
    : inited_(false),
      stoped_(false),
      running_(false),
      trace_id_(NULL),
      ready_queue_(),
      scheduler_task_(),
      scheduler_thread_(),
      job_utils_(),
      lock_(common::ObLatchIds::DBMS_JOB_MASTER_LOCK),
      allocator_("DBMSJobMaster"),
      alive_jobs_(),
      job_table_change_seq_(0),
      schedule_generation_(0),
      needs_reconcile_(false) {}

  virtual ~ObDBMSJobMaster() { alive_jobs_.destroy(); };

  static ObDBMSJobMaster &get_instance();

  bool is_inited() { return inited_; }

  int init(common::ObISQLClient *sql_client,
           share::schema::ObMultiVersionSchemaService *schema_service);

  int start();
  int stop();
  int scheduler();
  int destroy();

  int alloc_job_key(
    ObDBMSJobKey *&job_key, uint64_t job_id,
    uint64_t execute_at, uint64_t delay,
    bool check_job = false, bool check_new = false,
    uint64_t generation = 0);

  int load_and_register_new_jobs(bool can_advance_seq, uint64_t target_seq);
  int register_jobs(
    common::ObIArray<ObDBMSJobInfo> &job_infos,
    uint64_t generation);
  int register_job(ObDBMSJobInfo &job_info,
                   ObDBMSJobKey *job_key = NULL,
                   bool ignore_nextdate = false,
                   uint64_t generation = 0);

  int scheduler_job(ObDBMSJobKey *job_key, bool is_retry = false);

private:
  int check_table_change_(ObDBMSJobKey *job_key, bool &handled);
  int schedule_change_check_(ObDBMSJobKey *check_key, uint64_t generation);
  int clear_scheduled_jobs_();

  const static int MAX_READY_JOBS_CAPACITY = (1 << 20);
  const static int MIN_SCHEDULER_INTERVAL = 5 * 1000 * 1000;

  bool inited_;
  bool stoped_;
  bool running_;

  const uint64_t *trace_id_;

  ObDBMSJobQueue ready_queue_;
  ObDBMSJobTask scheduler_task_;
  ObDBMSJobThread scheduler_thread_;
  ObDBMSJobUtils job_utils_;

  common::ObSpinLock lock_;
  common::ObArenaAllocator allocator_;

  common::hash::ObHashSet<uint64_t> alive_jobs_;
  uint64_t job_table_change_seq_;
  uint64_t schedule_generation_;
  bool needs_reconcile_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObDBMSJobMaster);
};

} //end for namespace dbms_job
} //end for namespace oceanbase

#endif /* SRC_OBSERVER_OB_DBMS_JOB_MASTER_H_ */
