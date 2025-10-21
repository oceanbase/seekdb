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

#ifndef OCEANBASE_STORAGE_OB_LS_FREEZE_THREAD_
#define OCEANBASE_STORAGE_OB_LS_FREEZE_THREAD_

#include "lib/lock/ob_spin_lock.h"
#include "lib/thread/thread_mgr_interface.h"
#include "share/scn.h"

namespace oceanbase
{
using namespace lib;
using namespace common;
namespace storage
{
namespace checkpoint {
  class ObDataCheckpoint;
}

class ObLSFreezeThread;

// Traverse ls_frozen_list
class ObLSFreezeTask
{
public:
  void set_task(ObLSFreezeThread *host,
                checkpoint::ObDataCheckpoint *data_checkpoint,
                share::SCN rec_scn);

  void handle();

private:
  share::SCN rec_scn_;
  ObLSFreezeThread *host_;
  checkpoint::ObDataCheckpoint *data_checkpoint_;
};

class ObLSFreezeThread : public TGTaskHandler
{
  friend class ObLSFreezeTask;

public:
  static const int64_t QUEUE_THREAD_NUM = 3;
  static const int64_t MINI_MODE_QUEUE_THREAD_NUM = 1;
  static const int64_t MAX_FREE_TASK_NUM = 5;

  ObLSFreezeThread();
  virtual ~ObLSFreezeThread();

  int init(int64_t tenant_id, int tg_id);
  void destroy();

  int add_task(checkpoint::ObDataCheckpoint *data_checkpoint, share::SCN rec_scn);
  void handle(void *task);
  int get_tg_id() { return tg_id_; }

private:
  int push_back_(ObLSFreezeTask *task);

  bool inited_;
  int tg_id_;  // thread group id
  ObLSFreezeTask *task_array_[MAX_FREE_TASK_NUM];
  int64_t available_index_;
  ObSpinLock lock_;
};

}  // namespace storage
}  // namespace oceanbase
#endif
