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
#include "log_engine.h"                                // LogEngine
#include "palf_env_impl.h"                             // IPalfEnvImpl::
#include "share/ob_share_util.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace palf
{

PalfHandleImpl::PalfHandleImpl()
  : lock_(common::ObLatchIds::PALF_HANDLE_IMPL_LOCK),
    sw_(),
    mode_mgr_(),
    state_mgr_(),
    log_engine_(),
    log_cache_(),
    allocator_(NULL),
    self_(),
    fs_cb_wrapper_(),
    plugins_(),
    last_locate_scn_(),
    last_locate_block_(LOG_INVALID_BLOCK_ID),
    cannot_recv_log_warn_time_(OB_INVALID_TIMESTAMP),
    log_disk_full_warn_time_(OB_INVALID_TIMESTAMP),
    wait_slide_print_time_us_(OB_INVALID_TIMESTAMP),
    append_size_stat_time_us_(OB_INVALID_TIMESTAMP),
    last_record_append_lsn_(PALF_INITIAL_LSN_VAL),
    has_set_deleted_(false),
    palf_env_impl_(NULL),
    append_cost_stat_("[PALF STAT WRITE LOG COST TIME]", PALF_STAT_PRINT_INTERVAL_US),
    flush_cb_cost_stat_("[PALF STAT FLUSH CB COST TIME]", PALF_STAT_PRINT_INTERVAL_US),
    handle_submit_log_cost_stat_("[PALF STAT HANDLE SUBMIT LOG COST TIME]", PALF_STAT_PRINT_INTERVAL_US),
    last_accum_write_statistic_time_(OB_INVALID_TIMESTAMP),
    accum_write_log_size_(0),
    last_dump_info_time_us_(OB_INVALID_TIMESTAMP),
    is_inited_(false)
{
  log_dir_[0] = '\0';
}

PalfHandleImpl::~PalfHandleImpl()
{
  destroy();
}

int PalfHandleImpl::init(const AccessMode &access_mode,
                         const PalfBaseInfo &palf_base_info,
                         const char *log_dir,
                         ObILogAllocator *alloc_mgr,
                         ILogBlockPool *log_block_pool,
                         LogIOWorker *log_io_worker,
                         LogSharedQueueTh *log_shared_queue_th,
                         IPalfEnvImpl *palf_env_impl,
                         const common::ObAddr &self,
                         const int64_t palf_epoch,
                         LogIOAdapter *io_adapter)
{
  int ret = OB_SUCCESS;
  int pret = 0;
  LogMeta log_meta;
  LogSnapshotMeta snapshot_meta;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    PALF_LOG(ERROR, "LogServer has inited", K(ret));
  } else if (false == is_valid_access_mode(access_mode)
             || false == palf_base_info.is_valid()
             || NULL == log_dir
             || NULL == alloc_mgr
             || NULL == log_block_pool
             || NULL == log_io_worker
             || NULL == log_shared_queue_th 
             || NULL == palf_env_impl
             || false == self.is_valid()
             || palf_epoch < 0) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_base_info),
        K(access_mode), K(log_dir), K(alloc_mgr), K(log_block_pool),
        K(log_io_worker), K(log_shared_queue_th), K(palf_env_impl), K(self), K(palf_epoch));
  } else if (OB_FAIL(log_meta.generate_by_palf_base_info(palf_base_info, access_mode))) {
  } else if ((pret = snprintf(log_dir_, MAX_PATH_SIZE, "%s", log_dir)) && false) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "error unexpected", K(ret));
  } else if (OB_FAIL(log_engine_.init(log_dir, log_meta, alloc_mgr, log_block_pool, &log_cache_, \
          log_io_worker, log_shared_queue_th, &plugins_, palf_epoch, PALF_BLOCK_SIZE, PALF_META_BLOCK_SIZE, io_adapter))) {
  } else if (OB_FAIL(do_init_mem_(palf_base_info, log_meta, log_dir, self,
          alloc_mgr, palf_env_impl))) {
  } else {
    last_accum_write_statistic_time_ = ObTimeUtility::current_time();
    PALF_EVENT("PalfHandleImpl init success", K(ret), K(self), K(access_mode), K(palf_base_info),
        K(log_dir), K(log_meta), K(palf_epoch));
  }
  return ret;
}

bool PalfHandleImpl::check_can_be_used() const
{
  return false == ATOMIC_LOAD(&has_set_deleted_);
}

int PalfHandleImpl::load(const char *log_dir,
                         ObILogAllocator *alloc_mgr,
                         ILogBlockPool *log_block_pool,
                         LogIOWorker *log_io_worker,
                         LogSharedQueueTh *log_shared_queue_th,
                         IPalfEnvImpl *palf_env_impl,
                         const common::ObAddr &self,
                         const int64_t palf_epoch,
                         LogIOAdapter *io_adapter,
                         bool &is_integrity)
{
  int ret = OB_SUCCESS;
  PalfBaseInfo palf_base_info;
  LSN last_group_entry_header_lsn;
  LogGroupEntryHeader entry_header;
  LSN max_committed_end_lsn;
  LogSnapshotMeta snapshot_meta;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else if (NULL == log_dir
             || NULL == alloc_mgr
             || NULL == log_io_worker
             || NULL == log_shared_queue_th
             || false == self.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(log_dir), K(alloc_mgr),
        K(log_io_worker), K(log_shared_queue_th));
  } else if (OB_FAIL(log_engine_.load(log_dir, alloc_mgr, log_block_pool, &log_cache_,
        log_io_worker, log_shared_queue_th, &plugins_, last_group_entry_header_lsn, entry_header, palf_epoch, PALF_BLOCK_SIZE,
        PALF_META_BLOCK_SIZE, io_adapter, is_integrity))) {
  } else if (false == is_integrity) {
    PALF_LOG(INFO, "log stream is incomplete", KPC(this));
  } else if (FALSE_IT(snapshot_meta = log_engine_.get_log_meta().get_log_snapshot_meta())) {
  } else if (FALSE_IT(max_committed_end_lsn =
         (true == entry_header.is_valid() ?
          last_group_entry_header_lsn + entry_header.get_serialize_size() + entry_header.get_data_len() :
          snapshot_meta.base_lsn_))) {
  } else if (OB_FAIL(construct_palf_base_info_(max_committed_end_lsn, palf_base_info))) {
  } else if (OB_FAIL(do_init_mem_(palf_base_info, log_engine_.get_log_meta(), log_dir, self,
          alloc_mgr, palf_env_impl))) {
  } else if (OB_FAIL(append_disk_log_to_sw_(max_committed_end_lsn))) {
  } else {
    PALF_EVENT("PalfHandleImpl load success", K(ret), K(palf_base_info), K(log_dir), K(palf_epoch));
  }
  return ret;
}

