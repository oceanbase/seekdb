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

#ifndef __OB_SQL_PX_SQC_CTX_H__ 
#define __OB_SQL_PX_SQC_CTX_H__


#include "sql/engine/px/ob_dfo.h"
#include "sql/engine/px/ob_px_coord_msg_proc.h"
#include "sql/engine/px/ob_px_data_ch_provider.h"
#include "sql/engine/px/ob_granule_pump.h"
#include "sql/engine/px/ob_px_dtl_proc.h"
#include "sql/engine/px/ob_px_sqc_proxy.h"
#include "sql/dtl/ob_dtl_channel_loop.h"
#include "sql/engine/px/datahub/ob_dh_msg_provider.h"
#include "sql/engine/px/datahub/components/ob_dh_barrier.h"
#include "sql/engine/px/datahub/components/ob_dh_winbuf.h"
#include "sql/engine/px/datahub/components/ob_dh_rollup_key.h"
#include "sql/engine/px/datahub/components/ob_dh_sample.h"
#include "sql/engine/px/datahub/components/ob_dh_range_dist_wf.h"
#include "sql/engine/px/datahub/components/ob_dh_init_channel.h"
#include "sql/engine/px/datahub/components/ob_dh_second_stage_reporting_wf.h"
#include "sql/dtl/ob_dtl_msg_type.h"
#include "sql/engine/px/datahub/components/ob_dh_opt_stats_gather.h"
#include "sql/engine/px/datahub/components/ob_dh_join_filter_count_row.h"

namespace oceanbase
{
namespace storage
{
  class ObTabletDirectLoadMgrHandle;
}
namespace sql
{
// SQC status
class ObSqcCtx
{
public:
  ObSqcCtx(ObPxRpcInitSqcArgs &sqc_arg);
  ~ObSqcCtx() {  reset(); }
  common::ObIArray<ObPxTask> &get_tasks() { return tasks_; }
  // To ensure that add_task does not fail due to memory issues. Because once it fails, it may result in the launched task not being recorded
  int reserve_task_mem(int64_t cnt) { return tasks_.reserve(cnt); }
  int add_task(ObPxTask &task, ObPxTask *&task_ptr)
  {
    int ret = common::OB_SUCCESS;
    if (OB_SUCC(tasks_.push_back(task))) {
      task_ptr = &tasks_.at(tasks_.count() - 1);
    }
    return ret;
  }
  int get_task(int64_t idx, ObPxTask *&task_ptr)
  {
    int ret = common::OB_SUCCESS;
    if (idx >= tasks_.count() || idx < 0) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      task_ptr = &tasks_.at(idx);
    }
    return ret;
  }
  // Update the coroutine id of the last task
  void revert_last_task() { tasks_.pop_back(); }
  int64_t get_task_count() const { return tasks_.count(); }
  void reset() 
  {
    for (int i = 0; i < whole_msg_provider_list_.count(); ++i) {
      if (OB_NOT_NULL(whole_msg_provider_list_.at(i))) {
        whole_msg_provider_list_.at(i)->reset();
      }
    }

    if (nullptr != direct_load_mgr_handles_) {
      direct_load_mgr_handles_->reset();
      direct_load_mgr_handles_ = nullptr;
      lob_direct_load_mgr_handles_->reset();
      lob_direct_load_mgr_handles_ = nullptr;
    }
    arena_allocator_.reset();
  }

public:
  // sqc starts by registering all operators that use datahub in the sub-plan here, used for listening to whole messages
  int add_whole_msg_provider(uint64_t op_id, dtl::ObDtlMsgType msg_type, ObPxDatahubDataProvider &provider);
  int get_whole_msg_provider(uint64_t op_id, dtl::ObDtlMsgType msg_type, ObPxDatahubDataProvider *&provider);
  // when sqc init, register all init channel msg operator id in it
  int get_init_channel_msg_cnt(uint64_t op_id, int64_t *&curr_piece_cnt);
public:
  common::ObArray<ObPxTask> tasks_;
  ObGranulePump gi_pump_;
  dtl::ObDtlChannelLoop msg_loop_;
  ObPxSubCoordMsgProc msg_proc_;
  ObPxReceiveDataChannelMsgP receive_data_ch_msg_proc_;
  ObPxTransmitDataChannelMsgP transmit_data_ch_msg_proc_;
  ObBarrierWholeMsgP barrier_whole_msg_proc_;
  ObWinbufWholeMsgP winbuf_whole_msg_proc_;
  ObDynamicSampleWholeMsgP sample_whole_msg_proc_;
  ObRollupKeyWholeMsgP rollup_key_whole_msg_proc_;
  ObRDWFWholeMsgP rd_wf_whole_msg_proc_;
  ObInitChannelWholeMsgP init_channel_whole_msg_proc_;
  ObReportingWFWholeMsgP reporting_wf_piece_msg_proc_;
  ObPxSqcInterruptedP interrupt_proc_;
  ObPxSQCProxy sqc_proxy_; // Provide control message communication service for each worker
  ObPxReceiveChProvider receive_data_ch_provider_;
  ObPxTransmitChProvider transmit_data_ch_provider_;
  bool all_tasks_finish_;
  bool interrupted_; // Mark whether the current SQC is interrupted by QC
  common::ObSEArray<ObPxTabletInfo, 8> partitions_info_;
  ObPxBloomfilterChProvider bf_ch_provider_;
  ObPxCreateBloomFilterChannelMsgP px_bloom_filter_msg_proc_;
  ObOptStatsGatherWholeMsgP opt_stats_gather_whole_msg_proc_;
  // Used for saving whole msg provider in datahub, generally there will not be one in a single sub-plan
  // More than one operator will use datahub, so the size can default to 1
  common::ObSEArray<ObPxDatahubDataProvider *, 1> whole_msg_provider_list_;
  common::ObSEArray<std::pair<int64_t, int64_t>, 1> init_channel_msg_cnts_; // <op_id, piece_cnt>
  ObSPWinFuncPXWholeMsgP sp_winfunc_whole_msg_proc_;
  ObRDWinFuncPXWholeMsgP rd_winfunc_whole_msg_proc_;
  ObJoinFilterCountRowWholeMsgP join_filter_count_row_whole_msg_proc_;
  /* for ddl */
  ObArenaAllocator arena_allocator_;
  ObIArray<ObTabletDirectLoadMgrHandle>* direct_load_mgr_handles_;
  ObIArray<ObTabletDirectLoadMgrHandle>* lob_direct_load_mgr_handles_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObSqcCtx);
};

}
}
#endif /* __OB_SQL_PX_SQC_CTX_H__ */
//// end of header file


