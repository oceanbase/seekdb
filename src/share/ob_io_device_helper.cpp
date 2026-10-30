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

#define USING_LOG_PREFIX SHARE
#include <errno.h>
#include <limits>
#include "rust/file_io.h"
#include <stdlib.h>
#include <ctype.h>
#include "lib/restore/ob_io_device.h"
#include "share/ob_device_manager.h"
#include "share/ob_io_device_helper.h"
#include "share/ob_local_device.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace share
{
/**
 * --------------------------------ObSNIODeviceWrapper------------------------------------
 */
ObSNIODeviceWrapper &ObSNIODeviceWrapper::get_instance()
{
  static ObSNIODeviceWrapper instance;
  return instance;
}

ObIODevice &ObSNIODeviceWrapper::get_local_device()
{
  abort_unless(NULL != local_device_);
  return *local_device_;
}

ObSNIODeviceWrapper::ObSNIODeviceWrapper()
  : local_device_(NULL), is_inited_(false)
{
}

ObSNIODeviceWrapper::~ObSNIODeviceWrapper()
{
  destroy();
}

/*io device helper hold one local/ofs device, and never free*/
int ObSNIODeviceWrapper::get_local_device_from_mgr(share::ObLocalDevice *&local_device)
{
  int ret = OB_SUCCESS;

  common::ObIODevice* device = nullptr;
  common::ObString storage_type_prefix(OB_LOCAL_PREFIX);
  const ObStorageIdMod storage_id_mod(0, ObStorageUsedMod::STORAGE_USED_DATA);
  if(OB_FAIL(common::ObDeviceManager::get_local_device(storage_type_prefix, storage_id_mod, device))) {
  } else if (OB_ISNULL(local_device = static_cast<share::ObLocalDevice*>(device))) {
    ret = OB_ERR_UNEXPECTED;
  }

  if (OB_FAIL(ret)) {
    //if fail, release the resource
    if (nullptr != local_device) {
      common::ObDeviceManager::get_instance().release_device((ObIODevice*&)local_device);
      local_device = nullptr;
    }
  }
  return ret;
}

int ObSNIODeviceWrapper::init(
    const char *data_dir,
    const char *sstable_dir,
    const int64_t block_size,
    const int64_t data_disk_percentage,
    const int64_t data_disk_size,
    const ObILocalDeviceSpaceProvider &space_provider,
    const LocalIOConfig &io_config)
{
  int ret = OB_SUCCESS;
  const int64_t MAX_IOD_OPT_CNT = 5;
  ObIODOpt iod_opt_array[MAX_IOD_OPT_CNT];
  ObIODOpts iod_opts;
  iod_opts.opts_ = iod_opt_array;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(get_local_device_from_mgr(local_device_))) {
  } else if (OB_ISNULL(data_dir) || OB_ISNULL(sstable_dir)) {
    ret = OB_INVALID_ARGUMENT;
  } else if ('/' != data_dir[0] && '.' != data_dir[0]
#ifdef _WIN32
             && !(isalpha(data_dir[0]) && data_dir[1] == ':')
#endif
            ) {
    ret = OB_IO_ERROR;
    LOG_ERROR("unknown storage type, not support", K(ret), K(data_dir));
  } else {
    iod_opt_array[0].set("data_dir", data_dir);
    iod_opt_array[1].set("sstable_dir", sstable_dir);
    iod_opt_array[2].set("block_size", block_size);
    iod_opt_array[3].set("datafile_disk_percentage", data_disk_percentage);
    iod_opt_array[4].set("datafile_size", data_disk_size);
    iod_opts.opt_cnt_ = MAX_IOD_OPT_CNT;
  }

  if (OB_SUCC(ret) && OB_NOT_NULL(local_device_)) {
    if (OB_FAIL(local_device_->init(iod_opts, io_config, &space_provider))) {
    } else {
      is_inited_ = true;
      LOG_INFO("finish to init io device", K(ret), K(data_dir), K(sstable_dir), K(block_size),
          K(data_disk_percentage), K(data_disk_size));
    }
  }

  if (IS_NOT_INIT) {
    destroy();
  }

  return ret;
}

void ObSNIODeviceWrapper::destroy()
{
  if (is_inited_) {
    if (NULL != local_device_) {
      local_device_->destroy();
      common::ObDeviceManager::get_instance().release_device((ObIODevice*&)local_device_);
      local_device_ = NULL;
    }

    is_inited_ = false;
    LOG_INFO("io device wrapper destroy");
  }
}

int ObIODeviceLocalFileOp::convert_sys_errno()
{
  return ObIODeviceLocalFileOp::convert_sys_errno(errno);
}

// Notes: error_no is a positive value
int ObIODeviceLocalFileOp::convert_sys_errno(const int error_no)
{
  int ret = OB_IO_ERROR;
  bool use_warn_log = false;
  switch (error_no) {
    case EACCES:
      ret = OB_FILE_OR_DIRECTORY_PERMISSION_DENIED;
      break;
    case ENOENT:
      ret = OB_NO_SUCH_FILE_OR_DIRECTORY;
      break;
    case EEXIST:
    case ENOTEMPTY:
      ret = OB_FILE_OR_DIRECTORY_EXIST;
      break;
    case ETIMEDOUT:
      ret = OB_TIMEOUT;
      break;
    case EAGAIN:
      ret = OB_EAGAIN;
      break;
#ifndef _WIN32
    case EDQUOT:
#endif
    case ENOSPC:
      ret = OB_SERVER_OUTOF_DISK_SPACE;
      break;
    case EMFILE:
      ret = OB_TOO_MANY_OPEN_FILES;
      break;
    default:
      use_warn_log = true;
      break;
  }
  if (use_warn_log) {
    SHARE_LOG(WARN, "convert sys errno", K(ret), K(error_no), KERRNOMSG(error_no));
  } else {
    SHARE_LOG(INFO, "convert sys errno", K(ret), K(error_no), KERRNOMSG(error_no));
  }
  return ret;
}

