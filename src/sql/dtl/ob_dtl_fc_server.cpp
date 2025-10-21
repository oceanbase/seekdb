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

#include "ob_dtl_fc_server.h"
#include "sql/dtl/ob_dtl_rpc_channel.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::sql::dtl;
using namespace oceanbase::lib;
using namespace oceanbase::share;

// ObTenantDfc
ObTenantDfc::ObTenantDfc(uint64_t tenant_id)
: tenant_dfc_(), tenant_id_(tenant_id), blocked_dfc_cnt_(0), channel_total_cnt_(0), max_parallel_cnt_(0),
  max_blocked_buffer_size_(0), max_buffer_size_(0), tenant_mem_mgr_(tenant_id)
{}

ObTenantDfc::~ObTenantDfc()
{}

int ObTenantDfc::mtl_new(ObTenantDfc *&tenant_dfc)
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = MTL_ID();
  tenant_dfc = static_cast<ObTenantDfc *> (ob_malloc(sizeof(ObTenantDfc), ObMemAttr(tenant_id, "SqlDtlDfc")));
  if (OB_ISNULL(tenant_dfc)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc tenant dfc", K(ret));
  } else if (FALSE_IT(new (tenant_dfc) ObTenantDfc(tenant_id))) {
  }
  return ret;
}


int ObTenantDfc::mtl_init(ObTenantDfc *&tenant_dfc)
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = MTL_ID();
  if (OB_SUCC(ret)) {
    tenant_dfc->channel_total_cnt_ = 0;
    tenant_dfc->blocked_dfc_cnt_ = 0;
    tenant_dfc->max_parallel_cnt_ = 0;
    tenant_dfc->max_blocked_buffer_size_ = 0;
    tenant_dfc->max_buffer_size_ = 0;
    tenant_dfc->tenant_id_ = tenant_id;
    if (OB_FAIL(tenant_dfc->tenant_mem_mgr_.init())) {
      LOG_WARN("failed to init tenant memory manager", K(ret));
    }
    // tenant_dfc->calc_max_buffer(10);
    LOG_INFO("init tenant dfc", K(ret), K(tenant_dfc->tenant_id_));
  }
  return ret;
}

void ObTenantDfc::mtl_destroy(ObTenantDfc *&tenant_dfc)
{
  if (nullptr != tenant_dfc) {
    LOG_INFO("trace tenant dfc destroy", K(tenant_dfc->tenant_id_));
    tenant_dfc->tenant_mem_mgr_.destroy();
    common::ob_delete(tenant_dfc);
    tenant_dfc = nullptr;
  }
}

void ObTenantDfc::check_dtl(uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  if (tenant_id != get_tenant_id()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected status: tenant_id is not match",
      K(tenant_id), K(get_tenant_id()), K(ret));
  } else {
    check_dtl_buffer_size();
    clean_on_timeout();
  }
}
void ObTenantDfc::check_dtl_buffer_size()
{
  uint64_t tenant_id = get_tenant_id();
  int ret = OB_SUCCESS;
  double min_cpu = 0;
  double max_cpu = 0;
  if (OB_ISNULL(GCTX.omt_)) {
  } else if (OB_FAIL(GCTX.omt_->get_tenant_cpu(tenant_id, min_cpu, max_cpu))) {
    LOG_WARN("fail to get tenant cpu", K(ret));
  } else {
    calc_max_buffer(lround(max_cpu) * DFC_CPU_RATIO);
  }
}

int ObTenantDfc::clean_on_timeout()
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = get_tenant_id();
  if (OB_FAIL(tenant_mem_mgr_.auto_free_on_time())) {
    LOG_WARN("failed to auto free memory manager", K(ret));
  }
  LOG_INFO("tenant dfc status", K(ret), K(get_tenant_id()),
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

void ObTenantDfc::calc_max_buffer(int64_t max_parallel_cnt)
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
  LOG_INFO("trace tenant dfc parameters", K(max_parallel_cnt_), K(max_blocked_buffer_size_), K(max_buffer_size_));
}

int ObTenantDfc::register_dfc_channel(ObDtlFlowControl &dfc, ObDtlChannel* ch)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(dfc.register_channel(ch))) {
    LOG_WARN("failed to regiester channel", KP(ch->get_id()), K(ret));
  } else {
    increase_channel_cnt(1);
  }
  return ret;
}

int ObTenantDfc::unregister_dfc_channel(ObDtlFlowControl &dfc, ObDtlChannel* ch)
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

int ObTenantDfc::deregister_dfc(ObDtlFlowControl &dfc)
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

