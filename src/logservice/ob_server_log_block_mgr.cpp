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
#include "lib/file/path_operations.h"
#include "ob_server_log_block_mgr.h"
#include <regex>
#include "logservice/ob_log_service.h"          // ObLogService

#define BYTE_TO_MB(byte) (byte+1024*1024-1)/1024/1024

namespace oceanbase
{
using namespace palf;
using namespace share;
namespace logservice
{

int ObServerLogBlockMgr::check_clog_directory_is_empty(const char *clog_dir, bool &result)
{
  result = false;
  if (clog_dir == nullptr) return OB_INVALID_ARGUMENT;
  auto directory = file_io::new_directory_handle();
  if (directory->open_directory({reinterpret_cast<const uint8_t *>(clog_dir), strlen(clog_dir)}).domain != file_io::ErrorDomain::None) {
#ifdef _WIN32
    return OB_IO_ERROR;
#else
    return OB_ERR_SYS;
#endif
  }
  auto cursor = file_io::directory_entries(*directory);
  file_io::DirectoryStep entry;
  file_io::ObservationResult observation;
  bool empty = true;
  for (;;) {
    const int ret = next_directory_entry(*directory, clog_dir, *cursor, entry, observation);
    if (ret != OB_SUCCESS) return ret;
    if (entry.code == file_io::DirectoryStepCode::End) { result = empty; return OB_SUCCESS; }
    empty = false;
  }
}

ObServerLogBlockMgr::ObServerLogBlockMgr(share::StartupResult &startup_result)
    : startup_result_(startup_result),
      get_log_disk_info_in_config_func_(NULL),
      log_service_(NULL),
      owner_(storage_io::new_named_owner()),
      is_started_(false),
      is_inited_(false)
{
}

ObServerLogBlockMgr::~ObServerLogBlockMgr()
{
  destroy();
}

int ObServerLogBlockMgr::init(const char *path, GetLogDiskInfoInConfig get_config,
    const share::LocalIOConfig &io_config)
{
  if (is_inited_) return OB_INIT_TWICE;
  if (path == nullptr || get_config == nullptr) return OB_INVALID_ARGUMENT;
  if (!owner_->named_owner_replaceable()) return OB_STATE_NOT_MATCH;
  io_config_ = io_config;
  stopped_.store(false);
  int ret = OB_SUCCESS;
  const int error = owner_->begin_named_inventory(0);
  if (error != 0) { errno = error; ret = convert_sys_errno(); }
  else if (OB_FAIL(do_load_(path))) {}
  else { get_log_disk_info_in_config_func_ = get_config; is_inited_ = true; }
  if (OB_FAIL(ret)) destroy();
  return ret;
}

void ObServerLogBlockMgr::destroy()
{
  owner_->close_named_owner();
  const auto state = owner_->named_stats();
  CLOG_LOG_RET(WARN, OB_SUCCESS, "log physical owner closed", "phase", state.phase,
      "pending", state.pending, "active", state.active, "leases", state.leases,
      "charged", state.charged, "native_error", state.error);
  is_inited_ = false;
  is_started_ = false;
  get_log_disk_info_in_config_func_ = nullptr;
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
  } else if (OB_FAIL(set_capacity(new_size_byte))) {
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

int ObServerLogBlockMgr::create_and_publish_block_at(const file_io::DirectoryHandle &directory,
    const char *temporary_name, const char *name, const int64_t block_size)
{
  if (!is_inited_) return OB_NOT_INIT;
  if (temporary_name == nullptr || name == nullptr || block_size != BLOCK_SIZE) return OB_INVALID_ARGUMENT;
  if (stopped_.load()) return OB_CANCELED;
  const int error = owner_->create_and_publish_named_file(directory,
      {reinterpret_cast<const uint8_t *>(temporary_name), strlen(temporary_name)},
      {reinterpret_cast<const uint8_t *>(name), strlen(name)}, block_size, true);
  if (error == 0) return OB_SUCCESS;
  errno = error;
  const int ret = convert_sys_errno();
  (void)startup_result_.report_failure(ret);
  return ret;
}

int ObServerLogBlockMgr::remove_block_at(const file_io::DirectoryHandle &directory,
    const char *name, bool required)
{
  if (name == nullptr) return OB_INVALID_ARGUMENT;
  const int error = owner_->remove_named_file(directory,
      {reinterpret_cast<const uint8_t *>(name), strlen(name)}, required);
  if (error == 0) return OB_SUCCESS;
  errno = error;
  return convert_sys_errno();
}

int ObServerLogBlockMgr::rename_block_at(const file_io::DirectoryHandle &directory,
    const char *old_name, const char *new_name)
{
  if (!is_inited_) return OB_NOT_INIT;
  if (old_name == nullptr || new_name == nullptr) return OB_INVALID_ARGUMENT;
  const int error = owner_->rename_named_file(directory,
      {reinterpret_cast<const uint8_t *>(old_name), strlen(old_name)},
      {reinterpret_cast<const uint8_t *>(new_name), strlen(new_name)}, true);
  if (error == 0) return OB_SUCCESS;
  errno = error;
  return convert_sys_errno();
}

int ObServerLogBlockMgr::create_directory_at(const file_io::DirectoryHandle &directory,
    const char *name, uint32_t mode)
{
  if (!is_inited_) return OB_NOT_INIT;
  if (name == nullptr) return OB_INVALID_ARGUMENT;
  const int error = owner_->create_named_directory(directory,
      {reinterpret_cast<const uint8_t *>(name), strlen(name)}, mode);
  if (error == 0) return OB_SUCCESS;
  errno = error;
  return convert_sys_errno();
}

int ObServerLogBlockMgr::rename_directory_at(const file_io::DirectoryHandle &directory,
    const char *old_name, const char *new_name)
{
  if (!is_inited_) return OB_NOT_INIT;
  if (old_name == nullptr || new_name == nullptr) return OB_INVALID_ARGUMENT;
  const int error = owner_->rename_named_directory(directory,
      {reinterpret_cast<const uint8_t *>(old_name), strlen(old_name)},
      {reinterpret_cast<const uint8_t *>(new_name), strlen(new_name)}, true);
  if (error == 0) return OB_SUCCESS;
  errno = error;
  return convert_sys_errno();
}

int ObServerLogBlockMgr::remove_directory_at(const file_io::DirectoryHandle &directory,
    const char *name)
{
  if (name == nullptr) return OB_INVALID_ARGUMENT;
  const int error = owner_->remove_named_directory(directory,
      {reinterpret_cast<const uint8_t *>(name), strlen(name)});
  if (error == 0) return OB_SUCCESS;
  errno = error;
  return convert_sys_errno();
}

int ObServerLogBlockMgr::set_capacity(int64_t bytes)
{
  if (bytes < 0) return OB_INVALID_ARGUMENT;
  const int error = owner_->set_named_capacity(bytes);
  if (error == 0) return OB_SUCCESS;
  errno = error;
  return convert_sys_errno();
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
int ObServerLogBlockMgr::do_load_(const char *path)
{
  int ret = OB_SUCCESS;
  int64_t selected = 0;
  if (OB_FAIL(remove_tmp_file_or_directory_for_runtime_(path))) {}
  else if (OB_FAIL(get_has_allocated_blocks_cnt_in_(path, selected))) {}
  else {
    const int error = owner_->finish_named_inventory();
    if (error != 0) { errno = error; ret = convert_sys_errno(); }
  }
  return ret;
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
  return static_cast<int64_t>(owner_->named_stats().charged);
}





int ObServerLogBlockMgr::get_has_allocated_blocks_cnt_in_(const char *log_disk_path, int64_t &has_allocated_block_cnt)
{
  int ret = OB_SUCCESS;
  int64_t total = 0;
  std::regex pattern_runtime(".*/sys");
  std::regex pattern_log_pool(".*/log_pool/*");
  if (log_disk_path == nullptr) return OB_INVALID_ARGUMENT;
  auto owner = file_io::new_directory_handle();
  if (owner->open_directory({reinterpret_cast<const uint8_t *>(log_disk_path), strlen(log_disk_path)}).domain != file_io::ErrorDomain::None) return OB_ERR_SYS;
  const auto &directory = *owner;
  auto cursor = file_io::directory_entries(directory);
  file_io::DirectoryStep entry;
  file_io::ObservationResult observed;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(next_directory_entry(directory, log_disk_path, *cursor, entry, observed))) break;
    if (entry.code == file_io::DirectoryStepCode::End) break;
    const ObString name(static_cast<int32_t>(entry.name.size()), reinterpret_cast<const char *>(entry.name.data()));
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {};
    if (OB_FAIL(entry_path(log_disk_path, name, current_file_path, sizeof(current_file_path)))) break;
    if (observed.kind != file_io::EntryKind::Directory) {
      ret = OB_ERR_UNEXPECTED;
      LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                       "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                       ", unexpected file path is ", current_file_path);
    } else if (std::regex_match(current_file_path, pattern_runtime)) {
      auto child = file_io::new_directory_handle();
      const auto opened = file_io::open_child_directory(directory, {entry.name.data(), entry.name.size()}, *child);
      if (opened.domain != file_io::ErrorDomain::None) { ret = OB_ERR_SYS; break; }
      ret = scan_runtime_dir_(*child, current_file_path, total);
    } else if (!std::regex_match(current_file_path, pattern_log_pool)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                       "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                       ", unexpected directory is ", current_file_path);
    }
  }
  if (OB_SUCC(ret)) has_allocated_block_cnt += total;
  return ret;
}

int ObServerLogBlockMgr::remove_tmp_file_or_directory_for_runtime_(const char *log_disk_path)
{
  int ret = OB_SUCCESS;
  std::regex pattern_runtime(".*/sys");
  if (log_disk_path == nullptr) return OB_INVALID_ARGUMENT;
  auto owner = file_io::new_directory_handle();
  if (owner->open_directory({reinterpret_cast<const uint8_t *>(log_disk_path), strlen(log_disk_path)}).domain != file_io::ErrorDomain::None) return OB_ERR_SYS;
  const auto &directory = *owner;
  auto cursor = file_io::directory_entries(directory);
  file_io::DirectoryStep entry;
  file_io::ObservationResult observed;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(next_directory_entry(directory, log_disk_path, *cursor, entry, observed))) break;
    if (entry.code == file_io::DirectoryStepCode::End) break;
    const ObString name(static_cast<int32_t>(entry.name.size()), reinterpret_cast<const char *>(entry.name.data()));
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {};
    if (OB_FAIL(entry_path(log_disk_path, name, current_file_path, sizeof(current_file_path)))) break;
    if (observed.kind != file_io::EntryKind::Directory) {
      CLOG_LOG(ERROR, "unexpected non-directory in log disk", K(current_file_path));
    } else if (std::regex_match(current_file_path, pattern_runtime)) {
      auto child = file_io::new_directory_handle();
      const auto opened = file_io::open_child_directory(directory, {entry.name.data(), entry.name.size()}, *child);
      if (opened.domain != file_io::ErrorDomain::None) { ret = OB_ERR_SYS; break; }
      ret = remove_tmp_file_or_directory_at(*child, current_file_path, this);
    }
  }
  return ret;
}




