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
#include "log_engine.h"
#include "share/storage/local_store.h"
#include "logservice/ob_log_allocator.h"  // ObILogAllocator
#include "log_io_worker.h"                              // LogIOWorker
#include "log_shared_task.h"                            // LogSharedTask

namespace oceanbase
{
using namespace common;
using namespace share;
namespace palf
{

// ===================== LogEngine start =======================
LogEngine::LogEngine() :
    min_block_info_lock_(common::ObLatchIds::PALF_LOG_ENGINE_LOCK),
    min_block_max_scn_(),
    min_block_id_(LOG_INVALID_BLOCK_ID),
    min_block_min_scn_(),
    min_block_info_cache_version_(0),
    meta_write_lock_(common::ObLatchIds::PALF_LOG_ENGINE_LOCK),
    log_meta_lock_(common::ObLatchIds::PALF_LOG_ENGINE_LOCK),
    log_meta_(),
    store_(nullptr),
    log_storage_(),
    alloc_mgr_(NULL),
    log_io_worker_(NULL),
    log_shared_queue_th_(NULL),
    plugins_(NULL),
    palf_epoch_(-1),
    is_inited_(false)
{}

LogEngine::~LogEngine() { destroy(); }

int LogEngine::init(share::LocalStore &store, const char *base_dir,
                    const LogMeta &log_meta,
                    common::ObILogAllocator *alloc_mgr,
                    ILogBlockPool *log_block_pool,
                    LogCache *log_cache,
                    LogIOWorker *log_io_worker,
                    LogSharedQueueTh *log_shared_queue_th,
                    LogPlugins *plugins,
                    const int64_t palf_epoch,
                    const int64_t log_storage_block_size,
                    LogIOAdapter *io_adapter)
{
  int ret = OB_SUCCESS;
  auto update_manifest = [this](block_id_t block_id, bool) { return this->update_manifest(block_id); };
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(base_dir) || OB_ISNULL(alloc_mgr) || OB_ISNULL(log_io_worker)
      || OB_ISNULL(plugins) || OB_ISNULL(io_adapter) || !store.is_inited()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    store_ = &store;
    if (0 != log_storage_block_size && OB_FAIL(log_storage_.init(base_dir, "log",
        log_meta.get_log_snapshot_meta().base_lsn_, log_storage_block_size,
        LOG_DIO_ALIGN_SIZE, LOG_DIO_ALIGNED_BUF_SIZE_REDO, update_manifest,
        log_block_pool, plugins, log_cache, io_adapter))) {
    } else if (OB_FAIL(meta_store::create(store, log_meta))) {
    } else {
      log_meta_ = log_meta;
      alloc_mgr_ = alloc_mgr;
      log_io_worker_ = log_io_worker;
      log_shared_queue_th_ = log_shared_queue_th;
      plugins_ = plugins;
      palf_epoch_ = palf_epoch;
      is_inited_ = true;
      PALF_LOG(INFO, "LogEngine init success", K(base_dir), K(palf_epoch));
    }
  }
  if (OB_FAIL(ret) && OB_INIT_TWICE != ret) {
    PALF_LOG(WARN, "initialize PALF local metadata failed", K(ret), KCSTRING(base_dir));
    destroy();
  }
  return ret;
}

void LogEngine::destroy()
{
  if (IS_INIT) {
    PALF_LOG(INFO, "LogEngine destroy", K_(is_inited));
    is_inited_ = false;
    log_io_worker_ = NULL;
    log_shared_queue_th_ = NULL;
    alloc_mgr_ = NULL;
    store_ = nullptr;
    log_meta_.reset();
    log_storage_.destroy();
    min_block_id_ = LOG_INVALID_BLOCK_ID;
    min_block_max_scn_.reset();
    min_block_min_scn_.reset();
    min_block_info_cache_version_ = 0;
  }
}

int LogEngine::load(share::LocalStore &store, const char *base_dir,
                    common::ObILogAllocator *alloc_mgr,
                    ILogBlockPool *log_block_pool,
                    LogCache *log_cache,
                    LogIOWorker *log_io_worker,
                    LogSharedQueueTh *log_shared_queue_th,
                    LogPlugins *plugins,
                    LSN &last_group_entry_header_lsn,
                    LogGroupEntryHeader &entry_header,
                    const int64_t palf_epoch,
                    const int64_t log_storage_block_size,
                    LogIOAdapter *io_adapter,
                    bool &is_integrity)
{
  int ret = OB_SUCCESS;
  bool meta_exists = false;
  block_id_t expected_next_block_id = LOG_INVALID_BLOCK_ID;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(base_dir) || OB_ISNULL(alloc_mgr) || OB_ISNULL(log_io_worker)
      || OB_ISNULL(io_adapter) || !store.is_inited()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    store_ = &store;
    ret = meta_store::load(store, log_meta_, expected_next_block_id);
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else if (OB_SUCC(ret)) {
      meta_exists = true;
    }
    auto update_manifest = [this, expected_next_block_id](block_id_t block_id, bool in_restart) {
      return !in_restart || block_id == expected_next_block_id + 1
          ? this->update_manifest(block_id) : OB_SUCCESS;
    };
    const LSN base_lsn = meta_exists ? log_meta_.get_log_snapshot_meta().base_lsn_
                                    : LSN(PALF_INITIAL_LSN_VAL);
    if (OB_FAIL(ret)) {
    } else if (0 != log_storage_block_size && OB_FAIL(log_storage_.load(base_dir, "log",
        base_lsn, log_storage_block_size, LOG_DIO_ALIGN_SIZE, LOG_DIO_ALIGNED_BUF_SIZE_REDO,
        update_manifest, log_block_pool, plugins, log_cache, io_adapter,
        entry_header, last_group_entry_header_lsn))) {
    } else if (OB_FAIL(integrity_verify_(meta_exists, last_group_entry_header_lsn, is_integrity))) {
    } else if (meta_exists && 0 != log_storage_block_size
        && OB_FAIL(try_clear_up_holes_and_check_storage_integrity_(
            last_group_entry_header_lsn, expected_next_block_id, entry_header))) {
    } else {
      palf_epoch_ = palf_epoch;
      alloc_mgr_ = alloc_mgr;
      log_io_worker_ = log_io_worker;
      log_shared_queue_th_ = log_shared_queue_th;
      is_inited_ = true;
      PALF_LOG(INFO, "LogEngine load success", K(log_meta_), K(log_storage_), K(palf_epoch));
    }
  }
  if (OB_FAIL(ret) && OB_INIT_TWICE != ret) {
    PALF_LOG(WARN, "initialize PALF local metadata failed", K(ret), KCSTRING(base_dir));
    destroy();
  }
  return ret;
}

