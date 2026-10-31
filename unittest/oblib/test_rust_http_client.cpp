/*
 * Copyright (c) 2026 OceanBase.
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
#include "rust/http_client.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <thread>

namespace
{

class LocalServer
{
public:
  LocalServer()
  {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GE(fd_, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    EXPECT_EQ(0, bind(fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)));
    EXPECT_EQ(0, listen(fd_, 1));
    socklen_t length = sizeof(address);
    EXPECT_EQ(0, getsockname(fd_, reinterpret_cast<sockaddr *>(&address), &length));
    url_ = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/resource";
  }

  ~LocalServer()
  {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  const std::string &url() const { return url_; }

  void respond_once(const std::string &response)
  {
    worker_ = std::thread([this, response]() {
      const int connection = accept(fd_, nullptr, nullptr);
      EXPECT_GE(connection, 0);
      std::string request;
      char buffer[4096];
      while (request.find("\r\n\r\n") == std::string::npos) {
        const ssize_t count = recv(connection, buffer, sizeof(buffer), 0);
        EXPECT_GT(count, 0);
        if (count <= 0) {
          close(connection);
          return;
        }
        request.append(buffer, count);
      }
      const std::string body = "{\"input\":1}";
      while (request.find(body) == std::string::npos) {
        const ssize_t count = recv(connection, buffer, sizeof(buffer), 0);
        EXPECT_GT(count, 0);
        if (count <= 0) {
          close(connection);
          return;
        }
        request.append(buffer, count);
      }
      EXPECT_NE(std::string::npos, request.find("POST /resource HTTP/1.1"));
      EXPECT_NE(std::string::npos, request.find("X-Test: cxx-bridge"));
      EXPECT_EQ(static_cast<ssize_t>(response.size()),
                send(connection, response.data(), response.size(), 0));
      close(connection);
    });
  }

  void join() { worker_.join(); }

private:
  int fd_ = -1;
  std::string url_;
  std::thread worker_;
};

TEST(RustHttpClient, CxxSynchronousPostPreservesResponseAndRequest)
{
  LocalServer server;
  server.respond_once(
      "HTTP/1.1 201 Created\r\nContent-Length: 7\r\nConnection: close\r\n\r\ncreated");
  oceanbase::http::ClientConfig config;
  config.timeout_ms = 2000;
  config.connect_timeout_ms = 1000;
  config.https_only = false;
  config.follow_redirects = false;
  config.max_redirects = 0;
  config.verify_peer = true;
  config.verify_host = true;
  config.discard_body = false;
  config.accept_gzip = false;
  auto client = oceanbase::http::new_http_client(rust::Str(server.url()), config);
  ASSERT_EQ(oceanbase::http::Code::Ok, oceanbase::http::http_client_add_header(
      *client, rust::Str("Content-Type: application/json")));
  ASSERT_EQ(oceanbase::http::Code::Ok, oceanbase::http::http_client_add_header(
      *client, rust::Str("X-Test: cxx-bridge")));
  const std::string request = "{\"input\":1}";
  auto reply = oceanbase::http::http_post(*client, rust::Slice<const uint8_t>(
      reinterpret_cast<const uint8_t *>(request.data()), request.size()));
  const auto result = oceanbase::http::http_reply_result(*reply);
  ASSERT_EQ(oceanbase::http::Code::Ok, result.code);
  EXPECT_EQ(201U, result.http_status);
  EXPECT_EQ(7U, result.body_size);
  const auto body = oceanbase::http::http_reply_body(*reply);
  EXPECT_EQ("created", std::string(reinterpret_cast<const char *>(body.data()), body.size()));
  server.join();
}

TEST(RustHttpClient, CxxGzipResponseUsesRustZlib)
{
  static const unsigned char gzip[] = {
      0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff,
      0x4b, 0xce, 0xcf, 0x2d, 0x28, 0x4a, 0x2d, 0x2e, 0x4e, 0x4d,
      0x51, 0x00, 0x52, 0x05, 0xf9, 0x79, 0xc5, 0xa9, 0x00, 0xb1,
      0xff, 0x32, 0x6f, 0x13, 0x00, 0x00, 0x00,
  };
  LocalServer server;
  std::string response =
      "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: " +
      std::to_string(sizeof(gzip)) + "\r\nConnection: close\r\n\r\n";
  response.append(reinterpret_cast<const char *>(gzip), sizeof(gzip));
  server.respond_once(response);
  oceanbase::http::ClientConfig config;
  config.timeout_ms = 2000;
  config.connect_timeout_ms = 1000;
  config.https_only = false;
  config.follow_redirects = false;
  config.max_redirects = 0;
  config.verify_peer = true;
  config.verify_host = true;
  config.discard_body = false;
  config.accept_gzip = true;
  auto client = oceanbase::http::new_http_client(rust::Str(server.url()), config);
  ASSERT_EQ(oceanbase::http::Code::Ok, oceanbase::http::http_client_add_header(
      *client, rust::Str("X-Test: cxx-bridge")));
  const std::string request = "{\"input\":1}";
  auto reply = oceanbase::http::http_post(*client, rust::Slice<const uint8_t>(
      reinterpret_cast<const uint8_t *>(request.data()), request.size()));
  const auto result = oceanbase::http::http_reply_result(*reply);
  ASSERT_EQ(oceanbase::http::Code::Ok, result.code);
  const auto body = oceanbase::http::http_reply_body(*reply);
  EXPECT_EQ("compressed response",
            std::string(reinterpret_cast<const char *>(body.data()), body.size()));
  server.join();
}

} // namespace

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
