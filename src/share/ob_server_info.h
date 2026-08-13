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

#ifndef OCEANBASE_SHARE_OB_SERVER_INFO_H_
#define OCEANBASE_SHARE_OB_SERVER_INFO_H_

#include "share/ob_server_role.h"              // ObServerRole
#include "share/ob_server_switchover_status.h"  // ObServerSwitchoverStatus
#include "share/scn.h"                         // SCN
#include "lib/utility/ob_print_utils.h"        // TO_STRING_KV

namespace oceanbase
{
namespace share
{

struct ObServerInfo
{
  ObServerInfo()
    : server_role_(ObServerRole::INVALID_ROLE),
      pending_role_(ObServerRole::INVALID_ROLE),
      switchover_status_(ObServerSwitchoverStatus::INVALID_STATUS),
      cutover_scn_() {}
  ~ObServerInfo() {}

  bool is_valid() const {
    const bool normal = switchover_status_.is_normal_status()
        && !pending_role_.is_valid()
        && !cutover_scn_.is_valid();
    const bool preparing = switchover_status_.is_preparing_status()
        && (server_role_.is_primary() || server_role_.is_standby())
        && pending_role_.is_valid()
        && (pending_role_.is_primary() || pending_role_.is_standby())
        && pending_role_ != server_role_
        && cutover_scn_.is_valid();
    return server_role_.is_valid() && (normal || preparing);
  }

  void reset() {
    server_role_.reset();
    pending_role_.reset();
    switchover_status_.reset();
    cutover_scn_.reset();
  }

  const ObServerRole &get_server_role() const { return server_role_; }
  const ObServerRole &get_pending_role() const { return pending_role_; }
  const ObServerSwitchoverStatus &get_switchover_status() const { return switchover_status_; }
  const SCN &get_cutover_scn() const { return cutover_scn_; }

  bool is_primary() const { return server_role_.is_primary(); }
  bool is_standby() const { return server_role_.is_standby(); }
  bool has_pending_role() const { return pending_role_.is_valid(); }
  bool is_preparing_status() const { return switchover_status_.is_preparing_status(); }
  bool is_normal_status() const { return switchover_status_.is_normal_status(); }

  int activate_pending_role()
  {
    int ret = OB_SUCCESS;
    if (pending_role_.is_valid()) {
      if (!pending_role_.is_primary() && !pending_role_.is_standby()) {
        ret = OB_INVALID_DATA;
      } else {
        server_role_ = pending_role_;
        pending_role_.reset();
        switchover_status_ = NORMAL_SWITCHOVER_STATUS;
        cutover_scn_.reset();
      }
    }
    return ret;
  }

  TO_STRING_KV(K_(server_role), K_(pending_role), K_(switchover_status), K_(cutover_scn));

  ObServerRole server_role_;
  ObServerRole pending_role_;
  ObServerSwitchoverStatus switchover_status_;
  SCN cutover_scn_;
};

class IServerRoleStateProvider
{
public:
  virtual ~IServerRoleStateProvider() {}
  virtual int get_server_info(ObServerInfo &server_info) const = 0;
};

} // namespace share
} // namespace oceanbase

#endif /* OCEANBASE_SHARE_OB_SERVER_INFO_H_ */
