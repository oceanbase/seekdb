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

#ifndef OCEANBASE_TRANSACTION_OB_TRANS_RPC_
#define OCEANBASE_TRANSACTION_OB_TRANS_RPC_

#include "lib/thread/ob_queue_thread.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/utility/utility.h"
#include "share/ob_define.h"
#include "rpc/frame/ob_result_code.h"
#include "rpc/frame/ob_req_transport.h"
#include "share/ob_rpc_struct.h"
#include "ob_trans_define.h"
#include "ob_trans_factory.h"
#include "ob_tx_msg.h"
#include "share/config/ob_server_config.h"
#include "observer/ob_server_struct.h"

namespace oceanbase
{
namespace transaction
{
class ObTransService;
class ObTxMsg;
}

namespace obcall
{
class ObTransRpcResult
{
  OB_UNIS_VERSION(1);
public:
  ObTransRpcResult()
  { reset(); }
  virtual ~ObTransRpcResult() {}

  void init(const int status, const int64_t timestamp);
  int get_status() const { return status_; }
  int64_t get_timestamp() const { return send_timestamp_; }
  void reset();
  TO_STRING_KV(K_(status), K_(send_timestamp), K_(private_data));
private:
  int status_;
  int64_t send_timestamp_;
public:
  // for ObTxCommitReqMsg, it is commit version
  share::SCN private_data_;
};

struct ObTxRpcRollbackSPResult
{
  OB_UNIS_VERSION(1);
public:
  ObTxRpcRollbackSPResult(): ignore_(false) {}
  int status_;
  int64_t send_timestamp_;
  int64_t born_epoch_;
  ObAddr addr_;
  // rollback response has changed to use ObTxRollbackSPRespMsg
  // use this field to indicate handler ignore handle by this msg
  bool ignore_;
  ObSEArray<transaction::ObTxLSEpochPair, 1> downstream_parts_;
  // used for transfer info during rollback
  int64_t output_transfer_epoch_;
public:
  int get_status() const { return status_; }
  TO_STRING_KV(K_(status), K_(send_timestamp), K_(born_epoch), K_(addr),
               K_(ignore), K_(output_transfer_epoch), K_(downstream_parts));
};

// publich method
bool need_refresh_location_cache_(const int status);
int refresh_location_cache(const share::ObLSID ls);
int handle_trans_msg_callback(const share::ObLSID &sender_ls_id,
                              const share::ObLSID &receiver_ls_id,
                              const transaction::ObTransID &tx_id,
                              const int16_t msg_type,
                              const int status,
                              const ObAddr &receiver_addr,
                              const int64_t request_id,
                              const share::SCN &private_data);

int handle_sp_rollback_resp(const share::ObLSID &receiver_ls_id,
                            const int64_t epoch,
                            const transaction::ObTransID &tx_id,
                            const int status,
                            const int64_t request_id,
                            const ObTxRpcRollbackSPResult &result);
} // obcall

namespace transaction
{

class ObITransRpc
{
public:
  ObITransRpc() {}
  virtual ~ObITransRpc() {}
  virtual int start() = 0;
  virtual void stop() = 0;
  virtual void wait() = 0;
  virtual void destroy() = 0;
public:
  virtual int post_msg(const ObAddr &server, ObTxMsg &msg) = 0;
  virtual int post_msg(const share::ObLSID &p, ObTxMsg &msg) = 0;
  virtual int ask_tx_state_for_4377(const ObAskTxStateFor4377Msg &msg,
                                    ObAskTxStateFor4377RespMsg &resp) = 0;

};

/*
 * transaction msg rpc class
 *
 * single-replica: every transaction message (including 2PC) is delivered
 * in-process and async via ex_rpc::async_call; there is no inter-server
 * transport.
 */
class ObTransRpc : public ObITransRpc
{
public:
  ObTransRpc() : is_inited_(false),
                 is_running_(false),
                 trans_service_(NULL),
                 total_trans_msg_count_(0),
                 last_stat_ts_(0) {}
  ~ObTransRpc() { destroy(); }
  int init(ObTransService *trans_service,
           const common::ObAddr &self);
  int start();
  void stop();
  void wait();
  void destroy();
public:
  int post_msg(const ObAddr &server, ObTxMsg &msg);
  int post_msg(const share::ObLSID &p, ObTxMsg &msg);
  int ask_tx_state_for_4377(const ObAskTxStateFor4377Msg &msg,
                            ObAskTxStateFor4377RespMsg &resp);
  private:
  int post_(const ObAddr &server, ObTxMsg &msg);
  int post_commit_msg_(const ObAddr &server, ObTxMsg &msg);
  int post_sub_request_msg_(const ObAddr &server, ObTxMsg &msg);
  int post_sub_response_msg_(const ObAddr &server, ObTxMsg &msg);
  int post_standby_msg_(const ObAddr &server, ObTxMsg &msg);
  void statistics_();
private:
  static const int64_t STAT_INTERVAL = 1 * 1000 * 1000;
  bool is_inited_;
  bool is_running_;
  // common info
  
  ObTransService *trans_service_;
  // statistic info
  int64_t total_trans_msg_count_ CACHE_ALIGNED;
  int64_t last_stat_ts_ CACHE_ALIGNED;
};

} // transaction

} // oceanbase

#endif // OCEANBASE_TRANSACTION_OB_TRANS_RPC_
