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
#include "storage/compaction/ob_batch_freeze_tablets_dag.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/compaction/ob_tablet_scheduler.h"
#include "storage/tx_storage/ob_memstore_freezer.h"
#include "storage/tx_storage/ob_ls_service.h"
namespace oceanbase
{
using namespace share;
using namespace storage;
namespace compaction
{
/*
 *  ----------------------------------------ObBatchFreezeTabletsDag--------------------------------------------
 */
int ObBatchFreezeTabletsDag::inner_init()
{
  int ret = OB_SUCCESS;
  const ObBatchFreezeTabletsParam &param = get_param();
  if (!param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid param", K(ret), K(param));
  } else {
    (void) set_max_concurrent_task_cnt(MAX_CONCURRENT_FREEZE_TASK_CNT);
  }
  return ret;
}

int64_t ObBatchFreezeTabletsParam::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(param_type), K_(compaction_scn), K_(batch_size), K_(loop_cnt));
  if (tablet_info_array_.count() > 0) {
    J_COMMA();
    J_NAME("tablet_info_array");
    J_COLON();
    J_ARRAY_START();
    int64_t last_schedule_merge_scn = -1;
    for (int64_t i = 0; i < tablet_info_array_.count(); ++i) {
      const ObTabletSchedulePair &tablet_pair = tablet_info_array_.at(i);
      if (i != 0) {
        J_COMMA();
      }
      J_OBJ_START();
      J_KV("tablet_id", tablet_pair.tablet_id_);
      // check schedule_merge_scn is same with last one
      if (tablet_pair.schedule_merge_scn_ != last_schedule_merge_scn) {
        // if different, print full schedule_merge_scn
        J_COMMA();
        J_KV("schedule_merge_scn", tablet_pair.schedule_merge_scn_);
        last_schedule_merge_scn = tablet_pair.schedule_merge_scn_;
      }
      // else only print tablet_id
      J_OBJ_END();
    }
    J_ARRAY_END();
  }
  J_OBJ_END();
  return pos;
}

int ObBatchFreezeTabletsParam::assign(const ObBatchFreezeTabletsParam &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObBatchExecParam<ObTabletSchedulePair>::assign(other))) {
  } else {
    loop_cnt_ = other.loop_cnt_;
  }
  return ret;
}

bool ObBatchFreezeTabletsDag::operator == (const ObIDag &other) const
{
  bool is_same = true;
  if (this == &other) {
    // same
  } else if (get_type() != other.get_type()) {
    is_same = false;
  } else {
    const ObBatchFreezeTabletsDag &other_dag = static_cast<const ObBatchFreezeTabletsDag &>(other);
    if (get_param().compaction_scn_ != other_dag.get_param().compaction_scn_) {
      is_same = false;
    } else {
      // to ensure dag with same loop_cnt is not same
      is_same = get_param().loop_cnt_ != other_dag.get_param().loop_cnt_;
    }
  }
  return is_same;
}
/*
 *  ----------------------------------------ObBatchFreezeTabletsTask--------------------------------------------
 */
ObBatchFreezeTabletsTask::ObBatchFreezeTabletsTask()
  : ObBatchExecTask(ObITask::TASK_TYPE_BATCH_FREEZE_TABLETS)
{
}

ObBatchFreezeTabletsTask::~ObBatchFreezeTabletsTask()
{
}

int ObBatchFreezeTabletsTask::inner_process()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  int64_t cost_ts = ObTimeUtility::fast_current_time();
  const ObBatchFreezeTabletsParam &param = base_dag_->get_param();

  ObLS *ls = nullptr;
  int64_t weak_read_ts = 0;
  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls))) {
  } else {
    weak_read_ts = ls->get_ls_wrs_handler()->get_ls_weak_read_ts().get_val_for_tx();
  }

  int64_t fail_freeze_cnt = 0;
  int64_t succ_schedule_cnt = 0;
  const int64_t start_idx = get_start_idx();
  const int64_t end_idx = MIN(param.tablet_info_array_.count(), get_end_idx());
  for (int64_t i = start_idx; OB_SUCC(ret) && i < end_idx; ++i) {
    const ObTabletSchedulePair &cur_pair = param.tablet_info_array_.at(i);
    ObTabletHandle tablet_handle;
    ObTablet *tablet = nullptr;
    // just try tablet freeze for one second
    const int64_t max_retry_time_us = 1LL * 1000LL * 1000LL/* 1 second */;

    if (OB_UNLIKELY(!cur_pair.is_valid())) {
      tmp_ret = OB_ERR_UNEXPECTED;
      LOG_WARN_RET(tmp_ret, "get invalid tablet pair", K(cur_pair));
    } else if (cur_pair.schedule_merge_scn_ > weak_read_ts) {
      // no need to force freeze
    } else if (OB_TMP_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObMemstoreFreezer>()->tablet_freeze(cur_pair.tablet_id_,
                                                                 true/*is_sync*/,
                                                                 max_retry_time_us,
                                                                 true,/*need_rewrite_meta*/
                                                                 ObFreezeSourceFlag::MAJOR_FREEZE))) {
      LOG_ERROR_RET(tmp_ret, "failed to force freeze tablet", K(param), K(cur_pair));
      ++cnt_.failure_cnt_;
    } else if (FALSE_IT(++cnt_.success_cnt_)) {
    } else if (OB_TMP_FAIL(schedule_tablet_major_after_freeze(*ls, cur_pair))) {
      if (OB_SIZE_OVERFLOW != tmp_ret && OB_EAGAIN != tmp_ret) {
        LOG_ERROR_RET(tmp_ret, "failed to schedule medium merge dag", K(param), K(cur_pair));
      }
    }

    if (FAILEDx(share::dag_yield())) {
      LOG_WARN("failed to dag yield", K(ret));
    }
    if (REACH_THREAD_TIME_INTERVAL(5_s)) {
      weak_read_ts = ls->get_ls_wrs_handler()->get_ls_weak_read_ts().get_val_for_tx();
    }
  } // end for

  cost_ts = ObTimeUtility::fast_current_time() - cost_ts;
  FLOG_INFO("batch freeze tablets finished", KR(ret), K(param), K(start_idx), K(end_idx),
    K_(cnt), K(cost_ts));

  return ret;
}

int ObBatchFreezeTabletsTask::schedule_tablet_major_after_freeze(
  ObLS &ls,
  const ObTabletSchedulePair &cur_pair)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = NULL;
  if (!::oceanbase::share::server_service<::oceanbase::compaction::ObTabletScheduler>()->could_major_merge_start()) {
    // merge is suspended
  } else if (OB_FAIL(ls.get_tablet_svr()->get_tablet(
                 cur_pair.tablet_id_, tablet_handle, 0 /*timeout_us*/,
                 ObMDSGetTabletMode::READ_ALL_COMMITED))) {
  } else if (FALSE_IT(tablet = tablet_handle.get_obj())) {
  } else if (OB_UNLIKELY(tablet->get_snapshot_version() < cur_pair.schedule_merge_scn_)) {
    // do nothing
  } else if (!tablet->is_data_complete()) {
    // no need to schedule merge
  } else if (OB_FAIL(ObTabletScheduler::schedule_merge_dag(
                 *tablet, MEDIUM_MERGE,
                 cur_pair.schedule_merge_scn_))) {
    if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
      LOG_ERROR("failed to schedule medium merge dag", K(ret), K(cur_pair));
    }
  }
  return ret;
}

} // namespace compaction
} // namespace oceanbase
