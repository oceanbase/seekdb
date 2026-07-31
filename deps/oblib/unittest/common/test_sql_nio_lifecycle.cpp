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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <cstdio>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "nio.h"
#include "rpc/frame/ob_req_deliver.h"
#include "rpc/ob_request.h"
#include "rpc/ob_sql_request_operator.h"
#include "rpc/obmysql/ob_i_sm_conn_callback.h"
#include "rpc/obmysql/ob_mysql_packet.h"
#include "rpc/obmysql/ob_sql_sock_handler.h"
#include "rpc/obmysql/ob_sql_sock_session.h"

namespace
{
std::atomic<bool> g_fail_greeting(false);
std::atomic<bool> g_greeting_saw_connection_handle(false);
}

namespace oceanbase
{
namespace obmysql
{
using namespace common;
using namespace rpc;
using namespace std::chrono_literals;

namespace
{

bool write_all(int fd, const void *buf, size_t len)
{
  const char *pos = static_cast<const char *>(buf);
  while (len > 0)
  {
    const ssize_t written = ::write(fd, pos, len);
    if (written <= 0)
    {
      return false;
    }
    pos += written;
    len -= static_cast<size_t>(written);
  }
  return true;
}

bool read_all(int fd, void *buf, size_t len)
{
  char *pos = static_cast<char *>(buf);
  while (len > 0)
  {
    const ssize_t received = ::read(fd, pos, len);
    if (received <= 0)
    {
      return false;
    }
    pos += received;
    len -= static_cast<size_t>(received);
  }
  return true;
}

bool send_packet(int fd, uint8_t seq, const std::vector<uint8_t> &body)
{
  if (body.size() > 0xffffffU)
  {
    return false;
  }
  const uint8_t header[4] = {static_cast<uint8_t>(body.size()),
                             static_cast<uint8_t>(body.size() >> 8),
                             static_cast<uint8_t>(body.size() >> 16), seq};
  return write_all(fd, header, sizeof(header)) &&
         (body.empty() || write_all(fd, body.data(), body.size()));
}

bool read_packet(int fd, uint8_t &seq, std::vector<uint8_t> &body)
{
  uint8_t header[4] = {};
  if (!read_all(fd, header, sizeof(header)))
  {
    return false;
  }
  const size_t len = static_cast<size_t>(header[0]) | (static_cast<size_t>(header[1]) << 8) |
                     (static_cast<size_t>(header[2]) << 16);
  seq = header[3];
  body.resize(len);
  return len == 0 || read_all(fd, body.data(), len);
}

std::vector<uint8_t> make_login_packet()
{
  static const uint32_t CLIENT_PROTOCOL_41 = 0x00000200U;
  static const uint32_t CLIENT_SECURE_CONNECTION = 0x00008000U;
  const uint32_t capabilities = CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION;
  std::vector<uint8_t> body(4 + 4 + 1 + 23, 0);
  body[0] = static_cast<uint8_t>(capabilities);
  body[1] = static_cast<uint8_t>(capabilities >> 8);
  body[2] = static_cast<uint8_t>(capabilities >> 16);
  body[3] = static_cast<uint8_t>(capabilities >> 24);
  const uint32_t max_packet = 16U * 1024U * 1024U;
  body[4] = static_cast<uint8_t>(max_packet);
  body[5] = static_cast<uint8_t>(max_packet >> 8);
  body[6] = static_cast<uint8_t>(max_packet >> 16);
  body[7] = static_cast<uint8_t>(max_packet >> 24);
  body[8] = 45;
  const char username[] = "root";
  body.insert(body.end(), username, username + sizeof(username));
  body.push_back(0);  // empty secure-connection auth response
  return body;
}

// The loopback port the reactor under test listens on, chosen per SetUp.
uint16_t g_test_sql_port = 0;

// Bind an ephemeral loopback port, note it, and release it for nio_start.
// The gap between close() and the reactor's own bind is racy in principle;
// SetUp retries with a fresh port if nio_start loses it.
uint16_t pick_free_tcp_port()
{
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    return 0;
  }
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  uint16_t port = 0;
  socklen_t len = sizeof(addr);
  if (0 == ::bind(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) &&
      0 == ::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len))
  {
    port = ntohs(addr.sin_port);
  }
  ::close(fd);
  return port;
}

int connect_test_sql_socket()
{
  if (0 == g_test_sql_port)
  {
    return -1;
  }
  for (int attempt = 0; attempt < 200; ++attempt)
  {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
      return -1;
    }
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(g_test_sql_port);
    if (0 == ::connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)))
    {
      const timeval timeout = {2, 0};
      if (0 != ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
          0 != ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)))
      {
        ::close(fd);
        return -1;
      }
      return fd;
    }
    ::close(fd);
    std::this_thread::sleep_for(5ms);
  }
  return -1;
}

class FailingPacketPool : public ObICSMemPool
{
 public:
  void *alloc(int64_t) override { return NULL; }
};

class TestConnectionCallback : public ObISMConnectionCallback
{
 public:
  TestConnectionCallback() : initialized_(0), disconnected_(0), destroyed_(0), latest_session_(NULL)
  {
  }

  int init(ObSqlSockSession &session, observer::ObSMConnection &conn) override
  {
    conn.sessid_ = 42;
    conn.autocommit_snapshot_ = 1;
    for (int64_t i = 0; i < observer::ObSMConnection::SCRAMBLE_BUF_SIZE; ++i)
    {
      conn.scramble_buf_[i] = static_cast<char>('a' + i % 26);
    }
    conn.scramble_buf_[observer::ObSMConnection::SCRAMBLE_BUF_SIZE] = '\0';
    {
      std::lock_guard<std::mutex> guard(lock_);
      latest_session_ = &session;
      initialized_.fetch_add(1, std::memory_order_relaxed);
      cv_.notify_all();
    }
    return OB_SUCCESS;
  }

