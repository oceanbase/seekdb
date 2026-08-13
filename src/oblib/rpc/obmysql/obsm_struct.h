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

#ifndef OCEANBASE_OBSERVER_MYSQL_OBSM_STRUCT_H_
#define OCEANBASE_OBSERVER_MYSQL_OBSM_STRUCT_H_

#include <atomic>
#include <stdint.h>
#include "rpc/obmysql/ob_mysql_packet.h"
#include "rpc/ob_packet.h"
#include "lib/lock/ob_latch.h"

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}
namespace omt
{
class ObServerRuntime;
}
namespace observer
{

struct ObSMConnection
{
public:
  static const uint32_t INITIAL_SESSID = 0;
  static const int64_t SCRAMBLE_BUF_SIZE = 20;
  
  ObSMConnection()
  {
    cap_flags_.capability_ = 0;
    autocommit_snapshot_ = 0;
    is_sess_alloc_.store(false, std::memory_order_relaxed);
    is_sess_free_.store(false, std::memory_order_relaxed);
    has_inc_active_num_ = false;
    is_need_clear_sessid_ = true;
    is_runtime_locked_ = false;
    connection_phase_ = rpc::ConnectionPhaseEnum::CPE_CONNECTED;
    sessid_ = INITIAL_SESSID;
    runtime_ = NULL;
    connect_in_bytes_ = 0;
    ret_ = common::OB_SUCCESS;
    scramble_buf_[SCRAMBLE_BUF_SIZE] = '\0';
    group_id_ = 0;
    client_cs_type_ = 0;
    client_version_ = 0;
    logined_ = false;
  }

  common::ObCSProtocolType get_cs_protocol_type() const
  {
    common::ObCSProtocolType type = common::OB_INVALID_CS_TYPE;
    if (is_in_auth_switch_phase() && !is_logined()) {
      // if is change user, must is logined
      type = common::OB_MYSQL_CS_TYPE;
    } else if (1 == cap_flags_.cap_flags_.OB_CLIENT_COMPRESS) {
      type = common::OB_MYSQL_COMPRESS_CS_TYPE;
    } else {
      type = common::OB_MYSQL_CS_TYPE;
    }
    return type;
  }

  bool is_support_plugin_auth() const {
    return (1 == cap_flags_.cap_flags_.OB_CLIENT_PLUGIN_AUTH);
  }

  inline bool is_in_connected_phase() { return rpc::ConnectionPhaseEnum::CPE_CONNECTED == connection_phase_; }
  inline bool is_in_authed_phase() { return rpc::ConnectionPhaseEnum::CPE_AUTHED == connection_phase_; }
  inline bool is_in_auth_switch_phase() const { return rpc::ConnectionPhaseEnum::CPE_AUTH_SWITCH == connection_phase_; }
  inline void set_auth_switch_phase() { connection_phase_ = rpc::ConnectionPhaseEnum::CPE_AUTH_SWITCH; }
  inline void set_auth_phase() { connection_phase_ = rpc::ConnectionPhaseEnum::CPE_AUTHED; }
  inline void set_connect_phase() { connection_phase_ = rpc::ConnectionPhaseEnum::CPE_CONNECTED; }
  inline bool is_logined() const { return logined_; }
  inline void set_logined(bool logined) { logined_ = logined; }
public:
  obmysql::ObMySQLCapabilityFlags cap_flags_;
  int64_t autocommit_snapshot_; // global value advertised by the initial handshake
  std::atomic<bool> is_sess_alloc_;
  std::atomic<bool> is_sess_free_;
  bool has_inc_active_num_;
  bool is_need_clear_sessid_;
  bool is_runtime_locked_;

  rpc::ConnectionPhaseEnum connection_phase_;
  uint32_t sessid_;
  uint32_t version_;
  int64_t sess_create_time_; // client connection creation time

  // Errors may occur during the ObSMHandler::on_connect stage, and these error messages need to be returned to the client;
  // And in on_connect, accurate error information cannot be returned to the client, therefore it is recorded here, and processed in ObMPConnect::Process
  int ret_;
  omt::ObServerRuntime *runtime_;
  int64_t connect_in_bytes_;
  char scramble_buf_[SCRAMBLE_BUF_SIZE + 1];
  int32_t group_id_;
  int32_t client_cs_type_;
  uint64_t client_version_;
private:
  bool logined_;
};
} // end of namespace observer
} // end of namespace oceanbase

#endif // OCEANBASE_OBSERVER_MYSQL_OBSM_STRUCT_H_
