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

#ifndef LOGSERVICE_COORDINATOR_TABLE_ACCESSOR_H
#define LOGSERVICE_COORDINATOR_TABLE_ACCESSOR_H
#include "lib/container/ob_iarray.h"
#include "lib/list/ob_dlist.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/net/ob_addr.h"
#include "lib/ob_errno.h"
#include "lib/string/ob_string_holder.h"
#include "share/ob_errno.h"
#include "share/ob_ls_id.h"
#include "share/ob_table_access_helper.h"
#include "common_define.h"
#include <cstring>
#include "ob_leader_coordinator.h"
namespace oceanbase
{
namespace unittest
{
class TestElectionPriority;
}
namespace logservice
{
namespace coordinator
{

enum class InsertElectionBlacklistReason
{
  SWITCH_REPLICA = 1,
  MIGRATE = 2,
};

inline const char *to_cstring(InsertElectionBlacklistReason reason)
{
  switch (reason) {
  case InsertElectionBlacklistReason::SWITCH_REPLICA:
    return "SWITCH REPLICA";
  case InsertElectionBlacklistReason::MIGRATE:
    return "MIGRATE";
  default:
    return "unknown reason";
  }
}

class LsElectionReferenceInfoRow
{
  friend class unittest::TestElectionPriority;
  typedef ObTuple<int64_t/*tenant_id*/,
                int64_t/*ls_id*/,
                ObStringHolder/*zone_priority*/,
                ObStringHolder/*manual_leader_server*/,
                ObStringHolder/*remove_member_info*/> LsElectionReferenceInfoRowTypeForTable;
  typedef ObTuple<uint64_t/*tenant_id*/,
                  share::ObLSID/*ls_id*/,
                  ObArray<ObArray<ObStringHolder>>/*zone_priority*/,
                  common::ObAddr/*manual_leader_server*/,
                  ObArray<ObTuple<ObAddr/*removed_server*/,
                                  ObStringHolder/*removed_reason*/>>/*remove_member_info*/> LsElectionReferenceInfoRowTypeForUser;
public:
  /**
   * @description: Create a data structure mapping to a row in __all_ls_election_reference_info, determined by the tenant ID and log stream ID, through which the content of the corresponding row in __all_ls_election_reference_info can be modified via the interface of this data structure
   * @param {uint64_t} tenant_id tenant ID
   * @param {ObLSID &} ls_id log stream ID
   * @Date: 2022-01-29 16:45:33
   */
  LsElectionReferenceInfoRow(const uint64_t tenant_id, const share::ObLSID &ls_id);
  ~LsElectionReferenceInfoRow();
  /**
   * @description: Modify the content of the zone_priority column only when the corresponding row exists in the __all_ls_election_reference_info table
   * @param {ObArray<ObArray<ObStringHolder>>} &zone_list_list Election reference zone priorities, represented as a two-dimensional array, e.g., {{z1,z2},{z3},{z4,z5}}: z1 and z2 have the highest priority, z3 is next, and z4 and z5 are the lowest
   * @return {int} Error code
   * @Date: 2022-01-29 16:41:02
   */
  int change_zone_priority(const ObArray<ObArray<ObStringHolder>> &zone_list_list);
  /**
   * @description: Modify the content of the manual_leader_server column only when the corresponding row exists in the __all_ls_election_reference_info table
   * @param {ObAddr} &manual_leader_server Specified server address
   * @return {int} Error code
   * @Date: 2022-01-29 16:41:53
   */
  int change_manual_leader(const common::ObAddr &manual_leader_server);
  /**
   * @description: Add the server to be removed and the reason to the removed_member_info column only if the corresponding row exists in the __all_ls_election_reference_info table, and the server to be removed does not exist in the removed_member_info.
   * @param {ObAddr} &server Server that is not allowed to be leader
   * @param {ObString} &reason Reason why the server is not allowed to be leader
   * @return {*}
   * @Date: 2022-01-29 16:43:19
   */
  int add_server_to_blacklist(const common::ObAddr &server, InsertElectionBlacklistReason reason);
  /**
   * @description: Remove the deleted server from the removed_member_info column only if the corresponding row exists in the __all_ls_election_reference_info table and the server to be removed exists in removed_member_info.
   * @param {ObAddr} &server The server that has been recorded as not allowed to be a leader.
   * @return {*}
   * @Date: 2022-01-29 16:44:51
   */
  int delete_server_from_blacklist(const common::ObAddr &server);
  /**
   * @description: Set the election blacklist for this reason to the corresponding server, which will clear the server of this reason from the existing election blacklist
   * @param {ObAddr} &server Server that is not allowed to be leader
   * @param {ObString} &reason Reason why the server is not allowed to be leader
   * @return {*}
   * @Date: 2022-01-29 16:43:19
   */
  int set_or_replace_server_in_blacklist(const common::ObAddr &server, InsertElectionBlacklistReason reason);
  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(exec_tenant_id), K_(row_for_table), K_(row_for_user));
private:
  int begin_();
  int end_(const bool true_to_commit);
  int convert_table_info_to_user_info_();
  int convert_user_info_to_table_info_();
  int update_row_to_table_();
  int get_row_from_table_();
  int start_and_read_();
  int write_and_commit_();
  int schedule_refresh_priority_task_();
  int set_user_row_for_specific_reason_(const common::ObAddr &server, InsertElectionBlacklistReason reason);
private:
  const uint64_t tenant_id_;
  const share::ObLSID ls_id_;
  uint64_t exec_tenant_id_;// tenant_id used by the transaction when performing read and write operations
  ObMySQLTransaction trans_;
  LsElectionReferenceInfoRowTypeForTable row_for_table_;// Data organization form friendly to internal tables
  LsElectionReferenceInfoRowTypeForUser row_for_user_;// Data organization form friendly to users
};

class TableAccessor
{
  struct ServerZoneNameCache// zone name retrieval is successful once, avoid accessing the internal table every time
  {
    void set_zone_name_to_global_cache(const ObStringHolder &zone_name) {
      ObSpinLockGuard lg(lock_);
      if (!server_zone_name_.empty()) {
        server_zone_name_.assign(zone_name);
      }
    }
    int get_zone_name_from_global_cache(ObStringHolder &zone_name) {
      int ret = OB_SUCCESS;
      ObSpinLockGuard lg(lock_);
      if (server_zone_name_.empty()) {
        ret = OB_CACHE_INVALID;
      } else if (OB_FAIL(zone_name.assign(server_zone_name_))) {
      }
      return ret;
    }
  private:
    ObSpinLock lock_;
    ObStringHolder server_zone_name_;
  };
  static ServerZoneNameCache SERVER_ZONE_NAME_CACHE;
public:
  static int get_all_ls_election_reference_info(common::ObIArray<LsElectionReferenceInfo> &all_ls_election_reference_info);
  static int get_self_zone_name(ObStringHolder &zone_name_holder);
  static int get_self_zone_region(const ObStringHolder &zone_name_holder, ObStringHolder &region_name_holder);
  static int is_primary_region(const ObStringHolder &region_name_holder, bool &is_primary_region);
  static int calculate_zone_priority_score(ObStringHolder &zone_priority, ObStringHolder &self_zone_name, int64_t &score);
  static int get_removed_status_and_reason(ObStringHolder &remove_member_info, bool &status, ObStringHolder &reason);
  static int get_zone_stop_status(ObStringHolder &zone_name, bool &is_zone_stopped);
  static int get_server_stop_status(bool &is_server_stopped);
};

}
}
}
#endif
