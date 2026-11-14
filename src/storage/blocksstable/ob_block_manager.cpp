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

#define USING_LOG_PREFIX STORAGE_BLKMGR

#include "rust/file_io.h"
#include "rust/config.h"
#include "ob_block_manager.h"
#include <algorithm>
#include "share/rc/ob_server_runtime.h"
#include "storage/blocksstable/ob_shared_macro_block_manager.h"
#include "storage/tmp_file/ob_tmp_file_manager.h"
#include "storage/meta_mem/ob_storage_meta_mem_mgr.h"
#include "storage/meta_store/storage_meta_service.h"
#include "storage/meta_store/ob_local_storage_meta_service.h"
#include "storage/tablet/ob_tablet_macro_info_iterator.h"

using namespace oceanbase::common;
using namespace oceanbase::common::hash;
using namespace oceanbase::blocksstable;
using namespace oceanbase::tmp_file;
using namespace oceanbase::storage;
using namespace oceanbase::share;


namespace oceanbase {
namespace blocksstable {
/**
 * ------------------------------------ObMacroBlockRewriteSeqGenerator-------------------------------------
 */
ObMacroBlockRewriteSeqGenerator::ObMacroBlockRewriteSeqGenerator()
    : rewrite_seq_(0), lock_(common::ObLatchIds::BLOCK_ID_GENERATOR_LOCK) {}

ObMacroBlockRewriteSeqGenerator::~ObMacroBlockRewriteSeqGenerator() {
  rewrite_seq_ = 0;
}

void ObMacroBlockRewriteSeqGenerator::reset() { rewrite_seq_ = 0; }

int ObMacroBlockRewriteSeqGenerator::generate_next_sequence(uint64_t &blk_seq) {
  int ret = OB_SUCCESS;
  SpinWLockGuard guard(lock_);
  if (OB_UNLIKELY(MacroBlockId::MAX_WRITE_SEQ == rewrite_seq_)) {
    ret = OB_ERROR_OUT_OF_RANGE;
    LOG_ERROR("rewrite sequence number overflow!", K(ret),
              LITERAL_K(MacroBlockId::MAX_WRITE_SEQ), K(rewrite_seq_));
  } else {
    blk_seq = ++rewrite_seq_;
    if (OB_UNLIKELY(BLOCK_SEQUENCE_WARNING_LINE < blk_seq)) {
      const int64_t remaining_rewritten_block_count =
          MacroBlockId::MAX_WRITE_SEQ - blk_seq;
      LOG_ERROR("macro block rewrite sequence is close to exhaustion; stop local writes before overflow",
                K(remaining_rewritten_block_count));
    }
  }
  return ret;
}

/**
 * -----------------------------------------ObBlockManager------------------------------------------
 */
ObBlockManager::ObBlockManager()
    : bucket_lock_(), block_map_(), default_block_size_(0),
      marker_status_(), marker_lock_(), is_mark_sweep_enabled_(false),
      sweep_lock_(), mark_block_task_(*this), inspect_bad_block_task_(*this),
      timer_(), bad_block_lock_(), data_file_(storage_io::new_block_owner()),
      data_path_{}, initial_file_size_(0), disk_percentage_(0), blk_seq_generator_(),
      alloc_num_(0), is_inited_(false), is_started_(false) {}

ObBlockManager::~ObBlockManager() { destroy(); }

int ObBlockManager::prepare_file(const storage_io::ServiceHandle &service,
    const char *data_dir, const char *sstable_dir, int64_t block_size,
    int64_t initial_size, int64_t disk_percentage, size_t worker_threads, size_t max_depth)
{
  int ret = OB_SUCCESS;
  if (data_path_[0] != '\0') { return OB_INIT_TWICE; }
  if (data_dir == nullptr || sstable_dir == nullptr || data_dir[0] == '\0'
      || sstable_dir[0] == '\0' || STRLEN(sstable_dir) >= OB_MAX_FILE_NAME_LENGTH
      || block_size < DIO_READ_ALIGN_SIZE || block_size % DIO_READ_ALIGN_SIZE != 0
      || initial_size < 0 || disk_percentage < 0 || disk_percentage >= 100) {
    return OB_INVALID_ARGUMENT;
  }
  char path[OB_MAX_FILE_NAME_LENGTH];
  if (OB_FAIL(databuff_printf(path, sizeof(path), "%s/%s/%s",
      data_dir, BLOCK_SSTBALE_DIR_NAME, BLOCK_SSTBALE_FILE_NAME))) {
    return ret;
  }
  storage_io::ExecutionOptions execution{};
  execution.worker_threads = worker_threads;
  execution.max_depth = max_depth;
  if (OB_FAIL(convert_storage_error(data_file_->prepare_blocks(service,
      {reinterpret_cast<const uint8_t *>(sstable_dir), STRLEN(sstable_dir)}, execution)))) {
    return ret;
  }
  STRNCPY(data_path_, path, sizeof(data_path_));
  default_block_size_ = block_size;
  initial_file_size_ = initial_size;
  disk_percentage_ = disk_percentage;
  return ret;
}

int ObBlockManager::open_file(int64_t reserved_size)
{
  int ret = OB_SUCCESS;
  if (reserved_size < 0) { return OB_INVALID_ARGUMENT; }
  const rust::Slice<const uint8_t> path{
      reinterpret_cast<const uint8_t *>(data_path_), STRLEN(data_path_)};
  storage_io::BlockOptions options{};
  options.block_size = default_block_size_;
  options.initial_size = initial_file_size_;
  options.initial_percentage = disk_percentage_;
  options.reserved_bytes = reserved_size;
  options.punch_holes = config::_enable_block_file_punch_hole();
  auto opened = data_file_->open_blocks(path, options);
  ret = convert_block_status(opened.status);
  if (ret != OB_SUCCESS) {
    LOG_WARN("data file open failed", K(ret), K(data_path_),
        "native_error", opened.primary_error.native_code, "error", opened.primary_error.message.c_str());
  }
  auto &summary = opened.directory_summary;
  if (summary.count != 0) {
    LOG_WARN("directory durability unconfirmed; only first failure detail retained", K(ret),
        "failed_flush_count", summary.count, "count_is_lower_bound", summary.count == UINT64_MAX,
        "first_path", ObString(static_cast<int32_t>(summary.first_path.size()),
            reinterpret_cast<const char *>(summary.first_path.data())),
        "native_error", summary.first_error.native_code, "error", summary.first_error.message.c_str());
  }
  if (opened.sync.stage != file_io::SyncStage::Complete) {
    LOG_WARN("data file creation directory sync failed", K(ret), K(data_path_),
        "stage", static_cast<int>(opened.sync.stage), "native_error", opened.sync.primary.native_code,
        "error", opened.sync.primary.message.c_str(), "close_error", opened.sync.close.native_code);
  }
  if (opened.cleanup_close.domain != file_io::ErrorDomain::None
      || opened.cleanup_remove.domain != file_io::ErrorDomain::None
      || opened.cleanup_sync.stage != file_io::SyncStage::Complete) {
    LOG_WARN("data file creation cleanup failed", K(ret), K(data_path_),
        "close_error", opened.cleanup_close.native_code, "remove_error", opened.cleanup_remove.native_code,
        "sync_error", opened.cleanup_sync.primary.native_code, "sync_close_error", opened.cleanup_sync.close.native_code);
  }
  return ret;
}

int ObBlockManager::close_file()
{
  const int ret = convert_storage_error(data_file_->close_blocks());
  if (ret == OB_SUCCESS) {
    data_path_[0] = '\0';
  }
  return ret;
}

int ObBlockManager::validate_io_threads(size_t worker_threads) const
{
  return convert_storage_error(data_file_->validate_block_workers(worker_threads));
}

int ObBlockManager::fsync_block()
{
  return convert_block_status(data_file_->sync_blocks());
}

int ObBlockManager::submit_read(const MacroBlockId &id, int64_t offset, int64_t size,
    const ObIOFlag &flag, int64_t timeout_us,
    std::unique_ptr<storage_io::CallbackAdapter> callback, ObIOHandle &handle)
{
  handle.reset();
  if (!id.is_valid() || offset < 0 || size <= 0 || !flag.is_valid()) { return OB_INVALID_ARGUMENT; }
  const auto options = make_request_options(flag, storage_io::RequestMode::Read, offset, size, timeout_us);
  return accept_io_request(data_file_->submit_block(id.block_index(), options, {}, std::move(callback)), handle);
}

int ObBlockManager::submit_write(const MacroBlockId &id, int64_t offset, const char *buffer, int64_t size,
    const ObIOFlag &flag, int64_t timeout_us, ObIOHandle &handle)
{
  handle.reset();
  if (!id.is_valid() || offset < 0 || buffer == nullptr || size <= 0 || !flag.is_valid()) {
    return OB_INVALID_ARGUMENT;
  }
  const auto options = make_request_options(flag, storage_io::RequestMode::Write, offset, size, timeout_us);
  return accept_io_request(data_file_->submit_block(id.block_index(), options,
      {reinterpret_cast<const uint8_t *>(buffer), static_cast<size_t>(size)}, nullptr), handle);
}

void ObBlockManager::release_block(uint64_t index)
{
  const auto result = data_file_->release_block(index);
  const int ret = convert_block_status(result.status);
  if (ret != OB_SUCCESS || result.reclamation_error != 0) {
    LOG_WARN("block release or reclamation failed", K(ret), K(index),
        "reclamation_error", result.reclamation_error, "os_error", result.reclamation_os_error);
  }
}

int ObBlockManager::make_growth_policy(storage_io::BlockGrowth &policy) const
{
  int ret = OB_SUCCESS;
  int64_t reserved = 0;
  if (OB_FAIL(STORAGE_META_SERVICE.get_reserved_size(reserved))) {
  } else {
    policy.max_bytes = config::datafile_maxsize();
    policy.next_bytes = config::datafile_next();
    policy.usage_upper_percent = config::_datafile_usage_upper_bound_percentage();
    policy.reserved_bytes = reserved;
  }
  return ret;
}

int64_t ObBlockManager::get_max_block_size(int64_t reserved_size) const
{
  uint64_t maximum = 0;
  const int64_t configured = std::max(int64_t{0}, config::datafile_maxsize());
  if (reserved_size < 0 || OB_SUCCESS != convert_block_status(
      data_file_->maximum_block_capacity(configured, reserved_size, maximum))) {
    return get_total_block_size();
  }
  return static_cast<int64_t>(maximum);
}

int ObBlockManager::check_space_full(int64_t required_size, bool alarm_if_space_full) const
{
  if (!is_mark_sweep_enabled()) { return OB_NOT_INIT; }
  if (required_size < 0) { return OB_INVALID_ARGUMENT; }
  int ret = OB_SUCCESS;
  int64_t reserved = 0;
  if (OB_FAIL(STORAGE_META_SERVICE.get_reserved_size(reserved))) { return ret; }
  ret = convert_block_status(data_file_->check_block_space(required_size,
      config::datafile_maxsize(), reserved, config::data_disk_usage_limit_percentage()));
  if (ret == OB_SERVER_OUTOF_DISK_SPACE && alarm_if_space_full
      && REACH_TIME_INTERVAL(24 * 3600LL * 1000 * 1000)) {
    LOG_DBA_ERROR_V2(OB_SHARE_OUTOF_DISK_SPACE, ret,
        "disk is almost full. required size is ", required_size,
        ". [suggestion] Increase the datafile_size or datafile_disk_percentage parameter. ");
  }
  return ret;
}

int ObBlockManager::init(const int64_t block_size) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
  } else if (data_path_[0] == '\0' || block_size != default_block_size_
      || OB_UNLIKELY(block_size < DIO_READ_ALIGN_SIZE)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(timer_.set_run_wrapper_with_ret(share::server_runtime()))) {
  } else if (OB_FAIL(timer_.init("BlkMgr"))) {
  } else if (OB_FAIL(bucket_lock_.init(DEFAULT_LOCK_BUCKET_COUNT,
                                       ObLatchIds::BLOCK_MANAGER_LOCK))) {
  } else if (OB_FAIL(block_map_.init(
                 ObMemAttr("BlockMap")))) {
  } else {
    default_block_size_ = block_size;
    ATOMIC_STORE(&alloc_num_, 0);
    is_inited_ = true;
  }

