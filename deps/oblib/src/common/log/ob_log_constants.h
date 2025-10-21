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

#ifndef OCEANBASE_COMMON_OB_LOG_CONSTANTS_H_
#define OCEANBASE_COMMON_OB_LOG_CONSTANTS_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{
class ObLogConstants
{
public:
  static constexpr int64_t MAX_LOG_FILE_SIZE = 64 << 20; // 64MB
  static constexpr int64_t LOG_FILE_ALIGN_SIZE = 4 << 10; // 4KB
  static constexpr int64_t LOG_FILE_ALIGN_MASK = LOG_FILE_ALIGN_SIZE - 1;
  static constexpr int64_t LOG_BUF_RESERVED_SIZE = 3 * LOG_FILE_ALIGN_SIZE; // NOP + switch_log
  static constexpr int64_t LOG_ITEM_MAX_LENGTH = 63 << 20; // 63MB
  static constexpr int64_t NOP_SWITCH_LOG_SEQ = 0;
};
} // namespace blocksstable
} // namespace oceanbase

#endif // OCEANBASE_COMMON_OB_LOG_CONSTANTS_H_
