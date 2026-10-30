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

#include "ob_local_device.h"
#include <limits>
#include <mutex>
#include "rust/config.h"
#include "rust/file_io.h"
#include "lib/profile/ob_trace_id.h"
#include "share/config/ob_server_config.h"
#include "share/ob_io_device_helper.h"
#ifndef _WIN32
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <sys/mount.h>
#include <fcntl.h>
#elif defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdint.h>
#include <chrono>
#elif defined(__ANDROID__)
#include <sys/vfs.h>
#include <linux/falloc.h>
#elif defined(__linux__)
#include <sys/vfs.h>
#include <linux/falloc.h>
#endif

#if defined(__APPLE__)
#ifndef FALLOC_FL_KEEP_SIZE
#define FALLOC_FL_KEEP_SIZE 0x01
#endif
#ifndef FALLOC_FL_PUNCH_HOLE
#define FALLOC_FL_PUNCH_HOLE 0x02
#endif
#endif
using namespace oceanbase::common;

namespace oceanbase {
namespace share {
const char *BLOCK_SSTBALE_DIR_NAME = "sstable";
const char *BLOCK_SSTBALE_FILE_NAME = "block_file";

namespace {
rust::Slice<const uint8_t> path_bytes(const char *path)
{
  return {reinterpret_cast<const uint8_t *>(path), STRLEN(path)};
}

void copy_file_stat(const file_io::FileStat &source, ObIODFileStat &destination)
{
  destination.rdev_ = source.rdev;
  destination.dev_ = source.dev;
  destination.inode_ = source.inode;
  destination.mask_ = source.mask;
  destination.mode_ = source.mode;
  destination.nlink_ = source.nlink;
  destination.uid_ = source.uid;
  destination.gid_ = source.gid;
  destination.size_ = source.size;
  destination.block_cnt_ = source.block_cnt;
  destination.block_size_ = source.block_size;
  destination.atime_s_ = source.atime_s;
  destination.mtime_s_ = source.mtime_s;
  destination.ctime_s_ = source.ctime_s;
  destination.btime_s_ = source.btime_s;
}

template <typename Callback>
int scan_directory(const char *path, Callback &&callback)
{
  if (nullptr == path) { return OB_INVALID_ARGUMENT; }
  auto cursor = file_io::read_directory(path_bytes(path));
  for (;;) {
    const auto step = cursor->next_entry();
    switch (step.code) {
      case file_io::DirectoryStepCode::End: return OB_SUCCESS;
      case file_io::DirectoryStepCode::OpenError:
        return step.error == ENOENT ? OB_NO_SUCH_FILE_OR_DIRECTORY : OB_FILE_NOT_OPENED;
      case file_io::DirectoryStepCode::ReadError:
        return ObIODeviceLocalFileOp::convert_sys_errno(step.error);
      case file_io::DirectoryStepCode::Entry: break;
      default: return OB_ERR_UNEXPECTED;
    }
    dirent entry{};
    if (step.name.size() >= sizeof(entry.d_name)) { return OB_BUF_NOT_ENOUGH; }
    if (step.name_length != step.name.size()
        || step.inode > static_cast<uint64_t>(std::numeric_limits<decltype(entry.d_ino)>::max())
        || step.raw_type > static_cast<uint32_t>(std::numeric_limits<decltype(entry.d_type)>::max())) {
      return OB_SIZE_OVERFLOW;
    }
    entry.d_ino = static_cast<decltype(entry.d_ino)>(step.inode);
    entry.d_type = static_cast<decltype(entry.d_type)>(step.raw_type);
    entry.d_reclen = step.record_length;
#if defined(__APPLE__)
    entry.d_seekoff = step.seek_offset;
    entry.d_namlen = static_cast<decltype(entry.d_namlen)>(step.name_length);
#else
    if (step.offset < std::numeric_limits<decltype(entry.d_off)>::min()
        || step.offset > std::numeric_limits<decltype(entry.d_off)>::max()) {
      return OB_SIZE_OVERFLOW;
    }
    entry.d_off = static_cast<decltype(entry.d_off)>(step.offset);
#ifdef _WIN32
    static_assert(sizeof(entry.d_name) == 261, "pinned dirent name capacity changed");
    if (step.record_length != 0 && step.record_length != sizeof(entry)) {
      return OB_ERR_UNEXPECTED;
    }
    entry.d_namlen = static_cast<decltype(entry.d_namlen)>(step.name_length);
#endif
#endif
    MEMCPY(entry.d_name, step.name.data(), step.name.size());
    entry.d_name[step.name.size()] = '\0';
    const int ret = callback(&entry);
    if (OB_SUCCESS != ret) { return ret; }
  }
}

int block_status(const storage_io::BlockStatus &status)
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
    case storage_io::BlockCode::IoError: return status.error ? ObIODeviceLocalFileOp::convert_sys_errno(status.error) : OB_IO_ERROR;
    default: return OB_ERR_UNEXPECTED;
  }
}
}

