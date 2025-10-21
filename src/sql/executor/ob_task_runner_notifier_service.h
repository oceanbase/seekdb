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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_TASK_RUNNER_NOTIFIER_SERVICE_
#define OCEANBASE_SQL_EXECUTOR_OB_TASK_RUNNER_NOTIFIER_SERVICE_

#include "sql/executor/ob_task_runner_notifier.h"
#include "lib/net/ob_addr.h"
#include "sql/executor/ob_slice_id.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/lock/ob_spin_lock.h"

namespace oceanbase
{
namespace sql
{
class ObTask;
class ObTaskInfo;
class ObTaskRunnerNotifierService
{
public:
  class ObKillTaskRunnerNotifier
  {
  public:
    ObKillTaskRunnerNotifier() : ret_(common::OB_ERR_UNEXPECTED) {}
    virtual ~ObKillTaskRunnerNotifier() {}
    void operator()(common::hash::HashMapPair<ObTaskID, ObTaskRunnerNotifier*> &entry);
    int get_ret() { return ret_; }
  private:
    int ret_;
  private:
    DISALLOW_COPY_AND_ASSIGN(ObKillTaskRunnerNotifier);
  };

  class Guard
  {
  public:
    Guard(const ObTaskID &task_id, ObTaskRunnerNotifier *notifier);
    ~Guard();
  private:
    const ObTaskID task_id_;
  };

  ObTaskRunnerNotifierService();
  virtual ~ObTaskRunnerNotifierService();

  static int build_instance();
  static ObTaskRunnerNotifierService *get_instance();
  static int register_notifier(const ObTaskID &key,
                               ObTaskRunnerNotifier *notifier);
  static int unregister_notifier(const ObTaskID &key);

  void reset();
  int init();
private:
  static const int64_t NOTIFIER_MAP_BUCKET_SIZE = 1024;

  static ObTaskRunnerNotifierService *instance_;

  int set_notifier(const ObTaskID &key, ObTaskRunnerNotifier *notifier);
  int erase_notifier(const ObTaskID &key);
  template<class _callback> int atomic(const ObTaskID &key, _callback &callback);

  bool inited_;
  common::hash::ObHashMap<ObTaskID, ObTaskRunnerNotifier*> notifier_map_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTaskRunnerNotifierService);
};

}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_TASK_RUNNER_NOTIFIER_SERVICE_ */

