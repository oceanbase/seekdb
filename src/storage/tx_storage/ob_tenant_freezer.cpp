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

#define USING_LOG_PREFIX STORAGE

#include "lib/stat/ob_diagnostic_info_guard.h"
#include "ob_tenant_freezer.h"
#include "share/rc/ob_module_provider.h"
#include "lib/ob_running_mode.h"
#include "observer/ob_srv_network_frame.h"
#include "share/ob_ex_rpc.h"
#include "storage/tx_storage/ob_tenant_freezer_rpc.h"
#include "rootserver/freeze/ob_major_freeze_helper.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/multi_data_source/runtime_utility/mds_tenant_service.h"
#include "observer/ob_server_event_history_table_operator.h"
#include "storage/allocator/ob_memstore_allocator.h"  // relocated-definition owner
#include "storage/memtable/ob_memtable.h"  // needed by relocated functions

namespace oceanbase
{
using namespace share;
namespace storage
{
using namespace mds;


double ObTenantFreezer::MDS_TABLE_FREEZE_TRIGGER_TENANT_PERCENTAGE = 2;


ObTenantFreezer::ObTenantFreezer()
	: is_inited_(false),
    is_freezing_tx_data_(false),
    freeze_trigger_tg_id_(-1),
    freeze_trigger_timer_task_(*this),
    freeze_thread_pool_(),
    freeze_thread_pool_lock_(common::ObLatchIds::FREEZE_THREAD_POOL_LOCK),
    freezer_stat_(),
    freezer_history_(),
    throttle_is_skipping_cache_(),
    memstore_remain_memory_is_exhausting_cache_()
{
  freezer_stat_.reset();
}

ObTenantFreezer::~ObTenantFreezer()
{
	destroy();
}

void ObTenantFreezer::destroy()
{
  TG_DESTROY(freeze_trigger_tg_id_);
  is_freezing_tx_data_ = false;
  self_.reset();
  freezer_stat_.reset();
  freezer_history_.reset();
  throttle_is_skipping_cache_.reset();
  memstore_remain_memory_is_exhausting_cache_.reset();

  is_inited_ = false;
}

int ObTenantFreezer::mtl_init(ObTenantFreezer* &m)
{
  return m->init();
}

int ObTenantFreezer::init()
{
	int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("[TenantFreezer] tenant freezer init twice.", KR(ret));
  } else if (OB_UNLIKELY(!GCONF.self_addr_.is_valid()) ||
             OB_ISNULL(GCTX.net_frame_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[TenantFreezer] invalid argument", KR(ret), K(GCONF.self_addr_));
  } else if (OB_FAIL(freeze_thread_pool_.init_and_start(FREEZE_THREAD_NUM, 10, "FrzAsync"))) {
    LOG_WARN("[TenantFreezer] fail to initialize freeze thread pool", KR(ret));
  } else if (OB_FAIL(TG_CREATE_TENANT(lib::TGDefIDs::TenantFreezer, freeze_trigger_tg_id_))) {
    LOG_WARN("[TenantFreezer] fail to create TenantFreezer timer tg", KR(ret));
  } else if (OB_FAIL(TG_START(freeze_trigger_tg_id_))) {
    LOG_WARN("[TenantFreezer] fail to start TenantFreezer timer", K(ret));
  } else {
    is_freezing_tx_data_ = false;
    self_ = GCONF.self_addr_;
    
    freezer_stat_.reset();
    freezer_history_.reset();
    is_inited_ = true;
  }
  return ret;
}

void ObTenantFreezer::run_timer_task()
{
  LOG_INFO("====== tenant freeze timer task ======");
  this->do_freeze_diagnose();
  this->check_and_do_freeze();
}

int ObTenantFreezer::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant freezer not inited", KR(ret));
  } else if (OB_FAIL(TG_SCHEDULE(freeze_trigger_tg_id_, freeze_trigger_timer_task_,
                                 FREEZE_TRIGGER_INTERVAL, true/*repeat*/, false/*immediate*/))) {
    LOG_WARN("[TenantFreezer] fail to schedule freeze_trigger_timer_task", KR(ret));
  } else {
    LOG_INFO("[TenantFreezer] ObTenantFreezer start", K_(tenant_info));
  }
  return ret;
}

int ObTenantFreezer::stop()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant freezer not inited", KR(ret));
  } else {
    TG_STOP(freeze_trigger_tg_id_);
    // task_list_.stop_all();
    LOG_INFO("[TenantFreezer] ObTenantFreezer stoped done", K_(tenant_info));
  }
  return ret;
}

void ObTenantFreezer::wait()
{
  TG_WAIT(freeze_trigger_tg_id_);
  // task_list_.wait_all();
  LOG_INFO("[TenantFreezer] ObTenantFreezer wait done", K_(tenant_info));
}

bool ObTenantFreezer::exist_ls_freezing()
{
  int ret = OB_SUCCESS;
  bool exist_ls_freezing = false;
  common::ObSharedGuard<ObLSIterator> iter;
  ObLSService *ls_srv = share::g_mp->ls_service();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant freezer not inited", KR(ret));
  } else if (OB_FAIL(ls_srv->get_ls_iter(iter, ObLSGetMod::TXSTORAGE_MOD))) {
    LOG_WARN("[TenantFreezer] fail to get log stream iterator", KR(ret));
  } else {
    ObLS *ls = nullptr;
    while (OB_SUCC(iter->get_next(ls))) {
      if (ls->get_freezer()->is_ls_freeze_running()) {
        exist_ls_freezing = true;
        break;
      }
    }

    if (ret == OB_ITER_END) {
      ret = OB_SUCCESS;
    } 
    
    if (OB_FAIL(ret)) {
      LOG_WARN("[TenantFreezer] iter ls failed", K(ret));
    }
  }

  return exist_ls_freezing;
}

bool ObTenantFreezer::exist_ls_throttle_is_skipping()
{
  int ret = OB_SUCCESS;
  int64_t cur_ts = ObClockGenerator::getClock();
  int64_t last_update_ts = throttle_is_skipping_cache_.update_ts_;

  if ((cur_ts - last_update_ts > UPDATE_INTERVAL) &&
      ATOMIC_BCAS(&throttle_is_skipping_cache_.update_ts_, last_update_ts, cur_ts)) {
    bool exist_ls_throttle_is_skipping = false;

    common::ObSharedGuard<ObLSIterator> iter;
    ObLSService *ls_srv = share::g_mp->ls_service();
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      LOG_WARN("[TenantFreezer] tenant freezer not inited", KR(ret));
    } else if (OB_FAIL(ls_srv->get_ls_iter(iter, ObLSGetMod::TXSTORAGE_MOD))) {
      LOG_WARN("[TenantFreezer] fail to get log stream iterator", KR(ret));
    } else {
      ObLS *ls = nullptr;
      while (OB_SUCC(iter->get_next(ls))) {
        if (ls->get_freezer()->throttle_is_skipping()) {
          exist_ls_throttle_is_skipping = true;
          break;
        }
      }
      if (ret == OB_ITER_END) {
        ret = OB_SUCCESS;
      }

      if (OB_FAIL(ret)) {
        LOG_WARN("[TenantFreezer] iter ls failed", K(ret));
      }
    }

    // assign need_skip_throttle here because if some error happened, the value can be reset to false
    throttle_is_skipping_cache_.value_ = exist_ls_throttle_is_skipping;
  }

  return throttle_is_skipping_cache_.value_;
}

bool ObTenantFreezer::memstore_remain_memory_is_exhausting()
{
  int ret = OB_SUCCESS;
  int64_t cur_ts = ObClockGenerator::getClock();
  int64_t last_update_ts = memstore_remain_memory_is_exhausting_cache_.update_ts_;

  if ((cur_ts - last_update_ts > UPDATE_INTERVAL) &&
      ATOMIC_BCAS(&memstore_remain_memory_is_exhausting_cache_.update_ts_, last_update_ts, cur_ts)) {
    bool remain_mem_exhausting = false;
    if (false == tenant_info_.is_loaded_) {
      LOG_INFO("[TenantFreezer] This tenant not exist", KR(ret));
    } else {
      const int64_t MEMORY_IS_EXHAUSTING_PERCENTAGE = 10;

      // tenant memory condition
      const int64_t tenant_memory_limit = get_tenant_memory_limit();
      const int64_t tenant_memory_remain = get_tenant_memory_remain();
      const bool tenant_memory_exhausting =
          tenant_memory_remain < (tenant_memory_limit * MEMORY_IS_EXHAUSTING_PERCENTAGE / 100);

      // memstore memory condition
      const int64_t memstore_limit = tenant_info_.get_memstore_limit();
      const int64_t memstore_remain = (memstore_limit - get_tenant_memory_hold(ObCtxIds::MEMSTORE_CTX_ID));
      const bool memstore_memory_exhausting = memstore_remain < (memstore_limit * MEMORY_IS_EXHAUSTING_PERCENTAGE / 100);

      remain_mem_exhausting = tenant_memory_exhausting || memstore_memory_exhausting;

      if (remain_mem_exhausting && REACH_TIME_INTERVAL(1LL * 1000LL * 1000LL /* 1 second */)) {
        STORAGE_LOG(INFO,
                    "[TenantFreezer] memstore remain memory is exhausting",
                    K(tenant_memory_limit),
                    K(tenant_memory_remain),
                    K(tenant_memory_exhausting),
                    K(memstore_limit),
                    K(memstore_remain),
                    K(memstore_memory_exhausting));
      }
    }

    memstore_remain_memory_is_exhausting_cache_.value_ = remain_mem_exhausting;
  }

  return memstore_remain_memory_is_exhausting_cache_.value_;
}

int ObTenantFreezer::ls_freeze_data_(ObLS *ls)
{
  int ret = OB_SUCCESS;
  const int64_t SLEEP_TS = 1000 * 1000; // 1s
  const int64_t abs_timeout_ts = ObClockGenerator::getClock() + TENANT_FREEZE_RETRY_TIME_US;
  int64_t retry_times = 0;
  const bool is_sync = true;
  bool is_timeout = false;
  bool need_retry = false;
  // wait and retry if there is a freeze is doing
  // or if we can not get the ls lock.
  do {
    need_retry = false;
    retry_times++;
    if (OB_SUCC(ls->logstream_freeze(is_sync,
                                     abs_timeout_ts,
                                     ObFreezeSourceFlag::FREEZE_TRIGGER))) {
    } else {
      need_retry = (ObClockGenerator::getClock() < abs_timeout_ts) && (OB_EAGAIN == ret);
    }
    if (need_retry) {
      ob_usleep(SLEEP_TS);
    }
    if (retry_times % 10 == 0) {
      LOG_WARN_RET(OB_ERR_TOO_MUCH_TIME, "wait ls freeze finished cost too much time", K(retry_times));
    }
  } while (need_retry);
  if (OB_NOT_RUNNING == ret) {
    ret = OB_SUCCESS;
  }
  return ret;
}

