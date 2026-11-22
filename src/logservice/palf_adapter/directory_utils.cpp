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

#include "logservice/palf_adapter/directory_utils.h"

#include <errno.h>
#include <string.h>
#include "lib/ob_define.h"
#include "lib/utility/ob_platform_io.h"
#include "share/ob_errno.h"

namespace oceanbase
{
namespace palf
{
namespace
{
bool check_rename_success(const char *src_name, const char *dest_name)
{
  common::ObPlatformFileStat file_info;
  return 0 != common::ob_stat_utf8(src_name, file_info)
      && 0 == common::ob_stat_utf8(dest_name, file_info);
}
}

int rename_directory(const char *src_name, const char *dest_name)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(src_name) || OB_ISNULL(dest_name)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", KP(src_name), KP(dest_name));
  } else if (-1 == common::ob_rename_utf8(src_name, dest_name)) {
    ret = convert_sys_errno();
    if (OB_NO_SUCH_FILE_OR_DIRECTORY == ret && check_rename_success(src_name, dest_name)) {
      ret = OB_SUCCESS;
    } else {
      PALF_LOG(WARN, "rename directory failed", K(ret), K(errno), K(src_name), K(dest_name));
    }
  }
  return ret;
}

int scan_directory(const char *dir_name, DirectoryFunctor &functor)
{
  int ret = OB_SUCCESS;
  common::ObPlatformDir *open_dir = NULL;
  if (OB_ISNULL(dir_name)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", K(ret), K(dir_name));
  } else if (OB_ISNULL(open_dir = common::ob_opendir_utf8(dir_name))) {
    ret = ENOENT == errno ? OB_NO_SUCH_FILE_OR_DIRECTORY : OB_FILE_NOT_OPENED;
    PALF_LOG(WARN, "Fail to open dir", K(ret), K(dir_name));
  } else {
    char entry_name[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    int read_ret = 0;
    while (OB_SUCC(ret)
           && 1 == (read_ret = common::ob_readdir_utf8(
                open_dir, entry_name, sizeof(entry_name)))) {
      if (0 == STRCMP(entry_name, ".") || 0 == STRCMP(entry_name, "..")) {
      } else if (OB_FAIL(functor.func(entry_name))) {
        PALF_LOG(WARN, "fail to operate dir entry", K(ret), K(dir_name), K(entry_name));
      }
    }
    if (OB_SUCC(ret) && 0 > read_ret) {
      ret = convert_sys_errno();
      PALF_LOG(WARN, "read dir failed", K(ret), K(errno), K(dir_name));
    }
  }
  if (NULL != open_dir) {
    common::ob_closedir_utf8(open_dir);
  }
  return ret;
}

} // namespace palf
} // namespace oceanbase
