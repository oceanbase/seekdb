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

#define USING_LOG_PREFIX STORAGE_COMPACTION
#include "ob_compaction_diagnose.h"
#include "lib/alloc/alloc_func.h"
#include "share/rc/ob_server_runtime.h"
#include "ob_compaction_progress.h"
#include "data_plane/report/ob_tablet_report.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/compaction/ob_schedule_tablet_func.h"
#include "storage/compaction/ob_medium_compaction_func.h"

namespace oceanbase
{
using namespace storage;
using namespace share;
using namespace common;
using namespace common::hash;

namespace compaction
{
/*
 * ObScheduleSuspectInfo implement
 * */
int64_t ObScheduleSuspectInfo::hash() const
{
  return ObMergeDagHash::inner_hash();
}

bool ObScheduleSuspectInfo::is_valid() const
{
  bool bret = true;
  if (OB_UNLIKELY(!is_valid_merge_type(merge_type_)
      || !tablet_id_.is_valid())) {
    bret = false;
  }
  return bret;
}

void ObScheduleSuspectInfo::shallow_copy(ObIDiagnoseInfo *other)
{
  ObScheduleSuspectInfo *info = nullptr;
  if (OB_NOT_NULL(other) && OB_NOT_NULL(info = static_cast<ObScheduleSuspectInfo *>(other))) {
    merge_type_ = info->merge_type_;
    tablet_id_ = info->tablet_id_;
    priority_ = info->priority_;
    add_time_ = info->add_time_;
    hash_ = info->hash_;
  }
}

int64_t ObScheduleSuspectInfo::get_hash() const
{
  return hash_;
}

/*
 * ObIDiagnoseInfoIter implement
 * */
int ObIDiagnoseInfoMgr::Iterator::open(const uint64_t version, ObIDiagnoseInfo *current_info, ObIDiagnoseInfoMgr *info_pool)
{
  int ret = OB_SUCCESS;
  if (is_opened_) {
    ret = OB_OPEN_TWICE;
    STORAGE_LOG(WARN, "iterator is opened", K(ret));
  } else if (OB_ISNULL(current_info) || OB_ISNULL(info_pool)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), KP(current_info), KP(info_pool));
  } else {
    version_ = version;
    current_info_ = current_info;
    info_pool_ = info_pool;
    seq_num_ = 1; // header
    is_opened_ = true;
  }
  return ret;
}

int ObIDiagnoseInfoMgr::Iterator::get_next(ObIDiagnoseInfo *out_info, char *buf, const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  if (!is_opened_) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObIDiagnoseInfoIter is not init", K(ret));
  } else if (OB_ISNULL(out_info)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(out_info));
  } else {
    common::SpinRLockGuard RLockGuard(info_pool_->rwlock_);
    while (OB_SUCC(next())) {
      // (current_info_->seq_num_ <= seq_num_) means info has been visited
      if (current_info_->seq_num_ > seq_num_ && !current_info_->is_deleted()) {
        seq_num_ = current_info_->seq_num_;
        out_info->shallow_copy(current_info_);
        if (OB_ISNULL(buf)) {
          // do nothing // allow
        } else if (OB_NOT_NULL(current_info_->info_param_)) {
          if (OB_FAIL(current_info_->info_param_->fill_comment(buf, buf_len))) {
          }
        }
        break;
      }
    }
  }
  return ret;
}

int ObIDiagnoseInfoMgr::Iterator::next()
{
  int ret = OB_SUCCESS;
  if (version_ < info_pool_->version_) {
    // version changed, which means some infos have been purged, the current_info_ maybe invalid ptr
    version_ = info_pool_->version_;
    current_info_ = info_pool_->info_list_.get_header();
  } else if (version_ > info_pool_->version_) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "Unexpected version value", K(ret), "iter_version", version_,
        "pool_version", info_pool_->version_);
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(current_info_)) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "Unexpect value", K(ret), K(current_info_));
    } else if (0 == seq_num_) {
      // guarantee idempotency
      ret = OB_ITER_END;
    } else if (OB_ISNULL(current_info_ = current_info_->get_next())) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "failed to next", K(ret), K(current_info_));
    } else if (current_info_ == info_pool_->info_list_.get_header()) {
      // to ignore the version_ changing
      ret = OB_ITER_END;
      seq_num_ = 0; // tail
    }
  }
  return ret;
}
/*
 * ObIDiagnoseInfoMgr implement
 * */
void ObIDiagnoseInfoMgr::add_compaction_info_param(char *buf, const int64_t buf_size, const char* str)
{
  int64_t pos = strlen(buf);
  if (0 > pos || buf_size <= pos) {
  } else {
    int len = snprintf(buf + pos, buf_size - pos, "%s", str);
    if (OB_UNLIKELY(len < 0)) {
    } else if (OB_LIKELY(len < buf_size - pos)) {
      pos += len;
    } else {
      pos = buf_size - 1;  //skip '\0'
    }
    buf[pos] = '\0';
  }
}

int ObIDiagnoseInfoMgr::init(bool with_map,
           const char* basic_label,
           const int64_t page_size,
           int64_t max_size)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "ObIDiagnoseInfoMgr has already been initiated", K(ret));
  } else if (OB_ISNULL(basic_label)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(basic_label));
  } else {
    (void)snprintf(pool_label_, sizeof(pool_label_), "%s%s", basic_label, "Mgr");
    page_size_ = std::max(page_size, static_cast<int64_t>(INFO_PAGE_SIZE_LIMIT));
    max_size = upper_align(max_size, page_size_);
    if (OB_FAIL(allocator_.init(lib::ObMallocAllocator::get_instance(),
                                    page_size_,
                                    lib::ObMemAttr(pool_label_),
                                    0,
                                    max_size,
                                    max_size))) {
    } else if (with_map) {
      (void)snprintf(bucket_label_, sizeof(bucket_label_), "%s%s", basic_label, "Bkt");
      (void)snprintf(node_label_, sizeof(node_label_), "%s%s", basic_label, "Node");
      if (OB_FAIL(info_map_.create(INFO_BUCKET_LIMIT, bucket_label_, node_label_))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    version_ = 1;
    seq_num_ = 1;
    is_inited_ = true;
  } else {
    reset();
  }
  return ret;
}

void ObIDiagnoseInfoMgr::destroy()
{
  if (IS_INIT) {
    reset();
  }
}
void ObIDiagnoseInfoMgr::reset()
{
  common::SpinWLockGuard guard(lock_);
  common::SpinWLockGuard WLockGuard(rwlock_);
  clear_with_no_lock();
  if (info_map_.created()) {
    info_map_.destroy();
  }
  allocator_.reset();
  is_inited_ = false;
}

void ObIDiagnoseInfoMgr::clear()
{
  if (IS_INIT) {
    common::SpinWLockGuard guard(lock_);
    common::SpinWLockGuard WLockGuard(rwlock_);
    clear_with_no_lock();
  }
}

void ObIDiagnoseInfoMgr::clear_with_no_lock()
{
  if (info_map_.created()) {
    info_map_.clear();
  }
  DLIST_FOREACH_REMOVESAFE_NORET(iter, info_list_) {
    info_list_.remove(iter);
    if (allocator_.is_inited()) {
      iter->destroy(allocator_);
    }
  }
  info_list_.clear();
  version_ = 1;
  seq_num_ = 1;
}

int ObIDiagnoseInfoMgr::size()
{
  common::SpinRLockGuard guard(lock_);
  return info_list_.get_size();
}

int ObIDiagnoseInfoMgr::get_with_param(const int64_t key, ObIDiagnoseInfo &out_info, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObIDiagnoseInfoMgr is not init", K(ret));
  } else {
    common::SpinWLockGuard guard(lock_);
    ObIDiagnoseInfo *info = NULL;
    if (OB_FAIL(get_with_no_lock(key, info))) {
      if (OB_HASH_NOT_EXIST != ret) {
        STORAGE_LOG(WARN, "failed to get info from map", K(ret), K(key));
      }
    } else if (OB_ISNULL(info->info_param_)) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "info_param is null", K(ret), K(info));
    } else {
      out_info.shallow_copy(info/*src*/);
      if (OB_FAIL(info->info_param_->deep_copy(allocator, out_info.info_param_/*dst*/))) {
      }
    }
  }
  return ret;
}

