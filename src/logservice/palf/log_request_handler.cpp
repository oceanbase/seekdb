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

#include "log_request_handler.h"
#include "palf_handle_impl_guard.h"
#include "palf_env_impl.h"

namespace oceanbase
{
namespace palf
{

using namespace election;

LogRequestHandler::LogRequestHandler(IPalfEnvImpl *palf_env_impl) : palf_env_impl_(palf_env_impl)
{
}

LogRequestHandler::~LogRequestHandler()
{
  palf_env_impl_ = NULL;
}

template <>
int LogRequestHandler::handle_request<LogPushReq>(
    const int64_t palf_id,
    const ObAddr &server,
    const LogPushReq &req)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id) || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    IPalfHandleImplGuard guard;
    const char *buf = req.write_buf_.write_buf_[0].buf_;
    const int64_t buf_len = req.write_buf_.write_buf_[0].buf_len_;
    if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->receive_log(server,
                                                                 (PushLogType) req.push_log_type_,
                                                                 req.msg_proposal_id_,
                                                                 req.prev_lsn_,
                                                                 req.prev_log_proposal_id_,
                                                                 req.curr_lsn_,
                                                                 buf, buf_len))) {
    } else {
      PALF_LOG(TRACE, "PalfHandleImpl receive_log success", K(ret), K(palf_id),
          K(server), K(req), KPC(palf_env_impl_));
    }
  }
  return ret;
}

template <>
int LogRequestHandler::handle_request<LogPushResp>(
    const int64_t palf_id,
    const ObAddr &server,
    const LogPushResp &req)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id) || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    IPalfHandleImplGuard guard;
    if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->ack_log(server, req.msg_proposal_id_,
          req.lsn_))){
    } else {
      PALF_LOG(TRACE, "PalfHandleImpl ack_log success", K(ret), K(server), K(req), KPC(palf_env_impl_));
    }
  }
 return ret;
}

template <>
int LogRequestHandler::handle_request<NotifyRebuildReq>(
    const int64_t palf_id,
    const ObAddr &server,
    const NotifyRebuildReq &req)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id) || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    IPalfHandleImplGuard guard;
    if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->handle_notify_rebuild_req(server,
            req.base_lsn_, req.base_prev_log_info_))){
    } else {
      PALF_LOG(TRACE, "PalfHandleImpl handle_notify_rebuild_req success", K(ret), K(server), K(req), KPC(palf_env_impl_));
    }
  }
  return ret;
}

template <>
int LogRequestHandler::handle_request<NotifyFetchLogReq>(
    const int64_t palf_id,
    const ObAddr &server,
    const NotifyFetchLogReq &req)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id) || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    IPalfHandleImplGuard guard;
    if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->handle_notify_fetch_log_req(server))){
    } else {
      PALF_LOG(TRACE, "PalfHandleImpl handle_notify_fetch_log_req success", K(ret), K(server), K(req), KPC(palf_env_impl_));
    }
  }
  return ret;
}

template <>
int LogRequestHandler::handle_request<CommittedInfo>(
    const int64_t palf_id,
    const ObAddr &server,
    const CommittedInfo &req)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id) || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    IPalfHandleImplGuard guard;
    if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->handle_committed_info(server,
                                                                 req.msg_proposal_id_,
                                                                 req.prev_log_id_,
                                                                 req.prev_log_proposal_id_,
                                                                 req.committed_end_lsn_))) {
    } else {
      PALF_LOG(TRACE, "PalfHandleImpl handle_committed_info success", K(ret), K(palf_id),
          K(server), K(req), KPC(palf_env_impl_));
    }
  }
  return ret;
}

template <>
int LogRequestHandler::handle_request<LogFetchReq>(
    const int64_t palf_id,
    const ObAddr &server,
    const LogFetchReq &req)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id) || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    IPalfHandleImplGuard guard;
    if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->get_log(server, (FetchLogType) req.fetch_type_, req.msg_proposal_id_,
          req.prev_lsn_, req.lsn_, req.fetch_log_size_, req.fetch_log_count_, req.accepted_mode_pid_))) {
    } else {
      PALF_LOG(TRACE, "PalfHandleImpl get_log success", K(ret), K(server), K(req), KPC(palf_env_impl_));
    }
  }
  return ret;
}

