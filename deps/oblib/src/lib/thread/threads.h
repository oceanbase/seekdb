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

#ifndef USER_THREAD_H
#define USER_THREAD_H

#include <functional>
#include "lib/ob_errno.h"
#include "lib/thread/thread.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/alloc_assist.h"
#include "lib/lock/ob_spin_rwlock.h"

extern int64_t global_thread_stack_size;
extern const int64_t THREAD_STACK_RESERVED_SIZE;
namespace oceanbase {
namespace lib {
class ObPThread;

class IRunWrapper;
class Threads
{
public:
  friend class ObPThread;
  explicit Threads(int64_t n_threads = 1)
      : n_threads_(n_threads),
        init_threads_(n_threads),
        threads_(nullptr),
        stack_size_(global_thread_stack_size),
        stop_(true),
        run_wrapper_(nullptr)
  {}
  virtual ~Threads();
  static IRunWrapper *&get_expect_run_wrapper();

  /// \brief Set number of threads for running.
  ///
  /// When set before threads are running, this function simply set
  /// local varible which would be read for \c run().
  ///
  /// When set after threads are running, this function would adjust
  /// real threads count other than set local variable.
  ///
  /// \param n_threads Number of threads to set.
  ///
  /// \return Return OB_SUCCESS if threads count has successfully
  ///         adjust to that number, i.e. there are such exact number
  ///         of threads are running if it has started, or would run
  ///         after call \c start() function.
  int do_set_thread_count(int64_t n_threads, bool async_recycle=false);
  int set_thread_count(int64_t n_threads);
  int inc_thread_count(int64_t inc = 1);
  int thread_recycle();
  int try_thread_recycle();

  int init();
  // IRunWrapper specifies the runtime context inherited by worker threads.
  void set_run_wrapper(IRunWrapper *run_wrapper)
  {
    run_wrapper_ = run_wrapper;
  }
  static void set_default_run_wrapper(IRunWrapper *run_wrapper);
  static IRunWrapper *get_default_run_wrapper();
  IRunWrapper * get_run_wrapper()
  {
    return run_wrapper_;
  }
  IRunWrapper *get_effective_run_wrapper()
  {
    IRunWrapper *run_wrapper = run_wrapper_;
    return OB_NOT_NULL(run_wrapper) ? run_wrapper : get_default_run_wrapper();
  }
  virtual int start();
  virtual void stop();
  virtual void wait();
  void destroy();
  virtual void run(int64_t idx);
public:
  template <class Functor>
  int submit(const Functor &func)
  {
    UNUSED(func);
    int ret = OB_SUCCESS;
    return ret;
  }
  virtual bool has_set_stop() const
  {
    IGNORE_RETURN lib::Thread::update_loop_ts();
    return ATOMIC_LOAD(&stop_);
  }
  bool &has_set_stop()
  {
    IGNORE_RETURN lib::Thread::update_loop_ts();
    return stop_;
  }
  pthread_t get_pthread(int64_t idx)
  {
    pthread_t pth = pthread_null();
    if (idx < n_threads_) {
      pth = threads_[idx]->get_pthread();
    }
    return pth;
  }
  int64_t get_thread_count() const { return n_threads_; }
protected:
  uint64_t get_thread_idx() const { return thread_idx_; }
  void set_thread_idx(int64_t idx) { thread_idx_ = idx; }

private:
  virtual void run1() {}

  int do_thread_recycle(bool try_mode);
  /// \brief Create thread
  int create_thread(Thread *&thread, int64_t idx);

  /// \brief Destroy thread.
  void destroy_thread(Thread *thread);

private:
  static thread_local uint64_t thread_idx_;
  int64_t n_threads_;
  int64_t init_threads_;
  Thread **threads_;
  int64_t stack_size_;
  bool stop_;
  // protect for thread count changing.
  common::SpinRWLock lock_ __attribute__((__aligned__(16)));
  // Runtime context.
  IRunWrapper *run_wrapper_;
};

class ObPThread : public Threads
{
public:
  ObPThread(void *(*start_routine) (void *), void *arg)
    : start_routine_(start_routine), arg_(arg)
  {}
  void run1() override
  {
    start_routine_(arg_);
  }
  int try_wait();
private:
  void *(*start_routine_)(void *);
  void *arg_;
};

using ThreadPool = Threads;

}  // lib
}  // oceanbase


#endif /* USER_THREAD_H */