  if (IS_NOT_INIT) {
    destroy();
  }
  return ret;
}

int ObBlockManager::start(const int64_t reserved_size) {
  int ret = OB_SUCCESS;
  LOG_DBA_INFO_V2(OB_SERVER_BLOCK_MANAGER_START_BEGIN,
                  DBA_STEP_INC_INFO(server_start),
                  "block manager start begin.");

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(open_file(reserved_size))) {
  } else {
    if (!timer_.task_exist(inspect_bad_block_task_)) {
      if (OB_FAIL(timer_.schedule(inspect_bad_block_task_, INSPECT_DELAY_US,
                                  true))) {
      }
    }
    if (OB_SUCC(ret) && !timer_.task_exist(mark_block_task_)) {
      if (OB_FAIL(timer_.schedule(mark_block_task_, RECYCLE_DELAY_US, true))) {
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(timer_.start())) {
    }
  }
  if (OB_FAIL(ret)) {
    LOG_DBA_ERROR_V2(OB_SERVER_BLOCK_MANAGER_START_FAIL, ret,
                     DBA_STEP_INC_INFO(server_start),
                     "block manager start fail. ",
                     "you may find solutions in previous error logs or seek "
                     "help from official technicians.");
  } else {
    is_started_ = true;
    LOG_INFO("start block manager");
    LOG_DBA_INFO_V2(OB_SERVER_BLOCK_MANAGER_START_SUCCESS,
                    DBA_STEP_INC_INFO(server_start),
                    "block manager start success.");
  }

  return ret;
}

void ObBlockManager::stop() { timer_.stop(); }

void ObBlockManager::wait() {
  timer_.wait();
  LOG_INFO("the block manager finish wait");
}

