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

#ifndef OCEANBASE_STANDBY_CONTROL_STANDBY_STATE_STORE_H_
#define OCEANBASE_STANDBY_CONTROL_STANDBY_STATE_STORE_H_

#include "share/ob_server_info.h"

namespace oceanbase
{
namespace common
{
class ObConfigManager;
}
namespace standby
{

class StandbyStateStore final : public share::IServerRoleStateProvider
{
public:
  StandbyStateStore()
    : config_manager_(nullptr), boot_role_(share::ObServerRole::INVALID_ROLE)
  {}

  int init(common::ObConfigManager &config_manager,
           const share::ObServerRole::Role boot_role);
  void reset();

  int load(share::ObServerInfo &server_info) const;
  int initialize() const;
  int update(const share::ObServerInfo &server_info) const;
  int get_server_info(share::ObServerInfo &server_info) const override
  {
    return load(server_info);
  }

private:
  common::ObConfigManager *config_manager_;
  share::ObServerRole::Role boot_role_;
};

} // namespace standby
} // namespace oceanbase

#endif // OCEANBASE_STANDBY_CONTROL_STANDBY_STATE_STORE_H_