ObLocalDevice::ObLocalDevice()
  : files_(file_io::new_file_registry()),
    named_(storage_io::new_named_bindings()),
    blocks_(storage_io::new_block_owner()),
    is_inited_(false),
    is_marked_(false),
    block_lock_(common::ObLatchIds::LOCAL_DEVICE_LOCK),
    block_size_(0),
    block_file_size_(0),
    disk_percentage_(0),
    space_provider_(nullptr)
{

  MEMSET(store_dir_, 0, sizeof(store_dir_));
  MEMSET(sstable_dir_, 0, sizeof(sstable_dir_));
  MEMSET(store_path_, 0, sizeof(store_path_));
}

ObLocalDevice::~ObLocalDevice()
{
  destroy();
}

int ObLocalDevice::init(
    const common::ObIODOpts &opts,
    const LocalIOConfig &io_config,
    const ObILocalDeviceSpaceProvider *space_provider)
{
  std::unique_lock<std::shared_mutex> guard(file_session_lock_);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    SHARE_LOG(WARN, "The local device has been inited", K(ret));
  } else if (0 == opts.opt_cnt_) {
    // Log devices share the local-device implementation but do not own the
    // data block file, so LogIODeviceWrapper intentionally supplies no
    // block-file options.
    SHARE_LOG(INFO, "No block-file options supplied, skip initializing block_file");
  } else {
    const char *store_dir = nullptr;
    const char *sstable_dir = nullptr;
    int64_t datafile_size = 0;
    int64_t block_size = 0;
    int64_t datafile_disk_percentage = 0;
    int64_t media_id = 0;

    for (int64_t i = 0; OB_SUCC(ret) && i < opts.opt_cnt_; ++i) {
      if (0 == STRCMP(opts.opts_[i].key_, "data_dir")) {
        store_dir = opts.opts_[i].value_.value_str;
      } else if (0 == STRCMP(opts.opts_[i].key_, "sstable_dir")) {
        sstable_dir = opts.opts_[i].value_.value_str;
      } else if (0 == STRCMP(opts.opts_[i].key_, "block_size")) {
        block_size = opts.opts_[i].value_.value_int64;
      } else if (0 == STRCMP(opts.opts_[i].key_, "datafile_disk_percentage")) {
        datafile_disk_percentage = opts.opts_[i].value_.value_int64;
      } else if (0 == STRCMP(opts.opts_[i].key_, "datafile_size")) {
        datafile_size = opts.opts_[i].value_.value_int64;
      } else if (0 == STRCMP(opts.opts_[i].key_, "media_id")) {
        media_id = opts.opts_[i].value_.value_int64;
      } else {
        ret = OB_NOT_SUPPORTED;
        SHARE_LOG(WARN, "Not supported option, ", K(ret), K(i), K(opts.opts_[i].key_));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_ISNULL(store_dir) || OB_ISNULL(sstable_dir) || 0 == STRLEN(store_dir)
          || STRLEN(store_dir) >= sizeof(store_dir_) || STRLEN(sstable_dir) >= sizeof(sstable_dir_)) {
        ret = OB_INVALID_ARGUMENT;
        SHARE_LOG(WARN, "invalid args", K(ret), KP(store_dir));
      } else {
        block_size_ = block_size;
        block_file_size_ = datafile_size;
        disk_percentage_ = datafile_disk_percentage;
        STRNCPY(store_dir_, store_dir, STRLEN(store_dir));
        STRNCPY(sstable_dir_, sstable_dir, STRLEN(sstable_dir));
        media_id_ = media_id;
      }
    }
  }
  SHARE_LOG(INFO, "init local device", K(ret), KP(this),
            "direct_io", io_config.direct_io,
            "native_aio", io_config.backend == LocalIOBackend::NATIVE_AIO);

  if (OB_SUCC(ret)) {
    io_config_ = io_config;
    space_provider_ = space_provider;
    is_inited_ = true;
  }

  if (OB_UNLIKELY(!is_inited_)) {
    reset_file_session_();
  }

  return ret;
}

int ObLocalDevice::reconfig(const common::ObIODOpts &opts)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "The ObLocalDevice has not been inited, ", K(ret));
  } else {
    int64_t datafile_size = 0;
    int64_t datafile_disk_percentage = 0;
    int64_t reserved_size = 0;

    for (int64_t i = 0; OB_SUCC(ret) && i < opts.opt_cnt_; ++i) {
      if (0 == STRCMP(opts.opts_[i].key_, "datafile_disk_percentage")) {
        datafile_disk_percentage = opts.opts_[i].value_.value_int64;
      } else if (0 == STRCMP(opts.opts_[i].key_, "datafile_size")) {
        datafile_size = opts.opts_[i].value_.value_int64;
      } else if (0 == STRCMP(opts.opts_[i].key_, "reserved_size")) {
        reserved_size = opts.opts_[i].value_.value_int64;
      } else {
        ret = OB_NOT_SUPPORTED;
        SHARE_LOG(WARN, "Not supported option, ", K(ret), K(i), K(opts.opts_[i].key_));
      }
    }

    if (OB_SUCC(ret)) {
      lib::ObMutexGuard guard(block_lock_);
      int64_t new_datafile_size = get_total_block_size();
      if (OB_FAIL(ObIODeviceLocalFileOp::get_block_file_size(sstable_dir_, reserved_size, block_size_,
          datafile_size, datafile_disk_percentage, new_datafile_size))) {
      } else {
        ret = resize_block_file(new_datafile_size, reserved_size);
      }
    }
  }
  return ret;
}

