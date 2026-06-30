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

#include "ob_trans_rpc.h"
#include "share/rc/ob_module_provider.h"
#include "ob_trans_service.h"
#include "share/ob_ex_rpc.h"

namespace oceanbase
{

using namespace common;
using namespace transaction;
using namespace obcall;
using namespace storage;
using namespace share;

namespace obcall
{
OB_SERIALIZE_MEMBER(ObTransRpcResult, status_, send_timestamp_, private_data_);
OB_SERIALIZE_MEMBER(ObTxRpcRollbackSPResult, status_, send_timestamp_, addr_,
                    born_epoch_, ignore_, downstream_parts_, output_transfer_epoch_);

bool need_refresh_location_cache_(const int ret)
{
  return (common::OB_NOT_MASTER == ret ||
          common::OB_PARTITION_IS_BLOCKED == ret ||
          common::OB_REPLICA_NOT_READABLE == ret ||
          common::OB_LS_NOT_EXIST == ret ||
          common::OB_PARTITION_NOT_EXIST == ret ||
          common::OB_TENANT_NOT_EXIST == ret ||
          common::OB_TENANT_NOT_IN_SERVER == ret);
}

int refresh_location_cache(const share::ObLSID ls)
{
  return share::g_mp->trans_service()->refresh_location_cache(ls);
}

int handle_trans_msg_callback(const share::ObLSID &sender_ls_id,
                              const share::ObLSID &receiver_ls_id,
                              const transaction::ObTransID &tx_id,
                              const int16_t msg_type,
                              const int status,
                              const ObAddr &addr,
                              const int64_t request_id,
                              const SCN &private_data)
{
  return share::g_mp->trans_service()->handle_trans_msg_callback(sender_ls_id,
                                                  receiver_ls_id,
                                                  tx_id,
                                                  msg_type,
                                                  status,
                                                  addr,
                                                  request_id,
                                                  private_data);
}


int handle_sp_rollback_resp(const share::ObLSID &receiver_ls_id,
                            const int64_t epoch,
                            const transaction::ObTransID &tx_id,
                            const int status,
                            const int64_t request_id,
                            const ObTxRpcRollbackSPResult &result)
{
  if (result.ignore_) {
    return OB_SUCCESS;
  }
  return share::g_mp->trans_service()->handle_sp_rollback_resp(receiver_ls_id,
                                                        epoch,
                                                        tx_id,
                                                        status,
                                                        request_id,
                                                        result.born_epoch_,
                                                        result.addr_,
                                                        result.output_transfer_epoch_,
                                                        result.downstream_parts_);
}

void ObTransRpcResult::reset()
{
  status_ = OB_SUCCESS;
  send_timestamp_ = 0L;
  private_data_.reset();
}

void ObTransRpcResult::init(const int status, const int64_t timestamp)
{
  status_ = status;
  send_timestamp_ = timestamp;
}


} // obcall

namespace
{
// Local in-process dispatch of a transaction message, replacing the async RPC
// post + ObTxRPCCB completion. Runs fire-and-forget on the ASYNC_CALL worker
// pool, switches into the target tenant, invokes the same handler the ObTx*P
// processor used, then drives the sender-side callback. The local transport
// never fails, so the callback status comes from result.get_status() (exactly
// what ObTxRPCCB::process used when rcode == OB_SUCCESS). Single-replica: the
// receiver LS leader is always local, so dst == the resolved local server.
template <typename MsgType, typename Handler>
void dispatch_tx_msg_async_(const common::ObAddr &dst,
                            const MsgType &msg, Handler handler)
{
  (void)ex_rpc::async_call<void>(msg, [dst, handler](MsgType &m) {
    int ret = OB_SUCCESS;
    MOD_SCOPE {
      transaction::ObTransService *txs = share::g_mp->trans_service();
      obcall::ObTransRpcResult result;
      if (OB_ISNULL(txs)) {
        ret = OB_ERR_UNEXPECTED; TRANS_LOG(WARN, "get tx service fail", K(ret));
      } else {
        if (!m.is_valid()) {
          ret = OB_INVALID_ARGUMENT; TRANS_LOG(ERROR, "msg is invalid", K(ret), K(m));
        } else if (OB_FAIL((txs->*handler)(m, result)) && OB_TRANS_COMMITED != ret) {
          TRANS_LOG(WARN, "handle txn message fail", K(ret), "msg", m);
        }
        (void)obcall::handle_trans_msg_callback(m.get_sender(), m.get_receiver(),
                m.get_trans_id(), m.get_msg_type(), result.get_status(), dst,
                m.get_request_id(), result.private_data_);
      }
    }
  });
}

// rollback-savepoint request has its own result type and completion path
// (handle_sp_rollback_resp, which no-ops when result.ignore_ is set for the
// async-resp protocol). Otherwise identical to dispatch_tx_msg_async_.
void dispatch_rollback_sp_async_(const transaction::ObTxRollbackSPMsg &msg)
{
  (void)ex_rpc::async_call<void>(msg, [](transaction::ObTxRollbackSPMsg &m) {
    int ret = OB_SUCCESS;
    MOD_SCOPE {
      transaction::ObTransService *txs = share::g_mp->trans_service();
      obcall::ObTxRpcRollbackSPResult result;
      if (OB_ISNULL(txs)) {
        ret = OB_ERR_UNEXPECTED; TRANS_LOG(WARN, "get tx service fail", K(ret));
      } else {
        if (!m.is_valid()) {
          ret = OB_INVALID_ARGUMENT; TRANS_LOG(ERROR, "msg is invalid", K(ret), K(m));
        } else if (OB_FAIL(txs->handle_sp_rollback_request(m, result))) {
          TRANS_LOG(WARN, "handle txn message fail", K(ret), "msg", m);
        }
        (void)obcall::handle_sp_rollback_resp(m.get_receiver(), m.get_epoch(),
                m.get_trans_id(), result.get_status(), m.get_request_id(), result);
      }
    }
  });
}

// 2PC distributed messages (TX_2PC_*). The legacy transport coalesced these
// through obcall::ObBatchRpc -> ObBatchP::handle_tx_req -> handle_tx_batch_req.
// Single-replica: the receiver LS leader is always local, so we serialize the
// concrete msg (exactly as ObIFill::fill_buffer did) and dispatch it in-process
// + async into the same sink (ObTransService::handle_tx_batch_req), preserving
// the original async, fire-and-forget delivery semantics.
void dispatch_tx_2pc_async_(const transaction::ObTxMsg &msg)
{
  int ret = OB_SUCCESS;
  const int16_t msg_type = msg.get_msg_type();
  const int64_t size = msg.get_req_size();
  char *buf = NULL;
  int64_t filled = 0;
  if (size <= 0) {
    TRANS_LOG(WARN, "invalid 2pc msg size", K(size), K(msg_type));
  } else if (OB_ISNULL(buf = static_cast<char *>(ob_malloc(size, SET_USE_500("TxRpc2pc"))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    TRANS_LOG(WARN, "alloc 2pc msg buffer fail", K(ret), K(size), K(msg_type));
  } else if (OB_FAIL(msg.fill_buffer(buf, size, filled))) {
    TRANS_LOG(WARN, "serialize 2pc msg fail", K(ret), K(size), K(msg_type));
    ob_free(buf);
    buf = NULL;
  } else {
    const int32_t data_size = static_cast<int32_t>(filled);
    (void)ex_rpc::async_call([msg_type, buf, data_size]() {
      int ret = OB_SUCCESS;
      MOD_SCOPE {
        transaction::ObTransService *txs = share::g_mp->trans_service();
        if (OB_ISNULL(txs)) {
          ret = OB_ERR_UNEXPECTED; TRANS_LOG(WARN, "get tx service fail", K(ret));
        } else if (OB_FAIL(txs->handle_tx_batch_req(msg_type, buf, data_size))) {
          TRANS_LOG(WARN, "handle 2pc msg fail", K(ret), K(msg_type));
        }
      }
      ob_free(buf);
    });
  }
}
} // anonymous namespace

namespace transaction
{
int ObTransRpc::init(ObTransService *trans_service,
                     const common::ObAddr &self)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    TRANS_LOG(WARN, "ObTransRpc inited twice");
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(trans_service)
          || !self.is_valid()) {
    TRANS_LOG(WARN, "invalid argument", KP(trans_service), K(self));
    ret = OB_INVALID_ARGUMENT;
  } else {
    
    trans_service_ = trans_service;
    last_stat_ts_ = ObTimeUtility::current_time();
    is_inited_ = true;
    TRANS_LOG(INFO, "transaction rpc inited success");
  }
  return ret;
}

int ObTransRpc::start()
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    TRANS_LOG(WARN, "ObTransRpc is not inited");
    ret = OB_NOT_INIT;
  } else if (is_running_) {
    TRANS_LOG(WARN, "ObTransRpc is already running");
    ret = OB_ERR_UNEXPECTED;
  } else {
    is_running_ = true;
    TRANS_LOG(INFO, "ObTransRpc start success");
  }

