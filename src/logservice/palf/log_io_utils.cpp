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

#include <string>
#include "log_io_utils.h"
#include "share/ob_errno.h"
#include "logservice/ob_server_log_block_mgr.h"

namespace oceanbase
{
namespace palf
{

int open_parent_directory(const char *path, file_io::DirectoryHandle &parent, std::string &leaf)
{
  if (path == nullptr || path[0] == '\0') return OB_INVALID_ARGUMENT;
  std::string full(path);
#ifdef _WIN32
  while (full.size() > 1 && (full.back() == '/' || full.back() == '\\')) full.pop_back();
#else
  while (full.size() > 1 && full.back() == '/') full.pop_back();
#endif
#ifdef _WIN32
  const auto split = full.find_last_of("/\\");
#else
  const auto split = full.find_last_of('/');
#endif
  std::string parent_path = split == std::string::npos ? "." : full.substr(0, split + 1);
  leaf = split == std::string::npos ? full : full.substr(split + 1);
#ifdef _WIN32
  if (split == std::string::npos && full.size() >= 2 && full[1] == ':') {
    parent_path = full.substr(0, 2);
    leaf = full.substr(2);
  }
#endif
  return directory_error(parent.open_directory(
      {reinterpret_cast<const uint8_t *>(parent_path.data()), parent_path.size()}), path);
}

int directory_error(const file_io::FsError &error, const char *path)
{
  if (error.domain == file_io::ErrorDomain::None) return OB_SUCCESS;
  PALF_LOG_RET(WARN, OB_IO_ERROR, "directory operation failed", K(path), "native_domain", static_cast<int>(error.domain),
      "native_error", error.native_code, "error",
      common::ObString(static_cast<int32_t>(error.message.size()), error.message.data()));
  return OB_IO_ERROR;
}

int next_directory_entry(const file_io::DirectoryHandle &directory, const char *path,
                         file_io::DirectoryCursor &cursor,
                         file_io::DirectoryStep &entry,
                         file_io::ObservationResult &observation)
{
  for (;;) {
    entry = cursor.next_entry();
    if (entry.code == file_io::DirectoryStepCode::End) return OB_SUCCESS;
    if (entry.code != file_io::DirectoryStepCode::Entry) return directory_error(entry.detail, path);
    observation = file_io::inspect_at(directory, {entry.name.data(), entry.name.size()}, false);
    const int ret = directory_error(observation.error, path);
    if (ret != OB_SUCCESS) return ret;
    if (observation.presence == file_io::Presence::Missing) continue;
    if (observation.kind == file_io::EntryKind::Link) {
      const std::string entry_path = std::string(path) + "/" +
          std::string(reinterpret_cast<const char *>(entry.name.data()), entry.name.size());
      PALF_LOG_RET(WARN, OB_IO_ERROR, "symbolic link in log directory", "path", entry_path.c_str());
      return OB_IO_ERROR;
    }
    return OB_SUCCESS;
  }
}

int entry_path(const char *parent, const common::ObString &name, char *output, size_t capacity)
{
  const int length = snprintf(output, capacity, "%s/%.*s", parent, name.length(), name.ptr());
  return length > 0 && static_cast<size_t>(length) < capacity ? OB_SUCCESS : OB_ERR_UNEXPECTED;
}

int scan_dir(const char *dir_name, ObBaseDirFunctor &functor)
{
  if (dir_name == nullptr) return OB_INVALID_ARGUMENT;
  auto directory = file_io::new_directory_handle();
  const auto error = directory->open_directory({reinterpret_cast<const uint8_t *>(dir_name), strlen(dir_name)});
  if (error.domain != file_io::ErrorDomain::None) {
    return error.error == ENOENT ? OB_NO_SUCH_FILE_OR_DIRECTORY : OB_FILE_NOT_OPENED;
  }
  return scan_dir(*directory, dir_name, functor);
}

int scan_dir(const file_io::DirectoryHandle &directory, const char *path, ObBaseDirFunctor &functor)
{
  auto cursor = file_io::directory_entries(directory);
  file_io::DirectoryStep entry;
  file_io::ObservationResult observation;
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(next_directory_entry(directory, path, *cursor, entry, observation))) break;
    if (entry.code == file_io::DirectoryStepCode::End) break;
    const common::ObString name(static_cast<int32_t>(entry.name.size()), reinterpret_cast<const char *>(entry.name.data()));
    ret = functor.func(name);
  }
  return ret;
}

int parse_block_name(const common::ObString &name, block_id_t &id)
{
  if (name.empty()) return OB_INVALID_ARGUMENT;
  uint64_t value = 0;
  for (int32_t i = 0; i < name.length(); ++i) {
    const char digit = name.ptr()[i];
    if (digit < '0' || digit > '9' || value > (LOG_MAX_BLOCK_ID - (digit - '0')) / 10) {
      return OB_INVALID_ARGUMENT;
    }
    value = value * 10 + digit - '0';
  }
  if (!is_valid_block_id(value)) return OB_INVALID_ARGUMENT;
  id = value;
  return OB_SUCCESS;
}

int TrimLogDirectoryFunctor::func(const common::ObString &name)
{
  block_id_t block_id = LOG_INVALID_BLOCK_ID;
  const int ret = parse_block_name(name, block_id);
  if (ret != OB_SUCCESS) {
    PALF_LOG(WARN, "invalid numeric log block name", K(ret), K(name), K_(dir));
    return ret;
  }
  if (LOG_INVALID_BLOCK_ID == min_block_id_ || block_id < min_block_id_) min_block_id_ = block_id;
  if (LOG_INVALID_BLOCK_ID == max_block_id_ || block_id > max_block_id_) max_block_id_ = block_id;
  return OB_SUCCESS;
}

} // end namespace palf
} // end namespace oceanbase
