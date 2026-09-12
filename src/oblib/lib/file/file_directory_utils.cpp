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
#include "lib/string/ob_sql_string.h"

#include <dirent.h>
#ifdef _WIN32
#include <windows.h>
#include "lib/file/windows_file_path.h"
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif
#else
#include <sys/statvfs.h>
#endif


namespace oceanbase
{
namespace common
{

//return true if filename is exists
int FileDirectoryUtils::is_exists(const char *file_path, bool &result)
{
#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath path(allocator);
  result = false;
  WIN32_FILE_ATTRIBUTE_DATA info = {};
  int ret = path.assign(file_path);
  if (OB_SUCC(ret)) {
    ret = path.get_info(info);
    if (ret == OB_FILE_NOT_EXIST) { ret = OB_SUCCESS; }
    else if (OB_SUCC(ret)) { result = true; }
  }
  if (OB_FAIL(ret)) {
    LIB_LOG(WARN, "Windows file operation failed", K(ret), KCSTRING(file_path), K(path.win32_error()));
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  result = false;
  ob_stat64_t file_info;
  if (OB_ISNULL(file_path) || OB_UNLIKELY(strlen(file_path) == 0)) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(file_path), K(ret));
  } else {
    result = (0 == ob_stat64(file_path, &file_info));
  }

  return ret;
#endif
}

int FileDirectoryUtils::check_directory_mode(const char *file_path, int mode, bool &result)
{
#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath path(allocator);
  result = false;
  int ret = path.assign(file_path);
  if (OB_SUCC(ret)) { ret = path.check_mode(mode, result); }
  if (OB_FAIL(ret)) {
    LIB_LOG(WARN, "Windows file operation failed", K(ret), KCSTRING(file_path), K(path.win32_error()));
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  result = false;
  if (OB_ISNULL(file_path) || OB_UNLIKELY(strlen(file_path) == 0)) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(file_path), K(ret));
  } else {
    if (0 == access(file_path, mode)) {
      result = true;
    } else {
      LIB_LOG(WARN, "access file failed", KERRMSG, K(file_path));
    }
  }
  return ret;
#endif
}
//return true if file is accessible
int FileDirectoryUtils::is_accessible(const char *file_path, bool &result)
{
  return check_directory_mode(file_path, R_OK, result);
}

int FileDirectoryUtils::is_writable(const char *file_path, bool &result)
{
  return check_directory_mode(file_path, W_OK, result);
}

//return ture if dirname is a directory
int FileDirectoryUtils::is_directory(const char *directory_path, bool &result)
{
#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath path(allocator);
  result = false;
  WIN32_FILE_ATTRIBUTE_DATA info = {};
  int ret = path.assign(directory_path);
  if (OB_SUCC(ret)) {
    ret = path.get_info(info);
    if (ret == OB_FILE_NOT_EXIST) { ret = OB_SUCCESS; }
    else if (OB_SUCC(ret)) { result = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }
  }
  if (OB_FAIL(ret)) {
    LIB_LOG(WARN, "Windows file operation failed", K(ret), KCSTRING(directory_path), K(path.win32_error()));
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  result = false;
  ob_stat64_t file_info;
  if (NULL == directory_path ||  strlen(directory_path) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(directory_path), K(ret));
  } else {
    result = (0 == ob_stat64(directory_path, &file_info) && S_ISDIR(file_info.st_mode));
  }

  return ret;
#endif
}

