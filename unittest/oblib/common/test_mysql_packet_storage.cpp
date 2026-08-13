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

#include <gtest/gtest.h>

#include "rpc/obmysql/ob_mysql_packet_storage.h"
#include "rpc/obmysql/ob_mysql_packet.h"

namespace oceanbase
{
namespace obmysql
{
using namespace common;
namespace
{

class TestPacketPool : public ObICSMemPool
{
public:
  explicit TestPacketPool(bool fail = false) : fail_(fail), last_alloc_size_(-1) {}
  virtual void *alloc(int64_t size) override
  {
    last_alloc_size_ = size;
    return !fail_ && size >= 0 && size <= static_cast<int64_t>(sizeof(storage_))
        ? storage_ : NULL;
  }
  int64_t last_alloc_size() const { return last_alloc_size_; }
private:
  bool fail_;
  int64_t last_alloc_size_;
  alignas(ObMySQLRawPacket) unsigned char storage_[1024];
};

TEST(TestMySQLPacketStorage, builds_legacy_packet_views)
{
  {
    TestPacketPool pool;
    // Deliberately disagree with the typed view: C++ must route exclusively by
    // Rust's parsed command metadata and never read body[0].
    char body[] = {static_cast<char>(COM_PING), 'A', 'B', '\0'};
    nio_mysql_command_view command_view = {};
    command_view.command = COM_QUERY;
    command_view.layout = NIO_MYSQL_COMMAND_LAYOUT_BYTES;
    command_view.fields[0].off = 1;
    command_view.fields[0].len = 2;
    ObMySQLRawPacket *packet = NULL;
    ASSERT_EQ(OB_SUCCESS, build_mysql_raw_packet_view(
                              pool, body, 3, 7, ObMySQLRawPacketMode::COMMAND,
                              &command_view, packet));
    ASSERT_NE(nullptr, packet);
    EXPECT_EQ(static_cast<int64_t>(sizeof(ObMySQLRawPacket)), pool.last_alloc_size());
    EXPECT_EQ(COM_QUERY, packet->get_cmd());
    EXPECT_EQ(COM_PING, static_cast<ObMySQLCmd>(static_cast<uint8_t>(body[0])));
    EXPECT_EQ(7U, packet->get_wire_bytes());
    EXPECT_EQ(3, packet->get_clen());
    EXPECT_EQ(body + 1, packet->get_cdata());
    EXPECT_EQ('\0', packet->get_cdata()[packet->get_clen() - 1]);
    EXPECT_TRUE(packet->has_command_view());
    EXPECT_EQ(ObMySQLCommandLayout::BYTES, packet->get_command_layout());
    EXPECT_EQ(0, packet->get_command_scalar0());
    EXPECT_EQ(0, packet->get_command_scalar1());
    EXPECT_EQ(0, packet->get_command_scalar2());
    ObString field;
    ASSERT_EQ(OB_SUCCESS, packet->get_command_field(0, field));
    EXPECT_EQ(ObString(2, "AB"), field);

    // The callback view is call-scoped. The packet must own an independent,
    // pointer-free copy before a worker can outlive that callback.
    command_view.layout = NIO_MYSQL_COMMAND_LAYOUT_U32;
    command_view.scalar0 = 999;
    command_view.fields[0].off = 0;
    command_view.fields[0].len = 0;
    EXPECT_EQ(ObMySQLCommandLayout::BYTES, packet->get_command_layout());
    EXPECT_EQ(0, packet->get_command_scalar0());
    ASSERT_EQ(OB_SUCCESS, packet->get_command_field(0, field));
    EXPECT_EQ(ObString(2, "AB"), field);

    ObMySQLRawPacket copied(*packet);
    EXPECT_EQ(7U, copied.get_wire_bytes());
    EXPECT_EQ(ObMySQLCommandLayout::BYTES, copied.get_command_layout());
    EXPECT_EQ(0, copied.get_command_scalar0());
    ASSERT_EQ(OB_SUCCESS, copied.get_command_field(0, field));
    EXPECT_EQ(ObString(2, "AB"), field);

    ObMySQLRawPacket assigned;
    assigned = *packet;
    EXPECT_EQ(7U, assigned.get_wire_bytes());
    EXPECT_EQ(ObMySQLCommandLayout::BYTES, assigned.get_command_layout());
    EXPECT_EQ(0, assigned.get_command_scalar1());
    assigned.reset();
    EXPECT_EQ(0U, assigned.get_wire_bytes());
    EXPECT_FALSE(assigned.has_command_view());
    EXPECT_EQ(ObMySQLCommandLayout::INVALID, assigned.get_command_layout());
    EXPECT_EQ(OB_STATE_NOT_MATCH, assigned.get_command_field(0, field));

    // ObMPInitDB relies on the raw request body being writable in place.
    const_cast<char *>(packet->get_cdata())[0] = 'a';
    EXPECT_EQ('a', body[1]);
  }
  {
    TestPacketPool pool;
    char body[] = {static_cast<char>(COM_PING), 0, 0, 0, 0, '\0'};
    nio_mysql_command_view command_view = {};
    command_view.command = COM_PROCESS_KILL;
    command_view.layout = NIO_MYSQL_COMMAND_LAYOUT_U32;
    command_view.scalar0 = UINT32_MAX;
    ObMySQLRawPacket *packet = NULL;
    ASSERT_EQ(OB_SUCCESS, build_mysql_raw_packet_view(
                              pool, body, 5, 0, ObMySQLRawPacketMode::COMMAND,
                              &command_view, packet));
    ASSERT_NE(nullptr, packet);
    EXPECT_EQ(COM_PROCESS_KILL, packet->get_cmd());
    EXPECT_EQ(static_cast<int64_t>(UINT32_MAX), packet->get_command_scalar0());
  }
  {
    TestPacketPool pool;
    char body[] = {1, 2, 3, '\0'};
    ObMySQLRawPacket *packet = NULL;
    ASSERT_EQ(OB_SUCCESS,
              build_mysql_raw_packet_view(
                  pool, body, 3, 1, ObMySQLRawPacketMode::LOGIN, NULL, packet));
    EXPECT_EQ(body, packet->get_cdata());
    EXPECT_EQ(3, packet->get_clen());
    EXPECT_FALSE(packet->has_command_view());
  }
  {
    TestPacketPool pool;
    char sentinel[] = {'\0'};
    ObMySQLRawPacket *packet = NULL;
    ASSERT_EQ(OB_SUCCESS,
              build_mysql_raw_packet_view(
                  pool, sentinel, 0, 2,
                  ObMySQLRawPacketMode::AUTH_SWITCH_RESPONSE, NULL, packet));
    EXPECT_EQ(COM_AUTH_SWITCH_RESPONSE, packet->get_cmd());
    EXPECT_EQ(sentinel, packet->get_cdata());
    EXPECT_EQ(0, packet->get_clen());
    EXPECT_EQ('\0', packet->get_cdata()[0]);
  }
  {
    TestPacketPool pool;
    char body[] = {'\0', 'x', static_cast<char>(0xff), '\0'};
    ObMySQLRawPacket *packet = NULL;
    ASSERT_EQ(OB_SUCCESS,
              build_mysql_raw_packet_view(
                  pool, body, 3, 9, ObMySQLRawPacketMode::LOCAL_INFILE_DATA,
                  NULL, packet));
    EXPECT_EQ(9U, packet->get_wire_bytes());
    // Normalized layout: cdata is the payload start and clen its true length.
    EXPECT_EQ(3, packet->get_clen());
    EXPECT_EQ(body, packet->get_cdata());
    EXPECT_EQ(static_cast<char>(0xff),
              packet->get_cdata()[packet->get_clen() - 1]);
  }
  {
    TestPacketPool pool;
    char sentinel[] = {'\0'};
    ObMySQLRawPacket *packet = NULL;
    ASSERT_EQ(OB_SUCCESS,
              build_mysql_raw_packet_view(
                  pool, sentinel, 0, 10,
                  ObMySQLRawPacketMode::LOCAL_INFILE_DATA, NULL, packet));
    EXPECT_EQ(sentinel, packet->get_cdata());
    EXPECT_EQ(0, packet->get_clen());
  }
}

TEST(TestMySQLPacketStorage, rejects_invalid_or_unallocatable_packet_view)
{
  TestPacketPool pool;
  char body[] = {'x', '\0'};
  ObMySQLRawPacket *packet = NULL;
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            build_mysql_raw_packet_view(
                pool, NULL, 0, 0, ObMySQLRawPacketMode::LOGIN, NULL, packet));
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            build_mysql_raw_packet_view(
                pool, body, 0, 0, ObMySQLRawPacketMode::COMMAND, NULL, packet));
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            build_mysql_raw_packet_view(
                pool, body, static_cast<int64_t>(UINT32_MAX) + 1, 0,
                ObMySQLRawPacketMode::LOGIN, NULL, packet));
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            build_mysql_raw_packet_view(pool, body, 1, 0,
                                        static_cast<ObMySQLRawPacketMode>(255),
                                        NULL, packet));
  EXPECT_EQ(nullptr, packet);

  TestPacketPool failing_pool(true);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
            build_mysql_raw_packet_view(failing_pool, body, 1, 0,
                                        ObMySQLRawPacketMode::LOGIN, NULL,
                                        packet));
}

