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

#define USING_LOG_PREFIX SQL_DTL

#include "observer/omt/ob_server_runtime_controller.h"
#include "ob_dtl_fc_server.h"
#include "share/rc/ob_module_provider.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::sql::dtl;
using namespace oceanbase::lib;
using namespace oceanbase::share;

// ObDfc
ObDfc::ObDfc()
: aggregate_dfc_(), blocked_dfc_cnt_(0), channel_total_cnt_(0), max_parallel_cnt_(0),
  max_blocked_buffer_size_(0), max_buffer_size_(0), mem_mgr_{}
{}

ObDfc::~ObDfc()
{}

int ObDfc::server_module_new(ObDfc *&dfc_manager)
{
  int ret = OB_SUCCESS;
  
  dfc_manager = static_cast<ObDfc *> (ob_malloc(sizeof(ObDfc), ObMemAttr("SqlDtlDfc")));
  if (OB_ISNULL(dfc_manager)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc DFC manager", K(ret));
  } else if (FALSE_IT(new (dfc_manager) ObDfc{})) {
  }
  return ret;
}


int ObDfc::server_module_init(ObDfc *&dfc_manager)
{
  int ret = OB_SUCCESS;
  
  if (OB_SUCC(ret)) {
    dfc_manager->channel_total_cnt_ = 0;
    dfc_manager->blocked_dfc_cnt_ = 0;
    dfc_manager->max_parallel_cnt_ = 0;
    dfc_manager->max_blocked_buffer_size_ = 0;
    dfc_manager->max_buffer_size_ = 0;
    
    if (OB_FAIL(dfc_manager->mem_mgr_.init())) {
      LOG_WARN("failed to init DTL memory manager", K(ret));
    }
    // dfc_manager->calc_max_buffer(10);
    LOG_INFO("init DFC manager", K(ret));
  }
  return ret;
}

void ObDfc::server_module_destroy(ObDfc *&dfc_manager)
{
  if (nullptr != dfc_manager) {
    LOG_INFO("trace DFC manager destroy");
    dfc_manager->mem_mgr_.destroy();
    common::ob_delete(dfc_manager);
    dfc_manager = nullptr;
  }
}

void ObDfc::check_dtl()
{
  int ret = OB_SUCCESS;
  {
    check_dtl_buffer_size();
    clean_on_timeout();
  }
}
void ObDfc::check_dtl_buffer_size()
{
  
  int ret = OB_SUCCESS;
  double min_cpu = 0;
  double max_cpu = 0;
  if (OB_ISNULL(GCTX.server_runtime_controller_)) {
  } else if (OB_FAIL(GCTX.server_runtime_controller_->get_server_cpu(min_cpu, max_cpu))) {
    LOG_WARN("fail to get CPU capacity", K(ret));
  } else {
    calc_max_buffer(lround(max_cpu) * DFC_CPU_RATIO);
  }
}

int ObDfc::clean_on_timeout()
{
  int ret = OB_SUCCESS;
  
  if (OB_FAIL(mem_mgr_.auto_free_on_time())) {
    LOG_WARN("failed to auto free memory manager", K(ret));
  }
  LOG_INFO("DFC manager status", K(ret), K(1UL),
    K(get_channel_cnt()),
    K(get_current_buffer_used()),
    K(get_current_blocked_cnt()),
    K(get_current_buffer_cnt()),
    K(get_max_parallel()),
    K(get_max_blocked_buffer_size()),
    K(get_max_buffer_size()),
    K(get_accumulated_blocked_cnt()),
    K(get_max_size_per_channel()));
  return ret;
}

