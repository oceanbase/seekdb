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

#define USING_LOG_PREFIX CLOG
#include "ob_server_log_block_mgr.h"
#include <regex>
#ifdef __APPLE__
#include <fcntl.h>                              // For fcntl, F_PREALLOCATE on macOS
#include <unistd.h>                             // For ftruncate
#elif defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <direct.h>
#include <windows.h>
#include "lib/file/windows_file_path.h"
#include "lib/allocator/page_arena.h"
static bool ob_resolve_path_at2(int dir_fd, const char *relative,
    oceanbase::common::WindowsFilePath &path) {
  if (dir_fd < 0) { errno = EBADF; return false; }
  const int ret = path.assign_at(reinterpret_cast<HANDLE>(_get_osfhandle(dir_fd)), relative);
  if (ret != 0) { errno = path.error_to_errno(ret); }
  return ret == 0;
}
static int openat(int dir_fd, const char *name, int flags, ...) {
  const int mode = (flags & _O_CREAT) ? _S_IREAD | _S_IWRITE : 0;
  oceanbase::common::ObArenaAllocator allocator;
  oceanbase::common::WindowsFilePath path(allocator);
  return ob_resolve_path_at2(dir_fd, name, path) ? ::_wopen(path.wide(), flags | _O_BINARY, mode) : -1;
}
static int unlinkat(int dir_fd, const char *name, int flag) {
  oceanbase::common::ObArenaAllocator allocator;
  oceanbase::common::WindowsFilePath path(allocator);
  if (!ob_resolve_path_at2(dir_fd, name, path)) { return -1; }
  return flag ? ::_wrmdir(path.wide()) : ::_wunlink(path.wide());
}
// MSVCRT typedefs `off_t` as 32-bit `long`. Use int64_t explicitly so log
// block file sizes can exceed 2 GiB on Windows.
static int fallocate(int fd, int, int64_t, int64_t len) {
  const errno_t error = ::_chsize_s(fd, len);
  if (0 != error) {
    errno = error;
    return -1;
  }
  return 0;
}
#endif
#include "logservice/ob_log_service.h"          // ObLogService

#define BYTE_TO_MB(byte) (byte+1024*1024-1)/1024/1024

namespace oceanbase
{
using namespace palf;
using namespace share;
namespace logservice
{

#ifdef _WIN32
namespace
{
template <typename Visitor>
int scan_windows_log_pool_directory(const char *path, Visitor visitor)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator allocator;
  common::WindowsFilePath directory(allocator), child(allocator);
  common::WindowsDirectoryIterator iterator(allocator);
  if (OB_FAIL(directory.assign(path))) {
  } else if (OB_FAIL(iterator.open(directory))) {
  } else {
    while (OB_SUCC(ret)) {
      DWORD attributes = 0;
      const int next_ret = iterator.next(child, attributes);
      if (OB_ITER_END == next_ret) {
        break;
      } else if (OB_SUCCESS != next_ret) {
        ret = next_ret;
      } else if (0 != (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        ret = OB_NOT_SUPPORTED;
      } else {
        const char *name = child.utf8();
        for (const char *pos = name; *pos != '\0'; ++pos) {
          if (*pos == '/' || *pos == '\\') { name = pos + 1; }
        }
        ret = visitor(child.utf8(), name, 0 != (attributes & FILE_ATTRIBUTE_DIRECTORY));
      }
    }
  }
  return ret;
}
} // namespace
#endif

int ObServerLogBlockMgr::check_clog_directory_is_empty(const char *clog_dir, bool &result)
{
#ifdef _WIN32
  int ret = OB_SUCCESS;
  result = false;
  common::ObArenaAllocator allocator;
  common::WindowsFilePath directory(allocator), child(allocator);
  common::WindowsDirectoryIterator iterator(allocator);
  if (OB_FAIL(directory.assign(clog_dir))) {
  } else if (OB_FAIL(iterator.open(directory))) {
  } else {
    DWORD attributes = 0;
    ret = iterator.next(child, attributes);
    if (OB_ITER_END == ret) {
      result = true;
      ret = OB_SUCCESS;
    }
  }
  return ret;
#else
  int ret = OB_SUCCESS;
  DIR *dir = NULL;
  struct dirent *entry = NULL;
  result = false;
  if (NULL == clog_dir) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "directory path is NULL, ", K(ret));
  } else if (NULL == (dir = opendir(clog_dir))) {
    ret = OB_ERR_SYS;
    CLOG_LOG(WARN, "Fail to open dir, ", K(ret), K(errno), K(clog_dir));
  } else {
    result = true;
    while (NULL != (entry = readdir(dir))) {
      if (0 != strcmp(entry->d_name, ".") && 0 != strcmp(entry->d_name, "..")) {
        result = false;
      }
    }
  }

