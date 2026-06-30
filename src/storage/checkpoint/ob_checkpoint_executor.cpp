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

#include "ob_checkpoint_executor.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tx_storage/ob_checkpoint_service.h"
#include "logservice/ob_log_service.h"
#include "observer/ob_server_event_history_table_operator.h"

namespace oceanbase
{
using namespace share;
using namespace logservice;
using namespace palf;
namespace storage
{
namespace checkpoint
{

ObCheckpointExecutor::ObCheckpointExecutor()
    : rwlock_(common::ObLatchIds::CLOG_CKPT_RWLOCK),
      update_checkpoint_enabled_(false),
      reuse_recycle_scn_times_(0),
      prev_clog_checkpoint_lsn_(),
      prev_recycle_scn_(),
      update_clog_checkpoint_times_(0),
      last_add_server_history_time_(0)
{
  reset();
}

ObCheckpointExecutor::~ObCheckpointExecutor()
{
  reset();
}

void ObCheckpointExecutor::reset()
{
  for (int i = 0; i < ObLogBaseType::MAX_LOG_BASE_TYPE; i++) {
    handlers_[i] = NULL;
  }
  ls_ = NULL;
  loghandler_ = NULL;
  reuse_recycle_scn_times_ = 0;
  prev_clog_checkpoint_lsn_.reset();
  prev_recycle_scn_.set_invalid();
  update_clog_checkpoint_times_ = 0;
  last_add_server_history_time_ = 0;
}

int ObCheckpointExecutor::register_handler(const ObLogBaseType &type,
                                           ObICheckpointSubHandler *handler)
{
  int ret = OB_SUCCESS;

  if (!is_valid_log_base_type(type) || NULL == handler) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid arguments", K(ret), K(type), K(handler));
  } else {
    if (OB_NOT_NULL(handlers_[type])) {
      STORAGE_LOG(WARN, "repeat register into checkpoint_executor", K(ret), K(type), K(handler));
    } else {
      handlers_[type] = handler;
    }
  }

