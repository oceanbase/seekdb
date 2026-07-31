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

#include "ob_trans_deadlock_adapter.h"
#include "data_plane/transaction/ob_deadlock.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/memtable/ob_lock_wait_mgr.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
using namespace common;
using namespace share::detector;

namespace transaction
{

#define CHECK_DEADLOCK_ENABLED() \
do {\
  if (OB_UNLIKELY(!ObDeadLockDetectorMgr::is_deadlock_enabled())) {\
    if (REACH_TIME_INTERVAL(1_s)) {\
      DETECT_LOG(INFO, "deadlock not enabled");\
    }\
    return common::OB_NOT_RUNNING;\
  }\
} while(0)

void ObTransDeadlockDetectorAdapter::copy_str_and_translate_apostrophe(const char *src_ptr,
                                                                       const int64_t src_len,
                                                                       char *dest_ptr,// C-style str, contain '\0'
                                                                       const int64_t dest_len) {
  int64_t src_idx = 0;
  int64_t dest_idx = 0;
  if (dest_len > 0 && src_ptr && dest_ptr) {
    while (src_idx < src_len && dest_idx < dest_len - 3) {// remain 1 byte for '\0', reserve 2 bytes to translate '\''
      if (src_ptr[src_idx] == '\0') {
        break;
      } else if (src_ptr[src_idx] == '\'') {
        dest_ptr[dest_idx++] = '\\';
      }
      dest_ptr[dest_idx++] = src_ptr[src_idx++];
    }
    dest_ptr[dest_idx] = '\0';
  }
};

int ObTransDeadlockDetectorAdapter::kill_tx(
    query::ObIDeadlockSessionService &session_service,
    const uint32_t sess_id)
{
  int ret = OB_SUCCESS;
  memtable::ObLockWaitMgr *mgr = nullptr;
  if (OB_UNLIKELY(sess_id == 0)) {
    ret = OB_INVALID_ARGUMENT;
    DETECT_LOG(WARN, "invalid argument", K(ret), K(sess_id));
  } else if (OB_ISNULL(mgr = ::oceanbase::share::server_service<::oceanbase::memtable::ObLockWaitMgr>())) {
    ret = OB_ERR_UNEXPECTED;
    DETECT_LOG(WARN, "can't get lock wait mgr", K(ret), K(sess_id));
  } else {
    query::ObDeadlockSessionGuard session_guard(session_service);
    if (OB_FAIL(session_guard.acquire(sess_id))) {
      DETECT_LOG(WARN, "fail to acquire transaction deadlock victim",
                 K(ret), K(sess_id));
    } else if (OB_FAIL(session_guard.mark_transaction_victim())) {
      DETECT_LOG(WARN, "fail to mark transaction deadlock victim",
                 K(ret), K(sess_id));
    } else {
      mgr->notify_deadlocked_session(sess_id);
      DETECT_LOG(INFO, "set query deadlocked success in mysql mode",
                 K(ret), K(sess_id));
    }
  }
  return ret;
}

int ObTransDeadlockDetectorAdapter::kill_stmt(
    query::ObIDeadlockSessionService &session_service,
    const uint32_t sess_id)
{
  int ret = OB_SUCCESS;
  memtable::ObLockWaitMgr *mgr = nullptr;
  if (OB_UNLIKELY(sess_id == 0)) {
    ret = OB_INVALID_ARGUMENT;
    DETECT_LOG(WARN, "invalid argument", K(ret), K(sess_id));
  } else if (OB_ISNULL(mgr = ::oceanbase::share::server_service<::oceanbase::memtable::ObLockWaitMgr>())) {
    ret = OB_ERR_UNEXPECTED;
    DETECT_LOG(WARN, "can't get lock wait mgr", K(ret), K(sess_id));
  } else {
    query::ObDeadlockSessionGuard session_guard(session_service);
    if (OB_FAIL(session_guard.acquire(sess_id))) {
      TRANS_LOG(WARN, "fail to acquire statement deadlock victim",
                K(ret), K(sess_id));
    } else if (OB_FAIL(session_guard.mark_statement_victim())) {
      TRANS_LOG(WARN, "mark statement deadlock victim failed",
                K(ret), K(sess_id));
    } else {
      mgr->notify_deadlocked_session(sess_id);
      TRANS_LOG(INFO, "set query deadlocked success", K(ret), K(sess_id));
    }
  }
  return ret;
}

// Remote execution retries each time when the lock needed is held by others.
// Before each retry, the remote execution will rollback previous operations
// through end_stmt.
//
// So we register the lock dependency when the retry is needed. The conflicts
// are detected and passed through trans_result during remote execution. NB:
// what we need pay special attention to is how to handle different conflicts in
// different reties. If the deadlock detector just activate all previous
// conflicts and block on all new conflicts, the liveness may be broken because
// each retry will push the private label and cause the cycle to be unstable.
// Current solution is to use the primitive interface, replace_block_list,
// which keeps the unchanged conflicts, activates disappearred conflicts and
// blocks on new conflicts.
//
// The dependency is unregisterred when no conflicts appear in trans_result.

int ObTransOnDetectOperation::operator()(
    const common::ObIArray<share::detector::ObDetectorInnerReportInfo> &info,
    const int64_t self_idx) {
  CHECK_DEADLOCK_ENABLED();
  UNUSED(info);
  UNUSED(self_idx);
  int ret = OB_SUCCESS;
  int step = 0;

  if (++step && OB_UNLIKELY(nullptr == session_service_
                           || sess_id_ == 0
                           || !trans_id_.is_valid())) {
    ret = OB_NOT_INIT;
  } else if (FALSE_IT(step++)) {
    // seekdb is MySQL-only; always kill_tx path
  } else {
    ret = ObTransDeadlockDetectorAdapter::kill_tx(
        *session_service_, sess_id_);
  }

  if (!OB_SUCC(ret)) {
    DETECT_LOG(WARN, "execute on detect op failed", KR(ret), K(step), K(*this));
  }

  return ret;
}

/******************************[FOR REMOTE EXECUTION]******************************/

class RemoteDeadLockCollectCallBack {
public:
  RemoteDeadLockCollectCallBack(
      query::ObIDeadlockSessionService &session_service,
      const uint32_t &sess_id)
    : session_service_(&session_service), sess_id_(sess_id) {}
  int operator()(ObDetectorUserReportInfo &info) {
    CHECK_DEADLOCK_ENABLED();
    int ret = OB_SUCCESS;
    constexpr int64_t trans_id_str_len = 128;
    constexpr int64_t current_sql_str_len = 256;
    char * buffer_trans_id = nullptr;
    char * buffer_current_sql = nullptr;
    int step = 0;
    if (OB_UNLIKELY(nullptr == (buffer_trans_id = (char*)ob_malloc(trans_id_str_len, "deadlockCB")))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      DETECT_LOG(WARN, "alloc memory failed", KR(ret));
    } else if (OB_UNLIKELY(nullptr == (buffer_current_sql = (char*)ob_malloc(current_sql_str_len, "deadlockCB")))) {
      ob_free(buffer_trans_id);
      ret = OB_ALLOCATE_MEMORY_FAILED;
      DETECT_LOG(WARN, "alloc memory failed", KR(ret));
    } else {
      query::ObDeadlockSessionGuard session_guard(*session_service_);
      query::ObDeadlockSessionFacts session_facts;
      ObTransID session_tx_id;
      if (OB_FAIL(session_guard.acquire(sess_id_))) {
        DETECT_LOG(WARN, "got session info is NULL", KR(ret), K(sess_id_));
      } else if (OB_FAIL(session_guard.get_deadlock_facts(session_facts))) {
        DETECT_LOG(WARN, "get deadlock session facts failed", KR(ret), K(sess_id_));
      } else if (!session_facts.has_transaction_) {
        ret = OB_ERR_UNEXPECTED;
        DETECT_LOG(WARN, "desc on session is not valid", KR(ret));
      } else if (FALSE_IT(session_tx_id = session_facts.transaction_id_)) {
      } else if (!session_tx_id.is_valid()) {
        ret = OB_ERR_UNEXPECTED;
        DETECT_LOG(WARN, "trans id on desc on session is not valid", KR(ret));
      } else {
        ObSharedGuard<char> temp_guard;
        const ObString &cur_query_str = session_facts.current_query_;
        if (cur_query_str.empty()) {
          DETECT_LOG(WARN, "cur_query_str on session is empty", K(cur_query_str), K(sess_id_));
        } else {
          DETECT_LOG(WARN, "cur_query_str on session is not empty", K(cur_query_str), K(sess_id_));
        }
        ObTransDeadlockDetectorAdapter::copy_str_and_translate_apostrophe(cur_query_str.ptr(),
                                                                          cur_query_str.length(),
                                                                          buffer_current_sql,
                                                                          current_sql_str_len);
        (void) session_tx_id.to_string(buffer_trans_id, trans_id_str_len);
        if (++step && OB_FAIL(temp_guard.assign((char*)"transaction", [](char*){}))) {
        } else if (++step && OB_FAIL(info.set_module_name(temp_guard))) {
        } else if (++step && OB_FAIL(temp_guard.assign((char*)"remote row", [](char*){}))) {
        } else if (++step && OB_FAIL(info.set_resource(temp_guard))) {
        } else if (++step && OB_FAIL(temp_guard.assign(buffer_trans_id, [](char *ptr){ ob_free(ptr); }))) {
        } else if (FALSE_IT(buffer_trans_id = nullptr)) {
        } else if (++step && OB_FAIL(info.set_visitor(temp_guard))) {
        } else if (++step && OB_FAIL(temp_guard.assign(buffer_current_sql, [](char *ptr){ ob_free(ptr); }))) {
        } else if (FALSE_IT(buffer_current_sql = nullptr)) {
        } else if (++step && OB_FAIL(info.set_extra_info("current sql", temp_guard))) {
        }
      }
      if (OB_FAIL(ret)) {
        if (OB_NOT_NULL(buffer_trans_id)) {
          ob_free(buffer_trans_id);
        }
        if (OB_NOT_NULL(buffer_current_sql)) {
          ob_free(buffer_current_sql);
        }
        DETECT_LOG(WARN, "get string failed in deadlock", KR(ret), K(step));
      }
    }
    return ret;
  }
private:
  query::ObIDeadlockSessionService *session_service_;
  const uint32_t sess_id_;
};

int ObTransDeadlockDetectorAdapter::gen_dependency_resource_array_(const ObIArray<ObTransID> &blocked_trans_ids,
                                                                  ObIArray<ObDependencyResource> &dependency_resources)
{
  int ret = OB_SUCCESS;
  UserBinaryKey binary_key;
  ObDependencyResource resource;
  for (int64_t idx = 0; idx < blocked_trans_ids.count() && OB_SUCC(ret); idx++) {
    if (!blocked_trans_ids.at(idx).is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      DETECT_LOG(ERROR, "invalid trans id");
    } else if (OB_FAIL(binary_key.set_user_key(blocked_trans_ids.at(idx)))) {
      DETECT_LOG(ERROR, "fail to create key");
    } else if (OB_FAIL(resource.set_args(binary_key))) {
      DETECT_LOG(ERROR, "fail to create resource");
    } else if (OB_FAIL(dependency_resources.push_back(resource))) {
      DETECT_LOG(ERROR, "fail to push resource");
    }
  }
  return ret;
}

int ObTransDeadlockDetectorAdapter::register_to_deadlock_detector_(
                                                                   query::ObIDeadlockSessionService &session_service,
                                                                   const ObTransID self_tx_id,
                                                                   const uint32_t self_session_id,
                                                                   const ObIArray<ObTransID> &conflict_tx_ids,
                                                                   const query::ObDeadlockSessionFacts &session_facts)
{
  #define PRINT_WRAPPER KR(ret), K(self_tx_id), K(self_session_id), K(conflict_tx_ids), K(query_timeout)
  int ret = OB_SUCCESS;
  int64_t query_timeout = 0;
  RemoteDeadLockCollectCallBack on_collect_op(
      session_service, self_session_id);
  ObTransOnDetectOperation on_detect_op(
      session_service, self_session_id, self_tx_id);
  ObSEArray<ObDependencyResource, DEFAULT_BLOCKED_TRANS_ID_COUNT> blocked_resources;
  if (OB_UNLIKELY(conflict_tx_ids.empty())) {
    DETECT_LOG(INFO, "conflict tx idx is empty", PRINT_WRAPPER);
  } else if (!session_facts.has_transaction_) {
    ret = OB_ERR_UNEXPECTED;
    DETECT_LOG(ERROR, "tx desc on session is NULL", PRINT_WRAPPER);
  } else if (FALSE_IT(query_timeout = session_facts.query_timeout_us_)) {
  } else if (OB_FAIL(gen_dependency_resource_array_(conflict_tx_ids, blocked_resources))) {
    DETECT_LOG(WARN, "fail to generate block resource", PRINT_WRAPPER);
  } else if (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>())) {
    ret = OB_ERR_UNEXPECTED;
    DETECT_LOG(ERROR, "mtl deadlock detector mgr is null", PRINT_WRAPPER);
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->register_key(self_tx_id,
                                                               on_detect_op,
                                                               on_collect_op,
                                                               ~session_facts.transaction_start_ts_,
                                                               3_s,
                                                               10))) {
    DETECT_LOG(WARN, "fail to register deadlock", PRINT_WRAPPER);
  } else {
    ::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->set_timeout(self_tx_id, query_timeout);
    if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->block(self_tx_id, blocked_resources))) {
      DETECT_LOG(WARN, "block on resource failed", PRINT_WRAPPER);
    } else {
      DETECT_LOG(INFO, "register to deadlock detector success", PRINT_WRAPPER);
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int ObTransDeadlockDetectorAdapter::replace_conflict_trans_ids_(const ObTransID self_tx_id,
                                                                const ObIArray<ObTransID> &conflict_tx_ids)
{
  #define PRINT_WRAPPER KR(ret), K(self_tx_id), K(conflict_tx_ids), K(current_blocked_resources)
  int ret = OB_SUCCESS;
  ObSEArray<ObDependencyResource, DEFAULT_BLOCKED_TRANS_ID_COUNT> blocked_resources;
  ObSEArray<ObDependencyResource, DEFAULT_BLOCKED_TRANS_ID_COUNT> current_blocked_resources;
  auto check_at_least_one_holder_same = [](ObSEArray<ObDependencyResource, DEFAULT_BLOCKED_TRANS_ID_COUNT> &l,
                                           ObSEArray<ObDependencyResource, DEFAULT_BLOCKED_TRANS_ID_COUNT> &r) -> bool {
    bool has_same_holder = false;
    for (int64_t idx1 = 0; idx1 < l.count() && !has_same_holder; ++idx1) {
      for (int64_t idx2 = 0; idx2 < r.count() && !has_same_holder; ++idx2) {
        if (l[idx1] == r[idx2]) {
          has_same_holder = true;
        }
      }
    }
    return has_same_holder;
  };
  if (OB_UNLIKELY(!conflict_tx_ids.empty())) {
    if (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>())) {
      ret = OB_ERR_UNEXPECTED;
      DETECT_LOG(ERROR, "mtl deadlock detector mgr is null", PRINT_WRAPPER);
    } else if (OB_FAIL(gen_dependency_resource_array_(conflict_tx_ids, blocked_resources))) {
      DETECT_LOG(ERROR, "generate dependency array failed", PRINT_WRAPPER);
    } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->get_block_list(self_tx_id, current_blocked_resources))) {
      DETECT_LOG(WARN, "generate dependency array failed", PRINT_WRAPPER);
    } else if (check_at_least_one_holder_same(current_blocked_resources, blocked_resources)) {
      if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->replace_block_list(self_tx_id, blocked_resources))) {
        DETECT_LOG(WARN, "replace block list failed", PRINT_WRAPPER);
      }
      (void) ::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->dec_count_down_allow_detect(self_tx_id);
    } else {
      unregister_from_deadlock_detector(self_tx_id,
                                        UnregisterPath::REPLACE_MEET_TOTAL_DIFFERENT_LIST);
      DETECT_LOG(WARN, "unregister detector cause meet total different block list", PRINT_WRAPPER);
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int ObTransDeadlockDetectorAdapter::register_or_replace_conflict_trans_ids(
                                                                                            query::ObIDeadlockSessionService &session_service,
                                                                                            const ObTransID self_tx_id,
                                                                                            const uint32_t self_session_id,
                                                                                            const ObArray<ObTransID> &conflict_tx_ids)
{
  #define PRINT_WRAPPER KR(ret), K(self_tx_id), K(self_session_id), K(conflict_tx_ids)
  CHECK_DEADLOCK_ENABLED();
  int ret = OB_SUCCESS;
  query::ObDeadlockSessionGuard session_guard(session_service);
  query::ObDeadlockSessionFacts session_facts;
  bool is_detector_exist = false;
  if (self_session_id == 1) {
    DETECT_LOG(INFO, "inner session no need register to deadlock", PRINT_WRAPPER);
  } else if (self_session_id == 0) {
    DETECT_LOG(ERROR, "invalid session id", PRINT_WRAPPER);
  } else if (conflict_tx_ids.empty()) {
    DETECT_LOG(WARN, "empty conflict tx ids", PRINT_WRAPPER);
  } else if (OB_FAIL(session_guard.acquire(self_session_id))) {
    DETECT_LOG(ERROR, "fail to get session info", PRINT_WRAPPER);
  } else if (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>())) {
    ret = OB_ERR_UNEXPECTED;
    DETECT_LOG(ERROR, "MTL ObDeadLockDetectorMgr is NULL", PRINT_WRAPPER);
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->check_detector_exist(self_tx_id, is_detector_exist))) {
    DETECT_LOG(WARN, "fail to get detector exist status", PRINT_WRAPPER);
  } else if (!is_detector_exist) {
    if (OB_FAIL(session_guard.get_deadlock_facts(session_facts))) {
      DETECT_LOG(WARN, "get deadlock session facts failed", PRINT_WRAPPER);
    } else if (OB_FAIL(register_to_deadlock_detector_(
                   session_service,
                   self_tx_id,
                   self_session_id,
                   conflict_tx_ids,
                   session_facts))) {
      DETECT_LOG(WARN, "register new detector in remote execution failed", PRINT_WRAPPER);
    } else {
      DETECT_LOG(INFO, "register new detector in remote execution", PRINT_WRAPPER);
    }
  } else {
    if (OB_FAIL(replace_conflict_trans_ids_(self_tx_id, conflict_tx_ids))) {
      DETECT_LOG(INFO, "replace block list in remote execution", PRINT_WRAPPER);
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int ObTransDeadlockDetectorAdapter::create_detector_node_and_set_parent_if_needed_(CollectCallBack &on_collect_op,
                                                                                  const ObTransID &self_trans_id,
                                                                                  const uint32_t sess_id,
                                                                                  query::ObIDeadlockSessionService &session_service)
{
  #define PRINT_WRAPPER KR(ret), K(self_trans_id), K(sess_id), K(query_timeout), K(trans_begin_ts)
  int ret = OB_SUCCESS;
  int64_t query_timeout = 0;
  int64_t trans_begin_ts = 0;
  ObTransOnDetectOperation on_detect_op(
      session_service, sess_id, self_trans_id);
  query::ObDeadlockSessionGuard guard(session_service);
  query::ObDeadlockSessionFacts session_facts;
  if (OB_FAIL(guard.acquire(sess_id))) {
    DETECT_LOG(WARN, "fail to get session related info", PRINT_WRAPPER);
  } else if (OB_FAIL(guard.get_deadlock_facts(session_facts))) {
    DETECT_LOG(WARN, "fail to get deadlock session facts", PRINT_WRAPPER);
  } else if (!session_facts.has_transaction_) {
    ret = OB_BAD_NULL_ERROR;
    DETECT_LOG(WARN, "tx desc is NULL", PRINT_WRAPPER);
  } else if (FALSE_IT(query_timeout = session_facts.query_timeout_us_)) {
  } else if (FALSE_IT(trans_begin_ts = session_facts.transaction_start_ts_)) {
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->register_key(self_trans_id,
                                                               on_detect_op,
                                                               on_collect_op,
                                                               ~trans_begin_ts))) {
    DETECT_LOG(WARN, "fail to register key", PRINT_WRAPPER);
  } else {
    ::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->set_timeout(self_trans_id, query_timeout);
  }
  return ret;
  #undef PRINT_WRAPPER
}

/******************************BELOW INTERFACE CALL FROM OTHER FILES******************************/

// Call from SQL trans control, check if need register to deadlock or replace block list
// (depends on session status, is registered to deadlock or not)
//
// @param [in] on_collect_op collect deadlock related info when deadlock detected.
// @param [in] func the block function to tell detector waiting for who.
// @param [in] self_trans_id who am i.
// @param [in] sess_id which session to kill if this node is killed.
// @return the error code.
int ObTransDeadlockDetectorAdapter::maintain_deadlock_info_when_end_stmt(
    ObTxDesc &tx_desc,
    query::ObIDeadlockSessionService &session_service,
    const data_plane::ObStatementDeadlockContext &context)
{
  #define PRINT_WRAPPER K(step), KR(ret), K(context.exec_error_), \
                        K(context.is_rollback_), K(tx_desc), K(conflict_txs)
  int ret = OB_SUCCESS;
  int step = 0;
  CHECK_DEADLOCK_ENABLED();
  ObArray<ObTransID> conflict_txs;
  if (++step && context.is_inner_session_) {
    DETECT_LOG(TRACE, "inner session no need register to deadlock", PRINT_WRAPPER);
  } else if (++step && OB_FAIL(tx_desc.fetch_conflict_txs(conflict_txs))) {
    DETECT_LOG(WARN, "fail to get conflict txs from desc", PRINT_WRAPPER);
  } else if (++step && !tx_desc.is_valid()) {
    DETECT_LOG(INFO, "invalid tx desc no need register to deadlock", PRINT_WRAPPER);
  } else if (++step && context.is_rollback_) {// statment is failed, maybe will try again, check if need register to deadlock detector
    if (++step && context.query_timeout_ts_ < ObClockGenerator::getClock()) {
      unregister_from_deadlock_detector(tx_desc.tid(), UnregisterPath::END_STMT_TIMEOUT);
      DETECT_LOG(INFO, "query timeout, no need register to deadlock", PRINT_WRAPPER);
    } else if (++step && conflict_txs.empty()) {
      unregister_from_deadlock_detector(tx_desc.tid(), UnregisterPath::END_STMT_NO_CONFLICT);
      DETECT_LOG(INFO, "try unregister deadlock detecotr cause conflict array is empty", PRINT_WRAPPER);
    } else if (++step && context.exec_error_ != OB_TRY_LOCK_ROW_CONFLICT) {
      unregister_from_deadlock_detector(tx_desc.tid(), UnregisterPath::END_STMT_OTHER_ERR);
      DETECT_LOG(INFO, "try unregister deadlock detecotr cause meet non-lock error", PRINT_WRAPPER);
    } else if (++step && OB_FAIL(register_or_replace_conflict_trans_ids(
        session_service,
        tx_desc.tid(),
        context.session_id_,
        conflict_txs))) {
      DETECT_LOG(WARN, "register or replace list failed", PRINT_WRAPPER);
    } else {
      // do nothing, register success or keep retrying
    }
  } else {// statment is done, will not try again, all related deadlock info should be resetted
    unregister_from_deadlock_detector(tx_desc.tid(), UnregisterPath::END_STMT_DONE);
    DETECT_LOG(TRACE, "try unregister from deadlock detector", KR(ret), K(tx_desc.tid()));
  }
  tx_desc.reset_conflict_txs();
  if (OB_SUCC(ret)) {
    if (OB_SUCCESS != context.exec_error_) {
      if ((OB_ITER_END != context.exec_error_)) {
        if (context.retry_count_ <= 1 ||// first time lock conflict or other error
            context.retry_count_ % 10 == 0) {// other wise, control log print frequency
          DETECT_LOG(INFO, "maintain deadlock info", PRINT_WRAPPER);
        }
      }
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

// Call from LockWaitMgr, register local excution waiting for row
// 
// @param [in] on_collect_op collect deadlock related info when deadlock detected.
// @param [in] func the block function to tell detector waiting for who.
// @param [in] self_trans_id who am i.
// @param [in] sess_id which session to kill if this node is killed.
// @return the error code.
int ObTransDeadlockDetectorAdapter::lock_wait_mgr_reconstruct_detector_waiting_for_row(CollectCallBack &on_collect_op,
                                                                                       const BlockCallBack &func,
                                                                                       const ObTransID &self_trans_id,
                                                                                       const uint32_t sess_id,
                                                                                       query::ObIDeadlockSessionService &session_service)
{
  #define PRINT_WRAPPER KR(ret), K(self_trans_id), K(sess_id), K(exist)
  CHECK_DEADLOCK_ENABLED();
  int ret = OB_SUCCESS;
  bool exist = false;
  if (sess_id == 0) {
    DETECT_LOG(ERROR, "invalid session id", PRINT_WRAPPER);
  } else if (nullptr == ::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()) {
    ret = OB_ERR_UNEXPECTED;
    DETECT_LOG(WARN, "fail to get ObDeadLockDetectorMgr", PRINT_WRAPPER);
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->check_detector_exist(self_trans_id, exist))) {
    DETECT_LOG(WARN, "fail to check detector exist", PRINT_WRAPPER);
  } else if (exist) {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->unregister_key(self_trans_id))) {
      DETECT_LOG(WARN, "fail to unregister key", K(tmp_ret), PRINT_WRAPPER);
    }
  }
  if (OB_FAIL(ret)) {
    DETECT_LOG(WARN, "local execution register to deadlock detector waiting for row failed", PRINT_WRAPPER);
  } else if (OB_FAIL(create_detector_node_and_set_parent_if_needed_(
                 on_collect_op, self_trans_id, sess_id, session_service))) {
    DETECT_LOG(WARN, "fail to create detector node", PRINT_WRAPPER);
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->block(self_trans_id, func))) {
    DETECT_LOG(WARN, "fail to block on call back function", PRINT_WRAPPER);
  } else {
    DETECT_LOG(TRACE, "local execution register to deadlock detector waiting for row success", PRINT_WRAPPER);
  }
  return ret;
  #undef PRINT_WRAPPER
}