  if (NULL != dir) {
    closedir(dir);
  }
  return ret;
#endif
}

ObServerLogBlockMgr::ObServerLogBlockMgr()
    : get_log_disk_info_in_config_func_(NULL),
      log_service_(NULL),
      block_cnt_in_use_(0),
      is_started_(false),
      is_inited_(false)
{
}

ObServerLogBlockMgr::~ObServerLogBlockMgr()
{
  destroy();
}

int ObServerLogBlockMgr::init(
    const char *log_disk_base_path,
    GetLogDiskInfoInConfig get_log_disk_info_in_config)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    CLOG_LOG(ERROR, "ObServerLogBlockMgr inited twice", K(ret), KPC(this));
  } else if (OB_ISNULL(log_disk_base_path)
             || OB_ISNULL(get_log_disk_info_in_config)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(ERROR, "Invalid argument", K(ret), KPC(this), KP(log_disk_base_path));
  } else if (OB_FAIL(do_load_(log_disk_base_path))) {
  } else {
    get_log_disk_info_in_config_func_ = get_log_disk_info_in_config;
    is_inited_ = true;
    CLOG_LOG(INFO, "ObServerLogBlockMgr init success", KPC(this));
  }
  if (OB_FAIL(ret)) {
    destroy();
  }
  return ret;
}

void ObServerLogBlockMgr::destroy()
{
  CLOG_LOG_RET(WARN, OB_SUCCESS, "ObServerLogBlockMgr  destroy", KPC(this));
  is_inited_ = false;
  is_started_ = false;
  block_cnt_in_use_ = 0;
  get_log_disk_info_in_config_func_ = NULL;
}

int ObServerLogBlockMgr::start(const int64_t new_size_byte)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "ObServerLogBlockMGR is not inited", K(ret), KPC(this));
  } else if (!check_space_is_enough_(new_size_byte)) {
    ret = OB_MACHINE_RESOURCE_NOT_ENOUGH;
    CLOG_LOG(WARN, "server log disk is too small for the local runtime",
             K(ret), KPC(this), K(new_size_byte));
  } else {
    ATOMIC_STORE(&is_started_, true);
    CLOG_LOG(INFO, "ObServerLogBlockMGR start success", K(ret), KPC(this), K(new_size_byte));
  }
  return ret;
}

int ObServerLogBlockMgr::get_disk_usage(int64_t &in_use_size_byte)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "ObServerLogBlockMgr has not inited", K(ret), KPC(this));
  } else {
    in_use_size_byte = get_in_use_size_();
  }
  return ret;
}

int64_t ObServerLogBlockMgr::get_log_disk_size()
{
  int ret = OB_SUCCESS;
  int64_t log_disk_size = 0;
  int64_t expected_log_disk_size = 0;
  int64_t unused_log_disk_percentage = 0;
  int64_t total_log_disk_size = 0;
  if (OB_FAIL(get_runtime_log_disk_size_(log_disk_size))) {
  } else if (OB_ISNULL(get_log_disk_info_in_config_func_)) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "get_log_disk_info_in_config_func_ is null", K(ret), KPC(this));
  } else if (OB_FAIL(get_log_disk_info_in_config_func_(expected_log_disk_size,
             unused_log_disk_percentage,
             total_log_disk_size))) {
  } else if (expected_log_disk_size > total_log_disk_size) {
    ret = OB_MACHINE_RESOURCE_NOT_ENOUGH;
    CLOG_LOG(ERROR, "try_resize failed, log disk space is not enough", K(expected_log_disk_size), KPC(this));
  } else {
    log_disk_size = expected_log_disk_size;
  }
  return log_disk_size;
}