void ObBlockManager::destroy() {
  timer_.destroy();
  inspect_bad_block_task_.reset();
  bucket_lock_.destroy();
  block_map_.destroy();
  {
    lib::ObMutexGuard bad_block_guard(bad_block_lock_);
    bad_block_infos_.destroy();
  }
  const int close_ret = close_file();
  if (close_ret != OB_SUCCESS) { LOG_ERROR_RET(close_ret, "data file close failed", K(close_ret)); }
  default_block_size_ = 0;
  is_mark_sweep_enabled_ = false;
  marker_status_.reset();
  blk_seq_generator_.reset();
  ATOMIC_STORE(&alloc_num_, 0);
  is_inited_ = false;
}

int ObBlockManager::inner_alloc_block(uint64_t &index) {
  storage_io::BlockGrowth policy{};
  const int ret = make_growth_policy(policy);
  return ret == OB_SUCCESS
      ? convert_block_status(data_file_->allocate_block(policy, index)) : ret;
}

int ObBlockManager::alloc_object(ObStorageObjectHandle &object_handle) {
  int ret = OB_SUCCESS;
  MacroBlockId macro_id;
  uint64_t block_index = 0;
  bool physical_owned = false;
  uint64_t write_seq = 0;

  if (IS_NOT_INIT || !is_started()) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(inner_alloc_block(block_index))) {
  } else {
    physical_owned = true;
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(blk_seq_generator_.generate_next_sequence(write_seq))) {
    } else {
      macro_id.reset();
      macro_id.set_write_seq(write_seq);
      macro_id.set_block_index(block_index);
      if (OB_FAIL(object_handle.set_macro_block_id(macro_id))) {
      } else {
        physical_owned = false;
        ATOMIC_AAF(&alloc_num_, 1);
        FLOG_INFO("successfully alloc block", K(macro_id));
      }
    }
  }
  if (physical_owned) { release_block(block_index); }
  return ret;
}

int ObBlockManager::alloc_block(ObMacroBlockHandle &macro_handle) {
  int ret = OB_SUCCESS;
  uint64_t block_index = 0;
  bool physical_owned = false;
  uint64_t write_seq = 0;

  if (IS_NOT_INIT || !is_started()) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(inner_alloc_block(block_index))) {
  } else {
    physical_owned = true;
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(blk_seq_generator_.generate_next_sequence(write_seq))) {
    } else {
      MacroBlockId macro_id(write_seq, block_index, 0);
      if (OB_FAIL(macro_handle.set_macro_block_id(macro_id))) {
      } else {
        physical_owned = false;
        ATOMIC_AAF(&alloc_num_, 1);
        FLOG_INFO("successfully alloc block", K(macro_id));
      }
    }
  }

  if (physical_owned) {
    release_block(block_index);
  }
  return ret;
}

int ObBlockManager::async_read_block(const ObMacroBlockReadInfo &read_info,
                                     ObMacroBlockHandle &macro_handle) {
  return macro_handle.async_read(read_info);
}

int ObBlockManager::async_write_block(const ObMacroBlockWriteInfo &write_info,
                                      ObMacroBlockHandle &macro_handle) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!write_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(OB_SERVER_BLOCK_MGR.alloc_block(macro_handle))) {
  } else if (OB_FAIL(macro_handle.async_write(write_info))) {
  }
  return ret;
}

int ObBlockManager::read_block(const ObMacroBlockReadInfo &read_info,
                               ObMacroBlockHandle &macro_handle) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(async_read_block(read_info, macro_handle))) {
  } else if (OB_FAIL(macro_handle.wait())) {
  }
  return ret;
}

int ObBlockManager::write_block(const ObMacroBlockWriteInfo &write_info,
                                ObMacroBlockHandle &macro_handle) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(async_write_block(write_info, macro_handle))) {
  } else if (OB_FAIL(macro_handle.wait())) {
  }
  return ret;
}

int ObBlockManager::first_mark_device() {
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) { return OB_NOT_INIT; }
  if (is_mark_sweep_enabled()) { return OB_INIT_TWICE; }
  ret = convert_block_status(data_file_->restart_marking());
  BlockMap::BlurredIterator iter(block_map_);
  MacroBlockId id;
  BlockInfo info;
  uint64_t max_write_sequence = 0;
  while (OB_SUCC(ret) && OB_SUCC(ret = iter.next(id, info))) {
    if (id.block_index() < 1) { ret = OB_INVALID_DATA; }
    else {
      const uint64_t index = id.block_index();
      ret = convert_block_status(data_file_->mark_blocks({&index, 1}));
      max_write_sequence = std::max(max_write_sequence, id.write_seq());
    }
  }
  if (ret == OB_ITER_END) { ret = convert_block_status(data_file_->finish_marking()); }
  if (OB_SUCC(ret)) {
    blk_seq_generator_.update_sequence(max_write_sequence);
    enable_mark_sweep();
    storage_io::BlockStats stats{};
    if (convert_block_status(data_file_->block_stats(stats)) == OB_SUCCESS && stats.reclamation_failures > 0) {
      LOG_WARN("block reclamation failed during marking", "failures", stats.reclamation_failures);
    }
  } else {
    data_file_->restart_marking();
  }
  return ret;
}

int64_t ObBlockManager::get_max_macro_block_count(int64_t reserved_size) const {
  return default_block_size_ > 0 ? get_max_block_size(reserved_size) / default_block_size_ : 0;
}

int64_t ObBlockManager::get_free_macro_block_count() const {
  storage_io::BlockStats stats{};
  return convert_block_status(data_file_->block_stats(stats)) == OB_SUCCESS ? stats.free_blocks : 0;
}

int64_t ObBlockManager::get_used_macro_block_count() const {
  return block_map_.count();
}

int64_t ObBlockManager::get_total_block_size() const {
  storage_io::BlockStats stats{};
  return convert_block_status(data_file_->block_stats(stats)) == OB_SUCCESS ? stats.capacity : 0;
}

int ObBlockManager::get_macro_block_info(
    const MacroBlockId &macro_id, ObMacroBlockInfo &macro_block_info,
    ObMacroBlockHandle &macro_block_handle) {
  int ret = OB_SUCCESS;
  BlockInfo block_info;
  bool has_inc_ref = false;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!macro_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, macro_id.hash());
    if (OB_FAIL(block_map_.get(macro_id, block_info)) &&
        ret != OB_HASH_NOT_EXIST) {
      // BUG, should not happen
      LOG_ERROR("fatal error, this block should be in block map", K(ret),
                K(macro_id));
    } else if (OB_UNLIKELY(OB_SUCCESS == ret && block_info.ref_cnt_ < 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("fatal error, invalid refcnt", K(ret), K(macro_id),
                K(block_info));
    } else if (OB_UNLIKELY(OB_HASH_NOT_EXIST == ret ||
                           0 == block_info.ref_cnt_)) {
      // set `is_free_` to true, skip this MacroBlock in upper layer.
      ret = OB_SUCCESS;
      macro_block_info.is_free_ = true;
    } else {
      macro_block_info.is_free_ = false;
      macro_block_info.ref_cnt_ = block_info.ref_cnt_;
      macro_block_info.access_time_ = block_info.last_write_time_;
      block_info.access_time_ = ObTimeUtility::fast_current_time();
      block_info.ref_cnt_++;
      if (OB_FAIL(block_map_.insert_or_update(macro_id, block_info))) {
      } else {
        has_inc_ref = true;
        LOG_DEBUG("debug ref_cnt: inc_ref in memory", K(ret), K(macro_id),
                  K(block_info), K(lbt()));
      }
    }
  }
  if (OB_SUCC(ret) && !macro_block_info.is_free_) {
    if (OB_FAIL(macro_block_handle.set_macro_block_id(macro_id))) {
    }
  }
  if (has_inc_ref) {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(dec_ref(macro_id))) {
    }
  }

  return ret;
}

