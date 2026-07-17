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

#ifndef OCEANBASE_TRANSACTION_OB_TX_TIMESTAMP_WAITER_
#define OCEANBASE_TRANSACTION_OB_TX_TIMESTAMP_WAITER_

#include "lib/lock/ob_thread_cond.h"
#include "lib/queue/ob_link_queue.h"
#include "lib/thread/ob_simple_thread_pool.h"
#include "share/ob_thread_pool.h"

namespace oceanbase
{
namespace share
{
class SCN;
}
namespace transaction
{

class ObTsMgr;
class ObTxCtx;

class ObTxTimestampWaiter;

class ObTxTimestampCallbackWorker final : public common::ObSimpleThreadPool
{
public:
  ObTxTimestampCallbackWorker() : waiter_(NULL) {}
  int init(ObTxTimestampWaiter *waiter);
protected:
  void handle(void *task) override;
private:
  static const int64_t MAX_TASK_NUM = 10240;
  ObTxTimestampWaiter *waiter_;
};

class ObTxTimestampWaiter final : public share::ObThreadPool
{
  friend class ObTxTimestampCallbackWorker;
public:
  ObTxTimestampWaiter();
  ~ObTxTimestampWaiter() { destroy(); }

  int init(ObTsMgr *ts_mgr);
  int start() override;
  void stop() override;
  void destroy();
  int wait_gts_elapse(const share::SCN &target_scn, ObTxCtx *ctx, bool &need_wait);

private:
  void run1() override;
  void dispatch_ready_tasks_(const share::SCN &gts);
  void handle_callback_(ObTxCtx *ctx);
  void requeue_or_interrupt_(ObTxCtx *ctx);
  void drain_wait_queue_();
  bool is_running_() const { return ATOMIC_LOAD(&running_); }

private:
  static const int64_t POLL_INTERVAL_US = 500;
  bool is_inited_;
  bool running_;
  uint64_t latest_gts_;
  ObTsMgr *ts_mgr_;
  common::ObLinkQueue wait_queue_;
  common::ObThreadCond cond_;
  ObTxTimestampCallbackWorker callback_worker_;
};

} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_TRANSACTION_OB_TX_TIMESTAMP_WAITER_