void PalfHandleImpl::destroy()
{
  WLockGuard guard(lock_);
  if (IS_INIT) {
    PALF_EVENT("PalfHandleImpl destroy", KPC(this));
    is_inited_ = false;
    diskspace_enough_ = true;
    plugins_.destroy();
    self_.reset();
    allocator_ = NULL;
    log_cache_.destroy();
    log_engine_.destroy();
    state_mgr_.destroy();
    mode_mgr_.destroy();
    sw_.destroy();
    if (false == check_can_be_used()) {
      palf_env_impl_->remove_directory(log_dir_);
    }
    palf_env_impl_ = NULL;
    last_accum_write_statistic_time_ = OB_INVALID_TIMESTAMP;
    accum_write_log_size_ = 0;
  }
}

int PalfHandleImpl::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "PalfHandleImpl has not inited!!!", K(ret));
  } else {
    PALF_LOG(INFO, "PalfHandleImpl start success", K(ret), KPC(this));
  }
  return ret;
}

int PalfHandleImpl::bootstrap()
{
  return IS_NOT_INIT ? OB_NOT_INIT : OB_SUCCESS;
}

int PalfHandleImpl::get_begin_lsn(LSN &lsn) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "PalfHandleImpl not init", K(ret), KPC(this));
  } else {
    lsn = log_engine_.get_begin_lsn();
    const LSN snapshot_base_lsn =
        log_engine_.get_log_meta().get_log_snapshot_meta().base_lsn_;
    if (lsn < snapshot_base_lsn) {
      lsn = snapshot_base_lsn;
    }
  }
  return ret;
}

int PalfHandleImpl::get_begin_scn(SCN &scn)
{
  int ret = OB_SUCCESS;
  block_id_t unused_block_id;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "PalfHandleImpl not init", K(ret), KPC(this));
  } else if (OB_FAIL(log_engine_.get_min_block_info(unused_block_id, scn))) {
  }
  return ret;
}

int PalfHandleImpl::get_base_lsn(LSN &lsn) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "PalfHandleImpl not init", K(ret), KPC(this));
  } else {
    lsn = get_base_lsn_used_for_block_gc();
  }
  return ret;
}

int PalfHandleImpl::get_base_info(const LSN &base_lsn, PalfBaseInfo &base_info)
{
  int ret = OB_SUCCESS;
  LSN curr_end_lsn = get_end_lsn();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "PalfHandleImpl not init", K(ret), KPC(this));
  } else if (base_lsn > curr_end_lsn) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", K(ret), KPC(this), K(base_lsn), K(curr_end_lsn));
  } else if (OB_FAIL(construct_palf_base_info_(base_lsn, base_info))) {
  } else {
    PALF_LOG(INFO, "get_base_info success", K(ret), KPC(this), K(base_lsn), K(curr_end_lsn), K(base_info));
  }
  return ret;
}


int PalfHandleImpl::submit_log(
    const PalfAppendOptions &opts,
    const char *buf,
    const int64_t buf_len,
    const SCN &ref_scn,
    LSN &lsn,
    SCN &scn)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "PalfHandleImpl is not inited");
  } else if (NULL == buf || buf_len <= 0 || buf_len > MAX_LOG_BODY_SIZE
             || !ref_scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", KP(buf), K(buf_len), K(ref_scn));
  } else {
    RLockGuard guard(lock_);
    if (false == palf_env_impl_->check_disk_space_enough()) {
      ret = OB_LOG_OUTOF_DISK_SPACE;
      if (palf_reach_time_interval(1 * 1000 * 1000, log_disk_full_warn_time_)) {
        PALF_LOG(WARN, "log outof disk space", KPC(this), K(opts), K(ref_scn));
      }
    } else if (!state_mgr_.can_append()) {
      ret = OB_STATE_NOT_MATCH;
      PALF_LOG(WARN, "cannot submit_log", KPC(this), KP(buf), K(buf_len),
          "state", state_mgr_.get_state(), K(opts), "mode_mgr can_append", mode_mgr_.can_append());
    } else if (OB_FAIL(sw_.submit_log(buf, buf_len, ref_scn, lsn, scn))) {
      if (OB_EAGAIN != ret) {
        PALF_LOG(WARN, "submit_log failed", KPC(this), KP(buf), K(buf_len));
      }
    } else {
      if (palf_reach_time_interval(PALF_STAT_PRINT_INTERVAL_US, append_size_stat_time_us_)) {
        PALF_LOG(INFO, "[PALF STAT APPEND DATA SIZE]", KPC(this), "append size", lsn.val_ - last_record_append_lsn_.val_);
        last_record_append_lsn_ = lsn;
      }
    }
  }
  return ret;
}

int PalfHandleImpl::submit_imported_group(
    const LSN &source_lsn,
    const SCN &source_scn,
    const char *buf,
    const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!source_lsn.is_valid() || !source_scn.is_valid()
             || OB_ISNULL(buf) || buf_len <= 0 || buf_len > MAX_LOG_BUFFER_SIZE) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid imported group", K(ret), K(source_lsn), K(source_scn),
        KP(buf), K(buf_len));
  } else {
    RLockGuard guard(lock_);
    if (!palf_env_impl_->check_disk_space_enough()) {
      ret = OB_LOG_OUTOF_DISK_SPACE;
    } else if (!state_mgr_.can_append()) {
      ret = OB_STATE_NOT_MATCH;
      PALF_LOG(WARN, "cannot submit imported group", K(ret), KPC(this),
          K(buf_len), "state", state_mgr_.get_state());
    } else if (OB_FAIL(sw_.submit_imported_group(source_lsn, source_scn, buf, buf_len))) {
      if (OB_EAGAIN != ret) {
        PALF_LOG(WARN, "submit imported group failed", K(ret), KPC(this), K(buf_len));
      }
    }
  }
  return ret;
}

