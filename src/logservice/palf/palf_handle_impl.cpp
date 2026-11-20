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
#include "palf_handle_impl.h"
#include <cstring>
#include "lib/time/ob_time_utility.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace palf
{
namespace
{
void assign_scn(SCN &scn, const uint64_t value)
{
  scn = from_rust_scn(value);
}

palf_rs::DiskPolicy to_rust_disk_policy(const PalfDiskOptions &options)
{
  return palf_rs::DiskPolicy {
      static_cast<uint64_t>(options.log_disk_usage_limit_size_),
      static_cast<uint8_t>(options.log_disk_utilization_limit_threshold_),
      static_cast<uint8_t>(options.log_disk_throttling_percentage_),
      static_cast<uint64_t>(options.log_disk_throttling_maximum_duration_)};
}
}

PalfHandleImpl::PalfHandleImpl()
    : lock_(common::ObLatchIds::PALF_HANDLE_IMPL_LOCK),
      stream_(),
      self_(),
      access_mode_(AccessMode::INVALID_ACCESS_MODE),
      fs_cb_wrapper_(),
      plugins_(),
      palf_epoch_(0),
      io_lock_(common::ObLatchIds::PALF_HANDLE_IMPL_LOCK),
      active_io_count_(0),
      io_start_time_(OB_INVALID_TIMESTAMP),
      is_inited_(false)
{}

PalfHandleImpl::~PalfHandleImpl()
{
  destroy();
}

int PalfHandleImpl::init(const AccessMode &access_mode,
    const PalfBaseInfo &palf_base_info, const char *log_dir,
    const PalfDiskOptions &disk_options, const common::ObAddr &self, const int64_t palf_epoch)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (!is_valid_access_mode(access_mode) || !palf_base_info.is_valid()
             || OB_ISNULL(log_dir) || !disk_options.is_valid() || !self.is_valid() || palf_epoch < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    palf_rs::BaseInfo base {
        static_cast<uint64_t>(palf_base_info.curr_lsn_.val_),
        static_cast<uint64_t>(palf_base_info.prev_log_info_.lsn_.val_),
        palf_base_info.prev_log_info_.scn_.get_val_for_logservice(),
        palf_base_info.prev_log_info_.log_id_,
        static_cast<uint64_t>(palf_base_info.prev_log_info_.accum_checksum_)};
    stream_.emplace(palf_rs::create_stream(
        ::rust::Str(log_dir), base, 0, to_rust_disk_policy(disk_options)));
    ret = from_rust_code(palf_rs::status_ffi(**stream_));
    if (OB_SUCC(ret)) {
      access_mode_ = access_mode;
      self_ = self;
      palf_epoch_ = palf_epoch;
      is_inited_ = true;
    } else {
      stream_.reset();
    }
  }
  return ret;
}

int PalfHandleImpl::load(const char *log_dir, const PalfDiskOptions &disk_options,
    const common::ObAddr &self, const int64_t palf_epoch, bool &is_integrity)
{
  int ret = OB_SUCCESS;
  is_integrity = false;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(log_dir) || !disk_options.is_valid() || !self.is_valid() || palf_epoch < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    stream_.emplace(palf_rs::open_stream(::rust::Str(log_dir), to_rust_disk_policy(disk_options)));
    ret = from_rust_code(palf_rs::status_ffi(**stream_));
    if (OB_SUCC(ret)) {
      is_integrity = palf_rs::integrity_ffi(**stream_);
      access_mode_ = AccessMode::APPEND;
      self_ = self;
      palf_epoch_ = palf_epoch;
      is_inited_ = true;
    } else {
      stream_.reset();
    }
  }
  return ret;
}

bool PalfHandleImpl::check_can_be_used() const
{
  RLockGuard guard(lock_);
  return is_inited_;
}

void PalfHandleImpl::destroy()
{
  WLockGuard guard(lock_);
  if (is_inited_) {
    (void)palf_rs::close_ffi(**stream_);
    stream_.reset();
    is_inited_ = false;
    plugins_.destroy();
  }
}

int PalfHandleImpl::set_disk_options(const PalfDiskOptions &disk_options)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_ || !disk_options.is_valid()) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else {
    ret = from_rust_code(palf_rs::set_disk_policy_ffi(**stream_, to_rust_disk_policy(disk_options)));
  }
  return ret;
}

