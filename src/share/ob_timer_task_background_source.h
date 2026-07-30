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

#ifndef OCEANBASE_SHARE_OB_TIMER_TASK_BACKGROUND_SOURCE_H_
#define OCEANBASE_SHARE_OB_TIMER_TASK_BACKGROUND_SOURCE_H_

#include "share/ob_background_task_executor.h"

namespace oceanbase
{
namespace share
{

class ObTimerTaskBackgroundSource : public ObIBackgroundTaskSource
{
public:
  ObTimerTaskBackgroundSource();
  virtual ~ObTimerTaskBackgroundSource();

  int init(ObBackgroundTaskExecutor *background_executor);
  void stop();
  void wait();
  void destroy();

  virtual int process_one_quantum(
      const ObBackgroundTaskPriority priority,
      ObBackgroundTaskRunResult &result) override;

private:
  static void notify_callback_(void *arg);
  int notify_();
  int unregister_source_(const bool wait_running);

private:
  lib::ObMutex source_lock_;
  bool is_inited_;
  bool use_shared_executor_;
  ObBackgroundTaskExecutor *background_executor_;
  ObBackgroundTaskSourceHandle source_handle_;

  DISALLOW_COPY_AND_ASSIGN(ObTimerTaskBackgroundSource);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_TIMER_TASK_BACKGROUND_SOURCE_H_
