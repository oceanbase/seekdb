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
#include "share/ob_tablet_autoincrement_param.h"

namespace oceanbase
{
namespace share
{

OB_SERIALIZE_MEMBER(ObTabletAutoincInterval, tablet_id_, start_, end_)

void ObTabletCacheInterval::reset()
{
  tablet_id_ = OB_INVALID_ID;
  cache_size_ = 0;
  task_id_ = -1;
  next_value_ = 0;
  start_ = 0;
  end_ = 0;
}

void ObTabletCacheInterval::set(uint64_t start, uint64_t end)
{
  next_value_ = start;
  start_ = start;
  end_ = end;
}

int ObTabletCacheInterval::next_value(uint64_t &next_value)
{
  int ret = OB_SUCCESS;
  next_value = ATOMIC_FAA(&next_value_, 1);
  if (next_value > end_) {
    ret = OB_EAGAIN;
  }
  return ret;
}

int ObTabletCacheInterval::get_value(uint64_t &value)
{
  int ret = OB_SUCCESS;
  value = max(next_value_, start_);
  value = min(value, end_);
  return ret;
}

int ObTabletCacheInterval::fetch(uint64_t count, ObTabletCacheInterval &dest)
{
  int ret = OB_SUCCESS;
  uint64_t start = ATOMIC_LOAD(&next_value_);
  uint64_t end = 0;
  uint64_t old_start = start;
  while ((end = start + count - 1) <= end_ &&
         old_start != (start = ATOMIC_CAS(&next_value_, old_start, end + 1))) {
    old_start = start;
    PAUSE();
  }
  if (end > end_) {
    ret = OB_EAGAIN;
  } else {
    dest.set(start, end);
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObTabletAutoincParam, auto_increment_cache_size_)
OB_SERIALIZE_MEMBER(
    ObTabletAutoincSeqCopyParam,
    src_tablet_id_,
    dest_tablet_id_,
    ret_code_,
    autoinc_seq_)

} // namespace share
} // namespace oceanbase
