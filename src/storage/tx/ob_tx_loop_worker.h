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

#ifndef OCEANBASE_TRANSACTION_KEEP_ALIVE_SERVICE_
#define OCEANBASE_TRANSACTION_KEEP_ALIVE_SERVICE_

#include "lib/thread/thread_pool.h"

#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase {

namespace share
{
class ObLSID;
};

namespace storage
{
class ObLS;
}

namespace transaction 
{

class ObTxLoopWorker : public lib::ThreadPool
{
public:
  // keep alive
  const static int64_t LOOP_INTERVAL = 5 * 1000 * 1000;                       // 5s
  const static int64_t KEEP_ALIVE_PRINT_INFO_INTERVAL = 5 * 60 * 1000 * 1000; // 5min
  const static int64_t TX_GC_INTERVAL = 5 * 1000 * 1000;                     // 5s
  const static int64_t TX_RETAIN_CTX_GC_INTERVAL = 5 * 1000 * 1000;           // 5s
  const static int64_t TX_START_WORKING_RETRY_INTERVAL = 5 * 1000 * 1000;  //5s
  const static int64_t TX_LOG_CB_POOL_ADJUST_INTERVAL = 1 * 60 * 1000 * 1000; // 1min
public:
  ObTxLoopWorker() { reset(); }
  ~ObTxLoopWorker() {}
  static int mtl_init(ObTxLoopWorker *&ka);
  int init();
  int start();
  void stop();
  void wait();
  void destroy();

  void reset();

  virtual void run1();

private:
  int scan_all_ls_(bool can_tx_gc, bool can_gc_retain_ctx, bool can_check_and_retry_start_working, bool can_adjust_log_cb_pool);
  void do_keep_alive_(ObLS *ls, const share::SCN &min_start_scn, MinStartScnStatus status); // 100ms
  void do_tx_gc_(ObLS *ls, share::SCN &min_start_scn, MinStartScnStatus &status);     // 15s
  void update_max_commit_ts_();
  void do_retain_ctx_gc_(ObLS * ls);  // 15s
  void do_start_working_retry_(ObLS * ls);
  void do_log_cb_pool_adjust_(ObLS *ls, const common::ObRole role);
  void refresh_tenant_config_();

private:
  int64_t last_tx_gc_ts_;
  int64_t last_retain_ctx_gc_ts_;
  int64_t last_check_start_working_retry_ts_;
  int64_t last_log_cb_pool_adjust_ts_;
  int64_t last_tenant_config_refresh_ts_;
};


} // namespace transaction
} // namespace oceanbase

#endif
