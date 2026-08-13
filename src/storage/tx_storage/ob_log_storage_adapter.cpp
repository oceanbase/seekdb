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
#include "storage/tx_storage/ob_log_storage_adapter.h"

#include "logservice/ob_log_handler.h"
#include "logservice/replayservice/ob_replay_status.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tx_storage/ob_memstore_freezer.h"

namespace oceanbase
{
namespace storage
{

ObLogStorageAdapter::ObLogStorageAdapter()
    : is_inited_(false),
      ls_service_(nullptr),
      memstore_freezer_(nullptr)
{}

ObLogStorageAdapter::~ObLogStorageAdapter()
{
  destroy();
}

int ObLogStorageAdapter::init(
    ObLSService *ls_service,
    ObMemstoreFreezer *memstore_freezer)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("log storage adapter init twice", K(ret));
  } else if (OB_ISNULL(ls_service) || OB_ISNULL(memstore_freezer)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid log storage composition", K(ret), KP(ls_service),
        KP(memstore_freezer));
  } else {
    ls_service_ = ls_service;
    memstore_freezer_ = memstore_freezer;
    is_inited_ = true;
    LOG_INFO("log storage adapter initialized", KP(ls_service_),
        KP(memstore_freezer_));
  }
  return ret;
}

void ObLogStorageAdapter::destroy()
{
  is_inited_ = false;
  ls_service_ = nullptr;
  memstore_freezer_ = nullptr;
}

int ObLogStorageAdapter::replay(logservice::ObLogReplayTask *replay_task)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  const int64_t start_ts = common::ObTimeUtility::fast_current_time();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("log storage adapter not initialized", K(ret));
  } else if (OB_ISNULL(replay_task)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("replay task is null", K(ret));
  } else if (OB_FAIL(ls_service_->get_ls(ls))) {
  } else if (logservice::ObLogBaseType::PADDING_LOG_BASE_TYPE ==
             replay_task->log_type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("padding log entry cannot be replayed", K(ret),
        KPC(replay_task));
  } else if (OB_FAIL(ls->replay(replay_task->log_type_,
                                replay_task->get_replay_payload(),
                                replay_task->get_replay_payload_size(),
                                replay_task->lsn_,
                                replay_task->scn_))) {
  }

  if (OB_EAGAIN == ret) {
    if (common::OB_INVALID_TIMESTAMP == replay_task->first_handle_ts_) {
      replay_task->first_handle_ts_ = start_ts;
      replay_task->print_error_ts_ = start_ts;
    } else {
      replay_task->retry_cost_ = start_ts - replay_task->first_handle_ts_;
      if (start_ts - replay_task->print_error_ts_ >
          MAX_SINGLE_RETRY_WARNING_TIME_THRESHOLD) {
        if (replay_task->retry_cost_ > 100 * 1000 * 1000 &&
            REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
          LOG_ERROR("single replay task retry cost too much time",
              K(ret), KPC(replay_task));
        } else {
          LOG_WARN("single replay task retry cost too much time",
              K(ret), KPC(replay_task));
        }
        replay_task->print_error_ts_ = start_ts;
      }
    }
  }

  replay_task->replay_cost_ =
      common::ObTimeUtility::fast_current_time() - start_ts;
  if (replay_task->replay_cost_ > MAX_SINGLE_REPLAY_WARNING_TIME_THRESHOLD) {
    if (replay_task->replay_cost_ > MAX_SINGLE_REPLAY_ERROR_TIME_THRESHOLD &&
        !get_replay_is_writing_throttling() &&
        !lib::is_mini_mode()) {
      LOG_ERROR_RET(OB_ERR_TOO_MUCH_TIME,
          "single replay task cost too much time", KPC(replay_task));
    } else {
      LOG_WARN_RET(OB_ERR_TOO_MUCH_TIME,
          "single replay task cost too much time", KPC(replay_task));
    }
  }
  return ret;
}

int ObLogStorageAdapter::wait_append_sync()
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("log storage adapter not initialized", K(ret));
  } else if (OB_FAIL(ls_service_->get_ls(ls))) {
  } else if (OB_ISNULL(ls->get_log_handler())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log handler is null", K(ret));
  } else {
    ls->get_log_handler()->wait_append_sync();
  }
  return ret;
}

bool ObLogStorageAdapter::is_replay_pending_log_too_large(
    const int64_t pending_size)
{
  return !is_inited_ || OB_ISNULL(memstore_freezer_) ||
      memstore_freezer_->is_replay_pending_log_too_large(pending_size);
}

int ObLogStorageAdapter::get_log_handler(
    logservice::ObLogHandler *&log_handler)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  log_handler = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("log storage adapter not initialized", K(ret));
  } else if (OB_FAIL(ls_service_->get_ls(ls))) {
  } else if (OB_ISNULL(log_handler = ls->get_log_handler())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log handler is null", K(ret));
  }
  return ret;
}

int ObLogStorageAdapter::get_unrecyclable_log_disk_size(
    int64_t &unrecyclable_log_disk_size)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  unrecyclable_log_disk_size = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("log storage adapter not initialized", K(ret));
  } else if (OB_FAIL(ls_service_->get_ls(ls))) {
  } else {
    logservice::ObLogHandler *log_handler = ls->get_log_handler();
    palf::LSN end_lsn;
    const palf::LSN base_lsn = ls->get_clog_base_lsn();
    if (OB_ISNULL(log_handler)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("log handler is null", K(ret));
    } else if (OB_FAIL(log_handler->get_end_lsn(end_lsn))) {
    } else if (end_lsn < base_lsn) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("end lsn is smaller than base lsn",
          K(ret), K(end_lsn), K(base_lsn));
    } else {
      unrecyclable_log_disk_size = end_lsn - base_lsn;
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