int PalfHandleImpl::set_base_lsn(
    const LSN &lsn)
{
  // NB: Guarded by lock is important, otherwise there are some problems concurrent with rebuild or migrate.
  //
  // Thread1(assume it's migrate thread)
  // 1. if the 'base_lsn' of data source is greater than or equal to local, and then
  // 2. avoid the hole between blocks, delete all blocks before 'base_lsn', submit truncate prefix blocks task
  // 3. to update base lsn, submit update snpshot meta task.
  //
  //
  // Thread2(checkpoint thread)
  // 1. the clog disk is not enough, and update it via 'set_base_lsn'
  //
  // Consider that:
  //
  // Time1: thread1 has executed step1, assume the base lsn of data source is 100, local base lsn is 50,
  // and then thread2 execute 'set_base_lsn', the local base lsn set to 150
  //
  // Time2: thread1 submit truncate prefix blocks, and this task will be failed because the base lsn in this task
  // is smaller than local base lsn.
  //
  // Therefore, we need guarded by lock.
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  LSN end_lsn = get_end_lsn();
  LogSnapshotMeta log_snapshot_meta = log_engine_.get_log_meta().get_log_snapshot_meta();
  const LSN new_base_lsn(lsn_2_block(lsn, PALF_BLOCK_SIZE) * PALF_BLOCK_SIZE);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!lsn.is_valid() || lsn > end_lsn) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", K(ret), KPC(this), K(end_lsn), K(lsn));
  } else if (log_snapshot_meta.base_lsn_ >= new_base_lsn) {
    PALF_LOG(WARN, "no need to set new base lsn, curr base lsn is greater than or equal to new base lsn",
        KPC(this), K(log_snapshot_meta), K(new_base_lsn), K(lsn));
  } else {
    FlushMetaCbCtx flush_meta_cb_ctx;
    flush_meta_cb_ctx.type_ = SNAPSHOT_META;
    flush_meta_cb_ctx.base_lsn_ = new_base_lsn;
    if (OB_FAIL(log_snapshot_meta.generate(new_base_lsn,
                                           log_snapshot_meta.prev_log_info_,
                                           log_snapshot_meta.prev_log_tail_lsn_))) {
    } else if (OB_FAIL(log_engine_.submit_flush_snapshot_meta_task(flush_meta_cb_ctx, log_snapshot_meta))) {
    } else {
      PALF_EVENT("set_base_lsn success", K(ret), K(self_), K(lsn),
          K(log_snapshot_meta), K(new_base_lsn), K(flush_meta_cb_ctx));
      plugins_.record_set_base_lsn_event(new_base_lsn);
    }
  }
  return ret;
}

int PalfHandleImpl::locate_by_scn_coarsely(const SCN &scn, LSN &result_lsn)
{
  int ret = OB_SUCCESS;
  block_id_t result_block_id = LOG_INVALID_BLOCK_ID;
  result_lsn.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "PalfHandleImpl not init", KR(ret));
  } else if (OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", KR(ret), KPC(this), K(scn));
  } else if (OB_FAIL(get_block_id_by_scn_(scn, result_block_id))) {
  } else {
  }
  // 2. convert block_id to lsn
  if (OB_SUCC(ret)) {
    result_lsn = LSN(result_block_id * PALF_BLOCK_SIZE);
    LSN readable_begin_lsn;
    (void)get_begin_lsn(readable_begin_lsn);
    if (result_lsn < readable_begin_lsn) {
      result_lsn = readable_begin_lsn;
    }
    inc_update_last_locate_block_scn_(result_block_id, scn);
  }
  return ret;
}

int PalfHandleImpl::get_block_id_by_scn_(const SCN &scn, block_id_t &result_block_id)
{
  int ret = OB_SUCCESS;
  block_id_t mid_block_id = LOG_INVALID_BLOCK_ID, min_block_id = LOG_INVALID_BLOCK_ID;
  block_id_t max_block_id = LOG_INVALID_BLOCK_ID;
  int64_t mid_ts = OB_INVALID_TIMESTAMP;
  if (OB_FAIL(get_binary_search_range_(scn, min_block_id, max_block_id, result_block_id))) {
  } else {
    // 1. get lower bound lsn (result_lsn) by binary search
    SCN mid_scn;
    while(OB_SUCC(ret) && min_block_id <= max_block_id) {
      mid_block_id = min_block_id + ((max_block_id - min_block_id) >> 1);
      if (OB_FAIL(log_engine_.get_block_min_scn(mid_block_id, mid_scn))) {
        PALF_LOG(WARN, "get_block_min_scn failed", KR(ret), KPC(this), K(mid_block_id));
        // OB_ERR_OUT_OF_UPPER_BOUND: this block is a empty active block, just return
        // OB_ERR_OUT_OF_LOWER_BOUND: block_id_ is smaller than min_block_id, this block may be recycled
        // OB_ERR_UNEXPECTED: log files lost unexpectedly, just return
        // OB_IO_ERROR: just return
        if (OB_ERR_OUT_OF_LOWER_BOUND == ret) {
          // block mid_lsn.block_id_ may be recycled, get_binary_search_range_ again
          if (OB_FAIL(get_binary_search_range_(scn, min_block_id, max_block_id, result_block_id))) {
          }
        } else if (OB_ERR_OUT_OF_UPPER_BOUND == ret) {
          ret = OB_ENTRY_NOT_EXIST;
        }
      } else if (mid_scn < scn) {
        min_block_id = mid_block_id;
        if (max_block_id == min_block_id) {
          result_block_id = mid_block_id;
          break;
        } else if (max_block_id == min_block_id + 1) {
          SCN next_min_scn;
          if (OB_FAIL(log_engine_.get_block_min_scn(max_block_id, next_min_scn))) {
            // if fail to read next block, just return prev block lsn
            ret = OB_SUCCESS;
            result_block_id = mid_block_id;
          } else if (scn < next_min_scn) {
            result_block_id = mid_block_id;
          } else {
            result_block_id = max_block_id;
          }
          break;
        }
      } else if (mid_scn > scn) {
        // block_id is uint64_t, so check == 0 firstly.
        if (mid_block_id == 0 || mid_block_id - 1 < min_block_id) {
          ret = OB_ERR_OUT_OF_LOWER_BOUND;
          PALF_LOG(WARN, "scn is smaller than min scn of first block", KR(ret), KPC(this), K(min_block_id),
                          K(max_block_id), K(mid_block_id), K(scn), K(mid_scn));
        } else {
          max_block_id = mid_block_id - 1;
        }
      } else {
        result_block_id = mid_block_id;
        break;
      }
    }
  }
  return ret;
}

