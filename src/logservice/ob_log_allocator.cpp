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

#include "ob_log_allocator.h"
#include "observer/omt/ob_server_runtime_controller.h"
#include "share/ob_server_struct.h"
#include "logservice/palf/log_shared_task.h"
#include "logservice/replayservice/ob_replay_status.h"

namespace oceanbase
{
using namespace share;
using namespace palf;
using namespace logservice;
namespace common
{

ObLogAllocator::ObLogAllocator()
  : total_limit_(INT64_MAX), pending_replay_mutator_size_(0),
    LOG_HANDLE_SUBMIT_TASK_SIZE(sizeof(palf::LogHandleSubmitTask)),
    LOG_IO_FLUSH_LOG_TASK_SIZE(sizeof(palf::LogIOFlushLogTask)),
    LOG_IO_FLUSH_META_TASK_SIZE(sizeof(palf::LogIOFlushMetaTask)),
    LOG_IO_TRUNCATE_PREFIX_BLOCKS_TASK_SIZE(sizeof(palf::LogIOTruncatePrefixBlocksTask)),
    LOG_IO_PURGE_THROTTLING_TASK_SIZE(sizeof(palf::LogIOPurgeThrottlingTask)),
    clog_blk_alloc_(),
    replay_log_task_blk_alloc_(REPLAY_MEM_LIMIT_THRESHOLD),
    clog_ge_alloc_(ObMemAttr(ObModIds::OB_CLOG_GE), ObVSliceAlloc::DEFAULT_BLOCK_SIZE, clog_blk_alloc_),
    log_handle_submit_task_alloc_(LOG_HANDLE_SUBMIT_TASK_SIZE, ObMemAttr("HandleSubmit"), choose_blk_size(LOG_HANDLE_SUBMIT_TASK_SIZE), clog_blk_alloc_, this),
    log_io_flush_log_task_alloc_(LOG_IO_FLUSH_LOG_TASK_SIZE, ObMemAttr("FlushLog"), choose_blk_size(LOG_IO_FLUSH_LOG_TASK_SIZE), clog_blk_alloc_, this),
    log_io_flush_meta_task_alloc_(LOG_IO_FLUSH_META_TASK_SIZE, ObMemAttr("FlushMeta"), choose_blk_size(LOG_IO_FLUSH_META_TASK_SIZE), clog_blk_alloc_, this),
    log_io_truncate_prefix_blocks_task_alloc_(LOG_IO_TRUNCATE_PREFIX_BLOCKS_TASK_SIZE, ObMemAttr("FlushMeta"), choose_blk_size(LOG_IO_TRUNCATE_PREFIX_BLOCKS_TASK_SIZE), clog_blk_alloc_, this),
    replay_log_task_alloc_(ObMemAttr(ObModIds::OB_LOG_REPLAY_TASK), common::OB_MALLOC_BIG_BLOCK_SIZE, replay_log_task_blk_alloc_),
    log_io_purge_throttling_task_alloc_(LOG_IO_PURGE_THROTTLING_TASK_SIZE, ObMemAttr("PurgeThrottle"), choose_blk_size(LOG_IO_PURGE_THROTTLING_TASK_SIZE), clog_blk_alloc_, this)
{
  double min_cpu = 0;
  double max_cpu = 0;
  omt::ObServerRuntimeController *omt = GCTX.server_runtime_controller_;
  if (NULL == omt) {
  } else if (OB_SUCCESS != omt->get_server_cpu(min_cpu, max_cpu)) {
  } else {
    const int32_t nway = (int32_t)max_cpu;
    set_nway(nway);
  }
}

ObLogAllocator::~ObLogAllocator()
{
  OB_LOG(INFO, "~ObLogAllocator");
  destroy();
}

void ObLogAllocator::destroy()
{
  OB_LOG(INFO, "ObLogAllocator destroy");
  clog_ge_alloc_.destroy();
  log_handle_submit_task_alloc_.destroy();
  log_io_flush_log_task_alloc_.destroy();
  log_io_flush_meta_task_alloc_.destroy();
  log_io_truncate_prefix_blocks_task_alloc_.destroy();
  log_io_purge_throttling_task_alloc_.destroy();
  replay_log_task_alloc_.destroy();
}

int ObLogAllocator::choose_blk_size(int obj_size)
{
  static const int MIN_SLICE_CNT = 64;
  int blk_size = OB_MALLOC_NORMAL_BLOCK_SIZE;  // default blk size is 8KB
  if (obj_size <= 0) {
  } else if (MIN_SLICE_CNT <= (OB_MALLOC_NORMAL_BLOCK_SIZE / obj_size)) {
  } else if (MIN_SLICE_CNT <= (OB_MALLOC_MIDDLE_BLOCK_SIZE / obj_size)) {
    blk_size = OB_MALLOC_MIDDLE_BLOCK_SIZE;
  } else {
    blk_size = OB_MALLOC_BIG_BLOCK_SIZE;
  }
  return blk_size;
}

void *ObLogAllocator::ge_alloc(const int64_t size)
{
  void *ptr = NULL;
  ptr = clog_ge_alloc_.alloc(size);
  return ptr;
}

void ObLogAllocator::ge_free(void *ptr)
{
  clog_ge_alloc_.free(ptr);
}

void *ObLogAllocator::alloc(const int64_t size)
{
  return ob_malloc(size, lib::ObMemAttr("LogAlloc"));
}

void *ObLogAllocator::alloc(const int64_t size, const lib::ObMemAttr &attr)
{
  return ob_malloc(size, attr);
}

void ObLogAllocator::free(void *ptr)
{
  ob_free(ptr);
}

const ObBlockAllocMgr &ObLogAllocator::get_clog_blk_alloc_mgr() const
{
  return clog_blk_alloc_;
}

LogIOFlushLogTask *ObLogAllocator::alloc_log_io_flush_log_task(
		const int64_t palf_epoch)
{
  LogIOFlushLogTask *ret_ptr = NULL;
  void *ptr = log_io_flush_log_task_alloc_.alloc();
  if (NULL != ptr) {
    ret_ptr = new(ptr)LogIOFlushLogTask(palf_epoch);
    ATOMIC_INC(&flying_log_task_);
  }
  return ret_ptr;
}

void ObLogAllocator::free_log_io_flush_log_task(LogIOFlushLogTask *ptr)
{
  if (OB_LIKELY(NULL != ptr)) {
    ptr->~LogIOFlushLogTask();
    log_io_flush_log_task_alloc_.free(ptr);
    ATOMIC_DEC(&flying_log_task_);
  }
}

LogHandleSubmitTask *ObLogAllocator::alloc_log_handle_submit_task(
		const int64_t palf_epoch)
{
  LogHandleSubmitTask *ret_ptr = NULL;
  void *ptr = log_handle_submit_task_alloc_.alloc();
  if (NULL != ptr) {
    ret_ptr = new(ptr)LogHandleSubmitTask(palf_epoch);
    ATOMIC_INC(&flying_log_handle_submit_task_);
  }
  return ret_ptr;
}

void ObLogAllocator::free_log_handle_submit_task(LogHandleSubmitTask *ptr)
{
  if (OB_LIKELY(NULL != ptr)) {
    ptr->~LogHandleSubmitTask();
    log_handle_submit_task_alloc_.free(ptr);
    ATOMIC_DEC(&flying_log_handle_submit_task_);
  }
}

LogIOFlushMetaTask *ObLogAllocator::alloc_log_io_flush_meta_task(
		const int64_t palf_epoch)
{
  LogIOFlushMetaTask *ret_ptr = NULL;
  void *ptr = log_io_flush_meta_task_alloc_.alloc();
  if (NULL != ptr) {
    ret_ptr = new(ptr)LogIOFlushMetaTask(palf_epoch);
    ATOMIC_INC(&flying_meta_task_);
  }
  return ret_ptr;
}

void ObLogAllocator::free_log_io_flush_meta_task(LogIOFlushMetaTask *ptr)
{
  if (OB_LIKELY(NULL != ptr)) {
    ptr->~LogIOFlushMetaTask();
    log_io_flush_meta_task_alloc_.free(ptr);
    ATOMIC_DEC(&flying_meta_task_);
  }
}

palf::LogIOTruncatePrefixBlocksTask *ObLogAllocator::alloc_log_io_truncate_prefix_blocks_task(
		const int64_t palf_epoch)
{
  LogIOTruncatePrefixBlocksTask *ret_ptr = NULL;
  void *ptr = log_io_truncate_prefix_blocks_task_alloc_.alloc();
  if (NULL != ptr) {
    ret_ptr = new(ptr)LogIOTruncatePrefixBlocksTask(palf_epoch);
  }
  return ret_ptr;
}

void ObLogAllocator::free_log_io_truncate_prefix_blocks_task(palf::LogIOTruncatePrefixBlocksTask *ptr)
{
  if (OB_LIKELY(NULL != ptr)) {
    ptr->~LogIOTruncatePrefixBlocksTask();
    log_io_truncate_prefix_blocks_task_alloc_.free(ptr);
  }
}

void *ObLogAllocator::alloc_replay_task(const int64_t size)
{
  return replay_log_task_alloc_.alloc(size);
}

void *ObLogAllocator::alloc_replay_log_buf(const int64_t size)
{
  return replay_log_task_alloc_.alloc(size);
}

void ObLogAllocator::free_replay_task(logservice::ObLogReplayTask *ptr)
{
  if (OB_LIKELY(NULL != ptr)) {
    ptr->~ObLogReplayTask();
    replay_log_task_alloc_.free(ptr);
  }
}

void ObLogAllocator::free_replay_log_buf(void *ptr)
{
  if (OB_LIKELY(NULL != ptr)) {
    replay_log_task_alloc_.free(ptr);
  }
}

LogIOPurgeThrottlingTask *ObLogAllocator::alloc_log_io_purge_throttling_task(const int64_t palf_epoch)
{
  LogIOPurgeThrottlingTask *ret_ptr = NULL;
  void *ptr = log_io_purge_throttling_task_alloc_.alloc();
  if (NULL != ptr) {
    ret_ptr = new(ptr)LogIOPurgeThrottlingTask(palf_epoch);
  }
  return ret_ptr;
}

void ObLogAllocator::free_log_io_purge_throttling_task(palf::LogIOPurgeThrottlingTask *ptr)
{
  if (OB_LIKELY(NULL != ptr)) {
    ptr->~LogIOPurgeThrottlingTask();
    log_io_purge_throttling_task_alloc_.free(ptr);
  }
}


void ObLogAllocator::set_nway(const int32_t nway)
{
  if (nway > 0) {
    clog_ge_alloc_.set_nway(nway);
    OB_LOG(INFO, "finish set nway", K(nway));
  }
}

void ObLogAllocator::set_limit(const int64_t total_limit)
{
  if (total_limit > 0 && total_limit != ATOMIC_LOAD(&total_limit_)) {
    ATOMIC_STORE(&total_limit_, total_limit);
    const int64_t clog_limit = total_limit / 100 * CLOG_MEM_LIMIT_PERCENT;
    const int64_t replay_limit = std::min(total_limit / 100 * REPLAY_MEM_LIMIT_PERCENT, REPLAY_MEM_LIMIT_THRESHOLD);
    clog_blk_alloc_.set_limit(clog_limit);
    replay_log_task_alloc_.set_limit(replay_limit);
    OB_LOG(INFO, "ObLogAllocator memory limit updated", K(total_limit), K(clog_limit),
        K(replay_limit));
  }
}

int64_t ObLogAllocator::get_limit() const
{
  return ATOMIC_LOAD(&total_limit_);
}


#define SLICE_FREE_OBJ(name, cls) \
void ob_slice_free_##name(typeof(cls) *ptr) \
  { \
    if (NULL != ptr) { \
      ObBlockSlicer::Item *item = (ObBlockSlicer::Item*)ptr - 1; \
      if (NULL != item->host_) { \
        ObLogAllocator *allocator = reinterpret_cast<ObLogAllocator*>(item->host_->get_owner()); \
        if (NULL != allocator) { \
          allocator->free_##name(ptr); \
        } \
      } \
    } \
  } \

}
}
