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

#ifndef  OCEANBASE_COMMON_PARITY_CHECK_H_
#define  OCEANBASE_COMMON_PARITY_CHECK_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{
// If val contains an even number of 1, the value is 0, otherwise the value is 1.
bool parity_check(const uint16_t value);
// If val contains an even number of 1, the value is 0, otherwise the value is 1.
bool parity_check(const uint32_t value);
// If val contains an even number of 1, the value is 0, otherwise the value is 1.
bool parity_check(const uint64_t value);
}
}
#endif
