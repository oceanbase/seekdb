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
#include "lib/utility/ob_smart_call.h"
#include "log_block_pool_interface.h"
#include "log_io_utils.h"
namespace oceanbase
{
namespace palf
{

namespace
{
enum class Cleanup { All, Temporary, RuntimeTemporary };

int clean_directory(const file_io::DirectoryHandle &directory, const char *path,
                    Cleanup selection, ILogBlockPool *pool)
{
  using namespace common;
  int ret = OB_SUCCESS;
  auto cursor = file_io::directory_entries(directory);
  file_io::DirectoryStep entry;
  file_io::ObservationResult observation;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(next_directory_entry(directory, path, *cursor, entry, observation))) break;
    if (entry.code == file_io::DirectoryStepCode::End) break;
    const common::ObString name(static_cast<int32_t>(entry.name.size()), reinterpret_cast<const char *>(entry.name.data()));
    if (selection == Cleanup::RuntimeTemporary && name == "tmp_dir") continue;
    char current_file_path[OB_MAX_FILE_NAME_LENGTH] = {};
    if (OB_FAIL(entry_path(path, name, current_file_path, sizeof(current_file_path)))) break;
    const bool selected = selection == Cleanup::All || nullptr != strstr(current_file_path, ".tmp");
    if (observation.kind == file_io::EntryKind::Directory) {
      auto child = file_io::new_directory_handle();
      const auto opened = file_io::open_child_directory(directory, {entry.name.data(), entry.name.size()}, *child);
      if (opened.domain != file_io::ErrorDomain::None) {
        if (!selected) ret = OB_ERR_SYS;
        else if (pool == nullptr) ret = opened.error == ENOENT ? OB_ENTRY_NOT_EXIST : OB_FILE_NOT_OPENED;
        else { errno = opened.error; ret = convert_sys_errno(); }
        break;
      }
      if (OB_FAIL(SMART_CALL(clean_directory(*child, current_file_path, selected ? Cleanup::All : Cleanup::Temporary, pool)))) break;
      if (selected) {
        const std::string leaf(name.ptr(), name.length());
        ret = pool == nullptr
            ? directory_error(file_io::remove_dir_at(directory, {entry.name.data(), entry.name.size()}))
            : pool->remove_directory_at(directory, leaf.c_str());
      }
    } else if (selected && pool != nullptr) {
      char terminated[OB_MAX_FILE_NAME_LENGTH] = {};
      if (name.length() >= sizeof(terminated)) { ret = OB_ERR_UNEXPECTED; break; }
      MEMCPY(terminated, name.ptr(), name.length());
      ret = pool->remove_block_at(directory, terminated);
    } else if (selected) {
      ret = directory_error(file_io::remove_file_at(directory, {entry.name.data(), entry.name.size()}));
    }
  }
  return ret;
}

int remove_root(const char *path, ILogBlockPool *pool)
{
  auto parent = file_io::new_directory_handle();
  std::string leaf;
  int ret = open_parent_directory(path, *parent, leaf);
  if (OB_FAIL(ret)) return ret;
  auto directory = file_io::new_directory_handle();
  const auto error = file_io::open_child_directory(*parent, {reinterpret_cast<const uint8_t *>(leaf.data()), leaf.size()}, *directory);
  if (error.domain != file_io::ErrorDomain::None) {
    if (pool == nullptr) return error.error == ENOENT ? OB_ENTRY_NOT_EXIST : OB_FILE_NOT_OPENED;
    errno = error.error;
    return convert_sys_errno();
  }
  ret = clean_directory(*directory, path, Cleanup::All, pool);
  if (OB_SUCC(ret)) ret = pool == nullptr
      ? directory_error(file_io::remove_dir_at(*parent, {reinterpret_cast<const uint8_t *>(leaf.data()), leaf.size()}))
      : pool->remove_directory_at(*parent, leaf.c_str());
  return ret;
}

int clean_root(const char *path, ILogBlockPool *pool)
{
  if (path == nullptr) return OB_INVALID_ARGUMENT;
  auto directory = file_io::new_directory_handle();
  const auto error = directory->open_directory({reinterpret_cast<const uint8_t *>(path), strlen(path)});
  if (error.domain != file_io::ErrorDomain::None) return OB_ERR_SYS;
  return clean_directory(*directory, path, Cleanup::RuntimeTemporary, pool);
}
}

int remove_directory_rec(const char *path, ILogBlockPool *log_block_pool)
{
  return log_block_pool == nullptr ? OB_INVALID_ARGUMENT : remove_root(path, log_block_pool);
}
int remove_tmp_file_or_directory_at(const char *path, ILogBlockPool *log_block_pool)
{
  return log_block_pool == nullptr ? OB_INVALID_ARGUMENT : clean_root(path, log_block_pool);
}
int remove_tmp_file_or_directory_at(const file_io::DirectoryHandle &directory, const char *path, ILogBlockPool *log_block_pool)
{
  return log_block_pool == nullptr ? OB_INVALID_ARGUMENT : clean_directory(directory, path, Cleanup::RuntimeTemporary, log_block_pool);
}
int delete_directory_rec(const char *path)
{
  return remove_root(path, nullptr);
}
int delete_tmp_file_or_directory_at(const char *path)
{
  return clean_root(path, nullptr);
}
}
}
