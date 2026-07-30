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
 *
 * Process-wide Change Stream manager owned by the server.
 * Owns Fetcher / Dispatcher / ObCSWorker (which owns multiple ObCSExecutor).
 */

#ifndef OB_CS_MGR_H_
#define OB_CS_MGR_H_

#include "lib/ob_define.h"
#include "share/ob_background_task_executor.h"
#include "observer/change_stream/ob_change_stream_fetcher.h"
#include "observer/change_stream/ob_change_stream_dispatcher.h"
#include "observer/change_stream/ob_change_stream_worker.h"
namespace oceanbase
{
namespace share
{

/// Process-wide Change Stream manager.
/// Plugin instances are created per-batch in ObCSExecCtx, not held at Mgr level.
class ObChangeStreamMgr : public ObIBackgroundTaskSource
{
public:
  ObChangeStreamMgr();
  virtual ~ObChangeStreamMgr();

  /// Server module init: called after construction to initialize internal state.
  static int server_module_init(ObChangeStreamMgr *&mgr);

  int init();
  int start();
  void stop();
  void wait();
  void destroy();

  bool is_inited() const { return is_inited_; }

  int process_one_quantum(
      const ObBackgroundTaskPriority priority,
      ObBackgroundTaskRunResult &result) override;

  /// Wake the dedicated Fetcher after activation, or the mini-mode idle
  /// maintenance source while the Change Stream components are still lazy.
  void notify_schema_changed();

  /// Block until change_stream_refresh_scn >= current safe visible scn, or timeout.
  /// Can be called from any node (RS / observer) as long as sql_client is valid.
  static int wait_refresh_scn(
      common::ObISQLClient &sql_client,
      const int64_t timeout_us);

  /// Fetcher: consumes CLOG by transaction, pushes committed tx to Dispatcher.
  ObCSFetcher &get_fetcher() { return fetcher_; }

  /// Dispatcher: consumes committed tx from ring buffer, slices and pushes to Worker.
  ObCSDispatcher &get_dispatcher() { return dispatcher_; }

  ObCSWorker &get_worker() { return worker_; }

private:
  int start_components_();
  void stop_components_();
  void wait_components_();
  int register_background_source_();
  int unregister_background_source_(const bool wait_running);
  int notify_background_source_();

  bool is_inited_;
  bool use_lazy_start_;
  bool is_running_;
  bool components_started_;
  bool fetcher_started_;
  bool dispatcher_started_;
  bool worker_started_;
  lib::ObMutex lifecycle_lock_;
  ObBackgroundTaskExecutor *background_executor_;
  ObBackgroundTaskSourceHandle source_handle_;
  ObCSFetcher fetcher_;
  ObCSDispatcher dispatcher_;
  ObCSWorker worker_;
};

}  // namespace share
}  // namespace oceanbase

#endif  // OB_CS_MGR_H_
