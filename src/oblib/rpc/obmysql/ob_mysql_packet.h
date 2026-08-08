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

#ifndef _OB_MYSQL_PACKET_H_
#define _OB_MYSQL_PACKET_H_

#include "rpc/ob_packet.h"

namespace oceanbase
{
namespace obmysql
{

static constexpr uint8_t CURSOR_TYPE_READ_ONLY = 1;

#define INTERNAL_MYSQL_CMD_START 64

enum ObMySQLCmd
{
  COM_SLEEP,
  COM_QUIT,
  COM_INIT_DB,
  COM_QUERY,
  COM_FIELD_LIST,

  COM_CREATE_DB,
  COM_DROP_DB,
  COM_REFRESH,
  COM_SHUTDOWN,
  COM_STATISTICS,

  COM_PROCESS_INFO,
  COM_CONNECT,
  COM_PROCESS_KILL,
  COM_DEBUG,
  COM_PING,

  COM_TIME,
  COM_DELAYED_INSERT,
  COM_CHANGE_USER,
  COM_BINLOG_DUMP,

  COM_TABLE_DUMP,
  COM_CONNECT_OUT,
  COM_REGISTER_SLAVE,

  COM_STMT_PREPARE,
  COM_STMT_EXECUTE,
  COM_STMT_SEND_LONG_DATA,
  COM_STMT_CLOSE,

  COM_STMT_RESET,
  COM_SET_OPTION,
  COM_STMT_FETCH,
  COM_DAEMON,

  COM_BINLOG_DUMP_GTID,

  COM_RESET_CONNECTION,
  COM_END,


