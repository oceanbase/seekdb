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

#define USING_LOG_PREFIX WAL
#include "logservice/wal_adapter/handle_impl.h"
#include <cstring>
#include <string>
#include "lib/time/ob_time_utility.h"
#include "share/ob_structured_event_logger.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace wal
{
namespace
{
void assign_scn(SCN &scn, const uint64_t value)
{
  scn = from_rust_scn(value);
}

}

namespace wal_rs
{
void publish_flush_ffi(uint8_t *context, const uint64_t end_lsn,
    const uint64_t end_scn_value)
{
  auto *callbacks = reinterpret_cast<FsyncCallbackWrapper *>(context);
  SCN end_scn;
  assign_scn(end_scn, end_scn_value);
  (void)callbacks->update_end_lsn(LSN(end_lsn), end_scn);
}

void report_fatal_ffi(rust::Str path, rust::Str operation, const int32_t raw_os_error,
    const bool no_space)
{
  const int ret = no_space ? common::OB_LOG_OUTOF_DISK_SPACE : OB_IO_ERROR;
  const std::string segment(path);
  const std::string action(operation);
  LOG_ERROR("fatal WAL I/O failure", KR(ret), "segment", segment.c_str(),
      "operation", action.c_str(), "raw_os_error", raw_os_error, KERRNOMSG(raw_os_error));
  fprintf(stderr, "FATAL WAL I/O failure: segment=%s operation=%s raw_os_error=%d (%s) ret=%d\n",
      segment.c_str(), action.c_str(), raw_os_error, strerror(raw_os_error), ret);
  fflush(stderr);
  OB_LOGGER.stop();
  OB_LOGGER.wait();
  _Exit(1);
}
}

HandleImpl::HandleImpl()
    : lock_(common::ObLatchIds::WAL_HANDLE_IMPL_LOCK),
      stream_(),
      fs_cb_wrapper_(),
      io_lock_(common::ObLatchIds::WAL_HANDLE_IMPL_LOCK),
      active_io_count_(0),
      io_start_time_(OB_INVALID_TIMESTAMP),
      is_inited_(false)
{}

HandleImpl::~HandleImpl()
{
  destroy();
}

int HandleImpl::init(::rust::Box<wal_rs::Stream> stream)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else {
    stream_.emplace(std::move(stream));
    ret = from_rust_code(wal_rs::status_ffi(**stream_));
    if (OB_SUCC(ret)) {
      ret = from_rust_code(wal_rs::set_flush_callback_ffi(
          **stream_, reinterpret_cast<uint8_t *>(&fs_cb_wrapper_)));
    }
    if (OB_SUCC(ret)) {
      ret = from_rust_code(wal_rs::set_failure_callback_ffi(**stream_));
    }
    if (OB_SUCC(ret)) {
      is_inited_ = true;
    } else {
      stream_.reset();
    }
  }
  return ret;
}

bool HandleImpl::check_can_be_used() const
{
  RLockGuard guard(lock_);
  return is_inited_;
}

void HandleImpl::destroy()
{
  WLockGuard guard(lock_);
  if (is_inited_) {
    (void)wal_rs::close_ffi(**stream_);
    stream_.reset();
    is_inited_ = false;
  }
}

int HandleImpl::submit_log(const char *buf, const int64_t buf_len, const SCN &ref_scn,
    LSN &lsn, SCN &scn)
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  if (!is_inited_ || OB_ISNULL(buf) || buf_len <= 0 || buf_len > MAX_LOG_BODY_SIZE
      || !ref_scn.is_valid()) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else {
    IoScope io_scope(*this);
    const auto result = wal_rs::submit_ffi(**stream_,
        ::rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(buf), buf_len),
        ref_scn.get_val_for_logservice());
    ret = from_rust_code(result.code);
    if (OB_SUCC(ret)) {
      lsn = LSN(result.lsn);
      assign_scn(scn, result.scn);
    }
  }
  return ret;
}