int ObTenantFreezer::ls_freeze_all_unit_(ObLS *ls,
                                         const int64_t abs_timeout_ts,
                                         const ObFreezeSourceFlag source)
{
  int ret = OB_SUCCESS;
  const int64_t SLEEP_TS = 1000 * 1000; // 1s
  int64_t current_ts = 0;
  int64_t retry_times = 0;
  bool is_timeout = false;
  bool need_retry = false;
  // wait and retry if we can not get the ls lock.
  do {
    need_retry = false;
    retry_times++;
    if (OB_SUCC(ls->advance_checkpoint_by_flush(SCN::max_scn(),
                                                abs_timeout_ts,
                                                true, /* is_tenant_freeze */
                                                source))) {
    } else {
      current_ts = ObTimeUtil::current_time();
      is_timeout = (current_ts >= abs_timeout_ts);
      // retry condition 1
      need_retry = (!is_timeout);
      // retry condition 2
      need_retry = need_retry && (OB_EAGAIN == ret);
    }
    if (need_retry) {
      ob_usleep(SLEEP_TS);
    }
    if (retry_times % 10 == 0) {
      LOG_WARN_RET(OB_ERR_TOO_MUCH_TIME, "wait ls freeze finished cost too much time", K(retry_times));
    }
  } while (need_retry);
  if (OB_NOT_RUNNING == ret) {
    ret = OB_SUCCESS;
  }
  return ret;
}

int ObTenantFreezer::tenant_freeze_data_()
{
  int ret = OB_SUCCESS;
  int first_fail_ret = OB_SUCCESS;
  common::ObSharedGuard<ObLSIterator> iter;
  ObLSService *ls_srv = share::g_mp->ls_service();
  FLOG_INFO("[TenantFreezer] tenant_freeze start", KR(ret));

  ObTenantFreezeGuard freeze_guard(ret, tenant_info_);
  if (OB_FAIL(ls_srv->get_ls_iter(iter, ObLSGetMod::TXSTORAGE_MOD))) {
    LOG_WARN("[TenantFreezer] fail to get log stream iterator", KR(ret));
  } else {
    ObLS *ls = nullptr;
    int ls_cnt = 0;
    for (; OB_SUCC(iter->get_next(ls)); ++ls_cnt) {
      // wait until this ls freeze finished to make sure not freeze frequently because
      // of this ls freeze stuck.
      if (OB_FAIL(ls_freeze_data_(ls))) {
        if (OB_SUCCESS == first_fail_ret) {
          first_fail_ret = ret;
        }
        LOG_ERROR("[TenantFreezer] fail to freeze logstream", KR(ret), K(ls->get_ls_id()));
      }
    }
    if (ret == OB_ITER_END) {
      ret = OB_SUCCESS;
      if (ls_cnt > 0) {
        LOG_INFO("[TenantFreezer] succeed to freeze tenant", KR(ret), K(ls_cnt));
      } else {
        LOG_WARN("[TenantFreezer] no logstream", KR(ret), K(ls_cnt));
      }
    }
    if (first_fail_ret != OB_SUCCESS &&
        first_fail_ret != OB_ITER_END) {
      ret = first_fail_ret;
    }
  }

  return ret;
}

// only called by user triggered minor freeze
int ObTenantFreezer::tenant_freeze(const ObFreezeSourceFlag source)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObLSService *ls_svr = share::g_mp->ls_service();
  common::ObSharedGuard<ObLSIterator> guard;
  ObLSIterator *iter = NULL;
  ObLS *ls = nullptr;
  int ls_cnt = 0;
  int64_t abs_timeout_ts = INT64_MAX;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant freezer not inited", KR(ret));
  } else if (OB_FAIL(ObShareUtil::get_abs_timeout(MAX_FREEZE_TIMEOUT_US /* default timeout */,
                                                  abs_timeout_ts))) {
    LOG_WARN("get timeout ts failed", KR(ret));
  } else if (OB_FAIL(ls_svr->get_ls_iter(guard, ObLSGetMod::TXSTORAGE_MOD))) {
    LOG_WARN("get log stream iter failed", K(ret));
  } else if (OB_ISNULL(iter = guard.get_ptr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("iter is NULL", K(ret));
  } else if (OB_FAIL(set_tenant_freezing_())) {
    LOG_WARN("set tenant freeze failed", K(ret));
  } else {
    for (; OB_SUCC(iter->get_next(ls)); ++ls_cnt) {
      if (OB_TMP_FAIL(ls_freeze_all_unit_(ls, abs_timeout_ts, source))) {
        LOG_WARN("ls freeze all unit failed", K(tmp_ret), K(ls->get_ls_id()));
      }
    }
    if (ret == OB_ITER_END) {
      ret = OB_SUCCESS;
    }
  }

  if (OB_SUCC(ret)) {
    freezer_stat_.add_freeze_event();
  }
  if (OB_TMP_FAIL(unset_tenant_freezing_(OB_FAIL(ret)))) {
    LOG_WARN("unset tenant freeze failed", KR(tmp_ret));
  }

  LOG_INFO("tenant_freeze finished", KR(ret), K(abs_timeout_ts));

  return ret;
}

int ObTenantFreezer::ls_freeze_all_unit(const share::ObLSID &ls_id,
                                        const ObFreezeSourceFlag source)
{
  int ret = OB_SUCCESS;
  ObLSService *ls_srv = share::g_mp->ls_service();
  ObLSHandle handle;
  ObLS *ls = nullptr;
  const bool need_rewrite_tablet_meta = false;
  int64_t abs_timeout_ts = 0;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant freezer not inited", KR(ret));
  } else if (OB_FAIL(ObShareUtil::get_abs_timeout(MAX_FREEZE_TIMEOUT_US /* default timeout */,
                                                  abs_timeout_ts))) {
    LOG_WARN("get timeout ts failed", KR(ret));
  } else if (OB_FAIL(ls_srv->get_ls(ls_id, handle, ObLSGetMod::TXSTORAGE_MOD))) {
    LOG_WARN("[TenantFreezer] fail to get ls", K(ret), K(ls_id));
  } else if (OB_ISNULL(ls = handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[TenantFreezer] ls is null", KR(ret), K(ls_id));
  } else if (OB_FAIL(ls_freeze_all_unit_(ls, abs_timeout_ts, source))) {
    LOG_WARN("[TenantFreezer] logstream freeze failed", KR(ret), K(ls_id));
  }

  return ret;
}

int ObTenantFreezer::tablet_freeze(share::ObLSID ls_id,
                                   const common::ObTabletID &tablet_id,
                                   const bool is_sync,
                                   const int64_t max_retry_time_us,
                                   const bool need_rewrite_tablet_meta,
                                   const ObFreezeSourceFlag source)
{
  int ret = OB_SUCCESS;
  bool is_cache_hit = false;
  ObLSService *ls_srv = share::g_mp->ls_service();
  ObLSHandle handle;
  ObLS *ls = nullptr;
  // 0 as default timeout ts
  const int64_t abs_timeout_ts = (0 == max_retry_time_us) ? 0 : ObClockGenerator::getClock() + max_retry_time_us;

  FLOG_INFO("[TenantFreezer] tablet_freeze start",
            KR(ret),
            K(ls_id),
            K(is_sync),
            K(need_rewrite_tablet_meta),
            K(tablet_id),
            KTIME(abs_timeout_ts));

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant freezer not inited", KR(ret));
  } else if (!ls_id.is_valid()) {
    LOG_ERROR("[TenantFreezer] ls id can not be invalid", KR(ret), K(tablet_id));
  } else if (OB_FAIL(ls_srv->get_ls(ls_id, handle, ObLSGetMod::TXSTORAGE_MOD))) {
    LOG_WARN("[TenantFreezer] fail to get ls", K(ret), K(ls_id));
  } else if (OB_ISNULL(ls = handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[TenantFreezer] ls is null", KR(ret), K(ls_id));
  } else if (OB_FAIL(ls->tablet_freeze(tablet_id,
                                       is_sync,
                                       abs_timeout_ts,
                                       need_rewrite_tablet_meta,
                                       source))) {
    LOG_ERROR("[TenantFreezer] fail to freeze tablet", KR(ret), K(ls_id), K(tablet_id));
    if (OB_NOT_RUNNING == ret) {
      ret = OB_SUCCESS;
    }
  }

  return ret;
}

int ObTenantFreezer::check_and_freeze_normal_data_(ObTenantFreezeCtx &ctx)
{

  int ret = OB_SUCCESS;
  bool upgrade_mode = GCONF.in_major_version_upgrade_mode();
  int tmp_ret = OB_SUCCESS;
  bool need_freeze = false;
  if (OB_UNLIKELY(upgrade_mode)) {
    // skip trigger freeze while upgrading
  } else {
    if (OB_FAIL(get_freeze_trigger_(ctx))) {
      LOG_WARN("[TenantFreezer] fail to get minor freeze trigger", KR(ret));
    } else if (OB_FAIL(get_tenant_mem_usage_(ctx))) {
      LOG_WARN("[TenantFreezer] fail to get mem usage", KR(ret));
    } else {
      need_freeze = need_freeze_(ctx);
      log_frozen_memstore_info_if_need_(ctx);
      halt_prewarm_if_need_(ctx);
    }
    // must out of the lock, to make sure there is no deadlock, just because of tenant freeze hung.
    if (OB_TMP_FAIL(do_major_if_need_(need_freeze))) {
      LOG_WARN("[TenantFreezer] fail to do major freeze", K(tmp_ret));
    }
    if (need_freeze) {
      if (OB_TMP_FAIL(do_minor_freeze_data_(ctx))) {
        LOG_WARN("[TenantFreezer] fail to do minor freeze", K(tmp_ret));
      }
    }
  }
  return ret;
}


static const int64_t ONE_MB = 1024L * 1024L;
#define STATISTIC_PRINT_MACRO                                               \
  "Tenant Total Memory(MB)", total_memory/ONE_MB,                           \
  "Tenant Frozen TxData Memory(MB)", frozen_tx_data_mem_used/ONE_MB,        \
  "Tenant Active TxData Memory(MB)", active_tx_data_mem_used/ONE_MB,        \
  "Freeze TxData Trigger Memory(MB)", self_freeze_trigger_memory/ONE_MB,    \
  "Total TxDataTable Hold Memory(MB)", tx_data_mem_hold/ONE_MB,             \
  "Total TxDataTable Memory Limit(MB)", tx_data_mem_limit/ONE_MB