// Scan the local runtime log-stream directories under the fixed sys path.
int ObServerLogBlockMgr::scan_runtime_dir_(const file_io::DirectoryHandle &directory, const char *runtime_dir, int64_t &has_allocated_block_cnt)
{
  int ret = OB_SUCCESS;
  int64_t total = 0;
  auto cursor = file_io::directory_entries(directory);
  file_io::DirectoryStep entry;
  file_io::ObservationResult observed;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(next_directory_entry(directory, runtime_dir, *cursor, entry, observed))) break;
    if (entry.code == file_io::DirectoryStepCode::End) break;
    const ObString name(static_cast<int32_t>(entry.name.size()), reinterpret_cast<const char *>(entry.name.data()));
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {};
    if (OB_FAIL(entry_path(runtime_dir, name, current_file_path, sizeof(current_file_path)))) break;
    if (observed.kind != file_io::EntryKind::Directory) {
      ret = OB_ERR_UNEXPECTED;
      LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                       "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                       ", unexpected file is ", current_file_path);
    } else if (name == "log_stream") {
      auto child = file_io::new_directory_handle();
      const auto opened = file_io::open_child_directory(directory, {entry.name.data(), entry.name.size()}, *child);
      if (opened.domain != file_io::ErrorDomain::None) { ret = OB_ERR_SYS; break; }
      ret = scan_ls_dir_(*child, current_file_path, total);
    } else if (name != "tmp_dir") {
      ret = OB_ERR_UNEXPECTED;
      LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                       "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                       ", unexpected directory is ", current_file_path);
    }
  }
  if (OB_SUCC(ret)) has_allocated_block_cnt += total;
  return ret;
}

