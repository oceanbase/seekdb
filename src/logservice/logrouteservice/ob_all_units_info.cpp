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
#include "ob_all_units_info.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace logservice
{
int ObUnitsRecord::init(
    const common::ObAddr &server,
    ObString &zone,
    common::ObZoneType &zone_type,
    ObString &region)
{
  int ret = OB_SUCCESS;
  server_ = server;
  zone_type_ = zone_type;

  if (OB_FAIL(zone_.assign(zone))) {
    LOG_ERROR("zone assign fail", KR(ret), K(zone));
  } else if (OB_FAIL(region_.assign(region))) {
    LOG_ERROR("zone assign fail", KR(ret), K(region));
  } else {}

  return ret;
}

void ObUnitsRecordInfo::reset()
{
  cluster_id_ = 0;
  units_record_array_.reset();
}

int ObUnitsRecordInfo::init(const int64_t cluster_id)
{
  int ret = OB_SUCCESS;

  cluster_id_ = cluster_id;
  units_record_array_.reset();

  return ret;
}

int ObUnitsRecordInfo::add(ObUnitsRecord &record)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(units_record_array_.push_back(record))) {
    LOG_ERROR("units_record_array_ push_back failed", KR(ret), K(record));
  }

  return ret;
}

} // namespace logservice
} // namespace oceanbase
