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

#ifndef OCEANBASE_TRANSACTION_OB_TRANS_DEADLOCK_ADAPTER_H_
#define OCEANBASE_TRANSACTION_OB_TRANS_DEADLOCK_ADAPTER_H_

#include "storage/deadlock/ob_deadlock_detector_mgr.h"
#include "storage/tx/ob_trans_define.h"
#include "query/session/ob_deadlock_session.h"
#include "ob_trans_define_v4.h"

namespace oceanbase
{
namespace data_plane
{
struct ObStatementDeadlockContext;
}
namespace transaction
{

class ObTransOnDetectOperation
{
public:
  ObTransOnDetectOperation(query::ObIDeadlockSessionService &session_service,
                           const uint32_t sess_id,
                           const ObTransID &trans_id) :
    session_service_(&session_service), sess_id_(sess_id), trans_id_(trans_id) {}
  ~ObTransOnDetectOperation() {}
  int operator()(const common::ObIArray<share::detector::ObDetectorInnerReportInfo> &info,
                 const int64_t self_idx);
  TO_STRING_KV(KP(this), K_(sess_id), K_(trans_id));
private:
  query::ObIDeadlockSessionService *session_service_;
  uint32_t sess_id_;
  ObTransID trans_id_;
};

// ObTransDeadlockDetectorAdapter is a helper class which provides utility
// functions for deadlock detector of transaction component
class ObTransDeadlockDetectorAdapter
{
  typedef share::detector::DetectCallBack DetectCallBack;
  typedef share::detector::BlockCallBack BlockCallBack;
  typedef share::detector::CollectCallBack CollectCallBack;
 public:
  enum class UnregisterPath {
    LOCK_WAIT_MGR_REPOST = 1,
    LOCK_WAIT_MGR_WAIT_FAILED,
    LOCK_WAIT_MGR_TRANSFORM_WAITING_ROW_TO_TX,
    END_STMT_DONE,
    END_STMT_OTHER_ERR,
    END_STMT_NO_CONFLICT,
    END_STMT_TIMEOUT,
    REPLACE_MEET_TOTAL_DIFFERENT_LIST,
    DO_END_TRANS,
    TX_ROLLBACK_IN_END_STMT,
  };
  static const char* to_string(const UnregisterPath path)
  {
    switch (path) {
    case UnregisterPath::LOCK_WAIT_MGR_REPOST:
      return "LOCK_WAIT_MGR_REPOST";
    case UnregisterPath::LOCK_WAIT_MGR_WAIT_FAILED:
      return "LOCK_WAIT_MGR_WAIT_FAILED";
    case UnregisterPath::LOCK_WAIT_MGR_TRANSFORM_WAITING_ROW_TO_TX:
      return "LOCK_WAIT_MGR_TRANSFORM_WAITING_ROW_TO_TX";
    case UnregisterPath::END_STMT_DONE:
      return "END_STMT_DONE";
    case UnregisterPath::END_STMT_OTHER_ERR:
      return "END_STMT_OTHER_ERROR";
    case UnregisterPath::END_STMT_NO_CONFLICT:
      return "END_STMT_NO_CONFLICT";
    case UnregisterPath::END_STMT_TIMEOUT:
      return "END_STMT_TIMEOUT";
    case UnregisterPath::REPLACE_MEET_TOTAL_DIFFERENT_LIST:
      return "REPLACE_MEET_TOTAL_DIFFERENT_LIST";
    case UnregisterPath::DO_END_TRANS:
      return "DO_END_TRANS";
    case UnregisterPath::TX_ROLLBACK_IN_END_STMT:
      return "TX_ROLLBACK_IN_END_STMT";
    default:
      return "UNKNOWN";
    }
  }
  /**********MAIN INTERFACE**********/
  // for local execution, call from lock wait mgr
  static int lock_wait_mgr_reconstruct_detector_waiting_for_row(CollectCallBack &on_collect_op,
                                                                const BlockCallBack &call_back,
                                                                const ObTransID &self_trans_id,
                                                                const uint32_t sess_id,
                                                                query::ObIDeadlockSessionService &session_service);
  static int lock_wait_mgr_reconstruct_detector_waiting_for_trans(CollectCallBack &on_collect_op,
                                                                  const ObTransID &conflict_trans_id,
                                                                  const ObTransID &self_trans_id,
                                                                  const uint32_t sess_id,
                                                                  query::ObIDeadlockSessionService &session_service);
  static int maintain_deadlock_info_when_end_stmt(
      ObTxDesc &tx_desc,
      query::ObIDeadlockSessionService &session_service,
      const data_plane::ObStatementDeadlockContext &context);
  // for autonomous trans
  static int inner_tx_register_to_deadlock(const ObTransID parent_trans_id,
                                           const ObTransID inner_trans_id,
                                           const int64_t query_timeout);
  // if trans node on row removed(for example:1, dump trans. 2, a trans write too many row.)
  // change the dependency relationship from row to trans
  static int change_detector_waiting_obj_from_row_to_trans(const ObTransID &self_trans_id,
                                                           const ObTransID &conflict_trans_id);
  // for all path
  static void unregister_from_deadlock_detector(const ObTransID &self_trans_id, const UnregisterPath path);
  /**********************************/
  static int kill_tx(query::ObIDeadlockSessionService &session_service,
                     const uint32_t sess_id);
  static int register_or_replace_conflict_trans_ids(
                                                    query::ObIDeadlockSessionService &session_service,
                                                    const ObTransID self_tx_id,
                                                    const uint32_t self_session_id,
                                                    const ObArray<ObTransID> &conflict_tx_ids);
  static int kill_stmt(query::ObIDeadlockSessionService &session_service,
                       const uint32_t sess_id);
  static void copy_str_and_translate_apostrophe(const char *src_ptr,
                                                const int64_t src_len,
                                                char *dest_ptr,// C-style str, contain '\0'
                                                const int64_t dest_len);
private:
  static int register_to_deadlock_detector_(
                                            query::ObIDeadlockSessionService &session_service,
                                            const ObTransID self_tx_id,
                                            const uint32_t self_session_id,
                                            const ObIArray<ObTransID> &conflict_tx_ids,
                                            const query::ObDeadlockSessionFacts &session_facts);
  static int replace_conflict_trans_ids_(const ObTransID self_tx_id,
                                         const ObIArray<ObTransID> &conflict_tx_ids);
  static int create_detector_node_and_set_parent_if_needed_(CollectCallBack &on_collect_op,
                                                            const ObTransID &self_trans_id,
                                                            const uint32_t sess_id,
                                                            query::ObIDeadlockSessionService &session_service);
  static int gen_dependency_resource_array_(const ObIArray<ObTransID> &blocked_trans_ids,
                                            ObIArray<share::detector::ObDependencyResource> &dependency_resources);
};

} // namespace transaction
} // namespace oceanbase

#endif