void PalfHandleImpl::set_deleted()
{
  ATOMIC_STORE(&has_set_deleted_, true);
  PALF_LOG(INFO, "set_deleted success", KPC(this));
}

int PalfHandleImpl::get_binary_search_range_(const SCN &scn,
                                             block_id_t &min_block_id,
                                             block_id_t &max_block_id,
                                             block_id_t &result_block_id)
{
  int ret = OB_SUCCESS;
  result_block_id = LOG_INVALID_BLOCK_ID;
  const LSN committed_lsn = get_end_lsn();
  if (OB_FAIL(log_engine_.get_block_id_range(min_block_id, max_block_id))) {
    // there is no log whose scn is smaller than 'scn' now and in the future,
    // return OB_ERR_OUT_OF_LOWER_BOUND.
    if (OB_ENTRY_NOT_EXIST == ret && scn <= get_end_scn()) {
      ret = OB_ERR_OUT_OF_LOWER_BOUND;
    }
  } else {
    block_id_t committed_block_id = lsn_2_block(committed_lsn, PALF_BLOCK_SIZE);
    max_block_id = (committed_block_id < max_block_id)? committed_block_id : max_block_id;
    // optimization: cache last_locate_scn_ to shrink binary search range
    SpinLockGuard guard(last_locate_lock_);
   if (is_valid_block_id(last_locate_block_) &&
        min_block_id <= last_locate_block_ &&
        max_block_id >= last_locate_block_) {
      if (scn < last_locate_scn_) {
        max_block_id = last_locate_block_;
      } else if (scn > last_locate_scn_) {
        min_block_id = last_locate_block_;
      } else {
        result_block_id = last_locate_block_;
        // result_lsn hits last_locate_block_ cache, don't need binary search
        // let min_block_id > max_block_id
        min_block_id = 1;
        max_block_id = 0;
      }
    }
    PALF_LOG(INFO, "get_binary_search_range_", K(ret), KPC(this), K(min_block_id), K(max_block_id),
        K(result_block_id), K(committed_lsn), K(scn), K_(last_locate_scn), K_(last_locate_block));
  }
  return ret;
}

void PalfHandleImpl::inc_update_last_locate_block_scn_(const block_id_t &block_id, const SCN &scn)
{
  SpinLockGuard guard(last_locate_lock_);
  if (block_id > last_locate_block_) {
    last_locate_block_ = block_id;
    last_locate_scn_ = scn;
  }
}

int PalfHandleImpl::locate_by_lsn_coarsely(const LSN &lsn, SCN &result_scn)
{
  int ret = OB_SUCCESS;
  LSN readable_begin_lsn;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "PalfHandleImpl not init", KR(ret));
  } else if (!lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", KR(ret), KPC(this), K(lsn));
  } else if (OB_FAIL(get_begin_lsn(readable_begin_lsn))) {
    PALF_LOG(WARN, "get readable begin lsn failed", KR(ret), KPC(this), K(lsn));
  } else if (lsn < readable_begin_lsn) {
    ret = OB_ERR_OUT_OF_LOWER_BOUND;
    PALF_LOG(WARN, "lsn is too small, this block has been recycled", KR(ret), KPC(this),
        K(lsn), K(readable_begin_lsn));
  } else {
    const LSN committed_lsn = get_end_lsn();
    LSN curr_lsn = (committed_lsn <= lsn) ? committed_lsn: lsn;
    block_id_t curr_block_id = lsn_2_block(curr_lsn, PALF_BLOCK_SIZE);
    if (OB_FAIL(log_engine_.get_block_min_scn(curr_block_id, result_scn))) {
      // if this block is a empty active block, read prev block if exists
      if (OB_ERR_OUT_OF_UPPER_BOUND == ret &&
          curr_block_id > 0 &&
          OB_FAIL(log_engine_.get_block_min_scn(curr_block_id - 1, result_scn))) {
        PALF_LOG(WARN, "get_block_min_scn failed", KR(ret), KPC(this), K(curr_lsn), K(lsn));
      }
    }
    PALF_LOG(INFO, "locate_by_lsn_coarsely", KR(ret), KPC(this), K(lsn), K(committed_lsn), K(result_scn));
  }
  return ret;
}

int PalfHandleImpl::get_min_block_info_for_gc(block_id_t &min_block_id, SCN &max_scn)
{
  int ret = OB_SUCCESS;
//  if (false == end_lsn.is_valid()) {
//    ret = OB_ENTRY_NOT_EXIST;
//  }
  if (OB_FAIL(log_engine_.get_min_block_info_for_gc(min_block_id, max_scn))) {
  } else {
  }
  return ret;
}

int PalfHandleImpl::get_min_block_id_for_gc(block_id_t &min_block_id)
{
  int ret = OB_SUCCESS;
  block_id_t max_block_id = LOG_INVALID_BLOCK_ID;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(log_engine_.get_block_id_range(min_block_id, max_block_id))) {
  }
  return ret;
}

int PalfHandleImpl::delete_block(const block_id_t &block_id)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(log_engine_.delete_block(block_id))) {
  } else {
    PALF_LOG(WARN, "delete block success", K(ret), KPC(this), K(block_id));
  }
  return ret;
}

int PalfHandleImpl::inner_append_log(const LSN &lsn,
                                     const LogWriteBuf &write_buf,
                                     const SCN &scn)
{
  int ret = OB_SUCCESS;
  const int64_t begin_ts = ObTimeUtility::current_time();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "PalfHandleImpl not inited", K(ret), KPC(this));
  } else if (false == lsn.is_valid()
             || false == write_buf.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument", K(ret), KPC(this), K(lsn), K(write_buf));
  } else if (OB_FAIL(log_engine_.append_log(lsn, write_buf, scn))) {
  } else {
    const int64_t curr_size = write_buf.get_total_size();
    const int64_t accum_size = ATOMIC_AAF(&accum_write_log_size_, curr_size);
    const int64_t now = ObTimeUtility::current_time();
    const int64_t time_cost = now - begin_ts;
    append_cost_stat_.stat(time_cost);
    if (time_cost >= 5 * 1000) {
      PALF_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "write log cost too much time", K(ret), KPC(this),
                   K(lsn), K(scn), "size", write_buf.get_total_size(), K(accum_size), K(time_cost));
    }
    if (palf_reach_time_interval(PALF_STAT_PRINT_INTERVAL_US, last_accum_write_statistic_time_)) {
      PALF_LOG(INFO, "[PALF STAT INNER APPEND LOG SIZE]", KPC(this), K(accum_size));
      ATOMIC_STORE(&accum_write_log_size_, 0);
    }
  }
  return ret;
}

