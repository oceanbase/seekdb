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

#include "ob_log_external_storage_handler.h"
#include "share/io/ob_backup_io_adapter.h"                                // ObBackupIoAdapter
#include "ob_log_external_storage_utils.h"                                    // get_and_init_io_device

namespace oceanbase
{
using namespace common;

using namespace share;
namespace logservice
{
using namespace palf;
const int64_t ObLogExternalStorageHandler::CONCURRENCY_LIMIT = 128;
const int64_t ObLogExternalStorageHandler::DEFAULT_RETRY_INTERVAL = 2 * 1000;
const int64_t ObLogExternalStorageHandler::DEFAULT_TIME_GUARD_THRESHOLD = 2 * 1000;
const int64_t ObLogExternalStorageHandler::DEFAULT_PREAD_TIME_GUARD_THRESHOLD = 100 * 1000;
const int64_t ObLogExternalStorageHandler::DEFAULT_RESIZE_TIME_GUARD_THRESHOLD = 100 * 1000;
const int64_t ObLogExternalStorageHandler::CAPACITY_COEFFICIENT = 64;
int64_t ObLogExternalStorageHandler::SINGLE_TASK_MINIMUM_SIZE = 2 * 1024 * 1024;
const int64_t ObLogExternalStorageHandler::DEFAULT_PRINT_INTERVAL = 5 * 1000 * 1000;

ObLogExternalStorageHandler::ObLogExternalStorageHandler()
    : concurrency_(-1),
      capacity_(-1),
      resize_rw_lock_(common::ObLatchIds::LOG_EXTERNAL_STORAGE_HANDLER_RW_LOCK),
      construct_async_task_lock_(common::ObLatchIds::LOG_EXTERNAL_STORAGE_HANDLER_LOCK),
      handle_adapter_(NULL),
      read_size_("ReadSize", DEFAULT_PRINT_INTERVAL),
      read_cost_("ReadCost", DEFAULT_PRINT_INTERVAL),
      is_running_(false),
      is_inited_(false)
{}

ObLogExternalStorageHandler::~ObLogExternalStorageHandler()
{
  destroy();
}

int ObLogExternalStorageHandler::init()
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else if (NULL == (handle_adapter_ = MTL_NEW(ObLogExternalStorageHandleAdapter, "ObLogEXTHandler"))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    concurrency_ = 1;
    capacity_ = CAPACITY_COEFFICIENT;
    is_running_ = false;
    is_inited_ = true;
  }
  if (OB_FAIL(ret) && OB_INIT_TWICE != ret) {
    destroy();
  }
  return ret;
}

int ObLogExternalStorageHandler::start(const int64_t concurrency)
{
  int ret = OB_SUCCESS;
  WLockGuard guard(resize_rw_lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT; 
  } else if (is_running_) {
  } else if (!is_valid_concurrency_(concurrency)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(resize_(concurrency))) {
  } else {
    is_running_ = true;
  }
  return ret;
}

void ObLogExternalStorageHandler::stop()
{
  WLockGuard guard(resize_rw_lock_);
  is_running_ = false;
}

void ObLogExternalStorageHandler::wait()
{
}

void ObLogExternalStorageHandler::destroy()
{
  CLOG_LOG_RET(WARN, OB_SUCCESS, "ObLogExternalStorageHandler destroy");
  is_inited_ = false;
  stop();
  wait();
  concurrency_ = -1;
  capacity_ = -1;
  if (OB_NOT_NULL(handle_adapter_)) {
    MTL_DELETE(ObLogExternalStorageHandleAdapter, "ObLogEXTHandler", handle_adapter_);
  }
  handle_adapter_ = NULL;
}

int ObLogExternalStorageHandler::resize(const int64_t new_concurrency,
                                        const int64_t timeout_us)
{
  int ret = OB_SUCCESS;
  ObTimeGuard time_guard("resize thread pool", DEFAULT_RESIZE_TIME_GUARD_THRESHOLD);
  time_guard.click("after hold lock");
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!is_valid_concurrency_(new_concurrency) || 0 >= timeout_us) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!check_need_resize_(new_concurrency)) {
  } else {
    WLockGuardTimeout guard(resize_rw_lock_, timeout_us, ret);
    // hold lock failed
    if (OB_FAIL(ret)) {
    } else if (!is_running_) {
      ret = OB_NOT_RUNNING;
    } else {
      do {
        ret = resize_(new_concurrency);
        if (OB_FAIL(ret)) {
          usleep(DEFAULT_RETRY_INTERVAL);
        }
      } while (OB_FAIL(ret));
      time_guard.click("after create new thread pool");
    }
  } 
  return ret;
}

