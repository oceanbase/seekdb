// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
// Exercises the real Rust reactor and MySQL codec over bounded memory pipes.
// The worker returns protocol test data; this is not an SQL execution test.
#include "nio_memory.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
static constexpr uint64_t kLargeId = UINT64_C(0xfedcba9876543210);
static constexpr size_t kLargeSize = 65536;
static constexpr uint32_t kCaps = NIO_CLIENT_PROTOCOL_41 | 0x8000U;

template <typename Predicate> static void await(Predicate predicate)
{
  const auto end = std::chrono::steady_clock::now() + 15s;
  while (!predicate()) {
    assert(std::chrono::steady_clock::now() < end);
    std::this_thread::sleep_for(1ms);
  }
}

struct Session {
  nio_connection_handle *handle;
  uint64_t previous = 0;
};
struct Job {
  Session *session;
  char *body;
  int64_t length;
  int kind;
  uint64_t generation;
  nio_mysql_command_view command;
};
struct Harness {
  std::mutex mutex;
  std::condition_variable changed;
  std::deque<Job> jobs;
  bool stopping = false;
  std::atomic<int> connects{0}, disconnects{0}, closes{0}, completed{0};
  std::atomic<int> large_started{0}, large_finished{0}, cancelled{0}, stale_rejected{0};
  std::atomic<void *> retained_session{nullptr};
  std::vector<std::thread> workers;

  Harness()
  {
    for (int i = 0; i < 2; ++i) workers.emplace_back([this] { work(); });
  }
  ~Harness()
  {
    {
      std::lock_guard<std::mutex> lock(mutex);
      assert(jobs.empty());
      stopping = true;
    }
    changed.notify_all();
    for (auto &worker : workers) worker.join();
  }
  static int connect(void *ctx, void *storage, int fd, int local, nio_greeting_info *greeting)
  {
    auto &self = *static_cast<Harness *>(ctx);
    assert(fd == -1 && local == 1);
    auto *session = new (storage) Session{nio_connection_handle_acquire(storage)};
    assert(session->handle);
    greeting->sessid = static_cast<uint32_t>(++self.connects);
    std::memset(greeting->scramble, 's', sizeof(greeting->scramble));
    const char version[] = "5.7.25-memory-test";
    std::memcpy(greeting->version, version, sizeof(version) - 1);
    greeting->version_len = sizeof(version) - 1;
    greeting->status_flags = 2;
    return 0;
  }
  static int readable(void *ctx, void *storage, char *body, int64_t length,
                      uint64_t wire, int kind, const nio_mysql_command_view *command,
                      uint64_t generation)
  {
    auto &self = *static_cast<Harness *>(ctx);
    assert(length > 0 && body[length] == 0 && wire == static_cast<uint64_t>(length + 4));
    assert((kind == NIO_PACKET_COMMAND) == (command != nullptr));
    Job job{static_cast<Session *>(storage), body, length, kind, generation, {}};
    if (command) job.command = *command;
    {
      std::lock_guard<std::mutex> lock(self.mutex);
      self.jobs.push_back(job);
    }
    self.changed.notify_one();
    return 0;
  }
  static void disconnect(void *ctx, void *) { ++static_cast<Harness *>(ctx)->disconnects; }
  static void close(void *ctx, void *storage, int)
  {
    auto *session = static_cast<Session *>(storage);
    nio_connection_handle_release(session->handle);
    session->~Session();
    ++static_cast<Harness *>(ctx)->closes;
  }
  void work()
  {
    for (;;) {
      Job job{};
      {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [this] { return stopping || !jobs.empty(); });
        if (stopping && jobs.empty()) return;
        job = jobs.front();
        jobs.pop_front();
      }
      assert(job.body[job.length] == 0); // Lease survives the delivery callback.
      auto *handle = job.session->handle;
      nio_mysql_ok_view ok{};
      ok.affected_rows = 1;
      ok.last_insert_id = kLargeId;
      ok.capability_flags = kCaps;
      ok.behavior_flags = NIO_MYSQL_OK_USE_STANDARD_SERIALIZE;
      ok.status_flags = 2;
      int64_t framed = 0;
      if (job.session->previous != 0) {
        assert(nio_response_append_ok(handle, job.session->previous, &ok, &framed) == -1);
        ++stale_rejected;
      }
      const bool login = job.kind == NIO_PACKET_LOGIN;
      bool large = false, cancel = false;
      if (login) {
        nio_login_view view{};
        assert(nio_get_login_view(job.session, job.generation, &view) == 0);
        assert(view.username.len == 4);
        assert(std::memcmp(job.body + view.username.off, "root", 4) == 0);
        nio_tls_session_info tls{};
        assert(nio_get_tls_session_info(job.session, job.generation, &tls) == 0);
        assert(tls.tls_active == 0);
      } else {
        assert(job.kind == NIO_PACKET_COMMAND);
        assert(job.command.command == static_cast<uint8_t>(job.body[0]));
        if (job.command.command == 3) {
          const std::string command(job.body + 1, static_cast<size_t>(job.length - 1));
          large = command == "large" || command == "cancel";
          cancel = command == "cancel";
          if (command == "retain") {
            nio_bind_sql_session(job.session);
            retained_session.store(job.session);
          }
          assert(large || command == "ack" || command == "retain");
        } else {
          assert(job.command.command == 14); // COM_PING
        }
      }
      if (large) {
        std::string value(kLargeSize, 'v');
        ++large_started;
        assert(nio_response_append_string(handle, job.generation,
                                            value.data(), value.size(), &framed) == 0);
        assert(framed > static_cast<int64_t>(value.size()));
        const int result = nio_response_flush(handle, job.generation, 0);
        if (cancel) { assert(result == -1); ++cancelled; }
        else { assert(result == 0 && framed > static_cast<int64_t>(value.size())); }
        ++large_finished;
      } else {
        assert(nio_response_append_ok(handle, job.generation, &ok, &framed) == 0);
      }
      if (!cancel) assert(nio_response_flush(handle, job.generation, 1) == 0);
      assert(nio_prepare_commit(handle, job.generation,
                                 login ? NIO_AUTH_OK : NIO_AUTH_NO_CHANGE) == 0);
      job.session->previous = job.generation;
      nio_commit_request(handle, job.generation);
      // Neither session nor leased body may be used after commit.
      ++completed;
    }
  }
};

