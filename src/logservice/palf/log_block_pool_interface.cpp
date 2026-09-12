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
#ifdef _WIN32
#include "share/ob_errno.h"
#endif
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <windows.h>
#include "lib/file/windows_file_path.h"
#include "lib/allocator/page_arena.h"
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif
#endif

namespace oceanbase
{
namespace palf
{

int is_block_used_for_palf(const int fd, const char *path, bool &result)
{
  int ret = OB_SUCCESS;
  result = false;
#ifdef _WIN32
  if (fd < 0) { return OB_INVALID_ARGUMENT; }
  common::ObArenaAllocator allocator;
  common::WindowsFilePath resolved(allocator);
  if (OB_FAIL(resolved.assign_at(reinterpret_cast<HANDLE>(_get_osfhandle(fd)), path))) {
    PALF_LOG(ERROR, "resolve block directory handle failed", K(ret), K(fd), K(path),
        "win32_error", resolved.win32_error());
    return ret;
  }
  struct _stat64 st;
  if (-1 == ::_wstat64(resolved.wide(), &st)) {
#else
  struct stat st;
  if (-1 == ::fstatat(fd, path, &st, 0)) {
#endif
    ret = convert_sys_errno();
    PALF_LOG(ERROR, "::fstat failed", K(ret), K(path), K(errno));
  } else if (st.st_size == PALF_PHY_BLOCK_SIZE) {
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
  bool result = false;
  if (-1 == fd) {
    ret = convert_sys_errno();
    PALF_LOG(ERROR, "open_directory failed", K(ret), K(dir));
  } else if (OB_FAIL(log_block_pool->remove_block_at(fd, path))) {
  } else {
    PALF_LOG(INFO, "remove_file_at success", K(dir), K(path));
  }

  if (-1 != fd) {
#ifdef _WIN32
    const int sync_ret = fsync_with_retry(fd);
    if (OB_SUCCESS == ret) { ret = sync_ret; }
    if (0 != _close(fd) && OB_SUCCESS == ret) { ret = convert_sys_errno(); }
#else
    ::fsync(fd);
    ::close(fd);
#endif
  }
  return ret;
}

#ifdef _WIN32
static int remove_windows_palf_entries(const char *path, ILogBlockPool *pool, bool temporary_only)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator allocator;
  common::WindowsFilePath directory(allocator), child(allocator);
  common::WindowsDirectoryIterator iterator(allocator);
  if (NULL == pool) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(directory.assign(path))) {
  } else if (OB_FAIL(iterator.open(directory))) {
  } else {
    DWORD attributes = 0;
    while (OB_SUCC(ret)) {
      ret = iterator.next(child, attributes);
      if (OB_ITER_END == ret) { ret = OB_SUCCESS; break; }
      if (OB_FAIL(ret)) { break; }
      const char *name = child.utf8();
      for (const char *cursor = name; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') { name = cursor + 1; }
      }
      const bool selected = !temporary_only || NULL != strstr(child.utf8(), ".tmp");
      if (0 != (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        // Do not follow a replacement directory outside the PALF tree.
        ret = OB_NOT_SUPPORTED;
      } else if (0 != (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        ret = selected ? remove_directory_rec(child.utf8(), pool)
                       : remove_tmp_file_or_directory_at(child.utf8(), pool);
      } else if (selected) {
        ret = remove_file_at(directory.utf8(), name, pool);
      }
    }
  }
  return ret;
}
#endif

int remove_directory_rec(const char *path, ILogBlockPool *log_block_pool)
{
  int ret = OB_SUCCESS;
#ifdef _WIN32
  if (OB_SUCC(ret = remove_windows_palf_entries(path, log_block_pool, false))) {
    common::ObArenaAllocator allocator;
    common::WindowsFilePath directory(allocator);
    if (OB_SUCC(ret = directory.assign(path))) {
      ret = directory.delete_directory();
      if (OB_FAIL(ret) && ERROR_ACCESS_DENIED == directory.win32_error()) {
        ret = OB_FILE_OR_DIRECTORY_PERMISSION_DENIED;
      }
    }
  }
#else
  DIR *dir = NULL;
  struct dirent *entry = NULL;
  if (NULL == (dir = opendir(path))) {
    ret = convert_sys_errno();
    PALF_LOG(WARN, "opendir failed", K(path));
  } else {
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    while ((entry = readdir(dir)) != NULL && OB_SUCC(ret)) {
      bool is_dir = false;
      MEMSET(current_file_path, '\0', OB_MAX_FILE_NAME_LENGTH);
      if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, "..")) {
        // do nothing
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s", path, entry->d_name)) {
        ret = OB_ERR_UNEXPECTED;
        PALF_LOG(WARN, "snprintf failed", K(ret), K(current_file_path), K(path), K(entry->d_name));
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
      } else if (true == is_dir && OB_FAIL(remove_directory_rec(current_file_path, log_block_pool))) {
        PALF_LOG(WARN, "remove directory failed", K(ret), K(entry->d_name), K(path));
        // delete normal file
      } else if (false == is_dir && OB_FAIL(remove_file_at(path, entry->d_name, log_block_pool))) {
        PALF_LOG(WARN, "remove_file_at failed", K(ret), K(current_file_path));
      } else {
        PALF_LOG(INFO, "remove directory or file success", K(path), K(current_file_path));
      }
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(FileDirectoryUtils::delete_directory(path))) {
    PALF_LOG(WARN, "delete_directory failed", K(ret), K(path));
  }
  if (NULL != dir) {
    closedir(dir);
  }
#endif
  return ret;
}

int remove_tmp_file_or_directory_at(const char *path, ILogBlockPool *log_block_pool)
{
  int ret = OB_SUCCESS;
#ifdef _WIN32
  ret = remove_windows_palf_entries(path, log_block_pool, true);
#else
  DIR *dir = NULL;
  struct dirent *entry = NULL;
  if (NULL == (dir = opendir(path))) {
    ret = OB_ERR_SYS;
    PALF_LOG(WARN, "opendir failed", K(path));
  } else {
    auto check_is_tmp_file_or_dir = [](const char* file_name) -> bool {
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
        PALF_LOG(WARN, "snprintf failed", K(ret), K(current_file_path), K(path), K(entry->d_name));
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
      } else if (true == check_is_tmp_file_or_dir(current_file_path)) {
        if (true == is_dir && OB_FAIL(remove_directory_rec(current_file_path, log_block_pool))) {
          PALF_LOG(WARN, "delete_directory_rec failed", K(ret), K(entry->d_name), K(path));
        } else if (false == is_dir && OB_FAIL(remove_file_at(path, entry->d_name, log_block_pool))) {
          PALF_LOG(WARN, "delete_file failed", K(ret), K(current_file_path));
        } else {
        }
      } else if (true == is_dir && OB_FAIL(remove_tmp_file_or_directory_at(current_file_path, log_block_pool))) {
        PALF_LOG(WARN, "delete_tmp_file_or_directory_at failed", K(ret), K(current_file_path));
      } else {
      }
    }
  }
  if (NULL != dir) {
    closedir(dir);
  }
#endif
  return ret;
}
} // end namespace oceanbase
} // end namespace palf
