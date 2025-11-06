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

#include "storage/tablet/ob_tablet_mds_data_cache.h"

#include "storage/tablet/ob_tablet_create_delete_mds_user_data.h"
#include "storage/tablet/ob_tablet_binding_mds_user_data.h"

using namespace oceanbase::transaction;

namespace oceanbase
{
namespace storage
{
ObTabletStatusCache::ObTabletStatusCache()
  : tablet_status_(ObTabletStatus::MAX),
    create_commit_version_(ObTransVersion::INVALID_TRANS_VERSION),
    delete_commit_version_(ObTransVersion::INVALID_TRANS_VERSION)
{
}


void ObTabletStatusCache::set_value(const ObTabletCreateDeleteMdsUserData &user_data)
{
  tablet_status_ = user_data.tablet_status_;
  create_commit_version_ = user_data.create_commit_version_;
  delete_commit_version_ = user_data.delete_commit_version_;
}

void ObTabletStatusCache::reset()
{
  tablet_status_ = ObTabletStatus::MAX;
  create_commit_version_ = ObTransVersion::INVALID_TRANS_VERSION;
  delete_commit_version_ = ObTransVersion::INVALID_TRANS_VERSION;
}


ObDDLInfoCache::ObDDLInfoCache()
  : redefined_(false),
    schema_version_(INT64_MAX),
    snapshot_version_(INT64_MAX)
{
}


void ObDDLInfoCache::set_value(const ObTabletBindingMdsUserData &user_data)
{
  redefined_ = user_data.redefined_;
  schema_version_ = user_data.schema_version_;
  snapshot_version_ = user_data.snapshot_version_;
}

void ObDDLInfoCache::reset()
{
  redefined_ = false;
  schema_version_ = INT64_MAX;
  snapshot_version_ = INT64_MAX;
}

} // namespace storage
} // namespace oceanbase
