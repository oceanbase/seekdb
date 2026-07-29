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
#define USING_LOG_PREFIX STORAGE

#ifndef _WIN32
#include <sys/statvfs.h>
#endif
#include "observer/omt/ob_server_runtime_controller.h"
#include "share/rc/ob_module_provider.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "share/ob_force_print_log.h"
#include "share/ob_local_device.h"
#include "storage/ob_file_system_router.h"
#include "storage/meta_store/ob_local_storage_meta_service.h"
namespace oceanbase
{
namespace storage
{

ObServerStorageMetaService &ObServerStorageMetaService::get_instance()
{
  static ObServerStorageMetaService instance_;
  return instance_;
}
ObServerStorageMetaService::ObServerStorageMetaService()
  : is_inited_(false),
    is_started_(false),
    persister_(),
    replayer_(),
    server_slogger_(),
    ckpt_slog_handler_(),
    need_reserved_(false) {}

int ObServerStorageMetaService::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("has inited", K(ret));
  } else if (OB_FAIL(check_log_disk(
        OB_FILE_SYSTEM_ROUTER.get_sstable_dir(),
        OB_FILE_SYSTEM_ROUTER.get_slog_dir()))) {
    LOG_WARN("fail to set need reserved", K(ret));
  } else if (OB_FAIL(server_slogger_.init(
        OB_FILE_SYSTEM_ROUTER.get_slog_dir(),
        ObLogConstants::MAX_LOG_FILE_SIZE,
        OB_FILE_SYSTEM_ROUTER.get_slog_file_spec(),
        true /*is_server*/))) {
    LOG_WARN("fail to init server slogger", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.init(&server_slogger_))) {
    LOG_WARN("fail to init server checkpoint slog hander", K(ret));
  } else if (OB_FAIL(persister_.init(&server_slogger_))) {
    LOG_WARN("fail to init persister", K(ret));
  } else if (OB_FAIL(replayer_.init(persister_, ckpt_slog_handler_))) {
    LOG_WARN("fail to init replayer", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObServerStorageMetaService::start()
{
  int ret = OB_SUCCESS;
  const int64_t start_time = ObTimeUtility::current_time();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(server_slogger_.start())) {
    LOG_WARN("fail to start server slogger", K(ret));
  } else if (OB_FAIL(replayer_.start_replay()))  {
    LOG_WARN("fail to start replayer", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.start())) {
    LOG_WARN("fail to start ckpt slog handler", K(ret));
  } else {
    ATOMIC_STORE(&is_started_, true);
  }
  const int64_t cost_time_us = ObTimeUtility::current_time() - start_time;
  FLOG_INFO("finish start server storage meta service", K(ret), K(cost_time_us));
  return ret;
}

void ObServerStorageMetaService::stop()
{
  if (IS_INIT) {
    server_slogger_.stop();
    ckpt_slog_handler_.stop();
  }
}
void ObServerStorageMetaService::wait()
{
  if (IS_INIT) {
    server_slogger_.wait();
    ckpt_slog_handler_.wait();
  }
}
void ObServerStorageMetaService::destroy()
{
  server_slogger_.destroy();
  ckpt_slog_handler_.destroy();
  persister_.destroy();
  replayer_.destroy();
  need_reserved_ = false;
  is_inited_ = false;
}

int ObServerStorageMetaService::get_meta_block_list(
    ObIArray<blocksstable::MacroBlockId> &meta_block_list)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.get_meta_block_list(meta_block_list))) {
    LOG_WARN("fail to get meta block list", K(ret));
  }
  return ret;
}

int ObServerStorageMetaService::get_reserved_size(int64_t &reserved_size) const
{
  int ret = OB_SUCCESS;
  reserved_size = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (need_reserved_) {
    int64_t used_size = 0;
    if (OB_FAIL(get_using_disk_space(used_size))) {
      LOG_WARN("fail to get using size for slog", K(ret));
    } else {
      reserved_size = std::max(static_cast<int64_t>(0), SLOG_RESERVED_DISK_SIZE - used_size);
    }
  }
  return ret;
}

int ObServerStorageMetaService::get_server_slogger(ObStorageLogger *&slogger) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    slogger = const_cast<ObStorageLogger *>(&server_slogger_);
  }
  return ret;
}

