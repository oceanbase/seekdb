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

#include "lib/file/file_directory_utils.h"

#include "lib/utility/ob_smart_call.h"
#include "lib/utility/ob_hang_fatal_error.h"
#include "lib/utility/ob_platform_utils.h"  // Platform compatibility layer
#include "lib/utility/ob_platform_io.h"
#include "lib/string/ob_sql_string.h"

#include <limits.h>


namespace oceanbase
{
namespace common
{

//return true if filename is exists
int FileDirectoryUtils::is_exists(const char *file_path, bool &result)
{
  int ret = OB_SUCCESS;
  result = false;
  ObPlatformFileStat file_info;
  if (OB_ISNULL(file_path) || OB_UNLIKELY(strlen(file_path) == 0)) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(file_path), K(ret));
  } else {
    result = (0 == ob_stat_utf8(file_path, file_info));
  }

  return ret;
}

int FileDirectoryUtils::check_directory_mode(const char *file_path,
                                            ObPathPermission permission, bool &result)
{
  int ret = OB_SUCCESS;
  result = false;
  if (OB_ISNULL(file_path) || OB_UNLIKELY(strlen(file_path) == 0)) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(file_path), K(ret));
  } else {
    if (0 == ob_access_utf8(file_path, permission)) {
      result = true;
    } else {
      LIB_LOG(WARN, "access file failed", KERRMSG, K(file_path));
    }
  }
  return ret;
}
//return true if file is accessible
int FileDirectoryUtils::is_accessible(const char *file_path, bool &result)
{
  return check_directory_mode(file_path, ObPathPermission::READ, result);
}

int FileDirectoryUtils::is_writable(const char *file_path, bool &result)
{
  return check_directory_mode(file_path, ObPathPermission::WRITE, result);
}

//return ture if dirname is a directory
int FileDirectoryUtils::is_directory(const char *directory_path, bool &result)
{
  int ret = OB_SUCCESS;
  result = false;
  ObPlatformFileStat file_info;
  if (NULL == directory_path ||  strlen(directory_path) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(directory_path), K(ret));
  } else {
    result = (0 == ob_stat_utf8(directory_path, file_info) && ob_stat_is_dir(file_info.mode));
  }

  return ret;
}

int FileDirectoryUtils::is_link(const char *link_path, bool &result)
{
  int ret = OB_SUCCESS;
  if (NULL == link_path || strlen(link_path) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(link_path), K(ret));
  }  else {
    ObPlatformFileStat file_info;
    result = (0 == ob_stat_utf8(link_path, file_info, false) && ob_stat_is_link(file_info.mode));
  }
  return ret;
}

//create the give dirname, return true on success or dirname exists
int FileDirectoryUtils::create_directory(const char *directory_path)
{
  int ret = OB_SUCCESS;
  mode_t umake_value = umask(0);
  umask(umake_value);
  mode_t mode = (S_IRWXUGO & (~umake_value)) | S_IWUSR | S_IXUSR;

  if (NULL == directory_path || strlen(directory_path) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(directory_path), K(ret));
  } else if (ob_mkdir_utf8(directory_path, static_cast<int>(mode)) != 0) {
    if (EEXIST == errno) {
      ret = OB_SUCCESS;
    } else {
      ret = OB_IO_ERROR;
      LIB_LOG(WARN, "create directory failed.",
              KCSTRING(directory_path), K(errno), KERRMSG, K(ret));
    }
  }

  return ret;
}

//creates the full path of fullpath, return true on success
int FileDirectoryUtils::create_full_path(const char *fullpath)
{

  int ret = OB_SUCCESS;
  ObPlatformFileStat file_info;
  int64_t len = 0;
  if (NULL == fullpath || (len = strlen(fullpath)) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(fullpath), K(ret));
  } else {
    ret = ob_stat_utf8(fullpath, file_info);
    if (0 == ret) {
      if (!ob_stat_is_dir(file_info.mode)) {
        ret = OB_ENTRY_EXIST;
        LIB_LOG(WARN, "file is exists but not a directory.", KCSTRING(fullpath), K(ret));
      } else {
        ret = OB_SUCCESS;
      }
    } else {
      ret = OB_SUCCESS;
      // path not exists.
      char dirpath[MAX_PATH + 1];
      strncpy(dirpath, fullpath, len);
      dirpath[len] = '\0';
#ifdef _WIN32
      for (int64_t i = 0; i < len; ++i) {
        if (dirpath[i] == '\\') dirpath[i] = '/';
      }
#endif
      char *path = dirpath;

      // skip leading char '/'
      while (*path == '/') path++;
#ifdef _WIN32
      // skip drive letter like "C:/"
      if (len >= 3 && isalpha(dirpath[0]) && dirpath[1] == ':' && dirpath[2] == '/') {
        path = dirpath + 3;
      }
#endif

      while (OB_SUCC(ret)) {
        path = strchr(path, '/');
        if (NULL == path) {
          break;
        }

        *path = '\0';
        if (OB_FAIL(create_directory(dirpath))) {
        } else {
          *path++ = '/';
          // skip '/'
          while (*path == '/') path++;
        }
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(create_directory(dirpath))) {
        }
      }
    }
  }

  return ret;
}

