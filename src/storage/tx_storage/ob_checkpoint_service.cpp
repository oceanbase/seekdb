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

#include "storage/tx_storage/ob_checkpoint_service.h"
#include "share/rc/ob_server_runtime.h"
#include "logservice/ob_log_service.h"
#include "share/ob_server_struct.h"
#include "observer/change_stream/ob_change_stream_mgr.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
using namespace share;
using namespace palf;
namespace storage
{
namespace checkpoint
{

int64_t ObCheckPointService::CHECK_CLOG_USAGE_INTERVAL = 2000 * 1000L;
int64_t ObCheckPointService::CHECKPOINT_INTERVAL = 5000 * 1000L;
int64_t ObCheckPointService::TRAVERSAL_FLUSH_INTERVAL = 5000 * 1000L;

// Check if need flush all CLOG module each 1 minute
int64_t ObCheckPointService::TRY_ADVANCE_CKPT_INTERVAL = 60LL * 1000LL * 1000LL;

int ObCheckPointService::server_module_init(ObCheckPointService* &m)
{
  return m->init();
}

int ObCheckPointService::init()
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObCheckPointService init twice.", K(ret));
  } else if (OB_FAIL(freeze_thread_.init())) {
  } else {
    is_inited_ = true;
    prev_advance_ckpt_task_ts_ = ObClockGenerator::getClock();
  }
  return ret;
}

int ObCheckPointService::start()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(checkpoint_timer_.set_run_wrapper_with_ret(share::server_runtime()))) {
  } else if (OB_FAIL(checkpoint_timer_.init("TxCkpt", ObMemAttr("CheckPointTimer")))) {
  } else if (OB_FAIL(checkpoint_timer_.schedule(checkpoint_task_, CHECKPOINT_INTERVAL, true))) {
  } else if (OB_FAIL(traversal_flush_timer_.set_run_wrapper_with_ret(share::server_runtime()))) {
  } else if (OB_FAIL(traversal_flush_timer_.init("Flush", ObMemAttr("FlushTimer")))) {
  } else if (OB_FAIL(traversal_flush_timer_.schedule(traversal_flush_task_, TRAVERSAL_FLUSH_INTERVAL, true))) {
  } else if (OB_FAIL(check_clog_disk_usage_timer_.set_run_wrapper_with_ret(share::server_runtime()))) {
  } else if (OB_FAIL(check_clog_disk_usage_timer_.init("CKClogDisk", ObMemAttr("DiskUsageTimer")))) {
  } else if (OB_FAIL(check_clog_disk_usage_timer_.schedule(check_clog_disk_usage_task_, CHECK_CLOG_USAGE_INTERVAL, true))) {
  } else if (OB_FAIL(advance_ckpt_timer_.set_run_wrapper_with_ret(share::server_runtime()))) {
  } else if (OB_FAIL(advance_ckpt_timer_.init("AdvanceCKPT", ObMemAttr("AdvanceTimer")))) {
  } else if (OB_FAIL(advance_ckpt_timer_.schedule(advance_ckpt_task_, TRY_ADVANCE_CKPT_INTERVAL, true))) {
  }
  return ret;
}

int ObCheckPointService::stop()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObCheckPointService is not initialized", K(ret));
  } else {
    freeze_thread_.stop();
    LOG_INFO("ObCheckPointService stoped");
  }
  checkpoint_timer_.stop();
  traversal_flush_timer_.stop();
  check_clog_disk_usage_timer_.stop();
  return ret;
}

void ObCheckPointService::wait()
{
  checkpoint_timer_.wait();
  traversal_flush_timer_.wait();
  check_clog_disk_usage_timer_.wait();
  freeze_thread_.wait();
}

int ObCheckPointService::add_ls_freeze_task(
    ObDataCheckpoint *data_checkpoint,
    SCN rec_scn)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(freeze_thread_.add_task(data_checkpoint, rec_scn))) {
  }
  return ret;
}

void ObCheckPointService::destroy()
{
  freeze_thread_.destroy();
  is_inited_ = false;
  checkpoint_timer_.destroy();
  traversal_flush_timer_.destroy();
  check_clog_disk_usage_timer_.destroy();
}