int ObTenantDfc::enforce_block(ObDtlFlowControl *dfc, int64_t ch_idx)
{
  int ret = OB_SUCCESS;
  if (!dfc->is_block(ch_idx)) {
    increase_blocked_channel_cnt();
    dfc->set_block(ch_idx);
    LOG_TRACE("receive set channel block trace", K(dfc), K(ret), K(ch_idx));
  }
  return ret;
}

int ObTenantDfc::try_unblock_tenant_dfc(ObDtlFlowControl *dfc, int64_t ch_idx)
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
          LOG_TRACE("unblock channel on decrease size by self", K(dfc), K(ret), KP(ch->get_id()), K(ch->get_peer()), K(ch_idx),
            K(ch->get_processed_buffer_cnt()));
          if (OB_FAIL(dfc->notify_channel_unblocking(ch, unblock_cnt))) {
            LOG_WARN("failed to unblock channel",
              K(dfc), K(ret), KP(ch->get_id()), K(ch->get_peer()), K(ch->belong_to_receive_data()),
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

int ObTenantDfc::unblock_tenant_dfc(ObDtlFlowControl *dfc, int64_t ch_idx, int64_t size)
{
  int ret = OB_SUCCESS;
  dfc->decrease(size);
  decrease(size);
  if (OB_FAIL(try_unblock_tenant_dfc(dfc, ch_idx))) {
    LOG_WARN("failed to try unblock tenant dfc", K(ret));
  }
  return ret;
}

int ObTenantDfc::unblock_channel(ObDtlFlowControl *dfc, int64_t ch_idx)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(try_unblock_tenant_dfc(dfc, ch_idx))) {
    LOG_WARN("failed to unblock all blocked channel", K(dfc), K(ret));
  }
  return ret;
}

int ObTenantDfc::unblock_channels(ObDtlFlowControl *dfc)
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

int ObTenantDfc::block_tenant_dfc(ObDtlFlowControl *dfc, int64_t ch_idx, int64_t size)
{
  int ret = OB_SUCCESS;
  dfc->increase(size);
  increase(size);
  //LOG_TRACE("tenant dfc size", K(dfc->get_used()), K(dfc->get_total_buffer_cnt()), K(tenant_dfc_.get_used()), K(tenant_dfc_.get_total_buffer_cnt()), K(need_block(dfc)));
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

int ObDfcServer::get_current_tenant_dfc(uint64_t tenant_id, ObTenantDfc *&tenant_dfc)
{
  int ret = OB_SUCCESS;
  tenant_dfc = nullptr;
  tenant_dfc = MTL(ObTenantDfc*);
  if (nullptr == tenant_dfc) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to create tenant dfc", K(ret), K(tenant_id));
  } else if (tenant_id != tenant_dfc->get_tenant_id()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("unexpected tenant mtl", K(tenant_id), K(tenant_dfc->get_tenant_id()));
    // if (OB_SYS_TENANT_ID == tenant_dfc->get_tenant_id()) {
    //   // This is to solve the issue that after sys tenant changes tenant to another tenant, it should be able to use the dtl service
    //   // otherwise there is a bug:
    //   //
    //   // The scenario for entering this branch is: the init_sqc rpc did not find tenant resources on this machine,
    //   // Thus fallback to sys tenant, thus MTL retrieves the dfc tenant id as sys tenant id
    //   //
    //   // At this time: return the dfc resource of the sys tenant to the caller
    // } else {
    //   ret = OB_ERR_UNEXPECTED;
    //   LOG_WARN("the tenant id of tenant dfc is not match with tenant id hinted",
    //     K(ret), K(tenant_id), K(tenant_dfc->get_tenant_id()));
    // }
  }
  return ret;
}

ObDtlTenantMemManager *ObDfcServer::get_tenant_mem_manager(int64_t tenant_id)
{
  int ret = OB_SUCCESS;
  ObDtlTenantMemManager *tenant_mem_manager = nullptr;
  ObTenantDfc *tenant_dfc = nullptr;
  if (OB_FAIL(get_current_tenant_dfc(tenant_id, tenant_dfc))) {
    LOG_WARN("failed to get tenant dfc", K(tenant_id), K(ret));
  } else if (OB_ISNULL(tenant_dfc)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant dfc is null", K(tenant_id), K(ret));
  } else {
    tenant_mem_manager = tenant_dfc->get_tenant_mem_manager();
  }
  return tenant_mem_manager;
}