static void write_all(nio_memory_client *client, const std::vector<char> &bytes)
{
  size_t pos = 0;
  await([&] {
    const int64_t n = nio_memory_write(client, bytes.data() + pos, bytes.size() - pos);
    assert(n > 0 || n == -2);
    if (n > 0) pos += static_cast<size_t>(n);
    return pos == bytes.size();
  });
}
static std::vector<char> read_exact(nio_memory_client *client, size_t length)
{
  std::vector<char> bytes(length);
  size_t pos = 0;
  await([&] {
    const int64_t n = nio_memory_read(client, bytes.data() + pos, bytes.size() - pos);
    assert(n > 0 || n == -2);
    if (n > 0) pos += static_cast<size_t>(n);
    return pos == bytes.size();
  });
  return bytes;
}
static std::vector<char> frame(const std::vector<char> &body, unsigned seq)
{
  const size_t n = body.size();
  assert(n < (1 << 24));
  std::vector<char> bytes{static_cast<char>(n), static_cast<char>(n >> 8),
                          static_cast<char>(n >> 16), static_cast<char>(seq)};
  bytes.insert(bytes.end(), body.begin(), body.end());
  return bytes;
}
static std::vector<char> packet(nio_memory_client *client, unsigned sequence)
{
  const auto header = read_exact(client, 4);
  assert(static_cast<uint8_t>(header[3]) == sequence);
  const size_t n = static_cast<uint8_t>(header[0])
                 | (static_cast<size_t>(static_cast<uint8_t>(header[1])) << 8)
                 | (static_cast<size_t>(static_cast<uint8_t>(header[2])) << 16);
  assert(n > 0 && n < kLargeSize + 128);
  return read_exact(client, n);
}
static void expect_ok(nio_memory_client *client, unsigned sequence)
{
  const auto body = packet(client, sequence);
  assert(body.size() >= 15 && body[0] == 0 && body[1] == 1);
  assert(static_cast<uint8_t>(body[2]) == 0xfe);
  uint64_t id = 0;
  for (int i = 0; i < 8; ++i) id |= static_cast<uint64_t>(static_cast<uint8_t>(body[3 + i])) << (8 * i);
  assert(id == kLargeId);
}
static void login(nio_memory_client *client)
{
  const auto greeting = packet(client, 0);
  assert(greeting[0] == 10);
  const size_t version_end = std::strlen(greeting.data() + 1) + 1;
  const size_t caps_pos = version_end + 1 + 4 + 8 + 1;
  const uint32_t caps = static_cast<uint8_t>(greeting[caps_pos])
                      | (static_cast<uint32_t>(static_cast<uint8_t>(greeting[caps_pos + 1])) << 8);
  assert((caps & NIO_CLIENT_SSL) == 0);
  std::vector<char> body(32, 0);
  for (int i = 0; i < 4; ++i) body[i] = static_cast<char>(kCaps >> (i * 8));
  body[7] = 1; // max_packet_size = 16 MiB
  body[8] = 45;
  body.insert(body.end(), {'r', 'o', 'o', 't', 0, 0});
  write_all(client, frame(body, 1));
  expect_ok(client, 2);
}
static void command(nio_memory_client *client, const char *text)
{
  std::vector<char> body{3};
  body.insert(body.end(), text, text + std::strlen(text));
  write_all(client, frame(body, 0));
}