int ObIDiagnoseInfoMgr::delete_info(const int64_t key)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObIDiagnoseInfoMgr is not init", K(ret));
  } else {
    common::SpinWLockGuard guard(lock_);
    if (OB_FAIL(del_with_no_lock(key, nullptr))) {
      if (OB_HASH_NOT_EXIST != ret) {
        STORAGE_LOG(WARN, "failed to delete info", K(ret));
      }
    }
  }
  return ret;
}

int ObIDiagnoseInfoMgr::set_max(const int64_t size)
{
  int ret = OB_SUCCESS;
  int64_t max_size = upper_align(size, page_size_);
  common::SpinWLockGuard guard(lock_);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObIDiagnoseInfoMgr is not init", K(ret));
  } else if (OB_FAIL(allocator_.set_max(max_size, true))) {
  } else if (allocator_.total() <= allocator_.get_max()) {
  } else if (OB_FAIL(purge_with_rw_lock())) {
  }
  return ret;
}

int ObIDiagnoseInfoMgr::gc_info()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObIDiagnoseInfoMgr is not init", K(ret));
  } else {
    common::SpinWLockGuard guard(lock_);
    if ((allocator_.used() * 1.0) / allocator_.get_max() >= (GC_HIGH_PERCENTAGE * 1.0 / 100)) {
      if (OB_FAIL(purge_with_rw_lock())) {
      }
    }
  }
  return ret;
}

int ObIDiagnoseInfoMgr::open_iter(Iterator &iter)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObIDiagnoseInfoMgr is not init", K(ret));
  } else {
    common::SpinRLockGuard guard(rwlock_);
    if (OB_FAIL(iter.open(version_, info_list_.get_header(), this))) {
    }
  }
  return ret;
}

int ObIDiagnoseInfoMgr::add_with_no_lock(const int64_t key, ObIDiagnoseInfo *info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(info)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret));
  } else if (!info_list_.add_last(info)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "failed to add into info list", K(ret));
  } else if (info_map_.created()) {
    if (OB_FAIL(info_map_.set_refactored(key, info))) {
      STORAGE_LOG(WARN, "failed to set info into map", K(ret), K(key));
      if (OB_ISNULL(info_list_.remove(info))) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(ERROR, "failed to remove info from list", K(ret));
        // unexpected
        ob_abort();
      }
    }
  }

  if (OB_SUCC(ret)) {
    info->seq_num_ = ++seq_num_;
  } else if (OB_NOT_NULL(info)) {
    info->destroy(allocator_);
    info = nullptr;
  }
  return ret;
}

int ObIDiagnoseInfoMgr::del_with_no_lock(const int64_t key, ObIDiagnoseInfo *info)
{
  int ret = OB_SUCCESS;
  if (info_map_.created()) {
    ObIDiagnoseInfo *old_info = nullptr;
    if (OB_FAIL(info_map_.get_refactored(key, old_info))) {
      if (OB_HASH_NOT_EXIST != ret) {
        STORAGE_LOG(WARN, "failed to get info from map", K(ret), K(key), K(old_info));
      }
    } else if (nullptr != info && info->priority_ < old_info->priority_) {
      ret = OB_HASH_EXIST;
      STORAGE_LOG(INFO, "failed to del old info cause priority", K(ret),
          "old_priority", old_info->priority_, "new_priority", info->priority_);
    } else if (OB_FAIL(info_map_.erase_refactored(key))) {
    }
    if (OB_SUCC(ret) && OB_NOT_NULL(old_info)) {
      old_info->set_deleted();
      if (OB_NOT_NULL(info)) {
        info->update(old_info);
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "info map is not created", K(ret));
  }
  return ret;
}

int ObIDiagnoseInfoMgr::get_with_no_lock(const int64_t key, ObIDiagnoseInfo *&info)
{
  int ret = OB_SUCCESS;
  info = NULL;
  if (info_map_.created()) {
    if (OB_FAIL(info_map_.get_refactored(key, info))) {
      if (OB_HASH_NOT_EXIST != ret) {
        STORAGE_LOG(WARN, "failed to get info from map", K(ret), K(key));
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "info map is not created", K(ret));
  }
  return ret;
}

int ObIDiagnoseInfoMgr::purge_with_rw_lock(bool batch_purge)
{
  int ret = OB_SUCCESS;
  int64_t purge_count = 0;
  common::SpinWLockGuard WLockGuard(rwlock_);
  int batch_size = info_list_.get_size() / MAX_ALLOC_RETRY_TIMES;
  batch_size = std::max(batch_size, 10);
  DLIST_FOREACH_REMOVESAFE(iter, info_list_) {
    if (info_map_.created() && !iter->is_deleted()) {
      if (OB_FAIL(info_map_.erase_refactored(iter->get_hash()))) {
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(info_list_.remove(iter))) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(ERROR, "failed to remove info from list", K(ret));
        // unexpected
        ob_abort();
      }
      iter->destroy(allocator_);
      iter = nullptr;
      ++purge_count;
    }

    if (batch_purge && purge_count == batch_size) {
      break;
    } else if (!batch_purge && allocator_.total() <= allocator_.get_max() &&
        ((allocator_.used() * 1.0) / allocator_.get_max()) <= (GC_LOW_PERCENTAGE * 1.0 / 100)) {
      break;
    }
  }

  if (OB_SUCC(ret)) {
    STORAGE_LOG(INFO, "success to purge", K(ret), K(batch_purge), K(batch_size), "max_size", allocator_.get_max(),
      "used_size", allocator_.used(), "total_size", allocator_.total(), K(purge_count), K(info_list_.get_size()));
  }
  ++version_;
  return ret;
}
/*
 * ObScheduleSuspectInfoMgr implement
 * */
int ObScheduleSuspectInfoMgr::server_module_init(ObScheduleSuspectInfoMgr *&schedule_suspect_info)
{
  int64_t max_size = cal_max();
  return schedule_suspect_info->init(true, "SuspectInfo", INFO_PAGE_SIZE, max_size);
}

int64_t ObScheduleSuspectInfoMgr::cal_max()
{
  
  int64_t max_size = std::min(static_cast<int64_t>(lib::get_memory_budget() / 100 * MEMORY_PERCENTAGE),
                           static_cast<int64_t>(POOL_MAX_SIZE));
  return max_size;
}

int ObScheduleSuspectInfoMgr::add_suspect_info(const int64_t key, ObScheduleSuspectInfo &input_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "ObScheduleSuspectInfoMgr is not init", K(ret));
  } else if (OB_ISNULL(input_info.info_param_)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument. info param is null", K(ret));
  } else if (OB_FAIL((alloc_and_add(key, &input_info)))) {
  }
  return ret;
}
/*
 * ObCompactionDiagnose implement
 * */