int ObLocalDevice::get_config(ObIODOpts &opts)
{
  UNUSED(opts);
  return OB_NOT_SUPPORTED;
}

int ObLocalDevice::start(const common::ObIODOpts &opts)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "The ObLocalDevice has not been inited, ", K(ret));
  } else if (1 != opts.opt_cnt_ || NULL == opts.opts_) {
    SHARE_LOG(WARN, "opts args is wrong ", K(opts.opt_cnt_));
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(0 != STRCMP(opts.opts_[0].key_, "reserved size")
                      || opts.opts_[0].value_.value_int64 < 0)) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "invalid opt arguments", K(ret), K(opts.opts_[0].key_),
        K(opts.opts_[0].value_.value_int64));
  } else {
    const int64_t reserved = opts.opts_[0].value_.value_int64;
    bool exists = false;
    int64_t initial_size = 0;
    if (block_size_ < DIO_READ_ALIGN_SIZE || block_size_ % DIO_READ_ALIGN_SIZE != 0) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(databuff_printf(store_path_, sizeof(store_path_), "%s/%s/%s",
        store_dir_, BLOCK_SSTBALE_DIR_NAME, BLOCK_SSTBALE_FILE_NAME))) {
    } else if (OB_FAIL(exist(store_path_, exists))) {
    } else if (!exists && OB_FAIL(ObIODeviceLocalFileOp::get_block_file_size(sstable_dir_, reserved,
        block_size_, block_file_size_, disk_percentage_, initial_size))) {
    } else {
      storage_io::BlockOptions options{};
      options.block_size = block_size_;
      options.initial_size = initial_size;
      options.reserved_bytes = reserved;
      options.direct_io = io_config_.direct_io;
      options.punch_holes = config::_enable_block_file_punch_hole();
      auto opened = blocks_->open_blocks(
          {reinterpret_cast<const uint8_t *>(store_path_), STRLEN(store_path_)}, options);
      ret = block_status(opened.status);
      if (OB_SUCCESS != ret) {
        SHARE_LOG(WARN, "block file open failed", K(ret), K(store_path_),
            "native_domain", static_cast<int>(opened.primary_error.domain),
            "native_error", opened.primary_error.native_code, "error", opened.primary_error.message.c_str());
      }
      auto &summary = opened.directory_summary;
      if (summary.count != 0) {
        SHARE_LOG(WARN, "directory durability unconfirmed; only first failure detail retained", K(ret),
            "failed_flush_count", summary.count, "count_is_lower_bound", summary.count == UINT64_MAX,
            "first_path", common::ObString(static_cast<int32_t>(summary.first_path.size()),
                reinterpret_cast<const char *>(summary.first_path.data())),
            "native_domain", static_cast<int>(summary.first_error.domain),
            "native_error", summary.first_error.native_code, "error", summary.first_error.message.c_str());
      }
      if (opened.sync.stage != file_io::SyncStage::Complete) {
        SHARE_LOG(WARN, "block file creation directory sync failed", K(ret), K(store_path_),
            "stage", static_cast<int>(opened.sync.stage), "native_domain", static_cast<int>(opened.sync.primary.domain),
            "native_error", opened.sync.primary.native_code, "error", opened.sync.primary.message.c_str(),
            "sync_path", common::ObString(static_cast<int32_t>(opened.sync_path.size()),
                reinterpret_cast<const char *>(opened.sync_path.data())),
            "close_domain", static_cast<int>(opened.sync.close.domain), "close_error", opened.sync.close.native_code);
      }
      if (opened.cleanup_close.domain != file_io::ErrorDomain::None) {
        SHARE_LOG(WARN, "block creation cleanup close failed", K(ret), K(store_path_),
            "native_domain", static_cast<int>(opened.cleanup_close.domain), "native_error", opened.cleanup_close.native_code,
            "error", opened.cleanup_close.message.c_str());
      }
      if (opened.cleanup_remove.domain != file_io::ErrorDomain::None) {
        SHARE_LOG(WARN, "block creation cleanup removal failed", K(ret), K(store_path_),
            "native_domain", static_cast<int>(opened.cleanup_remove.domain), "native_error", opened.cleanup_remove.native_code,
            "error", opened.cleanup_remove.message.c_str());
      }
      if (opened.cleanup_sync.stage != file_io::SyncStage::Complete) {
        SHARE_LOG(WARN, "block creation cleanup directory sync failed", K(ret), K(store_path_),
            "stage", static_cast<int>(opened.cleanup_sync.stage), "native_domain", static_cast<int>(opened.cleanup_sync.primary.domain),
            "native_error", opened.cleanup_sync.primary.native_code, "error", opened.cleanup_sync.primary.message.c_str(),
            "close_domain", static_cast<int>(opened.cleanup_sync.close.domain), "close_error", opened.cleanup_sync.close.native_code);
      }
    }
  }
  return ret;
}

void ObLocalDevice::destroy()
{
  std::unique_lock<std::shared_mutex> guard(file_session_lock_);
  reset_file_session_();
}

