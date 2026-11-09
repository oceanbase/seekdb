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

#include "log_io_utils.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "lib/utility/ob_platform_io.h"
#include "share/ob_errno.h"
#include "logservice/ob_server_log_block_mgr.h"

namespace oceanbase
{
namespace palf
{

const int64_t RETRY_INTERVAL = 10*1000;

int open_directory(const char *dir_path)
{
  return common::ob_open_dir_utf8(dir_path);
}

int close_with_ret(const int fd)
{
  int ret = OB_SUCCESS;
  if (-1 == fd) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "invalid argument", K(fd));
  } else if (-1 == (::close(fd))) {
    ret = convert_sys_errno();
    PALF_LOG(ERROR, "close block failed", K(ret), K(errno), K(fd));
  } else {
  }
  return ret;
}

int check_file_exist(const char *file_name,
                     bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;
  common::ObPlatformFileStat file_info;
  if (OB_ISNULL(file_name) || OB_UNLIKELY(strlen(file_name) == 0)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid arguments.", KCSTRING(file_name), K(ret));
  } else {
    exist = (0 == common::ob_stat_utf8(file_name, file_info));
  }
  return ret;
}

int check_file_exist(const int dir_fd,
                     const char *file_name,
                     bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;
  common::ObPlatformFileStat file_info;
  if (OB_ISNULL(file_name) || OB_UNLIKELY(strlen(file_name) == 0)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid arguments.", KCSTRING(file_name), K(ret));
  } else {
    exist = (0 == common::ob_statat_utf8(dir_fd, file_name, file_info, true));
  }
  return ret;
}

bool check_rename_success(const char *src_name,
                          const char *dest_name)
{
  bool bool_ret = false;
  bool src_exist = false;
  bool dest_exist = false;
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_file_exist(src_name, src_exist))) {
  } else if (!src_exist && OB_FAIL(check_file_exist(dest_name, dest_exist))) {
    PALF_LOG(WARN, "check_file_exist failed", KR(ret), K(src_name), K(dest_name));
  } else if (!src_exist && dest_exist) {
    bool_ret = true;
    PALF_LOG(INFO, "check_rename_success return true",
             KR(ret), K(src_name), K(dest_name), K(src_exist), K(dest_exist));
  } else {
    bool_ret = false;
    LOG_DBA_ERROR(OB_ERR_UNEXPECTED, "msg", "rename file failed, unexpected error",
                  KR(ret), K(errno), K(src_name), K(dest_name), K(src_exist), K(dest_exist));
  }
  return bool_ret;
}

bool check_renameat_success(const int src_dir_fd,
                            const char *src_name,
                            const int dest_dir_fd,
                            const char *dest_name)
{
  bool bool_ret = false;
  bool src_exist = false;
  bool dest_exist = false;
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_file_exist(src_dir_fd, src_name, src_exist))) {
  } else if (!src_exist && OB_FAIL(check_file_exist(dest_dir_fd, dest_name, dest_exist))) {
    PALF_LOG(WARN, "check_file_exist failed", KR(ret), K(src_name), K(dest_name));
  } else if (!src_exist && dest_exist) {
    bool_ret = true;
    PALF_LOG(INFO, "check_renameat_success return true",
             KR(ret), K(src_name), K(dest_name), K(src_dir_fd), K(dest_dir_fd), K(src_exist), K(dest_exist));
  } else {
    bool_ret = false;
    LOG_DBA_ERROR(OB_ERR_UNEXPECTED, "msg", "renameat file failed, unexpected error",
                  KR(ret), K(errno), K(src_name), K(dest_name), K(src_exist), K(dest_exist));
  }
  return bool_ret;
}

int rename_with_retry(const char *src_name,
                      const char *dest_name)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(src_name) || OB_ISNULL(dest_name)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", KP(src_name), KP(dest_name));
  } else {
    do {
      if (-1 == common::ob_rename_utf8(src_name, dest_name)) {
        ret  = convert_sys_errno();
        LOG_DBA_WARN(OB_IO_ERROR, "msg", "rename file failed",
                     KR(ret), K(errno), K(src_name), K(dest_name));
        // for xfs, source file not exist and dest file exist after rename return ENOSPC, therefore, next rename will return
        // OB_NO_SUCH_FILE_OR_DIRECTORY, however, for some reason, we can not return OB_SUCCESS when rename return OB_NO_SUCH_FILE_OR_DIRECTORY.
        // consider that, if file names with 'src_name' has been delted by human and file names with 'dest_name' not exist.
        if (OB_NO_SUCH_FILE_OR_DIRECTORY == ret && check_rename_success(src_name, dest_name)) {
          ret = OB_SUCCESS;
          break;
        }
        ob_usleep(RETRY_INTERVAL);
      } else {
        ret = OB_SUCCESS;
        break;
      }
    } while(OB_FAIL(ret));
  }
  return ret;
}

