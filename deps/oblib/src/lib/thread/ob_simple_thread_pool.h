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

#ifndef OCEANBASE_COMMON_OB_SIMPLE_THREAD_POOL_H_
#define OCEANBASE_COMMON_OB_SIMPLE_THREAD_POOL_H_

#include "lib/queue/ob_lighty_queue.h"
#include "lib/queue/ob_priority_queue.h"
#include "lib/thread/thread_pool.h"
#include "lib/thread/ob_dynamic_thread_pool.h"
#include "common/ob_queue_thread.h"

namespace oceanbase
{
namespace common
{
template <class T = ObLightyQueue>
struct QueueTypeMap {
  using TaskType = void;
  using QElemType = void;
};

template <>
struct QueueTypeMap<ObTLinkQueue16> {
  using TaskType = LinkTask;
  using QElemType = ObLink;
};

template <class T = ObLightyQueue>
class ObSimpleThreadPoolBase
    : public ObSimpleDynamicThreadPool
{
  using TaskType = typename QueueTypeMap<T>::TaskType;
  using QElemType = typename QueueTypeMap<T>::QElemType;
  static const int64_t QUEUE_WAIT_TIME = 100 * 1000;
public:
  ObSimpleThreadPoolBase();
  virtual ~ObSimpleThreadPoolBase();

  int init(const int64_t thread_num, const int64_t task_num_limit, const char *name = "unknown", const uint64_t tenant_id = OB_SERVER_TENANT_ID);
  void destroy();
  int push(TaskType *task);
  virtual int64_t get_queue_num() const override
  {
    return queue_.size();
  }
private:
  virtual void handle(TaskType *task) = 0;
  virtual void handle_drop(TaskType *task) {
    // when thread set stop left task will be process by handle_drop (default impl is handle)
    // users should define it's behaviour to manage task memory or some what
    handle(task);
  }
protected:
  void run1();

private:
  const char* name_;
  bool is_inited_;
  T queue_;
};

using ObSimpleThreadPool = ObSimpleThreadPoolBase<ObLightyQueue>;
using ObLinkQueueThreadPool = ObSimpleThreadPoolBase<ObTLinkQueue16>;

} // namespace common
} // namespace oceanbase

#include "ob_simple_thread_pool.ipp"

#endif // OCEANBASE_COMMON_OB_SIMPLE_THREAD_POOL_H_
