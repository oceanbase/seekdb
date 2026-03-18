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

#ifndef OCEANBASE_ROOTSERVER_OB_TENANT_ROLE_TRANSITION_SERVICE_H
#define OCEANBASE_ROOTSERVER_OB_TENANT_ROLE_TRANSITION_SERVICE_H

#include "share/ob_rpc_struct.h"// ObSwitchRoleArg
#include "logservice/palf/palf_options.h"//access mode
#include "logservice/palf/log_define.h"//INVALID_PROPOSAL_ID
#include "share/ob_tenant_switchover_status.h" // ObTenantSwitchoverStatus
#include "share/ob_ls_id.h" // ObLSID
#include "share/rc/ob_tenant_base.h" // MTL_SWITCH
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ls/ob_ls.h" // ObLS

namespace oceanbase
{
namespace obrpc
{
struct ObLSAccessModeInfo;
}
namespace share
{
class SCN;
struct ObAllTenantInfo;
class ObAllTenantInfoProxy;

// Simplified LS Status enum for single LS scenario
enum ObLSStatus
{
  OB_LS_EMPTY = -1,
  OB_LS_CREATING = 0,
  OB_LS_CREATED,
  OB_LS_NORMAL,
  OB_LS_DROPPING,
  OB_LS_TENANT_DROPPING,
  OB_LS_WAIT_OFFLINE,
  OB_LS_CREATE_ABORT,
  OB_LS_PRE_TENANT_DROPPING,//only for sys ls
  OB_LS_DROPPED,//for __all_ls
  OB_LS_MAX_STATUS,
};

// Simplified LS Status Info for single LS scenario
struct ObLSStatusInfo
{
  ObLSStatusInfo() :
                      ls_id_(), status_(OB_LS_EMPTY) {}
  virtual ~ObLSStatusInfo() {}
  bool is_valid() const { return ls_id_.is_valid(); }
  ObLSStatus get_status() const { return status_; }
  ObLSID get_ls_id() const { return ls_id_; }
  bool ls_is_normal() const { return OB_LS_NORMAL == status_; }

  ObLSID ls_id_;
  ObLSStatus status_;

  TO_STRING_KV(K_(ls_id), "status", status_);
};

// Simplified LS Recovery Stat for single LS scenario
struct ObLSRecoveryStat
{
  ObLSRecoveryStat() : ls_id_(),
                       sync_scn_(), readable_scn_(), create_scn_() {}
  ~ObLSRecoveryStat() {}

  share::SCN get_sync_scn() const { return sync_scn_; }
  share::SCN get_create_scn() const { return create_scn_; }

  share::ObLSID ls_id_;
  share::SCN sync_scn_;
  share::SCN readable_scn_;
  share::SCN create_scn_;

  TO_STRING_KV(K_(ls_id), K_(sync_scn), K_(readable_scn), K_(create_scn));
};

extern int get_ls_recovery_stat(ObLSRecoveryStat &ls_recovery_stat);

}

namespace rootserver
{

using namespace share;


class ObTenantRoleTransitionConstants
{
public:
  static constexpr int64_t PRIMARY_UPDATE_LS_RECOVERY_STAT_TIME_US = 1000 * 1000;  // 1s
  static constexpr int64_t STANDBY_UPDATE_LS_RECOVERY_STAT_TIME_US = 100 * 1000;  // 100ms
  static constexpr int64_t STS_TENANT_INFO_REFRESH_TIME_US = 100 * 1000;  // 100ms
  static constexpr int64_t DEFAULT_TENANT_INFO_REFRESH_TIME_US = 1000 * 1000;  // 1s
  static constexpr int64_t TENANT_INFO_LEASE_TIME_US = 2 * DEFAULT_TENANT_INFO_REFRESH_TIME_US;  // 2s
  static const char* const SWITCH_TO_PRIMARY_LOG_MOD_STR;
  static const char* const SWITCH_TO_STANDBY_LOG_MOD_STR;
  static const char* const RESTORE_TO_STANDBY_LOG_MOD_STR;
};

struct ObTenantRoleTransCostDetail
{
public:
  enum CostType {
    WAIT_LOG_SYNC = 0,
    LOG_FLASHBACK = 1, // Reserved but not used - flashback not supported
    WAIT_LOG_END = 2,
    CHANGE_ACCESS_MODE = 3,
    MAX_COST_TYPE = 4
  };
  const char* type_to_str(CostType type) const;
public:
  ObTenantRoleTransCostDetail() : cost_type_{}, start_(0), end_(0) {}
  ~ObTenantRoleTransCostDetail() {}
  void set_start(int64_t start) { start_ = start; }
  void add_cost(CostType type, int64_t cost);
  void set_end(int64_t end) { end_ = end; }
  int64_t get_wait_log_end () { return cost_type_[WAIT_LOG_END]; }
  int64_t to_string (char *buf, const int64_t buf_len) const ;
private:
  int64_t cost_type_[MAX_COST_TYPE];
  int64_t start_;
  int64_t end_;
};

/*description:
 * for primary to standby and standby to primary
 */
class ObTenantRoleTransitionService
{
public:
  ObTenantRoleTransitionService()
    :
    switch_optype_(obrpc::ObSwitchRoleArg::OpType::INVALID),
    so_scn_(),
    cost_detail_(NULL),
    has_restore_source_(false),
    is_verify_(false) {}
  virtual ~ObTenantRoleTransitionService() {}
  int init(
      const obrpc::ObSwitchRoleArg::OpType &switch_optype,
      const bool is_verify,
      ObTenantRoleTransCostDetail *cost_detail);
  int failover_to_primary();
  int check_inner_stat();
  int do_switch_access_mode_to_append(const share::ObAllTenantInfo &tenant_info,
                             const share::ObTenantRole &target_tenant_role);
  int do_switch_access_mode_to_raw_rw(const share::ObAllTenantInfo &tenant_info);
  int get_tenant_ref_scn_(share::SCN &ref_scn);
  //before primary tenant switchover to standby, must set sys LS's sync_scn to lastest
  int report_sys_ls_sync_scn_();
  /**
   * @description:
   *    Update scn/tenant_role/switchover status when switchover is executed
   *    scn is the current sync point obtained through rpc
   * @param[in] switch_to_primary switch_to_primary or switch_to_standby
   * @param[in] new_role new tenant role
   * @param[in] old_status current switchover status
   * @param[in] new_status new switchover status
   * @param[out] new_tenant_info return the updated tenant_info
   * @return return code
   */
  int switchover_update_tenant_status(
      const bool switch_to_primary,
      const ObTenantRole &new_role,
      const ObTenantSwitchoverStatus &old_status,
      const ObTenantSwitchoverStatus &new_status,
      ObAllTenantInfo &new_tenant_info);

