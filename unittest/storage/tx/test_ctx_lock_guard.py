#!/usr/bin/env python3
# Copyright (c) 2026 OceanBase.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Exercise the production guard reset with a deterministic commit-retry race.

The small latch/callback fixture avoids the server's asynchronous scheduling.
Only CtxLockGuard::reset is extracted from production; this is not a server test.
"""
import argparse
import pathlib
import subprocess
import tempfile


def reset_definition(source):
    start = source.index("void CtxLockGuard::reset()")
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


FIXTURE = r'''
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <future>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
constexpr int64_t operator"" _ms(unsigned long long n) { return n * 1000; }
struct ObTimeUtility {
  static int64_t fast_current_time() { return 0; }
};
#define TRANS_LOG_RET(...) ((void)0)

struct CtxLock {
  std::timed_mutex access, redo, ctx, descriptor;
  std::promise<void> callback_entered;
  bool callback_completed = false;
  bool retry_completed = false;
  int callbacks = 0;
  void unlock_access() { access.unlock(); }
  void unlock_flush_redo() { redo.unlock(); }
  void unlock_ctx() {
    ctx.unlock();
    ++callbacks;
    callback_entered.set_value();
    callback_completed = descriptor.try_lock_for(500ms);
    if (callback_completed) descriptor.unlock();
  }
};

class CtxLockGuard {
public:
  enum MODE { CTX = 1, ACCESS = 2, REDO_FLUSH_X = 4, REDO_FLUSH_R = 8 };
  CtxLockGuard(CtxLock &lock, uint8_t mode) : lock_(&lock), mode_(mode) {
    if (mode & ACCESS) lock.access.lock();
    if (mode & (REDO_FLUSH_X | REDO_FLUSH_R)) lock.redo.lock();
    if (mode & CTX) lock.ctx.lock();
  }
  void reset();
private:
  CtxLock *lock_;
  uint8_t mode_;
  int64_t request_ts_ = 0, hold_ts_ = 0;
};

PRODUCTION_RESET

int main() {
  bool passed = true;
  for (const uint8_t mode : {1, 3, 5, 9, 7, 11}) {
    CtxLock lock;
    CtxLockGuard guard(lock, mode);
    std::promise<void> descriptor_held;
    auto ready = descriptor_held.get_future();
    auto callback = lock.callback_entered.get_future();
    std::thread retry([&] {
      lock.descriptor.lock();
      descriptor_held.set_value();
      callback.wait();
      // Commit retry takes descriptor -> access -> redo -> ctx.
      if (lock.access.try_lock_for(300ms)) {
        if (lock.redo.try_lock_for(300ms)) {
          if (lock.ctx.try_lock_for(300ms)) {
            lock.retry_completed = true;
            lock.ctx.unlock();
          }
          lock.redo.unlock();
        }
        lock.access.unlock();
      }
      lock.descriptor.unlock();
    });
    ready.wait();
    guard.reset();
    retry.join();
    guard.reset(); // Reset must remain idempotent.
    const bool ok = lock.callback_completed && lock.retry_completed && lock.callbacks == 1;
    std::printf("mode=%u callback=%d retry=%d callbacks=%d %s\n", mode,
                lock.callback_completed, lock.retry_completed, lock.callbacks,
                ok ? "PASS" : "FAIL");
    passed &= ok;
  }
  return passed ? 0 : 1;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[3] /
                        "src/storage/tx/ob_trans_ctx_lock.cpp")
    parser.add_argument("--cxx", default="c++")
    args = parser.parse_args()
    source = FIXTURE.replace("PRODUCTION_RESET", reset_definition(args.source.read_text()))
    with tempfile.TemporaryDirectory(prefix="ctx-lock-guard-") as directory:
        root = pathlib.Path(directory)
        cpp = root / "test.cpp"
        exe = root / "test"
        cpp.write_text(source)
        subprocess.run([args.cxx, "-std=c++14", "-O2", "-pthread", str(cpp), "-o", str(exe)],
                       check=True, timeout=60)
        return subprocess.run([str(exe)], timeout=10).returncode


if __name__ == "__main__":
    raise SystemExit(main())