int ObBlockManager::check_macro_block_free(const MacroBlockId &macro_id,
                                           bool &is_free) const {
  int ret = OB_SUCCESS;
  is_free = false;
  BlockInfo block_info;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!macro_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(block_map_.get(macro_id, block_info))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
    } else {
      is_free = true;
      ret = OB_SUCCESS;
    }
  } else {
    is_free = 0 == block_info.ref_cnt_;
  }
  return ret;
}

int ObBlockManager::get_bad_block_infos(
    common::ObIArray<ObBadBlockInfo> &bad_block_infos) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else {
    lib::ObMutexGuard bad_block_guard(bad_block_lock_);
    if (OB_FAIL(bad_block_infos.assign(bad_block_infos_))) {
    }
  }
  return ret;
}

int ObBlockManager::report_bad_block(const MacroBlockId &macro_block_id,
                                     const int64_t error_type,
                                     const char *error_msg,
                                     const char *file_path) {
  int ret = OB_SUCCESS;
  const int64_t MAX_BAD_BLOCK_NUMBER =
      std::max(static_cast<int64_t>(10), OB_STORAGE_OBJECT_MGR.get_total_macro_block_count() / 100);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!macro_block_id.is_valid() ||
                         OB_TIMEOUT == error_type || NULL == error_msg)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (is_bad_block(macro_block_id)) {
    ret = OB_SUCCESS; // No need to print warn log
    LOG_INFO("already found this bad block, ", K(macro_block_id), K(error_type),
             K(error_msg));
  } else {
    ObBadBlockInfo bad_block_info;
    lib::ObMutexGuard bad_block_guard(bad_block_lock_);
    if (bad_block_infos_.count() >= MAX_BAD_BLOCK_NUMBER) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(databuff_printf(bad_block_info.error_msg_,
                                       sizeof(bad_block_info.error_msg_), "%s",
                                       error_msg))) {
    } else {
      STRNCPY(bad_block_info.store_file_path_, file_path,
              sizeof(bad_block_info.store_file_path_) - 1);
      bad_block_info.disk_id_ = macro_block_id.first_id();
      bad_block_info.macro_block_id_ = macro_block_id;
      bad_block_info.error_type_ = error_type;
      bad_block_info.check_time_ = ObTimeUtility::current_time();
      if (OB_FAIL(bad_block_infos_.push_back(bad_block_info))) {
      } else {
        LOG_ERROR("add bad block info", K(bad_block_info));
      }
    }
  }
  return ret;
}

int ObBlockManager::resize_file(const int64_t new_data_file_size,
                                const int64_t new_data_file_disk_percentage,
                                const int64_t reserved_size) {
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(reserved_size < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!is_mark_sweep_enabled()) {
    LOG_INFO("mark and sweep is disabled, do not resize file at present");
  } else {
    SpinWLockGuard sweep_guard(sweep_lock_);
    if (new_data_file_size < 0 || new_data_file_disk_percentage < 0
        || new_data_file_disk_percentage >= 100) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      ret = convert_block_status(data_file_->resize_blocks(
          new_data_file_size, new_data_file_disk_percentage, reserved_size));
    }
  }
  return ret;
}

int ObBlockManager::inc_ref(const MacroBlockId &macro_id) {
  int ret = OB_SUCCESS;
  BlockInfo block_info;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("not init", K(ret));
  } else if (OB_UNLIKELY(!macro_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("Invalid argument, ", K(ret), K(macro_id));
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, macro_id.hash());
    if (OB_FAIL(block_map_.get(macro_id, block_info))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        block_info.reset();
        ret = OB_SUCCESS;
      } else {
        LOG_ERROR("get block_info fail", K(ret), K(macro_id));
      }
    } else if (OB_UNLIKELY(
                   block_info.ref_cnt_ < 0 ||
                   (0 == block_info.ref_cnt_ && is_mark_sweep_enabled()))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("un-expected MacroBlock refcnt", K(ret), K(macro_id),
                K(block_info));
    }

    if (OB_SUCC(ret)) {
      block_info.access_time_ = ObTimeUtility::fast_current_time();
      block_info.ref_cnt_++;
      if (OB_FAIL(block_map_.insert_or_update(macro_id, block_info))) {
      } else {
        LOG_DEBUG("debug ref_cnt: inc_ref in memory", K(ret), K(macro_id),
                  K(block_info), K(lbt()));
      }
    }
  }
  return ret;
}

int ObBlockManager::dec_ref(const MacroBlockId &macro_id) {
  int ret = OB_SUCCESS;
  BlockInfo block_info;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("not init", K(ret));
  } else if (OB_UNLIKELY(!macro_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("Invalid argument, ", K(ret), K(macro_id));
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, macro_id.hash());
    if (OB_FAIL(block_map_.get(macro_id, block_info))) {
    } else if (OB_UNLIKELY(0 == block_info.ref_cnt_)) {
      // BUG, should not happen
      ret = OB_ERR_SYS;
      LOG_ERROR("fatal error, ref cnt must not less than 0", K(ret),
                K(macro_id), K(block_info));
    } else {
      block_info.access_time_ = ObTimeUtility::fast_current_time();
      block_info.ref_cnt_--;
      if (OB_FAIL(block_map_.insert_or_update(macro_id, block_info))) {
      } else {
        LOG_DEBUG("debug ref_cnt: dec_ref in memory", K(ret), K(macro_id),
                  K(block_info), K(lbt()));
      }
    }
  }
  return ret;
}

int ObBlockManager::update_write_time(const MacroBlockId &macro_id,
                                      const bool update_to_max_time) {
  int ret = OB_SUCCESS;
  BlockInfo block_info;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("not init", K(ret));
  } else if (OB_UNLIKELY(!macro_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid argument", K(ret), K(macro_id));
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, macro_id.hash());
    if (OB_FAIL(block_map_.get(macro_id, block_info))) {
    } else {
      block_info.last_write_time_ =
          update_to_max_time ? INT64_MAX : ObTimeUtility::fast_current_time();
      if (OB_FAIL(block_map_.insert_or_update(macro_id, block_info))) {
      }
    }
  }
  return ret;
}

int ObBlockManager::get_marker_status(ObMacroBlockMarkerStatus &status) {
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    SpinRLockGuard guard(marker_lock_);
    status = marker_status_;
  }
  return ret;
}