int PalfHandleImpl::submit_log(const PalfAppendOptions &opts, const char *buf,
    const int64_t buf_len, const SCN &ref_scn, LSN &lsn, SCN &scn)
{
  int ret = OB_SUCCESS;
  LSN end_lsn;
  SCN end_scn;
  (void)opts;
  RLockGuard guard(lock_);
  if (!is_inited_ || OB_ISNULL(buf) || buf_len <= 0 || buf_len > MAX_LOG_BODY_SIZE
      || !ref_scn.is_valid()) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else {
    IoScope io_scope(*this);
    const auto result = palf_rs::append_ffi(**stream_,
        ::rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(buf), buf_len),
        ref_scn.get_val_for_logservice());
    ret = from_rust_code(result.code);
    if (OB_SUCC(ret)) {
      lsn = LSN(result.lsn);
      assign_scn(scn, result.scn);
      end_lsn = LSN(result.end_lsn);
      assign_scn(end_scn, result.end_scn);
    }
  }
  if (OB_SUCC(ret)) {
    (void)fs_cb_wrapper_.update_end_lsn(end_lsn, end_scn);
  }
  return ret;
}

int PalfHandleImpl::submit_imported_group(const LSN &source_lsn,
    const SCN &source_scn, const char *buf, const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  LSN end_lsn;
  SCN end_scn;
  RLockGuard guard(lock_);
  if (!is_inited_ || !source_lsn.is_valid() || !source_scn.is_valid()
      || OB_ISNULL(buf) || buf_len <= 0 || buf_len > MAX_LOG_BUFFER_SIZE) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else {
    IoScope io_scope(*this);
    const auto result = palf_rs::append_imported_ffi(**stream_, source_lsn.val_,
        source_scn.get_val_for_logservice(),
        ::rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(buf), buf_len));
    ret = from_rust_code(result.code);
    if (OB_SUCC(ret)) {
      end_lsn = LSN(result.end_lsn);
      assign_scn(end_scn, result.end_scn);
    }
  }
  if (OB_SUCC(ret)) {
    (void)fs_cb_wrapper_.update_end_lsn(end_lsn, end_scn);
  }
  return ret;
}

int PalfHandleImpl::set_base_lsn(const LSN &lsn)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  const LSN block_lsn(lsn.val_ / PALF_BLOCK_SIZE * PALF_BLOCK_SIZE);
  if (!is_inited_ || !lsn.is_valid()) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else if (OB_FAIL(from_rust_code(palf_rs::set_base_lsn_ffi(**stream_, block_lsn.val_).code))) {
  } else {
    (void)plugins_.record_set_base_lsn_event(block_lsn);
  }
  return ret;
}

int PalfHandleImpl::locate_by_scn_coarsely(const SCN &scn, LSN &result_lsn)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  result_lsn.reset();
  if (!is_inited_ || !scn.is_valid()) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else {
    const auto result = palf_rs::locate_by_scn_ffi(**stream_, scn.get_val_for_logservice());
    ret = from_rust_code(result.code);
    if (OB_SUCC(ret)) {
      result_lsn = LSN(result.lsn);
    }
  }
  return ret;
}

int PalfHandleImpl::locate_by_lsn_coarsely(const LSN &lsn, SCN &result_scn)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  result_scn.reset();
  if (!is_inited_ || !lsn.is_valid()) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else {
    const auto result = palf_rs::locate_by_lsn_ffi(**stream_, lsn.val_);
    ret = from_rust_code(result.code);
    if (OB_SUCC(ret)) {
      assign_scn(result_scn, result.scn);
    }
  }
  return ret;
}

int PalfHandleImpl::read_data_from_buffer(const LSN &read_begin_lsn,
    const int64_t in_read_size, char *buf, int64_t &out_read_size) const
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  out_read_size = 0;
  if (!is_inited_ || OB_ISNULL(buf) || in_read_size <= 0) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else {
    auto result = palf_rs::raw_read_ffi(**stream_, read_begin_lsn.val_, in_read_size);
    ret = from_rust_code(result.code);
    if (OB_SUCC(ret)) {
      out_read_size = result.bytes.size();
      memcpy(buf, result.bytes.data(), out_read_size);
    }
  }
  return ret;
}

