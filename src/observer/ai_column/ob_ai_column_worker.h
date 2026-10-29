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

#ifndef OCEANBASE_OBSERVER_AI_COLUMN_OB_AI_COLUMN_WORKER_H_
#define OCEANBASE_OBSERVER_AI_COLUMN_OB_AI_COLUMN_WORKER_H_

#include "lib/container/ob_array.h"
#include "lib/task/ob_timer.h"
#include "observer/ai_column/ob_ai_column_util.h"

namespace oceanbase
{
namespace share
{

// background worker: scans __all_ai_column_task periodically, calls the model
// endpoint per task and writes the typed result back to the business row.
class ObAiColumnWorker : public common::ObTimerTask
{
public:
  static const int64_t SCAN_PERIOD_US = 100L * 1000L;      // 100ms
  static const int64_t RETRY_BACKOFF_US = 300L * 1000L;    // 300ms
  static const int64_t MAX_TASKS_PER_ROUND = 128;

  static ObAiColumnWorker &get_instance();
  // idempotent; starts the timer on first call
  int ensure_started();

  virtual void runTimerTask() override;

private:
  ObAiColumnWorker()
      : is_inited_(false),
        is_started_(false),
        busy_(false),
        timer_()
  {}
  ~ObAiColumnWorker() { destroy(); }
  void destroy();
  int process_round();

  bool is_inited_;
  bool is_started_;
  volatile bool busy_;
  common::ObTimer timer_;
};

}  // end namespace share
}  // end namespace oceanbase

#endif  // OCEANBASE_OBSERVER_AI_COLUMN_OB_AI_COLUMN_WORKER_H_