int renameat_with_retry(const int src_dir_fd,
                        const char *src_name,
                        const int dest_dir_fd,
                        const char *dest_name)
{
  int ret = OB_SUCCESS;
  if (src_dir_fd < 0 || OB_ISNULL(src_name)
      || dest_dir_fd < 0 || OB_ISNULL(dest_name)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", KP(src_name), KP(dest_name));
  } else {
    do {
      if (-1 == common::ob_renameat_utf8(src_dir_fd, src_name, dest_dir_fd, dest_name)) {
        ret  = convert_sys_errno();
        LOG_DBA_WARN(OB_IO_ERROR, "msg", "renameat file failed",
                     KR(ret), K(errno), K(src_name), K(dest_name), K(src_dir_fd), K(dest_dir_fd));
        // for xfs, source file not exist and dest file exist after renameat return ENOSPC, therefore, next renameat will return
        // OB_NO_SUCH_FILE_OR_DIRECTORY, however, for some reason, we can not return OB_SUCCESS when renameat return OB_NO_SUCH_FILE_OR_DIRECTORY.
        // consider that, if file names with 'src_name' has been delted by human and file names with 'dest_name' not exist.
        if (OB_NO_SUCH_FILE_OR_DIRECTORY == ret && check_renameat_success(src_dir_fd, src_name, dest_dir_fd, dest_name)) {
          ret = OB_SUCCESS;
          break;
        }
        ob_usleep(RETRY_INTERVAL);
      } else {
        ret = OB_SUCCESS;
        break;
      }
    } while(OB_FAIL(ret));
  }
  return ret;
}

int fsync_with_retry(const int dir_fd)
{
  int ret = OB_SUCCESS;
  do {
    if (-1 == common::ob_fsync_fd(dir_fd)) {
      ret = convert_sys_errno();
      CLOG_LOG(ERROR, "fsync dest dir failed", K(ret), K(dir_fd));
      ob_usleep(RETRY_INTERVAL);
    } else {
      ret = OB_SUCCESS;
      break;
    }
  } while (OB_FAIL(ret));
  return ret;

}

int scan_dir(const char *dir_name, ObBaseDirFunctor &functor)
{
  int ret = OB_SUCCESS;
  common::ObPlatformDir *open_dir = NULL;

  if (OB_ISNULL(dir_name)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", K(ret), K(dir_name));
  } else if (OB_ISNULL(open_dir = common::ob_opendir_utf8(dir_name))) {
    if (ENOENT != errno) {
      ret = OB_FILE_NOT_OPENED;
      PALF_LOG(WARN, "Fail to open dir, ", K(ret), K(dir_name));
    } else {
      ret = OB_NO_SUCH_FILE_OR_DIRECTORY;
      PALF_LOG(WARN, "dir does not exist", K(ret), K(dir_name));
    }
  } else {
    char entry_name[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    int read_ret = 0;
    while (OB_SUCC(ret)
           && 1 == (read_ret = common::ob_readdir_utf8(open_dir, entry_name,
                                                       sizeof(entry_name)))) {
      if (0 == STRCMP(entry_name, ".") || 0 == STRCMP(entry_name, "..")) {
      } else if (OB_FAIL((functor.func)(entry_name))) {
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

int GetBlockCountFunctor::func(const char *entry_name)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(entry_name)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid args", K(ret), KP(entry_name));
  } else if (false == is_number(entry_name)) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(WARN, "this is block is not used for palf!!!", K(ret), K(entry_name));
  } else {
    count_ ++;
  }
  return ret;
}

int TrimLogDirectoryFunctor::func(const char *entry_name)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(entry_name)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid args", K(ret), KP(entry_name));
  } else if (false == is_number(entry_name)) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(WARN, "this is block is not used for palf!!!", K(ret), K(entry_name));
  } else {
    uint32_t block_id = static_cast<uint32_t>(strtol(entry_name, nullptr, 10));
    if (LOG_INVALID_BLOCK_ID == min_block_id_ || block_id < min_block_id_) {
      min_block_id_ = block_id;
    }
    if (LOG_INVALID_BLOCK_ID == max_block_id_ || block_id > max_block_id_) {
      max_block_id_ = block_id;
    }
  }
  return ret;
}

} // end namespace palf
} // end namespace oceanbase