int PalfHandleImpl::inner_append_log(const LSNArray &lsn_array,
                                     const LogWriteBufArray &write_buf_array,
                                     const SCNArray &scn_array)
{
  int ret = OB_SUCCESS;
  const int64_t begin_ts = ObTimeUtility::current_time();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "PalfHandleImpl not inited", K(ret), KPC(this));
  } else if (OB_FAIL(log_engine_.append_log(lsn_array, write_buf_array, scn_array))) {
  } else {
    int64_t accum_size = 0;
    int64_t curr_size = 0;
    int64_t lsn_array_count = lsn_array.count();
    int64_t write_buf_array_count = write_buf_array.count();
    if (0 < lsn_array_count && 0 < write_buf_array_count) {
      int64_t last_log_buf_len = write_buf_array[write_buf_array_count - 1]->get_total_size();
      curr_size = lsn_array[lsn_array_count - 1].val_ - lsn_array[0].val_ + last_log_buf_len;
    }
    accum_size = ATOMIC_AAF(&accum_write_log_size_, curr_size);
    const int64_t now = ObTimeUtility::current_time();
    const int64_t time_cost = now - begin_ts;
    append_cost_stat_.stat(time_cost);
    if (time_cost > 10 * 1000) {
      PALF_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "write log cost too much time", K(ret), KPC(this), K(lsn_array),
               K(scn_array), K(curr_size), K(accum_size), K(time_cost));
    }
    if (palf_reach_time_interval(PALF_STAT_PRINT_INTERVAL_US, last_accum_write_statistic_time_)) {
      PALF_LOG(INFO, "[PALF STAT INNER APPEND LOG SIZE]", KPC(this), K(accum_size));
      ATOMIC_STORE(&accum_write_log_size_, 0);
    }
  }
  return ret;
}

int PalfHandleImpl::inner_append_meta(const char *buf,
                                      const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "PalfHandleImpl not inited");
  } else if (NULL == buf
             || 0 >= buf_len) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument", K(ret), KPC(this), K(buf), K(buf_len));
  } else if (OB_FAIL(log_engine_.append_meta(buf, buf_len))) {
  } else {
  }
  return ret;
}

int PalfHandleImpl::inner_truncate_prefix_blocks(const LSN &lsn)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "PalfHandleImpl not inited");
  } else if (false == lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument", K(ret), KPC(this), K(lsn));
  } else if (OB_FAIL(log_engine_.truncate_prefix_blocks(lsn))) {
  } else {
    PALF_LOG(INFO, "LogEngine truncate_prefix_blocks success", K(ret), KPC(this), K(lsn));
  }
  return ret;
}

int PalfHandleImpl::set_scan_disk_log_finished()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(state_mgr_.set_scan_disk_log_finished())) {
  }
  return ret;
}

int PalfHandleImpl::get_access_mode_ref_scn(AccessMode &access_mode,
                                            SCN &ref_scn) const
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "PalfHandleImpl is not inited", K(ret), KPC(this));
  } else if (OB_FAIL(mode_mgr_.get_access_mode_ref_scn(access_mode, ref_scn))) {
  }
  return ret;
}

int PalfHandleImpl::alloc_palf_buffer_iterator(const LSN &offset,
                                               PalfBufferIterator &iterator)
{
  int ret = OB_SUCCESS;
  auto get_file_end_lsn = [this]() {
    LSN max_flushed_end_lsn;
    (void)sw_.get_max_flushed_end_lsn(max_flushed_end_lsn);
    LSN committed_end_lsn;
    sw_.get_committed_end_lsn(committed_end_lsn);
    return MIN(committed_end_lsn, max_flushed_end_lsn);
  };
  if (OB_FAIL(iterator.init(offset, get_file_end_lsn, log_engine_.get_log_storage()))) {
  } else {
  }
  return ret;
}

int PalfHandleImpl::alloc_palf_buffer_iterator(const SCN &scn,
                                               PalfBufferIterator &iterator)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "PalfHandleImpl not init", KR(ret), KPC(this));
  } else if (OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", KR(ret), K(scn));
  } else if (OB_FAIL(alloc_iterator_from_scn_(scn, iterator))) {
  } else {}
  return ret;
}

int PalfHandleImpl::alloc_palf_group_buffer_iterator(const LSN &offset,
                                                     PalfGroupBufferIterator &iterator)
{
  int ret = OB_SUCCESS;
  auto get_file_end_lsn = [&]() {
    LSN max_flushed_end_lsn;
    (void)sw_.get_max_flushed_end_lsn(max_flushed_end_lsn);
    LSN committed_end_lsn;
    sw_.get_committed_end_lsn(committed_end_lsn);
    return MIN(committed_end_lsn, max_flushed_end_lsn);
  };
  if (OB_FAIL(iterator.init(offset, get_file_end_lsn, log_engine_.get_log_storage()))) {
  } else {
  }
  return ret;
}

int PalfHandleImpl::alloc_palf_group_buffer_iterator(const SCN &scn,
                                                     PalfGroupBufferIterator &iterator)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "PalfHandleImpl not init", KR(ret), KPC(this));
  } else if (OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", KR(ret), K(scn));
  } else if (OB_FAIL(alloc_iterator_from_scn_(scn, iterator))) {
  } else {}
  return ret;
}

int PalfHandleImpl::register_file_size_cb(palf::PalfFSCbNode *fs_cb)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    if (OB_FAIL(fs_cb_wrapper_.add_cb_impl(fs_cb))) {
    } else {
      PALF_LOG(INFO, "register_file_size_cb success", KPC(this));
    }
  }
  return ret;
}

int PalfHandleImpl::unregister_file_size_cb(palf::PalfFSCbNode *fs_cb)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    fs_cb_wrapper_.del_cb_impl(fs_cb);
    PALF_LOG(INFO, "unregister_file_size_cb success", KPC(this));
  }
  return ret;
}

int PalfHandleImpl::set_monitor_cb(PalfMonitorCb *monitor_cb)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "not initted", KR(ret), KPC(this));
  } else if (OB_ISNULL(monitor_cb)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "lc_cb is NULL, can't register", KR(ret), KPC(this));
  } else if (OB_FAIL(plugins_.add_plugin(monitor_cb))) {
  } else {
    PALF_LOG(INFO, "set_monitor_cb success", KPC(this), K_(plugins), KP(monitor_cb));
  }
  return ret;
}