  void destroy(observer::ObSMConnection &) override
  {
    destroyed_.fetch_add(1, std::memory_order_relaxed);
    notify();
  }

  int on_disconnect(observer::ObSMConnection &) override
  {
    disconnected_.fetch_add(1, std::memory_order_relaxed);
    notify();
    return OB_SUCCESS;
  }

  bool wait_initialized(int expected, std::chrono::milliseconds timeout = 2s)
  {
    return wait_for(initialized_, expected, timeout);
  }
  bool wait_disconnected(int expected, std::chrono::milliseconds timeout = 2s)
  {
    return wait_for(disconnected_, expected, timeout);
  }
  bool wait_destroyed(int expected, std::chrono::milliseconds timeout = 2s)
  {
    return wait_for(destroyed_, expected, timeout);
  }
  int destroyed() const { return destroyed_.load(std::memory_order_relaxed); }

  ObSqlSockSession *latest_session() const
  {
    std::lock_guard<std::mutex> guard(lock_);
    return latest_session_;
  }

 private:
  bool wait_for(const std::atomic<int> &counter, int expected, std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> guard(lock_);
    return cv_.wait_for(guard, timeout,
                        [&] { return counter.load(std::memory_order_relaxed) >= expected; });
  }

  void notify()
  {
    std::lock_guard<std::mutex> guard(lock_);
    cv_.notify_all();
  }

 private:
  std::atomic<int> initialized_;
  std::atomic<int> disconnected_;
  std::atomic<int> destroyed_;
  mutable std::mutex lock_;
  std::condition_variable cv_;
  ObSqlSockSession *latest_session_;
};

class TestNioCallbackContext
{
 public:
  explicit TestNioCallbackContext(ObSqlSockHandler &handler) : handler_(handler), close_returns_(0)
  {
  }

  ObSqlSockHandler &handler() { return handler_; }

  void notify_close_returned()
  {
    std::lock_guard<std::mutex> guard(lock_);
    ++close_returns_;
    cv_.notify_all();
  }

  bool wait_close_returned(int expected, std::chrono::milliseconds timeout = 2s)
  {
    std::unique_lock<std::mutex> guard(lock_);
    return cv_.wait_for(guard, timeout, [&] { return close_returns_ >= expected; });
  }

 private:
  ObSqlSockHandler &handler_;
  int close_returns_;
  std::mutex lock_;
  std::condition_variable cv_;
};

struct DeliveredPacket
{
  ConnectionPhaseEnum phase;
  ObMySQLCmd command;
  uint64_t wire_bytes;
  uint64_t generation;
  std::string payload;
};

class TestDeliver : public frame::ObReqDeliver
{
 public:
  TestDeliver()
      : fail_on_(0),
        hold_on_(0),
        bind_login_(false),
        completed_deliveries_(0),
        latest_session_(NULL),
        held_request_(NULL),
        held_session_(NULL),
        held_generation_(0)
  {
  }

  int init() override { return OB_SUCCESS; }
  void stop() override {}

  void fail_on(int delivery) { fail_on_.store(delivery, std::memory_order_relaxed); }
  void hold_on(int delivery) { hold_on_.store(delivery, std::memory_order_relaxed); }
  void bind_sql_session_on_login() { bind_login_.store(true, std::memory_order_relaxed); }

  int deliver(ObRequest &req) override
  {
    ObSqlSockSession *session = req.get_server_handle_context();
    const ObMySQLRawPacket &packet = reinterpret_cast<const ObMySQLRawPacket &>(req.get_packet());
    DeliveredPacket delivered = {session->conn_.connection_phase_,
                                 packet.get_cmd(), packet.get_wire_bytes(),
                                 req.get_nio_request_generation(),
                                 std::string()};
    if (session->conn_.is_in_connected_phase())
    {
      delivered.payload.assign(packet.get_cdata(), packet.get_clen());
    }
    else if (packet.get_clen() > 1)
    {
      delivered.payload.assign(packet.get_cdata(), packet.get_clen() - 1);
    }

    int delivery = 0;
    {
      std::lock_guard<std::mutex> guard(lock_);
      latest_session_ = session;
      packets_.push_back(delivered);
      delivery = static_cast<int>(packets_.size());
    }

    int ret = OB_SUCCESS;
    if (delivery == fail_on_.load(std::memory_order_relaxed))
    {
      ret = OB_ERR_UNEXPECTED;
    }
    else
    {
      if (session->conn_.is_in_connected_phase())
      {
        if (bind_login_.load(std::memory_order_relaxed))
        {
          session->bind_sql_session();
        }
        session->conn_.set_auth_phase();
      }
      if (delivery == hold_on_.load(std::memory_order_relaxed))
      {
        std::lock_guard<std::mutex> guard(lock_);
        held_request_ = &req;
        held_session_ = session;
        held_generation_ = delivered.generation;
      } else if (0 != nio_response_flush(
                          session->get_nio_connection_handle(),
                          delivered.generation, 1)) {
        ret = OB_IO_ERROR;
      } else if (OB_FAIL(
                     session->prepare_request_commit(delivered.generation))) {
      } else {
        session->commit_request(delivered.generation);
      }
    }
    {
      std::lock_guard<std::mutex> guard(lock_);
      ++completed_deliveries_;
      cv_.notify_all();
    }
    return ret;
  }

  bool wait_deliveries(size_t expected, std::chrono::milliseconds timeout = 2s)
  {
    std::unique_lock<std::mutex> guard(lock_);
    return cv_.wait_for(guard, timeout, [&] { return completed_deliveries_ >= expected; });
  }

  std::vector<DeliveredPacket> packets() const
  {
    std::lock_guard<std::mutex> guard(lock_);
    return packets_;
  }

