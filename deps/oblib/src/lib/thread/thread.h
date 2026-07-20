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

#ifndef CORO_THREAD_H
#define CORO_THREAD_H

#include <functional>
#include "lib/time/ob_time_utility.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/lock/ob_latch.h"
#include "lib/net/ob_addr.h"
#include "io/easy_io_struct.h"   // easy_addr_t (RpcGuard); formerly via ob_call_packet.h
namespace oceanbase { namespace obcall {} }  // fwd obcall ns (reduce deps; replaces rpc/frame header)

// Windows PThreads4W: pthread_t is a struct, not an integer
#ifdef _WIN32
#include <pthread.h>
#define PTHREAD_NULL_INITIALIZER {NULL, 0}
inline pthread_t pthread_null() {
  pthread_t pt = PTHREAD_NULL_INITIALIZER;
  return pt;
}
inline bool pthread_is_null(pthread_t pt) { return pt.p == NULL; }
#else
#define pthread_null() 0
inline bool pthread_is_null(pthread_t pt) { return pt == 0; }
#endif

namespace oceanbase {

namespace common
{
class ObTimerService;
}

namespace lib {
class ObPThread;

class Thread;
class Threads;
class IRunWrapper
{
public:
  virtual ~IRunWrapper() {}
  virtual int pre_run()
  {
    int ret = OB_SUCCESS;
    return ret;
  }
  virtual int end_run()
  {
    int ret = OB_SUCCESS;
    return ret;
  }
  virtual uint64_t id() const = 0;
};

/// \class
/// A wrapper of Linux thread that supports normal thread operations.
class Thread {
public:
  friend class ObPThread;
  static constexpr int PATH_SIZE = 128;
  Thread(Threads *threads, int64_t idx, int64_t stack_size, int32_t numa_node = OB_NUMA_SHARED_INDEX);
  ~Thread();

  int start();
  void stop();
  void run();
  void wait();
  void destroy();
  void dump_pth();
  pthread_t get_pthread() { return pth_; }
  int try_wait();

  /// \brief Get current thread object.
  ///
  /// \warning It would encounter segment fault if current thread
  /// isn't created with this class.
  static Thread &current();

  bool has_set_stop() const;
  
  using ThreadListNode = common::ObDLinkNode<lib::Thread *>;
  ThreadListNode *get_thread_list_node() { return &thread_list_node_; }
  int get_cpu_time_inc(int64_t &cpu_time_inc);
  int64_t get_tid() { return tid_; }

  OB_INLINE static int64_t update_loop_ts(int64_t t)
  {
    UNUSED(t);
    ObLatch::clear_lock();
    return 0;
  }

  OB_INLINE static int64_t update_loop_ts()
  {
    ObLatch::clear_lock();
    return 0;
  }
  OB_INLINE static void set_doing_ddl(const bool v) { is_doing_ddl_ = v; }
public:
  static thread_local bool is_doing_ddl_;
private:
  static void* __th_start(void *th);
  void destroy_stack();
  static thread_local Thread* current_thread_;

private:
  static int64_t total_thread_count_;
private:
  pthread_t pth_;
  Threads *threads_;
  int64_t idx_;
  void *stack_addr_;
  int64_t stack_size_;
  bool stop_;
  int64_t join_concurrency_;
  pid_t pid_before_stop_;
  pid_t tid_before_stop_;
  int64_t tid_;
  ThreadListNode thread_list_node_;
  int64_t cpu_time_;
  int create_ret_;
  int32_t numa_node_;
};

OB_INLINE bool Thread::has_set_stop() const
{
  IGNORE_RETURN update_loop_ts();
  return stop_;
}

extern int get_max_thread_num();
}  // lib
}  // oceanbase

#endif /* CORO_THREAD_H */