template <>
int LogRequestHandler::handle_request<LogBatchFetchResp>(
    const int64_t palf_id,
    const ObAddr &server,
    const LogBatchFetchResp &req)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id) || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    const char *buf = req.write_buf_.write_buf_[0].buf_;
    const int64_t buf_len = req.write_buf_.write_buf_[0].buf_len_;
    IPalfHandleImplGuard guard;
    if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->receive_batch_log(server, req.msg_proposal_id_,
        req.prev_log_proposal_id_, req.prev_lsn_, req.curr_lsn_, buf, buf_len))) {
    } else {
      PALF_LOG(TRACE, "PalfHandleImpl receive_batch_log success", K(ret), K(server), K(req), KPC(palf_env_impl_));
    }
  }
  return ret;
}

template <>
int LogRequestHandler::handle_request<LogPrepareReq>(
    const int64_t palf_id,
    const ObAddr &server,
    const LogPrepareReq &req)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id) || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    IPalfHandleImplGuard guard;
    if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id,guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->handle_prepare_request(server, req.log_proposal_id_))) {
    } else {
      PALF_LOG(TRACE, "PalfHandleImpl handle_prepare_request success", K(ret), K(server), K(req), KPC(palf_env_impl_));
    }
  }
  return ret;
}

template <>
int LogRequestHandler::handle_request<LogPrepareResp>(
    const int64_t palf_id,
    const ObAddr &server,
    const LogPrepareResp &req)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id) || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    IPalfHandleImplGuard guard;
    if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->handle_prepare_response(server, req.msg_proposal_id_,
          req.vote_granted_, req.log_proposal_id_, req.max_flushed_lsn_, req.committed_end_lsn_, req.log_mode_meta_))) {
    } else {
      PALF_LOG(TRACE, "PalfHandleImpl handle_prepare_response success", K(ret), K(server),
          K(palf_id), K(req), KPC(palf_env_impl_));
    }
  }
  return ret;
}

template <>
int LogRequestHandler::handle_request<LogChangeConfigMetaReq>(
    const int64_t palf_id,
    const ObAddr &server,
    const LogChangeConfigMetaReq &req)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id)
      || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    IPalfHandleImplGuard guard;
    if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->receive_config_log(server, req.msg_proposal_id_,
          req.prev_log_proposal_id_, req.prev_lsn_, req.prev_mode_pid_, req.meta_))) {
    } else {
      PALF_LOG(TRACE, "receive_config_log success", K(ret), K(palf_id), K(server), K(req), KPC(palf_env_impl_));
    }
  }
  return ret;
}

template <>
int LogRequestHandler::handle_request<LogChangeConfigMetaResp>(
    const int64_t palf_id,
    const ObAddr &server,
    const LogChangeConfigMetaResp &req)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id) || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    IPalfHandleImplGuard guard;
    if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->ack_config_log(server, req.proposal_id_, req.config_version_))) {
    } else {
      PALF_LOG(TRACE, "ack_config_log success", K(ret), K(palf_id), K(server), K(req), KPC(palf_env_impl_));
    }
  }
  return ret;
}

template <>
int LogRequestHandler::handle_request<LogChangeModeMetaReq>(
    const int64_t palf_id,
    const ObAddr &server,
    const LogChangeModeMetaReq &req)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id)
      || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    IPalfHandleImplGuard guard;
    if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->receive_mode_meta(server, req.msg_proposal_id_,
        req.is_applied_mode_meta_, req.meta_))) {
    } else {
      PALF_LOG(INFO, "receive_mode_meta success", K(ret), K(palf_id), K(server), K(req), KPC(palf_env_impl_));
    }
  }
  return ret;
}

template <>
int LogRequestHandler::handle_request<LogChangeModeMetaResp>(
    const int64_t palf_id,
    const ObAddr &server,
    const LogChangeModeMetaResp &req)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id) || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    IPalfHandleImplGuard guard;
    if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->ack_mode_meta(server, req.msg_proposal_id_))) {
    } else {
      PALF_LOG(TRACE, "ack_mode_meta success", K(ret), K(palf_id), K(server), K(req), KPC(palf_env_impl_));
    }
  }
  return ret;
}

