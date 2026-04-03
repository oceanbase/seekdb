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
#include "ob_tenant_medium_checker.h"
#include "storage/compaction/ob_medium_compaction_func.h"
#include "storage/compaction/ob_server_compaction_event_history.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
using namespace storage;
namespace compaction
{
/*
 * ObTabletCheckInfo implement
 * */
bool ObTabletCheckInfo::is_valid() const
{
  return tablet_id_.is_valid() && ls_id_.is_valid() && 
    check_medium_scn_ != 0;
}

uint64_t ObTabletCheckInfo::hash() const
{
  uint64_t hash_val = 0;
  hash_val = murmurhash(&tablet_id_, sizeof(tablet_id_), hash_val);
  hash_val = murmurhash(&ls_id_, sizeof(ls_id_), hash_val);
  return hash_val;
}

bool ObTabletCheckInfo::operator==(const ObTabletCheckInfo &other) const
{
  return tablet_id_ == other.tablet_id_ &&
    ls_id_ == other.ls_id_;
}

/*
 * ObBatchFinishCheckStat implement
 * */
int64_t ObBatchFinishCheckStat::to_string(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(succ_cnt), K_(finish_cnt), K_(fail_cnt), K_(filter_cnt));
  if (0 != fail_cnt_) {
    J_COMMA();
    J_KV(K_(failed_info));
  }
  J_OBJ_END();
  return pos;
}

/*
 * ObTenantMediumChecker implement
 * */
int ObTenantMediumChecker::mtl_init(ObTenantMediumChecker *&tablet_medium_checker)
{
  return tablet_medium_checker->init();
}

ObTenantMediumChecker::ObTenantMediumChecker()
  : is_inited_(false),
    last_check_timestamp_(0),
    error_tablet_cnt_(0),
    tablet_ls_set_(),
    lock_(),
    ls_locality_cache_empty_(true)
{}

ObTenantMediumChecker::~ObTenantMediumChecker()
{
  destroy();
}

int ObTenantMediumChecker::init()
{
  int ret = OB_SUCCESS;
  if (GCTX.is_shared_storage_mode()) {
    FLOG_INFO("cluster is shared storage mode, not init ObTenantMediumChecker", KR(ret));
  } else if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTenantMediumChecker is inited before", KR(ret), KPC(this));
  } else if (OB_FAIL(tablet_ls_set_.create(DEFAULT_MAP_BUCKET, "MedCheckSet", "CheckSetNode", MTL_ID()))) {
    LOG_WARN("failed to create set", K(ret));
  } else {
    is_inited_ = true;
  }
  if (!is_inited_) {
    destroy();
  }
  return ret;
}

void ObTenantMediumChecker::destroy()
{
  is_inited_ = false;
  ls_locality_cache_empty_ = true;
  if (tablet_ls_set_.created()) {
    tablet_ls_set_.destroy();
  }
}

int ObTenantMediumChecker::add_tablet_ls(const ObTabletID &tablet_id, const share::ObLSID &ls_id, const int64_t medium_scn)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantMediumChecker is not inited", K(ret));
  } else if (!tablet_id.is_valid() || !ls_id.is_valid() || 0 == medium_scn) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(ls_id), K(medium_scn));
  } else {
    lib::ObMutexGuard guard(lock_);
    // just cover the old info
    if (OB_FAIL(tablet_ls_set_.set_refactored(ObTabletCheckInfo(tablet_id, ls_id, medium_scn)))) {
      LOG_WARN("failed to set tablet_ls_info", K(ret), K(ls_id), K(tablet_id));
    }
  }
  return ret;
}

bool ObTenantMediumChecker::locality_cache_empty()
{
  bool bret = true;
  if (IS_INIT) {
    bret = ls_locality_cache_empty_;
  }
  return bret;
}