int LogEngine::integrity_verify_(bool meta_exists,
                                 const LSN &last_group_entry_header_lsn,
                                 bool &is_integrity)
{
  int ret = OB_SUCCESS;
  is_integrity = true;
  bool meta_is_valid = meta_exists;
  bool redo_is_valid = last_group_entry_header_lsn.is_valid();
  // 1. meta is empty, but redo is not empty, unexpected error
  // 2. meta and redo are both empty, move this directory to another place, and remove this from PalfEnvImpl
  // 3. meta is not empty, but redo is empty, normal case
  // 4. meta and redo are not empty, normal case
  // In case2, we will load LogEngine success, and remove PalfHandleImpl in PalfEnvImpl.
  if (false == meta_is_valid && true == redo_is_valid) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "local PALF metadata is missing, but redo is not empty, unexpected error", K(ret), KPC(this));
  } else if (false == meta_is_valid && false == redo_is_valid) {
    is_integrity = false;
    PALF_LOG(WARN, "log engine is not integrity", KPC(this));
  } else {
  }
  return ret;
}

int LogEngine::submit_flush_log_task(const FlushLogCbCtx &flush_log_cb_ctx,
                                     const LogWriteBuf &write_buf)
{
  int ret = OB_SUCCESS;
  LogIOFlushLogTask *flush_log_task = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogEngine not inited!!!", K(ret));
  } else if (false == flush_log_cb_ctx.is_valid() || false == write_buf.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(flush_log_cb_ctx), K(write_buf));
  } else if (OB_FAIL(generate_flush_log_task_(flush_log_cb_ctx, write_buf, flush_log_task))) {
  } else if (OB_FAIL(log_io_worker_->submit_io_task(flush_log_task))) {
  } else {
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(flush_log_task)) {
    alloc_mgr_->free_log_io_flush_log_task(flush_log_task);
    flush_log_task = NULL;
  }
  return ret;
}

int LogEngine::submit_handle_submit_task()
{
  int ret = OB_SUCCESS;
  LogHandleSubmitTask *handle_submit_task = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogEngine not inited", K(ret), KPC(this));
  } else if (OB_ISNULL(log_shared_queue_th_)) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "log_shared_queue_th_ is NULL", K(ret), KPC(this));
  } else if (OB_FAIL(generate_handle_submit_task_(handle_submit_task))) {
  } else if (OB_FAIL(log_shared_queue_th_->push_submit_log_task(handle_submit_task))) {
    if (OB_IN_STOP_STATE == ret) {
      if (REACH_TIME_INTERVAL(100 * 1000)) {
        PALF_LOG(WARN, "push task failed", K(ret), KPC(this), KPC(handle_submit_task));
      }
    } else {
      PALF_LOG(ERROR, "push task failed", K(ret), KPC(this), KPC(handle_submit_task));
    }
  } else {
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(handle_submit_task)) {
    alloc_mgr_->free_log_handle_submit_task(handle_submit_task);
    handle_submit_task = NULL;
  }
  return ret;
}