  return ret;
}

void ObCheckpointExecutor::unregister_handler(const ObLogBaseType &type)
{
  int ret = OB_SUCCESS;

  if (!is_valid_log_base_type(type)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid arguments", K(ret), K(type));
  } else {
    handlers_[type] = NULL;
  }
}

int ObCheckpointExecutor::init(ObLS *ls, ObILogHandler *loghandler)
{
  int ret = OB_SUCCESS;
  ls_ = ls;
  loghandler_ = loghandler;
  return ret;
}

void ObCheckpointExecutor::start()
{
  online();
}

void ObCheckpointExecutor::online()
{
  WLockGuard guard(rwlock_);
  update_checkpoint_enabled_ = true;
}

void ObCheckpointExecutor::offline()
{
  WLockGuard guard(rwlock_);
  update_checkpoint_enabled_ = false;
}

void ObCheckpointExecutor::get_min_rec_scn(int &log_type, SCN &min_rec_scn) const
{
  for (int i = 1; i < ObLogBaseType::MAX_LOG_BASE_TYPE; i++) {
    if (OB_NOT_NULL(handlers_[i])) {
      SCN rec_scn = handlers_[i]->get_rec_scn();
      if (rec_scn > SCN::min_scn() && rec_scn < min_rec_scn) {
        min_rec_scn = rec_scn;
        log_type = i;
      }
    }
  }
}

inline void get_min_rec_scn_service_type_by_index_(int index, char* service_type, const int64_t str_len)
{
  int ret = OB_SUCCESS;
  if (index == 0) {
    strncpy(service_type ,"MAX_DECIDED_SCN", str_len);
  } else if (OB_FAIL(log_base_type_to_string(ObLogBaseType(index), service_type, str_len))) {
    STORAGE_LOG(WARN, "log_base_type_to_string failed", K(ret), K(index));
    strncpy(service_type ,"UNKNOWN_SERVICE_TYPE", str_len);
  }
  if (str_len > 0) {
    service_type[str_len - 1] = '\0';
  }
}

void ObCheckpointExecutor::add_server_event_history_for_update_clog_checkpoint(
    const SCN &checkpoint_scn,
    const char *service_type)
{
  int ret = OB_SUCCESS;
  if (update_clog_checkpoint_times_ > 0) {
    int64_t cur_time = ObClockGenerator::getClock();
    if (cur_time - last_add_server_history_time_ > ADD_SERVER_HISTORY_INTERVAL) {
      
      const int64_t ls_id = ls_->get_ls_id().id();
      last_add_server_history_time_ = cur_time;
      SERVER_EVENT_ADD("checkpoint", "update_clog_checkpoint", K(ls_id), K(checkpoint_scn), K(service_type), K_(update_clog_checkpoint_times)); 
      update_clog_checkpoint_times_ = 0;
    }
  }
}

int ObCheckpointExecutor::update_clog_checkpoint()
{
  int ret = OB_SUCCESS;
  WLockGuard guard_for_update_clog_checkpoint(rwlock_for_update_clog_checkpoint_);
  RLockGuard guard(rwlock_);
  if (update_checkpoint_enabled_) {
    ObFreezer *freezer = ls_->get_freezer();
    if (OB_NOT_NULL(freezer)) {
      SCN checkpoint_scn;
      checkpoint_scn.set_max();
      if (OB_FAIL(freezer->get_max_consequent_callbacked_scn(checkpoint_scn))) {
      } else {
        // used to record which handler provide the smallest rec_scn
        int min_rec_scn_service_type_index = 0;
        const int64_t buf_len = common::MAX_SERVICE_TYPE_BUF_LENGTH;
        char service_type[buf_len];
        get_min_rec_scn(min_rec_scn_service_type_index, checkpoint_scn);
        get_min_rec_scn_service_type_by_index_(min_rec_scn_service_type_index, service_type, buf_len);

        const SCN checkpoint_scn_in_ls_meta = ls_->get_clog_checkpoint_scn();
        const share::ObLSID ls_id = ls_->get_ls_id();
        LSN clog_checkpoint_lsn;
        if (checkpoint_scn == checkpoint_scn_in_ls_meta) {
        } else if (checkpoint_scn < checkpoint_scn_in_ls_meta) {
          if (min_rec_scn_service_type_index == 0) {
          } else {
          }
        } else if (OB_FAIL(loghandler_->locate_by_scn_coarsely(checkpoint_scn, clog_checkpoint_lsn))) {
          if (OB_NOT_INIT == ret) {
            STORAGE_LOG(WARN, "palf has been disabled", K(ret), K(checkpoint_scn), K(ls_->get_ls_id()));
            ret = OB_SUCCESS;
          } else if (OB_NEED_RETRY == ret || OB_ENTRY_NOT_EXIST == ret) {
            ret = OB_SUCCESS;
          } else {
            STORAGE_LOG(ERROR, "locate lsn by logts failed", K(ret), K(ls_id),
                        K(checkpoint_scn), K(checkpoint_scn_in_ls_meta));
          }
        } else if (OB_FAIL(ls_->set_clog_checkpoint(clog_checkpoint_lsn, checkpoint_scn, true/*write_slog*/))) {
        } else {
          update_clog_checkpoint_times_++;
          FLOG_INFO("[CHECKPOINT] update clog checkpoint successfully",
                    K(clog_checkpoint_lsn), K(checkpoint_scn), K(ls_id),
                    K(service_type));
        }
        add_server_event_history_for_update_clog_checkpoint(checkpoint_scn, service_type);
      }
    } else {
    }
  } else {
  }

  return ret;
}

int ObCheckpointExecutor::advance_checkpoint_by_flush(const share::SCN input_recycle_scn)
{
  int ret = OB_SUCCESS;
  const ObLSID ls_id = ls_->get_ls_id();
  SCN recycle_scn = input_recycle_scn;
  SCN max_decided_scn;

  RLockGuard guard(rwlock_);
  if (update_checkpoint_enabled_) {
    if (OB_FAIL(loghandler_->get_max_decided_scn(max_decided_scn))) {
    } else if (!recycle_scn.is_valid() && OB_FAIL(calculate_recycle_scn_(max_decided_scn, recycle_scn))) {
      if (OB_EAGAIN != ret) {
        STORAGE_LOG(WARN, "calculate recycle scn failed", KR(ret));
      }
    } else if (OB_FAIL(check_need_flush_(max_decided_scn, recycle_scn))) {
    } else {
      for (int i = 1; i < ObLogBaseType::MAX_LOG_BASE_TYPE; i++) {
        int tmp_ret = OB_SUCCESS;
        if (OB_NOT_NULL(handlers_[i]) && OB_TMP_FAIL(handlers_[i]->flush(recycle_scn))) {
          STORAGE_LOG(WARN, "handler flush failed", KR(tmp_ret), K(recycle_scn), K(tmp_ret), K(i), K(ls_id));
        }
      }
    }
  } else {
  }

  return ret;
}

/**
 * @brief There are four situations to decide recycle_scn to advance checkpoint:
 *
 * CASE 1 : If : 1) previous recycle_scn and previous clog_checkpoint is valid; 2) current clog_checkpiot == previous
 * clog_checkpoint; Then : use previous recycle_scn to advance checkpoint again
 *
 *
 * CASE 2 : If : min_recycle_scn > max_decided_scn; Then : skip advance checkpoint once
 *
 *       │ ◄── 5% ──► │                        │
 *       │            │                        │
 *       │            │                        │
 * checkpoint_lsn     │                     end_lsn
 *       │            │                        │
 *       │     min_recycle_lsn                 │
 *       │            │                        │
 *       │            │                        │
 *       ▼            ▼                        ▼
 *       ┌───────────────────────────────────────────────────────────────────────────────────┐
 *       │                                                                                   │
 *       │                                 total clog disk                                   │
 *       │                                                                                   │
 *       └───────────────────────────────────────────────────────────────────────────────────┘
 *              ▲
 *              │
 *              │
 *       max_decided_scn
 *
 * CASE 3 : If : max_decided_scn < expected_recycle_scn; Then : use max_decided_scn to advance checkpoint *
 
 *       │            │                        │
 *       ▼            ▼                        ▼
 *       ┌───────────────────────────────────────────────────────────────────────────────────┐
 *       │                                                                                   │
 *       │                                 total clog disk                                   │
 *       │                                                                                   │
 *       └───────────────────────────────────────────────────────────────────────────────────┘
 *                        ▲         ▲
 *                        │         │
 *                 max_decided_scn  │
 *                        │         │
 *                        │expected_recycle_scn
 *                        │         │
 *                        │         │
 *
 * CASE 4 : If : max_decided_scn > expected_recycle_scn; Then : use expected_recycle_scn to advance checkpoint
 *
 *       │            │                        │
 *       ▼            ▼                        ▼
 *       ┌───────────────────────────────────────────────────────────────────────────────────┐
 *       │                                                                                   │
 *       │                                 total clog disk                                   │
 *       │                                                                                   │
 *       └───────────────────────────────────────────────────────────────────────────────────┘
 *                                  ▲      ▲
 *                                  │      │
 *                                  │      │
 *                                  │      │
 *              expected_recycle_scn│      │max_decided_scn
 *                                  │      │
 *                                  │      │
 */
int ObCheckpointExecutor::calculate_recycle_scn_(const SCN max_decided_scn, SCN &recycle_scn)
{
  int ret = OB_SUCCESS;
  const ObLSID ls_id = ls_->get_ls_id();
  const LSN clog_checkpoint_lsn = ls_->get_clog_base_lsn();
  const SCN clog_checkpoint_scn = ls_->get_clog_checkpoint_scn();
  if (prev_clog_checkpoint_lsn_.is_valid() && (prev_clog_checkpoint_lsn_ == clog_checkpoint_lsn) &&
      prev_recycle_scn_.is_valid() && (prev_recycle_scn_ > clog_checkpoint_scn)) {
    recycle_scn = prev_recycle_scn_;
    reuse_recycle_scn_times_++;
    if (reuse_recycle_scn_times_ % 1000 == 0) {
      STORAGE_LOG_RET(WARN, 0, "attention! clog checkpiont has not changed for a long time", K(ls_id));
      recycle_scn.set_max();
    }
  } else {
    SCN min_recycle_scn;
    SCN expected_recycle_scn;
    if (OB_FAIL(calculate_min_recycle_scn_(clog_checkpoint_lsn, min_recycle_scn))) {
    } else if (OB_FAIL(calculate_expected_recycle_scn_(clog_checkpoint_lsn, expected_recycle_scn))) {
    } else {
      recycle_scn = MIN(max_decided_scn, expected_recycle_scn);
      if (recycle_scn < min_recycle_scn) {
        ret = OB_EAGAIN;
        STORAGE_LOG(INFO,
                    "recycle_scn too small, skip trigger flush once",
                    KR(ret),
                    K(ls_id),
                    K(min_recycle_scn),
                    K(expected_recycle_scn),
                    K(max_decided_scn));
      } else {
        prev_clog_checkpoint_lsn_ = clog_checkpoint_lsn;
        prev_recycle_scn_ = recycle_scn;
        reuse_recycle_scn_times_ = 0;
      }
    }
  }
  return ret;
}

/**
 * @brief As calculate_recycle_scn_() comments show : There is a threshold of clog recycle and the default value is 5%.
 *But if there are too many logstreams in a single tenant, for example, 20 logstreams. And each logstream used 4.9% clog
 *disk, then all logstreams used 98% clog disk but the clog recycling cannot be triggered. So another parameter is added
 *: MAX_TENANT_RECYCLE_CLOG_PERCENTAGE, which used to set the max recycle clog trigger.
 *
 *For example, in the previous logic, if there were a total of 10 logstreams and the minimum recycle threshold for each
 *logstream was 5%, then the total threshold would be 50%. However, in the new logic, the minimum recycle threshold for
 *each log stream cannot reach 5% but is calculated as MAX_TENANT_RECYCLE_CLOG_PERCENTAGE (30) divided by 10, which is
 *3%. Therefore, regardless of the number of log streams, it ensures that the clog recycling can be triggered properly.
 *
 */
int ObCheckpointExecutor::calculate_min_recycle_scn_(const LSN clog_checkpoint_lsn, SCN &min_recycle_scn)
{
  const int64_t DEFAULT_MIN_LS_RECYCLE_CLOG_PERCENTAGE = 5;
  const int64_t MAX_TENANT_RECYCLE_CLOG_PERCENTAGE = ObCheckPointService::NEED_FLUSH_CLOG_DISK_PERCENT;

  int ret = OB_SUCCESS;
  int64_t used_size = 0;
  int64_t total_size = 0;

  ObLogService *log_service = nullptr;
  ObLSService *ls_service = nullptr;
  if (OB_ISNULL(log_service = share::g_mp->log_service()) || OB_ISNULL(ls_service = share::g_mp->ls_service())) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "get_log_service failed", K(ret));
  } else if (OB_FAIL(log_service->get_palf_disk_usage(used_size, total_size))) {
  } else {
    int64_t ls_count = ls_service->get_ls_count();
    if (ls_count <= 0) {
      ls_count = 1;
    }

    int64_t ls_min_recycle_clog_percentage =
        MIN(DEFAULT_MIN_LS_RECYCLE_CLOG_PERCENTAGE, MAX_TENANT_RECYCLE_CLOG_PERCENTAGE / ls_count);

    LSN min_recycle_lsn = clog_checkpoint_lsn + (total_size * ls_min_recycle_clog_percentage / 100);
    if (OB_FAIL(loghandler_->locate_by_lsn_coarsely(min_recycle_lsn, min_recycle_scn))) {
    }
  }
  return ret;
}