int ObTenantFreezer::check_and_freeze_tx_data_()
{
  int ret = OB_SUCCESS;
  int64_t frozen_tx_data_mem_used = 0;
  int64_t active_tx_data_mem_used = 0;
  int64_t total_memory = lib::get_tenant_memory_limit();
  int64_t tx_data_mem_hold = lib::get_tenant_memory_hold(ObCtxIds::TX_DATA_TABLE);
  int64_t self_freeze_trigger_memory =
      total_memory * ObTenantTxDataAllocator::TX_DATA_FREEZE_TRIGGER_PERCENTAGE / 100;
  int64_t tx_data_mem_limit = total_memory * ObTenantTxDataAllocator::TX_DATA_LIMIT_PERCENTAGE / 100;

  static int skip_count = 0;
  bool need_re_freeze = false;
  if (true == ATOMIC_LOAD(&is_freezing_tx_data_)) {
    // skip freeze when there is another self freeze task is running
    if (++skip_count > 10) {
      int64_t cost_time = (FREEZE_TRIGGER_INTERVAL * skip_count);
      LOG_WARN_RET(OB_ERR_TOO_MUCH_TIME,
                   "A tx data tenant self freeze task cost too much time",
                   K(skip_count),
                   K(cost_time));
    }
  } else if (OB_FAIL(get_tx_data_info_for_freeze_(frozen_tx_data_mem_used, active_tx_data_mem_used, need_re_freeze))) {
    LOG_WARN("[TenantFreezer] get tenant tx data mem used failed.", KR(ret));
  } else if (need_re_freeze || active_tx_data_mem_used > self_freeze_trigger_memory) {
    // trigger tx data self freeze
    if (OB_FAIL(post_tx_data_freeze_request_())) {
      LOG_WARN("[TenantFreezer] fail to do tx data self freeze", KR(ret));
    }

    LOG_INFO("[TenantFreezer] Trigger Tx Data Table Self Freeze", STATISTIC_PRINT_MACRO);
  }

  // execute statistic print once a minute
  if (TC_REACH_TIME_INTERVAL(60 * 1000 * 1000)) {
    int tmp_ret = OB_SUCCESS;
    if (frozen_tx_data_mem_used + active_tx_data_mem_used > tx_data_mem_limit) {
      LOG_INFO("tx data use too much memory!!!", STATISTIC_PRINT_MACRO);
    } else if (OB_FAIL(get_tx_data_info_for_freeze_(
                   frozen_tx_data_mem_used, active_tx_data_mem_used, need_re_freeze, true /*for_statistic_print*/))) {
      LOG_INFO("print statistic failed");
    } else {
      LOG_INFO("TxData Memory Statistic : ", STATISTIC_PRINT_MACRO);
    }
  }
  return ret;
}
#undef STATISTIC_PRINT_MACRO

int ObTenantFreezer::get_tx_data_info_for_freeze_(int64_t &tenant_tx_data_frozen_mem_used,
                                                  int64_t &tenant_tx_data_active_mem_used,
                                                  bool &need_re_freeze,
                                                  bool for_statistic_print)
{
  int ret = OB_SUCCESS;
  tenant_tx_data_frozen_mem_used = 0;
  tenant_tx_data_active_mem_used = 0;
  common::ObSharedGuard<ObLSIterator> iter;
  ObLSService *ls_srv = share::g_mp->ls_service();

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant freezer not inited", KR(ret));
  } else if (OB_FAIL(ls_srv->get_ls_iter(iter, ObLSGetMod::TXSTORAGE_MOD))) {
    LOG_WARN("[TenantFreezer] fail to get log stream iterator", KR(ret));
  } else {
    ObLS *ls = nullptr;
    int ls_cnt = 0;
    for (; OB_SUCC(ret) && OB_SUCC(iter->get_next(ls)); ++ls_cnt) {
      int tmp_ret = OB_SUCCESS;
      int64_t ls_tx_data_frozen_mem_used = 0;
      int64_t ls_tx_data_active_mem_used = 0;
      if (!for_statistic_print && OB_NOT_NULL(ls) && ls->tx_table_need_re_freeze()) {
        need_re_freeze = true;
        break;
      }

      if (OB_TMP_FAIL(get_ls_tx_data_memory_info_(
              ls, ls_tx_data_frozen_mem_used, ls_tx_data_active_mem_used, for_statistic_print))) {
        LOG_WARN("[TenantFreezer] fail to get tx data mem used in one ls", KR(ret), K(ls->get_ls_id()));
      } else {
        tenant_tx_data_frozen_mem_used += ls_tx_data_frozen_mem_used;
        tenant_tx_data_active_mem_used += ls_tx_data_active_mem_used;
      }
    }

    if (ret == OB_ITER_END) {
      ret = OB_SUCCESS;
      if (0 == ls_cnt) {
        LOG_WARN("[TenantFreezer] no logstream", KR(ret), K(ls_cnt), K(tenant_info_));
      }
    }
  }
  return ret;
}


int ObTenantFreezer::get_ls_tx_data_memory_info_(ObLS *ls,
                                                 int64_t &ls_tx_data_frozen_mem_used,
                                                 int64_t &ls_tx_data_active_mem_used,
                                                 bool for_statistic_print)
{
  int ret = OB_SUCCESS;
  ObMemtableMgrHandle mgr_handle;
  ObTxDataMemtableMgr *memtable_mgr = nullptr;
  ObSEArray<ObTableHandleV2, 2> memtable_handles;
  ObTxDataMemtable *memtable = nullptr;
  if (OB_ISNULL(ls)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[TenantFreezer] get ls tx data mem used failed.", KR(ret));
  } else if (OB_FAIL(ls->get_tablet_svr()->get_tx_data_memtable_mgr(mgr_handle))) {
    LOG_WARN("[TenantFreezer] get tx data memtable mgr failed.", KR(ret));
  } else if (OB_ISNULL(memtable_mgr
                       = static_cast<ObTxDataMemtableMgr *>(mgr_handle.get_memtable_mgr()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[TenantFreezer] tx data memtable mgr is unexpected nullptr.", KR(ret));
  } else if (OB_FAIL(memtable_mgr->get_all_memtables(memtable_handles))) {
    LOG_WARN("get active memtable from tx data memtable mgr failed.", KR(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < memtable_handles.count(); i++) {
      if (OB_FAIL(memtable_handles.at(i).get_tx_data_memtable(memtable))) {
        LOG_ERROR("get tx data memtable failed.", KR(ret));
      } else if (OB_ISNULL(memtable)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("unexpected nullptr of tx data memtable", KR(ret));
      } else if (memtable->is_active_memtable()) {
        // the last memtable means active tx data memtable
        ls_tx_data_active_mem_used = memtable->get_occupied_size();
      } else {
        // the other frozen memtable
        ls_tx_data_frozen_mem_used += memtable->get_occupied_size();
      }

      if (OB_FAIL(ret)) {
        ret = OB_SUCCESS;
      }
    }
  }

  if (for_statistic_print) {
    LOG_INFO("TxData Memory Statistic(logstream info): ",
             "ls_id", ls->get_ls_id(),
             "Frozen TxData Memory(MB)", ls_tx_data_frozen_mem_used/ONE_MB,
             "Active TxData Memory(MB)", ls_tx_data_active_mem_used/ONE_MB);
  }

  return ret;
}

// design document : 
int ObTenantFreezer::check_and_freeze_mds_table_()
{
  int ret = OB_SUCCESS;

  if (REACH_TIME_INTERVAL(10 * 1000 * 1000 /*10 seconds*/)) {
    bool trigger_flush = false;
    int64_t total_memory = lib::get_tenant_memory_limit();
    int64_t trigger_freeze_memory = total_memory * (ObTenantFreezer::MDS_TABLE_FREEZE_TRIGGER_TENANT_PERCENTAGE / 100);
    ObTenantMdsAllocator &mds_allocator = share::g_mp->shared_mem_alloc_mgr()->mds_allocator();
    int64_t hold_memory = mds_allocator.hold();

    if (OB_UNLIKELY(0 == trigger_freeze_memory)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("invalid trigger freeze memory",
                K(trigger_freeze_memory),
                K(total_memory),
                K(ObTenantFreezer::MDS_TABLE_FREEZE_TRIGGER_TENANT_PERCENTAGE));
    } else if (hold_memory >= trigger_freeze_memory) {
      int tmp_ret = OB_SUCCESS;
      if (OB_TMP_FAIL(post_mds_table_freeze_request_())) {
        LOG_WARN("[TenantFreezer] fail to do mds table self freeze", K(tmp_ret));
      }

      LOG_INFO(
          "[TenantFreezer] Trigger Mds Table Self Freeze. ", KR(tmp_ret), K(total_memory), K(trigger_freeze_memory));
    }
  }

  return ret;
}


int ObTenantFreezer::do_freeze_diagnose()
{
  int ret = OB_SUCCESS;
  ObMemstoreAllocator &tenant_allocator = share::g_mp->shared_mem_alloc_mgr()->memstore_allocator();
  const int64_t current_time = ObTimeUtility::current_time();
  const int64_t capture_time_interval = 1_min;
  

  if (current_time - freezer_stat_.last_captured_timestamp_ >= 30 * 1_min) {
    int64_t current_retire_clock = tenant_allocator.get_retire_clock();

    if (freezer_stat_.last_captured_timestamp_ == 0) {
      // The first time we start capturing
      ATOMIC_SET(&freezer_stat_.last_captured_retire_clock_, current_retire_clock);
    } else {
      ATOMIC_FAA(&freezer_stat_.captured_data_size_, current_retire_clock
                 - ATOMIC_LOAD(&freezer_stat_.last_captured_retire_clock_));
      ATOMIC_SET(&freezer_stat_.last_captured_retire_clock_, current_retire_clock);

      (void)freezer_stat_.print_activity_metrics();
      (void)freezer_history_.add_activity_metric(freezer_stat_);

      (void)report_freezer_source_events();
    }

    freezer_stat_.last_captured_timestamp_ = current_time;
    freezer_stat_.refresh();
  }

  return ret;
}

void ObTenantFreezer::record_freezer_source_event(const ObLSID &ls_id,
                                                  const ObFreezeSourceFlag source)
{
  if (is_valid_freeze_source((source))) {
    ATOMIC_AAF(&freezer_stat_.captured_source_times_[static_cast<int64_t>(source)], 1);
    STORAGE_LOG(INFO, "[Freezer] freeze from source", K(ls_id), "freeze_source", obj_to_cstring(source));
  }
}

void ObTenantFreezer::report_freezer_source_events()
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;

  TRANS_LOG(INFO, "[TENANT_FREEZER_EVENT] print freeze source");
  char server_event_value[MAX_ROOTSERVICE_EVENT_VALUE_LENGTH] = {0};

  ret = common::databuff_printf(server_event_value,
                                MAX_ROOTSERVICE_EVENT_VALUE_LENGTH,
                                pos,
                                "[");

  for (int64_t i = 0; OB_SUCC(ret) && i < MAX_FREEZE_SOURCE_TYPE_COUNT; i++) {
    if (is_valid_freeze_source((ObFreezeSourceFlag(i)))) {
      int64_t captured_source_times = ATOMIC_LOAD(&(freezer_stat_.captured_source_times_[i]));
      TRANS_LOG(INFO, "[TENANT_FREEZER_EVENT] print source", K(i),
                "source_type", obj_to_cstring(ObFreezeSourceFlag(i)),
                K(captured_source_times));
      ret = common::databuff_printf(server_event_value,
                                    MAX_ROOTSERVICE_EVENT_VALUE_LENGTH,
                                    pos,
                                    "%s: %ld; ",
                                    obj_to_cstring(ObFreezeSourceFlag(i)),
                                    captured_source_times);
    }
  }

  if (OB_SUCC(ret)) {
      ret = common::databuff_printf(server_event_value,
                                    MAX_ROOTSERVICE_EVENT_VALUE_LENGTH,
                                    pos,
                                    "]");
  }

  if (OB_SUCC(ret)) {
    SERVER_EVENT_ADD("freezer", "freeze_source_statistics",
                     "source_statistics", server_event_value);
  } else {
    TRANS_LOG(WARN, "[TENANT_FREEZER_EVENT] print source failed", K(ret));
  }
}

int ObTenantFreezer::check_and_do_freeze()
{
  int ret = OB_SUCCESS;

  int64_t check_and_freeze_start_ts = ObTimeUtil::current_time();
  ObTenantFreezeCtx ctx;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant manager not init", KR(ret));
  } else if (!tenant_info_.is_loaded_) {
    // do nothing
  } else if (FALSE_IT(tenant_info_.get_freeze_ctx(ctx))) {
  } else {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(check_and_freeze_normal_data_(ctx))) {
      LOG_WARN("[TenantFreezer] check and freeze normal data failed.", KR(tmp_ret));
    }

    tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(check_and_freeze_tx_data_())) {
      LOG_WARN("[TenantFreezer] check and freeze tx data failed.", KR(tmp_ret));
    }

    tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(check_and_freeze_mds_table_())) {
      LOG_WARN("[TenantFreezer] check and freeze mds table failed.", KR(tmp_ret));
    }
  }

  int64_t check_and_freeze_end_ts = ObTimeUtil::current_time();
  int64_t spend_time = check_and_freeze_end_ts - check_and_freeze_start_ts;
  if (spend_time > 2_s) {
    STORAGE_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "check and do freeze spend too much time", K(spend_time));
  }
  return ret;
}