void ObLocalDevice::reset_file_session_()
{
  is_inited_ = false;
  files_->clear_registry();
  named_->clear_named_bindings();
  blocks_->close_blocks();
  block_size_ = 0;
  block_file_size_ = 0;
  is_marked_ = false;
  space_provider_ = nullptr;
  MEMSET(store_dir_, 0, sizeof(store_dir_));
  MEMSET(sstable_dir_, 0, sizeof(sstable_dir_));
  MEMSET(store_path_, 0, sizeof(store_path_));
}

//file/dir interfaces
int ObLocalDevice::open(const char *pathname, const int flags, const mode_t mode, ObIOFd &fd, common::ObIODOpts *opts)
{
  UNUSED(opts);
  std::unique_lock<std::shared_mutex> guard(file_session_lock_);
  if (fd.is_valid()) return OB_INVALID_ARGUMENT;
  fd.reset();
  if (!is_inited_) return OB_NOT_INIT;
  if (nullptr == pathname) return OB_INVALID_ARGUMENT;
  uint64_t id = 0;
  const auto path = path_bytes(pathname);
  const int error = named_->named_enabled()
      ? named_->open_named(*files_, path, io_config_.file_flags(flags), static_cast<uint32_t>(mode), id)
      : files_->open_registered(path, io_config_.file_flags(flags), static_cast<uint32_t>(mode), id);
  if (error != 0) return ObIODeviceLocalFileOp::convert_sys_errno(error);
  fd.first_id_ = ObIOFd::NORMAL_FILE_ID;
  fd.second_id_ = static_cast<int64_t>(id);
  fd.third_id_ = -1;
  fd.device_handle_ = this;
  return OB_SUCCESS;
}

int ObLocalDevice::close(const ObIOFd &fd)
{
  std::unique_lock<std::shared_mutex> guard(file_session_lock_);
  if (!is_inited_) return OB_NOT_INIT;
  if (!fd.is_normal_file() || (nullptr != fd.device_handle_ && fd.device_handle_ != this)) return OB_INVALID_ARGUMENT;
  const int error = files_->close_registered(fd.second_id_);
  named_->close_named_binding(fd.second_id_);
  return error ? ObIODeviceLocalFileOp::convert_sys_errno(error) : OB_SUCCESS;
}

int ObLocalDevice::mkdir(const char *pathname, mode_t mode)
{
  if (nullptr == pathname || '\0' == *pathname) { return OB_INVALID_ARGUMENT; }
  const int error = file_io::mkdir_path(path_bytes(pathname), static_cast<uint32_t>(mode));
  return error ? ObIODeviceLocalFileOp::convert_sys_errno(error) : OB_SUCCESS;
}

int ObLocalDevice::rmdir(const char *pathname)
{
  if (nullptr == pathname || '\0' == *pathname) { return OB_INVALID_ARGUMENT; }
  const int error = file_io::rmdir_path(path_bytes(pathname));
  return error ? ObIODeviceLocalFileOp::convert_sys_errno(error) : OB_SUCCESS;
}

int ObLocalDevice::unlink(const char *pathname)
{
  if (nullptr == pathname || '\0' == *pathname) { return OB_INVALID_ARGUMENT; }
  const int error = file_io::unlink_path(path_bytes(pathname));
  return error ? ObIODeviceLocalFileOp::convert_sys_errno(error) : OB_SUCCESS;
}

int ObLocalDevice::rename(const char *oldpath, const char *newpath)
{
  if (nullptr == oldpath || nullptr == newpath) { return OB_INVALID_ARGUMENT; }
  const int error = file_io::rename_path(path_bytes(oldpath), path_bytes(newpath));
  return error ? ObIODeviceLocalFileOp::convert_sys_errno(error) : OB_SUCCESS;
}

int ObLocalDevice::scan_dir(const char *dir_name, int (*func)(const dirent *entry))
{
  if (nullptr == func) { return OB_INVALID_ARGUMENT; }
  return scan_directory(dir_name, func);
}

int ObLocalDevice::scan_dir(const char *dir_name, common::ObBaseDirEntryOperator &op)
{
  return scan_directory(dir_name, [&op](const dirent *entry) { return op.func(entry); });
}

int ObLocalDevice::fsync(const ObIOFd &fd)
{
  std::shared_lock<std::shared_mutex> guard(file_session_lock_);
  if (!is_inited_) return OB_NOT_INIT;
  if (!fd.is_normal_file() || (nullptr != fd.device_handle_ && fd.device_handle_ != this)) return OB_INVALID_ARGUMENT;
  const int error = named_->named_enabled() ? named_->sync_named(fd.second_id_, false) : files_->sync_registered(fd.second_id_, false);
  return error ? ObIODeviceLocalFileOp::convert_sys_errno(error) : OB_SUCCESS;
}

int ObLocalDevice::fdatasync(const ObIOFd &fd)
{
  std::shared_lock<std::shared_mutex> guard(file_session_lock_);
  if (!is_inited_) return OB_NOT_INIT;
  if (!fd.is_normal_file() || (nullptr != fd.device_handle_ && fd.device_handle_ != this)) return OB_INVALID_ARGUMENT;
  const int error = named_->named_enabled() ? named_->sync_named(fd.second_id_, true) : files_->sync_registered(fd.second_id_, true);
  return error ? ObIODeviceLocalFileOp::convert_sys_errno(error) : OB_SUCCESS;
}