void ObBlockManager::update_marker_status(const ObMacroBlockMarkerStatus &tmp_status)
{
  SpinWLockGuard guard(marker_lock_);
  ObMacroBlockMarkerStatus prev_result = marker_status_;
  marker_status_.reset();
  marker_status_.total_block_count_ = OB_STORAGE_OBJECT_MGR.get_total_macro_block_count();
  constexpr int64_t FILE_HEADER_BLOCK_COUNT = 1;
  marker_status_.reserved_block_count_ = FILE_HEADER_BLOCK_COUNT + tmp_status.reserved_block_count_;
  marker_status_.free_count_ = get_free_macro_block_count();
  marker_status_.hold_count_ = tmp_status.hold_count_;
  marker_status_.mark_cost_time_ = tmp_status.mark_cost_time_;
  marker_status_.sweep_cost_time_ = tmp_status.sweep_cost_time_;
  marker_status_.start_time_ = tmp_status.start_time_;
  marker_status_.mark_finished_ = tmp_status.mark_finished_;
  if (tmp_status.mark_finished_) {
    // Mark succeed, update marker status with new result.
    marker_status_.last_end_time_ = tmp_status.last_end_time_;
    marker_status_.linked_block_count_ = tmp_status.linked_block_count_;
    marker_status_.index_block_count_ = tmp_status.index_block_count_;
    marker_status_.ids_block_count_ = tmp_status.ids_block_count_;
    marker_status_.tmp_file_count_ = tmp_status.tmp_file_count_;
    marker_status_.data_block_count_ = tmp_status.data_block_count_;
    marker_status_.pending_free_count_ = tmp_status.pending_free_count_;
    marker_status_.hold_info_ = tmp_status.hold_info_;
  } else {
    // Mark skipped, update marker status with previous result.
    marker_status_.last_end_time_ = prev_result.last_end_time_;
    marker_status_.linked_block_count_ = prev_result.linked_block_count_;
    marker_status_.index_block_count_ = prev_result.index_block_count_;
    marker_status_.ids_block_count_ = prev_result.ids_block_count_;
    marker_status_.tmp_file_count_ = prev_result.tmp_file_count_;
    marker_status_.data_block_count_ = prev_result.data_block_count_;
    marker_status_.pending_free_count_ = prev_result.pending_free_count_;
    marker_status_.hold_info_ = prev_result.hold_info_;
  }
}

bool ObBlockManager::GetOldestHoldBlockFunctor::operator()(
    const MacroBlockId &key, const BlockInfo &value) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(macro_id_set_.exist_refactored(key))) {
    if (OB_HASH_EXIST == ret) {
      ret = OB_SUCCESS;
    } else if (OB_HASH_NOT_EXIST == ret) {
      // TODO yunshan.tys : add new solutions to find leaked macro blocks
      if (0 != value.ref_cnt_ // not wash tablet block
          && (!oldest_hold_block_info_.macro_id_.is_valid() ||
              value.access_time_ < oldest_hold_block_info_.last_access_time_)) {
        oldest_hold_block_info_.macro_id_ = key;
        oldest_hold_block_info_.last_access_time_ = value.access_time_;
        oldest_hold_block_info_.ref_cnt_ = value.ref_cnt_;
      }
      ret = OB_SUCCESS;
    } else {
    }
  }
  ret_code_ = ret;
  return OB_SUCCESS == ret;
}

bool ObBlockManager::GetPendingFreeBlockFunctor::operator()(
    const MacroBlockId &key, const BlockInfo &value) {
  int ret = OB_SUCCESS;
  if (value.ref_cnt_ > 0) {
    hold_count_++;
  } else if (OB_UNLIKELY(value.ref_cnt_ < 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("fatal error, macro block ref cnt less than 0", K(ret), K(key),
              K(value));
  } else if (OB_UNLIKELY(blk_map_.count() >= max_free_blk_cnt_)) {
    // skip inserting more free block
  } else if (OB_FAIL(blk_map_.insert(key, true))) {
  }
  ret_code_ = ret;
  return OB_SUCCESS == ret;
}

bool ObBlockManager::GetAllMacroBlockIdFunctor::operator()(
    const MacroBlockId &key, const BlockInfo &value) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(value.ref_cnt_ < 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("fatal error, macro block ref cnt less than 0", K(ret), K(key),
              K(value));
  } else if (OB_FAIL(block_ids_.push_back(key))) {
  }
  ret_code_ = ret;
  return OB_SUCCESS == ret;
}

bool ObBlockManager::DoBlockSweepFunctor::operator()(
    const MacroBlockId &macro_id, const bool can_free) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!can_free)) {
    // ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(block_manager_.sweep_one_block(macro_id))) {
  }
  // record last failure ret
  ret_code_ = OB_SUCCESS == ret ? ret_code_ : ret;
  // ignore ret to sweep all blocks
  return true;
}

int ObBlockManager::get_limited_iter_macro_ids(ObArray<MacroBlockId> &ids_array, int max_iteration)
{
  int ret = OB_SUCCESS;
  ObBlockManager::LimitedIterGetBlockFunctor getter(ids_array, max_iteration);
  if (OB_FAIL(block_map_.for_each(getter))) {
    if (OB_EAGAIN == ret && OB_SUCCESS == getter.get_ret_code()) {
      // reach target iteration and early exit
      ret = OB_SUCCESS;
    } else {
    }
  }
  return ret;
}

bool ObBlockManager::LimitedIterGetBlockFunctor::operator()(const MacroBlockId &key, const BlockInfo &value)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(value.ref_cnt_ < 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("fatal error, macro block ref cnt less than 0", K(ret), K(key),
              K(value));
  } else if (OB_FAIL(block_ids_.push_back(key))) {
  }
  ret_code_ = ret;
  max_iteration_--;
  return max_iteration_ > 0 && OB_SUCCESS == ret;
}

bool ObBlockManager::is_bad_block(const MacroBlockId &macro_block_id) {
  bool is_exist = false;
  lib::ObMutexGuard bad_block_guard(bad_block_lock_);
  for (int64_t i = 0; i < bad_block_infos_.count(); ++i) {
    if (bad_block_infos_[i].macro_block_id_ == macro_block_id) {
      is_exist = true;
      break;
    }
  }
  return is_exist;
}

int ObBlockManager::do_sweep(MacroBlkIdMap &mark_info) {
  int ret = OB_SUCCESS;
  DoBlockSweepFunctor functor(*this);
  if (0 == mark_info.count()) {
    // do nothing
  } else if (OB_FAIL(mark_info.for_each(functor))) {
    ret = functor.get_ret_code();
  }
  return ret;
}

int ObBlockManager::sweep_one_block(const MacroBlockId &macro_id) {
  int ret = OB_SUCCESS;
  ObBucketHashWLockGuard lock_guard(bucket_lock_, macro_id.hash());
  BlockInfo block_info;
  const uint64_t block_index = macro_id.second_id();
  if (OB_FAIL(block_map_.get(macro_id, block_info))) {
  } else if (OB_UNLIKELY(block_info.ref_cnt_ > 0)) {
    // skip using block.
  } else if (OB_FAIL(block_map_.erase(macro_id))) {
  } else {
    release_block(block_index);
    FLOG_INFO("block manager free block", K(macro_id));
  }
  return ret;
}