int ObTenantFreezer::retry_failed_major_freeze_(bool &triggered)
{
  int ret = OB_SUCCESS;

  if (get_retry_major_info().is_valid()) {
    LOG_INFO("A major freeze is needed due to previous failure");
    if (OB_FAIL(do_major_freeze_(get_retry_major_info().frozen_scn_))) {
      LOG_WARN("major freeze failed", K(ret));
    }
    triggered = true;
  }

  return ret;
}

int ObTenantFreezer::set_tenant_freezing_()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant manager not init", KR(ret));
  } else {
    ATOMIC_AAF(&tenant_info_.freeze_cnt_, 1);
  }
  return ret;
}

int ObTenantFreezer::unset_tenant_freezing_(const bool rollback_freeze_cnt)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant manager not init", KR(ret));
  } else {
    if (rollback_freeze_cnt) {
      if (ATOMIC_AAF(&tenant_info_.freeze_cnt_, -1) < 0) {
        tenant_info_.freeze_cnt_ = 0;
      }
    }
  }
  return ret;
}

int ObTenantFreezer::set_tenant_slow_freeze(
    const common::ObTabletID &tablet_id,
    const int64_t retire_clock)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant manager not init", KR(ret));
  } else {
    tenant_info_.set_slow_freeze(tablet_id, retire_clock, FREEZE_TRIGGER_INTERVAL);
  }
  return ret;
}

int ObTenantFreezer::unset_tenant_slow_freeze(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant manager not init", KR(ret));
  } else {
    tenant_info_.unset_slow_freeze(tablet_id);
  }
  return ret;
}

bool ObTenantFreezer::is_tenant_mem_changed(const int64_t curr_lower_limit,
                                            const int64_t curr_upper_limit) const
{
  int ret = OB_SUCCESS;
  bool is_changed = false;
  int64_t old_lower_limit = 0;
  int64_t old_upper_limit = 0;
  
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant manager not init", KR(ret));
  } else if (false == tenant_info_.is_loaded_) {
    is_changed = true;
  } else {
    // 1. tenant memory limit changed
    tenant_info_.get_mem_limit(old_lower_limit, old_upper_limit);
    is_changed = (is_changed ||
                  old_lower_limit != curr_lower_limit ||
                  old_upper_limit != curr_upper_limit);
  }
  if (is_changed) {
    LOG_INFO("tenant memory changed",
             "before_min", old_lower_limit,
             "before_max", old_upper_limit,
             "after_min", curr_lower_limit,
             "after_max", curr_upper_limit);
  }
  return is_changed;
}

int ObTenantFreezer::set_tenant_mem_limit(const int64_t lower_limit,
                                          const int64_t upper_limit)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant manager not init", KR(ret));
  } else if (OB_UNLIKELY(lower_limit < 0)
             || OB_UNLIKELY(upper_limit < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[TenantFreezer] invalid argument", KR(ret), K(lower_limit), K(upper_limit));
  } else {
    const int64_t freeze_trigger_percentage = get_freeze_trigger_percentage_();
    const int64_t memstore_limit_percent = get_memstore_limit_percentage_();
    if (memstore_limit_percent > 100 ||
        memstore_limit_percent <= 0 ||
        freeze_trigger_percentage > 100 ||
        freeze_trigger_percentage <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("[TenantFreezer] memstore limit percent in ObServerConfig is invaild",
               "memstore limit percent",
               memstore_limit_percent,
               "minor freeze trigger percent",
               freeze_trigger_percentage,
               KR(ret));
    } else {
      
      ObTenantFreezeCtx ctx;
      tenant_info_.update_mem_limit(lower_limit, upper_limit);
      tenant_info_.update_memstore_limit(memstore_limit_percent);
      tenant_info_.is_loaded_ = true;
      tenant_info_.get_freeze_ctx(ctx);
      if (OB_FAIL(get_freeze_trigger_(ctx))) {
        LOG_WARN("[TenantFreezer] fail to get minor freeze trigger", KR(ret));
      }
      if (OB_SUCC(ret)) {
        LOG_INFO("[TenantFreezer] set tenant mem limit",
                 "mem_lower_limit", lower_limit,
                 "mem_upper_limit", upper_limit,
                 "mem_memstore_limit", ctx.mem_memstore_limit_,
                 "memstore_freeze_trigger_limit", ctx.memstore_freeze_trigger_,
                 "mem_tenant_limit", get_tenant_memory_limit(),
                 "mem_tenant_hold", get_tenant_memory_hold(),
                 "mem_memstore_used", get_tenant_memory_hold(ObCtxIds::MEMSTORE_CTX_ID));
      }
    }
  }
  return ret;
}

int ObTenantFreezer::get_tenant_mem_limit(
    int64_t &lower_limit,
    int64_t &upper_limit) const
{
  int ret = OB_SUCCESS;
  lower_limit = 0;
  upper_limit = 0;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant manager not init", KR(ret));
  } else {
    
    if (false == tenant_info_.is_loaded_) {
      ret = OB_NOT_REGISTERED;
    } else {
      tenant_info_.get_mem_limit(lower_limit, upper_limit);
    }
  }
  return ret;
}

bool ObTenantFreezer::is_replay_pending_log_too_large(const int64_t pending_size)
{
  int ret = OB_SUCCESS;
  bool bool_ret = true;
  int64_t total_memstore_used = 0;
  int64_t memstore_limit = 0;
  int64_t unused = 0;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant manager not init", KR(ret));
  } else if (OB_FAIL(get_tenant_memstore_cond(unused,
                                              total_memstore_used,
                                              unused,
                                              memstore_limit,
                                              unused,
                                              false/* not force refresh */))) {
    LOG_WARN("get tenant memstore condition failed", K(ret));
  } else {
    int64_t memstore_left = memstore_limit - total_memstore_used - REPLAY_RESERVE_MEMSTORE_BYTES;
    memstore_left = (memstore_left > 0 ? memstore_left : 0);
    memstore_left >>= 5; // Estimate the size of memstore based on 32 times expansion.
                         // 16 times for replay and 16 times for replay
    bool_ret = (pending_size >= memstore_left);
  }
  return bool_ret;
}


int ObTenantFreezer::get_tenant_memstore_cond(int64_t &active_memstore_used,
                                              int64_t &total_memstore_used,
                                              int64_t &memstore_freeze_trigger,
                                              int64_t &memstore_limit,
                                              int64_t &freeze_cnt,
                                              const bool force_refresh)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant manager not init", KR(ret));
  } else if (OB_FAIL(get_tenant_memstore_cond_(active_memstore_used,
                                               total_memstore_used,
                                               memstore_freeze_trigger,
                                               memstore_limit,
                                               freeze_cnt,
                                               force_refresh))) {
    LOG_WARN("get tenant memstore used failed", K(ret));
  }
  return ret;
}

