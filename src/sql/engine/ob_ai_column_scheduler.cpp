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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_ai_column_scheduler.h"
#include "sql/engine/expr/ob_ai_func_utils.h"
#include "share/schema/ob_schema_service.h"
#include "lib/string/ob_string_util.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

ObAiColumnScheduler* g_ai_column_scheduler = NULL;

ObAiColumnScheduler::ObAiColumnScheduler()
    : is_inited_(false)
{
}

ObAiColumnScheduler::~ObAiColumnScheduler()
{
}

int ObAiColumnScheduler::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("AI column scheduler has already been initialized", K(ret));
  } else if (OB_FAIL(task_queue_.create(1024, "AiColTaskQueue"))) {
    LOG_WARN("Failed to create task queue", K(ret));
  } else {
    is_inited_ = true;
    g_ai_column_scheduler = this;
    LOG_INFO("AI column scheduler initialized successfully");
  }
  return ret;
}

int ObAiColumnScheduler::submit_task(const ObAiColumnTask& task)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("AI column scheduler not initialized", K(ret));
  } else if (OB_FAIL(task_queue_lock_.lock())) {
    LOG_WARN("Failed to lock task queue", K(ret));
  } else {
    ObAiColumnTask new_task = task;
    new_task.retry_count_ = 0;

    if (OB_FAIL(task_queue_.push_back(new_task))) {
      LOG_WARN("Failed to push task to queue", K(ret), K(new_task));
    }

    task_queue_lock_.unlock();
  }

  return ret;
}

int ObAiColumnScheduler::process_pending_tasks()
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("AI column scheduler not initialized", K(ret));
  } else {
    // Process tasks from queue
    while (!task_queue_.empty() && OB_SUCC(ret)) {
      ObAiColumnTask task;

      if (OB_FAIL(task_queue_lock_.lock())) {
        LOG_WARN("Failed to lock task queue", K(ret));
        break;
      }

      if (task_queue_.empty()) {
        task_queue_lock_.unlock();
        break;
      }

      task = task_queue_[0];
      task_queue_.erase(0);
      task_queue_lock_.unlock();

      // Execute task
      int exec_ret = execute_task(task);
      if (OB_FAIL(exec_ret)) {
        LOG_WARN("Failed to execute AI column task", K(exec_ret), K(task));

        // Retry if retries remain
        if (task.retry_count_ < task.max_retries_) {
          task.retry_count_++;
          // Exponential backoff: delay = retry_delay * 2^retry_count
          int64_t backoff_delay = task.retry_delay_ * (1 << task.retry_count_);

          // Re-submit task after delay (simplified - would use timer in production)
          LOG_INFO("Scheduling retry for AI column task",
                   K(task.column_id_), K(task.row_id_),
                   K(task.retry_count_), K(backoff_delay));

          // In production, would schedule with timer
          // For now, just re-queue immediately
          submit_task(task);
        } else {
          // Max retries exceeded, mark as failed
          update_task_status(task.column_id_, task.row_id_,
                            "FAILED", "Max retries exceeded");
        }
      }
    }
  }

  return ret;
}

int ObAiColumnScheduler::execute_task(ObAiColumnTask& task)
{
  int ret = OB_SUCCESS;
  ObString result;

  // Update status to PROCESSING
  if (OB_FAIL(update_task_status(task.column_id_, task.row_id_, "PROCESSING", ""))) {
    LOG_WARN("Failed to update task status to PROCESSING", K(ret));
  }

  // Call AI service
  if (OB_SUCC(ret) && OB_FAIL(call_ai_service(task, result))) {
    LOG_WARN("Failed to call AI service", K(ret), K(task));
  }

  // Update hidden columns with result
  if (OB_SUCC(ret)) {
    ObString request_payload; // Would serialize task to JSON
    if (OB_FAIL(update_hidden_columns(task, request_payload, result))) {
      LOG_WARN("Failed to update hidden columns", K(ret));
    } else {
      // Update status to COMPLETED
      if (OB_FAIL(update_task_status(task.column_id_, task.row_id_, "COMPLETED", ""))) {
        LOG_WARN("Failed to update task status to COMPLETED", K(ret));
      }
    }
  }

  return ret;
}

int ObAiColumnScheduler::call_ai_service(ObAiColumnTask& task,
                                         ObString& result)
{
  int ret = OB_SUCCESS;

  // Route to appropriate AI function based on ai_type
  if (task.ai_type_.case_compare("AI_COMPLETE") == 0) {
    // Call AI_COMPLETE function
    // In production, would invoke through expression framework
    LOG_INFO("Calling AI_COMPLETE service", K(task.model_name_), K(task.prompt_expr_));
    // Placeholder: set mock result
    result = ObString("{\"result\": \"mock_complete_response\"}");
  } else if (task.ai_type_.case_compare("AI_EMBED") == 0) {
    // Call AI_EMBED function
    LOG_INFO("Calling AI_EMBED service", K(task.model_name_), K(task.prompt_expr_));
    // Placeholder: set mock embedding vector
    result = ObString("[0.1, 0.2, 0.3, 0.4]");
  } else if (task.ai_type_.case_compare("AI_RERANK") == 0) {
    // Call AI_RERANK function
    LOG_INFO("Calling AI_RERANK service", K(task.model_name_), K(task.prompt_expr_));
    // Placeholder: set mock rerank scores
    result = ObString("[0.9, 0.7, 0.5]");
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_type", task.ai_type_.ptr());
  }

  return ret;
}

int ObAiColumnScheduler::update_hidden_columns(const ObAiColumnTask& task,
                                               const ObString& request_payload,
                                               const ObString& result)
{
  int ret = OB_SUCCESS;
  // In production, would execute DML to update the three hidden columns:
  // 1. state_column: set to 'COMPLETED'
  // 2. request_column: store serialized request
  // 3. result_spec_column: store AI result
  LOG_INFO("Updating hidden columns for AI column",
           K(task.column_id_), K(task.row_id_));
  return ret;
}

int ObAiColumnScheduler::update_task_status(uint64_t column_id,
                                            uint64_t row_id,
                                            const ObString& status,
                                            const ObString& error_msg)
{
  int ret = OB_SUCCESS;
  // In production, would update __all_ai_column system table
  LOG_INFO("Updating AI column task status",
           K(column_id), K(row_id), K(status), K(error_msg));
  return ret;
}

int ObAiColumnScheduler::retry_task(const ObAiColumnTask& task)
{
  int ret = OB_SUCCESS;
  ObAiColumnTask retry_task = task;
  retry_task.retry_count_++;

  if (retry_task.retry_count_ > retry_task.max_retries_) {
    ret = OB_ERR_EXCEED_LIMIT;
    LOG_WARN("Task has exceeded max retries", K(ret), K(retry_task));
  } else if (OB_FAIL(submit_task(retry_task))) {
    LOG_WARN("Failed to submit retry task", K(ret));
  }

  return ret;
}

int ObAiColumnScheduler::cancel_tasks_for_column(uint64_t column_id)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(task_queue_lock_.lock())) {
    LOG_WARN("Failed to lock task queue", K(ret));
  } else {
    // Remove all tasks for this column
    for (int64_t i = task_queue_.count() - 1; i >= 0; i--) {
      if (task_queue_[i].column_id_ == column_id) {
        task_queue_.erase(i);
      }
    }
    task_queue_lock_.unlock();
  }

  return ret;
}

} // namespace sql
} // namespace oceanbase
