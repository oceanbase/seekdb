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

#include "log_block_pool_interface.h"
#include "log_io_utils.h"
#include "lib/utility/ob_platform_io.h"
#include <errno.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace oceanbase
{
namespace palf
{

int is_block_used_for_palf(const int fd, const char *path, bool &result)
{
  int ret = OB_SUCCESS;
  common::ObPlatformFileStat file_stat;
  result = false;
  if (-1 == common::ob_statat_utf8(fd, path, file_stat, true)) {
    ret = convert_sys_errno();
    PALF_LOG(ERROR, "ob_statat_utf8 failed", K(ret), K(path), K(errno));
  } else if (file_stat.size == PALF_PHY_BLOCK_SIZE) {
    result = true;
  } else {
    result = false;
  }
  return ret;
}

int remove_file_at(const char *dir, const char *path, ILogBlockPool *log_block_pool)
{
  int ret = OB_SUCCESS;
  int fd = open_directory(dir);
  if (-1 == fd) {
    ret = convert_sys_errno();
    PALF_LOG(ERROR, "open_directory failed", K(ret), K(dir));
  } else if (OB_FAIL(log_block_pool->remove_block_at(fd, path))) {
  } else {
    PALF_LOG(INFO, "remove_file_at success", K(dir), K(path));
  }

  if (-1 != fd) {
    common::ob_fsync_fd(fd);
    ::close(fd);
  }
  return ret;
}

namespace
{

int get_path_type_no_follow(const char *path, bool &is_dir)
{
  int ret = OB_SUCCESS;
  common::ObPlatformFileStat file_stat;
  is_dir = false;
  if (0 != common::ob_stat_utf8(path, file_stat, false)) {
    ret = convert_sys_errno();
    PALF_LOG(WARN, "failed to stat path without following links", K(ret), K(path), K(errno));
  } else if (common::ob_stat_is_link(file_stat.mode)) {
    ret = OB_IO_ERROR;
    PALF_LOG(WARN, "refuse to recursively remove a symbolic link or junction", K(ret), K(path));
  } else {
    is_dir = common::ob_stat_is_dir(file_stat.mode);
  }
  return ret;
}

} // namespace

int remove_directory_rec(const char *path, ILogBlockPool *log_block_pool)
{
  int ret = OB_SUCCESS;
  bool is_dir = false;
  common::ObPlatformDir *dir = NULL;
  if (OB_FAIL(get_path_type_no_follow(path, is_dir))) {
  } else if (!is_dir) {
    ret = OB_IO_ERROR;
    PALF_LOG(WARN, "path to remove recursively is not a directory", K(ret), K(path));
  } else if (NULL == (dir = common::ob_opendir_utf8(path))) {
    ret = convert_sys_errno();
    PALF_LOG(WARN, "opendir failed", K(ret), K(path));
  } else {
    char entry_name[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    int read_ret = 0;
    while (OB_SUCC(ret)
           && 1 == (read_ret = common::ob_readdir_utf8(dir, entry_name, sizeof(entry_name)))) {
      bool is_dir = false;
      char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
      if (0 == strcmp(entry_name, ".") || 0 == strcmp(entry_name, "..")) {
        // do nothing
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s", path, entry_name)) {
        ret = OB_ERR_UNEXPECTED;
        PALF_LOG(WARN, "snprintf failed", K(ret), K(current_file_path), K(path), K(entry_name));
      } else if (OB_FAIL(get_path_type_no_follow(current_file_path, is_dir))) {
      } else if (true == is_dir && OB_FAIL(remove_directory_rec(current_file_path, log_block_pool))) {
        PALF_LOG(WARN, "remove directory failed", K(ret), K(entry_name), K(path));
        // delete normal file
      } else if (false == is_dir && OB_FAIL(remove_file_at(path, entry_name, log_block_pool))) {
        PALF_LOG(WARN, "remove_file_at failed", K(ret), K(current_file_path));
      } else {
        PALF_LOG(INFO, "remove directory or file success", K(path), K(current_file_path));
      }
    }
    if (OB_SUCC(ret) && 0 > read_ret) {
      ret = convert_sys_errno();
      PALF_LOG(WARN, "read dir failed", K(ret), K(errno), K(path));
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(FileDirectoryUtils::delete_directory(path))) {
    PALF_LOG(WARN, "delete_directory failed", K(ret), K(path));
  }
  if (NULL != dir) {
    common::ob_closedir_utf8(dir);
  }
  return ret;
}

int remove_tmp_file_or_directory_at(const char *path, ILogBlockPool *log_block_pool)
{
  int ret = OB_SUCCESS;
  bool is_dir = false;
  common::ObPlatformDir *dir = NULL;
  if (OB_FAIL(get_path_type_no_follow(path, is_dir))) {
  } else if (!is_dir) {
    ret = OB_IO_ERROR;
    PALF_LOG(WARN, "path to clean is not a directory", K(ret), K(path));
  } else if (NULL == (dir = common::ob_opendir_utf8(path))) {
    ret = OB_ERR_SYS;
    PALF_LOG(WARN, "opendir failed", K(ret), K(path));
  } else {
    auto check_is_tmp_file_or_dir = [](const char* file_name) -> bool {
      return NULL != strstr(file_name, ".tmp");
    };
    char entry_name[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    int read_ret = 0;
    while (OB_SUCC(ret)
           && 1 == (read_ret = common::ob_readdir_utf8(dir, entry_name, sizeof(entry_name)))) {
      bool is_dir = false;
      char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
      if (0 == strcmp(entry_name, ".") || 0 == strcmp(entry_name, "..")) {
        // do nothing
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s", path, entry_name)) {
        ret = OB_ERR_UNEXPECTED;
        PALF_LOG(WARN, "snprintf failed", K(ret), K(current_file_path), K(path), K(entry_name));
      } else if (OB_FAIL(get_path_type_no_follow(current_file_path, is_dir))) {
      } else if (true == check_is_tmp_file_or_dir(current_file_path)) {
        if (true == is_dir && OB_FAIL(remove_directory_rec(current_file_path, log_block_pool))) {
          PALF_LOG(WARN, "delete_directory_rec failed", K(ret), K(entry_name), K(path));
        } else if (false == is_dir && OB_FAIL(remove_file_at(path, entry_name, log_block_pool))) {
          PALF_LOG(WARN, "delete_file failed", K(ret), K(current_file_path));
        } else {
        }
      } else if (true == is_dir && OB_FAIL(remove_tmp_file_or_directory_at(current_file_path, log_block_pool))) {
        PALF_LOG(WARN, "delete_tmp_file_or_directory_at failed", K(ret), K(current_file_path));
      } else {
      }
    }
    if (OB_SUCC(ret) && 0 > read_ret) {
      ret = convert_sys_errno();
      PALF_LOG(WARN, "read dir failed", K(ret), K(errno), K(path));
    }
  }
  if (NULL != dir) {
    common::ob_closedir_utf8(dir);
  }
  return ret;
}
} // end namespace oceanbase
} // end namespace palf
