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
#include "storage/compaction/ob_compaction_schedule_iterator.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
using namespace storage;
using namespace common;
using namespace share;
namespace compaction
{
ObBasicMergeScheduleIterator::ObTabletArray::ObTabletArray()
  : tablet_idx_(0),
    array_(),
    is_inited_(false)
{
  array_.set_attr(ObMemAttr("CompIter"));
}

int ObBasicMergeScheduleIterator::ObTabletArray::consume_tablet_id(ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("tablet array is not init", KR(ret), KPC(this));
  } else if (OB_UNLIKELY(tablet_idx_ < 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tablet idx is invalid", KR(ret), KPC(this));
  } else if (tablet_idx_ >= count()) {
    ret = OB_ITER_END;
  } else {
    tablet_id = array_.at(tablet_idx_++);
  }
  return ret;
}


ObBasicMergeScheduleIterator::ObBasicMergeScheduleIterator()
  : scan_finish_(false),
    merge_finish_(false),
    schedule_tablet_cnt_(0),
    max_batch_tablet_cnt_(0),
    ls_(nullptr),
    tablet_ids_()
{
}

int ObBasicMergeScheduleIterator::init(
    const int64_t schedule_batch_size,
    ObLS *ls)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(schedule_batch_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(schedule_batch_size));
  } else if (!is_valid()) {
    tablet_ids_.reset();
    scan_finish_ = false;
    merge_finish_ = true;
    ls_ = ls;
    schedule_tablet_cnt_ = 0;
    max_batch_tablet_cnt_ = schedule_batch_size;
    LOG_TRACE("build iter", K(ret), KPC(this));
  } else { // iter is valid, no need to build, just set var to start cur batch
    (void) start_cur_batch();
  }
  return ret;
}

#ifdef ERRSIM
void errsim_set_batch_cnt(
  const ObBasicMergeScheduleIterator::ObTabletArray &tablet_ids,
  int64_t &max_batch_tablet_cnt)
{
  int ret = OB_SUCCESS;
  ret = OB_E(EventTable::EN_COMPACTION_ITER_SET_BATCH_CNT) ret;
  if (OB_FAIL(ret)) {
    if (-ret <= 1) {
      max_batch_tablet_cnt = tablet_ids.array_.count();
    } else {
      max_batch_tablet_cnt = -ret;
    }
    FLOG_INFO("ERRSIM EN_COMPACTION_ITER_SET_BATCH_CNT", K(ret),
      K(max_batch_tablet_cnt), K(tablet_ids));
  }
}
#endif

int ObBasicMergeScheduleIterator::get_next_tablet(ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  if (scan_finish_) {
    ret = OB_ITER_END;
  } else if (!tablet_ids_.is_inited_) {
    if (OB_FAIL(get_tablet_ids())) {
      LOG_WARN("failed to get tablet ids", K(ret));
    } else {
      tablet_ids_.mark_inited();
      LOG_TRACE("build iter in get_next_tablet", K(ret), K(tablet_ids_));
#ifdef ERRSIM
      (void) errsim_set_batch_cnt(tablet_ids_, max_batch_tablet_cnt_);
#endif
    }
  }
  if (OB_FAIL(ret)) {
  } else if (tablet_ids_.is_tablet_iter_end()) {
    scan_finish_ = true;
    ret = OB_ITER_END;
    LOG_DEBUG("schedule tablet scan finish", K(ret));
  } else if (schedule_tablet_cnt_ >= max_batch_tablet_cnt_) {
    LOG_INFO("reach max batch tablet cnt, schedule next round", K(ret),
      K_(schedule_tablet_cnt), K_(max_batch_tablet_cnt), "tablet_cnt", tablet_ids_.count(),
      "tablet_idx", tablet_ids_.tablet_idx_);
    ret = OB_ITER_END;
  } else {
    ObTabletID tablet_id;
    do {
      if (OB_FAIL(tablet_ids_.consume_tablet_id(tablet_id))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("failed to get tablet id", KR(ret), K_(tablet_ids));
        } else {
          scan_finish_ = true;
        }
      } else if (OB_FAIL(get_tablet_handle(tablet_id, tablet_handle))) {
        if (OB_TABLET_NOT_EXIST == ret) {
          LOG_DEBUG("tablet not exist", K(ret), K(tablet_id), "tablet_cnt", tablet_ids_.count());
        } else {
          LOG_WARN("fail to get tablet", K(ret), K(tablet_ids_), K(tablet_id));
        }
      } else {
        tablet_handle.set_wash_priority(WashTabletPriority::WTP_LOW);
        schedule_tablet_cnt_++;
      }
    } while (OB_TABLET_NOT_EXIST == ret);
  }
  return ret;
}

void ObBasicMergeScheduleIterator::reset_basic_iter()
{
  scan_finish_ = false;
  merge_finish_ = false;
  schedule_tablet_cnt_ = 0;
  max_batch_tablet_cnt_ = 0;
  tablet_ids_.reset();
  ls_ = nullptr;
}

bool ObBasicMergeScheduleIterator::is_valid() const
{
  return max_batch_tablet_cnt_ > 0 && !scan_finish_ && !tablet_ids_.is_tablet_iter_end();
}