void ObCheckPointService::ObCheckpointTask::runTimerTask()
{
  STORAGE_LOG(INFO, "====== checkpoint timer task ======");
  int ret = OB_SUCCESS;
  DEBUG_SYNC(BEFORE_CHECKPOINT_TASK);
  ObLS *tenant_ls = nullptr;
  ObChangeStreamMgr *cs_mgr = nullptr;
  palf::LSN checkpoint_lsn;
  palf::LSN cs_min_dep_lsn;
  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(tenant_ls))) {
  } else if (OB_FAIL(tenant_ls->get_data_checkpoint()->check_can_move_to_active_in_newcreate())) {
  } else if (OB_FAIL(tenant_ls->get_checkpoint_executor()->update_clog_checkpoint())) {
  } else if (OB_ISNULL(cs_mgr =
          ::oceanbase::share::server_service<::oceanbase::share::ObChangeStreamMgr>())) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(cs_mgr->get_min_dep_lsn(cs_min_dep_lsn))) {
  } else {
    checkpoint_lsn = tenant_ls->get_clog_base_lsn();
    if (cs_min_dep_lsn < checkpoint_lsn) {
      FLOG_INFO("[CHECKPOINT] constrain base_lsn by change_stream_min_dep_lsn",
          K(checkpoint_lsn), K(cs_min_dep_lsn));
      checkpoint_lsn = cs_min_dep_lsn;
    }

    if (OB_FAIL(tenant_ls->get_log_handler()->advance_base_lsn(checkpoint_lsn))) {
    } else {
      FLOG_INFO("[CHECKPOINT] advance palf base lsn successfully", K(checkpoint_lsn));
      STORAGE_LOG(INFO, "succeed to update_clog_checkpoint");
    }
  }
}

int ObCheckPointService::flush_to_recycle_clog_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  ObLS *tenant_ls = nullptr;
  bool flushed = false;
  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(tenant_ls))) {
  } else if (tenant_ls->get_data_checkpoint()->is_flushing()) {
  } else if (OB_TMP_FAIL(tenant_ls->get_checkpoint_executor()->update_clog_checkpoint())) {
  } else if (OB_TMP_FAIL(tenant_ls->flush_to_recycle_clog())) {
  } else {
    flushed = true;
  }

  return ret;
}

void ObCheckPointService::ObTraversalFlushTask::runTimerTask()
{
  STORAGE_LOG(INFO, "====== traversal_flush timer task ======");
  int ret = OB_SUCCESS;
  ObCurTraceId::init(GCONF.self_addr_);
  ObLS *tenant_ls = nullptr;
  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(tenant_ls))) {
  } else if (OB_FAIL(tenant_ls->get_checkpoint_executor()->traversal_flush())) {
  } else {
    STORAGE_LOG(INFO, "succeed to traversal_flush");
  }
  ObCurTraceId::reset();
}

void ObCheckPointService::ObCheckClogDiskUsageTask::runTimerTask()
{
  STORAGE_LOG(INFO, "====== check clog disk timer task ======");
  int ret = OB_SUCCESS;
  bool need_flush = false;
  logservice::ObLogService *log_service = ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>();
  if (OB_ISNULL(log_service)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(ERROR, "unexpected error, ObLogService is nullptr", KP(log_service));
  } else if (OB_FAIL(log_service->check_need_do_checkpoint(need_flush))) {
  } else if (need_flush) {
    (void)checkpoint_service_.flush_to_recycle_clog_();
  }
}

void ObCheckPointService::ObAdvanceCkptTask::runTimerTask()
{
  int ret = OB_SUCCESS;

  // set 10 minutes as default value
  int64_t advance_checkpoint_interval = 10LL * 60LL * 1000LL * 1000LL;

  // use config value if config is valid
  advance_checkpoint_interval = GCONF._advance_checkpoint_interval;


  if (0 != advance_checkpoint_interval) {
    STORAGE_LOG(INFO, "====== Advance Checkpoint Task ======");
    const int64_t current_ts = ObClockGenerator::getClock();
    const int64_t prev_advance_ckpt_task_ts = ::oceanbase::share::server_service<::oceanbase::storage::checkpoint::ObCheckPointService>()->prev_advance_ckpt_task_ts();
    if (current_ts - prev_advance_ckpt_task_ts > advance_checkpoint_interval) {
      ObLS *tenant_ls = nullptr;
      if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(tenant_ls))) {
      } else if (OB_FAIL(tenant_ls->advance_checkpoint_by_flush(
          SCN::max_scn(), INT64_MAX /*timeout*/, false /*is_global_freeze*/, ObFreezeSourceFlag::CLOG_CHECKPOINT))) {
      }
      if (OB_SUCC(ret)) {
        ::oceanbase::share::server_service<::oceanbase::storage::checkpoint::ObCheckPointService>()->set_prev_advance_ckpt_task_ts(current_ts);
      }
    } else {
      STORAGE_LOG(INFO,
                  "skip advance checkpoint because interval is not reached",
                  K(advance_checkpoint_interval),
                  KTIME(current_ts),
                  KTIME(prev_advance_ckpt_task_ts));
    }
  }
}

} // checkpoint
} // storage
} // oceanbase
