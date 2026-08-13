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

#include "share/compaction/ob_tablet_check_info.h"

namespace oceanbase
{
namespace compaction
{

bool ObTabletCheckInfo::is_valid() const
{
  return tablet_id_.is_valid() && check_medium_scn_ != 0;
}

uint64_t ObTabletCheckInfo::hash() const
{
  uint64_t hash_val = 0;
  hash_val = murmurhash(&tablet_id_, sizeof(tablet_id_), hash_val);
  return hash_val;
}

bool ObTabletCheckInfo::operator==(const ObTabletCheckInfo &other) const
{
  return tablet_id_ == other.tablet_id_;
}

} // namespace compaction
} // namespace oceanbase
