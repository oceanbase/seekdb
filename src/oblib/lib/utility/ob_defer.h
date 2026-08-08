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

#ifndef OCEANBASE_COMMON_OB_DEFER_H_
#define OCEANBASE_COMMON_OB_DEFER_H_

#include <functional>

namespace oceanbase
{
namespace common
{
// C++ implementation of golang defer keyword, the concept of delayed execution, often used for resource release
template <typename FnType>
class ScopedLambda {
 public:
  explicit ScopedLambda(FnType fn) : fn_(std::move(fn)), active_(true) { }
  // Default movable.
  ScopedLambda(ScopedLambda&&) = default;
  ScopedLambda& operator=(ScopedLambda&&) = default;
  // Non-copyable. In particular, there is no good reasoning about which copy
  // remains active.
  ScopedLambda(const ScopedLambda&) = delete;
  ScopedLambda& operator=(const ScopedLambda&) = delete;
  ~ScopedLambda() {
    if (active_) fn_();
  }
  void run_and_expire() {
    if (active_) fn_();
    active_ = false;
  }
  void activate() { active_ = true; }
  void deactivate() { active_ = false; }

 private:
  FnType fn_;
  bool active_ = true;
};

template <typename FnType>
ScopedLambda<FnType> MakeScopedLambda(FnType fn) {
  return ScopedLambda<FnType>(std::move(fn));
}

#define TOKEN_PASTE(x, y) x ## y
#define TOKEN_PASTE2(x, y) TOKEN_PASTE(x, y)
#define SCOPE_UNIQUE_NAME(name) TOKEN_PASTE2(name, __LINE__)
#define NAMED_DEFER_X(name, C , ...) \
    auto name = common::MakeScopedLambda([C]() mutable { __VA_ARGS__; })
#define NAMED_DEFER(name, ...) NAMED_DEFER_X(name, &, __VA_ARGS__)
#define DEFER(...) \
    NAMED_DEFER_X(SCOPE_UNIQUE_NAME(defer_varname), &, __VA_ARGS__)
#define DEFER_C(...) \
    NAMED_DEFER_X(SCOPE_UNIQUE_NAME(defer_varname), =, __VA_ARGS__)
} // end namespace common
} // end namespace oceanbase

#endif //OCEANBASE_COMMON_OB_DEFER_H_
