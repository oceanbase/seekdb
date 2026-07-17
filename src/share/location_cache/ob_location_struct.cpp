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

#define USING_LOG_PREFIX SHARE_LOCATION

#include "lib/stat/ob_diagnostic_info_guard.h"
#include "share/location_cache/ob_location_struct.h"
#include "lib/statistic_event/ob_stat_event.h"

namespace oceanbase
{
using namespace common;
namespace share
{
OB_SERIALIZE_MEMBER(ObLSReplicaLocation,
    server_,
    role_,
    sql_port_,
    replica_type_,
    property_,
    restore_status_,
    proposal_id_);

ObLSReplicaLocation::ObLSReplicaLocation()
    : server_(),
      role_(FOLLOWER),
      sql_port_(OB_INVALID_INDEX),
      replica_type_(REPLICA_TYPE_FULL),
      property_(),
      restore_status_(),
      proposal_id_(OB_INVALID_ID)
{
}

void ObLSReplicaLocation::reset()
{
  server_.reset();
  role_ = FOLLOWER;
  sql_port_ = OB_INVALID_INDEX;
  replica_type_ = REPLICA_TYPE_FULL;
  property_.reset();
  restore_status_ = ObLSRestoreStatus::Status::NONE;
  proposal_id_ = OB_INVALID_ID;
}

bool ObLSReplicaLocation::is_valid() const
{
  return server_.is_valid()
      && OB_INVALID_INDEX != sql_port_
      && ObReplicaTypeCheck::is_replica_type_valid(replica_type_)
      && property_.is_valid()
      && proposal_id_ >= 0;
}

bool ObLSReplicaLocation::operator==(const ObLSReplicaLocation &other) const
{
  return server_ == other.server_
      && role_ == other.role_
      && sql_port_ == other.sql_port_
      && replica_type_ == other.replica_type_
      && property_ == other.property_
      && restore_status_ == other.restore_status_
      && proposal_id_ == other.proposal_id_;
}

bool ObLSReplicaLocation::operator!=(const ObLSReplicaLocation &other) const
{
  return !(*this == other);
}

int ObLSReplicaLocation::assign(const ObLSReplicaLocation &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    server_ = other.server_;
    role_ = other.role_;
    sql_port_ = other.sql_port_;
    replica_type_ = other.replica_type_;
    property_ = other.property_;
    restore_status_ = other.restore_status_;
    proposal_id_ = other.proposal_id_;
  }
  return ret;
}

int ObLSReplicaLocation::init(
    const common::ObAddr &server,
    const common::ObRole &role,
    const int64_t &sql_port,
    const common::ObReplicaType &replica_type,
    const common::ObReplicaProperty &property,
    const ObLSRestoreStatus &restore_status,
    const int64_t proposal_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!server.is_valid()
      || OB_INVALID_INDEX == sql_port
      || !ObReplicaTypeCheck::is_replica_type_valid(replica_type)
      || !property.is_valid()
      || proposal_id < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ObLSReplicaLocation init failed", KR(ret),
             K(server), K(role), K(sql_port), K(replica_type), K(property),
             K(restore_status), K(proposal_id));
  } else {
    server_ = server;
    role_ = role;
    sql_port_ = sql_port;
    replica_type_ = replica_type;
    property_ = property;
    restore_status_ = restore_status;
    proposal_id_ = proposal_id;
  }
  return ret;
}

int ObLSReplicaLocation::init_without_check(
    const common::ObAddr &server,
    const common::ObRole &role,
    const int64_t &sql_port,
    const common::ObReplicaType &replica_type,
    const common::ObReplicaProperty &property,
    const ObLSRestoreStatus &restore_status,
    const int64_t proposal_id)
{
  int ret = OB_SUCCESS;
  server_ = server;
  role_ = role;
  sql_port_ = sql_port;
  replica_type_ = replica_type;
  property_ = property;
  restore_status_ = restore_status;
  proposal_id_ = proposal_id;
  return ret;
}

ObLocationSem::ObLocationSem() : cur_count_(0), max_count_(0), cond_()
{
  cond_.init(ObWaitEventIds::LOCATION_CACHE_COND_WAIT);
}

ObLocationSem::~ObLocationSem()
{}

void ObLocationSem::set_max_count(const int64_t max_count)
{
  cond_.lock();
  max_count_ = max_count;
  cond_.unlock();
  LOG_INFO("location cache fetch location concurrent max count changed", K(max_count));
}

int ObLocationSem::acquire(const int64_t abs_timeout_us)
{
  // when we change max_count to small value, cur_count > max_count is possible
  int ret = OB_SUCCESS;
  const int64_t default_wait_time_ms = 1000;
  int64_t wait_time_ms = default_wait_time_ms;
  bool has_wait = false;
  cond_.lock();
  if (max_count_ <= 0 || cur_count_ < 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid max_count", K(ret), K_(max_count), K_(cur_count));
  } else {
    while (OB_SUCC(ret) && cur_count_ >= max_count_) {
      if (abs_timeout_us > 0) {
        wait_time_ms = (abs_timeout_us - ObTimeUtility::current_time()) / 1000 + 1;  // 1ms at least
        if (wait_time_ms <= 0) {
          ret = OB_TIMEOUT;
        }
      } else {
        wait_time_ms = default_wait_time_ms;
      }

      if (OB_SUCC(ret)) {
        if (wait_time_ms > INT32_MAX) {
          wait_time_ms = INT32_MAX;
          const bool force_print = true;
          LOG_DEBUG("wait time is longer than INT32_MAX", K(wait_time_ms), K(abs_timeout_us));
        }
        has_wait = true;
        cond_.wait(static_cast<int32_t>(wait_time_ms));
      }
    }

    if (has_wait) {
      EVENT_INC(LOCATION_CACHE_WAIT);
    }

    if (OB_SUCC(ret)) {
      ++cur_count_;
    }
  }
  cond_.unlock();
  return ret;
}

int ObLocationSem::release()
{
  int ret = OB_SUCCESS;
  cond_.lock();
  if (cur_count_ <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid cur_count", K(ret), K_(cur_count));
  } else {
    --cur_count_;
  }
  cond_.signal();
  cond_.unlock();
  return ret;
}

} // end namespace share
} // end namespace oceanbase
