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

#define USING_LOG_PREFIX OBLOG
#include "ob_all_zone_info.h"

namespace oceanbase
{
namespace logservice
{
int AllZoneRecord::init(ObString &zone,
    ObString &region)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(zone_.assign(zone))) {
    LOG_ERROR("zone assign fail", KR(ret), K(zone));
  } else if (OB_FAIL(region_.assign(region))) {
    LOG_ERROR("zone assign fail", KR(ret), K(region));
  } else {}

  return ret;
}

int AllZoneTypeRecord::init(ObString &zone,
    common::ObZoneType &zone_type)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(zone_.assign(zone))) {
    LOG_ERROR("zone assign fail", KR(ret), K(zone));
  } else {
    zone_type_ = zone_type;
  }

  return ret;
}

void ObAllZoneInfo::reset()
{
  cluster_id_ = 0;
  all_zone_array_.reset();
}

int ObAllZoneInfo::init(const int64_t cluster_id)
{
  int ret = OB_SUCCESS;

  cluster_id_ = cluster_id;
  all_zone_array_.reset();

  return ret;
}

int ObAllZoneInfo::add(AllZoneRecord &record)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(all_zone_array_.push_back(record))) {
    LOG_ERROR("all_zone_array_ push_back failed", KR(ret), K(record));
  }

  return ret;
}

void ObAllZoneTypeInfo::reset()
{
  cluster_id_ = 0;
  all_zone_type_array_.reset();
}

int ObAllZoneTypeInfo::init(const int64_t cluster_id)
{
  int ret = OB_SUCCESS;

  cluster_id_ = cluster_id;
  all_zone_type_array_.reset();

  return ret;
}

int ObAllZoneTypeInfo::add(AllZoneTypeRecord &record)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(all_zone_type_array_.push_back(record))) {
    LOG_ERROR("all_zone_type_array_ push_back failed", KR(ret), K(record));
  }

  return ret;
}

} // namespace logservice
} // namespace oceanbase
