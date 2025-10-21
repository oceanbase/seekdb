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

#define USING_LOG_PREFIX SHARE

#include "ob_ls_life_manager.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::palf;
namespace oceanbase
{
namespace share
{

int ObLSLifeAgentManager::create_new_ls(
    const ObLSStatusInfo &ls_info, const SCN &create_ls_scn,
    const common::ObString &zone_priority, const share::ObTenantSwitchoverStatus &working_sw_status)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans; 
  const uint64_t exec_tenant_id = ObLSLifeIAgent::get_exec_tenant_id(ls_info.tenant_id_);
  if (OB_UNLIKELY(!ls_info.is_valid() || !create_ls_scn.is_valid() || zone_priority.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(ls_info), K(create_ls_scn), K(zone_priority));
  } else {
    TAKE_IN_TRANS(create_new_ls, proxy_,
        exec_tenant_id, ls_info, create_ls_scn, zone_priority, working_sw_status);
  }
  return ret;
}

int ObLSLifeAgentManager::drop_ls(const uint64_t &tenant_id,
                                  const share::ObLSID &ls_id,
                                  const ObTenantSwitchoverStatus &working_sw_status)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans; 
  const uint64_t exec_tenant_id = ObLSLifeIAgent::get_exec_tenant_id(tenant_id);
  if (OB_UNLIKELY(!ls_id.is_valid() || OB_INVALID_TENANT_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(ls_id), K(tenant_id));
  } else {
    TAKE_IN_TRANS(drop_ls, proxy_, exec_tenant_id, tenant_id,
                 ls_id, working_sw_status);
  }
  return ret;

}

int ObLSLifeAgentManager::set_ls_offline(const uint64_t &tenant_id,
    const share::ObLSID &ls_id,
    const ObLSStatus &ls_status,
    const SCN &drop_scn,
    const ObTenantSwitchoverStatus &working_sw_status)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans; 
  const uint64_t exec_tenant_id = ObLSLifeIAgent::get_exec_tenant_id(tenant_id);
  if (OB_UNLIKELY(!ls_id.is_valid() || OB_INVALID_TENANT_ID == tenant_id
        || !drop_scn.is_valid()
        || (!ls_is_dropping_status(ls_status) && !ls_is_tenant_dropping_status(ls_status)))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(ls_id), K(tenant_id), K(drop_scn), K(ls_status));
  } else {
    TAKE_IN_TRANS(set_ls_offline, proxy_, exec_tenant_id, tenant_id,
        ls_id, ls_status, drop_scn, working_sw_status);
  }
  return ret;
}


int ObLSLifeAgentManager::create_new_ls_in_trans(const ObLSStatusInfo &ls_info,
                            const SCN &create_ls_scn,
                            const common::ObString &zone_priority,
                            const share::ObTenantSwitchoverStatus &working_sw_status,
                            ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ls_info.is_valid() || !create_ls_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(ls_info), K(create_ls_scn));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < MAX_AGENT_NUM; ++i) {
    if (OB_ISNULL(agents_[i])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("agent is null", KR(ret), K(i));
    } else if (OB_FAIL(agents_[i]->create_new_ls(ls_info, create_ls_scn, zone_priority, working_sw_status, trans))) {
      LOG_WARN("failed to create new ls", KR(ret), K(i), K(ls_info), K(create_ls_scn), K(zone_priority));
    }
  }
  return ret;
}

int ObLSLifeAgentManager::drop_ls_in_trans(const uint64_t &tenant_id,
                      const share::ObLSID &ls_id,
                      const ObTenantSwitchoverStatus &working_sw_status,
                      ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ls_id.is_valid() || OB_INVALID_TENANT_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(ls_id), K(tenant_id));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < MAX_AGENT_NUM; ++i) {
    if (OB_ISNULL(agents_[i])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("agent is null", KR(ret), K(i));
    } else if (OB_FAIL(agents_[i]->drop_ls(tenant_id, ls_id, working_sw_status, trans))) {
      LOG_WARN("failed to create new ls", KR(ret), K(i), K(tenant_id), K(ls_id));
    }
  }

  return ret;
}
int ObLSLifeAgentManager::set_ls_offline_in_trans(const uint64_t &tenant_id,
    const share::ObLSID &ls_id,
    const ObLSStatus &ls_status,
    const SCN &drop_scn,
    const ObTenantSwitchoverStatus &working_sw_status,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ls_id.is_valid() || OB_INVALID_TENANT_ID == tenant_id
        || !drop_scn.is_valid()
        || (!ls_is_dropping_status(ls_status) && !ls_is_tenant_dropping_status(ls_status)))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(ls_id), K(tenant_id), K(drop_scn), K(ls_status));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < MAX_AGENT_NUM; ++i) {
    if (OB_ISNULL(agents_[i])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("agent is null", KR(ret), K(i));
    } else if (OB_FAIL(agents_[i]->set_ls_offline(tenant_id, ls_id, 
            ls_status, drop_scn, working_sw_status, trans))) {
      LOG_WARN("failed to create new ls", KR(ret), K(i), K(tenant_id), K(ls_id),
          K(ls_status), K(drop_scn));
    }
  }
  return ret;

}

}
}