int PalfHandleImpl::raw_read(const palf::LSN &lsn, char *buffer,
    const int64_t nbytes, int64_t &read_size, LogIOContext &io_ctx)
{
  (void)io_ctx;
  return read_data_from_buffer(lsn, nbytes, buffer, read_size);
}

int PalfHandleImpl::alloc_palf_buffer_iterator(const LSN &offset, PalfBufferIterator &iterator)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_ || !offset.is_valid()) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else {
    ret = iterator.init(palf_rs::cursor_ffi(**stream_, offset.val_, false));
  }
  return ret;
}

int PalfHandleImpl::alloc_palf_group_buffer_iterator(const LSN &offset,
    PalfGroupBufferIterator &iterator)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_ || !offset.is_valid()) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else {
    ret = iterator.init(palf_rs::cursor_ffi(**stream_, offset.val_, true));
  }
  return ret;
}

template<typename IteratorType>
int PalfHandleImpl::alloc_iterator_from_scn_(const SCN &scn, IteratorType &iterator)
{
  int ret = OB_SUCCESS;
  LSN start_lsn;
  if (OB_FAIL(locate_by_scn_coarsely(scn, start_lsn))) {
    ret = OB_ITER_END == ret ? OB_ENTRY_NOT_EXIST : ret;
  } else {
    RLockGuard guard(lock_);
    constexpr bool group_mode = std::is_same<IteratorType, PalfGroupBufferIterator>::value;
    ret = iterator.init(palf_rs::cursor_ffi(**stream_, start_lsn.val_, group_mode));
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

int PalfHandleImpl::alloc_palf_buffer_iterator(const SCN &scn, PalfBufferIterator &iterator)
{
  return alloc_iterator_from_scn_(scn, iterator);
}

int PalfHandleImpl::alloc_palf_group_buffer_iterator(const SCN &scn,
    PalfGroupBufferIterator &iterator)
{
  return alloc_iterator_from_scn_(scn, iterator);
}

int PalfHandleImpl::register_file_size_cb(palf::PalfFSCbNode *fs_cb)
{
  RLockGuard guard(lock_);
  return is_inited_ ? fs_cb_wrapper_.add_cb_impl(fs_cb) : OB_NOT_INIT;
}

int PalfHandleImpl::unregister_file_size_cb(palf::PalfFSCbNode *fs_cb)
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

int PalfHandleImpl::set_monitor_cb(PalfMonitorCb *monitor_cb)
{
  return plugins_.add_plugin(monitor_cb);
}

int PalfHandleImpl::reset_monitor_cb()
{
  return plugins_.del_plugin(static_cast<PalfMonitorCb *>(NULL));
}

int PalfHandleImpl::get_begin_lsn(LSN &lsn) const
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    const auto progress = palf_rs::progress_ffi(**stream_);
    ret = from_rust_code(progress.code);
    if (OB_SUCC(ret)) {
      lsn = LSN(progress.begin_lsn);
    }
  }
  return ret;
}

int PalfHandleImpl::get_begin_scn(SCN &scn)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    const auto progress = palf_rs::progress_ffi(**stream_);
    ret = from_rust_code(progress.code);
    if (OB_SUCC(ret)) {
      assign_scn(scn, progress.begin_scn);
    }
  }
  return ret;
}

int PalfHandleImpl::get_base_lsn(LSN &lsn) const
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    const auto progress = palf_rs::progress_ffi(**stream_);
    ret = from_rust_code(progress.code);
    if (OB_SUCC(ret)) {
      lsn = LSN(progress.base_lsn);
    }
  }
  return ret;
}

