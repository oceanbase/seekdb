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
#include "palf_env_impl.h"
#ifdef _WIN32
#include <direct.h>
#endif
#include "palf_handle.h"
#include "share/ob_local_device.h"                            // ObLocalDevice
#include "share/io/ob_io_manager.h"                           // ObIOManager
#include "lib/ob_running_mode.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace palf
{
PalfHandleImpl *PalfHandleImplFactory::alloc()
{
  return SERVER_NEW(PalfHandleImpl, "palf_env");
}

void PalfHandleImplFactory::free(IPalfHandleImpl *palf_handle_impl)
{
  SERVER_DELETE(IPalfHandleImpl, "palf_env", palf_handle_impl);
}


int PalfDiskOptionsWrapper::init(const PalfDiskOptions &disk_opts)
{
  int ret = OB_SUCCESS;
  if (false == disk_opts.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    disk_opts_for_recycling_blocks_ = disk_opts_for_stopping_writing_ = disk_opts;
    status_ = Status::NORMAL_STATUS;
    cur_unrecyclable_log_disk_size_ = 0;
    sequence_ = 0;
  }
  return ret;
}

void PalfDiskOptionsWrapper::reset()
{
  ObSpinLockGuard guard(disk_opts_lock_);
  disk_opts_for_recycling_blocks_.reset();
  disk_opts_for_stopping_writing_.reset();
  status_ = Status::INVALID_STATUS;
  cur_unrecyclable_log_disk_size_ = -1;
  sequence_ = -1;
}

int PalfDiskOptionsWrapper::update_disk_options(const PalfDiskOptions &disk_opts_for_recycling_blocks)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(disk_opts_lock_);
  return update_disk_options_not_guarded_by_lock_(disk_opts_for_recycling_blocks);
}

void PalfDiskOptionsWrapper::set_cur_unrecyclable_log_disk_size(const int64_t unrecyclable_log_disk_size)
{
  ObSpinLockGuard guard(disk_opts_lock_);
  cur_unrecyclable_log_disk_size_ = unrecyclable_log_disk_size;
}

bool PalfDiskOptionsWrapper::need_throttling() const
{
  bool is_need = false;
  ObSpinLockGuard guard(disk_opts_lock_);
  const int64_t trigger_size = disk_opts_for_stopping_writing_.log_disk_usage_limit_size_ * disk_opts_for_stopping_writing_.log_disk_throttling_percentage_ / 100;
  return disk_opts_for_stopping_writing_.is_valid() && cur_unrecyclable_log_disk_size_ > trigger_size;
}

// Concurrent analysis
// BlockGCThread                                                                     ConfigChangeThread
// T1  get_disk_options
//                                                                                   T2  shrink log_disk when status is SHRINKING_STATUS,
//                                                                                       make disk_opts_for_recycling_blocks to new PalfDiskOptions.
// T3  change disk_opts_for_stopping_writing for disk_opts_for_recycling_blocks
//     and make status to NORMAL_STATUS
// This will cause write-stop, therefore, we only change status to NORMAL when sequence is same.
// And we only update sequence when PalfDiskOptions has change.
void PalfDiskOptionsWrapper::change_to_normal(const int64_t sequence)
{
  ObSpinLockGuard guard(disk_opts_lock_);
  if (sequence_ == sequence && Status::SHRINKING_STATUS == status_)  {
    status_ = Status::NORMAL_STATUS;
    disk_opts_for_stopping_writing_ = disk_opts_for_recycling_blocks_;
    PALF_LOG(INFO, "change_to_normal", KPC(this));
  } else {
    PALF_LOG(INFO, "sequence has changed or status not match", KPC(this), K(sequence));
  }
}