// Call from LockWaitMgr, register local excution waiting for trans
// 
// @param [in] on_collect_op collect deadlock related info when deadlock detected.
// @param [in] conflict_trans_id tell detector waiting for who.
// @param [in] self_trans_id who am i.
// @param [in] sess_id which session to kill if this node is killed.
// @return the error code.
int ObTransDeadlockDetectorAdapter::lock_wait_mgr_reconstruct_detector_waiting_for_trans(CollectCallBack &on_collect_op,
                                                                                         const ObTransID &conflict_trans_id,
                                                                                         const ObTransID &self_trans_id,
                                                                                         const uint32_t sess_id,
                                                                                         query::ObIDeadlockSessionService &session_service)
{
  #define PRINT_WRAPPER KR(ret), K(self_trans_id), K(sess_id), K(exist)
  CHECK_DEADLOCK_ENABLED();
  int ret = OB_SUCCESS;
  bool exist = false;
  if (nullptr == ::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()) {
    ret = OB_ERR_UNEXPECTED;
    DETECT_LOG(WARN, "fail to get ObDeadLockDetectorMgr", PRINT_WRAPPER);
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->check_detector_exist(self_trans_id, exist))) {
    DETECT_LOG(WARN, "fail to check detector exist", PRINT_WRAPPER);
  } else if (exist) {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->unregister_key(self_trans_id))) {
      DETECT_LOG(WARN, "fail to unregister key", K(tmp_ret), PRINT_WRAPPER);
    }
  }
  if (OB_FAIL(ret)) {
    DETECT_LOG(WARN, "local execution register to deadlock detector waiting for row failed", PRINT_WRAPPER);
  } else if (OB_FAIL(create_detector_node_and_set_parent_if_needed_(
                 on_collect_op, self_trans_id, sess_id, session_service))) {
    DETECT_LOG(WARN, "fail to create detector node", PRINT_WRAPPER);
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->block(self_trans_id, conflict_trans_id))) {
    DETECT_LOG(WARN, "fail to block on conflict trans", PRINT_WRAPPER);
  } else {
    DETECT_LOG(TRACE, "local execution register to deadlock detector waiting for trans success", PRINT_WRAPPER);
  }
  return ret;
  #undef PRINT_WRAPPER
}