void ObBlockManager::mark_and_sweep()
{
  int ret = OB_SUCCESS;
  ObHashSet<MacroBlockId, NoPthreadDefendMode> macro_id_set;
  MacroBlkIdMap mark_info;
  ObMacroBlockMarkerStatus tmp_status;
  // we must assign alloc_num_ before mark_macro_blocks, because it will be set to 0 in this func
  int64_t alloc_num = 0;
  // recycle maximum 400 GB space, but no more than 8MB memory consumption for mark_info
  const int64_t MAX_FREE_BLOCK_COUNT_PER_ROUND = 200000;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (!is_mark_sweep_enabled()) {
    LOG_INFO("mark and sweep is disabled, do not mark and sweep this round");
  } else {
    if (OB_FAIL(mark_info.init(ObModIds::OB_STORAGE_FILE_BLOCK_REF))) {
    } else if (OB_FAIL(macro_id_set.create(MAX(2, MIN(MAX_FREE_BLOCK_COUNT_PER_ROUND, block_map_.get_bkt_cnt())),
                                           "BlkIdSetBkt",
                                           "BlkIdSetNode"))) {
    } else {
      GetPendingFreeBlockFunctor pending_free_functor(
          MAX_FREE_BLOCK_COUNT_PER_ROUND, mark_info, tmp_status.hold_count_);
      tmp_status.start_time_ = ObTimeUtility::fast_current_time();
      if (OB_FAIL(block_map_.for_each(pending_free_functor))) {
        ret = pending_free_functor.get_ret_code();
      } else if ((mark_info.count() < MAX_FREE_BLOCK_COUNT_PER_ROUND)) {
        // Only try to set alloc_num_ to 0 when macro info is complete, else do mark and sweep again.
        if (0 != (alloc_num = ATOMIC_SET(&alloc_num_, 0))) {
          // Some one alloc block after GetPendingFreeBlockFunctor concurrently. let mark and sweep do again next round
          // whatever mark_info is empty or not
          ATOMIC_SET(&alloc_num_, alloc_num);
        }
      }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(mark_macro_blocks(mark_info, macro_id_set, tmp_status))) {
        if (OB_ALLOCATE_MEMORY_FAILED == ret) {
          LOG_INFO("mark blocks meet memory issue, still countinue sweep to lease compaction space");
          ret = OB_SUCCESS;
        } else {
        }
      }

      if (OB_FAIL(ret)) {
        ATOMIC_FAA(&alloc_num_, alloc_num); // add alloc_num back to trigger next round mark
      } else {
        tmp_status.pending_free_count_ += mark_info.count();
        tmp_status.mark_cost_time_ = ObTimeUtility::fast_current_time() - tmp_status.start_time_;
        // sweep
        SpinWLockGuard guard(sweep_lock_);
        if (OB_FAIL(do_sweep(mark_info))) {
        } else if (tmp_status.mark_finished_) {
          tmp_status.last_end_time_ = ObTimeUtility::fast_current_time();
          tmp_status.sweep_cost_time_ = tmp_status.last_end_time_ - tmp_status.start_time_ - tmp_status.mark_cost_time_;

          GetOldestHoldBlockFunctor hold_info_functor(macro_id_set, tmp_status.hold_info_);
          if (OB_FAIL(block_map_.for_each(hold_info_functor))) {
            ret = hold_info_functor.get_ret_code();
          }
        }
      }
      if (OB_SUCC(ret)) {
        update_marker_status(tmp_status);
      }
      FLOG_INFO("finish once mark and sweep", K(ret), K(alloc_num),
                K(mark_info.count()), K_(marker_status), "map_cnt",
                block_map_.count());
    }
  }
  macro_id_set.destroy();
}

int ObBlockManager::mark_macro_blocks(
    MacroBlkIdMap &mark_info,
    common::hash::ObHashSet<MacroBlockId, common::hash::NoPthreadDefendMode> &macro_id_set,
    ObMacroBlockMarkerStatus &tmp_status)
{
  int ret = OB_SUCCESS;
  if (0 == mark_info.count() && 0 == alloc_num_) {
    tmp_status.mark_finished_ = false;
    LOG_INFO("no block alloc/free, no need to mark blocks", K(ret));
  } else if (OB_FAIL(mark_tmp_file_blocks(mark_info, macro_id_set, tmp_status))) {
  } else {
    MacroBlockId macro_id;
    if (OB_FAIL(mark_local_storage_blocks(mark_info, macro_id_set, tmp_status))) {
    } else if (OB_FALSE_IT(
                   ::oceanbase::share::server_service<::oceanbase::blocksstable::ObSharedMacroBlockMgr>()->get_cur_shared_block(
                       macro_id))) {
    } else if (OB_FAIL(
                   mark_held_block(
                       macro_id, mark_info, macro_id_set, tmp_status))) {
    } else if (OB_FALSE_IT(::oceanbase::share::server_service<::oceanbase::storage::ObLocalStorageMetaService>()
                               ->get_object_reader_writer()
                               .get_cur_block(macro_id))) {
    } else if (OB_FAIL(mark_held_block(macro_id, mark_info, macro_id_set, tmp_status))) {
    }
    if (OB_SUCC(ret)) {
      tmp_status.mark_finished_ = true;
    }
  }
  return ret;
}

int ObBlockManager::mark_held_block(
    const MacroBlockId &macro_id, MacroBlkIdMap &mark_info,
    common::hash::ObHashSet<MacroBlockId, common::hash::NoPthreadDefendMode>
        &macro_id_set,
    ObMacroBlockMarkerStatus &tmp_status) {
  int ret = OB_SUCCESS;

  if (!macro_id.is_valid()) {
    // no block is currently held
  } else if (OB_FAIL(update_mark_info(macro_id, mark_info))) {
  } else if (OB_FAIL(
                 macro_id_set.set_refactored(macro_id, 0 /*no override*/))) {
    if (OB_HASH_EXIST != ret) {
    } else {
      ret = OB_SUCCESS;
    }
  } else {
    tmp_status.hold_count_--;
    tmp_status.reserved_block_count_++;
  }
  return ret;
}

int ObBlockManager::mark_local_storage_blocks(
    MacroBlkIdMap &mark_info,
    common::hash::ObHashSet<MacroBlockId, common::hash::NoPthreadDefendMode>
        &macro_id_set,
    ObMacroBlockMarkerStatus &tmp_status) {
  int ret = OB_SUCCESS;
  ObLocalStorageMetaService *meta_service = ::oceanbase::share::server_service<::oceanbase::storage::ObLocalStorageMetaService>();
  ObStorageMetaMemMgr *t3m = ::oceanbase::share::server_service<::oceanbase::storage::ObStorageMetaMemMgr>();
  if (OB_ISNULL(t3m) || OB_ISNULL(meta_service)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObArenaAllocator iter_allocator("MarkIter", OB_MALLOC_NORMAL_BLOCK_SIZE);
    ObTabletIterator tablet_iter(*t3m, iter_allocator, nullptr /*no op*/);
    ObTabletHandle handle;
    while (OB_SUCC(ret)) {
      handle.reset();
      iter_allocator.reuse();
      if (OB_FAIL(tablet_iter.get_next_tablet(handle))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          break;
        } else {
        }
      } else if (OB_FAIL(mark_tablet_block(mark_info, handle, macro_id_set,
                                          tmp_status))) {
      }
    }
  }
  return ret;
}

