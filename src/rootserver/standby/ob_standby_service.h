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

#ifndef OCEANBASE_STANDBY_OB_STANDBY_SERVICE_H_
#define OCEANBASE_STANDBY_OB_STANDBY_SERVICE_H_

#include "share/ob_rpc_struct.h"                          // ObAdminClusterArg
#include "lib/mysqlclient/ob_isql_client.h"               // ObISQLClient
#include "rootserver/ob_ddl_service.h"                    // ObDDLService
#include "share/schema/ob_multi_version_schema_service.h" // ObMultiVersionSchemaService
#include "rootserver/standby/ob_tenant_role_transition_service.h" // ObTenantRoleTransitionService

// usage: TENANT_ROLE_TRANS_USER_ERR_WITH_SUFFIX(OB_OP_NOT_ALLOW, "tenant status is not normal")
// the output to user will be "tenant status is not normal, switchover to primary is not allowed"
#define TENANT_ROLE_TRANS_USER_ERR_WITH_SUFFIX(TRT_ERR_RET, TRT_ERR_MSG, TRT_OP) \
({ \
  int tmp_ret = OB_SUCCESS; \
  ObSqlString err_msg; \
  if (OB_TMP_FAIL(err_msg.append_fmt(TRT_ERR_MSG))) {  \
    LOG_WARN("fail to assign error message", KR(tmp_ret));  \
  } else { \
    if (obrpc::ObSwitchRoleArg::OpType::SWITCH_TO_PRIMARY == TRT_OP) {  \
      tmp_ret = err_msg.append_fmt(", switchover to primary is"); \
    } else if (obrpc::ObSwitchRoleArg::OpType::SWITCH_TO_STANDBY == TRT_OP) {  \
      tmp_ret = err_msg.append_fmt(", switchover to standby is"); \
    } else if (obrpc::ObSwitchRoleArg::OpType::FAILOVER_TO_PRIMARY == TRT_OP) { \
      tmp_ret = err_msg.append_fmt(", failover to primary is"); \
    } else { \
      tmp_ret = err_msg.append_fmt(", this operation is"); \
    } \
    if (OB_SUCCESS != tmp_ret) { \
      LOG_WARN("fail to assign error message", KR(tmp_ret)); \
    } else { \
      LOG_USER_ERROR(TRT_ERR_RET, err_msg.ptr()); \
    } \
  }  \
})
namespace oceanbase
{

using namespace share;
using namespace rootserver;
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
}
}

namespace standby
{

class ObStandbyService
{
public:
  ObStandbyService():
           sql_proxy_(NULL),
           schema_service_(NULL),
           inited_(false) {}
  virtual ~ObStandbyService() {}
  typedef obrpc::ObSwitchRoleArg::OpType RoleTransType;
  int init(ObMySQLProxy *sql_proxy,
           share::schema::ObMultiVersionSchemaService *schema_service);
  void destroy();

  /**
   * @description:
   *    switch tenant role
   * @param[in] arg
   * @return return code
   */
  int switch_role(const obrpc::ObSwitchRoleArg &arg);

private:
  int check_inner_stat_();

  /**
   * @description:
   *    failover standby tenant to primary tenant
   * @param[in] arg tenant switch arguments
   * @return return code
   */
  int failover_to_primary(
      const obrpc::ObSwitchRoleArg::OpType &switch_optype,
      const bool is_verify,
      const share::ObAllTenantInfo &tenant_info,
      share::SCN &switch_scn,
      ObTenantRoleTransCostDetail &cost_detail);

  /**
   * @description:
   *    switch standby tenant to primary tenant
   * @param[in] arg tenant switch arguments which include primary tenant switchover checkpoint
   * @return return code
   */
  int switch_to_primary(
      const obrpc::ObSwitchRoleArg::OpType &switch_optype,
      const bool is_verify,
      share::SCN &switch_scn,
      ObTenantRoleTransCostDetail &cost_detail);

  /**
   * @description:
   *    switch primary tenant to standby tenant
   * @return return code
   */
  int switch_to_standby(
      const obrpc::ObSwitchRoleArg::OpType &switch_optype,
      const bool is_verify,
      share::ObAllTenantInfo &tenant_info,
      share::SCN &switch_scn,
      ObTenantRoleTransCostDetail &cost_detail);

  /**
   * @description:
   *    when do switchover, update tenant status before call common failover_to_primary interface
   *    before update, check current tenant status doesn't change, otherwise report error
   *    1. update tenant status to <PHYSICAL STANDBY, PREP SWITCHING TO PRIMARY>
   *    3. update sync_snapshot, replay_snapshot, recovery_until_snapshot to max{ all ls max_log_ts in check_point }
   * @param[in] cur_switchover_status
   * @param[in] cur_tenant_role
   * @param[out] new_tenant_info  after update done, return new_tenant_info get in the same trans
   * @return return code
   */
  int sw_update_tenant_status_before_switch_to_primary_(
      const ObTenantSwitchoverStatus cur_switchover_status,
      const ObTenantRole cur_tenant_role,
      ObAllTenantInfo &new_tenant_info);

  /**
   * @description:
   *    update tenant to <PHYSICAL STANDBY, SWITCHING TO STANDBY>
   *    before update, check current tenant status doesn't change, otherwise report error
   *    after update done, return new_tenant_info get in the same trans
   * @param[in] cur_switchover_status
   * @param[in] cur_tenant_role
   * @param[out] new_tenant_info  after update done, return new_tenant_info get in the same trans
   * @return return code
   */
  int update_tenant_status_before_sw_to_standby_(
      const ObTenantSwitchoverStatus cur_switchover_status,
      const ObTenantRole cur_tenant_role,
      ObAllTenantInfo &new_tenant_info);

  /**
   * @description:
   *    when switch to standby, prepare ls_status in all_ls and all_ls_status to proper status
   * @param[in] status only prepare in specified switchover status
   * @param[out] new_tenant_info return the updated tenant_info
   * @return return code
   */
  int switch_to_standby_prepare_ls_status_(
      const ObTenantSwitchoverStatus &status,
      ObAllTenantInfo &new_tenant_info);

  int check_if_tenant_status_is_normal_(const RoleTransType op_type);
  void tenant_event_start_(const obrpc::ObSwitchRoleArg &arg,
      int ret, int64_t begin_ts, const share::ObAllTenantInfo &tenant_info);
  void tenant_event_end_(const obrpc::ObSwitchRoleArg &arg,
      int ret, int64_t cost, int64_t end_ts, const share::SCN switch_scn,
      ObTenantRoleTransCostDetail &cost_detail);
private:
  const static int64_t SEC_UNIT = 1000L * 1000L;
  const static int64_t PRINT_INTERVAL = 10 * 1000 * 1000L;

  ObMySQLProxy *sql_proxy_;
  share::schema::ObMultiVersionSchemaService *schema_service_;
  bool inited_;
};

class ObStandbyServiceGetter
{
public:
  static ObStandbyService &get_instance()
  {
    static ObStandbyService standby_service;
    return standby_service;
  }
};

#define OB_STANDBY_SERVICE (oceanbase::standby::ObStandbyServiceGetter::get_instance())

}  // end namespace standby
}  // end namespace oceanbase

#endif  // OCEANBASE_STANDBY_OB_STANDBY_SERVICE_H_