int PalfDiskOptionsWrapper::update_disk_options_not_guarded_by_lock_(const PalfDiskOptions &disk_opts_for_recycling_blocks)
{
  int ret = OB_SUCCESS;
  int64_t curr_stop_write_limit_size =
    disk_opts_for_stopping_writing_.log_disk_usage_limit_size_;
  int64_t next_stop_write_limit_size =
    disk_opts_for_recycling_blocks.log_disk_usage_limit_size_;
  if (false == disk_opts_for_recycling_blocks.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (disk_opts_for_recycling_blocks_ == disk_opts_for_recycling_blocks) {
    PALF_LOG(INFO, "no need update disk options", K(ret), K(disk_opts_for_recycling_blocks_), K(disk_opts_for_recycling_blocks));
  } else {
    if (curr_stop_write_limit_size > next_stop_write_limit_size) {
      status_ = Status::SHRINKING_STATUS;
      // In process of shrinking, to avoid stopping writing,
      // 'disk_opts_for_stopping_writing_' is still an original value, update it
      // with 'disk_opts_for_recycling_blocks' until there is no possibility
      // caused stopping writing.
      disk_opts_for_recycling_blocks_ = disk_opts_for_recycling_blocks;
      PALF_LOG(INFO, "shrink log disk success", K(curr_stop_write_limit_size), K(next_stop_write_limit_size),
               KPC(this));
    } else {
      status_ = Status::NORMAL_STATUS;
      disk_opts_for_recycling_blocks_ = disk_opts_for_stopping_writing_ = disk_opts_for_recycling_blocks;
      PALF_LOG(INFO, "expand log disk success", K(curr_stop_write_limit_size), K(next_stop_write_limit_size),
               KPC(this));
    }
    //always update writing_throttling_trigger_percentage_
    const int64_t new_trigger_percentage = disk_opts_for_recycling_blocks.log_disk_throttling_percentage_;
    const int64_t new_maximum_duration = disk_opts_for_recycling_blocks.log_disk_throttling_maximum_duration_;
    disk_opts_for_recycling_blocks_.log_disk_throttling_percentage_ = new_trigger_percentage;
    disk_opts_for_stopping_writing_.log_disk_throttling_percentage_ = new_trigger_percentage;
    disk_opts_for_recycling_blocks_.log_disk_throttling_maximum_duration_ = new_maximum_duration;
    disk_opts_for_stopping_writing_.log_disk_throttling_maximum_duration_ = new_maximum_duration;
    sequence_++;
  }
  return ret;
}

PalfEnvImpl::PalfEnvImpl() : palf_meta_lock_(common::ObLatchIds::PALF_ENV_LOCK),
                             log_alloc_mgr_(NULL),
                             log_block_pool_(NULL),
                             cb_thread_pool_(),
                             log_io_worker_wrapper_(),
                             log_shared_queue_th_(),
                             block_gc_timer_task_(),
                             monitor_(NULL),
                             disk_options_wrapper_(),
                             disk_not_enough_print_interval_in_gc_thread_(OB_INVALID_TIMESTAMP),
                             disk_not_enough_print_interval_in_loop_thread_(OB_INVALID_TIMESTAMP),
                             self_(),
                             palf_handle_(nullptr),
                             last_palf_epoch_(0),
                             enable_log_cache_(false),
                             diskspace_enough_(true),
                             io_adapter_(),
                             is_inited_(false),
                             is_running_(false)
{
  log_dir_[0] = '\0';
  tmp_log_dir_[0] = '\0';
}

PalfEnvImpl::~PalfEnvImpl()
{
  destroy();
}

int PalfEnvImpl::init(
    const PalfOptions &options,
    const char *base_dir, const ObAddr &self,
    common::ObILogAllocator *log_alloc_mgr,
    ILogBlockPool *log_block_pool,
    PalfMonitorCb *monitor,
    common::ObIODevice *log_local_device,
    ObIOManager *io_manager)
{
  int ret = OB_SUCCESS;
  int pret = 0;
  const int64_t io_cb_num = PALF_SLIDING_WINDOW_SIZE * 128;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    PALF_LOG(ERROR, "PalfEnvImpl is inited twiced", K(ret));
  } else if (OB_ISNULL(base_dir) || !self.is_valid()
             || OB_ISNULL(log_alloc_mgr) || OB_ISNULL(log_block_pool) || OB_ISNULL(monitor) 
             || OB_ISNULL(log_local_device) || OB_ISNULL(io_manager)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "invalid arguments", K(ret), K(base_dir), K(self),
             KP(log_alloc_mgr), KP(log_block_pool), KP(monitor), KP(log_local_device), KP(io_manager));
  } else if (OB_FAIL(init_log_io_worker_config_(log_io_worker_config_))) {
  } else if (OB_FAIL(cb_thread_pool_.init(io_cb_num, this))) {
  } else if (OB_FAIL(log_io_worker_wrapper_.init(log_io_worker_config_,
                                                 &cb_thread_pool_,
                                                 log_alloc_mgr, this))) {
  } else if (OB_FAIL(log_shared_queue_th_.init(this))) {
  } else if (OB_FAIL(block_gc_timer_task_.init(this))) {
  } else if ((pret = snprintf(log_dir_, MAX_PATH_SIZE, "%s", base_dir)) && false) {
    ret = OB_ERR_UNEXPECTED;
  } else if ((pret = snprintf(tmp_log_dir_, MAX_PATH_SIZE, "%s/tmp_dir", log_dir_)) && false) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "error unexpected", K(ret));
  } else if (pret < 0 || pret >= MAX_PATH_SIZE) {
    ret = OB_BUF_NOT_ENOUGH;
    PALF_LOG(ERROR, "construct log path failed", K(ret), K(pret));
  } else if (OB_FAIL(log_loop_thread_.init(this))) {
  } else if (OB_FAIL(disk_options_wrapper_.init(options.disk_options_))) {
  } else if (OB_FAIL(io_adapter_.init(log_local_device, io_manager))) {
  } else {
    log_alloc_mgr_ = log_alloc_mgr;
    log_block_pool_ = log_block_pool;
    monitor_ = monitor;
    self_ = self;
    
    is_inited_ = true;
    is_running_ = true;
    enable_log_cache_ = options.enable_log_cache_;
    PALF_LOG(INFO, "PalfEnvImpl init success", K(ret), K(self_), KPC(this));
  }
  if (OB_FAIL(ret) && OB_INIT_TWICE != ret) {
    destroy();
  }
  return ret;
}

int PalfEnvImpl::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(reload_palf_handle_impl_())) {
  } else if (OB_FAIL(cb_thread_pool_.start())) {
  } else if (OB_FAIL(log_io_worker_wrapper_.start())) {
  } else if (OB_FAIL(log_shared_queue_th_.start())) {
  } else if (OB_FAIL(block_gc_timer_task_.start())) {
  } else if (OB_FAIL(log_loop_thread_.start())) {
  } else {
    is_running_ = true;
    PALF_LOG(INFO, "PalfEnv start success", K(ret));
  }
  return ret;
}

void PalfEnvImpl::stop()
{
  if (is_running_) {
    PALF_LOG(INFO, "PalfEnvImpl begin stop", KPC(this));
    is_running_ = false;
    log_io_worker_wrapper_.stop();
    log_shared_queue_th_.stop();
    cb_thread_pool_.stop();
    block_gc_timer_task_.stop();
    log_loop_thread_.stop();
    PALF_LOG(INFO, "PalfEnvImpl stop success", KPC(this));
  }
}

void PalfEnvImpl::wait()
{
  PALF_LOG(INFO, "PalfEnvImpl begin wait", KPC(this));
  log_io_worker_wrapper_.wait();
  log_shared_queue_th_.wait();
  cb_thread_pool_.wait();
  block_gc_timer_task_.wait();
  log_loop_thread_.wait();
  PALF_LOG(INFO, "PalfEnvImpl wait success", KPC(this));
}