int HandleImpl::submit_imported_groups(const ImportedGroups &groups)
{
  int ret = OB_SUCCESS;
  LSN end_lsn;
  SCN end_scn;
  RLockGuard guard(lock_);
  if (!is_inited_ || groups.empty()) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else {
    IoScope io_scope(*this);
    for (const auto &group : groups) {
      const LSN source_lsn(static_cast<int64_t>(group.source_lsn));
      SCN source_scn;
      assign_scn(source_scn, group.source_scn);
      if (!source_lsn.is_valid() || !source_scn.is_valid()
          || group.bytes.empty() || group.bytes.size() > MAX_LOG_BUFFER_SIZE) {
        ret = OB_INVALID_ARGUMENT;
        break;
      }
    }
    if (OB_SUCC(ret)) {
      const auto result = wal_rs::append_imported_ffi(**stream_, {groups.data(), groups.size()});
      ret = from_rust_code(result.code);
      if (OB_SUCC(ret)) {
        end_lsn = LSN(result.end_lsn);
        assign_scn(end_scn, result.end_scn);
      }
    }
  }
  if (OB_SUCC(ret)) {
    (void)fs_cb_wrapper_.update_end_lsn(end_lsn, end_scn);
  }
  return ret;
}

int HandleImpl::set_base_lsn(const LSN &lsn)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  const LSN block_lsn(lsn.val_ / WAL_BLOCK_SIZE * WAL_BLOCK_SIZE);
  if (!is_inited_ || !lsn.is_valid()) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else if (OB_FAIL(from_rust_code(wal_rs::set_base_lsn_ffi(**stream_, block_lsn.val_).code))) {
  } else {
    SERVER_EVENT_ADD("LOG", "SET BASE LSN", "NEW_BASE_LSN", block_lsn);
  }
  return ret;
}

int HandleImpl::locate_by_scn_coarsely(const SCN &scn, LSN &result_lsn)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  result_lsn.reset();
  if (!is_inited_ || !scn.is_valid()) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else {
    const auto result = wal_rs::locate_by_scn_ffi(**stream_, scn.get_val_for_logservice());
    ret = from_rust_code(result.code);
    if (OB_SUCC(ret)) {
      result_lsn = LSN(result.lsn);
    }
  }
  return ret;
}

int HandleImpl::locate_by_lsn_coarsely(const LSN &lsn, SCN &result_scn)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  result_scn.reset();
  if (!is_inited_ || !lsn.is_valid()) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else {
    const auto result = wal_rs::locate_by_lsn_ffi(**stream_, lsn.val_);
    ret = from_rust_code(result.code);
    if (OB_SUCC(ret)) {
      assign_scn(result_scn, result.scn);
    }
  }
  return ret;
}

int HandleImpl::alloc_wal_buffer_iterator(const LSN &offset, EntryIterator &iterator)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_ || !offset.is_valid()) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else {
    ret = iterator.init(wal_rs::cursor_ffi(**stream_, offset.val_, false));
  }
  return ret;
}

int HandleImpl::alloc_wal_group_buffer_iterator(const LSN &offset,
    GroupIterator &iterator)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_ || !offset.is_valid()) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else {
    ret = iterator.init(wal_rs::cursor_ffi(**stream_, offset.val_, true));
  }
  return ret;
}

template<typename IteratorType>
int HandleImpl::alloc_iterator_from_scn_(const SCN &scn, IteratorType &iterator)
{
  int ret = OB_SUCCESS;
  LSN start_lsn;
  if (OB_FAIL(locate_by_scn_coarsely(scn, start_lsn))) {
    ret = OB_ITER_END == ret ? OB_ENTRY_NOT_EXIST : ret;
  } else {
    RLockGuard guard(lock_);
    constexpr bool group_mode = std::is_same<IteratorType, GroupIterator>::value;
    ret = iterator.init(wal_rs::cursor_ffi(**stream_, start_lsn.val_, group_mode));
  }
  while (OB_SUCC(ret) && OB_SUCC(iterator.next())) {
    const char *buffer = NULL;
    int64_t nbytes = 0;
    SCN entry_scn;
    LSN entry_lsn;
    if (OB_FAIL(iterator.get_entry(buffer, nbytes, entry_scn, entry_lsn))) {
    } else if (entry_scn >= scn) {
      break;
    }
  }
  if (OB_ITER_END == ret) {
    ret = OB_ENTRY_NOT_EXIST;
  }
  if (OB_FAIL(ret)) {
    iterator.destroy();
  }
  return ret;
}

