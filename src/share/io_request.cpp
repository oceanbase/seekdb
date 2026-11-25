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

#define USING_LOG_PREFIX COMMON

#include "share/io_request.h"
#include "share/ob_errno.h"
#include "lib/utility/ob_tracepoint.h"
#include <cerrno>
#include <algorithm>
#include <stdexcept>

using namespace oceanbase;
using namespace oceanbase::common;

/******************             IOMode              **********************/
static const char *read_mode_name = "READ";
static const char *write_mode_name = "WRITE";
const char *oceanbase::common::get_io_mode_string(const ObIOMode mode)
{
  const char *ret_name = "UNKNOWN";
  switch (mode) {
    case ObIOMode::READ:
      ret_name = read_mode_name;
      break;
    case ObIOMode::WRITE:
      ret_name = write_mode_name;
      break;
    default:
      break;
  }
  return ret_name;
}

storage_io::RequestOptions oceanbase::common::make_request_options(const ObIOFlag &descriptor,
    storage_io::RequestMode mode, int64_t offset, int64_t length, int64_t timeout_us)
{
  storage_io::RequestOptions options{};
  options.mode = mode;
  options.offset = offset;
  options.length = length;
  options.timeout_us = timeout_us;
  options.worker_required = descriptor.is_sync();
  options.worker_fault = OB_E(EventTable::EN_IO_HANG_ERROR) OB_SUCCESS;
  const size_t module = descriptor.is_sys_module()
      ? descriptor.get_sys_module_id() - SYS_MODULE_START_ID + 1 : 0;
  options.statistics_lane = module * 2 + static_cast<size_t>(mode);
  return options;
}

/******************             IOCallback              **********************/
ObIOCallback::ObIOCallback(const ObIOCallbackType type)
  : type_(type)
{

}

ObIOCallback::~ObIOCallback()
{

}

