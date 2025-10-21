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
#include "sql/engine/px/ob_sqc_ctx.h"

using namespace oceanbase::sql;

ObSqcCtx::ObSqcCtx(ObPxRpcInitSqcArgs &sqc_arg) : msg_loop_(),
      msg_proc_(sqc_arg, *this),
      receive_data_ch_msg_proc_(msg_proc_),
      transmit_data_ch_msg_proc_(msg_proc_),
      barrier_whole_msg_proc_(msg_proc_),
      winbuf_whole_msg_proc_(msg_proc_),
      sample_whole_msg_proc_(msg_proc_),
      rollup_key_whole_msg_proc_(msg_proc_),
      rd_wf_whole_msg_proc_(msg_proc_),
      init_channel_whole_msg_proc_(msg_proc_),
      reporting_wf_piece_msg_proc_(msg_proc_),
      interrupt_proc_(msg_proc_),
      sqc_proxy_(*this, sqc_arg),
      receive_data_ch_provider_(sqc_proxy_.get_msg_ready_cond()),
      transmit_data_ch_provider_(sqc_proxy_.get_msg_ready_cond()),
      all_tasks_finish_(false),
      interrupted_(false),
      bf_ch_provider_(sqc_proxy_.get_msg_ready_cond()),
      px_bloom_filter_msg_proc_(msg_proc_),
      opt_stats_gather_whole_msg_proc_(msg_proc_),
      sp_winfunc_whole_msg_proc_(msg_proc_),
      rd_winfunc_whole_msg_proc_(msg_proc_),
      join_filter_count_row_whole_msg_proc_(msg_proc_),
      arena_allocator_(),
      direct_load_mgr_handles_(nullptr),
      lob_direct_load_mgr_handles_(nullptr)
{
  arena_allocator_.set_attr(ObMemAttr(MTL_ID(),"DDL_DLM"));
}

int ObSqcCtx::add_whole_msg_provider(uint64_t op_id, dtl::ObDtlMsgType msg_type, ObPxDatahubDataProvider &provider)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(provider.init(op_id, msg_type))) {
    LOG_WARN("failed to init provider");
  } else if (OB_FAIL(whole_msg_provider_list_.push_back(&provider))) {
    LOG_WARN("failed to push_back provider");
  }
  return ret;
}

int ObSqcCtx::get_whole_msg_provider(uint64_t op_id, dtl::ObDtlMsgType msg_type, ObPxDatahubDataProvider *&provider)
{
  int ret = OB_SUCCESS;
  provider = nullptr;
  for (int i = 0; OB_SUCC(ret) && i < whole_msg_provider_list_.count(); ++i) {
    if (OB_ISNULL(whole_msg_provider_list_.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("should never be nullptr, unexpected", K(ret));
    } else if (op_id == whole_msg_provider_list_.at(i)->op_id_
               && msg_type == whole_msg_provider_list_.at(i)->msg_type_) {
      provider = whole_msg_provider_list_.at(i);
      break;
    }
  }
  // Expected to traverse operators and register providers when sqc starts
  if (nullptr == provider) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("should have a whole msg provider for op", K(op_id), K(ret), K(msg_type));
  }
  return ret;
}

int ObSqcCtx::get_init_channel_msg_cnt(uint64_t op_id, int64_t *&curr_piece_cnt)
{
  int ret = OB_SUCCESS;
  curr_piece_cnt = nullptr;
  for (int64_t i = 0; i < init_channel_msg_cnts_.count(); ++i) {
    if (op_id == init_channel_msg_cnts_.at(i).first) {
      curr_piece_cnt = &init_channel_msg_cnts_.at(i).second;
      break;
    }
  }
  if (nullptr == curr_piece_cnt) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("should have a init channel msg cnt for op", K(ret), K(op_id));
  }
  return ret;
}