int FileDirectoryUtils::is_link(const char *link_path, bool &result)
{
#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath path(allocator);
  result = false;
  WIN32_FILE_ATTRIBUTE_DATA info = {};
  int ret = path.assign(link_path);
  if (OB_SUCC(ret)) {
    ret = path.get_info(info);
    if (ret == OB_FILE_NOT_EXIST) { ret = OB_SUCCESS; }
    else if (OB_SUCC(ret)) { result = (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0; }
  }
  if (OB_FAIL(ret)) {
    LIB_LOG(WARN, "Windows file operation failed", K(ret), KCSTRING(link_path), K(path.win32_error()));
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  if (NULL == link_path || strlen(link_path) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(link_path), K(ret));
  }  else {
    ob_stat64_t file_info;
    result = (0 == ob_lstat64(link_path, &file_info) && S_ISLNK(file_info.st_mode));
  }
  return ret;
#endif
}

//create the give dirname, return true on success or dirname exists
int FileDirectoryUtils::create_directory(const char *directory_path)
{
#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath path(allocator);
  int ret = path.assign(directory_path);
  if (OB_SUCC(ret)) { ret = path.create_directory(false); }
  if (OB_FAIL(ret)) {
    LIB_LOG(WARN, "create directory failed", K(ret), KCSTRING(directory_path), K(path.win32_error()));
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  mode_t umake_value = umask(0);
  umask(umake_value);
  mode_t mode = (S_IRWXUGO & (~umake_value)) | S_IWUSR | S_IXUSR;

  if (NULL == directory_path || strlen(directory_path) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(directory_path), K(ret));
  } else if (::mkdir(directory_path, mode) != 0) {
    if (EEXIST == errno) {
      ret = OB_SUCCESS;
    } else {
      ret = OB_IO_ERROR;
      LIB_LOG(WARN, "create directory failed.",
              KCSTRING(directory_path), K(errno), KERRMSG, K(ret));
    }
  }

  return ret;
#endif
}

//creates the full path of fullpath, return true on success
int FileDirectoryUtils::create_full_path(const char *fullpath)
{

#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath path(allocator);
  int ret = path.assign(fullpath);
  if (OB_SUCC(ret)) { ret = path.create_directory(true); }
  if (OB_FAIL(ret)) {
    LIB_LOG(WARN, "create full path failed", K(ret), KCSTRING(fullpath), K(path.win32_error()));
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  ob_stat64_t file_info;
  int64_t len = 0;
  if (NULL == fullpath || (len = strlen(fullpath)) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(fullpath), K(ret));
  } else {
    ret = ::ob_stat64(fullpath, &file_info);
    if (0 == ret) {
      if (!S_ISDIR(file_info.st_mode)) {
        ret = OB_ENTRY_EXIST;
        LIB_LOG(WARN, "file is exists but not a directory.", KCSTRING(fullpath), K(ret));
      } else {
        ret = OB_SUCCESS;
      }
    } else {
      ret = OB_SUCCESS;
      // path not exists.
      ObSqlString directory;
      if (OB_FAIL(directory.assign(fullpath, len))) { return ret; }
      char *dirpath = directory.ptr();
      char *path = dirpath;

      // skip leading char '/'
      while (*path == '/') path++;

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
#endif
}

//delete the given file, return true if filename exists
// return OB_SUCCESS on success;
int FileDirectoryUtils::delete_file(const char *filename)
{
#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath path(allocator);
  WIN32_FILE_ATTRIBUTE_DATA info = {};
  int ret = path.assign(filename);
  if (OB_SUCC(ret)) { ret = path.get_info(info); }
  if (OB_SUCC(ret)) {
    ret = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
        ? OB_FILE_NOT_EXIST : path.delete_file();
  }
  if (OB_FAIL(ret)) {
    LIB_LOG(WARN, "Windows file operation failed", K(ret), KCSTRING(filename), K(path.win32_error()));
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  ob_stat64_t file_info;
  if (NULL == filename || strlen(filename) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(filename), K(ret));
  } else {
    ret = ::ob_stat64(filename, &file_info);
    if (0 != ret) {
      ret = OB_FILE_NOT_EXIST;
      LIB_LOG(WARN, "file is not exists.", KCSTRING(filename), K(ret));
    } else if (S_ISDIR(file_info.st_mode)) {
      ret = OB_FILE_NOT_EXIST;
      LIB_LOG(WARN, "file is directory, use delete_directory.",
              KCSTRING(filename), K(ret));
    } else if (0 != unlink(filename)){
      ret = OB_IO_ERROR;
      LIB_LOG(WARN, "unlink file failed.",
              KCSTRING(filename), K(errno), KERRMSG, K(ret));
    }
  }
  return ret;
#endif
}

//delete the given directory and anything under it. Returns true on success
int FileDirectoryUtils::delete_directory(const char *dirname)
{
#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath path(allocator);
  WIN32_FILE_ATTRIBUTE_DATA info = {};
  int ret = path.assign(dirname);
  if (OB_SUCC(ret)) { ret = path.get_info(info); }
  if (OB_SUCC(ret)) {
    ret = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0
        ? OB_FILE_NOT_EXIST : path.delete_directory();
  }
  if (OB_FAIL(ret)) {
    LIB_LOG(WARN, "Windows file operation failed", K(ret), KCSTRING(dirname), K(path.win32_error()));
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  bool is_dir = false;
  if (NULL == dirname || strlen(dirname) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(dirname), K(ret));
  } else if (OB_FAIL(is_directory(dirname, is_dir))) {
  } else if (!is_dir) {
    ret = OB_FILE_NOT_EXIST;
    LIB_LOG(WARN, "file path is not a directory.", KCSTRING(dirname), K(ret));
  } else if (0 != rmdir(dirname)) {
    ret = OB_IO_ERROR;
    LIB_LOG(WARN, "rmdir failed.",
            KCSTRING(dirname), K(errno), KERRMSG, K(ret));
  }
  return ret;
#endif
}

//return the size of filename
int FileDirectoryUtils::get_file_size(const char *filename, int64_t &size)
{
#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath path(allocator);
  WIN32_FILE_ATTRIBUTE_DATA info = {};
  int ret = path.assign(filename);
  if (OB_SUCC(ret)) { ret = path.get_info(info); }
  if (OB_SUCC(ret)) {
    const uint64_t bytes = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) { ret = OB_FILE_NOT_EXIST; }
    else if (bytes > INT64_MAX) { ret = OB_SIZE_OVERFLOW; }
    else { size = static_cast<int64_t>(bytes); }
  }
  if (OB_FAIL(ret)) {
    LIB_LOG(WARN, "Windows file operation failed", K(ret), KCSTRING(filename), K(path.win32_error()));
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  ob_stat64_t file_info;
  if (NULL == filename || strlen(filename) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(filename), K(ret));
  } else {
    ret = ::ob_stat64(filename, &file_info);
    if (0 != ret) {
      ret = OB_FILE_NOT_EXIST;
      LIB_LOG(WARN, "file is not exists.", KCSTRING(filename), K(ret));
    } else if (S_ISDIR(file_info.st_mode)) {
      ret = OB_FILE_NOT_EXIST;
      LIB_LOG(WARN, "file is not a file.", KCSTRING(filename), K(ret));
    } else {
      size = file_info.st_size;
    }
  }
  return ret;
#endif
}

int FileDirectoryUtils::is_valid_path(const char *path, const bool print_error)
{
#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath native_path(allocator);
  int ret = native_path.assign(path);
  if (OB_FAIL(ret) && print_error) {
    LIB_LOG(WARN, "invalid Windows path", K(ret), KCSTRING(path), K(native_path.win32_error()));
  }
  return ret;
#else
  int ret = OB_SUCCESS;

  if (NULL == path) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "path must not null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && '\0' != path[i]; ++i) {
      char c = path[i];
      bool valid = isalnum(c) || '_' == c || '/' == c || '.' == c || '-' == c;
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
#endif
}

int FileDirectoryUtils::is_empty_directory(const char *directory_path, bool &result)
{
#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath path(allocator), child(allocator);
  WindowsDirectoryIterator iterator(allocator);
  result = false;
  DWORD attributes = 0;
  int ret = path.assign(directory_path);
  if (OB_SUCC(ret)) { ret = iterator.open(path); }
  if (OB_SUCC(ret)) {
    ret = iterator.next(child, attributes);
    if (ret == OB_ITER_END) { result = true; ret = OB_SUCCESS; }
  }
  if (OB_FAIL(ret)) {
    LIB_LOG(WARN, "enumerate Windows directory failed", K(ret), KCSTRING(directory_path),
        K(path.win32_error()), K(iterator.win32_error()));
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  DIR *dir = NULL;
  struct dirent *entry = NULL;
  int64_t num = 0;
  if (NULL == directory_path) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "directory path is NULL, ", K(ret));
  } else if (NULL == (dir = opendir(directory_path))) {
    ret = OB_ERR_SYS;
    LIB_LOG(WARN, "Fail to open dir, ", K(ret), K(errno), KCSTRING(directory_path));
  } else {
    while(NULL != (entry = readdir(dir))) {
      ++num;
    }
    if (2 == num) {
      result = true;
    } else {
      result = false;
    }
  }

  if (NULL != dir) {
    closedir(dir);
  }
  return ret;
#endif
}

int FileDirectoryUtils::open(const char *pathname, int flags, mode_t mode, int &fd)
{
#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath path(allocator);
  fd = -1;
  int ret = path.assign(pathname);
  if (OB_SUCC(ret)) { ret = path.open(flags, mode, fd); }
  if (OB_FAIL(ret)) {
    LIB_LOG(WARN, "Windows file operation failed", K(ret), KCSTRING(pathname), K(path.win32_error()));
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  if (NULL == pathname || strlen(pathname) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(pathname), K(ret));
  } else {
    fd = ::open(pathname, flags, mode);
    if (fd < 0) {
      ret = OB_IO_ERROR;
      LIB_LOG(WARN, "Fail to open", K(ret), K(errno), KCSTRING(pathname), K(mode), K(flags));
    }
  }
  return ret;
#endif
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
#ifdef _WIN32
    if (!CreateSymbolicLinkA(newpath, oldpath, 0)) {
#else
    if (0 != ::symlink(oldpath, newpath)) {
#endif
      ret = OB_IO_ERROR;
      LIB_LOG(WARN, "fail to symlink", K(ret), K(errno), KCSTRING(oldpath), KCSTRING(newpath));
    }
  }
  return ret;
}

int FileDirectoryUtils::unlink_symlink(const char *link_path)
{
#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath path(allocator);
  WIN32_FILE_ATTRIBUTE_DATA info = {};
  int ret = path.assign(link_path);
  if (OB_SUCC(ret)) { ret = path.get_info(info); }
  if (OB_SUCC(ret)) {
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ret = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
          ? path.delete_directory() : path.delete_file();
    }
  }
  if (OB_FAIL(ret)) {
    LIB_LOG(WARN, "Windows file operation failed", K(ret), KCSTRING(link_path), K(path.win32_error()));
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  bool is_link_file = false;
  if (NULL == link_path || strlen(link_path) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid arguments.", KCSTRING(link_path), K(ret));
  } else if (OB_FAIL(is_link(link_path, is_link_file))) {
  } else if (is_link_file) {
    if (0 != ::unlink(link_path)) {
      ret = OB_IO_ERROR;
      LIB_LOG(WARN, "fail to unlink", K(ret), K(errno), KERRMSG, KCSTRING(link_path));
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LIB_LOG(WARN, "not a symlink", K(ret), KCSTRING(link_path));
  }

  return ret;
#endif
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
    const char *path_name,
    int64_t &total_space,
    int64_t &free_space)
{
#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath path(allocator);
  total_space = free_space = 0;
  int ret = path.assign(path_name);
  if (OB_SUCC(ret)) { ret = path.get_disk_space(total_space, free_space); }
  if (OB_FAIL(ret)) {
    LIB_LOG(WARN, "Windows file operation failed", K(ret), KCSTRING(path_name), K(path.win32_error()));
  }
  return ret;
#else
  const char *path = path_name;
  int ret = OB_SUCCESS;
  total_space = 0;
  free_space = 0;

  if (OB_ISNULL(path)) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid args", K(ret), KP(path));
  } else {
    struct statvfs svfs;
    if (OB_FAIL(statvfs(path, &svfs))) {
      ret = OB_IO_ERROR;
      LIB_LOG(WARN, "get svfs fail", K(ret), KCSTRING(path), K(errno), KERRNOMSG(errno));
    } else {
      total_space =  (svfs.f_blocks + svfs.f_bavail - svfs.f_bfree) * svfs.f_bsize;
      free_space = svfs.f_bavail * svfs.f_bsize;
    }
  }
  return ret;
#endif
}

int FileDirectoryUtils::delete_directory_rec(const char *path)
{
#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath native_path(allocator);
  int ret = native_path.assign(path);
  if (OB_SUCC(ret)) { ret = native_path.remove_tree(false); }
  if (ret == OB_FILE_NOT_EXIST) { ret = OB_ENTRY_NOT_EXIST; }
  if (OB_FAIL(ret)) {
    LIB_LOG(WARN, "remove Windows directory contents failed", K(ret), KCSTRING(path),
        K(native_path.win32_error()));
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  DIR *dir = NULL;
  struct dirent *entry = nullptr;
  if (NULL == (dir = opendir(path))) {
    if (ENOENT != errno) {
      ret = OB_FILE_NOT_OPENED;
      LIB_LOG(WARN, "fail to open dir", K(ret), K(path), K(errno), KERRMSG);
    } else {
      ret = OB_ENTRY_NOT_EXIST;
      LIB_LOG(WARN, "dir does not exist", K(ret), K(path), K(errno), KERRMSG);
    }
  } else {
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    while ((entry = readdir(dir)) != NULL && OB_SUCC(ret)) {
      bool is_dir = false;
      MEMSET(current_file_path, '\0', OB_MAX_FILE_NAME_LENGTH);
      if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, "..")) {
        // do nothing
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s", path, entry->d_name)) {
        ret = OB_ERR_UNEXPECTED;
        LIB_LOG(WARN, "snprintf failed", K(ret), K(current_file_path), K(path), K(entry->d_name));
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
      } else if (true == is_dir && OB_FAIL(SMART_CALL(delete_directory_rec(current_file_path)))) {
        LIB_LOG(WARN, "delete directory failed", K(ret), K(entry->d_name), K(path));
        // delete normal file
      } else if (false == is_dir && OB_FAIL(FileDirectoryUtils::delete_file(current_file_path))) {
        LIB_LOG(WARN, "delete_file failed", K(ret), K(current_file_path));
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(delete_directory(path))) {
  }
  if (NULL != dir) {
    closedir(dir);
    dir = nullptr;
  }
  return ret;
#endif
}

int FileDirectoryUtils::delete_tmp_file_or_directory_at(const char *path)
{
#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath native_path(allocator);
  int ret = native_path.assign(path);
  if (OB_SUCC(ret)) { ret = native_path.remove_tree(true); }
  if (OB_FAIL(ret)) {
    LIB_LOG(WARN, "remove Windows directory contents failed", K(ret), KCSTRING(path),
        K(native_path.win32_error()));
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  DIR *dir = NULL;
  struct dirent *entry = nullptr;
  if (NULL == (dir = opendir(path))) {
    ret = OB_ERR_SYS;
    LIB_LOG(WARN, "opendir failed", K(path), K(errno), KERRMSG);
  } else {
    auto check_is_tmp_file = [](const char* file_name) -> bool {
      return NULL != strstr(file_name, ".tmp");
    };
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    while ((entry = readdir(dir)) != NULL && OB_SUCC(ret)) {
      bool is_dir = false;
      MEMSET(current_file_path, '\0', OB_MAX_FILE_NAME_LENGTH);
      if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, "..")) {
        // do nothing
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s", path, entry->d_name)) {
        ret = OB_ERR_UNEXPECTED;
        LIB_LOG(WARN, "snprintf failed", K(ret), K(current_file_path), K(path), K(entry->d_name));
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
      } else if (true == check_is_tmp_file(current_file_path)) {
        if (true == is_dir && OB_FAIL(delete_directory_rec(current_file_path))) {
          LIB_LOG(WARN, "delete_directory_rec failed", K(ret), K(entry->d_name), K(path));
        } else if (false == is_dir && OB_FAIL(FileDirectoryUtils::delete_file(current_file_path))) {
          LIB_LOG(WARN, "delete_file failed", K(ret), K(current_file_path));
        } else {
        }
      } else if (true == is_dir && OB_FAIL(delete_tmp_file_or_directory_at(current_file_path))) {
        LIB_LOG(WARN, "delete_tmp_file_or_directory_at failed", K(ret), K(current_file_path));
      } else {
      }
    }
  }
  if (NULL != dir) {
    closedir(dir);
  }
  return ret;
#endif
}

int FileDirectoryUtils::fsync_dir(const char *dir_path)
{
  int ret = OB_SUCCESS;
#ifdef _WIN32
  ObMalloc allocator("WindowsPath");
  WindowsFilePath path(allocator);
  HANDLE hDir = INVALID_HANDLE_VALUE;
  if (OB_FAIL(path.assign(dir_path))) {
    LIB_LOG(WARN, "normalize directory flush path failed", K(ret), K(dir_path), K(path.win32_error()));
  } else if (INVALID_HANDLE_VALUE == (hDir = CreateFileW(path.wide(), GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL))) {
    ret = OB_IO_ERROR;
    const DWORD error = GetLastError();
    LIB_LOG(WARN, "CreateFileW for dir failed", K(ret), K(dir_path), K(error));
  } else {
    if (!FlushFileBuffers(hDir)) {
      const DWORD error = GetLastError();
      ret = OB_IO_ERROR;
      LIB_LOG(WARN, "FlushFileBuffers for dir failed", K(ret), K(dir_path), K(error));
    }
    if (!CloseHandle(hDir)) {
      const DWORD error = GetLastError();
      ret = OB_IO_ERROR;
      LIB_LOG(WARN, "CloseHandle for dir failed", K(ret), K(dir_path), K(error));
    }
  }
#else
  int fd = ::open(dir_path, O_DIRECTORY | O_RDONLY);
  if (-1 == fd) {
    ret = OB_IO_ERROR;
    LIB_LOG(WARN, "::open failed", K(ret), K(dir_path), K(errno));
  } else if (-1 == ::fsync(fd)) {
    ret = OB_IO_ERROR;
    LIB_LOG(WARN, "::fsync failed", K(ret), K(dir_path), K(errno));
  } else {
  }
  if (-1 != fd) {
    ::close(fd);
  }
#endif
  return ret;
}

int FileDirectoryUtils::to_absolute_path(ObSqlString &dir)
{
#ifdef _WIN32
  int ret = OB_SUCCESS;
  if (!dir.empty()) {
    ObMalloc allocator("WindowsPath");
    WindowsFilePath path(allocator);
    if (OB_FAIL(path.assign(dir.ptr(), dir.length()))) {
      LIB_LOG(WARN, "normalize absolute path failed", K(ret), K(dir), K(path.win32_error()));
    } else if (OB_FAIL(dir.assign(path.utf8()))) {
    }
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  if (!dir.empty() && dir.ptr()[0] != '\0' && dir.ptr()[0] != '/') {
    char real_path[PATH_MAX] = {0};
    if (NULL == realpath(dir.ptr(), real_path)) {
      LIB_LOG(WARN, "Failed to get absolute path", K(dir), KCSTRING(strerror(errno)));
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(dir.assign(real_path))) {
    }
  }
  return ret;
#endif
}
}//end namespace common
}//end namespace oceanbase
