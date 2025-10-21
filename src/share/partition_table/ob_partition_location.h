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

#ifndef OCEANBASE_SHARE_PARTITION_TABLE_OB_PARTITION_LOCATION_H_
#define OCEANBASE_SHARE_PARTITION_TABLE_OB_PARTITION_LOCATION_H_

#include "share/ob_define.h"
#include "lib/net/ob_addr.h"
#include "lib/container/ob_se_array.h"
#include "lib/ob_replica_define.h"
#include "common/ob_role.h"
#include "share/ls/ob_ls_info.h"

namespace oceanbase
{
namespace sql
{
class ObOptTabletLoc;
}
namespace share
{

struct ObPidAddrPair {
public:
  int64_t pid_;
  common::ObAddr addr_;

  ObPidAddrPair(int64_t pid, common::ObAddr addr)
    : pid_(pid), addr_(addr)
  {
  }

  bool operator==(const ObPidAddrPair &other) const
  {
    return pid_ == other.pid_ && addr_ == other.addr_;
  }

  TO_STRING_KV(K_(pid), K_(addr));
};

class ObPartitionReplicaLocation;
struct ObReplicaLocation //TODO(xiumin): delete it
{
  OB_UNIS_VERSION(1);
public:
  common::ObAddr server_;
  common::ObRole role_;
  int64_t sql_port_;
  int64_t reserved_;
  common::ObReplicaType replica_type_;
  common::ObReplicaProperty property_; // memstore_percent is used

  ObReplicaLocation();
  void reset();
  inline bool is_valid() const;
  inline bool operator==(const ObReplicaLocation &other) const;
  inline bool operator!=(const ObReplicaLocation &other) const;
  bool is_leader_like() const { return common::is_leader_like(role_); }
  bool is_leader_by_election() const { return common::is_leader_by_election(role_); }
  bool is_strong_leader() const { return common::is_strong_leader(role_); }
  bool is_standby_leader() const { return common::is_standby_leader(role_); }
  bool is_follower() const { return common::is_follower(role_); }
  TO_STRING_KV(K_(server), K_(role), K_(sql_port), K_(replica_type), K_(reserved), K_(property));
};

inline bool ObReplicaLocation::is_valid() const
{
  return server_.is_valid();
  //TODO:
  //return server_.is_valid() && common::ObReplicaTypeCheck::is_replica_type_valid(replica_type_);
}

bool ObReplicaLocation::operator==(const ObReplicaLocation &other) const
{
  return server_ == other.server_ && role_ == other.role_
      && sql_port_ == other.sql_port_
      && replica_type_ == other.replica_type_
      && property_ == other.property_;
}

bool ObReplicaLocation::operator!=(const ObReplicaLocation &other) const
{
  return !(*this == other);
}

class ObPartitionLocation
{
  OB_UNIS_VERSION(1);
  friend class ObPartitionReplicaLocation;
  friend class sql::ObOptTabletLoc;
public:
  typedef common::ObSEArray<ObReplicaLocation, common::OB_DEFAULT_MEMBER_NUMBER> ObReplicaLocationArray;

  ObPartitionLocation();
  explicit ObPartitionLocation(common::ObIAllocator &allocator);
  virtual ~ObPartitionLocation();

  void reset();
  int assign(const ObPartitionLocation &partition_location);

  bool is_valid() const;
  int add(const ObReplicaLocation &replica_location);
  int add_with_no_check(const ObReplicaLocation &replica_location);
  // return OB_LOCATION_LEADER_NOT_EXIST for leader not exist.
  int get_strong_leader(ObReplicaLocation &replica_location, int64_t &replica_idx) const;
  int get_strong_leader(ObReplicaLocation &replica_location) const;
  // return OB_LOCATION_LEADER_NOT_EXIST for leader not exist.

  int64_t size() const { return replica_locations_.count(); }

  inline uint64_t get_table_id() const { return table_id_; }
  inline void set_table_id(const uint64_t table_id) { table_id_ = table_id; }

  inline int64_t get_partition_id() const { return partition_id_; }
  inline void set_partition_id(const int64_t partition_id) { partition_id_ = partition_id; }

  inline int64_t get_partition_cnt() const { return partition_cnt_; }
  inline void set_partition_cnt(const int64_t partition_cnt) { partition_cnt_ = partition_cnt; }

  inline int64_t get_renew_time() const { return renew_time_; }
  inline void set_renew_time(const int64_t renew_time) { renew_time_ = renew_time; }

  inline int64_t get_sql_renew_time() const { return sql_renew_time_; }
  inline void set_sql_renew_time(const int64_t sql_renew_time) { sql_renew_time_ = sql_renew_time; }

  inline const common::ObIArray<ObReplicaLocation> &get_replica_locations() const { return replica_locations_; }
  inline void mark_fail() { is_mark_fail_ = true; }
  inline void unmark_fail() { is_mark_fail_ = false; }
  inline bool is_mark_fail() const { return is_mark_fail_; }

  TO_STRING_KV(KT_(table_id), K_(partition_id), K_(partition_cnt),
      K_(replica_locations), K_(renew_time), K_(sql_renew_time), K_(is_mark_fail));

private:
  // return OB_ENTRY_NOT_EXIST for not found.
  int find(const common::ObAddr &server, int64_t &idx) const;

private:
  uint64_t table_id_;
  int64_t partition_id_;
  int64_t partition_cnt_;
  ObReplicaLocationArray replica_locations_;
  int64_t renew_time_;     // renew time when location_cache is renewed successfully.
  int64_t sql_renew_time_; // renew time when location_cache is renewed successfully by SQL.
  bool is_mark_fail_;
};

class ObPartitionReplicaLocation final
{
  OB_UNIS_VERSION(1);
  friend class ObPartitionLocation;
public:
public:
  ObPartitionReplicaLocation();

  void reset();
  int assign(const ObPartitionReplicaLocation &partition_location);

  bool is_valid() const;
  bool operator==(const ObPartitionReplicaLocation &other) const;

  inline uint64_t get_table_id() const { return table_id_; }
  inline void set_table_id(const uint64_t table_id) { table_id_ = table_id; }

  inline int64_t get_partition_id() const { return partition_id_; }
  inline void set_partition_id(const int64_t partition_id) { partition_id_ = partition_id; }

  inline int64_t get_partition_cnt() const { return partition_cnt_; }
  inline void set_partition_cnt(const int64_t partition_cnt) { partition_cnt_ = partition_cnt; }

  inline int64_t get_renew_time() const { return renew_time_; }
  inline void set_renew_time(const int64_t renew_time) { renew_time_ = renew_time; }

  inline const ObReplicaLocation &get_replica_location() const { return replica_location_; }
  inline void set_replica_location(const ObReplicaLocation &replica_location) { replica_location_ = replica_location; }

  TO_STRING_KV(KT_(table_id), K_(partition_id), K_(partition_cnt),
               K_(replica_location), K_(renew_time));

private:
  uint64_t table_id_;
  int64_t partition_id_;
  int64_t partition_cnt_;
  ObReplicaLocation replica_location_;
  int64_t renew_time_;
};

}//end namespace share
}//end namespace oceanbase

#endif
