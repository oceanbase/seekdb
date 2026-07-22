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

namespace oceanbase
{
namespace share
{

ObTabletRuntimeInfo::ObTabletRuntimeInfo()
    : tablet_id_(),
      snapshot_version_(0),
      data_size_(0),
      required_size_(0),
      report_scn_(0),
      status_(SCN_STATUS_MAX)
{
}

ObTabletRuntimeInfo::~ObTabletRuntimeInfo()
{
  reset();
}

void ObTabletRuntimeInfo::reset()
{
  tablet_id_.reset();
  snapshot_version_ = 0;
  data_size_ = 0;
  required_size_ = 0;
  report_scn_ = 0;
  status_ = SCN_STATUS_MAX;
}

int ObTabletRuntimeInfo::assign(const ObTabletRuntimeInfo &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    tablet_id_ = other.tablet_id_;
    snapshot_version_ = other.snapshot_version_;
    data_size_ = other.data_size_;
    required_size_ = other.required_size_;
    report_scn_ = other.report_scn_;
    status_ = other.status_;
  }
  return ret;
}

int ObTabletRuntimeInfo::init(
    const common::ObTabletID &tablet_id,
    const int64_t snapshot_version,
    const int64_t data_size,
    const int64_t required_size,
    const int64_t report_scn,
    const ScnStatus status)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(
      !tablet_id.is_valid()
      || snapshot_version < 0
      || data_size < 0
      || required_size < 0
      || report_scn < 0
      || !is_status_valid(status))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init with invalid arguments", KR(ret), K(tablet_id),
        K(snapshot_version), K(data_size), K(required_size), K(report_scn), K(status));
  } else {
    tablet_id_ = tablet_id;
    snapshot_version_ = snapshot_version;
    data_size_ = data_size;
    required_size_ = required_size;
    report_scn_ = report_scn;
    status_ = status;
  }
  return ret;
}

void ObTabletRuntimeInfo::fake_for_diagnose(const common::ObTabletID &tablet_id)
{
  reset();
  tablet_id_ = tablet_id;
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