  // Internal pseudo-commands used by the SQL connection lifecycle.
  // COM_DELETE_SESSION is not a standard mysql package type. This is a package used to process delete session
  // When the connection is disconnected, the session needs to be deleted, but at this time it may not be obtained in the callback function disconnect
  // Session lock, at this time, an asynchronous task will be added to the obmysql queue
  COM_DELETE_SESSION = INTERNAL_MYSQL_CMD_START,
  // COM_HANDSHAKE and COM_LOGIN are not standard mysql package types.
  // COM_HANDSHAKE represents client---->on_connect && observer--->hand shake or error
  // COM_LOGIN represents client---->hand shake response && observer---> ok or error
  COM_HANDSHAKE,
  COM_LOGIN,
  COM_AUTH_SWITCH_RESPONSE,
  COM_MAX_NUM
};

enum class ObMySQLPacketType
{
  INVALID_PKT = 0,
  PKT_OKP = 2,         // okp;
  PKT_ERR = 3,         // error packet;
  PKT_EOF = 4,         // eof packet;
  PKT_ROW = 5,         // row packet;
  PKT_FIELD = 6,       // field packet;
  PKT_STR = 8,         // string packet;
  PKT_PREPARE = 9,     // prepare packet;
  PKT_RESHEAD = 10,    // result header packet
  PKT_AUTH_SWITCH = 11, // auth switch request packet;
  PKT_FILENAME = 12    // send file name to client(load local infile)
};

union ObServerStatusFlags
{
  ObServerStatusFlags() : flags_(0) {}
  explicit ObServerStatusFlags(uint16_t flag) : flags_(flag) {}
  uint16_t flags_;
  //ref:http://dev.mysql.com/doc/internals/en/status-flags.html
  struct ServerStatusFlags
  {
    uint16_t OB_SERVER_STATUS_IN_TRANS:             1;  // a transaction is active
    uint16_t OB_SERVER_STATUS_AUTOCOMMIT:           1;  // auto-commit is enabled
    uint16_t OB_SERVER_STATUS_RESERVED:         1;
    uint16_t OB_SERVER_MORE_RESULTS_EXISTS:         1;
    uint16_t OB_SERVER_STATUS_NO_GOOD_INDEX_USED:   1;
    uint16_t OB_SERVER_STATUS_NO_INDEX_USED:        1;
    // used by Binary Protocol Resultset to signal that
    // COM_STMT_FETCH has to be used to fetch the row-data.
    uint16_t OB_SERVER_STATUS_CURSOR_EXISTS:        1;
    uint16_t OB_SERVER_STATUS_LAST_ROW_SENT:        1;
    uint16_t OB_SERVER_STATUS_DB_DROPPED:           1;
    uint16_t OB_SERVER_STATUS_NO_BACKSLASH_ESCAPES: 1;
    uint16_t OB_SERVER_STATUS_METADATA_CHANGED:     1;
    uint16_t:                                        1;
    uint16_t OB_SERVER_PS_OUT_PARAMS:               1;
    uint16_t OB_SERVER_STATUS_IN_TRANS_READONLY:    1;  // in a read-only transaction
    uint16_t OB_SERVER_SESSION_STATE_CHANGED:       1;  // connection state information has changed
  } status_flags_;
};

union ObMySQLCapabilityFlags
{
  ObMySQLCapabilityFlags() : capability_(0) {}
  explicit ObMySQLCapabilityFlags(uint32_t cap) : capability_(cap) {}
  uint32_t capability_;
  //ref:http://dev.mysql.com/doc/internals/en/capability-flags.html
  struct CapabilityFlags
  {
    uint32_t OB_CLIENT_LONG_PASSWORD:                   1;
    uint32_t OB_CLIENT_FOUND_ROWS:                      1;
    uint32_t OB_CLIENT_LONG_FLAG:                       1;
    uint32_t OB_CLIENT_CONNECT_WITH_DB:                 1;
    uint32_t OB_CLIENT_NO_SCHEMA:                       1;
    uint32_t OB_CLIENT_COMPRESS:                        1;
    uint32_t OB_CLIENT_ODBC:                            1;
    uint32_t OB_CLIENT_LOCAL_FILES:                     1;
    uint32_t OB_CLIENT_IGNORE_SPACE:                    1;
    uint32_t OB_CLIENT_PROTOCOL_41:                     1;
    uint32_t OB_CLIENT_INTERACTIVE:                     1;
    uint32_t OB_CLIENT_SSL:                             1;
    uint32_t OB_CLIENT_IGNORE_SIGPIPE:                  1;
    uint32_t OB_CLIENT_TRANSACTIONS:                    1;
    uint32_t OB_CLIENT_RESERVED:                        1;
    uint32_t OB_CLIENT_SECURE_CONNECTION:               1;
    uint32_t OB_CLIENT_MULTI_STATEMENTS:                1;
    uint32_t OB_CLIENT_MULTI_RESULTS:                   1;
    uint32_t OB_CLIENT_PS_MULTI_RESULTS:                1;
    uint32_t OB_CLIENT_PLUGIN_AUTH:                     1;
    uint32_t OB_CLIENT_CONNECT_ATTRS:                   1;
    uint32_t OB_CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA:  1;
    uint32_t OB_CLIENT_CAN_HANDLE_EXPIRED_PASSWORDS:    1;
    uint32_t OB_CLIENT_SESSION_TRACK:                   1;
    uint32_t OB_CLIENT_DEPRECATE_EOF:                   1;
    uint32_t OB_CLIENT_RESERVED_NOT_USE:                5;
    uint32_t OB_CLIENT_SSL_VERIFY_SERVER_CERT:          1;
    uint32_t OB_CLIENT_REMEMBER_OPTIONS:                1;
  } cap_flags_;
};

enum ObClientCapabilityPos
{
  OB_CLIENT_LONG_PASSWORD_POS = 0,
  OB_CLIENT_FOUND_ROWS_POS,
  OB_CLIENT_LONG_FLAG_POS,
  OB_CLIENT_CONNECT_WITH_DB_POS,
  OB_CLIENT_NO_SCHEMA_POS,
  OB_CLIENT_COMPRESS_POS,
  OB_CLIENT_ODBC_POS,
  OB_CLIENT_LOCAL_FILES_POS,
  OB_CLIENT_IGNORE_SPACE_POS,
  OB_CLIENT_PROTOCOL_41_POS,
  OB_CLIENT_INTERACTIVE_POS,
  OB_CLIENT_SSL_POS,
  OB_CLIENT_IGNORE_SIGPIPE_POS,
  OB_CLIENT_TRANSACTION_POS,
  OB_CLIENT_RESERVED_POS,
  OB_CLIENT_SECURE_CONNECTION_POS,
  OB_CLIENT_MULTI_STATEMENTS_POS,
  OB_CLIENT_MULTI_RESULTS_POS,
  OB_CLIENT_PS_MULTI_RESULTS_POS,
  OB_CLIENT_PLUGIN_AUTH_POS,
  OB_CLIENT_CONNECT_ATTRS_POS,
  OB_CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA_POS,
  OB_CLIENT_CAN_HANDLE_EXPIRED_PASSWORDS_POS,
  OB_CLIENT_SESSION_TRACK_POS,
  OB_CLIENT_DEPRECATE_EOF_POS,
  // positions 25-29 are reserved
  OB_CLIENT_SSL_VERIFY_SERVER_CERT_POS = 30,
  OB_CLIENT_REMEMBER_OPTIONS_POS = 31,
};

enum ObServerStatusFlagsPos
{
  OB_SERVER_STATUS_IN_TRANS_POS = 0,
  OB_SERVER_STATUS_AUTOCOMMIT_POS,
  OB_SERVER_STATUS_RESERVED_POS,
  OB_SERVER_MORE_RESULTS_EXISTS_POS,
  OB_SERVER_STATUS_NO_GOOD_INDEX_USED_POS,
  OB_SERVER_STATUS_NO_INDEX_USED_POS,
  OB_SERVER_STATUS_CURSOR_EXISTS_POS,
  OB_SERVER_STATUS_LAST_ROW_SENT_POS,
  OB_SERVER_STATUS_DB_DROPPED_POS,
  OB_SERVER_STATUS_NO_BACKSLASH_ESCAPES_POS,
  OB_SERVER_STATUS_METADATA_CHANGED_POS,
  OB_SERVER_PS_OUT_PARAMS_POS = 12,
  OB_SERVER_STATUS_IN_TRANS_READONLY_POS,
  OB_SERVER_SESSION_STATE_CHANGED_POS,
};

char const *get_mysql_cmd_str(ObMySQLCmd mysql_cmd);

//http://dev.mysql.com/doc/refman/5.7/en/information-functions.html#function_current-user
enum ObInformationFunctions
{
  BENCHMARK_FUNC = 0,     // Repeatedly execute an expression
  CHARSET_FUNC,           // Return the character set of the argument
  COERCIBILITY_FUNC,      // Return the collation coercibility value of the string argument
  COLLATION_FUNC,         // Return the collation of the string argument
  CONNECTION_ID_FUNC,     // Return the connection ID (thread ID) for the connection
  CURRENT_USER_FUNC,      // The authenticated user name and host name
  DATABASE_FUNC,          // Return the default (current) database name
  FOUND_ROWS_FUNC,        // For a SELECT with a LIMIT clause, the number of rows
                          // that would be returned were there no LIMIT clause
  LAST_INSERT_ID_FUNC,    // Value of the AUTOINCREMENT column for the last INSERT
  ROW_COUNT_FUNC,         // The number of rows updated
  SCHEAM_FUNC,            // Synonym for DATABASE()
  SESSION_USER_FUNC,      // Synonym for USER()
  SYSTEM_USER_FUNC,       // Synonym for USER()
  USER_FUNC,              // The user name and host name provided by the client
  VERSION_FUNC,           // Return a string that indicates the MySQL server version
  MAX_INFO_FUNC           // end
};


template<class K, class V>
class ObCommonKV
{
public:
  ObCommonKV() : key_(), value_() {}
  void reset()
  {
    key_.reset();
    value_.reset();
  }
  K key_;
  V value_;
  TO_STRING_KV(K_(key), K_(value));
};

typedef ObCommonKV<common::ObString, common::ObString> ObStringKV;

// Pointer-free C++ sidecar for the Rust command metadata. The command itself
// stays in ObMySQLRawPacket::cmd_. The ABI-facing nio_mysql_command_view is
// copied field-by-field at the packet-storage boundary, so this core packet
// header does not depend on nio.h and never retains a Rust view pointer.
enum class ObMySQLCommandLayout : uint32_t {
  BYTES = 1,
  FIELD_LIST = 2,
  U32 = 3,
  U16 = 4,
  FETCH = 5,
  LONG_DATA = 6,
  CHANGE_USER = 7,
  EXECUTE = 8,
  EMPTY = 9,
  U8 = 10,
  INVALID = static_cast<uint32_t>(-1)
};

struct ObMySQLCommandField {
  int32_t off_;
  int32_t len_;
};

struct ObMySQLCommandView {
  ObMySQLCommandLayout layout_;
  int64_t scalar0_;
  int64_t scalar1_;
  ObMySQLCommandField fields_[4];
  int64_t scalar2_;
};

static_assert(sizeof(ObMySQLCommandField) == 8,
              "mysql command field layout changed");
static_assert(sizeof(ObMySQLCommandLayout) == sizeof(uint32_t),
              "mysql command layout width changed");

class ObMySQLPacket
    : public rpc::ObPacket
{
public:
  ObMySQLPacket() = default;
  virtual ~ObMySQLPacket() {}

  virtual ObMySQLPacketType get_mysql_packet_type() { return ObMySQLPacketType::INVALID_PKT; }

  VIRTUAL_TO_STRING_KV("packet_type", "MYSQL");
};

class ObMySQLRawPacket
    : public ObMySQLPacket
{
public:
  ObMySQLRawPacket()
      : ObMySQLPacket(), cdata_(NULL), clen_(0), wire_bytes_(0),
        cmd_(COM_MAX_NUM), command_view_() {
    reset_command_view();
  }

  ObMySQLRawPacket(const ObMySQLRawPacket &other)
      : ObMySQLPacket(), cdata_(NULL), clen_(0), wire_bytes_(0),
        cmd_(COM_MAX_NUM), command_view_() {
    assign(other);
  }

  ObMySQLRawPacket &operator=(const ObMySQLRawPacket &other) {
    if (this != &other) {
      assign(other);
    }
    return *this;
  }

  virtual ~ObMySQLRawPacket() {}

  inline void set_cmd(ObMySQLCmd cmd);
  inline ObMySQLCmd get_cmd() const;

  inline void set_content(const char *content, uint32_t len);
  inline const char *get_cdata() const;
  inline uint32_t get_clen() const;
  inline void set_wire_bytes(uint64_t wire_bytes) { wire_bytes_ = wire_bytes; }
  inline uint64_t get_wire_bytes() const { return wire_bytes_; }

  inline void set_command_view(const ObMySQLCommandView &view) {
    command_view_ = view;
  }
  inline bool has_command_view() const {
    return ObMySQLCommandLayout::INVALID != command_view_.layout_;
  }
  inline ObMySQLCommandLayout get_command_layout() const {
    return command_view_.layout_;
  }
  inline int64_t get_command_scalar0() const { return command_view_.scalar0_; }
  inline int64_t get_command_scalar1() const { return command_view_.scalar1_; }
  inline int64_t get_command_scalar2() const { return command_view_.scalar2_; }
  int get_command_field(int64_t index, common::ObString &field) const;

  virtual void reset() {
    cdata_ = NULL;
    clen_ = 0;
    wire_bytes_ = 0;
    cmd_ = COM_MAX_NUM;
    reset_command_view();
  }

  virtual void assign(const ObMySQLRawPacket &other)
  {
    cdata_ = other.cdata_;
    clen_ = other.clen_;
    wire_bytes_ = other.wire_bytes_;
    cmd_ = other.cmd_;
    command_view_ = other.command_view_;
  }

  TO_STRING_KV(K_(clen), K_(wire_bytes));

private:
  void reset_command_view() {
    command_view_.layout_ = ObMySQLCommandLayout::INVALID;
    command_view_.scalar0_ = 0;
    command_view_.scalar1_ = 0;
    for (int64_t i = 0; i < 4; ++i) {
      command_view_.fields_[i].off_ = 0;
      command_view_.fields_[i].len_ = 0;
    }
    command_view_.scalar2_ = 0;
  }

private:
  const char *cdata_;
  uint32_t clen_;
  uint64_t wire_bytes_;
  ObMySQLCmd cmd_;
  ObMySQLCommandView command_view_;
};

void ObMySQLRawPacket::set_cmd(ObMySQLCmd cmd)
{
  cmd_ = cmd;
}

ObMySQLCmd ObMySQLRawPacket::get_cmd() const
{
  return cmd_;
}

inline void ObMySQLRawPacket::set_content(const char *content, uint32_t len) {
  cdata_ = content;
  clen_ = len;
}

inline const char *ObMySQLRawPacket::get_cdata() const
{
  return cdata_;
}

inline uint32_t ObMySQLRawPacket::get_clen() const { return clen_; }
} // end of namespace obmysql
} // end of namespace oceanbase

#endif /* _OB_MYSQL_PACKET_H_ */