  int finish_held()
  {
    ObSqlSockSession *session = NULL;
    uint64_t generation = 0;
    {
      std::lock_guard<std::mutex> guard(lock_);
      session = held_session_;
      generation = held_generation_;
    }
    int ret = OB_NOT_INIT;
    if (NULL != session && 0 != generation)
    {
      // Mirror ObMPPacketSender::flush_buffer(true): a peer-close may reject
      // publication, but the current owner must still prepare/commit so Rust
      // can release REQUEST_BUSY and retire the connection.
      (void)nio_response_flush(session->get_nio_connection_handle(),
                               generation, 1);
      ret = session->prepare_request_commit(generation);
      if (OB_SUCC(ret))
      {
        {
          std::lock_guard<std::mutex> guard(lock_);
          if (held_session_ == session && held_generation_ == generation)
          {
            held_request_ = NULL;
            held_session_ = NULL;
            held_generation_ = 0;
          }
        }
        session->commit_request(generation);
      }
    }
    return ret;
  }

  ObRequest *held_request() const
  {
    std::lock_guard<std::mutex> guard(lock_);
    return held_request_;
  }

  ObSqlSockSession *latest_session() const
  {
    std::lock_guard<std::mutex> guard(lock_);
    return latest_session_;
  }

 private:
  std::atomic<int> fail_on_;
  std::atomic<int> hold_on_;
  std::atomic<bool> bind_login_;
  mutable std::mutex lock_;
  std::condition_variable cv_;
  std::vector<DeliveredPacket> packets_;
  size_t completed_deliveries_;
  ObSqlSockSession *latest_session_;
  ObRequest *held_request_;
  ObSqlSockSession *held_session_;
  uint64_t held_generation_;
};

int on_connect(void *ctx, void *session, int fd, int is_unix,
               nio_greeting_info *greeting)
{
  int ret = static_cast<TestNioCallbackContext *>(ctx)->handler().on_connect(session, fd,
                                                                            0 != is_unix);
  if (0 != ret)
  {
    return ret;
  }
  // Greeting inputs now travel forward through this callback (ABI 22). The
  // failure injection and the handle-visibility probe that used to live in the
  // reverse-FFI symbol moved here with them.
  ObSqlSockSession *sock_session = static_cast<ObSqlSockSession *>(session);
  g_greeting_saw_connection_handle.store(
      NULL != sock_session->get_nio_connection_handle(), std::memory_order_relaxed);
  if (g_fail_greeting.load(std::memory_order_relaxed))
  {
    return -1;
  }
  const observer::ObSMConnection &conn = sock_session->conn_;
  greeting->sessid = conn.sessid_;
  std::memcpy(greeting->scramble, conn.scramble_buf_, sizeof(greeting->scramble));
  static const char SERVER_VERSION[] = "5.7.25";
  greeting->version_len = static_cast<int64_t>(sizeof(SERVER_VERSION) - 1);
  std::memcpy(greeting->version, SERVER_VERSION,
              static_cast<size_t>(greeting->version_len));
  ObServerStatusFlags status_flags;
  status_flags.status_flags_.OB_SERVER_STATUS_AUTOCOMMIT =
      0 != conn.autocommit_snapshot_;
  greeting->status_flags = status_flags.flags_;
  return 0;
}

int on_readable(void *ctx, void *session, char *body, int64_t body_len,
                uint64_t wire_bytes, int packet_kind,
                const nio_mysql_command_view *command_view,
                uint64_t generation) {
  return static_cast<TestNioCallbackContext *>(ctx)->handler().on_readable(
      session, body, body_len, wire_bytes, packet_kind, command_view,
      generation);
}

void on_disconnect(void *, void *session)
{
  static_cast<ObSqlSockSession *>(session)->on_disconnect();
}

void on_close(void *ctx, void *session, int err)
{
  TestNioCallbackContext *callback_ctx = static_cast<TestNioCallbackContext *>(ctx);
  callback_ctx->handler().on_close(session, err);
  callback_ctx->notify_close_returned();
}

class TestSqlNioLifecycle : public ::testing::Test
{
 protected:
  TestSqlNioLifecycle()
      : handler_(connection_callback_),
        callback_context_(handler_),
        reactor_(NULL),
        client_fd_(-1),
        bound_session_(NULL)
  {
  }

  void SetUp() override
  {
    g_fail_greeting.store(false, std::memory_order_relaxed);
    g_greeting_saw_connection_handle.store(false,
                                            std::memory_order_relaxed);
    char cwd[1024] = {};
    ASSERT_NE(nullptr, ::getcwd(cwd, sizeof(cwd)));
    old_cwd_ = cwd;
    std::error_code error;
    const std::filesystem::path temp_base = std::filesystem::temp_directory_path(error);
    ASSERT_FALSE(error);
    std::string dir_template = (temp_base / "sql-nio-lifecycle-XXXXXX").string();
    std::vector<char> dir_template_buf(dir_template.begin(), dir_template.end());
    dir_template_buf.push_back('\0');
    char *dir = ::mkdtemp(dir_template_buf.data());
    ASSERT_NE(nullptr, dir);
    temp_dir_ = dir;
    ASSERT_EQ(0, ::chdir(temp_dir_.c_str()));

    ASSERT_EQ(OB_SUCCESS, handler_.init(&deliver_));
    nio_callbacks callbacks = {};
    callbacks.ctx = &callback_context_;
    callbacks.on_connect = on_connect;
    callbacks.on_readable = on_readable;
    callbacks.on_disconnect = on_disconnect;
    callbacks.on_close = on_close;
    for (int attempt = 0; NULL == reactor_ && attempt < 3; ++attempt)
    {
      const uint16_t port = pick_free_tcp_port();
      ASSERT_NE(0, port);
      g_test_sql_port = port;
      char addr[32] = {};
      std::snprintf(addr, sizeof(addr), "127.0.0.1:%u", port);
      int32_t start_err = NIO_START_OK;
      reactor_ = nio_start(addr, NIO_ABI_VERSION, &callbacks, sizeof(callbacks),
                           sizeof(ObSqlSockSession), 1, NULL, 0, &start_err, 0);
      if (NULL == reactor_)
      {
        // A lost port race is the only expected retryable cause here.
        ASSERT_EQ(NIO_START_EIO, start_err);
      }
    }
    ASSERT_NE(nullptr, reactor_);
  }

