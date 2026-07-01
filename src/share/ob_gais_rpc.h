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

#ifndef _OB_SHARE_OB_GAIS_RPC_H_
#define _OB_SHARE_OB_GAIS_RPC_H_

#include "config/ob_server_config.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/utility/utility.h"
#include "observer/ob_server_struct.h"
#include "share/ob_define.h"
#include "share/ob_rpc_struct.h"
#include "share/ob_gais_msg.h"
#include "rpc/frame/ob_result_code.h"

namespace oceanbase
{
namespace obcall
{
struct ObGAISNextValRpcResult
{
  ObGAISNextValRpcResult() : start_inclusive_(0), end_inclusive_(0), sync_value_(0) {}
  int init(const uint64_t start_inclusive, const uint64_t end_inclusive, const uint64_t sync_value);
  bool is_valid() const
  {
    return start_inclusive_ > 0 && end_inclusive_ > 0 && start_inclusive_ <= end_inclusive_
             && sync_value_ <= end_inclusive_;
  }
  TO_STRING_KV(K_(start_inclusive), K_(end_inclusive), K_(sync_value));

  uint64_t start_inclusive_;
  uint64_t end_inclusive_;
  uint64_t sync_value_;

  OB_UNIS_VERSION(1);
};

struct ObGAISCurrValRpcResult
{
  ObGAISCurrValRpcResult() : sequence_value_(0), sync_value_(0) {}
  int init(const uint64_t sequence_value, const uint64_t sync_value);
  bool is_valid() const
  {
    return sequence_value_ > 0 && sequence_value_ >= sync_value_;
  }
  void reset()
  {
    sequence_value_ = 0;
    sync_value_ = 0;
  }
  TO_STRING_KV(K_(sequence_value), K_(sync_value));

  uint64_t sequence_value_;
  uint64_t sync_value_;

  OB_UNIS_VERSION(1);
};

struct ObGAISNextSequenceValRpcResult
{
  ObGAISNextSequenceValRpcResult() : nextval_() {}
  TO_STRING_KV(K_(nextval));
  share::ObSequenceValue nextval_;

  OB_UNIS_VERSION(1);
};


} // obcall

namespace share
{

class ObGAISRequestRpc
{
public:
  ObGAISRequestRpc() : is_inited_(false) {}
  ~ObGAISRequestRpc() { destroy(); }
  int init(const common::ObAddr &self);
  void destroy();
public:
  /*
   * Returns the next (batch) auto-increment value of specified key,
   * and changes the current auto-increment value.
   */
  int next_autoinc_val(const common::ObAddr &server,
                       const ObGAISNextAutoIncValReq &msg,
                       obcall::ObGAISNextValRpcResult &rpc_result);

  /*
   * Returns the next sequence value of specified key,
   * and changes the current sequence value.
   */
  int next_sequence_val(const common::ObAddr &server,
                       const ObGAISNextSequenceValReq &msg,
                       obcall::ObGAISNextSequenceValRpcResult &rpc_result);
  /*
   * Returns the current auto-increment value of specified key.
   */
  int curr_autoinc_val(const common::ObAddr &server,
                       const ObGAISAutoIncKeyArg &msg,
                       obcall::ObGAISCurrValRpcResult &rpc_result);
  /*
   * Push local sync value to global auto-increment service. This function may
   * change global sync value and current auto-increment value, and return
   * updated latest sync value.
   */
  int push_autoinc_val(const common::ObAddr &server,
                       const ObGAISPushAutoIncValReq &msg,
                       uint64_t &sync_value);

  int clear_autoinc_cache(const common::ObAddr &server,
                          const ObGAISAutoIncKeyArg &msg);

  int broadcast_global_autoinc_cache(const ObGAISBroadcastAutoIncCacheReq &msg);

private:
  bool is_inited_;
  common::ObAddr self_;
};

} // share
} // oceanbase

#endif // _OB_SHARE_OB_GAIS_RPC_H_