  return ret;
}

void ObTransRpc::stop()
{
  if (!is_inited_) {
    TRANS_LOG_RET(WARN, OB_NOT_INIT, "ObTransRpc is not inited");
  } else if (!is_running_) {
    TRANS_LOG_RET(WARN, OB_IN_STOP_STATE, "ObTransRpc already has been stopped");
  } else {
    is_running_ = false;
    TRANS_LOG(INFO, "ObTransRpc stop success");
  }
}

void ObTransRpc::wait()
{
  if (!is_inited_) {
    TRANS_LOG_RET(WARN, OB_NOT_INIT, "ObTransRpc is not inited");
  } else if (is_running_) {
    TRANS_LOG_RET(WARN, OB_IN_STOP_STATE, "ObTransRpc is already running");
  } else {
    TRANS_LOG(INFO, "ObTransRpc wait success");
  }
}

void ObTransRpc::destroy()
{
  if (is_inited_) {
    if (is_running_) {
      stop();
      wait();
    }
    is_inited_ = false;
    trans_service_ = NULL;
    TRANS_LOG(INFO, "transaction rpc destroyed");
  }
}
int ObTransRpc::post_commit_msg_(const ObAddr &server, ObTxMsg &msg)
{
  int ret = OB_SUCCESS;
  const int64_t msg_type = msg.get_msg_type();
  
  switch (msg_type)
  {
    case TX_COMMIT:
    {
      dispatch_tx_msg_async_(server, static_cast<ObTxCommitMsg&>(msg),
                             &ObTransService::handle_trans_commit_request);
      break;
    }
    case TX_COMMIT_RESP:
    {
      dispatch_tx_msg_async_(server, static_cast<ObTxCommitRespMsg&>(msg),
                             &ObTransService::handle_trans_commit_response);
      break;
    }
    case TX_ABORT:
    {
      dispatch_tx_msg_async_(server, static_cast<ObTxAbortMsg&>(msg),
                             &ObTransService::handle_trans_abort_request);
      break;
    }
    default:
      ret = OB_NOT_SUPPORTED;
      TRANS_LOG(WARN, "rpc proxy not supported", K(server), K(msg));
      break;
  }
  return ret;
}