int ObTransDeadlockDetectorAdapter::change_detector_waiting_obj_from_row_to_trans(const ObTransID &self_trans_id,
                                                                                  const ObTransID &conflict_trans_id)
{
  #define PRINT_WRAPPER KR(ret), K(self_trans_id), K(conflict_trans_id)
  CHECK_DEADLOCK_ENABLED();
  int ret = OB_SUCCESS;
  if (nullptr == ::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()) {
    ret = OB_ERR_UNEXPECTED;
    DETECT_LOG(WARN, "fail to get ObDeadLockDetectorMgr", PRINT_WRAPPER);
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->activate_all(self_trans_id))) {
    DETECT_LOG(WARN, "fail to activate all", PRINT_WRAPPER);
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->block(self_trans_id, conflict_trans_id))) {
    DETECT_LOG(WARN, "fail to block on conflict trans", PRINT_WRAPPER);
  } else {
    DETECT_LOG(INFO, "change denpendency relationship from row to trnas", PRINT_WRAPPER);
  }
  return ret;
  #undef PRINT_WRAPPER
}

// Register autonomous trans dependency relationship, no need session id here, cause this trans should not be killed
// 
// @param [in] last_trans_id who is the trans before start autonomous trans.
// @param [in] now_trans_id who is the trans after start autonomous trans.
// @param [in] query_timeout from session, to tell detector how long it will live(avoid leak).
// @return void.
int ObTransDeadlockDetectorAdapter::inner_tx_register_to_deadlock(const ObTransID last_trans_id,
                                                                 const ObTransID now_trans_id,
                                                                 const int64_t query_timeout)
{
  #define PRINT_WRAPPER KR(ret), K(last_trans_id), K(now_trans_id), K(query_timeout)
  CHECK_DEADLOCK_ENABLED();
  int ret = OB_SUCCESS;
  if (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>())) {
    ret = OB_ERR_UNEXPECTED;
    DETECT_LOG(ERROR, "tenant deadlock detector mgr is null", PRINT_WRAPPER);
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->register_key(last_trans_id,
                                                  [](const common::ObIArray<ObDetectorInnerReportInfo> &,
                                                    const int64_t) { DETECT_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "should not kill inner node");
                                                                      return common::OB_ERR_UNEXPECTED; },
                                                  [last_trans_id](ObDetectorUserReportInfo& report_info) {
                                                    ObSharedGuard<char> ptr;
                                                    ptr.assign((char*)"detector", [](char*){});
                                                    report_info.set_module_name(ptr);
                                                    char *buffer = (char *)ob_malloc(sizeof(char) * 64, "DeadLockDA");
                                                    if (OB_NOT_NULL(buffer)) {
                                                      last_trans_id.to_string(buffer, 64);
                                                      buffer[63] = '\0';
                                                      ptr.assign((char*)buffer, [](char* p){ ob_free(p); });
                                                    } else {
                                                      DETECT_LOG_RET(WARN, OB_ALLOCATE_MEMORY_FAILED, "alloc memory failed");
                                                      ptr.assign((char*)"inner visitor", [](char*){});
                                                    }
                                                    report_info.set_visitor(ptr);
                                                    ptr.assign((char*)"waiting for autonomous trans", [](char*){});
                                                    report_info.set_resource(ptr);
                                                    return common::OB_SUCCESS;
                                                  },
                                                  ObDetectorPriority(PRIORITY_RANGE::EXTREMELY_HIGH, 0)))) {
    DETECT_LOG(WARN, "register key failed", PRINT_WRAPPER);
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->block(last_trans_id, now_trans_id))) {
    DETECT_LOG(WARN, "block resource failed", PRINT_WRAPPER);
  } else {
    ::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>()->set_timeout(last_trans_id, query_timeout);
    DETECT_LOG(INFO, "register autonomous deadlock dependency success", PRINT_WRAPPER);
  }
  return ret;
}