TEST(TestMySQLPacketStorage, rejects_invalid_command_metadata) {
  TestPacketPool pool;
  char body[] = {static_cast<char>(COM_QUERY), 'x', '\0'};
  ObMySQLRawPacket *packet = NULL;
  nio_mysql_command_view view = {};
  view.command = COM_QUERY;
  view.layout = NIO_MYSQL_COMMAND_LAYOUT_BYTES;

  view.fields[0].off = 1;
  view.fields[0].len = 1;

  EXPECT_EQ(OB_INVALID_ARGUMENT,
            build_mysql_raw_packet_view(
                pool, body, 2, 0, ObMySQLRawPacketMode::COMMAND, NULL, packet));
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            build_mysql_raw_packet_view(
                pool, body, 2, 0, ObMySQLRawPacketMode::LOGIN, &view, packet));

  view.command = UINT8_MAX + 1U;
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            build_mysql_raw_packet_view(pool, body, 2, 0,
                                        ObMySQLRawPacketMode::COMMAND, &view,
                                        packet));
  view.command = COM_AUTH_SWITCH_RESPONSE;
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            build_mysql_raw_packet_view(pool, body, 2, 0,
                                        ObMySQLRawPacketMode::COMMAND, &view,
                                        packet));
  view.command = COM_QUERY;

  view.layout = UINT32_MAX;
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            build_mysql_raw_packet_view(pool, body, 2, 0,
                                        ObMySQLRawPacketMode::COMMAND, &view,
                                        packet));
  view.layout = NIO_MYSQL_COMMAND_LAYOUT_BYTES;

  view.fields[0].off = -1;
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            build_mysql_raw_packet_view(pool, body, 2, 0,
                                        ObMySQLRawPacketMode::COMMAND, &view,
                                        packet));
  view.fields[0].off = 0;
  view.fields[0].len = -1;
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            build_mysql_raw_packet_view(pool, body, 2, 0,
                                        ObMySQLRawPacketMode::COMMAND, &view,
                                        packet));
  view.fields[0].off = 2;
  view.fields[0].len = 1;
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            build_mysql_raw_packet_view(pool, body, 2, 0,
                                        ObMySQLRawPacketMode::COMMAND, &view,
                                        packet));

  // Layout substitution and per-layout scalar/canonical-shape checks moved to
  // Rust: command.rs owns the command -> layout tables and shapes (pinned by
  // its unit tests), and ob_nio_abi_check.cpp pins the enum values. C++ now
  // copies such views verbatim -- only memory safety is enforced here.
  view.fields[0].off = 1;
  view.fields[0].len = 1;
  view.layout = NIO_MYSQL_COMMAND_LAYOUT_U32;
  view.scalar0 = -1;
  view.scalar2 = 1;
  packet = NULL;
  EXPECT_EQ(OB_SUCCESS,
            build_mysql_raw_packet_view(pool, body, 2, 0,
                                        ObMySQLRawPacketMode::COMMAND, &view,
                                        packet));
  EXPECT_NE(nullptr, packet);
}

