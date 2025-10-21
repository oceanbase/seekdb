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

#ifndef OCEANBASE_STORAGE_UTL_FILE_OB_UTL_CONSTANTS_H_
#define OCEANBASE_STORAGE_UTL_FILE_OB_UTL_CONSTANTS_H_

#include <sys/stat.h>
#include <sys/types.h>
#include "lib/alloc/alloc_assist.h"

namespace oceanbase
{
namespace storage
{
class ObUtlFileConstants
{
public:
  static constexpr int MAX_LINE_SIZE_LOWER_LIMIT = 1;
  static constexpr int MAX_LINE_SIZE_UPPER_LIMIT = 32767;
  static constexpr int DEFAULT_MAX_LINE_SIZE = 1024;
  static constexpr int DEFAULT_IO_RETRY_CNT = 3;
  static constexpr int UTF_FILE_BUFFER_ALIGN_SIZE = 4 * 1024; // 4KB
  static constexpr int UTF_FILE_WRITE_BUFFER_SIZE = 32 * 1024; // 32KB
  static constexpr int UTL_PATH_SIZE_LIMIT = 256;
  static constexpr mode_t UTL_FILE_ACCESS_MODE = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_UTL_FILE_OB_UTL_CONSTANTS_H_
