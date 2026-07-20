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

static const int64_t OB_MYSQL_MAX_PACKET_LENGTH = (1L << 24); //3bytes , 16M
static const int64_t OB_MYSQL_MAX_PAYLOAD_LENGTH = (OB_MYSQL_MAX_PACKET_LENGTH - 1);
// EASY_IO_BUFFER_SIZE is 16k, reserve 3k for libeasy header and request
static const int64_t OB_MULTI_RESPONSE_BUF_SIZE = (1L << 14) - 3 * 1024; //13k, for result
static const int64_t OB_ONE_RESPONSE_BUF_SIZE = (1L << 14); //16k, error/ok(proxy)

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
  PKT_MYSQL,     // 1 -> mysql packet;
  PKT_OKP,       // 2 -> okp;
  PKT_ERR,       // 3 -> error packet;
  PKT_EOF,       // 4 -> eof packet;
  PKT_ROW,       // 5 -> row packet; 
  PKT_FIELD,     // 6 -> field packet;
  PKT_PIECE,     // 7 -> piece packet;
  PKT_STR,       // 8 -> string packet;
  PKT_PREPARE,   // 9 -> prepare packet; 
  PKT_RESHEAD,   // 10 -> result header packet
  PKT_AUTH_SWITCH,// 11 -> auth switch request packet;
  PKT_FILENAME,  // 12 -> send file name to client(load local infile)
  PKT_END        // 13 -> end of packet type
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
    uint16_t OB_SERVER_QUERY_WAS_SLOW:              1;
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
  OB_SERVER_QUERY_WAS_SLOW_POS,
  OB_SERVER_PS_OUT_PARAMS_POS,
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

static const int64_t MAX_STORE_LENGTH = 9;

class ObMySQLPacketHeader
{
public:
  ObMySQLPacketHeader()
      : len_(0), seq_(0)
  { }

  void reset() {
    len_ = 0;
    seq_ = 0;
  }

  TO_STRING_KV("length", len_, "sequence", seq_);

public:
  uint32_t len_;         // MySQL packet length not include packet header
  uint8_t  seq_;         // MySQL packet sequence
};

/*
 * when use compress, packet header looks like:
 *  3B  length of compressed payload
 *  1B  compressed sequence id
 *  3B  length of payload before compression
 */
class ObMySQLCompressedPacketHeader
{
public:
  ObMySQLCompressedPacketHeader()
      : comp_len_(0), comp_seq_(0), uncomp_len(0)
  { }

  TO_STRING_KV("compressed_length", comp_len_,
               "compressed_sequence", comp_seq_,
               "length_before_compression", uncomp_len);

public:
  uint32_t comp_len_;         // length of compressed payload, not include packet header
  uint8_t  comp_seq_;         // compressed sequence id
  uint32_t uncomp_len;        // length of payload before compressio
};

class ObMySQLPacket
    : public rpc::ObPacket
{
public:
  ObMySQLPacket()
      : hdr_(), cdata_(NULL), is_packed_(false)
  {}
  virtual ~ObMySQLPacket() {}

  static int store_string_kv(char *buf, int64_t len, const ObStringKV &str, int64_t &pos);
  static uint64_t get_kv_encode_len(const ObStringKV &string_kv);
  static const char *get_mysql_cmd_name(const ObMySQLCmd &cmd);
  inline static ObStringKV get_separator_kv(); // separator for system variables and user variables

  inline void set_seq(uint8_t seq);
  inline uint8_t get_seq(void) const;
  inline void set_content(const char *content, uint32_t len);
  inline const char *get_cdata() const { return cdata_; }

  virtual int64_t get_serialize_size() const;
  int encode(char *buffer, int64_t length, int64_t &pos, int64_t &pkt_count) const;
  int encode(char *buffer, int64_t length, int64_t &pos);
  int get_pkt_len() { return hdr_.len_; }
  virtual int decode() { return common::OB_NOT_SUPPORTED; }
  virtual ObMySQLPacketType get_mysql_packet_type() { return ObMySQLPacketType::INVALID_PKT; }

  virtual void reset()
  {
    hdr_.reset();
    cdata_ = NULL;
  }

  virtual void assign(const ObMySQLPacket &other) {
    cdata_ = other.cdata_;
    hdr_.len_ = other.hdr_.len_;
    hdr_.seq_ = other.hdr_.seq_;
  }

  VIRTUAL_TO_STRING_KV("header", hdr_);

protected:

  virtual int serialize(char*, const int64_t, int64_t&) const
  {
    return common::OB_NOT_SUPPORTED;
  }

public:
  static const int64_t HANDSHAKE_RESPONSE_RESERVED_SIZE = 23;
  // 4: capability flags
  // 4: max-packet size
  // 1: character set
  static const int64_t HANDSHAKE_RESPONSE_MIN_SIZE = 9 + HANDSHAKE_RESPONSE_RESERVED_SIZE;
  // 4: capability flags
  static const int64_t JDBC_SSL_MIN_SIZE = 4;
  // 2: capability flags
  static const int64_t MIN_CAPABILITY_SIZE = 2;
  bool is_packed() const { return is_packed_; }
  void set_is_packed(const bool is_packed) { is_packed_ = is_packed; }
protected:
  ObMySQLPacketHeader hdr_;
  const char *cdata_;
  //parallel encoding of output_expr in advance to speed up packet response
  bool is_packed_;
};