int ObIOCallback::alloc_and_copy_data(const char *io_data_buffer, const int64_t data_size, common::ObIAllocator *allocator, char *&data_buffer)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(allocator) || nullptr == io_data_buffer) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(data_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(NULL == (data_buffer = (char*) (allocator->alloc(data_size))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("fail to allocate memory", K(ret), K(data_size));
  } else {
    MEMCPY(data_buffer, io_data_buffer, data_size);
  }
  return ret;
}

int64_t oceanbase::common::get_io_interval(const int64_t &end_time, const int64_t &begin_time)
{
  return std::min(std::max(end_time - begin_time, static_cast<int64_t>(0)),
      static_cast<int64_t>(600L * 1000L * 1000L));
}

ObIOHandle::ObIOHandle(const ObIOHandle &other)
{
  if (other.request_) { request_.emplace((*other.request_)->clone_handle()); }
}

ObIOHandle &ObIOHandle::operator=(const ObIOHandle &other)
{
  if (this != &other) {
    if (other.request_) { request_.emplace((*other.request_)->clone_handle()); }
    else { request_.reset(); }
  }
  return *this;
}

void ObIOHandle::set_request(rust::Box<storage_io::Request> request)
{
  request_.emplace(std::move(request));
}

bool ObIOHandle::is_finished() const
{
  return request_ && (*request_)->is_finished();
}

bool ObIOHandle::cancel() const
{
  return request_ && (*request_)->request_cancel();
}

int ObIOHandle::wait(uint64_t wait_timeout_ms)
{
  if (!request_) { return OB_NOT_INIT; }
  const uint64_t timeout_us = wait_timeout_ms > UINT64_MAX / 1000
      ? UINT64_MAX : wait_timeout_ms * 1000;
  const int error = (*request_)->wait(timeout_us);
  if (error != 0) { return convert_storage_error(error); }
  const auto result = (*request_)->result();
  if (result.callback_error != 0) { return result.callback_error; }
  using storage_io::CompletionCode;
  switch (result.code) {
    case CompletionCode::Complete: return OB_SUCCESS;
    case CompletionCode::ShortIo: return OB_DATA_OUT_OF_RANGE;
    case CompletionCode::IncompleteEnvelope: return OB_NOT_SUPPORTED;
    case CompletionCode::TimedOut: return OB_TIMEOUT;
    case CompletionCode::Cancelled:
    case CompletionCode::Shutdown: return OB_CANCELED;
    default: return OB_IO_ERROR;
  }
}

const char *ObIOHandle::get_buffer() const
{
  if (!request_ || !(*request_)->is_finished()) { return nullptr; }
  if ((*request_)->has_callback_output()) { return (*request_)->callback_output().data(); }
  const auto data = (*request_)->data();
  return data.empty() ? nullptr : reinterpret_cast<const char *>(data.data());
}

const oceanbase::storage_io::CallbackAdapter *ObIOHandle::get_callback_output() const
{
  return request_ && (*request_)->has_callback_output()
      ? &(*request_)->callback_output() : nullptr;
}

int64_t ObIOHandle::get_data_size() const
{
  return request_ ? (*request_)->data_size() : 0;
}

int ObIOHandle::get_fs_errno(int &io_errno) const
{
  if (!request_) { return OB_NOT_INIT; }
  io_errno = (*request_)->result().os_error;
  return OB_SUCCESS;
}

int ObIOHandle::get_io_time_us(int64_t &io_time_us) const
{
  if (!request_) { return OB_NOT_INIT; }
  io_time_us = (*request_)->elapsed_us();
  return OB_SUCCESS;
}

namespace oceanbase {
namespace storage_io {
namespace {
void destroy_callback(common::ObIOCallback *callback) noexcept
{
  if (callback != nullptr) {
    auto *allocator = callback->get_allocator();
    callback->~ObIOCallback();
    allocator->free(callback);
  }
}
}

CallbackAdapter::~CallbackAdapter()
{
  destroy_callback(callback_);
}

std::unique_ptr<CallbackAdapter> adopt_callback(common::ObIOCallback *callback) noexcept
{
  if (callback == nullptr) { return nullptr; }
  std::unique_ptr<common::ObIOCallback, decltype(&destroy_callback)> owner(callback, destroy_callback);
  try {
    auto adapter = std::unique_ptr<CallbackAdapter>(new CallbackAdapter(owner.get()));
    owner.release();
    return adapter;
  } catch (const std::bad_alloc &) {
    return nullptr;
  }
}

int32_t CallbackAdapter::process(rust::Slice<const uint8_t> input)
{
  try {
    return callback_->process(reinterpret_cast<const char *>(input.data()), input.size());
  } catch (const std::exception &) {
    throw;
  } catch (...) {
    throw std::runtime_error("I/O callback threw an unknown exception");
  }
}

const char *CallbackAdapter::data() const
{
  return callback_->get_data();
}

const blocksstable::ObMicroBlockCacheValue *CallbackAdapter::micro_block() const
{
  return callback_->get_type() == common::ObIOCallbackType::ASYNC_SINGLE_MICRO_BLOCK_CALLBACK
      ? reinterpret_cast<const blocksstable::ObMicroBlockCacheValue *>(callback_->get_data()) : nullptr;
}

const blocksstable::ObMultiBlockIOResult *CallbackAdapter::micro_blocks() const
{
  return callback_->get_type() == common::ObIOCallbackType::MULTI_DATA_BLOCK_CALLBACK
      ? reinterpret_cast<const blocksstable::ObMultiBlockIOResult *>(callback_->get_data()) : nullptr;
}

const blocksstable::ObMicroBlockData *CallbackAdapter::decoded_micro_block() const
{
  return callback_->get_type() == common::ObIOCallbackType::SYNC_SINGLE_MICRO_BLOCK_CALLBACK
      ? reinterpret_cast<const blocksstable::ObMicroBlockData *>(callback_->get_data()) : nullptr;
}

} // namespace storage_io
} // namespace oceanbase

int oceanbase::common::accept_io_request(rust::Box<storage_io::Request> request, ObIOHandle &handle)
{
  handle.reset();
  const int ret = convert_storage_error(request->submission_error());
  if (ret == OB_SUCCESS) { handle.set_request(std::move(request)); }
  return ret;
}

int oceanbase::common::convert_file_error(int error)
{
  switch (error) {
    case 0: return OB_SUCCESS;
    case EACCES: return OB_FILE_OR_DIRECTORY_PERMISSION_DENIED;
    case ENOENT: return OB_NO_SUCH_FILE_OR_DIRECTORY;
    case EEXIST:
    case ENOTEMPTY: return OB_FILE_OR_DIRECTORY_EXIST;
    case ETIMEDOUT: return OB_TIMEOUT;
    case EAGAIN: return OB_EAGAIN;
#ifndef _WIN32
    case EDQUOT:
#endif
    case ENOSPC: return OB_SERVER_OUTOF_DISK_SPACE;
    case EMFILE: return OB_TOO_MANY_OPEN_FILES;
    default: return OB_IO_ERROR;
  }
}

int oceanbase::common::convert_storage_error(int error)
{
  switch (error) {
    case EINTR: return OB_CANCELED;
    case EINVAL: return OB_INVALID_ARGUMENT;
    case ENOMEM: return OB_ALLOCATE_MEMORY_FAILED;
    case ENOSYS: return OB_NOT_SUPPORTED;
    case EBADF: return OB_NOT_INIT;
    case EPIPE: return OB_STATE_NOT_MATCH;
    default: return convert_file_error(error);
  }
}

int oceanbase::common::convert_block_status(const storage_io::BlockStatus &status)
{
  switch (status.code) {
    case storage_io::BlockCode::Ok: return OB_SUCCESS;
    case storage_io::BlockCode::InvalidArgument: return OB_INVALID_ARGUMENT;
    case storage_io::BlockCode::InvalidHeader: return OB_INVALID_DATA;
    case storage_io::BlockCode::ChecksumMismatch: return OB_PHYSIC_CHECKSUM_ERROR;
    case storage_io::BlockCode::Exhausted: return OB_SERVER_OUTOF_DISK_SPACE;
    case storage_io::BlockCode::Memory: return OB_ALLOCATE_MEMORY_FAILED;
    case storage_io::BlockCode::NotOpen: return OB_NOT_INIT;
    case storage_io::BlockCode::AlreadyOpen: return OB_INIT_TWICE;
    case storage_io::BlockCode::IoError: return status.error ? convert_file_error(status.error) : OB_IO_ERROR;
    default: return OB_ERR_UNEXPECTED;
  }
}

void switch_check_io_hang_errsim()
{
  oceanbase::storage_io::toggle_worker_fault_delay();
}