int LogEngine::submit_truncate_prefix_blocks_task(
    const TruncatePrefixBlocksCbCtx &truncate_prefix_blocks_ctx)
{
  int ret = OB_SUCCESS;
  LogIOTruncatePrefixBlocksTask *truncate_prefix_blocks_task = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogEngine not inited!!!", K(ret));
  } else if (false == truncate_prefix_blocks_ctx.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(truncate_prefix_blocks_ctx));
  } else if (OB_FAIL(generate_truncate_prefix_blocks_task_(truncate_prefix_blocks_ctx,
                                                           truncate_prefix_blocks_task))) {
  } else if (OB_FAIL(log_io_worker_->submit_io_task(truncate_prefix_blocks_task))) {
  } else {
    PALF_LOG(
        INFO, "submit_truncate_prefix_blocks_task success", K(ret), K(truncate_prefix_blocks_ctx));
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(truncate_prefix_blocks_task)) {
    alloc_mgr_->free_log_io_truncate_prefix_blocks_task(truncate_prefix_blocks_task);
    truncate_prefix_blocks_task = NULL;
  }
  return ret;
}

// ====================== LogStorage start =====================
int LogEngine::append_log(const LSN &lsn, const LogWriteBuf &write_buf, const SCN &scn)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogEngine not inited!!!", K(ret), K_(is_inited));
  } else if (false == lsn.is_valid() || false == write_buf.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K_(is_inited), K(lsn), K(write_buf));
  } else if (OB_FAIL(log_storage_.writev(lsn, write_buf, scn))) {
  } else {
  }
  return ret;
}

int LogEngine::append_log(const LSNArray &lsn_array, const LogWriteBufArray &write_buf_array,
                          const SCNArray &scn_array)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogEngine not inited!!!", K(ret), K_(is_inited));
  } else if (OB_FAIL(log_storage_.writev(lsn_array, write_buf_array, scn_array))) {
  } else {
  }
  return ret;
}

int LogEngine::read_log(const LSN &lsn,
                        const int64_t in_read_size,
                        ReadBuf &read_buf,
                        int64_t &out_read_size)
{
  int ret = OB_SUCCESS;
  LogIOContext io_ctx(LogIOUser::DEFAULT);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogEngine not inited!!!", K(ret), K_(is_inited));
  } else if (false == lsn.is_valid() || 0 >= in_read_size || false == read_buf.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K_(is_inited), K(lsn), K(in_read_size), K(read_buf));
  } else if (OB_FAIL(log_storage_.pread(lsn, in_read_size, read_buf, out_read_size, io_ctx))) {
  } else {
  }
  return ret;
}

int LogEngine::read_group_entry_header(const LSN &lsn, LogGroupEntryHeader &log_group_entry_header)
{
  int ret = OB_SUCCESS;
  // 4K is enough to read log_group_entry_header;
  const int64_t in_read_size = MAX_LOG_HEADER_SIZE;
  ReadBufGuard read_buf_guard("LogEngine", in_read_size);
  ReadBuf &read_buf = read_buf_guard.read_buf_;
  int64_t out_read_size = 0;
  int64_t pos = 0;
  LogIOContext io_ctx(LogIOUser::META_INFO);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (false == lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!read_buf.is_valid()) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    PALF_LOG(WARN, "allocate memory failed", KPC(this), K(lsn));
  } else if (OB_FAIL(log_storage_.pread(lsn, in_read_size, read_buf, out_read_size, io_ctx))) {
  } else if (OB_FAIL(log_group_entry_header.deserialize(read_buf.buf_, in_read_size, pos))) {
  } else if (false == log_group_entry_header.check_header_integrity()) {
    ret = OB_INVALID_DATA;
    PALF_LOG(ERROR, "the data has been corrupted!!!", K(ret), K(lsn), K(log_group_entry_header));
  } else {
  }
  return ret;
}

int LogEngine::truncate(const LSN &lsn)
{
  int ret = OB_SUCCESS;
  LogGroupEntryHeader log_group_entry_header;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogEngine not inited!!!", K(ret), K_(is_inited));
  } else if (false == lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K_(is_inited), K(lsn));
  } else if (OB_FAIL(read_group_entry_header(lsn, log_group_entry_header))
      && OB_ERR_OUT_OF_UPPER_BOUND != ret) {
    PALF_LOG(ERROR, "read_group_entry_header failed, unexpected error, lsn must be the start position"
        "of one LogGroupEntry", K(ret), K(lsn), K_(is_inited));
  } else if (OB_FAIL(log_storage_.truncate(lsn))) {
  } else {
    (void)reset_min_block_info_();
    PALF_LOG(INFO, "truncate success", K(lsn));
  }
  return ret;
}

