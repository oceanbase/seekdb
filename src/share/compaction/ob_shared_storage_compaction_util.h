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
#ifndef OB_SHARE_STORAGE_SHARE_COMPACTION_SHARED_COMPACTION_UTIL_H_
#define OB_SHARE_STORAGE_SHARE_COMPACTION_SHARED_COMPACTION_UTIL_H_
#include "stdint.h"

namespace oceanbase
{
namespace compaction
{
static const int64_t MACRO_STEP_SIZE = 0x1 << 25;
static const int64_t INVALID_TASK_IDX = -1;

} // namespace compaction
} // namespace oceanbase

#endif // OB_SHARE_STORAGE_SHARE_COMPACTION_SHARED_COMPACTION_UTIL_H_
