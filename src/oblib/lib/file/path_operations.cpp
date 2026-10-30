/*
 * Copyright (c) 2026 OceanBase.
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

#include "lib/file/path_operations.h"
#include "lib/string/ob_sql_string.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log.h"
#include "lib/utility/ob_platform_utils.h"
#include "rust/file_io.h"
#include <cerrno>
#include <climits>
#include <cstring>

namespace oceanbase
{
namespace common
{
namespace path_operations
{
namespace
{
bool valid(const char *path) { return path != nullptr && path[0] != '\0'; }

rust::Slice<const uint8_t> bytes(const char *path)
{
  return {reinterpret_cast<const uint8_t *>(path), std::strlen(path)};
}

file_io::ResolvedPath resolved(const char *path)
{
#ifdef _WIN32
  return file_io::resolve_path(bytes(path), true);
#else
  return file_io::resolve_path(bytes(path), false);
#endif
}
}

int is_exists(const char *path, bool &result)
{
  result = false;
  if (!valid(path)) return OB_INVALID_ARGUMENT;
  result = file_io::inspect_path(bytes(path), true).error == 0;
  return OB_SUCCESS;
}

int is_directory(const char *path, bool &result)
{
  result = false;
  if (!valid(path)) return OB_INVALID_ARGUMENT;
  const auto info = file_io::inspect_path(bytes(path), true);
  result = info.error == 0 && info.directory;
  return OB_SUCCESS;
}

int create_directory(const char *path)
{
  if (!valid(path)) return OB_INVALID_ARGUMENT;
  const int error = file_io::create_directory_path(bytes(path), 0777, false);
  return error == 0 || error == EEXIST ? OB_SUCCESS : OB_IO_ERROR;
}

int create_full_path(const char *path)
{
  if (!valid(path)) return OB_INVALID_ARGUMENT;
  const int error = file_io::create_directory_path(bytes(path), 0777, true);
  return error == 0 ? OB_SUCCESS : error == EEXIST ? OB_ENTRY_EXIST : OB_IO_ERROR;
}

int delete_file(const char *path)
{
  if (!valid(path)) return OB_INVALID_ARGUMENT;
  const auto info = file_io::inspect_path(bytes(path), true);
  if (info.error != 0 || info.directory) return OB_FILE_NOT_EXIST;
  return file_io::remove_file_path(bytes(path)) == 0 ? OB_SUCCESS : OB_IO_ERROR;
}

int delete_directory(const char *path)
{
  if (!valid(path)) return OB_INVALID_ARGUMENT;
  const auto info = file_io::inspect_path(bytes(path), true);
  if (info.error != 0 || !info.directory) return OB_FILE_NOT_EXIST;
  return file_io::remove_directory_path(bytes(path)) == 0 ? OB_SUCCESS : OB_IO_ERROR;
}

int get_file_size(const char *path, int64_t &size)
{
  if (!valid(path)) return OB_INVALID_ARGUMENT;
  const auto info = file_io::inspect_path(bytes(path), true);
  if (info.error != 0 || info.directory) return OB_FILE_NOT_EXIST;
  if (info.length > static_cast<uint64_t>(INT64_MAX)) return OB_IO_ERROR;
  size = static_cast<int64_t>(info.length);
  return OB_SUCCESS;
}

int symlink(const char *target, const char *link)
{
  if (!valid(target) || !valid(link)) return OB_INVALID_ARGUMENT;
  return file_io::symlink_file_path(bytes(target), bytes(link)) == 0 ? OB_SUCCESS : OB_IO_ERROR;
}

int unlink_symlink(const char *path)
{
  if (!valid(path)) return OB_INVALID_ARGUMENT;
  const auto info = file_io::inspect_path(bytes(path), false);
  if (info.error != 0 || !info.symlink) return OB_ERR_UNEXPECTED;
  return file_io::remove_file_path(bytes(path)) == 0 ? OB_SUCCESS : OB_IO_ERROR;
}

int get_disk_space(const char *path, int64_t &total, int64_t &available)
{
  total = 0;
  available = 0;
  if (path == nullptr) return OB_INVALID_ARGUMENT;
  const auto space = file_io::read_usable_space(bytes(path));
  if (space.error != 0 || space.total > static_cast<uint64_t>(INT64_MAX)
      || space.available > static_cast<uint64_t>(INT64_MAX)) return OB_IO_ERROR;
  total = static_cast<int64_t>(space.total);
  available = static_cast<int64_t>(space.available);
  return OB_SUCCESS;
}

int resolve_path(const char *path, char *output, size_t capacity)
{
  if (output == nullptr || capacity == 0) return OB_INVALID_ARGUMENT;
  output[0] = '\0';
  if (!valid(path)) return OB_INVALID_ARGUMENT;
  const auto result = resolved(path);
  if (result.error != 0 || result.path.size() >= capacity) return OB_ERR_UNEXPECTED;
  std::memcpy(output, result.path.data(), result.path.size());
  output[result.path.size()] = '\0';
  return OB_SUCCESS;
}

int to_absolute_path(ObSqlString &path)
{
  if (path.empty() || path.ptr()[0] == '\0' || path.ptr()[0] == '/') return OB_SUCCESS;
  const auto result = resolved(path.ptr());
  if (result.error != 0 || result.path.size() >= PATH_MAX) return OB_ERR_UNEXPECTED;
  return path.assign(reinterpret_cast<const char *>(result.path.data()), result.path.size());
}
int fsync_dir(const char *dir_path)
{
  if (nullptr == dir_path) { return OB_INVALID_ARGUMENT; }
  auto result = file_io::sync_directory_path(
      {reinterpret_cast<const uint8_t *>(dir_path), strlen(dir_path)});
  if (result.stage == file_io::SyncStage::Complete) { return OB_SUCCESS; }
  const int ret = OB_IO_ERROR;
#ifdef _WIN32
  if (result.stage == file_io::SyncStage::FlushFailed && result.close.domain == file_io::ErrorDomain::None) {
    LIB_LOG(WARN, "directory durability unconfirmed after failed flush", K(dir_path),
        "native_domain", static_cast<int>(result.primary.domain), "native_error", result.primary.native_code,
        "error", result.primary.message.c_str());
    return OB_SUCCESS;
  }
#endif
  LIB_LOG(WARN, "directory synchronization failed", K(dir_path), "stage", static_cast<int>(result.stage),
      "native_domain", static_cast<int>(result.primary.domain), "native_error", result.primary.native_code,
      "error", result.primary.message.c_str(), "close_domain", static_cast<int>(result.close.domain),
      "close_error", result.close.native_code);
  return ret;
}


}
}
}
