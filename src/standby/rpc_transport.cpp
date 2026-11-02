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


#include "standby/rpc_transport.h"
#include <exception>
#include <atomic>
#include "lib/profile/ob_trace_id.h"
#include "lib/oblog/ob_log.h"
namespace oceanbase::rpc {
void report_call(const CallReport &report) {
  struct Counters { std::atomic<uint64_t> calls{0}, failed{0}; };
  static Counters counters[2][10];
  const unsigned method = static_cast<unsigned>(report.method);
  if (method >= 10) return;
  common::ObCurTraceId::TraceId trace;
  const std::string trace_text(report.trace_id);
  trace.set(trace_text.c_str());
  common::ObTraceIdGuard guard(trace);
  auto &counter = counters[report.server ? 1 : 0][method];
  const uint64_t calls = counter.calls.fetch_add(1, std::memory_order_relaxed) + 1;
  if (report.error != 0) {
    counter.failed.fetch_add(1, std::memory_order_relaxed);
    const int ret = report.error;
    LOG_WARN("RPC call failed", "method", method, "server", report.server,
        "destination", std::string(report.address).c_str(), "ret", report.error,
        "elapsed_us", report.elapsed_us);
  }
  if (calls % 1000 == 0) {
    LOG_INFO("[rpc report]", "method", method, "server", report.server,
        "send_cnt", calls, "failed_cnt", counter.failed.load(std::memory_order_relaxed));
  }
}
bool Reader::read(Message &message) {
  auto received = stream_->receive();
  error_ = received.error;
  if (received.present) message = Message(std::move(received.frame));
  return received.present;
}
int Client::init(const common::ObAddr &address, int64_t timeout, bool tls) {
  char text[common::MAX_IP_PORT_LENGTH]{};
  const int error = address.ip_port_to_string(text, sizeof(text));
  if (error != 0) return error;
  try { handle_.emplace(create_client(text, tls)); }
  catch (const std::exception &error) {
    const int ret = common::OB_RPC_SEND_ERROR;
    LOG_WARN("failed to initialize RPC client", "error", error.what());
    return common::OB_RPC_SEND_ERROR;
  }
  destination_ = address;
  timeout_us_ = timeout > 0 ? timeout : 9000000;
  return 0;
}
std::unique_ptr<Reader> Client::stream(Method method, Message &request) {
  try {
    if (!handle_) return nullptr;
    common::ObCurTraceId::TraceId trace = *common::ObCurTraceId::get_trace_id();
    if (trace.get()[0] == 0) trace.init(destination_);
    char text[common::OB_MAX_TRACE_ID_BUFFER_SIZE]{};
    trace.to_string(text, sizeof(text));
    return std::make_unique<Reader>(
        (*handle_)->open_stream(method, request.take_frame(), text, timeout_us_));
  } catch (const std::exception &) { return nullptr; }
}
int Client::call(Method method, Message &request, Message &response) {
  auto reader = stream(method, request);
  if (!reader) return common::OB_RPC_SEND_ERROR;
  if (!reader->read(response)) {
    const int error = reader->finish();
    return error == 0 ? common::OB_ERR_UNEXPECTED : error;
  }
  Message extra;
  if (reader->read(extra)) return common::OB_ERR_UNEXPECTED;
  return reader->finish();
}
int RpcServer::start(int port, bool tls) {
  if (handle_) return 0;
  try { handle_.emplace(start_server(port, tls, service_)); return 0; }
  catch (const std::exception &error) {
    const int ret = common::OB_RPC_SEND_ERROR;
    LOG_WARN("failed to start RPC server", "error", error.what());
    return common::OB_RPC_SEND_ERROR;
  }
}
}
