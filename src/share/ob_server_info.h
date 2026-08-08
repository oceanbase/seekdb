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
      switchover_status_(ObServerSwitchoverStatus::INVALID_STATUS) {}
  ~ObServerInfo() {}

  bool is_valid() const {
    return server_role_.is_valid()
           && switchover_status_.is_valid();
  }

  void reset() {
    server_role_.reset();
    switchover_status_.reset();
  }

  int assign(const ObServerInfo &other) {
    server_role_ = other.server_role_;
    switchover_status_ = other.switchover_status_;
    return OB_SUCCESS;
  }

  // Getters
  const ObServerRole &get_server_role() const { return server_role_; }
  const ObServerSwitchoverStatus &get_switchover_status() const { return switchover_status_; }

  // Convenience methods
  bool is_primary() const { return server_role_.is_primary(); }
  bool is_standby() const { return server_role_.is_standby(); }
  bool is_normal_status() const { return switchover_status_.is_normal_status(); }
  bool is_switching_to_primary_status() const { return switchover_status_.is_switching_to_primary_status(); }
  bool is_switching_to_standby_status() const { return switchover_status_.is_switching_to_standby_status(); }
  bool is_prepare_switching_to_standby_status() const { return switchover_status_.is_prepare_switching_to_standby_status(); }
  bool is_prepare_switching_to_primary_status() const { return switchover_status_.is_prepare_switching_to_primary_status(); }

  TO_STRING_KV(K_(server_role), K_(switchover_status));

  ObServerRole server_role_;
  ObServerSwitchoverStatus switchover_status_;

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

};

} // namespace share
} // namespace oceanbase

#endif /* OCEANBASE_SHARE_OB_SERVER_INFO_H_ */