// Scan one log-stream directory.
int ObServerLogBlockMgr::scan_ls_dir_(const file_io::DirectoryHandle &directory, const char *ls_dir, int64_t &has_allocated_block_cnt)
{
  int ret = OB_SUCCESS;
  int64_t total = 0;
  auto cursor = file_io::directory_entries(directory);
  file_io::DirectoryStep entry;
  file_io::ObservationResult observed;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(next_directory_entry(directory, ls_dir, *cursor, entry, observed))) break;
    if (entry.code == file_io::DirectoryStepCode::End) break;
    const ObString name(static_cast<int32_t>(entry.name.size()), reinterpret_cast<const char *>(entry.name.data()));
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {};
    if (OB_FAIL(entry_path(ls_dir, name, current_file_path, sizeof(current_file_path)))) break;
    if (observed.kind != file_io::EntryKind::Directory) {
      ret = OB_ERR_UNEXPECTED;
      LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                       "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                       ", unexpected file is ", current_file_path);
    } else if (name != "log") {
      ret = OB_ERR_UNEXPECTED;
      LOG_DBA_ERROR_V2(OB_LOG_EXTERNAL_FILE_EXIST, ret, "Attention!!!", "There are several files in the log directory that are not generated by "
                       "OceanBase.", "[suggestion] Please confirm whether manual deletion is required",
                       ", unexpected directory is ", current_file_path);
    } else {
      auto child = file_io::new_directory_handle();
      const auto opened = file_io::open_child_directory(directory, {entry.name.data(), entry.name.size()}, *child);
      if (opened.domain != file_io::ErrorDomain::None) {
        ret = opened.error == ENOENT ? OB_NO_SUCH_FILE_OR_DIRECTORY : OB_FILE_NOT_OPENED;
        break;
      }
      auto files = file_io::directory_entries(*child);
      uint64_t maximum = 0;
      uint64_t maximum_candidate = 0;
      uint64_t maximum_length = 0;
      bool has_maximum = false;
      // Candidates pin the opened file before C++ chooses the maximum-name repair.
      for (;;) {
        const auto file = files->next_entry();
        if (file.code == file_io::DirectoryStepCode::End) break;
        if (file.code != file_io::DirectoryStepCode::Entry) { ret = directory_error(file.detail, current_file_path); break; }
        block_id_t number = LOG_INVALID_BLOCK_ID;
        const ObString block_name(static_cast<int32_t>(file.name.size()), reinterpret_cast<const char *>(file.name.data()));
        if (OB_FAIL(parse_block_name(block_name, number))) {
          LOG_WARN("invalid numeric log block name", K(ret), K(block_name), KCSTRING(current_file_path));
          break;
        }
        uint64_t candidate = 0;
        uint64_t length = 0;
        int error = owner_->select_named_candidate(*child, {file.name.data(), file.name.size()}, BLOCK_SIZE, candidate, length);
        if (error != 0) { errno = error; ret = convert_sys_errno(); break; }
        if (has_maximum && number == maximum) { ret = OB_ERR_UNEXPECTED; break; }
        if (!has_maximum || number > maximum) {
          if (has_maximum && maximum_length != BLOCK_SIZE) { ret = OB_ERR_UNEXPECTED; break; }
          maximum = number;
          maximum_candidate = candidate;
          maximum_length = length;
          has_maximum = true;
        } else if (length != BLOCK_SIZE) {
          ret = OB_ERR_UNEXPECTED;
          break;
        }
        if (0 != (error = owner_->admit_named_candidate(candidate))) { errno = error; ret = convert_sys_errno(); break; }
        ++total;
      }
      if (OB_SUCC(ret) && has_maximum && maximum_length != BLOCK_SIZE) {
        const int error = owner_->repair_named_candidate(maximum_candidate);
        if (error != 0) { errno = error; ret = convert_sys_errno(); }
      }
    }
  }
  if (OB_SUCC(ret)) has_allocated_block_cnt += total;
  return ret;
}
} // namespace logservice
} // namespace oceanbase