void PalfEnvImpl::destroy()
{
  PALF_LOG_RET(WARN, OB_SUCCESS, "PalfEnvImpl destroy", KPC(this));
  is_running_ = false;
  is_inited_ = false;
  log_io_worker_wrapper_.destroy();
  log_shared_queue_th_.destroy();
  cb_thread_pool_.destroy();
  log_loop_thread_.destroy();
  block_gc_timer_task_.destroy();
  log_alloc_mgr_ = NULL;
  monitor_ = NULL;
  disk_not_enough_print_interval_in_gc_thread_ = OB_INVALID_TIMESTAMP;
  disk_not_enough_print_interval_in_loop_thread_ = OB_INVALID_TIMESTAMP;
  self_.reset();
  log_dir_[0] = '\0';
  tmp_log_dir_[0] = '\0';
  disk_options_wrapper_.reset();
  enable_log_cache_ = false;
  io_adapter_.destroy();
}

// NB: not thread safe
int PalfEnvImpl::create_palf_handle_impl(const AccessMode &access_mode,
                                         const PalfBaseInfo &palf_base_info,
                                         IPalfHandleImpl *&palf_handle_impl)
{
  int ret = OB_SUCCESS;
  WLockGuard guard(palf_meta_lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "PalfEnvImpl is not inited", K(ret));
  } else if (OB_FAIL(create_palf_handle_impl_(access_mode, palf_base_info, palf_handle_impl))) {
    palf_handle_impl = NULL;
  } else {
    PALF_LOG(INFO, "PalfEnvImpl create_palf_handle_impl finished", K(ret), K(access_mode),
        K(palf_base_info), KPC(this));
  }
  return ret;
}

int PalfEnvImpl::create_palf_handle_impl_(const AccessMode &access_mode,
                                          const PalfBaseInfo &palf_base_info,
                                          IPalfHandleImpl *&ipalf_handle_impl)
{
  int ret = OB_SUCCESS;
  int pret = 0;
  char base_dir[MAX_PATH_SIZE] = {'\0'};
  PalfHandleImpl *palf_handle_impl = NULL;
  const int64_t palf_epoch = ATOMIC_AAF(&last_palf_epoch_, 1);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "PalfEnvImpl is not running", K(ret));
  } else if (!is_running_) {
    ret = OB_NOT_RUNNING;
    PALF_LOG(WARN, "PalfEnvImpl is not running", K(ret));
  } else if (NULL != palf_handle_) {
    ret = OB_ENTRY_EXIST;
    PALF_LOG(WARN, "palf_handle has exist, ignore this request", K(ret));
  } else if (false == has_minimum_log_disk_capacity_()) {
    ret = OB_LOG_OUTOF_DISK_SPACE;
    PALF_LOG(WARN, "PalfEnv can not hold the log stream", K(ret), KPC(this));
  } else if (0 > (pret = snprintf(base_dir, MAX_PATH_SIZE, "%s/log_stream", log_dir_))) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "snprintf failed", K(pret));
  // Note:: order is vital, allocate memory may be fail
  } else if (NULL == (palf_handle_impl = PalfHandleImplFactory::alloc())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    PALF_LOG(WARN, "alloc palf_handle_impl failed", K(ret));
  } else if (OB_FAIL(create_directory(base_dir))) {
  } else if (OB_FAIL(palf_handle_impl->init(access_mode, palf_base_info,
      base_dir, log_alloc_mgr_, log_block_pool_,
      log_io_worker_wrapper_.get_log_io_worker(), &log_shared_queue_th_, this,
      self_, palf_epoch, &io_adapter_))) {
  } else {
    palf_handle_ = palf_handle_impl;
    (void) palf_handle_impl->set_monitor_cb(monitor_);
    palf_handle_impl->set_scan_disk_log_finished();
    ipalf_handle_impl = palf_handle_impl;
  }

  if (OB_FAIL(ret) && NULL != palf_handle_impl) {
    PalfHandleImplFactory::free(palf_handle_impl);
    palf_handle_impl = NULL;
    if (NULL == palf_handle_) {
      remove_directory_while_exist_(base_dir);
    }
  }

  PALF_LOG(INFO, "PalfEnvImpl create_palf_handle_impl_ finished", K(ret),
      K(access_mode), K(palf_base_info), KPC(this));

  return ret;
}

int PalfEnvImpl::remove_palf_handle_impl()
{
  int ret = OB_SUCCESS;
  WLockGuard guard(palf_meta_lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "PalfEnvImpl is not inited", K(ret));
  } else if (OB_FAIL(remove_palf_handle_impl_())) {
  } else {
    // Handle error and shutdown paths where the stream was marked for removal.
  }
  return ret;
}

int PalfEnvImpl::get_palf_handle_impl(IPalfHandleImplGuard &palf_handle_impl_guard)
{
  int ret = OB_SUCCESS;
  IPalfHandleImpl *palf_handle_impl = NULL;
  if (OB_FAIL(get_palf_handle_impl(palf_handle_impl))) {
  } else {
    palf_handle_impl_guard.palf_env_impl_ = this;
    palf_handle_impl_guard.palf_handle_impl_ = palf_handle_impl;
    // do nothing
  }
  return ret;
}

int PalfEnvImpl::get_palf_handle_impl(IPalfHandleImpl *&ipalf_handle_impl)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(palf_handle_)
             || false == palf_handle_->check_can_be_used()) {
    ret = OB_ENTRY_NOT_EXIST;
  } else {
    ipalf_handle_impl = palf_handle_;
  }
  return ret;
}

void PalfEnvImpl::revert_palf_handle_impl(IPalfHandleImpl *ipalf_handle_impl)
{
  // The handle remains owned by the environment during operation.
  UNUSED(ipalf_handle_impl);
}

