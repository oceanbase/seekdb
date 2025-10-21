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

#ifndef OCEANBASE_SIGNAL_HANDLERS_H_
#define OCEANBASE_SIGNAL_HANDLERS_H_

#include <stdio.h>
#include <stdint.h>
#include <time.h>

namespace oceanbase
{
namespace common
{
class ObSigFaststack
{
public:
  ObSigFaststack(const ObSigFaststack &) = delete;
  ObSigFaststack& operator=(const ObSigFaststack &) = delete;
  static ObSigFaststack &get_instance();
  inline int64_t get_min_interval() const { return min_interval_; }
  inline void set_min_interval(int64_t interval) { min_interval_ = interval; } 
private:
  ObSigFaststack();
  ~ObSigFaststack();
private:
  int64_t min_interval_;
};
extern int minicoredump(int sig, int64_t tid, pid_t& pid);
extern int faststack();
} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SIGNAL_HANDLERS_H_