void ObDfc::calc_max_buffer(int64_t max_parallel_cnt)
{
  if (0 == max_parallel_cnt) {
    max_parallel_cnt = 1;
  }
  max_parallel_cnt_ = max_parallel_cnt;
  // MAX_BUFFER_CNT indicates the maximum buffer data for an operator, +2 indicates a maximum of 2 at the transmit end, MAX_BUFFER_FACTOR indicates the floating ratio, /2 indicates that the maximum parallelism is half of max_parallel_cnt
  // Assume max_parallel_cnt_=1, then 1 * (4 + 2) * 64 * 1024 * 2 / 2, then maximum 6 buffer pages
  //    max_parallel_cnt_=10, then 10 * (4 + 2) * 64 * 1024 * 2 / 2, then maximum 60 buffer pages, assuming the maximum number of channels is 5*5*2=50,
  //       then each channel has 1.2 buffer pages, if an operator has 5 channels, then 1.2*5=6 buffer pages
  //    max_parallel_cnt_=600, then 600 * (4 + 2) * 64 * 1024 * 2 / 2, then maximum 3600 buffer pages
  //      Assume a 1:1 ratio, then for 300 concurrent SQLs, the maximum number of channels is 600, each dfc has about 6 buffers
  //      Assume 2 queries, each with 150*2, then the number of channels is approximately 150*150*2, each dfc has about 12 buffers
  max_blocked_buffer_size_ = max_parallel_cnt_ * (MAX_BUFFER_CNT + 2) * GCONF.dtl_buffer_size * MAX_BUFFER_FACTOR / 2;
  max_buffer_size_ = max_blocked_buffer_size_ * MAX_BUFFER_FACTOR;
  int64_t factor = 1;
  int ret = OB_SUCCESS;
  ret = OB_E(EventTable::EN_DFC_FACTOR) ret;
  if (OB_FAIL(ret)) {
    factor = -ret;
    max_buffer_size_ *= factor;
    max_blocked_buffer_size_ *= factor;
    ret = OB_SUCCESS;
  }
  LOG_INFO("trace DFC manager parameters", K(max_parallel_cnt_), K(max_blocked_buffer_size_), K(max_buffer_size_));
}

int ObDfc::register_dfc_channel(ObDtlFlowControl &dfc, ObDtlChannel* ch)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(dfc.register_channel(ch))) {
    LOG_WARN("failed to regiester channel", KP(ch->get_id()), K(ret));
  } else {
    increase_channel_cnt(1);
  }
  return ret;
}

int ObDfc::unregister_dfc_channel(ObDtlFlowControl &dfc, ObDtlChannel* ch)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_SUCCESS != (tmp_ret = dfc.unregister_channel(ch))) {
    ret = tmp_ret;
    LOG_WARN("failed to regiester channel", KP(ch->get_id()), K(ret));
  }
  if (OB_ENTRY_NOT_EXIST != ret) {
    decrease_channel_cnt(1);
  }
  return ret;
}

int ObDfc::deregister_dfc(ObDtlFlowControl &dfc)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  int64_t n_ch = dfc.get_channel_count();
  if (dfc.is_receive()) {
    ObDtlChannel* ch = nullptr;
    for (int i = 0; i < n_ch; ++i) {
      if (OB_SUCCESS != (tmp_ret = dfc.get_channel(i, ch))) {
        ret = tmp_ret;
        LOG_WARN("failed to free channel or no channel", K(i), K(dfc.get_channel_count()), K(n_ch), K(ret));
      } else if (nullptr == ch) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to free channel or no channel", K(i), K(dfc.get_channel_count()), K(n_ch), K(ret));
      }
    }
  }
  if (OB_SUCCESS != (tmp_ret = dfc.unregister_all_channel())) {
    ret = tmp_ret;
    LOG_ERROR("fail unregister all channel from dfc", KR(tmp_ret));
  }
  decrease_channel_cnt(n_ch);
  return ret;
}

int ObDfc::enforce_block(ObDtlFlowControl *dfc, int64_t ch_idx)
{
  int ret = OB_SUCCESS;
  if (!dfc->is_block(ch_idx)) {
    increase_blocked_channel_cnt();
    dfc->set_block(ch_idx);
    LOG_TRACE("receive set channel block trace", K(dfc), K(ret), K(ch_idx));
  }
  return ret;
}