int PalfEnvImpl::create_directory(const char *base_dir)
{
  int ret = OB_SUCCESS;
  int pret = 0;
  const mode_t mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
  char tmp_base_dir[MAX_PATH_SIZE] = {'\0'};
  char log_dir[MAX_PATH_SIZE] = {'\0'};
  char meta_dir[MAX_PATH_SIZE] = {'\0'};
  if (0 > (pret = snprintf(tmp_base_dir, MAX_PATH_SIZE, "%s%s", base_dir, TMP_SUFFIX))) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "snprinf failed", K(pret), K(base_dir));
  } else if (0 > (pret = snprintf(log_dir, MAX_PATH_SIZE, "%s/log", tmp_base_dir))) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "snprinf failed", K(pret), K(base_dir));
  } else if (0 > (pret = snprintf(meta_dir, MAX_PATH_SIZE, "%s/meta", tmp_base_dir))) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "snprinf failed", K(pret), K(base_dir));
#ifdef _WIN32
  } else if (-1 == (::_mkdir(tmp_base_dir))) {
#else
  } else if (-1 == (::mkdir(tmp_base_dir, mode))) {
#endif
    ret = convert_sys_errno();
    PALF_LOG(WARN, "mkdir failed", K(ret), K(errno), K(tmp_base_dir), K(base_dir));
#ifdef _WIN32
  } else if (-1 == (::_mkdir(log_dir))) {
#else
  } else if (-1 == (::mkdir(log_dir, mode))) {
#endif
    ret = convert_sys_errno();
    PALF_LOG(WARN, "mkdir failed", K(ret), K(errno), K(tmp_base_dir), K(base_dir));
#ifdef _WIN32
  } else if (-1 == (::_mkdir(meta_dir))) {
#else
  } else if (-1 == (::mkdir(meta_dir, mode))) {
#endif
    ret = convert_sys_errno();
    PALF_LOG(WARN, "mkdir failed", K(ret), K(errno), K(tmp_base_dir), K(base_dir));
  } else if (OB_FAIL(rename_with_retry(tmp_base_dir, base_dir))) {
  } else if (OB_FAIL(FileDirectoryUtils::fsync_dir(log_dir_))) {
  } else {
    PALF_LOG(INFO, "prepare_directory_for_creating_ls success", K(ret), K(base_dir));
  }
  if (OB_FAIL(ret)) {
    remove_directory_while_exist_(tmp_base_dir);
    remove_directory_while_exist_(base_dir);
  }
  return ret;
}

// step:
// 1. rename log directory to tmp directory.
// 2. delete tmp directory.
// NB: '%s.tmp' is invalid block or invalid directory, before the restart phase of PalfEnvImpl,
//     need delete these tmp block or directory.
int PalfEnvImpl::remove_directory(const char *log_dir)
{
  int ret = OB_SUCCESS;
  int pret = 0;
  char tmp_log_dir[MAX_PATH_SIZE] = {'\0'};
  if (0 > (pret = snprintf(tmp_log_dir, MAX_PATH_SIZE, "%s%s", log_dir, TMP_SUFFIX))) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "snprintf failed", K(ret), K(pret), K(log_dir), K(tmp_log_dir));
  } else if (OB_FAIL(rename_with_retry(log_dir, tmp_log_dir))) {
  } else {
    bool result = true;
    do {
      if (OB_FAIL(FileDirectoryUtils::is_exists(tmp_log_dir, result))) {
      } else if (!result) {
        PALF_LOG(WARN, "directory not exists", KPC(this), K(log_dir));
        break;
      } else if (OB_FAIL(remove_directory_rec(tmp_log_dir, log_block_pool_))) {
      } else {
      }
      if (OB_FAIL(ret) && true == result) {
        PALF_LOG(WARN, "remove directory failed, may be physical disk full", K(ret), KPC(this));
        ob_usleep(100*1000);
      }
    } while (OB_FAIL(ret));
  }
  (void)FileDirectoryUtils::fsync_dir(log_dir_);
  PALF_LOG(WARN, "remove_directory finished", KR(ret), K(log_dir), KP(this));
  return ret;
}

