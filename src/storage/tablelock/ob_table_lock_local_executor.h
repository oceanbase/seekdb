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

#ifndef OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_LOCAL_EXECUTOR_H_
#define OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_LOCAL_EXECUTOR_H_

#include <new>

#include "storage/tablelock/ob_table_lock_rpc_struct.h"

namespace oceanbase
{
namespace observer
{
// Local batch-lock handlers run synchronously in the current tenant MTL context.
// The return value preserves the former transport-code channel, while result carries
// the task and transaction execution status.
int handle_batch_lock_task(
    const transaction::tablelock::ObLockTaskBatchRequest<transaction::tablelock::ObLockParam> &arg,
    transaction::tablelock::ObTableLockTaskResult &result);
int handle_high_priority_batch_lock_task(
    const transaction::tablelock::ObLockTaskBatchRequest<transaction::tablelock::ObLockParam> &arg,
    transaction::tablelock::ObTableLockTaskResult &result);
int handle_batch_replace_lock_task(
    const transaction::tablelock::ObLockTaskBatchRequest<transaction::tablelock::ObReplaceLockParam> &arg,
    transaction::tablelock::ObTableLockTaskResult &result);

template <class Request>
class ObLocalBatchLockExecutor
{
public:
  using Result = transaction::tablelock::ObTableLockTaskResult;
  using Handler = int (*)(const Request &, Result &);
  explicit ObLocalBatchLockExecutor(Handler handler)
    : handler_(handler), return_code_(common::OB_SUCCESS), result_() {}
  ~ObLocalBatchLockExecutor() = default;

  int execute(const Request &request)
  {
    result_.~Result();
    new (&result_) Result();
    return_code_ = handler_(request, result_);
    return common::OB_SUCCESS;
  }

  int get_return_code() const { return return_code_; }
  const Result &get_result() const { return result_; }

private:
  Handler handler_;
  int return_code_;
  Result result_;
};

} // namespace observer
} // namespace oceanbase
#endif /* OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_LOCAL_EXECUTOR_H_ */
