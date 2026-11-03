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

// OB-P:a4L9E_IDIyEM2Ol_ZvDXqw

#pragma once

#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_array.h"
#include "lib/string/ob_string.h"
#include "lib/task/ob_timer.h"
#include "share/schema/ob_ai_column_spec.h"

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}
namespace observer
{

// The state of a task row of __all_ai_column_task.
//   1 PENDING      : the task waits for its first execution
//   2 RUNNING      : reserved
//   3 RETRY_WAIT   : the task failed and waits for its next execution
class ObAiColumnTaskState
{
public:
  static const int64_t STATE_PENDING = 1;
  static const int64_t STATE_RETRY_WAIT = 3;
};

// One fetched row of __all_ai_column_task.  The strings are deep copied into
// the arena allocator of the current round.
struct ObAiColumnTaskRow
{
  uint64_t table_id_;
  common::ObString request_id_;
  uint64_t column_id_;
  common::ObString rowkey_predicate_;
  common::ObString payload_;
  int64_t retry_count_;
  int64_t state_;

  ObAiColumnTaskRow()
      : table_id_(common::OB_INVALID_ID),
        column_id_(common::OB_INVALID_ID),
        retry_count_(0),
        state_(0)
  {}
};

// The background worker of the asynchronous AI column feature.  The timer
// fetches the due tasks of __all_ai_column_task, groups them by request id,
// calls the model once per AI column of the group and publishes the results
// of one group atomically inside one transaction.
//
// The publish of one group is guarded by the request id of the row: the
// UPDATE only matches when the hidden request column still carries the
// request id of the group, so a late result of an old request can never
// overwrite the invalidation of a newer request.
class ObAiColumnWorkerTask : public common::ObTimerTask
{
public:
  ObAiColumnWorkerTask();
  virtual ~ObAiColumnWorkerTask();

  int init(common::ObMySQLProxy &sql_proxy);
  virtual void runTimerTask() override;
  void destroy() {}

private:
  // The work of one timer round.
  void process_tasks();
  // Fetches the due tasks from __all_ai_column_task.
  int fetch_due_tasks(common::ObIAllocator &alloc,
                      common::ObIArray<ObAiColumnTaskRow> &task_rows);
  // Processes one group of tasks that shares one request id.
  int process_group(common::ObArenaAllocator &alloc,
                    const common::ObIArray<ObAiColumnTaskRow> &group);
  // The generation of one AI column of the group.
  struct ColumnResult
  {
    uint64_t column_id_;
    common::ObString value_;
    common::ObString result_name_;
    common::ObString state_name_;
    common::ObString request_name_;
    bool success_;
    int err_;

    ColumnResult() : column_id_(common::OB_INVALID_ID), success_(false), err_(OB_SUCCESS) {}
  };
  // Calls the model for one AI column of the group.
  int generate_column(common::ObArenaAllocator &alloc,
                      const share::schema::ObAiColumnSpec &spec,
                      const share::schema::ObColumnSchemaV2 &column,
                      const common::ObString &payload,
                      common::ObString &result);
  // Publishes the results of the group atomically and removes the tasks.
  int publish_group(common::ObArenaAllocator &alloc,
                    const common::ObIArray<ObAiColumnTaskRow> &group,
                    const common::ObIArray<ColumnResult> &results,
                    const common::ObString &db_name,
                    const common::ObString &table_name);
  // Schedules the next attempt of the group.
  int retry_group(common::ObArenaAllocator &alloc,
                  const common::ObIArray<ObAiColumnTaskRow> &group,
                  int64_t next_retry_count,
                  const common::ObString &last_error);
  // Marks the rows of the group as failed and removes the tasks.
  int fail_group(common::ObArenaAllocator &alloc,
                 const common::ObIArray<ObAiColumnTaskRow> &group,
                 const common::ObIArray<ColumnResult> &results,
                 const common::ObString &db_name,
                 const common::ObString &table_name);

private:
  common::ObMySQLProxy *sql_proxy_;
  bool is_inited_;
};

// The scheduler that owns the timer of the AI column worker.
class ObAiColumnWorkerScheduler
{
public:
  static const int64_t AI_COLUMN_WORKER_PERIOD_US = 500 * 1000L; // 500ms

  ObAiColumnWorkerScheduler();
  virtual ~ObAiColumnWorkerScheduler() {}

  int init(common::ObMySQLProxy &sql_proxy);
  int start();
  void stop();
  void wait();
  void destroy();

private:
  bool is_inited_;
  common::ObTimer timer_;
  ObAiColumnWorkerTask worker_task_;
};

} // namespace observer
} // namespace oceanbase