int HandleImpl::alloc_wal_buffer_iterator(const SCN &scn, EntryIterator &iterator)
{
  return alloc_iterator_from_scn_(scn, iterator);
}

int HandleImpl::alloc_wal_group_buffer_iterator(const SCN &scn,
    GroupIterator &iterator)
{
  return alloc_iterator_from_scn_(scn, iterator);
}

int HandleImpl::register_file_size_cb(wal::FsyncCallbackNode *fs_cb)
{
  RLockGuard guard(lock_);
  return is_inited_ ? fs_cb_wrapper_.add_cb_impl(fs_cb) : OB_NOT_INIT;
}

int HandleImpl::unregister_file_size_cb(wal::FsyncCallbackNode *fs_cb)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    fs_cb_wrapper_.del_cb_impl(fs_cb);
  }
  return ret;
}

int HandleImpl::get_base_lsn(LSN &lsn) const
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    const auto progress = wal_rs::progress_ffi(**stream_);
    ret = from_rust_code(progress.code);
    if (OB_SUCC(ret)) {
      lsn = LSN(progress.base_lsn);
    }
  }
  return ret;
}

int HandleImpl::get_base_info(const LSN &base_lsn, BaseInfo &base_info)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_ || !base_lsn.is_valid()) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else {
    const auto result = wal_rs::base_info_ffi(**stream_, base_lsn.val_);
    ret = from_rust_code(result.code);
    if (OB_SUCC(ret)) {
      LogInfo previous;
      previous.generate_by_default();
      previous.log_id_ = result.info.prev_log_id;
      previous.lsn_ = LSN(result.info.prev_lsn);
      assign_scn(previous.scn_, result.info.prev_scn);
      previous.accum_checksum_ = static_cast<int64_t>(result.info.prev_accumulated_checksum);
      base_info.generate(LSN(result.info.curr_lsn), previous);
    }
  }
  return ret;
}

const LSN HandleImpl::get_end_lsn() const
{
  RLockGuard guard(lock_);
  LSN end_lsn;
  if (is_inited_) {
    const auto progress = wal_rs::progress_ffi(**stream_);
    if (wal_rs::Code::Ok == progress.code) {
      end_lsn = LSN(progress.end_lsn);
    }
  }
  return end_lsn;
}

const SCN HandleImpl::get_end_scn() const
{
  RLockGuard guard(lock_);
  SCN end_scn;
  if (is_inited_) {
    const auto progress = wal_rs::progress_ffi(**stream_);
    if (wal_rs::Code::Ok == progress.code) {
      assign_scn(end_scn, progress.end_scn);
    }
  }
  return end_scn;
}

int HandleImpl::stat(Stat &wal_stat)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    const auto progress = wal_rs::progress_ffi(**stream_);
    ret = from_rust_code(progress.code);
    if (OB_SUCC(ret)) {
      assign_scn(wal_stat.begin_scn_, progress.begin_scn);
      wal_stat.base_lsn_ = LSN(progress.base_lsn);
      wal_stat.end_lsn_ = LSN(progress.end_lsn);
      assign_scn(wal_stat.end_scn_, progress.end_scn);
    }
  }
  return ret;
}

void HandleImpl::begin_io_()
{
  ObSpinLockGuard guard(io_lock_);
  if (0 == active_io_count_++) {
    io_start_time_ = ObTimeUtility::fast_current_time();
  }
}

void HandleImpl::end_io_()
{
  ObSpinLockGuard guard(io_lock_);
  if (0 == --active_io_count_) {
    io_start_time_ = OB_INVALID_TIMESTAMP;
  }
}

int HandleImpl::get_io_start_time(int64_t &io_start_time) const
{
  ObSpinLockGuard guard(io_lock_);
  io_start_time = io_start_time_;
  return OB_SUCCESS;
}

Stat::Stat()
    : begin_scn_(), base_lsn_(), end_lsn_(), end_scn_()
{}

bool Stat::is_valid() const
{
  return begin_scn_.is_valid() && base_lsn_.is_valid() && end_lsn_.is_valid() && end_scn_.is_valid();
}

void Stat::reset()
{
  begin_scn_.reset();
  base_lsn_.reset();
  end_lsn_.reset();
  end_scn_.reset();
}
} // namespace wal
} // namespace oceanbase
