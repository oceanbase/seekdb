// Included in the Observer composition unit. Thin TCP entry for namespace
// worker mode: the shared process owns the public MySQL port, resolves
// "user@branch" to a namespace worker, and byte-proxies the connection to
// that worker's Unix socket. Authentication and every MySQL command run in
// the worker; this layer never parses beyond the login packet.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "namespace/namespace.h"
#include "rpc/obmysql/ob_sql_nio_server.h"
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <random>
#include <thread>
#include <unordered_set>
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
namespace proxy {
// Same composition as rust/sql-nio capability::SERVER_CAPABILITIES, minus
// CLIENT_SSL: v1 serves cleartext on this entry. TLS upgrade hooks into
// serve_connection() where the SSLRequest is rejected.
constexpr uint32_t CLIENT_CONNECT_WITH_DB = 0x00000008;
constexpr uint32_t CLIENT_PROTOCOL_41 = 0x00000200;
constexpr uint32_t CLIENT_SSL = 0x00000800;
constexpr uint32_t ENTRY_CAPABILITIES =
    (0x009CF7DFu | 0x20u | 0x10000u | 0x20000u) & ~CLIENT_SSL;
constexpr int64_t RESOLVE_TIMEOUT_US = 60 * 1000 * 1000;
constexpr size_t MAX_LOGIN_PACKET = 1024 * 1024;
std::atomic<bool> stopping{false};
int listen_fd = -1;
std::thread accept_thread;
std::mutex active_mutex;
std::unordered_set<int> active_fds;
// Client-visible connection ids are allocated here and handed to the worker
// through a PROXY v2 TLV, so CONNECTION_ID() matches the greeting the client
// saw. Start high to stay clear of the worker's own session-id pool.
std::atomic<uint32_t> next_conn_id{1u << 30};
void track_fd(int fd) {
  std::lock_guard<std::mutex> guard(active_mutex);
  active_fds.insert(fd);
}
void untrack_fd(int fd) {
  std::lock_guard<std::mutex> guard(active_mutex);
  active_fds.erase(fd);
}
bool read_full(int fd, uint8_t *buf, size_t n) {
  size_t done = 0;
  while (done < n) {
    const ssize_t got = ::read(fd, buf + done, n - done);
    if (got > 0) { done += static_cast<size_t>(got); }
    else if (got < 0 && errno == EINTR) { continue; }
    else { return false; }
  }
  return true;
}
bool write_full(int fd, const uint8_t *buf, size_t n) {
  size_t done = 0;
  while (done < n) {
    const ssize_t sent = ::write(fd, buf + done, n - done);
    if (sent > 0) { done += static_cast<size_t>(sent); }
    else if (sent < 0 && errno == EINTR) { continue; }
    else { return false; }
  }
  return true;
}
bool read_packet(int fd, uint8_t &seq, std::vector<uint8_t> &body) {
  uint8_t header[4];
  if (!read_full(fd, header, sizeof(header))) { return false; }
  const uint32_t len = uint32_t(header[0]) | (uint32_t(header[1]) << 8)
      | (uint32_t(header[2]) << 16);
  seq = header[3];
  if (len > MAX_LOGIN_PACKET) { return false; }
  body.resize(len);
  return len == 0 || read_full(fd, body.data(), len);
}
// MSG_PEEK variant of read_packet (ticket 06): waits for one full login
// packet without consuming it, so the in-process fast path can hand the
// socket to the local NIO with the login bytes still queued. Falls back to
// read_packet for worker endpoints, which then sees identical bytes.
bool peek_packet(int fd, uint8_t &seq, std::vector<uint8_t> &body) {
  std::vector<uint8_t> buffer(4);
  size_t want = 4;
  while (true) {
    const ssize_t got = ::recv(fd, buffer.data(), want, MSG_PEEK);
    if (got < 0) {
      if (errno == EINTR) { continue; }
      return false;
    }
    if (got == 0) { return false; }
    if (static_cast<size_t>(got) < want) {
      pollfd pfd = { fd, POLLIN, 0 };
      int ready;
      do { ready = ::poll(&pfd, 1, 1000); } while (ready < 0 && errno == EINTR);
      if (ready <= 0) { return false; }
      continue;
    }
    if (want == 4) {
      const uint32_t len = uint32_t(buffer[0]) | (uint32_t(buffer[1]) << 8)
          | (uint32_t(buffer[2]) << 16);
      seq = buffer[3];
      if (len > MAX_LOGIN_PACKET) { return false; }
      want = 4 + len;
      buffer.resize(want);
      continue;
    }
    body.assign(buffer.begin() + 4, buffer.end());
    return true;
  }
}
bool write_packet(int fd, uint8_t seq, const uint8_t *body, size_t len) {
  uint8_t header[4] = { static_cast<uint8_t>(len), static_cast<uint8_t>(len >> 8),
      static_cast<uint8_t>(len >> 16), seq };
  return write_full(fd, header, sizeof(header))
      && (len == 0 || write_full(fd, body, len));
}
void send_error(int fd, uint8_t seq, uint32_t code, const char *sqlstate,
                const std::string &message) {
  std::vector<uint8_t> body;
  body.push_back(0xff);
  body.push_back(static_cast<uint8_t>(code));
  body.push_back(static_cast<uint8_t>(code >> 8));
  body.push_back('#');
  body.insert(body.end(), sqlstate, sqlstate + 5);
  body.insert(body.end(), message.begin(), message.end());
  write_packet(fd, seq, body.data(), body.size());
}
// Wire layout identical to rust/sql-nio handshake::build_greeting.
bool send_greeting(int fd, uint32_t conn_id) {
  uint8_t scramble[20];
  std::random_device source;
  for (auto &byte : scramble) { byte = static_cast<uint8_t>(source()); }
  std::vector<uint8_t> body;
  body.reserve(96);
  body.push_back(10);
  const char version[] = "5.7.25";
  body.insert(body.end(), version, version + sizeof(version)); // includes NUL
  for (unsigned i = 0; i < 4; ++i) { body.push_back(static_cast<uint8_t>(conn_id >> (8 * i))); }
  body.insert(body.end(), scramble, scramble + 8);
  body.push_back(0);
  body.push_back(static_cast<uint8_t>(ENTRY_CAPABILITIES));
  body.push_back(static_cast<uint8_t>(ENTRY_CAPABILITIES >> 8));
  body.push_back(46); // utf8mb4 server charset
  body.push_back(2); body.push_back(0); // SERVER_STATUS_AUTOCOMMIT
  body.push_back(static_cast<uint8_t>(ENTRY_CAPABILITIES >> 16));
  body.push_back(static_cast<uint8_t>(ENTRY_CAPABILITIES >> 24));
  body.push_back(21); // auth plugin data length
  body.insert(body.end(), 10, 0);
  body.insert(body.end(), scramble + 8, scramble + 20);
  body.push_back(0);
  const char plugin[] = "mysql_native_password";
  body.insert(body.end(), plugin, plugin + sizeof(plugin)); // includes NUL
  return write_packet(fd, 0, body.data(), body.size());
}
struct ClientLogin {
  uint32_t caps = 0;
  size_t user_off = 0;
  size_t user_len = 0;
  size_t suffix_off = 0; // auth response, database, plugin and attrs follow
};
bool parse_client_login(const std::vector<uint8_t> &body, ClientLogin &login) {
  constexpr size_t FIXED = 32;
  if (body.size() < FIXED) { return false; }
  login.caps = uint32_t(body[0]) | (uint32_t(body[1]) << 8)
      | (uint32_t(body[2]) << 16) | (uint32_t(body[3]) << 24);
  if (!(login.caps & CLIENT_PROTOCOL_41)) { return false; }
  const auto nul = std::find(body.begin() + FIXED, body.end(), 0);
  if (nul == body.end()) { return false; }
  login.user_off = FIXED;
  login.user_len = static_cast<size_t>(nul - (body.begin() + FIXED));
  login.suffix_off = login.user_off + login.user_len + 1;
  return true;
}
struct ResolveOutcome {
  std::mutex mutex;
  std::condition_variable changed;
  bool done = false;
  int ret = OB_SUCCESS;
  uint64_t namespace_id = 0;
  std::string endpoint;
};
// Runs on an ObServerRuntime thread: inner SQL needs its worker context.
int resolve_branch(const std::string &branch, uint64_t &namespace_id,
                   std::string &endpoint) {
  namespace_id = 1;
  endpoint.clear();
  int ret = OB_SUCCESS;
  if (!branch.empty()) {
    static const char HEX[] = "0123456789abcdef";
    std::string hexed;
    hexed.reserve(branch.size() * 2);
    for (const unsigned char c : branch) {
      hexed.push_back(HEX[c >> 4]); hexed.push_back(HEX[c & 0xf]);
    }
    ObSqlString statement;
    ObMySQLProxy::MySQLResult result;
    sqlclient::ObMySQLResult *rows = nullptr;
    ret = statement.assign_fmt(
        "SELECT namespace_id FROM __fork_proto_meta.namespaces "
        "WHERE name=UNHEX('%s') AND state=0", hexed.c_str());
    if (!ret) { ret = GCTX.sql_proxy_->read(result, statement.ptr()); }
    if (!ret && !(rows = result.get_result())) { ret = OB_ERR_UNEXPECTED; }
    if (!ret) { ret = rows->next(); }
    if (!ret) { ret = rows->get_uint(0L, namespace_id); }
    // The bootstrap fork registers its target name as an alias of namespace 1.
    if (!ret && (namespace_id == 0 || namespace_id >= (1ULL << 30))) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  if (!ret) {
    if (namespace_id == 1 && ns1_in_process()) {
      // In-process ns1 (ticket 05a): the shared process's own NIO Unix
      // endpoint serves the connection; no worker is spawned for ns 1.
      endpoint = "run/sql.sock";
    } else if (namespace_id > 1 && forked_in_process()) {
      // In-process forked namespace (ticket 05c): register the resolved
      // name so the forwarded login binds the runtime on this process's own
      // NIO endpoint; per-namespace services activate lazily on first login.
      // The fork commit already registered the id; only a restart needs this.
      ns::NamespaceRuntime *existing = nullptr;
      if (ns::namespace_registry().get(namespace_id, existing)
          && existing != nullptr) {
        endpoint = "run/sql.sock";
      } else if (0 == ns::namespace_registry().add(namespace_id, branch.c_str())) {
        endpoint = "run/sql.sock";
      } else {
        ret = OB_ERR_UNEXPECTED;
      }
    } else {
      std::shared_ptr<Channel> channel;
      ret = ensure_channel(namespace_id, channel);
      if (!ret) { endpoint = channel->client_endpoint; }
    }
  }
  return ret;
}
int connect_worker(const std::string &endpoint) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) { return -1; }
  sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  if (endpoint.size() >= sizeof(addr.sun_path)) { ::close(fd); return -1; }
  // Endpoints are published relative to the instance base directory, which is
  // this process's working directory.
  std::strncpy(addr.sun_path, endpoint.c_str(), sizeof(addr.sun_path) - 1);
  if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr))) {
    ::close(fd);
    return -1;
  }
  return fd;
}
// PROXY protocol v2 header carrying the real client address plus a private
// TLV with the client-visible connection id. The worker consumes it on its
// Unix socket before the login packet.
constexpr uint8_t PROXY_TLV_CONN_ID = 0xE1;
bool send_proxy_header(int fd, const sockaddr_storage &peer, uint32_t conn_id) {
  uint8_t header[64] = { '\r', '\n', '\r', '\n', 0, '\r', '\n',
      'Q', 'U', 'I', 'T', '\n', 0x21, 0, 0, 0 };
  size_t len = 0;
  const uint32_t id_wire = htonl(conn_id);
  if (peer.ss_family == AF_INET) {
    const auto &in = *reinterpret_cast<const sockaddr_in *>(&peer);
    header[13] = 0x11;
    const uint16_t body_len = htons(12 + 7);
    std::memcpy(header + 14, &body_len, 2);
    std::memcpy(header + 16, &in.sin_addr, 4);
    const uint32_t local = htonl(INADDR_LOOPBACK);
    std::memcpy(header + 20, &local, 4);
    std::memcpy(header + 24, &in.sin_port, 2);
    const uint16_t port = htons(static_cast<uint16_t>(GCONF.mysql_port));
    std::memcpy(header + 26, &port, 2);
    header[28] = PROXY_TLV_CONN_ID;
    const uint16_t tlv_len = htons(4);
    std::memcpy(header + 29, &tlv_len, 2);
    std::memcpy(header + 31, &id_wire, 4);
    len = 35;
  } else if (peer.ss_family == AF_INET6) {
    const auto &in6 = *reinterpret_cast<const sockaddr_in6 *>(&peer);
    header[13] = 0x12;
    const uint16_t body_len = htons(36 + 7);
    std::memcpy(header + 14, &body_len, 2);
    std::memcpy(header + 16, &in6.sin6_addr, 16);
    static const uint8_t loopback6[16] = { [15] = 1 };
    std::memcpy(header + 32, loopback6, 16);
    std::memcpy(header + 48, &in6.sin6_port, 2);
    const uint16_t port = htons(static_cast<uint16_t>(GCONF.mysql_port));
    std::memcpy(header + 50, &port, 2);
    header[52] = PROXY_TLV_CONN_ID;
    const uint16_t tlv_len = htons(4);
    std::memcpy(header + 53, &tlv_len, 2);
    std::memcpy(header + 55, &id_wire, 4);
    len = 59;
  } else {
    return false;
  }
  return write_full(fd, header, len);
}
// Half-close aware byte pump: client EOF shuts down the worker write side and
// drains the worker's final packets; worker EOF ends the session.
void pump_bytes(int client_fd, int worker_fd) {
  bool client_open = true, worker_open = true;
  uint8_t buffer[65536];
  while (worker_open && !stopping.load(std::memory_order_relaxed)) {
    pollfd fds[2] = {
      { client_open ? client_fd : -1, POLLIN, 0 },
      { worker_fd, POLLIN, 0 },
    };
    const int ready = ::poll(fds, 2, 1000);
    if (ready < 0) { if (errno == EINTR) { continue; } break; }
    if (fds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
      const ssize_t got = ::read(client_fd, buffer, sizeof(buffer));
      if (got > 0 && write_full(worker_fd, buffer, static_cast<size_t>(got))) {
      } else {
        client_open = false;
        ::shutdown(worker_fd, SHUT_WR);
      }
    }
    if (fds[1].revents & (POLLIN | POLLERR | POLLHUP)) {
      const ssize_t got = ::read(worker_fd, buffer, sizeof(buffer));
      if (got > 0 && write_full(client_fd, buffer, static_cast<size_t>(got))) {
      } else {
        // The session owner is gone; nothing more can be said to the client.
        worker_open = false;
      }
    }
  }
}
void serve_connection(int client_fd, sockaddr_storage peer) {
  track_fd(client_fd);
  int worker_fd = -1;
  const uint32_t conn_id = ++next_conn_id;
  uint8_t seq = 0;
  std::vector<uint8_t> body;
  ClientLogin login;
  std::string user, branch;
  do {
    if (!send_greeting(client_fd, conn_id)) { break; }
    if (!peek_packet(client_fd, seq, body) || seq != 1
        || !parse_client_login(body, login)) { break; }
    if (login.caps & CLIENT_SSL) {
      // TLS upgrade hook: an SSLRequest arrives here once this entry
      // advertises CLIENT_SSL; v1 serves cleartext only.
      send_error(client_fd, 2, 1043, "08S01",
                 "TLS is not supported on this entry yet");
      break;
    }
    if (!ATOMIC_LOAD(&GCTX.sys_package_ready_)) {
      send_error(client_fd, 2, 1043, "08S01", "server is initializing");
      break;
    }
    user.assign(reinterpret_cast<const char *>(body.data()) + login.user_off,
                login.user_len);
    const size_t at = user.find('@');
    if (at != std::string::npos) {
      branch = user.substr(at + 1);
      user.resize(at);
      if (branch.empty()) {
        send_error(client_fd, 2, 1043, "08S01", "empty namespace name");
        break;
      }
    }
    auto outcome = std::make_shared<ResolveOutcome>();
    auto dispatch = std::make_shared<StorageDispatch>();
    dispatch->process = [outcome, branch](Frame &) {
      uint64_t namespace_id = 1;
      std::string endpoint;
      const int ret = resolve_branch(branch, namespace_id, endpoint);
      std::lock_guard<std::mutex> guard(outcome->mutex);
      outcome->ret = ret;
      outcome->namespace_id = namespace_id;
      outcome->endpoint = std::move(endpoint);
      outcome->done = true;
      outcome->changed.notify_all();
      return ret;
    };
    int ret = dispatch->submit(Frame());
    if (!ret) {
      std::unique_lock<std::mutex> lock(outcome->mutex);
      if (!outcome->changed.wait_for(lock,
              std::chrono::microseconds(RESOLVE_TIMEOUT_US),
              [&] { return outcome->done; })) {
        ret = OB_TIMEOUT;
      } else {
        ret = outcome->ret;
      }
    }
    if (ret == OB_ITER_END) {
      send_error(client_fd, 2, 1049, "42000",
                 "Unknown namespace '" + branch + "'");
      break;
    } else if (ret) {
      send_error(client_fd, 2, 1105, "HY000",
                 "namespace worker unavailable (err=" + std::to_string(ret) + ")");
      break;
    }
    if (outcome->endpoint == "run/sql.sock"
        && obmysql::global_sql_nio_server != nullptr) {
      // Ticket 06 fast path: the target namespace is served by this
      // process's own NIO. Hand the client socket over with the peeked
      // login still queued; NIO admits it without a second greeting. On
      // failure fall through to the byte pump (login consumed below).
      if (obmysql::global_sql_nio_server->inject_fd(client_fd) == OB_SUCCESS) {
        untrack_fd(client_fd);
        return;
      }
    }
    {
      // Worker endpoint (or injection failure): consume the peeked login.
      uint8_t consumed_seq = 0;
      std::vector<uint8_t> consumed;
      if (!read_packet(client_fd, consumed_seq, consumed)) { break; }
    }
    worker_fd = connect_worker(outcome->endpoint);
    if (worker_fd < 0
        || !send_proxy_header(worker_fd, peer, conn_id)) {
      send_error(client_fd, 2, 1105, "HY000", "namespace worker unavailable");
      break;
    }
    // The worker's own greeting and scramble are discarded; the client already
    // answered this entry's greeting. Empty-password accounts authenticate
    // directly; salted accounts take the worker's auth switch round through
    // the byte pump below.
    std::vector<uint8_t> discarded;
    uint8_t worker_seq = 0;
    if (!read_packet(worker_fd, worker_seq, discarded)) { break; }
    track_fd(worker_fd);
    // In-process forked namespaces resolve the branch name on this
    // process's own NIO entry, so the login keeps its @branch suffix.
    // Worker endpoints bind their home namespace instead; strip the suffix.
    const bool keep_branch = outcome->namespace_id > 1 && forked_in_process();
    if (keep_branch) {
      if (!write_packet(worker_fd, 1, body.data(), body.size())) { break; }
    } else {
      // Forward the login unchanged except for the routed branch suffix.
      std::vector<uint8_t> rewritten(body.begin(), body.begin() + login.user_off);
      rewritten.insert(rewritten.end(), user.begin(), user.end());
      rewritten.push_back(0);
      rewritten.insert(rewritten.end(), body.begin() + login.suffix_off, body.end());
      if (!write_packet(worker_fd, 1, rewritten.data(), rewritten.size())) { break; }
    }
    pump_bytes(client_fd, worker_fd);
  } while (false);
  if (worker_fd >= 0) {
    untrack_fd(worker_fd);
    ::close(worker_fd);
  }
  untrack_fd(client_fd);
  ::close(client_fd);
}
void accept_loop() {
  while (!stopping.load(std::memory_order_relaxed)) {
    sockaddr_storage peer = {};
    socklen_t len = sizeof(peer);
    const int fd = ::accept(listen_fd, reinterpret_cast<sockaddr *>(&peer), &len);
    if (fd < 0) {
      if (errno == EINTR) { continue; }
      break; // listener closed by stop()
    }
    std::thread(serve_connection, fd, peer).detach();
  }
}
int start() {
  if (worker_process) { return OB_SUCCESS; }
  int ret = OB_SUCCESS;
  // The shared process owns the system namespace. Register it so logins on
  // the local Unix endpoint bind a runtime (in-process ns1 dispatch and
  // direct socket logins); name routing happens on this proxy entry.
  if (0 != ns::namespace_registry().add(1, "")) {
    return OB_ERR_UNEXPECTED;
  }
  ns::NamespaceRuntime *home_runtime = nullptr;
  if (ns::namespace_registry().get(1, home_runtime) && home_runtime != nullptr) {
    home_runtime->set_service(ns::NamespaceRuntime::SCHEMA_SERVICE,
        &share::schema::ObMultiVersionSchemaService::get_instance());
  }
  const int64_t port = GCONF.mysql_port;
  const bool ipv6 = lib::use_ipv6();
  const int fd = ::socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
  if (fd < 0 || port <= 0 || port > 65535) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_storage addr = {};
    socklen_t len = 0;
    if (ipv6) {
      auto &in6 = *reinterpret_cast<sockaddr_in6 *>(&addr);
      in6.sin6_family = AF_INET6;
      in6.sin6_addr = in6addr_any;
      in6.sin6_port = htons(static_cast<uint16_t>(port));
      len = sizeof(in6);
    } else {
      auto &in = *reinterpret_cast<sockaddr_in *>(&addr);
      in.sin_family = AF_INET;
      in.sin_addr.s_addr = htonl(INADDR_ANY);
      in.sin_port = htons(static_cast<uint16_t>(port));
      len = sizeof(in);
    }
    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), len)
        || ::listen(fd, SOMAXCONN)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("namespace worker proxy failed to bind the MySQL port",
                KR(ret), K(port), K(errno));
    }
  }
  if (!ret) {
    listen_fd = fd;
    stopping.store(false);
    accept_thread = std::thread(accept_loop);
    fprintf(stderr, "PROTOTYPE_NAMESPACE_PROXY_LISTEN port=%lld\n",
            static_cast<long long>(port));
  } else if (fd >= 0) {
    ::close(fd);
  }
  return ret;
}
void stop() {
  if (listen_fd < 0) { return; }
  stopping.store(true);
  ::shutdown(listen_fd, SHUT_RDWR);
  ::close(listen_fd);
  listen_fd = -1;
  if (accept_thread.joinable()) { accept_thread.join(); }
  {
    std::lock_guard<std::mutex> guard(active_mutex);
    for (const int fd : active_fds) { ::shutdown(fd, SHUT_RDWR); }
  }
}
} // namespace proxy
} } }