  void TearDown() override
  {
    g_fail_greeting.store(false, std::memory_order_relaxed);
    // A fatal assertion in a hold-on-delivery test must not strand Rust's
    // REQUEST_BUSY gate and hang nio_wait_destroy until the ctest timeout.
    (void)deliver_.finish_held();
    if (NULL != bound_session_)
    {
      (void)nio_release_sql_session(bound_session_);
      bound_session_ = NULL;
    }
    if (client_fd_ >= 0)
    {
      ::close(client_fd_);
      client_fd_ = -1;
    }
    if (NULL != reactor_)
    {
      nio_stop(reactor_);
      nio_wait_destroy(reactor_);
      reactor_ = NULL;
    }
    if (!old_cwd_.empty())
    {
      (void)::chdir(old_cwd_.c_str());
    }
    if (!temp_dir_.empty())
    {
      std::error_code error;
      std::filesystem::remove_all(temp_dir_, error);
    }
  }

  void connect_and_read_greeting(int expected_initialized = 1)
  {
    client_fd_ = connect_test_sql_socket();
    ASSERT_GE(client_fd_, 0);
    uint8_t seq = 0xff;
    std::vector<uint8_t> greeting;
    ASSERT_TRUE(read_packet(client_fd_, seq, greeting));
    ASSERT_EQ(0, seq);
    ASSERT_FALSE(greeting.empty());
    const size_t version_end =
        static_cast<size_t>(std::find(greeting.begin() + 1, greeting.end(), 0) -
                            greeting.begin());
    ASSERT_LT(version_end + 18, greeting.size());
    EXPECT_EQ(0x02, greeting[version_end + 17]);
    EXPECT_EQ(0x00, greeting[version_end + 18]);
    ASSERT_TRUE(connection_callback_.wait_initialized(expected_initialized));
  }

  void login()
  {
    ASSERT_TRUE(send_packet(client_fd_, 1, make_login_packet()));
    ASSERT_TRUE(deliver_.wait_deliveries(1));
  }

 protected:
  TestConnectionCallback connection_callback_;
  TestDeliver deliver_;
  ObSqlSockHandler handler_;
  TestNioCallbackContext callback_context_;
  nio_reactor *reactor_;
  int client_fd_;
  void *bound_session_;
  std::string old_cwd_;
  std::string temp_dir_;
};

TEST_F(TestSqlNioLifecycle, login_and_command_cross_the_rust_cpp_boundary)
{
  connect_and_read_greeting();
  login();

  const std::string sql = "select 1";
  std::vector<uint8_t> command(1, static_cast<uint8_t>(COM_QUERY));
  command.insert(command.end(), sql.begin(), sql.end());
  ASSERT_TRUE(send_packet(client_fd_, 0, command));
  ASSERT_TRUE(deliver_.wait_deliveries(2));

  const std::vector<DeliveredPacket> packets = deliver_.packets();
  ASSERT_EQ(2U, packets.size());
  EXPECT_EQ(ConnectionPhaseEnum::CPE_CONNECTED, packets[0].phase);
  EXPECT_EQ(packets[0].payload.size() + 4, packets[0].wire_bytes);
  EXPECT_GT(packets[0].generation, 0U);
  EXPECT_EQ(ConnectionPhaseEnum::CPE_AUTHED, packets[1].phase);
  EXPECT_EQ(COM_QUERY, packets[1].command);
  EXPECT_EQ(sql.size() + 5, packets[1].wire_bytes);
  EXPECT_EQ(sql, packets[1].payload);
  EXPECT_GT(packets[1].generation, packets[0].generation);

  ::close(client_fd_);
  client_fd_ = -1;
  EXPECT_TRUE(connection_callback_.wait_disconnected(1));
  EXPECT_TRUE(connection_callback_.wait_destroyed(1));
}

TEST_F(TestSqlNioLifecycle, held_request_owns_a_pointer_free_command_sidecar) {
  deliver_.hold_on(2);
  connect_and_read_greeting();
  login();

  const std::string sql = "select repeat('x', 8)";
  std::vector<uint8_t> command(1, static_cast<uint8_t>(COM_QUERY));
  command.insert(command.end(), sql.begin(), sql.end());
  ASSERT_TRUE(send_packet(client_fd_, 0, command));
  ASSERT_TRUE(deliver_.wait_deliveries(2));

  // TestDeliver has returned while the request remains worker-owned. The Rust
  // callback view was call-scoped, so only the packet's copied scalar/offset
  // metadata may be consulted here.
  ObRequest *request = deliver_.held_request();
  ASSERT_NE(nullptr, request);
  const ObMySQLRawPacket &packet =
      reinterpret_cast<const ObMySQLRawPacket &>(request->get_packet());
  EXPECT_TRUE(packet.has_command_view());
  EXPECT_EQ(ObMySQLCommandLayout::BYTES, packet.get_command_layout());
  EXPECT_EQ(0, packet.get_command_scalar0());
  EXPECT_EQ(0, packet.get_command_scalar1());
  ObString sql_field;
  ASSERT_EQ(OB_SUCCESS, packet.get_command_field(0, sql_field));
  EXPECT_EQ(ObString(static_cast<int32_t>(sql.length()), sql.data()),
            sql_field);

  ASSERT_EQ(OB_SUCCESS, deliver_.finish_held());
  ::close(client_fd_);
  client_fd_ = -1;
  EXPECT_TRUE(connection_callback_.wait_disconnected(1));
  EXPECT_TRUE(connection_callback_.wait_destroyed(1));
}