int ObDfcServer::block_on_increase_size(ObDtlFlowControl *dfc, int64_t ch_idx, int64_t size)
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = dfc->get_tenant_id();
  ObTenantDfc *tenant_dfc = nullptr;
  if (OB_FAIL(get_current_tenant_dfc(tenant_id, tenant_dfc))) {
    LOG_WARN("failed to get tenant dfc", K(tenant_id), K(ret));
  } else if (OB_ISNULL(tenant_dfc)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant dfc is null", K(tenant_id), K(ret));
  } else if (OB_FAIL(tenant_dfc->block_tenant_dfc(dfc, ch_idx, size))) {
    LOG_WARN("failed to block tenant dfc", K(tenant_id), K(ret));
  }
  return ret;
}

int ObDfcServer::unblock_on_decrease_size(ObDtlFlowControl *dfc, int64_t ch_idx, int64_t size)
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = dfc->get_tenant_id();
  ObTenantDfc *tenant_dfc = nullptr;
  if (OB_FAIL(get_current_tenant_dfc(tenant_id, tenant_dfc))) {
    LOG_WARN("failed to get tenant dfc", K(tenant_id), K(ret));
  } else if (OB_ISNULL(tenant_dfc)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant dfc is null", K(tenant_id), K(ret));
  } else if (OB_FAIL(tenant_dfc->unblock_tenant_dfc(dfc, ch_idx, size))) {
    LOG_WARN("failed to unblock tenant dfc", K(tenant_id), K(ch_idx), K(ret));
  }
  return ret;
}

int ObDfcServer::unblock_channel(ObDtlFlowControl *dfc, int64_t ch_idx)
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = dfc->get_tenant_id();
  ObTenantDfc *tenant_dfc = nullptr;
  if (OB_FAIL(get_current_tenant_dfc(tenant_id, tenant_dfc))) {
    LOG_WARN("failed to get tenant dfc", K(tenant_id), K(ret));
  } else if (OB_FAIL(tenant_dfc->unblock_channel(dfc, ch_idx))) {
    LOG_WARN("failed to unblock tenant dfc", K(tenant_id), K(ret));
  }
  return ret;
}

int ObDfcServer::unblock_channels(ObDtlFlowControl *dfc)
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = dfc->get_tenant_id();
  ObTenantDfc *tenant_dfc = nullptr;
  if (OB_FAIL(get_current_tenant_dfc(tenant_id, tenant_dfc))) {
    LOG_WARN("failed to get tenant dfc", K(tenant_id), K(ret));
  } else if (OB_ISNULL(tenant_dfc)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant dfc is null", K(tenant_id), K(ret));
  } else if (OB_FAIL(tenant_dfc->unblock_channels(dfc))) {
    LOG_WARN("failed to unblock tenant dfc", K(tenant_id), K(ret));
  }
  return ret;
}

int ObDfcServer::register_dfc_channel(ObDtlFlowControl &dfc, ObDtlChannel* ch)
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = dfc.get_tenant_id();
  ObTenantDfc *tenant_dfc = nullptr;
  if (OB_FAIL(get_current_tenant_dfc(tenant_id, tenant_dfc))) {
    LOG_WARN("failed to get tenant dfc", K(tenant_id), K(ret));
  } else if (OB_ISNULL(tenant_dfc)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant dfc is null", K(tenant_id), K(ret));
  } else if (OB_FAIL(tenant_dfc->register_dfc_channel(dfc, ch))) {
    LOG_WARN("failed to register dfc", K(tenant_id), K(ret));
  }
  return ret;
}

int ObDfcServer::unregister_dfc_channel(ObDtlFlowControl &dfc, ObDtlChannel* ch)
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = dfc.get_tenant_id();
  ObTenantDfc *tenant_dfc = nullptr;
  if (OB_FAIL(get_current_tenant_dfc(tenant_id, tenant_dfc))) {
    LOG_WARN("failed to get tenant dfc", K(tenant_id), K(ret));
  } else if (OB_ISNULL(tenant_dfc)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant dfc is null", K(tenant_id), K(ret));
  } else if (OB_FAIL(tenant_dfc->unregister_dfc_channel(dfc, ch))) {
    LOG_WARN("failed to register dfc", K(tenant_id), K(ret));
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
    uint64_t tenant_id = dfc.get_tenant_id();
    ObTenantDfc *tenant_dfc = nullptr;
    if (OB_FAIL(get_current_tenant_dfc(tenant_id, tenant_dfc))) {
      LOG_WARN("failed to get tenant dfc", K(tenant_id), K(ret));
    } else if (OB_ISNULL(tenant_dfc)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tenant dfc is null", K(tenant_id), K(ret));
    } else if (OB_FAIL(tenant_dfc->deregister_dfc(dfc))) {
      LOG_WARN("failed to deregister dfc", K(tenant_id), K(ret));
    }
  }
  return ret;
}