int PalfHandleImpl::reset_monitor_cb()
{
  int ret = OB_SUCCESS;
  PalfMonitorCb *monitor_cb = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(plugins_.del_plugin(monitor_cb))) {
  }
  return ret;
}

int PalfHandleImpl::check_and_switch_freeze_mode()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    RLockGuard guard(lock_);
    sw_.check_and_switch_freeze_mode();
  }
  return ret;
}

bool PalfHandleImpl::is_in_period_freeze_mode() const
{
  return sw_.is_in_period_freeze_mode();
}

int PalfHandleImpl::period_freeze_last_log()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    RLockGuard guard(lock_);
    sw_.period_freeze_last_log();
  }
  return ret;
}

int PalfHandleImpl::check_and_switch_state()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    bool state_changed = false;
    do {
      RLockGuard guard(lock_);
      state_changed = state_mgr_.is_state_changed();
    } while (0);
    if (state_changed) {
      WLockGuard guard(lock_);
      if (OB_FAIL(state_mgr_.switch_state())) {
      }
    }
    if (palf_reach_time_interval(PALF_DUMP_DEBUG_INFO_INTERVAL_US, last_dump_info_time_us_)) {
      RLockGuard guard(lock_);
      FLOG_INFO("[PALF_DUMP]", K_(self), "[SlidingWindow]", sw_, "[StateMgr]", state_mgr_,
          "[ModeMgr]", mode_mgr_, "[LogEngine]", log_engine_);
      (void) sw_.report_log_task_trace(sw_.get_start_id());
    }
  }
  return ret;
}





int PalfHandleImpl::do_init_mem_(
    const PalfBaseInfo &palf_base_info,
    const LogMeta &log_meta,
    const char *log_dir,
    const common::ObAddr &self,
    ObILogAllocator *alloc_mgr,
    IPalfEnvImpl *palf_env_impl)
{
  int ret = OB_SUCCESS;
  int pret = -1;
  if ((pret = snprintf(log_dir_, MAX_PATH_SIZE, "%s", log_dir)) && false) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "error unexpected", K(ret));
  } else if (OB_FAIL(sw_.init(self, &state_mgr_, &mode_mgr_,
          &log_engine_, &fs_cb_wrapper_, alloc_mgr, palf_base_info))) {
  } else if (OB_FAIL(log_cache_.init(this))) {
  } else if (OB_FAIL(state_mgr_.init(self, &sw_, &mode_mgr_))) {
  } else if (OB_FAIL(mode_mgr_.init(self, log_meta.get_log_mode_meta()))) {
  } else {
    allocator_ = alloc_mgr;
    self_ = self;
    has_set_deleted_ = false;
    palf_env_impl_ = palf_env_impl;
    is_inited_ = true;
    PALF_LOG(INFO, "PalfHandleImpl do_init_ success", K(ret), K(self), K(log_dir), K(palf_base_info),
        K(log_meta), K(alloc_mgr));
  }
  if (OB_FAIL(ret)) {
    is_inited_ = true;
    destroy();
  }
  return ret;
}

int PalfHandleImpl::get_palf_epoch(int64_t &palf_epoch) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    palf_epoch = log_engine_.get_palf_epoch();
  }
  return ret;
}

int PalfHandleImpl::get_total_used_disk_space(int64_t &total_used_disk_space, int64_t &unrecyclable_disk_space) const
{
  int ret = OB_SUCCESS;
  total_used_disk_space = 0;
  unrecyclable_disk_space = 0;
  if (OB_FAIL(log_engine_.get_total_used_disk_space(total_used_disk_space, unrecyclable_disk_space))) {
  }
  return ret;
}

int PalfHandleImpl::advance_reuse_lsn(const LSN &flush_log_end_lsn)
{
  // Do not hold lock here.
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(sw_.advance_reuse_lsn(flush_log_end_lsn))) {
  } else {
  }
  return ret;
}

int PalfHandleImpl::try_handle_next_submit_log()
{
  int ret = OB_SUCCESS;
  const int64_t begin_ts = ObTimeUtility::current_time();
  RLockGuard guard(lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(sw_.try_handle_next_submit_log())) {
  } else {
    const int64_t time_cost = ObTimeUtility::current_time() - begin_ts;
    handle_submit_log_cost_stat_.stat(time_cost);
  }
  return ret;
}

int PalfHandleImpl::inner_after_flush_log(const FlushLogCbCtx &flush_log_cb_ctx)
{
  int ret = OB_SUCCESS;
  PALF_LOG(TRACE, "after_flush_log begin", K(flush_log_cb_ctx), K_(self),
      "cost time", ObTimeUtility::current_time() - flush_log_cb_ctx.begin_ts_);
  const int64_t begin_ts = ObTimeUtility::current_time();
  RLockGuard guard(lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(sw_.after_flush_log(flush_log_cb_ctx))) {
  } else {
    const int64_t time_cost = ObTimeUtility::current_time() - begin_ts;
    flush_cb_cost_stat_.stat(time_cost);
  }
  return ret;
}

// NB: execute 'inner_after_flush_meta' is serially.
int PalfHandleImpl::inner_after_flush_meta(const FlushMetaCbCtx &flush_meta_cb_ctx)
{
  int ret = OB_SUCCESS;
  PALF_LOG(INFO, "inner_after_flush_meta", K(flush_meta_cb_ctx));
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (SNAPSHOT_META != flush_meta_cb_ctx.type_) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    RLockGuard guard(lock_);
    ret = after_flush_snapshot_meta_(flush_meta_cb_ctx.base_lsn_);
  }
  return ret;
}

int PalfHandleImpl::inner_after_truncate_prefix_blocks(const TruncatePrefixBlocksCbCtx &truncate_prefix_cb_ctx)
{
  int ret = OB_SUCCESS;
  WLockGuard guard(lock_);
  if (OB_FAIL(sw_.after_rebuild(truncate_prefix_cb_ctx.lsn_))) {
  }
  return ret;
}

int PalfHandleImpl::after_flush_snapshot_meta_(const LSN &lsn)
{
  return log_engine_.update_base_lsn_used_for_gc(lsn);
}