#define ADD_DIAGNOSE_INFO(merge_type, tablet_id, status, time, ...) \
SET_DIAGNOSE_INFO((can_add_diagnose_info() ? &info_array_[idx_++] : NULL), normal_, merge_type, tablet_id, status, time, __VA_ARGS__)
#define ADD_DIAGNOSE_INFO_FOR_TABLET(merge_type, status, time, ...) \
ADD_DIAGNOSE_INFO(merge_type, tablet_id, status, time, __VA_ARGS__)
#define ADD_COMMON_DIAGNOSE_INFO(merge_type, status, time, ...) \
ADD_DIAGNOSE_INFO(merge_type, UNKNOW_TABLET_ID, status, time, __VA_ARGS__)

#define ADD_MAJOR_WAIT_SCHEDULE(time, info) \
  if (ObTimeUtility::current_time_ns() > time) { \
    if (DIAGNOSE_TABELT_MAX_COUNT > diagnose_tablet_count_[COMPACTION_DIAGNOSE_MAJOR_NOT_SCHEDULE] \
      && OB_TMP_FAIL(ADD_DIAGNOSE_INFO_FOR_TABLET( \
          MAJOR_MERGE, \
          gen_diagnose_status(compaction_scn), \
          ObTimeUtility::fast_current_time(), \
          "major not schedule for long time", info, \
          "max_receive_medium_snapshot", max_sync_medium_scn, \
          "compaction_scn", compaction_scn, \
          "tablet_snapshot", tablet.get_snapshot_version(), \
          "last_major_scn", last_major_snapshot_version))) { \
      LOG_WARN("failed to add diagnose info", K(ret), K(tablet_id)); \
    } \
    ++diagnose_tablet_count_[COMPACTION_DIAGNOSE_MAJOR_NOT_SCHEDULE]; \
  }

#define ADD_MEDIUM_WAIT_SCHEDULE(time, info) \
  if (ObTimeUtility::current_time_ns() > time) { \
    if (DIAGNOSE_TABELT_MAX_COUNT > diagnose_tablet_count_[COMPACTION_DIAGNOSE_MEDIUM_NOT_SCHEDULE] \
      && OB_TMP_FAIL(ADD_DIAGNOSE_INFO_FOR_TABLET( \
          MEDIUM_MERGE, \
          gen_diagnose_status(max_sync_medium_scn), \
          ObTimeUtility::fast_current_time(), \
          "medium not schedule for long time", info,\
          "max_receive_medium_scn", max_sync_medium_scn, \
          "tablet_snapshot", tablet.get_snapshot_version(), \
          "last_major_scn", last_major_snapshot_version))) { \
      LOG_WARN("failed to add diagnose info", K(ret), K(tablet_id)); \
    } \
    ++diagnose_tablet_count_[COMPACTION_DIAGNOSE_MEDIUM_NOT_SCHEDULE]; \
  }

const char *ObCompactionDiagnoseInfo::ObDiagnoseStatusStr[DIA_STATUS_MAX] = {
    "NOT_SCHEDULE",
    "RUNNING",
    "WARN",
    "FAILED",
    "RS_UNCOMPACTED",
    "SPECIAL"
};

const char * ObCompactionDiagnoseInfo::get_diagnose_status_str(ObDiagnoseStatus status)
{
  STATIC_ASSERT(DIA_STATUS_MAX == ARRAYSIZEOF(ObDiagnoseStatusStr), "diagnose status str len is mismatch");
  const char *str = "";
  if (status >= DIA_STATUS_MAX || status < DIA_STATUS_NOT_SCHEDULE) {
    str = "invalid_status";
  } else {
    str = ObDiagnoseStatusStr[status];
  }
  return str;
}

const char *ObCompactionDiagnoseMgr::ObCompactionDiagnoseTypeStr[COMPACTION_DIAGNOSE_TYPE_MAX] = {
    "MEDIUM_NOT_SCHEDULE",
    "MAJOR_NOT_SCHEDULE"
};

const char * ObCompactionDiagnoseMgr::get_compaction_diagnose_type_str(ObCompactionDiagnoseType type)
{
  STATIC_ASSERT(COMPACTION_DIAGNOSE_TYPE_MAX == ARRAYSIZEOF(ObCompactionDiagnoseTypeStr), "diagnose type str len is mismatch");
  const char *str = "";
  if (type >= COMPACTION_DIAGNOSE_TYPE_MAX || type < COMPACTION_DIAGNOSE_MEDIUM_NOT_SCHEDULE) {
    str = "invalid_status";
  } else {
    str = ObCompactionDiagnoseTypeStr[type];
  }
  return str;
}

ObMergeType ObCompactionDiagnoseMgr::get_compaction_diagnose_merge_type(ObCompactionDiagnoseType type)
{
  ObMergeType merge_type = INVALID_MERGE_TYPE;
  if (COMPACTION_DIAGNOSE_MEDIUM_NOT_SCHEDULE == type) {
    merge_type = MEDIUM_MERGE;
  } else if (COMPACTION_DIAGNOSE_MAJOR_NOT_SCHEDULE == type) {
    merge_type = MAJOR_MERGE;
  }
  return merge_type;
}

ObCompactionDiagnoseMgr::ObCompactionDiagnoseMgr()
 : is_inited_(false),
   normal_(true),
   info_array_(nullptr),
   max_cnt_(0),
   idx_(0)
  {
    MEMSET(suspect_tablet_count_, 0, sizeof(suspect_tablet_count_));
    MEMSET(suspect_merge_type_, -1, sizeof(suspect_merge_type_));
    MEMSET(diagnose_tablet_count_, 0, sizeof(diagnose_tablet_count_));
  }

void ObCompactionDiagnoseMgr::reset()
{
  info_array_ = nullptr;
  max_cnt_ = 0;
  idx_ = 0;
  is_inited_ = false;
  normal_ = true;
  MEMSET(suspect_tablet_count_, 0, sizeof(suspect_tablet_count_));
  MEMSET(suspect_merge_type_, -1, sizeof(suspect_merge_type_));
  MEMSET(diagnose_tablet_count_, 0, sizeof(diagnose_tablet_count_));
}

int ObCompactionDiagnoseMgr::init(
    common::ObIAllocator *allocator,
    ObCompactionDiagnoseInfo *info_array,
    const int64_t max_cnt)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "ObCompactionDiagnoseMgr has already been initiated", K(ret));
  } else if (OB_UNLIKELY(nullptr == info_array || max_cnt <= 0 || nullptr == allocator)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(info_array), K(max_cnt));
  } else {
    info_array_ = info_array;
    max_cnt_ = max_cnt;
    is_inited_ = true;
  }
  if (!is_inited_) {
    reset();
  }
  return ret;
}