TEST(TestMySQLPacketStorage, copies_typed_complex_command_views) {
  {
    TestPacketPool pool;
    char body[] = {static_cast<char>(COM_CHANGE_USER),
                   'u',
                   '\0',
                   1,
                   'x',
                   'd',
                   'b',
                   '\0',
                   45,
                   0,
                   '\0'};
    nio_mysql_command_view view = {};
    view.command = COM_CHANGE_USER;
    view.layout = NIO_MYSQL_COMMAND_LAYOUT_CHANGE_USER;
    view.scalar0 = 45;
    view.scalar1 =
        NIO_MYSQL_CHANGE_USER_HAS_CHARSET | NIO_MYSQL_CHANGE_USER_SECURE_AUTH;
    view.fields[0] = {1, 1};
    view.fields[1] = {4, 1};
    view.fields[2] = {5, 2};
    ObMySQLRawPacket *packet = NULL;
    ASSERT_EQ(OB_SUCCESS, build_mysql_raw_packet_view(
                              pool, body, 10, 0, ObMySQLRawPacketMode::COMMAND,
                              &view, packet));
    ASSERT_NE(nullptr, packet);
    EXPECT_EQ(ObMySQLCommandLayout::CHANGE_USER, packet->get_command_layout());
    EXPECT_EQ(45, packet->get_command_scalar0());
    EXPECT_EQ(view.scalar1, packet->get_command_scalar1());
    ObString field;
    ASSERT_EQ(OB_SUCCESS, packet->get_command_field(1, field));
    EXPECT_EQ(ObString(1, "x"), field);

    ObMySQLRawPacket copied(*packet);
    EXPECT_EQ(ObMySQLCommandLayout::CHANGE_USER, copied.get_command_layout());
    EXPECT_EQ(view.scalar1, copied.get_command_scalar1());
  }
  {
    TestPacketPool pool;
    char body[] = {static_cast<char>(COM_STMT_EXECUTE),
                   0,
                   0,
                   0,
                   0,
                   0x19,
                   1,
                   0,
                   0,
                   0,
                   'a',
                   '\0',
                   static_cast<char>(0xff),
                   '\0'};
    nio_mysql_command_view view = {};
    view.command = COM_STMT_EXECUTE;
    view.layout = NIO_MYSQL_COMMAND_LAYOUT_EXECUTE;
    view.scalar0 = UINT32_MAX;
    view.scalar1 = 1;
    view.scalar2 = 0x19;
    view.fields[0] = {10, 3};
    ObMySQLRawPacket *packet = NULL;
    ASSERT_EQ(OB_SUCCESS, build_mysql_raw_packet_view(
                              pool, body, 13, 0, ObMySQLRawPacketMode::COMMAND,
                              &view, packet));
    ASSERT_NE(nullptr, packet);
    EXPECT_EQ(ObMySQLCommandLayout::EXECUTE, packet->get_command_layout());
    EXPECT_EQ(static_cast<int64_t>(UINT32_MAX), packet->get_command_scalar0());
    EXPECT_EQ(0x19, packet->get_command_scalar2());
    ObString tail;
    ASSERT_EQ(OB_SUCCESS, packet->get_command_field(0, tail));
    ASSERT_EQ(3, tail.length());
    EXPECT_EQ('a', tail[0]);
    EXPECT_EQ('\0', tail[1]);
    EXPECT_EQ(static_cast<char>(0xff), tail[2]);
  }
}

