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

#include "share/log/types/log_define.h"
#include "share/ob_errno.h"

namespace oceanbase
{
namespace wal
{

int convert_sys_errno()
{
  int ret = OB_IO_ERROR;
  switch (errno) {
  case EACCES:
    ret = OB_FILE_OR_DIRECTORY_PERMISSION_DENIED;
    break;
  case ENOENT:
    ret = OB_NO_SUCH_FILE_OR_DIRECTORY;
    break;
  case EEXIST:
  case ENOTEMPTY:
    ret = OB_FILE_OR_DIRECTORY_EXIST;
    break;
  case EAGAIN:
    ret = OB_EAGAIN;
    break;
#ifndef _WIN32
  case EDQUOT:
#endif
  case ENOSPC:
    ret = OB_ALLOCATE_DISK_SPACE_FAILED;
    break;
  case ENOMEM:
    ret = OB_ALLOCATE_MEMORY_FAILED;
    break;
  case EMFILE:
    ret = OB_TOO_MANY_OPEN_FILES;
    break;
  default:
    ret = OB_IO_ERROR;
  }
  return ret;
}

bool is_number(const char* str)
{
	bool bool_ret = true;
  for (int64_t i = 0; true == bool_ret; ++i) {
    if ('\0' == str[i]) {
      break;
    } else if (!isdigit(str[i])) {
      bool_ret = false;
		}
  }
	return bool_ret;
}

} // namespace wal
} // namespace oceanbase