int ObLogExternalStorageHandler::pread(const common::ObString &uri,
                                       const common::ObString &storage_info,
                                       const uint64_t storage_id,
                                       const int64_t offset,
                                       char *buf,
                                       const int64_t read_buf_size,
                                       int64_t &real_read_size,
                                       palf::LogIOContext &io_ctx)
{
  int ret = OB_SUCCESS;
  int64_t async_task_count = 0;
  ObTimeGuard time_guard("slow pread", DEFAULT_PREAD_TIME_GUARD_THRESHOLD);
  int64_t file_size = 0;
  int64_t real_read_buf_size = 0;
  ObLogExternalStorageCtx run_ctx;
  real_read_size = 0;
  RLockGuard guard(resize_rw_lock_);
  time_guard.click("after hold by lock");
  CONSUMER_GROUP_FUNC_GUARD(io_ctx.get_function_type());

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!is_running_) {
    ret = OB_NOT_RUNNING;
    // when uri is NFS, storage_info is empty.
  } else if (uri.empty() || 0 > offset || NULL == buf || 0 >= read_buf_size) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(handle_adapter_->get_file_size(uri, storage_info, file_size))) {
  } else if (offset > file_size) {
    ret = OB_FILE_LENGTH_INVALID;
  } else if (offset == file_size) {
    real_read_size = 0;
  } else if (FALSE_IT(time_guard.click("after get file size"))) {
    // NB: limit read size.
  } else if (FALSE_IT(real_read_buf_size = std::min(file_size - offset, read_buf_size))) {
  } else if (OB_FAIL(construct_async_pread_tasks_(
      uri, storage_info, storage_id, offset, buf, real_read_buf_size, run_ctx))) {
  } else if (FALSE_IT(time_guard.click("after construct async tasks"))) {
  } else if (OB_FAIL(run_ctx.wait(real_read_size))) {
  } else if (FALSE_IT(time_guard.click("after wait async tasks"))) {
  } else {
    read_size_.stat(real_read_size);
    read_cost_.stat(time_guard.get_diff());
  }
  return ret;
}


bool ObLogExternalStorageHandler::is_valid_concurrency_(const int64_t concurrency) const
{
  return 0 <= concurrency;
}

int64_t ObLogExternalStorageHandler::get_async_task_count_(const int64_t total_size) const
{
  // TODO by runlin: consider free async thread num.
  int64_t minimum_async_task_size = SINGLE_TASK_MINIMUM_SIZE;
  int64_t minimum_async_task_count = 1;
  int64_t async_task_count = std::max(minimum_async_task_count,
                                      std::min(concurrency_,
                                               (total_size + minimum_async_task_size - 1)/minimum_async_task_size));
  return async_task_count;
}

int ObLogExternalStorageHandler::construct_async_pread_tasks_(
    const common::ObString &uri,
    const common::ObString &storage_info,
    const uint64_t storage_id,
    const int64_t offset,
    char *read_buf,
    const int64_t read_buf_size,
    ObLogExternalStorageCtx &run_ctx)
{
  int ret = OB_SUCCESS;
  int64_t async_task_count = get_async_task_count_(read_buf_size);
  int64_t async_task_size = (read_buf_size + async_task_count - 1) / async_task_count;
  int64_t remained_task_count = async_task_count;
  int64_t remained_total_size = read_buf_size;
  if (OB_FAIL(run_ctx.init(uri, storage_info, storage_id, async_task_count, OPEN_FLAG::READ_FLAG))) {
  } else {
    int64_t curr_read_offset = offset;
    int64_t curr_read_buf_pos = 0;
    int64_t curr_task_idx = 0;
    while (remained_task_count > 0 && OB_SUCC(ret)) {
      ObLogExternalStorageCtxItem *item = NULL;
      int64_t async_task_read_buf_size = std::min(remained_total_size, async_task_size);
      if (OB_FAIL(run_ctx.get_item(curr_task_idx, item))) {
      } else if (OB_FAIL(handle_adapter_->async_pread(curr_read_offset, read_buf + curr_read_buf_pos, async_task_read_buf_size, *item))) {
      } else if (OB_FAIL(run_ctx.inc_count())) {
      } else {
        ++curr_task_idx;
        curr_read_offset += async_task_read_buf_size;
        curr_read_buf_pos += async_task_read_buf_size;
        remained_total_size -= async_task_read_buf_size;
        --remained_task_count;
      }
    }
    if (OB_FAIL(ret)) {
      int64_t unused_read_size = 0;
      run_ctx.wait(unused_read_size);
    }
  }
  return ret; 
}

int ObLogExternalStorageHandler::resize_(const int64_t new_concurrency)
{
  int ret = OB_SUCCESS;
  int64_t real_concurrency = MIN(new_concurrency, CONCURRENCY_LIMIT);  
  concurrency_ = real_concurrency;  
  capacity_ = CAPACITY_COEFFICIENT * real_concurrency;
  return ret;
}

bool ObLogExternalStorageHandler::check_need_resize_(const int64_t new_concurrency) const
{
  RLockGuard guard(resize_rw_lock_);
  return new_concurrency != concurrency_;
}

int ObLogExternalStorageHandler::convert_ret_code_(const int ret_code)
{
  int ret = ret_code;
  switch (ret_code) {
    case OB_OBJECT_STORAGE_PERMISSION_DENIED:
    case OB_OBJECT_STORAGE_PWRITE_OFFSET_NOT_MATCH:
      ret = OB_OBJECT_STORAGE_IO_ERROR;
      break;
    case OB_OBJECT_NOT_EXIST: 
      ret = OB_NO_SUCH_FILE_OR_DIRECTORY;
      break;
    default: 
      ret = OB_OBJECT_STORAGE_IO_ERROR;
  }
  return ret;
}

} // end namespace logservice
} // end namespace oceanbase