int ObBlockManager::mark_tablet_block(
    MacroBlkIdMap &mark_info, storage::ObTabletHandle &handle,
    common::hash::ObHashSet<MacroBlockId, common::hash::NoPthreadDefendMode>
        &macro_id_set,
    ObMacroBlockMarkerStatus &tmp_status) {
  int ret = OB_SUCCESS;
  const ObTablet *tablet = handle.get_obj();
  ObTabletBlockInfo block_info(tablet->get_tablet_addr().block_id(),
                               ObTabletMacroType::META_BLOCK,
                               0 /*useless param*/);

  if (tablet->get_tablet_addr().is_block() &&
      OB_FAIL(do_mark_tablet_block(block_info, mark_info, macro_id_set,
                                   tmp_status))) {
  } else if (!tablet
                  ->is_empty_shell()) { // empty shell may don't have macro info
    ObArenaAllocator allocator("MarkTabletBlock");
    ObTabletMacroInfo *macro_info = nullptr;
    bool in_memory = true;
    ObMacroInfoIterator macro_iter;
    if (OB_FAIL(tablet->load_macro_info(
                                        allocator, macro_info, in_memory))) {
    } else if (OB_FAIL(macro_iter.init(ObTabletMacroType::MAX, *macro_info))) {
    }
    while (OB_SUCC(ret)) {
      block_info.reset();
      if (OB_FAIL(macro_iter.get_next(block_info))) {
        if (OB_ITER_END != ret) {
        } else {
          ret = OB_SUCCESS;
          break;
        }
      } else if (OB_FAIL(do_mark_tablet_block(block_info, mark_info, macro_id_set, tmp_status))) {
      }
    }
    if (OB_NOT_NULL(macro_info) && !in_memory) {
      macro_info->reset();
    }
  }
  return ret;
}

int ObBlockManager::do_mark_tablet_block(
    const ObTabletBlockInfo &block_info, MacroBlkIdMap &mark_info,
    common::hash::ObHashSet<MacroBlockId, common::hash::NoPthreadDefendMode>
        &macro_id_set,
    ObMacroBlockMarkerStatus &tmp_status) {
  int ret = OB_SUCCESS;
  const MacroBlockId &macro_id = block_info.macro_id_;
  if (OB_FAIL(update_mark_info(macro_id, mark_info))) {
  } else if (OB_FAIL(macro_id_set.set_refactored(macro_id,
                                                 0 /* not overwrite */))) {
    if (OB_HASH_EXIST != ret) {
    } else {
      ret = OB_SUCCESS;
    }
  } else {
    switch (block_info.block_type_) {
    case ObTabletMacroType::META_BLOCK:
    case ObTabletMacroType::LINKED_BLOCK:
      tmp_status.index_block_count_++;
      break;
    case ObTabletMacroType::DATA_BLOCK:
    case ObTabletMacroType::SHARED_DATA_BLOCK:
      tmp_status.data_block_count_++;
      break;
    default:
      ret = OB_INVALID_ARGUMENT;
      break;
    }
    if (OB_SUCC(ret)) {
      tmp_status.hold_count_--;
    }
  }
  return ret;
}

int ObBlockManager::mark_tmp_file_blocks(
    MacroBlkIdMap &mark_info,
    common::hash::ObHashSet<MacroBlockId, common::hash::NoPthreadDefendMode>
        &macro_id_set,
    ObMacroBlockMarkerStatus &tmp_status) {
  int ret = OB_SUCCESS;

  ObArray<MacroBlockId> macro_block_list;
  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::tmp_file::ObTmpFileManager>()->get_macro_block_list(macro_block_list))) {
  } else if (OB_FAIL(update_mark_info(macro_block_list, macro_id_set, mark_info))){
  } else {
    tmp_status.tmp_file_count_ += macro_block_list.count();
    tmp_status.hold_count_ -= macro_block_list.count();
  }
  return ret;
}

int ObBlockManager::update_mark_info(
    const ObIArray<MacroBlockId> &macro_block_list,
    common::hash::ObHashSet<MacroBlockId, common::hash::NoPthreadDefendMode>
        &macro_id_set,
    MacroBlkIdMap &mark_info) {
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < macro_block_list.count(); i++) {
    const MacroBlockId &macro_id = macro_block_list.at(i);
    if (OB_FAIL(update_mark_info(macro_id, mark_info))) {
    } else if (OB_FAIL(macro_id_set.set_refactored(macro_id))) {
    }
  }
  return ret;
}

int ObBlockManager::update_mark_info(const MacroBlockId &macro_id,
                                     MacroBlkIdMap &mark_info) {
  int ret = OB_SUCCESS;
  BlockInfo block_info;
  bool can_free = false;
  if (OB_UNLIKELY(!macro_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(block_map_.get(macro_id, block_info))) { // double check.
    if (OB_ENTRY_NOT_EXIST == ret) {
      // BUG, should not happen
      LOG_ERROR("macro block is using, not exist in block map, fatal error",
                K(ret), K(macro_id), K(block_info));
    } else {
    }
  } else if (OB_UNLIKELY(block_info.ref_cnt_ < 0)) {
    LOG_ERROR("macro block should is using, ref cnt shouldn't be less than or "
              "equal to 0, "
              "fatal error",
              K(ret), K(macro_id), K(block_info));
  } else if (OB_FAIL(mark_info.get(macro_id, can_free))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
    }
  } else if (!can_free) {
    // do nothing.
  } else {
    if (OB_UNLIKELY(0 == block_info.ref_cnt_)) {
      // BUG, should not happen
      LOG_ERROR("macro block is using, should not mark sweep, fatal error",
                K(ret), K(macro_id), K(block_info));
    } else {
      LOG_INFO("macro block is using, and ref cnt is more than 0", K(macro_id),
               K(block_info));
    }

    if (OB_FAIL(mark_info.insert_or_update(macro_id, false))) {
    }
  }
  return ret;
}

void ObBlockManager::MarkBlockTask::runTimerTask() {
  blk_mgr_.mark_and_sweep();
  (void)blk_mgr_.extend_file_size_if_need(); // auto extend
}

// 2 days
const int64_t ObBlockManager::InspectBadBlockTask::ACCESS_TIME_INTERVAL =
    2 * 86400 * 1000000ull;
// block count inspected per round
const int64_t ObBlockManager::InspectBadBlockTask::MIN_OPEN_BLOCKS_PER_ROUND =
    1;
// max search number per round
const int64_t ObBlockManager::InspectBadBlockTask::MAX_SEARCH_COUNT_PER_ROUND =
    1000;

ObBlockManager::InspectBadBlockTask::InspectBadBlockTask(
    ObBlockManager &blk_mgr)
    : blk_mgr_(blk_mgr), last_macro_idx_(-1), last_check_time_(0) {}

ObBlockManager::InspectBadBlockTask::~InspectBadBlockTask() { reset(); }

void ObBlockManager::InspectBadBlockTask::reset() {
  last_macro_idx_ = -1;
  last_check_time_ = 0;
}

void ObBlockManager::InspectBadBlockTask::runTimerTask() {
  const int64_t next_check_time = last_check_time_ + ACCESS_TIME_INTERVAL;
  if (next_check_time <=
      ObTimeUtility::fast_current_time()) { /* exceed 2 days */
    inspect_bad_block();
    if (last_macro_idx_ == -1) { // finished
      last_check_time_ = ObTimeUtility::fast_current_time();
    }
  } else {
    LOG_INFO("skip inspect bad block", K_(last_check_time), K_(last_macro_idx));
  }
}