int ObTenantMediumChecker::check_medium_finish_schedule()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantMediumChecker is not inited", K(ret));
  } else {
    DEL_SUSPECT_INFO(MEDIUM_MERGE, UNKNOW_LS_ID, UNKNOW_TABLET_ID, ObDiagnoseTabletType::TYPE_MEDIUM_MERGE);
    TabletLSArray tablet_ls_infos;
    tablet_ls_infos.set_attr(ObMemAttr(MTL_ID(), "CheckInfos"));
    TabletLSArray batch_tablet_ls_infos;
    batch_tablet_ls_infos.set_attr(ObMemAttr(MTL_ID(), "BCheckInfos"));
    TabletLSArray finish_tablet_ls_infos;
    finish_tablet_ls_infos.set_attr(ObMemAttr(MTL_ID(), "FinishInfos"));
    // copy the tablet_ls_infos from set // with lock
    {
      lib::ObMutexGuard guard(lock_);
      if (OB_FAIL(tablet_ls_infos.reserve(tablet_ls_set_.size()))) {
        LOG_WARN("fail to reserve array", K(ret), "size", tablet_ls_set_.size());
      } else {
        for (TabletLSSet::const_iterator iter = tablet_ls_set_.begin();
            OB_SUCC(ret) && iter != tablet_ls_set_.end(); ++iter) {
          if (OB_FAIL(tablet_ls_infos.push_back(iter->first))) {
            LOG_WARN("failed to push back tablet_ls_info", K(ret), K(iter->first));
          }
        }
      }
      if (OB_SUCC(ret)) {
        tablet_ls_set_.clear();
      }
    }
    const int64_t batch_size = MTL(ObTenantTabletScheduler *)->get_checker_batch_size();
    if (OB_FAIL(ret) || tablet_ls_infos.empty()) {
    } else if (OB_FAIL(batch_tablet_ls_infos.reserve(batch_size))) {
      LOG_WARN("fail to reserve array", K(ret), "size", batch_size);
    } else if (OB_FAIL(finish_tablet_ls_infos.reserve(batch_size))) {
      LOG_WARN("fail to reserve array", K(ret), "size", batch_size);
    } else {
      // batch check
      int64_t info_count = tablet_ls_infos.count();
      int64_t start_idx = 0;
      int64_t end_idx = min(batch_size, info_count);
      int64_t cost_ts = ObTimeUtility::fast_current_time();
      ObBatchFinishCheckStat stat;
      while (start_idx < end_idx) {
        if (OB_TMP_FAIL(check_medium_finish(tablet_ls_infos, start_idx, end_idx, batch_tablet_ls_infos, finish_tablet_ls_infos, stat))) {
          LOG_WARN("failed to check medium finish", K(tmp_ret));
        }
        start_idx = end_idx;
        end_idx = min(start_idx + batch_size, info_count);
      }
      cost_ts = ObTimeUtility::fast_current_time() - cost_ts;
      ADD_COMPACTION_EVENT(
        MERGE_SCHEDULER_PTR->get_frozen_version(),
        ObServerCompactionEvent::COMPACTION_FINISH_CHECK,
        ObTimeUtility::fast_current_time(),
        K(cost_ts), "batch_check_stat", stat);
    }
#ifdef ERRSIM
    if (OB_SUCC(ret)) {
      ret = OB_E(EventTable::EN_MEDIUM_REPLICA_CHECKSUM_ERROR) OB_SUCCESS;
      if (OB_SUCC(ret)) {
        if (REACH_THREAD_TIME_INTERVAL(CLEAR_CKM_ERROR_INTERVAL)) {
          clear_error_tablet_cnt();
        }
      }
      ret = OB_SUCCESS;
    }
#else
    if (REACH_THREAD_TIME_INTERVAL(CLEAR_CKM_ERROR_INTERVAL)) {
      clear_error_tablet_cnt();
    }
#endif
  }
  return ret;
}

