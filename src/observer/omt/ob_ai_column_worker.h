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

#include "lib/container/ob_iarray.h"
#include "lib/string/ob_string.h"
#include "lib/task/ob_timer.h"

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
class ObIAllocator;
}
namespace omt
{

// Polls oceanbase.__all_ai_column_task, calls the registered AI models and
// publishes the results back to the user tables.
class ObAiColumnWorker : public common::ObTimerTask
{
public:
  ObAiColumnWorker();
  virtual ~ObAiColumnWorker();
  int init(common::ObMySQLProxy &sql_proxy);
  int start();
  void stop();
  void wait();
  void destroy();
  virtual void runTimerTask() override;

private:
  struct TaskRow
  {
    TaskRow() : table_id_(0), column_id_(0), request_id_(), rowkey_predicate_(), retry_count_(0) {}
    uint64_t table_id_;
    uint64_t column_id_;
    common::ObString request_id_;
    common::ObString rowkey_predicate_;
    int64_t retry_count_;
  };
  int do_work();
  int claim_tasks(common::ObIAllocator &alloc, common::ObIArray<TaskRow> &tasks);
  int process_all(common::ObIAllocator &alloc, common::ObIArray<TaskRow> &tasks);
  int process_group(common::ObIAllocator &alloc, common::ObIArray<TaskRow> &group);
  int delete_group_tasks(const uint64_t table_id, const common::ObString &request_id);

  common::ObTimer timer_;
  common::ObMySQLProxy *sql_proxy_;
  bool is_inited_;
  volatile bool is_running_;
  int64_t round_;
};

} // namespace omt
} // namespace oceanbase
