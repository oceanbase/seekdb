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

#ifndef _OCEABASE_OBSERVER_OMT_OB_TH_WORKER_H_
#define _OCEABASE_OBSERVER_OMT_OB_TH_WORKER_H_

#include <pthread.h>
#include "lib/worker.h"
#include "lib/lock/ob_thread_cond.h"
#include "rpc/ob_request.h"
#include "lib/thread/threads.h"
#include "lib/thread/ob_thread_name.h"
#include "observer/omt/ob_worker_processor.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{

namespace rpc { namespace frame { class ObReqTranslator; } }
namespace omt
{

// Forward declarations
class ObServerRuntime;

static const int64_t WORKER_CHECK_PERIOD = 500L;

// Quick Queue Priorities
enum { QQ_HIGH = 0, QQ_NORMAL, QQ_LOW, QQ_MAX_PRIO };
// Request queue priorities
enum { RQ_HIGH = QQ_MAX_PRIO, RQ_NORMAL, RQ_LOW, RQ_MAX_PRIO };

class ObThWorker
    : public lib::Worker, public lib::Threads
{
  friend class ObServerRuntime;
public:
  explicit ObThWorker();
  virtual ~ObThWorker();

  virtual ObThWorker::Status check_wait() override;
  virtual int check_status() override;
  // retry relating
  virtual bool can_retry() const override { return can_retry_; }
  // Note: you CAN NOT call set_need_retry when can_retry_ == false
  virtual void set_need_retry() override { need_retry_ = true; }
  // THIS is _only_ used (for easy impl) in query_retry_ctrl decide to retry
  // but following process want to invalid the decision.
  // refer `ObQueryRetryCtrl::on_close_resulet_fail_`
  virtual void unset_need_retry() override { need_retry_ = false; }
  virtual bool need_retry() const override { return need_retry_; }
  virtual void resume() override;

  int init();
  void destroy();
  inline void reset();

  OB_INLINE void set_runtime(ObServerRuntime *runtime)
  {
    runtime_ = runtime;
    set_run_wrapper(share::server_runtime());
  }

  void worker(int64_t &tid, int64_t &req_recv_timestamp, int32_t &worker_level);
  void run(int64_t idx) override;

  OB_INLINE void pause() { pause_flag_ = true; }

  OB_INLINE int64_t get_query_start_time() const { return query_start_time_; }
  OB_INLINE int64_t get_query_enqueue_time() const { return query_enqueue_time_; }
  OB_INLINE ObServerRuntime *get_runtime() { return runtime_; }
  OB_INLINE const char *get_module_name() const { return module_name_; }
  OB_INLINE bool is_doing_ddl() const { return OB_NOT_NULL(is_doing_ddl_) ? (*is_doing_ddl_) : false; }

  static thread_local bool thread_name_set_;
private:
  void set_th_worker_thread_name();
  void process_request(rpc::ObRequest &req);

private:
  ObWorkerProcessor procor_;

  bool is_inited_;

  ObServerRuntime *runtime_;
  common::ObThreadCond run_cond_;

  bool pause_flag_;
  int64_t query_start_time_;
  int64_t query_enqueue_time_;
  int64_t last_check_time_;

  // indicate whether upper scheduler support retry mechanism or not.
  bool can_retry_;
  // if upper scheduler support retry, need this request retry?
  bool need_retry_;

  int64_t idle_us_;
  static const int64_t MAX_MODULE_NAME_LEN = 23; //no more than 3 int64_t
  char module_name_[MAX_MODULE_NAME_LEN];
  bool* is_doing_ddl_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObThWorker);
}; // end of class ObThWorker

inline void ObThWorker::reset()
{
  OB_ASSERT(!pause_flag_);
  runtime_ = nullptr;
  group_ = nullptr;
  pause_flag_ = false;
  query_start_time_ = 0;
  query_enqueue_time_ = 0;
  can_retry_ = true;
  need_retry_ = false;
}

int create_worker(ObThWorker* &worker, ObServerRuntime *runtime);
int destroy_worker(ObThWorker *worker);

#define THIS_THWORKER static_cast<oceanbase::omt::ObThWorker &>(THIS_WORKER)
#define THIS_THWORKER_SAFE dynamic_cast<oceanbase::omt::ObThWorker *>(&THIS_WORKER)

} // end of namespace omt
} // end of namespace oceanbase


#endif /* _OCEABASE_OBSERVER_OMT_OB_TH_WORKER_H_ */