int PalfEnvImpl::try_recycle_blocks()
{
  int ret = OB_SUCCESS;
  PalfDiskOptions disk_opts_for_stopping_writing;
  PalfDiskOptions disk_opts_for_recycling_blocks;
  PalfDiskOptionsWrapper::Status status = PalfDiskOptionsWrapper::Status::INVALID_STATUS;
  int64_t sequence = -1;
  disk_options_wrapper_.get_disk_opts(disk_opts_for_stopping_writing,
                                      disk_opts_for_recycling_blocks,
                                      status,
                                      sequence);
  int64_t total_used_size_byte = 0;
  int64_t total_unrecyclable_size_byte = 0;
  int64_t total_size_to_recycle_blocks = disk_opts_for_recycling_blocks.log_disk_usage_limit_size_;
  int64_t total_size_to_stop_write = disk_opts_for_stopping_writing.log_disk_usage_limit_size_;
  int64_t utl_threshold_to_recycle_blocks = disk_opts_for_recycling_blocks.log_disk_utilization_threshold_;
  int64_t utl_threshold_to_stop_write = disk_opts_for_stopping_writing.log_disk_utilization_threshold_;
  utl_threshold_to_recycle_blocks = 0 == utl_threshold_to_recycle_blocks ? DEFAULT_LOG_UTL_THRESHOLD : utl_threshold_to_recycle_blocks;
  utl_threshold_to_stop_write = 0 == utl_threshold_to_stop_write ? DEFAULT_LOG_UTL_THRESHOLD : utl_threshold_to_stop_write;
  int tmp_ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(get_disk_usage_(total_used_size_byte, total_unrecyclable_size_byte))) {
  } else if (FALSE_IT(disk_options_wrapper_.set_cur_unrecyclable_log_disk_size(total_unrecyclable_size_byte))) {
  } else if (OB_SUCCESS != (tmp_ret = log_io_worker_wrapper_.notify_need_writing_throttling(disk_options_wrapper_.need_throttling()))) {
    PALF_LOG_RET(WARN, tmp_ret, "failed to update_disk_info", K(disk_options_wrapper_));
  } else {
    const int64_t usable_disk_size_to_recycle_blocks =
        total_size_to_recycle_blocks
        * utl_threshold_to_recycle_blocks / 100LL;
    const int64_t usable_disk_limit_size_to_stop_writing =
        total_size_to_stop_write
        * disk_opts_for_stopping_writing.log_disk_utilization_limit_threshold_ / 100LL;
    const bool need_recycle = (disk_opts_for_recycling_blocks.log_disk_utilization_threshold_ == 0 ||
        usable_disk_size_to_recycle_blocks < total_used_size_byte) ? true : false;
    const bool is_shrinking = disk_options_wrapper_.is_shrinking();
    constexpr int64_t MB = 1024 * 1024LL;
    const int64_t print_error_log_disk_size =
        disk_opts_for_stopping_writing.log_disk_usage_limit_size_
        * utl_threshold_to_stop_write / 100LL;
    const bool need_print_error_log =
        print_error_log_disk_size >= total_used_size_byte ? false : true;

    // step1. change SHRINKING_STATUS to normal
    // 1. when there is no possibility to stop writing,
    // 2. the snapshot of status is SHRINKING_STATUS.
    bool has_recycled = false;
    const bool in_shrinking = (PalfDiskOptionsWrapper::Status::SHRINKING_STATUS == status);
    if (OB_SUCC(ret) && in_shrinking) {
      if (total_used_size_byte <= usable_disk_size_to_recycle_blocks) {
        disk_options_wrapper_.change_to_normal(sequence);
        PALF_LOG(INFO, "change_to_normal success", K(disk_options_wrapper_),
                 K(total_used_size_byte), K(usable_disk_size_to_recycle_blocks));
      }
    }

    // step2. try recycle blocks
    if (true == need_recycle) {
      if (OB_FAIL(recycle_blocks_(has_recycled))) {
      }
    }

    // step3. try print error log
    // NB: print error log when:
    // 1. write-stop.(i.e. set 'diskspace_enough_' to true when the disk usage execeed than the 'log_disk_throttling_percentage_' in disk_opts_for_stopping_writing);
    // 2. the used log disk space exceeded the log disk recycle threshold and there is no recycable block(in shrinking log disk status, disk_opts_for_stopping_writing is not
    //    same with disk_opts_for_recycling_blocks).
    if (!check_disk_space_enough() || (true == need_print_error_log && false == has_recycled)) {
      constexpr int64_t INTERVAL = 1*1000*1000;
      if (palf_reach_time_interval(INTERVAL, disk_not_enough_print_interval_in_gc_thread_)) {
        int tmp_ret = OB_LOG_OUTOF_DISK_SPACE;
        const int64_t log_disk_warn_percent = utl_threshold_to_stop_write;
        const int64_t log_disk_usage_limit_size = disk_opts_for_stopping_writing.log_disk_usage_limit_size_;
        const int64_t log_disk_limit_percent = disk_opts_for_stopping_writing.log_disk_utilization_limit_threshold_;
        LOG_DBA_ERROR(OB_LOG_OUTOF_DISK_SPACE, "msg", "log disk space is almost full", "ret", tmp_ret,
            "total_size(MB)", log_disk_usage_limit_size/MB,
            "used_size(MB)", total_used_size_byte/MB,
            "used_percent(%)", (total_used_size_byte*100) / (log_disk_usage_limit_size+1),
            "warn_size(MB)", (log_disk_usage_limit_size*log_disk_warn_percent)/100/MB,
            "warn_percent(%)", log_disk_warn_percent,
            "limit_size(MB)", (log_disk_usage_limit_size*log_disk_limit_percent)/100/MB,
            "limit_percent(%)", log_disk_limit_percent,
            "total_unrecyclable_size_byte(MB)", total_unrecyclable_size_byte/MB,
            "in_shrinking", in_shrinking);
        LOG_DBA_ERROR_(OB_LOG_DISK_SPACE_ALMOST_FULL, tmp_ret, "log disk space is almost full",
            ", total_size(MB)=", log_disk_usage_limit_size/MB,
            ", used_size(MB)=", total_used_size_byte/MB,
            ", used_percent(%)=", (total_used_size_byte*100) / (log_disk_usage_limit_size+1),
            ", warn_size(MB)=", (log_disk_usage_limit_size*log_disk_warn_percent)/100/MB,
            ", warn_percent(%)=", log_disk_warn_percent,
            ", limit_size(MB)=", (log_disk_usage_limit_size*log_disk_limit_percent)/100/MB,
            ", limit_percent(%)=", log_disk_limit_percent,
            ", total_unrecyclable_size_byte(MB)=", total_unrecyclable_size_byte/MB,
            ", in_shrinking=", in_shrinking);
      }
    } else {
       if (REACH_TIME_INTERVAL(2 * 1000 * 1000L)) {
         PALF_LOG(INFO, "LOG_DISK_OPTION", K(disk_options_wrapper_));
       }
    }

    (void)remove_stale_incomplete_palf_();
  }
  return ret;
}

bool PalfEnvImpl::check_disk_space_enough()
{
  return true == ATOMIC_LOAD(&diskspace_enough_);
}

PalfEnvImpl::RemoveStaleIncompletePalfFunctor::RemoveStaleIncompletePalfFunctor(PalfEnvImpl *palf_env_impl)
  : palf_env_impl_(palf_env_impl)
{}

 PalfEnvImpl::RemoveStaleIncompletePalfFunctor::~RemoveStaleIncompletePalfFunctor()
{
  palf_env_impl_ = NULL;
}