// Call from ALL PATH, unregister detector, and mark the reason
// 
// @param [in] self_trans_id who am i.
// @param [in] path call from which code path.
// @return void.
void ObTransDeadlockDetectorAdapter::unregister_from_deadlock_detector(const ObTransID &self_trans_id,
                                                                       const UnregisterPath path)
{
  int ret = common::OB_SUCCESS;
  ObDeadLockDetectorMgr *mgr = nullptr;
  if (nullptr == (mgr = ::oceanbase::share::server_service<::oceanbase::share::detector::ObDeadLockDetectorMgr>())) {
    ret = OB_ERR_UNEXPECTED;
    DETECT_LOG(WARN, "fail to get ObDeadLockDetectorMgr", K(self_trans_id), K(to_string(path)));
  } else if (OB_FAIL(mgr->unregister_key(self_trans_id))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      DETECT_LOG(ERROR, "unregister from deadlock detector failed", K(self_trans_id), K(to_string(path)));
    } else {
      ret = OB_SUCCESS;// it's ok if detector not exist
    }
  } else {
    DETECT_LOG(TRACE, "unregister from deadlock detector success", K(self_trans_id), K(to_string(path)));
  }
}

} // namespace transaction

namespace data_plane
{

int maintain_deadlock_after_statement(
    transaction::ObTxDesc &tx,
    query::ObIDeadlockSessionService &session_service,
    const ObStatementDeadlockContext &context)
{
  return transaction::ObTransDeadlockDetectorAdapter::
      maintain_deadlock_info_when_end_stmt(
          tx, session_service, context);
}

int register_autonomous_transaction_dependency(
    const transaction::ObTransID &suspended_tx_id,
    const transaction::ObTransID &autonomous_tx_id,
    int64_t timeout_us)
{
  int ret = common::OB_SUCCESS;
  if (!suspended_tx_id.is_valid()
      || !autonomous_tx_id.is_valid()
      || suspended_tx_id == autonomous_tx_id) {
    ret = common::OB_INVALID_ARGUMENT;
    DETECT_LOG(WARN, "invalid autonomous transaction dependency",
               K(ret), K(suspended_tx_id), K(autonomous_tx_id), K(timeout_us));
  } else {
    ret = transaction::ObTransDeadlockDetectorAdapter::
        inner_tx_register_to_deadlock(
            suspended_tx_id, autonomous_tx_id, timeout_us);
  }
  return ret;
}

void finish_transaction_deadlock(const transaction::ObTransID &tx_id)
{
  transaction::ObTransDeadlockDetectorAdapter::unregister_from_deadlock_detector(
      tx_id,
      transaction::ObTransDeadlockDetectorAdapter::UnregisterPath::DO_END_TRANS);
}

void rollback_statement_deadlock(const transaction::ObTransID &tx_id)
{
  transaction::ObTransDeadlockDetectorAdapter::unregister_from_deadlock_detector(
      tx_id,
      transaction::ObTransDeadlockDetectorAdapter::UnregisterPath::TX_ROLLBACK_IN_END_STMT);
}

} // namespace data_plane
} // namespace oceanbase
