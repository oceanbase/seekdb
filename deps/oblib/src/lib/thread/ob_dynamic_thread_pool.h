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

#ifndef SRC_LIBRARY_SRC_LIB_THREAD_OB_DYNAMIC_THREAD_POOL_H_
#define SRC_LIBRARY_SRC_LIB_THREAD_OB_DYNAMIC_THREAD_POOL_H_

#include "lib/utility/utility.h"
#include "lib/queue/ob_fixed_queue.h"
#include "lib/lock/ob_thread_cond.h"
#include "lib/thread/thread_pool.h"
#include "lib/container/ob_se_array.h"
#include "lib/thread/thread_mgr_interface.h"

namespace oceanbase
{
namespace common
{
class ObDynamicThreadPool;

struct ObDynamicThreadInfo
{
  ObDynamicThreadInfo();
  void *tid_;
  int64_t idx_;
  ObDynamicThreadPool *pool_;
  bool is_stop_;
  bool is_alive_;
  bool error_thread_; // only record error during start thread

  TO_STRING_KV(K_(tid), K_(idx), KP_(pool), K_(is_stop), K_(is_alive), K_(error_thread));
};

class ObDynamicThreadTask
{
public:
  virtual ~ObDynamicThreadTask() {}
  virtual int process(const bool &is_stop) = 0;
};

class ObDynamicThreadPool: public lib::ThreadPool
{
public:
  static const int64_t MAX_THREAD_NUM = 512;
  static const int64_t MAX_TASK_NUM = 1024 * 1024;
  static const int64_t DEFAULT_CHECK_TIME_MS = 1000; // 1s

  ObDynamicThreadPool();
  ~ObDynamicThreadPool();
  int init(const char* thread_name = nullptr);
  int set_task_thread_num(const int64_t thread_num);
  int add_task(ObDynamicThreadTask *task);

  void run1() override;
  void stop();
  void destroy();
  void task_thread_idle();
  int64_t get_task_count() const { return task_queue_.get_total(); }
  TO_STRING_KV(K_(is_inited), K_(is_stop), K_(thread_num),
      K_(need_idle), K_(start_thread_num), K_(stop_thread_num), "left_task", task_queue_.get_total());
private:
  int check_thread_status();
  int stop_all_threads();
  void wakeup();

  int start_thread(ObDynamicThreadInfo &thread_info);
  int stop_thread(ObDynamicThreadInfo &thread_info);
  int pop_task(ObDynamicThreadTask *&task);
  static void *task_thread_func(void *data);
private:
  bool is_inited_;
  volatile bool is_stop_;
  int64_t thread_num_;
  ObFixedQueue<ObDynamicThreadTask> task_queue_;
  ObDynamicThreadInfo thread_infos_[MAX_THREAD_NUM];
  ObThreadCond task_thread_cond_;
  ObThreadCond cond_;
  bool need_idle_;
  int64_t start_thread_num_;
  int64_t stop_thread_num_;
  const char* thread_name_;
  DISALLOW_COPY_AND_ASSIGN(ObDynamicThreadPool);
};
class ObSimpleDynamicThreadPool
{
  friend class ObSimpleThreadPoolDynamicMgr;
public:
  static const int64_t MAX_THREAD_NUM = 1024;
  ObSimpleDynamicThreadPool()
    : has_bind_(false), min_thread_cnt_(-1), max_thread_cnt_(-1),
      name_("unknown"), ref_cnt_(0)
  {}
  virtual ~ObSimpleDynamicThreadPool();
  void inc_ref() { ATOMIC_INC(&ref_cnt_); }
  void dec_ref() { ATOMIC_SAF(&ref_cnt_, 1); }
  int64_t get_ref_cnt() { return ATOMIC_LOAD(&ref_cnt_); }

  // Mgr interface — implemented by ObSimpleThreadPoolBase
  virtual int64_t get_queue_num() const = 0;
  virtual void reap_workers() = 0;
  virtual int64_t worker_count() const = 0;
  virtual void notify_stop() {}

  TO_STRING_KV(KCSTRING_(name), KP(this), K_(min_thread_cnt), K_(max_thread_cnt));

  bool has_bind_;
  int64_t min_thread_cnt_;
  int64_t max_thread_cnt_;
  const char* name_;
  
private:
  int64_t ref_cnt_;
};

class ObSimpleThreadPoolDynamicMgr : public lib::TGRunnable {
public:
  static const int64_t CHECK_INTERVAL_US = 3 * 1000 * 1000;
  ObSimpleThreadPoolDynamicMgr() : pool_list_(), pool_list_lock_(), is_inited_(false) {}
  virtual ~ObSimpleThreadPoolDynamicMgr();
  int init();
  void stop();
  void wait();
  void destroy();
  void run1();
  int bind(ObSimpleDynamicThreadPool *pool);
  int unbind(ObSimpleDynamicThreadPool *pool);
  static ObSimpleThreadPoolDynamicMgr &get_instance();
private:
  ObArray<ObSimpleDynamicThreadPool *> pool_list_;
  common::SpinRWLock pool_list_lock_;
  int is_inited_;
};

class ObResetThreadTenantIdGuard {
public:
  DISABLE_COPY_ASSIGN(ObResetThreadTenantIdGuard);
  ObResetThreadTenantIdGuard() = default;
  ~ObResetThreadTenantIdGuard() = default;
};
}
}

#endif /* SRC_LIBRARY_SRC_LIB_THREAD_OB_DYNAMIC_THREAD_POOL_H_ */