int ObCompactionDiagnoseMgr::diagnose_dag(
    compaction::ObMergeType merge_type,
    ObTabletID tablet_id,
    const int64_t merge_version,
    ObTabletMergeDag &dag,
    ObDiagnoseTabletCompProgress &progress)
{
  int ret = OB_SUCCESS;
  // create a fake dag to get compaction progress
  ObTabletMergeDagParam param;
  param.merge_type_ = merge_type;
  param.merge_version_ = merge_version;
  param.tablet_id_ = tablet_id;
  param.skip_get_tablet_ = true;
  param.is_reserve_mode_ = false;

  if (OB_FAIL(dag.init_by_param(&param))) {
  } else if (is_minor_merge(merge_type)) {
    if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::ObDagScheduler>()->diagnose_minor_exe_dag(&dag, progress))) {
      if (OB_HASH_NOT_EXIST != ret) {
        STORAGE_LOG(WARN, "failed to diagnose minor execute dag", K(ret), K(tablet_id), K(progress));
      }
    }
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::ObDagScheduler>()->diagnose_dag(&dag, progress))) {
    if (OB_HASH_NOT_EXIST != ret) {
      STORAGE_LOG(WARN, "failed to diagnose dag", K(ret), K(tablet_id), K(progress));
    }
  }
  if (OB_HASH_NOT_EXIST == ret) {
  }
  return ret;
}

int ObCompactionDiagnoseMgr::diagnose_all_tablets()
{
  int ret = OB_SUCCESS;
  {
    {
      SERVER_MODULE_SCOPE {
        (void) diagnose_database_tablets(); // storage side
        (void) diagnose_database_major_merge(); // root-service side
        (void) diagnose_count_info();
        (void) diagnose_existing_runtime_meta_update_task();
      } else {
        if (OB_SERVER_RUNTIME_NOT_READY != ret) {
          STORAGE_LOG(WARN, "enter server module scope failed", K(ret));
        } else {
          ret = OB_SUCCESS;
          continue;
        }
      }
    }
  }
  return ret;
}

int ObCompactionDiagnoseMgr::get_and_set_suspect_info(
    const ObMergeType merge_type,
    const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObScheduleSuspectInfo ret_info;
  char tmp_str[common::OB_DIAGNOSE_INFO_LENGTH] = "\0";
  share::ObSuspectInfoType suspect_info_type;
  
  if (OB_FAIL(get_suspect_info(merge_type, tablet_id, ret_info, suspect_info_type, tmp_str, sizeof(tmp_str)))) {
    if (OB_HASH_NOT_EXIST != ret) {
      LOG_WARN("failed get suspect info", K(ret), K(tablet_id));
    }
  } else if (OB_FAIL(ADD_DIAGNOSE_INFO_FOR_TABLET(
                merge_type,
                ObCompactionDiagnoseInfo::DIA_STATUS_FAILED, // TODO(@jingshui): use status by priority
                ret_info.add_time_,
                "schedule_suspect_info", tmp_str))) {
  }
  return ret;
}

int ObCompactionDiagnoseMgr::get_suspect_info(
    const ObMergeType merge_type,
    const ObTabletID &tablet_id,
    ObScheduleSuspectInfo &ret_info,
    share::ObSuspectInfoType &suspect_info_type,
    char *buf,
    const int64_t buf_len)
{
  int ret = OB_SUCCESS;
  suspect_info_type = share::ObSuspectInfoType::SUSPECT_INFO_TYPE_MAX;
  ObScheduleSuspectInfo input_info;
  input_info.merge_type_ = merge_type;
  input_info.tablet_id_ = tablet_id;
  ObInfoParamBuffer allocator; // info_param_ will be invalid after return
  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::compaction::ObScheduleSuspectInfoMgr>()->get_with_param(input_info.hash(), ret_info, allocator))) {
    if (OB_HASH_NOT_EXIST != ret) {
      LOG_WARN("failed to get suspect info", K(ret), K(input_info));
    }
  } else if (OB_FAIL(ret_info.info_param_->fill_comment(buf, buf_len))) {
  } else {
    suspect_info_type = ret_info.info_param_->type_.suspect_type_;
    ret_info.info_param_ = nullptr;
  }
  return ret;
}

int ObCompactionDiagnoseMgr::diagnose_database(
    bool &diagnose_major_flag,
    int64_t &compaction_scn)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  const int64_t merged_version = MERGE_SCHEDULER_PTR->get_inner_table_merged_scn();

  // major compaction is not finished, need to diagnose
  if (compaction_scn > merged_version) {
    diagnose_major_flag = true;

    // step 1: check common suspect info
    (void) get_and_set_suspect_info(MEDIUM_MERGE, UNKNOW_TABLET_ID);

    // step 2: check if major compaction is paused
    if (!MERGE_SCHEDULER_PTR->could_major_merge_start()) {
      ADD_COMMON_DIAGNOSE_INFO(!MERGE_SCHEDULER_PTR->could_major_merge_start() ? MAJOR_MERGE : MEDIUM_MERGE,
                               ObCompactionDiagnoseInfo::DIA_STATUS_NOT_SCHEDULE,
                               ObTimeUtility::fast_current_time(),
                               "info", "major or medium may be paused",
                               "could_major_merge", MERGE_SCHEDULER_PTR->could_major_merge_start(),
                               "runtime_status", MERGE_SCHEDULER_PTR->get_runtime_status());
    }

    // step 3: get next freeze info
    ObSEArray<share::ObFreezeInfo, 4> freeze_infos;
    if (merged_version == ObBasicMergeScheduler::INIT_COMPACTION_SCN) {
      // do nothing
    } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObFreezeInfoMgr>()->get_freeze_info_behind_major_snapshot(merged_version, false/*include_equal*/, freeze_infos))) {
      LOG_WARN("failed to get freeze info behind snapshot version", K(ret), K(merged_version));
      if (can_add_diagnose_info()
          && OB_TMP_FAIL(ADD_COMMON_DIAGNOSE_INFO(
                    MEDIUM_MERGE,
                    ObCompactionDiagnoseInfo::DIA_STATUS_FAILED,
                    ObTimeUtility::fast_current_time(),
                    "error_code", ret,
                    "freeze_info is invalid, merged_version", merged_version))) {
        LOG_ERROR("failed to add dignose info about freeze_info", K(tmp_ret), K(merged_version));
      }
    } else {
      compaction_scn = freeze_infos.at(0).frozen_scn_.get_val_for_tx();
    }
  }
  return ret;
}

void ObCompactionDiagnoseMgr::diagnose_database_ls(
    const bool diagnose_major_flag,
    const int64_t compaction_scn,
    const ObLSStatusCache &ls_status)
{
  int tmp_ret = OB_SUCCESS;
  // Check the tenant merge state derived from weak read progress.
  if (diagnose_major_flag
      && !ls_status.can_merge()
      && OB_TMP_FAIL(ADD_DIAGNOSE_INFO(
                MEDIUM_MERGE,
                UNKNOW_TABLET_ID,
                ObCompactionDiagnoseInfo::DIA_STATUS_FAILED,
                ObTimeUtility::fast_current_time(),
                "ls can't schedule merge",
                ObLSStatusCache::ls_state_to_str(ls_status.state_),
                "weak read ts",
                ls_status.weak_read_ts_.is_valid() ? ls_status.weak_read_ts_.get_val_for_tx() : -1))) {
    LOG_WARN_RET(tmp_ret, "failed to add dignose info about ls", K(tmp_ret), K(compaction_scn));
  }
  // Check suspect information for memtable freezing.
  (void) get_and_set_suspect_info(MINI_MERGE, UNKNOW_TABLET_ID);

}