template<>
int LogRequestHandler::handle_request<LogLearnerReq>(
    const int64_t palf_id,
    const ObAddr &server,
    const LogLearnerReq &req)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id) || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    IPalfHandleImplGuard guard;
    if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->handle_learner_req(req.sender_, req.req_type_))) {
    } else {
      PALF_LOG(TRACE, "handle_learner_req success", K(ret), K(palf_id), K(server), K(req), KPC(palf_env_impl_));
    }
  }
  return ret;
}

template<>
int LogRequestHandler::handle_request<LogRegisterParentReq>(
    const int64_t palf_id,
    const ObAddr &server,
    const LogRegisterParentReq &req)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id) || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    IPalfHandleImplGuard guard;
    if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->handle_register_parent_req(req.child_, req.is_to_leader_))) {
    } else {
      PALF_LOG(TRACE, "handle_register_parent_req success", K(ret), K(palf_id), K(server), K(req), KPC(palf_env_impl_));
    }
  }
  return ret;
}

template<>
int LogRequestHandler::handle_request<LogRegisterParentResp>(
    const int64_t palf_id,
    const ObAddr &server,
    const LogRegisterParentResp &req)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id) || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    IPalfHandleImplGuard guard;
    if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->handle_register_parent_resp(req.parent_, req.candidate_list_, req.reg_ret_))) {
    } else {
      PALF_LOG(TRACE, "handle_register_parent_resp success", K(ret), K(palf_id), K(server), K(req), KPC(palf_env_impl_));
    }
  }
  return ret;
}

// [Election Message] handlers removed: single-replica seekdb has no election
// transport, so these request specializations are dead.

template <>
int LogRequestHandler::handle_sync_request<LogGetMCStReq, LogGetMCStResp>(
    const int64_t palf_id,
    const ObAddr &server,
    const LogGetMCStReq &req,
    LogGetMCStResp &resp)
{
  int ret = common::OB_SUCCESS;
  if (false == is_valid_palf_id(palf_id) || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else {
    IPalfHandleImplGuard guard;
    if (false == palf_env_impl_->check_disk_space_enough()) {
      resp.is_normal_replica_ = false;
      PALF_LOG(WARN, "check_disk_space_enough returns false", K(req), K(resp));
    } else if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
    } else if (OB_FAIL(guard.get_palf_handle_impl()->handle_config_change_pre_check(server, req, resp))) {
    } else {
      PALF_LOG(INFO, "PalfHandleImpl config_change_pre_check success", K(ret), K(palf_id), K(server), K(req), K(resp), KPC(palf_env_impl_));
    }
  }
  return ret;
}

template <>
int LogRequestHandler::handle_sync_request<LogGetStatReq, LogGetStatResp>(
    const int64_t palf_id,
    const ObAddr &server,
    const LogGetStatReq &req,
    LogGetStatResp &resp)
{
  int ret = common::OB_SUCCESS;
  IPalfHandleImplGuard guard;
  if (false == is_valid_palf_id(palf_id) || false == req.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "Invalid argument!!!", K(ret), K(palf_id), K(req), KPC(palf_env_impl_));
  } else if (OB_FAIL(palf_env_impl_->get_palf_handle_impl(palf_id, guard))) {
  } else if (req.get_type_ == LogGetStatType::GET_LEADER_MAX_SCN) {
    common::ObRole role = FOLLOWER;
    int64_t unused_pid;
    bool is_pending_state = true;
    if (OB_FAIL(guard.get_palf_handle_impl()->get_role(role, unused_pid, is_pending_state))) {
    } else if ((role != LEADER || true == is_pending_state)) {
      ret = OB_NOT_MASTER;
      CLOG_LOG(INFO, "i am not leader", K(ret), K(palf_id), K(req), K(role), K(is_pending_state));
    } else {
      resp.max_scn_ = guard.get_palf_handle_impl()->get_max_scn();
      resp.end_lsn_ = guard.get_palf_handle_impl()->get_end_lsn();
      CLOG_LOG(TRACE, "get_leader_max_scn success", K(ret), K(palf_id), K(server), K(req), K(resp));
    }
  }
  return ret;
}

} // end namespace palf
} // end namespace oceanbase
