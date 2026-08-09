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
namespace common
{
class ObMySQLProxy;
class ObISQLClient;
class ObConfigManager;
}
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
    return server_role_.is_valid()
           && switchover_status_.is_valid()
           && (!pending_role_.is_valid()
               || pending_role_.is_primary()
               || pending_role_.is_standby());
  }

  void reset() {
    server_role_.reset();
    pending_role_.reset();
    switchover_status_.reset();
    cutover_scn_.reset();
  }

  int assign(const ObServerInfo &other) {
    server_role_ = other.server_role_;
    pending_role_ = other.pending_role_;
    switchover_status_ = other.switchover_status_;
    cutover_scn_ = other.cutover_scn_;
    return OB_SUCCESS;
  }

  // Getters
  const ObServerRole &get_server_role() const { return server_role_; }
  const ObServerRole &get_pending_role() const { return pending_role_; }
  const ObServerSwitchoverStatus &get_switchover_status() const { return switchover_status_; }
  const SCN &get_cutover_scn() const { return cutover_scn_; }

  // Convenience methods
  bool is_primary() const { return server_role_.is_primary(); }
  bool is_standby() const { return server_role_.is_standby(); }
  bool has_pending_role() const { return pending_role_.is_valid(); }
  bool is_preparing_status() const { return switchover_status_.is_preparing_status(); }
  bool is_normal_status() const { return switchover_status_.is_normal_status(); }
  bool is_switching_to_primary_status() const { return switchover_status_.is_switching_to_primary_status(); }
  bool is_switching_to_standby_status() const { return switchover_status_.is_switching_to_standby_status(); }
  bool is_prepare_switching_to_standby_status() const { return switchover_status_.is_prepare_switching_to_standby_status(); }
  bool is_prepare_switching_to_primary_status() const { return switchover_status_.is_prepare_switching_to_primary_status(); }
  bool is_prepare_flashback_for_failover_to_primary_status() const { return switchover_status_.is_prepare_flashback_for_failover_to_primary_status(); }
  bool is_flashback_status() const { return switchover_status_.is_flashback_status(); }

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

  OB_UNIS_VERSION(1);
};

class ObServerInfoProxy
{
public:
  static int load_server_info(
      common::ObConfigManager *config_mgr,
      const ObServerRole::Role fallback_role,
      ObServerInfo &server_info);

  static int init_server_info_from_role(
      common::ObConfigManager *config_mgr,
      const ObServerRole::Role server_role);

  static int update_server_info(
      common::ObConfigManager *config_mgr,
      const ObServerInfo &server_info);

};

} // namespace share
} // namespace oceanbase

#endif /* OCEANBASE_SHARE_OB_SERVER_INFO_H_ */
