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

#ifndef OCEANBASE_COMMON_MAP_QUEUE_THREAD_POOL_H__
#define OCEANBASE_COMMON_MAP_QUEUE_THREAD_POOL_H__

#include "ob_map_queue.h"      // ObMapQueue

#include "lib/utility/ob_macro_utils.h"     // UNUSED
#include "lib/oblog/ob_log_module.h"        // LIB_LOG
#include "lib/atomic/ob_atomic.h"           // ATOMIC_*
#include "lib/thread/thread_pool.h"         // lib::ThreadPool
#include "common/ob_queue_thread.h"         // ObCond

namespace oceanbase
{
namespace common
{

// Thread pool
//
// One ObMapQueue per thread
// Since ObMapQueue is scalable, push operations do not block
class ObMapQueueThreadPool
    : public lib::ThreadPool
{
  typedef ObMapQueue<void *> QueueType;
  static const int64_t DATA_OP_TIMEOUT = 1L * 1000L * 1000L;

public:
  ObMapQueueThreadPool();
  virtual ~ObMapQueueThreadPool();

public:
  // Inserting data
  // Non-blocking
  //
  // @retval OB_SUCCESS           Success
  // @retval Other_return_values  Fail
  int push(void *data, const uint64_t hash_val);

  // Thread execution function
  // Users can override this function to customize the thread execution
  void run1();

  // Data handling function
  // Users can also override this function to process data directly while keeping the run() function
  virtual void handle(void *data, volatile bool &is_stoped)
  {
    UNUSED(data);
  }

protected:
  /// pop data from a thread-specific queue
  ///
  /// @param thread_index Thread number
  /// @param data The data returned
  ///
  /// @retval OB_SUCCESS        success
  /// @retval OB_EAGAIN         empty queue
  /// @retval other_error_code  Fail

  /// Execute cond timedwait on a specific thread's queue

public:
  int init(const uint64_t tenant_id, const int64_t thread_num, const char *label);
  void destroy();
  int start();
  void stop();
  bool is_stoped() const { return lib::ThreadPool::has_set_stop(); }
  int64_t get_thread_num() const { return lib::ThreadPool::get_thread_count(); }

public:
  static const int64_t MAX_THREAD_NUM = 64;
  typedef ObMapQueueThreadPool HostType;
  struct ThreadConf
  {
    HostType    *host_;
    int64_t     thread_index_;
    QueueType   queue_;
    ObCond      cond_;

    ThreadConf();
    virtual ~ThreadConf();

    int init(const char *label, const int64_t thread_index, HostType *host);
    void destroy();
  };

private:
  int next_task_(const int64_t thread_index, void *&task);

private:
  bool          is_inited_;
  const char    *name_;
  ThreadConf    tc_[MAX_THREAD_NUM];

private:
  DISALLOW_COPY_AND_ASSIGN(ObMapQueueThreadPool);
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace common
} // namespace oceanbase
#endif /* OCEANBASE_COMMON_OB_SIMPLE_THREAD_POOL_H_ */
