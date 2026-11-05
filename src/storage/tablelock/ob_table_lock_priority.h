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
#ifndef OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_PRIORITY_H_
#define OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_PRIORITY_H_
// plain value enum moved out of ob_table_lock_common.h(generated through def.h X-macro,no upper-layer dependency;
// for by-value use by share RPC args,this header is conf logical L2)
#include <stdint.h>
namespace oceanbase
{
namespace transaction
{
namespace tablelock
{
enum class ObTableLockPriority : int8_t
{
  INVALID = -1,
#define DEF_LOCK_PRIORITY(n, type)              \
  type = n,
#include "storage/tablelock/ob_table_lock_def.h"
#undef DEF_LOCK_PRIORITY
};
}  // namespace tablelock
}  // namespace transaction
}  // namespace oceanbase
#endif
