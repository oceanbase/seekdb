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

#ifndef OCEANBASE_COMMON_OB_TIMER_MONITOR_
#define OCEANBASE_COMMON_OB_TIMER_MONITOR_

#include <stdint.h>

#include "lib/task/ob_timer.h"

namespace oceanbase
{
namespace common
{

class ObTimerMonitor
{
public:
  static ObTimerMonitor &get_instance();

  int init();
  int start();
  void wait();
  void stop();
  void destroy();

  void start_task(const int64_t thread_id,
                  const int64_t start_time,
                  const int64_t interval,
                  const ObTimerTask* task);
  void end_task(const int64_t thread_id, const int64_t end_time);
  void dump(const bool print_trace);

  ObTimerMonitor(const ObTimerMonitor&) = delete;
  ObTimerMonitor& operator=(const ObTimerMonitor&) = delete;
private:
  ObTimerMonitor();
  ~ObTimerMonitor();

  int64_t find_pos(const int64_t thread_id) const;
  int64_t record_new_thread(const int64_t thread_id);

  static const int64_t MAX_MONITOR_THREAD_NUM = 256L;
  static const int64_t CHECK_INTERVAL = 10L * 1000L * 1000L;

  class ObTimerMonitorTask : public ObTimerTask
  {
  public:
    ObTimerMonitorTask(ObTimerMonitor &monitor);
    virtual ~ObTimerMonitorTask();
    virtual void runTimerTask() override;
  private:
    ObTimerMonitor &monitor_;
    int64_t running_cnt_;
  };

  struct TimerRecord
  {
    int64_t thread_id_;
    int64_t start_time_;
    int64_t interval_;
    const ObTimerTask *task_;

    int64_t task_cnt_;
    int64_t cost_time_;

    int64_t seq_;

    TO_STRING_KV(K(thread_id_), K(start_time_), K(interval_), K(*task_));
  };

  bool inited_;
  common::ObTimer timer_;
  ObTimerMonitorTask monitor_task_;
  TimerRecord records_[MAX_MONITOR_THREAD_NUM];
  int64_t tail_;
};


}
}

#endif /* OCEANBASE_COMMON_OB_TIMER_MONITOR_ */
