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

#define USING_LOG_PREFIX STORAGE
#include "storage/truncate_info/ob_truncate_tablet_arg.h"

namespace oceanbase
{
namespace storage
{

int ObTruncateTabletArg::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, info_, index_tablet_id_, truncate_info_);
  return ret;
}

int64_t ObTruncateTabletArg::get_serialize_size() const
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, info_, index_tablet_id_, truncate_info_);
  return len;
}

int ObTruncateTabletArg::deserialize(
    common::ObIAllocator &allocator,
    const char *buf,
    const int64_t data_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, info_, index_tablet_id_);
  if (FAILEDx(truncate_info_.deserialize(allocator, buf, data_len, pos))) {
    LOG_WARN("failed to deserialize truncate arg", KR(ret));
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