int main()
{
  Harness harness;
  nio_callbacks callbacks{&harness, Harness::connect, Harness::readable,
                          Harness::disconnect, Harness::close};
  int32_t error = -1;
  auto start = [&](const nio_tls_config *tls, int disable) {
    return nio_start("127.0.0.1:0", NIO_ABI_VERSION, &callbacks, sizeof(callbacks),
                     sizeof(Session), 2, tls, tls ? sizeof(*tls) : 0, &error, disable);
  };
  nio_tls_config tls{};
  assert(start(&tls, 1) == nullptr && error == NIO_START_ETLS);
  assert(start(nullptr, 0) == nullptr && error == NIO_START_EINVAL);
  auto *reactor = start(nullptr, 1);
  assert(reactor && error == NIO_START_OK && nio_get_bound_tcp_port(reactor) == 0);
  assert(!nio_memory_connect(reactor, 0));
  assert(!nio_memory_connect(reactor, 16 * 1024 * 1024 + 1));
  auto *stalled = nio_memory_connect(reactor, 1);
  auto *tiny = nio_memory_connect(reactor, 7);
  auto *peer = nio_memory_connect(reactor, 127);
  assert(stalled && tiny && peer);
  assert(nio_memory_read(tiny, nullptr, 0) == 0);
  assert(nio_memory_write(tiny, nullptr, 1) == -1);
  assert(nio_memory_write(tiny, "x", -1) == -1);
  login(tiny); // A partial/stalled greeting on the same reactor does not block admission.
  login(peer);
  command(tiny, "ack");
  expect_ok(tiny, 1);
  auto pipeline = frame({14}, 0);
  const auto second = pipeline;
  pipeline.insert(pipeline.end(), second.begin(), second.end());
  write_all(tiny, pipeline);
  expect_ok(tiny, 1);
  expect_ok(tiny, 1);
  command(peer, "large");
  await([&] { return harness.large_started == 1; });
  std::this_thread::sleep_for(20ms);
  assert(harness.large_finished == 0); // Bounded output really applies backpressure.
  command(tiny, "ack");
  expect_ok(tiny, 1); // Another connection still makes progress during blocked worker IO.
  const auto large = packet(peer, 1);
  assert(large.size() == kLargeSize);
  for (char byte : large) assert(byte == 'v');
  await([&] { return harness.large_finished == 1; });
  command(peer, "cancel");
  await([&] { return harness.large_started == 2; });
  std::this_thread::sleep_for(20ms);
  assert(harness.large_finished == 1);
  nio_memory_close(peer);
  await([&] { return harness.cancelled == 1 && harness.completed == 8; });
  assert(harness.stale_rejected == 6);
  command(tiny, "retain");
  expect_ok(tiny, 1);
  await([&] { return harness.completed == 9; });
  void *retained = harness.retained_session.load();
  assert(retained);
  nio_shutdown(retained);
  await([&] { return harness.closes >= 2; });
  char eof;
  assert(nio_memory_read(tiny, &eof, 1) == 0); // Transport closes before storage is reclaimed.
  assert(nio_release_sql_session(retained) == 0);
  std::vector<nio_memory_client *> pending;
  for (int i = 0; i < 32; ++i) {
    auto *client = nio_memory_connect(reactor, 1);
    assert(client);
    pending.push_back(client);
  }
  nio_stop(reactor);
  assert(!nio_memory_connect(reactor, 7));
  nio_wait_destroy(reactor);
  assert(harness.connects == harness.disconnects && harness.connects == harness.closes);
  pending.push_back(stalled);
  pending.push_back(tiny);
  for (auto *client : pending) {
    char buffer[128];
    int64_t n;
    do { n = nio_memory_read(client, buffer, sizeof(buffer)); } while (n > 0);
    assert(n == 0);
    assert(nio_memory_write(client, "x", 1) == -1);
    nio_memory_close(client);
  }
  std::printf("memory NIO: partial handshake, login, pipeline, u64 response, backpressure, "
              "cancel, stale generation, stop/close passed (%d sessions)\n", harness.connects.load());
}