int ObTransRpc::post_(const ObAddr &server, ObTxMsg &msg)
{
  int ret = OB_SUCCESS;
  const int64_t msg_type = msg.get_msg_type();
  
  switch (msg_type)
  {
    case ROLLBACK_SAVEPOINT:
    {
      dispatch_rollback_sp_async_(static_cast<ObTxRollbackSPMsg &>(msg));
      break;
    }
    case ROLLBACK_SAVEPOINT_RESP:
    {
      dispatch_tx_msg_async_(server, static_cast<ObTxRollbackSPRespMsg &>(msg),
                             &ObTransService::handle_sp_rollback_response);
      break;
    }
    case KEEPALIVE:
    {
      dispatch_tx_msg_async_(server, static_cast<ObTxKeepaliveMsg &>(msg),
                             &ObTransService::handle_trans_keepalive);
      break;
    }
    case KEEPALIVE_RESP:
    {
      dispatch_tx_msg_async_(server, static_cast<ObTxKeepaliveRespMsg &>(msg),
                             &ObTransService::handle_trans_keepalive_response);
      break;
    }
    case TX_COMMIT:
    case TX_COMMIT_RESP:
    case TX_ABORT:
    {
      // Why we shoud set a new mehtod : post_commit_msg ?
      // Method stack size is overflow (max size = 10KB) because of rpc_proxy deep_copy
      ret = post_commit_msg_(server, msg);
      break;
    }
    case SUBPREPARE:
    case SUBCOMMIT:
    case SUBROLLBACK:
    {
      ret = post_sub_request_msg_(server, msg);
      break;
    }
    case SUBPREPARE_RESP:
    case SUBCOMMIT_RESP:
    case SUBROLLBACK_RESP:
    {
      ret = post_sub_response_msg_(server, msg);
      break;
    }
    case ASK_STATE:
    case ASK_STATE_RESP:
    case COLLECT_STATE:
    case COLLECT_STATE_RESP:
    {
      ret = post_standby_msg_(server, msg);
      break;
    }
    default:
      ret = OB_NOT_SUPPORTED;
      TRANS_LOG(WARN, "rpc proxy not supported", K(server), K(msg));
      break;
  }
  return ret;
}