TEST_F(TestSqlNioLifecycle, typed_commands_cross_the_real_rust_cpp_bridge) {
  connect_and_read_greeting();
  login();

  auto send_and_hold =
      [&](int delivery,
          const std::vector<uint8_t> &body) -> const ObMySQLRawPacket * {
    deliver_.hold_on(delivery);
    if (!send_packet(client_fd_, 0, body)) {
      ADD_FAILURE() << "failed to send delivery " << delivery;
      return nullptr;
    }
    if (!deliver_.wait_deliveries(static_cast<size_t>(delivery))) {
      ADD_FAILURE() << "delivery timed out: " << delivery;
      return nullptr;
    }
    ObRequest *request = deliver_.held_request();
    if (nullptr == request) {
      ADD_FAILURE() << "delivery was not held: " << delivery;
      return nullptr;
    }
    return &reinterpret_cast<const ObMySQLRawPacket &>(request->get_packet());
  };
  auto expect_field = [](const ObMySQLRawPacket &packet, int64_t index,
                         const char *expected, int32_t expected_len) {
    ObString actual;
    ASSERT_EQ(OB_SUCCESS, packet.get_command_field(index, actual));
    ASSERT_EQ(expected_len, actual.length());
    if (expected_len > 0) {
      EXPECT_EQ(0, std::memcmp(actual.ptr(), expected,
                               static_cast<size_t>(expected_len)));
    }
  };
  auto expect_empty_fields = [](const ObMySQLRawPacket &packet, int64_t first) {
    for (int64_t index = first; index < 4; ++index) {
      ObString actual;
      ASSERT_EQ(OB_SUCCESS, packet.get_command_field(index, actual));
      EXPECT_EQ(0, actual.length());
    }
  };

  const std::vector<uint8_t> change_user = {
      static_cast<uint8_t>(COM_CHANGE_USER),
      'n',
      'e',
      'w',
      0,
      3,
      0,
      0xff,
      'x',
      'd',
      'b',
      0,
      45,
      0};
  const ObMySQLRawPacket *packet = send_and_hold(2, change_user);
  ASSERT_NE(nullptr, packet);
  EXPECT_TRUE(packet->has_command_view());
  EXPECT_EQ(COM_CHANGE_USER, packet->get_cmd());
  EXPECT_EQ(change_user.size() + 4, packet->get_wire_bytes());
  EXPECT_EQ(ObMySQLCommandLayout::CHANGE_USER, packet->get_command_layout());
  EXPECT_EQ(45, packet->get_command_scalar0());
  EXPECT_EQ(static_cast<int64_t>(NIO_MYSQL_CHANGE_USER_HAS_CHARSET |
                                 NIO_MYSQL_CHANGE_USER_SECURE_AUTH),
            packet->get_command_scalar1());
  EXPECT_EQ(0, packet->get_command_scalar2());
  expect_field(*packet, 0, "new", 3);
  const char binary_auth[] = {0, static_cast<char>(0xff), 'x'};
  expect_field(*packet, 1, binary_auth,
               static_cast<int32_t>(sizeof(binary_auth)));
  expect_field(*packet, 2, "db", 2);
  expect_empty_fields(*packet, 3);
  ASSERT_EQ(OB_SUCCESS, deliver_.finish_held());

  const std::vector<uint8_t> execute = {static_cast<uint8_t>(COM_STMT_EXECUTE),
                                        0xff,
                                        0xff,
                                        0xff,
                                        0xff,
                                        0x19,
                                        0x01,
                                        0,
                                        0,
                                        0x80,
                                        0,
                                        0xff,
                                        'x'};
  packet = send_and_hold(3, execute);
  ASSERT_NE(nullptr, packet);
  EXPECT_TRUE(packet->has_command_view());
  EXPECT_EQ(COM_STMT_EXECUTE, packet->get_cmd());
  EXPECT_EQ(execute.size() + 4, packet->get_wire_bytes());
  EXPECT_EQ(ObMySQLCommandLayout::EXECUTE, packet->get_command_layout());
  EXPECT_EQ(static_cast<int64_t>(UINT32_MAX), packet->get_command_scalar0());
  EXPECT_EQ(static_cast<int64_t>(0x80000001U), packet->get_command_scalar1());
  EXPECT_EQ(0x19, packet->get_command_scalar2());
  const char binary_tail[] = {0, static_cast<char>(0xff), 'x'};
  expect_field(*packet, 0, binary_tail,
               static_cast<int32_t>(sizeof(binary_tail)));
  expect_empty_fields(*packet, 1);
  ASSERT_EQ(OB_SUCCESS, deliver_.finish_held());

  packet = send_and_hold(4, {static_cast<uint8_t>(COM_PING)});
  ASSERT_NE(nullptr, packet);
  EXPECT_TRUE(packet->has_command_view());
  EXPECT_EQ(COM_PING, packet->get_cmd());
  EXPECT_EQ(5U, packet->get_wire_bytes());
  EXPECT_EQ(ObMySQLCommandLayout::EMPTY, packet->get_command_layout());
  EXPECT_EQ(0, packet->get_command_scalar0());
  EXPECT_EQ(0, packet->get_command_scalar1());
  EXPECT_EQ(0, packet->get_command_scalar2());
  expect_empty_fields(*packet, 0);
  ASSERT_EQ(OB_SUCCESS, deliver_.finish_held());

  packet = send_and_hold(5, {static_cast<uint8_t>(COM_REFRESH), 0xff});
  ASSERT_NE(nullptr, packet);
  EXPECT_TRUE(packet->has_command_view());
  EXPECT_EQ(COM_REFRESH, packet->get_cmd());
  EXPECT_EQ(6U, packet->get_wire_bytes());
  EXPECT_EQ(ObMySQLCommandLayout::U8, packet->get_command_layout());
  EXPECT_EQ(static_cast<int64_t>(UINT8_MAX), packet->get_command_scalar0());
  EXPECT_EQ(0, packet->get_command_scalar1());
  EXPECT_EQ(0, packet->get_command_scalar2());
  expect_empty_fields(*packet, 0);
  ASSERT_EQ(OB_SUCCESS, deliver_.finish_held());

  ::close(client_fd_);
  client_fd_ = -1;
  EXPECT_TRUE(connection_callback_.wait_disconnected(1));
  EXPECT_TRUE(connection_callback_.wait_destroyed(1));
}

