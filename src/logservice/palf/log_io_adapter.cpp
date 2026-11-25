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

#define USING_LOG_PREFIX PALF
#include "log_io_adapter.h"
#include <cstring>
#include "share/io_request.h"

namespace oceanbase::palf
{
using namespace common;

int LogIOAdapter::init(const storage_io::NamedOwnerHandle *log_files)
{
  if (log_files == nullptr) { return OB_INVALID_ARGUMENT; }
  if (is_valid()) { return OB_INIT_TWICE; }
  log_files_ = log_files;
  return OB_SUCCESS;
}

int LogIOAdapter::open(const char *path, bool read_only, LogFile &file)
{
  if (!is_valid()) { return OB_NOT_INIT; }
  if (path == nullptr || path[0] == '\0') { return OB_INVALID_ARGUMENT; }
  if (file) { return OB_INIT_TWICE; }
  auto opened = storage_io::new_named_file();
  const int ret = convert_storage_error(log_files_->open_named_file(
      {reinterpret_cast<const uint8_t *>(path), strlen(path)}, read_only, *opened));
  if (ret == OB_SUCCESS) { file.emplace(std::move(opened)); }
  return ret;
}

int LogIOAdapter::pwrite(const LogFile &file, const char *buf, int64_t count,
    int64_t offset, int64_t &write_size)
{
  write_size = 0;
  if (!is_valid()) { return OB_NOT_INIT; }
  if (!file || buf == nullptr || count <= 0 || offset < 0) { return OB_INVALID_ARGUMENT; }
  ObIOFlag descriptor;
  descriptor.set_write();
  descriptor.set_sync();
  descriptor.set_sys_module_id(CLOG_WRITE_IO);
  auto options = make_request_options(descriptor, storage_io::RequestMode::Write,
      offset, count, 300L * 1000L * 1000L);
  options.deadline_before_start = true;
  ObIOHandle handle;
  int ret = accept_io_request((*file)->submit_named(options,
      {reinterpret_cast<const uint8_t *>(buf), static_cast<size_t>(count)}, nullptr), handle);
  if (ret == OB_SUCCESS) {
    ret = handle.wait();
    if (ret == OB_SUCCESS || ret == OB_DATA_OUT_OF_RANGE) { write_size = handle.get_data_size(); }
    if (ret == OB_SUCCESS && write_size != count) { ret = OB_ERR_UNEXPECTED; }
  }
  return ret;
}

int LogIOAdapter::pread(const LogFile &file, int64_t count, int64_t offset,
    char *buf, int64_t &read_size)
{
  read_size = 0;
  if (!is_valid()) { return OB_NOT_INIT; }
  if (!file || buf == nullptr || count < 0 || offset < 0) { return OB_INVALID_ARGUMENT; }
  ObIOFlag descriptor;
  descriptor.set_read();
  descriptor.set_sync();
  descriptor.set_sys_module_id(CLOG_READ_IO);
  const auto options = make_request_options(descriptor, storage_io::RequestMode::Read,
      offset, count, 300L * 1000L * 1000L);
  ObIOHandle handle;
  int ret = accept_io_request((*file)->submit_named(options, {}, nullptr), handle);
  if (ret == OB_SUCCESS) {
    ret = handle.wait();
    if (ret == OB_SUCCESS || ret == OB_DATA_OUT_OF_RANGE) {
      read_size = handle.get_data_size();
      if (read_size > 0) { MEMCPY(buf, handle.get_buffer(), read_size); }
    }
    if (ret == OB_SUCCESS && read_size != count) { ret = OB_ERR_UNEXPECTED; }
  }
  return ret;
}

int LogIOAdapter::pread_direct(const LogFile &file, int64_t count, int64_t offset,
    char *buf, int64_t &read_size)
{
  read_size = 0;
  if (!is_valid()) { return OB_NOT_INIT; }
  if (!file || buf == nullptr || count < 0 || offset < 0) { return OB_INVALID_ARGUMENT; }
  const auto result = (*file)->read_named(
      {reinterpret_cast<uint8_t *>(buf), static_cast<size_t>(count)}, offset);
  read_size = result.bytes;
  const int ret = convert_storage_error(result.error);
  return ret != OB_SUCCESS ? ret : (read_size == count ? OB_SUCCESS : OB_IO_ERROR);
}

int LogIOAdapter::truncate(const LogFile &file, int64_t length)
{
  if (!is_valid()) { return OB_NOT_INIT; }
  if (!file || length < 0) { return OB_INVALID_ARGUMENT; }
  return convert_storage_error((*file)->resize_named(length));
}
}