int64_t ObBasicMergeScheduleIterator::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(tablet_ids), K_(schedule_tablet_cnt), K_(max_batch_tablet_cnt));
  if (is_valid() && OB_NOT_NULL(ls_)) {
    J_COMMA();
    J_KV("has_ls", OB_NOT_NULL(ls_));
  }
  J_OBJ_END();
  return pos;
}


/************************************************ ObCompactionScheduleIterator ************************************************/
ObCompactionScheduleIterator::ObCompactionScheduleIterator(
    const bool is_major)
  : ObBasicMergeScheduleIterator(),
    is_major_(is_major),
    report_scn_flag_(false),
    tablet_get_mode_(storage::ObMDSGetTabletMode::READ_ALL_COMMITED)
{
}

int ObCompactionScheduleIterator::build_iter(
    const int64_t schedule_batch_size,
    ObLSService &ls_service)
{
  int ret = OB_SUCCESS;
  bool need_reset_report_scn = !is_valid();
  ObLS *ls = nullptr;

  if (OB_UNLIKELY(schedule_batch_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(schedule_batch_size));
  } else if (!need_reset_report_scn) {
    ls = ls_;
  } else if (OB_FAIL(ls_service.get_ls(ls))) {
    LOG_WARN("failed to get ls", K(ret));
  }

  if (OB_SUCC(ret) && OB_NOT_NULL(ls) && OB_FAIL(init(schedule_batch_size, ls))) {
    LOG_WARN("failed to inner build iter", K(ret));
  }
  if (OB_SUCC(ret) && need_reset_report_scn) {
    report_scn_flag_ = false;
    if (REACH_THREAD_TIME_INTERVAL(CHECK_REPORT_SCN_INTERVAL)) {
      report_scn_flag_ = true;
    }
#ifdef ERRSIM
      report_scn_flag_ = true;
#endif
  }
  return ret;
}

void ObCompactionScheduleIterator::reset()
{
  reset_basic_iter();
  report_scn_flag_ = false;
}

#ifdef ERRSIM
void errsim_iter_invalid_tablet_id(int &ret, ObBasicMergeScheduleIterator::ObTabletArray &tablet_ids) {
  if (OB_SUCC(ret)) {
    ret = OB_E(EventTable::EN_COMPACTION_ITER_INVALID_TABLET_ID) ret;
    if (OB_FAIL(ret)) {
      FLOG_INFO("ERRSIM EN_COMPACTION_ITER_INVALID_TABLET_ID", KR(ret));
      common::ObSEArray<common::ObTabletID, 100> tmp_tablet_ids;
      int tmp_ret = OB_SUCCESS;
      const int64_t max_tablet_id = tablet_ids.array_.at(tablet_ids.count() -1).id();
      if (OB_TMP_FAIL(tmp_tablet_ids.assign(tablet_ids.array_))) {
        LOG_WARN_RET(tmp_ret, "failed to assign tablet_ids");
      } else {
        tablet_ids.reset();
        // push several invalid tablet id, rest tablet should be scheduled
        tmp_ret = tablet_ids.array_.push_back(ObTabletID(max_tablet_id + 100));
        for (int64_t i = 0; OB_SUCC(tmp_ret) && i < tmp_tablet_ids.count(); ++i) {
          if (i == tmp_tablet_ids.count() / 2) {
            tmp_ret = tablet_ids.array_.push_back(ObTabletID(max_tablet_id + 200));
          }
          if (OB_SUCC(tmp_ret)) {
            tmp_ret = tablet_ids.array_.push_back(tmp_tablet_ids.at(i));
          }
        }
        if (OB_SUCC(tmp_ret)) {
          tmp_ret = tablet_ids.array_.push_back(ObTabletID(max_tablet_id + 300));
        }
      }
      if (OB_SUCCESS == tmp_ret) {
        ret = OB_SUCCESS;
      }
    }
  }
}
#endif

int ObCompactionScheduleIterator::get_tablet_ids()
{
  tablet_ids_.reset();
  int ret = ls_->get_tablet_svr()->get_all_tablet_ids(is_major_/*except_ls_inner_tablet*/, tablet_ids_.array_);
#ifdef ERRSIM
  (void) errsim_iter_invalid_tablet_id(ret, tablet_ids_);
#endif
  return ret;
}

int ObCompactionScheduleIterator::get_tablet_handle(
  const ObTabletID &tablet_id, ObTabletHandle &tablet_handle)
{
  int ret = ls_->get_tablet_svr()->get_tablet(tablet_id, tablet_handle,  0/*timeout*/, tablet_get_mode_);
#ifdef ERRSIM
  if (OB_SUCC(ret) && tablet_id.id() > ObTabletID::MIN_USER_TABLET_ID) {
    ret = OB_E(EventTable::EN_COMPACTION_ITER_TABLET_NOT_EXIST) ret;
    if (OB_FAIL(ret)) {
      FLOG_INFO("ERRSIM EN_COMPACTION_ITER_TABLET_NOT_EXIST", KR(ret));
      ret = OB_TABLET_NOT_EXIST;
      tablet_handle.reset();
    }
  }
#endif
  return ret;
}


} // namespace compaction
} // namespace oceanbase