int ObServerLogBlockMgr::create_block_at(const FileDesc &dest_dir_fd,
                                         const char *dest_block_path,
                                         const int64_t block_size)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(ERROR, "ObServerLogBlockMgr has not inited", K(ret), KPC(this));
  } else if (false == is_valid_file_desc(dest_dir_fd)
             || NULL == dest_block_path || BLOCK_SIZE != block_size) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(ERROR, "Invalid argument", K(ret), KPC(this), K(dest_dir_fd),
             K(dest_block_path), K(block_size));
  }
  if (OB_SUCC(ret)) {
    bool retryable = true;
    while (OB_FAIL(allocate_block_at_(dest_dir_fd, dest_block_path, block_size, retryable))) {
      CLOG_LOG(WARN, "allocate_block_at_ failed", K(ret), KPC(this),
               K(dest_dir_fd), K(dest_block_path));
      if (!retryable) {
        break;
      }
      ob_usleep(10 * 1000); // 10ms
    }
  }
  // make sure the meta info of both directory has been flushed.
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(fsync_until_success_(dest_dir_fd))) {
  } else {
    ATOMIC_INC(&block_cnt_in_use_);
    CLOG_LOG(INFO, "create_new_block_at success", K(ret), KPC(this), K(dest_dir_fd),
             K(dest_block_path));
  }
  return ret;
}

int ObServerLogBlockMgr::remove_block_at(const FileDesc &src_dir_fd,
                                         const char *src_block_path)
{
  int ret = OB_SUCCESS;
  block_id_t dest_block_id = LOG_INVALID_BLOCK_ID;
  char dest_block_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  bool result = true;
  if (OB_FAIL(is_block_used_for_palf(src_dir_fd, src_block_path, result))) {
  } else if (false == result) {
    CLOG_LOG(ERROR, "this block is not used for palf", K(ret), K(src_block_path));
#ifdef _WIN32
    ret = unlinkat_until_success_(src_dir_fd, src_block_path, 0);
#else
    ::unlinkat(src_dir_fd, src_block_path, 0);
#endif
  } else {
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      CLOG_LOG(ERROR, "ObServerLogBlockMGR has not inited", K(ret), KPC(this));
    } else if (OB_FAIL(free_block_at_(src_dir_fd, src_block_path))) {
    } else if (OB_FAIL(fsync_until_success_(src_dir_fd))) {
    } else {
      ATOMIC_DEC(&block_cnt_in_use_);
      CLOG_LOG(INFO, "delete_block_at success", K(ret), KPC(this), K(src_dir_fd),
               K(src_block_path));
    }
  }
  return ret;
}

int ObServerLogBlockMgr::update_log_disk_size(const int64_t old_log_disk_size,
                                              const int64_t new_log_disk_size,
                                              int64_t &allowed_new_log_disk_size,
                                              logservice::ObLogService *log_service)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    CLOG_LOG(WARN, "ObServerLogBlockMGR is not inited", K(old_log_disk_size), K(new_log_disk_size), KPC(this));
  } else if (old_log_disk_size < 0 || new_log_disk_size < 0 || OB_ISNULL(log_service)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid argument", K(old_log_disk_size), K(new_log_disk_size), KP(log_service), KPC(this));
  } else {
    allowed_new_log_disk_size = new_log_disk_size;
    if (OB_FAIL(log_service->update_log_disk_usage_limit_size(new_log_disk_size))) {
    }
  }
  return ret;
}

