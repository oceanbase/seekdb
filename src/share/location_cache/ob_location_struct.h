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

#ifndef OCEANBASE_SHARE_OB_LS_LOCATION
#define OCEANBASE_SHARE_OB_LS_LOCATION


#include "lib/ob_replica_define.h"
#include "common/ob_role.h"
#include "share/ob_define.h"
#include "share/ob_share_util.h" // for ObShareUtil
#include "share/ls/ob_ls_restore_status.h"
#include "lib/lock/ob_thread_cond.h"

namespace oceanbase
{
namespace common
{
class ObAddr;
class ObReplicaProperty;
}
namespace share
{
class ObLSReplicaLocation
{
  OB_UNIS_VERSION(1);
public:
  ObLSReplicaLocation();
  virtual ~ObLSReplicaLocation() {}
  void reset();
  bool is_valid() const;
  bool operator==(const ObLSReplicaLocation &other) const;
  bool operator!=(const ObLSReplicaLocation &other) const;
  inline const common::ObAddr &get_server() const { return server_; }
  inline void set_server(const common::ObAddr &addr) { server_ = addr; }
  inline const common::ObRole &get_role() const { return role_; }
  inline int64_t get_sql_port() const { return sql_port_; }
  inline void set_sql_port(const int64_t &sql_port) { sql_port_ = sql_port; }
  inline void set_proposal_id(const int64_t proposal_id) { proposal_id_ = proposal_id; }
  inline const common::ObReplicaType &get_replica_type() const { return replica_type_; }
  inline void set_replica_type(const common::ObReplicaType &type) { replica_type_ = type; }
  inline const common::ObReplicaProperty &get_property() const { return property_; }
  inline const ObLSRestoreStatus &get_restore_status() const { return restore_status_; }
  inline int64_t get_proposal_id() const { return proposal_id_; }
  bool is_strong_leader() const { return common::is_strong_leader(role_); }
  bool is_follower() const { return common::is_follower(role_); }
  int assign(const ObLSReplicaLocation &other);
  int init(
      const common::ObAddr &server,
      const common::ObRole &role,
      const int64_t &sql_port,
      const common::ObReplicaType &replica_type,
      const common::ObReplicaProperty &property,
      const ObLSRestoreStatus &restore_status,
      const int64_t proposal_id);
  // make fake location for vtable
  int init_without_check(
      const common::ObAddr &server,
      const common::ObRole &role,
      const int64_t &sql_port,
      const common::ObReplicaType &replica_type,
      const common::ObReplicaProperty &property,
      const ObLSRestoreStatus &restore_status,
      const int64_t proposal_id);
  // set role for tenant_server in __all_virtual_proxy_schema
  void set_role(const common::ObRole &role) { role_ = role; }
  TO_STRING_KV(
      K_(server),
      K_(role),
      K_(sql_port),
      "replica_type",
      ObShareUtil::replica_type_to_string(replica_type_),
      K_(property),
      K_(restore_status),
      K_(proposal_id));
protected:
  common::ObAddr server_;
  common::ObRole role_;
  int64_t sql_port_;
  common::ObReplicaType replica_type_;
  common::ObReplicaProperty property_; // memstore_percent is used
  ObLSRestoreStatus restore_status_;
  int64_t proposal_id_; // only leader's proposal_id_ is useful
};

class ObLocationSem
{
public:
  ObLocationSem();
  ~ObLocationSem();
  void set_max_count(const int64_t max_count);
  int acquire(const int64_t abs_timeout_us);
  int release();
private:
  int64_t cur_count_;
  int64_t max_count_;
  common::ObThreadCond cond_;
};

struct ObLSExistState final
{
public:
  enum State
  {
    INVALID_STATE = -1,
    UNCREATED,
    DELETED,
    EXISTING,
    MAX_STATE
  };
  ObLSExistState() : state_(INVALID_STATE) {}
  ObLSExistState(State state) : state_(state) {}
  ~ObLSExistState() {}
  void reset() { state_ = INVALID_STATE; }
  void set_existing() { state_ = EXISTING; }
  void set_deleted() { state_ = DELETED; }
  void set_uncreated() { state_ = UNCREATED; }
  bool is_valid() const { return state_ > INVALID_STATE && state_ < MAX_STATE; }
  bool is_existing() const { return EXISTING == state_; }
  bool is_deleted() const { return DELETED == state_; }
  bool is_uncreated() const { return UNCREATED == state_; }

  TO_STRING_KV(K_(state));
private:
  State state_;
};

} // end namespace share
} // end namespace oceanbase
#endif
