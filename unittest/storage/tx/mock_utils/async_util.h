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

#ifndef OCEANBASE_TRANSACTION_TEST_OB_ASYNC_UTILS_
#define OCEANBASE_TRANSACTION_TEST_OB_ASYNC_UTILS_
#include "lib/thread/ob_simple_thread_pool.h"
namespace oceanbase {
namespace test {
class Async {
public:
  class AsyncRunner : public common::ObSimpleThreadPool {
  public:
    AsyncRunner() {
      ObSimpleThreadPool::init(2, 256, "AsyncRunner-for-Testing");
    }
    void handle(void *task) {
      Async *async = (Async*)task;
      async->eval();
    }
  };

  Async(std::function<int(void)> &f) {
    static AsyncRunner runner;
    started_ = false;
    evaluated_ = false;
    f_ = f;
    runner.push(this);
  }
  void wait_started() {
    while(!ATOMIC_LOAD(&started_)) {
      usleep(1000);
    }
  }
  void eval() {
    ATOMIC_STORE(&started_, true);
    ret_ = f_();
    evaluated_ = true;
  }
  bool is_evaled() const { return evaluated_; }
  int get() const {
    while(!evaluated_) usleep(100);
    return ret_;
  }
private:
  bool started_ = false;
  bool evaluated_ = false;
  std::function<int(void)> f_;
  int ret_;
};
template<typename F>
inline Async static make_async(F &f) {
  std::function<int(void)> ff = f;
  return Async(ff);
}
} // test
} // oceanbase
#endif
