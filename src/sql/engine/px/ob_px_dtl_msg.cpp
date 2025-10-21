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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_px_dtl_msg.h"
#include "sql/engine/ob_physical_plan_ctx.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::sql::dtl;

OB_SERIALIZE_MEMBER(ObPxTabletInfo,
                    tablet_id_,
                    logical_row_count_,
                    physical_row_count_);
OB_SERIALIZE_MEMBER(ObPxTaskMonitorInfo, sched_exec_time_start_, sched_exec_time_end_, exec_time_start_, exec_time_end_, metrics_);
OB_SERIALIZE_MEMBER((ObPxTaskChSet, dtl::ObDtlChSet), sqc_id_, task_id_);
OB_SERIALIZE_MEMBER(ObPxPartChMapItem, first_, second_, third_);
OB_SERIALIZE_MEMBER(ObPxReceiveDataChannelMsg, child_dfo_id_, ch_sets_, ch_total_info_, has_filled_channel_);
OB_SERIALIZE_MEMBER(ObPxTransmitDataChannelMsg, ch_sets_, part_affinity_map_, ch_total_info_, has_filled_channel_);
OB_SERIALIZE_MEMBER(ObPxInitSqcResultMsg, dfo_id_, sqc_id_, rc_, task_count_, err_msg_);
OB_SERIALIZE_MEMBER(ObPxFinishSqcResultMsg, dfo_id_, sqc_id_, rc_, trans_result_,
                    task_monitor_info_array_, sqc_affected_rows_, dml_row_info_, temp_table_id_,
                    interm_result_ids_, fb_info_, err_msg_, das_retry_rc_,
                    sqc_memstore_row_read_count_, sqc_ssstore_row_read_count_);
OB_SERIALIZE_MEMBER(ObPxFinishTaskResultMsg, dfo_id_, sqc_id_, task_id_, rc_);
OB_SERIALIZE_MEMBER((ObPxBloomFilterChInfo, dtl::ObDtlChTotalInfo), filter_id_);
OB_SERIALIZE_MEMBER((ObPxBloomFilterChSet, dtl::ObDtlChSet), filter_id_, sqc_id_);
OB_SERIALIZE_MEMBER(ObPxCreateBloomFilterChannelMsg, sqc_count_, sqc_id_, ch_set_info_);
OB_SERIALIZE_MEMBER(ObPxBloomFilterData, filter_, tenant_id_, filter_id_,
                    server_id_, px_sequence_id_, bloom_filter_count_);
OB_SERIALIZE_MEMBER(ObPxDmlRowInfo, row_match_count_, row_duplicated_count_, row_deleted_count_);
OB_SERIALIZE_MEMBER(ObPxTabletRange, tablet_id_, range_cut_, range_weights_);

int ObPxTaskChSet::assign(const ObPxTaskChSet &other)
{
  int ret = OB_SUCCESS;
  sqc_id_ = other.sqc_id_;
  task_id_ = other.task_id_;
  sm_group_id_ = other.sm_group_id_;
  if (OB_FAIL(dtl::ObDtlChSet::assign(other))) {
    LOG_WARN("fail assign ObPxTaskChSet", K(other), K(ret));
  }
  return ret;
}


void ObPxDmlRowInfo::set_px_dml_row_info(const ObPhysicalPlanCtx &plan_ctx)
{
  row_match_count_ = plan_ctx.get_row_matched_count();
  row_duplicated_count_ = plan_ctx.get_row_duplicated_count();
  row_deleted_count_ = plan_ctx.get_row_deleted_count();
}

ObPxTabletRange::ObPxTabletRange()
  : tablet_id_(OB_INVALID_ID), range_weights_(0), range_cut_()
{
  range_weights_ = 1;
}

void ObPxTabletRange::reset()
{
  tablet_id_ = OB_INVALID_ID;
  range_cut_.reset();
  range_weights_ = 1;
}

bool ObPxTabletRange::is_valid() const
{
  return tablet_id_ >= 0 && range_cut_.count() >= 0;
}

int ObPxTabletRange::assign(const ObPxTabletRange &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(range_cut_.assign(other.range_cut_))) {
    LOG_WARN("assign range cut failed", K(ret), K(other));
  } else {
    tablet_id_ = other.tablet_id_;
    range_weights_ = other.range_weights_;
  }
  return ret;
}

