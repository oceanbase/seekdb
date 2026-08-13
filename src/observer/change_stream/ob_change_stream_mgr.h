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
#include "observer/change_stream/ob_change_stream_fetcher.h"
#include "observer/change_stream/ob_change_stream_dispatcher.h"
#include "observer/change_stream/ob_change_stream_worker.h"
namespace oceanbase
{
namespace logservice
{
class ObILogStorage;
}
namespace share
{
namespace schema
{
class ObSchemaPublishSignal;
}

/// Process-wide Change Stream manager.
/// Plugin instances are created per-batch in ObCSExecCtx, not held at Mgr level.
class ObChangeStreamMgr
{
public:
  ObChangeStreamMgr();
  virtual ~ObChangeStreamMgr();

  /// Server module init: called after construction to initialize internal state.
  static int server_module_init(
      ObChangeStreamMgr *&mgr,
      logservice::ObILogStorage &log_storage,
      schema::ObSchemaPublishSignal &schema_publish_signal,
      lib::IRunWrapper *run_wrapper);

  int init(
      logservice::ObILogStorage &log_storage,
      schema::ObSchemaPublishSignal &schema_publish_signal,
      lib::IRunWrapper *run_wrapper);
  int start();
  void stop();
  void wait();
  void destroy();

  bool is_inited() const { return is_inited_; }

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
  bool is_inited_;
  ObCSFetcher fetcher_;
  ObCSDispatcher dispatcher_;
  ObCSWorker worker_;
};

}  // namespace share
}  // namespace oceanbase

#endif  // OB_CS_MGR_H_
