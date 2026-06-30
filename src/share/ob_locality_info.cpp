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
#include "share/ob_locality_info.h"

namespace oceanbase
{
using namespace common;
namespace share
{
void ObLocalityZone::reset()
{
}

ObLocalityZone &ObLocalityZone::operator = (const ObLocalityZone &item)
{
  UNUSED(item);
  return *this;
}

void ObLocalityInfo::reset()
{
  version_ = 0;
  local_zone_.reset();
  local_zone_type_ = ObZoneType::ZONE_TYPE_INVALID;
  local_zone_status_ = ObZoneStatus::UNKNOWN;
  locality_zone_array_.reset();
}

void ObLocalityInfo::destroy()
{
  locality_zone_array_.destroy();
  STORAGE_LOG(INFO, "ObLocalityInfo destroy finished");
}

int ObLocalityInfo::add_locality_zone(const ObLocalityZone &item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(locality_zone_array_.push_back(item))) {
  } else {
    // do nothing
  }

  return ret;
}

void ObLocalityInfo::set_version(const int64_t version)
{
  version_= version;
}

int64_t ObLocalityInfo::get_version() const
{
  return version_;
}







ObZoneType ObLocalityInfo::get_local_zone_type()
{
  return local_zone_type_;
}


int ObLocalityInfo::get_locality_zone(ObLocalityZone &item)
{
  int ret = OB_SUCCESS;
  int64_t i = 0;
  item.reset();
  for (i = 0;i < locality_zone_array_.count(); i++) {
    {
      item = locality_zone_array_.at(i);
      break;
    }
  }
  if (i == locality_zone_array_.count()) {
    ret = OB_ITER_END;
  }

  return ret;
}


bool ObLocalityInfo::is_valid()
{
  return !local_zone_.is_empty()
         && ObZoneType::ZONE_TYPE_INVALID != local_zone_type_;
}

int ObLocalityInfo::copy_to(ObLocalityInfo &locality_info)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(locality_info.local_zone_.assign(local_zone_))) {
  } else if (OB_FAIL(locality_info.locality_zone_array_.assign(locality_zone_array_))) {
  } else {
    locality_info.local_zone_type_ = local_zone_type_;
    locality_info.local_zone_status_ = local_zone_status_;
  }
  return ret;
}
} // end namespace share
} // end namespace oceanbase