//delete the given file, return true if filename exists
// return OB_SUCCESS on success;
int FileDirectoryUtils::delete_file(const char *filename)
{
  int ret = OB_SUCCESS;
  ObPlatformFileStat file_info;
  if (NULL == filename || strlen(filename) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(filename), K(ret));
  } else {
    ret = ob_stat_utf8(filename, file_info);
    if (0 != ret) {
      ret = OB_FILE_NOT_EXIST;
      LIB_LOG(WARN, "file is not exists.", KCSTRING(filename), K(ret));
    } else if (ob_stat_is_dir(file_info.mode)) {
      ret = OB_FILE_NOT_EXIST;
      LIB_LOG(WARN, "file is directory, use delete_directory.",
              KCSTRING(filename), K(ret));
    } else if (0 != ob_unlink_utf8(filename)) {
      ret = OB_IO_ERROR;
      LIB_LOG(WARN, "unlink file failed.",
              KCSTRING(filename), K(errno), KERRMSG, K(ret));
    }
  }
  return ret;
}

//delete the given directory and anything under it. Returns true on success
int FileDirectoryUtils::delete_directory(const char *dirname)
{
  int ret = OB_SUCCESS;
  bool is_dir = false;
  if (NULL == dirname || strlen(dirname) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(dirname), K(ret));
  } else if (OB_FAIL(is_directory(dirname, is_dir))) {
  } else if (!is_dir) {
    ret = OB_FILE_NOT_EXIST;
    LIB_LOG(WARN, "file path is not a directory.", KCSTRING(dirname), K(ret));
  } else if (0 != ob_rmdir_utf8(dirname)) {
    ret = OB_IO_ERROR;
    LIB_LOG(WARN, "rmdir failed.",
            KCSTRING(dirname), K(errno), KERRMSG, K(ret));
  }
  return ret;
}

//return the size of filename
int FileDirectoryUtils::get_file_size(const char *filename, int64_t &size)
{
  int ret = OB_SUCCESS;
  ObPlatformFileStat file_info;
  if (NULL == filename || strlen(filename) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(filename), K(ret));
  } else {
    ret = ob_stat_utf8(filename, file_info);
    if (0 != ret) {
      ret = OB_FILE_NOT_EXIST;
      LIB_LOG(WARN, "file is not exists.", KCSTRING(filename), K(ret));
    } else if (ob_stat_is_dir(file_info.mode)) {
      ret = OB_FILE_NOT_EXIST;
      LIB_LOG(WARN, "file is not a file.", KCSTRING(filename), K(ret));
    } else {
      size = file_info.size;
    }
  }
  return ret;
}

int FileDirectoryUtils::is_valid_path(const char *path, const bool print_error)
{
  int ret = OB_SUCCESS;

  if (NULL == path) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "path must not null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && '\0' != path[i]; ++i) {
      char c = path[i];
      bool valid = isalnum(c) || '_' == c || '/' == c || '.' == c || '-' == c;
#ifdef _WIN32
      valid = valid || '\\' == c || ':' == c;
#endif
      if (!valid) {
        ret = OB_INVALID_ARGUMENT;
        if (print_error) {
          LIB_LOG(WARN, "invalid path", K(ret), K(i), K(path[i]), KCSTRING(path));
        }
        break;
      }
    }
  }
  return ret;
}

int FileDirectoryUtils::is_empty_directory(const char *directory_path, bool &result)
{
  int ret = OB_SUCCESS;
  ObPlatformDir *dir = NULL;
  char entry_name[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  int64_t num = 0;
  int read_ret = 0;
  if (NULL == directory_path) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "directory path is NULL, ", K(ret));
  } else if (NULL == (dir = ob_opendir_utf8(directory_path))) {
    ret = OB_ERR_SYS;
    LIB_LOG(WARN, "Fail to open dir, ", K(ret), K(errno), KCSTRING(directory_path));
  } else {
    while (1 == (read_ret = ob_readdir_utf8(dir, entry_name, sizeof(entry_name)))) {
      ++num;
    }
    if (read_ret < 0) {
      ret = OB_ERR_SYS;
      LIB_LOG(WARN, "Fail to read dir, ", K(ret), K(errno), KCSTRING(directory_path));
    } else {
      result = (2 == num);
    }
  }

  if (NULL != dir) {
    ob_closedir_utf8(dir);
  }
  return ret;
}

