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

#ifndef OCEANBASE_DATA_PLANE_API_ACCESS_OB_LOCK_FLAG_H_
#define OCEANBASE_DATA_PLANE_API_ACCESS_OB_LOCK_FLAG_H_

namespace oceanbase
{
namespace data_plane
{

enum ObLockFlag
{
  LF_NONE = 0,
  LF_WRITE = 1,
};

} // namespace data_plane

// Compatibility names for data-plane implementation code.  New callers at
// the boundary use data_plane::ObLockFlag directly.
namespace storage
{
using ObLockFlag = data_plane::ObLockFlag;
static constexpr ObLockFlag LF_NONE = data_plane::LF_NONE;
static constexpr ObLockFlag LF_WRITE = data_plane::LF_WRITE;
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_ACCESS_OB_LOCK_FLAG_H_
