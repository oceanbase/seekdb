#!/usr/bin/env python3
"""Fault-test the production Linux ObUUIDNode::init body without a running server.

Run on Linux: python3 unittest/sql/engine/expr/test_uuid_node_diagnostics.py
Requires a C++11 compiler (CXX, or c++). Only system calls and logging are stubbed.
"""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / "src/sql/engine/expr/ob_expr_uuid.cpp"
HARNESS = r"""
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#define OB_SUCCESS 0
#define OB_ERR_UNEXPECTED -4016
#define OB_ISNULL(x) ((x) == nullptr)
#define OB_SUCC(x) ((x) == 0)
#define OB_FAIL(x) ((x) != 0)
#define MEMCPY std::memcpy
struct Field { std::string key, value; };
template<class T> Field field(const char *key, const T &value) {
  std::ostringstream out; out << value; return {key, out.str()};
}
#define K(x) field(#x, x)
struct Event { std::string message; std::vector<Field> fields; };
static std::vector<Event> events;
template<class... T> void log_event(const char *message, T... fields) {
  events.push_back({message, {fields...}});
  errno = ENOENT; // A logger must not corrupt an already captured syscall error.
}
#define LOG_ERROR log_event
#define LOG_WARN log_event
static int scenario, closes;
static int fake_socket(int, int, int) {
  if (scenario == 0) { errno = EPERM; return -1; }
  return 42;
}
static int fake_close(int fd) {
  if (fd != 42) std::abort();
  ++closes; errno = EIO; return 0;
}
static int fake_ioctl(int, unsigned long request, void *arg) {
  if (request == SIOCGIFCONF) {
    if (scenario == 1) { errno = EPERM; return -1; }
    auto *conf = static_cast<ifconf *>(arg);
    conf->ifc_len = (scenario == 2 ? 0 : scenario == 7 ? 2 : 1) * sizeof(ifreq);
    std::memset(conf->ifc_buf, 0, conf->ifc_len);
    if (conf->ifc_len) std::strcpy(conf->ifc_req[0].ifr_name, "eth0");
    if (scenario == 7) std::strcpy(conf->ifc_req[1].ifr_name, "eth1");
    return 0;
  }
  auto *req = static_cast<ifreq *>(arg);
  if (request == SIOCGIFFLAGS) {
    if (scenario == 4) { errno = EPERM; return -1; }
    req->ifr_flags = (scenario == 3 ? IFF_LOOPBACK : 0);
    return 0;
  }
  if (request == SIOCGIFHWADDR) {
    if (scenario == 5 || (scenario == 7 && std::strcmp(req->ifr_name, "eth0") == 0)) {
      errno = EACCES; return -1;
    }
    std::memcpy(req->ifr_hwaddr.sa_data, "ABCDEF", 6);
    return 0;
  }
  std::abort();
}
#define socket fake_socket
#define ioctl fake_ioctl
#define close fake_close
class ObUUIDNode {
public:
  bool is_inited_ = false;
  unsigned char mac_addr_[6] = {};
  int init();
};
// PRODUCTION_INIT
static bool has_event(const std::string &message, const std::string &key = "",
                      const std::string &value = "") {
  for (const auto &event : events) {
    if (event.message.find(message) != std::string::npos) {
      if (key.empty()) return true;
      for (const auto &f : event.fields) if (f.key == key && f.value == value) return true;
    }
  }
  return false;
}
int main(int argc, char **argv) {
  scenario = std::stoi(argv[1]);
  ObUUIDNode node;
  const int ret = node.init();
  const bool success = scenario >= 6;
  if (ret != (success ? OB_SUCCESS : OB_ERR_UNEXPECTED) || node.is_inited_ != success) return 1;
  if (closes != (scenario == 0 ? 0 : 1)) return 2;
  if (success && std::memcmp(node.mac_addr_, "ABCDEF", 6) != 0) return 3;
  const char *operation[] = {"socket failed", "SIOCGIFCONF failed", "no usable MAC",
    "no usable MAC", "SIOCGIFFLAGS failed", "SIOCGIFHWADDR failed", "", "SIOCGIFHWADDR failed"};
  if (scenario != 6 && !has_event(operation[scenario])) return 4;
  if (scenario == 0 || scenario == 1 || scenario == 4 || scenario == 5 || scenario == 7) {
    if (!has_event(operation[scenario], "sys_errno", std::to_string(scenario >= 5 ? EACCES : EPERM))) return 5;
  }
  if (scenario == 4 || scenario == 5 || scenario == 7) {
    if (!has_event(operation[scenario], "it->ifr_name", "eth0")) return 6;
  }
  if (scenario == 2 || scenario == 3) {
    for (const auto &event : events) for (const auto &f : event.fields)
      if (f.key == "errno" || f.key == "sys_errno") return 7;
  }
  if (scenario == 6 && !events.empty()) return 8;
  return 0;
}
"""


class UUIDNodeDiagnostics(unittest.TestCase):
    def test_linux_faults_and_success(self):
        source = SOURCE.read_text(encoding="utf-8")
        start = source.index("int ObUUIDNode::init()")
        end = source.index("int ObUUIDTime::get_time", start)
        body = source[start:end]
        with tempfile.TemporaryDirectory(prefix="uuid-node-test-") as tmp:
            cpp, exe = Path(tmp) / "test.cpp", Path(tmp) / "test"
            cpp.write_text(
                HARNESS.replace("// PRODUCTION_INIT", body), encoding="utf-8"
            )
            subprocess.run(
                [os.environ.get("CXX", "c++"), "-std=c++11", str(cpp), "-o", str(exe)],
                check=True,
            )
            for case in range(8):
                with self.subTest(case=case):
                    subprocess.run([str(exe), str(case)], check=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
