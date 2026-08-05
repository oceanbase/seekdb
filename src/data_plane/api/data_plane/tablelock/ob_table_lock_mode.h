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

#ifndef OCEANBASE_DATA_PLANE_API_TABLELOCK_OB_TABLE_LOCK_MODE_H_
#define OCEANBASE_DATA_PLANE_API_TABLELOCK_OB_TABLE_LOCK_MODE_H_

namespace oceanbase
{
namespace transaction
{
namespace tablelock
{

// Stable lock-mode protocol shared by query and the data plane.  Keep the
// numeric values wire-compatible with persisted/RPC table-lock requests.
using ObTableLockMode = unsigned char;

static constexpr ObTableLockMode NO_LOCK = 0x0;
static constexpr ObTableLockMode ROW_SHARE = 0x8;
static constexpr ObTableLockMode ROW_EXCLUSIVE = 0x4;
static constexpr ObTableLockMode SHARE = 0x2;
static constexpr ObTableLockMode SHARE_ROW_EXCLUSIVE = 0x6;
static constexpr ObTableLockMode EXCLUSIVE = 0x1;
static constexpr char TABLE_LOCK_MODE_COUNT = 5;
static constexpr ObTableLockMode MAX_LOCK_MODE = 0xf;

} // namespace tablelock
} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TABLELOCK_OB_TABLE_LOCK_MODE_H_