void ObCompactionDiagnoseMgr::diagnose_failed_report_task(
    const ObTabletID &tablet_id,
    const int64_t compaction_scn)
{
  int tmp_ret = OB_SUCCESS;
  bool exist = false;
  bool processing = false;
  ObScheduleSuspectInfo ret_info;
  char tmp_str[common::OB_DIAGNOSE_INFO_LENGTH] = "\0";
  share::ObSuspectInfoType suspect_info_type;
  if (OB_TMP_FAIL(get_suspect_info(MEDIUM_MERGE, tablet_id, ret_info, suspect_info_type, tmp_str, sizeof(tmp_str)))) {
    LOG_WARN_RET(tmp_ret, "failed to get suspect info", K(tmp_ret), K(tablet_id));
  } else if (is_runtime_meta_update_suspect(suspect_info_type)) {
    if (OB_TMP_FAIL(data_plane::get_tablet_update_task_status(
        tablet_id, exist, processing))) {
      LOG_WARN_RET(tmp_ret, "failed to check task status", K(tmp_ret), K(tablet_id));
    }
  }

  if ((ObSuspectInfoType::SUSPECT_RUNTIME_META_UPDATE_ADD_FAILED == suspect_info_type && !exist && !processing)
      || (ObSuspectInfoType::SUSPECT_RUNTIME_META_UPDATE_PROGRESS_FAILED == suspect_info_type && (exist || processing))) {
    if (OB_TMP_FAIL(ADD_DIAGNOSE_INFO_FOR_TABLET(
                  MEDIUM_MERGE,
                  ObCompactionDiagnoseInfo::DIA_STATUS_FAILED,
                  ret_info.add_time_,
                  "compaction_scn", compaction_scn,
                  "schedule_suspect_info", tmp_str,
                  "is_waiting", exist,
                  "is_processing", processing))) {
      LOG_WARN_RET(tmp_ret, "failed to add dignose info", K(tmp_ret), K(tmp_str));
    }
  }
}

void ObCompactionDiagnoseMgr::diagnose_existing_runtime_meta_update_task()
{
  int tmp_ret = OB_SUCCESS;
  ObSEArray<data_plane::ObTabletUpdateTaskInfo, MAX_RUNTIME_META_TASK_DIAGNOSE_CNT> waiting_tasks;
  ObSEArray<data_plane::ObTabletUpdateTaskInfo, MAX_RUNTIME_META_TASK_DIAGNOSE_CNT> processing_tasks;
  if (OB_TMP_FAIL(data_plane::get_stalled_tablet_update_tasks(
      waiting_tasks, processing_tasks))) {
    LOG_WARN_RET(tmp_ret, "fail to diagnose existing task", K(tmp_ret));
  } else {
    FOREACH(iter, waiting_tasks) {
      if (OB_TMP_FAIL(ADD_DIAGNOSE_INFO(
                        MEDIUM_MERGE,
                        iter->tablet_id_,
                        ObCompactionDiagnoseInfo::DIA_STATUS_FAILED,
                        ObTimeUtility::fast_current_time(),
                        "runtime metadata update task waiting for a long time: add_time", iter->add_timestamp_))) {
        LOG_WARN_RET(tmp_ret, "failed to add dignose info", K(tmp_ret), K(*iter));                  
      }
    }
    FOREACH(iter, processing_tasks) {
      if (OB_TMP_FAIL(ADD_DIAGNOSE_INFO(
                        MEDIUM_MERGE,
                        iter->tablet_id_,
                        ObCompactionDiagnoseInfo::DIA_STATUS_FAILED,
                        ObTimeUtility::fast_current_time(),
                        "runtime metadata update task processing for a long time: add_time", iter->add_timestamp_,
                        "start_time", iter->start_timestamp_))) {
        LOG_WARN_RET(tmp_ret, "failed to add dignose info", K(tmp_ret), K(*iter));                  
      }
    }
  }
}

void ObCompactionDiagnoseMgr::diagnose_count_info()
{
  int tmp_ret = OB_SUCCESS;
  for (int64_t i = 0; i < share::ObSuspectInfoType::SUSPECT_INFO_TYPE_MAX; ++i) {
    if (suspect_tablet_count_[i] > DIAGNOSE_TABELT_MAX_COUNT) {
      if (OB_TMP_FAIL(ADD_COMMON_DIAGNOSE_INFO(
            suspect_merge_type_[i],
            ObCompactionDiagnoseInfo::DIA_STATUS_SPECIAL,
            ObTimeUtility::fast_current_time(),
            "schedule_suspect_info type", OB_SUSPECT_INFO_TYPES[i].info_str,
            "count of tablets with the same problem", suspect_tablet_count_[i]))) {
        LOG_WARN_RET(tmp_ret, "failed to add diagnose info", K(tmp_ret));
      }
    }
  }
  for (int64_t i = 0; i < COMPACTION_DIAGNOSE_TYPE_MAX; ++i) {
    if (diagnose_tablet_count_[i] > DIAGNOSE_TABELT_MAX_COUNT) {
      if (OB_TMP_FAIL(ADD_COMMON_DIAGNOSE_INFO(
            get_compaction_diagnose_merge_type(ObCompactionDiagnoseType(i)),
            ObCompactionDiagnoseInfo::DIA_STATUS_SPECIAL,
            ObTimeUtility::fast_current_time(),
            "diagnose info type", get_compaction_diagnose_type_str(ObCompactionDiagnoseType(i)),
            "count of tablets with the same problem", diagnose_tablet_count_[i]))) {
        LOG_WARN_RET(tmp_ret, "failed to add diagnose info", K(tmp_ret));
      }
    }
  }
}

int ObCompactionDiagnoseMgr::diagnose_database_tablets()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObCompactionDiagnoseMgr is not init", K(ret));
  } else {
    // collect compaction dags whose running time exceed 90mins and add them to diagnose_tablet_map
    if (OB_TMP_FAIL(::oceanbase::share::server_service<::oceanbase::share::ObDagScheduler>()->diagnose_all_compaction_dags())) {
    }
    bool diagnose_major_flag = false;
    int64_t compaction_scn = MAX(MERGE_SCHEDULER_PTR->get_frozen_version(), ::oceanbase::share::server_service<::oceanbase::storage::ObFreezeInfoMgr>()->get_latest_frozen_version());

    // Check database-wide compaction state before inspecting individual tablets.
    if (OB_TMP_FAIL(diagnose_database(diagnose_major_flag, compaction_scn))) {
    }

    // get all diagnose tablets from diagnose_tablet_map
    DiagnoseTabletArray diagnose_tablets;
    DiagnoseTabletArray tablet_array;
    if (OB_TMP_FAIL(::oceanbase::share::server_service<::oceanbase::compaction::ObDiagnoseTabletMgr>()->get_diagnose_tablets(diagnose_tablets))) {
    }

    ObLS *ls = nullptr;
    ObTabletHandle tablet_handle;
    ObTablet *tablet = nullptr;
    ObScheduleTabletFunc func(compaction_scn);
    if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls))) {
    } else if (OB_FAIL(func.diagnose_init(ls))) {
    } else {
      diagnose_database_ls(diagnose_major_flag, compaction_scn, func.get_ls_status());
    }
    ARRAY_FOREACH_NORET(diagnose_tablets, idx) {
      normal_ = true;
      bool need_merge = false;
      bool weak_read_ts_ready = false;
      const ObDiagnoseTablet &diagnose_tablet = diagnose_tablets.at(idx);
      const ObTabletID &tablet_id = diagnose_tablet.tablet_id_;
      if (OB_FAIL(ret) || IS_UNKNOW_TABLET_ID(tablet_id)) {
        continue;
      }
      if (OB_FAIL(ret)) {
      } else if (!func.get_ls_status().can_merge()) {
        // do nothing
      } else if (OB_TMP_FAIL(ls->get_tablet(
              tablet_id, tablet_handle, ObTabletCommon::DEFAULT_GET_TABLET_NO_WAIT))) {
      } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
        tmp_ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid tablet handle", K(tmp_ret), K(tablet_handle));
      } else if (FALSE_IT(tablet = tablet_handle.get_obj())) {
      } else {
        if (diagnose_major_flag) {
          if (OB_TMP_FAIL(func.diagnose_switch_tablet(*ls, *tablet))) {
          } else if (OB_TMP_FAIL(diagnose_tablet_major_merge(compaction_scn, func.get_tablet_status(), *tablet))) {
          }
        }
        if (OB_TMP_FAIL(diagnose_tablet_medium_merge(diagnose_major_flag, compaction_scn, *tablet))) {
        }
        if (OB_TMP_FAIL(diagnose_tablet_mini_merge(*tablet))) {
        }
        if (OB_TMP_FAIL(diagnose_tablet_minor_merge(*tablet))) {
        }
      }
      // don't have any diagnose info, push_back this tablet
      if (normal_) {
        tablet_array.push_back(diagnose_tablet);
      }
    } // end of foreach
    (void)::oceanbase::share::server_service<::oceanbase::compaction::ObDiagnoseTabletMgr>()->remove_diagnose_tablets(tablet_array);
  }
  return ret;
}