int ObTransRpc::post_msg(const ObAddr &server, ObTxMsg &msg)
{
  int ret = OB_SUCCESS;
  

  if (OB_UNLIKELY(!is_inited_)) {
    TRANS_LOG(WARN, "ObTransRpc not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_running_)) {
    TRANS_LOG(WARN, "ObTransRpc is not running");
    ret = OB_NOT_RUNNING;
  } else if (OB_UNLIKELY(!true) ||
      OB_UNLIKELY(!server.is_valid()) || OB_UNLIKELY(!msg.is_valid())) {
    TRANS_LOG(WARN, "invalid argument", K(server), K(msg));
    ret = OB_INVALID_ARGUMENT;
  } else if (ObTxMsgTypeChecker::is_2pc_msg_type(msg.get_msg_type())) {
    dispatch_tx_2pc_async_(msg);
  } else if (OB_FAIL(post_(server, msg))) {
    TRANS_LOG(WARN, "post msg error", K(ret), K(server), K(msg));
  } else {
    // do nothing
  }

  if (OB_SUCC(ret)) {
    total_trans_msg_count_++;
    statistics_();
    TRANS_LOG(DEBUG, "post transaction message success", K(msg));
  }

  return ret;
}

int ObTransRpc::post_msg(const ObLSID &ls_id, ObTxMsg &msg)
{
  int ret = OB_SUCCESS;
#ifdef TRANS_ERROR
  const int64_t random = ObRandom::rand(1, 100);
  if (0 == random % 20) {
    //mock package drop: 5%
    TRANS_LOG(INFO, "post trans msg failed for random error (discard msg)", K(server), K(msg));
    return ret;
  } else if (0 == random % 50) {
    TRANS_LOG(INFO, "post trans msg failed for random error (delayed msg)", K(server), K(msg));
  } else {
    // do nothing
  }
#endif

  
  int64_t cluster_id = GCONF.cluster_id;
  ObAddr server;

  if (OB_UNLIKELY(!is_inited_)) {
    TRANS_LOG(WARN, "ObTransRpc not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_running_)) {
    TRANS_LOG(WARN, "ObTransRpc is not running");
    ret = OB_NOT_RUNNING;
  } else if (OB_UNLIKELY(!true) || OB_UNLIKELY(!msg.is_valid())) {
    TRANS_LOG(WARN, "invalid argument", K(msg));
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(trans_service_->get_location_adapter()->nonblock_get_leader(cluster_id, ls_id, server))) {
    TRANS_LOG(WARN, "get leader failed", KR(ret), K(msg), K(cluster_id), K(ls_id));
    if (ObTxMsgTypeChecker::is_2pc_msg_type(msg.get_msg_type())) {
      if (OB_LS_IS_DELETED == ret) {
        int tmp_ret = trans_service_->handle_ls_deleted(msg);
        if (OB_SUCCESS == tmp_ret) {
          ret = OB_SUCCESS;
        }
      }
    }
  } else if (ObTxMsgTypeChecker::is_2pc_msg_type(msg.get_msg_type())) {
    // 2pc msg: in-process async dispatch (single-replica leader is local)
    dispatch_tx_2pc_async_(msg);
  } else if (OB_FAIL(post_(server, msg))) {
    TRANS_LOG(WARN, "post msg error", K(ret), K(server), K(msg));
  } else {
    // do nothing
  }

  if (OB_SUCC(ret)) {
    total_trans_msg_count_++;
    statistics_();
    TRANS_LOG(DEBUG, "post transaction message success", K(msg));
  }

  return ret;
}

int ObTransRpc::post_sub_request_msg_(const ObAddr &server, ObTxMsg &msg)
{
  int ret = OB_SUCCESS;
  const int64_t msg_type = msg.get_msg_type();
  
  switch (msg_type) {
    case SUBPREPARE: {
      dispatch_tx_msg_async_(server, static_cast<ObTxSubPrepareMsg&>(msg),
                             &ObTransService::handle_sub_prepare_request);
      break;
    }
    case SUBCOMMIT: {
      dispatch_tx_msg_async_(server, static_cast<ObTxSubCommitMsg&>(msg),
                             &ObTransService::handle_sub_commit_request);
      break;
    }
    case SUBROLLBACK: {
      dispatch_tx_msg_async_(server, static_cast<ObTxSubRollbackMsg&>(msg),
                             &ObTransService::handle_sub_rollback_request);
      break;
    }
    default: {
      ret = OB_NOT_SUPPORTED;
      TRANS_LOG(WARN, "rpc proxy not supported", K(server), K(msg));
      break;
    }
  }
  return ret;
}

int ObTransRpc::post_sub_response_msg_(const ObAddr &server, ObTxMsg &msg)
{
  int ret = OB_SUCCESS;
  const int64_t msg_type = msg.get_msg_type();
  
  switch (msg_type) {
    case SUBPREPARE_RESP: {
      dispatch_tx_msg_async_(server, static_cast<ObTxSubPrepareRespMsg&>(msg),
                             &ObTransService::handle_sub_prepare_response);
      break;
    }
    case SUBCOMMIT_RESP: {
      dispatch_tx_msg_async_(server, static_cast<ObTxSubCommitRespMsg&>(msg),
                             &ObTransService::handle_sub_commit_response);
      break;
    }
    case SUBROLLBACK_RESP: {
      dispatch_tx_msg_async_(server, static_cast<ObTxSubRollbackRespMsg&>(msg),
                             &ObTransService::handle_sub_rollback_response);
      break;
    }
    default: {
      ret = OB_NOT_SUPPORTED;
      TRANS_LOG(WARN, "rpc proxy not supported", K(server), K(msg));
      break;
    }
  }
  return ret;
}

int ObTransRpc::ask_tx_state_for_4377(const ObAskTxStateFor4377Msg &msg,
                                      ObAskTxStateFor4377RespMsg &resp)
{
  int ret = OB_SUCCESS;

  
  int64_t cluster_id = GCONF.cluster_id;
  ObAddr server;

  if (OB_UNLIKELY(!is_inited_)) {
    TRANS_LOG(WARN, "ObTransRpc not inited");
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!is_running_)) {
    TRANS_LOG(WARN, "ObTransRpc is not running");
    ret = OB_NOT_RUNNING;
  } else if (OB_UNLIKELY(!true)
             || OB_UNLIKELY(!msg.is_valid())) {
    TRANS_LOG(WARN, "invalid argument", K(msg));
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(trans_service_->get_location_adapter()->nonblock_get_leader(cluster_id,
                                                                                 msg.ls_id_,
                                                                                 server))) {
    TRANS_LOG(WARN, "get leader failed", KR(ret), K(msg), K(cluster_id));
  } else {
    // single-replica: the target LS leader is local, dispatch in-process
    // (mirrors ObAskTxStateFor4377P::process: the handler status is carried back
    // in resp.ret_, the call itself returns the transport code OB_SUCCESS).
    ret = ex_rpc::sync_call([this, &msg, &resp]() -> int {
      int ret = OB_SUCCESS;
      bool is_alive = false;
      if (OB_FAIL(trans_service_->handle_ask_tx_state_for_4377(msg, is_alive))) {
        TRANS_LOG(WARN, "handle ask tx state for 4377 failed", K(ret), K(msg));
      }
      resp.is_alive_ = is_alive;
      resp.ret_ = ret;
      return OB_SUCCESS;
    });
    TRANS_LOG(WARN, "ask tx state for 4377 finished", KR(ret), K(msg), K(cluster_id));
  }

  return ret;
}

int ObTransRpc::post_standby_msg_(const ObAddr &server, ObTxMsg &msg)
{
  int ret = OB_SUCCESS;
  const int64_t msg_type = msg.get_msg_type();
  
  switch (msg_type) {
    case ASK_STATE: {
      dispatch_tx_msg_async_(server, static_cast<ObAskStateMsg&>(msg),
                             &ObTransService::handle_trans_ask_state);
      break;
    }
    case ASK_STATE_RESP: {
      dispatch_tx_msg_async_(server, static_cast<ObAskStateRespMsg&>(msg),
                             &ObTransService::handle_trans_ask_state_response);
      break;
    }
    case COLLECT_STATE: {
      dispatch_tx_msg_async_(server, static_cast<ObCollectStateMsg&>(msg),
                             &ObTransService::handle_trans_collect_state);
      break;
    }
    case COLLECT_STATE_RESP: {
      dispatch_tx_msg_async_(server, static_cast<ObCollectStateRespMsg&>(msg),
                             &ObTransService::handle_trans_collect_state_response);
      break;
    }
    default: {
      ret = OB_NOT_SUPPORTED;
      TRANS_LOG(WARN, "rpc proxy not supported", K(server), K(msg));
      break;
    }
  }
  return ret;
}

void ObTransRpc::statistics_()
{
  const int64_t cur_ts = ObTimeUtility::current_time();
  if (cur_ts - last_stat_ts_ > STAT_INTERVAL) {
    TRANS_LOG(INFO, "rpc statistics", K_(total_trans_msg_count));
    total_trans_msg_count_ = 0;
    last_stat_ts_ = cur_ts;
  }
}


} // transaction

} // oceanbase