TEST_F(TestSqlNioLifecycle, cpp_delivery_failure_closes_the_connection_once)
{
  deliver_.fail_on(1);
  connect_and_read_greeting();
  ASSERT_TRUE(send_packet(client_fd_, 1, make_login_packet()));
  ASSERT_TRUE(deliver_.wait_deliveries(1));
  EXPECT_TRUE(connection_callback_.wait_disconnected(1));
  EXPECT_TRUE(connection_callback_.wait_destroyed(1));
  EXPECT_EQ(1, connection_callback_.destroyed());
}

TEST_F(TestSqlNioLifecycle,
       admission_failure_after_handle_acquire_unwinds_once)
{
  g_fail_greeting.store(true, std::memory_order_relaxed);
  client_fd_ = connect_test_sql_socket();
  ASSERT_GE(client_fd_, 0);
  uint8_t seq = 0xff;
  std::vector<uint8_t> greeting;
  EXPECT_FALSE(read_packet(client_fd_, seq, greeting));
  EXPECT_TRUE(connection_callback_.wait_initialized(1));
  EXPECT_TRUE(g_greeting_saw_connection_handle.load(
      std::memory_order_relaxed));
  EXPECT_TRUE(connection_callback_.wait_disconnected(1));
  EXPECT_TRUE(connection_callback_.wait_destroyed(1));
  EXPECT_TRUE(callback_context_.wait_close_returned(1));
  EXPECT_EQ(1, connection_callback_.destroyed());

  ::close(client_fd_);
  client_fd_ = -1;
  g_fail_greeting.store(false, std::memory_order_relaxed);

  // The failed admission released its connection-scoped handle and did not
  // poison the reactor; a later connection can complete normal admission.
  connect_and_read_greeting(2);
  ::close(client_fd_);
  client_fd_ = -1;
  EXPECT_TRUE(connection_callback_.wait_disconnected(2));
  EXPECT_TRUE(connection_callback_.wait_destroyed(2));
  EXPECT_TRUE(callback_context_.wait_close_returned(2));
  EXPECT_EQ(2, connection_callback_.destroyed());
}

TEST_F(TestSqlNioLifecycle, bound_sql_session_releases_retired_rust_storage)
{
  deliver_.bind_sql_session_on_login();
  connect_and_read_greeting();
  login();
  ObSqlSockSession *session = deliver_.latest_session();
  ASSERT_NE(nullptr, session);
  bound_session_ = session;

  ::close(client_fd_);
  client_fd_ = -1;
  ASSERT_TRUE(connection_callback_.wait_disconnected(1));
  ASSERT_TRUE(connection_callback_.wait_destroyed(1));
  ASSERT_TRUE(callback_context_.wait_close_returned(1));
  EXPECT_EQ(nullptr, session->get_nio_connection_handle());

  // on_close's C callback returns before Rust executes retire_or_release().
  // A second on_connect on the same reactor is therefore the serialization
  // barrier proving retirement of the first connection has completed.
  connect_and_read_greeting(2);
  ASSERT_NE(session, connection_callback_.latest_session());

  // Production ObSQLSessionInfo invokes this after on_close. Calling through
  // the retained C++ session proves Rust kept its backing storage alive.
  ASSERT_EQ(0, session->clear_sql_session_info());
  bound_session_ = NULL;

  ::close(client_fd_);
  client_fd_ = -1;
  EXPECT_TRUE(connection_callback_.wait_disconnected(2));
  EXPECT_TRUE(connection_callback_.wait_destroyed(2));
  EXPECT_TRUE(callback_context_.wait_close_returned(2));
}

TEST_F(TestSqlNioLifecycle, peer_close_waits_for_the_held_cpp_request)
{
  deliver_.hold_on(2);
  connect_and_read_greeting();
  login();

  const std::vector<uint8_t> command = {static_cast<uint8_t>(COM_PING)};
  ASSERT_TRUE(send_packet(client_fd_, 0, command));
  ASSERT_TRUE(deliver_.wait_deliveries(2));

  ::close(client_fd_);
  client_fd_ = -1;
  ASSERT_TRUE(connection_callback_.wait_disconnected(1));
  EXPECT_FALSE(connection_callback_.wait_destroyed(1, 100ms));

  ASSERT_EQ(OB_SUCCESS, deliver_.finish_held());
  EXPECT_TRUE(connection_callback_.wait_destroyed(1));
  EXPECT_EQ(1, connection_callback_.destroyed());
}

TEST_F(TestSqlNioLifecycle,
       reactor_stop_waits_for_the_held_cpp_request)
{
  deliver_.hold_on(2);
  connect_and_read_greeting();
  login();

  const std::vector<uint8_t> command = {static_cast<uint8_t>(COM_PING)};
  ASSERT_TRUE(send_packet(client_fd_, 0, command));
  ASSERT_TRUE(deliver_.wait_deliveries(2));
  ObSqlSockSession *session = deliver_.latest_session();
  ASSERT_NE(nullptr, session);
  ASSERT_NE(nullptr, session->get_nio_connection_handle());

  nio_stop(reactor_);
  ASSERT_TRUE(connection_callback_.wait_disconnected(1));
  EXPECT_FALSE(connection_callback_.wait_destroyed(1, 100ms));

  ASSERT_EQ(OB_SUCCESS, deliver_.finish_held());
  EXPECT_TRUE(connection_callback_.wait_destroyed(1));
  EXPECT_TRUE(callback_context_.wait_close_returned(1));
  EXPECT_EQ(1, connection_callback_.destroyed());

  nio_wait_destroy(reactor_);
  reactor_ = NULL;
}

