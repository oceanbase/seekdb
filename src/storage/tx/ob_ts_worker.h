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

#ifndef OCEANBASE_TRANSACTION_OB_TS_WORKER_
#define OCEANBASE_TRANSACTION_OB_TS_WORKER_

#include "lib/utility/utility.h"
#include "lib/thread/thread_mgr_interface.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace transaction
{
class ObTsResponseTask;
class ObTsMgr;
class ObTsWorker : public lib::TGTaskHandler
{
public:
  ObTsWorker() : is_inited_(false), use_local_worker_(false), ts_mgr_(NULL), tg_id_(-1) {}
  ~ObTsWorker() {}
  int init(ObTsMgr *ts_mgr, const bool use_local_worker = false);
  void stop();
  void wait();
  void destroy();
public:
  int push_task(const uint64_t tenant_id, ObTsResponseTask *task);
  void handle(void *task);
public:
  static const int64_t MAX_TASK_NUM = 10240;
private:
  bool is_inited_;
  bool use_local_worker_;
  ObTsMgr *ts_mgr_;
  int tg_id_;
};

} // transaction
} // oceanbase

#endif // OCEANBASE_TRANSACTION_OB_TS_WORKER_