int ObCheckpointExecutor::calculate_expected_recycle_scn_(const palf::LSN clog_checkpoint_lsn,
                                                          SCN &expected_recycle_scn)
{
  int ret = OB_SUCCESS;
  LSN end_lsn;
  if (OB_FAIL(loghandler_->get_end_lsn(end_lsn))) {
  } else {
    LSN calcu_recycle_lsn = clog_checkpoint_lsn + ((end_lsn - clog_checkpoint_lsn) * CLOG_GC_PERCENT / 100);
    if (OB_FAIL(loghandler_->locate_by_lsn_coarsely(calcu_recycle_lsn, expected_recycle_scn))) {
    }
  }
  return ret;
}

int ObCheckpointExecutor::check_need_flush_(const SCN max_decided_scn, const SCN recycle_scn)
{
  int ret = OB_SUCCESS;
  const ObLSID ls_id = ls_->get_ls_id();
  const SCN clog_checkpoint_scn = ls_->get_clog_checkpoint_scn();
  if (recycle_scn.is_max()) {
    // must do flush
  } else if (recycle_scn < clog_checkpoint_scn) {
    ret = OB_NO_NEED_UPDATE;
  } else if (recycle_scn > max_decided_scn) {
    ret = OB_EAGAIN;
  }
  return ret;
}

