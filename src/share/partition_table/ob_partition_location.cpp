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

#define USING_LOG_PREFIX SHARE_PT

#include "ob_partition_location.h"


namespace oceanbase
{
using namespace common;
namespace share
{

OB_SERIALIZE_MEMBER(ObPartitionLocation,
                    table_id_,
                    partition_id_,
                    partition_cnt_,
                    replica_locations_,
                    renew_time_,
                    is_mark_fail_);

OB_SERIALIZE_MEMBER(ObReplicaLocation,
                    server_,
                    role_,
                    sql_port_,
                    reserved_,
                    replica_type_,
                    property_);

OB_SERIALIZE_MEMBER(ObPartitionReplicaLocation,
                    table_id_,
                    partition_id_,
                    partition_cnt_,
                    replica_location_,
                    renew_time_);

ObReplicaLocation::ObReplicaLocation()
{
  reset();
}

void ObReplicaLocation::reset()
{
  server_.reset();
  role_ = FOLLOWER;
  sql_port_ = OB_INVALID_INDEX;
  reserved_ = 0;
  replica_type_ = REPLICA_TYPE_FULL;
  property_.reset();
}

ObPartitionLocation::ObPartitionLocation()
    :replica_locations_(ObModIds::OB_MS_LOCATION_CACHE, OB_MALLOC_NORMAL_BLOCK_SIZE)
{
  reset();
}

ObPartitionLocation::ObPartitionLocation(common::ObIAllocator &allocator)
    :replica_locations_(common::OB_MALLOC_NORMAL_BLOCK_SIZE,
                        common::ModulePageAllocator(allocator))
{
  reset();
}

ObPartitionLocation::~ObPartitionLocation()
{
  reset();
}

void ObPartitionLocation::reset()
{
  table_id_ = OB_INVALID_ID;
  partition_id_ = OB_INVALID_INDEX;
  partition_cnt_ = 0;
  replica_locations_.reset();
  renew_time_ = 0;
  sql_renew_time_ = 0;
  is_mark_fail_ = false;
}

int ObPartitionLocation::assign(const ObPartitionLocation &other)
{
  int ret = OB_SUCCESS;

  if (this != &other) {
    table_id_ = other.table_id_;
    partition_id_ = other.partition_id_;
    partition_cnt_ = other.partition_cnt_;
    renew_time_ = other.renew_time_;
    sql_renew_time_ = other.sql_renew_time_;
    is_mark_fail_ = other.is_mark_fail_;
    if (OB_FAIL(replica_locations_.assign(other.replica_locations_))) {
      LOG_WARN("Failed to assign replica locations", K(ret));
    }
  }

  return ret;
}



bool ObPartitionLocation::is_valid() const
{
  return OB_INVALID_ID != table_id_ && OB_INVALID_INDEX != partition_id_
      && renew_time_ >= 0 && sql_renew_time_ >= 0;
}


int ObPartitionLocation::add(const ObReplicaLocation &replica_location)
{
  int ret = OB_SUCCESS;
  // Replica location may be added before table_id/partition_id set,
  // can not check self validity here.
  if (!replica_location.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid replica location", K(replica_location), K(ret));
  } else if (OB_FAIL(add_with_no_check(replica_location))) {
    LOG_WARN("fail to do add replica location", K(replica_location), K(ret));
  }
  return ret;
}

int ObPartitionLocation::add_with_no_check(const ObReplicaLocation &replica_location)
{
  int ret = OB_SUCCESS;
  int64_t idx = OB_INVALID_INDEX;
  if (OB_LIKELY(OB_SUCCESS != (ret = (find(replica_location.server_, idx))))) {
    if (OB_LIKELY(OB_ENTRY_NOT_EXIST == ret)) {
      if (OB_FAIL(replica_locations_.push_back(replica_location))) {
        LOG_WARN("push back replica location failed", K(ret));
      }
    } else {
      LOG_WARN("find server location failed", K(ret), "server", replica_location.server_);
    }
  } else {
    ret = OB_ERR_ALREADY_EXISTS;
    LOG_WARN("replica location already exist, can not add", K(replica_location), K(ret));
  }
  return ret;
}



int ObPartitionLocation::get_strong_leader(ObReplicaLocation &replica_location, int64_t &replica_idx) const
{
  int ret = OB_LOCATION_LEADER_NOT_EXIST;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "self", *this);
  } else {
    ARRAY_FOREACH_X(replica_locations_, i, cnt, OB_LOCATION_LEADER_NOT_EXIST == ret) {
      if (replica_locations_.at(i).is_strong_leader()) {
        replica_location = replica_locations_.at(i);
        replica_idx = i;
        ret = OB_SUCCESS;
      }
    }
  }
  return ret;
}

int ObPartitionLocation::get_strong_leader(ObReplicaLocation &replica_location) const
{
  int64_t replica_idx = OB_INVALID_INDEX;
  return get_strong_leader(replica_location, replica_idx);
}



int ObPartitionLocation::find(const ObAddr &server, int64_t &idx) const
{
  int ret = OB_ENTRY_NOT_EXIST;
  idx = OB_INVALID_INDEX;
  // May be used before table_id/partition_id set, can not check self validity here.
  // server(ObAddr) is checked by caller, no need check again.
  for (int64_t i = 0; OB_ENTRY_NOT_EXIST == ret && i < replica_locations_.count(); ++i) {
    if (replica_locations_.at(i).server_ == server) {
      idx = i;
      ret = OB_SUCCESS;
    }
  }
  return ret;
}




ObPartitionReplicaLocation::ObPartitionReplicaLocation()
{
  reset();
}



void ObPartitionReplicaLocation::reset()
{
  table_id_ = OB_INVALID_ID;
  partition_id_ = OB_INVALID_INDEX;
  partition_cnt_ = 0;
  replica_location_.reset();
  renew_time_ = 0;
}

int ObPartitionReplicaLocation::assign(const ObPartitionReplicaLocation &other)
{
  int ret = OB_SUCCESS;
  table_id_ = other.table_id_;
  partition_id_ = other.partition_id_;
  partition_cnt_ = other.partition_cnt_;
  replica_location_ = other.replica_location_;
  renew_time_ = other.renew_time_;
  return ret;
}



bool ObPartitionReplicaLocation::is_valid() const
{
  return OB_INVALID_ID != table_id_ && OB_INVALID_INDEX != partition_id_ && renew_time_ >= 0;
}

bool ObPartitionReplicaLocation::operator==(const ObPartitionReplicaLocation &other) const
{
  bool equal = true;
  if (!is_valid() || !other.is_valid()) {
    equal = false;
    LOG_WARN_RET(common::OB_INVALID_ARGUMENT, "invalid argument", "self", *this, K(other));
  } else if (replica_location_ != other.replica_location_) {
    equal = false;
  } else {
    equal = (table_id_ == other.table_id_)
        && (partition_id_ == other.partition_id_)
        && (partition_cnt_ == other.partition_cnt_)
        && (renew_time_ == other.renew_time_);
  }
  return equal;
}

}//end namespace share
}//end namespace oceanbase
