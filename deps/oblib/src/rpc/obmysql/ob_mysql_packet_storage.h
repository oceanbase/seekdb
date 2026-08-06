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

#ifndef OCEANBASE_RPC_OBMYSQL_OB_MYSQL_PACKET_STORAGE_H_
#define OCEANBASE_RPC_OBMYSQL_OB_MYSQL_PACKET_STORAGE_H_

#include <new>
#include <stddef.h>
#include <stdint.h>

#include "lib/ob_errno.h"
#include "nio.h"
#include "rpc/obmysql/ob_i_cs_mem_pool.h"
#include "rpc/obmysql/ob_mysql_packet.h"

namespace oceanbase
{
namespace obmysql
{

enum class ObMySQLRawPacketMode : uint8_t
{
  LOGIN,
  COMMAND,
  AUTH_SWITCH_RESPONSE,
  LOCAL_INFILE_DATA
};

namespace detail
{
inline bool is_valid_mysql_raw_packet_mode(ObMySQLRawPacketMode mode)
{
  return ObMySQLRawPacketMode::LOGIN == mode
         || ObMySQLRawPacketMode::COMMAND == mode
         || ObMySQLRawPacketMode::AUTH_SWITCH_RESPONSE == mode
         || ObMySQLRawPacketMode::LOCAL_INFILE_DATA == mode;
}

static_assert(NIO_MYSQL_COMMAND_LAYOUT_BYTES ==
                  static_cast<uint32_t>(ObMySQLCommandLayout::BYTES),
              "mysql command bytes layout mismatch");
static_assert(NIO_MYSQL_COMMAND_LAYOUT_FIELD_LIST ==
                  static_cast<uint32_t>(ObMySQLCommandLayout::FIELD_LIST),
              "mysql command field-list layout mismatch");
static_assert(NIO_MYSQL_COMMAND_LAYOUT_U32 ==
                  static_cast<uint32_t>(ObMySQLCommandLayout::U32),
              "mysql command u32 layout mismatch");
static_assert(NIO_MYSQL_COMMAND_LAYOUT_U16 ==
                  static_cast<uint32_t>(ObMySQLCommandLayout::U16),
              "mysql command u16 layout mismatch");
static_assert(NIO_MYSQL_COMMAND_LAYOUT_FETCH ==
                  static_cast<uint32_t>(ObMySQLCommandLayout::FETCH),
              "mysql command fetch layout mismatch");
static_assert(NIO_MYSQL_COMMAND_LAYOUT_LONG_DATA ==
                  static_cast<uint32_t>(ObMySQLCommandLayout::LONG_DATA),
              "mysql command long-data layout mismatch");
static_assert(NIO_MYSQL_COMMAND_LAYOUT_CHANGE_USER ==
                  static_cast<uint32_t>(ObMySQLCommandLayout::CHANGE_USER),
              "mysql command change-user layout mismatch");
static_assert(NIO_MYSQL_COMMAND_LAYOUT_EXECUTE ==
                  static_cast<uint32_t>(ObMySQLCommandLayout::EXECUTE),
              "mysql command execute layout mismatch");
static_assert(NIO_MYSQL_COMMAND_LAYOUT_EMPTY ==
                  static_cast<uint32_t>(ObMySQLCommandLayout::EMPTY),
              "mysql command empty layout mismatch");
static_assert(NIO_MYSQL_COMMAND_LAYOUT_U8 ==
                  static_cast<uint32_t>(ObMySQLCommandLayout::U8),
              "mysql command u8 layout mismatch");
static_assert(sizeof(nio_mysql_command_field) == sizeof(ObMySQLCommandField),
              "mysql command field ABI mismatch");

inline bool is_valid_mysql_command_layout(uint32_t layout) {
  return NIO_MYSQL_COMMAND_LAYOUT_BYTES == layout ||
         NIO_MYSQL_COMMAND_LAYOUT_FIELD_LIST == layout ||
         NIO_MYSQL_COMMAND_LAYOUT_U32 == layout ||
         NIO_MYSQL_COMMAND_LAYOUT_U16 == layout ||
         NIO_MYSQL_COMMAND_LAYOUT_FETCH == layout ||
         NIO_MYSQL_COMMAND_LAYOUT_LONG_DATA == layout ||
         NIO_MYSQL_COMMAND_LAYOUT_CHANGE_USER == layout ||
         NIO_MYSQL_COMMAND_LAYOUT_EXECUTE == layout ||
         NIO_MYSQL_COMMAND_LAYOUT_EMPTY == layout ||
         NIO_MYSQL_COMMAND_LAYOUT_U8 == layout;
}

inline bool is_valid_mysql_command_field(const nio_mysql_command_field &field,
                                         int64_t body_len) {
  return field.off >= 0 && field.len >= 0 &&
         static_cast<int64_t>(field.off) <= body_len &&
         static_cast<int64_t>(field.len) <=
             body_len - static_cast<int64_t>(field.off);
}

// Trust-boundary validation only. Rust's classifier (command.rs) is the sole
// owner of the command -> layout mapping and each layout's canonical byte
// shape, pinned by its unit tests; re-deriving those tables here made the
// wire shape triple-maintained and re-parsed every packet in reverse on the
// hot path. What C++ still checks is exactly what memory safety needs: the
// command range, a known layout enum value (a cheap ABI-drift tripwire backed
// by the static_asserts in ob_nio_abi_check.cpp), and that all four field
// ranges lie inside the delivered body.
inline bool is_valid_mysql_command_view(const nio_mysql_command_view &view,
                                        int64_t body_len) {
  const bool is_internal_command =
      view.command >= INTERNAL_MYSQL_CMD_START &&
      view.command <= static_cast<uint32_t>(COM_AUTH_SWITCH_RESPONSE);
  // command < COM_END: COM_END is a sentinel and 0x21..0x3f are unassigned;
  // neither may reach dispatch from a network client.
  bool valid = view.command < static_cast<uint32_t>(COM_END) &&
               !is_internal_command && body_len >= 1 &&
               is_valid_mysql_command_layout(view.layout);
  for (int64_t i = 0; valid && i < 4; ++i) {
    valid = is_valid_mysql_command_field(view.fields[i], body_len);
  }
  return valid;
}

inline ObMySQLCommandView
copy_mysql_command_view(const nio_mysql_command_view &view) {
  ObMySQLCommandView copied = {};
  copied.layout_ = static_cast<ObMySQLCommandLayout>(view.layout);
  copied.scalar0_ = view.scalar0;
  copied.scalar1_ = view.scalar1;
  for (int64_t i = 0; i < 4; ++i) {
    copied.fields_[i].off_ = view.fields[i].off;
    copied.fields_[i].len_ = view.fields[i].len;
  }
  copied.scalar2_ = view.scalar2;
  return copied;
}

inline void init_mysql_raw_packet(ObMySQLRawPacket &packet, char *payload,
                                  uint32_t body_len, uint64_t wire_bytes,
                                  ObMySQLRawPacketMode mode,
                                  const nio_mysql_command_view *command_view) {
  packet.set_wire_bytes(wire_bytes);
  switch (mode) {
    case ObMySQLRawPacketMode::LOGIN:
      packet.set_content(payload, body_len);
      break;
    case ObMySQLRawPacketMode::COMMAND:
      packet.set_cmd(static_cast<ObMySQLCmd>(command_view->command));
      packet.set_command_view(copy_mysql_command_view(*command_view));
      // Preserve the legacy convention: cdata skips cmd, while clen still
      // includes it and therefore ends on the sentinel byte.
      packet.set_content(payload + 1, body_len);
      break;
    case ObMySQLRawPacketMode::AUTH_SWITCH_RESPONSE:
      packet.set_cmd(COM_AUTH_SWITCH_RESPONSE);
      packet.set_content(payload, body_len);
      break;
    case ObMySQLRawPacketMode::LOCAL_INFILE_DATA:
      // Normalized: get_cdata() is the start of the Rust-leased body and
      // get_clen() its true length (an empty packet marks EOF). The historical
      // "+1 so the reader's get_cdata() - 1 lands on the payload" handshake
      // is gone; ObPacketStreamFileReader::read copies from get_cdata()
      // directly.
      packet.set_content(payload, body_len);
      break;
  }
}
} // namespace detail

// Build only the C++ compatibility object over a Rust-owned writable body.
// `body` contains a sentinel at body[body_len] and remains valid until the
// request generation is committed or the synchronous packet lease is
// explicitly released.
inline int
build_mysql_raw_packet_view(ObICSMemPool &pool, char *body, int64_t body_len,
                            uint64_t wire_bytes, ObMySQLRawPacketMode mode,
                            const nio_mysql_command_view *command_view,
                            ObMySQLRawPacket *&packet) {
  int ret = common::OB_SUCCESS;
  packet = NULL;
  if (NULL == body || body_len < 0 ||
      body_len > static_cast<int64_t>(UINT32_MAX) ||
      (ObMySQLRawPacketMode::COMMAND == mode && 0 == body_len) ||
      !detail::is_valid_mysql_raw_packet_mode(mode) ||
      (ObMySQLRawPacketMode::COMMAND == mode
           ? (NULL == command_view ||
              !detail::is_valid_mysql_command_view(*command_view, body_len))
           : NULL != command_view)) {
      ret = common::OB_INVALID_ARGUMENT;
  } else if (NULL == (packet = reinterpret_cast<ObMySQLRawPacket *>(pool.alloc(
                          static_cast<int64_t>(sizeof(ObMySQLRawPacket)))))) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
  } else {
      packet = new (packet) ObMySQLRawPacket();
      detail::init_mysql_raw_packet(*packet, body,
                                    static_cast<uint32_t>(body_len), wire_bytes,
                                    mode, command_view);
  }
  return ret;
}

} // namespace obmysql
} // namespace oceanbase

#endif // OCEANBASE_RPC_OBMYSQL_OB_MYSQL_PACKET_STORAGE_H_
