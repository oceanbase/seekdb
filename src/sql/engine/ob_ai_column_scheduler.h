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

#ifndef OCEANBASE_SQL_ENGINE_OB_AI_COLUMN_SCHEDULER_H_
#define OCEANBASE_SQL_ENGINE_OB_AI_COLUMN_SCHEDULER_H_

#include "lib/container/ob_se_array.h"
#include "common/ob_i_task.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase
{
namespace sql
{

struct ObAiColumnTask
{
  uint64_t tenant_id_;
  uint64_t table_id_;
  uint64_t column_id_;
  uint64_t row_id_;
  common::ObString ai_type_;
  common::ObString model_name_;
  common::ObString prompt_expr_;
  common::ObString sync_mode_;
  int64_t max_retries_;
  int64_t retry_count_;
  int64_t retry_delay_; // seconds

  ObAiColumnTask()
    : tenant_id_(OB_INVALID_TENANT_ID),
      table_id_(OB_INVALID_ID),
      column_id_(OB_INVALID_ID),
      row_id_(OB_INVALID_ID),
      max_retries_(3),
      retry_count_(0),
      retry_delay_(1) {}

  TO_STRING_KV(K_(tenant_id), K_(table_id), K_(column_id), K_(row_id),
               K_(ai_type), K_(model_name), K_(prompt_expr),
               K_(sync_mode), K_(max_retries), K_(retry_count), K_(retry_delay));
};

class ObAiColumnScheduler
{
public:
  ObAiColumnScheduler();
  virtual ~ObAiColumnScheduler();

  // Initialize scheduler
  int init();

  // Submit AI column task for async processing
  int submit_task(const ObAiColumnTask& task);

  // Process pending tasks (called by background worker)
  int process_pending_tasks();

  // Retry failed task with exponential backoff
  int retry_task(const ObAiColumnTask& task);

  // Update task status
  int update_task_status(uint64_t column_id,
                         uint64_t row_id,
                         const common::ObString& status,
                         const common::ObString& error_msg);

  // Cancel all pending tasks for a column
  int cancel_tasks_for_column(uint64_t column_id);

private:
  // Execute single AI task
  int execute_task(ObAiColumnTask& task);

  // Call AI service (complete/embed/rerank)
  int call_ai_service(ObAiColumnTask& task,
                      common::ObString& result);

  // Update hidden columns with request/result
  int update_hidden_columns(const ObAiColumnTask& task,
                            const common::ObString& request_payload,
                            const common::ObString& result);

private:
  bool is_inited_;
  common::ObSEArray<ObAiColumnTask, 1024> task_queue_;
  common::ObMutex task_queue_lock_;

  DISALLOW_COPY_AND_ASSIGN(ObAiColumnScheduler);
};

// Global scheduler instance
extern ObAiColumnScheduler* g_ai_column_scheduler;

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_OB_AI_COLUMN_SCHEDULER_H_