// Clean temporary files, then restore the allocated block count of the single log stream.
int ObServerLogBlockMgr::do_load_(const char *log_disk_path)
{
  int ret = OB_SUCCESS;
  int64_t has_allocated_block_cnt = 0;
  ObTimeGuard time_guard("RestartServerBlockMgr", 1 * 1000 * 1000);
  if (OB_FAIL(remove_tmp_file_or_directory_for_runtime_(log_disk_path))) {
  } else if (OB_FAIL(scan_log_disk_dir_(log_disk_path, has_allocated_block_cnt))) {
  } else {
    time_guard.click("scan_log_disk_");
    ATOMIC_STORE(&block_cnt_in_use_, has_allocated_block_cnt);
    CLOG_LOG(INFO, "do_load_ success", K(ret), KPC(this), K(time_guard));
  }
  return ret;
}

int ObServerLogBlockMgr::scan_log_disk_dir_(const char *log_disk_path,
                                            int64_t &has_allocated_block_cnt)
{
  return get_has_allocated_blocks_cnt_in_(log_disk_path, has_allocated_block_cnt);
}

bool ObServerLogBlockMgr::check_space_is_enough_(const int64_t log_disk_size) const
{
  bool bool_ret = false;
  int64_t runtime_log_disk_size = 0;
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_runtime_log_disk_size_(runtime_log_disk_size))) {
  } else {
    bool_ret = runtime_log_disk_size <= log_disk_size;
    CLOG_LOG(INFO, "check_space_is_enough_ finished", K(runtime_log_disk_size), K(log_disk_size));
  }
  return bool_ret;
}

int ObServerLogBlockMgr::get_runtime_log_disk_size_(int64_t &runtime_log_disk_size) const
{
  int ret = OB_SUCCESS;
  runtime_log_disk_size = 0;
  // Called during boot before the server modules are constructed, so a missing
  // log_service contributes zero until the module set becomes ready.
  PalfOptions opts;
  if (NULL == log_service_) {
  } else if (OB_FAIL(log_service_->get_palf_options(opts))) {
  } else {
    runtime_log_disk_size += opts.disk_options_.log_disk_usage_limit_size_;
  }
  return ret;
}

int64_t ObServerLogBlockMgr::get_in_use_size_()
{
  return ATOMIC_LOAD(&block_cnt_in_use_) * BLOCK_SIZE;
}

int ObServerLogBlockMgr::allocate_block_at_(const FileDesc &dir_fd,
                                            const char *block_path,
                                            const int64_t block_size, bool &retryable)
{
  int ret = OB_SUCCESS;
  retryable = true;
#ifdef _WIN32
  // Capture the operation error before logging or closing the file changes it.
  const auto can_retry = []() {
    const int operation_errno = errno;
    unsigned long native_error = 0;
    _get_doserrno(&native_error);
    return operation_errno != EEXIST && operation_errno != EINVAL
        && operation_errno != ENAMETOOLONG
        && operation_errno != EBADF && operation_errno != ENOTDIR
        && operation_errno != ENOENT
        && (operation_errno != EACCES || native_error == ERROR_SHARING_VIOLATION
            || native_error == ERROR_LOCK_VIOLATION);
  };
#endif
  FileDesc fd = -1;
  if (-1 == (fd = ::openat(dir_fd, block_path, CREATE_FILE_FLAG, CREATE_FILE_MODE))) {
#ifdef _WIN32
    retryable = can_retry();
#endif
    ret = convert_sys_errno();
    CLOG_LOG(ERROR, "::openat failed", K(ret), KPC(this), K(dir_fd), K(block_path));
#ifdef __APPLE__
  } else if (-1 == ftruncate(fd, block_size)) {
    ret = convert_sys_errno();
    CLOG_LOG(ERROR, "::ftruncate failed (macOS fallocate replacement)", K(ret), KPC(this), K(dir_fd), K(block_path),
             K(errno));
#else
  } else if (-1 == ::fallocate(fd, 0, 0, block_size)) {
#ifdef _WIN32
    retryable = can_retry();
#endif
    ret = convert_sys_errno();
    CLOG_LOG(ERROR, "::fallocate failed", K(ret), KPC(this), K(dir_fd), K(block_path),
             K(errno));
#endif
  } else {
    if (REACH_TIME_INTERVAL(PRINT_INTERVAL)) {
      CLOG_LOG(INFO, "allocate_block_at_ success", K(ret), KPC(this), K(dir_fd),
               K(block_path));
    }
  }
  if (-1 != fd && -1 == ::close(fd)) {
#ifdef _WIN32
    if (OB_SUCCESS == ret) {
      retryable = can_retry();
    }
#endif
    int tmp_ret = convert_sys_errno();
    CLOG_LOG(ERROR, "::close failed", K(ret), K(tmp_ret), KPC(this), K(dir_fd), K(block_path));
    ret = (OB_SUCCESS == ret ? tmp_ret : ret);
  }
  return ret;
}

