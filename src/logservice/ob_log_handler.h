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

#ifndef OCEANBASE_LOGSERVICE_OB_LOG_HANDLER_
#define OCEANBASE_LOGSERVICE_OB_LOG_HANDLER_
#include <atomic>
#include <cstdint>
#include "lib/utility/ob_macro_utils.h"
#include "lib/lock/ob_tc_rwlock.h"
#include "common/ob_role.h"
#include "share/ob_delegate.h"
#include "wal_adapter/env.h"
#include "wal_adapter/handle.h"
#include "share/log/types/base_info.h"
#include "wal_adapter/iterator.h"

namespace oceanbase
{
namespace common
{
class ObAddr;
}
namespace share
{
class SCN;
}
namespace transaction
{
class ObTsMgr;
}
namespace wal
{
class LSN;
}
namespace logservice
{
class ObLogApplyService;
class ObApplyStatus;
class ObLogService;
class AppendCb;
class ObILogHandler
{
public:
  virtual ~ObILogHandler() {}
  virtual bool is_valid() const = 0;
  virtual int append(const void *buffer,
                     const int64_t nbytes,
                     const share::SCN &ref_scn,
                     AppendCb *cb,
                     wal::LSN &lsn,
                     share::SCN &scn) = 0;

  virtual int seek(const wal::LSN &lsn, wal::EntryIterator &iter) = 0;
  virtual int seek(const wal::LSN &lsn, wal::GroupIterator &iter) = 0;

  virtual int locate_by_scn_coarsely(const share::SCN &scn, wal::LSN &result_lsn) = 0;
  virtual int locate_by_lsn_coarsely(const wal::LSN &lsn, share::SCN &result_scn) = 0;
  virtual int advance_base_lsn(const wal::LSN &lsn) = 0;
  virtual int get_base_lsn(wal::LSN &lsn) const = 0;
  virtual int get_end_lsn(wal::LSN &lsn) const = 0;

  virtual int get_end_scn(share::SCN &scn) const = 0;
  virtual int get_wal_base_info(const wal::LSN &base_lsn, wal::BaseInfo &wal_base_info) = 0;
  virtual void wait_append_sync() = 0;
  virtual int enable_replay(const wal::LSN &initial_lsn, const share::SCN &initial_scn) = 0;
  virtual int disable_replay() = 0;
  virtual int get_max_decided_scn(share::SCN &scn) = 0;
  virtual int pend_submit_replay_log() = 0;
  virtual int restore_submit_replay_log() = 0;
  virtual bool is_replay_enabled() const = 0;
  virtual int offline() = 0;
  virtual int online(const wal::LSN &lsn, const share::SCN &scn) = 0;
  virtual bool is_offline() const = 0;
  virtual int is_replay_fatal_error(bool &has_fatal_error) = 0;
};

class ObLogHandler : public ObILogHandler
{
public:
  ObLogHandler();
  virtual ~ObLogHandler();

  int init(const common::ObAddr &self,
           ObLogApplyService *apply_service,
           ObLogService *log_service,
           wal::Env *wal_env);
  bool is_valid() const;
  int stop();
  void destroy();
  // @brief append count bytes from the buffer starting at buf to the wal handle, return the LSN and timestamp
  // @param[in] const void *, the data buffer.
  // @param[in] const uint64_t, the length of data buffer.
  // @param[in] const int64_t, the base timestamp(ns), wal will ensure that the return tiemstamp will greater
  //            or equal than this field.
  // @param[in] const bool, decide this append option whether need block thread.
  // @param[int] AppendCb*, the callback of this append option, log handler will ensure that cb will be called after log has been committed
  // @param[out] LSN&, the append position.
  // @param[out] int64_t&, the append timestamp.
  // @retval
  //    OB_SUCCESS
  //    OB_NOT_MASTER, the prospoal_id of ObLogHandler is not same with Handle.
  int append(const void *buffer,
             const int64_t nbytes,
             const share::SCN &ref_scn,
             AppendCb *cb,
             wal::LSN &lsn,
             share::SCN &scn) override final;
  
  int append_imported_groups(const wal::ImportedGroups &groups);
  void set_local_append_enabled(const bool enabled)
  {
    local_append_enabled_.store(enabled, std::memory_order_release);
  }

  // @desc: seek a log buffer iterator by lsn, the first log A in iterator must meet
  //        the start lsn of log A must equal to 'start_lsn'.
  // @params [in] start_lsn:
  // @params [out] iter: buffer iterator in which all logs's lsn are higher to 'start_lsn'
  //                    (include 'start_lsn').
  // @return
  // - OB_SUCCESS
  // - OB_INVALID_ARGUMENT
  // - OB_ALLOCATE_MEMORY_FAILED
  // - OB_ENTRY_NOT_EXIST: there is no log's lsn is higher than lsn
  // - OB_ERR_OUT_OF_LOWER_BOUND: lsn is too small, log files may have been recycled
  // - others: bug
  int seek(const wal::LSN &lsn,
           wal::EntryIterator &iter) override final;

