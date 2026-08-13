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

#include "storage/tmp_file/ob_tmp_file_page_cache_controller.h"
#include "storage/tmp_file/ob_tmp_file_manager.h"

namespace oceanbase
{
namespace tmp_file
{

int ObTmpFilePageCacheController::init()
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "ObTmpFilePageCacheController init twice");
  } else if (OB_FAIL(task_allocator_.init(lib::ObMallocAllocator::get_instance(),
                                          OB_MALLOC_MIDDLE_BLOCK_SIZE,
                                          ObMemAttr("TmpFileCtl", ObCtxIds::DEFAULT_CTX_ID)))) {
  } else if (OB_FAIL(flush_mgr_.init())) {
  } else if (OB_FAIL(flush_priority_mgr_.init())) {
  } else if (OB_FAIL(write_buffer_pool_.init())) {
  } else if (OB_FAIL(flush_thread_.init())) {
  } else if (OB_FAIL(swap_thread_.init())) {
  } else {
    flush_all_data_ = false;
    is_inited_ = true;
  }
  return ret;
}

int ObTmpFilePageCacheController::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    STORAGE_LOG(WARN, "tmp file page cache controller is not inited");
  } else if (OB_FAIL(flush_thread_.start())) {
  } else if (OB_FAIL(swap_thread_.start())) {
  }
  return ret;
}

void ObTmpFilePageCacheController::stop()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    STORAGE_LOG(WARN, "tmp file page cache controller is not inited");
  } else {
    // stop background threads should follow the order 'swap' -> 'flush' because 'swap' holds ref to 'flush'
    swap_thread_.stop();
    flush_thread_.stop();
  }
}

void ObTmpFilePageCacheController::wait()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    STORAGE_LOG(WARN, "tmp file page cache controller is not inited");
  } else {
    swap_thread_.wait();
    flush_thread_.wait();
  }
}

void ObTmpFilePageCacheController::destroy()
{
  swap_thread_.destroy();
  flush_thread_.destroy();
  task_allocator_.reset();
  write_buffer_pool_.destroy();
  flush_mgr_.destroy();
  evict_mgr_.destroy();
  flush_priority_mgr_.destroy();
  flush_all_data_ = false;
  is_inited_ = false;
}

int ObTmpFilePageCacheController::swap_job_enqueue_(ObTmpFileSwapJob *swap_job)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(swap_job)){
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "swap job is null", KR(ret));
  } else if (!swap_job->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "swap job is not valid", KR(ret), KPC(swap_job));
  } else if (OB_FAIL(swap_thread_.swap_job_enqueue(swap_job))) {
  }
  return ret;
}

int ObTmpFilePageCacheController::free_swap_job_(ObTmpFileSwapJob *swap_job)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(swap_job)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "swap job is null", KR(ret));
  } else if (swap_job->is_inited() && !swap_job->is_finished()) {
    ret = OB_EAGAIN;
    STORAGE_LOG(ERROR, "swap job is not finished", KR(ret), KPC(swap_job));
  } else {
    swap_job->~ObTmpFileSwapJob();
    task_allocator_.free(swap_job);
  }
  return ret;
}

// Refresh the temporary-file disk limit from runtime configuration with a 10 ms timeout.
void ObTmpFilePageCacheController::refresh_disk_usage_limit()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tmp file page cache controller is not inited", KR(ret));
  } else {
    const int64_t max_disk_usage = GCONF.temporary_file_max_disk_size;
    int64_t disk_limit = max_disk_usage > 0 ? max_disk_usage : 0;
    ATOMIC_SET(&disk_usage_limit_, disk_limit);
  }
}

int ObTmpFilePageCacheController::invoke_swap_and_wait(int64_t expect_swap_size, int64_t timeout_ms)
{
  int ret = OB_SUCCESS;

  int64_t mem_limit = write_buffer_pool_.get_memory_limit();
  int64_t min_swap_size =
      max(ObTmpFileGlobal::ALLOC_PAGE_SIZE, min(expect_swap_size, static_cast<int64_t>(0.2 * mem_limit)));
  expect_swap_size = upper_align(min_swap_size, ObTmpFileGlobal::ALLOC_PAGE_SIZE);

  void *task_buf = nullptr;
  ObTmpFileSwapJob *swap_job = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "tmp file page cache controller is not inited", KR(ret));
  } else if (OB_ISNULL(task_buf = task_allocator_.alloc(sizeof(ObTmpFileSwapJob)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    STORAGE_LOG(WARN, "fail to allocate memory for swap job", KR(ret));
  } else if (FALSE_IT(swap_job = new (task_buf) ObTmpFileSwapJob())) {
  } else if (OB_FAIL(swap_job->init(expect_swap_size, timeout_ms))) {
  } else if (OB_FAIL(swap_job_enqueue_(swap_job))) {
  } else {
    swap_thread_.notify_doing_swap();
    if (OB_FAIL(swap_job->wait_swap_complete())) {
    }
  }

  if (OB_NOT_NULL(swap_job)) {
    if (OB_SUCCESS != swap_job->get_ret_code()) {
      ret = swap_job->get_ret_code();
      STORAGE_LOG(WARN, "swap job complete with error code", KR(ret));
    }
    // reset swap job to set is_finished to false in case of failure to push into queue:
    // otherwise job is not finished, but it will not be executed, so it will never become finished.
    swap_job->reset();
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(free_swap_job_(swap_job))) {
    }
  }
  return ret;
}

}  // end namespace tmp_file
}  // end namespace oceanbase