TEST(TestMySQLPacketStorage, copies_exact_empty_and_u8_command_views) {
  {
    TestPacketPool pool;
    char body[] = {static_cast<char>(COM_PING), '\0'};
    nio_mysql_command_view view = {};
    view.command = COM_PING;
    view.layout = NIO_MYSQL_COMMAND_LAYOUT_EMPTY;
    ObMySQLRawPacket *packet = NULL;
    ASSERT_EQ(OB_SUCCESS, build_mysql_raw_packet_view(
                              pool, body, 1, 0, ObMySQLRawPacketMode::COMMAND,
                              &view, packet));
    ASSERT_NE(nullptr, packet);
    EXPECT_EQ(ObMySQLCommandLayout::EMPTY, packet->get_command_layout());
    // (The former body_len == 1 canonical-shape rejection for EMPTY moved to
    // Rust with the rest of the shape tables.)
  }
  {
    TestPacketPool pool;
    char body[] = {static_cast<char>(COM_REFRESH), static_cast<char>(0xff),
                   '\0'};
    nio_mysql_command_view view = {};
    view.command = COM_REFRESH;
    view.layout = NIO_MYSQL_COMMAND_LAYOUT_U8;
    view.scalar0 = UINT8_MAX;
    ObMySQLRawPacket *packet = NULL;
    ASSERT_EQ(OB_SUCCESS, build_mysql_raw_packet_view(
                              pool, body, 2, 0, ObMySQLRawPacketMode::COMMAND,
                              &view, packet));
    ASSERT_NE(nullptr, packet);
    EXPECT_EQ(ObMySQLCommandLayout::U8, packet->get_command_layout());
    EXPECT_EQ(UINT8_MAX, packet->get_command_scalar0());
  }
}