int ObLocalDevice::fallocate(const ObIOFd &fd, mode_t mode, const int64_t offset, const int64_t len)
{
  std::shared_lock<std::shared_mutex> guard(file_session_lock_);
  if (!is_inited_) return OB_NOT_INIT;
  if (!fd.is_normal_file() || (nullptr != fd.device_handle_ && fd.device_handle_ != this) || offset < 0 || len < 0) return OB_INVALID_ARGUMENT;
  const int error = named_->named_enabled() ? named_->allocate_named(fd.second_id_, mode, offset, len) : files_->allocate_registered(fd.second_id_, mode, offset, len);
  return error ? ObIODeviceLocalFileOp::convert_sys_errno(error) : OB_SUCCESS;
}

int ObLocalDevice::lseek(const ObIOFd &fd, const int64_t offset, const int whence, int64_t &result_offset)
{
  std::shared_lock<std::shared_mutex> guard(file_session_lock_);
  result_offset = 0;
  if (!is_inited_) return OB_NOT_INIT;
  if (!fd.is_normal_file() || (nullptr != fd.device_handle_ && fd.device_handle_ != this)) return OB_INVALID_ARGUMENT;
  if (named_->named_enabled()) return OB_NOT_SUPPORTED;
  uint64_t result = 0;
  const int error = files_->seek_registered(fd.second_id_, offset, whence, result);
  if (error) return ObIODeviceLocalFileOp::convert_sys_errno(error);
  if (result > INT64_MAX) return OB_SIZE_OVERFLOW;
  result_offset = static_cast<int64_t>(result);
  return OB_SUCCESS;
}

int ObLocalDevice::truncate(const char *pathname, const int64_t len)
{
  if (nullptr == pathname || '\0' == *pathname) { return OB_INVALID_ARGUMENT; }
  const int error = file_io::truncate_legacy_path(path_bytes(pathname), len);
  return error ? ObIODeviceLocalFileOp::convert_sys_errno(error) : OB_SUCCESS;
}

int ObLocalDevice::ftruncate(const ObIOFd &fd, const int64_t len)
{
  std::shared_lock<std::shared_mutex> guard(file_session_lock_);
  if (!is_inited_) return OB_NOT_INIT;
  if (!fd.is_normal_file() || (nullptr != fd.device_handle_ && fd.device_handle_ != this) || len < 0) return OB_INVALID_ARGUMENT;
  const int error = named_->named_enabled() ? named_->resize_named(fd.second_id_, len) : files_->truncate_registered(fd.second_id_, len);
  return error ? ObIODeviceLocalFileOp::convert_sys_errno(error) : OB_SUCCESS;
}

int ObLocalDevice::exist(const char *pathname, bool &is_exist)
{
  if (nullptr == pathname || '\0' == *pathname) { return OB_INVALID_ARGUMENT; }
  const int error = file_io::exists_path(path_bytes(pathname), is_exist);
  return error ? ObIODeviceLocalFileOp::convert_sys_errno(error) : OB_SUCCESS;
}

int ObLocalDevice::stat(const char *pathname, ObIODFileStat &statbuf)
{
  if (nullptr == pathname || '\0' == *pathname) { return OB_INVALID_ARGUMENT; }
  file_io::FileStat result{};
  const int error = file_io::stat_path(path_bytes(pathname), result);
  if (error) { return ObIODeviceLocalFileOp::convert_sys_errno(error); }
  copy_file_stat(result, statbuf);
  return OB_SUCCESS;
}

int ObLocalDevice::fstat(const ObIOFd &fd, ObIODFileStat &statbuf)
{
  std::shared_lock<std::shared_mutex> guard(file_session_lock_);
  if (!is_inited_) return OB_NOT_INIT;
  if (!fd.is_normal_file() || (nullptr != fd.device_handle_ && fd.device_handle_ != this)) return OB_INVALID_ARGUMENT;
  file_io::FileStat result{};
  const int error = named_->named_enabled() ? named_->stat_named(fd.second_id_, result) : files_->stat_registered(fd.second_id_, result);
  if (error) return ObIODeviceLocalFileOp::convert_sys_errno(error);
  copy_file_stat(result, statbuf);
  return OB_SUCCESS;
}

int ObLocalDevice::mark_blocks(ObIBlockIterator &iter)
{
  if (!is_inited_) return OB_NOT_INIT;
  lib::ObMutexGuard guard(block_lock_);
  if (is_marked_) return OB_INIT_TWICE;
  int ret = block_status(blocks_->restart_marking());
  ObIOFd id;
  while (OB_SUCC(ret)) {
    ret = iter.get_next_block(id);
    if (OB_SUCC(ret)) {
      if (id.second_id_ < RESERVED_BLOCK_INDEX) {
        ret = OB_INVALID_DATA;
      } else {
        const uint64_t index = id.second_id_;
        ret = block_status(blocks_->mark_blocks({&index, 1}));
      }
    }
  }
  if (ret == OB_ITER_END) ret = block_status(blocks_->finish_marking());
  if (OB_SUCC(ret)) {
    is_marked_ = true;
    storage_io::BlockStats stats{};
    if (block_status(blocks_->block_stats(stats)) == OB_SUCCESS && stats.reclamation_failures > 0) {
      SHARE_LOG(WARN, "block reclamation failed during marking", "failures", stats.reclamation_failures);
    }
  } else {
    blocks_->restart_marking();
  }
  return ret;
}

