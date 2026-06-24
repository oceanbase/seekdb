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

#include <stdint.h>
#include "rpc/obmysql/ob_mysql_request_utils.h"
#include "rpc/ob_packet.h"
#include "lib/lock/ob_latch.h"
#include "rpc/obmysql/ob_packet_record.h"
#include "rpc/obmysql/ob_2_0_protocol_struct.h"

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}
namespace omt
{
class ObTenant;
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
    is_java_client_ = false;
    is_oci_client_ = false;
    is_jdbc_client_ = false;
    is_sess_alloc_ = false;
    is_sess_free_ = false;
    has_inc_active_num_ = false;
    is_need_clear_sessid_ = true;
    is_tenant_locked_ = false;
    connection_phase_ = rpc::ConnectionPhaseEnum::CPE_CONNECTED;
    sessid_ = INITIAL_SESSID;
    sess_create_time_ = 0;
    resource_group_id_ = 0;
    
    proxy_cap_flags_.capability_ = 0,
    tenant_ = NULL;
    MEMSET(tenant_name_buf_, 0, sizeof(tenant_name_buf_));
    MEMSET(user_name_buf_, 0, sizeof(user_name_buf_));
    vid_ = OB_INVALID_ID;
    MEMSET(vip_buf_, 0, sizeof(vip_buf_));
    vport_ = 0;
    connect_in_bytes_ = 0;
    ret_ = common::OB_SUCCESS;
    scramble_buf_[SCRAMBLE_BUF_SIZE] = '\0';
    group_id_ = 0;
    client_cs_type_ = 0;
    pkt_rec_wrapper_.init();
    client_type_ = common::OB_CLIENT_INVALID_TYPE;
    client_version_ = 0;
    client_sessid_ = INVALID_SESSID;
    client_addr_port_ = 0;
    client_create_time_ = 0;
    has_service_name_ = false;
    logined_ = false;
  }

  obmysql::ObCompressType get_compress_type() {
    obmysql::ObCompressType type_ret = obmysql::ObCompressType::NO_COMPRESS;
    //unauthed connection, treat it do not use compress
    //if during change user(is logined) and need compress, need return COMPRESS here
    if ((is_in_authed_phase() || (is_in_auth_switch_phase() && is_logined())) &&
        (1 == cap_flags_.cap_flags_.OB_CLIENT_COMPRESS
        || proxy_cap_flags_.is_ob_protocol_v2_compress())) {
      if (is_java_client_) {
        if (1 == proxy_cap_flags_.cap_flags_.OB_CAP_CHECKSUM) {
          type_ret = obmysql::ObCompressType::DEFAULT_CHECKSUM;
        } else {
          type_ret = obmysql::ObCompressType::DEFAULT_COMPRESS;
        }
      } else if (is_oci_client_) {
        if (1 == proxy_cap_flags_.cap_flags_.OB_CAP_CHECKSUM) {
          type_ret = obmysql::ObCompressType::DEFAULT_CHECKSUM;
        } else {
          type_ret = obmysql::ObCompressType::DEFAULT_COMPRESS;
        }
      } else if (is_jdbc_client_) {
        if (1 == proxy_cap_flags_.cap_flags_.OB_CAP_CHECKSUM) {
          type_ret = obmysql::ObCompressType::DEFAULT_CHECKSUM;
        } else {
          type_ret = obmysql::ObCompressType::DEFAULT_COMPRESS;
        }
      } else {
        type_ret = obmysql::ObCompressType::DEFAULT_COMPRESS;
      }
    }
    return type_ret;
  }

  bool need_send_extra_ok_packet() const {
    return (is_java_client_ && proxy_cap_flags_.is_extra_ok_packet_for_ocj_support());
  }

  bool is_normal_client() const {
    return (!is_java_client_ && !is_oci_client_ && !is_jdbc_client_);
  }

  bool is_driver_client() const {
    return is_oci_client_ || is_jdbc_client_;
  }

  common::ObCSProtocolType get_cs_protocol_type() const
  {
    common::ObCSProtocolType type = common::OB_INVALID_CS_TYPE;
    if (is_in_auth_switch_phase() && !is_logined()) {
      // if is change user, must is logined
      type = common::OB_MYSQL_CS_TYPE;
    } else if (proxy_cap_flags_.is_ob_protocol_v2_support()) {
      type = common::OB_2_0_CS_TYPE;
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
  inline bool is_in_ssl_connect_phase() { return rpc::ConnectionPhaseEnum::CPE_SSL_CONNECT == connection_phase_; }
  inline bool is_in_authed_phase() { return rpc::ConnectionPhaseEnum::CPE_AUTHED == connection_phase_; }
  inline bool is_in_auth_switch_phase() const { return rpc::ConnectionPhaseEnum::CPE_AUTH_SWITCH == connection_phase_; }
  inline void set_auth_switch_phase() { connection_phase_ = rpc::ConnectionPhaseEnum::CPE_AUTH_SWITCH; }
  inline void set_ssl_connect_phase() { connection_phase_ = rpc::ConnectionPhaseEnum::CPE_SSL_CONNECT; }
  inline void set_auth_phase() { connection_phase_ = rpc::ConnectionPhaseEnum::CPE_AUTHED; }
  inline void set_connect_phase() { connection_phase_ = rpc::ConnectionPhaseEnum::CPE_CONNECTED; }
  inline bool is_logined() const { return logined_; }
  inline void set_logined(bool logined) { logined_ = logined; }
public:
  obmysql::ObMySQLCapabilityFlags cap_flags_;
  bool is_java_client_;
  bool is_oci_client_;
  bool is_jdbc_client_;
  bool is_sess_alloc_;
  bool is_sess_free_;
  bool has_inc_active_num_;
  bool is_need_clear_sessid_;
  bool is_tenant_locked_;

  rpc::ConnectionPhaseEnum connection_phase_;
  uint32_t sessid_;
  uint32_t version_;
  int64_t sess_create_time_; // proxy connection mode, record the session connection time from client to proxy
  
  uint64_t resource_group_id_;
  obmysql::ObProxyCapabilityFlags proxy_cap_flags_;
  // Errors may occur during the ObSMHandler::on_connect stage, and these error messages need to be returned to the client;
  // And in on_connect, accurate error information cannot be returned to the client, therefore it is recorded here, and processed in ObMPConnect::Process
  int ret_;
  omt::ObTenant *tenant_;
  char tenant_name_buf_[OB_MAX_TENANT_NAME_LENGTH + 1];
  char user_name_buf_[OB_MAX_USER_NAME_LENGTH + 1];
  int64_t vid_;
  char vip_buf_[MAX_IP_ADDR_LENGTH];
  int32_t vport_;
  int64_t connect_in_bytes_;
  obmysql::ObMysqlPktContext mysql_pkt_context_;
  obmysql::ObCompressedPktContext compressed_pkt_context_;
  obmysql::ObProto20PktContext proto20_pkt_context_;
  char scramble_buf_[SCRAMBLE_BUF_SIZE + 1];
  int32_t group_id_;
  int32_t client_cs_type_;
  obmysql::ObPacketRecordWrapper pkt_rec_wrapper_;
  ObClientType client_type_;
  uint64_t client_version_;
  // The client establishes a connection ID to ensure that the tenant is globally unique.
  uint32_t client_sessid_;
  int32_t client_addr_port_;
  int64_t client_create_time_;
  bool has_service_name_;
private:
  bool logined_;
};
} // end of namespace observer
} // end of namespace oceanbase

#endif // OCEANBASE_OBSERVER_MYSQL_OBSM_STRUCT_H_