int ObServerLogBlockMgr::free_block_at_(const FileDesc &src_dir_fd,
                                        const char *block_path)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(unlinkat_until_success_(src_dir_fd, block_path, 0))) {
  } else {
    if (REACH_TIME_INTERVAL(PRINT_INTERVAL)) {
      CLOG_LOG(INFO, "free_block_at_ success", K(ret), KPC(this), K(src_dir_fd),
               K(block_path));
    }
  }
  return ret;
}

int ObServerLogBlockMgr::get_has_allocated_blocks_cnt_in_(
    const char *log_disk_path, int64_t &has_allocated_block_cnt)
{
#ifdef _WIN32
  return scan_windows_log_pool_directory(log_disk_path,
      [this, &has_allocated_block_cnt](const char *path, const char *name, bool is_dir) {
        int ret = OB_SUCCESS;
        if (!is_dir) {
          ret = OB_ERR_UNEXPECTED;
        } else if (0 == strcmp(name, "sys")) {
          ret = scan_runtime_dir_(path, has_allocated_block_cnt);
        } else if (0 != strcmp(name, "log_pool")) {
          ret = OB_ERR_UNEXPECTED;
        }
        if (OB_FAIL(ret)) { CLOG_LOG(ERROR, "invalid log pool entry", K(ret), K(path)); }
        return ret;
      });
#else
  int ret = OB_SUCCESS;
  DIR *dir = NULL;
  std::regex pattern_runtime(".*/sys");
  std::regex pattern_log_pool(".*/log_pool/*");
  struct dirent *entry = NULL;
  if (NULL == (dir = opendir(log_disk_path))) {
    ret = OB_ERR_SYS;
    CLOG_LOG(WARN, "opendir failed", K(log_disk_path));
  } else {
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    while ((entry = readdir(dir)) != NULL && OB_SUCC(ret)) {
      bool is_dir = false;
      MEMSET(current_file_path, '\0', OB_MAX_FILE_NAME_LENGTH);
      if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, "..")) {
        // do nothing
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s",
                               log_disk_path, entry->d_name)) {
        ret = OB_ERR_UNEXPECTED;
        CLOG_LOG(WARN, "snprintf failed", K(ret), K(current_file_path), K(log_disk_path),
                K(entry->d_name));
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
      } else if (false == is_dir) {
        ret = OB_ERR_UNEXPECTED;
        LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                         "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                         ", unexpected file path is ", current_file_path);
      } else if (true == std::regex_match(current_file_path, pattern_runtime)) {
        ret = scan_runtime_dir_(current_file_path, has_allocated_block_cnt);
      } else if (true == std::regex_match(current_file_path, pattern_log_pool)) {
        CLOG_LOG(INFO, "ignore log_pool path", K(current_file_path), KPC(this));
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                         "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                         ", unexpected directory is ", current_file_path);
      }
    }
  }
  if (NULL != dir) {
    closedir(dir);
  }
  return ret;
#endif
}