TEST_F(TestSqlNioLifecycle, mid_request_packet_uses_an_exact_cpp_lease)
{
  deliver_.hold_on(2);
  connect_and_read_greeting();
  login();

  const std::vector<uint8_t> command = {static_cast<uint8_t>(COM_PING)};
  ASSERT_TRUE(send_packet(client_fd_, 0, command));
  ASSERT_TRUE(deliver_.wait_deliveries(2));
  ObRequest *request = deliver_.held_request();
  ASSERT_NE(nullptr, request);

  const uint64_t generation = request->get_nio_request_generation();
  ASSERT_NE(0U, generation);

  const std::vector<uint8_t> file_data = {'a', 'b', 'c'};
  std::atomic<bool> write_ok(false);
  std::thread writer(
      [&]
      {
        std::this_thread::sleep_for(20ms);
        write_ok.store(send_packet(client_fd_, 1, file_data), std::memory_order_relaxed);
      });
  ObPacket *packet = NULL;
  uint64_t packet_lease = 0;
  const auto started = std::chrono::steady_clock::now();
  const int wait_ret = SQL_REQ_OP.wait_packet(
      request, request->get_server_handle_context()->pool_, generation,
      1000000, packet, packet_lease);
  writer.join();
  const auto elapsed = std::chrono::steady_clock::now() - started;
  ASSERT_TRUE(write_ok.load(std::memory_order_relaxed));
  ASSERT_EQ(OB_SUCCESS, wait_ret);
  EXPECT_LT(elapsed, 500ms);
  ASSERT_NE(nullptr, packet);
  ASSERT_NE(0U, packet_lease);
  const ObMySQLRawPacket &raw = reinterpret_cast<const ObMySQLRawPacket &>(*packet);
  EXPECT_EQ(file_data.size(), static_cast<size_t>(raw.get_clen()));
  // Normalized LOCAL_INFILE layout: cdata is the payload itself.
  EXPECT_EQ(0, std::memcmp(raw.get_cdata(), file_data.data(), file_data.size()));

  // A stale generation is rejected the same way a stale read handle was, and
  // leaves the live lease intact.
  EXPECT_EQ(OB_STATE_NOT_MATCH,
            SQL_REQ_OP.release_read_packet(request, generation - 1, packet_lease));
  EXPECT_EQ(OB_SUCCESS, SQL_REQ_OP.release_read_packet(request, generation, packet_lease));
  EXPECT_EQ(OB_STATE_NOT_MATCH, SQL_REQ_OP.release_read_packet(request, generation, packet_lease));
  ASSERT_EQ(OB_SUCCESS, deliver_.finish_held());

  ::close(client_fd_);
  client_fd_ = -1;
  EXPECT_TRUE(connection_callback_.wait_disconnected(1));
  EXPECT_TRUE(connection_callback_.wait_destroyed(1));
}

TEST_F(TestSqlNioLifecycle, packet_view_allocation_failure_releases_the_rust_lease)
{
  deliver_.hold_on(2);
  connect_and_read_greeting();
  login();

  const std::vector<uint8_t> command = {static_cast<uint8_t>(COM_PING)};
  ASSERT_TRUE(send_packet(client_fd_, 0, command));
  ASSERT_TRUE(deliver_.wait_deliveries(2));
  ObRequest *request = deliver_.held_request();
  ASSERT_NE(nullptr, request);

  const uint64_t generation = request->get_nio_request_generation();
  ASSERT_NE(0U, generation);

  ASSERT_TRUE(send_packet(client_fd_, 1, {'x'}));
  FailingPacketPool failing_pool;
  ObPacket *packet = NULL;
  uint64_t packet_lease = 0;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
            SQL_REQ_OP.wait_packet(request, failing_pool, generation, 1000000,
                                   packet, packet_lease));
  EXPECT_EQ(nullptr, packet);
  EXPECT_EQ(0U, packet_lease);

  // A second packet can be leased only if the failed C++ materialization
  // released the first Rust body exactly once.
  ASSERT_TRUE(send_packet(client_fd_, 2, {'y'}));
  ASSERT_EQ(OB_SUCCESS,
            SQL_REQ_OP.wait_packet(request,
                                   request->get_server_handle_context()->pool_,
                                   generation, 1000000, packet, packet_lease));
  ASSERT_NE(nullptr, packet);
  ASSERT_NE(0U, packet_lease);
  EXPECT_EQ(OB_SUCCESS, SQL_REQ_OP.release_read_packet(request, generation, packet_lease));
  ASSERT_EQ(OB_SUCCESS, deliver_.finish_held());

  ::close(client_fd_);
  client_fd_ = -1;
  EXPECT_TRUE(connection_callback_.wait_disconnected(1));
  EXPECT_TRUE(connection_callback_.wait_destroyed(1));
}