int ObTenantFreezer::get_tenant_memstore_cond_(
    int64_t &active_memstore_used,
    int64_t &total_memstore_used,
    int64_t &memstore_freeze_trigger,
    int64_t &memstore_limit,
    int64_t &freeze_cnt,
    const bool force_refresh)
{
  int ret = OB_SUCCESS;
  int64_t unused = 0;
  int64_t current_time = ObClockGenerator::getClock();
  RLOCAL_INIT(int64_t, last_refresh_timestamp, 0);
  RLOCAL(int64_t, last_active_memstore_used);
  RLOCAL(int64_t, last_total_memstore_used);
  RLOCAL(int64_t, last_memstore_freeze_trigger);
  RLOCAL(int64_t, last_memstore_limit);
  RLOCAL(int64_t, last_freeze_cnt);
  ObTenantFreezeCtx ctx;

  active_memstore_used = 0;
  total_memstore_used = 0;
  memstore_freeze_trigger = 0;
  memstore_limit = 0;

  if (!force_refresh &&
      current_time - last_refresh_timestamp < MEMSTORE_USED_CACHE_REFRESH_INTERVAL) {
    active_memstore_used = last_active_memstore_used;
    total_memstore_used = last_total_memstore_used;
    memstore_freeze_trigger = last_memstore_freeze_trigger;
    memstore_limit = last_memstore_limit;
    freeze_cnt = last_freeze_cnt;
  } else {
    
    if (false == tenant_info_.is_loaded_) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_INFO("[TenantFreezer] This tenant not exist", KR(ret));
    } else if (FALSE_IT(tenant_info_.get_freeze_ctx(ctx))) {
    } else if (OB_FAIL(get_tenant_mem_usage_(ctx))) {
      LOG_WARN("[TenantFreezer] failed to get tenant mem usage", KR(ret));
    } else if (OB_FAIL(get_freeze_trigger_(ctx))) {
      LOG_WARN("[TenantFreezer] fail to get minor freeze trigger", KR(ret));
    } else {
      memstore_limit = ctx.mem_memstore_limit_;
      active_memstore_used = ctx.active_memstore_used_;
      total_memstore_used = ctx.total_memstore_used_;
      memstore_freeze_trigger = ctx.memstore_freeze_trigger_ + ctx.max_cached_memstore_size_;
      freeze_cnt = tenant_info_.freeze_cnt_;

      // cache the result
      last_refresh_timestamp = current_time;
      last_active_memstore_used = active_memstore_used;
      last_total_memstore_used = total_memstore_used;
      last_memstore_freeze_trigger = memstore_freeze_trigger;
      last_memstore_limit = memstore_limit;
      last_freeze_cnt = freeze_cnt;
    }
  }
  return ret;
}

int ObTenantFreezer::get_tenant_memstore_limit(int64_t &mem_limit)
{
  int ret = OB_SUCCESS;
  mem_limit = INT64_MAX;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant manager not init", KR(ret));
  } else {
    
    if (false == tenant_info_.is_loaded_) {
      mem_limit = INT64_MAX;
      LOG_INFO("[TenantFreezer] This tenant not exist", KR(ret));
    } else {
      mem_limit = tenant_info_.get_memstore_limit();
    }
  }
  return ret;
}

int64_t ObTenantFreezer::get_memstore_limit_percentage()
{
  return get_memstore_limit_percentage_();
}

int ObTenantFreezer::get_tenant_mem_usage_(ObTenantFreezeCtx &ctx)
{
  int ret = OB_SUCCESS;
  ObMemstoreAllocator &tenant_allocator = share::g_mp->shared_mem_alloc_mgr()->memstore_allocator();

  int64_t active_memstore_used = 0;
  int64_t freezable_active_memstore_used = 0;
  int64_t total_memstore_used = 0;
  int64_t total_memstore_hold = 0;
  int64_t max_cached_memstore_size = 0;

  
  active_memstore_used = tenant_allocator.get_active_memstore_used();
  freezable_active_memstore_used = tenant_allocator.get_freezable_active_memstore_used();
  total_memstore_used = tenant_allocator.get_total_memstore_used();
  max_cached_memstore_size = tenant_allocator.get_max_cached_memstore_size();
  total_memstore_hold = get_tenant_memory_hold(ObCtxIds::MEMSTORE_CTX_ID);
  ctx.active_memstore_used_ = active_memstore_used;
  ctx.freezable_active_memstore_used_ = freezable_active_memstore_used;
  ctx.total_memstore_used_ = total_memstore_used;
  ctx.total_memstore_hold_ = total_memstore_hold;
  ctx.max_cached_memstore_size_ = max_cached_memstore_size;

  return ret;
}

int ObTenantFreezer::get_tenant_mem_stat_(ObTenantStatistic &stat)
{
  int ret = OB_SUCCESS;
  ObMemstoreAllocator &tenant_allocator = share::g_mp->shared_mem_alloc_mgr()->memstore_allocator();
  int64_t active_memstore_used = 0;
  int64_t total_memstore_used = 0;
  int64_t total_memstore_hold = 0;
  int64_t max_cached_memstore_size = 0;

  int64_t memstore_allocated_pos = 0;
  int64_t memstore_frozen_pos = 0;
  int64_t memstore_reclaimed_pos = 0;

  
  ObTenantFreezeCtx ctx;
  tenant_info_.get_freeze_ctx(ctx);
  if (OB_FAIL(get_freeze_trigger_(ctx))) {
    LOG_WARN("[TenantFreezer] get tenant minor freeze trigger error", KR(ret));
  } else {
    active_memstore_used = tenant_allocator.get_active_memstore_used();
    total_memstore_used = tenant_allocator.get_total_memstore_used();
    total_memstore_hold = get_tenant_memory_hold(ObCtxIds::MEMSTORE_CTX_ID);
    max_cached_memstore_size = tenant_allocator.get_max_cached_memstore_size();
    memstore_allocated_pos = tenant_allocator.get_memstore_allocated_pos();
    memstore_frozen_pos = tenant_allocator.get_frozen_memstore_pos();
    memstore_reclaimed_pos = tenant_allocator.get_memstore_reclaimed_pos();
  }
  stat.active_memstore_used_ = active_memstore_used;
  stat.total_memstore_used_ = total_memstore_used;
  stat.total_memstore_hold_ = total_memstore_hold;
  stat.memstore_freeze_trigger_ = ctx.memstore_freeze_trigger_;
  stat.memstore_limit_ = ctx.mem_memstore_limit_;
  stat.tenant_memory_limit_ = get_tenant_memory_limit();
  stat.tenant_memory_hold_ = get_tenant_memory_hold();
  stat.max_cached_memstore_size_ = max_cached_memstore_size;
  stat.memstore_can_get_now_ = ctx.max_mem_memstore_can_get_now_;

  stat.memstore_allocated_pos_ = memstore_allocated_pos;
  stat.memstore_frozen_pos_ = memstore_frozen_pos;
  stat.memstore_reclaimed_pos_ = memstore_reclaimed_pos;

  return ret;
}

static inline bool is_add_overflow(int64_t first, int64_t second, int64_t &res)
{
  if (first + second < 0) {
    return true;
  } else {
    res = first + second;
    return false;
  }
}

int ObTenantFreezer::get_freeze_trigger_(ObTenantFreezeCtx &ctx)
{
  static const int64_t MEMSTORE_USABLE_REMAIN_MEMORY_PERCETAGE = 50;
  static const int64_t MAX_UNUSABLE_MEMORY = 2LL * 1024LL * 1024LL * 1024LL;

  int ret = OB_SUCCESS;
  ObTenantResourceMgrHandle resource_handle;
  
  const int64_t mem_memstore_limit = ctx.mem_memstore_limit_;
  int64_t memstore_freeze_trigger = 0;
  int64_t max_mem_memstore_can_get_now = 0;
  int64_t tenant_remain_memory = get_tenant_memory_remain();
  int64_t tenant_memstore_hold = get_tenant_memory_hold(ObCtxIds::MEMSTORE_CTX_ID);
  int64_t usable_remain_memory = tenant_remain_memory / 100 * MEMSTORE_USABLE_REMAIN_MEMORY_PERCETAGE;
  if (tenant_remain_memory > MAX_UNUSABLE_MEMORY) {
    usable_remain_memory = std::max(usable_remain_memory, tenant_remain_memory - MAX_UNUSABLE_MEMORY);
  }

  bool is_overflow = true;
  if (is_add_overflow(usable_remain_memory, tenant_memstore_hold, max_mem_memstore_can_get_now)) {
    if (REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
      LOG_WARN("[TenantFreezer] max memstore can get is overflow",
               K(tenant_memstore_hold),
               K(usable_remain_memory),
               K(tenant_remain_memory));
    }
  } else {
    is_overflow = false;
  }

  int64_t min = mem_memstore_limit;
  if (!is_overflow) {
    min = MIN(mem_memstore_limit, max_mem_memstore_can_get_now);
  }

  memstore_freeze_trigger = min / 100 * get_freeze_trigger_percentage_();

  // result
  ctx.max_mem_memstore_can_get_now_ = max_mem_memstore_can_get_now;
  ctx.memstore_freeze_trigger_ = memstore_freeze_trigger;

  return ret;
}

int ObTenantFreezer::check_memstore_full_(bool &last_result,
                                          int64_t &last_check_timestamp,
                                          bool &is_out_of_mem,
                                          const bool from_user)
{
  int ret = OB_SUCCESS;
  int64_t current_time = ObClockGenerator::getClock();
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant manager not init", KR(ret));
  } else {
    
    if (!last_result &&
        current_time - last_check_timestamp < MEMSTORE_USED_CACHE_REFRESH_INTERVAL) {
      // Check once when the last memory burst or tenant does not match or the interval reaches the threshold
      is_out_of_mem = false;
    } else {
      const int64_t reserved_memstore = from_user ? REPLAY_RESERVE_MEMSTORE_BYTES : 0;
      ObTenantFreezeCtx ctx;
      if (false == tenant_info_.is_loaded_) {
        is_out_of_mem = false;
        LOG_INFO("[TenantFreezer] This tenant not exist", KR(ret));
      } else if (FALSE_IT(tenant_info_.get_freeze_ctx(ctx))) {
      } else if (OB_FAIL(get_tenant_mem_usage_(ctx))) {
        LOG_WARN("[TenantFreezer] fail to get mem usage", KR(ret));
      } else {
        is_out_of_mem = (ctx.total_memstore_hold_ > ctx.mem_memstore_limit_ - reserved_memstore);
      }
      last_check_timestamp = current_time;
    }
  }

  if (OB_SUCC(ret)) {
    last_result = is_out_of_mem;
  }
  return ret;
}

int ObTenantFreezer::check_memstore_full_internal(bool &is_out_of_mem)
{
  int ret = OB_SUCCESS;
  RLOCAL_INIT(int64_t, last_check_timestamp, 0);
  RLOCAL_INIT(bool, last_result, false);
  if (OB_FAIL(check_memstore_full_(last_result,
                                   last_check_timestamp,
                                   is_out_of_mem,
                                   false /* does not from user */))) {
    LOG_WARN("check memstore full failed", K(ret));
  }
  return ret;
}

int ObTenantFreezer::check_memstore_full(bool &is_out_of_mem)
{
  int ret = OB_SUCCESS;
  RLOCAL_INIT(int64_t, last_check_timestamp, 0);
  RLOCAL_INIT(bool, last_result, false);
  if (OB_FAIL(check_memstore_full_(last_result,
                                   last_check_timestamp,
                                   is_out_of_mem,
                                   true /* from user */))) {
    LOG_WARN("check memstore full failed", K(ret));
  }
  return ret;
}