int PalfHandleImpl::get_prev_log_info_(const LSN &lsn,
                                       LogInfo &prev_log_info)
{
  int ret = OB_SUCCESS;
  // NB: when lsn.val_ is not 0, need iterate prev block
  block_id_t lsn_block_id = lsn_2_block(lsn, PALF_BLOCK_SIZE);
  offset_t lsn_block_offset = lsn_2_offset(lsn, PALF_BLOCK_SIZE);
  LSN start_lsn;
  if (LOG_INITIAL_BLOCK_ID == lsn_block_id) {
    start_lsn.val_ = lsn_block_id * PALF_BLOCK_SIZE;
  } else {
    start_lsn.val_ =
      (0ul == lsn_block_offset ? (lsn_block_id-1) * PALF_BLOCK_SIZE : lsn_block_id * PALF_BLOCK_SIZE);
  }
  PalfGroupBufferIterator iterator;
  const LogSnapshotMeta log_snapshot_meta = log_engine_.get_log_meta().get_log_snapshot_meta();
  LogInfo log_info;
  auto get_file_end_lsn = [&]() { return get_end_lsn(); };
  LSN log_info_tail_lsn;
  if (OB_SUCC(log_snapshot_meta.get_prev_log_info(lsn, log_info, log_info_tail_lsn))) {
    prev_log_info = log_info;
    PALF_LOG(INFO, "lsn is same as base_lsn, and log_snapshot_meta is valid", K(lsn), K(log_snapshot_meta));
  } else if (OB_FAIL(iterator.init(start_lsn, get_file_end_lsn, log_engine_.get_log_storage()))) {
  } else if (OB_FAIL(iterator.set_io_context(palf::LogIOContext(LogIOUser::FETCHLOG)))) {
  } else {
    LSN curr_lsn;
    LSN prev_lsn;
    LogGroupEntry curr_entry;
    LogGroupEntryHeader prev_entry_header;
    while (OB_SUCC(ret) && OB_SUCC(iterator.next())) {
      if (OB_FAIL(iterator.get_entry(curr_entry, curr_lsn))) {
      } else if (curr_lsn + curr_entry.get_serialize_size() > lsn) {
        ret = OB_ITER_END;
        break;
      } else {
        prev_entry_header = curr_entry.get_header();
        prev_lsn = curr_lsn;
      }
    }
    if (OB_ITER_END == ret) {
      if (false == prev_lsn.is_valid()) {
        if (curr_lsn <= lsn) {
          ret = OB_ERR_OUT_OF_LOWER_BOUND;
          PALF_LOG(WARN, "get log out of lower bound", K(ret), K(lsn), K(curr_lsn), K(curr_entry));
        } else {
          ret = OB_ENTRY_NOT_EXIST;
          PALF_LOG(WARN, "there is no log before lsn", K(ret), K(lsn), KPC(this), K(iterator));
        }
      // defense code
      } else if (prev_lsn >= lsn) {
        ret = OB_ERR_UNEXPECTED;
        PALF_LOG(WARN, "prev lsn must be smaller than lsn", K(ret), K(iterator), K(lsn), K(prev_lsn), K(prev_entry_header));
      } else {
        prev_log_info.log_id_ = prev_entry_header.get_log_id();
        prev_log_info.scn_ = prev_entry_header.get_max_scn();
        prev_log_info.accum_checksum_ = prev_entry_header.get_accum_checksum();
        prev_log_info.lsn_ = prev_lsn;
        ret = OB_SUCCESS;
      }
    }
    if (OB_SUCC(ret)) {
      PALF_LOG(INFO, "get_prev_log_info_ success", K(ret), K(lsn), K(prev_lsn), K(prev_entry_header),
          K(prev_log_info), K(iterator));
    }
  }
  return ret;
}

int PalfHandleImpl::construct_palf_base_info_(const LSN &max_committed_lsn,
                                              PalfBaseInfo &palf_base_info)
{
  int ret = OB_SUCCESS;
  LogInfo prev_log_info;
  const LSN base_lsn = log_engine_.get_log_meta().get_log_snapshot_meta().base_lsn_;
  if (false == max_committed_lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", K(ret), K(max_committed_lsn), K(base_lsn));
    // NB:
    // 1. for rebuild, there may be no valid block on disk, however, the 'prev_log_info' has been saved
    //    in LogMeta, if 'max_committed_end_lsn' is same as 'base_lsn', we can construct PalfBaseInfo
    //    as 'prev_log_info'
    // 2. for gc, there is at least two blocks on disk, if 'max_committed_end_lsn' is same as 'base_lsn',
    //    we can construct PalfBaseInfo via iterator.
  } else if (OB_FAIL(get_prev_log_info_(max_committed_lsn, prev_log_info))) {
  } else {
    palf_base_info.prev_log_info_ = prev_log_info;
    palf_base_info.curr_lsn_ = max_committed_lsn;
    PALF_LOG(INFO, "construct_palf_base_info_ success", K(ret), K(max_committed_lsn),
        K(palf_base_info), K(prev_log_info));
  }
  return ret;
}

int PalfHandleImpl::append_disk_log_to_sw_(const LSN &start_lsn)
{
  int ret = OB_SUCCESS;
  PalfGroupBufferIterator iterator;
  auto get_file_end_lsn = []() { return LSN(LOG_MAX_LSN_VAL); };
  if (false == start_lsn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(iterator.init(start_lsn, get_file_end_lsn, log_engine_.get_log_storage()))) {
  } else if (OB_FAIL(iterator.set_io_context(LogIOContext(LogIOUser::RESTART)))) {
  } else {
    LogGroupEntry group_entry;
    LSN lsn;
    while (OB_SUCC(ret) && OB_SUCC(iterator.next())) {
      if (OB_FAIL(iterator.get_entry(group_entry, lsn))) {
      } else if (OB_FAIL(sw_.append_disk_log(lsn, group_entry))) {
      }
    }
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
      PALF_LOG(INFO, "append_disk_log_to_sw_ success", K(ret), K(iterator), K(start_lsn));
    }
  }
  return ret;
}

int PalfHandleImpl::diagnose(PalfDiagnoseInfo &diagnose_info) const
{
  int ret = OB_SUCCESS;
  diagnose_info.log_state_ = state_mgr_.get_state();
  return ret;
}