int ObCompactionDiagnoseMgr::diagnose_database_major_merge()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObCompactionDiagnoseMgr is not init", K(ret));
  } else if (OB_ISNULL(
                 ::oceanbase::share::server_service<
                     ::oceanbase::data_plane::ObIMajorFreezeCoordinator>())) {
    ret = OB_NOT_INIT;
    LOG_WARN("major freeze coordinator is not available", KR(ret));
  } else {
    bool need_diagnose = false;
    bool is_paused = true;
    ObArray<data_plane::ObMajorMergeTabletDiagnostic> uncompacted_tablets;
    ObArray<uint64_t> uncompacted_table_ids;
    if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::data_plane::ObIMajorFreezeCoordinator>()->
            collect_major_merge_diagnostics(
                need_diagnose,
                is_paused,
                uncompacted_tablets,
                uncompacted_table_ids))) {
    } else if (!need_diagnose) {
      LOG_INFO("no need to diagnose database major merge on this server");
    } else if (is_paused) {
      DEL_SUSPECT_INFO(
          MAJOR_MERGE,
          UNKNOW_TABLET_ID,
          ObDiagnoseTabletType::TYPE_RS_MAJOR_MERGE);
    } else {
      (void)get_and_set_suspect_info(MAJOR_MERGE, UNKNOW_TABLET_ID);
      (void)add_uncompacted_tablet_to_diagnose(uncompacted_tablets);
      add_uncompacted_table_ids_to_diagnose(uncompacted_table_ids);
      LOG_INFO("finish diagnosing database major merge");
    }
  }
  return ret;
}

int ObCompactionDiagnoseMgr::add_uncompacted_tablet_to_diagnose(
    const ObIArray<data_plane::ObMajorMergeTabletDiagnostic>
        &uncompacted_tablets)
{
  int ret = OB_SUCCESS;
  const int64_t frozen_scn = MAX(MERGE_SCHEDULER_PTR->get_frozen_version(), ::oceanbase::share::server_service<::oceanbase::storage::ObFreezeInfoMgr>()->get_latest_frozen_version());
  const int64_t uncompacted_tablets_cnt = uncompacted_tablets.count();
  LOG_INFO("finish get uncompacted tablets for diagnose", K(ret), K(uncompacted_tablets_cnt));
  for (int64_t i = 0; OB_SUCC(ret) && i < uncompacted_tablets_cnt; ++i) {
    const data_plane::ObMajorMergeTabletDiagnostic &tablet =
        uncompacted_tablets.at(i);
    const bool compaction_scn_not_valid =
        frozen_scn > tablet.snapshot_version_;
    const char *status = tablet.checksum_error_
        ? "CHECKSUM_ERROR"
        : (compaction_scn_not_valid
               ? "compaction_scn_not_update"
               : "report_scn_not_update");
    if (OB_FAIL(ADD_DIAGNOSE_INFO(
            MAJOR_MERGE,
            tablet.tablet_id_,
            ObCompactionDiagnoseInfo::DIA_STATUS_RS_UNCOMPACTED,
            ObTimeUtility::fast_current_time(), "server",
            tablet.server_, "status", status,
            "frozen_scn", frozen_scn, "compaction_scn",
            tablet.snapshot_version_, "report_scn",
            tablet.report_scn_))) {
      LOG_WARN("fail to set diagnose info", KR(ret), "uncompacted_tablet",
               tablet);
      ret = OB_SUCCESS; // ignore ret, and process next uncompacted_tablet
    }
  }
  return ret;
}

void ObCompactionDiagnoseMgr::add_uncompacted_table_ids_to_diagnose(const ObIArray<uint64_t> &uncompacted_table_ids)
{
  int tmp_ret = OB_SUCCESS;
  for (int64_t i = 0; i < uncompacted_table_ids.count(); ++i) {
    if (OB_TMP_FAIL(ADD_COMMON_DIAGNOSE_INFO(MAJOR_MERGE,
                                             ObCompactionDiagnoseInfo::DIA_STATUS_RS_UNCOMPACTED,
                                             ObTimeUtility::fast_current_time(),
                                             "table_id", uncompacted_table_ids.at(i)))) {
      LOG_WARN_RET(tmp_ret, "fail to set diagnose info", "uncompacted_tablet", uncompacted_table_ids.at(i));
    }
  }
}

int ObCompactionDiagnoseMgr::diagnose_tablet_mini_merge(ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  const ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
  ObITable *first_frozen_memtable = nullptr;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;

  if (OB_FAIL(tablet.fetch_table_store(table_store_wrapper))) {
  } else if (OB_FAIL(table_store_wrapper.get_member()->get_first_frozen_memtable(first_frozen_memtable))) {
  } else if (nullptr != first_frozen_memtable) { // have frozen memtable
    bool diagnose_flag = false;
    ObSSTable *latest_sstable = nullptr;
    storage::ObIMemtable *frozen_memtable = static_cast<storage::ObIMemtable *>(first_frozen_memtable);
    if (OB_ISNULL(latest_sstable = static_cast<ObSSTable*>(
        table_store_wrapper.get_member()->get_minor_sstables().get_boundary_table(true/*last*/)))) {
      diagnose_flag = true;
    } else {
      if (latest_sstable->get_end_scn() < frozen_memtable->get_end_scn()
          || tablet.get_snapshot_version() < frozen_memtable->get_snapshot_version()) { // not merge finish
        diagnose_flag = true;
      }
    }
    if (diagnose_flag) {
      if (OB_TMP_FAIL(diagnose_tablet_merge(
          MINI_MERGE,
          tablet))) {
      }
    } else {
      (void) get_and_set_suspect_info(MINI_MERGE, tablet_id);
    }
  }
  return ret;
}

int ObCompactionDiagnoseMgr::diagnose_tablet_minor_merge(ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  int64_t minor_compact_trigger = ObPartitionMergePolicy::DEFAULT_MINOR_COMPACT_TRIGGER;
  {

    minor_compact_trigger = GCONF.minor_compact_trigger;

  }
  if (tablet.get_minor_table_count() >= minor_compact_trigger) {
    if (OB_FAIL(diagnose_tablet_merge(
        MINOR_MERGE,
        tablet))) {
    }
  }
  return ret;
}