int FileDirectoryUtils::open(const char *pathname, int flags, mode_t mode, int &fd)
{
  int ret = OB_SUCCESS;
  if (NULL == pathname || strlen(pathname) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(pathname), K(ret));
  } else {
    fd = ob_open_utf8(pathname, flags, static_cast<int>(mode));
    if (fd < 0) {
      ret = OB_IO_ERROR;
      LIB_LOG(WARN, "Fail to open", K(ret), K(errno), KCSTRING(pathname), K(mode), K(flags));
    }
  }
  return ret;
}

int FileDirectoryUtils::close(const int fd)
{
  int ret = OB_SUCCESS;
  if (fd < 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", K(fd), K(ret));
  } else {
    if (0 != ::close(fd)) {
      ret = OB_IO_ERROR;
      LIB_LOG(WARN, "fail to close", K(ret), K(errno), K(fd));
    }
  }

  return ret;
}

int FileDirectoryUtils::symlink(const char *oldpath, const char *newpath)
{
  int ret = OB_SUCCESS;
  if (NULL == oldpath || strlen(oldpath) == 0 || NULL == newpath || strlen(newpath) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "path must not null", K(ret), KCSTRING(oldpath), KCSTRING(newpath));
  } else {
    if (0 != ob_symlink_utf8(oldpath, newpath)) {
      ret = OB_IO_ERROR;
      LIB_LOG(WARN, "fail to symlink", K(ret), K(errno), KCSTRING(oldpath), KCSTRING(newpath));
    }
  }
  return ret;
}

int FileDirectoryUtils::unlink_symlink(const char *link_path)
{
  int ret = OB_SUCCESS;
  bool is_link_file = false;
  if (NULL == link_path || strlen(link_path) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(link_path), K(ret));
  } else if (OB_FAIL(is_link(link_path, is_link_file))) {
  } else if (is_link_file) {
    if (0 != ob_unlink_utf8(link_path)) {
      ret = OB_IO_ERROR;
      LIB_LOG(WARN, "fail to unlink", K(ret), K(errno), KERRMSG, KCSTRING(link_path));
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LIB_LOG(WARN, "not a symlink", K(ret), KCSTRING(link_path));
  }

  return ret;
}

int FileDirectoryUtils::dup_fd(const int fd, int &dup_fd)
{
  int ret = OB_SUCCESS;
  if (fd < 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", K(fd), K(ret));
  } else if (0  > (dup_fd = ::dup(fd))) {
    ret = OB_ERR_UNEXPECTED;
    LIB_LOG(WARN, "fail to dup", K(ret), K(fd), K(errno), KERRMSG);
  }
  return ret;
}

int FileDirectoryUtils::get_disk_space(
    const char *path,
    int64_t &total_space,
    int64_t &free_space)
{
  int ret = OB_SUCCESS;
  total_space = 0;
  free_space = 0;

  if (OB_ISNULL(path)) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid args", K(ret), KP(path));
  } else if (0 != ob_get_disk_space_utf8(path, total_space, free_space)) {
    ret = OB_IO_ERROR;
    LIB_LOG(WARN, "get disk space fail", K(ret), K(errno), KERRNOMSG(errno), KCSTRING(path));
  }
  return ret;
}

