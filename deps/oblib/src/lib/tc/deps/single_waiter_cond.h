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

class SingleWaiterCond
{
public:
  SingleWaiterCond(): stock_(0) {}
  ~SingleWaiterCond() {}
  void wait(int64_t timeout_us) {
    if (0 == ATOMIC_LOAD(&stock_)) {
      tc_futex_wait(&stock_, 0, timeout_us);
    } else {
      ATOMIC_STORE(&stock_, 0);
    }
  }
  void signal() {
    if (0 == ATOMIC_LOAD(&stock_) && ATOMIC_BCAS(&stock_, 0, 1)) {
      tc_futex_wake(&stock_, 1);
    }
  }
private:
  int stock_;
};