int ObIODeviceLocalFileOp::get_block_file_size(
    const char *sstable_dir,
    const int64_t reserved_size,
    const int64_t block_size,
    const int64_t suggest_file_size,
    const int64_t disk_percentage,
    int64_t &block_file_size)
{
  int ret = OB_SUCCESS;
  // check space availability if file not exist
  const int64_t old_block_file_size = block_file_size;
  if (OB_ISNULL(sstable_dir)
      || OB_UNLIKELY(reserved_size < 0)
      || OB_UNLIKELY(block_size <= 0)
      || OB_UNLIKELY(suggest_file_size < 0)
      || OB_UNLIKELY(disk_percentage < 0)
      || OB_UNLIKELY(disk_percentage >= 100)
      || OB_UNLIKELY(block_file_size < 0)) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "Invalid argument, ", K(ret), KP(sstable_dir), K(reserved_size), K(block_size),
        K(suggest_file_size), K(disk_percentage), K(block_file_size));
  } else if (OB_FAIL(ObIODeviceLocalFileOp::compute_block_file_size(sstable_dir, reserved_size, block_size,
                     suggest_file_size, disk_percentage, block_file_size))) {
  } else if (OB_FAIL(ObIODeviceLocalFileOp::check_disk_space_available(sstable_dir,
                     block_file_size, reserved_size, old_block_file_size, (0 == block_file_size)))) {
  }
  return ret;
}

int ObIODeviceLocalFileOp::compute_block_file_size(
    const char *sstable_dir,
    const int64_t reserved_size,
    const int64_t block_size,
    const int64_t suggest_file_size,
    const int64_t disk_percentage,
    int64_t &block_file_size)
{
  int ret = OB_SUCCESS;
  int64_t total_space = 0;
  file_io::SpaceResult space{};
  if (OB_ISNULL(sstable_dir)
      || OB_UNLIKELY(reserved_size < 0)
      || OB_UNLIKELY(block_size <= 0)
      || OB_UNLIKELY(suggest_file_size < 0)
      || OB_UNLIKELY(disk_percentage < 0)
      || OB_UNLIKELY(disk_percentage >= 100)
      || OB_UNLIKELY(block_file_size < 0)) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "Invalid argument, ", K(ret), KP(sstable_dir), K(reserved_size), K(block_size),
        K(suggest_file_size), K(disk_percentage), K(block_file_size));
  } else if (0 != (space = file_io::read_space({reinterpret_cast<const uint8_t *>(sstable_dir), STRLEN(sstable_dir)})).error) {
    ret = ObIODeviceLocalFileOp::convert_sys_errno(space.error);
    SHARE_LOG(WARN, "Failed to get disk space ", K(ret), K(sstable_dir));
  } else {
    // remove reserved space for root user
    total_space = std::max(static_cast<int64_t>(0), (int64_t)((space.capacity + space.available - space.free) - reserved_size));
    block_file_size = suggest_file_size > 0 ? suggest_file_size : total_space * disk_percentage / 100;
    if (block_file_size <= block_size) {
      ret = OB_INVALID_ARGUMENT;
      SHARE_LOG(ERROR, "data file size is too small, ", K(ret), K(total_space),
          K(reserved_size), K(block_file_size));
    } else {
      block_file_size = lower_align(block_file_size, block_size);
    }
  }
  return ret;
}

int ObIODeviceLocalFileOp::check_disk_space_available(
    const char *sstable_dir,
    const int64_t data_disk_size,
    const int64_t reserved_size,
    const int64_t used_disk_size,
    const bool need_report_user_error)
{
  int ret = OB_SUCCESS;
  file_io::SpaceResult space{};
  if (OB_ISNULL(sstable_dir)
      || OB_UNLIKELY(reserved_size < 0)
      || OB_UNLIKELY(data_disk_size < 0)
      || OB_UNLIKELY(used_disk_size < 0)) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "Invalid argument, ", K(ret), KP(sstable_dir),
              K(reserved_size), K(data_disk_size), K(used_disk_size));
  } else if (0 != (space = file_io::read_space({reinterpret_cast<const uint8_t *>(sstable_dir), STRLEN(sstable_dir)})).error) {
    ret = ObIODeviceLocalFileOp::convert_sys_errno(space.error);
    SHARE_LOG(WARN, "Failed to get disk space ", K(ret), K(sstable_dir));
  } else {
    // check disk space availability for datafile_size, must satisfy datafile_size < used_disk_size + disk_free_space
    const int64_t free_space = std::max(static_cast<int64_t>(0), (int64_t)(space.available - reserved_size));
    if (data_disk_size > used_disk_size + free_space) {
      ret = OB_SERVER_OUTOF_DISK_SPACE;
      if (need_report_user_error) {
        LOG_DBA_ERROR(OB_SERVER_OUTOF_DISK_SPACE, "msg", "data file size is too large", K(ret),
            K(free_space), K(reserved_size), K(used_disk_size), K(data_disk_size));
      } else {
        LOG_DBA_WARN(OB_SERVER_OUTOF_DISK_SPACE, "msg", "data file size is too large", K(ret),
            K(free_space), K(reserved_size), K(used_disk_size), K(data_disk_size));
      }
    }
  }
  return ret;
}


} // namespace share
} // namespace oceanbase
