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

#define USING_LOG_PREFIX LIB
#include "ob_running_mode.h"

namespace oceanbase {
namespace lib {
const int64_t ObRunningModeConfig::MIN_MEM = 1L << 30;  // The minimum value for memory_limit.
const int64_t ObRunningModeConfig::MINI_MEM_LOWER = 1L << 30;
const int64_t ObRunningModeConfig::MINI_MEM_UPPER = 12L << 30;
const int64_t ObRunningModeConfig::MINI_CPU_UPPER = 8;

bool __attribute__((weak)) mtl_is_mini_mode() { return false; }
} //end of namespace lib
} //end of namespace oceanbase

extern "C" {
} /* extern "C" */