int PalfEnvImpl::RemoveStaleIncompletePalfFunctor::func(const dirent *entry)
{
  int ret = OB_SUCCESS;
  char *saveptr = NULL;
  char file_name[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  const char *d_name = entry->d_name;
  MEMCPY(file_name, d_name, strlen(d_name));
  char *tmp = strtok_r(file_name, "_", &saveptr);
  char *timestamp_str = NULL;
  if (NULL == tmp || NULL == (timestamp_str = strtok_r(NULL, "_", &saveptr))) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(WARN, "unexpected format", K(ret), K(tmp), K(file_name));
  } else {
    int64_t timestamp = atol(timestamp_str);
    int64_t current_timestamp = ObTimeUtility::current_time();
    int64_t delta = current_timestamp - timestamp;
    constexpr int64_t week_us = 7 * 24 * 60 * 60 * 1000 * 1000ll;
    if (delta <= week_us) {
    } else {
      char path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
      int pret = OB_SUCCESS;
      if (0 > (pret = snprintf(path, MAX_PATH_SIZE, "%s/%s", palf_env_impl_->tmp_log_dir_, d_name))) {
        ret = OB_ERR_UNEXPECTED;
        PALF_LOG(WARN, "snprintf failed", K(ret), K(file_name), K(d_name));
      } else if (OB_FAIL(FileDirectoryUtils::delete_directory_rec(path))) {
      } else {
        PALF_LOG(WARN, "current incomplete palf has bee staled, delete it", K(timestamp), K(current_timestamp), K(path));
      }
    }
  }
  return ret;
}

int PalfEnvImpl::get_disk_usage(int64_t &used_size_byte, int64_t &total_usable_size_byte)
{
  int ret = OB_SUCCESS;
  constexpr int64_t MB = 1024 * 1024;
  PalfDiskOptions disk_options = disk_options_wrapper_.get_disk_opts_for_recycling_blocks();
  if (OB_FAIL(get_disk_usage_(used_size_byte))) {
  } else {
    total_usable_size_byte = disk_options.log_disk_usage_limit_size_;
    PALF_LOG(INFO, "get_disk_usage", K(ret), "capacity(MB):", total_usable_size_byte/MB, "used(MB):", used_size_byte/MB);
  }
  return ret;
}

int PalfEnvImpl::get_stable_disk_usage(int64_t &used_size_byte, int64_t &total_usable_size_byte)
{
  int ret = OB_SUCCESS;
  constexpr int64_t MB = 1024 * 1024;
  PalfDiskOptions disk_options = disk_options_wrapper_.get_disk_opts_for_stopping_writing();
  if (OB_FAIL(get_disk_usage_(used_size_byte))) {
  } else {
    total_usable_size_byte = disk_options.log_disk_usage_limit_size_;
    PALF_LOG(INFO, "get_stable_disk_usage", K(ret), "capacity(MB):", total_usable_size_byte/MB, "used(MB):", used_size_byte/MB);
  }
  return ret;
}

int PalfEnvImpl::update_options(const PalfOptions &options)
{
  int ret = OB_SUCCESS;
  WLockGuard guard(palf_meta_lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (false == options.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", K(options));
  } else if (OB_FAIL(check_can_update_log_disk_options_(options.disk_options_))) {
  } else if (OB_FAIL(disk_options_wrapper_.update_disk_options(options.disk_options_))) {
  } else {
    enable_log_cache_ = options.enable_log_cache_;
    PALF_LOG(INFO, "update_options successs", K(options), KPC(this)); 
  }
  return ret;
}

int PalfEnvImpl::get_options(PalfOptions &options)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    options.disk_options_ = disk_options_wrapper_.get_disk_opts_for_recycling_blocks();
    options.enable_log_cache_ = enable_log_cache_;
  }
  return ret;
}

common::ObILogAllocator* PalfEnvImpl::get_log_allocator()
{
  return log_alloc_mgr_;
}

int PalfEnvImpl::reload_palf_handle_impl_()
{
  int ret = OB_SUCCESS;
  int pret = 0;
  PalfHandleImpl *tmp_palf_handle_impl = nullptr;
  char base_dir[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  int64_t start_ts = ObTimeUtility::current_time();
  bool is_integrity = true;
  bool dir_exist = false;
  const int64_t palf_epoch = ATOMIC_AAF(&last_palf_epoch_, 1);
  if (0 > (pret = snprintf(base_dir, MAX_PATH_SIZE, "%s/log_stream", log_dir_))) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(WARN, "snprint failed", K(ret), K(pret));
  } else if (OB_FAIL(FileDirectoryUtils::is_exists(base_dir, dir_exist))) {
  } else if (!dir_exist) {
    PALF_LOG(INFO, "palf directory does not exist", K(base_dir));
  } else if (NULL == (tmp_palf_handle_impl = PalfHandleImplFactory::alloc())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    PALF_LOG(WARN, "alloc ipalf_handle_impl failed", K(ret));
  } else if (OB_FAIL(tmp_palf_handle_impl->load(base_dir, log_alloc_mgr_,
          log_block_pool_, log_io_worker_wrapper_.get_log_io_worker(), &log_shared_queue_th_,
          this, self_, palf_epoch, &io_adapter_, is_integrity))) {
  } else {
    palf_handle_ = tmp_palf_handle_impl;
    (void) tmp_palf_handle_impl->set_monitor_cb(monitor_);
    (void) tmp_palf_handle_impl->set_scan_disk_log_finished();
    int64_t cost_ts = ObTimeUtility::current_time() - start_ts;
    PALF_LOG(INFO, "reload_palf_handle_impl success", K(ret), K(cost_ts), KP(this));
  }

  if (OB_FAIL(ret) && NULL != tmp_palf_handle_impl) {
    PALF_LOG(ERROR, "reload_palf_handle_impl_ failed, need free tmp_palf_handle_impl", K(ret), K(tmp_palf_handle_impl));
    if (NULL == palf_handle_) {
      PalfHandleImplFactory::free(tmp_palf_handle_impl);
      tmp_palf_handle_impl = NULL;
    }
  } else if (false == is_integrity) {
    PALF_LOG(WARN, "log stream is incomplete");
    ret = move_incomplete_palf_into_tmp_dir_();
  }
  return ret;
}

