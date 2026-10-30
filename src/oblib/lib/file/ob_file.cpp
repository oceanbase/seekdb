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

#include "lib/file/ob_file.h"
#include "rust/file_io.h"
#include <cerrno>
#include <limits>
#include <new>

namespace oceanbase
{
namespace common
{
namespace
{
int file_error(int error)
{
  switch (error) {
    case 0: return OB_SUCCESS;
    case ENOENT: return OB_FILE_NOT_EXIST;
    case EEXIST: return OB_FILE_ALREADY_EXIST;
    case EINVAL: return OB_INVALID_ARGUMENT;
    case ENOMEM: return OB_ALLOCATE_MEMORY_FAILED;
    case ENOSYS: return OB_NOT_SUPPORTED;
    case EALREADY: return OB_INIT_TWICE;
    case EBADF: return OB_NOT_INIT;
    default: return OB_IO_ERROR;
  }
}
}

struct ObFileReader::Impl
{
  rust::Box<file_io::ReaderFacade> owner = file_io::new_reader();
};
struct ObFileAppender::Impl
{
  rust::Box<file_io::AppenderFacade> owner = file_io::new_appender();
};

ObFileReader::ObFileReader() : file_(nullptr) {}
ObFileReader::~ObFileReader() { close(); }
bool ObFileReader::is_opened() const { return file_ != nullptr; }
void ObFileReader::close() { delete file_; file_ = nullptr; }
int ObFileReader::open(const ObString &fname, bool dio, int64_t align_size)
{
  int ret = OB_SUCCESS;
  if (is_opened()) {
    ret = OB_INIT_TWICE;
  } else if (fname.empty() || fname.ptr() == nullptr || align_size <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    Impl *candidate = new (std::nothrow) Impl();
    if (candidate == nullptr) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      ret = file_error(candidate->owner->open_reader(
          rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(fname.ptr()), fname.length()),
          dio, align_size));
      if (ret == OB_SUCCESS) { file_ = candidate; } else { delete candidate; }
    }
  }
  return ret;
}
int ObFileReader::pread(void *buf, int64_t count, int64_t offset, int64_t &read_size)
{
  read_size = 0;
  if (!is_opened()) { return OB_NOT_INIT; }
  if (count < 0 || offset < 0 || (count > 0 && buf == nullptr)) { return OB_INVALID_ARGUMENT; }
  const auto result = file_->owner->read(rust::Slice<uint8_t>(static_cast<uint8_t *>(buf), count), offset);
  read_size = result.bytes;
  return file_error(result.error);
}
int ObFileReader::get_file_size(int64_t &size) const
{
  size = 0;
  if (!is_opened()) { return OB_NOT_INIT; }
  uint64_t length = 0;
  int ret = file_error(file_->owner->reader_len(length));
  if (ret == OB_SUCCESS && length > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    ret = OB_SIZE_OVERFLOW;
  }
  if (ret == OB_SUCCESS) { size = length; }
  return ret;
}

ObFileAppender::ObFileAppender() : file_(nullptr) {}
ObFileAppender::~ObFileAppender() { delete file_; }
bool ObFileAppender::is_opened() const { return file_ != nullptr && file_->owner->is_open(); }
int64_t ObFileAppender::position() const { return file_ == nullptr ? -1 : file_->owner->position(); }
int ObFileAppender::open_(const ObString &fname, bool dio, uint8_t create, int64_t align_size)
{
  if (is_opened()) { return OB_INIT_TWICE; }
  if (fname.empty() || fname.ptr() == nullptr || align_size <= 0) { return OB_INVALID_ARGUMENT; }
  if (file_ == nullptr) {
    file_ = new (std::nothrow) Impl();
    if (file_ == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
  }
  return file_error(file_->owner->open_appender(
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(fname.ptr()), fname.length()),
      create, dio, align_size));
}
int ObFileAppender::open(const ObString &fname, bool dio, bool is_create, bool is_trunc, int64_t align_size)
{
  return open_(fname, dio, (is_create ? 1 : 0) + (is_trunc ? 2 : 0), align_size);
}
int ObFileAppender::create(const ObString &fname, bool dio, int64_t align_size)
{
  return open_(fname, dio, 4, align_size);
}
int ObFileAppender::append(const void *buf, int64_t count)
{
  if (file_ == nullptr) { return OB_NOT_INIT; }
  if (count < 0 || (count > 0 && buf == nullptr)) { return OB_INVALID_ARGUMENT; }
  return file_error(file_->owner->append(
      rust::Slice<const uint8_t>(static_cast<const uint8_t *>(buf), count)).error);
}
int ObFileAppender::fsync() { return file_ == nullptr ? OB_NOT_INIT : file_error(file_->owner->sync()); }
int ObFileAppender::finish() { return file_ == nullptr ? OB_SUCCESS : file_error(file_->owner->finish()); }
}
}