bool ObTenantFreezer::tenant_need_major_freeze()
{
  int ret = OB_SUCCESS;
  bool bool_ret = false;
  ObTenantFreezeCtx ctx;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("tenant manager not init", K(ret));
  } else {
    if (!tenant_info_.is_loaded_) {
      // do nothing
    } else if (FALSE_IT(tenant_info_.get_freeze_ctx(ctx))) {
    } else if (OB_FAIL(get_freeze_trigger_(ctx))) {
      LOG_WARN("fail to get minor freeze trigger", K(ret));
    } else if (OB_FAIL(get_tenant_mem_usage_(ctx))) {
      LOG_WARN("fail to get mem usage", K(ret));
    } else {
      bool_ret = need_freeze_(ctx);
      if (bool_ret) {
        LOG_INFO("A major freeze is needed",
                 "active_memstore_used_",
                 ctx.freezable_active_memstore_used_,
                 "memstore_freeze_trigger_limit_",
                 ctx.memstore_freeze_trigger_);
      }
    }
  }
  return bool_ret;
}

int64_t ObTenantFreezer::get_freeze_trigger_percentage_()
{
  static const int64_t DEFAULT_FREEZE_TRIGGER_PERCENTAGE = 20;
  int64_t percent = DEFAULT_FREEZE_TRIGGER_PERCENTAGE;

  percent = GCONF.freeze_trigger_percentage;

  return percent;
}

int64_t ObTenantFreezer::get_memstore_limit_percentage_()
{
  int ret = OB_SUCCESS;
  static const int64_t SMALL_TENANT_MEMORY_LIMIT = 8 * 1024 * 1024 * 1024L; // 8G
  static const int64_t SMALL_MEMSTORE_LIMIT_PERCENTAGE = 40;
  static const int64_t LARGE_MEMSTORE_LIMIT_PERCENTAGE = 50;

  const int64_t tenant_memory = lib::get_tenant_memory_limit();
  const int64_t cluster_memstore_limit_percent = GCONF.memstore_limit_percentage;
  int64_t tenant_memstore_limit_percent = 0;
  int64_t percent = 0;
  tenant_memstore_limit_percent = GCONF._memstore_limit_percentage;
  if (tenant_memstore_limit_percent != 0) {
    percent = tenant_memstore_limit_percent;
  } else if (cluster_memstore_limit_percent != 0) {
    percent = cluster_memstore_limit_percent;
  } else {
    // both is default value, adjust automatically
    if (tenant_memory <= SMALL_TENANT_MEMORY_LIMIT) {
      percent = SMALL_MEMSTORE_LIMIT_PERCENTAGE;
    } else {
      percent = LARGE_MEMSTORE_LIMIT_PERCENTAGE;
    }
  }
  return percent;
}

int ObTenantFreezer::async_freeze_(const ObTenantFreezeArg &arg)
{
  int ret = OB_SUCCESS;
  // single-replica: the freeze used to be posted to self_ via ObTenantFreezerRpcProxy.
  // Dispatch it to the local handler on a worker thread via async_call (fire-and-forget),
  // switching into the target tenant's MTL context (the handler relies on MTL(...)).
  
  // arg is serialized into an owned buffer (lifecycle-safe even if ObTenantFreezeArg
  // later gains non-POD/shallow-ref members); tenant is a scalar, safe to capture.
  auto handle = ex_rpc::async_call<void>(arg, [](const ObTenantFreezeArg &req) {
      int ret = OB_SUCCESS;  // named 'ret' because MTL_SWITCH/OB_SUCC require it
      MOD_SCOPE {
        if (OB_FAIL(obcall::tenant_freeze_dispatch(req))) {
          LOG_WARN("[TenantFreezer] async tenant freeze failed", KR(ret), K(req));
        }
      }
    });
  if (OB_ISNULL(handle)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("[TenantFreezer] fail to dispatch async freeze", KR(ret), K(arg));
  }
  return ret;
}

int ObTenantFreezer::post_freeze_request_(
    const storage::ObFreezeType freeze_type,
    const int64_t try_frozen_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant manager not init", KR(ret));
  } else {
    ObTenantFreezeArg arg;
    arg.freeze_type_ = freeze_type;
    arg.try_frozen_scn_ = try_frozen_scn;
    LOG_INFO("[TenantFreezer] post freeze request to remote", K(arg));
    if (OB_FAIL(async_freeze_(arg))) {
      LOG_WARN("[TenantFreezer] fail to post async freeze request", K(arg), KR(ret));
    }
    LOG_INFO("[TenantFreezer] after freeze at remote");
  }
  return ret;
}

int ObTenantFreezer::post_tx_data_freeze_request_()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("tenant manager not init", KR(ret));
  } else {
    ObTenantFreezeArg arg;
    arg.freeze_type_ = ObFreezeType::TX_DATA_TABLE_FREEZE;
    if (OB_FAIL(async_freeze_(arg))) {
      LOG_WARN("[TenantFreezer] fail to post async freeze request", K(arg), KR(ret));
    }
  }
  return ret;
}

int ObTenantFreezer::post_mds_table_freeze_request_()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("tenant manager not init", KR(ret));
  } else {
    ObTenantFreezeArg arg;
    arg.freeze_type_ = ObFreezeType::MDS_TABLE_FREEZE;
    if (OB_FAIL(async_freeze_(arg))) {
      LOG_WARN("[TenantFreezer] fail to post async freeze request", K(arg), KR(ret));
    }
  }
  return ret;
}

int ObTenantFreezer::reload_config()
{
  int ret = OB_SUCCESS;
  const int64_t freeze_trigger_percentage = get_freeze_trigger_percentage_();
  const int64_t memstore_limit_percent = get_memstore_limit_percentage_();
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant manager not init", KR(ret));
  } else if (memstore_limit_percent > 100
             || memstore_limit_percent <= 0
             || freeze_trigger_percentage > 100
             || freeze_trigger_percentage <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[TenantFreezer] memstore limit percent in ObServerConfig is invalid",
             "memstore limit percent",
             memstore_limit_percent,
             "minor freeze trigger percent",
             freeze_trigger_percentage,
             KR(ret));
  } else if (true == tenant_info_.is_loaded_ &&
             tenant_info_.is_memstore_limit_changed(memstore_limit_percent)) {
    tenant_info_.update_memstore_limit(memstore_limit_percent);
    LOG_INFO("[TenantFreezer] reload config for tenant freezer",
             "new memstore limit percent",
             memstore_limit_percent,
             "new minor freeze trigger percent",
             freeze_trigger_percentage);
  }
  return ret;
}

int ObTenantFreezer::print_tenant_usage(
    char *print_buf,
    int64_t buf_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  ObTenantStatistic stat;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("[TenantFreezer] tenant manager not init", KR(ret));
  } else if (OB_FAIL(get_tenant_mem_stat_(stat))) {
    LOG_WARN("[TenantFreezer] fail to get tenant mem stat", KR(ret));
  } else {
    ret = databuff_printf(print_buf, buf_len, pos,
                          "[TENANT_MEMORY] "
                          "now=% '15ld "
                          "active_memstore_used=% '15ld "
                          "total_memstore_used=% '15ld "
                          "total_memstore_hold=% '15ld "
                          "memstore_freeze_trigger_limit=% '15ld "
                          "memstore_limit=% '15ld "
                          "mem_tenant_limit=% '15ld "
                          "mem_tenant_hold=% '15ld "
                          "max_mem_memstore_can_get_now=% '15ld "
                          "memstore_alloc_pos=% '15ld "
                          "memstore_frozen_pos=% '15ld "
                          "memstore_reclaimed_pos=% '15ld\n",
                          ObClockGenerator::getClock(),
                          stat.active_memstore_used_,
                          stat.total_memstore_used_,
                          stat.total_memstore_hold_,
                          stat.memstore_freeze_trigger_,
                          stat.memstore_limit_,
                          stat.tenant_memory_limit_,
                          stat.tenant_memory_hold_,
                          stat.memstore_can_get_now_,
                          stat.memstore_allocated_pos_,
                          stat.memstore_frozen_pos_,
                          stat.memstore_reclaimed_pos_);
  }

  return ret;
}

int ObTenantFreezer::get_global_frozen_scn_(int64_t &frozen_scn)
{
  int ret = OB_SUCCESS;
  

  SCN tmp_frozen_scn;
  if (OB_FAIL(rootserver::ObMajorFreezeHelper::get_frozen_scn(tmp_frozen_scn))) {
    LOG_WARN("get_frozen_scn failed", KR(ret));
  } else {
    frozen_scn = tmp_frozen_scn.get_val_for_tx();
  }

  return ret;
}

bool ObTenantFreezer::need_freeze_(const ObTenantFreezeCtx &ctx)
{
  bool need_freeze = false;
  // 1. trigger by active memstore used.
  if (ctx.freezable_active_memstore_used_ > ctx.memstore_freeze_trigger_) {
    need_freeze = true;
  }
  // 2. may be slowed
  if (need_freeze && tenant_info_.is_freeze_need_slow()) {
    need_freeze = false;
    LOG_INFO("[TenantFreezer] A minor freeze is needed but slowed.",
             K_(tenant_info),
             K(ctx.active_memstore_used_),
             K(ctx.memstore_freeze_trigger_), K(ctx.max_cached_memstore_size_));
  }
  if (need_freeze) {
    LOG_INFO("[TenantFreezer] A minor freeze is needed by active memstore used.",
             K(ctx.freezable_active_memstore_used_), K(ctx.memstore_freeze_trigger_), K(ctx.max_cached_memstore_size_));
  }
  return need_freeze;
}

bool ObTenantFreezer::is_major_freeze_turn_()
{
  const int64_t freeze_cnt = tenant_info_.freeze_cnt_;
  int64_t major_compact_trigger = INT64_MAX;

  major_compact_trigger = GCONF.major_compact_trigger;

  return (major_compact_trigger != 0 && freeze_cnt >= major_compact_trigger);
}