int LogEngine::truncate_prefix_blocks(const LSN &lsn)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogEngine not inited!!!", K(ret), K_(is_inited));
  } else if (false == lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K_(is_inited), K(lsn));
  } else if (OB_FAIL(log_storage_.truncate_prefix_blocks(lsn))) {
  } else {
    (void)reset_min_block_info_();
    PALF_LOG(INFO, "truncate_prefix_blocks success", KPC(this), K(lsn));
  }
  return ret;
}

// NB: delete_block only called by GC
//
// Nowdays, may be concurrently executed with 'truncate_prefix_blocks', need handle follow cases:
// 1. delete block may be failed;
// 2. get_block_min_scn may be failed, and then, we need reset 'min_block_scn' which used by GC.
int LogEngine::delete_block(const block_id_t &block_id)
{
  int ret = OB_SUCCESS;
  block_id_t next_block_id = block_id + 1;
  SCN next_block_max_scn;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogEngine not inited!!!", K(ret), K_(is_inited));
  } else if (false == is_valid_block_id(block_id)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K_(is_inited), K(block_id));
  } else if (OB_FAIL(log_storage_.delete_block(block_id))) {
  } else {
    PALF_LOG(INFO, "delete success", K(block_id), K_(is_inited));
  }

  // The block named by 'block_id' may be concurrently deleted while advancing base info;
  // in this way, we don't need update min block info.
  if (OB_NO_SUCH_FILE_OR_DIRECTORY == ret) {
    PALF_LOG(INFO, "file not exist, may be concurrently delete by others module", K(ret), K(block_id));
    ret = OB_SUCCESS;
  } else if (OB_SUCC(ret)) {
    int64_t min_block_info_cache_version = -1;
    {
      ObSpinLockGuard guard(min_block_info_lock_);
      min_block_info_cache_version = min_block_info_cache_version_;
    }
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = get_block_min_scn(next_block_id + 1, next_block_max_scn))) {
      PALF_LOG(WARN, "get the max ts of next block failed", K(tmp_ret), K(next_block_id));
      reset_min_block_info_();
    } else {
      // assume there are four blocks on disk, | 10 | 11 | 12 | 13 |, only when min using block id(i.e. cacluate by snapshot)
      // is greater than or equal to 12, we can delete blocks whose names is smaller than or equal to 10.
      //
      // assume the block to be deleted is 10(i.e. min_block_id is 10), next_block_id is 11, next_block_id + 1 is 12.
      // because the block whose names with 'next_block_id + 1' may be truncated, double check
      // min_block_info_cache_version is important, otherwise, the cache of min block info
      // (i.e. min_block_min_scn_, min_block_max_scn_) is incorrect, consider following case:
      // T1 timestamp, thread A call delete_block, and get_block_min_scn successfully(e.g. next_block_id + 1 is 12);
      // T2 timestamp, thread B calls truncate, the content of block 12 is overwritten;
      // T3 timestamp, thread A call set_min_block_info_for_gc_, and the min_block_info(i.e. min_block_max_scn_, min_block_min_scn_)
      // will reset to incorrect scn.
      set_min_block_info_for_gc_(min_block_info_cache_version, next_block_id, next_block_max_scn);
    }
  }
  return ret;
}

int LogEngine::get_block_min_scn(const block_id_t &block_id, SCN &min_scn) const
{
  return log_storage_.get_block_min_scn(block_id, min_scn);
}

const LSN LogEngine::get_begin_lsn() const { return log_storage_.get_begin_lsn(); }

int LogEngine::get_block_id_range(block_id_t &min_block_id, block_id_t &max_block_id) const
{
  return log_storage_.get_block_id_range(min_block_id, max_block_id);
}