int PalfHandleImpl::stat(PalfStat &palf_stat)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    // following members should be protected by rlock_
    RLockGuard guard(lock_);
    block_id_t min_block_id = LOG_INVALID_BLOCK_ID;
    SCN min_block_min_scn;
    palf_stat.self_ = self_;
    (void)mode_mgr_.get_access_mode(palf_stat.access_mode_);
    palf_stat.base_lsn_ = log_engine_.get_log_meta().get_log_snapshot_meta().base_lsn_;
    (void)log_engine_.get_min_block_info(min_block_id, min_block_min_scn);
    palf_stat.begin_lsn_ = LSN(min_block_id * PALF_BLOCK_SIZE);
    palf_stat.begin_scn_ = min_block_min_scn;
    palf_stat.end_lsn_ = get_end_lsn();
    palf_stat.end_scn_ = get_end_scn();
    palf_stat.max_lsn_ = get_max_lsn();
    palf_stat.max_scn_ = get_max_scn();
  }
  return ret;
}

PalfStat::PalfStat()
    : self_(),
      access_mode_(AccessMode::INVALID_ACCESS_MODE),
      begin_lsn_(),
      begin_scn_(),
      base_lsn_(),
      end_lsn_(),
      end_scn_(),
      max_lsn_(),
      max_scn_()
{}

bool PalfStat::is_valid() const
{
  return self_.is_valid() &&
         access_mode_ != AccessMode::INVALID_ACCESS_MODE;
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

int PalfHandleImpl::read_data_from_buffer(const LSN &read_begin_lsn,
                                          const int64_t in_read_size,
                                          char *buf,
                                          int64_t &out_read_size) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!read_begin_lsn.is_valid() || in_read_size <= 0 || OB_ISNULL(buf)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid arguments", K(ret), K(read_begin_lsn), K(in_read_size),
        KP(buf));
  } else if (OB_FAIL(sw_.read_data_from_buffer(read_begin_lsn, in_read_size, buf, out_read_size))) {
    if (OB_ERR_OUT_OF_LOWER_BOUND != ret) {
      PALF_LOG(WARN, "read_data_from_buffer failed", K(ret), K(read_begin_lsn),
          K(in_read_size));
    }
  } else {
  }
  return ret;
}

int PalfHandleImpl::raw_read(const LSN &lsn,
                             char *buffer,
                             const int64_t nbytes,
                             int64_t &read_size,
                             LogIOContext &io_ctx)
{
  int ret = OB_SUCCESS;
  const LSN readable_end_lsn = get_end_lsn();
  LSN readable_begin_lsn;
  int64_t real_read_size = 0;
  read_size = 0;
  const bool need_read_block_header = false;
  ObTimeGuard time_guard("raw_read", 100 * 1000);
  ReadBuf read_buf(buffer, nbytes);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "PalfHandleImp not inited", K(ret), K(lsn), K(nbytes), K(read_buf));
  } else if (!lsn.is_valid()
             || !is_valid_raw_read_buf(read_buf, lsn_2_offset(lsn, PALF_BLOCK_SIZE), nbytes)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid arguments", K(ret), K(lsn), K(nbytes), K(read_buf));
  } else if (OB_FAIL(get_begin_lsn(readable_begin_lsn))) {
  } else if (lsn < readable_begin_lsn) {
    ret = OB_ERR_OUT_OF_LOWER_BOUND;
    PALF_LOG(WARN, "read something out of lower bound", K(ret), K(lsn), K(nbytes),
             K(read_size), K(readable_begin_lsn));
  } else if (lsn >= readable_end_lsn) {
    ret = OB_ERR_OUT_OF_UPPER_BOUND;
    PALF_LOG(WARN, "read something out of upper bound", K(ret), K(lsn),
             K(nbytes), K(read_size), K(readable_end_lsn));
    // only read the data before readable_end_lsn
  } else if (FALSE_IT(real_read_size = MIN(nbytes, readable_end_lsn - lsn))) {
  } else if (OB_FAIL(log_engine_.raw_read(lsn, real_read_size, need_read_block_header, read_buf, read_size, io_ctx))) {
  } else {
  }
  return ret;
}


template<typename LogEntryType>
int PalfHandleImpl::alloc_iterator_from_scn_(const SCN &scn,
                                             PalfIterator<LogEntryType> &iterator)
{
  int ret = OB_SUCCESS;
  LSN start_lsn;
  const auto get_file_end_lsn = [&]() {
    LSN max_flushed_end_lsn;
    (void)sw_.get_max_flushed_end_lsn(max_flushed_end_lsn);
    LSN committed_end_lsn;
    sw_.get_committed_end_lsn(committed_end_lsn);
    return MIN(committed_end_lsn, max_flushed_end_lsn);
  };
  PalfIterator<LogEntryType> local_iter;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "PalfHandleImpl not init", KR(ret), KPC(this));
  } else if (OB_UNLIKELY(!scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", KR(ret), K(scn));
  } else if (OB_FAIL(locate_by_scn_coarsely(scn, start_lsn)) &&
             OB_ERR_OUT_OF_LOWER_BOUND != ret) {
    PALF_LOG(WARN, "locate_by_scn_coarsely failed", KR(ret), KPC(this), K(scn));
  } else if (OB_SUCCESS != ret &&
            !FALSE_IT((void)get_begin_lsn(start_lsn)) &&
            start_lsn.val_ != PALF_INITIAL_LSN_VAL) {
    PALF_LOG(WARN, "log may have been recycled", KR(ret), KPC(this), K(scn), K(start_lsn));
  } else if (OB_FAIL(local_iter.init(start_lsn, get_file_end_lsn, log_engine_.get_log_storage()))) {
  } else {
    LogEntryType curr_entry;
    LSN curr_lsn, result_lsn;
    while (OB_SUCC(ret) && OB_SUCC(local_iter.next())) {
      if (OB_FAIL(local_iter.get_entry(curr_entry, curr_lsn))) {
      } else if (curr_entry.get_scn() >= scn) {
        result_lsn = curr_lsn;
        break;
      } else {
        continue;
      }
    }
    if (OB_SUCC(ret) &&
        result_lsn.is_valid()) {
      if (iterator.is_inited()) {
        ret = iterator.reuse(result_lsn);
      } else if (OB_FAIL(iterator.init(result_lsn, get_file_end_lsn, log_engine_.get_log_storage()))) {
      } else {}
    } else {
      if (OB_ITER_END == ret) {
        ret = OB_ENTRY_NOT_EXIST;
      }
      PALF_LOG(WARN, "locate_by_scn failed", KR(ret), KPC(this), K(scn), K(result_lsn));
    }
  }
  return ret;
}

} // end namespace palf
} // end namespace oceanbase