int ObCompactionDiagnoseMgr::diagnose_tablet_major_merge(
    const int64_t compaction_scn,
    const ObTabletStatusCache &tablet_status,
    ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  const ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
  const int64_t last_major_snapshot_version = tablet.get_last_major_snapshot_version();
  int64_t max_sync_medium_scn = 0;
  if (tablet_id.is_ls_inner_tablet()) {
    // do nothing
  } else if (!tablet_status.can_merge()) {
    // including DATA_NOT_COMPLETE / NO_MAJOR_SSTABLE
    ADD_MAJOR_WAIT_SCHEDULE(compaction_scn + WAIT_MEDIUM_SCHEDULE_INTERVAL * 2,
      ObTabletStatusCache::tablet_execute_state_to_str(tablet_status.get_execute_state()));
  } else if (OB_ISNULL(tablet_status.medium_list())) {
    // tablet status has null medium list, cannot check sycn medium scn
  } else if (OB_FAIL(ObMediumCompactionScheduleFunc::get_max_sync_medium_scn(
      tablet, *tablet_status.medium_list(), max_sync_medium_scn))) {
  } else if (tablet_status.tablet_merge_finish()) {
    diagnose_failed_report_task(tablet_id, compaction_scn);
  } else {
    if (max_sync_medium_scn < compaction_scn) {
      // max_sync_medium_scn > last_major_snapshot_version means last compaction is not finished,
      // this will be diagnosed in diagnose_tablet_medium_merge
      if (max_sync_medium_scn == last_major_snapshot_version) {
        // now last compaction finish
        if (OB_HASH_NOT_EXIST == get_and_set_suspect_info(MEDIUM_MERGE, tablet_id)) {
          if (ObTabletStatusCache::DIAGNOSE_NORMAL != tablet_status.get_new_round_state()) {
            ADD_MAJOR_WAIT_SCHEDULE(compaction_scn + WAIT_MEDIUM_SCHEDULE_INTERVAL * 2,
              ObTabletStatusCache::new_round_state_to_str(tablet_status.get_new_round_state()));
          } else {
            const char *info = "no medium info behind major";
            ADD_MAJOR_WAIT_SCHEDULE(compaction_scn + WAIT_MEDIUM_SCHEDULE_INTERVAL * 2, info);
          }
        }
      }
    } else if (tablet.get_snapshot_version() < compaction_scn) { // wait mini compaction or tablet freeze
    const char* info = "major wait for freeze";
    ADD_MAJOR_WAIT_SCHEDULE(compaction_scn + WAIT_MEDIUM_SCHEDULE_INTERVAL, info);
    }
    if (OB_TMP_FAIL(diagnose_tablet_merge(
        MEDIUM_MERGE,
        tablet,
        compaction_scn))) {
    }
  }
  return ret;
}

int ObCompactionDiagnoseMgr::diagnose_tablet_medium_merge(
    const bool diagnose_major_flag,
    const int64_t compaction_scn,
    ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  const ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
  const int64_t last_major_snapshot_version = tablet.get_last_major_snapshot_version();
  int64_t max_sync_medium_scn = 0;
  ObArenaAllocator allocator("GetMediumList", OB_MALLOC_NORMAL_BLOCK_SIZE);
  const compaction::ObMediumCompactionInfoList *medium_list = nullptr;
  if (tablet_id.is_ls_inner_tablet()) {
    // do nothing
  } else if (OB_FAIL(tablet.read_medium_info_list(allocator, medium_list))) {
  } else if (OB_FAIL(ObMediumCompactionScheduleFunc::get_max_sync_medium_scn(
      tablet, *medium_list, max_sync_medium_scn))) {
  } else {
    if (!diagnose_major_flag || (diagnose_major_flag && max_sync_medium_scn < compaction_scn)) {
      if (max_sync_medium_scn > last_major_snapshot_version) {
        if (tablet.get_snapshot_version() < max_sync_medium_scn) { // wait mini compaction or tablet freeze
          const char *info = "medium wait for freeze";
          ADD_MEDIUM_WAIT_SCHEDULE(max_sync_medium_scn + WAIT_MEDIUM_SCHEDULE_INTERVAL, info);
        } else if (0 == last_major_snapshot_version) {
          const char *info = "no major sstable";
          ADD_MEDIUM_WAIT_SCHEDULE(max_sync_medium_scn + WAIT_MEDIUM_SCHEDULE_INTERVAL, info);
        } else if (OB_TMP_FAIL(diagnose_tablet_merge(
            MEDIUM_MERGE,
            tablet,
            max_sync_medium_scn))) {
        }
      }
    }
  }
  return ret;
}

int ObCompactionDiagnoseMgr::diagnose_row_store_dag(
    const ObMergeType merge_type,
    const ObTabletID &tablet_id,
    const int64_t compaction_scn)
{
  int ret = OB_SUCCESS;
  ObTabletMajorMergeDag major_dag;
  ObTabletMergeExecuteDag minor_dag;
  ObTabletMiniMergeDag mini_dag;
  ObTabletMergeDag *dag = nullptr;
  if (is_major_merge_type(merge_type)) {
    dag = &major_dag;
  } else if (is_minor_merge(merge_type)) {
    dag = &minor_dag;
  } else if (is_mini_merge(merge_type)) {
    dag = &mini_dag;
  }
  if (OB_ISNULL(dag)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to diagnose dag", K(ret), K(tablet_id), K(merge_type));
  } else {
    ObDiagnoseTabletCompProgress progress;
    if (OB_FAIL(diagnose_dag(merge_type, tablet_id, compaction_scn, *dag, progress))) {
      if (OB_HASH_NOT_EXIST != ret) {
        LOG_WARN("failed to diagnose dag", K(ret), K(tablet_id));
      } else if (OB_FAIL(diagnose_no_dag(dag->hash(), merge_type, tablet_id, compaction_scn))) {
      }
    } else if (progress.is_valid()) { // dag exist, means compaction is running
      // check progress is normal
      if (progress.is_suspect_abormal_ &&
          OB_FAIL(ADD_DIAGNOSE_INFO_FOR_TABLET(
                merge_type,
                ObCompactionDiagnoseInfo::DIA_STATUS_RUNNING,
                ObTimeUtility::fast_current_time(),
                "current_status", "dag may hang",
                "merge_progress", progress))) {
        LOG_WARN("failed to add diagnose info", K(ret), K(tablet_id), K(progress));
      }
    } else if (OB_FAIL(diagnose_no_dag(dag->hash(), merge_type, tablet_id, compaction_scn))) {
    }
  }
  return ret;
}

int ObCompactionDiagnoseMgr::diagnose_tablet_merge(
    const ObMergeType merge_type,
    ObTablet &tablet,
    const int64_t compaction_scn)
{
  int ret = OB_SUCCESS;
  const ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
  if (OB_FAIL(diagnose_row_store_dag(merge_type, tablet_id, compaction_scn))) {
  }
  return ret;
}