int LogEngine::get_min_block_info_for_gc(block_id_t &block_id, SCN &max_scn)
{
  int ret = OB_SUCCESS;
  block_id_t min_block_id_cache = LOG_INVALID_BLOCK_ID;
  block_id_t min_block_id = LOG_INVALID_BLOCK_ID;
  block_id_t max_block_id = LOG_INVALID_BLOCK_ID;
  SCN min_block_max_scn;
  int64_t min_block_info_cache_version = -1;

  do {
    ObSpinLockGuard guard(min_block_info_lock_);
    min_block_id_cache = min_block_id_;
    min_block_max_scn = min_block_max_scn_;
    min_block_info_cache_version = min_block_info_cache_version_;
  } while (0);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "LogEngine is not inited", K(ret), KPC(this));
  } else if (OB_FAIL(get_block_id_range(min_block_id, max_block_id))) {
  } else if (min_block_id_cache == min_block_id && min_block_max_scn.is_valid()) {
    block_id = min_block_id;
    max_scn = min_block_max_scn;
    // NB: used next block min_block_ts as the max_scn of current block
  } else if (OB_FAIL(get_block_min_scn(min_block_id+1, min_block_max_scn))) {
  } else {
    // after the first call, 'min_block_max_scn_' is always valid (except after base-info advancement or truncate)
    // it's important to get 'min_block_info_cache_version' before 'get_block_min_scn', otherwise, the cache of
    // min block info(i.e. min_block_max_scn_, min_block_min_scn_) is incorrect, consider following case:
    // T1 timestamp, thread A call get_min_block_info_for_gc, and get_block_min_scn successfully(e.g. min_block_id is 10);
    // T2 timestamp, thread B calls truncate, the content of block 11 is overwritten;
    // T3 timestamp, thread A call set_min_block_info_for_gc_, and the min_block_info(i.e. min_block_max_scn_, min_block_min_scn_)
    // will reset to incorrect scn.
    set_min_block_info_for_gc_(min_block_info_cache_version, min_block_id, min_block_max_scn);
    block_id = min_block_id;
    max_scn = min_block_max_scn;
  }
  return ret;
}

int LogEngine::get_min_block_info(block_id_t &block_id, SCN &min_scn)
{
  int ret = OB_SUCCESS;
  block_id_t min_block_id_cache = LOG_INVALID_BLOCK_ID;
  block_id_t min_block_id = LOG_INVALID_BLOCK_ID;
  block_id_t max_block_id = LOG_INVALID_BLOCK_ID;
  SCN min_block_min_scn;
  int64_t min_block_info_cache_version = -1;

  do {
    ObSpinLockGuard guard(min_block_info_lock_);
    min_block_id_cache = min_block_id_;
    min_block_min_scn = min_block_min_scn_;
    min_block_info_cache_version = min_block_info_cache_version_;
  } while (0);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "LogEngine is not inited", K(ret), KPC(this));
  } else if (OB_FAIL(get_block_id_range(min_block_id, max_block_id))) {
    // only use cache value when the min_block_id_cache is same as min_block_id get from LogBlockMgr
  } else if (min_block_id_cache == min_block_id && min_block_min_scn.is_valid()) {
    block_id = min_block_id;
    min_scn = min_block_min_scn;
  } else if (OB_FAIL(get_block_min_scn(min_block_id, min_block_min_scn))) {
  } else {
    // after the first call, 'min_block_min_scn_' is always valid (except after base-info advancement or truncate)
    // it's important to get 'min_block_info_cache_version' before 'get_block_min_scn', otherwise, the cache of
    // min block info(i.e. min_block_min_scn_) is incorrect, consider following case:
    //
    // T1 timestamp, thread A call get_min_block_info, and get_block_min_scn successfully(e.g. min_block_id is 10);
    // T2 timestamp, thread B calls truncate, the content of block 10 is overwritten;
    // T3 timestamp, thread A call set_min_block_info_, and the min_block_info(i.e. min_block_min_scn_) will reset
    // to incorrect scn.
    set_min_block_info_(min_block_info_cache_version, min_block_id, min_block_min_scn);
    block_id = min_block_id;
    min_scn = min_block_min_scn;
  }
  return ret;
}

LogMeta LogEngine::get_log_meta() const
{
  ObSpinLockGuard guard(log_meta_lock_);
  return log_meta_;
}

LSN LogEngine::get_base_lsn_used_for_block_gc() const
{
  ObSpinLockGuard guard(log_meta_lock_);
  return log_meta_.get_log_snapshot_meta().base_lsn_;
}

int LogEngine::get_total_used_disk_space(int64_t &total_used_size_byte,
                                         int64_t &unrecyclable_disk_space) const
{
  int ret = OB_SUCCESS;
  block_id_t min_block_id = LOG_INVALID_BLOCK_ID;
  block_id_t max_block_id = LOG_INVALID_BLOCK_ID;
  int64_t block_size = 0;
  total_used_size_byte = 0;
  unrecyclable_disk_space = 0;
  if (OB_FAIL(get_block_id_range(min_block_id, max_block_id)) && OB_ENTRY_NOT_EXIST != ret) {
  } else if (OB_ENTRY_NOT_EXIST == ret) {
    ret = OB_SUCCESS;
  } else if (OB_FAIL(log_storage_.get_logical_block_size(block_size))) {
  } else {
    total_used_size_byte = (max_block_id - min_block_id) * (block_size + MAX_INFO_BLOCK_SIZE)
        + lsn_2_offset(log_storage_.get_end_lsn(), block_size) + MAX_INFO_BLOCK_SIZE;
    unrecyclable_disk_space = log_storage_.get_end_lsn() - get_base_lsn_used_for_block_gc();
  }
  return ret;
}

