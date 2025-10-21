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

#ifndef OCEANBASE_ROOTSERVER_FREEZE_OB_FREEZE_REENTRANT_THREAD_H_
#define OCEANBASE_ROOTSERVER_FREEZE_OB_FREEZE_REENTRANT_THREAD_H_

#include "rootserver/ob_rs_reentrant_thread.h"
#include "common/ob_role.h"

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}
namespace rootserver
{

class ObFreezeReentrantThread : public ObRsReentrantThread
{
public:
  ObFreezeReentrantThread(const uint64_t tenant_id);
  virtual ~ObFreezeReentrantThread() {}

  virtual void pause();
  virtual void resume();
  bool is_paused() const { return is_paused_; }

  int64_t get_epoch() const { return epoch_; }
  int set_epoch(const int64_t epoch);

protected:
  virtual int try_idle(const int64_t idle_time_us, const int exe_ret);
  int obtain_proposal_id_from_ls(const bool is_primary_service, int64_t &proposal_id, common::ObRole &role);

protected:
  uint64_t tenant_id_;
  common::ObMySQLProxy *sql_proxy_;

private:
  bool is_paused_;
  // @epoch, is used to solve 'multi-freeze_service' may operate inner table concurrently.
  //
  // For solving switching-role slowly, we keep the tenant major_freeze_service, just
  // mark it as 'paused' state, not destroy it.
  // It is not a perfect way cuz it may occur that 'new freeze_service' start to work
  // while the 'old freeze_service' is still working before changing to 'paused' state.
  //
  // So we add a column in __all_service_epoch table, named 'freeze_service_epoch'. We use epoch_
  // to update it only when epoch_ is greater than it.
  // If epoch_ is changing during one round execution, we should mark this round execution as
  // failed, and retry in next round.
  int64_t epoch_;
};

} // rootserver
} // oceanbase
#endif // OCEANBASE_ROOTSERVER_FREEZE_OB_FREEZE_REENTRANT_THREAD_H_