int ObLocalDevice::alloc_block(const common::ObIODOpts *opts, ObIOFd &id)
{
  UNUSED(opts);
  if (!is_inited_ || !is_marked_) return OB_NOT_INIT;
  rust::Vec<uint64_t> indices;
  int ret = block_status(blocks_->allocate_blocks(1, indices));
  if (OB_SUCC(ret)) {
    if (indices.size() != 1) ret = OB_SERVER_OUTOF_DISK_SPACE;
    else { id.first_id_ = 0; id.second_id_ = indices[0]; }
  }
  return ret;
}

int ObLocalDevice::alloc_blocks(const common::ObIODOpts *opts, const int64_t count,
    common::ObIArray<ObIOFd> &out)
{
  if (count < 0) return OB_INVALID_ARGUMENT;
  int ret = OB_SUCCESS;
  out.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    ObIOFd id;
    ret = alloc_block(opts, id);
    if (ret == OB_SERVER_OUTOF_DISK_SPACE) { ret = OB_SUCCESS; break; }
    if (OB_SUCC(ret) && OB_FAIL(out.push_back(id))) free_block(id);
  }
  return ret;
}

void ObLocalDevice::free_block(const ObIOFd &id)
{
  int ret = OB_SUCCESS;
  if (id.second_id_ < RESERVED_BLOCK_INDEX) {
    SHARE_LOG(WARN, "invalid block release", K(id));
  } else {
    const auto result = blocks_->release_block(id.second_id_);
    ret = block_status(result.status);
    if (ret != OB_SUCCESS || result.reclamation_error != 0) {
      SHARE_LOG(WARN, "block release or reclamation failed", K(ret), K(id),
          "reclamation_error", result.reclamation_error, "os_error", result.reclamation_os_error);
    }
  }
}

int ObLocalDevice::fsync_block()
{
  return is_inited_ ? block_status(blocks_->sync_blocks()) : OB_NOT_INIT;
}

int ObLocalDevice::get_restart_sequence(uint32_t &restart_id) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "The ObLocalDevice has not been inited, ", K(ret));
  } else {
    restart_id = 0;
  }
  return ret;
}

int ObLocalDevice::mark_blocks(const common::ObIArray<ObIOFd> &blocks)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "The ObLocalDevice has not been inited, ", K(ret));
  } else if (OB_UNLIKELY(0 == blocks.count())) {
    // do nothing
  } else {
    ret = OB_NOT_SUPPORTED;
  }
  return ret;
}

//sync io interfaces
int ObLocalDevice::pread(
    const ObIOFd &fd,
    const int64_t offset,
    const int64_t size,
    void *buf,
    int64_t &read_size)
{
  std::shared_lock<std::shared_mutex> guard(file_session_lock_);
  int ret = OB_SUCCESS;
  read_size = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "The ObLocalDevice has not been inited, ", K(ret));
  } else if (fd.is_block_file() && (fd.second_id_ < RESERVED_BLOCK_INDEX
      || (fd.device_handle_ != nullptr && fd.device_handle_ != this))) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (fd.is_block_file()) {
      if (offset < 0 || size < 0 || (size > 0 && nullptr == buf)) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        const auto result = storage_io::read_block(*blocks_, fd.second_id_, offset,
            {static_cast<uint8_t *>(buf), static_cast<size_t>(size)});
        read_size = result.bytes;
        if (result.error) ret = ObIODeviceLocalFileOp::convert_sys_errno(result.error);
      }
    } else if (fd.is_normal_file()) {
      if ((nullptr != fd.device_handle_ && fd.device_handle_ != this) || offset < 0 || size < 0 || (size > 0 && nullptr == buf)) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        const auto result = named_->named_enabled()
            ? named_->read_named(fd.second_id_, {static_cast<uint8_t *>(buf), static_cast<size_t>(size)}, offset)
            : files_->read_registered(fd.second_id_, {static_cast<uint8_t *>(buf), static_cast<size_t>(size)}, offset);
        read_size = result.bytes;
        if (result.error) ret = ObIODeviceLocalFileOp::convert_sys_errno(result.error);
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
    }

    if (read_size < 0) {
      ret = OB_IO_ERROR;
      SHARE_LOG(WARN, "Fail to pread fd, ", K(ret), K(read_size), K(offset), K(size), KP(buf), KERRMSG);
    }
  }
  return ret;
}