  // @desc: seek a log group buffer iterator by lsn, the first log A in iterator must meet
  //        the start lsn of log A must equal to 'start_lsn'.
  // @params [in] start_lsn:
  // @params [out] iter: buffer iterator in which all logs's lsn are higher to 'start_lsn'
  //                    (include 'start_lsn').
  // @return
  // - OB_SUCCESS
  // - OB_INVALID_ARGUMENT
  // - OB_ALLOCATE_MEMORY_FAILED
  // - OB_ENTRY_NOT_EXIST: there is no log's lsn is higher than lsn
  // - OB_ERR_OUT_OF_LOWER_BOUND: lsn is too small, log files may have been recycled
  // - others: bug
  int seek(const wal::LSN &lsn,
           wal::GroupIterator &iter) override final;

  // @desc: seek a log group buffer iterator by scn, the first A in iterator must meet
  // one of the following conditions:
  // 1. scn of log A equals to scn
  // 2. scn of log A is higher than scn and A is the first log which scn is higher
  // than scn in all committed logs
  // Note that this function may be time-consuming
  // @params [in] scn:
  // @params [out] group_iterator: log group buffer iterator in which all logs's scn are higher than/equal to
  //                     scn
  // @return
  // - OB_SUCCESS
  // - OB_INVALID_ARGUMENT
  // - OB_ALLOCATE_MEMORY_FAILED
  // - OB_ENTRY_NOT_EXIST: there is no log's scn is higher than scn
  // - OB_ERR_OUT_OF_LOWER_BOUND: scn is too old, log files may have been recycled
  // - others: bug
  // @desc: seek a log buffer iterator by scn, the first A in iterator must meet
  // one of the following conditions:
  // 1. scn of log A equals to scn
  // 2. scn of log A is higher than scn and A is the first log which scn is higher
  // than scn in all committed logs
  // Note that this function may be time-consuming
  // @params [in] scn:
  // @params [out] buff_iterator: log buffer iterator in which all logs's scn are higher than/equal to
  //                     scn
  // @return
  // - OB_SUCCESS
  // - OB_INVALID_ARGUMENT
  // - OB_ALLOCATE_MEMORY_FAILED
  // - OB_ENTRY_NOT_EXIST: there is no log's scn is higher than scn
  // - OB_ERR_OUT_OF_LOWER_BOUND: scn is too old, log files may have been recycled
  // - others: bug

  // @desc: query coarse lsn by ts(ns), that means there is a group record in disk,
  // its lsn and scn are result_lsn and result_scn, and result_scn <= scn.
  // Note that this function may be time-consuming
  // Note that result_lsn is a readable coarse lower bound for the located log.
  // @params [in] scn: timestamp(nano second)
  // @params [out] result_lsn: the lower bound lsn which includes scn
  // @return
  // - OB_SUCCESS: locate_by_scn_coarsely success
  // - OB_INVALID_ARGUMENT
  // - OB_ENTRY_NOT_EXIST: there is no log in disk
  // - OB_ERR_OUT_OF_LOWER_BOUND: scn is too old, log files may have been recycled
  // - OB_NEED_RETRY: the block is being recycled or switched, need retry.
  // - others: bug
  int locate_by_scn_coarsely(const share::SCN &scn, wal::LSN &result_lsn) override final;

  // @desc: query coarse ts by lsn, that means there is a log in disk,
  // its lsn and scn are result_lsn and result_scn, and result_lsn <= lsn.
  // Note that this function may be time-consuming
  // @params [in] lsn: lsn
  // @params [out] result_scn: the lower bound timestamp which includes lsn
  // - OB_SUCCESS; locate_by_lsn_coarsely success
  // - OB_INVALID_ARGUMENT
  // - OB_ERR_OUT_OF_LOWER_BOUND: lsn is too small, log files may have been recycled
  // - OB_NEED_RETRY: the block is being recycled or switched, need retry.
  // - others: bug
  int locate_by_lsn_coarsely(const wal::LSN &lsn, share::SCN &result_scn) override final;
  // @brief, set the recycable lsn, wal will ensure that the data before recycable lsn readable.
  // @param[in] const LSN&, recycable lsn.
  int advance_base_lsn(const wal::LSN &lsn) override final;
  // @brief, get the persisted recyclable boundary of WAL.
  // @param[out] LSN&, base lsn
  int get_base_lsn(wal::LSN &lsn) const override final;
  int get_end_lsn(wal::LSN &lsn) const override final;
  // @brief, get timestamp of end lsn.
  // @param[out] int64_t, timestamp.
  int get_end_scn(share::SCN &scn) const override final;
  // @brief, get parent
  // @param[out] addr: address of parent
  // retval:
  //   OB_SUCCESS
  //   OB_NOT_INIT
  //   OB_ENTRY_NOT_EXIST: parent is invalid
  // BaseInfo include the 'base_lsn' and the 'prev_log_info' of sliding window.
  // @param[in] const LSN&, base_lsn of ls.
  // @param[out] BaseInfo&, wal_base_info
  // retval:
  //   OB_SUCCESS
  //   OB_ERR_OUT_OF_LOWER_BOUND, the block of 'base_lsn' has been recycled
  int get_wal_base_info(const wal::LSN &base_lsn, wal::BaseInfo &wal_base_info) override final;
  // @breif, wait cb append onto apply service Qsync
  // protect submit log and push cb in Qsync guard
  void wait_append_sync() override final;
  // @brief, enable replay status with specific start point.
  // @param[in] const wal::LSN &initial_lsn: replay new start lsn.
  // @param[in] const int64_t &initial_scn: replay new start ts.
  int enable_replay(const wal::LSN &initial_lsn,
                    const share::SCN &initial_scn) override final;
  // @brief, disable replay for current ls.
  int disable_replay() override final;
  // @brief, pending sumbit replay log
  int pend_submit_replay_log() override final;
  // @brief, restore sumbit replay log
  int restore_submit_replay_log() override final;