int ObTenantFreezer::do_minor_freeze_data_(const ObTenantFreezeCtx &ctx)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  bool rollback_freeze_cnt = false;
  LOG_INFO("[TenantFreezer] A minor freeze is needed",
           "active_memstore_used_", ctx.freezable_active_memstore_used_,
           "memstore_freeze_trigger", ctx.memstore_freeze_trigger_,
           "max_cached_memstore_size", ctx.max_cached_memstore_size_,
           "mem_tenant_remain", get_tenant_memory_remain(),
           "mem_tenant_limit", get_tenant_memory_limit(),
           "mem_tenant_hold", get_tenant_memory_hold(),
           "mem_memstore_used", get_tenant_memory_hold(ObCtxIds::MEMSTORE_CTX_ID));

  if (OB_FAIL(set_tenant_freezing_())) {
  } else {
    bool rollback_freeze_cnt = false;
    if (OB_FAIL(tenant_freeze_data_())) {
      rollback_freeze_cnt = true;
      LOG_ERROR("fail to minor freeze", K(ret));
    } else {
      tenant_info_.update_slow_freeze_interval();
      LOG_INFO("finish tenant minor freeze", K(ret));
    }
    // clear freezing mark for tenant
    int tmp_ret = OB_SUCCESS;
    if (OB_UNLIKELY(OB_SUCCESS !=
                    (tmp_ret = unset_tenant_freezing_(rollback_freeze_cnt)))) {
      LOG_WARN("unset tenant freezing mark failed", K(tmp_ret));
      if (OB_SUCC(ret)) {
        ret = tmp_ret;
      }
    }
  }

  if (OB_SUCC(ret)) {
    freezer_stat_.add_freeze_event();
  }

  return ret;
}

int ObTenantFreezer::do_major_if_need_(const bool need_freeze)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  int64_t frozen_scn = 0;
  int64_t curr_frozen_scn = 0;
  bool need_major = false;
  bool major_triggered = false;
  if (OB_TMP_FAIL(retry_failed_major_freeze_(major_triggered))) {
    LOG_WARN("fail to do major freeze due to previous failure", K(tmp_ret));
  }
  if (!tenant_info_.is_loaded_) {
    // do nothing
    // update frozen scn
  } else if (!need_freeze) {
    // no need major
  } else if (!is_major_freeze_turn_()) {
    // do nothing
  } else if (OB_FAIL(get_global_frozen_scn_(frozen_scn))) {
    LOG_WARN("fail to get global frozen version", K(ret));
  } else if (0 != frozen_scn && OB_FAIL(tenant_info_.update_frozen_scn(frozen_scn))) {
    LOG_WARN("fail to update frozen version", K(ret), K(frozen_scn), K_(tenant_info));
  } else {
    need_major = (need_freeze &&
                  !major_triggered &&
                  is_major_freeze_turn_());
    curr_frozen_scn = tenant_info_.frozen_scn_;
  }
  if (need_major) {
    if (OB_FAIL(do_major_freeze_(curr_frozen_scn))) {
      LOG_WARN("[TenantFreezer] fail to do major freeze", K(tmp_ret));
    } else {
      // do nothing
    }
  }
  return ret;
}

int ObTenantFreezer::do_major_freeze_(const int64_t try_frozen_scn)
{
  int ret = OB_SUCCESS;
  LOG_INFO("A major freeze is needed", K(try_frozen_scn));
  if (OB_FAIL(post_freeze_request_(MAJOR_FREEZE,
                                   try_frozen_scn))) {
    LOG_WARN("major freeze failed", K(ret), K_(tenant_info));
  }

  return ret;
}

void ObTenantFreezer::log_frozen_memstore_info_if_need_(const ObTenantFreezeCtx &ctx)
{
  int ret = OB_SUCCESS;
  ObMemstoreAllocator &tenant_allocator = share::g_mp->shared_mem_alloc_mgr()->memstore_allocator();
  if (ctx.total_memstore_hold_ > ctx.memstore_freeze_trigger_ ||
      ctx.freezable_active_memstore_used_ > ctx.memstore_freeze_trigger_) {
    // There is an unreleased memstable
    LOG_INFO("[TenantFreezer] tenant have inactive memstores",
             K(ctx.freezable_active_memstore_used_),
             K(ctx.total_memstore_used_),
             K(ctx.total_memstore_hold_),
             "memstore_freeze_trigger_limit_",
             ctx.memstore_freeze_trigger_);

    char frozen_mt_info[DEFAULT_BUF_LENGTH];
    tenant_allocator.log_frozen_memstore_info(frozen_mt_info, sizeof(frozen_mt_info));
    LOG_INFO("[TenantFreezer] oldest frozen memtable", "list", frozen_mt_info);
  }
}

void ObTenantFreezer::halt_prewarm_if_need_(const ObTenantFreezeCtx &ctx)
{
  int ret = OB_SUCCESS;
  // When the memory is tight, try to abort the warm-up to release memstore
  int64_t mem_danger_limit = ctx.mem_memstore_limit_
  - ((ctx.mem_memstore_limit_ - ctx.memstore_freeze_trigger_) >> 2);
  if (ctx.total_memstore_hold_ > mem_danger_limit) {
    int64_t curr_ts = ObClockGenerator::getClock();
    if (curr_ts - tenant_info_.last_halt_ts_ > 10L * 1000L * 1000L) {
      // halt_all_prewarming was a no-op RPC (handler removed); prewarming halt is
      // not supported, so only keep the rate-limit bookkeeping.
      tenant_info_.last_halt_ts_ = curr_ts;
    }
  }
}

void ObTenantFreezer::get_freezer_stat_history_snapshot(int64_t &length)
{
  length = freezer_history_.length_;
}

void ObTenantFreezer::get_freezer_stat_from_history(int64_t pos, ObTenantFreezerStat& stat)
{
  stat = freezer_history_.history_[(freezer_history_.start_ + pos)
                                   % ObTenantFreezerStatHistory::MAX_HISTORY_LENGTH];
}

int ObTenantFreezer::update_frozen_scn(const int64_t frozen_scn)
{
  int ret = OB_SUCCESS;
  if (!tenant_info_.is_loaded_) {
    // do nothing
  } else if (OB_FAIL(tenant_info_.update_frozen_scn(frozen_scn))) {
    LOG_WARN("update frozen scn failed", K(ret), K(frozen_scn));
  }
  return ret;
}

ObTenantFreezerStat::ObFreezerMergeType ObTenantFreezerStat::switch_to_freezer_merge_type(const compaction::ObMergeType type)
{
  ObFreezerMergeType ret_merge_type = ObFreezerMergeType::UNNECESSARY_TYPE;

  if (is_major_merge(type)) {
    ret_merge_type = ObFreezerMergeType::MAJOR_MERGE;
  } else if (is_minor_merge(type)) {
    ret_merge_type = ObFreezerMergeType::MINOR_MERGE;
  } else if (is_mini_merge(type)) {
    ret_merge_type = ObFreezerMergeType::MINI_MERGE;
  } else {
    ret_merge_type = ObFreezerMergeType::UNNECESSARY_TYPE;
  }

  return ret_merge_type;
}

const char *ObTenantFreezerStat::freezer_merge_type_to_str(const ObFreezerMergeType merge_type)
{
  const char *str = "";
  if (ObFreezerMergeType::UNNECESSARY_TYPE == merge_type) {
    str = "unnecessary_merge_type";
  } else if (ObFreezerMergeType::MINI_MERGE == merge_type) {
    str = "mini_merge";
  } else if (ObFreezerMergeType::MINOR_MERGE == merge_type) {
    str = "minor_merge";
  } else if (ObFreezerMergeType::MAJOR_MERGE == merge_type) {
    str = "major_merge";
  } else {
    str = "invalid_merge_type";
  }
  return str;
}

bool ObTenantFreezerStat::is_useful_freezer_merge_type(const ObFreezerMergeType merge_type)
{
  if (merge_type > ObFreezerMergeType::UNNECESSARY_TYPE &&
      merge_type < ObFreezerMergeType::MAX_MERGE_TYPE) {
    return true;
  } else {
    return false;
  }
}

void ObTenantFreezerStat::reset(int64_t retire_clock)
{
  ATOMIC_SET(&last_captured_timestamp_, 0);
  ATOMIC_SET(&captured_data_size_, 0);
  ATOMIC_SET(&captured_freeze_times_, 0);
  for (int64_t i = 0; i < ObFreezerMergeType::MAX_MERGE_TYPE; i++) {
    ATOMIC_SET(&(captured_merge_time_cost_[i]), 0);
    ATOMIC_SET(&(captured_merge_times_[i]), 0);
  }

  for (int64_t i = 0; i < MAX_FREEZE_SOURCE_TYPE_COUNT; i++) {
    ATOMIC_SET(&(captured_source_times_[i]), 0);
  }

  ATOMIC_SET(&last_captured_retire_clock_, retire_clock);
}

void ObTenantFreezerStat::refresh()
{
  ATOMIC_SET(&captured_data_size_, 0);
  ATOMIC_SET(&captured_freeze_times_, 0);
  for (int64_t i = 0; i < ObFreezerMergeType::MAX_MERGE_TYPE; i++) {
    ATOMIC_SET(&(captured_merge_time_cost_[i]), 0);
    ATOMIC_SET(&(captured_merge_times_[i]), 0);
  }

  for (int64_t i = 0; i < MAX_FREEZE_SOURCE_TYPE_COUNT; i++) {
    ATOMIC_SET(&(captured_source_times_[i]), 0);
  }
}

void ObTenantFreezerStat::add_freeze_event()
{
  ATOMIC_FAA(&captured_freeze_times_, 1);
}

void ObTenantFreezerStat::add_merge_event(const compaction::ObMergeType type, const int64_t cost)
{
  ObFreezerMergeType real_merge_type = switch_to_freezer_merge_type(type);
  if (is_useful_freezer_merge_type(real_merge_type)) {
    ATOMIC_FAA(&(captured_merge_time_cost_[real_merge_type]), cost);
    ATOMIC_FAA(&(captured_merge_times_[real_merge_type]), 1);
  }
}

void ObTenantFreezerStat::print_activity_metrics()
{
  TRANS_LOG(INFO, "[TENANT_FREEZER_EVENT] print captured event", KPC(this));

  for (int64_t i = 0; i < ObFreezerMergeType::MAX_MERGE_TYPE; i++) {
    int64_t captured_merge_time_cost = ATOMIC_LOAD(&(captured_merge_time_cost_[i]));
    int64_t captured_merge_times = ATOMIC_LOAD(&(captured_merge_times_[i]));
    const ObFreezerMergeType type = (ObFreezerMergeType)i;

    TRANS_LOG(INFO, "[TENANT_FREEZER_EVENT] print merge event",
              K(freezer_merge_type_to_str(type)),
              K(captured_merge_times),
              K(captured_merge_time_cost));
  }
}

void ObTenantFreezerStat::assign(const ObTenantFreezerStat stat)
{
  last_captured_timestamp_ = stat.last_captured_timestamp_;
  captured_data_size_ = stat.captured_data_size_;
  captured_freeze_times_ = stat.captured_freeze_times_;

  for (int64_t i = 0; i < ObFreezerMergeType::MAX_MERGE_TYPE; i++) {
    captured_merge_time_cost_[i] = stat.captured_merge_time_cost_[i];
    captured_merge_times_[i] = stat.captured_merge_times_[i];
  }

  for (int64_t i = 0; i < MAX_FREEZE_SOURCE_TYPE_COUNT; i++) {
    captured_source_times_[i] = stat.captured_source_times_[i];
  }

  last_captured_retire_clock_ = stat.last_captured_retire_clock_;
}