int ObCheckpointExecutor::get_checkpoint_info(ObIArray<ObCheckpointVTInfo> &checkpoint_array)
{
  int ret = OB_SUCCESS;
  checkpoint_array.reset();
  for (int i = 1; i < ObLogBaseType::MAX_LOG_BASE_TYPE; i++) {
    if (OB_NOT_NULL(handlers_[i])) {
      ObCheckpointVTInfo info;
      info.rec_scn = handlers_[i]->get_rec_scn();
      info.service_type = i;
      checkpoint_array.push_back(info);
    }
  }
  return ret;
}


int ObCheckpointExecutor::get_clog_checkpoint_stat(ObLsClogCheckpointStat &stat) const
{
  int ret = OB_SUCCESS;
  RLockGuard guard(rwlock_);
  int log_type_index = 0;
  stat.clog_checkpoint_scn_ = ls_->get_clog_checkpoint_scn();
  stat.min_rec_scn_.set_max();
  get_min_rec_scn(log_type_index, stat.min_rec_scn_);
  ObLogBaseType log_type = static_cast<ObLogBaseType>(log_type_index);
  stat.min_rec_scn_log_type_ = log_type;
  return ret;
}

int ObCheckpointExecutor::traversal_flush() const
{
  int ret = OB_SUCCESS;
  ObLSTxService *ls_tx_ser = nullptr;
  if (!update_checkpoint_enabled_) {
  } else if (OB_ISNULL(ls_tx_ser = ls_->get_tx_svr())) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "ls_tx_ser should not be null", K(ret), K(ls_->get_ls_id()));
  } else if (OB_FAIL(ls_tx_ser->traversal_flush())) {
  }
  return ret;
}

}  // namespace checkpoint
}  // namespace storage
}  // namespace oceanbase