int ObServerLogBlockMgr::remove_tmp_file_or_directory_for_runtime_(const char *log_disk_path)
{
#ifdef _WIN32
  return scan_windows_log_pool_directory(log_disk_path,
      [this](const char *path, const char *name, bool is_dir) {
        // The following disk scan diagnoses unexpected files, as on POSIX.
        return is_dir && 0 == strcmp(name, "sys")
            ? remove_tmp_file_or_directory_at(path, this) : OB_SUCCESS;
      });
#else
  int ret = OB_SUCCESS;
  DIR *dir = NULL;
  std::regex pattern_runtime(".*/sys");
  struct dirent *entry = NULL;
  if (NULL == (dir = opendir(log_disk_path))) {
    ret = OB_ERR_SYS;
    CLOG_LOG(WARN, "opendir failed", K(log_disk_path));
  } else {
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    while ((entry = readdir(dir)) != NULL && OB_SUCC(ret)) {
      bool is_dir = false;
      MEMSET(current_file_path, '\0', OB_MAX_FILE_NAME_LENGTH);
      if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, "..")) {
        // do nothing
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s",
                               log_disk_path, entry->d_name)) {
        ret = OB_ERR_UNEXPECTED;
        CLOG_LOG(WARN, "snprintf failed", K(ret), K(current_file_path), K(log_disk_path),
                K(entry->d_name));
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
      } else if (false == is_dir) {
        CLOG_LOG(ERROR, "is not diectory, unexpected", K(ret), K(log_disk_path), K(current_file_path));
      } else if (true == std::regex_match(current_file_path, pattern_runtime)) {
        if (OB_FAIL(remove_tmp_file_or_directory_at(current_file_path, this))) {
        } else {
          CLOG_LOG(INFO, "this dir is runtime, remove_tmp_file_or_directory_at success", K(ret), K(current_file_path));
        }
      }
    }
  }
  if (NULL != dir) {
    closedir(dir);
  }
  return ret;
#endif
}

int ObServerLogBlockMgr::unlinkat_until_success_(const palf::FileDesc &src_dir_fd,
                                                 const char *block_path, const int flag)
{
  int ret = OB_SUCCESS;
  do {
    if (-1 == ::unlinkat(src_dir_fd, block_path, flag)) {
#ifdef _WIN32
      const int operation_errno = errno;
      unsigned long native_error = 0;
      _get_doserrno(&native_error);
#endif
      ret = convert_sys_errno();
#ifdef _WIN32
      // Sharing conflicts can clear when another handle closes. Invalid paths
      // and permanent access failures cannot be repaired by this retry loop.
      if (operation_errno == EINVAL || operation_errno == ENAMETOOLONG
          || operation_errno == EBADF || operation_errno == ENOTDIR
          || operation_errno == ENOENT
          || (operation_errno == EACCES && native_error != ERROR_SHARING_VIOLATION
              && native_error != ERROR_LOCK_VIOLATION)) {
        break;
      }
#endif
      CLOG_LOG(ERROR, "::unlink failed", K(ret), KPC(this), K(src_dir_fd), K(block_path),
               K(flag));
      ob_usleep(SLEEP_TS_US);
    } else {
      ret = OB_SUCCESS;
      break;
    }
  } while (OB_FAIL(ret));
  return ret;
}

int ObServerLogBlockMgr::fsync_until_success_(const FileDesc &dest_dir_fd)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(fsync_with_retry(dest_dir_fd))) {
  }
  return ret;
}
// Scan the local runtime log-stream directories under the fixed sys path.
int ObServerLogBlockMgr::scan_runtime_dir_(const char *runtime_dir,
                                          int64_t &has_allocated_block_cnt)
{
#ifdef _WIN32
  return scan_windows_log_pool_directory(runtime_dir,
      [this, &has_allocated_block_cnt](const char *path, const char *name, bool is_dir) {
        int ret = OB_SUCCESS;
        if (!is_dir) {
          ret = OB_ERR_UNEXPECTED;
        } else if (0 == strcmp(name, "log_stream")) {
          ret = scan_ls_dir_(path, has_allocated_block_cnt);
        } else if (0 != strcmp(name, "tmp_dir")) {
          ret = OB_ERR_UNEXPECTED;
        }
        if (OB_FAIL(ret)) { CLOG_LOG(ERROR, "invalid runtime log entry", K(ret), K(path)); }
        return ret;
      });
#else
  int ret = OB_SUCCESS;
  DIR *dir = NULL;
  struct dirent *entry = NULL;
  if (NULL == (dir = opendir(runtime_dir))) {
    ret = OB_ERR_SYS;
    CLOG_LOG(WARN, "opendir failed", K(runtime_dir));
  } else {
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    while ((entry = readdir(dir)) != NULL && OB_SUCC(ret)) {
      bool is_dir = false;
      MEMSET(current_file_path, '\0', OB_MAX_FILE_NAME_LENGTH);
      if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, "..")) {
        // do nothing
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s",
                               runtime_dir, entry->d_name)) {
        ret = OB_ERR_UNEXPECTED;
        CLOG_LOG(WARN, "snprintf failed", K(ret), K(current_file_path), K(runtime_dir),
                K(entry->d_name));
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
      } else if (false == is_dir) {
        ret = OB_ERR_UNEXPECTED;
        LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                         "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                         ", unexpected file is ", current_file_path);
      } else if (0 == strcmp(entry->d_name, "log_stream")) {
        ret = scan_ls_dir_(current_file_path, has_allocated_block_cnt);
      } else if (0 == strcmp(entry->d_name, "tmp_dir")) {
        CLOG_LOG(INFO, "ignore tmp_dir", K(current_file_path), K(has_allocated_block_cnt), KPC(this));
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                         "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                         ", unexpected directory is ", current_file_path);
      }
    }
  }
  if (NULL != dir) {
    closedir(dir);
  }
  return ret;