int ObDfc::try_unblock_dfc(ObDtlFlowControl *dfc, int64_t ch_idx)
{
  int ret = OB_SUCCESS;
  if (dfc->is_block()) {
    int64_t unblock_cnt = 0;
    if (can_unblock(dfc)) {
      if (OB_FAIL(dfc->notify_all_blocked_channels_unblocking(unblock_cnt))) {
        LOG_WARN("failed to unblock all blocked channel", K(dfc), K(ch_idx), K(ret));
      }
      if (0 < unblock_cnt) {
        decrease_blocked_channel_cnt(unblock_cnt);
      }
      LOG_TRACE("unblock channel on decrease size", K(dfc), K(ret), K(unblock_cnt), K(ch_idx));
    } else if (dfc->is_block(ch_idx)) {
      ObDtlChannel *dtl_ch = nullptr;
      if (OB_FAIL(dfc->get_channel(ch_idx, dtl_ch))) {
        LOG_WARN("failed to get dtl channel", K(dfc), K(ch_idx), K(ret));
      } else {
        ObDtlBasicChannel *ch = reinterpret_cast<ObDtlBasicChannel*>(dtl_ch);
        int64_t unblock_cnt = 0;
        if (dfc->is_qc_coord() && ch->has_less_buffer_cnt()) {
          // For merge sort coord's channel, ensure that each channel's recv_list is not empty, i.e., extend unblock condition
          // Otherwise merge sort receive may deadlock, i.e., the blocked channel cannot send unblocking msg
          LOG_TRACE("unblock channel on decrease size by self", K(dfc), K(ret), KP(ch->get_id()), K(ch_idx),
            K(ch->get_processed_buffer_cnt()));
          if (OB_FAIL(dfc->notify_channel_unblocking(ch, unblock_cnt))) {
            LOG_WARN("failed to unblock channel",
              K(dfc), K(ret), KP(ch->get_id()), K(ch->belong_to_receive_data()),
              K(ch->belong_to_transmit_data()), K(ch->get_processed_buffer_cnt()));
          }
          decrease_blocked_channel_cnt(unblock_cnt);
        }
      }
    }
    LOG_TRACE("unblock channel on decrease size", K(dfc), K(ret), K(dfc->is_block()));
  }
  return ret;
}

int ObDfc::unblock_dfc(ObDtlFlowControl *dfc, int64_t ch_idx, int64_t size)
{
  int ret = OB_SUCCESS;
  dfc->decrease(size);
  decrease(size);
  if (OB_FAIL(try_unblock_dfc(dfc, ch_idx))) {
    LOG_WARN("failed to try unblock DFC manager", K(ret));
  }
  return ret;
}

int ObDfc::unblock_channel(ObDtlFlowControl *dfc, int64_t ch_idx)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(try_unblock_dfc(dfc, ch_idx))) {
    LOG_WARN("failed to unblock all blocked channel", K(dfc), K(ret));
  }
  return ret;
}

int ObDfc::unblock_channels(ObDtlFlowControl *dfc)
{
  int ret = OB_SUCCESS;
  if (dfc->is_block()) {
    int64_t unblock_cnt = 0;
    if (OB_FAIL(dfc->notify_all_blocked_channels_unblocking(unblock_cnt))) {
      LOG_WARN("failed to unblock all blocked channel", K(dfc), K(ret));
    }
    if (0 < unblock_cnt) {
      decrease_blocked_channel_cnt(unblock_cnt);
    }
    LOG_TRACE("unblock channel on decrease size", K(dfc), K(ret), K(unblock_cnt));
  }
  return ret;
}

int ObDfc::block_dfc(ObDtlFlowControl *dfc, int64_t ch_idx, int64_t size)
{
  int ret = OB_SUCCESS;
  dfc->increase(size);
  increase(size);
  //LOG_TRACE("DFC manager size", K(dfc->get_used()), K(dfc->get_total_buffer_cnt()), K(aggregate_dfc_.get_used()), K(aggregate_dfc_.get_total_buffer_cnt()), K(need_block(dfc)));
  if (need_block(dfc)) {
    if (OB_FAIL(enforce_block(dfc, ch_idx))) {
      LOG_WARN("failed to block channel", K(size), K(dfc), K(ret), K(ch_idx));
    }
  }
  return ret;
}
// dfc server
int ObDfcServer::init()
{
  int ret = OB_SUCCESS;
  return ret;
}

void ObDfcServer::destroy()
{
}

int ObDfcServer::get_current_dfc(ObDfc *&dfc_manager)
{
  int ret = OB_SUCCESS;
  dfc_manager = nullptr;
  dfc_manager = share::g_mp->dfc_manager();
  if (nullptr == dfc_manager) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to create DFC manager", K(ret));
  } else ;
  return ret;
}

ObDtlMemManager *ObDfcServer::get_mem_manager()
{
  int ret = OB_SUCCESS;
  ObDtlMemManager *memory_manager = nullptr;
  ObDfc *dfc_manager = nullptr;
  if (OB_FAIL(get_current_dfc(dfc_manager))) {
    LOG_WARN("failed to get DFC manager", K(ret));
  } else if (OB_ISNULL(dfc_manager)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("DFC manager is null", K(ret));
  } else {
    memory_manager = dfc_manager->get_mem_manager();
  }
  return memory_manager;
}