int ObTenantMediumChecker::check_medium_finish(
    const ObIArray<ObTabletCheckInfo> &tablet_ls_infos,
    int64_t start_idx,
    int64_t end_idx,
    ObArray<ObTabletCheckInfo> &check_tablet_ls_infos,
    ObArray<ObTabletCheckInfo> &finish_tablet_ls_infos,
    ObBatchFinishCheckStat &stat)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_UNLIKELY(tablet_ls_infos.empty()
      || start_idx < 0
      || start_idx >= end_idx
      || end_idx > tablet_ls_infos.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_ls_infos), K(start_idx), K(end_idx));
  } else {
    check_tablet_ls_infos.reuse();
    finish_tablet_ls_infos.reuse();
    for (int64_t i = start_idx; OB_SUCC(ret) && i < end_idx; ++i) {
      const ObLSID &ls_id = tablet_ls_infos.at(i).get_ls_id();
      if (OB_UNLIKELY(0 == tablet_ls_infos.at(i).get_medium_scn())) {
        tmp_ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected medium scn", K(tmp_ret), K(tablet_ls_infos.at(i)));
      } else {
        if (OB_FAIL(check_tablet_ls_infos.push_back(tablet_ls_infos.at(i)))) {
          LOG_WARN("fail to push back check_tablet_ls_infos", K(ret), K(tablet_ls_infos.at(i)));
        }
      }
    }
    ObCompactionScheduleTimeGuard time_guard;
    stat.filter_cnt_ += (end_idx - start_idx - check_tablet_ls_infos.count());
    if (FAILEDx(ObMediumCompactionScheduleFunc::batch_check_medium_finish(
         finish_tablet_ls_infos, check_tablet_ls_infos, time_guard))) {
      LOG_WARN("failed to batch check medium finish", K(ret), K(tablet_ls_infos.count()), K(check_tablet_ls_infos.count()),
        K(tablet_ls_infos), K(check_tablet_ls_infos));
      stat.fail_cnt_ += check_tablet_ls_infos.count();
      if (0 != stat.fail_cnt_) {
        stat.failed_info_ = check_tablet_ls_infos.at(0);
      }
      if (OB_TMP_FAIL(reput_check_info(check_tablet_ls_infos))) {
        LOG_WARN("fail to reput failed check infos", K(tmp_ret));
      }
    } else {
      LOG_INFO("success to check medium finish", K(ret), K(tablet_ls_infos.count()), K(check_tablet_ls_infos.count()),
        K(finish_tablet_ls_infos.count()), K(tablet_ls_infos), K(check_tablet_ls_infos), K(finish_tablet_ls_infos));
      stat.succ_cnt_ += check_tablet_ls_infos.count();
      stat.finish_cnt_ += finish_tablet_ls_infos.count();
      ObScheduleNewMediumLoop medium_loop(finish_tablet_ls_infos);
      if (OB_FAIL(medium_loop.loop())) {
        LOG_WARN("failed to leader schedule", K(ret));
      } else {
        time_guard.click(ObCompactionScheduleTimeGuard::SCHEDULER_NEXT_ROUND);
      }
    }
    LOG_INFO("finish medium check", K(ret), K(start_idx), K(end_idx),
          K(check_tablet_ls_infos.count()), K(finish_tablet_ls_infos.count()), K(time_guard));
  }
  return ret;
}

// just reput into set without ls status check
int ObTenantMediumChecker::reput_check_info(ObIArray<ObTabletCheckInfo> &tablet_ls_infos)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTenantMediumChecker is not inited", K(ret));
  } else {
    lib::ObMutexGuard guard(lock_);
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ls_infos.count(); ++i) {
      const ObTabletCheckInfo &check_info = tablet_ls_infos.at(i);
      if (OB_HASH_EXIST == (tmp_ret = tablet_ls_set_.exist_refactored(check_info))) {
        ret = OB_SUCCESS; // tablet exist
      } else if (OB_UNLIKELY(OB_HASH_NOT_EXIST != tmp_ret)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to check exist in tablet set", K(ret), K(tmp_ret), K(check_info));
      } else if (OB_TMP_FAIL(tablet_ls_set_.set_refactored(check_info))) {
        LOG_WARN("failed to set tablet_ls_info", K(tmp_ret), K(check_info));
      }
    }
  }
  return ret;
}
}
}