#endif
}

// Scan one log-stream directory.
int ObServerLogBlockMgr::scan_ls_dir_(const char *ls_dir,
                                      int64_t &has_allocated_block_cnt)
{
#ifdef _WIN32
  return scan_windows_log_pool_directory(ls_dir,
      [&has_allocated_block_cnt](const char *path, const char *name, bool is_dir) {
        int ret = OB_SUCCESS;
        if (!is_dir || (0 != strcmp(name, "log") && 0 != strcmp(name, "meta"))) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          GetBlockCountFunctor functor(path);
          if (OB_FAIL(palf::scan_dir(path, functor))) {
          } else {
            has_allocated_block_cnt += functor.get_block_count();
          }
        }
        if (OB_FAIL(ret)) { CLOG_LOG(ERROR, "invalid log stream entry", K(ret), K(path)); }
        return ret;
      });
#else
  int ret = OB_SUCCESS;
  DIR *dir = NULL;
  struct dirent *entry = NULL;
  if (NULL == (dir = opendir(ls_dir))) {
    ret = OB_ERR_SYS;
    CLOG_LOG(WARN, "opendir failed", K(ls_dir));
  } else {
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
    while ((entry = readdir(dir)) != NULL && OB_SUCC(ret)) {
      bool is_dir = false;
      MEMSET(current_file_path, '\0', OB_MAX_FILE_NAME_LENGTH);
      if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, "..")) {
        // do nothing
      } else if (0 >= snprintf(current_file_path, OB_MAX_FILE_NAME_LENGTH, "%s/%s",
                               ls_dir, entry->d_name)) {
        ret = OB_ERR_UNEXPECTED;
        CLOG_LOG(WARN, "snprintf failed", K(ret), K(current_file_path), K(ls_dir),
                K(entry->d_name));
      } else if (OB_FAIL(FileDirectoryUtils::is_directory(current_file_path, is_dir))) {
      } else if (false == is_dir) {
        ret = OB_ERR_UNEXPECTED;
        LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                         "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                         ", unexpected file is ", current_file_path);
      } else if (0 == strcmp(entry->d_name, "log")
                 || 0 == strcmp(entry->d_name, "meta")) {
        GetBlockCountFunctor functor(current_file_path);
        if (OB_FAIL(palf::scan_dir(current_file_path, functor))) {
          LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                           "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                           ", unexpected directory is ", current_file_path);
        } else {
          has_allocated_block_cnt += functor.get_block_count();
          CLOG_LOG(INFO, "get_has_allocated_blocks_cnt_in_ success", K(ret),
                   K(current_file_path), "block_cnt", functor.get_block_count());
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                         "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                         ", unexpected directory is ", current_file_path);
      }
    }
  }
  if (NULL != dir) {
    closedir(dir);
  }
  return ret;
#endif
}
} // namespace logservice
} // namespace oceanbase
