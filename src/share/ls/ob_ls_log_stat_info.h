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

#ifndef OCEANBASE_SHARE_OB_LS_LOG_STAT_INFO_H_
#define OCEANBASE_SHARE_OB_LS_LOG_STAT_INFO_H_

#include "share/ob_ls_id.h"      // ObLSID
#include "lib/net/ob_addr.h"     // ObAddr
#include "common/ob_role.h"      // ObRole
#include "share/ls/ob_ls_info.h" // MemberList
#include "share/scn.h" // SCN

namespace oceanbase
{
namespace share
{
const int64_t LOG_IN_SYNC_INTERVAL_NS = 5 * 1000 * 1000 * 1000L;  // 5s
// ObLSLogStatReplica is part of ObLSLogStatInfo.
// It records replica info of ls from __all_virtual_log_stat.
class ObLSLogStatReplica
{
public:
  ObLSLogStatReplica();
  virtual ~ObLSLogStatReplica() {}
  void reset();
  bool is_valid() const;
  int init(
      const uint64_t tenant_id,
      const ObLSID &ls_id,
      const common::ObAddr &server,
      const ObRole &role,
      const int64_t proposal_id,
      const ObLSReplica::MemberList &member_list,
      const int64_t paxos_replica_num,
      const int64_t end_scn);
  int assign(const ObLSLogStatReplica &other);
  bool is_leader() const { return LEADER == role_; }

  uint64_t get_tenant_id() const { return tenant_id_; }
  const ObLSID &get_ls_id() const { return ls_id_; }
  const common::ObAddr &get_server() const { return server_; }
  const ObRole &get_role() const { return role_; }
  int64_t get_proposal_id() const { return proposal_id_; }
  const ObLSReplica::MemberList &get_member_list() const { return member_list_; }
  int64_t get_paxos_replica_num() const { return paxos_replica_num_; }
  int64_t get_end_scn() const { return end_scn_; }

  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(server), K_(role),
      K_(proposal_id), K_(member_list), K_(paxos_replica_num), K_(end_scn));
private:
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  common::ObAddr server_;
  common::ObRole role_;
  int64_t proposal_id_;
  ObLSReplica::MemberList member_list_;
  int64_t paxos_replica_num_;
  int64_t end_scn_; // used to check log in sync
};

typedef common::ObSEArray<ObLSLogStatReplica, OB_DEFAULT_REPLICA_NUM> ObLSLogStatReplicaArray;
// ObLSLogStatInfo records ls info from __all_virtual_log_stat
class ObLSLogStatInfo
{
public:
  ObLSLogStatInfo();
  virtual ~ObLSLogStatInfo() {}
  void reset();
  int init(const uint64_t tenant_id, const ObLSID &ls_id);
  bool is_valid() const;
  // check if a replica is belong to itself
  bool is_self_replica(const ObLSLogStatReplica &replica) const;
  // check if a replica exist in replicas_
  bool has_replica(const ObLSLogStatReplica &replica) const;
  int add_replica(const ObLSLogStatReplica &replica);
  bool empty() const { return replicas_.empty(); }
  // check if ls enough member filter by valid_servers
  //
  // @param [in] valid_servers: valid servers used to filter replica
  // @param [in] arb_replica_num: the number of arb replica
  // @param [out] has: if ls has enough members to satisfy majority
  // @return: OB_LEADER_NOT_EXIST if no leader
  int check_has_majority(
      const common::ObIArray<ObAddr> &valid_servers,
      const int64_t arb_replica_num,
      bool &has) const;
  // check if ls' majority is log sync
  //
  // @param [in] valid_servers: valid servers used to filter replica
  // @param [in] arb_replica_num: the number of arb replica
  // @param [out] is_log_sync: if majority's log is in sync
  // @return: OB_LEADER_NOT_EXIST if no leader
  int check_log_sync(
      const common::ObIArray<ObAddr> &valid_servers,
      const int64_t arb_replica_number,
       bool &is_log_sync) const;

  uint64_t get_tenant_id() const { return tenant_id_; }
  const ObLSID &get_ls_id() const { return ls_id_; }
  // get leader replica from ObLSLogStatInfo
  //
  // @param [out] leader: leader's ObLSLogStatReplica
  // @return: OB_LEADER_NOT_EXIST if no leader
  //          OB_INVALID_ARGUMENT if this ls_log_stat_info is invalid
  int get_leader_replica(ObLSLogStatReplica &leader) const;

  // get replicas
  const ObLSLogStatReplicaArray &get_replicas() const { return replicas_; }
  TO_STRING_KV(K_(tenant_id), K_(ls_id), K_(replicas));
private:
  // if no leader, return OB_INVALID_INDEX
  // if there are multiple leaders, return the one whose proposal_id is bigger
  int64_t get_leader_index_() const;

  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  ObLSLogStatReplicaArray replicas_;
};

} // end namespace share
} // end namespace oceanbase
#endif /* OCEANBASE_SHARE_OB_LS_LOG_STAT_INFO_H_ */