void ObTenantFreezerStatHistory::add_activity_metric(const ObTenantFreezerStat stat)
{
  int ret = OB_SUCCESS;

  if (start_ < 0 || start_ >= MAX_HISTORY_LENGTH) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "unexpected start position", K(start_), K(length_));
  } else if (length_ == MAX_HISTORY_LENGTH) {
    (void)history_[start_].assign(stat);
    start_ = (start_ + 1) % MAX_HISTORY_LENGTH;
  } else if (length_ < MAX_HISTORY_LENGTH && 0 == start_) {
    (void)history_[start_ + length_].assign(stat);
    length_++;
  } else {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "unexpected history length", K(start_), K(length_));
  }
}

void ObTenantFreezerStatHistory::reset()
{
  start_ = 0;
  length_ = 0;
}


}
}

namespace oceanbase
{
namespace storage
{
// share/throttle hook registration(avoids share->storage dependency, predecessor ob_tenant_srs.cpp)
static struct ObMemstoreLimitPctFnRegister
{
  ObMemstoreLimitPctFnRegister()
  {
    share::g_memstore_limit_percentage_fn = []() -> int64_t {
      return share::g_mp->tenant_freezer()->get_memstore_limit_percentage();
    };
  }
} g_memstore_limit_pct_fn_register;
}  // namespace storage
}  // namespace oceanbase

// ===== definition moved from storage/allocator/ob_memstore_allocator.cpp(Memtable/Freezer real user) =====
namespace oceanbase
{
namespace share
{

int FrozenMemstoreInfoLogger::operator()(ObDLink* link)
{
  int ret = OB_SUCCESS;
#ifdef _WIN32
  ObMemstoreAllocator::AllocHandle* handle = CONTAINER_OF(link, ObMemstoreAllocator::AllocHandle, total_list_);
#else
  ObMemstoreAllocator::AllocHandle* handle = CONTAINER_OF(link, typeof(*handle), total_list_);
#endif
  memtable::ObMemtable& mt = handle->mt_;
  if (handle->is_frozen()) {
    if (OB_FAIL(databuff_print_obj(buf_, limit_, pos_, mt))) {
    } else {
      ret = databuff_printf(buf_, limit_, pos_, ",");
    }
  }
  return ret;
}


int ActiveMemstoreInfoLogger::operator()(ObDLink* link)
{
  int ret = OB_SUCCESS;
#ifdef _WIN32
  ObMemstoreAllocator::AllocHandle* handle = CONTAINER_OF(link, ObMemstoreAllocator::AllocHandle, total_list_);
#else
  ObMemstoreAllocator::AllocHandle* handle = CONTAINER_OF(link, typeof(*handle), total_list_);
#endif
  memtable::ObMemtable& mt = handle->mt_;
  if (handle->is_active()) {
    if (OB_FAIL(databuff_print_obj(buf_, limit_, pos_, mt))) {
    } else {
      ret = databuff_printf(buf_, limit_, pos_, ",");
    }
  }
  return ret;
}


void* ObMemstoreAllocator::alloc(AllocHandle& handle, int64_t size, const int64_t expire_ts)
{
  int ret = OB_SUCCESS;
  int64_t align_size = upper_align(size, sizeof(int64_t));

  bool is_out_of_mem = false;
  if (!handle.is_id_valid()) {
    COMMON_LOG(TRACE, "MTALLOC.first_alloc", KP(&handle.mt_));
    LockGuard guard(lock_);
    if (handle.is_frozen()) {
      ret = OB_EAGAIN;
      if (!handle.mt_.get_offlined()) {
        COMMON_LOG(ERROR, "cannot alloc because allocator is frozen", K(ret), K(handle.mt_));
      } else {
        COMMON_LOG(WARN, "cannot alloc because allocator is frozen", K(ret), K(handle.mt_));
      }
    } else if (!handle.is_id_valid()) {
      handle.set_clock(arena_.retired());
      hlist_.set_active(handle);
    }
  }

  if (OB_SUCC(ret)) {
    storage::ObTenantFreezer *freezer = nullptr;
    if (FALSE_IT(freezer = share::g_mp->tenant_freezer())) {
    } else if (OB_FAIL(freezer->check_memstore_full_internal(is_out_of_mem))) {
      COMMON_LOG(ERROR, "fail to check tenant out of mem limit", K(ret), K(1UL));
    }
  }

  void *res = nullptr;
  if (OB_FAIL(ret) || is_out_of_mem) {
    if (REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
      STORAGE_LOG(WARN, "this tenant is already out of memstore limit or some thing wrong.", K(1UL));
    }
    res = nullptr;
  } else {
    bool is_throttled = false;
    (void)throttle_tool_->alloc_resource<ObMemstoreAllocator>(align_size, expire_ts, is_throttled);
    if (is_throttled) {
      share::memstore_throttled_alloc() += align_size;
    }
    const int64_t effective_group_id = handle.mt_.is_inner_tablet() ? 0 : handle.id_;
    res = arena_.alloc(effective_group_id, handle.arena_handle_, align_size);
  }
  return res;
}


int ObMemstoreAllocator::set_memstore_threshold_without_lock()
{
  int ret = OB_SUCCESS;
  int64_t memstore_threshold = INT64_MAX;

  storage::ObTenantFreezer *freezer = nullptr;
  if (FALSE_IT(freezer = share::g_mp->tenant_freezer())) {
  } else if (OB_FAIL(freezer->get_tenant_memstore_limit(memstore_threshold))) {
    COMMON_LOG(WARN, "failed to get_tenant_memstore_limit", K(ret));
  } else {
    throttle_tool_->set_resource_limit<ObMemstoreAllocator>(memstore_threshold);
  }
  return ret;
}


void ObMemstoreAllocator::init_throttle_config(int64_t &resource_limit,
                                               int64_t &trigger_percentage,
                                               int64_t &max_duration)
{
  // define some default value
  const int64_t MEMSTORE_THROTTLE_TRIGGER_PERCENTAGE = 60;
  const int64_t MEMSTORE_THROTTLE_MAX_DURATION = 2LL * 60LL * 60LL * 1000LL * 1000LL;  // 2 hours

  int64_t total_memory = lib::get_tenant_memory_limit();

  // Use tenant config to init throttle config
  omt::ObTenantConfigGuard tenant_config(TENANT_CONF());
  if (tenant_config.is_valid()) {
    trigger_percentage = tenant_config->writing_throttling_trigger_percentage;
    max_duration = tenant_config->writing_throttling_maximum_duration;
  } else {
    COMMON_LOG_RET(WARN, OB_INVALID_CONFIG, "init throttle config with default value");
    trigger_percentage = MEMSTORE_THROTTLE_TRIGGER_PERCENTAGE;
    max_duration = MEMSTORE_THROTTLE_MAX_DURATION;
  }
  resource_limit = total_memory * share::g_mp->tenant_freezer()->get_memstore_limit_percentage() / 100;
}


}  // namespace share
}  // namespace oceanbase

// ===== allocator_mgr.cpp freezer fn =====
namespace oceanbase
{
namespace share
{

void ObSharedMemAllocMgr::update_throttle_config()
{
  

  int64_t total_memory = lib::get_tenant_memory_limit();
  int64_t hard_memory_limit = lib::get_hard_memory_limit();
  common::ObServerConfig *tenant_config = &GCONF;
  {
    int64_t share_mem_limit_percentage = tenant_config->_tx_share_memory_limit_percentage;
    int64_t tenant_memstore_limit_percentage = share::g_mp->tenant_freezer()->get_memstore_limit_percentage();
    int64_t tx_data_limit_percentage = tenant_config->_tx_data_memory_limit_percentage;
    int64_t mds_limit_percentage = tenant_config->_mds_memory_limit_percentage;
    int64_t trigger_percentage = tenant_config->writing_throttling_trigger_percentage;
    int64_t max_duration = tenant_config->writing_throttling_maximum_duration;
    int64_t tenant_vector_limit_percentage = ObTenantVectorAllocator::get_vector_mem_limit_percentage(tenant_config);
    if (0 == share_mem_limit_percentage) {
      // 0 means use (max(memstore_limit, vector_limit + 5) + 10)
      share_mem_limit_percentage = MAX(tenant_memstore_limit_percentage, tenant_vector_limit_percentage + 5) + 10;
    }

    int64_t share_mem_limit = hard_memory_limit / 100 * share_mem_limit_percentage;
    int64_t memstore_limit = total_memory / 100 * tenant_memstore_limit_percentage;
    int64_t tx_data_limit = total_memory / 100 * tx_data_limit_percentage;
    int64_t mds_limit = total_memory / 100 * mds_limit_percentage;
    int64_t vector_limit = hard_memory_limit / 100 * tenant_vector_limit_percentage;

    bool share_config_changed = false;
    (void)share_resource_throttle_tool_.update_throttle_config<FakeAllocatorForTxShare>(
        share_mem_limit, trigger_percentage, max_duration, share_config_changed);

    bool memstore_config_changed = false;
    (void)share_resource_throttle_tool_.update_throttle_config<ObMemstoreAllocator>(
        memstore_limit, trigger_percentage, max_duration, memstore_config_changed);

    bool tx_data_config_changed = false;
    (void)share_resource_throttle_tool_.update_throttle_config<ObTenantTxDataAllocator>(
        tx_data_limit, trigger_percentage, max_duration, tx_data_config_changed);

    bool mds_config_changed = false;
    (void)share_resource_throttle_tool_.update_throttle_config<ObTenantMdsAllocator>(
        mds_limit, trigger_percentage, max_duration, mds_config_changed);

    bool vector_config_changed = false;
    (void)share_resource_throttle_tool_.update_throttle_config<ObTenantVectorAllocator>(
        vector_limit, trigger_percentage, max_duration, vector_config_changed);

    if (share_config_changed || memstore_config_changed || tx_data_config_changed || mds_config_changed ||
        vector_config_changed) {
      SHARE_LOG(INFO,
                "[Throttle] Update Config",
                K(total_memory),
                K(share_mem_limit_percentage),
                K(share_mem_limit),
                K(tenant_memstore_limit_percentage),
                K(memstore_limit),
                K(tx_data_limit_percentage),
                K(tx_data_limit),
                K(mds_limit_percentage),
                K(mds_limit),
                K(trigger_percentage),
                K(max_duration),
                K(tenant_vector_limit_percentage),
                K(vector_limit));

    }
  }
}


}  // namespace share
}  // namespace oceanbase
