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

#include "lib/container/ob_array.h"
#include "lib/net/ob_addr.h"
#include "lib/allocator/page_arena.h"
#include "storage/tablelock/ob_table_lock_rpc_struct.h"

namespace oceanbase
{
namespace observer
{
// Local batch-lock handlers (single-replica): formerly the ObBatchLockTaskP /
// ObHighPriorityBatchLockTaskP / ObBatchReplaceLockTaskP processor bodies. They run in
// the current tenant MTL context (the table-lock service already runs there) and stuff
// the per-LS result; the return value is the "transport" code (OB_SUCCESS for local).
int handle_batch_lock_task(
    const transaction::tablelock::ObLockTaskBatchRequest<transaction::tablelock::ObLockParam> &arg,
    transaction::tablelock::ObTableLockTaskResult &result);
int handle_high_priority_batch_lock_task(
    const transaction::tablelock::ObLockTaskBatchRequest<transaction::tablelock::ObLockParam> &arg,
    transaction::tablelock::ObTableLockTaskResult &result);
int handle_batch_replace_lock_task(
    const transaction::tablelock::ObLockTaskBatchRequest<transaction::tablelock::ObReplaceLockParam> &arg,
    transaction::tablelock::ObTableLockTaskResult &result);

// Drop-in replacement for the removed ObBatchLockProxy / ObHighPriorityBatchLockProxy /
// ObBatchReplaceLockProxy. Mimics the subset of the async rpc-proxy interface that
// ObTableLockService uses (reuse/call/wait_all/check_return_cnt/get_results), but dispatches
// each per-LS request directly to the local handler instead of sending an RPC.
template <class Request>
class ObLocalBatchLockProxy
{
public:
  using Result = transaction::tablelock::ObTableLockTaskResult;
  using Handler = int (*)(const Request &, Result &);
  explicit ObLocalBatchLockProxy(Handler handler)
    : handler_(handler), allocator_("LocalLockRpc") {}
  ~ObLocalBatchLockProxy() { destroy_results_(); }
  void reuse()
  {
    destroy_results_();
    dests_.reuse();
    rcodes_.reuse();
    result_ptrs_.reuse();
    allocator_.reset();
  }
  // Result is non-copyable (DISALLOW_COPY_AND_ASSIGN); allocate each from the arena and
  // fill it in place, mirroring the old proxy which owned deserialized Result objects.
  int call(const common::ObAddr &dest,
           const int64_t timeout_us,
           const int64_t cluster_id,
           const uint64_t tenant_id,
           const int32_t group_id,
           const Request &request)
  {
    UNUSED(timeout_us);
    UNUSED(cluster_id);
    UNUSED(tenant_id);
    UNUSED(group_id);
    int ret = common::OB_SUCCESS;
    void *buf = allocator_.alloc(sizeof(Result));
    if (OB_ISNULL(buf)) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
    } else {
      Result *result = new (buf) Result();
      const int rcode = handler_(request, *result);
      if (OB_FAIL(dests_.push_back(dest))) {
      } else if (OB_FAIL(rcodes_.push_back(rcode))) {
      } else if (OB_FAIL(result_ptrs_.push_back(result))) {
      }
    }
    return ret;
  }
  int wait_all(common::ObArray<int> &return_code_array)
  {
    return return_code_array.assign(rcodes_);
  }
  int check_return_cnt(const int64_t return_cnt) const
  {
    return (return_cnt == result_ptrs_.count()) ? common::OB_SUCCESS : common::OB_ERR_UNEXPECTED;
  }
  common::ObIArray<Result *> &get_results() { return result_ptrs_; }
  const common::ObIArray<common::ObAddr> &get_dests() const { return dests_; }
private:
  void destroy_results_()
  {
    for (int64_t i = 0; i < result_ptrs_.count(); ++i) {
      if (OB_NOT_NULL(result_ptrs_.at(i))) {
        result_ptrs_.at(i)->~Result();
      }
    }
  }
  Handler handler_;
  common::ObArenaAllocator allocator_;
  common::ObArray<common::ObAddr> dests_;
  common::ObArray<int> rcodes_;
  common::ObArray<Result *> result_ptrs_;
};

} // namespace observer
} // namespace oceanbase
#endif /* OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_LOCAL_EXECUTOR_H_ */
