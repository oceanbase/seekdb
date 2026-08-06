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

#define USING_LOG_PREFIX RPC_OBMYSQL

#include "rpc/obmysql/ob_mysql_packet.h"

using namespace oceanbase::common;
using namespace oceanbase::obmysql;

namespace oceanbase
{
namespace obmysql
{

int ObMySQLRawPacket::get_command_field(int64_t index,
                                        common::ObString &field) const {
  int ret = OB_SUCCESS;
  field.reset();
  if (index < 0 || index >= 4) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!has_command_view() || NULL == cdata_ || 0 == clen_) {
    ret = OB_STATE_NOT_MATCH;
  } else {
    const ObMySQLCommandField &command_field = command_view_.fields_[index];
    const int64_t off = command_field.off_;
    const int64_t len = command_field.len_;
    const int64_t body_len = clen_;
    if (off < 0 || len < 0 || off > body_len || len > body_len - off) {
      ret = OB_INVALID_DATA;
    } else {
      // Legacy command cdata skips the command byte. The Rust offsets are
      // relative to the complete logical body, so recover that leased base.
      field.assign_ptr(const_cast<char *>(cdata_ - 1 + off),
                       static_cast<int32_t>(len));
    }
  }
  return ret;
}

char const *get_mysql_cmd_str(ObMySQLCmd mysql_cmd)
{
  const char *str = "invalid";
  static const char *mysql_cmd_array[COM_MAX_NUM] =
  {
    "Sleep",  // COM_SLEEP,
    "Quit",  // COM_QUIT,
    "Init DB",  // COM_INIT_DB,
    "Query",  // COM_QUERY,
    "Field List",  // COM_FIELD_LIST,

    "Create DB",  // COM_CREATE_DB,
    "Drop DB",  // COM_DROP_DB,
    "Refresh",  // COM_REFRESH,
    "Shutdown",  // COM_SHUTDOWN,
    "Statistics",  // COM_STATISTICS,

    "Process Info",  // COM_PROCESS_INFO,
    "Connect",  // COM_CONNECT,
    "Process Kill",  // COM_PROCESS_KILL,
    "Debug",  // COM_DEBUG,
    "Ping",  // COM_PING,

    "Time",  // COM_TIME,
    "Delayed insert",  // COM_DELAYED_INSERT,
    "Change user",  // COM_CHANGE_USER,
    "Binlog dump",  // COM_BINLOG_DUMP,

    "Table dump",  // COM_TABLE_DUMP,
    "Connect out",  // COM_CONNECT_OUT,
    "Register slave",  // COM_REGISTER_SLAVE,

    "Prepare",  // COM_STMT_PREPARE,
    "Execute",  // COM_STMT_EXECUTE,
    "Stmt send long data",  // COM_STMT_SEND_LONG_DATA,
    "Close stmt",  // COM_STMT_CLOSE,

    "Stmt reset",  // COM_STMT_RESET,
    "Set option",  // COM_SET_OPTION,
    "Stmt fetch",  // COM_STMT_FETCH,
    "Daemno",  // COM_DAEMON,

    "Binlog dump gtid",  // COM_BINLOG_DUMP_GTID,
    "Reset connection",  // COM_RESET_CONNECTION,
    "End",  // COM_END,

    "Delete session", // COM_DELETE_SESSION
    "Handshake",  // COM_HANDSHAKE,
    "Login",  // COM_LOGIN,
    "Auth switch response",  // COM_AUTH_SWITCH_RESPONSE,
  };

  if (mysql_cmd >= COM_SLEEP && mysql_cmd <= COM_END) {
    str = mysql_cmd_array[mysql_cmd];
  } else if (mysql_cmd > COM_END && mysql_cmd <= COM_AUTH_SWITCH_RESPONSE) {
    str = mysql_cmd_array[mysql_cmd + COM_END - INTERNAL_MYSQL_CMD_START + 1];
  } else {
    // do nothing
  }
  return str;

}

} // end of namespace obmysql
} // end of namespace oceanbase