TEST(TestMySQLPacketStorage,
     copies_opaque_standard_commands_and_rejects_untrusted_commands) {
  const ObMySQLCmd opaque_commands[] = {
      COM_SLEEP,       COM_CREATE_DB,    COM_DROP_DB,
      COM_SHUTDOWN,    COM_CONNECT,      COM_TIME,
      COM_DELAYED_INSERT, COM_BINLOG_DUMP, COM_TABLE_DUMP,
      COM_CONNECT_OUT, COM_REGISTER_SLAVE, COM_DAEMON,
      COM_BINLOG_DUMP_GTID};
  for (int64_t i = 0; i < ARRAYSIZEOF(opaque_commands); ++i) {
    TestPacketPool pool;
    char body[] = {static_cast<char>(opaque_commands[i]), 'x',
                   static_cast<char>(0xff), '\0'};
    nio_mysql_command_view view = {};
    view.command = opaque_commands[i];
    view.layout = NIO_MYSQL_COMMAND_LAYOUT_BYTES;
    view.fields[0] = {1, 2};
    ObMySQLRawPacket *packet = NULL;
    ASSERT_EQ(OB_SUCCESS, build_mysql_raw_packet_view(
                              pool, body, 3, 0,
                              ObMySQLRawPacketMode::COMMAND, &view, packet));
    ASSERT_NE(nullptr, packet);
    EXPECT_EQ(opaque_commands[i], packet->get_cmd());
    EXPECT_EQ(ObMySQLCommandLayout::BYTES, packet->get_command_layout());
    ObString payload;
    ASSERT_EQ(OB_SUCCESS, packet->get_command_field(0, payload));
    EXPECT_EQ(2, payload.length());
    EXPECT_EQ('x', payload[0]);
    EXPECT_EQ(static_cast<char>(0xff), payload[1]);
  }

  TestPacketPool pool;
  char body[] = {static_cast<char>(COM_PING), '\0'};
  nio_mysql_command_view view = {};
  view.layout = NIO_MYSQL_COMMAND_LAYOUT_BYTES;
  view.fields[0] = {1, 0};
  ObMySQLRawPacket *packet = NULL;
  // The command-range trust boundary stays in C++: sentinels, unassigned
  // bytes, and internal lifecycle commands must never reach dispatch. (The
  // former layout-substitution rejections for valid commands moved to Rust
  // with the layout tables.)
  const uint32_t rejected_commands[] = {COM_END, COM_DELETE_SESSION, 0x21,
                                        UINT8_MAX};
  for (int64_t i = 0; i < ARRAYSIZEOF(rejected_commands); ++i) {
    view.command = rejected_commands[i];
    EXPECT_EQ(OB_INVALID_ARGUMENT,
              build_mysql_raw_packet_view(
                  pool, body, 1, 0, ObMySQLRawPacketMode::COMMAND, &view,
                  packet));
  }
}

} // anonymous namespace
} // namespace obmysql
} // namespace oceanbase