int LogEngine::raw_read(const LSN &lsn,
                        const int64_t in_read_size,
                        const bool need_read_block_header,
                        ReadBuf &read_buf,
                        int64_t &out_read_size,
                        LogIOContext &io_ctx)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogEngine not inited!!!", K(ret), K_(is_inited));
  } else if (false == lsn.is_valid() || 0 >= in_read_size || false == read_buf.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K_(is_inited), K(lsn), K(in_read_size), K(read_buf));
  } else if (need_read_block_header) {
    ret = log_storage_.pread_with_block_header(lsn, in_read_size, read_buf, out_read_size, io_ctx);
  } else if (!need_read_block_header) {
    ret = log_storage_.pread(lsn, in_read_size, read_buf, out_read_size, io_ctx);
  } else {}
  return ret;
}

// ====================== LogStorage end =======================

// ===================== MetaStorage start =====================
int LogEngine::advance_base_lsn(const LSN &lsn)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard write_guard(meta_write_lock_);
  LogMeta next = get_log_meta();
  LogSnapshotMeta snapshot = next.get_log_snapshot_meta();
  if (OB_FAIL(write_guard.get_ret())) {
  } else if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (snapshot.base_lsn_ >= lsn) {
    // A concurrent caller may already have persisted this or a later position.
  } else if (OB_FAIL(snapshot.generate(lsn, snapshot.prev_log_info_, snapshot.prev_log_tail_lsn_))) {
  } else if (OB_FAIL(next.update_log_snapshot_meta(snapshot))) {
  } else if (OB_FAIL(meta_store::save(*store_, next))) {
    PALF_LOG(WARN, "persist PALF checkpoint failed", K(ret), K(lsn));
  } else {
    // Readers and GC only observe durable state; no spin lock spans local IO.
    {
      ObSpinLockGuard guard(log_meta_lock_);
      log_meta_ = next;
    }
    PALF_LOG(INFO, "persisted PALF checkpoint", K(lsn));
  }
  return ret;
}

int LogEngine::update_manifest(const block_id_t block_id)
{
  // LogStorage invokes this while loading too, before is_inited_ becomes true.
  return store_ == nullptr ? OB_NOT_INIT : meta_store::update_manifest(*store_, block_id);
}

