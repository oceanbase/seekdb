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

#ifndef OCEANBASE_COMMON_OB_LOG_DEFINITION_H_
#define OCEANBASE_COMMON_OB_LOG_DEFINITION_H_

#include <fcntl.h>

namespace oceanbase
{
namespace common
{
enum ObRedoLogType
{
  OB_REDO_TYPE_INVALID = 0,
  OB_REDO_TYPE_CLOG = 1,
  OB_REDO_TYPE_ILOG = 2,
  OB_REDO_TYPE_SLOG = 3,
};

class ObLogDefinition
{
public:
  static constexpr int LOG_READ_FLAG = O_RDONLY | O_DIRECT;
  static constexpr int LOG_WRITE_FLAG = O_RDWR | O_DIRECT | O_SYNC | O_CREAT;
  static constexpr int LOG_APPEND_FLAG = O_RDWR | O_DIRECT | O_SYNC | O_CREAT | O_APPEND;
  static constexpr int FILE_OPEN_MODE = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
  static constexpr mode_t DIR_CREATE_MODE = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
  static constexpr int DEFAULT_IO_RETRY_CNT = 3;
  static constexpr int64_t RETRY_SLEEP_TIME_IN_US = 100 * 1000;
};
} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_COMMON_OB_LOG_DEFINITION_H_
