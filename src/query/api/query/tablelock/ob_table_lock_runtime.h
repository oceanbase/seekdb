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

#ifndef OCEANBASE_QUERY_API_TABLELOCK_OB_TABLE_LOCK_RUNTIME_H_
#define OCEANBASE_QUERY_API_TABLELOCK_OB_TABLE_LOCK_RUNTIME_H_

#include <stdint.h>

namespace oceanbase
{
namespace query
{

// Query-owned autonomous cleanup for session-scoped locks.  The data plane
// supplies only the persisted owner identity; transaction/session setup stays
// behind this semantic interface.
int release_locks_for_dead_owner(uint8_t owner_type, int64_t owner_id);

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_TABLELOCK_OB_TABLE_LOCK_RUNTIME_H_
