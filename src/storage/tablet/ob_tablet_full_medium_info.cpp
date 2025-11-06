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

#include "storage/tablet/ob_tablet_full_medium_info.h"

#define USING_LOG_PREFIX STORAGE

using namespace oceanbase::common;

namespace oceanbase
{
namespace storage
{
ObTabletFullMediumInfo::ObTabletFullMediumInfo()
  : extra_medium_info_(),
    medium_info_list_()
{
}

void ObTabletFullMediumInfo::reset()
{
  extra_medium_info_.reset();
  medium_info_list_.reset();
}

int ObTabletFullMediumInfo::assign(common::ObIAllocator &allocator, const ObTabletFullMediumInfo &other)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(medium_info_list_.assign(allocator, other.medium_info_list_))) {
    LOG_WARN("failed to assign", K(ret), K(other));
  } else {
    extra_medium_info_.info_ = other.extra_medium_info_.info_;
    extra_medium_info_.last_medium_scn_ = other.extra_medium_info_.last_medium_scn_;
  }

  return ret;
}

int ObTabletFullMediumInfo::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
      extra_medium_info_,
      medium_info_list_);

  return ret;
}

int ObTabletFullMediumInfo::deserialize(common::ObIAllocator &allocator, const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(extra_medium_info_.deserialize(buf, data_len, pos))) {
    LOG_WARN("failed to deserialize", K(ret));
  } else if (OB_FAIL(medium_info_list_.deserialize(allocator, buf, data_len, pos))) {
    LOG_WARN("failed to deserialize", K(ret));
  }

  return ret;
}

int64_t ObTabletFullMediumInfo::get_serialize_size() const
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
      extra_medium_info_,
      medium_info_list_);

  return len;
}
} // namespace storage
} // namespace oceanbase