int ObLocalDevice::pwrite(
    const ObIOFd &fd,
    const int64_t offset,
    const int64_t size,
    const void *buf,
    int64_t &write_size)
{
  std::shared_lock<std::shared_mutex> guard(file_session_lock_);
  int ret = OB_SUCCESS;
  write_size = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "The ObLocalDevice has not been inited, ", K(ret));
  } else if (fd.is_block_file() && (fd.second_id_ < RESERVED_BLOCK_INDEX
      || (fd.device_handle_ != nullptr && fd.device_handle_ != this))) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (fd.is_block_file()) {
      if (offset < 0 || size < 0 || (size > 0 && nullptr == buf)) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        const auto result = storage_io::write_block(*blocks_, fd.second_id_, offset,
            {static_cast<const uint8_t *>(buf), static_cast<size_t>(size)});
        write_size = result.bytes;
        if (result.error) ret = ObIODeviceLocalFileOp::convert_sys_errno(result.error);
      }
    } else if (fd.is_normal_file()) {
      if ((nullptr != fd.device_handle_ && fd.device_handle_ != this) || offset < 0 || size < 0 || (size > 0 && nullptr == buf)) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        const auto result = named_->named_enabled()
            ? named_->write_named(fd.second_id_, {static_cast<const uint8_t *>(buf), static_cast<size_t>(size)}, offset)
            : files_->write_registered(fd.second_id_, {static_cast<const uint8_t *>(buf), static_cast<size_t>(size)}, offset);
        write_size = result.bytes;
        if (result.error) ret = ObIODeviceLocalFileOp::convert_sys_errno(result.error);
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
    }

    if (write_size < 0) {
      ret = OB_IO_ERROR;
      SHARE_LOG(WARN, "Fail to pwrite fd, ", K(ret), K(fd), K(write_size),
          K(offset), K(size), KP(buf), K(errno), KERRNOMSG(errno));
    }
  }

  return ret;
}

int ObLocalDevice::read(
  const ObIOFd &fd,
  void *buf,
  const int64_t size,
  int64_t &read_size)
{
  std::shared_lock<std::shared_mutex> guard(file_session_lock_);
  read_size = 0;
  if (!is_inited_) return OB_NOT_INIT;
  if (!fd.is_normal_file() || (nullptr != fd.device_handle_ && fd.device_handle_ != this) || size < 0 || (size > 0 && nullptr == buf)) return OB_INVALID_ARGUMENT;
  if (named_->named_enabled()) return OB_NOT_SUPPORTED;
  const auto result = files_->read_next_registered(fd.second_id_, {static_cast<uint8_t *>(buf), static_cast<size_t>(size)});
  read_size = result.bytes;
  return result.error ? ObIODeviceLocalFileOp::convert_sys_errno(result.error) : OB_SUCCESS;
}

int ObLocalDevice::write(
  const ObIOFd &fd,
  const void *buf,
  const int64_t size,
  int64_t &write_size)
{
  std::shared_lock<std::shared_mutex> guard(file_session_lock_);
  write_size = 0;
  if (!is_inited_) return OB_NOT_INIT;
  if (!fd.is_normal_file() || (nullptr != fd.device_handle_ && fd.device_handle_ != this) || size < 0 || (size > 0 && nullptr == buf)) return OB_INVALID_ARGUMENT;
  if (named_->named_enabled()) return OB_NOT_SUPPORTED;
  const auto result = files_->write_next_registered(fd.second_id_, {static_cast<const uint8_t *>(buf), static_cast<size_t>(size)});
  write_size = result.bytes;
  return result.error ? ObIODeviceLocalFileOp::convert_sys_errno(result.error) : OB_SUCCESS;
}



int ObLocalDevice::attach_named_owner(const storage_io::NamedOwnerHandle &owner)
{
  std::unique_lock<std::shared_mutex> guard(file_session_lock_);
  if (!is_inited_) return OB_NOT_INIT;
  const int error = named_->attach_named_owner(owner);
  return error ? ObIODeviceLocalFileOp::convert_sys_errno(error) : OB_SUCCESS;
}

int ObLocalDevice::bind_request_target(const ObIOFd &fd, storage_io::RequestSlot &request, storage_io::RequestLifetime &lifetime)
{
  std::shared_lock<std::shared_mutex> guard(file_session_lock_);
  if (!is_inited_) return OB_NOT_INIT;
  int error = 0;
  if (fd.is_normal_file()) {
    if (fd.device_handle_ != this) return OB_INVALID_ARGUMENT;
    error = named_->named_enabled()
        ? storage_io::bind_named_target(*named_, fd.second_id_, request, lifetime)
        : storage_io::bind_registered_target(*files_, fd.second_id_, request, lifetime);
  } else if (fd.is_valid() && fd.second_id_ >= RESERVED_BLOCK_INDEX
      && (fd.device_handle_ == nullptr || fd.device_handle_ == this)) {
    error = storage_io::bind_block_target(*blocks_, fd.second_id_, request, lifetime);
  } else {
    return OB_INVALID_ARGUMENT;
  }
  return error ? ObIODeviceLocalFileOp::convert_sys_errno(error) : OB_SUCCESS;
}

int64_t ObLocalDevice::get_total_block_size() const
{
  storage_io::BlockStats stats{};
  return block_status(blocks_->block_stats(stats)) == OB_SUCCESS ? stats.capacity : 0;
}

int64_t ObLocalDevice::get_free_block_count() const
{
  storage_io::BlockStats stats{};
  return block_status(blocks_->block_stats(stats)) == OB_SUCCESS ? stats.free_blocks : 0;
}

int64_t ObLocalDevice::get_reserved_block_count() const
{
  return RESERVED_BLOCK_INDEX;
}

int64_t ObLocalDevice::get_max_block_count(int64_t reserved_size) const
{
  return block_size_ > 0 ? get_max_block_size(reserved_size) / block_size_ : 0;
}

