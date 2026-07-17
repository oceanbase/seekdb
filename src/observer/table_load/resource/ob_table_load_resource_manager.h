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

#pragma once

#include "lib/hash/ob_hashmap.h"
#include "lib/lock/ob_mutex.h"
#include "lib/task/ob_timer.h"
#include "observer/table_load/resource/ob_table_load_resource_struct.h"

namespace oceanbase
{
namespace observer
{

class ObTableLoadResourceManager
{
public:
  ObTableLoadResourceManager();
  ~ObTableLoadResourceManager() = default;
  int init();
  int start();
  void stop();
  int wait();
  void destroy();
  int apply_resource(ObDirectLoadResourceApplyArg &arg);
  int release_resource(ObDirectLoadResourceReleaseArg &arg);
  int release_all_resource();

private:
  struct ObResourceAssigned
  {
    ObResourceAssigned() = default;
    explicit ObResourceAssigned(const ObDirectLoadResourceApplyArg &arg) : apply_arg_(arg) {}
    ObDirectLoadResourceApplyArg apply_arg_;
  };

  class ObRefreshMemoryTask : public common::ObTimerTask
  {
  public:
    explicit ObRefreshMemoryTask(ObTableLoadResourceManager &manager) : manager_(manager) {}
    void runTimerTask() override;

  private:
    ObTableLoadResourceManager &manager_;
  };

  static int64_t get_required_memory_(const ObDirectLoadResourceApplyArg &arg);
  int refresh_memory_limit_();

private:
  typedef common::hash::ObHashMap<ObTableLoadUniqueKey,
                                  ObResourceAssigned,
                                  common::hash::NoPthreadDefendMode> ResourceAssignedMap;
  static const int64_t REFRESH_MEMORY_INTERVAL = 3LL * 1000LL * 1000LL;
  ObRefreshMemoryTask refresh_memory_task_;
  ResourceAssignedMap assigned_tasks_;
  mutable lib::ObMutex refresh_mutex_;
  mutable lib::ObMutex mutex_;
  int64_t memory_total_;
  int64_t memory_remain_;
  bool is_stop_;
  bool is_inited_;
};

} // namespace observer
} // namespace oceanbase