int ObDfcServer::block_on_increase_size(ObDtlFlowControl *dfc, int64_t ch_idx, int64_t size)
{
  int ret = OB_SUCCESS;
  
  ObDfc *dfc_manager = nullptr;
  if (OB_FAIL(get_current_dfc(dfc_manager))) {
    LOG_WARN("failed to get DFC manager", K(ret));
  } else if (OB_ISNULL(dfc_manager)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("DFC manager is null", K(ret));
  } else if (OB_FAIL(dfc_manager->block_dfc(dfc, ch_idx, size))) {
    LOG_WARN("failed to block DFC manager", K(ret));
  }
  return ret;
}

int ObDfcServer::unblock_on_decrease_size(ObDtlFlowControl *dfc, int64_t ch_idx, int64_t size)
{
  int ret = OB_SUCCESS;
  
  ObDfc *dfc_manager = nullptr;
  if (OB_FAIL(get_current_dfc(dfc_manager))) {
    LOG_WARN("failed to get DFC manager", K(ret));
  } else if (OB_ISNULL(dfc_manager)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("DFC manager is null", K(ret));
  } else if (OB_FAIL(dfc_manager->unblock_dfc(dfc, ch_idx, size))) {
    LOG_WARN("failed to unblock DFC manager", K(ch_idx), K(ret));
  }
  return ret;
}

int ObDfcServer::unblock_channel(ObDtlFlowControl *dfc, int64_t ch_idx)
{
  int ret = OB_SUCCESS;
  
  ObDfc *dfc_manager = nullptr;
  if (OB_FAIL(get_current_dfc(dfc_manager))) {
    LOG_WARN("failed to get DFC manager", K(ret));
  } else if (OB_FAIL(dfc_manager->unblock_channel(dfc, ch_idx))) {
    LOG_WARN("failed to unblock DFC manager", K(ret));
  }
  return ret;
}

int ObDfcServer::unblock_channels(ObDtlFlowControl *dfc)
{
  int ret = OB_SUCCESS;
  
  ObDfc *dfc_manager = nullptr;
  if (OB_FAIL(get_current_dfc(dfc_manager))) {
    LOG_WARN("failed to get DFC manager", K(ret));
  } else if (OB_ISNULL(dfc_manager)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("DFC manager is null", K(ret));
  } else if (OB_FAIL(dfc_manager->unblock_channels(dfc))) {
    LOG_WARN("failed to unblock DFC manager", K(ret));
  }
  return ret;
}

int ObDfcServer::register_dfc_channel(ObDtlFlowControl &dfc, ObDtlChannel* ch)
{
  int ret = OB_SUCCESS;
  
  ObDfc *dfc_manager = nullptr;
  if (OB_FAIL(get_current_dfc(dfc_manager))) {
    LOG_WARN("failed to get DFC manager", K(ret));
  } else if (OB_ISNULL(dfc_manager)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("DFC manager is null", K(ret));
  } else if (OB_FAIL(dfc_manager->register_dfc_channel(dfc, ch))) {
    LOG_WARN("failed to register dfc", K(ret));
  }
  return ret;
}

int ObDfcServer::unregister_dfc_channel(ObDtlFlowControl &dfc, ObDtlChannel* ch)
{
  int ret = OB_SUCCESS;
  
  ObDfc *dfc_manager = nullptr;
  if (OB_FAIL(get_current_dfc(dfc_manager))) {
    LOG_WARN("failed to get DFC manager", K(ret));
  } else if (OB_ISNULL(dfc_manager)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("DFC manager is null", K(ret));
  } else if (OB_FAIL(dfc_manager->unregister_dfc_channel(dfc, ch))) {
    LOG_WARN("failed to register dfc", K(ret));
  }
  return ret;
}

int ObDfcServer::register_dfc(ObDtlFlowControl &dfc)
{
  UNUSED(dfc);
  return OB_SUCCESS;
}

int ObDfcServer::deregister_dfc(ObDtlFlowControl &dfc)
{
  int ret = OB_SUCCESS;
  if (dfc.is_init()) {
    
    ObDfc *dfc_manager = nullptr;
    if (OB_FAIL(get_current_dfc(dfc_manager))) {
      LOG_WARN("failed to get DFC manager", K(ret));
    } else if (OB_ISNULL(dfc_manager)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("DFC manager is null", K(ret));
    } else if (OB_FAIL(dfc_manager->deregister_dfc(dfc))) {
      LOG_WARN("failed to deregister dfc", K(ret));
    }
  }
  return ret;
}