  int wait_sys_ls_sync_to_latest_until_timeout_(const ObAllTenantInfo &tenant_info);
  int check_restore_source_for_switchover_to_primary_();

  /**
   * @description:
   *    do the checking to see whether the standby tenant ls has synchronize to primary tenant checkpoints
   * @param[in] primary_checkpoints primary switchover checkpoint
   * @param[out] has_sync_to_checkpoint whether the standby tenant sync to primary tenant checkpoints
   * @return return code
   */
  int check_sync_to_restore_source_(
                                    const ObAllTenantInfo &tenant_info,
                                    bool &has_sync_to_checkpoint);
  /**
   * @description:
   *    get specified ls list sync_scn by rpc, which is named as checkpoint
   * @param[in] status_info_array ls list to get sync scn
   * @param[in] get_latest_scn whether to get latest scn
   * @param[out] checkpoints switchover checkpoint
   * @return return code
   */
  static int get_checkpoint(
      const share::ObLSStatusInfo &status_info,
      const bool get_latest_scn,
      obrpc::ObCheckpoint &checkpoint
  );
  share::SCN get_so_scn() const { return so_scn_; }

private:
  int do_failover_to_primary_(share::ObAllTenantInfo &tenant_info);
  int do_prepare_flashback_(share::ObAllTenantInfo &tenant_info);
  int do_prepare_flashback_for_switch_to_primary_(share::ObAllTenantInfo &tenant_info);
  int do_prepare_flashback_for_failover_to_primary_(share::ObAllTenantInfo &tenant_info);
  int do_flashback_();
  int get_status_and_change_ls_access_mode_(
      palf::AccessMode target_access_mode,
      const SCN &ref_scn);
  int change_ls_access_mode_(
      const share::ObLSStatusInfo &status_info,
      const palf::AccessMode target_access_mode,
      const share::SCN &ref_scn,
      const share::SCN &sys_ls_sync_scn);
  int get_ls_access_mode_(
      const share::ObLSStatusInfo &status_info,
      obrpc::ObLSAccessModeInfo &ls_access_info);
  int do_change_ls_access_mode_(const obrpc::ObLSAccessModeInfo &ls_access_info,
                                const palf::AccessMode target_access_mode,
                                const share::SCN &ref_scn,
                                const share::SCN &sys_ls_sync_scn);

  /**
   * @description:
   *    get sys ls sync_snapshot from checkpoints array
   * @param[in] checkpoints checkpoint
   * @return return code
   */
  int get_sys_ls_sync_scn_(
    const bool need_check_sync_to_latest,
    share::SCN &sys_ls_sync_scn,
    bool &is_sync_to_latest);
  int get_sys_ls_sync_scn_(
      obrpc::ObCheckpoint &checkpoints,
      share::SCN &sys_ls_sync_scn,
      bool &is_sync_to_latest);
  /**
   * @description:
   *    wait tenant/sys ls sync to switchover checkpoint until timeout
   * @param[in] only_check_sys_ls true: only wait sys ls sync; false: wait tenant sync
   * @return return code
   */
  int check_sync_to_latest_do_while_(
    const ObAllTenantInfo &tenant_info);
  int check_sync_to_latest_(
        const ObAllTenantInfo &tenant_info,
        bool &is_sys_ls_synced);

  /**
   * @description:
   *    when switch to primary, check all ls are sync to latest
   * @param[in] tenant_info
   * @param[out] is_all_ls_synced whether sync to latest
   * @return return code
   */

  // Removed clear_service_name_ and double_check_service_name_ for single tenant/single LS scenario
  int check_and_update_sys_ls_recovery_stat_in_switchover_(
      const bool switch_to_primary,
      const SCN &max_sys_ls_sync_scn/* SYS LS real max sync scn */,
      const SCN &target_tenant_sync_scn/* tenant target sync scn in switchover */);
  int get_all_ls_status_and_change_access_mode_(
      const palf::AccessMode target_access_mode,
      const share::SCN &ref_scn,
      const share::SCN &sys_ls_sync_scn);
  int ls_status_stats_when_change_access_mode_(const share::ObLSStatusInfo &status_info);

private:
  const static int64_t SEC_UNIT = 1000L * 1000L;
  const static int64_t PRINT_INTERVAL = 1000L * 1000L;

private:
  obrpc::ObSwitchRoleArg::OpType switch_optype_;
  share::SCN so_scn_;
  ObTenantRoleTransCostDetail *cost_detail_;
  bool has_restore_source_;
  bool is_verify_;
};
}
}


#endif /* !OCEANBASE_ROOTSERVER_OB_TENANT_ROLE_TRANSITION_SERVICE_H */