class ObMySQLRawPacket
    : public ObMySQLPacket
{
public:
  ObMySQLRawPacket() : ObMySQLPacket(), cmd_(COM_MAX_NUM)
  {}

  explicit ObMySQLRawPacket(obmysql::ObMySQLCmd cmd)
    : ObMySQLPacket(), cmd_(cmd), consume_size_(0)
  {}

  virtual ~ObMySQLRawPacket() {}

  inline void set_cmd(ObMySQLCmd cmd);
  inline ObMySQLCmd get_cmd() const;

  inline const char *get_cdata() const;
  inline uint32_t get_clen() const;

  virtual int64_t get_serialize_size() const;

  void set_consume_size(int64_t consume_size) { consume_size_ = consume_size; }
  int64_t get_consume_size() const { return consume_size_; }

  virtual void reset() {
    ObMySQLPacket::reset();
    cmd_ = COM_MAX_NUM;
    consume_size_ = 0;
  }

  virtual void assign(const ObMySQLRawPacket &other)
  {
    ObMySQLPacket::assign(other);
    cmd_ = other.cmd_;
    consume_size_ = other.consume_size_;
  }

  TO_STRING_KV("header", hdr_, "consume_size", consume_size_);
protected:
  virtual int serialize(char*, const int64_t, int64_t&) const;

private:
  void set_len(uint32_t len);
private:
  ObMySQLCmd cmd_;

  // In load local scenario, we should tell the NIO to consume specific size data.
  // The size is a packet size in usually. But the mysql packet size if not equal
  // to the packet that we received if we use compress protocol.
  // NOTE: one compress packet has only one mysql packet in request message.
  int64_t consume_size_;
};

class ObMySQLCompressedPacket
    : public rpc::ObPacket
{
public:
  ObMySQLCompressedPacket()
      : hdr_(), cdata_(NULL)
  {}
  virtual ~ObMySQLCompressedPacket() {}

  inline uint32_t get_comp_len() const { return hdr_.comp_len_; }
  inline uint8_t get_comp_seq() const { return hdr_.comp_seq_; }
  inline uint32_t get_uncomp_len() const { return hdr_.uncomp_len; }
  inline const char *get_cdata() const { return cdata_; }
  inline void set_content(const char *content, const uint32_t comp_len,
                          const uint8_t comp_seq, const uint32_t uncomp_len)
  {
    cdata_ = content;
    hdr_.comp_len_ = comp_len;
    hdr_.comp_seq_ = comp_seq;
    hdr_.uncomp_len = uncomp_len;
  }

  VIRTUAL_TO_STRING_KV("header", hdr_);

protected:
  ObMySQLCompressedPacketHeader hdr_;
  const char *cdata_;
};

ObStringKV ObMySQLPacket::get_separator_kv()
{
  static ObStringKV separator_kv;
  separator_kv.key_ = common::ObString::make_string("__NULL");
  separator_kv.value_ = common::ObString::make_string("__NULL");
  return separator_kv;
}

void ObMySQLPacket::set_seq(uint8_t seq)
{
  hdr_.seq_ = seq;
}

uint8_t ObMySQLPacket::get_seq(void) const
{
  return hdr_.seq_;
}

void ObMySQLPacket::set_content(const char *content, uint32_t len)
{
  cdata_ = content;
  hdr_.len_ = len;
}

void ObMySQLRawPacket::set_cmd(ObMySQLCmd cmd)
{
  cmd_ = cmd;
}

ObMySQLCmd ObMySQLRawPacket::get_cmd() const
{
  return cmd_;
}

inline const char *ObMySQLRawPacket::get_cdata() const
{
  return cdata_;
}

inline uint32_t ObMySQLRawPacket::get_clen() const
{
  return hdr_.len_;
}

} // end of namespace obmysql
} // end of namespace oceanbase

#endif /* _OB_MYSQL_PACKET_H_ */