int ObBlockManager::InspectBadBlockTask::check_block(
    ObMacroBlockHandle &macro_block_handle) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!macro_block_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const MacroBlockId &macro_id = macro_block_handle.get_macro_id();
    ObMacroBlockReadInfo read_info;
    read_info.io_timeout_ms_ = std::max(config::_data_storage_io_timeout() / 1000,
                                        DEFAULT_IO_WAIT_TIME_MS);
    read_info.macro_block_id_ = macro_id;
    read_info.offset_ = 0;
    read_info.size_ = OB_STORAGE_OBJECT_MGR.get_macro_block_size();
    read_info.io_desc_.set_sys_module_id(ObIOModule::INSPECT_BAD_BLOCK_IO);

    if (OB_FAIL(ObBlockManager::async_read_block(read_info,
                                                        macro_block_handle))) {
    } else if (OB_FAIL(macro_block_handle.wait())) {
    } else if (macro_block_handle.get_data_size() != read_info.size_) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ObSSTableMacroBlockChecker::check(
                   macro_block_handle.get_buffer(), read_info.size_,
                   ObMacroBlockCheckLevel::CHECK_LEVEL_PHYSICAL))) {
      LOG_ERROR("fail to check sstable macro block", K(ret), K(macro_id),
                KP(macro_block_handle.get_buffer()), K(read_info.size_));
      char error_msg[common::OB_MAX_ERROR_MSG_LEN];
      char macro_id_str[128];
      MEMSET(error_msg, 0, sizeof(error_msg));
      MEMSET(macro_id_str, 0, sizeof(macro_id_str));
      int tmp_ret = OB_SUCCESS;
      macro_id.to_string(macro_id_str, sizeof(macro_id_str));
      if (OB_SUCCESS != (tmp_ret = databuff_printf(
                             error_msg, sizeof(error_msg),
                             "Bad data block: macro id=%s", macro_id_str))) {
      } else if (OB_SUCCESS !=
                 (tmp_ret = blk_mgr_.report_bad_block(macro_id, ret, error_msg,
                                                      config::data_dir().c_str()))) {
      } else {
        ret = OB_SUCCESS; // after report bad block, overwrite ret code and
                          // continue to check.
      }
    }
  }
  return ret;
}

int ObBlockManager::extend_file_size_if_need() {
  if (IS_NOT_INIT) { return OB_NOT_INIT; }
  if (!is_mark_sweep_enabled()) { return OB_SUCCESS; }
  storage_io::BlockGrowth policy{};
  const int ret = make_growth_policy(policy);
  return ret == OB_SUCCESS
      ? convert_block_status(data_file_->extend_blocks(policy)) : ret;
}

static inline int64_t get_disk_allowed_iops(const int64_t macro_block_size) {
  const int64_t max_bkgd_band_width = 64 * 1024 * 1024;
  const int64_t max_check_iops = max_bkgd_band_width / macro_block_size;
  return max_check_iops;
}

void ObBlockManager::InspectBadBlockTask::inspect_bad_block() {
  int ret = OB_SUCCESS;
  const int64_t macro_block_size = OB_STORAGE_OBJECT_MGR.get_macro_block_size();
  const int64_t verify_cycle = config::builtin_db_data_verify_cycle();
  const int64_t sec_per_day = 24 * 3600;
  const int64_t check_times_per_day =
      sec_per_day * 1000 * 1000 / ObBlockManager::INSPECT_DELAY_US;
  ObArray<MacroBlockId> macro_ids;
  GetAllMacroBlockIdFunctor getter(macro_ids);

  if (OB_UNLIKELY(verify_cycle <= 0)) {
    // macro block inspection is disabled, do nothing
  } else if (OB_UNLIKELY(!blk_mgr_.is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(macro_ids.reserve(blk_mgr_.block_map_.count()))) {
  } else if (OB_FAIL(blk_mgr_.block_map_.for_each(getter))) {
  } else if (OB_UNLIKELY(0 == macro_ids.size())) {
    // nothing to do.
  } else {
    const int64_t total_used_macro_block_count = macro_ids.size();
    const int64_t check_blk_cnt_per_day = std::max(
        MIN_OPEN_BLOCKS_PER_ROUND, total_used_macro_block_count / verify_cycle);
    const int64_t blk_cnt_per_round =
        check_blk_cnt_per_day / check_times_per_day;
    const int64_t search_num_per_round =
        0 == blk_cnt_per_round ? MIN_OPEN_BLOCKS_PER_ROUND : blk_cnt_per_round;
    const int64_t disk_allowed_iops = get_disk_allowed_iops(macro_block_size);
    const int64_t max_check_count_per_round =
        std::min(search_num_per_round,
                 std::max(MIN_OPEN_BLOCKS_PER_ROUND, disk_allowed_iops));
    const int64_t inspect_timeout_us =
        std::max(config::_data_storage_io_timeout() * 1,
                 max_check_count_per_round * DEFAULT_IO_WAIT_TIME_MS * 1000);
    const int64_t begin_time = ObTimeUtility::current_time();
    int64_t check_count = 0;

    for (int64_t i = 0;
         i < MAX_SEARCH_COUNT_PER_ROUND &&
         check_count < max_check_count_per_round &&
         (ObTimeUtility::current_time() - begin_time) < inspect_timeout_us;
         ++i) {
      ++last_macro_idx_;
      if (last_macro_idx_ >= total_used_macro_block_count) {
        last_macro_idx_ = -1; // finished this round
        break;
      }

      const MacroBlockId &macro_id = macro_ids.at(last_macro_idx_);
      ObMacroBlockInfo block_info;
      ObMacroBlockHandle macro_block_handle;
      if (OB_FAIL(blk_mgr_.get_macro_block_info(macro_id, block_info,
                                                macro_block_handle))) {
      } else if (OB_UNLIKELY(block_info.is_free_)) {
        // do nothing, this MacroBlock has been released. skip this MacroBlock
        // and continue.
      } else if (!block_info.is_free_ && block_info.ref_cnt_ > 0
#ifdef ERRSIM
                 && (begin_time - block_info.access_time_) >
                        static_cast<int64_t>(10_s)) {
        LOG_INFO("errsim bad block: start check macro block", K(block_info));
#else
                 && (begin_time - block_info.access_time_) >
                        ACCESS_TIME_INTERVAL) {
#endif
        ++check_count;
        LOG_INFO("check macro block", K(block_info), "time_interval",
                 begin_time - block_info.access_time_);
        if (OB_FAIL(check_block(macro_block_handle))) {
        }
      }
    }
    if (REACH_COUNT_INTERVAL(60)) { // print log per 60 times.
      const int64_t cost_time = ObTimeUtility::current_time() - begin_time;
      LOG_INFO("inspect bad block cost time", K(cost_time), K(check_count),
               K(total_used_macro_block_count), K(max_check_count_per_round));
    }
  }
}

ObServerBlockManager &ObServerBlockManager::get_instance() {
  static ObServerBlockManager instance_;
  return instance_;
}

} // namespace blocksstable
} // namespace oceanbase
