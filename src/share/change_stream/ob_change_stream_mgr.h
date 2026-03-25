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
 * Change Stream manager: tenant-level MTL module.
 * Owns Fetcher / Dispatcher / ObCSWorker (which owns multiple ObCSExecutor).
 */

#ifndef OB_CS_MGR_H_
#define OB_CS_MGR_H_

#include "lib/ob_define.h"
#include "share/change_stream/ob_change_stream_fetcher.h"
#include "share/change_stream/ob_change_stream_dispatcher.h"
#include "share/change_stream/ob_change_stream_worker.h"
namespace oceanbase
{
namespace share
{

/// Tenant-level Change Stream manager (MTL module).
/// Plugin instances are created per-batch in ObCSExecCtx, not held at Mgr level.
class ObChangeStreamMgr
{
public:
  ObChangeStreamMgr();
  virtual ~ObChangeStreamMgr();

  /// MTL init: called after default new; inits tenant_id and internal state.
  static int mtl_init(ObChangeStreamMgr *&mgr);

  int init();
  int start();
  void stop();
  void wait();
  void destroy();

  bool is_inited() const { return is_inited_; }

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
