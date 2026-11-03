// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#pragma once
#include "rust/standby_rpc.h"
#include "lib/net/ob_addr.h"
#include "lib/ob_errno.h"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <cstring>
#include <utility>
namespace oceanbase::rpc {
class Message {
public:
  Message() = default;
  explicit Message(Frame frame) : frame_(std::move(frame)) {}
  std::string_view buf() const {
    return {reinterpret_cast<const char *>(frame_.buf.data()), frame_.buf.size()};
  }
  char *mutable_buf(size_t size) {
    resize_buffer(frame_, size);
    return reinterpret_cast<char *>(frame_.buf.data());
  }
  void set_buf(const char *data, size_t size) {
    char *dest = mutable_buf(size);
    if (size != 0) std::memcpy(dest, data, size);
  }
  void set_buf(const std::string &data) { set_buf(data.data(), data.size()); }
  uint64_t size() const { return frame_.size; }
  void set_size(uint64_t value) { frame_.size = value; }
  int32_t entry_type() const { return frame_.entry_type; }
  void set_entry_type(int32_t value) { frame_.entry_type = value; }
  uint64_t start_lsn() const { return frame_.start_lsn; }
  void set_start_lsn(uint64_t value) { frame_.start_lsn = value; }
  uint64_t max_bytes() const { return frame_.max_bytes; }
  void set_max_bytes(uint64_t value) { frame_.max_bytes = value; }
  uint64_t source_lsn() const { return frame_.source_lsn; }
  void set_source_lsn(uint64_t value) { frame_.source_lsn = value; }
  uint64_t end_scn() const { return frame_.end_scn; }
  void set_end_scn(uint64_t value) { frame_.end_scn = value; }
  Control &control() { return frame_.control; }
  const Control &control() const { return frame_.control; }
  Frame take_frame() { return std::exchange(frame_, Frame{}); }
private:
  Frame frame_{};
};
class Reader {
public:
  explicit Reader(rust::Box<ClientStream> stream) : stream_(std::move(stream)) {}
  bool read(Message &message);
  void cancel() { stream_->cancel(); }
  int finish() { cancel(); return error_; }
private:
  rust::Box<ClientStream> stream_;
  int error_ = 0;
};
class Client {
public:
  int init(const common::ObAddr &address, int64_t timeout, bool tls);
  std::unique_ptr<Reader> stream(Method method, Message &request);
  int call(Method method, Message &request, Message &response);
private:
  std::optional<rust::Box<ClientHandle>> handle_;
  common::ObAddr destination_;
  uint64_t timeout_us_ = 9000000;
};
class RpcServer {
public:
  int register_service(std::shared_ptr<Service> service) { service_ = std::move(service); return 0; }
  int start(int port, bool tls);
  void stop() { if (handle_) { (*handle_)->stop(); handle_.reset(); } }
  int64_t cert_expire_time() const { return handle_ ? (*handle_)->cert_expire_time() : 0; }
  ~RpcServer() { stop(); }
private:
  std::shared_ptr<Service> service_;
  std::optional<rust::Box<ServerHandle>> handle_;
};
}