int ObServerStorageMetaService::set_need_reserved_for_test(const bool need_reserved)
{
  need_reserved_ = need_reserved;
  return OB_SUCCESS;
}

int ObServerStorageMetaService::write_checkpoint(bool is_force)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.write_checkpoint(is_force))) {
    LOG_WARN("fail to write checkpoint", K(ret));
  }
  return ret;
}

int ObServerStorageMetaService::check_log_disk(
    const char *data_dir,
    const char *log_dir)
{
  int ret = OB_SUCCESS;
  need_reserved_ = false;
  if (OB_ISNULL(data_dir) || OB_ISNULL(log_dir)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(data_dir), KP(log_dir));
#ifdef _WIN32
  } else {
    UNUSEDx(data_dir, log_dir);
  }
#else
  } else {
    struct statvfs data_svfs;
    struct statvfs log_svfs;
    if (OB_UNLIKELY(0 != statvfs(data_dir, &data_svfs))) {
      ret = OB_IO_ERROR;
      LOG_WARN("fail to get sstable directory vfs", K(ret), K(data_dir));
    } else if (OB_UNLIKELY(0 != statvfs(log_dir, &log_svfs))) {
      ret = OB_IO_ERROR;
      LOG_WARN("fail to get slog directory vfs", K(ret), K(log_dir));
    } else if (OB_UNLIKELY(0 >= log_svfs.f_bavail)) {
      ret = OB_DISK_ERROR;
      LOG_ERROR("slog disk is full", K(ret), K(log_dir), K(log_svfs.f_bavail));
    } else {
      need_reserved_ = (data_svfs.f_fsid == log_svfs.f_fsid);
    }
  }
#endif
  return ret;
}

int ObServerStorageMetaService::get_using_disk_space(int64_t &using_space) const
{
  int ret = OB_SUCCESS;
  omt::ObServerRuntimeController *omt = GCTX.server_runtime_controller_;
  using_space = 0;
  if (OB_FAIL(server_slogger_.get_using_disk_space(using_space))) {
    LOG_WARN("fail to get using disk space", K(ret), K(using_space));
  } else if (OB_ISNULL(omt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, omt is nullptr", K(ret), KP(omt));
  } else {
    if (OB_FAIL(share::check_server_runtime_ready())) {
      LOG_WARN("server runtime is not ready", K(ret));
    } else {
      int64_t local_storage_using_size = 0;
      if (OB_FAIL(share::g_mp->local_storage_meta_service()->get_slogger().get_using_disk_space(local_storage_using_size))) {
        LOG_WARN("fail to get the disk space that slog used", K(ret));
      } else {
        using_space += local_storage_using_size;
      }
    }
    if (OB_SERVER_RUNTIME_NOT_READY == ret) {
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase

// ===== definition moved from src/share/ob_local_device.cpp =====
namespace oceanbase
{
namespace share
{

int ObLocalDevice::get_data_disk_used_percentage_(
    const int64_t required_size,
    int64_t &percent) const
{
  int ret = OB_SUCCESS;
  int64_t reserved_size = storage::ObServerStorageMetaService::SLOG_RESERVED_DISK_SIZE;

  if (OB_UNLIKELY(!is_marked_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "The ObLocalDevice has not been marked", K(ret));
  } else if (OB_UNLIKELY(required_size < 0)) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "invalid argument", K(ret), K(required_size));
  } else if (OB_FAIL(SERVER_STORAGE_META_SERVICE.get_reserved_size(reserved_size))) {
    SHARE_LOG(WARN, "Fail to get reserved size", K(ret));
  } else {
    int64_t max_block_cnt = get_max_block_count(reserved_size);
    int64_t actual_free_block_cnt = free_block_cnt_;
    if (max_block_cnt > total_block_cnt_) {  // auto extend is on
      actual_free_block_cnt = max_block_cnt - total_block_cnt_ + free_block_cnt_;
    }
    const int64_t required_count = required_size / block_size_;
    const int64_t free_count = actual_free_block_cnt - required_count;
    percent = 100 - 100 * free_count / total_block_cnt_;
  }
  return ret;
}

}  // namespace share
}  // namespace oceanbase