int FileDirectoryUtils::delete_directory_rec(const char *path)
{
  int ret = OB_SUCCESS;
  ObPlatformDir *dir = NULL;
  char entry_name[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  int read_ret = 0;
  if (NULL == (dir = ob_opendir_utf8(path))) {
    if (ENOENT != errno) {
      ret = OB_FILE_NOT_OPENED;
      LIB_LOG(WARN, "fail to open dir", K(ret), K(path), K(errno), KERRMSG);
    } else {
      ret = OB_ENTRY_NOT_EXIST;
      LIB_LOG(WARN, "dir does not exist", K(ret), K(path), K(errno), KERRMSG);
    }
  } else {
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    while (OB_SUCC(ret)) {
      read_ret = ob_readdir_utf8(dir, entry_name, sizeof(entry_name));
      if (1 != read_ret) {
        break;
      }
      bool is_dir = false;
      MEMSET(current_file_path, '\0', OB_MAX_FILE_NAME_LENGTH);
      if (0 == strcmp(entry_name, ".") || 0 == strcmp(entry_name, "..")) {
        // do nothing
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s", path, entry_name)) {
        ret = OB_ERR_UNEXPECTED;
        LIB_LOG(WARN, "snprintf failed", K(ret), K(current_file_path), K(path), K(entry_name));
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
      } else if (true == is_dir && OB_FAIL(SMART_CALL(delete_directory_rec(current_file_path)))) {
        LIB_LOG(WARN, "delete directory failed", K(ret), K(entry_name), K(path));
        // delete normal file
      } else if (false == is_dir && OB_FAIL(FileDirectoryUtils::delete_file(current_file_path))) {
        LIB_LOG(WARN, "delete_file failed", K(ret), K(current_file_path));
      }
    }
    if (OB_SUCC(ret) && read_ret < 0) {
      ret = OB_ERR_SYS;
      LIB_LOG(WARN, "fail to read dir", K(ret), K(path), K(errno), KERRMSG);
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(delete_directory(path))) {
  }
  if (NULL != dir) {
    ob_closedir_utf8(dir);
    dir = NULL;
  }
  return ret;
}

int FileDirectoryUtils::delete_tmp_file_or_directory_at(const char *path)
{
  int ret = OB_SUCCESS;
  ObPlatformDir *dir = NULL;
  char entry_name[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  int read_ret = 0;
  if (NULL == (dir = ob_opendir_utf8(path))) {
    ret = OB_ERR_SYS;
    LIB_LOG(WARN, "opendir failed", K(path), K(errno), KERRMSG);
  } else {
    auto check_is_tmp_file = [](const char *file_name) -> bool {
      return NULL != strstr(file_name, ".tmp");
    };
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    while (OB_SUCC(ret)) {
      read_ret = ob_readdir_utf8(dir, entry_name, sizeof(entry_name));
      if (1 != read_ret) {
        break;
      }
      bool is_dir = false;
      MEMSET(current_file_path, '\0', OB_MAX_FILE_NAME_LENGTH);
      if (0 == strcmp(entry_name, ".") || 0 == strcmp(entry_name, "..")) {
        // do nothing
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s", path, entry_name)) {
        ret = OB_ERR_UNEXPECTED;
        LIB_LOG(WARN, "snprintf failed", K(ret), K(current_file_path), K(path), K(entry_name));
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
      } else if (true == check_is_tmp_file(current_file_path)) {
        if (true == is_dir && OB_FAIL(delete_directory_rec(current_file_path))) {
          LIB_LOG(WARN, "delete_directory_rec failed", K(ret), K(entry_name), K(path));
        } else if (false == is_dir && OB_FAIL(FileDirectoryUtils::delete_file(current_file_path))) {
          LIB_LOG(WARN, "delete_file failed", K(ret), K(current_file_path));
        } else {
        }
      } else if (true == is_dir && OB_FAIL(delete_tmp_file_or_directory_at(current_file_path))) {
        LIB_LOG(WARN, "delete_tmp_file_or_directory_at failed", K(ret), K(current_file_path));
      } else {
      }
    }
    if (OB_SUCC(ret) && read_ret < 0) {
      ret = OB_ERR_SYS;
      LIB_LOG(WARN, "fail to read dir", K(ret), K(path), K(errno), KERRMSG);
    }
  }
  if (NULL != dir) {
    ob_closedir_utf8(dir);
  }
  return ret;
}

int FileDirectoryUtils::fsync_dir(const char *dir_path)
{
  int ret = OB_SUCCESS;
  if (0 != ob_fsync_dir_utf8(dir_path)) {
    ret = OB_IO_ERROR;
    LIB_LOG(WARN, "fsync dir failed", K(ret), K(errno), KERRMSG, K(dir_path));
  }
  return ret;
}

int FileDirectoryUtils::to_absolute_path(ObSqlString &dir)
{
  int ret = OB_SUCCESS;
  if (!dir.empty() && dir.ptr()[0] != '\0' && !ob_is_absolute_path_utf8(dir.ptr())) {
    char real_path[4096] = {0};
    if (0 != ob_absolute_path_utf8(dir.ptr(), real_path, sizeof(real_path))) {
      LIB_LOG(WARN, "Failed to get absolute path", K(dir), K(errno), KCSTRING(strerror(errno)));
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(dir.assign(real_path))) {
    }
  }
  return ret;
}
}//end namespace common
}//end namespace oceanbase