int PalfHandleImpl::get_base_info(const LSN &base_lsn, PalfBaseInfo &base_info)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_ || !base_lsn.is_valid()) {
    ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
  } else {
    const auto result = palf_rs::base_info_ffi(**stream_, base_lsn.val_);
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

const LSN PalfHandleImpl::get_end_lsn() const
{
  RLockGuard guard(lock_);
  LSN end_lsn;
  if (is_inited_) {
    const auto progress = palf_rs::progress_ffi(**stream_);
    if (palf_rs::Code::Ok == progress.code) {
      end_lsn = LSN(progress.end_lsn);
    }
  }
  return end_lsn;
}

const SCN PalfHandleImpl::get_end_scn() const
{
  RLockGuard guard(lock_);
  SCN end_scn;
  if (is_inited_) {
    const auto progress = palf_rs::progress_ffi(**stream_);
    if (palf_rs::Code::Ok == progress.code) {
      assign_scn(end_scn, progress.end_scn);
    }
  }
  return end_scn;
}

int PalfHandleImpl::get_total_used_disk_space(int64_t &total_used_disk_space,
    int64_t &unrecyclable_disk_space) const
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    const auto progress = palf_rs::progress_ffi(**stream_);
    ret = from_rust_code(progress.code);
    if (OB_SUCC(ret)) {
      total_used_disk_space = progress.used_disk;
      unrecyclable_disk_space = progress.unrecyclable_disk;
    }
  }
  return ret;
}

int PalfHandleImpl::stat(PalfStat &palf_stat)
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    const auto progress = palf_rs::progress_ffi(**stream_);
    ret = from_rust_code(progress.code);
    if (OB_SUCC(ret)) {
      palf_stat.self_ = self_;
      palf_stat.access_mode_ = access_mode_;
      palf_stat.begin_lsn_ = LSN(progress.begin_lsn);
      assign_scn(palf_stat.begin_scn_, progress.begin_scn);
      palf_stat.base_lsn_ = LSN(progress.base_lsn);
      palf_stat.end_lsn_ = LSN(progress.end_lsn);
      assign_scn(palf_stat.end_scn_, progress.end_scn);
      palf_stat.max_lsn_ = palf_stat.end_lsn_;
      palf_stat.max_scn_ = palf_stat.end_scn_;
    }
  }
  return ret;
}

int PalfHandleImpl::get_palf_epoch(int64_t &palf_epoch) const
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    palf_epoch = palf_epoch_;
  }
  return ret;
}

int PalfHandleImpl::get_access_mode_ref_scn(AccessMode &access_mode, SCN &ref_scn) const
{
  RLockGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else {
    const auto progress = palf_rs::progress_ffi(**stream_);
    ret = from_rust_code(progress.code);
    if (OB_SUCC(ret)) {
      access_mode = access_mode_;
      assign_scn(ref_scn, progress.ref_scn);
    }
  }
  return ret;
}

int PalfHandleImpl::diagnose(PalfDiagnoseInfo &diagnose_info) const
{
  RLockGuard guard(lock_);
  diagnose_info.log_state_ = is_inited_ ? LogState::ACTIVE : LogState::INVALID_STATE;
  return is_inited_ ? OB_SUCCESS : OB_NOT_INIT;
}

void PalfHandleImpl::begin_io_()
{
  ObSpinLockGuard guard(io_lock_);
  if (0 == active_io_count_++) {
    io_start_time_ = ObTimeUtility::fast_current_time();
  }
}

void PalfHandleImpl::end_io_()
{
  ObSpinLockGuard guard(io_lock_);
  if (0 == --active_io_count_) {
    io_start_time_ = OB_INVALID_TIMESTAMP;
  }
}

int PalfHandleImpl::get_io_start_time(int64_t &io_start_time) const
{
  ObSpinLockGuard guard(io_lock_);
  io_start_time = io_start_time_;
  return OB_SUCCESS;
}

PalfStat::PalfStat()
    : self_(), access_mode_(AccessMode::INVALID_ACCESS_MODE), begin_lsn_(), begin_scn_(),
      base_lsn_(), end_lsn_(), end_scn_(), max_lsn_(), max_scn_()
{}

bool PalfStat::is_valid() const
{
  return self_.is_valid() && access_mode_ != AccessMode::INVALID_ACCESS_MODE;
}

void PalfStat::reset()
{
  self_.reset();
  access_mode_ = AccessMode::INVALID_ACCESS_MODE;
  begin_lsn_.reset();
  begin_scn_.reset();
  base_lsn_.reset();
  end_lsn_.reset();
  end_scn_.reset();
  max_lsn_.reset();
  max_scn_.reset();
}
} // namespace palf
} // namespace oceanbase