int PalfEnvImpl::get_total_used_disk_space_(int64_t &total_used_disk_space,
                                            int64_t &total_unrecyclable_disk_space)
{
  int ret = OB_SUCCESS;
  if (NULL == palf_handle_) {
    // GC timer starts before LS is created, handle not ready yet
    total_used_disk_space = 0;
    total_unrecyclable_disk_space = 0;
  } else if (OB_FAIL(palf_handle_->get_total_used_disk_space(
                 total_used_disk_space, total_unrecyclable_disk_space))) {
  }
  return ret;
}

int PalfEnvImpl::get_disk_usage_(int64_t &used_size_byte,
                                 int64_t &unrecyclable_disk_space)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_total_used_disk_space_(used_size_byte, unrecyclable_disk_space))) {
  }
  return ret;
}

int PalfEnvImpl::get_disk_usage_(int64_t &used_size_byte)
{
  int ret = OB_SUCCESS;
  int64_t unused_unrecyclable_size = 0;
  if (OB_FAIL(get_disk_usage_(used_size_byte, unused_unrecyclable_size))) {
  }
  return ret;
}

int PalfEnvImpl::recycle_blocks_(bool &has_recycled)
{
  int ret = OB_SUCCESS;
  has_recycled = false;
  if (NULL == palf_handle_) {
    // GC timer starts before LS is created, handle not ready yet
  } else {
    const LSN base_lsn = palf_handle_->get_base_lsn_used_for_block_gc();
    const block_id_t min_using_block_id = lsn_2_block(base_lsn, PALF_BLOCK_SIZE);
    block_id_t min_block_id = LOG_INVALID_BLOCK_ID;
    auto need_skip_by_ret = [](const int ret) {
      return OB_ENTRY_NOT_EXIST == ret || OB_NO_SUCH_FILE_OR_DIRECTORY == ret
          || OB_ERR_OUT_OF_UPPER_BOUND == ret;
    };
    if (false == base_lsn.is_valid()) {
      PALF_LOG(WARN, "base_lsn is invalid", K(base_lsn));
    } else if (OB_FAIL(palf_handle_->get_min_block_id_for_gc(min_block_id))
               && !need_skip_by_ret(ret)) {
      PALF_LOG(WARN, "get_min_block_id_for_gc failed", K(ret));
    } else if (need_skip_by_ret(ret)
               || min_using_block_id < min_block_id
               || min_using_block_id - min_block_id < 2) {
    } else if (OB_FAIL(palf_handle_->delete_block(min_block_id))) {
    } else {
      has_recycled = true;
      PALF_LOG(INFO, "recycle_blocks success", K(min_block_id), K(min_using_block_id));
    }
  }
  return ret;
}

bool PalfEnvImpl::has_minimum_log_disk_capacity_() const
{
  const PalfDiskOptions disk_opts = disk_options_wrapper_.get_disk_opts_for_recycling_blocks();
  return MIN_DISK_SIZE_PER_PALF_INSTANCE <= disk_opts.log_disk_usage_limit_size_;
}

int PalfEnvImpl::remove_palf_handle_impl_()
{
  int ret = OB_SUCCESS;
  if (NULL == palf_handle_) {
    ret = OB_ENTRY_NOT_EXIST;
  } else {
    palf_handle_->set_deleted();
    PalfHandleImplFactory::free(palf_handle_);
    palf_handle_ = nullptr;
    PALF_LOG(INFO, "remove_palf_handle_impl success", K(ret));
  }
  return ret;
}

int PalfEnvImpl::move_incomplete_palf_into_tmp_dir_()
{
  int ret = OB_SUCCESS;
  int pret = 0;
  const mode_t mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
  char src_log_dir[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  char dest_log_dir[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  bool tmp_dir_exist = false;
  int64_t timestamp = ObTimeUtility::current_time();
  IPalfHandleImpl *old_handle = palf_handle_;
  palf_handle_ = nullptr;
  if (NULL != old_handle) {
    PalfHandleImplFactory::free(old_handle);
  }
  if (OB_FAIL(check_tmp_log_dir_exist_(tmp_dir_exist))) {
    PALF_LOG(WARN, "check_tmp_log_dir_exist_ failed", K(ret), KPC(this), K(tmp_log_dir_));
#ifdef _WIN32
  } else if (false == tmp_dir_exist && (-1 == ::_mkdir(tmp_log_dir_))) {
#else
  } else if (false == tmp_dir_exist && (-1 == ::mkdir(tmp_log_dir_, mode))) {
#endif
    ret = convert_sys_errno();
    PALF_LOG(ERROR, "mkdir tmp log dir failed", K(ret), KPC(this), K(tmp_log_dir_));
  } else if (0 > (pret = snprintf(src_log_dir, MAX_PATH_SIZE, "%s/log_stream", log_dir_))) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "snprintf failed, unexpected error", K(ret));
  } else if (0 > (pret = snprintf(dest_log_dir, MAX_PATH_SIZE, "%s/log_stream_%ld", tmp_log_dir_, timestamp))) {
    ret = OB_ERR_UNEXPECTED;
    PALF_LOG(ERROR, "snprintf failed, unexpected error", K(ret));
  } else if (OB_FAIL(rename_with_retry(src_log_dir, dest_log_dir))) {
  } else if (OB_FAIL(FileDirectoryUtils::fsync_dir(log_dir_))) {
  } else {
  }
  return ret;
}

