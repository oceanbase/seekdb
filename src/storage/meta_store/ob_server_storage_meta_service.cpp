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

#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/ob_file_system_router.h"
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
    slogger_mgr_(),
    server_slogger_(nullptr),
    ckpt_slog_handler_() {}

int ObServerStorageMetaService::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("has inited", K(ret));
  } else if (OB_FAIL(slogger_mgr_.init(
        OB_FILE_SYSTEM_ROUTER.get_slog_dir(),
	      OB_FILE_SYSTEM_ROUTER.get_sstable_dir(),
	      ObLogConstants::MAX_LOG_FILE_SIZE,
        OB_FILE_SYSTEM_ROUTER.get_slog_file_spec()))) {
  } else if (OB_FAIL(slogger_mgr_.get_server_slogger(server_slogger_))) {
  } else if (OB_FAIL(ckpt_slog_handler_.init(server_slogger_))) {
  } else if (OB_FAIL(persister_.init(server_slogger_))) {
  } else if (OB_FAIL(replayer_.init(persister_, ckpt_slog_handler_))) {
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
  } else if (OB_FAIL(slogger_mgr_.start())) {
  } else if (OB_FAIL(replayer_.start_replay()))  {
  } else if (OB_FAIL(ckpt_slog_handler_.start())) {
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
    slogger_mgr_.stop();
    ckpt_slog_handler_.stop();
  }
}
void ObServerStorageMetaService::wait()
{
  if (IS_INIT) {
    slogger_mgr_.wait();
    ckpt_slog_handler_.wait();
  }
}
void ObServerStorageMetaService::destroy()
{
  slogger_mgr_.destroy();
  server_slogger_ = nullptr;
  ckpt_slog_handler_.destroy();
  persister_.destroy();
  replayer_.destroy();
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
  }
  return ret;
}

int ObServerStorageMetaService::get_reserved_size(int64_t &reserved_size) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(slogger_mgr_.get_reserved_size(reserved_size))) {
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
    slogger = server_slogger_;
  }
  return ret;
}

int ObServerStorageMetaService::write_checkpoint(bool is_force)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ckpt_slog_handler_.write_checkpoint(is_force))) {
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
