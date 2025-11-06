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

#ifndef OCEANBASE_LOGSERVICE_OB_RECONFIG_CHECKER_ADAPTER_H_
#define OCEANBASE_LOGSERVICE_OB_RECONFIG_CHECKER_ADAPTER_H_

#include <stdint.h>
#include "logservice/palf/palf_callback.h"
#include "lib/net/ob_addr.h"
#include "rootserver/ob_ls_recovery_stat_handler.h"

namespace oceanbase
{
namespace logservice
{
class ObReconfigCheckerAdapter : public palf::PalfReconfigCheckerCb
{
public:
  explicit ObReconfigCheckerAdapter();
  virtual ~ObReconfigCheckerAdapter() { }
  int init(const uint64_t tenant_id, const share::ObLSID &ls_id, const int64_t &timeout = -1);
  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(timeout));
public:
  virtual int check_can_add_member(const ObAddr &server,
                                   const int64_t timeout_us) override final;
  virtual int check_can_change_memberlist(const ObMemberList &new_member_list,
                                          const int64_t paxos_replica_num,
                                          const int64_t timeout_us) override final;
private:
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  int64_t timeout_;
  rootserver::ObLSRecoveryGuard guard_;
};

} // logservice
} // oceanbase

#endif