int PalfEnvImpl::check_tmp_log_dir_exist_(bool &exist) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(FileDirectoryUtils::is_exists(tmp_log_dir_, exist))) {
  } else {
  }
  return ret;
}

int PalfEnvImpl::remove_stale_incomplete_palf_()
{
  int ret = OB_SUCCESS;
  bool exist = false;
  RemoveStaleIncompletePalfFunctor functor(this);
  if (OB_FAIL(check_tmp_log_dir_exist_(exist))) {
  } else if (false == exist) {
  } else if (OB_FAIL(scan_dir(tmp_log_dir_, functor))){
  } else {
  }
  return ret;
}

int PalfEnvImpl::get_io_start_time(int64_t &last_working_time)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    last_working_time = log_io_worker_wrapper_.get_last_working_time();
  }
  return ret;
}


int PalfEnvImpl::get_throttling_options(PalfThrottleOptions &options)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    (void)disk_options_wrapper_.get_throttling_options(options);
  }
  return ret;
}

void PalfEnvImpl::period_calc_disk_usage()
{
  int ret = OB_SUCCESS;
  constexpr int64_t MB = 1024 * 1024;
  PalfDiskOptions disk_options = disk_options_wrapper_.get_disk_opts_for_stopping_writing();
  int64_t used_size_byte = 0;
  int64_t total_usable_size_byte = 0;
  if (OB_FAIL(get_disk_usage_(used_size_byte))) {
  } else {
    const int64_t log_disk_usage_limit_size =  disk_options.log_disk_usage_limit_size_;
    const int64_t log_disk_limit_percent = disk_options.log_disk_utilization_limit_threshold_;
    const int64_t log_disk_warn_percent = 0 == disk_options.log_disk_utilization_threshold_ ?
                                          DEFAULT_LOG_UTL_THRESHOLD : disk_options.log_disk_utilization_threshold_;
    const int64_t usable_disk_limit_size_to_stop_writing =
      log_disk_usage_limit_size * log_disk_limit_percent / 100LL;
    const bool curr_diskspace_enough =
        usable_disk_limit_size_to_stop_writing >= used_size_byte ? true : false;
    const int64_t warn_siz =
      log_disk_usage_limit_size * log_disk_warn_percent / 100LL;
    if (diskspace_enough_ != curr_diskspace_enough) {
      ATOMIC_STORE(&diskspace_enough_, curr_diskspace_enough);
    }
    // NB: print error log when:
    // 1. write-stop.
    if (!curr_diskspace_enough) {
      constexpr int64_t INTERVAL = 1*1000*1000;
      if (palf_reach_time_interval(INTERVAL, disk_not_enough_print_interval_in_loop_thread_)) {
        int tmp_ret = OB_LOG_OUTOF_DISK_SPACE;
        LOG_DBA_ERROR(OB_LOG_OUTOF_DISK_SPACE, "msg", "log disk space is almost full", "ret", tmp_ret,
            "total_size(MB)", log_disk_usage_limit_size/MB,
            "used_size(MB)", used_size_byte/MB,
            "used_percent(%)", (used_size_byte*100) / (log_disk_usage_limit_size + 1),
            "warn_size(MB)", warn_siz/MB,
            "warn_percent(%)", log_disk_warn_percent,
            "limit_size(MB)", usable_disk_limit_size_to_stop_writing/MB,
            "limit_percent(%)", log_disk_limit_percent);
        LOG_DBA_ERROR_(OB_LOG_DISK_SPACE_ALMOST_FULL, tmp_ret, "log disk space is almost full",
            ", total_size(MB)=", log_disk_usage_limit_size/MB,
            ", used_size(MB)=", used_size_byte/MB,
            ", used_percent(%)=", (used_size_byte*100) / (log_disk_usage_limit_size + 1),
            ", warn_size(MB)=", warn_siz/MB,
            ", warn_percent(%)=", log_disk_warn_percent,
            ", limit_size(MB)=", usable_disk_limit_size_to_stop_writing/MB,
            ", limit_percent(%)=", log_disk_limit_percent);
        }
      }
  }

}

int PalfEnvImpl::init_log_io_worker_config_(LogIOWorkerConfig &config)
{
  int ret = OB_SUCCESS;
  constexpr int64_t default_io_queue_cap = 100 * 1024;
  constexpr int64_t default_min_io_queue_cap = PALF_SLIDING_WINDOW_SIZE * 2;
  config.io_queue_capcity_ = MAX(default_min_io_queue_cap, default_io_queue_cap);
  config.batch_depth_ = PALF_SLIDING_WINDOW_SIZE;
  PALF_LOG(INFO, "init_log_io_worker_config_ success", K(config));
  return ret;
}

int PalfEnvImpl::check_can_update_log_disk_options_(const PalfDiskOptions &disk_opts)
{
  int ret = OB_SUCCESS;
  if (NULL != palf_handle_ &&
      disk_opts.log_disk_usage_limit_size_ < MIN_DISK_SIZE_PER_PALF_INSTANCE) {
    ret = OB_NOT_SUPPORTED;
    PALF_LOG(WARN, "log disk is too small", K(disk_opts));
  }
  return ret;
}

int PalfEnvImpl::remove_directory_while_exist_(const char *log_dir)
{
  int ret = OB_SUCCESS;
  bool result = true;
  if (OB_FAIL(FileDirectoryUtils::is_exists(log_dir, result))) {
  } else if (!result) {
    PALF_LOG(WARN, "directory not exist, remove_directory success!", K(log_dir), K(result));
  } else if (OB_FAIL(remove_directory(log_dir))) {
  } else {}
  return ret;
}

LogSharedQueueTh *PalfEnvImpl::get_log_shared_queue_thread()
{
  return &log_shared_queue_th_;
}

} // end namespace palf
} // end namespace oceanbase