int ObCompactionDiagnoseMgr::get_suspect_and_warning_info(
    const int64_t dag_key,
    const ObMergeType merge_type,
    const ObTabletID tablet_id,
    ObScheduleSuspectInfo &info,
    ObSuspectInfoType &suspect_type,
    char *buf,
    const int64_t buf_len)
{
  int ret = OB_SUCCESS;

  suspect_type = ObSuspectInfoType::SUSPECT_INFO_TYPE_MAX;
  ObDagWarningInfo warning_info;
  bool add_schedule_info = false;
  ObInfoParamBuffer allocator;
  compaction::ObMergeDagHash dag_hash;
  dag_hash.merge_type_ = merge_type;
  dag_hash.tablet_id_ = tablet_id;
  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::compaction::ObScheduleSuspectInfoMgr>()->get_with_param(dag_hash.inner_hash(), info, allocator))) {
    if (OB_HASH_NOT_EXIST != ret) {
      LOG_WARN("failed to get suspect info", K(ret), K(dag_hash));
    } else { // no schedule suspect info
      info.info_param_ = nullptr;
      allocator.reuse();
      char tmp_str[common::OB_DAG_WARNING_INFO_LENGTH] = "\0";
      if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::ObDagWarningHistoryManager>()->get_with_param(
                    dag_key, warning_info, allocator))) {
        // check __all_virtual_dag_warning_history
        if (OB_HASH_NOT_EXIST != ret) {
          LOG_WARN("failed to get dag warning info", K(ret), K(dag_hash));
        } else { // no execute failure
          ret = OB_SUCCESS;
          LOG_INFO("no dag warning info. may wait for schedule", K(ret), K(dag_key), K(dag_hash));
        }
      } else if (can_add_diagnose_info()) {
        if (OB_FAIL(warning_info.info_param_->fill_comment(tmp_str, sizeof(tmp_str)))) {
        } else if (warning_info.location_.is_valid()) {
          if (OB_FAIL(ADD_DIAGNOSE_INFO_FOR_TABLET(
                  merge_type,
                  ObCompactionDiagnoseInfo::DIA_STATUS_FAILED,
                  warning_info.gmt_create_,
                  "error_no", warning_info.dag_ret_,
                  "last_error_time", warning_info.gmt_modified_,
                  "error_trace", warning_info.task_id_,
                  "location", warning_info.location_,
                  "warning", tmp_str))) {
          }
        } else if (OB_FAIL(ADD_DIAGNOSE_INFO_FOR_TABLET(
                merge_type,
                ObCompactionDiagnoseInfo::DIA_STATUS_FAILED,
                warning_info.gmt_create_,
                "error_no", warning_info.dag_ret_,
                "last_error_time", warning_info.gmt_modified_,
                "error_trace", warning_info.task_id_,
                "warning", tmp_str))) {
        }
      }
    }
  } else if (OB_FAIL(info.info_param_->fill_comment(buf, buf_len))) {
  } else if (FALSE_IT(suspect_type = info.info_param_->type_.suspect_type_)) {
  }
  return ret;
}

int ObCompactionDiagnoseMgr::diagnose_no_dag(
    const int64_t dag_key,
    const ObMergeType merge_type,
    const ObTabletID tablet_id,
    const int64_t compaction_scn)
{
  int ret = OB_SUCCESS;
  ObScheduleSuspectInfo info;
  bool add_schedule_info = false;
  ObSuspectInfoType suspect_type = SUSPECT_INFO_TYPE_MAX;
  char tmp_str[common::OB_DIAGNOSE_INFO_LENGTH] = "\0";
  if (OB_FAIL(get_suspect_and_warning_info(dag_key, merge_type, tablet_id, info, suspect_type, tmp_str, sizeof(tmp_str)))) {
  } else if (!info.is_valid()) {
    // do nothing
  } else if (is_medium_merge(merge_type)) {
    if (OB_UNLIKELY(compaction_scn <= 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("merge version or freeze ts is invalid", K(ret), K(compaction_scn));
    } else {
      LOG_INFO("diagnose major", K(ret), K(tablet_id), "merge_type", merge_type_to_str(merge_type));
      ObDiagnoseTabletCompProgress progress;
      ObTabletMiniMergeDag mini_dag;
      if (OB_FAIL(diagnose_dag(MINI_MERGE, tablet_id, ObVersionRange::MIN_VERSION, mini_dag, progress))) {
        if (OB_HASH_NOT_EXIST != ret) {
          LOG_WARN("failed to init dag", K(ret), K(tablet_id));
        } else {
          add_schedule_info = true;
          ret = OB_SUCCESS;
        }
      } else if (progress.base_version_ < compaction_scn && progress.snapshot_version_ >= compaction_scn) {
        // a mini merge for major
        if (OB_FAIL(ADD_DIAGNOSE_INFO_FOR_TABLET(
                merge_type,
                ObCompactionDiagnoseInfo::DIA_STATUS_NOT_SCHEDULE,
                ObTimeUtility::fast_current_time(),
                "current_status", "wait for mini merge",
                "mini_merge_progress", progress))) {
        }
      } else { // no running mini dag
        add_schedule_info = true;
      }
    }
  } else { // is mini merge
    add_schedule_info = true;
  }

  if (OB_SUCC(ret) && add_schedule_info && suspect_type < SUSPECT_INFO_TYPE_MAX) {
    // check tablet_type in get_diagnose_tablet_count
    if (suspect_tablet_count_[suspect_type] < DIAGNOSE_TABELT_MAX_COUNT) {
      if (OB_FAIL(ADD_DIAGNOSE_INFO_FOR_TABLET(
            merge_type,
            ObCompactionDiagnoseInfo::DIA_STATUS_NOT_SCHEDULE,
            info.add_time_,
            "schedule_suspect_info", tmp_str))) {
      }
    }
    ++suspect_tablet_count_[suspect_type];
    suspect_merge_type_[suspect_type] = merge_type;
  }
  return ret;
}

/*
 * ObTabletCompactionProgressIterator implement
 * */

int ObCompactionDiagnoseIterator::get_diagnose_info()
{
  int ret = OB_SUCCESS;
  ObCompactionDiagnoseMgr diagnose_mgr;
  void * buf = nullptr;
  if (NULL == (buf = allocator_.alloc(sizeof(ObCompactionDiagnoseInfo) * MAX_DIAGNOSE_INFO_CNT))) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
    STORAGE_LOG(WARN, "failed to alloc info array", K(ret));
  } else if (FALSE_IT(info_array_ = new (buf) ObCompactionDiagnoseInfo[MAX_DIAGNOSE_INFO_CNT])) {
  } else if (OB_FAIL(diagnose_mgr.init(&allocator_, info_array_, MAX_DIAGNOSE_INFO_CNT))) {
  } else if (OB_FAIL(diagnose_mgr.diagnose_all_tablets())) {
  } else {
    cnt_ = diagnose_mgr.get_cnt();
  }
  return ret;
}

int ObCompactionDiagnoseIterator::open()
{
  int ret = OB_SUCCESS;
  if (is_opened_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("The ObCompactionDiagnoseIterator has been opened", K(ret));
  } else if (OB_FAIL(get_diagnose_info())) {
  } else {
    cur_idx_ = 0;
    is_opened_ = true;
  }
  return ret;
}

void ObCompactionDiagnoseIterator::reset()
{
  if (OB_NOT_NULL(info_array_)) {
    allocator_.free(info_array_);
    info_array_ = nullptr;
  }
  cnt_ = 0;
  cur_idx_ = 0;
  is_opened_ = false;
}

int ObCompactionDiagnoseIterator::get_next_info(ObCompactionDiagnoseInfo &info)
{
  int ret = OB_SUCCESS;
  if (!is_opened_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (cur_idx_ >= cnt_) {
    ret = OB_ITER_END;
  } else if (OB_ISNULL(info_array_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("array is null", K(ret));
  } else {
    info = info_array_[cur_idx_++];
  }
  return ret;
}

}//compaction
}//oceanbase
