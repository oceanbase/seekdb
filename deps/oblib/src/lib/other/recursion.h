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

#ifndef OCEANBASE_COMMON_RECURSION_H_
#define OCEANBASE_COMMON_RECURSION_H_

namespace oceanbase
{
namespace common
{

#ifdef DETECT_RECURSION
class RecursionCheckerGuard
{
public:
  RecursionCheckerGuard();
  ~RecursionCheckerGuard();
public:
  constexpr static int max_cap = 256;
  static thread_local void** tl_func;
  static thread_local int    tl_n_func;
private:
  void**           func_bak_;
  int              n_func_bak_;
  void*            func_[max_cap];
};
#define RECURSION_CHECKER_GUARD RecursionCheckerGuard guard
#else
#define RECURSION_CHECKER_GUARD
#endif

} // end namespace common
} // end namespace oceanbase


#endif //OCEANBASE_COMMON_RECURSION_H_