  // @brief, check if replay is enabled, including after its executor finishes.
  bool is_replay_enabled() const override final;
  // @brief, get max decided scn considering both apply and replay.
  // @param[out] int64_t&, max decided scn.
  // @return
  // OB_NOT_INIT: not inited
  // OB_STATE_NOT_MATCH: ls is offline or stopped
  // OB_SUCCESS
  int get_max_decided_scn(share::SCN &scn) override final;
  TO_STRING_KV(KP(wal_env_), K(is_in_stop_state_), K(is_inited_));
  int offline() override final;
  int online(const wal::LSN &lsn, const share::SCN &scn) override final;
  bool is_offline() const override final;
  // @brief: check there's a fatal error in replay service.
  // @param[out] has_fatal_error.
  // @return:
  // OB_NOT_INIT: not inited
  // OB_NOT_RUNNING: in stop state
  // OB_EAGAIN: try lock failed, need retry.
  int is_replay_fatal_error(bool &has_fatal_error);
private:
  static constexpr int64_t MIN_CONN_TIMEOUT_US = 5 * 1000 * 1000;     // 5s
  typedef common::RWLock RWLock;
  typedef RWLock::RLockGuard RLockGuard;
  typedef RWLock::WLockGuard WLockGuard;
private:
  int append_(const void *buffer,
              const int64_t nbytes,
              const share::SCN &ref_scn,
              AppendCb *cb,
              wal::LSN &lsn,
              share::SCN &scn);

  template<typename StartPoint, typename IteratorType>
  int seek_log_iterator_dispatch_(const StartPoint &start_point,
                                  IteratorType &iterator);

  int advance_base_lsn_impl_(const wal::LSN &lsn);
  DISALLOW_COPY_AND_ASSIGN(ObLogHandler);
private:
  common::ObAddr self_;
  mutable RWLock lock_;
  wal::Handle wal_handle_;
  wal::Env *wal_env_;
  bool is_in_stop_state_;
  bool is_inited_;
  //log_handler will frequently call apply_status, reducing the overhead of hashing through applyservice
  ObApplyStatus *apply_status_;
  ObLogApplyService *apply_service_;
  ObLogService *log_service_;
  common::TCRWLock deps_lock_;
  common::ObQSync ls_qs_;
  ObMiniStat::ObStatItem append_cost_stat_;
  std::atomic<bool> local_append_enabled_;
  bool is_offline_;
  mutable int64_t get_max_decided_scn_debug_time_;
};

struct ObLogStat
{
public:
  wal::Stat wal_stat_;
  bool in_sync_;
  TO_STRING_KV(K_(wal_stat), K_(in_sync))
};

// =============================== Iterator begin ===========================
template <typename StartPoint, typename IteratorType>
int seek_log_iterator_no_shared_storage(wal::Env *wal_env,
                                        const StartPoint &start_point,
                                        IteratorType &iterator)
{
  int ret = OB_SUCCESS;
  wal::Handle wal_handle;
  if (NULL == wal_env || !start_point.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid argument", KP(wal_env), K(start_point));
  } else if (OB_FAIL(wal_env->open(wal_handle))) {
  } else if (OB_FAIL(wal_handle.seek(start_point, iterator))) {
  }
  if (wal_handle.is_valid()) {
    wal_env->close(wal_handle);
  }
  return ret;
}

template <class StartPoint, class IteratorType>
int ObLogHandler::seek_log_iterator_dispatch_(const StartPoint &start_point,
                                              IteratorType &iterator)
{
  int ret = OB_SUCCESS;
  RLockGuard guard(lock_);
  ret = seek_log_iterator_no_shared_storage(wal_env_, start_point, iterator);
  return ret;
}

// =============================== Iterator end===========================
} // end namespace logservice
} // end namespace oceanbase
#endif