int64_t ObLocalDevice::get_max_block_size(int64_t reserved_size) const
{
  int ret = OB_SUCCESS;
  file_io::SpaceResult space{};

  const int64_t config_max_file_size = config::datafile_maxsize();
  const int64_t current_size = get_total_block_size();
  int64_t block_file_max_size = current_size;

  if (config_max_file_size < block_file_max_size) {
    // auto extend is off
  } else if (OB_ISNULL(sstable_dir_)) {
    ret = OB_ERR_UNEXPECTED;
    SHARE_LOG(WARN, "Failed to get max block size", K(ret), K(sstable_dir_));
  } else if (0 != (space = file_io::read_space({reinterpret_cast<const uint8_t *>(sstable_dir_), STRLEN(sstable_dir_)})).error) {
    ret = ObIODeviceLocalFileOp::convert_sys_errno(space.error);
    SHARE_LOG(WARN, "Failed to get disk space", K(ret), K(sstable_dir_));
  } else {
    const int64_t free_space = std::max(static_cast<int64_t>(0), (int64_t)(space.available));
    const int64_t max_file_size = std::max(current_size, current_size + free_space - reserved_size);
    /* when datafile_maxsize is large than current datafile_size, we should return
       the Maximun left space that can be extend. */
    if (max_file_size > config_max_file_size) {
      block_file_max_size = lower_align(config_max_file_size, block_size_);
    } else {
      block_file_max_size = lower_align(max_file_size, block_size_);
    }
  }
  // still return current block file size when ret=fail
  return block_file_max_size;
}

int ObLocalDevice::get_data_disk_used_percentage_(
    const int64_t required_size,
    int64_t &percent) const
{
  int ret = OB_SUCCESS;
  int64_t reserved_size = 0;

  if (OB_UNLIKELY(!is_marked_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "The ObLocalDevice has not been marked", K(ret));
  } else if (OB_UNLIKELY(required_size < 0)) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "invalid argument", K(ret), K(required_size));
  } else if (OB_ISNULL(space_provider_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "local device space provider is not initialized", K(ret));
  } else if (OB_FAIL(space_provider_->get_reserved_size(reserved_size))) {
  } else {
    const int64_t max_block_cnt = get_max_block_count(reserved_size);
    storage_io::BlockStats stats{};
    if (OB_FAIL(block_status(blocks_->block_stats(stats)))) return ret;
    const int64_t total_block_cnt = stats.total_blocks;
    int64_t actual_free_block_cnt = stats.free_blocks;
    if (max_block_cnt > total_block_cnt) {
      actual_free_block_cnt = max_block_cnt - total_block_cnt + stats.free_blocks;
    }
    const int64_t required_count = required_size / block_size_;
    const int64_t free_count = actual_free_block_cnt - required_count;
    percent = 100 - 100 * free_count / total_block_cnt;
  }
  return ret;
}

int ObLocalDevice::check_space_full(
    const int64_t required_size,
    const bool alarm_if_space_full) const
{
  int ret = OB_SUCCESS;
  int64_t used_percent = 0;
  const int64_t NO_LIMIT_PERCENT = 100;

  if (OB_UNLIKELY(!is_marked_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "The ObLocalDevice has not been marked", K(ret));
  } else if (OB_FAIL(get_data_disk_used_percentage_(required_size,
                                                    used_percent))) {
  } else {
    if (config::data_disk_usage_limit_percentage() != NO_LIMIT_PERCENT
        && used_percent >= config::data_disk_usage_limit_percentage()) {
      ret = OB_SERVER_OUTOF_DISK_SPACE;
      if (alarm_if_space_full && REACH_TIME_INTERVAL(24 * 3600LL * 1000 * 1000 /* 24h */)) {
        LOG_DBA_ERROR_V2(OB_SHARE_OUTOF_DISK_SPACE, OB_SERVER_OUTOF_DISK_SPACE,
                         "disk is almost full. resuired size is ", required_size,
                         " and used percent is ", used_percent, "%. ",
                         "[suggestion] Increase the datafile_size or datafile_disk_percentage parameter. ");
      }
    }
  }
  return ret;
}

int ObLocalDevice::check_write_limited() const
{
  int ret = OB_SUCCESS;
  int64_t used_percent = 0;
  const int64_t required_size = 0;
  const int64_t limit_percent = config::data_disk_write_limit_percentage();

  if (OB_UNLIKELY(!is_marked_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "The ObLocalDevice has not been marked", K(ret));
  } else if (OB_FAIL(get_data_disk_used_percentage_(required_size,
                                                    used_percent))) {
  } else if (limit_percent != 0 && used_percent >= limit_percent) {
    ret = OB_SERVER_OUTOF_DISK_SPACE;
    if (REACH_TIME_INTERVAL(60 * 1000 * 1000 /* 1min */)) {
      SHARE_LOG(WARN, "disk is full, user write should be stopped", K(ret), K(used_percent),
                K(limit_percent));
    }
  }
  return ret;
}

int ObLocalDevice::resize_block_file(const int64_t new_size, const int64_t reserved_size)
{
  if (new_size < 0 || reserved_size < 0) return OB_INVALID_ARGUMENT;
  return block_status(blocks_->grow_blocks(new_size, reserved_size));
}

} /* namespace share */
} /* namespace oceanbase */
