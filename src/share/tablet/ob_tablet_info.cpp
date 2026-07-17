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

#include "share/tablet/ob_tablet_info.h"
#include "share/tablet/ob_tablet_filter.h" // ObTabletFilter

namespace oceanbase
{
namespace share
{

const static char * ObDataChecksumTypeStr[] = {
  "NORMAL",
  "NORMAL_WITH_NORMAL_COLUMN"
};


ObTabletReplica::ObTabletReplica()
    : tablet_id_(),
      server_(),
      snapshot_version_(0),
      data_size_(0),
      required_size_(0),
      report_scn_(0),
      status_(SCN_STATUS_MAX)
{
}

ObTabletReplica::~ObTabletReplica()
{
  reset();
}

void ObTabletReplica::reset()
{
  tablet_id_.reset();
  server_.reset();
  snapshot_version_ = 0;
  data_size_ = 0;
  required_size_ = 0;
  report_scn_ = 0;
  status_ = SCN_STATUS_MAX;
}

int ObTabletReplica::assign(const ObTabletReplica &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    tablet_id_ = other.tablet_id_;
    server_ = other.server_;
    snapshot_version_ = other.snapshot_version_;
    data_size_ = other.data_size_;
    required_size_ = other.required_size_;
    report_scn_ = other.report_scn_;
    status_ = other.status_;
  }
  return ret;
}

int ObTabletReplica::init(
    const common::ObTabletID &tablet_id,
    const common::ObAddr &server,
    const int64_t snapshot_version,
    const int64_t data_size,
    const int64_t required_size,
    const int64_t report_scn,
    const ScnStatus status)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(
      !tablet_id.is_valid_with_tenant()
      || !server.is_valid()
      || snapshot_version < 0
      || data_size < 0
      || required_size < 0
      || report_scn < 0
      || !is_status_valid(status))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init with invalid arguments", KR(ret), K(tablet_id),
        K(server), K(snapshot_version), K(data_size), K(required_size), K(report_scn), K(status));
  } else {
    tablet_id_ = tablet_id;
    server_ = server;
    snapshot_version_ = snapshot_version;
    data_size_ = data_size;
    required_size_ = required_size;
    report_scn_ = report_scn;
    status_ = status;
  }
  return ret;
}

bool ObTabletReplica::is_equal_for_report(const ObTabletReplica &other) const
{
  bool is_equal = false;
  if (this == &other) {
    is_equal = true;
  } else if (true
      && tablet_id_ == other.tablet_id_
      && server_ == other.server_
      && snapshot_version_ == other.snapshot_version_
      && data_size_ == other.data_size_
      && required_size_ == other.required_size_) {
    is_equal = true;
  }
  return is_equal;
}

void ObTabletReplica::fake_for_diagnose(const common::ObTabletID &tablet_id)
{
  reset();
  tablet_id_ = tablet_id;
}

ObTabletInfo::ObTabletInfo()
    : tablet_id_(),
      has_replica_(false),
      replica_()
{
}

ObTabletInfo::ObTabletInfo(const common::ObTabletID &tablet_id)
    : tablet_id_(tablet_id),
      has_replica_(false),
      replica_()
{
}

ObTabletInfo::ObTabletInfo(
    const common::ObTabletID &tablet_id,
    const ObTabletReplica &replica)
    : tablet_id_(),
      has_replica_(false),
      replica_()
{
  (void)init(tablet_id, replica);
}

ObTabletInfo::~ObTabletInfo()
{
  reset();
}

void ObTabletInfo::reset()
{
  tablet_id_.reset();
  has_replica_ = false;
  replica_.reset();
}

int ObTabletInfo::assign(const ObTabletInfo &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    tablet_id_ = other.tablet_id_;
    has_replica_ = other.has_replica_;
    if (OB_FAIL(replica_.assign(other.replica_))) {
      LOG_WARN("fail to assign replica", KR(ret), K_(tablet_id), K_(replica));
    }
  }
  return ret;
}

int ObTabletInfo::init_empty(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_UNLIKELY(!tablet_id.is_valid_with_tenant())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init empty tablet info with invalid arguments", KR(ret), K(tablet_id));
  } else {
    tablet_id_ = tablet_id;
  }
  return ret;
}

int ObTabletInfo::init(
    const common::ObTabletID &tablet_id,
    const ObTabletReplica &replica)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_UNLIKELY(!tablet_id.is_valid_with_tenant()
      || !replica.is_valid()
      || tablet_id != replica.get_tablet_id())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init with invalid arguments", KR(ret), K(tablet_id), K(replica));
  } else if (OB_FAIL(replica_.assign(replica))) {
    LOG_WARN("fail to assign replica", KR(ret), K(replica));
  } else {
    tablet_id_ = tablet_id;
    has_replica_ = true;
  }
  return ret;
}

int ObTabletInfo::init_by_replica(const ObTabletReplica &replica)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_UNLIKELY(!replica.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid replica", KR(ret), K(replica));
  } else if (OB_FAIL(init(replica.get_tablet_id(), replica))) {
    LOG_WARN("fail to init tablet_info", KR(ret), K(replica));
  }
  return ret;
}

int ObTabletInfo::set_replica(const ObTabletReplica &replica)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id_.is_valid_with_tenant()
      || !replica.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KPC(this), K(replica));
  } else if (OB_UNLIKELY(tablet_id_ != replica.get_tablet_id())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("replica not belong to this tablet",
        KR(ret), K_(tablet_id), K(replica));
  } else if (OB_FAIL(replica_.assign(replica))) {
    LOG_WARN("fail to assign replica", KR(ret), K(replica));
  } else {
    has_replica_ = true;
  }
  return ret;
}

bool ObTabletInfo::is_self_replica(const ObTabletReplica &replica) const
{
  return replica.get_tablet_id() == tablet_id_;
}

int ObTabletInfo::filter(const ObTabletReplicaFilter &filter)
{
  int ret = OB_SUCCESS;
  if (!tablet_id_.is_valid_with_tenant()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "tablet_info", *this);
  } else if (has_replica_) {
    bool pass = true;
    if (OB_FAIL(filter.check(replica_, pass))) {
      LOG_WARN("filter tablet meta row failed", K(ret), "tablet_meta_row", replica_);
    } else if (!pass) {
      has_replica_ = false;
      replica_.reset();
    }
  }
  return ret;
}

ObTabletTablePair::ObTabletTablePair()
  : tablet_id_(), table_id_(OB_INVALID_ID)
{}

ObTabletTablePair::ObTabletTablePair(
  const common::ObTabletID &tablet_id,
  const uint64_t table_id)
  : tablet_id_(tablet_id), table_id_(table_id)
{}

ObTabletTablePair::~ObTabletTablePair()
{}

void ObTabletTablePair::reset()
{
  tablet_id_.reset();
  table_id_ = OB_INVALID_ID;
}

int ObTabletTablePair::init(
  const common::ObTabletID &tablet_id,
  const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id.is_valid() || OB_INVALID_ID == table_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init with invalid argument", KR(ret), K(tablet_id), K(table_id));
  } else {
    tablet_id_ = tablet_id;
    table_id_ = table_id;
  }
  return ret;
}

int ObTabletTablePair::assign(const ObTabletTablePair &other)
{
  int ret = OB_SUCCESS;
  if (&other != this) {
    tablet_id_ = other.tablet_id_;
    table_id_ = other.table_id_;
  }
  return ret;
}

bool ObTabletTablePair::is_valid() const
{
  return tablet_id_.is_valid() && OB_INVALID_ID != table_id_;
}

} // end namespace share
} // end namespace oceanbase