TEST_F(TestSqlNioLifecycle, mid_request_wait_is_interruptible_without_a_packet)
{
  deliver_.hold_on(2);
  connect_and_read_greeting();
  login();

  const std::vector<uint8_t> command = {static_cast<uint8_t>(COM_PING)};
  ASSERT_TRUE(send_packet(client_fd_, 0, command));
  ASSERT_TRUE(deliver_.wait_deliveries(2));
  ObRequest *request = deliver_.held_request();
  ASSERT_NE(nullptr, request);

  const uint64_t generation = request->get_nio_request_generation();
  ASSERT_NE(0U, generation);

  ObSqlSockDesc socket_desc;
  SQL_REQ_OP.get_sock_desc(request, socket_desc);
  std::thread interrupter(
      [&]
      {
        std::this_thread::sleep_for(20ms);
        SQL_REQ_OP.interrupt_read_by_sql_sock_desc(socket_desc);
      });
  ObPacket *packet = NULL;
  uint64_t packet_lease = 0;
  const auto started = std::chrono::steady_clock::now();
  const int wait_ret = SQL_REQ_OP.wait_packet(
      request, request->get_server_handle_context()->pool_, generation,
      1000000, packet, packet_lease);
  interrupter.join();
  const auto elapsed = std::chrono::steady_clock::now() - started;

  EXPECT_EQ(OB_SUCCESS, wait_ret);
  EXPECT_LT(elapsed, 500ms);
  EXPECT_EQ(nullptr, packet);
  EXPECT_EQ(0U, packet_lease);
  ASSERT_EQ(OB_SUCCESS, deliver_.finish_held());

  ::close(client_fd_);
  client_fd_ = -1;
  EXPECT_TRUE(connection_callback_.wait_disconnected(1));
  EXPECT_TRUE(connection_callback_.wait_destroyed(1));
}

TEST_F(TestSqlNioLifecycle, mid_request_wait_times_out_without_polling)
{
  deliver_.hold_on(2);
  connect_and_read_greeting();
  login();

  const std::vector<uint8_t> command = {static_cast<uint8_t>(COM_PING)};
  ASSERT_TRUE(send_packet(client_fd_, 0, command));
  ASSERT_TRUE(deliver_.wait_deliveries(2));
  ObRequest *request = deliver_.held_request();
  ASSERT_NE(nullptr, request);

  const uint64_t generation = request->get_nio_request_generation();
  ASSERT_NE(0U, generation);
  ObPacket *packet = NULL;
  uint64_t packet_lease = 0;
  const auto started = std::chrono::steady_clock::now();
  EXPECT_EQ(OB_SUCCESS,
            SQL_REQ_OP.wait_packet(request,
                                   request->get_server_handle_context()->pool_,
                                   generation, 20000, packet, packet_lease));
  const auto elapsed = std::chrono::steady_clock::now() - started;
  EXPECT_GE(elapsed, 10ms);
  EXPECT_EQ(nullptr, packet);
  EXPECT_EQ(0U, packet_lease);
  ASSERT_EQ(OB_SUCCESS, deliver_.finish_held());

  ::close(client_fd_);
  client_fd_ = -1;
  EXPECT_TRUE(connection_callback_.wait_disconnected(1));
  EXPECT_TRUE(connection_callback_.wait_destroyed(1));
}

TEST_F(TestSqlNioLifecycle, login_view_is_owned_by_commit_not_by_consumers)
{
  deliver_.hold_on(1);
  connect_and_read_greeting();
  ASSERT_TRUE(send_packet(client_fd_, 1, make_login_packet()));
  ASSERT_TRUE(deliver_.wait_deliveries(1));

  ObRequest *request = deliver_.held_request();
  ASSERT_NE(nullptr, request);
  void *sess = request->get_server_handle_context();
  const uint64_t generation = request->get_nio_request_generation();

  // Two independent loads of the same generation (the deliver-path tenant
  // lookup, then the worker's deserialize) both see the parsed login and
  // neither releases it.
  nio_login_view first = {};
  nio_login_view second = {};
  ASSERT_EQ(0, nio_get_login_view(sess, generation, &first));
  ASSERT_EQ(0, nio_get_login_view(sess, generation, &second));
  EXPECT_EQ(first.capabilities, second.capabilities);
  ASSERT_EQ(4, first.username.len);
  const ObMySQLRawPacket &raw =
      reinterpret_cast<const ObMySQLRawPacket &>(request->get_packet());
  EXPECT_EQ(0, std::memcmp(raw.get_cdata() + first.username.off, "root", 4));

  // Commit owns the release: after the request completes, the same
  // generation's view is gone without any nio_release_login call.
  ASSERT_EQ(OB_SUCCESS, deliver_.finish_held());
  nio_login_view after = {};
  EXPECT_NE(0, nio_get_login_view(sess, generation, &after));

  ::close(client_fd_);
  client_fd_ = -1;
  EXPECT_TRUE(connection_callback_.wait_disconnected(1));
  EXPECT_TRUE(connection_callback_.wait_destroyed(1));
}

TEST_F(TestSqlNioLifecycle, stale_generation_append_fails_after_commit)
{
  deliver_.hold_on(2);
  connect_and_read_greeting();
  login();

  const std::vector<uint8_t> command = {static_cast<uint8_t>(COM_PING)};
  ASSERT_TRUE(send_packet(client_fd_, 0, command));
  ASSERT_TRUE(deliver_.wait_deliveries(2));

  ObRequest *request = deliver_.held_request();
  ASSERT_NE(nullptr, request);
  void *sess = request->get_server_handle_context();
  const uint64_t generation = request->get_nio_request_generation();
  nio_connection_handle *handle = nio_connection_handle_acquire(sess);
  ASSERT_NE(nullptr, handle);

  // Commit first; a worker still holding the old generation must not be able
  // to append into the next request's response stream.
  ASSERT_EQ(OB_SUCCESS, deliver_.finish_held());
  int64_t framed_len = 0;
  EXPECT_NE(0, nio_response_append_string(handle, generation, "x", 1,
                                          &framed_len));
  EXPECT_EQ(0, framed_len);
  nio_connection_handle_release(handle);

  // The connection itself survives the rejected stale append.
  ASSERT_TRUE(send_packet(client_fd_, 0, command));
  ASSERT_TRUE(deliver_.wait_deliveries(3));

  ::close(client_fd_);
  client_fd_ = -1;
  EXPECT_TRUE(connection_callback_.wait_disconnected(1));
  EXPECT_TRUE(connection_callback_.wait_destroyed(1));
}

}  // anonymous namespace
}  // namespace obmysql
}  // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