int LogEngine::generate_flush_log_task_(const FlushLogCbCtx &flush_log_cb_ctx,
                                        const LogWriteBuf &write_buf,
                                        LogIOFlushLogTask *&flush_log_task)
{
  int ret = OB_SUCCESS;
  // Be careful to handle the duration of this pointer
  flush_log_task = NULL;
  if (false == flush_log_cb_ctx.is_valid() || false == write_buf.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (NULL == (flush_log_task = alloc_mgr_->alloc_log_io_flush_log_task(palf_epoch_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    PALF_LOG(ERROR, "alloc_log_io_flush_log_task failed", K(ret));
  } else if (OB_FAIL(flush_log_task->init(flush_log_cb_ctx, write_buf))) {
  } else {/*do nothing*/}
  if (OB_FAIL(ret) && NULL != flush_log_task) {
    alloc_mgr_->free_log_io_flush_log_task(flush_log_task);
    flush_log_task = NULL;
  }
  return ret;
}

int LogEngine::generate_handle_submit_task_(LogHandleSubmitTask *&handle_submit_task)
{
  int ret = OB_SUCCESS;
  handle_submit_task = NULL;
  if (NULL == (handle_submit_task = alloc_mgr_->alloc_log_handle_submit_task(palf_epoch_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    PALF_LOG(ERROR, "alloc_log_handle_submit_task failed", K(ret));
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(handle_submit_task)) {
    alloc_mgr_->free_log_handle_submit_task(handle_submit_task);
    handle_submit_task = NULL;
  }
  return ret;
}

int LogEngine::generate_truncate_prefix_blocks_task_(
    const TruncatePrefixBlocksCbCtx &truncate_prefix_blocks_ctx,
    LogIOTruncatePrefixBlocksTask *&truncate_prefix_blocks_task)
{
  int ret = OB_SUCCESS;
  if (NULL
      == (truncate_prefix_blocks_task = alloc_mgr_->alloc_log_io_truncate_prefix_blocks_task(palf_epoch_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    PALF_LOG(ERROR, "alloc_log_io_truncate_prefix_blocks_task failed", K(ret));
  } else if (OB_FAIL(truncate_prefix_blocks_task->init(truncate_prefix_blocks_ctx))) {
  } else {/*do nothing*/}
  if (OB_FAIL(ret) && NULL != truncate_prefix_blocks_task) {
    alloc_mgr_->free_log_io_truncate_prefix_blocks_task(truncate_prefix_blocks_task);
    truncate_prefix_blocks_task = NULL;
  }
  return ret;
}

// Background: 
int LogEngine::try_clear_up_holes_and_check_storage_integrity_(
    const LSN &last_entry_begin_lsn,
    const block_id_t &expected_next_block_id,
    LogGroupEntryHeader &last_group_entry_header)
{
  int ret = OB_SUCCESS;
  const LogSnapshotMeta &log_snapshot_meta = log_meta_.get_log_snapshot_meta();
  const LSN base_lsn = log_snapshot_meta.base_lsn_;
  LogInfo prev_log_info;
  LSN prev_log_tail_lsn;
  const bool prev_log_info_is_valid =
    (OB_SUCCESS == log_snapshot_meta.get_prev_log_info(base_lsn, prev_log_info, prev_log_tail_lsn) ? true : false);
  // 'expected_next_block_id': ethier it's the empty block or not exist.
  block_id_t base_block_id = LOG_INVALID_BLOCK_ID;
  block_id_t min_block_id = LOG_INVALID_BLOCK_ID;
  block_id_t max_block_id = LOG_INVALID_BLOCK_ID;
  int64_t logical_block_size = 0;
  const LSN log_storage_tail = log_storage_.get_end_lsn();
  if (OB_FAIL(log_storage_.get_logical_block_size(logical_block_size))) {
  } else if (FALSE_IT(base_block_id = lsn_2_block(base_lsn, logical_block_size))) {
  } else if (OB_FAIL(log_storage_.get_block_id_range(min_block_id, max_block_id))
      && OB_ENTRY_NOT_EXIST != ret) {
    PALF_LOG(ERROR, "get_block_id_range failed", K(ret), K_(is_inited));
  } else if (OB_ENTRY_NOT_EXIST == ret) {
    // For an empty directory, either 'base_lsn' is the initial LSN or restored 'prev_log_info' is valid;
    // meanwhile, 'expected_next_block_id' must be smaller than or eaul to 'base_block_id'.
    if (true == is_valid_block_id(expected_next_block_id)
        && expected_next_block_id > base_block_id) {
      ret = OB_ERR_UNEXPECTED;
      PALF_LOG(ERROR,
               "the directory of log is empty, but expected_next_block_id is greater than "
               "base_block_id, unexpected error!!!",
               K(ret), K_(is_inited));
    } else {
      ret = OB_SUCCESS;
    }
    // If log_storage_ is not empty but last_group_entry_header is invalid, unexpected error.
    // During physical restore, base_lsn may be greater than LogStorage's tail because
    // LogSnapshotMeta is updated first.
  } else if (log_storage_.get_end_lsn() != base_lsn && !last_group_entry_header.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "unexpected error, LogStorage are not empty bus last log entry is invalid",
        K(last_entry_begin_lsn), K(expected_next_block_id), K(last_group_entry_header));
  } else {
    // NB: assume before 'truncate_prefix_blocks' finish, there is not any new write opt!!!
    //
    // For non-empty directory:
    // 1. If 'base_block_id' is greater than 'expected_next_block_id', we need delete all blocks
    //    between ['min_block_id', 'base_block_id');
    // 2. otherwise, check 'expected_next_block_id' whether is integrity.
    //
    // Ensure that:
    // 1. check LogStorage integrity only when 'expected_next_block_id' is greater than
    // 'base_block_id'; physical restore may have discarded all older blocks;
    // 2. 'min_block_id' must be smaller than or equal to 'base_block_id';
    // 3. the last block is integral, means that 'max_block_id' is ethier equal to
    // 'expected_next_block_id'(last block is
    //    empty) or 'expected_next_block_id' is equal to 'max_block_id' + 1.
    LSN log_storage_tail = last_entry_begin_lsn + last_group_entry_header.get_serialize_size()
                           + last_group_entry_header.get_data_len();
    if (false == is_valid_block_id(expected_next_block_id)) {
      PALF_LOG(INFO,
               "expected_next_block_id is invalid, no need check_last_block_whether_is_integrity_",
               K(ret), KPC(this), K(expected_next_block_id));
    } else if (expected_next_block_id <= base_block_id) {
      // do nothing
    } else if (min_block_id > base_block_id
               || false
                      == check_last_block_whether_is_integrity_(expected_next_block_id,
                                                                max_block_id, log_storage_tail)) {
      ret = OB_ERR_UNEXPECTED;
      PALF_LOG(ERROR,
               "directory is not empty, but there are something unexpected!!!(may be min block is "
               "less than base block "
               "or last block is incorrect)",
               K(min_block_id), K(base_block_id), K(expected_next_block_id), K(max_block_id));
      // NB: Advancing 'base_lsn' can create holes that 'get_block_id_range' must handle;
      // an easy way to do this is that, after advance 'base_lsn', the 'min_block_id_' of
      // 'LogBlockMgr' is lsn_2_block('base_lsn', PALF_BLOCK_SIZE), meanwhile, we need make
      // 'BlockGCTimerTask' can see the really 'min_block_id_' and 'max_block_id_'.
    } else {
    }

    // Deleting holes before 'base_block_id'
    // NB: deleting holes only when 'prev_log_info' is valid. 'prev_log_info' is valid means that
    // physical restore has advanced base info and we cannot determine whether logs before
    // 'base_lsn' were confirmed, so delete these blocks.
    // if the tail of last entry (log_storage_tail) is smaller than or equal to base lsn, need
    // reset last_group_entry_header, PalfHandleImpl will be inited with the prev log info saved
    // snapshot meta.
    if (OB_SUCC(ret) && true == prev_log_info_is_valid) {
      if (OB_FAIL(log_storage_.truncate_prefix_blocks(base_lsn))) {
      } else if (base_lsn >= last_group_entry_header.get_committed_end_lsn()) {
        PALF_LOG(WARN, "the max committed end lsn is smaller than or equal to base_lsn,"
            " base info was advanced before restart, and we will use prev_log_info"
            " to construct PalfBaseInfo", K(base_lsn), K(last_group_entry_header),
            K(log_snapshot_meta));
        last_group_entry_header.reset();
      }
    }
  }
  PALF_LOG(INFO, "try_clear_up_holes_and_check_storage_integrity_ finish", K(ret), K(min_block_id),
           K(max_block_id), K(base_block_id), K(expected_next_block_id), K(prev_log_info), K(log_snapshot_meta),
           K(prev_log_info_is_valid));
  return ret;
}

bool LogEngine::check_last_block_whether_is_integrity_(const block_id_t expected_next_block_id,
                                                       const block_id_t max_block_id,
                                                       const LSN &log_storage_tail)
{
  // NB:
  // 1. 'expected_next_block_id' == 'max_block_id' + 1, normal case
  // 2. 'expected_next_block_id' <= 'max_block_id', means:
  //    1. a truncate before stopping palf updates the manifest first,
  //       and stop palf before delete blocks on disk, 'expected_next_block_id' is smaller
  //       than or equal to 'max_block_id'.
  //    2. a switch block opt before stop palf, and just create new block on disk success,
  //       expected_next_block_id is equal to 'max_block_id', and 'max_block_id' is empty,
  //       we need update manifest to max_block_id + 1 in process of restart.
  return expected_next_block_id == max_block_id + 1
         || expected_next_block_id <= max_block_id;
}

void LogEngine::set_min_block_info_(const int64_t min_block_info_cache_version,
                                    const block_id_t min_block_id,
                                    const SCN &min_block_min_scn)
{
  ObSpinLockGuard guard(min_block_info_lock_);
  if (min_block_info_cache_version_ == min_block_info_cache_version) {
    min_block_id_ = min_block_id;
    min_block_min_scn_ = min_block_min_scn;
  }
}

void LogEngine::set_min_block_info_for_gc_(const int64_t min_block_info_cache_version,
                                           const block_id_t min_block_id,
                                           const SCN &min_block_max_scn)
{
  ObSpinLockGuard guard(min_block_info_lock_);
  if (min_block_info_cache_version_ == min_block_info_cache_version) {
    // defense code:
    // only update min_block_min_scn_ to min_block_max_scn_ when min_block_id is same as min_block_id_ + 1
    if (!is_valid_block_id(min_block_id_)
        || (min_block_id == min_block_id_ + 1 && min_block_max_scn_.is_valid())) {
      min_block_min_scn_ = min_block_max_scn_;
    } else {
      min_block_min_scn_.reset();
    }
    min_block_id_ = min_block_id;
    min_block_max_scn_ = min_block_max_scn;
  }
}

void LogEngine::reset_min_block_info_()
{
  ObSpinLockGuard guard(min_block_info_lock_);
  min_block_id_ = LOG_INVALID_BLOCK_ID;
  min_block_min_scn_.reset();
  min_block_max_scn_.reset();
  min_block_info_cache_version_++;
}

} // end namespace palf
} // end namespace oceanbase
